/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <Python.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <strings.h>
#include <unistd.h>
#include <libnvpair.h>
#include <libintl.h>
#include <libzfs.h>
#include <libzfs_impl.h>
#include "zfs_prop.h"

static PyObject *ZFSError;
static int zfsdevfd;

#ifdef __lint
#define	dgettext(x, y) y
#endif

#define	_(s) dgettext(TEXT_DOMAIN, s)

/*PRINTFLIKE1*/
static void
seterr(char *fmt, ...)
{
	char errstr[1024];
	va_list v;

	va_start(v, fmt);
	(void) vsnprintf(errstr, sizeof (errstr), fmt, v);
	va_end(v);

	PyErr_SetObject(ZFSError, Py_BuildValue("is", errno, errstr));
}

static char cmdstr[HIS_MAX_RECORD_LEN];

static int
ioctl_with_cmdstr(int ioc, zfs_cmd_t *zc)
{
	int err;

	if (cmdstr[0])
		zc->zc_history = (uint64_t)(uintptr_t)cmdstr;
	err = ioctl(zfsdevfd, ioc, zc);
	cmdstr[0] = '\0';
	return (err);
}

static PyObject *
nvl2py(nvlist_t *nvl)
{
	PyObject *pyo;
	nvpair_t *nvp;

	pyo = PyDict_New();

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		PyObject *pyval;
		char *sval;
		uint64_t ival;
		boolean_t bval;
		nvlist_t *nval;

		switch (nvpair_type(nvp)) {
		case DATA_TYPE_STRING:
			(void) nvpair_value_string(nvp, &sval);
			pyval = Py_BuildValue("s", sval);
			break;

		case DATA_TYPE_UINT64:
			(void) nvpair_value_uint64(nvp, &ival);
			pyval = Py_BuildValue("K", ival);
			break;

		case DATA_TYPE_NVLIST:
			(void) nvpair_value_nvlist(nvp, &nval);
			pyval = nvl2py(nval);
			break;

		case DATA_TYPE_BOOLEAN:
			Py_INCREF(Py_None);
			pyval = Py_None;
			break;

		case DATA_TYPE_BOOLEAN_VALUE:
			(void) nvpair_value_boolean_value(nvp, &bval);
			pyval = Py_BuildValue("i", bval);
			break;

		default:
			PyErr_SetNone(PyExc_ValueError);
			Py_DECREF(pyo);
			return (NULL);
		}

		PyDict_SetItemString(pyo, nvpair_name(nvp), pyval);
		Py_DECREF(pyval);
	}

	return (pyo);
}

static nvlist_t *
dict2nvl(PyObject *d)
{
	nvlist_t *nvl;
	int err;
	PyObject *key, *value;
	int pos = 0;

	if (!PyDict_Check(d)) {
		PyErr_SetObject(PyExc_ValueError, d);
		return (NULL);
	}

	err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0);
	assert(err == 0);

	while (PyDict_Next(d, &pos, &key, &value)) {
		char *keystr = PyString_AsString(key);
		if (keystr == NULL) {
			PyErr_SetObject(PyExc_KeyError, key);
			nvlist_free(nvl);
			return (NULL);
		}

		if (PyDict_Check(value)) {
			nvlist_t *valnvl = dict2nvl(value);
			err = nvlist_add_nvlist(nvl, keystr, valnvl);
			nvlist_free(valnvl);
		} else if (value == Py_None) {
			err = nvlist_add_boolean(nvl, keystr);
		} else if (PyString_Check(value)) {
			char *valstr = PyString_AsString(value);
			err = nvlist_add_string(nvl, keystr, valstr);
		} else if (PyInt_Check(value)) {
			uint64_t valint = PyInt_AsUnsignedLongLongMask(value);
			err = nvlist_add_uint64(nvl, keystr, valint);
		} else if (PyBool_Check(value)) {
			boolean_t valbool = value == Py_True ? B_TRUE : B_FALSE;
			err = nvlist_add_boolean_value(nvl, keystr, valbool);
		} else {
			PyErr_SetObject(PyExc_ValueError, value);
			nvlist_free(nvl);
			return (NULL);
		}
		assert(err == 0);
	}

	return (nvl);
}

static PyObject *
fakepropval(uint64_t value)
{
	PyObject *d = PyDict_New();
	PyDict_SetItemString(d, "value", Py_BuildValue("K", value));
	return (d);
}

static void
add_ds_props(zfs_cmd_t *zc, PyObject *nvl)
{
	dmu_objset_stats_t *s = &zc->zc_objset_stats;
	PyDict_SetItemString(nvl, "numclones",
	    fakepropval(s->dds_num_clones));
	PyDict_SetItemString(nvl, "issnap",
	    fakepropval(s->dds_is_snapshot));
	PyDict_SetItemString(nvl, "inconsistent",
	    fakepropval(s->dds_inconsistent));
}

/* On error, returns NULL but does not set python exception. */
static PyObject *
ioctl_with_dstnv(int ioc, zfs_cmd_t *zc)
{
	int nvsz = 2048;
	void *nvbuf;
	PyObject *pynv = NULL;

again:
	nvbuf = malloc(nvsz);
	zc->zc_nvlist_dst_size = nvsz;
	zc->zc_nvlist_dst = (uintptr_t)nvbuf;

	if (ioctl(zfsdevfd, ioc, zc) == 0) {
		nvlist_t *nvl;

		errno = nvlist_unpack(nvbuf, zc->zc_nvlist_dst_size, &nvl, 0);
		if (errno == 0) {
			pynv = nvl2py(nvl);
			nvlist_free(nvl);
		}
	} else if (errno == ENOMEM) {
		free(nvbuf);
		nvsz = zc->zc_nvlist_dst_size;
		goto again;
	}
	free(nvbuf);
	return (pynv);
}

static PyObject *
py_next_dataset(PyObject *self, PyObject *args)
{
	int ioc;
	uint64_t cookie;
	zfs_cmd_t zc = { 0 };
	int snaps;
	char *name;
	PyObject *nvl;
	PyObject *ret = NULL;

	if (!PyArg_ParseTuple(args, "siK", &name, &snaps, &cookie))
		return (NULL);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));
	zc.zc_cookie = cookie;

	if (snaps)
		ioc = ZFS_IOC_SNAPSHOT_LIST_NEXT;
	else
		ioc = ZFS_IOC_DATASET_LIST_NEXT;

	nvl = ioctl_with_dstnv(ioc, &zc);
	if (nvl) {
		add_ds_props(&zc, nvl);
		ret = Py_BuildValue("sKO", zc.zc_name, zc.zc_cookie, nvl);
		Py_DECREF(nvl);
	} else if (errno == ESRCH) {
		PyErr_SetNone(PyExc_StopIteration);
	} else {
		if (snaps)
			seterr(_("cannot get snapshots of %s"), name);
		else
			seterr(_("cannot get child datasets of %s"), name);
	}
	return (ret);
}

static PyObject *
py_dataset_props(PyObject *self, PyObject *args)
{
	zfs_cmd_t zc = { 0 };
	int snaps;
	char *name;
	PyObject *nvl;

	if (!PyArg_ParseTuple(args, "s", &name))
		return (NULL);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	nvl = ioctl_with_dstnv(ZFS_IOC_OBJSET_STATS, &zc);
	if (nvl) {
		add_ds_props(&zc, nvl);
	} else {
		seterr(_("cannot access dataset %s"), name);
	}
	return (nvl);
}

static PyObject *
py_get_fsacl(PyObject *self, PyObject *args)
{
	zfs_cmd_t zc = { 0 };
	char *name;
	PyObject *nvl;

	if (!PyArg_ParseTuple(args, "s", &name))
		return (NULL);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	nvl = ioctl_with_dstnv(ZFS_IOC_GET_FSACL, &zc);
	if (nvl == NULL)
		seterr(_("cannot get permissions on %s"), name);

	return (nvl);
}

static PyObject *
py_set_fsacl(PyObject *self, PyObject *args)
{
	int un;
	size_t nvsz;
	zfs_cmd_t zc = { 0 };
	char *name, *nvbuf;
	PyObject *dict, *file;
	nvlist_t *nvl;
	int err;

	if (!PyArg_ParseTuple(args, "siO!", &name, &un,
	    &PyDict_Type, &dict))
		return (NULL);

	nvl = dict2nvl(dict);
	if (nvl == NULL)
		return (NULL);

	err = nvlist_size(nvl, &nvsz, NV_ENCODE_NATIVE);
	assert(err == 0);
	nvbuf = malloc(nvsz);
	err = nvlist_pack(nvl, &nvbuf, &nvsz, NV_ENCODE_NATIVE, 0);
	assert(err == 0);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));
	zc.zc_nvlist_src_size = nvsz;
	zc.zc_nvlist_src = (uintptr_t)nvbuf;
	zc.zc_perm_action = un;

	err = ioctl_with_cmdstr(ZFS_IOC_SET_FSACL, &zc);
	free(nvbuf);
	if (err) {
		seterr(_("cannot set permissions on %s"), name);
		return (NULL);
	}

	Py_RETURN_NONE;
}

static PyObject *
py_get_holds(PyObject *self, PyObject *args)
{
	zfs_cmd_t zc = { 0 };
	char *name;
	PyObject *nvl;

	if (!PyArg_ParseTuple(args, "s", &name))
		return (NULL);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	nvl = ioctl_with_dstnv(ZFS_IOC_GET_HOLDS, &zc);
	if (nvl == NULL)
		seterr(_("cannot get holds for %s"), name);

	return (nvl);
}

static PyObject *
py_userspace_many(PyObject *self, PyObject *args)
{
	zfs_cmd_t zc = { 0 };
	zfs_userquota_prop_t type;
	char *name, *propname;
	int bufsz = 1<<20;
	void *buf;
	PyObject *dict, *file;
	int error;

	if (!PyArg_ParseTuple(args, "ss", &name, &propname))
		return (NULL);

	for (type = 0; type < ZFS_NUM_USERQUOTA_PROPS; type++)
		if (strcmp(propname, zfs_userquota_prop_prefixes[type]) == 0)
			break;
	if (type == ZFS_NUM_USERQUOTA_PROPS) {
		PyErr_SetString(PyExc_KeyError, propname);
		return (NULL);
	}

	dict = PyDict_New();
	buf = malloc(bufsz);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));
	zc.zc_objset_type = type;
	zc.zc_cookie = 0;

	while (1) {
		zfs_useracct_t *zua = buf;

		zc.zc_nvlist_dst = (uintptr_t)buf;
		zc.zc_nvlist_dst_size = bufsz;

		error = ioctl(zfsdevfd, ZFS_IOC_USERSPACE_MANY, &zc);
		if (error || zc.zc_nvlist_dst_size == 0)
			break;

		while (zc.zc_nvlist_dst_size > 0) {
			PyObject *pykey, *pyval;

			pykey = Py_BuildValue("sI",
			    zua->zu_domain, zua->zu_rid);
			pyval = Py_BuildValue("K", zua->zu_space);
			PyDict_SetItem(dict, pykey, pyval);
			Py_DECREF(pykey);
			Py_DECREF(pyval);

			zua++;
			zc.zc_nvlist_dst_size -= sizeof (zfs_useracct_t);
		}
	}

	free(buf);

	if (error != 0) {
		Py_DECREF(dict);
		seterr(_("cannot get %s property on %s"), propname, name);
		return (NULL);
	}

	return (dict);
}

static PyObject *
py_userspace_upgrade(PyObject *self, PyObject *args)
{
	zfs_cmd_t zc = { 0 };
	char *name;
	int error;

	if (!PyArg_ParseTuple(args, "s", &name))
		return (NULL);

	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));
	error = ioctl(zfsdevfd, ZFS_IOC_USERSPACE_UPGRADE, &zc);

	if (error != 0) {
		seterr(_("cannot initialize user accounting information on %s"),
		    name);
		return (NULL);
	}

	Py_RETURN_NONE;
}

static PyObject *
py_set_cmdstr(PyObject *self, PyObject *args)
{
	char *str;

	if (!PyArg_ParseTuple(args, "s", &str))
		return (NULL);

	(void) strlcpy(cmdstr, str, sizeof (cmdstr));

	Py_RETURN_NONE;
}

static PyObject *
py_get_proptable(PyObject *self, PyObject *args)
{
	zprop_desc_t *t = zfs_prop_get_table();
	PyObject *d = PyDict_New();
	zfs_prop_t i;

	for (i = 0; i < ZFS_NUM_PROPS; i++) {
		zprop_desc_t *p = &t[i];
		PyObject *tuple;
		static const char *typetable[] =
		    {"number", "string", "index"};
		static const char *attrtable[] =
		    {"default", "readonly", "inherit", "onetime"};
		PyObject *indextable;

		if (p->pd_proptype == PROP_TYPE_INDEX) {
			const zprop_index_t *it = p->pd_table;
			indextable = PyDict_New();
			int j;
			for (j = 0; it[j].pi_name; j++) {
				PyDict_SetItemString(indextable,
				    it[j].pi_name,
				    Py_BuildValue("K", it[j].pi_value));
			}
		} else {
			Py_INCREF(Py_None);
			indextable = Py_None;
		}

		tuple = Py_BuildValue("sissKsissiiO",
		    p->pd_name, p->pd_propnum, typetable[p->pd_proptype],
		    p->pd_strdefault, p->pd_numdefault,
		    attrtable[p->pd_attr], p->pd_types,
		    p->pd_values, p->pd_colname,
		    p->pd_rightalign, p->pd_visible, indextable);
		PyDict_SetItemString(d, p->pd_name, tuple);
		Py_DECREF(tuple);
	}

	return (d);
}

static PyMethodDef zfsmethods[] = {
	{"next_dataset", py_next_dataset, METH_VARARGS,
	    "Get next child dataset or snapshot."},
	{"get_fsacl", py_get_fsacl, METH_VARARGS, "Get allowed permissions."},
	{"set_fsacl", py_set_fsacl, METH_VARARGS, "Set allowed permissions."},
	{"userspace_many", py_userspace_many, METH_VARARGS,
	    "Get user space accounting."},
	{"userspace_upgrade", py_userspace_upgrade, METH_VARARGS,
	    "Upgrade fs to enable user space accounting."},
	{"set_cmdstr", py_set_cmdstr, METH_VARARGS,
	    "Set command string for history logging."},
	{"dataset_props", py_dataset_props, METH_VARARGS,
	    "Get dataset properties."},
	{"get_proptable", py_get_proptable, METH_NOARGS,
	    "Get property table."},
	{"get_holds", py_get_holds, METH_VARARGS, "Get user holds."},
	{NULL, NULL, 0, NULL}
};

void
initioctl(void)
{
	PyObject *zfs_ioctl = Py_InitModule("zfs.ioctl", zfsmethods);
	PyObject *zfs_util = PyImport_ImportModule("zfs.util");
	PyObject *devfile;

	if (zfs_util == NULL)
		return;

	ZFSError = PyObject_GetAttrString(zfs_util, "ZFSError");
	devfile = PyObject_GetAttrString(zfs_util, "dev");
	zfsdevfd = PyObject_AsFileDescriptor(devfile);

	zfs_prop_init();
}
