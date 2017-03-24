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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/lov/lov_obd.c
 *
 * Author: Phil Schwan <phil@clusterfs.com>
 * Author: Peter Braam <braam@clusterfs.com>
 * Author: Mike Shaver <shaver@clusterfs.com>
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LOV
#include "../../include/linux/libcfs/libcfs.h"

#include "../include/lustre/lustre_idl.h"
#include "../include/lustre/lustre_ioctl.h"

#include "../include/cl_object.h"
#include "../include/lustre_dlm.h"
#include "../include/lustre_fid.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_mds.h"
#include "../include/lustre_net.h"
#include "../include/lustre_param.h"
#include "../include/lustre_swab.h"
#include "../include/lprocfs_status.h"
#include "../include/obd_class.h"
#include "../include/obd_support.h"

#include "lov_internal.h"

/* Keep a refcount of lov->tgt usage to prevent racing with addition/deletion.
 * Any function that expects lov_tgts to remain stationary must take a ref.
 */
static void lov_getref(struct obd_device *obd)
{
	struct lov_obd *lov = &obd->u.lov;

	/* nobody gets through here until lov_putref is done */
	mutex_lock(&lov->lov_lock);
	atomic_inc(&lov->lov_refcount);
	mutex_unlock(&lov->lov_lock);
	return;
}

static void __lov_del_obd(struct obd_device *obd, struct lov_tgt_desc *tgt);

static void lov_putref(struct obd_device *obd)
{
	struct lov_obd *lov = &obd->u.lov;

	mutex_lock(&lov->lov_lock);
	/* ok to dec to 0 more than once -- ltd_exp's will be null */
	if (atomic_dec_and_test(&lov->lov_refcount) && lov->lov_death_row) {
		LIST_HEAD(kill);
		int i;
		struct lov_tgt_desc *tgt, *n;

		CDEBUG(D_CONFIG, "destroying %d lov targets\n",
		       lov->lov_death_row);
		for (i = 0; i < lov->desc.ld_tgt_count; i++) {
			tgt = lov->lov_tgts[i];

			if (!tgt || !tgt->ltd_reap)
				continue;
			list_add(&tgt->ltd_kill, &kill);
			/* XXX - right now there is a dependency on ld_tgt_count
			 * being the maximum tgt index for computing the
			 * mds_max_easize. So we can't shrink it.
			 */
			lov_ost_pool_remove(&lov->lov_packed, i);
			lov->lov_tgts[i] = NULL;
			lov->lov_death_row--;
		}
		mutex_unlock(&lov->lov_lock);

		list_for_each_entry_safe(tgt, n, &kill, ltd_kill) {
			list_del(&tgt->ltd_kill);
			/* Disconnect */
			__lov_del_obd(obd, tgt);
		}

		if (lov->lov_tgts_kobj)
			kobject_put(lov->lov_tgts_kobj);

	} else {
		mutex_unlock(&lov->lov_lock);
	}
}

static int lov_set_osc_active(struct obd_device *obd, struct obd_uuid *uuid,
			      enum obd_notify_event ev);
static int lov_notify(struct obd_device *obd, struct obd_device *watched,
		      enum obd_notify_event ev, void *data);

int lov_connect_obd(struct obd_device *obd, __u32 index, int activate,
		    struct obd_connect_data *data)
{
	struct lov_obd *lov = &obd->u.lov;
	struct obd_uuid *tgt_uuid;
	struct obd_device *tgt_obd;
	static struct obd_uuid lov_osc_uuid = { "LOV_OSC_UUID" };
	struct obd_import *imp;
	int rc;

	if (!lov->lov_tgts[index])
		return -EINVAL;

	tgt_uuid = &lov->lov_tgts[index]->ltd_uuid;
	tgt_obd = lov->lov_tgts[index]->ltd_obd;

	if (!tgt_obd->obd_set_up) {
		CERROR("Target %s not set up\n", obd_uuid2str(tgt_uuid));
		return -EINVAL;
	}

	/* override the sp_me from lov */
	tgt_obd->u.cli.cl_sp_me = lov->lov_sp_me;

	if (data && (data->ocd_connect_flags & OBD_CONNECT_INDEX))
		data->ocd_index = index;

	/*
	 * Divine LOV knows that OBDs under it are OSCs.
	 */
	imp = tgt_obd->u.cli.cl_import;

	if (activate) {
		tgt_obd->obd_no_recov = 0;
		/* FIXME this is probably supposed to be
		 * ptlrpc_set_import_active.  Horrible naming.
		 */
		ptlrpc_activate_import(imp);
	}

	rc = obd_register_observer(tgt_obd, obd);
	if (rc) {
		CERROR("Target %s register_observer error %d\n",
		       obd_uuid2str(tgt_uuid), rc);
		return rc;
	}

	if (imp->imp_invalid) {
		CDEBUG(D_CONFIG, "not connecting OSC %s; administratively disabled\n",
		       obd_uuid2str(tgt_uuid));
		return 0;
	}

	rc = obd_connect(NULL, &lov->lov_tgts[index]->ltd_exp, tgt_obd,
			 &lov_osc_uuid, data, NULL);
	if (rc || !lov->lov_tgts[index]->ltd_exp) {
		CERROR("Target %s connect error %d\n",
		       obd_uuid2str(tgt_uuid), rc);
		return -ENODEV;
	}

	lov->lov_tgts[index]->ltd_reap = 0;

	CDEBUG(D_CONFIG, "Connected tgt idx %d %s (%s) %sactive\n", index,
	       obd_uuid2str(tgt_uuid), tgt_obd->obd_name, activate ? "":"in");

	if (lov->lov_tgts_kobj)
		/* Even if we failed, that's ok */
		rc = sysfs_create_link(lov->lov_tgts_kobj, &tgt_obd->obd_kobj,
				       tgt_obd->obd_name);

	return 0;
}

static int lov_connect(const struct lu_env *env,
		       struct obd_export **exp, struct obd_device *obd,
		       struct obd_uuid *cluuid, struct obd_connect_data *data,
		       void *localdata)
{
	struct lov_obd *lov = &obd->u.lov;
	struct lov_tgt_desc *tgt;
	struct lustre_handle conn;
	int i, rc;

	CDEBUG(D_CONFIG, "connect #%d\n", lov->lov_connects);

	rc = class_connect(&conn, obd, cluuid);
	if (rc)
		return rc;

	*exp = class_conn2export(&conn);

	/* Why should there ever be more than 1 connect? */
	lov->lov_connects++;
	LASSERT(lov->lov_connects == 1);

	memset(&lov->lov_ocd, 0, sizeof(lov->lov_ocd));
	if (data)
		lov->lov_ocd = *data;

	obd_getref(obd);

	lov->lov_tgts_kobj = kobject_create_and_add("target_obds",
						    &obd->obd_kobj);

	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		tgt = lov->lov_tgts[i];
		if (!tgt || obd_uuid_empty(&tgt->ltd_uuid))
			continue;
		/* Flags will be lowest common denominator */
		rc = lov_connect_obd(obd, i, tgt->ltd_activate, &lov->lov_ocd);
		if (rc) {
			CERROR("%s: lov connect tgt %d failed: %d\n",
			       obd->obd_name, i, rc);
			continue;
		}
		/* connect to administrative disabled ost */
		if (!lov->lov_tgts[i]->ltd_exp)
			continue;

		rc = lov_notify(obd, lov->lov_tgts[i]->ltd_exp->exp_obd,
				OBD_NOTIFY_CONNECT, (void *)&i);
		if (rc) {
			CERROR("%s error sending notify %d\n",
			       obd->obd_name, rc);
		}
	}
	obd_putref(obd);

	return 0;
}

static int lov_disconnect_obd(struct obd_device *obd, struct lov_tgt_desc *tgt)
{
	struct lov_obd *lov = &obd->u.lov;
	struct obd_device *osc_obd;
	int rc;

	osc_obd = class_exp2obd(tgt->ltd_exp);
	CDEBUG(D_CONFIG, "%s: disconnecting target %s\n",
	       obd->obd_name, osc_obd ? osc_obd->obd_name : "NULL");

	if (tgt->ltd_active) {
		tgt->ltd_active = 0;
		lov->desc.ld_active_tgt_count--;
		tgt->ltd_exp->exp_obd->obd_inactive = 1;
	}

	if (osc_obd) {
		if (lov->lov_tgts_kobj)
			sysfs_remove_link(lov->lov_tgts_kobj,
					  osc_obd->obd_name);

		/* Pass it on to our clients.
		 * XXX This should be an argument to disconnect,
		 * XXX not a back-door flag on the OBD.  Ah well.
		 */
		osc_obd->obd_force = obd->obd_force;
		osc_obd->obd_fail = obd->obd_fail;
		osc_obd->obd_no_recov = obd->obd_no_recov;
	}

	obd_register_observer(osc_obd, NULL);

	rc = obd_disconnect(tgt->ltd_exp);
	if (rc) {
		CERROR("Target %s disconnect error %d\n",
		       tgt->ltd_uuid.uuid, rc);
		rc = 0;
	}

	tgt->ltd_exp = NULL;
	return 0;
}

static int lov_disconnect(struct obd_export *exp)
{
	struct obd_device *obd = class_exp2obd(exp);
	struct lov_obd *lov = &obd->u.lov;
	int i, rc;

	if (!lov->lov_tgts)
		goto out;

	/* Only disconnect the underlying layers on the final disconnect. */
	lov->lov_connects--;
	if (lov->lov_connects != 0) {
		/* why should there be more than 1 connect? */
		CERROR("disconnect #%d\n", lov->lov_connects);
		goto out;
	}

	/* Let's hold another reference so lov_del_obd doesn't spin through
	 * putref every time
	 */
	obd_getref(obd);

	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		if (lov->lov_tgts[i] && lov->lov_tgts[i]->ltd_exp) {
			/* Disconnection is the last we know about an obd */
			lov_del_target(obd, i, NULL, lov->lov_tgts[i]->ltd_gen);
		}
	}

	obd_putref(obd);

out:
	rc = class_disconnect(exp); /* bz 9811 */
	return rc;
}

/* Error codes:
 *
 *  -EINVAL  : UUID can't be found in the LOV's target list
 *  -ENOTCONN: The UUID is found, but the target connection is bad (!)
 *  -EBADF   : The UUID is found, but the OBD is the wrong type (!)
 *  any >= 0 : is log target index
 */
static int lov_set_osc_active(struct obd_device *obd, struct obd_uuid *uuid,
			      enum obd_notify_event ev)
{
	struct lov_obd *lov = &obd->u.lov;
	struct lov_tgt_desc *tgt;
	int index, activate, active;

	CDEBUG(D_INFO, "Searching in lov %p for uuid %s event(%d)\n",
	       lov, uuid->uuid, ev);

	obd_getref(obd);
	for (index = 0; index < lov->desc.ld_tgt_count; index++) {
		tgt = lov->lov_tgts[index];
		if (!tgt)
			continue;
		/*
		 * LU-642, initially inactive OSC could miss the obd_connect,
		 * we make up for it here.
		 */
		if (ev == OBD_NOTIFY_ACTIVATE && !tgt->ltd_exp &&
		    obd_uuid_equals(uuid, &tgt->ltd_uuid)) {
			struct obd_uuid lov_osc_uuid = {"LOV_OSC_UUID"};

			obd_connect(NULL, &tgt->ltd_exp, tgt->ltd_obd,
				    &lov_osc_uuid, &lov->lov_ocd, NULL);
		}
		if (!tgt->ltd_exp)
			continue;

		CDEBUG(D_INFO, "lov idx %d is %s conn %#llx\n",
		       index, obd_uuid2str(&tgt->ltd_uuid),
		       tgt->ltd_exp->exp_handle.h_cookie);
		if (obd_uuid_equals(uuid, &tgt->ltd_uuid))
			break;
	}

	if (index == lov->desc.ld_tgt_count) {
		index = -EINVAL;
		goto out;
	}

	if (ev == OBD_NOTIFY_DEACTIVATE || ev == OBD_NOTIFY_ACTIVATE) {
		activate = (ev == OBD_NOTIFY_ACTIVATE) ? 1 : 0;

		if (lov->lov_tgts[index]->ltd_activate == activate) {
			CDEBUG(D_INFO, "OSC %s already %sactivate!\n",
			       uuid->uuid, activate ? "" : "de");
		} else {
			lov->lov_tgts[index]->ltd_activate = activate;
			CDEBUG(D_CONFIG, "%sactivate OSC %s\n",
			       activate ? "" : "de", obd_uuid2str(uuid));
		}

	} else if (ev == OBD_NOTIFY_INACTIVE || ev == OBD_NOTIFY_ACTIVE) {
		active = (ev == OBD_NOTIFY_ACTIVE) ? 1 : 0;

		if (lov->lov_tgts[index]->ltd_active == active) {
			CDEBUG(D_INFO, "OSC %s already %sactive!\n",
			       uuid->uuid, active ? "" : "in");
			goto out;
		}
		CDEBUG(D_CONFIG, "Marking OSC %s %sactive\n",
		       obd_uuid2str(uuid), active ? "" : "in");

		lov->lov_tgts[index]->ltd_active = active;
		if (active) {
			lov->desc.ld_active_tgt_count++;
			lov->lov_tgts[index]->ltd_exp->exp_obd->obd_inactive = 0;
		} else {
			lov->desc.ld_active_tgt_count--;
			lov->lov_tgts[index]->ltd_exp->exp_obd->obd_inactive = 1;
		}
	} else {
		CERROR("Unknown event(%d) for uuid %s", ev, uuid->uuid);
	}

 out:
	obd_putref(obd);
	return index;
}

static int lov_notify(struct obd_device *obd, struct obd_device *watched,
		      enum obd_notify_event ev, void *data)
{
	int rc = 0;
	struct lov_obd *lov = &obd->u.lov;

	down_read(&lov->lov_notify_lock);
	if (!lov->lov_connects) {
		up_read(&lov->lov_notify_lock);
		return rc;
	}

	if (ev == OBD_NOTIFY_ACTIVE || ev == OBD_NOTIFY_INACTIVE ||
	    ev == OBD_NOTIFY_ACTIVATE || ev == OBD_NOTIFY_DEACTIVATE) {
		struct obd_uuid *uuid;

		LASSERT(watched);

		if (strcmp(watched->obd_type->typ_name, LUSTRE_OSC_NAME)) {
			up_read(&lov->lov_notify_lock);
			CERROR("unexpected notification of %s %s!\n",
			       watched->obd_type->typ_name,
			       watched->obd_name);
			return -EINVAL;
		}
		uuid = &watched->u.cli.cl_target_uuid;

		/* Set OSC as active before notifying the observer, so the
		 * observer can use the OSC normally.
		 */
		rc = lov_set_osc_active(obd, uuid, ev);
		if (rc < 0) {
			up_read(&lov->lov_notify_lock);
			CERROR("event(%d) of %s failed: %d\n", ev,
			       obd_uuid2str(uuid), rc);
			return rc;
		}
		/* active event should be pass lov target index as data */
		data = &rc;
	}

	/* Pass the notification up the chain. */
	if (watched) {
		rc = obd_notify_observer(obd, watched, ev, data);
	} else {
		/* NULL watched means all osc's in the lov (only for syncs) */
		/* sync event should be send lov idx as data */
		struct lov_obd *lov = &obd->u.lov;
		int i, is_sync;

		data = &i;
		is_sync = (ev == OBD_NOTIFY_SYNC) ||
			  (ev == OBD_NOTIFY_SYNC_NONBLOCK);

		obd_getref(obd);
		for (i = 0; i < lov->desc.ld_tgt_count; i++) {
			if (!lov->lov_tgts[i])
				continue;

			/* don't send sync event if target not
			 * connected/activated
			 */
			if (is_sync &&  !lov->lov_tgts[i]->ltd_active)
				continue;

			rc = obd_notify_observer(obd, lov->lov_tgts[i]->ltd_obd,
						 ev, data);
			if (rc) {
				CERROR("%s: notify %s of %s failed %d\n",
				       obd->obd_name,
				       obd->obd_observer->obd_name,
				       lov->lov_tgts[i]->ltd_obd->obd_name,
				       rc);
			}
		}
		obd_putref(obd);
	}

	up_read(&lov->lov_notify_lock);
	return rc;
}

static int lov_add_target(struct obd_device *obd, struct obd_uuid *uuidp,
			  __u32 index, int gen, int active)
{
	struct lov_obd *lov = &obd->u.lov;
	struct lov_tgt_desc *tgt;
	struct obd_device *tgt_obd;
	int rc;

	CDEBUG(D_CONFIG, "uuid:%s idx:%d gen:%d active:%d\n",
	       uuidp->uuid, index, gen, active);

	if (gen <= 0) {
		CERROR("request to add OBD %s with invalid generation: %d\n",
		       uuidp->uuid, gen);
		return -EINVAL;
	}

	tgt_obd = class_find_client_obd(uuidp, LUSTRE_OSC_NAME,
					&obd->obd_uuid);
	if (!tgt_obd)
		return -EINVAL;

	mutex_lock(&lov->lov_lock);

	if ((index < lov->lov_tgt_size) && lov->lov_tgts[index]) {
		tgt = lov->lov_tgts[index];
		CERROR("UUID %s already assigned at LOV target index %d\n",
		       obd_uuid2str(&tgt->ltd_uuid), index);
		mutex_unlock(&lov->lov_lock);
		return -EEXIST;
	}

	if (index >= lov->lov_tgt_size) {
		/* We need to reallocate the lov target array. */
		struct lov_tgt_desc **newtgts, **old = NULL;
		__u32 newsize, oldsize = 0;

		newsize = max_t(__u32, lov->lov_tgt_size, 2);
		while (newsize < index + 1)
			newsize <<= 1;
		newtgts = kcalloc(newsize, sizeof(*newtgts), GFP_NOFS);
		if (!newtgts) {
			mutex_unlock(&lov->lov_lock);
			return -ENOMEM;
		}

		if (lov->lov_tgt_size) {
			memcpy(newtgts, lov->lov_tgts, sizeof(*newtgts) *
			       lov->lov_tgt_size);
			old = lov->lov_tgts;
			oldsize = lov->lov_tgt_size;
		}

		lov->lov_tgts = newtgts;
		lov->lov_tgt_size = newsize;
		smp_rmb();
		kfree(old);

		CDEBUG(D_CONFIG, "tgts: %p size: %d\n",
		       lov->lov_tgts, lov->lov_tgt_size);
	}

	tgt = kzalloc(sizeof(*tgt), GFP_NOFS);
	if (!tgt) {
		mutex_unlock(&lov->lov_lock);
		return -ENOMEM;
	}

	rc = lov_ost_pool_add(&lov->lov_packed, index, lov->lov_tgt_size);
	if (rc) {
		mutex_unlock(&lov->lov_lock);
		kfree(tgt);
		return rc;
	}

	tgt->ltd_uuid = *uuidp;
	tgt->ltd_obd = tgt_obd;
	/* XXX - add a sanity check on the generation number. */
	tgt->ltd_gen = gen;
	tgt->ltd_index = index;
	tgt->ltd_activate = active;
	lov->lov_tgts[index] = tgt;
	if (index >= lov->desc.ld_tgt_count)
		lov->desc.ld_tgt_count = index + 1;

	mutex_unlock(&lov->lov_lock);

	CDEBUG(D_CONFIG, "idx=%d ltd_gen=%d ld_tgt_count=%d\n",
	       index, tgt->ltd_gen, lov->desc.ld_tgt_count);

	if (lov->lov_connects == 0) {
		/* lov_connect hasn't been called yet. We'll do the
		 * lov_connect_obd on this target when that fn first runs,
		 * because we don't know the connect flags yet.
		 */
		return 0;
	}

	obd_getref(obd);

	rc = lov_connect_obd(obd, index, active, &lov->lov_ocd);
	if (rc)
		goto out;

	/* connect to administrative disabled ost */
	if (!tgt->ltd_exp) {
		rc = 0;
		goto out;
	}

	if (lov->lov_cache) {
		rc = obd_set_info_async(NULL, tgt->ltd_exp,
					sizeof(KEY_CACHE_SET), KEY_CACHE_SET,
					sizeof(struct cl_client_cache),
					lov->lov_cache, NULL);
		if (rc < 0)
			goto out;
	}

	rc = lov_notify(obd, tgt->ltd_exp->exp_obd,
			active ? OBD_NOTIFY_CONNECT : OBD_NOTIFY_INACTIVE,
			(void *)&index);

out:
	if (rc) {
		CERROR("add failed (%d), deleting %s\n", rc,
		       obd_uuid2str(&tgt->ltd_uuid));
		lov_del_target(obd, index, NULL, 0);
	}
	obd_putref(obd);
	return rc;
}

/* Schedule a target for deletion */
int lov_del_target(struct obd_device *obd, __u32 index,
		   struct obd_uuid *uuidp, int gen)
{
	struct lov_obd *lov = &obd->u.lov;
	int count = lov->desc.ld_tgt_count;
	int rc = 0;

	if (index >= count) {
		CERROR("LOV target index %d >= number of LOV OBDs %d.\n",
		       index, count);
		return -EINVAL;
	}

	/* to make sure there's no ongoing lov_notify() now */
	down_write(&lov->lov_notify_lock);
	obd_getref(obd);

	if (!lov->lov_tgts[index]) {
		CERROR("LOV target at index %d is not setup.\n", index);
		rc = -EINVAL;
		goto out;
	}

	if (uuidp && !obd_uuid_equals(uuidp, &lov->lov_tgts[index]->ltd_uuid)) {
		CERROR("LOV target UUID %s at index %d doesn't match %s.\n",
		       lov_uuid2str(lov, index), index,
		       obd_uuid2str(uuidp));
		rc = -EINVAL;
		goto out;
	}

	CDEBUG(D_CONFIG, "uuid: %s idx: %d gen: %d exp: %p active: %d\n",
	       lov_uuid2str(lov, index), index,
	       lov->lov_tgts[index]->ltd_gen, lov->lov_tgts[index]->ltd_exp,
	       lov->lov_tgts[index]->ltd_active);

	lov->lov_tgts[index]->ltd_reap = 1;
	lov->lov_death_row++;
	/* we really delete it from obd_putref */
out:
	obd_putref(obd);
	up_write(&lov->lov_notify_lock);

	return rc;
}

static void __lov_del_obd(struct obd_device *obd, struct lov_tgt_desc *tgt)
{
	struct obd_device *osc_obd;

	LASSERT(tgt);
	LASSERT(tgt->ltd_reap);

	osc_obd = class_exp2obd(tgt->ltd_exp);

	CDEBUG(D_CONFIG, "Removing tgt %s : %s\n",
	       tgt->ltd_uuid.uuid,
	       osc_obd ? osc_obd->obd_name : "<no obd>");

	if (tgt->ltd_exp)
		lov_disconnect_obd(obd, tgt);

	kfree(tgt);

	/* Manual cleanup - no cleanup logs to clean up the osc's.  We must
	 * do it ourselves. And we can't do it from lov_cleanup,
	 * because we just lost our only reference to it.
	 */
	if (osc_obd)
		class_manual_cleanup(osc_obd);
}

void lov_fix_desc_stripe_size(__u64 *val)
{
	if (*val < LOV_MIN_STRIPE_SIZE) {
		if (*val != 0)
			LCONSOLE_INFO("Increasing default stripe size to minimum %u\n",
				      LOV_DESC_STRIPE_SIZE_DEFAULT);
		*val = LOV_DESC_STRIPE_SIZE_DEFAULT;
	} else if (*val & (LOV_MIN_STRIPE_SIZE - 1)) {
		*val &= ~(LOV_MIN_STRIPE_SIZE - 1);
		LCONSOLE_WARN("Changing default stripe size to %llu (a multiple of %u)\n",
			      *val, LOV_MIN_STRIPE_SIZE);
	}
}

void lov_fix_desc_stripe_count(__u32 *val)
{
	if (*val == 0)
		*val = 1;
}

void lov_fix_desc_pattern(__u32 *val)
{
	/* from lov_setstripe */
	if ((*val != 0) && (*val != LOV_PATTERN_RAID0)) {
		LCONSOLE_WARN("Unknown stripe pattern: %#x\n", *val);
		*val = 0;
	}
}

void lov_fix_desc_qos_maxage(__u32 *val)
{
	if (*val == 0)
		*val = LOV_DESC_QOS_MAXAGE_DEFAULT;
}

void lov_fix_desc(struct lov_desc *desc)
{
	lov_fix_desc_stripe_size(&desc->ld_default_stripe_size);
	lov_fix_desc_stripe_count(&desc->ld_default_stripe_count);
	lov_fix_desc_pattern(&desc->ld_pattern);
	lov_fix_desc_qos_maxage(&desc->ld_qos_maxage);
}

int lov_setup(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	struct lprocfs_static_vars lvars = { NULL };
	struct lov_desc *desc;
	struct lov_obd *lov = &obd->u.lov;
	int rc;

	if (LUSTRE_CFG_BUFLEN(lcfg, 1) < 1) {
		CERROR("LOV setup requires a descriptor\n");
		return -EINVAL;
	}

	desc = (struct lov_desc *)lustre_cfg_buf(lcfg, 1);

	if (sizeof(*desc) > LUSTRE_CFG_BUFLEN(lcfg, 1)) {
		CERROR("descriptor size wrong: %d > %d\n",
		       (int)sizeof(*desc), LUSTRE_CFG_BUFLEN(lcfg, 1));
		return -EINVAL;
	}

	if (desc->ld_magic != LOV_DESC_MAGIC) {
		if (desc->ld_magic == __swab32(LOV_DESC_MAGIC)) {
			CDEBUG(D_OTHER, "%s: Swabbing lov desc %p\n",
			       obd->obd_name, desc);
			lustre_swab_lov_desc(desc);
		} else {
			CERROR("%s: Bad lov desc magic: %#x\n",
			       obd->obd_name, desc->ld_magic);
			return -EINVAL;
		}
	}

	lov_fix_desc(desc);

	desc->ld_active_tgt_count = 0;
	lov->desc = *desc;
	lov->lov_tgt_size = 0;

	mutex_init(&lov->lov_lock);
	atomic_set(&lov->lov_refcount, 0);
	lov->lov_sp_me = LUSTRE_SP_CLI;

	init_rwsem(&lov->lov_notify_lock);

	lov->lov_pools_hash_body = cfs_hash_create("POOLS", HASH_POOLS_CUR_BITS,
						   HASH_POOLS_MAX_BITS,
						   HASH_POOLS_BKT_BITS, 0,
						   CFS_HASH_MIN_THETA,
						   CFS_HASH_MAX_THETA,
						   &pool_hash_operations,
						   CFS_HASH_DEFAULT);
	INIT_LIST_HEAD(&lov->lov_pool_list);
	lov->lov_pool_count = 0;
	rc = lov_ost_pool_init(&lov->lov_packed, 0);
	if (rc)
		goto out;

	lprocfs_lov_init_vars(&lvars);
	lprocfs_obd_setup(obd, lvars.obd_vars, lvars.sysfs_vars);

	rc = ldebugfs_seq_create(obd->obd_debugfs_entry, "target_obd",
				 0444, &lov_proc_target_fops, obd);
	if (rc)
		CWARN("Error adding the target_obd file\n");

	lov->lov_pool_debugfs_entry = ldebugfs_register("pools",
						     obd->obd_debugfs_entry,
						     NULL, NULL);
	return 0;

out:
	return rc;
}

static int lov_cleanup(struct obd_device *obd)
{
	struct lov_obd *lov = &obd->u.lov;
	struct list_head *pos, *tmp;
	struct pool_desc *pool;

	list_for_each_safe(pos, tmp, &lov->lov_pool_list) {
		pool = list_entry(pos, struct pool_desc, pool_list);
		/* free pool structs */
		CDEBUG(D_INFO, "delete pool %p\n", pool);
		/* In the function below, .hs_keycmp resolves to
		 * pool_hashkey_keycmp()
		 */
		/* coverity[overrun-buffer-val] */
		lov_pool_del(obd, pool->pool_name);
	}
	cfs_hash_putref(lov->lov_pools_hash_body);
	lov_ost_pool_free(&lov->lov_packed);

	lprocfs_obd_cleanup(obd);
	if (lov->lov_tgts) {
		int i;

		obd_getref(obd);
		for (i = 0; i < lov->desc.ld_tgt_count; i++) {
			if (!lov->lov_tgts[i])
				continue;

			/* Inactive targets may never have connected */
			if (lov->lov_tgts[i]->ltd_active ||
			    atomic_read(&lov->lov_refcount))
			    /* We should never get here - these
			     * should have been removed in the
			     * disconnect.
			     */
				CERROR("lov tgt %d not cleaned! deathrow=%d, lovrc=%d\n",
				       i, lov->lov_death_row,
				       atomic_read(&lov->lov_refcount));
			lov_del_target(obd, i, NULL, 0);
		}
		obd_putref(obd);
		kfree(lov->lov_tgts);
		lov->lov_tgt_size = 0;
	}

	if (lov->lov_cache) {
		cl_cache_decref(lov->lov_cache);
		lov->lov_cache = NULL;
	}

	return 0;
}

int lov_process_config_base(struct obd_device *obd, struct lustre_cfg *lcfg,
			    __u32 *indexp, int *genp)
{
	struct obd_uuid obd_uuid;
	int cmd;
	int rc = 0;

	switch (cmd = lcfg->lcfg_command) {
	case LCFG_LOV_ADD_OBD:
	case LCFG_LOV_ADD_INA:
	case LCFG_LOV_DEL_OBD: {
		__u32 index;
		int gen;
		/* lov_modify_tgts add  0:lov_mdsA  1:ost1_UUID  2:0  3:1 */
		if (LUSTRE_CFG_BUFLEN(lcfg, 1) > sizeof(obd_uuid.uuid)) {
			rc = -EINVAL;
			goto out;
		}

		obd_str2uuid(&obd_uuid,  lustre_cfg_buf(lcfg, 1));

		rc = kstrtoint(lustre_cfg_buf(lcfg, 2), 10, indexp);
		if (rc < 0)
			goto out;
		rc = kstrtoint(lustre_cfg_buf(lcfg, 3), 10, genp);
		if (rc < 0)
			goto out;
		index = *indexp;
		gen = *genp;
		if (cmd == LCFG_LOV_ADD_OBD)
			rc = lov_add_target(obd, &obd_uuid, index, gen, 1);
		else if (cmd == LCFG_LOV_ADD_INA)
			rc = lov_add_target(obd, &obd_uuid, index, gen, 0);
		else
			rc = lov_del_target(obd, index, &obd_uuid, gen);
		goto out;
	}
	case LCFG_PARAM: {
		struct lprocfs_static_vars lvars = { NULL };
		struct lov_desc *desc = &obd->u.lov.desc;

		if (!desc) {
			rc = -EINVAL;
			goto out;
		}

		lprocfs_lov_init_vars(&lvars);

		rc = class_process_proc_param(PARAM_LOV, lvars.obd_vars,
					      lcfg, obd);
		if (rc > 0)
			rc = 0;
		goto out;
	}
	case LCFG_POOL_NEW:
	case LCFG_POOL_ADD:
	case LCFG_POOL_DEL:
	case LCFG_POOL_REM:
		goto out;

	default: {
		CERROR("Unknown command: %d\n", lcfg->lcfg_command);
		rc = -EINVAL;
		goto out;
	}
	}
out:
	return rc;
}

int lov_statfs_interpret(struct ptlrpc_request_set *rqset, void *data, int rc)
{
	struct lov_request_set *lovset = (struct lov_request_set *)data;
	int err;

	if (rc)
		atomic_set(&lovset->set_completes, 0);

	err = lov_fini_statfs_set(lovset);
	return rc ? rc : err;
}

static int lov_statfs_async(struct obd_export *exp, struct obd_info *oinfo,
			    __u64 max_age, struct ptlrpc_request_set *rqset)
{
	struct obd_device      *obd = class_exp2obd(exp);
	struct lov_request_set *set;
	struct lov_request *req;
	struct lov_obd *lov;
	int rc = 0;

	LASSERT(oinfo->oi_osfs);

	lov = &obd->u.lov;
	rc = lov_prep_statfs_set(obd, oinfo, &set);
	if (rc)
		return rc;

	list_for_each_entry(req, &set->set_list, rq_link) {
		rc = obd_statfs_async(lov->lov_tgts[req->rq_idx]->ltd_exp,
				      &req->rq_oi, max_age, rqset);
		if (rc)
			break;
	}

	if (rc || list_empty(&rqset->set_requests)) {
		int err;

		if (rc)
			atomic_set(&set->set_completes, 0);
		err = lov_fini_statfs_set(set);
		return rc ? rc : err;
	}

	LASSERT(!rqset->set_interpret);
	rqset->set_interpret = lov_statfs_interpret;
	rqset->set_arg = (void *)set;
	return 0;
}

static int lov_statfs(const struct lu_env *env, struct obd_export *exp,
		      struct obd_statfs *osfs, __u64 max_age, __u32 flags)
{
	struct ptlrpc_request_set *set = NULL;
	struct obd_info oinfo = {
		.oi_osfs = osfs,
		.oi_flags = flags,
	};
	int rc = 0;

	/* for obdclass we forbid using obd_statfs_rqset, but prefer using async
	 * statfs requests
	 */
	set = ptlrpc_prep_set();
	if (!set)
		return -ENOMEM;

	rc = lov_statfs_async(exp, &oinfo, max_age, set);
	if (rc == 0)
		rc = ptlrpc_set_wait(set);
	ptlrpc_set_destroy(set);

	return rc;
}

static int lov_iocontrol(unsigned int cmd, struct obd_export *exp, int len,
			 void *karg, void __user *uarg)
{
	struct obd_device *obddev = class_exp2obd(exp);
	struct lov_obd *lov = &obddev->u.lov;
	int i = 0, rc = 0, count = lov->desc.ld_tgt_count;
	struct obd_uuid *uuidp;

	switch (cmd) {
	case IOC_OBD_STATFS: {
		struct obd_ioctl_data *data = karg;
		struct obd_device *osc_obd;
		struct obd_statfs stat_buf = {0};
		__u32 index;
		__u32 flags;

		memcpy(&index, data->ioc_inlbuf2, sizeof(__u32));
		if (index >= count)
			return -ENODEV;

		if (!lov->lov_tgts[index])
			/* Try again with the next index */
			return -EAGAIN;
		if (!lov->lov_tgts[index]->ltd_active)
			return -ENODATA;

		osc_obd = class_exp2obd(lov->lov_tgts[index]->ltd_exp);
		if (!osc_obd)
			return -EINVAL;

		/* copy UUID */
		if (copy_to_user(data->ioc_pbuf2, obd2cli_tgt(osc_obd),
				 min_t(unsigned long, data->ioc_plen2,
				       sizeof(struct obd_uuid))))
			return -EFAULT;

		memcpy(&flags, data->ioc_inlbuf1, sizeof(__u32));
		flags = flags & LL_STATFS_NODELAY ? OBD_STATFS_NODELAY : 0;

		/* got statfs data */
		rc = obd_statfs(NULL, lov->lov_tgts[index]->ltd_exp, &stat_buf,
				cfs_time_shift_64(-OBD_STATFS_CACHE_SECONDS),
				flags);
		if (rc)
			return rc;
		if (copy_to_user(data->ioc_pbuf1, &stat_buf,
				 min_t(unsigned long, data->ioc_plen1,
				       sizeof(stat_buf))))
			return -EFAULT;
		break;
	}
	case OBD_IOC_LOV_GET_CONFIG: {
		struct obd_ioctl_data *data;
		struct lov_desc *desc;
		char *buf = NULL;
		__u32 *genp;

		len = 0;
		if (obd_ioctl_getdata(&buf, &len, uarg))
			return -EINVAL;

		data = (struct obd_ioctl_data *)buf;

		if (sizeof(*desc) > data->ioc_inllen1) {
			obd_ioctl_freedata(buf, len);
			return -EINVAL;
		}

		if (sizeof(uuidp->uuid) * count > data->ioc_inllen2) {
			obd_ioctl_freedata(buf, len);
			return -EINVAL;
		}

		if (sizeof(__u32) * count > data->ioc_inllen3) {
			obd_ioctl_freedata(buf, len);
			return -EINVAL;
		}

		desc = (struct lov_desc *)data->ioc_inlbuf1;
		memcpy(desc, &lov->desc, sizeof(*desc));

		uuidp = (struct obd_uuid *)data->ioc_inlbuf2;
		genp = (__u32 *)data->ioc_inlbuf3;
		/* the uuid will be empty for deleted OSTs */
		for (i = 0; i < count; i++, uuidp++, genp++) {
			if (!lov->lov_tgts[i])
				continue;
			*uuidp = lov->lov_tgts[i]->ltd_uuid;
			*genp = lov->lov_tgts[i]->ltd_gen;
		}

		if (copy_to_user(uarg, buf, len))
			rc = -EFAULT;
		obd_ioctl_freedata(buf, len);
		break;
	}
	case OBD_IOC_QUOTACTL: {
		struct if_quotactl *qctl = karg;
		struct lov_tgt_desc *tgt = NULL;
		struct obd_quotactl *oqctl;

		if (qctl->qc_valid == QC_OSTIDX) {
			if (count <= qctl->qc_idx)
				return -EINVAL;

			tgt = lov->lov_tgts[qctl->qc_idx];
			if (!tgt || !tgt->ltd_exp)
				return -EINVAL;
		} else if (qctl->qc_valid == QC_UUID) {
			for (i = 0; i < count; i++) {
				tgt = lov->lov_tgts[i];
				if (!tgt ||
				    !obd_uuid_equals(&tgt->ltd_uuid,
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
			qctl->qc_valid = QC_OSTIDX;
			qctl->obd_uuid = tgt->ltd_uuid;
		}
		kfree(oqctl);
		break;
	}
	default: {
		int set = 0;

		if (count == 0)
			return -ENOTTY;

		for (i = 0; i < count; i++) {
			int err;
			struct obd_device *osc_obd;

			/* OST was disconnected */
			if (!lov->lov_tgts[i] || !lov->lov_tgts[i]->ltd_exp)
				continue;

			/* ll_umount_begin() sets force flag but for lov, not
			 * osc. Let's pass it through
			 */
			osc_obd = class_exp2obd(lov->lov_tgts[i]->ltd_exp);
			osc_obd->obd_force = obddev->obd_force;
			err = obd_iocontrol(cmd, lov->lov_tgts[i]->ltd_exp,
					    len, karg, uarg);
			if (err) {
				if (lov->lov_tgts[i]->ltd_active) {
					CDEBUG(err == -ENOTTY ?
					       D_IOCTL : D_WARNING,
					       "iocontrol OSC %s on OST idx %d cmd %x: err = %d\n",
					       lov_uuid2str(lov, i),
					       i, cmd, err);
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
	}

	return rc;
}

static int lov_get_info(const struct lu_env *env, struct obd_export *exp,
			__u32 keylen, void *key, __u32 *vallen, void *val)
{
	struct obd_device *obddev = class_exp2obd(exp);
	struct lov_obd *lov = &obddev->u.lov;
	struct lov_desc *ld = &lov->desc;
	int rc = 0;

	if (!vallen || !val)
		return -EFAULT;

	obd_getref(obddev);

	if (KEY_IS(KEY_MAX_EASIZE)) {
		u32 max_stripe_count = min_t(u32, ld->ld_active_tgt_count,
					     LOV_MAX_STRIPE_COUNT);

		*((u32 *)val) = lov_mds_md_size(max_stripe_count, LOV_MAGIC_V3);
	} else if (KEY_IS(KEY_DEFAULT_EASIZE)) {
		u32 def_stripe_count = min_t(u32, ld->ld_default_stripe_count,
					     LOV_MAX_STRIPE_COUNT);

		*((u32 *)val) = lov_mds_md_size(def_stripe_count, LOV_MAGIC_V3);
	} else if (KEY_IS(KEY_TGT_COUNT)) {
		*((int *)val) = lov->desc.ld_tgt_count;
	} else {
		rc = -EINVAL;
	}

	obd_putref(obddev);
	return rc;
}

static int lov_set_info_async(const struct lu_env *env, struct obd_export *exp,
			      u32 keylen, void *key, u32 vallen,
			      void *val, struct ptlrpc_request_set *set)
{
	struct obd_device *obddev = class_exp2obd(exp);
	struct lov_obd *lov = &obddev->u.lov;
	u32 count;
	int i, rc = 0, err;
	struct lov_tgt_desc *tgt;
	int do_inactive = 0, no_set = 0;

	if (!set) {
		no_set = 1;
		set = ptlrpc_prep_set();
		if (!set)
			return -ENOMEM;
	}

	obd_getref(obddev);
	count = lov->desc.ld_tgt_count;

	if (KEY_IS(KEY_CHECKSUM)) {
		do_inactive = 1;
	} else if (KEY_IS(KEY_CACHE_SET)) {
		LASSERT(!lov->lov_cache);
		lov->lov_cache = val;
		do_inactive = 1;
		cl_cache_incref(lov->lov_cache);
	}

	for (i = 0; i < count; i++) {
		tgt = lov->lov_tgts[i];

		/* OST was disconnected */
		if (!tgt || !tgt->ltd_exp)
			continue;

		/* OST is inactive and we don't want inactive OSCs */
		if (!tgt->ltd_active && !do_inactive)
			continue;

		err = obd_set_info_async(env, tgt->ltd_exp, keylen, key,
					 vallen, val, set);
		if (!rc)
			rc = err;
	}

	obd_putref(obddev);
	if (no_set) {
		err = ptlrpc_set_wait(set);
		if (!rc)
			rc = err;
		ptlrpc_set_destroy(set);
	}
	return rc;
}

void lov_stripe_lock(struct lov_stripe_md *md)
		__acquires(&md->lsm_lock)
{
	LASSERT(md->lsm_lock_owner != current_pid());
	spin_lock(&md->lsm_lock);
	LASSERT(md->lsm_lock_owner == 0);
	md->lsm_lock_owner = current_pid();
}

void lov_stripe_unlock(struct lov_stripe_md *md)
		__releases(&md->lsm_lock)
{
	LASSERT(md->lsm_lock_owner == current_pid());
	md->lsm_lock_owner = 0;
	spin_unlock(&md->lsm_lock);
}

static int lov_quotactl(struct obd_device *obd, struct obd_export *exp,
			struct obd_quotactl *oqctl)
{
	struct lov_obd      *lov = &obd->u.lov;
	struct lov_tgt_desc *tgt;
	__u64		curspace = 0;
	__u64		bhardlimit = 0;
	int		  i, rc = 0;

	if (oqctl->qc_cmd != Q_GETOQUOTA &&
	    oqctl->qc_cmd != LUSTRE_Q_SETQUOTA) {
		CERROR("bad quota opc %x for lov obd\n", oqctl->qc_cmd);
		return -EFAULT;
	}

	/* for lov tgt */
	obd_getref(obd);
	for (i = 0; i < lov->desc.ld_tgt_count; i++) {
		int err;

		tgt = lov->lov_tgts[i];

		if (!tgt)
			continue;

		if (!tgt->ltd_active || tgt->ltd_reap) {
			if (oqctl->qc_cmd == Q_GETOQUOTA &&
			    lov->lov_tgts[i]->ltd_activate) {
				rc = -EREMOTEIO;
				CERROR("ost %d is inactive\n", i);
			} else {
				CDEBUG(D_HA, "ost %d is inactive\n", i);
			}
			continue;
		}

		err = obd_quotactl(tgt->ltd_exp, oqctl);
		if (err) {
			if (tgt->ltd_active && !rc)
				rc = err;
			continue;
		}

		if (oqctl->qc_cmd == Q_GETOQUOTA) {
			curspace += oqctl->qc_dqblk.dqb_curspace;
			bhardlimit += oqctl->qc_dqblk.dqb_bhardlimit;
		}
	}
	obd_putref(obd);

	if (oqctl->qc_cmd == Q_GETOQUOTA) {
		oqctl->qc_dqblk.dqb_curspace = curspace;
		oqctl->qc_dqblk.dqb_bhardlimit = bhardlimit;
	}
	return rc;
}

static struct obd_ops lov_obd_ops = {
	.owner          = THIS_MODULE,
	.setup          = lov_setup,
	.cleanup        = lov_cleanup,
	/*.process_config       = lov_process_config,*/
	.connect        = lov_connect,
	.disconnect     = lov_disconnect,
	.statfs         = lov_statfs,
	.statfs_async   = lov_statfs_async,
	.iocontrol      = lov_iocontrol,
	.get_info       = lov_get_info,
	.set_info_async = lov_set_info_async,
	.notify         = lov_notify,
	.pool_new       = lov_pool_new,
	.pool_rem       = lov_pool_remove,
	.pool_add       = lov_pool_add,
	.pool_del       = lov_pool_del,
	.getref         = lov_getref,
	.putref         = lov_putref,
	.quotactl       = lov_quotactl,
};

struct kmem_cache *lov_oinfo_slab;

static int __init lov_init(void)
{
	struct lprocfs_static_vars lvars = { NULL };
	int rc;

	/* print an address of _any_ initialized kernel symbol from this
	 * module, to allow debugging with gdb that doesn't support data
	 * symbols from modules.
	 */
	CDEBUG(D_INFO, "Lustre LOV module (%p).\n", &lov_caches);

	rc = lu_kmem_init(lov_caches);
	if (rc)
		return rc;

	lov_oinfo_slab = kmem_cache_create("lov_oinfo",
					   sizeof(struct lov_oinfo),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!lov_oinfo_slab) {
		lu_kmem_fini(lov_caches);
		return -ENOMEM;
	}
	lprocfs_lov_init_vars(&lvars);

	rc = class_register_type(&lov_obd_ops, NULL,
				 LUSTRE_LOV_NAME, &lov_device_type);

	if (rc) {
		kmem_cache_destroy(lov_oinfo_slab);
		lu_kmem_fini(lov_caches);
	}

	return rc;
}

static void /*__exit*/ lov_exit(void)
{
	class_unregister_type(LUSTRE_LOV_NAME);
	kmem_cache_destroy(lov_oinfo_slab);

	lu_kmem_fini(lov_caches);
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Logical Object Volume");
MODULE_LICENSE("GPL");
MODULE_VERSION(LUSTRE_VERSION_STRING);

module_init(lov_init);
module_exit(lov_exit);
