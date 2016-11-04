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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * cl code shared between vvp and liblustre (and other Lustre clients in the
 * future).
 *
 */
#include "../include/obd_class.h"
#include "../include/obd_support.h"
#include "../include/obd.h"
#include "../include/cl_object.h"

#include "llite_internal.h"

/* Initialize the default and maximum LOV EA and cookie sizes.  This allows
 * us to make MDS RPCs with large enough reply buffers to hold the
 * maximum-sized (= maximum striped) EA and cookie without having to
 * calculate this (via a call into the LOV + OSCs) each time we make an RPC.
 */
int cl_init_ea_size(struct obd_export *md_exp, struct obd_export *dt_exp)
{
	struct lov_stripe_md lsm = { .lsm_magic = LOV_MAGIC_V3 };
	__u32 valsize = sizeof(struct lov_desc);
	int rc, easize, def_easize, cookiesize;
	struct lov_desc desc;
	__u16 stripes, def_stripes;

	rc = obd_get_info(NULL, dt_exp, sizeof(KEY_LOVDESC), KEY_LOVDESC,
			  &valsize, &desc, NULL);
	if (rc)
		return rc;

	stripes = min_t(__u32, desc.ld_tgt_count, LOV_MAX_STRIPE_COUNT);
	lsm.lsm_stripe_count = stripes;
	easize = obd_size_diskmd(dt_exp, &lsm);

	def_stripes = min_t(__u32, desc.ld_default_stripe_count,
			    LOV_MAX_STRIPE_COUNT);
	lsm.lsm_stripe_count = def_stripes;
	def_easize = obd_size_diskmd(dt_exp, &lsm);

	cookiesize = stripes * sizeof(struct llog_cookie);

	/* default cookiesize is 0 because from 2.4 server doesn't send
	 * llog cookies to client.
	 */
	CDEBUG(D_HA,
	       "updating def/max_easize: %d/%d def/max_cookiesize: 0/%d\n",
	       def_easize, easize, cookiesize);

	rc = md_init_ea_size(md_exp, easize, def_easize, cookiesize, 0);
	return rc;
}

/**
 * This function is used as an upcall-callback hooked by liblustre and llite
 * clients into obd_notify() listeners chain to handle notifications about
 * change of import connect_flags. See llu_fsswop_mount() and
 * lustre_common_fill_super().
 */
int cl_ocd_update(struct obd_device *host,
		  struct obd_device *watched,
		  enum obd_notify_event ev, void *owner, void *data)
{
	struct lustre_client_ocd *lco;
	struct client_obd	*cli;
	__u64 flags;
	int   result;

	if (!strcmp(watched->obd_type->typ_name, LUSTRE_OSC_NAME) &&
	    watched->obd_set_up && !watched->obd_stopping) {
		cli = &watched->u.cli;
		lco = owner;
		flags = cli->cl_import->imp_connect_data.ocd_connect_flags;
		CDEBUG(D_SUPER, "Changing connect_flags: %#llx -> %#llx\n",
		       lco->lco_flags, flags);
		mutex_lock(&lco->lco_lock);
		lco->lco_flags &= flags;
		/* for each osc event update ea size */
		if (lco->lco_dt_exp)
			cl_init_ea_size(lco->lco_md_exp, lco->lco_dt_exp);

		mutex_unlock(&lco->lco_lock);
		result = 0;
	} else {
		CERROR("unexpected notification from %s %s (setup:%d,stopping:%d)!\n",
		       watched->obd_type->typ_name,
		       watched->obd_name, watched->obd_set_up,
		       watched->obd_stopping);
		result = -EINVAL;
	}
	return result;
}

#define GROUPLOCK_SCOPE "grouplock"

int cl_get_grouplock(struct cl_object *obj, unsigned long gid, int nonblock,
		     struct ll_grouplock *cg)
{
	struct lu_env	  *env;
	struct cl_io	   *io;
	struct cl_lock	 *lock;
	struct cl_lock_descr   *descr;
	__u32		   enqflags;
	int		     refcheck;
	int		     rc;

	env = cl_env_get(&refcheck);
	if (IS_ERR(env))
		return PTR_ERR(env);

	io = vvp_env_thread_io(env);
	io->ci_obj = obj;
	io->ci_ignore_layout = 1;

	rc = cl_io_init(env, io, CIT_MISC, io->ci_obj);
	if (rc != 0) {
		cl_io_fini(env, io);
		cl_env_put(env, &refcheck);
		/* Does not make sense to take GL for released layout */
		if (rc > 0)
			rc = -ENOTSUPP;
		return rc;
	}

	lock = vvp_env_lock(env);
	descr = &lock->cll_descr;
	descr->cld_obj = obj;
	descr->cld_start = 0;
	descr->cld_end = CL_PAGE_EOF;
	descr->cld_gid = gid;
	descr->cld_mode = CLM_GROUP;

	enqflags = CEF_MUST | (nonblock ? CEF_NONBLOCK : 0);
	descr->cld_enq_flags = enqflags;

	rc = cl_lock_request(env, io, lock);
	if (rc < 0) {
		cl_io_fini(env, io);
		cl_env_put(env, &refcheck);
		return rc;
	}

	cg->lg_env  = cl_env_get(&refcheck);
	cg->lg_io   = io;
	cg->lg_lock = lock;
	cg->lg_gid  = gid;
	LASSERT(cg->lg_env == env);

	cl_env_unplant(env, &refcheck);
	return 0;
}

void cl_put_grouplock(struct ll_grouplock *cg)
{
	struct lu_env  *env  = cg->lg_env;
	struct cl_io   *io   = cg->lg_io;
	struct cl_lock *lock = cg->lg_lock;
	int	     refcheck;

	LASSERT(cg->lg_env);
	LASSERT(cg->lg_gid);

	cl_env_implant(env, &refcheck);
	cl_env_put(env, &refcheck);

	cl_lock_release(env, lock);
	cl_io_fini(env, io);
	cl_env_put(env, NULL);
}
