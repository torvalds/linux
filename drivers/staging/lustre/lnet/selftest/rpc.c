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
 * lnet/selftest/rpc.c
 *
 * Author: Isaac Huang <isaac@clusterfs.com>
 *
 * 2012-05-13: Liang Zhen <liang@whamcloud.com>
 * - percpt data for service to improve smp performance
 * - code cleanup
 */

#define DEBUG_SUBSYSTEM S_LNET

#include "selftest.h"

typedef enum {
	SRPC_STATE_NONE,
	SRPC_STATE_NI_INIT,
	SRPC_STATE_EQ_INIT,
	SRPC_STATE_RUNNING,
	SRPC_STATE_STOPPING,
} srpc_state_t;

struct smoketest_rpc {
	spinlock_t	 rpc_glock;	/* global lock */
	srpc_service_t	*rpc_services[SRPC_SERVICE_MAX_ID + 1];
	lnet_handle_eq_t rpc_lnet_eq;	/* _the_ LNet event queue */
	srpc_state_t	 rpc_state;
	srpc_counters_t	 rpc_counters;
	__u64		 rpc_matchbits;	/* matchbits counter */
} srpc_data;

static inline int
srpc_serv_portal(int svc_id)
{
	return svc_id < SRPC_FRAMEWORK_SERVICE_MAX_ID ?
	       SRPC_FRAMEWORK_REQUEST_PORTAL : SRPC_REQUEST_PORTAL;
}

/* forward ref's */
int srpc_handle_rpc (swi_workitem_t *wi);

void srpc_get_counters (srpc_counters_t *cnt)
{
	spin_lock(&srpc_data.rpc_glock);
	*cnt = srpc_data.rpc_counters;
	spin_unlock(&srpc_data.rpc_glock);
}

void srpc_set_counters (const srpc_counters_t *cnt)
{
	spin_lock(&srpc_data.rpc_glock);
	srpc_data.rpc_counters = *cnt;
	spin_unlock(&srpc_data.rpc_glock);
}

int
srpc_add_bulk_page(srpc_bulk_t *bk, struct page *pg, int i, int nob)
{
	nob = min(nob, (int)PAGE_CACHE_SIZE);

	LASSERT(nob > 0);
	LASSERT(i >= 0 && i < bk->bk_niov);

	bk->bk_iovs[i].kiov_offset = 0;
	bk->bk_iovs[i].kiov_page   = pg;
	bk->bk_iovs[i].kiov_len    = nob;
	return nob;
}

void
srpc_free_bulk (srpc_bulk_t *bk)
{
	int	 i;
	struct page *pg;

	LASSERT (bk != NULL);

	for (i = 0; i < bk->bk_niov; i++) {
		pg = bk->bk_iovs[i].kiov_page;
		if (pg == NULL) break;

		__free_page(pg);
	}

	LIBCFS_FREE(bk, offsetof(srpc_bulk_t, bk_iovs[bk->bk_niov]));
	return;
}

srpc_bulk_t *
srpc_alloc_bulk(int cpt, unsigned bulk_npg, unsigned bulk_len, int sink)
{
	srpc_bulk_t  *bk;
	struct page  **pages;
	int	      i;

	LASSERT(bulk_npg > 0 && bulk_npg <= LNET_MAX_IOV);

	LIBCFS_CPT_ALLOC(bk, lnet_cpt_table(), cpt,
			 offsetof(srpc_bulk_t, bk_iovs[bulk_npg]));
	if (bk == NULL) {
		CERROR("Can't allocate descriptor for %d pages\n", bulk_npg);
		return NULL;
	}

	memset(bk, 0, offsetof(srpc_bulk_t, bk_iovs[bulk_npg]));
	bk->bk_sink   = sink;
	bk->bk_len    = bulk_len;
	bk->bk_niov   = bulk_npg;
	UNUSED(pages);

	for (i = 0; i < bulk_npg; i++) {
		struct page *pg;
		int	    nob;

		pg = cfs_page_cpt_alloc(lnet_cpt_table(), cpt, GFP_IOFS);
		if (pg == NULL) {
			CERROR("Can't allocate page %d of %d\n", i, bulk_npg);
			srpc_free_bulk(bk);
			return NULL;
		}

		nob = srpc_add_bulk_page(bk, pg, i, bulk_len);
		bulk_len -= nob;
	}

	return bk;
}

static inline __u64
srpc_next_id (void)
{
	__u64 id;

	spin_lock(&srpc_data.rpc_glock);
	id = srpc_data.rpc_matchbits++;
	spin_unlock(&srpc_data.rpc_glock);
	return id;
}

void
srpc_init_server_rpc(struct srpc_server_rpc *rpc,
		     struct srpc_service_cd *scd,
		     struct srpc_buffer *buffer)
{
	memset(rpc, 0, sizeof(*rpc));
	swi_init_workitem(&rpc->srpc_wi, rpc, srpc_handle_rpc,
			  srpc_serv_is_framework(scd->scd_svc) ?
			  lst_sched_serial : lst_sched_test[scd->scd_cpt]);

	rpc->srpc_ev.ev_fired = 1; /* no event expected now */

	rpc->srpc_scd      = scd;
	rpc->srpc_reqstbuf = buffer;
	rpc->srpc_peer     = buffer->buf_peer;
	rpc->srpc_self     = buffer->buf_self;
	LNetInvalidateHandle(&rpc->srpc_replymdh);
}

static void
srpc_service_fini(struct srpc_service *svc)
{
	struct srpc_service_cd	*scd;
	struct srpc_server_rpc	*rpc;
	struct srpc_buffer	*buf;
	struct list_head		*q;
	int			i;

	if (svc->sv_cpt_data == NULL)
		return;

	cfs_percpt_for_each(scd, i, svc->sv_cpt_data) {
		while (1) {
			if (!list_empty(&scd->scd_buf_posted))
				q = &scd->scd_buf_posted;
			else if (!list_empty(&scd->scd_buf_blocked))
				q = &scd->scd_buf_blocked;
			else
				break;

			while (!list_empty(q)) {
				buf = list_entry(q->next,
						     struct srpc_buffer,
						     buf_list);
				list_del(&buf->buf_list);
				LIBCFS_FREE(buf, sizeof(*buf));
			}
		}

		LASSERT(list_empty(&scd->scd_rpc_active));

		while (!list_empty(&scd->scd_rpc_free)) {
			rpc = list_entry(scd->scd_rpc_free.next,
					     struct srpc_server_rpc,
					     srpc_list);
			list_del(&rpc->srpc_list);
			LIBCFS_FREE(rpc, sizeof(*rpc));
		}
	}

	cfs_percpt_free(svc->sv_cpt_data);
	svc->sv_cpt_data = NULL;
}

static int
srpc_service_nrpcs(struct srpc_service *svc)
{
	int nrpcs = svc->sv_wi_total / svc->sv_ncpts;

	return srpc_serv_is_framework(svc) ?
	       max(nrpcs, SFW_FRWK_WI_MIN) : max(nrpcs, SFW_TEST_WI_MIN);
}

int srpc_add_buffer(struct swi_workitem *wi);

static int
srpc_service_init(struct srpc_service *svc)
{
	struct srpc_service_cd	*scd;
	struct srpc_server_rpc	*rpc;
	int			nrpcs;
	int			i;
	int			j;

	svc->sv_shuttingdown = 0;

	svc->sv_cpt_data = cfs_percpt_alloc(lnet_cpt_table(),
					    sizeof(struct srpc_service_cd));
	if (svc->sv_cpt_data == NULL)
		return -ENOMEM;

	svc->sv_ncpts = srpc_serv_is_framework(svc) ?
			1 : cfs_cpt_number(lnet_cpt_table());
	nrpcs = srpc_service_nrpcs(svc);

	cfs_percpt_for_each(scd, i, svc->sv_cpt_data) {
		scd->scd_cpt = i;
		scd->scd_svc = svc;
		spin_lock_init(&scd->scd_lock);
		INIT_LIST_HEAD(&scd->scd_rpc_free);
		INIT_LIST_HEAD(&scd->scd_rpc_active);
		INIT_LIST_HEAD(&scd->scd_buf_posted);
		INIT_LIST_HEAD(&scd->scd_buf_blocked);

		scd->scd_ev.ev_data = scd;
		scd->scd_ev.ev_type = SRPC_REQUEST_RCVD;

		/* NB: don't use lst_sched_serial for adding buffer,
		 * see details in srpc_service_add_buffers() */
		swi_init_workitem(&scd->scd_buf_wi, scd,
				  srpc_add_buffer, lst_sched_test[i]);

		if (i != 0 && srpc_serv_is_framework(svc)) {
			/* NB: framework service only needs srpc_service_cd for
			 * one partition, but we allocate for all to make
			 * it easier to implement, it will waste a little
			 * memory but nobody should care about this */
			continue;
		}

		for (j = 0; j < nrpcs; j++) {
			LIBCFS_CPT_ALLOC(rpc, lnet_cpt_table(),
					 i, sizeof(*rpc));
			if (rpc == NULL) {
				srpc_service_fini(svc);
				return -ENOMEM;
			}
			list_add(&rpc->srpc_list, &scd->scd_rpc_free);
		}
	}

	return 0;
}

int
srpc_add_service(struct srpc_service *sv)
{
	int id = sv->sv_id;

	LASSERT(0 <= id && id <= SRPC_SERVICE_MAX_ID);

	if (srpc_service_init(sv) != 0)
		return -ENOMEM;

	spin_lock(&srpc_data.rpc_glock);

	LASSERT(srpc_data.rpc_state == SRPC_STATE_RUNNING);

	if (srpc_data.rpc_services[id] != NULL) {
		spin_unlock(&srpc_data.rpc_glock);
		goto failed;
	}

	srpc_data.rpc_services[id] = sv;
	spin_unlock(&srpc_data.rpc_glock);

	CDEBUG(D_NET, "Adding service: id %d, name %s\n", id, sv->sv_name);
	return 0;

 failed:
	srpc_service_fini(sv);
	return -EBUSY;
}

int
srpc_remove_service (srpc_service_t *sv)
{
	int id = sv->sv_id;

	spin_lock(&srpc_data.rpc_glock);

	if (srpc_data.rpc_services[id] != sv) {
		spin_unlock(&srpc_data.rpc_glock);
		return -ENOENT;
	}

	srpc_data.rpc_services[id] = NULL;
	spin_unlock(&srpc_data.rpc_glock);
	return 0;
}

int
srpc_post_passive_rdma(int portal, int local, __u64 matchbits, void *buf,
		       int len, int options, lnet_process_id_t peer,
		       lnet_handle_md_t *mdh, srpc_event_t *ev)
{
	int		 rc;
	lnet_md_t	 md;
	lnet_handle_me_t meh;

	rc = LNetMEAttach(portal, peer, matchbits, 0, LNET_UNLINK,
			  local ? LNET_INS_LOCAL : LNET_INS_AFTER, &meh);
	if (rc != 0) {
		CERROR ("LNetMEAttach failed: %d\n", rc);
		LASSERT (rc == -ENOMEM);
		return -ENOMEM;
	}

	md.threshold = 1;
	md.user_ptr  = ev;
	md.start     = buf;
	md.length    = len;
	md.options   = options;
	md.eq_handle = srpc_data.rpc_lnet_eq;

	rc = LNetMDAttach(meh, md, LNET_UNLINK, mdh);
	if (rc != 0) {
		CERROR ("LNetMDAttach failed: %d\n", rc);
		LASSERT (rc == -ENOMEM);

		rc = LNetMEUnlink(meh);
		LASSERT (rc == 0);
		return -ENOMEM;
	}

	CDEBUG (D_NET,
		"Posted passive RDMA: peer %s, portal %d, matchbits "LPX64"\n",
		libcfs_id2str(peer), portal, matchbits);
	return 0;
}

int
srpc_post_active_rdma(int portal, __u64 matchbits, void *buf, int len,
		      int options, lnet_process_id_t peer, lnet_nid_t self,
		      lnet_handle_md_t *mdh, srpc_event_t *ev)
{
	int       rc;
	lnet_md_t md;

	md.user_ptr  = ev;
	md.start     = buf;
	md.length    = len;
	md.eq_handle = srpc_data.rpc_lnet_eq;
	md.threshold = ((options & LNET_MD_OP_GET) != 0) ? 2 : 1;
	md.options   = options & ~(LNET_MD_OP_PUT | LNET_MD_OP_GET);

	rc = LNetMDBind(md, LNET_UNLINK, mdh);
	if (rc != 0) {
		CERROR ("LNetMDBind failed: %d\n", rc);
		LASSERT (rc == -ENOMEM);
		return -ENOMEM;
	}

	/* this is kind of an abuse of the LNET_MD_OP_{PUT,GET} options.
	 * they're only meaningful for MDs attached to an ME (i.e. passive
	 * buffers... */
	if ((options & LNET_MD_OP_PUT) != 0) {
		rc = LNetPut(self, *mdh, LNET_NOACK_REQ, peer,
			     portal, matchbits, 0, 0);
	} else {
		LASSERT ((options & LNET_MD_OP_GET) != 0);

		rc = LNetGet(self, *mdh, peer, portal, matchbits, 0);
	}

	if (rc != 0) {
		CERROR ("LNet%s(%s, %d, "LPD64") failed: %d\n",
			((options & LNET_MD_OP_PUT) != 0) ? "Put" : "Get",
			libcfs_id2str(peer), portal, matchbits, rc);

		/* The forthcoming unlink event will complete this operation
		 * with failure, so fall through and return success here.
		 */
		rc = LNetMDUnlink(*mdh);
		LASSERT (rc == 0);
	} else {
		CDEBUG (D_NET,
			"Posted active RDMA: peer %s, portal %u, matchbits "LPX64"\n",
			libcfs_id2str(peer), portal, matchbits);
	}
	return 0;
}

int
srpc_post_active_rqtbuf(lnet_process_id_t peer, int service, void *buf,
			int len, lnet_handle_md_t *mdh, srpc_event_t *ev)
{
	return srpc_post_active_rdma(srpc_serv_portal(service), service,
				     buf, len, LNET_MD_OP_PUT, peer,
				     LNET_NID_ANY, mdh, ev);
}

int
srpc_post_passive_rqtbuf(int service, int local, void *buf, int len,
			 lnet_handle_md_t *mdh, srpc_event_t *ev)
{
	lnet_process_id_t any = {0};

	any.nid = LNET_NID_ANY;
	any.pid = LNET_PID_ANY;

	return srpc_post_passive_rdma(srpc_serv_portal(service),
				      local, service, buf, len,
				      LNET_MD_OP_PUT, any, mdh, ev);
}

int
srpc_service_post_buffer(struct srpc_service_cd *scd, struct srpc_buffer *buf)
{
	struct srpc_service	*sv = scd->scd_svc;
	struct srpc_msg		*msg = &buf->buf_msg;
	int			rc;

	LNetInvalidateHandle(&buf->buf_mdh);
	list_add(&buf->buf_list, &scd->scd_buf_posted);
	scd->scd_buf_nposted++;
	spin_unlock(&scd->scd_lock);

	rc = srpc_post_passive_rqtbuf(sv->sv_id,
				      !srpc_serv_is_framework(sv),
				      msg, sizeof(*msg), &buf->buf_mdh,
				      &scd->scd_ev);

	/* At this point, a RPC (new or delayed) may have arrived in
	 * msg and its event handler has been called. So we must add
	 * buf to scd_buf_posted _before_ dropping scd_lock */

	spin_lock(&scd->scd_lock);

	if (rc == 0) {
		if (!sv->sv_shuttingdown)
			return 0;

		spin_unlock(&scd->scd_lock);
		/* srpc_shutdown_service might have tried to unlink me
		 * when my buf_mdh was still invalid */
		LNetMDUnlink(buf->buf_mdh);
		spin_lock(&scd->scd_lock);
		return 0;
	}

	scd->scd_buf_nposted--;
	if (sv->sv_shuttingdown)
		return rc; /* don't allow to change scd_buf_posted */

	list_del(&buf->buf_list);
	spin_unlock(&scd->scd_lock);

	LIBCFS_FREE(buf, sizeof(*buf));

	spin_lock(&scd->scd_lock);
	return rc;
}

int
srpc_add_buffer(struct swi_workitem *wi)
{
	struct srpc_service_cd	*scd = wi->swi_workitem.wi_data;
	struct srpc_buffer	*buf;
	int			rc = 0;

	/* it's called by workitem scheduler threads, these threads
	 * should have been set CPT affinity, so buffers will be posted
	 * on CPT local list of Portal */
	spin_lock(&scd->scd_lock);

	while (scd->scd_buf_adjust > 0 &&
	       !scd->scd_svc->sv_shuttingdown) {
		scd->scd_buf_adjust--; /* consume it */
		scd->scd_buf_posting++;

		spin_unlock(&scd->scd_lock);

		LIBCFS_ALLOC(buf, sizeof(*buf));
		if (buf == NULL) {
			CERROR("Failed to add new buf to service: %s\n",
			       scd->scd_svc->sv_name);
			spin_lock(&scd->scd_lock);
			rc = -ENOMEM;
			break;
		}

		spin_lock(&scd->scd_lock);
		if (scd->scd_svc->sv_shuttingdown) {
			spin_unlock(&scd->scd_lock);
			LIBCFS_FREE(buf, sizeof(*buf));

			spin_lock(&scd->scd_lock);
			rc = -ESHUTDOWN;
			break;
		}

		rc = srpc_service_post_buffer(scd, buf);
		if (rc != 0)
			break; /* buf has been freed inside */

		LASSERT(scd->scd_buf_posting > 0);
		scd->scd_buf_posting--;
		scd->scd_buf_total++;
		scd->scd_buf_low = MAX(2, scd->scd_buf_total / 4);
	}

	if (rc != 0) {
		scd->scd_buf_err_stamp = cfs_time_current_sec();
		scd->scd_buf_err = rc;

		LASSERT(scd->scd_buf_posting > 0);
		scd->scd_buf_posting--;
	}

	spin_unlock(&scd->scd_lock);
	return 0;
}

int
srpc_service_add_buffers(struct srpc_service *sv, int nbuffer)
{
	struct srpc_service_cd	*scd;
	int			rc = 0;
	int			i;

	LASSERTF(nbuffer > 0, "nbuffer must be positive: %d\n", nbuffer);

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data) {
		spin_lock(&scd->scd_lock);

		scd->scd_buf_err = 0;
		scd->scd_buf_err_stamp = 0;
		scd->scd_buf_posting = 0;
		scd->scd_buf_adjust = nbuffer;
		/* start to post buffers */
		swi_schedule_workitem(&scd->scd_buf_wi);
		spin_unlock(&scd->scd_lock);

		/* framework service only post buffer for one partition  */
		if (srpc_serv_is_framework(sv))
			break;
	}

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data) {
		spin_lock(&scd->scd_lock);
		/*
		 * NB: srpc_service_add_buffers() can be called inside
		 * thread context of lst_sched_serial, and we don't normally
		 * allow to sleep inside thread context of WI scheduler
		 * because it will block current scheduler thread from doing
		 * anything else, even worse, it could deadlock if it's
		 * waiting on result from another WI of the same scheduler.
		 * However, it's safe at here because scd_buf_wi is scheduled
		 * by thread in a different WI scheduler (lst_sched_test),
		 * so we don't have any risk of deadlock, though this could
		 * block all WIs pending on lst_sched_serial for a moment
		 * which is not good but not fatal.
		 */
		lst_wait_until(scd->scd_buf_err != 0 ||
			       (scd->scd_buf_adjust == 0 &&
				scd->scd_buf_posting == 0),
			       scd->scd_lock, "waiting for adding buffer\n");

		if (scd->scd_buf_err != 0 && rc == 0)
			rc = scd->scd_buf_err;

		spin_unlock(&scd->scd_lock);
	}

	return rc;
}

void
srpc_service_remove_buffers(struct srpc_service *sv, int nbuffer)
{
	struct srpc_service_cd	*scd;
	int			num;
	int			i;

	LASSERT(!sv->sv_shuttingdown);

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data) {
		spin_lock(&scd->scd_lock);

		num = scd->scd_buf_total + scd->scd_buf_posting;
		scd->scd_buf_adjust -= min(nbuffer, num);

		spin_unlock(&scd->scd_lock);
	}
}

/* returns 1 if sv has finished, otherwise 0 */
int
srpc_finish_service(struct srpc_service *sv)
{
	struct srpc_service_cd	*scd;
	struct srpc_server_rpc	*rpc;
	int			i;

	LASSERT(sv->sv_shuttingdown); /* srpc_shutdown_service called */

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data) {
		spin_lock(&scd->scd_lock);
		if (!swi_deschedule_workitem(&scd->scd_buf_wi))
			return 0;

		if (scd->scd_buf_nposted > 0) {
			CDEBUG(D_NET, "waiting for %d posted buffers to unlink",
			       scd->scd_buf_nposted);
			spin_unlock(&scd->scd_lock);
			return 0;
		}

		if (list_empty(&scd->scd_rpc_active)) {
			spin_unlock(&scd->scd_lock);
			continue;
		}

		rpc = list_entry(scd->scd_rpc_active.next,
				     struct srpc_server_rpc, srpc_list);
		CNETERR("Active RPC %p on shutdown: sv %s, peer %s, "
			"wi %s scheduled %d running %d, "
			"ev fired %d type %d status %d lnet %d\n",
			rpc, sv->sv_name, libcfs_id2str(rpc->srpc_peer),
			swi_state2str(rpc->srpc_wi.swi_state),
			rpc->srpc_wi.swi_workitem.wi_scheduled,
			rpc->srpc_wi.swi_workitem.wi_running,
			rpc->srpc_ev.ev_fired, rpc->srpc_ev.ev_type,
			rpc->srpc_ev.ev_status, rpc->srpc_ev.ev_lnet);
		spin_unlock(&scd->scd_lock);
		return 0;
	}

	/* no lock needed from now on */
	srpc_service_fini(sv);
	return 1;
}

/* called with sv->sv_lock held */
void
srpc_service_recycle_buffer(struct srpc_service_cd *scd, srpc_buffer_t *buf)
{
	if (!scd->scd_svc->sv_shuttingdown && scd->scd_buf_adjust >= 0) {
		if (srpc_service_post_buffer(scd, buf) != 0) {
			CWARN("Failed to post %s buffer\n",
			      scd->scd_svc->sv_name);
		}
		return;
	}

	/* service is shutting down, or we want to recycle some buffers */
	scd->scd_buf_total--;

	if (scd->scd_buf_adjust < 0) {
		scd->scd_buf_adjust++;
		if (scd->scd_buf_adjust < 0 &&
		    scd->scd_buf_total == 0 && scd->scd_buf_posting == 0) {
			CDEBUG(D_INFO,
			       "Try to recyle %d buffers but nothing left\n",
			       scd->scd_buf_adjust);
			scd->scd_buf_adjust = 0;
		}
	}

	spin_unlock(&scd->scd_lock);
	LIBCFS_FREE(buf, sizeof(*buf));
	spin_lock(&scd->scd_lock);
}

void
srpc_abort_service(struct srpc_service *sv)
{
	struct srpc_service_cd	*scd;
	struct srpc_server_rpc	*rpc;
	int			i;

	CDEBUG(D_NET, "Aborting service: id %d, name %s\n",
	       sv->sv_id, sv->sv_name);

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data) {
		spin_lock(&scd->scd_lock);

		/* schedule in-flight RPCs to notice the abort, NB:
		 * racing with incoming RPCs; complete fix should make test
		 * RPCs carry session ID in its headers */
		list_for_each_entry(rpc, &scd->scd_rpc_active, srpc_list) {
			rpc->srpc_aborted = 1;
			swi_schedule_workitem(&rpc->srpc_wi);
		}

		spin_unlock(&scd->scd_lock);
	}
}

void
srpc_shutdown_service(srpc_service_t *sv)
{
	struct srpc_service_cd	*scd;
	struct srpc_server_rpc	*rpc;
	srpc_buffer_t		*buf;
	int			i;

	CDEBUG(D_NET, "Shutting down service: id %d, name %s\n",
	       sv->sv_id, sv->sv_name);

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data)
		spin_lock(&scd->scd_lock);

	sv->sv_shuttingdown = 1; /* i.e. no new active RPC */

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data)
		spin_unlock(&scd->scd_lock);

	cfs_percpt_for_each(scd, i, sv->sv_cpt_data) {
		spin_lock(&scd->scd_lock);

		/* schedule in-flight RPCs to notice the shutdown */
		list_for_each_entry(rpc, &scd->scd_rpc_active, srpc_list)
			swi_schedule_workitem(&rpc->srpc_wi);

		spin_unlock(&scd->scd_lock);

		/* OK to traverse scd_buf_posted without lock, since no one
		 * touches scd_buf_posted now */
		list_for_each_entry(buf, &scd->scd_buf_posted, buf_list)
			LNetMDUnlink(buf->buf_mdh);
	}
}

int
srpc_send_request (srpc_client_rpc_t *rpc)
{
	srpc_event_t *ev = &rpc->crpc_reqstev;
	int	   rc;

	ev->ev_fired = 0;
	ev->ev_data  = rpc;
	ev->ev_type  = SRPC_REQUEST_SENT;

	rc = srpc_post_active_rqtbuf(rpc->crpc_dest, rpc->crpc_service,
				     &rpc->crpc_reqstmsg, sizeof(srpc_msg_t),
				     &rpc->crpc_reqstmdh, ev);
	if (rc != 0) {
		LASSERT (rc == -ENOMEM);
		ev->ev_fired = 1;  /* no more event expected */
	}
	return rc;
}

int
srpc_prepare_reply (srpc_client_rpc_t *rpc)
{
	srpc_event_t *ev = &rpc->crpc_replyev;
	__u64	*id = &rpc->crpc_reqstmsg.msg_body.reqst.rpyid;
	int	   rc;

	ev->ev_fired = 0;
	ev->ev_data  = rpc;
	ev->ev_type  = SRPC_REPLY_RCVD;

	*id = srpc_next_id();

	rc = srpc_post_passive_rdma(SRPC_RDMA_PORTAL, 0, *id,
				    &rpc->crpc_replymsg, sizeof(srpc_msg_t),
				    LNET_MD_OP_PUT, rpc->crpc_dest,
				    &rpc->crpc_replymdh, ev);
	if (rc != 0) {
		LASSERT (rc == -ENOMEM);
		ev->ev_fired = 1;  /* no more event expected */
	}
	return rc;
}

int
srpc_prepare_bulk (srpc_client_rpc_t *rpc)
{
	srpc_bulk_t  *bk = &rpc->crpc_bulk;
	srpc_event_t *ev = &rpc->crpc_bulkev;
	__u64	*id = &rpc->crpc_reqstmsg.msg_body.reqst.bulkid;
	int	   rc;
	int	   opt;

	LASSERT (bk->bk_niov <= LNET_MAX_IOV);

	if (bk->bk_niov == 0) return 0; /* nothing to do */

	opt = bk->bk_sink ? LNET_MD_OP_PUT : LNET_MD_OP_GET;
	opt |= LNET_MD_KIOV;

	ev->ev_fired = 0;
	ev->ev_data  = rpc;
	ev->ev_type  = SRPC_BULK_REQ_RCVD;

	*id = srpc_next_id();

	rc = srpc_post_passive_rdma(SRPC_RDMA_PORTAL, 0, *id,
				    &bk->bk_iovs[0], bk->bk_niov, opt,
				    rpc->crpc_dest, &bk->bk_mdh, ev);
	if (rc != 0) {
		LASSERT (rc == -ENOMEM);
		ev->ev_fired = 1;  /* no more event expected */
	}
	return rc;
}

int
srpc_do_bulk (srpc_server_rpc_t *rpc)
{
	srpc_event_t  *ev = &rpc->srpc_ev;
	srpc_bulk_t   *bk = rpc->srpc_bulk;
	__u64	  id = rpc->srpc_reqstbuf->buf_msg.msg_body.reqst.bulkid;
	int	    rc;
	int	    opt;

	LASSERT (bk != NULL);

	opt = bk->bk_sink ? LNET_MD_OP_GET : LNET_MD_OP_PUT;
	opt |= LNET_MD_KIOV;

	ev->ev_fired = 0;
	ev->ev_data  = rpc;
	ev->ev_type  = bk->bk_sink ? SRPC_BULK_GET_RPLD : SRPC_BULK_PUT_SENT;

	rc = srpc_post_active_rdma(SRPC_RDMA_PORTAL, id,
				   &bk->bk_iovs[0], bk->bk_niov, opt,
				   rpc->srpc_peer, rpc->srpc_self,
				   &bk->bk_mdh, ev);
	if (rc != 0)
		ev->ev_fired = 1;  /* no more event expected */
	return rc;
}

/* only called from srpc_handle_rpc */
void
srpc_server_rpc_done(srpc_server_rpc_t *rpc, int status)
{
	struct srpc_service_cd	*scd = rpc->srpc_scd;
	struct srpc_service	*sv  = scd->scd_svc;
	srpc_buffer_t		*buffer;

	LASSERT (status != 0 || rpc->srpc_wi.swi_state == SWI_STATE_DONE);

	rpc->srpc_status = status;

	CDEBUG_LIMIT (status == 0 ? D_NET : D_NETERROR,
		"Server RPC %p done: service %s, peer %s, status %s:%d\n",
		rpc, sv->sv_name, libcfs_id2str(rpc->srpc_peer),
		swi_state2str(rpc->srpc_wi.swi_state), status);

	if (status != 0) {
		spin_lock(&srpc_data.rpc_glock);
		srpc_data.rpc_counters.rpcs_dropped++;
		spin_unlock(&srpc_data.rpc_glock);
	}

	if (rpc->srpc_done != NULL)
		(*rpc->srpc_done) (rpc);
	LASSERT(rpc->srpc_bulk == NULL);

	spin_lock(&scd->scd_lock);

	if (rpc->srpc_reqstbuf != NULL) {
		/* NB might drop sv_lock in srpc_service_recycle_buffer, but
		 * sv won't go away for scd_rpc_active must not be empty */
		srpc_service_recycle_buffer(scd, rpc->srpc_reqstbuf);
		rpc->srpc_reqstbuf = NULL;
	}

	list_del(&rpc->srpc_list); /* from scd->scd_rpc_active */

	/*
	 * No one can schedule me now since:
	 * - I'm not on scd_rpc_active.
	 * - all LNet events have been fired.
	 * Cancel pending schedules and prevent future schedule attempts:
	 */
	LASSERT(rpc->srpc_ev.ev_fired);
	swi_exit_workitem(&rpc->srpc_wi);

	if (!sv->sv_shuttingdown && !list_empty(&scd->scd_buf_blocked)) {
		buffer = list_entry(scd->scd_buf_blocked.next,
					srpc_buffer_t, buf_list);
		list_del(&buffer->buf_list);

		srpc_init_server_rpc(rpc, scd, buffer);
		list_add_tail(&rpc->srpc_list, &scd->scd_rpc_active);
		swi_schedule_workitem(&rpc->srpc_wi);
	} else {
		list_add(&rpc->srpc_list, &scd->scd_rpc_free);
	}

	spin_unlock(&scd->scd_lock);
	return;
}

/* handles an incoming RPC */
int
srpc_handle_rpc(swi_workitem_t *wi)
{
	struct srpc_server_rpc	*rpc = wi->swi_workitem.wi_data;
	struct srpc_service_cd	*scd = rpc->srpc_scd;
	struct srpc_service	*sv = scd->scd_svc;
	srpc_event_t		*ev = &rpc->srpc_ev;
	int			rc = 0;

	LASSERT(wi == &rpc->srpc_wi);

	spin_lock(&scd->scd_lock);

	if (sv->sv_shuttingdown || rpc->srpc_aborted) {
		spin_unlock(&scd->scd_lock);

		if (rpc->srpc_bulk != NULL)
			LNetMDUnlink(rpc->srpc_bulk->bk_mdh);
		LNetMDUnlink(rpc->srpc_replymdh);

		if (ev->ev_fired) { /* no more event, OK to finish */
			srpc_server_rpc_done(rpc, -ESHUTDOWN);
			return 1;
		}
		return 0;
	}

	spin_unlock(&scd->scd_lock);

	switch (wi->swi_state) {
	default:
		LBUG ();
	case SWI_STATE_NEWBORN: {
		srpc_msg_t	   *msg;
		srpc_generic_reply_t *reply;

		msg = &rpc->srpc_reqstbuf->buf_msg;
		reply = &rpc->srpc_replymsg.msg_body.reply;

		if (msg->msg_magic == 0) {
			/* moaned already in srpc_lnet_ev_handler */
			srpc_server_rpc_done(rpc, EBADMSG);
			return 1;
		}

		srpc_unpack_msg_hdr(msg);
		if (msg->msg_version != SRPC_MSG_VERSION) {
			CWARN("Version mismatch: %u, %u expected, from %s\n",
			      msg->msg_version, SRPC_MSG_VERSION,
			      libcfs_id2str(rpc->srpc_peer));
			reply->status = EPROTO;
			/* drop through and send reply */
		} else {
			reply->status = 0;
			rc = (*sv->sv_handler)(rpc);
			LASSERT(reply->status == 0 || !rpc->srpc_bulk);
			if (rc != 0) {
				srpc_server_rpc_done(rpc, rc);
				return 1;
			}
		}

		wi->swi_state = SWI_STATE_BULK_STARTED;

		if (rpc->srpc_bulk != NULL) {
			rc = srpc_do_bulk(rpc);
			if (rc == 0)
				return 0; /* wait for bulk */

			LASSERT (ev->ev_fired);
			ev->ev_status = rc;
		}
	}
	case SWI_STATE_BULK_STARTED:
		LASSERT (rpc->srpc_bulk == NULL || ev->ev_fired);

		if (rpc->srpc_bulk != NULL) {
			rc = ev->ev_status;

			if (sv->sv_bulk_ready != NULL)
				rc = (*sv->sv_bulk_ready) (rpc, rc);

			if (rc != 0) {
				srpc_server_rpc_done(rpc, rc);
				return 1;
			}
		}

		wi->swi_state = SWI_STATE_REPLY_SUBMITTED;
		rc = srpc_send_reply(rpc);
		if (rc == 0)
			return 0; /* wait for reply */
		srpc_server_rpc_done(rpc, rc);
		return 1;

	case SWI_STATE_REPLY_SUBMITTED:
		if (!ev->ev_fired) {
			CERROR("RPC %p: bulk %p, service %d\n",
			       rpc, rpc->srpc_bulk, sv->sv_id);
			CERROR("Event: status %d, type %d, lnet %d\n",
			       ev->ev_status, ev->ev_type, ev->ev_lnet);
			LASSERT (ev->ev_fired);
		}

		wi->swi_state = SWI_STATE_DONE;
		srpc_server_rpc_done(rpc, ev->ev_status);
		return 1;
	}

	return 0;
}

void
srpc_client_rpc_expired (void *data)
{
	srpc_client_rpc_t *rpc = data;

	CWARN ("Client RPC expired: service %d, peer %s, timeout %d.\n",
	       rpc->crpc_service, libcfs_id2str(rpc->crpc_dest),
	       rpc->crpc_timeout);

	spin_lock(&rpc->crpc_lock);

	rpc->crpc_timeout = 0;
	srpc_abort_rpc(rpc, -ETIMEDOUT);

	spin_unlock(&rpc->crpc_lock);

	spin_lock(&srpc_data.rpc_glock);
	srpc_data.rpc_counters.rpcs_expired++;
	spin_unlock(&srpc_data.rpc_glock);
}

inline void
srpc_add_client_rpc_timer (srpc_client_rpc_t *rpc)
{
	stt_timer_t *timer = &rpc->crpc_timer;

	if (rpc->crpc_timeout == 0) return;

	INIT_LIST_HEAD(&timer->stt_list);
	timer->stt_data    = rpc;
	timer->stt_func    = srpc_client_rpc_expired;
	timer->stt_expires = cfs_time_add(rpc->crpc_timeout,
					  cfs_time_current_sec());
	stt_add_timer(timer);
	return;
}

/*
 * Called with rpc->crpc_lock held.
 *
 * Upon exit the RPC expiry timer is not queued and the handler is not
 * running on any CPU. */
void
srpc_del_client_rpc_timer (srpc_client_rpc_t *rpc)
{
	/* timer not planted or already exploded */
	if (rpc->crpc_timeout == 0)
		return;

	/* timer sucessfully defused */
	if (stt_del_timer(&rpc->crpc_timer))
		return;

	/* timer detonated, wait for it to explode */
	while (rpc->crpc_timeout != 0) {
		spin_unlock(&rpc->crpc_lock);

		schedule();

		spin_lock(&rpc->crpc_lock);
	}
}

void
srpc_client_rpc_done (srpc_client_rpc_t *rpc, int status)
{
	swi_workitem_t *wi = &rpc->crpc_wi;

	LASSERT(status != 0 || wi->swi_state == SWI_STATE_DONE);

	spin_lock(&rpc->crpc_lock);

	rpc->crpc_closed = 1;
	if (rpc->crpc_status == 0)
		rpc->crpc_status = status;

	srpc_del_client_rpc_timer(rpc);

	CDEBUG_LIMIT ((status == 0) ? D_NET : D_NETERROR,
		"Client RPC done: service %d, peer %s, status %s:%d:%d\n",
		rpc->crpc_service, libcfs_id2str(rpc->crpc_dest),
		swi_state2str(wi->swi_state), rpc->crpc_aborted, status);

	/*
	 * No one can schedule me now since:
	 * - RPC timer has been defused.
	 * - all LNet events have been fired.
	 * - crpc_closed has been set, preventing srpc_abort_rpc from
	 *   scheduling me.
	 * Cancel pending schedules and prevent future schedule attempts:
	 */
	LASSERT (!srpc_event_pending(rpc));
	swi_exit_workitem(wi);

	spin_unlock(&rpc->crpc_lock);

	(*rpc->crpc_done)(rpc);
	return;
}

/* sends an outgoing RPC */
int
srpc_send_rpc (swi_workitem_t *wi)
{
	int		rc = 0;
	srpc_client_rpc_t *rpc;
	srpc_msg_t	*reply;
	int		do_bulk;

	LASSERT(wi != NULL);

	rpc = wi->swi_workitem.wi_data;

	LASSERT (rpc != NULL);
	LASSERT (wi == &rpc->crpc_wi);

	reply = &rpc->crpc_replymsg;
	do_bulk = rpc->crpc_bulk.bk_niov > 0;

	spin_lock(&rpc->crpc_lock);

	if (rpc->crpc_aborted) {
		spin_unlock(&rpc->crpc_lock);
		goto abort;
	}

	spin_unlock(&rpc->crpc_lock);

	switch (wi->swi_state) {
	default:
		LBUG ();
	case SWI_STATE_NEWBORN:
		LASSERT (!srpc_event_pending(rpc));

		rc = srpc_prepare_reply(rpc);
		if (rc != 0) {
			srpc_client_rpc_done(rpc, rc);
			return 1;
		}

		rc = srpc_prepare_bulk(rpc);
		if (rc != 0) break;

		wi->swi_state = SWI_STATE_REQUEST_SUBMITTED;
		rc = srpc_send_request(rpc);
		break;

	case SWI_STATE_REQUEST_SUBMITTED:
		/* CAVEAT EMPTOR: rqtev, rpyev, and bulkev may come in any
		 * order; however, they're processed in a strict order:
		 * rqt, rpy, and bulk. */
		if (!rpc->crpc_reqstev.ev_fired) break;

		rc = rpc->crpc_reqstev.ev_status;
		if (rc != 0) break;

		wi->swi_state = SWI_STATE_REQUEST_SENT;
		/* perhaps more events, fall thru */
	case SWI_STATE_REQUEST_SENT: {
		srpc_msg_type_t type = srpc_service2reply(rpc->crpc_service);

		if (!rpc->crpc_replyev.ev_fired) break;

		rc = rpc->crpc_replyev.ev_status;
		if (rc != 0) break;

		srpc_unpack_msg_hdr(reply);
		if (reply->msg_type != type ||
		    (reply->msg_magic != SRPC_MSG_MAGIC &&
		     reply->msg_magic != __swab32(SRPC_MSG_MAGIC))) {
			CWARN ("Bad message from %s: type %u (%d expected),"
			       " magic %u (%d expected).\n",
			       libcfs_id2str(rpc->crpc_dest),
			       reply->msg_type, type,
			       reply->msg_magic, SRPC_MSG_MAGIC);
			rc = -EBADMSG;
			break;
		}

		if (do_bulk && reply->msg_body.reply.status != 0) {
			CWARN ("Remote error %d at %s, unlink bulk buffer in "
			       "case peer didn't initiate bulk transfer\n",
			       reply->msg_body.reply.status,
			       libcfs_id2str(rpc->crpc_dest));
			LNetMDUnlink(rpc->crpc_bulk.bk_mdh);
		}

		wi->swi_state = SWI_STATE_REPLY_RECEIVED;
	}
	case SWI_STATE_REPLY_RECEIVED:
		if (do_bulk && !rpc->crpc_bulkev.ev_fired) break;

		rc = do_bulk ? rpc->crpc_bulkev.ev_status : 0;

		/* Bulk buffer was unlinked due to remote error. Clear error
		 * since reply buffer still contains valid data.
		 * NB rpc->crpc_done shouldn't look into bulk data in case of
		 * remote error. */
		if (do_bulk && rpc->crpc_bulkev.ev_lnet == LNET_EVENT_UNLINK &&
		    rpc->crpc_status == 0 && reply->msg_body.reply.status != 0)
			rc = 0;

		wi->swi_state = SWI_STATE_DONE;
		srpc_client_rpc_done(rpc, rc);
		return 1;
	}

	if (rc != 0) {
		spin_lock(&rpc->crpc_lock);
		srpc_abort_rpc(rpc, rc);
		spin_unlock(&rpc->crpc_lock);
	}

abort:
	if (rpc->crpc_aborted) {
		LNetMDUnlink(rpc->crpc_reqstmdh);
		LNetMDUnlink(rpc->crpc_replymdh);
		LNetMDUnlink(rpc->crpc_bulk.bk_mdh);

		if (!srpc_event_pending(rpc)) {
			srpc_client_rpc_done(rpc, -EINTR);
			return 1;
		}
	}
	return 0;
}

srpc_client_rpc_t *
srpc_create_client_rpc (lnet_process_id_t peer, int service,
			int nbulkiov, int bulklen,
			void (*rpc_done)(srpc_client_rpc_t *),
			void (*rpc_fini)(srpc_client_rpc_t *), void *priv)
{
	srpc_client_rpc_t *rpc;

	LIBCFS_ALLOC(rpc, offsetof(srpc_client_rpc_t,
				   crpc_bulk.bk_iovs[nbulkiov]));
	if (rpc == NULL)
		return NULL;

	srpc_init_client_rpc(rpc, peer, service, nbulkiov,
			     bulklen, rpc_done, rpc_fini, priv);
	return rpc;
}

/* called with rpc->crpc_lock held */
void
srpc_abort_rpc (srpc_client_rpc_t *rpc, int why)
{
	LASSERT (why != 0);

	if (rpc->crpc_aborted || /* already aborted */
	    rpc->crpc_closed)    /* callback imminent */
		return;

	CDEBUG (D_NET,
		"Aborting RPC: service %d, peer %s, state %s, why %d\n",
		rpc->crpc_service, libcfs_id2str(rpc->crpc_dest),
		swi_state2str(rpc->crpc_wi.swi_state), why);

	rpc->crpc_aborted = 1;
	rpc->crpc_status  = why;
	swi_schedule_workitem(&rpc->crpc_wi);
	return;
}

/* called with rpc->crpc_lock held */
void
srpc_post_rpc (srpc_client_rpc_t *rpc)
{
	LASSERT (!rpc->crpc_aborted);
	LASSERT (srpc_data.rpc_state == SRPC_STATE_RUNNING);

	CDEBUG (D_NET, "Posting RPC: peer %s, service %d, timeout %d\n",
		libcfs_id2str(rpc->crpc_dest), rpc->crpc_service,
		rpc->crpc_timeout);

	srpc_add_client_rpc_timer(rpc);
	swi_schedule_workitem(&rpc->crpc_wi);
	return;
}


int
srpc_send_reply(struct srpc_server_rpc *rpc)
{
	srpc_event_t		*ev = &rpc->srpc_ev;
	struct srpc_msg		*msg = &rpc->srpc_replymsg;
	struct srpc_buffer	*buffer = rpc->srpc_reqstbuf;
	struct srpc_service_cd	*scd = rpc->srpc_scd;
	struct srpc_service	*sv = scd->scd_svc;
	__u64			rpyid;
	int			rc;

	LASSERT(buffer != NULL);
	rpyid = buffer->buf_msg.msg_body.reqst.rpyid;

	spin_lock(&scd->scd_lock);

	if (!sv->sv_shuttingdown && !srpc_serv_is_framework(sv)) {
		/* Repost buffer before replying since test client
		 * might send me another RPC once it gets the reply */
		if (srpc_service_post_buffer(scd, buffer) != 0)
			CWARN("Failed to repost %s buffer\n", sv->sv_name);
		rpc->srpc_reqstbuf = NULL;
	}

	spin_unlock(&scd->scd_lock);

	ev->ev_fired = 0;
	ev->ev_data  = rpc;
	ev->ev_type  = SRPC_REPLY_SENT;

	msg->msg_magic   = SRPC_MSG_MAGIC;
	msg->msg_version = SRPC_MSG_VERSION;
	msg->msg_type    = srpc_service2reply(sv->sv_id);

	rc = srpc_post_active_rdma(SRPC_RDMA_PORTAL, rpyid, msg,
				   sizeof(*msg), LNET_MD_OP_PUT,
				   rpc->srpc_peer, rpc->srpc_self,
				   &rpc->srpc_replymdh, ev);
	if (rc != 0)
		ev->ev_fired = 1;  /* no more event expected */
	return rc;
}

/* when in kernel always called with LNET_LOCK() held, and in thread context */
void
srpc_lnet_ev_handler(lnet_event_t *ev)
{
	struct srpc_service_cd	*scd;
	srpc_event_t      *rpcev = ev->md.user_ptr;
	srpc_client_rpc_t *crpc;
	srpc_server_rpc_t *srpc;
	srpc_buffer_t     *buffer;
	srpc_service_t    *sv;
	srpc_msg_t	*msg;
	srpc_msg_type_t    type;

	LASSERT (!in_interrupt());

	if (ev->status != 0) {
		spin_lock(&srpc_data.rpc_glock);
		srpc_data.rpc_counters.errors++;
		spin_unlock(&srpc_data.rpc_glock);
	}

	rpcev->ev_lnet = ev->type;

	switch (rpcev->ev_type) {
	default:
		CERROR("Unknown event: status %d, type %d, lnet %d\n",
		       rpcev->ev_status, rpcev->ev_type, rpcev->ev_lnet);
		LBUG ();
	case SRPC_REQUEST_SENT:
		if (ev->status == 0 && ev->type != LNET_EVENT_UNLINK) {
			spin_lock(&srpc_data.rpc_glock);
			srpc_data.rpc_counters.rpcs_sent++;
			spin_unlock(&srpc_data.rpc_glock);
		}
	case SRPC_REPLY_RCVD:
	case SRPC_BULK_REQ_RCVD:
		crpc = rpcev->ev_data;

		if (rpcev != &crpc->crpc_reqstev &&
		    rpcev != &crpc->crpc_replyev &&
		    rpcev != &crpc->crpc_bulkev) {
			CERROR("rpcev %p, crpc %p, reqstev %p, replyev %p, bulkev %p\n",
			       rpcev, crpc, &crpc->crpc_reqstev,
			       &crpc->crpc_replyev, &crpc->crpc_bulkev);
			CERROR("Bad event: status %d, type %d, lnet %d\n",
			       rpcev->ev_status, rpcev->ev_type, rpcev->ev_lnet);
			LBUG ();
		}

		spin_lock(&crpc->crpc_lock);

		LASSERT(rpcev->ev_fired == 0);
		rpcev->ev_fired  = 1;
		rpcev->ev_status = (ev->type == LNET_EVENT_UNLINK) ?
						-EINTR : ev->status;
		swi_schedule_workitem(&crpc->crpc_wi);

		spin_unlock(&crpc->crpc_lock);
		break;

	case SRPC_REQUEST_RCVD:
		scd = rpcev->ev_data;
		sv = scd->scd_svc;

		LASSERT(rpcev == &scd->scd_ev);

		spin_lock(&scd->scd_lock);

		LASSERT (ev->unlinked);
		LASSERT (ev->type == LNET_EVENT_PUT ||
			 ev->type == LNET_EVENT_UNLINK);
		LASSERT (ev->type != LNET_EVENT_UNLINK ||
			 sv->sv_shuttingdown);

		buffer = container_of(ev->md.start, srpc_buffer_t, buf_msg);
		buffer->buf_peer = ev->initiator;
		buffer->buf_self = ev->target.nid;

		LASSERT(scd->scd_buf_nposted > 0);
		scd->scd_buf_nposted--;

		if (sv->sv_shuttingdown) {
			/* Leave buffer on scd->scd_buf_nposted since
			 * srpc_finish_service needs to traverse it. */
			spin_unlock(&scd->scd_lock);
			break;
		}

		if (scd->scd_buf_err_stamp != 0 &&
		    scd->scd_buf_err_stamp < cfs_time_current_sec()) {
			/* re-enable adding buffer */
			scd->scd_buf_err_stamp = 0;
			scd->scd_buf_err = 0;
		}

		if (scd->scd_buf_err == 0 && /* adding buffer is enabled */
		    scd->scd_buf_adjust == 0 &&
		    scd->scd_buf_nposted < scd->scd_buf_low) {
			scd->scd_buf_adjust = MAX(scd->scd_buf_total / 2,
						  SFW_TEST_WI_MIN);
			swi_schedule_workitem(&scd->scd_buf_wi);
		}

		list_del(&buffer->buf_list); /* from scd->scd_buf_posted */
		msg = &buffer->buf_msg;
		type = srpc_service2request(sv->sv_id);

		if (ev->status != 0 || ev->mlength != sizeof(*msg) ||
		    (msg->msg_type != type &&
		     msg->msg_type != __swab32(type)) ||
		    (msg->msg_magic != SRPC_MSG_MAGIC &&
		     msg->msg_magic != __swab32(SRPC_MSG_MAGIC))) {
			CERROR ("Dropping RPC (%s) from %s: "
				"status %d mlength %d type %u magic %u.\n",
				sv->sv_name, libcfs_id2str(ev->initiator),
				ev->status, ev->mlength,
				msg->msg_type, msg->msg_magic);

			/* NB can't call srpc_service_recycle_buffer here since
			 * it may call LNetM[DE]Attach. The invalid magic tells
			 * srpc_handle_rpc to drop this RPC */
			msg->msg_magic = 0;
		}

		if (!list_empty(&scd->scd_rpc_free)) {
			srpc = list_entry(scd->scd_rpc_free.next,
					      struct srpc_server_rpc,
					      srpc_list);
			list_del(&srpc->srpc_list);

			srpc_init_server_rpc(srpc, scd, buffer);
			list_add_tail(&srpc->srpc_list,
					  &scd->scd_rpc_active);
			swi_schedule_workitem(&srpc->srpc_wi);
		} else {
			list_add_tail(&buffer->buf_list,
					  &scd->scd_buf_blocked);
		}

		spin_unlock(&scd->scd_lock);

		spin_lock(&srpc_data.rpc_glock);
		srpc_data.rpc_counters.rpcs_rcvd++;
		spin_unlock(&srpc_data.rpc_glock);
		break;

	case SRPC_BULK_GET_RPLD:
		LASSERT (ev->type == LNET_EVENT_SEND ||
			 ev->type == LNET_EVENT_REPLY ||
			 ev->type == LNET_EVENT_UNLINK);

		if (!ev->unlinked)
			break; /* wait for final event */

	case SRPC_BULK_PUT_SENT:
		if (ev->status == 0 && ev->type != LNET_EVENT_UNLINK) {
			spin_lock(&srpc_data.rpc_glock);

			if (rpcev->ev_type == SRPC_BULK_GET_RPLD)
				srpc_data.rpc_counters.bulk_get += ev->mlength;
			else
				srpc_data.rpc_counters.bulk_put += ev->mlength;

			spin_unlock(&srpc_data.rpc_glock);
		}
	case SRPC_REPLY_SENT:
		srpc = rpcev->ev_data;
		scd  = srpc->srpc_scd;

		LASSERT(rpcev == &srpc->srpc_ev);

		spin_lock(&scd->scd_lock);

		rpcev->ev_fired  = 1;
		rpcev->ev_status = (ev->type == LNET_EVENT_UNLINK) ?
				   -EINTR : ev->status;
		swi_schedule_workitem(&srpc->srpc_wi);

		spin_unlock(&scd->scd_lock);
		break;
	}
}


int
srpc_startup (void)
{
	int rc;

	memset(&srpc_data, 0, sizeof(struct smoketest_rpc));
	spin_lock_init(&srpc_data.rpc_glock);

	/* 1 second pause to avoid timestamp reuse */
	cfs_pause(cfs_time_seconds(1));
	srpc_data.rpc_matchbits = ((__u64) cfs_time_current_sec()) << 48;

	srpc_data.rpc_state = SRPC_STATE_NONE;

	rc = LNetNIInit(LUSTRE_SRV_LNET_PID);
	if (rc < 0) {
		CERROR ("LNetNIInit() has failed: %d\n", rc);
		return rc;
	}

	srpc_data.rpc_state = SRPC_STATE_NI_INIT;

	LNetInvalidateHandle(&srpc_data.rpc_lnet_eq);
	rc = LNetEQAlloc(0, srpc_lnet_ev_handler, &srpc_data.rpc_lnet_eq);
	if (rc != 0) {
		CERROR("LNetEQAlloc() has failed: %d\n", rc);
		goto bail;
	}

	rc = LNetSetLazyPortal(SRPC_FRAMEWORK_REQUEST_PORTAL);
	LASSERT(rc == 0);
	rc = LNetSetLazyPortal(SRPC_REQUEST_PORTAL);
	LASSERT(rc == 0);

	srpc_data.rpc_state = SRPC_STATE_EQ_INIT;

	rc = stt_startup();

bail:
	if (rc != 0)
		srpc_shutdown();
	else
		srpc_data.rpc_state = SRPC_STATE_RUNNING;

	return rc;
}

void
srpc_shutdown (void)
{
	int i;
	int rc;
	int state;

	state = srpc_data.rpc_state;
	srpc_data.rpc_state = SRPC_STATE_STOPPING;

	switch (state) {
	default:
		LBUG ();
	case SRPC_STATE_RUNNING:
		spin_lock(&srpc_data.rpc_glock);

		for (i = 0; i <= SRPC_SERVICE_MAX_ID; i++) {
			srpc_service_t *sv = srpc_data.rpc_services[i];

			LASSERTF (sv == NULL,
				  "service not empty: id %d, name %s\n",
				  i, sv->sv_name);
		}

		spin_unlock(&srpc_data.rpc_glock);

		stt_shutdown();

	case SRPC_STATE_EQ_INIT:
		rc = LNetClearLazyPortal(SRPC_FRAMEWORK_REQUEST_PORTAL);
		rc = LNetClearLazyPortal(SRPC_REQUEST_PORTAL);
		LASSERT (rc == 0);
		rc = LNetEQFree(srpc_data.rpc_lnet_eq);
		LASSERT (rc == 0); /* the EQ should have no user by now */

	case SRPC_STATE_NI_INIT:
		LNetNIFini();
	}

	return;
}
