// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_RPC

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_net.h>
#include <lustre_req_layout.h>

#include "ptlrpc_internal.h"

extern spinlock_t ptlrpc_last_xid_lock;
#if RS_DEBUG
extern spinlock_t ptlrpc_rs_debug_lock;
#endif

static int __init ptlrpc_init(void)
{
	int rc, cleanup_phase = 0;

	lustre_assert_wire_constants();
#if RS_DEBUG
	spin_lock_init(&ptlrpc_rs_debug_lock);
#endif
	mutex_init(&ptlrpc_all_services_mutex);
	mutex_init(&pinger_mutex);
	mutex_init(&ptlrpcd_mutex);
	ptlrpc_init_xid();

	rc = req_layout_init();
	if (rc)
		return rc;

	rc = ptlrpc_hr_init();
	if (rc)
		return rc;

	cleanup_phase = 1;
	rc = ptlrpc_request_cache_init();
	if (rc)
		goto cleanup;

	cleanup_phase = 2;
	rc = ptlrpc_init_portals();
	if (rc)
		goto cleanup;

	cleanup_phase = 3;

	rc = ptlrpc_connection_init();
	if (rc)
		goto cleanup;

	cleanup_phase = 4;
	ptlrpc_put_connection_superhack = ptlrpc_connection_put;

	rc = ptlrpc_start_pinger();
	if (rc)
		goto cleanup;

	cleanup_phase = 5;
	rc = ldlm_init();
	if (rc)
		goto cleanup;

	cleanup_phase = 6;
	rc = sptlrpc_init();
	if (rc)
		goto cleanup;

	cleanup_phase = 7;
	rc = ptlrpc_nrs_init();
	if (rc)
		goto cleanup;

	cleanup_phase = 8;
	rc = tgt_mod_init();
	if (rc)
		goto cleanup;
	return 0;

cleanup:
	switch (cleanup_phase) {
	case 8:
		ptlrpc_nrs_fini();
		/* Fall through */
	case 7:
		sptlrpc_fini();
		/* Fall through */
	case 6:
		ldlm_exit();
		/* Fall through */
	case 5:
		ptlrpc_stop_pinger();
		/* Fall through */
	case 4:
		ptlrpc_connection_fini();
		/* Fall through */
	case 3:
		ptlrpc_exit_portals();
		/* Fall through */
	case 2:
		ptlrpc_request_cache_fini();
		/* Fall through */
	case 1:
		ptlrpc_hr_fini();
		req_layout_fini();
		/* Fall through */
	default:
		;
	}

	return rc;
}

static void __exit ptlrpc_exit(void)
{
	tgt_mod_exit();
	ptlrpc_nrs_fini();
	sptlrpc_fini();
	ldlm_exit();
	ptlrpc_stop_pinger();
	ptlrpc_exit_portals();
	ptlrpc_request_cache_fini();
	ptlrpc_hr_fini();
	ptlrpc_connection_fini();
}

MODULE_AUTHOR("OpenSFS, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre Request Processor and Lock Management");
MODULE_VERSION(LUSTRE_VERSION_STRING);
MODULE_LICENSE("GPL");

module_init(ptlrpc_init);
module_exit(ptlrpc_exit);
