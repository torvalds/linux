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
 * lustre/llite/remote_perm.c
 *
 * Lustre Permission Cache for Remote Client
 *
 * Author: Lai Siyao <lsy@clusterfs.com>
 * Author: Fan Yong <fanyong@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LLITE

#include <linux/module.h>
#include <linux/types.h>

#include <lustre_lite.h>
#include <lustre_ha.h>
#include <lustre_dlm.h>
#include <lprocfs_status.h>
#include <lustre_disk.h>
#include <lustre_param.h>
#include "llite_internal.h"

struct kmem_cache *ll_remote_perm_cachep = NULL;
struct kmem_cache *ll_rmtperm_hash_cachep = NULL;

static inline struct ll_remote_perm *alloc_ll_remote_perm(void)
{
	struct ll_remote_perm *lrp;

	OBD_SLAB_ALLOC_PTR_GFP(lrp, ll_remote_perm_cachep, GFP_KERNEL);
	if (lrp)
		INIT_HLIST_NODE(&lrp->lrp_list);
	return lrp;
}

static inline void free_ll_remote_perm(struct ll_remote_perm *lrp)
{
	if (!lrp)
		return;

	if (!hlist_unhashed(&lrp->lrp_list))
		hlist_del(&lrp->lrp_list);
	OBD_SLAB_FREE(lrp, ll_remote_perm_cachep, sizeof(*lrp));
}

struct hlist_head *alloc_rmtperm_hash(void)
{
	struct hlist_head *hash;
	int i;

	OBD_SLAB_ALLOC_GFP(hash, ll_rmtperm_hash_cachep,
			   REMOTE_PERM_HASHSIZE * sizeof(*hash),
			   GFP_IOFS);
	if (!hash)
		return NULL;

	for (i = 0; i < REMOTE_PERM_HASHSIZE; i++)
		INIT_HLIST_HEAD(hash + i);

	return hash;
}

void free_rmtperm_hash(struct hlist_head *hash)
{
	int i;
	struct ll_remote_perm *lrp;
	struct hlist_node *next;

	if(!hash)
		return;

	for (i = 0; i < REMOTE_PERM_HASHSIZE; i++)
		hlist_for_each_entry_safe(lrp, next, hash + i,
					      lrp_list)
			free_ll_remote_perm(lrp);
	OBD_SLAB_FREE(hash, ll_rmtperm_hash_cachep,
		      REMOTE_PERM_HASHSIZE * sizeof(*hash));
}

static inline int remote_perm_hashfunc(uid_t uid)
{
	return uid & (REMOTE_PERM_HASHSIZE - 1);
}

/* NB: setxid permission is not checked here, instead it's done on
 * MDT when client get remote permission. */
static int do_check_remote_perm(struct ll_inode_info *lli, int mask)
{
	struct hlist_head *head;
	struct ll_remote_perm *lrp;
	int found = 0, rc;

	if (!lli->lli_remote_perms)
		return -ENOENT;

	head = lli->lli_remote_perms +
		remote_perm_hashfunc(from_kuid(&init_user_ns, current_uid()));

	spin_lock(&lli->lli_lock);
	hlist_for_each_entry(lrp, head, lrp_list) {
		if (lrp->lrp_uid != from_kuid(&init_user_ns, current_uid()))
			continue;
		if (lrp->lrp_gid != from_kgid(&init_user_ns, current_gid()))
			continue;
		if (lrp->lrp_fsuid != from_kuid(&init_user_ns, current_fsuid()))
			continue;
		if (lrp->lrp_fsgid != from_kgid(&init_user_ns, current_fsgid()))
			continue;
		found = 1;
		break;
	}

	if (!found)
		GOTO(out, rc = -ENOENT);

	CDEBUG(D_SEC, "found remote perm: %u/%u/%u/%u - %#x\n",
	       lrp->lrp_uid, lrp->lrp_gid, lrp->lrp_fsuid, lrp->lrp_fsgid,
	       lrp->lrp_access_perm);
	rc = ((lrp->lrp_access_perm & mask) == mask) ? 0 : -EACCES;

out:
	spin_unlock(&lli->lli_lock);
	return rc;
}

int ll_update_remote_perm(struct inode *inode, struct mdt_remote_perm *perm)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_remote_perm *lrp = NULL, *tmp = NULL;
	struct hlist_head *head, *perm_hash = NULL;

	LASSERT(ll_i2sbi(inode)->ll_flags & LL_SBI_RMT_CLIENT);

#if 0
	if (perm->rp_uid != current->uid ||
	    perm->rp_gid != current->gid ||
	    perm->rp_fsuid != current->fsuid ||
	    perm->rp_fsgid != current->fsgid) {
		/* user might setxid in this small period */
		CDEBUG(D_SEC,
		       "remote perm user %u/%u/%u/%u != current %u/%u/%u/%u\n",
		       perm->rp_uid, perm->rp_gid, perm->rp_fsuid,
		       perm->rp_fsgid, current->uid, current->gid,
		       current->fsuid, current->fsgid);
		return -EAGAIN;
	}
#endif

	if (!lli->lli_remote_perms) {
		perm_hash = alloc_rmtperm_hash();
		if (perm_hash == NULL) {
			CERROR("alloc lli_remote_perms failed!\n");
			return -ENOMEM;
		}
	}

	spin_lock(&lli->lli_lock);

	if (!lli->lli_remote_perms)
		lli->lli_remote_perms = perm_hash;
	else if (perm_hash)
		free_rmtperm_hash(perm_hash);

	head = lli->lli_remote_perms + remote_perm_hashfunc(perm->rp_uid);

again:
	hlist_for_each_entry(tmp, head, lrp_list) {
		if (tmp->lrp_uid != perm->rp_uid)
			continue;
		if (tmp->lrp_gid != perm->rp_gid)
			continue;
		if (tmp->lrp_fsuid != perm->rp_fsuid)
			continue;
		if (tmp->lrp_fsgid != perm->rp_fsgid)
			continue;
		if (lrp)
			free_ll_remote_perm(lrp);
		lrp = tmp;
		break;
	}

	if (!lrp) {
		spin_unlock(&lli->lli_lock);
		lrp = alloc_ll_remote_perm();
		if (!lrp) {
			CERROR("alloc memory for ll_remote_perm failed!\n");
			return -ENOMEM;
		}
		spin_lock(&lli->lli_lock);
		goto again;
	}

	lrp->lrp_access_perm = perm->rp_access_perm;
	if (lrp != tmp) {
		lrp->lrp_uid	 = perm->rp_uid;
		lrp->lrp_gid	 = perm->rp_gid;
		lrp->lrp_fsuid       = perm->rp_fsuid;
		lrp->lrp_fsgid       = perm->rp_fsgid;
		hlist_add_head(&lrp->lrp_list, head);
	}
	lli->lli_rmtperm_time = cfs_time_current();
	spin_unlock(&lli->lli_lock);

	CDEBUG(D_SEC, "new remote perm@%p: %u/%u/%u/%u - %#x\n",
	       lrp, lrp->lrp_uid, lrp->lrp_gid, lrp->lrp_fsuid, lrp->lrp_fsgid,
	       lrp->lrp_access_perm);

	return 0;
}

int lustre_check_remote_perm(struct inode *inode, int mask)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct ll_sb_info *sbi = ll_i2sbi(inode);
	struct ptlrpc_request *req = NULL;
	struct mdt_remote_perm *perm;
	struct obd_capa *oc;
	cfs_time_t save;
	int i = 0, rc;

	do {
		save = lli->lli_rmtperm_time;
		rc = do_check_remote_perm(lli, mask);
		if (!rc || (rc != -ENOENT && i))
			break;

		might_sleep();

		mutex_lock(&lli->lli_rmtperm_mutex);
		/* check again */
		if (save != lli->lli_rmtperm_time) {
			rc = do_check_remote_perm(lli, mask);
			if (!rc || (rc != -ENOENT && i)) {
				mutex_unlock(&lli->lli_rmtperm_mutex);
				break;
			}
		}

		if (i++ > 5) {
			CERROR("check remote perm falls in dead loop!\n");
			LBUG();
		}

		oc = ll_mdscapa_get(inode);
		rc = md_get_remote_perm(sbi->ll_md_exp, ll_inode2fid(inode), oc,
					ll_i2suppgid(inode), &req);
		capa_put(oc);
		if (rc) {
			mutex_unlock(&lli->lli_rmtperm_mutex);
			break;
		}

		perm = req_capsule_server_swab_get(&req->rq_pill, &RMF_ACL,
						   lustre_swab_mdt_remote_perm);
		if (unlikely(perm == NULL)) {
			mutex_unlock(&lli->lli_rmtperm_mutex);
			rc = -EPROTO;
			break;
		}

		rc = ll_update_remote_perm(inode, perm);
		mutex_unlock(&lli->lli_rmtperm_mutex);
		if (rc == -ENOMEM)
			break;

		ptlrpc_req_finished(req);
		req = NULL;
	} while (1);
	ptlrpc_req_finished(req);
	return rc;
}

#if 0  /* NB: remote perms can't be freed in ll_mdc_blocking_ast of UPDATE lock,
	* because it will fail sanity test 48.
	*/
void ll_free_remote_perms(struct inode *inode)
{
	struct ll_inode_info *lli = ll_i2info(inode);
	struct hlist_head *hash = lli->lli_remote_perms;
	struct ll_remote_perm *lrp;
	struct hlist_node *node, *next;
	int i;

	LASSERT(hash);

	spin_lock(&lli->lli_lock);

	for (i = 0; i < REMOTE_PERM_HASHSIZE; i++) {
		hlist_for_each_entry_safe(lrp, node, next, hash + i,
					      lrp_list)
			free_ll_remote_perm(lrp);
	}

	spin_unlock(&lli->lli_lock);
}
#endif
