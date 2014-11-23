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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
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
#include <asm/uaccess.h>

#include "../include/lustre/lustre_idl.h"
#include "../include/obd_support.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_net.h"
#include "../include/obd_class.h"
#include "../include/lprocfs_status.h"
#include "../include/lustre_lite.h"
#include "../include/lustre_fid.h"
#include "lmv_internal.h"

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
static int lmv_set_mdc_active(struct lmv_obd *lmv, struct obd_uuid *uuid,
			      int activate)
{
	struct lmv_tgt_desc    *uninitialized_var(tgt);
	struct obd_device      *obd;
	int		     i;
	int		     rc = 0;

	CDEBUG(D_INFO, "Searching in lmv %p for uuid %s (activate=%d)\n",
	       lmv, uuid->uuid, activate);

	spin_lock(&lmv->lmv_lock);
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		tgt = lmv->tgts[i];
		if (tgt == NULL || tgt->ltd_exp == NULL)
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
	if (obd == NULL) {
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

struct obd_uuid *lmv_get_uuid(struct obd_export *exp)
{
	struct lmv_obd *lmv = &exp->exp_obd->u.lmv;

	return obd_get_uuid(lmv->tgts[0]->ltd_exp);
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
	struct proc_dir_entry *lmv_proc_dir;
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

	if (obd->obd_proc_private != NULL) {
		lmv_proc_dir = obd->obd_proc_private;
	} else {
		lmv_proc_dir = lprocfs_register("target_obds", obd->obd_proc_entry,
						NULL, NULL);
		if (IS_ERR(lmv_proc_dir)) {
			CERROR("could not register /proc/fs/lustre/%s/%s/target_obds.",
			       obd->obd_type->typ_name, obd->obd_name);
			lmv_proc_dir = NULL;
		}
		obd->obd_proc_private = lmv_proc_dir;
	}

	/*
	 * All real clients should perform actual connection right away, because
	 * it is possible, that LMV will not have opportunity to connect targets
	 * and MDC stuff will be called directly, for instance while reading
	 * ../mdc/../kbytesfree procfs file, etc.
	 */
	if (data->ocd_connect_flags & OBD_CONNECT_REAL)
		rc = lmv_check_connect(obd);

	if (rc && lmv_proc_dir) {
		lprocfs_remove(&lmv_proc_dir);
		obd->obd_proc_private = NULL;
	}

	return rc;
}

static void lmv_set_timeouts(struct obd_device *obd)
{
	struct lmv_tgt_desc   *tgt;
	struct lmv_obd	*lmv;
	int		    i;

	lmv = &obd->u.lmv;
	if (lmv->server_timeout == 0)
		return;

	if (lmv->connected == 0)
		return;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		tgt = lmv->tgts[i];
		if (tgt == NULL || tgt->ltd_exp == NULL || tgt->ltd_active == 0)
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
	int		  i;
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
		if (lmv->tgts[i] == NULL ||
		    lmv->tgts[i]->ltd_exp == NULL ||
		    lmv->tgts[i]->ltd_active == 0) {
			CWARN("%s: NULL export for %d\n", obd->obd_name, i);
			continue;
		}

		rc = md_init_ea_size(lmv->tgts[i]->ltd_exp, easize, def_easize,
				     cookiesize, def_cookiesize);
		if (rc) {
			CERROR("%s: obd_init_ea_size() failed on MDT target %d: rc = %d.\n",
			       obd->obd_name, i, rc);
			break;
		}
	}
	return rc;
}

#define MAX_STRING_SIZE 128

int lmv_connect_mdc(struct obd_device *obd, struct lmv_tgt_desc *tgt)
{
	struct proc_dir_entry   *lmv_proc_dir;
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
		tgt->ltd_uuid.uuid, obd->obd_uuid.uuid,
		cluuid->uuid);

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

	lmv_proc_dir = obd->obd_proc_private;
	if (lmv_proc_dir) {
		struct proc_dir_entry *mdc_symlink;

		LASSERT(mdc_obd->obd_type != NULL);
		LASSERT(mdc_obd->obd_type->typ_name != NULL);
		mdc_symlink = lprocfs_add_symlink(mdc_obd->obd_name,
						  lmv_proc_dir,
						  "../../../%s/%s",
						  mdc_obd->obd_type->typ_name,
						  mdc_obd->obd_name);
		if (mdc_symlink == NULL) {
			CERROR("Could not register LMV target /proc/fs/lustre/%s/%s/target_obds/%s.",
			       obd->obd_type->typ_name, obd->obd_name,
			       mdc_obd->obd_name);
			lprocfs_remove(&lmv_proc_dir);
			obd->obd_proc_private = NULL;
		}
	}
	return 0;
}

static void lmv_del_target(struct lmv_obd *lmv, int index)
{
	if (lmv->tgts[index] == NULL)
		return;

	OBD_FREE_PTR(lmv->tgts[index]);
	lmv->tgts[index] = NULL;
	return;
}

static int lmv_add_target(struct obd_device *obd, struct obd_uuid *uuidp,
			   __u32 index, int gen)
{
	struct lmv_obd      *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt;
	int		  rc = 0;

	CDEBUG(D_CONFIG, "Target uuid: %s. index %d\n", uuidp->uuid, index);

	lmv_init_lock(lmv);

	if (lmv->desc.ld_tgt_count == 0) {
		struct obd_device *mdc_obd;

		mdc_obd = class_find_client_obd(uuidp, LUSTRE_MDC_NAME,
						&obd->obd_uuid);
		if (!mdc_obd) {
			lmv_init_unlock(lmv);
			CERROR("%s: Target %s not attached: rc = %d\n",
			       obd->obd_name, uuidp->uuid, -EINVAL);
			return -EINVAL;
		}
	}

	if ((index < lmv->tgts_size) && (lmv->tgts[index] != NULL)) {
		tgt = lmv->tgts[index];
		CERROR("%s: UUID %s already assigned at LOV target index %d: rc = %d\n",
		       obd->obd_name,
		       obd_uuid2str(&tgt->ltd_uuid), index, -EEXIST);
		lmv_init_unlock(lmv);
		return -EEXIST;
	}

	if (index >= lmv->tgts_size) {
		/* We need to reallocate the lmv target array. */
		struct lmv_tgt_desc **newtgts, **old = NULL;
		__u32 newsize = 1;
		__u32 oldsize = 0;

		while (newsize < index + 1)
			newsize = newsize << 1;
		OBD_ALLOC(newtgts, sizeof(*newtgts) * newsize);
		if (newtgts == NULL) {
			lmv_init_unlock(lmv);
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
		if (old)
			OBD_FREE(old, sizeof(*old) * oldsize);

		CDEBUG(D_CONFIG, "tgts: %p size: %d\n", lmv->tgts,
		       lmv->tgts_size);
	}

	OBD_ALLOC_PTR(tgt);
	if (!tgt) {
		lmv_init_unlock(lmv);
		return -ENOMEM;
	}

	mutex_init(&tgt->ltd_fid_mutex);
	tgt->ltd_idx = index;
	tgt->ltd_uuid = *uuidp;
	tgt->ltd_active = 0;
	lmv->tgts[index] = tgt;
	if (index >= lmv->desc.ld_tgt_count)
		lmv->desc.ld_tgt_count = index + 1;

	if (lmv->connected) {
		rc = lmv_connect_mdc(obd, tgt);
		if (rc) {
			spin_lock(&lmv->lmv_lock);
			lmv->desc.ld_tgt_count--;
			memset(tgt, 0, sizeof(*tgt));
			spin_unlock(&lmv->lmv_lock);
		} else {
			int easize = sizeof(struct lmv_stripe_md) +
				lmv->desc.ld_tgt_count * sizeof(struct lu_fid);
			lmv_init_ea_size(obd->obd_self_export, easize, 0, 0, 0);
		}
	}

	lmv_init_unlock(lmv);
	return rc;
}

int lmv_check_connect(struct obd_device *obd)
{
	struct lmv_obd       *lmv = &obd->u.lmv;
	struct lmv_tgt_desc  *tgt;
	int		   i;
	int		   rc;
	int		   easize;

	if (lmv->connected)
		return 0;

	lmv_init_lock(lmv);
	if (lmv->connected) {
		lmv_init_unlock(lmv);
		return 0;
	}

	if (lmv->desc.ld_tgt_count == 0) {
		lmv_init_unlock(lmv);
		CERROR("%s: no targets configured.\n", obd->obd_name);
		return -EINVAL;
	}

	CDEBUG(D_CONFIG, "Time to connect %s to %s\n",
	       lmv->cluuid.uuid, obd->obd_name);

	LASSERT(lmv->tgts != NULL);

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		tgt = lmv->tgts[i];
		if (tgt == NULL)
			continue;
		rc = lmv_connect_mdc(obd, tgt);
		if (rc)
			goto out_disc;
	}

	lmv_set_timeouts(obd);
	class_export_put(lmv->exp);
	lmv->connected = 1;
	easize = lmv_get_easize(lmv);
	lmv_init_ea_size(obd->obd_self_export, easize, 0, 0, 0);
	lmv_init_unlock(lmv);
	return 0;

 out_disc:
	while (i-- > 0) {
		int rc2;
		tgt = lmv->tgts[i];
		if (tgt == NULL)
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
	lmv_init_unlock(lmv);
	return rc;
}

static int lmv_disconnect_mdc(struct obd_device *obd, struct lmv_tgt_desc *tgt)
{
	struct proc_dir_entry  *lmv_proc_dir;
	struct lmv_obd	 *lmv = &obd->u.lmv;
	struct obd_device      *mdc_obd;
	int		     rc;

	LASSERT(tgt != NULL);
	LASSERT(obd != NULL);

	mdc_obd = class_exp2obd(tgt->ltd_exp);

	if (mdc_obd) {
		mdc_obd->obd_force = obd->obd_force;
		mdc_obd->obd_fail = obd->obd_fail;
		mdc_obd->obd_no_recov = obd->obd_no_recov;
	}

	lmv_proc_dir = obd->obd_proc_private;
	if (lmv_proc_dir)
		lprocfs_remove_proc_entry(mdc_obd->obd_name, lmv_proc_dir);

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
	int		    i;

	if (!lmv->tgts)
		goto out_local;

	/*
	 * Only disconnect the underlying layers on the final disconnect.
	 */
	lmv->refcount--;
	if (lmv->refcount != 0)
		goto out_local;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (lmv->tgts[i] == NULL || lmv->tgts[i]->ltd_exp == NULL)
			continue;

		lmv_disconnect_mdc(obd, lmv->tgts[i]);
	}

	if (obd->obd_proc_private)
		lprocfs_remove((struct proc_dir_entry **)&obd->obd_proc_private);
	else
		CERROR("/proc/fs/lustre/%s/%s/target_obds missing\n",
		       obd->obd_type->typ_name, obd->obd_name);

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

static int lmv_fid2path(struct obd_export *exp, int len, void *karg, void *uarg)
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
	 * path on the remote MDT, copy this path segment to gf */
	if (remote_gf != NULL) {
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
	if (remote_gf == NULL) {
		remote_gf_size = sizeof(*remote_gf) + PATH_MAX;
		OBD_ALLOC(remote_gf, remote_gf_size);
		if (remote_gf == NULL) {
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
	if (remote_gf != NULL)
		OBD_FREE(remote_gf, remote_gf_size);
	return rc;
}

static int lmv_hsm_req_count(struct lmv_obd *lmv,
			     const struct hsm_user_request *hur,
			     const struct lmv_tgt_desc *tgt_mds)
{
	int			i, nr = 0;
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
				 struct lustre_kernelcomm *lk, void *uarg)
{
	int	i, rc = 0;

	/* unregister request (call from llapi_hsm_copytool_fini) */
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		/* best effort: try to clean as much as possible
		 * (continue on error) */
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
			       struct lustre_kernelcomm *lk, void *uarg)
{
	struct file	*filp;
	int		 i, j, err;
	int		 rc = 0;
	bool		 any_set = false;

	/* All or nothing: try to register to all MDS.
	 * In case of failure, unregister from previous MDS,
	 * except if it because of inactive target. */
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		err = obd_iocontrol(cmd, lmv->tgts[i]->ltd_exp,
				   len, lk, uarg);
		if (err) {
			if (lmv->tgts[i]->ltd_active) {
				/* permanent error */
				CERROR("error: iocontrol MDC %s on MDTidx %d cmd %x: err = %d\n",
				       lmv->tgts[i]->ltd_uuid.uuid,
				       i, cmd, err);
				rc = err;
				lk->lk_flags |= LK_FLG_STOP;
				/* unregister from previous MDS */
				for (j = 0; j < i; j++)
					obd_iocontrol(cmd,
						  lmv->tgts[j]->ltd_exp,
						  len, lk, uarg);
				return rc;
			}
			/* else: transient error.
			 * kuc will register to the missing MDT
			 * when it is back */
		} else {
			any_set = true;
		}
	}

	if (!any_set)
		/* no registration done: return error */
		return -ENOTCONN;

	/* at least one registration done, with no failure */
	filp = fget(lk->lk_wfd);
	if (filp == NULL) {
		return -EBADF;
	}
	rc = libcfs_kkuc_group_add(filp, lk->lk_uid, lk->lk_group, lk->lk_data);
	if (rc != 0 && filp != NULL)
		fput(filp);
	return rc;
}




static int lmv_iocontrol(unsigned int cmd, struct obd_export *exp,
			 int len, void *karg, void *uarg)
{
	struct obd_device    *obddev = class_exp2obd(exp);
	struct lmv_obd       *lmv = &obddev->u.lmv;
	int		   i = 0;
	int		   rc = 0;
	int		   set = 0;
	int		   count = lmv->desc.ld_tgt_count;

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

		if (lmv->tgts[index] == NULL ||
		    lmv->tgts[index]->ltd_active == 0)
			return -ENODATA;

		mdc_obd = class_exp2obd(lmv->tgts[index]->ltd_exp);
		if (!mdc_obd)
			return -EINVAL;

		/* copy UUID */
		if (copy_to_user(data->ioc_pbuf2, obd2cli_tgt(mdc_obd),
				     min((int) data->ioc_plen2,
					 (int) sizeof(struct obd_uuid))))
			return -EFAULT;

		rc = obd_statfs(NULL, lmv->tgts[index]->ltd_exp, &stat_buf,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				0);
		if (rc)
			return rc;
		if (copy_to_user(data->ioc_pbuf1, &stat_buf,
				     min((int) data->ioc_plen1,
					 (int) sizeof(stat_buf))))
			return -EFAULT;
		break;
	}
	case OBD_IOC_QUOTACTL: {
		struct if_quotactl *qctl = karg;
		struct lmv_tgt_desc *tgt = NULL;
		struct obd_quotactl *oqctl;

		if (qctl->qc_valid == QC_MDTIDX) {
			if (qctl->qc_idx < 0 || count <= qctl->qc_idx)
				return -EINVAL;

			tgt = lmv->tgts[qctl->qc_idx];
			if (tgt == NULL || tgt->ltd_exp == NULL)
				return -EINVAL;
		} else if (qctl->qc_valid == QC_UUID) {
			for (i = 0; i < count; i++) {
				tgt = lmv->tgts[i];
				if (tgt == NULL)
					continue;
				if (!obd_uuid_equals(&tgt->ltd_uuid,
						     &qctl->obd_uuid))
					continue;

				if (tgt->ltd_exp == NULL)
					return -EINVAL;

				break;
			}
		} else {
			return -EINVAL;
		}

		if (i >= count)
			return -EAGAIN;

		LASSERT(tgt && tgt->ltd_exp);
		OBD_ALLOC_PTR(oqctl);
		if (!oqctl)
			return -ENOMEM;

		QCTL_COPY(oqctl, qctl);
		rc = obd_quotactl(tgt->ltd_exp, oqctl);
		if (rc == 0) {
			QCTL_COPY(qctl, oqctl);
			qctl->qc_valid = QC_MDTIDX;
			qctl->obd_uuid = tgt->ltd_uuid;
		}
		OBD_FREE_PTR(oqctl);
		break;
	}
	case OBD_IOC_CHANGELOG_SEND:
	case OBD_IOC_CHANGELOG_CLEAR: {
		struct ioc_changelog *icc = karg;

		if (icc->icc_mdtindex >= count)
			return -ENODEV;

		if (lmv->tgts[icc->icc_mdtindex] == NULL ||
		    lmv->tgts[icc->icc_mdtindex]->ltd_exp == NULL ||
		    lmv->tgts[icc->icc_mdtindex]->ltd_active == 0)
			return -ENODEV;
		rc = obd_iocontrol(cmd, lmv->tgts[icc->icc_mdtindex]->ltd_exp,
				   sizeof(*icc), icc, NULL);
		break;
	}
	case LL_IOC_GET_CONNECT_FLAGS: {
		if (lmv->tgts[0] == NULL)
			return -ENODATA;
		rc = obd_iocontrol(cmd, lmv->tgts[0]->ltd_exp, len, karg, uarg);
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
		struct lmv_tgt_desc	*tgt;

		tgt = lmv_find_target(lmv, &op_data->op_fid1);
		if (IS_ERR(tgt))
				return PTR_ERR(tgt);

		if (tgt->ltd_exp == NULL)
				return -EINVAL;

		rc = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
		break;
	}
	case LL_IOC_HSM_PROGRESS: {
		const struct hsm_progress_kernel *hpk = karg;
		struct lmv_tgt_desc	*tgt;

		tgt = lmv_find_target(lmv, &hpk->hpk_fid);
		if (IS_ERR(tgt))
			return PTR_ERR(tgt);
		rc = obd_iocontrol(cmd, tgt->ltd_exp, len, karg, uarg);
		break;
	}
	case LL_IOC_HSM_REQUEST: {
		struct hsm_user_request *hur = karg;
		struct lmv_tgt_desc	*tgt;
		unsigned int reqcount = hur->hur_request.hr_itemcount;

		if (reqcount == 0)
			return 0;

		/* if the request is about a single fid
		 * or if there is a single MDS, no need to split
		 * the request. */
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

				nr = lmv_hsm_req_count(lmv, hur, lmv->tgts[i]);
				if (nr == 0) /* nothing for this MDS */
					continue;

				/* build a request with fids for this MDS */
				reqlen = offsetof(typeof(*hur),
						  hur_user_item[nr])
					 + hur->hur_request.hr_data_len;
				OBD_ALLOC_LARGE(req, reqlen);
				if (req == NULL)
					return -ENOMEM;

				lmv_hsm_req_build(lmv, hur, lmv->tgts[i], req);

				rc1 = obd_iocontrol(cmd, lmv->tgts[i]->ltd_exp,
						    reqlen, req, uarg);
				if (rc1 != 0 && rc == 0)
					rc = rc1;
				OBD_FREE_LARGE(req, reqlen);
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

		if ((tgt1->ltd_exp == NULL) || (tgt2->ltd_exp == NULL))
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

			if (lmv->tgts[i] == NULL ||
			    lmv->tgts[i]->ltd_exp == NULL)
				continue;
			/* ll_umount_begin() sets force flag but for lmv, not
			 * mdc. Let's pass it through */
			mdc_obd = class_exp2obd(lmv->tgts[i]->ltd_exp);
			mdc_obd->obd_force = obddev->obd_force;
			err = obd_iocontrol(cmd, lmv->tgts[i]->ltd_exp, len,
					    karg, uarg);
			if (err == -ENODATA && cmd == OBD_IOC_POLL_QUOTACHECK) {
				return err;
			} else if (err) {
				if (lmv->tgts[i]->ltd_active) {
					CERROR("error: iocontrol MDC %s on MDTidx %d cmd %x: err = %d\n",
					       lmv->tgts[i]->ltd_uuid.uuid,
					       i, cmd, err);
					if (!rc)
						rc = err;
				}
			} else
				set = 1;
		}
		if (!set && !rc)
			rc = -EIO;
	}
	return rc;
}

#if 0
static int lmv_all_chars_policy(int count, const char *name,
				int len)
{
	unsigned int c = 0;

	while (len > 0)
		c += name[--len];
	c = c % count;
	return c;
}

static int lmv_nid_policy(struct lmv_obd *lmv)
{
	struct obd_import *imp;
	__u32	      id;

	/*
	 * XXX: To get nid we assume that underlying obd device is mdc.
	 */
	imp = class_exp2cliimp(lmv->tgts[0].ltd_exp);
	id = imp->imp_connection->c_self ^ (imp->imp_connection->c_self >> 32);
	return id % lmv->desc.ld_tgt_count;
}

static int lmv_choose_mds(struct lmv_obd *lmv, struct md_op_data *op_data,
			  enum placement_policy placement)
{
	switch (placement) {
	case PLACEMENT_CHAR_POLICY:
		return lmv_all_chars_policy(lmv->desc.ld_tgt_count,
					    op_data->op_name,
					    op_data->op_namelen);
	case PLACEMENT_NID_POLICY:
		return lmv_nid_policy(lmv);

	default:
		break;
	}

	CERROR("Unsupported placement policy %x\n", placement);
	return -EINVAL;
}
#endif

/**
 * This is _inode_ placement policy function (not name).
 */
static int lmv_placement_policy(struct obd_device *obd,
				struct md_op_data *op_data, u32 *mds)
{
	struct lmv_obd	  *lmv = &obd->u.lmv;

	LASSERT(mds != NULL);

	if (lmv->desc.ld_tgt_count == 1) {
		*mds = 0;
		return 0;
	}

	/**
	 * If stripe_offset is provided during setdirstripe
	 * (setdirstripe -i xx), xx MDS will be chosen.
	 */
	if (op_data->op_cli_flags & CLI_SET_MEA) {
		struct lmv_user_md *lum;

		lum = (struct lmv_user_md *)op_data->op_data;
		if (lum->lum_type == LMV_STRIPE_TYPE &&
		    lum->lum_stripe_offset != -1) {
			if (lum->lum_stripe_offset >= lmv->desc.ld_tgt_count) {
				CERROR("%s: Stripe_offset %d > MDT count %d: rc = %d\n",
				       obd->obd_name,
				       lum->lum_stripe_offset,
				       lmv->desc.ld_tgt_count, -ERANGE);
				return -ERANGE;
			}
			*mds = lum->lum_stripe_offset;
			return 0;
		}
	}

	/* Allocate new fid on target according to operation type and parent
	 * home mds. */
	*mds = op_data->op_mds;
	return 0;
}

int __lmv_fid_alloc(struct lmv_obd *lmv, struct lu_fid *fid, u32 mds)
{
	struct lmv_tgt_desc	*tgt;
	int			 rc;

	tgt = lmv_get_target(lmv, mds);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	/*
	 * New seq alloc and FLD setup should be atomic. Otherwise we may find
	 * on server that seq in new allocated fid is not yet known.
	 */
	mutex_lock(&tgt->ltd_fid_mutex);

	if (tgt->ltd_active == 0 || tgt->ltd_exp == NULL) {
		rc = -ENODEV;
		goto out;
	}

	/*
	 * Asking underlaying tgt layer to allocate new fid.
	 */
	rc = obd_fid_alloc(tgt->ltd_exp, fid, NULL);
	if (rc > 0) {
		LASSERT(fid_is_sane(fid));
		rc = 0;
	}

out:
	mutex_unlock(&tgt->ltd_fid_mutex);
	return rc;
}

int lmv_fid_alloc(struct obd_export *exp, struct lu_fid *fid,
		  struct md_op_data *op_data)
{
	struct obd_device     *obd = class_exp2obd(exp);
	struct lmv_obd	*lmv = &obd->u.lmv;
	u32		       mds = 0;
	int		    rc;

	LASSERT(op_data != NULL);
	LASSERT(fid != NULL);

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
	struct lprocfs_static_vars  lvars;
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

	OBD_ALLOC(lmv->tgts, sizeof(*lmv->tgts) * 32);
	if (lmv->tgts == NULL)
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
	mutex_init(&lmv->init_mutex);

	lprocfs_lmv_init_vars(&lvars);

	lprocfs_obd_setup(obd, lvars.obd_vars);
#if defined (CONFIG_PROC_FS)
	{
		rc = lprocfs_seq_create(obd->obd_proc_entry, "target_obd",
					0444, &lmv_proc_target_fops, obd);
		if (rc)
			CWARN("%s: error adding LMV target_obd file: rc = %d\n",
			       obd->obd_name, rc);
       }
#endif
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
	if (lmv->tgts != NULL) {
		int i;
		for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
			if (lmv->tgts[i] == NULL)
				continue;
			lmv_del_target(lmv, i);
		}
		OBD_FREE(lmv->tgts, sizeof(*lmv->tgts) * lmv->tgts_size);
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
		 * 2:0  3:1  4:lustre-MDT0000-mdc_UUID */
		if (LUSTRE_CFG_BUFLEN(lcfg, 1) > sizeof(obd_uuid.uuid)) {
			rc = -EINVAL;
			goto out;
		}

		obd_str2uuid(&obd_uuid,  lustre_cfg_buf(lcfg, 1));

		if (sscanf(lustre_cfg_buf(lcfg, 2), "%d", &index) != 1) {
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
	int		    i;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	OBD_ALLOC(temp, sizeof(*temp));
	if (temp == NULL)
		return -ENOMEM;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (lmv->tgts[i] == NULL || lmv->tgts[i]->ltd_exp == NULL)
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
			 * MDT0 is in service*/
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
	OBD_FREE(temp, sizeof(*temp));
	return rc;
}

static int lmv_getstatus(struct obd_export *exp,
			 struct lu_fid *fid,
			 struct obd_capa **pc)
{
	struct obd_device    *obd = exp->exp_obd;
	struct lmv_obd       *lmv = &obd->u.lmv;
	int		   rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	rc = md_getstatus(lmv->tgts[0]->ltd_exp, fid, pc);
	return rc;
}

static int lmv_getxattr(struct obd_export *exp, const struct lu_fid *fid,
			struct obd_capa *oc, u64 valid, const char *name,
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

	rc = md_getxattr(tgt->ltd_exp, fid, oc, valid, name, input,
			 input_size, output_size, flags, request);

	return rc;
}

static int lmv_setxattr(struct obd_export *exp, const struct lu_fid *fid,
			struct obd_capa *oc, u64 valid, const char *name,
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

	rc = md_setxattr(tgt->ltd_exp, fid, oc, valid, name, input,
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
	int		  i;
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
		if (lmv->tgts[i] == NULL || lmv->tgts[i]->ltd_exp == NULL)
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
	int		  i;
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
		if (lmv->tgts[i] == NULL || lmv->tgts[i]->ltd_exp == NULL)
			continue;
		rc = md_find_cbdata(lmv->tgts[i]->ltd_exp, fid, it, data);
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

struct lmv_tgt_desc
*lmv_locate_mds(struct lmv_obd *lmv, struct md_op_data *op_data,
		struct lu_fid *fid)
{
	struct lmv_tgt_desc *tgt;

	tgt = lmv_find_target(lmv, fid);
	if (IS_ERR(tgt))
		return tgt;

	op_data->op_mds = tgt->ltd_idx;

	return tgt;
}

int lmv_create(struct obd_export *exp, struct md_op_data *op_data,
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

	rc = lmv_fid_alloc(exp, &op_data->op_fid2, op_data);
	if (rc)
		return rc;

	CDEBUG(D_INODE, "CREATE '%*s' on "DFID" -> mds #%x\n",
	       op_data->op_namelen, op_data->op_name, PFID(&op_data->op_fid1),
	       op_data->op_mds);

	op_data->op_flags |= MF_MDC_CANCEL_FID1;
	rc = md_create(tgt->ltd_exp, op_data, data, datalen, mode, uid, gid,
		       cap_effective, rdev, request);

	if (rc == 0) {
		if (*request == NULL)
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
lmv_enqueue_remote(struct obd_export *exp, struct ldlm_enqueue_info *einfo,
		   struct lookup_intent *it, struct md_op_data *op_data,
		   struct lustre_handle *lockh, void *lmm, int lmmsize,
		   __u64 extra_lock_flags)
{
	struct ptlrpc_request      *req = it->d.lustre.it_data;
	struct obd_device	  *obd = exp->exp_obd;
	struct lmv_obd	     *lmv = &obd->u.lmv;
	struct lustre_handle	plock;
	struct lmv_tgt_desc	*tgt;
	struct md_op_data	  *rdata;
	struct lu_fid	       fid1;
	struct mdt_body	    *body;
	int			 rc = 0;
	int			 pmode;

	body = req_capsule_server_get(&req->rq_pill, &RMF_MDT_BODY);
	LASSERT(body != NULL);

	if (!(body->valid & OBD_MD_MDS))
		return 0;

	CDEBUG(D_INODE, "REMOTE_ENQUEUE '%s' on "DFID" -> "DFID"\n",
	       LL_IT2STR(it), PFID(&op_data->op_fid1), PFID(&body->fid1));

	/*
	 * We got LOOKUP lock, but we really need attrs.
	 */
	pmode = it->d.lustre.it_lock_mode;
	LASSERT(pmode != 0);
	memcpy(&plock, lockh, sizeof(plock));
	it->d.lustre.it_lock_mode = 0;
	it->d.lustre.it_data = NULL;
	fid1 = body->fid1;

	ptlrpc_req_finished(req);

	tgt = lmv_find_target(lmv, &fid1);
	if (IS_ERR(tgt)) {
		rc = PTR_ERR(tgt);
		goto out;
	}

	OBD_ALLOC_PTR(rdata);
	if (rdata == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rdata->op_fid1 = fid1;
	rdata->op_bias = MDS_CROSS_REF;

	rc = md_enqueue(tgt->ltd_exp, einfo, it, rdata, lockh,
			lmm, lmmsize, NULL, extra_lock_flags);
	OBD_FREE_PTR(rdata);
out:
	ldlm_lock_decref(&plock, pmode);
	return rc;
}

static int
lmv_enqueue(struct obd_export *exp, struct ldlm_enqueue_info *einfo,
	    struct lookup_intent *it, struct md_op_data *op_data,
	    struct lustre_handle *lockh, void *lmm, int lmmsize,
	    struct ptlrpc_request **req, __u64 extra_lock_flags)
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

	rc = md_enqueue(tgt->ltd_exp, einfo, it, op_data, lockh,
			lmm, lmmsize, req, extra_lock_flags);

	if (rc == 0 && it && it->it_op == IT_OPEN) {
		rc = lmv_enqueue_remote(exp, einfo, it, op_data, lockh,
					lmm, lmmsize, extra_lock_flags);
	}
	return rc;
}

static int
lmv_getattr_name(struct obd_export *exp, struct md_op_data *op_data,
		 struct ptlrpc_request **request)
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

	rc = md_getattr_name(tgt->ltd_exp, op_data, request);
	if (rc != 0)
		return rc;

	body = req_capsule_server_get(&(*request)->rq_pill,
				      &RMF_MDT_BODY);
	LASSERT(body != NULL);

	if (body->valid & OBD_MD_MDS) {
		struct lu_fid rid = body->fid1;
		CDEBUG(D_INODE, "Request attrs for "DFID"\n",
		       PFID(&rid));

		tgt = lmv_find_target(lmv, &rid);
		if (IS_ERR(tgt)) {
			ptlrpc_req_finished(*request);
			return PTR_ERR(tgt);
		}

		op_data->op_fid1 = rid;
		op_data->op_valid |= OBD_MD_FLCROSSREF;
		op_data->op_namelen = 0;
		op_data->op_name = NULL;
		rc = md_getattr_name(tgt->ltd_exp, op_data, &req);
		ptlrpc_req_finished(*request);
		*request = req;
	}

	return rc;
}

#define md_op_data_fid(op_data, fl)		     \
	(fl == MF_MDC_CANCEL_FID1 ? &op_data->op_fid1 : \
	 fl == MF_MDC_CANCEL_FID2 ? &op_data->op_fid2 : \
	 fl == MF_MDC_CANCEL_FID3 ? &op_data->op_fid3 : \
	 fl == MF_MDC_CANCEL_FID4 ? &op_data->op_fid4 : \
	 NULL)

static int lmv_early_cancel(struct obd_export *exp, struct md_op_data *op_data,
			    int op_tgt, ldlm_mode_t mode, int bits, int flag)
{
	struct lu_fid	  *fid = md_op_data_fid(op_data, flag);
	struct obd_device      *obd = exp->exp_obd;
	struct lmv_obd	 *lmv = &obd->u.lmv;
	struct lmv_tgt_desc    *tgt;
	ldlm_policy_data_t      policy = {{0}};
	int		     rc = 0;

	if (!fid_is_sane(fid))
		return 0;

	tgt = lmv_find_target(lmv, fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

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
	tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid2);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	/*
	 * Cancel UPDATE lock on child (fid1).
	 */
	op_data->op_flags |= MF_MDC_CANCEL_FID2;
	rc = lmv_early_cancel(exp, op_data, tgt->ltd_idx, LCK_EX,
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
	struct lmv_tgt_desc     *tgt_tgt;
	int			rc;

	LASSERT(oldlen != 0);

	CDEBUG(D_INODE, "RENAME %*s in "DFID" to %*s in "DFID"\n",
	       oldlen, old, PFID(&op_data->op_fid1),
	       newlen, new, PFID(&op_data->op_fid2));

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	op_data->op_fsuid = from_kuid(&init_user_ns, current_fsuid());
	op_data->op_fsgid = from_kgid(&init_user_ns, current_fsgid());
	op_data->op_cap = cfs_curproc_cap_pack();
	src_tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid1);
	if (IS_ERR(src_tgt))
		return PTR_ERR(src_tgt);

	tgt_tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid2);
	if (IS_ERR(tgt_tgt))
		return PTR_ERR(tgt_tgt);
	/*
	 * LOOKUP lock on src child (fid3) should also be cancelled for
	 * src_tgt in mdc_rename.
	 */
	op_data->op_flags |= MF_MDC_CANCEL_FID1 | MF_MDC_CANCEL_FID3;

	/*
	 * Cancel UPDATE locks on tgt parent (fid2), tgt_tgt is its
	 * own target.
	 */
	rc = lmv_early_cancel(exp, op_data, src_tgt->ltd_idx,
			      LCK_EX, MDS_INODELOCK_UPDATE,
			      MF_MDC_CANCEL_FID2);

	/*
	 * Cancel LOOKUP locks on tgt child (fid4) for parent tgt_tgt.
	 */
	if (rc == 0) {
		rc = lmv_early_cancel(exp, op_data, src_tgt->ltd_idx,
				      LCK_EX, MDS_INODELOCK_LOOKUP,
				      MF_MDC_CANCEL_FID4);
	}

	/*
	 * Cancel all the locks on tgt child (fid4).
	 */
	if (rc == 0)
		rc = lmv_early_cancel(exp, op_data, src_tgt->ltd_idx,
				      LCK_EX, MDS_INODELOCK_FULL,
				      MF_MDC_CANCEL_FID4);

	if (rc == 0)
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
	int		      rc = 0;

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
		    struct obd_capa *oc, struct ptlrpc_request **request)
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

	rc = md_sync(tgt->ltd_exp, fid, oc, request);
	return rc;
}

/*
 * Adjust a set of pages, each page containing an array of lu_dirpages,
 * so that each page can be used as a single logical lu_dirpage.
 *
 * A lu_dirpage is laid out as follows, where s = ldp_hash_start,
 * e = ldp_hash_end, f = ldp_flags, p = padding, and each "ent" is a
 * struct lu_dirent.  It has size up to LU_PAGE_SIZE. The ldp_hash_end
 * value is used as a cookie to request the next lu_dirpage in a
 * directory listing that spans multiple pages (two in this example):
 *   ________
 *  |	|
 * .|--------v-------   -----.
 * |s|e|f|p|ent|ent| ... |ent|
 * '--|--------------   -----'   Each CFS_PAGE contains a single
 *    '------.		   lu_dirpage.
 * .---------v-------   -----.
 * |s|e|f|p|ent| 0 | ... | 0 |
 * '-----------------   -----'
 *
 * However, on hosts where the native VM page size (PAGE_CACHE_SIZE) is
 * larger than LU_PAGE_SIZE, a single host page may contain multiple
 * lu_dirpages. After reading the lu_dirpages from the MDS, the
 * ldp_hash_end of the first lu_dirpage refers to the one immediately
 * after it in the same CFS_PAGE (arrows simplified for brevity, but
 * in general e0==s1, e1==s2, etc.):
 *
 * .--------------------   -----.
 * |s0|e0|f0|p|ent|ent| ... |ent|
 * |---v----------------   -----|
 * |s1|e1|f1|p|ent|ent| ... |ent|
 * |---v----------------   -----|  Here, each CFS_PAGE contains
 *	     ...		 multiple lu_dirpages.
 * |---v----------------   -----|
 * |s'|e'|f'|p|ent|ent| ... |ent|
 * '---|----------------   -----'
 *     v
 * .----------------------------.
 * |	next CFS_PAGE       |
 *
 * This structure is transformed into a single logical lu_dirpage as follows:
 *
 * - Replace e0 with e' so the request for the next lu_dirpage gets the page
 *   labeled 'next CFS_PAGE'.
 *
 * - Copy the LDF_COLLIDE flag from f' to f0 to correctly reflect whether
 *   a hash collision with the next page exists.
 *
 * - Adjust the lde_reclen of the ending entry of each lu_dirpage to span
 *   to the first entry of the next lu_dirpage.
 */
#if PAGE_CACHE_SIZE > LU_PAGE_SIZE
static void lmv_adjust_dirpages(struct page **pages, int ncfspgs, int nlupgs)
{
	int i;

	for (i = 0; i < ncfspgs; i++) {
		struct lu_dirpage	*dp = kmap(pages[i]);
		struct lu_dirpage	*first = dp;
		struct lu_dirent	*end_dirent = NULL;
		struct lu_dirent	*ent;
		__u64			hash_end = dp->ldp_hash_end;
		__u32			flags = dp->ldp_flags;

		while (--nlupgs > 0) {
			ent = lu_dirent_start(dp);
			for (end_dirent = ent; ent != NULL;
			     end_dirent = ent, ent = lu_dirent_next(ent));

			/* Advance dp to next lu_dirpage. */
			dp = (struct lu_dirpage *)((char *)dp + LU_PAGE_SIZE);

			/* Check if we've reached the end of the CFS_PAGE. */
			if (!((unsigned long)dp & ~CFS_PAGE_MASK))
				break;

			/* Save the hash and flags of this lu_dirpage. */
			hash_end = dp->ldp_hash_end;
			flags = dp->ldp_flags;

			/* Check if lu_dirpage contains no entries. */
			if (!end_dirent)
				break;

			/* Enlarge the end entry lde_reclen from 0 to
			 * first entry of next lu_dirpage. */
			LASSERT(le16_to_cpu(end_dirent->lde_reclen) == 0);
			end_dirent->lde_reclen =
				cpu_to_le16((char *)(dp->ldp_entries) -
					    (char *)end_dirent);
		}

		first->ldp_hash_end = hash_end;
		first->ldp_flags &= ~cpu_to_le32(LDF_COLLIDE);
		first->ldp_flags |= flags & cpu_to_le32(LDF_COLLIDE);

		kunmap(pages[i]);
	}
	LASSERTF(nlupgs == 0, "left = %d", nlupgs);
}
#else
#define lmv_adjust_dirpages(pages, ncfspgs, nlupgs) do {} while (0)
#endif	/* PAGE_CACHE_SIZE > LU_PAGE_SIZE */

static int lmv_readpage(struct obd_export *exp, struct md_op_data *op_data,
			struct page **pages, struct ptlrpc_request **request)
{
	struct obd_device	*obd = exp->exp_obd;
	struct lmv_obd		*lmv = &obd->u.lmv;
	__u64			offset = op_data->op_offset;
	int			rc;
	int			ncfspgs; /* pages read in PAGE_CACHE_SIZE */
	int			nlupgs; /* pages read in LU_PAGE_SIZE */
	struct lmv_tgt_desc	*tgt;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	CDEBUG(D_INODE, "READPAGE at %#llx from "DFID"\n",
	       offset, PFID(&op_data->op_fid1));

	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_readpage(tgt->ltd_exp, op_data, pages, request);
	if (rc != 0)
		return rc;

	ncfspgs = ((*request)->rq_bulk->bd_nob_transferred + PAGE_CACHE_SIZE - 1)
		 >> PAGE_CACHE_SHIFT;
	nlupgs = (*request)->rq_bulk->bd_nob_transferred >> LU_PAGE_SHIFT;
	LASSERT(!((*request)->rq_bulk->bd_nob_transferred & ~LU_PAGE_MASK));
	LASSERT(ncfspgs > 0 && ncfspgs <= op_data->op_npages);

	CDEBUG(D_INODE, "read %d(%d)/%d pages\n", ncfspgs, nlupgs,
	       op_data->op_npages);

	lmv_adjust_dirpages(pages, ncfspgs, nlupgs);

	return rc;
}

static int lmv_unlink(struct obd_export *exp, struct md_op_data *op_data,
		      struct ptlrpc_request **request)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt = NULL;
	struct mdt_body		*body;
	int		     rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;
retry:
	/* Send unlink requests to the MDT where the child is located */
	if (likely(!fid_is_zero(&op_data->op_fid2)))
		tgt = lmv_locate_mds(lmv, op_data, &op_data->op_fid2);
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
	rc = lmv_early_cancel(exp, op_data, tgt->ltd_idx, LCK_EX,
			      MDS_INODELOCK_FULL, MF_MDC_CANCEL_FID3);

	if (rc != 0)
		return rc;

	CDEBUG(D_INODE, "unlink with fid="DFID"/"DFID" -> mds #%d\n",
	       PFID(&op_data->op_fid1), PFID(&op_data->op_fid2), tgt->ltd_idx);

	rc = md_unlink(tgt->ltd_exp, op_data, request);
	if (rc != 0 && rc != -EREMOTE)
		return rc;

	body = req_capsule_server_get(&(*request)->rq_pill, &RMF_MDT_BODY);
	if (body == NULL)
		return -EPROTO;

	/* Not cross-ref case, just get out of here. */
	if (likely(!(body->valid & OBD_MD_MDS)))
		return 0;

	CDEBUG(D_INODE, "%s: try unlink to another MDT for "DFID"\n",
	       exp->exp_obd->obd_name, PFID(&body->fid1));

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
	 * be very rare case.  */
	op_data->op_fid2 = body->fid1;
	ptlrpc_req_finished(*request);
	*request = NULL;

	goto retry;
}

static int lmv_precleanup(struct obd_device *obd, enum obd_cleanup_stage stage)
{
	struct lmv_obd *lmv = &obd->u.lmv;

	switch (stage) {
	case OBD_CLEANUP_EARLY:
		/* XXX: here should be calling obd_precleanup() down to
		 * stack. */
		break;
	case OBD_CLEANUP_EXPORTS:
		fld_client_proc_fini(&lmv->lmv_fld);
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
	if (obd == NULL) {
		CDEBUG(D_IOCTL, "Invalid client cookie %#llx\n",
		       exp->exp_handle.h_cookie);
		return -EINVAL;
	}

	lmv = &obd->u.lmv;
	if (keylen >= strlen("remote_flag") && !strcmp(key, "remote_flag")) {
		struct lmv_tgt_desc *tgt;
		int i;

		rc = lmv_check_connect(obd);
		if (rc)
			return rc;

		LASSERT(*vallen == sizeof(__u32));
		for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
			tgt = lmv->tgts[i];
			/*
			 * All tgts should be connected when this gets called.
			 */
			if (tgt == NULL || tgt->ltd_exp == NULL)
				continue;

			if (!obd_get_info(env, tgt->ltd_exp, keylen, key,
					  vallen, val, NULL))
				return 0;
		}
		return -EINVAL;
	} else if (KEY_IS(KEY_MAX_EASIZE) ||
		   KEY_IS(KEY_DEFAULT_EASIZE) ||
		   KEY_IS(KEY_MAX_COOKIESIZE) ||
		   KEY_IS(KEY_DEFAULT_COOKIESIZE) ||
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

int lmv_set_info_async(const struct lu_env *env, struct obd_export *exp,
		       u32 keylen, void *key, u32 vallen,
		       void *val, struct ptlrpc_request_set *set)
{
	struct lmv_tgt_desc    *tgt;
	struct obd_device      *obd;
	struct lmv_obd	 *lmv;
	int rc = 0;

	obd = class_exp2obd(exp);
	if (obd == NULL) {
		CDEBUG(D_IOCTL, "Invalid client cookie %#llx\n",
		       exp->exp_handle.h_cookie);
		return -EINVAL;
	}
	lmv = &obd->u.lmv;

	if (KEY_IS(KEY_READ_ONLY) || KEY_IS(KEY_FLUSH_CTX)) {
		int i, err = 0;

		for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
			tgt = lmv->tgts[i];

			if (tgt == NULL || tgt->ltd_exp == NULL)
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

int lmv_packmd(struct obd_export *exp, struct lov_mds_md **lmmp,
	       struct lov_stripe_md *lsm)
{
	struct obd_device	 *obd = class_exp2obd(exp);
	struct lmv_obd	    *lmv = &obd->u.lmv;
	struct lmv_stripe_md      *meap;
	struct lmv_stripe_md      *lsmp;
	int			mea_size;
	int			i;

	mea_size = lmv_get_easize(lmv);
	if (!lmmp)
		return mea_size;

	if (*lmmp && !lsm) {
		OBD_FREE_LARGE(*lmmp, mea_size);
		*lmmp = NULL;
		return 0;
	}

	if (*lmmp == NULL) {
		OBD_ALLOC_LARGE(*lmmp, mea_size);
		if (*lmmp == NULL)
			return -ENOMEM;
	}

	if (!lsm)
		return mea_size;

	lsmp = (struct lmv_stripe_md *)lsm;
	meap = (struct lmv_stripe_md *)*lmmp;

	if (lsmp->mea_magic != MEA_MAGIC_LAST_CHAR &&
	    lsmp->mea_magic != MEA_MAGIC_ALL_CHARS)
		return -EINVAL;

	meap->mea_magic = cpu_to_le32(lsmp->mea_magic);
	meap->mea_count = cpu_to_le32(lsmp->mea_count);
	meap->mea_master = cpu_to_le32(lsmp->mea_master);

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		meap->mea_ids[i] = lsmp->mea_ids[i];
		fid_cpu_to_le(&meap->mea_ids[i], &lsmp->mea_ids[i]);
	}

	return mea_size;
}

int lmv_unpackmd(struct obd_export *exp, struct lov_stripe_md **lsmp,
		 struct lov_mds_md *lmm, int lmm_size)
{
	struct obd_device	  *obd = class_exp2obd(exp);
	struct lmv_stripe_md      **tmea = (struct lmv_stripe_md **)lsmp;
	struct lmv_stripe_md       *mea = (struct lmv_stripe_md *)lmm;
	struct lmv_obd	     *lmv = &obd->u.lmv;
	int			 mea_size;
	int			 i;
	__u32		       magic;

	mea_size = lmv_get_easize(lmv);
	if (lsmp == NULL)
		return mea_size;

	if (*lsmp != NULL && lmm == NULL) {
		OBD_FREE_LARGE(*tmea, mea_size);
		*lsmp = NULL;
		return 0;
	}

	LASSERT(mea_size == lmm_size);

	OBD_ALLOC_LARGE(*tmea, mea_size);
	if (*tmea == NULL)
		return -ENOMEM;

	if (!lmm)
		return mea_size;

	if (mea->mea_magic == MEA_MAGIC_LAST_CHAR ||
	    mea->mea_magic == MEA_MAGIC_ALL_CHARS ||
	    mea->mea_magic == MEA_MAGIC_HASH_SEGMENT) {
		magic = le32_to_cpu(mea->mea_magic);
	} else {
		/*
		 * Old mea is not handled here.
		 */
		CERROR("Old not supportable EA is found\n");
		LBUG();
	}

	(*tmea)->mea_magic = magic;
	(*tmea)->mea_count = le32_to_cpu(mea->mea_count);
	(*tmea)->mea_master = le32_to_cpu(mea->mea_master);

	for (i = 0; i < (*tmea)->mea_count; i++) {
		(*tmea)->mea_ids[i] = mea->mea_ids[i];
		fid_le_to_cpu(&(*tmea)->mea_ids[i], &(*tmea)->mea_ids[i]);
	}
	return mea_size;
}

static int lmv_cancel_unused(struct obd_export *exp, const struct lu_fid *fid,
			     ldlm_policy_data_t *policy, ldlm_mode_t mode,
			     ldlm_cancel_flags_t flags, void *opaque)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	int		      rc = 0;
	int		      err;
	int		      i;

	LASSERT(fid != NULL);

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (lmv->tgts[i] == NULL || lmv->tgts[i]->ltd_exp == NULL ||
		    lmv->tgts[i]->ltd_active == 0)
			continue;

		err = md_cancel_unused(lmv->tgts[i]->ltd_exp, fid,
				       policy, mode, flags, opaque);
		if (!rc)
			rc = err;
	}
	return rc;
}

int lmv_set_lock_data(struct obd_export *exp, __u64 *lockh, void *data,
		      __u64 *bits)
{
	struct lmv_obd	  *lmv = &exp->exp_obd->u.lmv;
	int		      rc;

	rc =  md_set_lock_data(lmv->tgts[0]->ltd_exp, lockh, data, bits);
	return rc;
}

ldlm_mode_t lmv_lock_match(struct obd_export *exp, __u64 flags,
			   const struct lu_fid *fid, ldlm_type_t type,
			   ldlm_policy_data_t *policy, ldlm_mode_t mode,
			   struct lustre_handle *lockh)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	ldlm_mode_t	      rc;
	int		      i;

	CDEBUG(D_INODE, "Lock match for "DFID"\n", PFID(fid));

	/*
	 * With CMD every object can have two locks in different namespaces:
	 * lookup lock in space of mds storing direntry and update/open lock in
	 * space of mds storing inode. Thus we check all targets, not only that
	 * one fid was created in.
	 */
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		if (lmv->tgts[i] == NULL ||
		    lmv->tgts[i]->ltd_exp == NULL ||
		    lmv->tgts[i]->ltd_active == 0)
			continue;

		rc = md_lock_match(lmv->tgts[i]->ltd_exp, flags, fid,
				   type, policy, mode, lockh);
		if (rc)
			return rc;
	}

	return 0;
}

int lmv_get_lustre_md(struct obd_export *exp, struct ptlrpc_request *req,
		      struct obd_export *dt_exp, struct obd_export *md_exp,
		      struct lustre_md *md)
{
	struct lmv_obd	  *lmv = &exp->exp_obd->u.lmv;

	return md_get_lustre_md(lmv->tgts[0]->ltd_exp, req, dt_exp, md_exp, md);
}

int lmv_free_lustre_md(struct obd_export *exp, struct lustre_md *md)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;

	if (md->mea)
		obd_free_memmd(exp, (void *)&md->mea);
	return md_free_lustre_md(lmv->tgts[0]->ltd_exp, md);
}

int lmv_set_open_replay_data(struct obd_export *exp,
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

int lmv_clear_open_replay_data(struct obd_export *exp,
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

static int lmv_get_remote_perm(struct obd_export *exp,
			       const struct lu_fid *fid,
			       struct obd_capa *oc, __u32 suppgid,
			       struct ptlrpc_request **request)
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

	rc = md_get_remote_perm(tgt->ltd_exp, fid, oc, suppgid, request);
	return rc;
}

static int lmv_renew_capa(struct obd_export *exp, struct obd_capa *oc,
			  renew_capa_cb_t cb)
{
	struct obd_device       *obd = exp->exp_obd;
	struct lmv_obd	  *lmv = &obd->u.lmv;
	struct lmv_tgt_desc     *tgt;
	int		      rc;

	rc = lmv_check_connect(obd);
	if (rc)
		return rc;

	tgt = lmv_find_target(lmv, &oc->c_capa.lc_fid);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_renew_capa(tgt->ltd_exp, oc, cb);
	return rc;
}

int lmv_unpack_capa(struct obd_export *exp, struct ptlrpc_request *req,
		    const struct req_msg_field *field, struct obd_capa **oc)
{
	struct lmv_obd *lmv = &exp->exp_obd->u.lmv;

	return md_unpack_capa(lmv->tgts[0]->ltd_exp, req, field, oc);
}

int lmv_intent_getattr_async(struct obd_export *exp,
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

	tgt = lmv_find_target(lmv, &op_data->op_fid1);
	if (IS_ERR(tgt))
		return PTR_ERR(tgt);

	rc = md_intent_getattr_async(tgt->ltd_exp, minfo, einfo);
	return rc;
}

int lmv_revalidate_lock(struct obd_export *exp, struct lookup_intent *it,
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

/**
 * For lmv, only need to send request to master MDT, and the master MDT will
 * process with other slave MDTs. The only exception is Q_GETOQUOTA for which
 * we directly fetch data from the slave MDTs.
 */
int lmv_quotactl(struct obd_device *unused, struct obd_export *exp,
		 struct obd_quotactl *oqctl)
{
	struct obd_device   *obd = class_exp2obd(exp);
	struct lmv_obd      *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt = lmv->tgts[0];
	int		  rc = 0, i;
	__u64		curspace, curinodes;

	if (!lmv->desc.ld_tgt_count || !tgt->ltd_active) {
		CERROR("master lmv inactive\n");
		return -EIO;
	}

	if (oqctl->qc_cmd != Q_GETOQUOTA) {
		rc = obd_quotactl(tgt->ltd_exp, oqctl);
		return rc;
	}

	curspace = curinodes = 0;
	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		int err;
		tgt = lmv->tgts[i];

		if (tgt == NULL || tgt->ltd_exp == NULL || tgt->ltd_active == 0)
			continue;
		if (!tgt->ltd_active) {
			CDEBUG(D_HA, "mdt %d is inactive.\n", i);
			continue;
		}

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

int lmv_quotacheck(struct obd_device *unused, struct obd_export *exp,
		   struct obd_quotactl *oqctl)
{
	struct obd_device   *obd = class_exp2obd(exp);
	struct lmv_obd      *lmv = &obd->u.lmv;
	struct lmv_tgt_desc *tgt;
	int		  i, rc = 0;

	for (i = 0; i < lmv->desc.ld_tgt_count; i++) {
		int err;
		tgt = lmv->tgts[i];
		if (tgt == NULL || tgt->ltd_exp == NULL || !tgt->ltd_active) {
			CERROR("lmv idx %d inactive\n", i);
			return -EIO;
		}

		err = obd_quotacheck(tgt->ltd_exp, oqctl);
		if (err && !rc)
			rc = err;
	}

	return rc;
}

struct obd_ops lmv_obd_ops = {
	.o_owner		= THIS_MODULE,
	.o_setup		= lmv_setup,
	.o_cleanup	      = lmv_cleanup,
	.o_precleanup	   = lmv_precleanup,
	.o_process_config       = lmv_process_config,
	.o_connect	      = lmv_connect,
	.o_disconnect	   = lmv_disconnect,
	.o_statfs	       = lmv_statfs,
	.o_get_info	     = lmv_get_info,
	.o_set_info_async       = lmv_set_info_async,
	.o_packmd	       = lmv_packmd,
	.o_unpackmd	     = lmv_unpackmd,
	.o_notify	       = lmv_notify,
	.o_get_uuid	     = lmv_get_uuid,
	.o_iocontrol	    = lmv_iocontrol,
	.o_quotacheck	   = lmv_quotacheck,
	.o_quotactl	     = lmv_quotactl
};

struct md_ops lmv_md_ops = {
	.m_getstatus	    = lmv_getstatus,
	.m_null_inode		= lmv_null_inode,
	.m_find_cbdata	  = lmv_find_cbdata,
	.m_close		= lmv_close,
	.m_create	       = lmv_create,
	.m_done_writing	 = lmv_done_writing,
	.m_enqueue	      = lmv_enqueue,
	.m_getattr	      = lmv_getattr,
	.m_getxattr	     = lmv_getxattr,
	.m_getattr_name	 = lmv_getattr_name,
	.m_intent_lock	  = lmv_intent_lock,
	.m_link		 = lmv_link,
	.m_rename	       = lmv_rename,
	.m_setattr	      = lmv_setattr,
	.m_setxattr	     = lmv_setxattr,
	.m_sync		 = lmv_sync,
	.m_readpage	     = lmv_readpage,
	.m_unlink	       = lmv_unlink,
	.m_init_ea_size	 = lmv_init_ea_size,
	.m_cancel_unused	= lmv_cancel_unused,
	.m_set_lock_data	= lmv_set_lock_data,
	.m_lock_match	   = lmv_lock_match,
	.m_get_lustre_md	= lmv_get_lustre_md,
	.m_free_lustre_md       = lmv_free_lustre_md,
	.m_set_open_replay_data = lmv_set_open_replay_data,
	.m_clear_open_replay_data = lmv_clear_open_replay_data,
	.m_renew_capa	   = lmv_renew_capa,
	.m_unpack_capa	  = lmv_unpack_capa,
	.m_get_remote_perm      = lmv_get_remote_perm,
	.m_intent_getattr_async = lmv_intent_getattr_async,
	.m_revalidate_lock      = lmv_revalidate_lock
};

int __init lmv_init(void)
{
	struct lprocfs_static_vars lvars;
	int			rc;

	lprocfs_lmv_init_vars(&lvars);

	rc = class_register_type(&lmv_obd_ops, &lmv_md_ops,
				 lvars.module_vars, LUSTRE_LMV_NAME, NULL);
	return rc;
}

static void lmv_exit(void)
{
	class_unregister_type(LUSTRE_LMV_NAME);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Logical Metadata Volume OBD driver");
MODULE_LICENSE("GPL");

module_init(lmv_init);
module_exit(lmv_exit);
