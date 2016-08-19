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
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LMV
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <asm/div64.h>
#include <linux/seq_file.h>
#include <linux/namei.h>
#include <linux/uaccess.h>

#include "../include/lustre/lustre_idl.h"
#include "../include/obd_support.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_net.h"
#include "../include/obd_class.h"
#include "../include/lustre_lmv.h"
#include "../include/lprocfs_status.h"
#include "../include/cl_object.h"
#include "../include/lustre_lite.h"
#include "../include/lustre_fid.h"
#include "../include/lustre/lustre_ioctl.h"
#include "../include/lustre_kernelcomm.h"
#include "lmv_internal.h"

/* This hash is only for testing purpose */
static inline unsigned int
lmv_hash_all_chars(unsigned int count, const char *name, int namelen)
{
	const unsigned char *p = (const unsigned char *)name;
	unsigned int c = 0;

	while (--namelen >= 0)
		c += p[namelen];

	c = c % count;

	return c;
}

static inline unsigned int
lmv_hash_fnv1a(unsigned int count, const char *name, int namelen)
{
	__u64 hash;

	hash = lustre_hash_fnv_1a_64(name, namelen);

	return do_div(hash, count);
}

int lmv_name_to_stripe_index(__u32 lmv_hash_type, unsigned int stripe_count,
			     const char *name, int namelen)
{
	__u32 hash_type = lmv_hash_type & LMV_HASH_TYPE_MASK;
	int idx;

	LASSERT(namelen > 0);
	if (stripe_count <= 1)
		return 0;

	/* for migrating object, always start from 0 stripe */
	if (lmv_hash_type & LMV_HASH_FLAG_MIGRATION)
		return 0;

	switch (hash_type) {
	case LMV_HASH_TYPE_ALL_CHARS:
		idx = lmv_hash_all_chars(stripe_count, name, namelen);
		break;
	case LMV_HASH_TYPE_FNV_1A_64:
		idx = lmv_hash_fnv1a(stripe_count, name, namelen);
		break;
	default:
		idx = -EBADFD;
		break;
	}

	CDEBUG(D_INFO, "name %.*s hash_type %d idx %d\n", namelen, name,
	       hash_type, idx);

	return idx;
}

static void lmv_activate_target(struct lmv_obd *lmv,
				struct lmv_tgt_desc *tgt,
				int activate)
{
	if (tgt->ltd_active == activate)
		return;

	tgt->ltd_active = activate;
	lmv->desc.ld_active_tgt_count += (activate ? 1 : -1);
}

/**
 * Error codes:
 *
 *  -EINVAL  : UUID can't be found in the LMV's target list
 *  -ENOTCONN: The UUID is found, but the target connection is bad (!)
 *  -EBADF   : The UUID is found, but the OBD of the wrong type (!)
 */
static int lmv_set_mdc_active(struct lmv_obd *lmv, const struct obd_uuid *uuid,
			      int activate)
{
	struct lmv_tgt_desc    *uninitialized_var(tgt);
	struct obd_device      *obd;
	u32		     i;
	int		     rc = 0;

	CDEBUG(D_INFO, "Searching in lmv %p for uuid %s (activate=%d)\n",
	       lmv, uuid->uuid, activate);

	spin_lock(&lmv->lmv_lock);
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		tgt = lmv->tgts[i];
		if (!tgt || !tgt->ltd_exp)
			continue;

		CDEBUG(D_INFO, "Target idx %d is %s conn %#llx\n", i,
		       tgt->ltd_uuid.uuid, tgt->ltd_exp->exp_handle.h_cookie);

		if (obd_uuid_equals(uuid, &tgt->ltd_uuid))
			break;
	}

	if (i == lmv->desc.ld_tgt_count) {
		rc = -EINVAL;
		goto out_lmv_lock;
	}

	obd = class_exp2obd(tgt->ltd_exp);
	if (!obd) {
		rc = -ENOTCONN;
		goto out_lmv_lock;
	}

	CDEBUG(D_INFO, "Found OBD %s=%s device %d (%p) type %s at LMV idx %d\n",
	       obd->obd_name, obd->obd_uuid.uuid, obd->obd_minor, obd,
	       obd->obd_type->typ_name, i);
	LASSERT(strcmp(obd->obd_type->typ_name, LUSTRE_MDC_NAME) == 0);

	if (tgt->ltd_active == activate) {
		CDEBUG(D_INFO, "OBD %p already %sactive!\n", obd,
		       activate ? "" : "in");
		goto out_lmv_lock;
	}

	CDEBUG(D_INFO, "Marking OBD %p %sactive\n", obd,
	       activate ? "" : "in");
	lmv_activate_target(lmv, tgt, activate);

 out_lmv_lock:
	spin_unlock(&lmv->lmv_lock);
	return rc;
}

static struct obd_uuid *lmv_get_uuid(struct obd_export *exp)
{
	struct lmv_obd *lmv = &exp->exp_obd->u.lmv;
	struct lmv_tgt_desc *tgt = lmv->tgts[0];

	return tgt ? obd_get_uuid(tgt->ltd_exp) : NULL;
}

static int lmv_notify(struct obd_device *obd, struct obd_device *watched,
		      enum obd_notify_event ev, void *data)
{
	struct obd_connect_data *conn_data;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct obd_uuid	 *uuid;
	int		      rc = 0;

	if (strcmp(watched->obd_type->typ_name, LUSTRE_MDC_NAME)) {
		CERROR("unexpected notification of %s %s!\n",
		       watched->obd_type->typ_name,
		       watched->obd_name);
		return -EINVAL;
	}

	uuid = &watched->u.cli.cl_target_uuid;
	if (ev == OBD_NOTIFY_ACTIVE || ev == OBD_NOTIFY_INACTIVE) {
		/*
		 * Set MDC as active before notifying the observer, so the
		 * observer can use the MDC normally.
		 */
		rc = lmv_set_mdc_active(lmv, uuid,
					ev == OBD_NOTIFY_ACTIVE);
		if (rc) {
			CERROR("%sactivation of %s failed: %d\n",
			       ev == OBD_NOTIFY_ACTIVE ? "" : "de",
			       uuid->uuid, rc);
			return rc;
		}
	} else if (ev == OBD_NOTIFY_OCD) {
		conn_data = &watched->u.cli.cl_import->imp_connect_data;
		/*
		 * XXX: Make sure that ocd_connect_flags from all targets are
		 * the same. Otherwise one of MDTs runs wrong version or
		 * something like this.  --umka
		 */
		obd->obd_self_export->exp_connect_data = *conn_data;
	}
#if 0
	else if (ev == OBD_NOTIFY_DISCON) {
		/*
		 * For disconnect event, flush fld cache for failout MDS case.
		 */
		fld_client_flush(&lmv->lmv_fld);
	}
#endif
	/*
	 * Pass the notification up the chain.
	 */
	if (obd->obd_observer)
		rc = obd_notify(obd->obd_observer, watched, ev, data);

	return rc;
}

/**
 * This is fake connect function. Its purpose is to initialize lmv and say
 * caller that everything is okay. Real connection will be performed later.
 */
static int lmv_connect(const struct lu_env *env,
		       struct obd_export **exp, struct obd_device *obd,
		       struct obd_uuid *cluuid, struct obd_connect_data *data,
		       void *localdata)
{
	struct lmv_obd	*lmv = &obd->u.lmv;
	struct lustre_handle  conn = { 0 };
	int		    rc = 0;

	/*
	 * We don't want to actually do the underlying connections more than
	 * once, so keep track.
	 */
	lmv->refcount++;
	if (lmv->refcount > 1) {
		*exp = NULL;
		return 0;
	}

	rc = class_connect(&conn, obd, cluuid);
	if (rc) {
		CERROR("class_connection() returned %d\n", rc);
		return rc;
	}

	*exp = class_conn2export(&conn);
	class_export_get(*exp);

	lmv->exp = *exp;
	lmv->connected = 0;
	lmv->cluuid = *cluuid;

	if (data)
		lmv->conn_data = *data;

	lmv->lmv_tgts_kobj = kobject_create_and_add("target_obds",
						    &obd->obd_kobj);
	/*
	 * All real clients should perform actual connection right away, because
	 * it is possible, that LMV will not have opportunity to connect targets
	 * and MDC stuff will be called directly, for instance while reading
	 * ../mdc/../kbytesfree procfs file, etc.
	 */
	if (data && data->ocd_connect_flags & OBD_CONNECT_REAL)
		rc = lmv_check_connect(obd);

	if (rc && lmv->lmv_tgts_kobj)
		kobject_put(lmv->lmv_tgts_kobj);

	return rc;
}

static void lmv_set_timeouts(struct obd_device *obd)
{
	struct lmv_obd	*lmv;
	u32 i;

	lmv = &obd->u.lmv;
	if (lmv->server_timeout == 0)
		return;

	if (lmv->connected == 0)
		return;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		struct lmv_tgt_desc *tgt = lmv->tgts[i];

		tgt = lmv->tgts[i];
		if (!tgt || !tgt->ltd_exp || !tgt->ltd_active)
			continue;

		obd_set_info_async(NULL, tgt->ltd_exp, sizeof(KEY_INTERMDS),
				   KEY_INTERMDS, 0, NULL, NULL);
	}
}

static int lmv_init_ea_size(struct obd_export *exp, int easize,
			    int def_easize, int cookiesize, int def_cookiesize)
{
	struct obd_device   *obd = exp->exp_obd;
	struct lmv_obd      *lmv = &obd->u.lmv;
	u32 i;
	int		  rc = 0;
	int		  change = 0;

	if (lmv->max_easize < easize) {
		lmv->max_easize = easize;
		change = 1;
	}
	if (lmv->max_def_easize < def_easize) {
		lmv->max_def_easize = def_easize;
		change = 1;
	}
	if (lmv->max_cookiesize < cookiesize) {
		lmv->max_cookiesize = cookiesize;
		change = 1;
	}
	if (lmv->max_def_cookiesize < def_cookiesize) {
		lmv->max_def_cookiesize = def_cookiesize;
		change = 1;
	}
	if (change == 0)
		return 0;

	if (lmv->connected == 0)
		return 0;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		struct lmv_tgt_desc *tgt = lmv->tgts[i];

		if (!tgt || !tgt->ltd_exp || !tgt->ltd_active) {
			CWARN("%s: NULL export for %d\n", obd->obd_name, i);
			continue;
		}

		rc = md_init_ea_size(tgt->ltd_exp, easize, def_easize,
				     cookiesize, def_cookiesize);
		if (rc) {
			CERROR("%s: obd_init_ea_size() failed on MDT target %d: rc = %d\n",
			       obd->obd_name, i, rc);
			break;
		}
	}
	return rc;
}

#define MAX_STRING_SIZE 128

static int lmv_connect_mdc(struct obd_device *obd, struct lmv_tgt_desc *tgt)
{
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct obd_uuid	 *cluuid = &lmv->cluuid;
	struct obd_uuid	  lmv_mdc_uuid = { "LMV_MDC_UUID" };
	struct obd_device       *mdc_obd;
	struct obd_export       *mdc_exp;
	struct lu_fld_target     target;
	int		      rc;

	mdc_obd = class_find_client_obd(&tgt->ltd_uuid, LUSTRE_MDC_NAME,
					&obd->obd_uuid);
	if (!mdc_obd) {
		CERROR("target %s not attached\n", tgt->ltd_uuid.uuid);
		return -EINVAL;
	}

	CDEBUG(D_CONFIG, "connect to %s(%s) - %s, %s FOR %s\n",
	       mdc_obd->obd_name, mdc_obd->obd_uuid.uuid,
	       tgt->ltd_uuid.uuid, obd->obd_uuid.uuid, cluuid->uuid);

	if (!mdc_obd->obd_set_up) {
		CERROR("target %s is not set up\n", tgt->ltd_uuid.uuid);
		return -EINVAL;
	}

	rc = obd_connect(NULL, &mdc_exp, mdc_obd, &lmv_mdc_uuid,
			 &lmv->conn_data, NULL);
	if (rc) {
		CERROR("target %s connect error %d\n", tgt->ltd_uuid.uuid, rc);
		return rc;
	}

	/*
	 * Init fid sequence client for this mdc and add new fld target.
	 */
	rc = obd_fid_init(mdc_obd, mdc_exp, LUSTRE_SEQ_METADATA);
	if (rc)
		return rc;

	target.ft_srv = NULL;
	target.ft_exp = mdc_exp;
	target.ft_idx = tgt->ltd_idx;

	fld_client_add_target(&lmv->lmv_fld, &target);

	rc = obd_register_observer(mdc_obd, obd);
	if (rc) {
		obd_disconnect(mdc_exp);
		CERROR("target %s register_observer error %d\n",
		       tgt->ltd_uuid.uuid, rc);
		return rc;
	}

	if (obd->obd_observer) {
		/*
		 * Tell the observer about the new target.
		 */
		rc = obd_notify(obd->obd_observer, mdc_exp->exp_obd,
				OBD_NOTIFY_ACTIVE,
				(void *)(tgt - lmv->tgts[0]));
		if (rc) {
			obd_disconnect(mdc_exp);
			return rc;
		}
	}

	tgt->ltd_active = 1;
	tgt->ltd_exp = mdc_exp;
	lmv->desc.ld_active_tgt_count++;

	md_init_ea_size(tgt->ltd_exp, lmv->max_easize, lmv->max_def_easize,
			lmv->max_cookiesize, lmv->max_def_cookiesize);

	CDEBUG(D_CONFIG, "Connected to %s(%s) successfully (%d)\n",
	       mdc_obd->obd_name, mdc_obd->obd_uuid.uuid,
	       atomic_read(&obd->obd_refcount));

	if (lmv->lmv_tgts_kobj)
		/* Even if we failed to create the link, that's fine */
		rc = sysfs_create_link(lmv->lmv_tgts_kobj, &mdc_obd->obd_kobj,
				       mdc_obd->obd_name);
	return 0;
}

static void lmv_del_target(struct lmv_obd *lmv, int index)
{
	if (!lmv->tgts[index])
		return;

	kfree(lmv->tgts[index]);
	lmv->tgts[index] = NULL;
	return;
}

static int lmv_add_target(struct obd_device *obd, struct obd_uuid *uuidp,
			  __u32 index, int gen)
{
	struct lmv_obd      *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt;
	int orig_tgt_count = 0;
	int		  rc = 0;

	CDEBUG(D_CONFIG, "Target uuid: %s. index %d\n", uuidp->uuid, index);

	mutex_lock(&lmv->lmv_init_mutex);

	if (lmv->desc.ld_tgt_count == 0) {
		struct obd_device *mdc_obd;

		mdc_obd = class_find_client_obd(uuidp, LUSTRE_MDC_NAME,
						&obd->obd_uuid);
		if (!mdc_obd) {
			mutex_unlock(&lmv->lmv_init_mutex);
			CERROR("%s: Target %s not attached: rc = %d\n",
			       obd->obd_name, uuidp->uuid, -EINVAL);
			return -EINVAL;
		}
	}

	if ((index < lmv->tgts_size) && lmv->tgts[index]) {
		tgt = lmv->tgts[index];
		CERROR("%s: UUID %s already assigned at LOV target index %d: rc = %d\n",
		       obd->obd_name,
		       obd_uuid2str(&tgt->ltd_uuid), index, -EEXIST);
		mutex_unlock(&lmv->lmv_init_mutex);
		return -EEXIST;
	}

	if (index >= lmv->tgts_size) {
		/* We need to reallocate the lmv target array. */
		struct lmv_tgt_desc **newtgts, **old = NULL;
		__u32 newsize = 1;
		__u32 oldsize = 0;

		while (newsize < index + 1)
			newsize <<= 1;
		newtgts = kcalloc(newsize, sizeof(*newtgts), GFP_NOFS);
		if (!newtgts) {
			mutex_unlock(&lmv->lmv_init_mutex);
			return -ENOMEM;
		}

		if (lmv->tgts_size) {
			memcpy(newtgts, lmv->tgts,
			       sizeof(*newtgts) * lmv->tgts_size);
			old = lmv->tgts;
			oldsize = lmv->tgts_size;
		}

		lmv->tgts = newtgts;
		lmv->tgts_size = newsize;
		smp_rmb();
		kfree(old);

		CDEBUG(D_CONFIG, "tgts: %p size: %d\n", lmv->tgts,
		       lmv->tgts_size);
	}

	tgt = kzalloc(sizeof(*tgt), GFP_NOFS);
	if (!tgt) {
		mutex_unlock(&lmv->lmv_init_mutex);
		return -ENOMEM;
	}

	mutex_init(&tgt->ltd_fid_mutex);
	tgt->ltd_idx = index;
	tgt->ltd_uuid = *uuidp;
	tgt->ltd_active = 0;
	lmv->tgts[index] = tgt;
	if (index >= lmv->desc.ld_tgt_count) {
		orig_tgt_count = lmv->desc.ld_tgt_count;
		lmv->desc.ld_tgt_count = index + 1;
	}

	if (lmv->connected) {
		rc = lmv_connect_mdc(obd, tgt);
		if (rc) {
			spin_lock(&lmv->lmv_lock);
			if (lmv->desc.ld_tgt_count == index + 1)
				lmv->desc.ld_tgt_count = orig_tgt_count;
			memset(tgt, 0, sizeof(*tgt));
			spin_unlock(&lmv->lmv_lock);
		} else {
			int easize = sizeof(struct lmv_stripe_md) +
				lmv->desc.ld_tgt_count * sizeof(struct lu_fid);
			lmv_init_ea_size(obd->obd_self_export, easize, 0, 0, 0);
		}
	}

	mutex_unlock(&lmv->lmv_init_mutex);
	return rc;
}

int lmv_check_connect(struct obd_device *obd)
{
	struct lmv_obd       *lmv = &obd->u.lmv;
	struct lmv_tgt_desc  *tgt;
	u32 i;
	int		   rc;
	int		   easize;

	if (lmv->connected)
		return 0;

	mutex_lock(&lmv->lmv_init_mutex);
	if (lmv->connected) {
		mutex_unlock(&lmv->lmv_init_mutex);
		return 0;
	}

	if (lmv->desc.ld_tgt_count == 0) {
		mutex_unlock(&lmv->lmv_init_mutex);
		CERROR("%s: no targets configured.\n", obd->obd_name);
		return -EINVAL;
	}

	LASSERT(lmv->tgts);

	if (!lmv->tgts[0]) {
		mutex_unlock(&lmv->lmv_init_mutex);
		CERROR("%s: no target configured for index 0.\n",
		       obd->obd_name);
		return -EINVAL;
	}

	CDEBUG(D_CONFIG, "Time to connect %s to %s\n",
	       lmv->cluuid.uuid, obd->obd_name);

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		tgt = lmv->tgts[i];
		if (!tgt)
			continue;
		rc = lmv_connect_mdc(obd, tgt);
		if (rc)
			goto out_disc;
	}

	lmv_set_timeouts(obd);
	class_export_put(lmv->exp);
	lmv->connected = 1;
	easize = lmv_mds_md_size(lmv->desc.ld_tgt_count, LMV_MAGIC);
	lmv_init_ea_size(obd->obd_self_export, easize, 0, 0, 0);
	mutex_unlock(&lmv->lmv_init_mutex);
	return 0;

 out_disc:
	while (i-- > 0) {
		int rc2;

		tgt = lmv->tgts[i];
		if (!tgt)
			continue;
		tgt->ltd_active = 0;
		if (tgt->ltd_exp) {
			--lmv->desc.ld_active_tgt_count;
			rc2 = obd_disconnect(tgt->ltd_exp);
			if (rc2) {
				CERROR("LMV target %s disconnect on MDC idx %d: error %d\n",
				       tgt->ltd_uuid.uuid, i, rc2);
			}
		}
	}
	class_disconnect(lmv->exp);
	mutex_unlock(&lmv->lmv_init_mutex);
	return rc;
}

static int lmv_disconnect_mdc(struct obd_device *obd, struct lmv_tgt_desc *tgt)
{
	struct lmv_obd	 *lmv = &obd->u.lmv;
	struct obd_device      *mdc_obd;
	int		     rc;

	mdc_obd = class_exp2obd(tgt->ltd_exp);

	if (mdc_obd) {
		mdc_obd->obd_force = obd->obd_force;
		mdc_obd->obd_fail = obd->obd_fail;
		mdc_obd->obd_no_recov = obd->obd_no_recov;

		if (lmv->lmv_tgts_kobj)
			sysfs_remove_link(lmv->lmv_tgts_kobj,
					  mdc_obd->obd_name);
	}

	rc = obd_fid_fini(tgt->ltd_exp->exp_obd);
	if (rc)
		CERROR("Can't finalize fids factory\n");

	CDEBUG(D_INFO, "Disconnected from %s(%s) successfully\n",
	       tgt->ltd_exp->exp_obd->obd_name,
	       tgt->ltd_exp->exp_obd->obd_uuid.uuid);

	obd_register_observer(tgt->ltd_exp->exp_obd, NULL);
	rc = obd_disconnect(tgt->ltd_exp);
	if (rc) {
		if (tgt->ltd_active) {
			CERROR("Target %s disconnect error %d\n",
			       tgt->ltd_uuid.uuid, rc);
		}
	}

	lmv_activate_target(lmv, tgt, 0);
	tgt->ltd_exp = NULL;
	return 0;
}

static int lmv_disconnect(struct obd_export *exp)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct lmv_obd	*lmv = &obd->u.lmv;
	int		    rc;
	u32 i;

	if (!lmv->tgts)
		goto out_local;

	/*
	 * Only disconnect the underlying layers on the final disconnect.
	 */
	lmv->refcount--;
	if (lmv->refcount != 0)
		goto out_local;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (!lmv->tgts[i] || !lmv->tgts[i]->ltd_exp)
			continue;

		lmv_disconnect_mdc(obd, lmv->tgts[i]);
	}

	if (lmv->lmv_tgts_kobj)
		kobject_put(lmv->lmv_tgts_kobj);

out_local:
	/*
	 * This is the case when no real connection is established by
	 * lmv_check_connect().
	 */
	if (!lmv->connected)
		class_export_put(exp);
	rc = class_disconnect(exp);
	if (lmv->refcount == 0)
		lmv->connected = 0;
	return rc;
}

static int lmv_fid2path(struct obd_export *exp, int len, void *karg,
			void __user *uarg)
{
	struct obd_device	*obddev = class_exp2obd(exp);
	struct lmv_obd		*lmv = &obddev->u.lmv;
	struct getinfo_fid2path *gf;
	struct lmv_tgt_desc     *tgt;
	struct getinfo_fid2path *remote_gf = NULL;
	int			remote_gf_size = 0;
	int			rc;

	gf = (struct getinfo_fid2path *)karg;
	tgt = lmv_find_target(lmv, &gf->gf_fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

repeat_fid2path:
	rc = obd_iocontrol(OBD_IOC_FID2PATH, tgt->ltd_exp, len, gf, uarg);
	if (rc != 0 && rc != -EREMOTE)
		goto out_fid2path;

	/* If remote_gf != NULL, it means just building the
	 * path on the remote MDT, copy this path segment to gf
	 */
	if (remote_gf) {
		struct getinfo_fid2path *ori_gf;
		char *ptr;

		ori_gf = (struct getinfo_fid2path *)karg;
		if (strlen(ori_gf->gf_path) +
		    strlen(gf->gf_path) > ori_gf->gf_pathlen) {
			rc = -EOVERFLOW;
			goto out_fid2path;
		}

		ptr = ori_gf->gf_path;

		memmove(ptr + strlen(gf->gf_path) + 1, ptr,
			strlen(ori_gf->gf_path));

		strncpy(ptr, gf->gf_path, strlen(gf->gf_path));
		ptr += strlen(gf->gf_path);
		*ptr = '/';
	}

	CDEBUG(D_INFO, "%s: get path %s "DFID" rec: %llu ln: %u\n",
	       tgt->ltd_exp->exp_obd->obd_name,
	       gf->gf_path, PFID(&gf->gf_fid), gf->gf_recno,
	       gf->gf_linkno);

	if (rc == 0)
		goto out_fid2path;

	/* sigh, has to go to another MDT to do path building further */
	if (!remote_gf) {
		remote_gf_size = sizeof(*remote_gf) + PATH_MAX;
		remote_gf = kzalloc(remote_gf_size, GFP_NOFS);
		if (!remote_gf) {
			rc = -ENOMEM;
			goto out_fid2path;
		}
		remote_gf->gf_pathlen = PATH_MAX;
	}

	if (!fid_is_sane(&gf->gf_fid)) {
		CERROR("%s: invalid FID "DFID": rc = %d\n",
		       tgt->ltd_exp->exp_obd->obd_name,
		       PFID(&gf->gf_fid), -EINVAL);
		rc = -EINVAL;
		goto out_fid2path;
	}

	tgt = lmv_find_target(lmv, &gf->gf_fid);
	if (IS_ERR(tgt)) {
		rc = -EINVAL;
		goto out_fid2path;
	}

	remote_gf->gf_fid = gf->gf_fid;
	remote_gf->gf_recno = -1;
	remote_gf->gf_linkno = -1;
	memset(remote_gf->gf_path, 0, remote_gf->gf_pathlen);
	gf = remote_gf;
	goto repeat_fid2path;

out_fid2path:
	kfree(remote_gf);
	return rc;
}

static int lmv_hsm_req_count(struct lmv_obd *lmv,
			     const struct hsm_user_request *hur,
			     const struct lmv_tgt_desc *tgt_mds)
{
	u32 i, nr = 0;
	struct lmv_tgt_desc    *curr_tgt;

	/* count how many requests must be sent to the given target */
	for (i = 0; i < hur->hur_request.hr_itemcount; i++) {
		curr_tgt = lmv_find_target(lmv, &hur->hur_user_item[i].hui_fid);
		if (obd_uuid_equals(&curr_tgt->ltd_uuid, &tgt_mds->ltd_uuid))
			nr++;
	}
	return nr;
}

static void lmv_hsm_req_build(struct lmv_obd *lmv,
			      struct hsm_user_request *hur_in,
			      const struct lmv_tgt_desc *tgt_mds,
			      struct hsm_user_request *hur_out)
{
	int			i, nr_out;
	struct lmv_tgt_desc    *curr_tgt;

	/* build the hsm_user_request for the given target */
	hur_out->hur_request = hur_in->hur_request;
	nr_out = 0;
	for (i = 0; i < hur_in->hur_request.hr_itemcount; i++) {
		curr_tgt = lmv_find_target(lmv,
					   &hur_in->hur_user_item[i].hui_fid);
		if (obd_uuid_equals(&curr_tgt->ltd_uuid, &tgt_mds->ltd_uuid)) {
			hur_out->hur_user_item[nr_out] =
				hur_in->hur_user_item[i];
			nr_out++;
		}
	}
	hur_out->hur_request.hr_itemcount = nr_out;
	memcpy(hur_data(hur_out), hur_data(hur_in),
	       hur_in->hur_request.hr_data_len);
}

static int lmv_hsm_ct_unregister(struct lmv_obd *lmv, unsigned int cmd, int len,
				 struct lustre_kernelcomm *lk,
				 void __user *uarg)
{
	int rc = 0;
	__u32 i;

	/* unregister request (call from llapi_hsm_copytool_fini) */
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		struct lmv_tgt_desc *tgt = lmv->tgts[i];

		if (!tgt || !tgt->ltd_exp)
			continue;

		/* best effort: try to clean as much as possible
		 * (continue on error)
		 */
		obd_iocontrol(cmd, lmv->tgts[i]->ltd_exp, len, lk, uarg);
	}

	/* Whatever the result, remove copytool from kuc groups.
	 * Unreached coordinators will get EPIPE on next requests
	 * and will unregister automatically.
	 */
	rc = libcfs_kkuc_group_rem(lk->lk_uid, lk->lk_group);

	return rc;
}

static int lmv_hsm_ct_register(struct lmv_obd *lmv, unsigned int cmd, int len,
			       struct lustre_kernelcomm *lk, void __user *uarg)
{
	struct file *filp;
	__u32 i, j;
	int err, rc = 0;
	bool any_set = false;
	struct kkuc_ct_data kcd = { 0 };

	/* All or nothing: try to register to all MDS.
	 * In case of failure, unregister from previous MDS,
	 * except if it because of inactive target.
	 */
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		struct lmv_tgt_desc *tgt = lmv->tgts[i];

		if (!tgt || !tgt->ltd_exp)
			continue;

		err = obd_iocontrol(cmd, tgt->ltd_exp, len, lk, uarg);
		if (err) {
			if (tgt->ltd_active) {
				/* permanent error */
				CERROR("error: iocontrol MDC %s on MDTidx %d cmd %x: err = %d\n",
				       tgt->ltd_uuid.uuid, i, cmd, err);
				rc = err;
				lk->lk_flags |= LK_FLG_STOP;
				/* unregister from previous MDS */
				for (j = 0; j < i; j++) {
					tgt = lmv->tgts[j];

					if (!tgt || !tgt->ltd_exp)
						continue;
					obd_iocontrol(cmd, tgt->ltd_exp, len,
						      lk, uarg);
				}
				return rc;
			}
			/* else: transient error.
			 * kuc will register to the missing MDT when it is back
			 */
		} else {
			any_set = true;
		}
	}

	if (!any_set)
		/* no registration done: return error */
		return -ENOTCONN;

	/* at least one registration done, with no failure */
	filp = fget(lk->lk_wfd);
	if (!filp)
		return -EBADF;

	kcd.kcd_magic = KKUC_CT_DATA_MAGIC;
	kcd.kcd_uuid = lmv->cluuid;
	kcd.kcd_archive = lk->lk_data;

	rc = libcfs_kkuc_group_add(filp, lk->lk_uid, lk->lk_group,
				   &kcd, sizeof(kcd));
	if (rc) {
		if (filp)
			fput(filp);
	}

	return rc;
}

static int lmv_iocontrol(unsigned int cmd, struct obd_export *exp,
			 int len, void *karg, void __user *uarg)
{
	struct obd_device    *obddev = class_exp2obd(exp);
	struct lmv_obd       *lmv = &obddev->u.lmv;
	struct lmv_tgt_desc *tgt = NULL;
	u32 i = 0;
	int		   rc = 0;
	int		   set = 0;
	u32 count = lmv->desc.ld_tgt_count;

	if (count == 0)
		return -ENOTTY;

	switch (cmd) {
	case IOC_OBD_STATFS: {
		struct obd_ioctl_data *data = karg;
		struct obd_device *mdc_obd;
		struct obd_statfs stat_buf = {0};
		__u32 index;

		memcpy(&index, data->ioc_inlbuf2, sizeof(__u32));
		if (index >= count)
			return -ENODEV;

		tgt = lmv->tgts[index];
		if (!tgt || !tgt->ltd_active)
			return -ENODATA;

		mdc_obd = class_exp2obd(tgt->ltd_exp);
		if (!mdc_obd)
			return -EINVAL;

		/* copy UUID */
		if (copy_to_user(data->ioc_pbuf2, obd2cli_tgt(mdc_obd),
				 min((int)data->ioc_plen2,
				     (int)sizeof(struct obd_uuid))))
			return -EFAULT;

		rc = obd_statfs(NULL, tgt->ltd_exp, &stat_buf,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				0);
		if (rc)
			return rc;
		if (copy_to_user(data->ioc_pbuf1, &stat_buf,
				 min((int)data->ioc_plen1,
				     (int)sizeof(stat_buf))))
			return -EFAULT;
		break;
	}
	case OBD_IOC_QUOTACTL: {
		struct if_quotactl *qctl = karg;
		struct obd_quotactl *oqctl;

		if (qctl->qc_valid == QC_MDTIDX) {
			if (count <= qctl->qc_idx)
				return -EINVAL;

			tgt = lmv->tgts[qctl->qc_idx];
			if (!tgt || !tgt->ltd_exp)
				return -EINVAL;
		} else if (qctl->qc_valid == QC_UUID) {
			for (i = 0; i < count; i++) {
				tgt = lmv->tgts[i];
				if (!tgt)
					continue;
				if (!obd_uuid_equals(&tgt->ltd_uuid,
						     &qctl->obd_uuid))
					continue;

				if (!tgt->ltd_exp)
					return -EINVAL;

				break;
			}
		} else {
			return -EINVAL;
		}

		if (i >= count)
			return -EAGAIN;

		LASSERT(tgt && tgt->ltd_exp);
		oqctl = kzalloc(sizeof(*oqctl), GFP_NOFS);
		if (!oqctl)
			return -ENOMEM;

		QCTL_COPY(oqctl, qctl);
		rc = obd_quotactl(tgt->ltd_exp, oqctl);
		if (rc == 0) {
			QCTL_COPY(qctl, oqctl);
			qctl->qc_valid = QC_MDTIDX;
			qctl->obd_uuid = tgt->ltd_uuid;
		}
		kfree(oqctl);
		break;
	}
	case OBD_IOC_CHANGELOG_SEND:
	case OBD_IOC_CHANGELOG_CLEAR: {
		struct ioc_changelog *icc = karg;

		if (icc->icc_mdtindex >= count)
			return -ENODEV;

		tgt = lmv->tgts[icc->icc_mdtindex];
		if (!tgt || !tgt->ltd_exp || !tgt->ltd_active)
			return -ENODEV;
		rc = obd_iocontrol(cmd, tgt->ltd_exp, sizeof(*icc), icc, NULL);
		break;
	}
	case LL_IOC_GET_CONNECT_FLAGS: {
		tgt = lmv->tgts[0];

		if (!tgt || !tgt->ltd_exp)
			return -ENODATA;
		rc = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
		break;
	}
	case OBD_IOC_FID2PATH: {
		rc = lmv_fid2path(exp, len, karg, uarg);
		break;
	}
	case LL_IOC_HSM_STATE_GET:
	case LL_IOC_HSM_STATE_SET:
	case LL_IOC_HSM_ACTION: {
		struct md_op_data	*op_data = karg;

		tgt = lmv_find_target(lmv, &op_data->op_fid1);
		if (IS_ERR(tgt))
			return PTR_ERR(tgt);

		if (!tgt->ltd_exp)
			return -EINVAL;

		rc = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
		break;
	}
	case LL_IOC_HSM_PROGRESS: {
		const struct hsm_progress_kernel *hpk = karg;

		tgt = lmv_find_target(lmv, &hpk->hpk_fid);
		if (IS_ERR(tgt))
			return PTR_ERR(tgt);
		rc = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
		break;
	}
	case LL_IOC_HSM_REQUEST: {
		struct hsm_user_request *hur = karg;
		unsigned int reqcount = hur->hur_request.hr_itemcount;

		if (reqcount == 0)
			return 0;

		/* if the request is about a single fid
		 * or if there is a single MDS, no need to split
		 * the request.
		 */
		if (reqcount == 1 || count == 1) {
			tgt = lmv_find_target(lmv,
					      &hur->hur_user_item[0].hui_fid);
			if (IS_ERR(tgt))
				return PTR_ERR(tgt);
			rc = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
		} else {
			/* split fid list to their respective MDS */
			for (i = 0; i < count; i++) {
				unsigned int		nr, reqlen;
				int			rc1;
				struct hsm_user_request *req;

				tgt = lmv->tgts[i];
				if (!tgt || !tgt->ltd_exp)
					continue;

				nr = lmv_hsm_req_count(lmv, hur, tgt);
				if (nr == 0) /* nothing for this MDS */
					continue;

				/* build a request with fids for this MDS */
				reqlen = offsetof(typeof(*hur),
						  hur_user_item[nr])
					 + hur->hur_request.hr_data_len;
				req = libcfs_kvzalloc(reqlen, GFP_NOFS);
				if (!req)
					return -ENOMEM;

				lmv_hsm_req_build(lmv, hur, tgt, req);

				rc1 = obd_iocontrol(cmd, tgt->ltd_exp, reqlen,
						    req, uarg);
				if (rc1 != 0 && rc == 0)
					rc = rc1;
				kvfree(req);
			}
		}
		break;
	}
	case LL_IOC_LOV_SWAP_LAYOUTS: {
		struct md_op_data	*op_data = karg;
		struct lmv_tgt_desc	*tgt1, *tgt2;

		tgt1 = lmv_find_target(lmv, &op_data->op_fid1);
		if (IS_ERR(tgt1))
			return PTR_ERR(tgt1);

		tgt2 = lmv_find_target(lmv, &op_data->op_fid2);
		if (IS_ERR(tgt2))
			return PTR_ERR(tgt2);

		if (!tgt1->ltd_exp || !tgt2->ltd_exp)
			return -EINVAL;

		/* only files on same MDT can have their layouts swapped */
		if (tgt1->ltd_idx != tgt2->ltd_idx)
			return -EPERM;

		rc = obd_iocontrol(cmd, tgt1->ltd_exp, len, karg, uarg);
		break;
	}
	case LL_IOC_HSM_CT_START: {
		struct lustre_kernelcomm *lk = karg;

		if (lk->lk_flags & LK_FLG_STOP)
			rc = lmv_hsm_ct_unregister(lmv, cmd, len, lk, uarg);
		else
			rc = lmv_hsm_ct_register(lmv, cmd, len, lk, uarg);
		break;
	}
	default:
		for (i = 0; i < count; i++) {
			struct obd_device *mdc_obd;
			int err;

			tgt = lmv->tgts[i];
			if (!tgt || !tgt->ltd_exp)
				continue;
			/* ll_umount_begin() sets force flag but for lmv, not
			 * mdc. Let's pass it through
			 */
			mdc_obd = class_exp2obd(tgt->ltd_exp);
			mdc_obd->obd_force = obddev->obd_force;
			err = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
			if (err == -ENODATA && cmd == OBD_IOC_POLL_QUOTACHECK) {
				return err;
			} else if (err) {
				if (tgt->ltd_active) {
					CERROR("error: iocontrol MDC %s on MDTidx %d cmd %x: err = %d\n",
					       tgt->ltd_uuid.uuid, i, cmd, err);
					if (!rc)
						rc = err;
				}
			} else {
				set = 1;
			}
		}
		if (!set && !rc)
			rc = -EIO;
	}
	return rc;
}

/**
 * This is _inode_ placement policy function (not name).
 */
static int lmv_placement_policy(struct obd_device *obd,
				struct md_op_data *op_data, u32 *mds)
{
	struct lmv_obd	  *lmv = &obd->u.lmv;

	LASSERT(mds);

	if (lmv->desc.ld_tgt_count == 1) {
		*mds = 0;
		return 0;
	}

	/**
	 * If stripe_offset is provided during setdirstripe
	 * (setdirstripe -i xx), xx MDS will be chosen.
	 */
	if (op_data->op_cli_flags & CLI_SET_MEA && op_data->op_data) {
		struct lmv_user_md *lum;

		lum = op_data->op_data;
		if (le32_to_cpu(lum->lum_stripe_offset) != (__u32)-1) {
			*mds = le32_to_cpu(lum->lum_stripe_offset);
		} else {
			/*
			 * -1 means default, which will be in the same MDT with
			 * the stripe
			 */
			*mds = op_data->op_mds;
			lum->lum_stripe_offset = cpu_to_le32(op_data->op_mds);
		}
	} else {
		/*
		 * Allocate new fid on target according to operation type and
		 * parent home mds.
		 */
		*mds = op_data->op_mds;
	}

	return 0;
}

int __lmv_fid_alloc(struct lmv_obd *lmv, struct lu_fid *fid, u32 mds)
{
	struct lmv_tgt_desc	*tgt;
	int			 rc;

	tgt = lmv_get_target(lmv, mds, NULL);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	/*
	 * New seq alloc and FLD setup should be atomic. Otherwise we may find
	 * on server that seq in new allocated fid is not yet known.
	 */
	mutex_lock(&tgt->ltd_fid_mutex);

	if (tgt->ltd_active == 0 || !tgt->ltd_exp) {
		rc = -ENODEV;
		goto out;
	}

	/*
	 * Asking underlaying tgt layer to allocate new fid.
	 */
	rc = obd_fid_alloc(NULL, tgt->ltd_exp, fid, NULL);
	if (rc > 0) {
		LASSERT(fid_is_sane(fid));
		rc = 0;
	}

out:
	mutex_unlock(&tgt->ltd_fid_mutex);
	return rc;
}

int lmv_fid_alloc(const struct lu_env *env, struct obd_export *exp,
		  struct lu_fid *fid, struct md_op_data *op_data)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct lmv_obd	*lmv = &obd->u.lmv;
	u32		       mds = 0;
	int		    rc;

	LASSERT(op_data);
	LASSERT(fid);

	rc = lmv_placement_policy(obd, op_data, &mds);
	if (rc) {
		CERROR("Can't get target for allocating fid, rc %d\n",
		       rc);
		return rc;
	}

	rc = __lmv_fid_alloc(lmv, fid, mds);
	if (rc) {
		CERROR("Can't alloc new fid, rc %d\n", rc);
		return rc;
	}

	return rc;
}

static int lmv_setup(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	struct lmv_obd	     *lmv = &obd->u.lmv;
	struct lprocfs_static_vars  lvars = { NULL };
	struct lmv_desc	    *desc;
	int			 rc;

	if (LUSTRE_CFG_BUFLEN(lcfg, 1) < 1) {
		CERROR("LMV setup requires a descriptor\n");
		return -EINVAL;
	}

	desc = (struct lmv_desc *)lustre_cfg_buf(lcfg, 1);
	if (sizeof(*desc) > LUSTRE_CFG_BUFLEN(lcfg, 1)) {
		CERROR("Lmv descriptor size wrong: %d > %d\n",
		       (int)sizeof(*desc), LUSTRE_CFG_BUFLEN(lcfg, 1));
		return -EINVAL;
	}

	lmv->tgts = kcalloc(32, sizeof(*lmv->tgts), GFP_NOFS);
	if (!lmv->tgts)
		return -ENOMEM;
	lmv->tgts_size = 32;

	obd_str2uuid(&lmv->desc.ld_uuid, desc->ld_uuid.uuid);
	lmv->desc.ld_tgt_count = 0;
	lmv->desc.ld_active_tgt_count = 0;
	lmv->max_cookiesize = 0;
	lmv->max_def_easize = 0;
	lmv->max_easize = 0;
	lmv->lmv_placement = PLACEMENT_CHAR_POLICY;

	spin_lock_init(&lmv->lmv_lock);
	mutex_init(&lmv->lmv_init_mutex);

	lprocfs_lmv_init_vars(&lvars);

	lprocfs_obd_setup(obd, lvars.obd_vars, lvars.sysfs_vars);
	rc = ldebugfs_seq_create(obd->obd_debugfs_entry, "target_obd",
				 0444, &lmv_proc_target_fops, obd);
	if (rc)
		CWARN("%s: error adding LMV target_obd file: rc = %d\n",
		      obd->obd_name, rc);
	rc = fld_client_init(&lmv->lmv_fld, obd->obd_name,
			     LUSTRE_CLI_FLD_HASH_DHT);
	if (rc) {
		CERROR("Can't init FLD, err %d\n", rc);
		goto out;
	}

	return 0;

out:
	return rc;
}

static int lmv_cleanup(struct obd_device *obd)
{
	struct lmv_obd   *lmv = &obd->u.lmv;

	fld_client_fini(&lmv->lmv_fld);
	if (lmv->tgts) {
		int i;

		for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
			if (!lmv->tgts[i])
				continue;
			lmv_del_target(lmv, i);
		}
		kfree(lmv->tgts);
		lmv->tgts_size = 0;
	}
	return 0;
}

static int lmv_process_config(struct obd_device *obd, u32 len, void *buf)
{
	struct lustre_cfg	*lcfg = buf;
	struct obd_uuid		obd_uuid;
	int			gen;
	__u32			index;
	int			rc;

	switch (lcfg->lcfg_command) {
	case LCFG_ADD_MDC:
		/* modify_mdc_tgts add 0:lustre-clilmv  1:lustre-MDT0000_UUID
		 * 2:0  3:1  4:lustre-MDT0000-mdc_UUID
		 */
		if (LUSTRE_CFG_BUFLEN(lcfg, 1) > sizeof(obd_uuid.uuid)) {
			rc = -EINVAL;
			goto out;
		}

		obd_str2uuid(&obd_uuid,  lustre_cfg_buf(lcfg, 1));

		if (sscanf(lustre_cfg_buf(lcfg, 2), "%u", &index) != 1) {
			rc = -EINVAL;
			goto out;
		}
		if (sscanf(lustre_cfg_buf(lcfg, 3), "%d", &gen) != 1) {
			rc = -EINVAL;
			goto out;
		}
		rc = lmv_add_target(obd, &obd_uuid, index, gen);
		goto out;
	default:
		CERROR("Unknown command: %d\n", lcfg->lcfg_command);
		rc = -EINVAL;
		goto out;
	}
out:
	return rc;
}

static int lmv_statfs(const struct lu_env *env, struct obd_export *exp,
		      struct obd_statfs *osfs, __u64 max_age, __u32 flags)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct lmv_obd	*lmv = &obd->u.lmv;
	struct obd_statfs     *temp;
	int		    rc = 0;
	u32 i;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	temp = kzalloc(sizeof(*temp), GFP_NOFS);
	if (!temp)
		return -ENOMEM;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (!lmv->tgts[i] || !lmv->tgts[i]->ltd_exp)
			continue;

		rc = obd_statfs(env, lmv->tgts[i]->ltd_exp, temp,
				max_age, flags);
		if (rc) {
			CERROR("can't stat MDS #%d (%s), error %d\n", i,
			       lmv->tgts[i]->ltd_exp->exp_obd->obd_name,
			       rc);
			goto out_free_temp;
		}

		if (i == 0) {
			*osfs = *temp;
			/* If the statfs is from mount, it will needs
			 * retrieve necessary information from MDT0.
			 * i.e. mount does not need the merged osfs
			 * from all of MDT.
			 * And also clients can be mounted as long as
			 * MDT0 is in service
			 */
			if (flags & OBD_STATFS_FOR_MDT0)
				goto out_free_temp;
		} else {
			osfs->os_bavail += temp->os_bavail;
			osfs->os_blocks += temp->os_blocks;
			osfs->os_ffree += temp->os_ffree;
			osfs->os_files += temp->os_files;
		}
	}

out_free_temp:
	kfree(temp);
	return rc;
}

static int lmv_getstatus(struct obd_export *exp,
			 struct lu_fid *fid)
{
	struct obd_device    *obd = exp->exp_obd;
	struct lmv_obd       *lmv = &obd->u.lmv;
	int		   rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	rc = md_getstatus(lmv->tgts[0]->ltd_exp, fid);
	return rc;
}

static int lmv_getxattr(struct obd_export *exp, const struct lu_fid *fid,
			u64 valid, const char *name,
			const char *input, int input_size, int output_size,
			int flags, struct ptlrpc_request **request)
{
	struct obd_device      *obd = exp->exp_obd;
	struct lmv_obd	 *lmv = &obd->u.lmv;
	struct lmv_tgt_desc    *tgt;
	int		     rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_getxattr(tgt->ltd_exp, fid, valid, name, input,
			 input_size, output_size, flags, request);

	return rc;
}

static int lmv_setxattr(struct obd_export *exp, const struct lu_fid *fid,
			u64 valid, const char *name,
			const char *input, int input_size, int output_size,
			int flags, __u32 suppgid,
			struct ptlrpc_request **request)
{
	struct obd_device      *obd = exp->exp_obd;
	struct lmv_obd	 *lmv = &obd->u.lmv;
	struct lmv_tgt_desc    *tgt;
	int		     rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_setxattr(tgt->ltd_exp, fid, valid, name, input,
			 input_size, output_size, flags, suppgid,
			 request);

	return rc;
}

static int lmv_getattr(struct obd_export *exp, struct md_op_data *op_data,
		       struct ptlrpc_request **request)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	if (op_data->op_flags & MF_GET_MDT_IDX) {
		op_data->op_mds = tgt->ltd_idx;
		return 0;
	}

	rc = md_getattr(tgt->ltd_exp, op_data, request);

	return rc;
}

static int lmv_null_inode(struct obd_export *exp, const struct lu_fid *fid)
{
	struct obd_device   *obd = exp->exp_obd;
	struct lmv_obd      *lmv = &obd->u.lmv;
	u32 i;
	int		  rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	CDEBUG(D_INODE, "CBDATA for "DFID"\n", PFID(fid));

	/*
	 * With DNE every object can have two locks in different namespaces:
	 * lookup lock in space of MDT storing direntry and update/open lock in
	 * space of MDT storing inode.
	 */
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (!lmv->tgts[i] || !lmv->tgts[i]->ltd_exp)
			continue;
		md_null_inode(lmv->tgts[i]->ltd_exp, fid);
	}

	return 0;
}

static int lmv_find_cbdata(struct obd_export *exp, const struct lu_fid *fid,
			   ldlm_iterator_t it, void *data)
{
	struct obd_device   *obd = exp->exp_obd;
	struct lmv_obd      *lmv = &obd->u.lmv;
	int tgt;
	u32 i;
	int		  rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	CDEBUG(D_INODE, "CBDATA for "DFID"\n", PFID(fid));

	/*
	 * With DNE every object can have two locks in different namespaces:
	 * lookup lock in space of MDT storing direntry and update/open lock in
	 * space of MDT storing inode. Try the MDT that the FID maps to first,
	 * since this can be easily found, and only try others if that fails.
	 */
	for (i = 0, tgt = lmv_find_target_index(lmv, fid);
	     i < lmv->desc.ld_tgt_count;
	     i++, tgt = (tgt + 1) % lmv->desc.ld_tgt_count) {
		if (tgt < 0) {
			CDEBUG(D_HA, "%s: "DFID" is inaccessible: rc = %d\n",
			       obd->obd_name, PFID(fid), tgt);
			tgt = 0;
		}

		if (!lmv->tgts[tgt] || !lmv->tgts[tgt]->ltd_exp)
			continue;

		rc = md_find_cbdata(lmv->tgts[tgt]->ltd_exp, fid, it, data);
		if (rc)
			return rc;
	}

	return rc;
}

static int lmv_close(struct obd_export *exp, struct md_op_data *op_data,
		     struct md_open_data *mod, struct ptlrpc_request **request)
{
	struct obd_device     *obd = exp->exp_obd;
	struct lmv_obd	*lmv = &obd->u.lmv;
	struct lmv_tgt_desc   *tgt;
	int		    rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	CDEBUG(D_INODE, "CLOSE "DFID"\n", PFID(&op_data->op_fid1));
	rc = md_close(tgt->ltd_exp, op_data, mod, request);
	return rc;
}

/**
 * Choosing the MDT by name or FID in @op_data.
 * For non-striped directory, it will locate MDT by fid.
 * For striped-directory, it will locate MDT by name. And also
 * it will reset op_fid1 with the FID of the chosen stripe.
 **/
struct lmv_tgt_desc *
lmv_locate_target_for_name(struct lmv_obd *lmv, struct lmv_stripe_md *lsm,
			   const char *name, int namelen, struct lu_fid *fid,
			   u32 *mds)
{
	const struct lmv_oinfo *oinfo;
	struct lmv_tgt_desc *tgt;

	oinfo = lsm_name_to_stripe_info(lsm, name, namelen);
	if (IS_ERR(oinfo))
		return ERR_CAST(oinfo);

	*fid = oinfo->lmo_fid;
	*mds = oinfo->lmo_mds;
	tgt = lmv_get_target(lmv, *mds, NULL);

	CDEBUG(D_INFO, "locate on mds %u "DFID"\n", *mds, PFID(fid));
	return tgt;
}

/**
 * Locate mds by fid or name
 *
 * For striped directory (lsm != NULL), it will locate the stripe
 * by name hash (see lsm_name_to_stripe_info()). Note: if the hash_type
 * is unknown, it will return -EBADFD, and lmv_intent_lookup might need
 * walk through all of stripes to locate the entry.
 *
 * For normal direcotry, it will locate MDS by FID directly.
 * \param[in] lmv	LMV device
 * \param[in] op_data	client MD stack parameters, name, namelen
 *			mds_num etc.
 * \param[in] fid	object FID used to locate MDS.
 *
 * retval		pointer to the lmv_tgt_desc if succeed.
 *			ERR_PTR(errno) if failed.
 */
struct lmv_tgt_desc
*lmv_locate_mds(struct lmv_obd *lmv, struct md_op_data *op_data,
		struct lu_fid *fid)
{
	struct lmv_stripe_md *lsm = op_data->op_mea1;
	struct lmv_tgt_desc *tgt;

	if (!lsm || !op_data->op_namelen) {
		tgt = lmv_find_target(lmv, fid);
		if (IS_ERR(tgt))
			return tgt;

		op_data->op_mds = tgt->ltd_idx;

		return tgt;
	}

	return lmv_locate_target_for_name(lmv, lsm, op_data->op_name,
					  op_data->op_namelen, fid,
					  &op_data->op_mds);
}

static int lmv_create(struct obd_export *exp, struct md_op_data *op_data,
		      const void *data, int datalen, int mode, __u32 uid,
		      __u32 gid, cfs_cap_t cap_effective, __u64 rdev,
		      struct ptlrpc_request **request)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	if (!lmv->desc.ld_active_tgt_count)
		return -EIO;

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	CDEBUG(D_INODE, "CREATE name '%.*s' on "DFID" -> mds #%x\n",
	       op_data->op_namelen, op_data->op_name, PFID(&op_data->op_fid1),
	       op_data->op_mds);

	rc = lmv_fid_alloc(NULL, exp, &op_data->op_fid2, op_data);
	if (rc)
		return rc;

	/*
	 * Send the create request to the MDT where the object
	 * will be located
	 */
	tgt = lmv_find_target(lmv, &op_data->op_fid2);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	op_data->op_mds = tgt->ltd_idx;

	CDEBUG(D_INODE, "CREATE obj "DFID" -> mds #%x\n",
	       PFID(&op_data->op_fid1), op_data->op_mds);

	op_data->op_flags |= MF_MDC_CANCEL_FID1;
	rc = md_create(tgt->ltd_exp, op_data, data, datalen, mode, uid, gid,
		       cap_effective, rdev, request);

	if (rc == 0) {
		if (!*request)
			return rc;
		CDEBUG(D_INODE, "Created - "DFID"\n", PFID(&op_data->op_fid2));
	}
	return rc;
}

static int lmv_done_writing(struct obd_export *exp,
			    struct md_op_data *op_data,
			    struct md_open_data *mod)
{
	struct obd_device     *obd = exp->exp_obd;
	struct lmv_obd	*lmv = &obd->u.lmv;
	struct lmv_tgt_desc   *tgt;
	int		    rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_done_writing(tgt->ltd_exp, op_data, mod);
	return rc;
}

static int
lmv_enqueue(struct obd_export *exp, struct ldlm_enqueue_info *einfo,
	    const ldlm_policy_data_t *policy,
	    struct lookup_intent *it, struct md_op_data *op_data,
	    struct lustre_handle *lockh, __u64 extra_lock_flags)
{
	struct obd_device	*obd = exp->exp_obd;
	struct lmv_obd	   *lmv = &obd->u.lmv;
	struct lmv_tgt_desc      *tgt;
	int		       rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	CDEBUG(D_INODE, "ENQUEUE '%s' on "DFID"\n",
	       LL_IT2STR(it), PFID(&op_data->op_fid1));

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	CDEBUG(D_INODE, "ENQUEUE '%s' on "DFID" -> mds #%d\n",
	       LL_IT2STR(it), PFID(&op_data->op_fid1), tgt->ltd_idx);

	rc = md_enqueue(tgt->ltd_exp, einfo, policy, it, op_data, lockh,
			extra_lock_flags);

	return rc;
}

static int
lmv_getattr_name(struct obd_export *exp, struct md_op_data *op_data,
		 struct ptlrpc_request **preq)
{
	struct ptlrpc_request   *req = NULL;
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	struct mdt_body	 *body;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	CDEBUG(D_INODE, "GETATTR_NAME for %*s on "DFID" -> mds #%d\n",
	       op_data->op_namelen, op_data->op_name, PFID(&op_data->op_fid1),
	       tgt->ltd_idx);

	rc = md_getattr_name(tgt->ltd_exp, op_data, preq);
	if (rc != 0)
		return rc;

	body = req_capsule_server_get(&(*preq)->rq_pill, &RMF_MDT_BODY);
	if (body->mbo_valid & OBD_MD_MDS) {
		struct lu_fid rid = body->mbo_fid1;

		CDEBUG(D_INODE, "Request attrs for "DFID"\n",
		       PFID(&rid));

		tgt = lmv_find_target(lmv, &rid);
		if (IS_ERR(tgt)) {
			ptlrpc_req_finished(*preq);
			*preq = NULL;
			return PTR_ERR(tgt);
		}

		op_data->op_fid1 = rid;
		op_data->op_valid |= OBD_MD_FLCROSSREF;
		op_data->op_namelen = 0;
		op_data->op_name = NULL;
		rc = md_getattr_name(tgt->ltd_exp, op_data, &req);
		ptlrpc_req_finished(*preq);
		*preq = req;
	}

	return rc;
}

#define md_op_data_fid(op_data, fl)		     \
	(fl == MF_MDC_CANCEL_FID1 ? &op_data->op_fid1 : \
	 fl == MF_MDC_CANCEL_FID2 ? &op_data->op_fid2 : \
	 fl == MF_MDC_CANCEL_FID3 ? &op_data->op_fid3 : \
	 fl == MF_MDC_CANCEL_FID4 ? &op_data->op_fid4 : \
	 NULL)

static int lmv_early_cancel(struct obd_export *exp, struct lmv_tgt_desc *tgt,
			    struct md_op_data *op_data, int op_tgt,
			    enum ldlm_mode mode, int bits, int flag)
{
	struct lu_fid	  *fid = md_op_data_fid(op_data, flag);
	struct obd_device      *obd = exp->exp_obd;
	struct lmv_obd	 *lmv = &obd->u.lmv;
	ldlm_policy_data_t      policy = { {0} };
	int		     rc = 0;

	if (!fid_is_sane(fid))
		return 0;

	if (!tgt) {
		tgt = lmv_find_target(lmv, fid);
		if (IS_ERR(tgt))
			return PTR_ERR(tgt);
	}

	if (tgt->ltd_idx != op_tgt) {
		CDEBUG(D_INODE, "EARLY_CANCEL on "DFID"\n", PFID(fid));
		policy.l_inodebits.bits = bits;
		rc = md_cancel_unused(tgt->ltd_exp, fid, &policy,
				      mode, LCF_ASYNC, NULL);
	} else {
		CDEBUG(D_INODE,
		       "EARLY_CANCEL skip operation target %d on "DFID"\n",
		       op_tgt, PFID(fid));
		op_data->op_flags |= flag;
		rc = 0;
	}

	return rc;
}

/*
 * llite passes fid of an target inode in op_data->op_fid1 and id of directory in
 * op_data->op_fid2
 */
static int lmv_link(struct obd_export *exp, struct md_op_data *op_data,
		    struct ptlrpc_request **request)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	LASSERT(op_data->op_namelen != 0);

	CDEBUG(D_INODE, "LINK "DFID":%*s to "DFID"\n",
	       PFID(&op_data->op_fid2), op_data->op_namelen,
	       op_data->op_name, PFID(&op_data->op_fid1));

	op_data->op_fsuid = from_kuid(&init_user_ns, current_fsuid());
	op_data->op_fsgid = from_kgid(&init_user_ns, current_fsgid());
	op_data->op_cap = cfs_curproc_cap_pack();
	if (op_data->op_mea2) {
		struct lmv_stripe_md *lsm = op_data->op_mea2;
		const struct lmv_oinfo *oinfo;

		oinfo = lsm_name_to_stripe_info(lsm, op_data->op_name,
						op_data->op_namelen);
		if (IS_ERR(oinfo))
			return PTR_ERR(oinfo);

		op_data->op_fid2 = oinfo->lmo_fid;
	}

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid2);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	/*
	 * Cancel UPDATE lock on child (fid1).
	 */
	op_data->op_flags |= MF_MDC_CANCEL_FID2;
	rc = lmv_early_cancel(exp, NULL, op_data, tgt->ltd_idx, LCK_EX,
			      MDS_INODELOCK_UPDATE, MF_MDC_CANCEL_FID1);
	if (rc != 0)
		return rc;

	rc = md_link(tgt->ltd_exp, op_data, request);

	return rc;
}

static int lmv_rename(struct obd_export *exp, struct md_op_data *op_data,
		      const char *old, int oldlen, const char *new, int newlen,
		      struct ptlrpc_request **request)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *src_tgt;
	int			rc;

	LASSERT(oldlen != 0);

	CDEBUG(D_INODE, "RENAME %.*s in "DFID":%d to %.*s in "DFID":%d\n",
	       oldlen, old, PFID(&op_data->op_fid1),
	       op_data->op_mea1 ? op_data->op_mea1->lsm_md_stripe_count : 0,
	       newlen, new, PFID(&op_data->op_fid2),
	       op_data->op_mea2 ? op_data->op_mea2->lsm_md_stripe_count : 0);

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	op_data->op_fsuid = from_kuid(&init_user_ns, current_fsuid());
	op_data->op_fsgid = from_kgid(&init_user_ns, current_fsgid());
	op_data->op_cap = cfs_curproc_cap_pack();

	if (op_data->op_cli_flags & CLI_MIGRATE) {
		LASSERTF(fid_is_sane(&op_data->op_fid3), "invalid FID "DFID"\n",
			 PFID(&op_data->op_fid3));
		rc = lmv_fid_alloc(NULL, exp, &op_data->op_fid2, op_data);
		if (rc)
			return rc;
		src_tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid3);
	} else {
		if (op_data->op_mea1) {
			struct lmv_stripe_md *lsm = op_data->op_mea1;

			src_tgt = lmv_locate_target_for_name(lmv, lsm, old,
							     oldlen,
							     &op_data->op_fid1,
							     &op_data->op_mds);
			if (IS_ERR(src_tgt))
				return PTR_ERR(src_tgt);
		} else {
			src_tgt = lmv_find_target(lmv, &op_data->op_fid1);
			if (IS_ERR(src_tgt))
				return PTR_ERR(src_tgt);

			op_data->op_mds = src_tgt->ltd_idx;
		}

		if (op_data->op_mea2) {
			struct lmv_stripe_md *lsm = op_data->op_mea2;
			const struct lmv_oinfo *oinfo;

			oinfo = lsm_name_to_stripe_info(lsm, new, newlen);
			if (IS_ERR(oinfo))
				return PTR_ERR(oinfo);

			op_data->op_fid2 = oinfo->lmo_fid;
		}
	}
	if (IS_ERR(src_tgt))
		return PTR_ERR(src_tgt);

	/*
	 * LOOKUP lock on src child (fid3) should also be cancelled for
	 * src_tgt in mdc_rename.
	 */
	op_data->op_flags |= MF_MDC_CANCEL_FID1 | MF_MDC_CANCEL_FID3;

	/*
	 * Cancel UPDATE locks on tgt parent (fid2), tgt_tgt is its
	 * own target.
	 */
	rc = lmv_early_cancel(exp, NULL, op_data, src_tgt->ltd_idx,
			      LCK_EX, MDS_INODELOCK_UPDATE,
			      MF_MDC_CANCEL_FID2);
	if (rc)
		return rc;
	/*
	 * Cancel LOOKUP locks on source child (fid3) for parent tgt_tgt.
	 */
	if (fid_is_sane(&op_data->op_fid3)) {
		struct lmv_tgt_desc *tgt;

		tgt = lmv_find_target(lmv, &op_data->op_fid1);
		if (IS_ERR(tgt))
			return PTR_ERR(tgt);

		/* Cancel LOOKUP lock on its parent */
		rc = lmv_early_cancel(exp, tgt, op_data, src_tgt->ltd_idx,
				      LCK_EX, MDS_INODELOCK_LOOKUP,
				      MF_MDC_CANCEL_FID3);
		if (rc)
			return rc;

		rc = lmv_early_cancel(exp, NULL, op_data, src_tgt->ltd_idx,
				      LCK_EX, MDS_INODELOCK_FULL,
				      MF_MDC_CANCEL_FID3);
		if (rc)
			return rc;
	}

	/*
	 * Cancel all the locks on tgt child (fid4).
	 */
	if (fid_is_sane(&op_data->op_fid4))
		rc = lmv_early_cancel(exp, NULL, op_data, src_tgt->ltd_idx,
				      LCK_EX, MDS_INODELOCK_FULL,
				      MF_MDC_CANCEL_FID4);

	CDEBUG(D_INODE, DFID":m%d to "DFID"\n", PFID(&op_data->op_fid1),
	       op_data->op_mds, PFID(&op_data->op_fid2));

	rc = md_rename(src_tgt->ltd_exp, op_data, old, oldlen,
		       new, newlen, request);
	return rc;
}

static int lmv_setattr(struct obd_export *exp, struct md_op_data *op_data,
		       void *ea, int ealen, void *ea2, int ea2len,
		       struct ptlrpc_request **request,
		       struct md_open_data **mod)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	CDEBUG(D_INODE, "SETATTR for "DFID", valid 0x%x\n",
	       PFID(&op_data->op_fid1), op_data->op_attr.ia_valid);

	op_data->op_flags |= MF_MDC_CANCEL_FID1;
	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_setattr(tgt->ltd_exp, op_data, ea, ealen, ea2,
			ea2len, request, mod);

	return rc;
}

static int lmv_sync(struct obd_export *exp, const struct lu_fid *fid,
		    struct ptlrpc_request **request)
{
	struct obd_device	 *obd = exp->exp_obd;
	struct lmv_obd	    *lmv = &obd->u.lmv;
	struct lmv_tgt_desc       *tgt;
	int			rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_sync(tgt->ltd_exp, fid, request);
	return rc;
}

/**
 * Get current minimum entry from striped directory
 *
 * This function will search the dir entry, whose hash value is the
 * closest(>=) to @hash_offset, from all of sub-stripes, and it is
 * only being called for striped directory.
 *
 * \param[in] exp		export of LMV
 * \param[in] op_data		parameters transferred beween client MD stack
 *				stripe_information will be included in this
 *				parameter
 * \param[in] cb_op		ldlm callback being used in enqueue in
 *				mdc_read_page
 * \param[in] hash_offset	the hash value, which is used to locate
 *				minum(closet) dir entry
 * \param[in|out] stripe_offset	the caller use this to indicate the stripe
 *				index of last entry, so to avoid hash conflict
 *				between stripes. It will also be used to
 *				return the stripe index of current dir entry.
 * \param[in|out] entp		the minum entry and it also is being used
 *				to input the last dir entry to resolve the
 *				hash conflict
 *
 * \param[out] ppage		the page which holds the minum entry
 *
 * \retval			= 0 get the entry successfully
 *				negative errno (< 0) does not get the entry
 */
static int lmv_get_min_striped_entry(struct obd_export *exp,
				     struct md_op_data *op_data,
				     struct md_callback *cb_op,
				     __u64 hash_offset, int *stripe_offset,
				     struct lu_dirent **entp,
				     struct page **ppage)
{
	struct lmv_stripe_md *lsm = op_data->op_mea1;
	struct obd_device *obd = exp->exp_obd;
	struct lmv_obd *lmv = &obd->u.lmv;
	struct lu_dirent *min_ent = NULL;
	struct page *min_page = NULL;
	struct lmv_tgt_desc *tgt;
	int stripe_count;
	int min_idx = 0;
	int rc = 0;
	int i;

	stripe_count = lsm->lsm_md_stripe_count;
	for (i = 0; i < stripe_count; i++) {
		__u64 stripe_hash = hash_offset;
		struct lu_dirent *ent = NULL;
		struct page *page = NULL;
		struct lu_dirpage *dp;

		tgt = lmv_get_target(lmv, lsm->lsm_md_oinfo[i].lmo_mds, NULL);
		if (IS_ERR(tgt)) {
			rc = PTR_ERR(tgt);
			goto out;
		}

		/*
		 * op_data will be shared by each stripe, so we need
		 * reset these value for each stripe
		 */
		op_data->op_fid1 = lsm->lsm_md_oinfo[i].lmo_fid;
		op_data->op_fid2 = lsm->lsm_md_oinfo[i].lmo_fid;
		op_data->op_data = lsm->lsm_md_oinfo[i].lmo_root;
next:
		rc = md_read_page(tgt->ltd_exp, op_data, cb_op, stripe_hash,
				  &page);
		if (rc)
			goto out;

		dp = page_address(page);
		for (ent = lu_dirent_start(dp); ent;
		     ent = lu_dirent_next(ent)) {
			/* Skip dummy entry */
			if (!le16_to_cpu(ent->lde_namelen))
				continue;

			if (le64_to_cpu(ent->lde_hash) < hash_offset)
				continue;

			if (le64_to_cpu(ent->lde_hash) == hash_offset &&
			    (*entp == ent || i < *stripe_offset))
				continue;

			/* skip . and .. for other stripes */
			if (i && (!strncmp(ent->lde_name, ".",
					   le16_to_cpu(ent->lde_namelen)) ||
				  !strncmp(ent->lde_name, "..",
					   le16_to_cpu(ent->lde_namelen))))
				continue;
			break;
		}

		if (!ent) {
			stripe_hash = le64_to_cpu(dp->ldp_hash_end);

			kunmap(page);
			put_page(page);
			page = NULL;

			/*
			 * reach the end of current stripe, go to next stripe
			 */
			if (stripe_hash == MDS_DIR_END_OFF)
				continue;
			else
				goto next;
		}

		if (min_ent) {
			if (le64_to_cpu(min_ent->lde_hash) >
			    le64_to_cpu(ent->lde_hash)) {
				min_ent = ent;
				kunmap(min_page);
				put_page(min_page);
				min_idx = i;
				min_page = page;
			} else {
				kunmap(page);
				put_page(page);
				page = NULL;
			}
		} else {
			min_ent = ent;
			min_page = page;
			min_idx = i;
		}
	}

out:
	if (*ppage) {
		kunmap(*ppage);
		put_page(*ppage);
	}
	*stripe_offset = min_idx;
	*entp = min_ent;
	*ppage = min_page;
	return rc;
}

/**
 * Build dir entry page from a striped directory
 *
 * This function gets one entry by @offset from a striped directory. It will
 * read entries from all of stripes, and choose one closest to the required
 * offset(&offset). A few notes
 * 1. skip . and .. for non-zero stripes, because there can only have one .
 * and .. in a directory.
 * 2. op_data will be shared by all of stripes, instead of allocating new
 * one, so need to restore before reusing.
 * 3. release the entry page if that is not being chosen.
 *
 * \param[in] exp	obd export refer to LMV
 * \param[in] op_data	hold those MD parameters of read_entry
 * \param[in] cb_op	ldlm callback being used in enqueue in mdc_read_entry
 * \param[out] ldp	the entry being read
 * \param[out] ppage	the page holding the entry. Note: because the entry
 *			will be accessed in upper layer, so we need hold the
 *			page until the usages of entry is finished, see
 *			ll_dir_entry_next.
 *
 * retval		=0 if get entry successfully
 *			<0 cannot get entry
 */
static int lmv_read_striped_page(struct obd_export *exp,
				 struct md_op_data *op_data,
				 struct md_callback *cb_op,
				 __u64 offset, struct page **ppage)
{
	struct inode *master_inode = op_data->op_data;
	struct lu_fid master_fid = op_data->op_fid1;
	struct obd_device *obd = exp->exp_obd;
	__u64 hash_offset = offset;
	struct page *min_ent_page = NULL;
	struct page *ent_page = NULL;
	struct lu_dirent *min_ent = NULL;
	struct lu_dirent *last_ent;
	struct lu_dirent *ent;
	struct lu_dirpage *dp;
	size_t left_bytes;
	int ent_idx = 0;
	void *area;
	int rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	/*
	 * Allocate a page and read entries from all of stripes and fill
	 * the page by hash order
	 */
	ent_page = alloc_page(GFP_KERNEL);
	if (!ent_page)
		return -ENOMEM;

	/* Initialize the entry page */
	dp = kmap(ent_page);
	memset(dp, 0, sizeof(*dp));
	dp->ldp_hash_start = cpu_to_le64(offset);
	dp->ldp_flags |= LDF_COLLIDE;

	area = dp + 1;
	left_bytes = PAGE_SIZE - sizeof(*dp);
	ent = area;
	last_ent = ent;
	do {
		__u16 ent_size;

		/* Find the minum entry from all sub-stripes */
		rc = lmv_get_min_striped_entry(exp, op_data, cb_op, hash_offset,
					       &ent_idx, &min_ent,
					       &min_ent_page);
		if (rc)
			goto out;

		/*
		 * If it can not get minum entry, it means it already reaches
		 * the end of this directory
		 */
		if (!min_ent) {
			last_ent->lde_reclen = 0;
			hash_offset = MDS_DIR_END_OFF;
			goto out;
		}

		ent_size = le16_to_cpu(min_ent->lde_reclen);

		/*
		 * the last entry lde_reclen is 0, but it might not
		 * the end of this entry of this temporay entry
		 */
		if (!ent_size)
			ent_size = lu_dirent_calc_size(
					le16_to_cpu(min_ent->lde_namelen),
					le32_to_cpu(min_ent->lde_attrs));
		if (ent_size > left_bytes) {
			last_ent->lde_reclen = cpu_to_le16(0);
			hash_offset = le64_to_cpu(min_ent->lde_hash);
			goto out;
		}

		memcpy(ent, min_ent, ent_size);

		/*
		 * Replace . with master FID and Replace .. with the parent FID
		 * of master object
		 */
		if (!strncmp(ent->lde_name, ".",
			     le16_to_cpu(ent->lde_namelen)) &&
		    le16_to_cpu(ent->lde_namelen) == 1)
			fid_cpu_to_le(&ent->lde_fid, &master_fid);
		else if (!strncmp(ent->lde_name, "..",
				  le16_to_cpu(ent->lde_namelen)) &&
			 le16_to_cpu(ent->lde_namelen) == 2)
			fid_cpu_to_le(&ent->lde_fid, &op_data->op_fid3);

		left_bytes -= ent_size;
		ent->lde_reclen = cpu_to_le16(ent_size);
		last_ent = ent;
		ent = (void *)ent + ent_size;
		hash_offset = le64_to_cpu(min_ent->lde_hash);
		if (hash_offset == MDS_DIR_END_OFF) {
			last_ent->lde_reclen = 0;
			break;
		}
	} while (1);
out:
	if (min_ent_page) {
		kunmap(min_ent_page);
		put_page(min_ent_page);
	}

	if (unlikely(rc)) {
		__free_page(ent_page);
		ent_page = NULL;
	} else {
		if (ent == area)
			dp->ldp_flags |= LDF_EMPTY;
		dp->ldp_flags = cpu_to_le32(dp->ldp_flags);
		dp->ldp_hash_end = cpu_to_le64(hash_offset);
	}

	/*
	 * We do not want to allocate md_op_data during each
	 * dir entry reading, so op_data will be shared by every stripe,
	 * then we need to restore it back to original value before
	 * return to the upper layer
	 */
	op_data->op_fid1 = master_fid;
	op_data->op_fid2 = master_fid;
	op_data->op_data = master_inode;

	*ppage = ent_page;

	return rc;
}

int lmv_read_page(struct obd_export *exp, struct md_op_data *op_data,
		  struct md_callback *cb_op, __u64 offset,
		  struct page **ppage)
{
	struct lmv_stripe_md *lsm = op_data->op_mea1;
	struct obd_device *obd = exp->exp_obd;
	struct lmv_obd *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt;
	int rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	if (unlikely(lsm)) {
		rc = lmv_read_striped_page(exp, op_data, cb_op, offset, ppage);
		return rc;
	}

	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_read_page(tgt->ltd_exp, op_data, cb_op, offset, ppage);

	return rc;
}

/**
 * Unlink a file/directory
 *
 * Unlink a file or directory under the parent dir. The unlink request
 * usually will be sent to the MDT where the child is located, but if
 * the client does not have the child FID then request will be sent to the
 * MDT where the parent is located.
 *
 * If the parent is a striped directory then it also needs to locate which
 * stripe the name of the child is located, and replace the parent FID
 * (@op->op_fid1) with the stripe FID. Note: if the stripe is unknown,
 * it will walk through all of sub-stripes until the child is being
 * unlinked finally.
 *
 * \param[in] exp	export refer to LMV
 * \param[in] op_data	different parameters transferred beween client
 *			MD stacks, name, namelen, FIDs etc.
 *			op_fid1 is the parent FID, op_fid2 is the child
 *			FID.
 * \param[out] request point to the request of unlink.
 *
 * retval		0 if succeed
 *			negative errno if failed.
 */
static int lmv_unlink(struct obd_export *exp, struct md_op_data *op_data,
		      struct ptlrpc_request **request)
{
	struct lmv_stripe_md *lsm = op_data->op_mea1;
	struct obd_device    *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *parent_tgt = NULL;
	struct lmv_tgt_desc     *tgt = NULL;
	struct mdt_body		*body;
	int stripe_index = 0;
	int		     rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;
retry_unlink:
	/* For striped dir, we need to locate the parent as well */
	if (lsm) {
		struct lmv_tgt_desc *tmp;

		LASSERT(op_data->op_name && op_data->op_namelen);

		tmp = lmv_locate_target_for_name(lmv, lsm,
						 op_data->op_name,
						 op_data->op_namelen,
						 &op_data->op_fid1,
						 &op_data->op_mds);

		/*
		 * return -EBADFD means unknown hash type, might
		 * need try all sub-stripe here
		 */
		if (IS_ERR(tmp) && PTR_ERR(tmp) != -EBADFD)
			return PTR_ERR(tmp);

		/*
		 * Note: both migrating dir and unknown hash dir need to
		 * try all of sub-stripes, so we need start search the
		 * name from stripe 0, but migrating dir is already handled
		 * inside lmv_locate_target_for_name(), so we only check
		 * unknown hash type directory here
		 */
		if (!lmv_is_known_hash_type(lsm)) {
			struct lmv_oinfo *oinfo;

			oinfo = &lsm->lsm_md_oinfo[stripe_index];

			op_data->op_fid1 = oinfo->lmo_fid;
			op_data->op_mds = oinfo->lmo_mds;
		}
	}

try_next_stripe:
	/* Send unlink requests to the MDT where the child is located */
	if (likely(!fid_is_zero(&op_data->op_fid2)))
		tgt = lmv_find_target(lmv, &op_data->op_fid2);
	else if (lsm)
		tgt = lmv_get_target(lmv, op_data->op_mds, NULL);
	else
		tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);

	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	op_data->op_fsuid = from_kuid(&init_user_ns, current_fsuid());
	op_data->op_fsgid = from_kgid(&init_user_ns, current_fsgid());
	op_data->op_cap = cfs_curproc_cap_pack();

	/*
	 * If child's fid is given, cancel unused locks for it if it is from
	 * another export than parent.
	 *
	 * LOOKUP lock for child (fid3) should also be cancelled on parent
	 * tgt_tgt in mdc_unlink().
	 */
	op_data->op_flags |= MF_MDC_CANCEL_FID1 | MF_MDC_CANCEL_FID3;

	/*
	 * Cancel FULL locks on child (fid3).
	 */
	parent_tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(parent_tgt))
		return PTR_ERR(parent_tgt);

	if (parent_tgt != tgt) {
		rc = lmv_early_cancel(exp, parent_tgt, op_data, tgt->ltd_idx,
				      LCK_EX, MDS_INODELOCK_LOOKUP,
				      MF_MDC_CANCEL_FID3);
	}

	rc = lmv_early_cancel(exp, NULL, op_data, tgt->ltd_idx, LCK_EX,
			      MDS_INODELOCK_FULL, MF_MDC_CANCEL_FID3);
	if (rc != 0)
		return rc;

	CDEBUG(D_INODE, "unlink with fid="DFID"/"DFID" -> mds #%d\n",
	       PFID(&op_data->op_fid1), PFID(&op_data->op_fid2), tgt->ltd_idx);

	rc = md_unlink(tgt->ltd_exp, op_data, request);
	if (rc != 0 && rc != -EREMOTE  && rc != -ENOENT)
		return rc;

	/* Try next stripe if it is needed. */
	if (rc == -ENOENT && lsm && lmv_need_try_all_stripes(lsm)) {
		struct lmv_oinfo *oinfo;

		stripe_index++;
		if (stripe_index >= lsm->lsm_md_stripe_count)
			return rc;

		oinfo = &lsm->lsm_md_oinfo[stripe_index];

		op_data->op_fid1 = oinfo->lmo_fid;
		op_data->op_mds = oinfo->lmo_mds;

		ptlrpc_req_finished(*request);
		*request = NULL;

		goto try_next_stripe;
	}

	body = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_BODY);
	if (!body)
		return -EPROTO;

	/* Not cross-ref case, just get out of here. */
	if (likely(!(body->mbo_valid & OBD_MD_MDS)))
		return 0;

	CDEBUG(D_INODE, "%s: try unlink to another MDT for "DFID"\n",
	       exp->exp_obd->obd_name, PFID(&body->mbo_fid1));

	/* This is a remote object, try remote MDT, Note: it may
	 * try more than 1 time here, Considering following case
	 * /mnt/lustre is root on MDT0, remote1 is on MDT1
	 * 1. Initially A does not know where remote1 is, it send
	 *    unlink RPC to MDT0, MDT0 return -EREMOTE, it will
	 *    resend unlink RPC to MDT1 (retry 1st time).
	 *
	 * 2. During the unlink RPC in flight,
	 *    client B mv /mnt/lustre/remote1 /mnt/lustre/remote2
	 *    and create new remote1, but on MDT0
	 *
	 * 3. MDT1 get unlink RPC(from A), then do remote lock on
	 *    /mnt/lustre, then lookup get fid of remote1, and find
	 *    it is remote dir again, and replay -EREMOTE again.
	 *
	 * 4. Then A will resend unlink RPC to MDT0. (retry 2nd times).
	 *
	 * In theory, it might try unlimited time here, but it should
	 * be very rare case.
	 */
	op_data->op_fid2 = body->mbo_fid1;
	ptlrpc_req_finished(*request);
	*request = NULL;

	goto retry_unlink;
}

static int lmv_precleanup(struct obd_device *obd, enum obd_cleanup_stage stage)
{
	struct lmv_obd *lmv = &obd->u.lmv;

	switch (stage) {
	case OBD_CLEANUP_EARLY:
		/* XXX: here should be calling obd_precleanup() down to
		 * stack.
		 */
		break;
	case OBD_CLEANUP_EXPORTS:
		fld_client_debugfs_fini(&lmv->lmv_fld);
		lprocfs_obd_cleanup(obd);
		break;
	default:
		break;
	}
	return 0;
}

static int lmv_get_info(const struct lu_env *env, struct obd_export *exp,
			__u32 keylen, void *key, __u32 *vallen, void *val,
			struct lov_stripe_md *lsm)
{
	struct obd_device       *obd;
	struct lmv_obd	  *lmv;
	int		      rc = 0;

	obd = class_exp2obd(exp);
	if (!obd) {
		CDEBUG(D_IOCTL, "Invalid client cookie %#llx\n",
		       exp->exp_handle.h_cookie);
		return -EINVAL;
	}

	lmv = &obd->u.lmv;
	if (keylen >= strlen("remote_flag") && !strcmp(key, "remote_flag")) {
		int i;

		rc = lmv_check_connect(obd);
		if (rc)
			return rc;

		LASSERT(*vallen == sizeof(__u32));
		for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
			struct lmv_tgt_desc *tgt = lmv->tgts[i];

			/*
			 * All tgts should be connected when this gets called.
			 */
			if (!tgt || !tgt->ltd_exp)
				continue;

			if (!obd_get_info(env, tgt->ltd_exp, keylen, key,
					  vallen, val, NULL))
				return 0;
		}
		return -EINVAL;
	} else if (KEY_IS(KEY_MAX_EASIZE) ||
		   KEY_IS(KEY_DEFAULT_EASIZE) ||
		   KEY_IS(KEY_CONN_DATA)) {
		rc = lmv_check_connect(obd);
		if (rc)
			return rc;

		/*
		 * Forwarding this request to first MDS, it should know LOV
		 * desc.
		 */
		rc = obd_get_info(env, lmv->tgts[0]->ltd_exp, keylen, key,
				  vallen, val, NULL);
		if (!rc && KEY_IS(KEY_CONN_DATA))
			exp->exp_connect_data = *(struct obd_connect_data *)val;
		return rc;
	} else if (KEY_IS(KEY_TGT_COUNT)) {
		*((int *)val) = lmv->desc.ld_tgt_count;
		return 0;
	}

	CDEBUG(D_IOCTL, "Invalid key\n");
	return -EINVAL;
}

static int lmv_set_info_async(const struct lu_env *env, struct obd_export *exp,
			      u32 keylen, void *key, u32 vallen,
			      void *val, struct ptlrpc_request_set *set)
{
	struct lmv_tgt_desc    *tgt;
	struct obd_device      *obd;
	struct lmv_obd	 *lmv;
	int rc = 0;

	obd = class_exp2obd(exp);
	if (!obd) {
		CDEBUG(D_IOCTL, "Invalid client cookie %#llx\n",
		       exp->exp_handle.h_cookie);
		return -EINVAL;
	}
	lmv = &obd->u.lmv;

	if (KEY_IS(KEY_READ_ONLY) || KEY_IS(KEY_FLUSH_CTX)) {
		int i, err = 0;

		for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
			tgt = lmv->tgts[i];

			if (!tgt || !tgt->ltd_exp)
				continue;

			err = obd_set_info_async(env, tgt->ltd_exp,
						 keylen, key, vallen, val, set);
			if (err && rc == 0)
				rc = err;
		}

		return rc;
	}

	return -EINVAL;
}

static int lmv_pack_md_v1(const struct lmv_stripe_md *lsm,
			  struct lmv_mds_md_v1 *lmm1)
{
	int cplen;
	int i;

	lmm1->lmv_magic = cpu_to_le32(lsm->lsm_md_magic);
	lmm1->lmv_stripe_count = cpu_to_le32(lsm->lsm_md_stripe_count);
	lmm1->lmv_master_mdt_index = cpu_to_le32(lsm->lsm_md_master_mdt_index);
	lmm1->lmv_hash_type = cpu_to_le32(lsm->lsm_md_hash_type);
	cplen = strlcpy(lmm1->lmv_pool_name, lsm->lsm_md_pool_name,
			sizeof(lmm1->lmv_pool_name));
	if (cplen >= sizeof(lmm1->lmv_pool_name))
		return -E2BIG;

	for (i = 0; i < lsm->lsm_md_stripe_count; i++)
		fid_cpu_to_le(&lmm1->lmv_stripe_fids[i],
			      &lsm->lsm_md_oinfo[i].lmo_fid);
	return 0;
}

int lmv_pack_md(union lmv_mds_md **lmmp, const struct lmv_stripe_md *lsm,
		int stripe_count)
{
	int lmm_size = 0, rc = 0;
	bool allocated = false;

	LASSERT(lmmp);

	/* Free lmm */
	if (*lmmp && !lsm) {
		int stripe_cnt;

		stripe_cnt = lmv_mds_md_stripe_count_get(*lmmp);
		lmm_size = lmv_mds_md_size(stripe_cnt,
					   le32_to_cpu((*lmmp)->lmv_magic));
		if (!lmm_size)
			return -EINVAL;
		kvfree(*lmmp);
		*lmmp = NULL;
		return 0;
	}

	/* Alloc lmm */
	if (!*lmmp && !lsm) {
		lmm_size = lmv_mds_md_size(stripe_count, LMV_MAGIC);
		LASSERT(lmm_size > 0);
		*lmmp = libcfs_kvzalloc(lmm_size, GFP_NOFS);
		if (!*lmmp)
			return -ENOMEM;
		lmv_mds_md_stripe_count_set(*lmmp, stripe_count);
		(*lmmp)->lmv_magic = cpu_to_le32(LMV_MAGIC);
		return lmm_size;
	}

	/* pack lmm */
	LASSERT(lsm);
	lmm_size = lmv_mds_md_size(lsm->lsm_md_stripe_count,
				   lsm->lsm_md_magic);
	if (!*lmmp) {
		*lmmp = libcfs_kvzalloc(lmm_size, GFP_NOFS);
		if (!*lmmp)
			return -ENOMEM;
		allocated = true;
	}

	switch (lsm->lsm_md_magic) {
	case LMV_MAGIC_V1:
		rc = lmv_pack_md_v1(lsm, &(*lmmp)->lmv_md_v1);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc && allocated) {
		kvfree(*lmmp);
		*lmmp = NULL;
	}

	return lmm_size;
}
EXPORT_SYMBOL(lmv_pack_md);

static int lmv_unpack_md_v1(struct obd_export *exp, struct lmv_stripe_md *lsm,
			    const struct lmv_mds_md_v1 *lmm1)
{
	struct lmv_obd *lmv = &exp->exp_obd->u.lmv;
	int stripe_count;
	int rc = 0;
	int cplen;
	int i;

	lsm->lsm_md_magic = le32_to_cpu(lmm1->lmv_magic);
	lsm->lsm_md_stripe_count = le32_to_cpu(lmm1->lmv_stripe_count);
	lsm->lsm_md_master_mdt_index = le32_to_cpu(lmm1->lmv_master_mdt_index);
	if (OBD_FAIL_CHECK(OBD_FAIL_UNKNOWN_LMV_STRIPE))
		lsm->lsm_md_hash_type = LMV_HASH_TYPE_UNKNOWN;
	else
		lsm->lsm_md_hash_type = le32_to_cpu(lmm1->lmv_hash_type);
	lsm->lsm_md_layout_version = le32_to_cpu(lmm1->lmv_layout_version);
	cplen = strlcpy(lsm->lsm_md_pool_name, lmm1->lmv_pool_name,
			sizeof(lsm->lsm_md_pool_name));

	if (cplen >= sizeof(lsm->lsm_md_pool_name))
		return -E2BIG;

	CDEBUG(D_INFO, "unpack lsm count %d, master %d hash_type %d layout_version %d\n",
	       lsm->lsm_md_stripe_count, lsm->lsm_md_master_mdt_index,
	       lsm->lsm_md_hash_type, lsm->lsm_md_layout_version);

	stripe_count = le32_to_cpu(lmm1->lmv_stripe_count);
	for (i = 0; i < le32_to_cpu(stripe_count); i++) {
		fid_le_to_cpu(&lsm->lsm_md_oinfo[i].lmo_fid,
			      &lmm1->lmv_stripe_fids[i]);
		rc = lmv_fld_lookup(lmv, &lsm->lsm_md_oinfo[i].lmo_fid,
				    &lsm->lsm_md_oinfo[i].lmo_mds);
		if (rc)
			return rc;
		CDEBUG(D_INFO, "unpack fid #%d "DFID"\n", i,
		       PFID(&lsm->lsm_md_oinfo[i].lmo_fid));
	}

	return rc;
}

int lmv_unpack_md(struct obd_export *exp, struct lmv_stripe_md **lsmp,
		  const union lmv_mds_md *lmm, int stripe_count)
{
	struct lmv_stripe_md *lsm;
	bool allocated = false;
	int lsm_size, rc;

	LASSERT(lsmp);

	lsm = *lsmp;
	/* Free memmd */
	if (lsm && !lmm) {
		int i;

		for (i = 1; i < lsm->lsm_md_stripe_count; i++) {
			/*
			 * For migrating inode, the master stripe and master
			 * object will be the same, so do not need iput, see
			 * ll_update_lsm_md
			 */
			if (!(lsm->lsm_md_hash_type & LMV_HASH_FLAG_MIGRATION &&
			      !i) && lsm->lsm_md_oinfo[i].lmo_root)
				iput(lsm->lsm_md_oinfo[i].lmo_root);
		}

		kvfree(lsm);
		*lsmp = NULL;
		return 0;
	}

	/* Alloc memmd */
	if (!lsm && !lmm) {
		lsm_size = lmv_stripe_md_size(stripe_count);
		lsm = libcfs_kvzalloc(lsm_size, GFP_NOFS);
		if (!lsm)
			return -ENOMEM;
		lsm->lsm_md_stripe_count = stripe_count;
		*lsmp = lsm;
		return 0;
	}

	if (le32_to_cpu(lmm->lmv_magic) == LMV_MAGIC_STRIPE)
		return -EPERM;

	/* Unpack memmd */
	if (le32_to_cpu(lmm->lmv_magic) != LMV_MAGIC_V1 &&
	    le32_to_cpu(lmm->lmv_magic) != LMV_USER_MAGIC) {
		CERROR("%s: invalid lmv magic %x: rc = %d\n",
		       exp->exp_obd->obd_name, le32_to_cpu(lmm->lmv_magic),
		       -EIO);
		return -EIO;
	}

	if (le32_to_cpu(lmm->lmv_magic) == LMV_MAGIC_V1)
		lsm_size = lmv_stripe_md_size(lmv_mds_md_stripe_count_get(lmm));
	else
		/**
		 * Unpack default dirstripe(lmv_user_md) to lmv_stripe_md,
		 * stripecount should be 0 then.
		 */
		lsm_size = lmv_stripe_md_size(0);

	if (!lsm) {
		lsm = libcfs_kvzalloc(lsm_size, GFP_NOFS);
		if (!lsm)
			return -ENOMEM;
		allocated = true;
		*lsmp = lsm;
	}

	switch (le32_to_cpu(lmm->lmv_magic)) {
	case LMV_MAGIC_V1:
		rc = lmv_unpack_md_v1(exp, lsm, &lmm->lmv_md_v1);
		break;
	default:
		CERROR("%s: unrecognized magic %x\n", exp->exp_obd->obd_name,
		       le32_to_cpu(lmm->lmv_magic));
		rc = -EINVAL;
		break;
	}

	if (rc && allocated) {
		kvfree(lsm);
		*lsmp = NULL;
		lsm_size = rc;
	}
	return lsm_size;
}
EXPORT_SYMBOL(lmv_unpack_md);

int lmv_unpackmd(struct obd_export *exp, struct lov_stripe_md **lsmp,
		 struct lov_mds_md *lmm, int disk_len)
{
	return lmv_unpack_md(exp, (struct lmv_stripe_md **)lsmp,
			     (union lmv_mds_md *)lmm, disk_len);
}

int lmv_packmd(struct obd_export *exp, struct lov_mds_md **lmmp,
	       struct lov_stripe_md *lsm)
{
	const struct lmv_stripe_md *lmv = (struct lmv_stripe_md *)lsm;
	struct obd_device *obd = exp->exp_obd;
	struct lmv_obd *lmv_obd = &obd->u.lmv;
	int stripe_count;

	if (!lmmp) {
		if (lsm)
			stripe_count = lmv->lsm_md_stripe_count;
		else
			stripe_count = lmv_obd->desc.ld_tgt_count;

		return lmv_mds_md_size(stripe_count, LMV_MAGIC_V1);
	}

	return lmv_pack_md((union lmv_mds_md **)lmmp, lmv, 0);
}

static int lmv_cancel_unused(struct obd_export *exp, const struct lu_fid *fid,
			     ldlm_policy_data_t *policy, enum ldlm_mode mode,
			     enum ldlm_cancel_flags flags, void *opaque)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	int		      rc = 0;
	int		      err;
	u32 i;

	LASSERT(fid);

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		struct lmv_tgt_desc *tgt = lmv->tgts[i];

		if (!tgt || !tgt->ltd_exp || !tgt->ltd_active)
			continue;

		err = md_cancel_unused(tgt->ltd_exp, fid, policy, mode, flags,
				       opaque);
		if (!rc)
			rc = err;
	}
	return rc;
}

static int lmv_set_lock_data(struct obd_export *exp, __u64 *lockh, void *data,
			     __u64 *bits)
{
	struct lmv_obd	  *lmv = &exp->exp_obd->u.lmv;
	struct lmv_tgt_desc *tgt = lmv->tgts[0];
	int		      rc;

	if (!tgt || !tgt->ltd_exp)
		return -EINVAL;

	rc = md_set_lock_data(tgt->ltd_exp, lockh, data, bits);
	return rc;
}

static enum ldlm_mode lmv_lock_match(struct obd_export *exp, __u64 flags,
				     const struct lu_fid *fid,
				     enum ldlm_type type,
				     ldlm_policy_data_t *policy,
				     enum ldlm_mode mode,
				     struct lustre_handle *lockh)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	enum ldlm_mode	      rc;
	int tgt;
	u32 i;

	CDEBUG(D_INODE, "Lock match for "DFID"\n", PFID(fid));

	/*
	 * With DNE every object can have two locks in different namespaces:
	 * lookup lock in space of MDT storing direntry and update/open lock in
	 * space of MDT storing inode.  Try the MDT that the FID maps to first,
	 * since this can be easily found, and only try others if that fails.
	 */
	for (i = 0, tgt = lmv_find_target_index(lmv, fid);
	     i < lmv->desc.ld_tgt_count;
	     i++, tgt = (tgt + 1) % lmv->desc.ld_tgt_count) {
		if (tgt < 0) {
			CDEBUG(D_HA, "%s: "DFID" is inaccessible: rc = %d\n",
			       obd->obd_name, PFID(fid), tgt);
			tgt = 0;
		}

		if (!lmv->tgts[tgt] || !lmv->tgts[tgt]->ltd_exp ||
		    !lmv->tgts[tgt]->ltd_active)
			continue;

		rc = md_lock_match(lmv->tgts[tgt]->ltd_exp, flags, fid,
				   type, policy, mode, lockh);
		if (rc)
			return rc;
	}

	return 0;
}

static int lmv_get_lustre_md(struct obd_export *exp,
			     struct ptlrpc_request *req,
			     struct obd_export *dt_exp,
			     struct obd_export *md_exp,
			     struct lustre_md *md)
{
	struct lmv_obd	  *lmv = &exp->exp_obd->u.lmv;
	struct lmv_tgt_desc *tgt = lmv->tgts[0];

	if (!tgt || !tgt->ltd_exp)
		return -EINVAL;
	return md_get_lustre_md(tgt->ltd_exp, req, dt_exp, md_exp, md);
}

static int lmv_free_lustre_md(struct obd_export *exp, struct lustre_md *md)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt = lmv->tgts[0];

	if (md->lmv) {
		lmv_free_memmd(md->lmv);
		md->lmv = NULL;
	}
	if (!tgt || !tgt->ltd_exp)
		return -EINVAL;
	return md_free_lustre_md(tgt->ltd_exp, md);
}

static int lmv_set_open_replay_data(struct obd_export *exp,
				    struct obd_client_handle *och,
				    struct lookup_intent *it)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;

	tgt = lmv_find_target(lmv, &och->och_fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	return md_set_open_replay_data(tgt->ltd_exp, och, it);
}

static int lmv_clear_open_replay_data(struct obd_export *exp,
				      struct obd_client_handle *och)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;

	tgt = lmv_find_target(lmv, &och->och_fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	return md_clear_open_replay_data(tgt->ltd_exp, och);
}

static int lmv_intent_getattr_async(struct obd_export *exp,
				    struct md_enqueue_info *minfo,
				    struct ldlm_enqueue_info *einfo)
{
	struct md_op_data       *op_data = &minfo->mi_data;
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt = NULL;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_intent_getattr_async(tgt->ltd_exp, minfo, einfo);
	return rc;
}

static int lmv_revalidate_lock(struct obd_export *exp, struct lookup_intent *it,
			       struct lu_fid *fid, __u64 *bits)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_revalidate_lock(tgt->ltd_exp, it, fid, bits);
	return rc;
}

int lmv_get_fid_from_lsm(struct obd_export *exp,
			 const struct lmv_stripe_md *lsm,
			 const char *name, int namelen, struct lu_fid *fid)
{
	const struct lmv_oinfo *oinfo;

	LASSERT(lsm);
	oinfo = lsm_name_to_stripe_info(lsm, name, namelen);
	if (IS_ERR(oinfo))
		return PTR_ERR(oinfo);

	*fid = oinfo->lmo_fid;

	return 0;
}

/**
 * For lmv, only need to send request to master MDT, and the master MDT will
 * process with other slave MDTs. The only exception is Q_GETOQUOTA for which
 * we directly fetch data from the slave MDTs.
 */
static int lmv_quotactl(struct obd_device *unused, struct obd_export *exp,
			struct obd_quotactl *oqctl)
{
	struct obd_device   *obd = class_exp2obd(exp);
	struct lmv_obd      *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt = lmv->tgts[0];
	int rc = 0;
	__u64 curspace = 0, curinodes = 0;
	u32 i;

	if (!tgt || !tgt->ltd_exp || !tgt->ltd_active ||
	    !lmv->desc.ld_tgt_count) {
		CERROR("master lmv inactive\n");
		return -EIO;
	}

	if (oqctl->qc_cmd != Q_GETOQUOTA) {
		rc = obd_quotactl(tgt->ltd_exp, oqctl);
		return rc;
	}

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		int err;

		tgt = lmv->tgts[i];

		if (!tgt || !tgt->ltd_exp || !tgt->ltd_active)
			continue;

		err = obd_quotactl(tgt->ltd_exp, oqctl);
		if (err) {
			CERROR("getquota on mdt %d failed. %d\n", i, err);
			if (!rc)
				rc = err;
		} else {
			curspace += oqctl->qc_dqblk.dqb_curspace;
			curinodes += oqctl->qc_dqblk.dqb_curinodes;
		}
	}
	oqctl->qc_dqblk.dqb_curspace = curspace;
	oqctl->qc_dqblk.dqb_curinodes = curinodes;

	return rc;
}

static int lmv_quotacheck(struct obd_device *unused, struct obd_export *exp,
			  struct obd_quotactl *oqctl)
{
	struct obd_device   *obd = class_exp2obd(exp);
	struct lmv_obd      *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt;
	int rc = 0;
	u32 i;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		int err;

		tgt = lmv->tgts[i];
		if (!tgt || !tgt->ltd_exp || !tgt->ltd_active) {
			CERROR("lmv idx %d inactive\n", i);
			return -EIO;
		}

		err = obd_quotacheck(tgt->ltd_exp, oqctl);
		if (err && !rc)
			rc = err;
	}

	return rc;
}

int lmv_update_lsm_md(struct obd_export *exp, struct lmv_stripe_md *lsm,
		      struct mdt_body *body, ldlm_blocking_callback cb_blocking)
{
	return lmv_revalidate_slaves(exp, body, lsm, cb_blocking, 0);
}

int lmv_merge_attr(struct obd_export *exp, const struct lmv_stripe_md *lsm,
		   struct cl_attr *attr)
{
	int i;

	for (i = 0; i < lsm->lsm_md_stripe_count; i++) {
		struct inode *inode = lsm->lsm_md_oinfo[i].lmo_root;

		CDEBUG(D_INFO, ""DFID" size %llu, nlink %u, atime %lu ctime %lu, mtime %lu.\n",
		       PFID(&lsm->lsm_md_oinfo[i].lmo_fid),
		       i_size_read(inode), inode->i_nlink,
		       LTIME_S(inode->i_atime), LTIME_S(inode->i_ctime),
		       LTIME_S(inode->i_mtime));

		/* for slave stripe, it needs to subtract nlink for . and .. */
		if (i)
			attr->cat_nlink += inode->i_nlink - 2;
		else
			attr->cat_nlink = inode->i_nlink;

		attr->cat_size += i_size_read(inode);

		if (attr->cat_atime < LTIME_S(inode->i_atime))
			attr->cat_atime = LTIME_S(inode->i_atime);

		if (attr->cat_ctime < LTIME_S(inode->i_ctime))
			attr->cat_ctime = LTIME_S(inode->i_ctime);

		if (attr->cat_mtime < LTIME_S(inode->i_mtime))
			attr->cat_mtime = LTIME_S(inode->i_mtime);
	}
	return 0;
}

static struct obd_ops lmv_obd_ops = {
	.owner		= THIS_MODULE,
	.setup		= lmv_setup,
	.cleanup	= lmv_cleanup,
	.precleanup	= lmv_precleanup,
	.process_config	= lmv_process_config,
	.connect	= lmv_connect,
	.disconnect	= lmv_disconnect,
	.statfs		= lmv_statfs,
	.get_info	= lmv_get_info,
	.set_info_async	= lmv_set_info_async,
	.packmd		= lmv_packmd,
	.unpackmd	= lmv_unpackmd,
	.notify		= lmv_notify,
	.get_uuid	= lmv_get_uuid,
	.iocontrol	= lmv_iocontrol,
	.quotacheck	= lmv_quotacheck,
	.quotactl	= lmv_quotactl
};

static struct md_ops lmv_md_ops = {
	.getstatus		= lmv_getstatus,
	.null_inode		= lmv_null_inode,
	.find_cbdata		= lmv_find_cbdata,
	.close			= lmv_close,
	.create			= lmv_create,
	.done_writing		= lmv_done_writing,
	.enqueue		= lmv_enqueue,
	.getattr		= lmv_getattr,
	.getxattr		= lmv_getxattr,
	.getattr_name		= lmv_getattr_name,
	.intent_lock		= lmv_intent_lock,
	.link			= lmv_link,
	.rename			= lmv_rename,
	.setattr		= lmv_setattr,
	.setxattr		= lmv_setxattr,
	.sync			= lmv_sync,
	.read_page		= lmv_read_page,
	.unlink			= lmv_unlink,
	.init_ea_size		= lmv_init_ea_size,
	.cancel_unused		= lmv_cancel_unused,
	.set_lock_data		= lmv_set_lock_data,
	.lock_match		= lmv_lock_match,
	.get_lustre_md		= lmv_get_lustre_md,
	.free_lustre_md		= lmv_free_lustre_md,
	.update_lsm_md		= lmv_update_lsm_md,
	.merge_attr		= lmv_merge_attr,
	.set_open_replay_data	= lmv_set_open_replay_data,
	.clear_open_replay_data	= lmv_clear_open_replay_data,
	.intent_getattr_async	= lmv_intent_getattr_async,
	.revalidate_lock	= lmv_revalidate_lock,
	.get_fid_from_lsm	= lmv_get_fid_from_lsm,
};

static int __init lmv_init(void)
{
	struct lprocfs_static_vars lvars;
	int			rc;

	lprocfs_lmv_init_vars(&lvars);

	rc = class_register_type(&lmv_obd_ops, &lmv_md_ops,
				 LUSTRE_LMV_NAME, NULL);
	return rc;
}

static void lmv_exit(void)
{
	class_unregister_type(LUSTRE_LMV_NAME);
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Logical Metadata Volume");
MODULE_VERSION(LUSTRE_VERSION_STRING);
MODULE_LICENSE("GPL");

module_init(lmv_init);
module_exit(lmv_exit);
