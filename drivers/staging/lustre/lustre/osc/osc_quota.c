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
 * version 2 along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 *
 * Code originally extracted from quota directory
 */

#include <obd_ost.h>
#include "osc_internal.h"

static inline struct osc_quota_info *osc_oqi_alloc(obd_uid id)
{
	struct osc_quota_info *oqi;

	OBD_SLAB_ALLOC_PTR(oqi, osc_quota_kmem);
	if (oqi != NULL)
		oqi->oqi_id = id;

	return oqi;
}

int osc_quota_chkdq(struct client_obd *cli, const unsigned int qid[])
{
	int type;
	ENTRY;

	for (type = 0; type < MAXQUOTAS; type++) {
		struct osc_quota_info *oqi;

		oqi = cfs_hash_lookup(cli->cl_quota_hash[type], &qid[type]);
		if (oqi) {
			obd_uid id = oqi->oqi_id;

			LASSERTF(id == qid[type],
				 "The ids don't match %u != %u\n",
				 id, qid[type]);

			/* the slot is busy, the user is about to run out of
			 * quota space on this OST */
			CDEBUG(D_QUOTA, "chkdq found noquota for %s %d\n",
			       type == USRQUOTA ? "user" : "grout", qid[type]);
			RETURN(NO_QUOTA);
		}
	}

	RETURN(QUOTA_OK);
}

#define MD_QUOTA_FLAG(type) ((type == USRQUOTA) ? OBD_MD_FLUSRQUOTA \
						: OBD_MD_FLGRPQUOTA)
#define FL_QUOTA_FLAG(type) ((type == USRQUOTA) ? OBD_FL_NO_USRQUOTA \
						: OBD_FL_NO_GRPQUOTA)

int osc_quota_setdq(struct client_obd *cli, const unsigned int qid[],
		    obd_flag valid, obd_flag flags)
{
	int type;
	int rc = 0;
	ENTRY;

	if ((valid & (OBD_MD_FLUSRQUOTA | OBD_MD_FLGRPQUOTA)) == 0)
		RETURN(0);

	for (type = 0; type < MAXQUOTAS; type++) {
		struct osc_quota_info *oqi;

		if ((valid & MD_QUOTA_FLAG(type)) == 0)
			continue;

		/* lookup the ID in the per-type hash table */
		oqi = cfs_hash_lookup(cli->cl_quota_hash[type], &qid[type]);
		if ((flags & FL_QUOTA_FLAG(type)) != 0) {
			/* This ID is getting close to its quota limit, let's
			 * switch to sync I/O */
			if (oqi != NULL)
				continue;

			oqi = osc_oqi_alloc(qid[type]);
			if (oqi == NULL) {
				rc = -ENOMEM;
				break;
			}

			rc = cfs_hash_add_unique(cli->cl_quota_hash[type],
						 &qid[type], &oqi->oqi_hash);
			/* race with others? */
			if (rc == -EALREADY) {
				rc = 0;
				OBD_SLAB_FREE_PTR(oqi, osc_quota_kmem);
			}

			CDEBUG(D_QUOTA, "%s: setdq to insert for %s %d (%d)\n",
			       cli->cl_import->imp_obd->obd_name,
			       type == USRQUOTA ? "user" : "group",
			       qid[type], rc);
		} else {
			/* This ID is now off the hook, let's remove it from
			 * the hash table */
			if (oqi == NULL)
				continue;

			oqi = cfs_hash_del_key(cli->cl_quota_hash[type],
					       &qid[type]);
			if (oqi)
				OBD_SLAB_FREE_PTR(oqi, osc_quota_kmem);

			CDEBUG(D_QUOTA, "%s: setdq to remove for %s %d (%p)\n",
			       cli->cl_import->imp_obd->obd_name,
			       type == USRQUOTA ? "user" : "group",
			       qid[type], oqi);
		}
	}

	RETURN(rc);
}

/*
 * Hash operations for uid/gid <-> osc_quota_info
 */
static unsigned
oqi_hashfn(cfs_hash_t *hs, const void *key, unsigned mask)
{
	return cfs_hash_u32_hash(*((__u32*)key), mask);
}

static int
oqi_keycmp(const void *key, struct hlist_node *hnode)
{
	struct osc_quota_info *oqi;
	obd_uid uid;

	LASSERT(key != NULL);
	uid = *((obd_uid*)key);
	oqi = hlist_entry(hnode, struct osc_quota_info, oqi_hash);

	return uid == oqi->oqi_id;
}

static void *
oqi_key(struct hlist_node *hnode)
{
	struct osc_quota_info *oqi;
	oqi = hlist_entry(hnode, struct osc_quota_info, oqi_hash);
	return &oqi->oqi_id;
}

static void *
oqi_object(struct hlist_node *hnode)
{
	return hlist_entry(hnode, struct osc_quota_info, oqi_hash);
}

static void
oqi_get(cfs_hash_t *hs, struct hlist_node *hnode)
{
}

static void
oqi_put_locked(cfs_hash_t *hs, struct hlist_node *hnode)
{
}

static void
oqi_exit(cfs_hash_t *hs, struct hlist_node *hnode)
{
	struct osc_quota_info *oqi;

	oqi = hlist_entry(hnode, struct osc_quota_info, oqi_hash);

	OBD_SLAB_FREE_PTR(oqi, osc_quota_kmem);
}

#define HASH_QUOTA_BKT_BITS 5
#define HASH_QUOTA_CUR_BITS 5
#define HASH_QUOTA_MAX_BITS 15

static cfs_hash_ops_t quota_hash_ops = {
	.hs_hash	= oqi_hashfn,
	.hs_keycmp	= oqi_keycmp,
	.hs_key		= oqi_key,
	.hs_object	= oqi_object,
	.hs_get		= oqi_get,
	.hs_put_locked	= oqi_put_locked,
	.hs_exit	= oqi_exit,
};

int osc_quota_setup(struct obd_device *obd)
{
	struct client_obd *cli = &obd->u.cli;
	int i, type;
	ENTRY;

	for (type = 0; type < MAXQUOTAS; type++) {
		cli->cl_quota_hash[type] = cfs_hash_create("QUOTA_HASH",
							   HASH_QUOTA_CUR_BITS,
							   HASH_QUOTA_MAX_BITS,
							   HASH_QUOTA_BKT_BITS,
							   0,
							   CFS_HASH_MIN_THETA,
							   CFS_HASH_MAX_THETA,
							   &quota_hash_ops,
							   CFS_HASH_DEFAULT);
		if (cli->cl_quota_hash[type] == NULL)
			break;
	}

	if (type == MAXQUOTAS)
		RETURN(0);

	for (i = 0; i < type; i++)
		cfs_hash_putref(cli->cl_quota_hash[i]);

	RETURN(-ENOMEM);
}

int osc_quota_cleanup(struct obd_device *obd)
{
	struct client_obd     *cli = &obd->u.cli;
	int type;
	ENTRY;

	for (type = 0; type < MAXQUOTAS; type++)
		cfs_hash_putref(cli->cl_quota_hash[type]);

	RETURN(0);
}

int osc_quotactl(struct obd_device *unused, struct obd_export *exp,
		 struct obd_quotactl *oqctl)
{
	struct ptlrpc_request *req;
	struct obd_quotactl   *oqc;
	int		    rc;
	ENTRY;

	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp),
					&RQF_OST_QUOTACTL, LUSTRE_OST_VERSION,
					OST_QUOTACTL);
	if (req == NULL)
		RETURN(-ENOMEM);

	oqc = req_capsule_client_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
	*oqc = *oqctl;

	ptlrpc_request_set_replen(req);
	ptlrpc_at_set_req_timeout(req);
	req->rq_no_resend = 1;

	rc = ptlrpc_queue_wait(req);
	if (rc)
		CERROR("ptlrpc_queue_wait failed, rc: %d\n", rc);

	if (req->rq_repmsg &&
	    (oqc = req_capsule_server_get(&req->rq_pill, &RMF_OBD_QUOTACTL))) {
		*oqctl = *oqc;
	} else if (!rc) {
		CERROR ("Can't unpack obd_quotactl\n");
		rc = -EPROTO;
	}
	ptlrpc_req_finished(req);

	RETURN(rc);
}

int osc_quotacheck(struct obd_device *unused, struct obd_export *exp,
		   struct obd_quotactl *oqctl)
{
	struct client_obd       *cli = &exp->exp_obd->u.cli;
	struct ptlrpc_request   *req;
	struct obd_quotactl     *body;
	int		      rc;
	ENTRY;

	req = ptlrpc_request_alloc_pack(class_exp2cliimp(exp),
					&RQF_OST_QUOTACHECK, LUSTRE_OST_VERSION,
					OST_QUOTACHECK);
	if (req == NULL)
		RETURN(-ENOMEM);

	body = req_capsule_client_get(&req->rq_pill, &RMF_OBD_QUOTACTL);
	*body = *oqctl;

	ptlrpc_request_set_replen(req);

	/* the next poll will find -ENODATA, that means quotacheck is
	 * going on */
	cli->cl_qchk_stat = -ENODATA;
	rc = ptlrpc_queue_wait(req);
	if (rc)
		cli->cl_qchk_stat = rc;
	ptlrpc_req_finished(req);
	RETURN(rc);
}

int osc_quota_poll_check(struct obd_export *exp, struct if_quotacheck *qchk)
{
	struct client_obd *cli = &exp->exp_obd->u.cli;
	int rc;
	ENTRY;

	qchk->obd_uuid = cli->cl_target_uuid;
	memcpy(qchk->obd_type, LUSTRE_OST_NAME, strlen(LUSTRE_OST_NAME));

	rc = cli->cl_qchk_stat;
	/* the client is not the previous one */
	if (rc == CL_NOT_QUOTACHECKED)
		rc = -EINTR;
	RETURN(rc);
}
