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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/mgc/libmgc.c
 *
 * Lustre Management Client
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */

/* Minimal MGC for liblustre: only used to read the config log from the MGS
   at setup time, no updates. */

#define DEBUG_SUBSYSTEM S_MGC

#include <liblustre.h>

#include <obd_class.h>
#include <lustre_dlm.h>
#include <lustre_log.h>
#include <lustre_fsfilt.h>
#include <lustre_disk.h>


static int mgc_setup(struct obd_device *obd, struct lustre_cfg *lcfg)
{
	int rc;

	ptlrpcd_addref();

	rc = client_obd_setup(obd, lcfg);
	if (rc)
		GOTO(err_decref, rc);

	/* liblustre only support null flavor to MGS */
	obd->u.cli.cl_flvr_mgc.sf_rpc = SPTLRPC_FLVR_NULL;

	rc = obd_llog_init(obd, &obd->obd_olg, obd, NULL);
	if (rc) {
		CERROR("failed to setup llogging subsystems\n");
		GOTO(err_cleanup, rc);
	}

	return rc;

err_cleanup:
	client_obd_cleanup(obd);
err_decref:
	ptlrpcd_decref();
	return rc;
}

static int mgc_precleanup(struct obd_device *obd, enum obd_cleanup_stage stage)
{
	int rc = 0;

	switch (stage) {
	case OBD_CLEANUP_EARLY:
	case OBD_CLEANUP_EXPORTS:
		obd_cleanup_client_import(obd);
		rc = obd_llog_finish(obd, 0);
		if (rc != 0)
			CERROR("failed to cleanup llogging subsystems\n");
		break;
	}
	return rc;
}

static int mgc_cleanup(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;
	int rc;

	LASSERT(cli->cl_mgc_vfsmnt == NULL);

	ptlrpcd_decref();

	rc = client_obd_cleanup(obd);
	return rc;
}

static int mgc_llog_init(struct obd_device *obd, struct obd_llog_group *olg,
			 struct obd_device *tgt, int *index)
{
	struct llog_ctxt *ctxt;
	int rc;

	LASSERT(olg == &obd->obd_olg);
	rc = llog_setup(NULL, obd, olg, LLOG_CONFIG_REPL_CTXT, tgt,
			&llog_client_ops);
	if (rc < 0)
		return rc;

	ctxt = llog_group_get_ctxt(olg, LLOG_CONFIG_REPL_CTXT);
	llog_initiator_connect(ctxt);
	llog_ctxt_put(ctxt);

	return rc;
}

static int mgc_llog_finish(struct obd_device *obd, int count)
{
	struct llog_ctxt *ctxt;


	ctxt = llog_get_context(obd, LLOG_CONFIG_REPL_CTXT);
	if (ctxt)
		llog_cleanup(NULL, ctxt);

	return 0;
}

struct obd_ops mgc_obd_ops = {
	.o_owner	= THIS_MODULE,
	.o_setup	= mgc_setup,
	.o_precleanup   = mgc_precleanup,
	.o_cleanup      = mgc_cleanup,
	.o_add_conn     = client_import_add_conn,
	.o_del_conn     = client_import_del_conn,
	.o_connect      = client_connect_import,
	.o_disconnect   = client_disconnect_export,
	.o_llog_init    = mgc_llog_init,
	.o_llog_finish  = mgc_llog_finish,
};

int __init mgc_init(void)
{
	return class_register_type(&mgc_obd_ops, NULL,
				   NULL, LUSTRE_MGC_NAME, NULL);
}
