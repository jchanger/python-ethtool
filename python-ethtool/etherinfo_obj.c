/*
 * Copyright (C) 2009-2013 Red Hat Inc.
 *
 * David Sommerseth <davids@redhat.com>
 *
 * This application is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2.
 *
 * This application is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */


/**
 * @file   etherinfo_obj.c
 * @author David Sommerseth <davids@redhat.com>
 * @date   Fri Sep  4 18:41:28 2009
 *
 * @brief  Python ethtool.etherinfo class functions.
 *
 */

#include <Python.h>
#include "structmember.h"

#include <netlink/route/rtnl.h>
#include <netlink/route/addr.h>
#include "etherinfo_struct.h"
#include "etherinfo.h"

/**
 * ethtool.etherinfo deallocator - cleans up when a object is deleted
 *
 * @param self PyEtherInfo Python object to deallocate
 */
static void _ethtool_etherinfo_dealloc(PyEtherInfo *self)
{
	close_netlink(self);
        Py_XDECREF(self->device);    self->device = NULL;
        Py_XDECREF(self->hwaddress); self->hwaddress = NULL;
	self->ob_type->tp_free((PyObject*)self);
}


/*
  The old approach of having a single IPv4 address per device meant each result
  that came in from netlink overwrote the old result.

  Mimic it by returning the last entry in the list (if any).

  The return value is a *borrowed reference* (or NULL)
*/
static PyNetlinkIPaddress * get_last_ipv4_address(PyObject *addrlist)
{
	Py_ssize_t size;

	if (!addrlist) {
		return NULL;
	}

	if (!PyList_Check(addrlist)) {
		return NULL;
	}

	size = PyList_Size(addrlist);
	if (size > 0) {
		PyNetlinkIPaddress *item = (PyNetlinkIPaddress *)PyList_GetItem(addrlist, size - 1);
		if (Py_TYPE(item) == &ethtool_netlink_ip_address_Type) {
			return item;
		}
	}

	return NULL;
}

/**
 * ethtool.etherinfo function for retrieving data from a Python object.
 *
 * @param self    Pointer to the current PyEtherInfo device object
 * @param attr_o  contains the object member request (which element to return)
 *
 * @return Returns a PyObject with the value requested on success, otherwise NULL
 */
PyObject *_ethtool_etherinfo_getter(PyEtherInfo *self, PyObject *attr_o)
{
	char *attr = PyString_AsString(attr_o);
	PyNetlinkIPaddress *py_addr;
        PyObject *addrlist = NULL;

	if( !self ) {
		PyErr_SetString(PyExc_AttributeError, "No data available");
		return NULL;
	}

	if( strcmp(attr, "device") == 0 ) {
                if( self->device ) {
                        Py_INCREF(self->device);
                        return self->device;
                } else {
                        return Py_INCREF(Py_None), Py_None;
                }
	} else if( strcmp(attr, "mac_address") == 0 ) {
		get_etherinfo_link(self);
		if( self->hwaddress ) {
			Py_INCREF(self->hwaddress);
		}
		return self->hwaddress;
	} else if( strcmp(attr, "ipv4_address") == 0 ) {
		addrlist = get_etherinfo_address(self, NLQRY_ADDR4);
		/* For compatiblity with old approach, return last IPv4 address: */
		py_addr = get_last_ipv4_address(addrlist);
		if (py_addr) {
		  if (py_addr->local) {
		      Py_INCREF(py_addr->local);
		      return py_addr->local;
		  }
		}
		Py_RETURN_NONE;
	} else if( strcmp(attr, "ipv4_netmask") == 0 ) {
		addrlist = get_etherinfo_address(self, NLQRY_ADDR4);
		py_addr = get_last_ipv4_address(addrlist);
		if (py_addr) {
		  return PyInt_FromLong(py_addr->prefixlen);
		}
		return PyInt_FromLong(0);
	} else if( strcmp(attr, "ipv4_broadcast") == 0 ) {
		addrlist = get_etherinfo_address(self, NLQRY_ADDR4);
		py_addr = get_last_ipv4_address(addrlist);
		if (py_addr) {
		  if (py_addr->ipv4_broadcast) {
		      Py_INCREF(py_addr->ipv4_broadcast);
		      return py_addr->ipv4_broadcast;
		  }
		}
		Py_RETURN_NONE;
	} else {
		return PyObject_GenericGetAttr((PyObject *)self, attr_o);
	}
}

/**
 * ethtool.etherinfo function for setting a value to a object member.  This feature is
 * disabled by always returning -1, as the values are read-only by the user.
 *
 * @param self
 * @param attr_o
 * @param val_o
 *
 * @return Returns always -1 (failure).
 */
int _ethtool_etherinfo_setter(PyEtherInfo *self, PyObject *attr_o, PyObject *val_o)
{
	PyErr_SetString(PyExc_AttributeError, "etherinfo member values are read-only.");
	return -1;
}


/**
 * Creates a human readable format of the information when object is being treated as a string
 *
 * @param self  Pointer to the current PyEtherInfo device object
 *
 * @return Returns a PyObject with a string with all of the information
 */
PyObject *_ethtool_etherinfo_str(PyEtherInfo *self)
{
	PyObject *ret = NULL;
        PyObject *ipv4addrs = NULL, *ipv6addrs = NULL;

	if( !self ) {
		PyErr_SetString(PyExc_AttributeError, "No data available");
		return NULL;
	}

	get_etherinfo_link(self);

	ret = PyString_FromFormat("Device ");
	PyString_Concat(&ret, self->device);
	PyString_ConcatAndDel(&ret, PyString_FromString(":\n"));

	if( self->hwaddress ) {
		PyString_ConcatAndDel(&ret, PyString_FromString("\tMAC address: "));
		PyString_Concat(&ret, self->hwaddress);
		PyString_ConcatAndDel(&ret, PyString_FromString("\n"));
	}

	ipv4addrs = get_etherinfo_address(self, NLQRY_ADDR4);
	if( ipv4addrs ) {
               Py_ssize_t i;
               for (i = 0; i < PyList_Size(ipv4addrs); i++) {
                       PyNetlinkIPaddress *py_addr = (PyNetlinkIPaddress *)PyList_GetItem(ipv4addrs, i);
                       PyObject *tmp = PyString_FromFormat("\tIPv4 address: ");
                       PyString_Concat(&tmp, py_addr->local);
                       PyString_ConcatAndDel(&tmp, PyString_FromFormat("/%d", py_addr->prefixlen));
                       if (py_addr->ipv4_broadcast ) {
                                PyString_ConcatAndDel(&tmp,
                                                      PyString_FromString("	  Broadcast: "));
                                PyString_Concat(&tmp, py_addr->ipv4_broadcast);
                       }
                       PyString_ConcatAndDel(&tmp, PyString_FromString("\n"));
                       PyString_ConcatAndDel(&ret, tmp);
               }
	}

	ipv6addrs = get_etherinfo_address(self, NLQRY_ADDR6);
	if( ipv6addrs ) {
	       Py_ssize_t i;
	       for (i = 0; i < PyList_Size(ipv6addrs); i++) {
		       PyNetlinkIPaddress *py_addr = (PyNetlinkIPaddress *)PyList_GetItem(ipv6addrs, i);
		       PyObject *tmp = PyString_FromFormat("\tIPv6 address: [");
		       PyString_Concat(&tmp, py_addr->scope);
		       PyString_ConcatAndDel(&tmp, PyString_FromString("] "));
		       PyString_Concat(&tmp, py_addr->local);
		       PyString_ConcatAndDel(&tmp, PyString_FromFormat("/%d", py_addr->prefixlen));
		       PyString_ConcatAndDel(&tmp, PyString_FromString("\n"));
		       PyString_ConcatAndDel(&ret, tmp);
	       }
	}

	return ret;
}

/**
 * Returns a tuple list of configured IPv4 addresses
 *
 * @param self     Pointer to the current PyEtherInfo device object to extract IPv4 info from
 * @param notused
 *
 * @return Returns a Python tuple list of NetlinkIP4Address objects
 */
static PyObject *_ethtool_etherinfo_get_ipv4_addresses(PyEtherInfo *self, PyObject *notused) {
	if( !self ) {
		PyErr_SetString(PyExc_AttributeError, "No data available");
		return NULL;
	}

	return get_etherinfo_address(self, NLQRY_ADDR4);
}


/**
 * Returns a tuple list of configured IPv6 addresses
 *
 * @param self     Pointer to the current PyEtherInfo device object to extract IPv6 info from
 * @param notused
 *
 * @return Returns a Python tuple list of NetlinkIP6Address objects
 */
static PyObject *_ethtool_etherinfo_get_ipv6_addresses(PyEtherInfo *self, PyObject *notused) {
	if( !self ) {
		PyErr_SetString(PyExc_AttributeError, "No data available");
		return NULL;
	}

	return get_etherinfo_address(self, NLQRY_ADDR6);
}


/**
 * Defines all available methods in the ethtool.etherinfo class
 *
 */
static PyMethodDef _ethtool_etherinfo_methods[] = {
	{"get_ipv4_addresses", (PyCFunction)_ethtool_etherinfo_get_ipv4_addresses, METH_NOARGS,
	 "Retrieve configured IPv4 addresses.  Returns a list of NetlinkIPaddress objects"},
	{"get_ipv6_addresses", (PyCFunction)_ethtool_etherinfo_get_ipv6_addresses, METH_NOARGS,
	 "Retrieve configured IPv6 addresses.  Returns a list of NetlinkIPaddress objects"},
	{NULL}  /**< No methods defined */
};

/**
 * Definition of the functions a Python class/object requires.
 *
 */
PyTypeObject PyEtherInfo_Type = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "ethtool.etherinfo",
    .tp_basicsize = sizeof(PyEtherInfo),
    .tp_flags = Py_TPFLAGS_HAVE_CLASS,
    .tp_dealloc = (destructor)_ethtool_etherinfo_dealloc,
    .tp_str = (reprfunc)_ethtool_etherinfo_str,
    .tp_getattro = (getattrofunc)_ethtool_etherinfo_getter,
    .tp_setattro = (setattrofunc)_ethtool_etherinfo_setter,
    .tp_methods = _ethtool_etherinfo_methods,
    .tp_doc = "Contains information about a specific ethernet device"
};

