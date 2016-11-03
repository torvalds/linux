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
	u32 val_size, max_easize, def_easize;
	int rc;

	val_size = sizeof(max_easize);
	rc = obd_get_info(NULL, dt_exp, sizeof(KEY_MAX_EASIZE), KEY_MAX_EASIZE,
			  &val_size, &max_easize);
	if (rc)
		return rc;

	val_size = sizeof(def_easize);
	rc = obd_get_info(NULL, dt_exp, sizeof(KEY_DEFAULT_EASIZE),
			  KEY_DEFAULT_EASIZE, &val_size, &def_easize);
	if (rc)
		return rc;

	/*
	 * default cookiesize is 0 because from 2.4 server doesn't send
	 * llog cookies to client.
	 */
	CDEBUG(D_HA, "updating def/max_easize: %d/%d\n",
	       def_easize, max_easize);

	rc = md_init_ea_size(md_exp, max_easize, def_easize);
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

	cg->lg_env  = env;
	cg->lg_io   = io;
	cg->lg_lock = lock;
	cg->lg_gid  = gid;

	return 0;
}

void cl_put_grouplock(struct ll_grouplock *cg)
{
	struct lu_env  *env  = cg->lg_env;
	struct cl_io   *io   = cg->lg_io;
	struct cl_lock *lock = cg->lg_lock;

	LASSERT(cg->lg_env);
	LASSERT(cg->lg_gid);

	cl_lock_release(env, lock);
	cl_io_fini(env, io);
	cl_env_put(env, NULL);
}
