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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lnet/lnet/peer.c
 */

#define DEBUG_SUBSYSTEM S_LNET

#include "../../include/linux/lnet/lib-lnet.h"
#include "../../include/linux/lnet/lib-dlc.h"

int
lnet_peer_tables_create(void)
{
	struct lnet_peer_table *ptable;
	struct list_head *hash;
	int i;
	int j;

	the_lnet.ln_peer_tables = cfs_percpt_alloc(lnet_cpt_table(),
						   sizeof(*ptable));
	if (!the_lnet.ln_peer_tables) {
		CERROR("Failed to allocate cpu-partition peer tables\n");
		return -ENOMEM;
	}

	cfs_percpt_for_each(ptable, i, the_lnet.ln_peer_tables) {
		INIT_LIST_HEAD(&ptable->pt_deathrow);

		LIBCFS_CPT_ALLOC(hash, lnet_cpt_table(), i,
				 LNET_PEER_HASH_SIZE * sizeof(*hash));
		if (!hash) {
			CERROR("Failed to create peer hash table\n");
			lnet_peer_tables_destroy();
			return -ENOMEM;
		}

		for (j = 0; j < LNET_PEER_HASH_SIZE; j++)
			INIT_LIST_HEAD(&hash[j]);
		ptable->pt_hash = hash; /* sign of initialization */
	}

	return 0;
}

void
lnet_peer_tables_destroy(void)
{
	struct lnet_peer_table *ptable;
	struct list_head *hash;
	int i;
	int j;

	if (!the_lnet.ln_peer_tables)
		return;

	cfs_percpt_for_each(ptable, i, the_lnet.ln_peer_tables) {
		hash = ptable->pt_hash;
		if (!hash) /* not initialized */
			break;

		LASSERT(list_empty(&ptable->pt_deathrow));

		ptable->pt_hash = NULL;
		for (j = 0; j < LNET_PEER_HASH_SIZE; j++)
			LASSERT(list_empty(&hash[j]));

		LIBCFS_FREE(hash, LNET_PEER_HASH_SIZE * sizeof(*hash));
	}

	cfs_percpt_free(the_lnet.ln_peer_tables);
	the_lnet.ln_peer_tables = NULL;
}

static void
lnet_peer_table_cleanup_locked(lnet_ni_t *ni, struct lnet_peer_table *ptable)
{
	int i;
	lnet_peer_t *lp;
	lnet_peer_t *tmp;

	for (i = 0; i < LNET_PEER_HASH_SIZE; i++) {
		list_for_each_entry_safe(lp, tmp, &ptable->pt_hash[i],
					 lp_hashlist) {
			if (ni && ni != lp->lp_ni)
				continue;
			list_del_init(&lp->lp_hashlist);
			/* Lose hash table's ref */
			ptable->pt_zombies++;
			lnet_peer_decref_locked(lp);
		}
	}
}

static void
lnet_peer_table_deathrow_wait_locked(struct lnet_peer_table *ptable,
				     int cpt_locked)
{
	int i;

	for (i = 3; ptable->pt_zombies; i++) {
		lnet_net_unlock(cpt_locked);

		if (is_power_of_2(i)) {
			CDEBUG(D_WARNING,
			       "Waiting for %d zombies on peer table\n",
			       ptable->pt_zombies);
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(cfs_time_seconds(1) >> 1);
		lnet_net_lock(cpt_locked);
	}
}

static void
lnet_peer_table_del_rtrs_locked(lnet_ni_t *ni, struct lnet_peer_table *ptable,
				int cpt_locked)
{
	lnet_peer_t *lp;
	lnet_peer_t *tmp;
	lnet_nid_t lp_nid;
	int i;

	for (i = 0; i < LNET_PEER_HASH_SIZE; i++) {
		list_for_each_entry_safe(lp, tmp, &ptable->pt_hash[i],
					 lp_hashlist) {
			if (ni != lp->lp_ni)
				continue;

			if (!lp->lp_rtr_refcount)
				continue;

			lp_nid = lp->lp_nid;

			lnet_net_unlock(cpt_locked);
			lnet_del_route(LNET_NIDNET(LNET_NID_ANY), lp_nid);
			lnet_net_lock(cpt_locked);
		}
	}
}

void
lnet_peer_tables_cleanup(lnet_ni_t *ni)
{
	struct lnet_peer_table *ptable;
	struct list_head deathrow;
	lnet_peer_t *lp;
	lnet_peer_t *temp;
	int i;

	INIT_LIST_HEAD(&deathrow);

	LASSERT(the_lnet.ln_shutdown || ni);
	/*
	 * If just deleting the peers for a NI, get rid of any routes these
	 * peers are gateways for.
	 */
	cfs_percpt_for_each(ptable, i, the_lnet.ln_peer_tables) {
		lnet_net_lock(i);
		lnet_peer_table_del_rtrs_locked(ni, ptable, i);
		lnet_net_unlock(i);
	}

	/*
	 * Start the process of moving the applicable peers to
	 * deathrow.
	 */
	cfs_percpt_for_each(ptable, i, the_lnet.ln_peer_tables) {
		lnet_net_lock(i);
		lnet_peer_table_cleanup_locked(ni, ptable);
		lnet_net_unlock(i);
	}

	/* Cleanup all entries on deathrow. */
	cfs_percpt_for_each(ptable, i, the_lnet.ln_peer_tables) {
		lnet_net_lock(i);
		lnet_peer_table_deathrow_wait_locked(ptable, i);
		list_splice_init(&ptable->pt_deathrow, &deathrow);
		lnet_net_unlock(i);
	}

	list_for_each_entry_safe(lp, temp, &deathrow, lp_hashlist) {
		list_del(&lp->lp_hashlist);
		LIBCFS_FREE(lp, sizeof(*lp));
	}
}

void
lnet_destroy_peer_locked(lnet_peer_t *lp)
{
	struct lnet_peer_table *ptable;

	LASSERT(!lp->lp_refcount);
	LASSERT(!lp->lp_rtr_refcount);
	LASSERT(list_empty(&lp->lp_txq));
	LASSERT(list_empty(&lp->lp_hashlist));
	LASSERT(!lp->lp_txqnob);

	ptable = the_lnet.ln_peer_tables[lp->lp_cpt];
	LASSERT(ptable->pt_number > 0);
	ptable->pt_number--;

	lnet_ni_decref_locked(lp->lp_ni, lp->lp_cpt);
	lp->lp_ni = NULL;

	list_add(&lp->lp_hashlist, &ptable->pt_deathrow);
	LASSERT(ptable->pt_zombies > 0);
	ptable->pt_zombies--;
}

lnet_peer_t *
lnet_find_peer_locked(struct lnet_peer_table *ptable, lnet_nid_t nid)
{
	struct list_head *peers;
	lnet_peer_t *lp;

	LASSERT(!the_lnet.ln_shutdown);

	peers = &ptable->pt_hash[lnet_nid2peerhash(nid)];
	list_for_each_entry(lp, peers, lp_hashlist) {
		if (lp->lp_nid == nid) {
			lnet_peer_addref_locked(lp);
			return lp;
		}
	}

	return NULL;
}

int
lnet_nid2peer_locked(lnet_peer_t **lpp, lnet_nid_t nid, int cpt)
{
	struct lnet_peer_table *ptable;
	lnet_peer_t *lp = NULL;
	lnet_peer_t *lp2;
	int cpt2;
	int rc = 0;

	*lpp = NULL;
	if (the_lnet.ln_shutdown) /* it's shutting down */
		return -ESHUTDOWN;

	/* cpt can be LNET_LOCK_EX if it's called from router functions */
	cpt2 = cpt != LNET_LOCK_EX ? cpt : lnet_cpt_of_nid_locked(nid);

	ptable = the_lnet.ln_peer_tables[cpt2];
	lp = lnet_find_peer_locked(ptable, nid);
	if (lp) {
		*lpp = lp;
		return 0;
	}

	if (!list_empty(&ptable->pt_deathrow)) {
		lp = list_entry(ptable->pt_deathrow.next,
				lnet_peer_t, lp_hashlist);
		list_del(&lp->lp_hashlist);
	}

	/*
	 * take extra refcount in case another thread has shutdown LNet
	 * and destroyed locks and peer-table before I finish the allocation
	 */
	ptable->pt_number++;
	lnet_net_unlock(cpt);

	if (lp)
		memset(lp, 0, sizeof(*lp));
	else
		LIBCFS_CPT_ALLOC(lp, lnet_cpt_table(), cpt2, sizeof(*lp));

	if (!lp) {
		rc = -ENOMEM;
		lnet_net_lock(cpt);
		goto out;
	}

	INIT_LIST_HEAD(&lp->lp_txq);
	INIT_LIST_HEAD(&lp->lp_rtrq);
	INIT_LIST_HEAD(&lp->lp_routes);

	lp->lp_notify = 0;
	lp->lp_notifylnd = 0;
	lp->lp_notifying = 0;
	lp->lp_alive_count = 0;
	lp->lp_timestamp = 0;
	lp->lp_alive = !lnet_peers_start_down(); /* 1 bit!! */
	lp->lp_last_alive = cfs_time_current(); /* assumes alive */
	lp->lp_last_query = 0; /* haven't asked NI yet */
	lp->lp_ping_timestamp = 0;
	lp->lp_ping_feats = LNET_PING_FEAT_INVAL;
	lp->lp_nid = nid;
	lp->lp_cpt = cpt2;
	lp->lp_refcount = 2;	/* 1 for caller; 1 for hash */
	lp->lp_rtr_refcount = 0;

	lnet_net_lock(cpt);

	if (the_lnet.ln_shutdown) {
		rc = -ESHUTDOWN;
		goto out;
	}

	lp2 = lnet_find_peer_locked(ptable, nid);
	if (lp2) {
		*lpp = lp2;
		goto out;
	}

	lp->lp_ni = lnet_net2ni_locked(LNET_NIDNET(nid), cpt2);
	if (!lp->lp_ni) {
		rc = -EHOSTUNREACH;
		goto out;
	}

	lp->lp_txcredits = lp->lp_ni->ni_peertxcredits;
	lp->lp_mintxcredits = lp->lp_ni->ni_peertxcredits;
	lp->lp_rtrcredits = lnet_peer_buffer_credits(lp->lp_ni);
	lp->lp_minrtrcredits = lnet_peer_buffer_credits(lp->lp_ni);

	list_add_tail(&lp->lp_hashlist,
		      &ptable->pt_hash[lnet_nid2peerhash(nid)]);
	ptable->pt_version++;
	*lpp = lp;

	return 0;
out:
	if (lp)
		list_add(&lp->lp_hashlist, &ptable->pt_deathrow);
	ptable->pt_number--;
	return rc;
}

void
lnet_debug_peer(lnet_nid_t nid)
{
	char *aliveness = "NA";
	lnet_peer_t *lp;
	int rc;
	int cpt;

	cpt = lnet_cpt_of_nid(nid);
	lnet_net_lock(cpt);

	rc = lnet_nid2peer_locked(&lp, nid, cpt);
	if (rc) {
		lnet_net_unlock(cpt);
		CDEBUG(D_WARNING, "No peer %s\n", libcfs_nid2str(nid));
		return;
	}

	if (lnet_isrouter(lp) || lnet_peer_aliveness_enabled(lp))
		aliveness = lp->lp_alive ? "up" : "down";

	CDEBUG(D_WARNING, "%-24s %4d %5s %5d %5d %5d %5d %5d %ld\n",
	       libcfs_nid2str(lp->lp_nid), lp->lp_refcount,
	       aliveness, lp->lp_ni->ni_peertxcredits,
	       lp->lp_rtrcredits, lp->lp_minrtrcredits,
	       lp->lp_txcredits, lp->lp_mintxcredits, lp->lp_txqnob);

	lnet_peer_decref_locked(lp);

	lnet_net_unlock(cpt);
}

int
lnet_get_peer_info(__u32 peer_index, __u64 *nid,
		   char aliveness[LNET_MAX_STR_LEN],
		   __u32 *cpt_iter, __u32 *refcount,
		   __u32 *ni_peer_tx_credits, __u32 *peer_tx_credits,
		   __u32 *peer_rtr_credits, __u32 *peer_min_rtr_credits,
		   __u32 *peer_tx_qnob)
{
	struct lnet_peer_table *peer_table;
	lnet_peer_t *lp;
	bool found = false;
	int lncpt, j;

	/* get the number of CPTs */
	lncpt = cfs_percpt_number(the_lnet.ln_peer_tables);

	/*
	 * if the cpt number to be examined is >= the number of cpts in
	 * the system then indicate that there are no more cpts to examin
	 */
	if (*cpt_iter >= lncpt)
		return -ENOENT;

	/* get the current table */
	peer_table = the_lnet.ln_peer_tables[*cpt_iter];
	/* if the ptable is NULL then there are no more cpts to examine */
	if (!peer_table)
		return -ENOENT;

	lnet_net_lock(*cpt_iter);

	for (j = 0; j < LNET_PEER_HASH_SIZE && !found; j++) {
		struct list_head *peers = &peer_table->pt_hash[j];

		list_for_each_entry(lp, peers, lp_hashlist) {
			if (peer_index-- > 0)
				continue;

			snprintf(aliveness, LNET_MAX_STR_LEN, "NA");
			if (lnet_isrouter(lp) ||
			    lnet_peer_aliveness_enabled(lp))
				snprintf(aliveness, LNET_MAX_STR_LEN,
					 lp->lp_alive ? "up" : "down");

			*nid = lp->lp_nid;
			*refcount = lp->lp_refcount;
			*ni_peer_tx_credits = lp->lp_ni->ni_peertxcredits;
			*peer_tx_credits = lp->lp_txcredits;
			*peer_rtr_credits = lp->lp_rtrcredits;
			*peer_min_rtr_credits = lp->lp_mintxcredits;
			*peer_tx_qnob = lp->lp_txqnob;

			found = true;
		}
	}
	lnet_net_unlock(*cpt_iter);

	*cpt_iter = lncpt;

	return found ? 0 : -ENOENT;
}
