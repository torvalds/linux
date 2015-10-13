/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/obdclass/genops.c
 *
 * These are the only exported functions, they provide some generic
 * infrastructure for managing object devices
 */

#define DEBUG_SUBSYSTEM S_CLASS
#include "../include/obd_class.h"
#include "../include/lprocfs_status.h"

spinlock_t obd_types_lock;

static struct kmem_cache *obd_device_cachep;
struct kmem_cache *obdo_cachep;
EXPORT_SYMBOL(obdo_cachep);
static struct kmem_cache *import_cachep;

static struct list_head      obd_zombie_imports;
static struct list_head      obd_zombie_exports;
static spinlock_t  obd_zombie_impexp_lock;
static void obd_zombie_impexp_notify(void);
static void obd_zombie_export_add(struct obd_export *exp);
static void obd_zombie_import_add(struct obd_import *imp);
static void print_export_data(struct obd_export *exp,
			      const char *status, int locks);

int (*ptlrpc_put_connection_superhack)(struct ptlrpc_connection *c);
EXPORT_SYMBOL(ptlrpc_put_connection_superhack);

/*
 * support functions: we could use inter-module communication, but this
 * is more portable to other OS's
 */
static struct obd_device *obd_device_alloc(void)
{
	struct obd_device *obd;

	OBD_SLAB_ALLOC_PTR_GFP(obd, obd_device_cachep, GFP_NOFS);
	if (obd != NULL)
		obd->obd_magic = OBD_DEVICE_MAGIC;
	return obd;
}

static void obd_device_free(struct obd_device *obd)
{
	LASSERT(obd != NULL);
	LASSERTF(obd->obd_magic == OBD_DEVICE_MAGIC, "obd %p obd_magic %08x != %08x\n",
		 obd, obd->obd_magic, OBD_DEVICE_MAGIC);
	if (obd->obd_namespace != NULL) {
		CERROR("obd %p: namespace %p was not properly cleaned up (obd_force=%d)!\n",
		       obd, obd->obd_namespace, obd->obd_force);
		LBUG();
	}
	lu_ref_fini(&obd->obd_reference);
	OBD_SLAB_FREE_PTR(obd, obd_device_cachep);
}

struct obd_type *class_search_type(const char *name)
{
	struct list_head *tmp;
	struct obd_type *type;

	spin_lock(&obd_types_lock);
	list_for_each(tmp, &obd_types) {
		type = list_entry(tmp, struct obd_type, typ_chain);
		if (strcmp(type->typ_name, name) == 0) {
			spin_unlock(&obd_types_lock);
			return type;
		}
	}
	spin_unlock(&obd_types_lock);
	return NULL;
}
EXPORT_SYMBOL(class_search_type);

struct obd_type *class_get_type(const char *name)
{
	struct obd_type *type = class_search_type(name);

	if (!type) {
		const char *modname = name;

		if (strcmp(modname, "obdfilter") == 0)
			modname = "ofd";

		if (strcmp(modname, LUSTRE_LWP_NAME) == 0)
			modname = LUSTRE_OSP_NAME;

		if (!strncmp(modname, LUSTRE_MDS_NAME, strlen(LUSTRE_MDS_NAME)))
			modname = LUSTRE_MDT_NAME;

		if (!request_module("%s", modname)) {
			CDEBUG(D_INFO, "Loaded module '%s'\n", modname);
			type = class_search_type(name);
		} else {
			LCONSOLE_ERROR_MSG(0x158, "Can't load module '%s'\n",
					   modname);
		}
	}
	if (type) {
		spin_lock(&type->obd_type_lock);
		type->typ_refcnt++;
		try_module_get(type->typ_dt_ops->o_owner);
		spin_unlock(&type->obd_type_lock);
	}
	return type;
}
EXPORT_SYMBOL(class_get_type);

void class_put_type(struct obd_type *type)
{
	LASSERT(type);
	spin_lock(&type->obd_type_lock);
	type->typ_refcnt--;
	module_put(type->typ_dt_ops->o_owner);
	spin_unlock(&type->obd_type_lock);
}
EXPORT_SYMBOL(class_put_type);

#define CLASS_MAX_NAME 1024

int class_register_type(struct obd_ops *dt_ops, struct md_ops *md_ops,
			const char *name,
			struct lu_device_type *ldt)
{
	struct obd_type *type;
	int rc = 0;

	/* sanity check */
	LASSERT(strnlen(name, CLASS_MAX_NAME) < CLASS_MAX_NAME);

	if (class_search_type(name)) {
		CDEBUG(D_IOCTL, "Type %s already registered\n", name);
		return -EEXIST;
	}

	rc = -ENOMEM;
	type = kzalloc(sizeof(*type), GFP_NOFS);
	if (!type)
		return rc;

	type->typ_dt_ops = kzalloc(sizeof(*type->typ_dt_ops), GFP_NOFS);
	type->typ_md_ops = kzalloc(sizeof(*type->typ_md_ops), GFP_NOFS);
	type->typ_name = kzalloc(strlen(name) + 1, GFP_NOFS);

	if (type->typ_dt_ops == NULL ||
	    type->typ_md_ops == NULL ||
	    type->typ_name == NULL)
		goto failed;

	*(type->typ_dt_ops) = *dt_ops;
	/* md_ops is optional */
	if (md_ops)
		*(type->typ_md_ops) = *md_ops;
	strcpy(type->typ_name, name);
	spin_lock_init(&type->obd_type_lock);

	type->typ_debugfs_entry = ldebugfs_register(type->typ_name,
						    debugfs_lustre_root,
						    NULL, type);
	if (IS_ERR_OR_NULL(type->typ_debugfs_entry)) {
		rc = type->typ_debugfs_entry ? PTR_ERR(type->typ_debugfs_entry)
					     : -ENOMEM;
		type->typ_debugfs_entry = NULL;
		goto failed;
	}

	type->typ_kobj = kobject_create_and_add(type->typ_name, lustre_kobj);
	if (!type->typ_kobj) {
		rc = -ENOMEM;
		goto failed;
	}

	if (ldt != NULL) {
		type->typ_lu = ldt;
		rc = lu_device_type_init(ldt);
		if (rc != 0)
			goto failed;
	}

	spin_lock(&obd_types_lock);
	list_add(&type->typ_chain, &obd_types);
	spin_unlock(&obd_types_lock);

	return 0;

 failed:
	if (type->typ_kobj)
		kobject_put(type->typ_kobj);
	kfree(type->typ_name);
	kfree(type->typ_md_ops);
	kfree(type->typ_dt_ops);
	kfree(type);
	return rc;
}
EXPORT_SYMBOL(class_register_type);

int class_unregister_type(const char *name)
{
	struct obd_type *type = class_search_type(name);

	if (!type) {
		CERROR("unknown obd type\n");
		return -EINVAL;
	}

	if (type->typ_refcnt) {
		CERROR("type %s has refcount (%d)\n", name, type->typ_refcnt);
		/* This is a bad situation, let's make the best of it */
		/* Remove ops, but leave the name for debugging */
		kfree(type->typ_dt_ops);
		kfree(type->typ_md_ops);
		return -EBUSY;
	}

	if (type->typ_kobj)
		kobject_put(type->typ_kobj);

	if (!IS_ERR_OR_NULL(type->typ_debugfs_entry))
		ldebugfs_remove(&type->typ_debugfs_entry);

	if (type->typ_lu)
		lu_device_type_fini(type->typ_lu);

	spin_lock(&obd_types_lock);
	list_del(&type->typ_chain);
	spin_unlock(&obd_types_lock);
	kfree(type->typ_name);
	kfree(type->typ_dt_ops);
	kfree(type->typ_md_ops);
	kfree(type);
	return 0;
} /* class_unregister_type */
EXPORT_SYMBOL(class_unregister_type);

/**
 * Create a new obd device.
 *
 * Find an empty slot in ::obd_devs[], create a new obd device in it.
 *
 * \param[in] type_name obd device type string.
 * \param[in] name      obd device name.
 *
 * \retval NULL if create fails, otherwise return the obd device
 *	 pointer created.
 */
struct obd_device *class_newdev(const char *type_name, const char *name)
{
	struct obd_device *result = NULL;
	struct obd_device *newdev;
	struct obd_type *type = NULL;
	int i;
	int new_obd_minor = 0;

	if (strlen(name) >= MAX_OBD_NAME) {
		CERROR("name/uuid must be < %u bytes long\n", MAX_OBD_NAME);
		return ERR_PTR(-EINVAL);
	}

	type = class_get_type(type_name);
	if (type == NULL) {
		CERROR("OBD: unknown type: %s\n", type_name);
		return ERR_PTR(-ENODEV);
	}

	newdev = obd_device_alloc();
	if (newdev == NULL) {
		result = ERR_PTR(-ENOMEM);
		goto out_type;
	}

	LASSERT(newdev->obd_magic == OBD_DEVICE_MAGIC);

	write_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		struct obd_device *obd = class_num2obd(i);

		if (obd && (strcmp(name, obd->obd_name) == 0)) {
			CERROR("Device %s already exists at %d, won't add\n",
			       name, i);
			if (result) {
				LASSERTF(result->obd_magic == OBD_DEVICE_MAGIC,
					 "%p obd_magic %08x != %08x\n", result,
					 result->obd_magic, OBD_DEVICE_MAGIC);
				LASSERTF(result->obd_minor == new_obd_minor,
					 "%p obd_minor %d != %d\n", result,
					 result->obd_minor, new_obd_minor);

				obd_devs[result->obd_minor] = NULL;
				result->obd_name[0] = '\0';
			 }
			result = ERR_PTR(-EEXIST);
			break;
		}
		if (!result && !obd) {
			result = newdev;
			result->obd_minor = i;
			new_obd_minor = i;
			result->obd_type = type;
			strncpy(result->obd_name, name,
				sizeof(result->obd_name) - 1);
			obd_devs[i] = result;
		}
	}
	write_unlock(&obd_dev_lock);

	if (result == NULL && i >= class_devno_max()) {
		CERROR("all %u OBD devices used, increase MAX_OBD_DEVICES\n",
		       class_devno_max());
		result = ERR_PTR(-EOVERFLOW);
		goto out;
	}

	if (IS_ERR(result))
		goto out;

	CDEBUG(D_IOCTL, "Adding new device %s (%p)\n",
	       result->obd_name, result);

	return result;
out:
	obd_device_free(newdev);
out_type:
	class_put_type(type);
	return result;
}

void class_release_dev(struct obd_device *obd)
{
	struct obd_type *obd_type = obd->obd_type;

	LASSERTF(obd->obd_magic == OBD_DEVICE_MAGIC, "%p obd_magic %08x != %08x\n",
		 obd, obd->obd_magic, OBD_DEVICE_MAGIC);
	LASSERTF(obd == obd_devs[obd->obd_minor], "obd %p != obd_devs[%d] %p\n",
		 obd, obd->obd_minor, obd_devs[obd->obd_minor]);
	LASSERT(obd_type != NULL);

	CDEBUG(D_INFO, "Release obd device %s at %d obd_type name =%s\n",
	       obd->obd_name, obd->obd_minor, obd->obd_type->typ_name);

	write_lock(&obd_dev_lock);
	obd_devs[obd->obd_minor] = NULL;
	write_unlock(&obd_dev_lock);
	obd_device_free(obd);

	class_put_type(obd_type);
}

int class_name2dev(const char *name)
{
	int i;

	if (!name)
		return -1;

	read_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		struct obd_device *obd = class_num2obd(i);

		if (obd && strcmp(name, obd->obd_name) == 0) {
			/* Make sure we finished attaching before we give
			   out any references */
			LASSERT(obd->obd_magic == OBD_DEVICE_MAGIC);
			if (obd->obd_attached) {
				read_unlock(&obd_dev_lock);
				return i;
			}
			break;
		}
	}
	read_unlock(&obd_dev_lock);

	return -1;
}
EXPORT_SYMBOL(class_name2dev);

struct obd_device *class_name2obd(const char *name)
{
	int dev = class_name2dev(name);

	if (dev < 0 || dev > class_devno_max())
		return NULL;
	return class_num2obd(dev);
}
EXPORT_SYMBOL(class_name2obd);

int class_uuid2dev(struct obd_uuid *uuid)
{
	int i;

	read_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		struct obd_device *obd = class_num2obd(i);

		if (obd && obd_uuid_equals(uuid, &obd->obd_uuid)) {
			LASSERT(obd->obd_magic == OBD_DEVICE_MAGIC);
			read_unlock(&obd_dev_lock);
			return i;
		}
	}
	read_unlock(&obd_dev_lock);

	return -1;
}
EXPORT_SYMBOL(class_uuid2dev);

struct obd_device *class_uuid2obd(struct obd_uuid *uuid)
{
	int dev = class_uuid2dev(uuid);
	if (dev < 0)
		return NULL;
	return class_num2obd(dev);
}
EXPORT_SYMBOL(class_uuid2obd);

/**
 * Get obd device from ::obd_devs[]
 *
 * \param num [in] array index
 *
 * \retval NULL if ::obd_devs[\a num] does not contains an obd device
 *	 otherwise return the obd device there.
 */
struct obd_device *class_num2obd(int num)
{
	struct obd_device *obd = NULL;

	if (num < class_devno_max()) {
		obd = obd_devs[num];
		if (obd == NULL)
			return NULL;

		LASSERTF(obd->obd_magic == OBD_DEVICE_MAGIC,
			 "%p obd_magic %08x != %08x\n",
			 obd, obd->obd_magic, OBD_DEVICE_MAGIC);
		LASSERTF(obd->obd_minor == num,
			 "%p obd_minor %0d != %0d\n",
			 obd, obd->obd_minor, num);
	}

	return obd;
}
EXPORT_SYMBOL(class_num2obd);

/**
 * Get obd devices count. Device in any
 *    state are counted
 * \retval obd device count
 */
int get_devices_count(void)
{
	int index, max_index = class_devno_max(), dev_count = 0;

	read_lock(&obd_dev_lock);
	for (index = 0; index <= max_index; index++) {
		struct obd_device *obd = class_num2obd(index);
		if (obd != NULL)
			dev_count++;
	}
	read_unlock(&obd_dev_lock);

	return dev_count;
}
EXPORT_SYMBOL(get_devices_count);

void class_obd_list(void)
{
	char *status;
	int i;

	read_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		struct obd_device *obd = class_num2obd(i);

		if (obd == NULL)
			continue;
		if (obd->obd_stopping)
			status = "ST";
		else if (obd->obd_set_up)
			status = "UP";
		else if (obd->obd_attached)
			status = "AT";
		else
			status = "--";
		LCONSOLE(D_CONFIG, "%3d %s %s %s %s %d\n",
			 i, status, obd->obd_type->typ_name,
			 obd->obd_name, obd->obd_uuid.uuid,
			 atomic_read(&obd->obd_refcount));
	}
	read_unlock(&obd_dev_lock);
	return;
}

/* Search for a client OBD connected to tgt_uuid.  If grp_uuid is
   specified, then only the client with that uuid is returned,
   otherwise any client connected to the tgt is returned. */
struct obd_device *class_find_client_obd(struct obd_uuid *tgt_uuid,
					  const char *typ_name,
					  struct obd_uuid *grp_uuid)
{
	int i;

	read_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		struct obd_device *obd = class_num2obd(i);

		if (obd == NULL)
			continue;
		if ((strncmp(obd->obd_type->typ_name, typ_name,
			     strlen(typ_name)) == 0)) {
			if (obd_uuid_equals(tgt_uuid,
					    &obd->u.cli.cl_target_uuid) &&
			    ((grp_uuid)? obd_uuid_equals(grp_uuid,
							 &obd->obd_uuid) : 1)) {
				read_unlock(&obd_dev_lock);
				return obd;
			}
		}
	}
	read_unlock(&obd_dev_lock);

	return NULL;
}
EXPORT_SYMBOL(class_find_client_obd);

/* Iterate the obd_device list looking devices have grp_uuid. Start
   searching at *next, and if a device is found, the next index to look
   at is saved in *next. If next is NULL, then the first matching device
   will always be returned. */
struct obd_device *class_devices_in_group(struct obd_uuid *grp_uuid, int *next)
{
	int i;

	if (next == NULL)
		i = 0;
	else if (*next >= 0 && *next < class_devno_max())
		i = *next;
	else
		return NULL;

	read_lock(&obd_dev_lock);
	for (; i < class_devno_max(); i++) {
		struct obd_device *obd = class_num2obd(i);

		if (obd == NULL)
			continue;
		if (obd_uuid_equals(grp_uuid, &obd->obd_uuid)) {
			if (next != NULL)
				*next = i+1;
			read_unlock(&obd_dev_lock);
			return obd;
		}
	}
	read_unlock(&obd_dev_lock);

	return NULL;
}
EXPORT_SYMBOL(class_devices_in_group);

/**
 * to notify sptlrpc log for \a fsname has changed, let every relevant OBD
 * adjust sptlrpc settings accordingly.
 */
int class_notify_sptlrpc_conf(const char *fsname, int namelen)
{
	struct obd_device  *obd;
	const char	 *type;
	int		 i, rc = 0, rc2;

	LASSERT(namelen > 0);

	read_lock(&obd_dev_lock);
	for (i = 0; i < class_devno_max(); i++) {
		obd = class_num2obd(i);

		if (obd == NULL || obd->obd_set_up == 0 || obd->obd_stopping)
			continue;

		/* only notify mdc, osc, mdt, ost */
		type = obd->obd_type->typ_name;
		if (strcmp(type, LUSTRE_MDC_NAME) != 0 &&
		    strcmp(type, LUSTRE_OSC_NAME) != 0 &&
		    strcmp(type, LUSTRE_MDT_NAME) != 0 &&
		    strcmp(type, LUSTRE_OST_NAME) != 0)
			continue;

		if (strncmp(obd->obd_name, fsname, namelen))
			continue;

		class_incref(obd, __func__, obd);
		read_unlock(&obd_dev_lock);
		rc2 = obd_set_info_async(NULL, obd->obd_self_export,
					 sizeof(KEY_SPTLRPC_CONF),
					 KEY_SPTLRPC_CONF, 0, NULL, NULL);
		rc = rc ? rc : rc2;
		class_decref(obd, __func__, obd);
		read_lock(&obd_dev_lock);
	}
	read_unlock(&obd_dev_lock);
	return rc;
}
EXPORT_SYMBOL(class_notify_sptlrpc_conf);

void obd_cleanup_caches(void)
{
	if (obd_device_cachep) {
		kmem_cache_destroy(obd_device_cachep);
		obd_device_cachep = NULL;
	}
	if (obdo_cachep) {
		kmem_cache_destroy(obdo_cachep);
		obdo_cachep = NULL;
	}
	if (import_cachep) {
		kmem_cache_destroy(import_cachep);
		import_cachep = NULL;
	}
	if (capa_cachep) {
		kmem_cache_destroy(capa_cachep);
		capa_cachep = NULL;
	}
}

int obd_init_caches(void)
{
	LASSERT(obd_device_cachep == NULL);
	obd_device_cachep = kmem_cache_create("ll_obd_dev_cache",
						 sizeof(struct obd_device),
						 0, 0, NULL);
	if (!obd_device_cachep)
		goto out;

	LASSERT(obdo_cachep == NULL);
	obdo_cachep = kmem_cache_create("ll_obdo_cache", sizeof(struct obdo),
					   0, 0, NULL);
	if (!obdo_cachep)
		goto out;

	LASSERT(import_cachep == NULL);
	import_cachep = kmem_cache_create("ll_import_cache",
					     sizeof(struct obd_import),
					     0, 0, NULL);
	if (!import_cachep)
		goto out;

	LASSERT(capa_cachep == NULL);
	capa_cachep = kmem_cache_create("capa_cache",
					   sizeof(struct obd_capa), 0, 0, NULL);
	if (!capa_cachep)
		goto out;

	return 0;
 out:
	obd_cleanup_caches();
	return -ENOMEM;

}

/* map connection to client */
struct obd_export *class_conn2export(struct lustre_handle *conn)
{
	struct obd_export *export;

	if (!conn) {
		CDEBUG(D_CACHE, "looking for null handle\n");
		return NULL;
	}

	if (conn->cookie == -1) {  /* this means assign a new connection */
		CDEBUG(D_CACHE, "want a new connection\n");
		return NULL;
	}

	CDEBUG(D_INFO, "looking for export cookie %#llx\n", conn->cookie);
	export = class_handle2object(conn->cookie);
	return export;
}
EXPORT_SYMBOL(class_conn2export);

struct obd_device *class_exp2obd(struct obd_export *exp)
{
	if (exp)
		return exp->exp_obd;
	return NULL;
}
EXPORT_SYMBOL(class_exp2obd);

struct obd_device *class_conn2obd(struct lustre_handle *conn)
{
	struct obd_export *export;
	export = class_conn2export(conn);
	if (export) {
		struct obd_device *obd = export->exp_obd;
		class_export_put(export);
		return obd;
	}
	return NULL;
}
EXPORT_SYMBOL(class_conn2obd);

struct obd_import *class_exp2cliimp(struct obd_export *exp)
{
	struct obd_device *obd = exp->exp_obd;
	if (obd == NULL)
		return NULL;
	return obd->u.cli.cl_import;
}
EXPORT_SYMBOL(class_exp2cliimp);

struct obd_import *class_conn2cliimp(struct lustre_handle *conn)
{
	struct obd_device *obd = class_conn2obd(conn);
	if (obd == NULL)
		return NULL;
	return obd->u.cli.cl_import;
}
EXPORT_SYMBOL(class_conn2cliimp);

/* Export management functions */
static void class_export_destroy(struct obd_export *exp)
{
	struct obd_device *obd = exp->exp_obd;

	LASSERT_ATOMIC_ZERO(&exp->exp_refcount);
	LASSERT(obd != NULL);

	CDEBUG(D_IOCTL, "destroying export %p/%s for %s\n", exp,
	       exp->exp_client_uuid.uuid, obd->obd_name);

	/* "Local" exports (lctl, LOV->{mdc,osc}) have no connection. */
	if (exp->exp_connection)
		ptlrpc_put_connection_superhack(exp->exp_connection);

	LASSERT(list_empty(&exp->exp_outstanding_replies));
	LASSERT(list_empty(&exp->exp_uncommitted_replies));
	LASSERT(list_empty(&exp->exp_req_replay_queue));
	LASSERT(list_empty(&exp->exp_hp_rpcs));
	obd_destroy_export(exp);
	class_decref(obd, "export", exp);

	OBD_FREE_RCU(exp, sizeof(*exp), &exp->exp_handle);
}

static void export_handle_addref(void *export)
{
	class_export_get(export);
}

static struct portals_handle_ops export_handle_ops = {
	.hop_addref = export_handle_addref,
	.hop_free   = NULL,
};

struct obd_export *class_export_get(struct obd_export *exp)
{
	atomic_inc(&exp->exp_refcount);
	CDEBUG(D_INFO, "GETting export %p : new refcount %d\n", exp,
	       atomic_read(&exp->exp_refcount));
	return exp;
}
EXPORT_SYMBOL(class_export_get);

void class_export_put(struct obd_export *exp)
{
	LASSERT(exp != NULL);
	LASSERT_ATOMIC_GT_LT(&exp->exp_refcount, 0, LI_POISON);
	CDEBUG(D_INFO, "PUTting export %p : new refcount %d\n", exp,
	       atomic_read(&exp->exp_refcount) - 1);

	if (atomic_dec_and_test(&exp->exp_refcount)) {
		LASSERT(!list_empty(&exp->exp_obd_chain));
		CDEBUG(D_IOCTL, "final put %p/%s\n",
		       exp, exp->exp_client_uuid.uuid);

		/* release nid stat refererence */
		lprocfs_exp_cleanup(exp);

		obd_zombie_export_add(exp);
	}
}
EXPORT_SYMBOL(class_export_put);

/* Creates a new export, adds it to the hash table, and returns a
 * pointer to it. The refcount is 2: one for the hash reference, and
 * one for the pointer returned by this function. */
struct obd_export *class_new_export(struct obd_device *obd,
				    struct obd_uuid *cluuid)
{
	struct obd_export *export;
	struct cfs_hash *hash = NULL;
	int rc = 0;

	export = kzalloc(sizeof(*export), GFP_NOFS);
	if (!export)
		return ERR_PTR(-ENOMEM);

	export->exp_conn_cnt = 0;
	export->exp_lock_hash = NULL;
	export->exp_flock_hash = NULL;
	atomic_set(&export->exp_refcount, 2);
	atomic_set(&export->exp_rpc_count, 0);
	atomic_set(&export->exp_cb_count, 0);
	atomic_set(&export->exp_locks_count, 0);
#if LUSTRE_TRACKS_LOCK_EXP_REFS
	INIT_LIST_HEAD(&export->exp_locks_list);
	spin_lock_init(&export->exp_locks_list_guard);
#endif
	atomic_set(&export->exp_replay_count, 0);
	export->exp_obd = obd;
	INIT_LIST_HEAD(&export->exp_outstanding_replies);
	spin_lock_init(&export->exp_uncommitted_replies_lock);
	INIT_LIST_HEAD(&export->exp_uncommitted_replies);
	INIT_LIST_HEAD(&export->exp_req_replay_queue);
	INIT_LIST_HEAD(&export->exp_handle.h_link);
	INIT_LIST_HEAD(&export->exp_hp_rpcs);
	class_handle_hash(&export->exp_handle, &export_handle_ops);
	export->exp_last_request_time = get_seconds();
	spin_lock_init(&export->exp_lock);
	spin_lock_init(&export->exp_rpc_lock);
	INIT_HLIST_NODE(&export->exp_uuid_hash);
	INIT_HLIST_NODE(&export->exp_nid_hash);
	spin_lock_init(&export->exp_bl_list_lock);
	INIT_LIST_HEAD(&export->exp_bl_list);

	export->exp_sp_peer = LUSTRE_SP_ANY;
	export->exp_flvr.sf_rpc = SPTLRPC_FLVR_INVALID;
	export->exp_client_uuid = *cluuid;
	obd_init_export(export);

	spin_lock(&obd->obd_dev_lock);
	/* shouldn't happen, but might race */
	if (obd->obd_stopping) {
		rc = -ENODEV;
		goto exit_unlock;
	}

	hash = cfs_hash_getref(obd->obd_uuid_hash);
	if (hash == NULL) {
		rc = -ENODEV;
		goto exit_unlock;
	}
	spin_unlock(&obd->obd_dev_lock);

	if (!obd_uuid_equals(cluuid, &obd->obd_uuid)) {
		rc = cfs_hash_add_unique(hash, cluuid, &export->exp_uuid_hash);
		if (rc != 0) {
			LCONSOLE_WARN("%s: denying duplicate export for %s, %d\n",
				      obd->obd_name, cluuid->uuid, rc);
			rc = -EALREADY;
			goto exit_err;
		}
	}

	spin_lock(&obd->obd_dev_lock);
	if (obd->obd_stopping) {
		cfs_hash_del(hash, cluuid, &export->exp_uuid_hash);
		rc = -ENODEV;
		goto exit_unlock;
	}

	class_incref(obd, "export", export);
	list_add(&export->exp_obd_chain, &export->exp_obd->obd_exports);
	list_add_tail(&export->exp_obd_chain_timed,
			  &export->exp_obd->obd_exports_timed);
	export->exp_obd->obd_num_exports++;
	spin_unlock(&obd->obd_dev_lock);
	cfs_hash_putref(hash);
	return export;

exit_unlock:
	spin_unlock(&obd->obd_dev_lock);
exit_err:
	if (hash)
		cfs_hash_putref(hash);
	class_handle_unhash(&export->exp_handle);
	LASSERT(hlist_unhashed(&export->exp_uuid_hash));
	obd_destroy_export(export);
	kfree(export);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL(class_new_export);

void class_unlink_export(struct obd_export *exp)
{
	class_handle_unhash(&exp->exp_handle);

	spin_lock(&exp->exp_obd->obd_dev_lock);
	/* delete an uuid-export hashitem from hashtables */
	if (!hlist_unhashed(&exp->exp_uuid_hash))
		cfs_hash_del(exp->exp_obd->obd_uuid_hash,
			     &exp->exp_client_uuid,
			     &exp->exp_uuid_hash);

	list_move(&exp->exp_obd_chain, &exp->exp_obd->obd_unlinked_exports);
	list_del_init(&exp->exp_obd_chain_timed);
	exp->exp_obd->obd_num_exports--;
	spin_unlock(&exp->exp_obd->obd_dev_lock);
	class_export_put(exp);
}
EXPORT_SYMBOL(class_unlink_export);

/* Import management functions */
static void class_import_destroy(struct obd_import *imp)
{
	CDEBUG(D_IOCTL, "destroying import %p for %s\n", imp,
		imp->imp_obd->obd_name);

	LASSERT_ATOMIC_ZERO(&imp->imp_refcount);

	ptlrpc_put_connection_superhack(imp->imp_connection);

	while (!list_empty(&imp->imp_conn_list)) {
		struct obd_import_conn *imp_conn;

		imp_conn = list_entry(imp->imp_conn_list.next,
					  struct obd_import_conn, oic_item);
		list_del_init(&imp_conn->oic_item);
		ptlrpc_put_connection_superhack(imp_conn->oic_conn);
		kfree(imp_conn);
	}

	LASSERT(imp->imp_sec == NULL);
	class_decref(imp->imp_obd, "import", imp);
	OBD_FREE_RCU(imp, sizeof(*imp), &imp->imp_handle);
}

static void import_handle_addref(void *import)
{
	class_import_get(import);
}

static struct portals_handle_ops import_handle_ops = {
	.hop_addref = import_handle_addref,
	.hop_free   = NULL,
};

struct obd_import *class_import_get(struct obd_import *import)
{
	atomic_inc(&import->imp_refcount);
	CDEBUG(D_INFO, "import %p refcount=%d obd=%s\n", import,
	       atomic_read(&import->imp_refcount),
	       import->imp_obd->obd_name);
	return import;
}
EXPORT_SYMBOL(class_import_get);

void class_import_put(struct obd_import *imp)
{
	LASSERT(list_empty(&imp->imp_zombie_chain));
	LASSERT_ATOMIC_GT_LT(&imp->imp_refcount, 0, LI_POISON);

	CDEBUG(D_INFO, "import %p refcount=%d obd=%s\n", imp,
	       atomic_read(&imp->imp_refcount) - 1,
	       imp->imp_obd->obd_name);

	if (atomic_dec_and_test(&imp->imp_refcount)) {
		CDEBUG(D_INFO, "final put import %p\n", imp);
		obd_zombie_import_add(imp);
	}

	/* catch possible import put race */
	LASSERT_ATOMIC_GE_LT(&imp->imp_refcount, 0, LI_POISON);
}
EXPORT_SYMBOL(class_import_put);

static void init_imp_at(struct imp_at *at)
{
	int i;
	at_init(&at->iat_net_latency, 0, 0);
	for (i = 0; i < IMP_AT_MAX_PORTALS; i++) {
		/* max service estimates are tracked on the server side, so
		   don't use the AT history here, just use the last reported
		   val. (But keep hist for proc histogram, worst_ever) */
		at_init(&at->iat_service_estimate[i], INITIAL_CONNECT_TIMEOUT,
			AT_FLG_NOHIST);
	}
}

struct obd_import *class_new_import(struct obd_device *obd)
{
	struct obd_import *imp;

	imp = kzalloc(sizeof(*imp), GFP_NOFS);
	if (!imp)
		return NULL;

	INIT_LIST_HEAD(&imp->imp_pinger_chain);
	INIT_LIST_HEAD(&imp->imp_zombie_chain);
	INIT_LIST_HEAD(&imp->imp_replay_list);
	INIT_LIST_HEAD(&imp->imp_sending_list);
	INIT_LIST_HEAD(&imp->imp_delayed_list);
	INIT_LIST_HEAD(&imp->imp_committed_list);
	imp->imp_replay_cursor = &imp->imp_committed_list;
	spin_lock_init(&imp->imp_lock);
	imp->imp_last_success_conn = 0;
	imp->imp_state = LUSTRE_IMP_NEW;
	imp->imp_obd = class_incref(obd, "import", imp);
	mutex_init(&imp->imp_sec_mutex);
	init_waitqueue_head(&imp->imp_recovery_waitq);

	atomic_set(&imp->imp_refcount, 2);
	atomic_set(&imp->imp_unregistering, 0);
	atomic_set(&imp->imp_inflight, 0);
	atomic_set(&imp->imp_replay_inflight, 0);
	atomic_set(&imp->imp_inval_count, 0);
	INIT_LIST_HEAD(&imp->imp_conn_list);
	INIT_LIST_HEAD(&imp->imp_handle.h_link);
	class_handle_hash(&imp->imp_handle, &import_handle_ops);
	init_imp_at(&imp->imp_at);

	/* the default magic is V2, will be used in connect RPC, and
	 * then adjusted according to the flags in request/reply. */
	imp->imp_msg_magic = LUSTRE_MSG_MAGIC_V2;

	return imp;
}
EXPORT_SYMBOL(class_new_import);

void class_destroy_import(struct obd_import *import)
{
	LASSERT(import != NULL);
	LASSERT(import != LP_POISON);

	class_handle_unhash(&import->imp_handle);

	spin_lock(&import->imp_lock);
	import->imp_generation++;
	spin_unlock(&import->imp_lock);
	class_import_put(import);
}
EXPORT_SYMBOL(class_destroy_import);

#if LUSTRE_TRACKS_LOCK_EXP_REFS

void __class_export_add_lock_ref(struct obd_export *exp, struct ldlm_lock *lock)
{
	spin_lock(&exp->exp_locks_list_guard);

	LASSERT(lock->l_exp_refs_nr >= 0);

	if (lock->l_exp_refs_target != NULL &&
	    lock->l_exp_refs_target != exp) {
		LCONSOLE_WARN("setting export %p for lock %p which already has export %p\n",
			      exp, lock, lock->l_exp_refs_target);
	}
	if ((lock->l_exp_refs_nr ++) == 0) {
		list_add(&lock->l_exp_refs_link, &exp->exp_locks_list);
		lock->l_exp_refs_target = exp;
	}
	CDEBUG(D_INFO, "lock = %p, export = %p, refs = %u\n",
	       lock, exp, lock->l_exp_refs_nr);
	spin_unlock(&exp->exp_locks_list_guard);
}
EXPORT_SYMBOL(__class_export_add_lock_ref);

void __class_export_del_lock_ref(struct obd_export *exp, struct ldlm_lock *lock)
{
	spin_lock(&exp->exp_locks_list_guard);
	LASSERT(lock->l_exp_refs_nr > 0);
	if (lock->l_exp_refs_target != exp) {
		LCONSOLE_WARN("lock %p, mismatching export pointers: %p, %p\n",
			      lock, lock->l_exp_refs_target, exp);
	}
	if (-- lock->l_exp_refs_nr == 0) {
		list_del_init(&lock->l_exp_refs_link);
		lock->l_exp_refs_target = NULL;
	}
	CDEBUG(D_INFO, "lock = %p, export = %p, refs = %u\n",
	       lock, exp, lock->l_exp_refs_nr);
	spin_unlock(&exp->exp_locks_list_guard);
}
EXPORT_SYMBOL(__class_export_del_lock_ref);
#endif

/* A connection defines an export context in which preallocation can
   be managed. This releases the export pointer reference, and returns
   the export handle, so the export refcount is 1 when this function
   returns. */
int class_connect(struct lustre_handle *conn, struct obd_device *obd,
		  struct obd_uuid *cluuid)
{
	struct obd_export *export;
	LASSERT(conn != NULL);
	LASSERT(obd != NULL);
	LASSERT(cluuid != NULL);

	export = class_new_export(obd, cluuid);
	if (IS_ERR(export))
		return PTR_ERR(export);

	conn->cookie = export->exp_handle.h_cookie;
	class_export_put(export);

	CDEBUG(D_IOCTL, "connect: client %s, cookie %#llx\n",
	       cluuid->uuid, conn->cookie);
	return 0;
}
EXPORT_SYMBOL(class_connect);

/* if export is involved in recovery then clean up related things */
static void class_export_recovery_cleanup(struct obd_export *exp)
{
	struct obd_device *obd = exp->exp_obd;

	spin_lock(&obd->obd_recovery_task_lock);
	if (exp->exp_delayed)
		obd->obd_delayed_clients--;
	if (obd->obd_recovering) {
		if (exp->exp_in_recovery) {
			spin_lock(&exp->exp_lock);
			exp->exp_in_recovery = 0;
			spin_unlock(&exp->exp_lock);
			LASSERT_ATOMIC_POS(&obd->obd_connected_clients);
			atomic_dec(&obd->obd_connected_clients);
		}

		/* if called during recovery then should update
		 * obd_stale_clients counter,
		 * lightweight exports are not counted */
		if (exp->exp_failed &&
		    (exp_connect_flags(exp) & OBD_CONNECT_LIGHTWEIGHT) == 0)
			exp->exp_obd->obd_stale_clients++;
	}
	spin_unlock(&obd->obd_recovery_task_lock);

	spin_lock(&exp->exp_lock);
	/** Cleanup req replay fields */
	if (exp->exp_req_replay_needed) {
		exp->exp_req_replay_needed = 0;

		LASSERT(atomic_read(&obd->obd_req_replay_clients));
		atomic_dec(&obd->obd_req_replay_clients);
	}

	/** Cleanup lock replay data */
	if (exp->exp_lock_replay_needed) {
		exp->exp_lock_replay_needed = 0;

		LASSERT(atomic_read(&obd->obd_lock_replay_clients));
		atomic_dec(&obd->obd_lock_replay_clients);
	}
	spin_unlock(&exp->exp_lock);
}

/* This function removes 1-3 references from the export:
 * 1 - for export pointer passed
 * and if disconnect really need
 * 2 - removing from hash
 * 3 - in client_unlink_export
 * The export pointer passed to this function can destroyed */
int class_disconnect(struct obd_export *export)
{
	int already_disconnected;

	if (export == NULL) {
		CWARN("attempting to free NULL export %p\n", export);
		return -EINVAL;
	}

	spin_lock(&export->exp_lock);
	already_disconnected = export->exp_disconnected;
	export->exp_disconnected = 1;
	spin_unlock(&export->exp_lock);

	/* class_cleanup(), abort_recovery(), and class_fail_export()
	 * all end up in here, and if any of them race we shouldn't
	 * call extra class_export_puts(). */
	if (already_disconnected) {
		LASSERT(hlist_unhashed(&export->exp_nid_hash));
		goto no_disconn;
	}

	CDEBUG(D_IOCTL, "disconnect: cookie %#llx\n",
	       export->exp_handle.h_cookie);

	if (!hlist_unhashed(&export->exp_nid_hash))
		cfs_hash_del(export->exp_obd->obd_nid_hash,
			     &export->exp_connection->c_peer.nid,
			     &export->exp_nid_hash);

	class_export_recovery_cleanup(export);
	class_unlink_export(export);
no_disconn:
	class_export_put(export);
	return 0;
}
EXPORT_SYMBOL(class_disconnect);

/* Return non-zero for a fully connected export */
int class_connected_export(struct obd_export *exp)
{
	if (exp) {
		int connected;
		spin_lock(&exp->exp_lock);
		connected = exp->exp_conn_cnt > 0;
		spin_unlock(&exp->exp_lock);
		return connected;
	}
	return 0;
}
EXPORT_SYMBOL(class_connected_export);

static void class_disconnect_export_list(struct list_head *list,
					 enum obd_option flags)
{
	int rc;
	struct obd_export *exp;

	/* It's possible that an export may disconnect itself, but
	 * nothing else will be added to this list. */
	while (!list_empty(list)) {
		exp = list_entry(list->next, struct obd_export,
				     exp_obd_chain);
		/* need for safe call CDEBUG after obd_disconnect */
		class_export_get(exp);

		spin_lock(&exp->exp_lock);
		exp->exp_flags = flags;
		spin_unlock(&exp->exp_lock);

		if (obd_uuid_equals(&exp->exp_client_uuid,
				    &exp->exp_obd->obd_uuid)) {
			CDEBUG(D_HA,
			       "exp %p export uuid == obd uuid, don't discon\n",
			       exp);
			/* Need to delete this now so we don't end up pointing
			 * to work_list later when this export is cleaned up. */
			list_del_init(&exp->exp_obd_chain);
			class_export_put(exp);
			continue;
		}

		class_export_get(exp);
		CDEBUG(D_HA, "%s: disconnecting export at %s (%p), last request at " CFS_TIME_T "\n",
		       exp->exp_obd->obd_name, obd_export_nid2str(exp),
		       exp, exp->exp_last_request_time);
		/* release one export reference anyway */
		rc = obd_disconnect(exp);

		CDEBUG(D_HA, "disconnected export at %s (%p): rc %d\n",
		       obd_export_nid2str(exp), exp, rc);
		class_export_put(exp);
	}
}

void class_disconnect_exports(struct obd_device *obd)
{
	struct list_head work_list;

	/* Move all of the exports from obd_exports to a work list, en masse. */
	INIT_LIST_HEAD(&work_list);
	spin_lock(&obd->obd_dev_lock);
	list_splice_init(&obd->obd_exports, &work_list);
	list_splice_init(&obd->obd_delayed_exports, &work_list);
	spin_unlock(&obd->obd_dev_lock);

	if (!list_empty(&work_list)) {
		CDEBUG(D_HA, "OBD device %d (%p) has exports, disconnecting them\n",
		       obd->obd_minor, obd);
		class_disconnect_export_list(&work_list,
					     exp_flags_from_obd(obd));
	} else
		CDEBUG(D_HA, "OBD device %d (%p) has no exports\n",
		       obd->obd_minor, obd);
}
EXPORT_SYMBOL(class_disconnect_exports);

/* Remove exports that have not completed recovery.
 */
void class_disconnect_stale_exports(struct obd_device *obd,
				    int (*test_export)(struct obd_export *))
{
	struct list_head work_list;
	struct obd_export *exp, *n;
	int evicted = 0;

	INIT_LIST_HEAD(&work_list);
	spin_lock(&obd->obd_dev_lock);
	list_for_each_entry_safe(exp, n, &obd->obd_exports,
				     exp_obd_chain) {
		/* don't count self-export as client */
		if (obd_uuid_equals(&exp->exp_client_uuid,
				    &exp->exp_obd->obd_uuid))
			continue;

		/* don't evict clients which have no slot in last_rcvd
		 * (e.g. lightweight connection) */
		if (exp->exp_target_data.ted_lr_idx == -1)
			continue;

		spin_lock(&exp->exp_lock);
		if (exp->exp_failed || test_export(exp)) {
			spin_unlock(&exp->exp_lock);
			continue;
		}
		exp->exp_failed = 1;
		spin_unlock(&exp->exp_lock);

		list_move(&exp->exp_obd_chain, &work_list);
		evicted++;
		CDEBUG(D_HA, "%s: disconnect stale client %s@%s\n",
		       obd->obd_name, exp->exp_client_uuid.uuid,
		       exp->exp_connection == NULL ? "<unknown>" :
		       libcfs_nid2str(exp->exp_connection->c_peer.nid));
		print_export_data(exp, "EVICTING", 0);
	}
	spin_unlock(&obd->obd_dev_lock);

	if (evicted)
		LCONSOLE_WARN("%s: disconnecting %d stale clients\n",
			      obd->obd_name, evicted);

	class_disconnect_export_list(&work_list, exp_flags_from_obd(obd) |
						 OBD_OPT_ABORT_RECOV);
}
EXPORT_SYMBOL(class_disconnect_stale_exports);

void class_fail_export(struct obd_export *exp)
{
	int rc, already_failed;

	spin_lock(&exp->exp_lock);
	already_failed = exp->exp_failed;
	exp->exp_failed = 1;
	spin_unlock(&exp->exp_lock);

	if (already_failed) {
		CDEBUG(D_HA, "disconnecting dead export %p/%s; skipping\n",
		       exp, exp->exp_client_uuid.uuid);
		return;
	}

	CDEBUG(D_HA, "disconnecting export %p/%s\n",
	       exp, exp->exp_client_uuid.uuid);

	if (obd_dump_on_timeout)
		libcfs_debug_dumplog();

	/* need for safe call CDEBUG after obd_disconnect */
	class_export_get(exp);

	/* Most callers into obd_disconnect are removing their own reference
	 * (request, for example) in addition to the one from the hash table.
	 * We don't have such a reference here, so make one. */
	class_export_get(exp);
	rc = obd_disconnect(exp);
	if (rc)
		CERROR("disconnecting export %p failed: %d\n", exp, rc);
	else
		CDEBUG(D_HA, "disconnected export %p/%s\n",
		       exp, exp->exp_client_uuid.uuid);
	class_export_put(exp);
}
EXPORT_SYMBOL(class_fail_export);

char *obd_export_nid2str(struct obd_export *exp)
{
	if (exp->exp_connection != NULL)
		return libcfs_nid2str(exp->exp_connection->c_peer.nid);

	return "(no nid)";
}
EXPORT_SYMBOL(obd_export_nid2str);

int obd_export_evict_by_nid(struct obd_device *obd, const char *nid)
{
	struct cfs_hash *nid_hash;
	struct obd_export *doomed_exp = NULL;
	int exports_evicted = 0;

	lnet_nid_t nid_key = libcfs_str2nid((char *)nid);

	spin_lock(&obd->obd_dev_lock);
	/* umount has run already, so evict thread should leave
	 * its task to umount thread now */
	if (obd->obd_stopping) {
		spin_unlock(&obd->obd_dev_lock);
		return exports_evicted;
	}
	nid_hash = obd->obd_nid_hash;
	cfs_hash_getref(nid_hash);
	spin_unlock(&obd->obd_dev_lock);

	do {
		doomed_exp = cfs_hash_lookup(nid_hash, &nid_key);
		if (doomed_exp == NULL)
			break;

		LASSERTF(doomed_exp->exp_connection->c_peer.nid == nid_key,
			 "nid %s found, wanted nid %s, requested nid %s\n",
			 obd_export_nid2str(doomed_exp),
			 libcfs_nid2str(nid_key), nid);
		LASSERTF(doomed_exp != obd->obd_self_export,
			 "self-export is hashed by NID?\n");
		exports_evicted++;
		LCONSOLE_WARN("%s: evicting %s (at %s) by administrative request\n",
			      obd->obd_name,
			      obd_uuid2str(&doomed_exp->exp_client_uuid),
			      obd_export_nid2str(doomed_exp));
		class_fail_export(doomed_exp);
		class_export_put(doomed_exp);
	} while (1);

	cfs_hash_putref(nid_hash);

	if (!exports_evicted)
		CDEBUG(D_HA,
		       "%s: can't disconnect NID '%s': no exports found\n",
		       obd->obd_name, nid);
	return exports_evicted;
}
EXPORT_SYMBOL(obd_export_evict_by_nid);

int obd_export_evict_by_uuid(struct obd_device *obd, const char *uuid)
{
	struct cfs_hash *uuid_hash;
	struct obd_export *doomed_exp = NULL;
	struct obd_uuid doomed_uuid;
	int exports_evicted = 0;

	spin_lock(&obd->obd_dev_lock);
	if (obd->obd_stopping) {
		spin_unlock(&obd->obd_dev_lock);
		return exports_evicted;
	}
	uuid_hash = obd->obd_uuid_hash;
	cfs_hash_getref(uuid_hash);
	spin_unlock(&obd->obd_dev_lock);

	obd_str2uuid(&doomed_uuid, uuid);
	if (obd_uuid_equals(&doomed_uuid, &obd->obd_uuid)) {
		CERROR("%s: can't evict myself\n", obd->obd_name);
		cfs_hash_putref(uuid_hash);
		return exports_evicted;
	}

	doomed_exp = cfs_hash_lookup(uuid_hash, &doomed_uuid);

	if (doomed_exp == NULL) {
		CERROR("%s: can't disconnect %s: no exports found\n",
		       obd->obd_name, uuid);
	} else {
		CWARN("%s: evicting %s at administrative request\n",
		       obd->obd_name, doomed_exp->exp_client_uuid.uuid);
		class_fail_export(doomed_exp);
		class_export_put(doomed_exp);
		exports_evicted++;
	}
	cfs_hash_putref(uuid_hash);

	return exports_evicted;
}
EXPORT_SYMBOL(obd_export_evict_by_uuid);

#if LUSTRE_TRACKS_LOCK_EXP_REFS
void (*class_export_dump_hook)(struct obd_export*) = NULL;
EXPORT_SYMBOL(class_export_dump_hook);
#endif

static void print_export_data(struct obd_export *exp, const char *status,
			      int locks)
{
	struct ptlrpc_reply_state *rs;
	struct ptlrpc_reply_state *first_reply = NULL;
	int nreplies = 0;

	spin_lock(&exp->exp_lock);
	list_for_each_entry(rs, &exp->exp_outstanding_replies,
				rs_exp_list) {
		if (nreplies == 0)
			first_reply = rs;
		nreplies++;
	}
	spin_unlock(&exp->exp_lock);

	CDEBUG(D_HA, "%s: %s %p %s %s %d (%d %d %d) %d %d %d %d: %p %s %llu\n",
	       exp->exp_obd->obd_name, status, exp, exp->exp_client_uuid.uuid,
	       obd_export_nid2str(exp), atomic_read(&exp->exp_refcount),
	       atomic_read(&exp->exp_rpc_count),
	       atomic_read(&exp->exp_cb_count),
	       atomic_read(&exp->exp_locks_count),
	       exp->exp_disconnected, exp->exp_delayed, exp->exp_failed,
	       nreplies, first_reply, nreplies > 3 ? "..." : "",
	       exp->exp_last_committed);
#if LUSTRE_TRACKS_LOCK_EXP_REFS
	if (locks && class_export_dump_hook != NULL)
		class_export_dump_hook(exp);
#endif
}

void dump_exports(struct obd_device *obd, int locks)
{
	struct obd_export *exp;

	spin_lock(&obd->obd_dev_lock);
	list_for_each_entry(exp, &obd->obd_exports, exp_obd_chain)
		print_export_data(exp, "ACTIVE", locks);
	list_for_each_entry(exp, &obd->obd_unlinked_exports, exp_obd_chain)
		print_export_data(exp, "UNLINKED", locks);
	list_for_each_entry(exp, &obd->obd_delayed_exports, exp_obd_chain)
		print_export_data(exp, "DELAYED", locks);
	spin_unlock(&obd->obd_dev_lock);
	spin_lock(&obd_zombie_impexp_lock);
	list_for_each_entry(exp, &obd_zombie_exports, exp_obd_chain)
		print_export_data(exp, "ZOMBIE", locks);
	spin_unlock(&obd_zombie_impexp_lock);
}
EXPORT_SYMBOL(dump_exports);

void obd_exports_barrier(struct obd_device *obd)
{
	int waited = 2;
	LASSERT(list_empty(&obd->obd_exports));
	spin_lock(&obd->obd_dev_lock);
	while (!list_empty(&obd->obd_unlinked_exports)) {
		spin_unlock(&obd->obd_dev_lock);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(cfs_time_seconds(waited));
		if (waited > 5 && IS_PO2(waited)) {
			LCONSOLE_WARN("%s is waiting for obd_unlinked_exports more than %d seconds. The obd refcount = %d. Is it stuck?\n",
				      obd->obd_name, waited,
				      atomic_read(&obd->obd_refcount));
			dump_exports(obd, 1);
		}
		waited *= 2;
		spin_lock(&obd->obd_dev_lock);
	}
	spin_unlock(&obd->obd_dev_lock);
}
EXPORT_SYMBOL(obd_exports_barrier);

/* Total amount of zombies to be destroyed */
static int zombies_count;

/**
 * kill zombie imports and exports
 */
void obd_zombie_impexp_cull(void)
{
	struct obd_import *import;
	struct obd_export *export;

	do {
		spin_lock(&obd_zombie_impexp_lock);

		import = NULL;
		if (!list_empty(&obd_zombie_imports)) {
			import = list_entry(obd_zombie_imports.next,
						struct obd_import,
						imp_zombie_chain);
			list_del_init(&import->imp_zombie_chain);
		}

		export = NULL;
		if (!list_empty(&obd_zombie_exports)) {
			export = list_entry(obd_zombie_exports.next,
						struct obd_export,
						exp_obd_chain);
			list_del_init(&export->exp_obd_chain);
		}

		spin_unlock(&obd_zombie_impexp_lock);

		if (import != NULL) {
			class_import_destroy(import);
			spin_lock(&obd_zombie_impexp_lock);
			zombies_count--;
			spin_unlock(&obd_zombie_impexp_lock);
		}

		if (export != NULL) {
			class_export_destroy(export);
			spin_lock(&obd_zombie_impexp_lock);
			zombies_count--;
			spin_unlock(&obd_zombie_impexp_lock);
		}

		cond_resched();
	} while (import != NULL || export != NULL);
}

static struct completion	obd_zombie_start;
static struct completion	obd_zombie_stop;
static unsigned long		obd_zombie_flags;
static wait_queue_head_t		obd_zombie_waitq;
static pid_t			obd_zombie_pid;

enum {
	OBD_ZOMBIE_STOP		= 0x0001,
};

/**
 * check for work for kill zombie import/export thread.
 */
static int obd_zombie_impexp_check(void *arg)
{
	int rc;

	spin_lock(&obd_zombie_impexp_lock);
	rc = (zombies_count == 0) &&
	     !test_bit(OBD_ZOMBIE_STOP, &obd_zombie_flags);
	spin_unlock(&obd_zombie_impexp_lock);

	return rc;
}

/**
 * Add export to the obd_zombie thread and notify it.
 */
static void obd_zombie_export_add(struct obd_export *exp)
{
	spin_lock(&exp->exp_obd->obd_dev_lock);
	LASSERT(!list_empty(&exp->exp_obd_chain));
	list_del_init(&exp->exp_obd_chain);
	spin_unlock(&exp->exp_obd->obd_dev_lock);
	spin_lock(&obd_zombie_impexp_lock);
	zombies_count++;
	list_add(&exp->exp_obd_chain, &obd_zombie_exports);
	spin_unlock(&obd_zombie_impexp_lock);

	obd_zombie_impexp_notify();
}

/**
 * Add import to the obd_zombie thread and notify it.
 */
static void obd_zombie_import_add(struct obd_import *imp)
{
	LASSERT(imp->imp_sec == NULL);
	LASSERT(imp->imp_rq_pool == NULL);
	spin_lock(&obd_zombie_impexp_lock);
	LASSERT(list_empty(&imp->imp_zombie_chain));
	zombies_count++;
	list_add(&imp->imp_zombie_chain, &obd_zombie_imports);
	spin_unlock(&obd_zombie_impexp_lock);

	obd_zombie_impexp_notify();
}

/**
 * notify import/export destroy thread about new zombie.
 */
static void obd_zombie_impexp_notify(void)
{
	/*
	 * Make sure obd_zombie_impexp_thread get this notification.
	 * It is possible this signal only get by obd_zombie_barrier, and
	 * barrier gulps this notification and sleeps away and hangs ensues
	 */
	wake_up_all(&obd_zombie_waitq);
}

/**
 * check whether obd_zombie is idle
 */
static int obd_zombie_is_idle(void)
{
	int rc;

	LASSERT(!test_bit(OBD_ZOMBIE_STOP, &obd_zombie_flags));
	spin_lock(&obd_zombie_impexp_lock);
	rc = (zombies_count == 0);
	spin_unlock(&obd_zombie_impexp_lock);
	return rc;
}

/**
 * wait when obd_zombie import/export queues become empty
 */
void obd_zombie_barrier(void)
{
	struct l_wait_info lwi = { 0 };

	if (obd_zombie_pid == current_pid())
		/* don't wait for myself */
		return;
	l_wait_event(obd_zombie_waitq, obd_zombie_is_idle(), &lwi);
}
EXPORT_SYMBOL(obd_zombie_barrier);


/**
 * destroy zombie export/import thread.
 */
static int obd_zombie_impexp_thread(void *unused)
{
	unshare_fs_struct();
	complete(&obd_zombie_start);

	obd_zombie_pid = current_pid();

	while (!test_bit(OBD_ZOMBIE_STOP, &obd_zombie_flags)) {
		struct l_wait_info lwi = { 0 };

		l_wait_event(obd_zombie_waitq,
			     !obd_zombie_impexp_check(NULL), &lwi);
		obd_zombie_impexp_cull();

		/*
		 * Notify obd_zombie_barrier callers that queues
		 * may be empty.
		 */
		wake_up(&obd_zombie_waitq);
	}

	complete(&obd_zombie_stop);

	return 0;
}


/**
 * start destroy zombie import/export thread
 */
int obd_zombie_impexp_init(void)
{
	struct task_struct *task;

	INIT_LIST_HEAD(&obd_zombie_imports);
	INIT_LIST_HEAD(&obd_zombie_exports);
	spin_lock_init(&obd_zombie_impexp_lock);
	init_completion(&obd_zombie_start);
	init_completion(&obd_zombie_stop);
	init_waitqueue_head(&obd_zombie_waitq);
	obd_zombie_pid = 0;

	task = kthread_run(obd_zombie_impexp_thread, NULL, "obd_zombid");
	if (IS_ERR(task))
		return PTR_ERR(task);

	wait_for_completion(&obd_zombie_start);
	return 0;
}
/**
 * stop destroy zombie import/export thread
 */
void obd_zombie_impexp_stop(void)
{
	set_bit(OBD_ZOMBIE_STOP, &obd_zombie_flags);
	obd_zombie_impexp_notify();
	wait_for_completion(&obd_zombie_stop);
}

/***** Kernel-userspace comm helpers *******/

/* Get length of entire message, including header */
int kuc_len(int payload_len)
{
	return sizeof(struct kuc_hdr) + payload_len;
}
EXPORT_SYMBOL(kuc_len);

/* Get a pointer to kuc header, given a ptr to the payload
 * @param p Pointer to payload area
 * @returns Pointer to kuc header
 */
struct kuc_hdr *kuc_ptr(void *p)
{
	struct kuc_hdr *lh = ((struct kuc_hdr *)p) - 1;
	LASSERT(lh->kuc_magic == KUC_MAGIC);
	return lh;
}
EXPORT_SYMBOL(kuc_ptr);

/* Test if payload is part of kuc message
 * @param p Pointer to payload area
 * @returns boolean
 */
int kuc_ispayload(void *p)
{
	struct kuc_hdr *kh = ((struct kuc_hdr *)p) - 1;

	if (kh->kuc_magic == KUC_MAGIC)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(kuc_ispayload);

/* Alloc space for a message, and fill in header
 * @return Pointer to payload area
 */
void *kuc_alloc(int payload_len, int transport, int type)
{
	struct kuc_hdr *lh;
	int len = kuc_len(payload_len);

	lh = kzalloc(len, GFP_NOFS);
	if (!lh)
		return ERR_PTR(-ENOMEM);

	lh->kuc_magic = KUC_MAGIC;
	lh->kuc_transport = transport;
	lh->kuc_msgtype = type;
	lh->kuc_msglen = len;

	return (void *)(lh + 1);
}
EXPORT_SYMBOL(kuc_alloc);

/* Takes pointer to payload area */
inline void kuc_free(void *p, int payload_len)
{
	struct kuc_hdr *lh = kuc_ptr(p);
	kfree(lh);
}
EXPORT_SYMBOL(kuc_free);
