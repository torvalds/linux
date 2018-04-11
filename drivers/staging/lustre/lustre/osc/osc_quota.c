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
 * GPL HEADER END
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 *
 * Code originally extracted from quota directory
 */

#include <obd_class.h>
#include "osc_internal.h"

static const struct rhashtable_params quota_hash_params = {
	.key_len	= sizeof(u32),
	.key_offset	= offsetof(struct osc_quota_info, oqi_id),
	.head_offset	= offsetof(struct osc_quota_info, oqi_hash),
	.automatic_shrinking = true,
};

static inline struct osc_quota_info *osc_oqi_alloc(u32 id)
{
	struct osc_quota_info *oqi;

	oqi = kmem_cache_zalloc(osc_quota_kmem, GFP_NOFS);
	if (oqi)
		oqi->oqi_id = id;

	return oqi;
}

int osc_quota_chkdq(struct client_obd *cli, const unsigned int qid[])
{
	int type;

	for (type = 0; type < MAXQUOTAS; type++) {
		struct osc_quota_info *oqi;

		oqi = rhashtable_lookup_fast(&cli->cl_quota_hash[type], &qid[type],
					     quota_hash_params);
		if (oqi) {
			/* Must not access oqi here, it could have been
			 * freed by osc_quota_setdq()
			 */

			/* the slot is busy, the user is about to run out of
			 * quota space on this OST
			 */
			CDEBUG(D_QUOTA, "chkdq found noquota for %s %d\n",
			       type == USRQUOTA ? "user" : "grout", qid[type]);
			return NO_QUOTA;
		}
	}

	return QUOTA_OK;
}

static void osc_quota_free(struct rcu_head *head)
{
	struct osc_quota_info *oqi = container_of(head, struct osc_quota_info, rcu);

	kmem_cache_free(osc_quota_kmem, oqi);
}


#define MD_QUOTA_FLAG(type) ((type == USRQUOTA) ? OBD_MD_FLUSRQUOTA \
						: OBD_MD_FLGRPQUOTA)
#define FL_QUOTA_FLAG(type) ((type == USRQUOTA) ? OBD_FL_NO_USRQUOTA \
						: OBD_FL_NO_GRPQUOTA)

int osc_quota_setdq(struct client_obd *cli, const unsigned int qid[],
		    u32 valid, u32 flags)
{
	int type;
	int rc = 0;

	if ((valid & (OBD_MD_FLUSRQUOTA | OBD_MD_FLGRPQUOTA)) == 0)
		return 0;

	for (type = 0; type < MAXQUOTAS; type++) {
		struct osc_quota_info *oqi;

		if ((valid & MD_QUOTA_FLAG(type)) == 0)
			continue;

		/* lookup the ID in the per-type hash table */
		rcu_read_lock();
		oqi = rhashtable_lookup_fast(&cli->cl_quota_hash[type], &qid[type],
					     quota_hash_params);
		if ((flags & FL_QUOTA_FLAG(type)) != 0) {
			/* This ID is getting close to its quota limit, let's
			 * switch to sync I/O
			 */
			rcu_read_unlock();
			if (oqi)
				continue;

			oqi = osc_oqi_alloc(qid[type]);
			if (!oqi) {
				rc = -ENOMEM;
				break;
			}

			rc = rhashtable_lookup_insert_fast(&cli->cl_quota_hash[type],
							   &oqi->oqi_hash, quota_hash_params);
			/* race with others? */
			if (rc) {
				kmem_cache_free(osc_quota_kmem, oqi);
				if (rc != -EEXIST) {
					rc = -ENOMEM;
					break;
				}
				rc = 0;
			}

			CDEBUG(D_QUOTA, "%s: setdq to insert for %s %d (%d)\n",
			       cli_name(cli),
			       type == USRQUOTA ? "user" : "group",
			       qid[type], rc);
		} else {
			/* This ID is now off the hook, let's remove it from
			 * the hash table
			 */
			if (!oqi) {
				rcu_read_unlock();
				continue;
			}
			if (rhashtable_remove_fast(&cli->cl_quota_hash[type],
						   &oqi->oqi_hash, quota_hash_params) == 0)
				call_rcu(&oqi->rcu, osc_quota_free);
			rcu_read_unlock();
			CDEBUG(D_QUOTA, "%s: setdq to remove for %s %d (%p)\n",
			       cli_name(cli),
			       type == USRQUOTA ? "user" : "group",
			       qid[type], oqi);
		}
	}

	return rc;
}

static void
oqi_exit(void *vquota, void *data)
{
	struct osc_quota_info *oqi = vquota;

	osc_quota_free(&oqi->rcu);
}

int osc_quota_setup(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;
	int i, type;

	for (type = 0; type < MAXQUOTAS; type++) {
		if (rhashtable_init(&cli->cl_quota_hash[type], &quota_hash_params) != 0)
			break;
	}

	if (type == MAXQUOTAS)
		return 0;

	for (i = 0; i < type; i++)
		rhashtable_destroy(&cli->cl_quota_hash[i]);

	return -ENOMEM;
}

int osc_quota_cleanup(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;
	int type;

	for (type = 0; type < MAXQUOTAS; type++)
		rhashtable_free_and_destroy(&cli->cl_quota_hash[type],
					    oqi_exit, NULL);

	return 0;
}

int osc_quotactl(struct obd_device *unused, struct obd_export *exp,
		 struct obd_quotactl *oqctl)
{
	struct ptlrpc_request *req;
	struct obd_quotactl *oqc;
	int rc;

	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp),
					&RQF_OST_QUOTACTL, LUSTRE_OST_VERSION,
					OST_QUOTACTL);
	if (!req)
		return -ENOMEM;

	oqc = req_capsule_client_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
	*oqc = *oqctl;

	ptlrpc_request_set_replen(req);
	ptlrpc_at_set_req_timeout(req);
	req->rq_no_resend = 1;

	rc = ptlrpc_queue_wait(req);
	if (rc)
		CERROR("ptlrpc_queue_wait failed, rc: %d\n", rc);

	if (req->rq_repmsg) {
		oqc = req_capsule_server_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
		if (oqc) {
			*oqctl = *oqc;
		} else if (!rc) {
			CERROR("Can't unpack obd_quotactl\n");
			rc = -EPROTO;
		}
	} else if (!rc) {
		CERROR("Can't unpack obd_quotactl\n");
		rc = -EPROTO;
	}
	ptlrpc_req_finished(req);

	return rc;
}
