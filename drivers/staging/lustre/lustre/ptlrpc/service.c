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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2010, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_RPC

#include <obd_support.h>
#include <obd_class.h>
#include <lustre_net.h>
#include <lu_object.h>
#include <uapi/linux/lnet/lnet-types.h>
#include "ptlrpc_internal.h"

/* The following are visible and mutable through /sys/module/ptlrpc */
int test_req_buffer_pressure;
module_param(test_req_buffer_pressure, int, 0444);
MODULE_PARM_DESC(test_req_buffer_pressure, "set non-zero to put pressure on request buffer pools");
module_param(at_min, int, 0644);
MODULE_PARM_DESC(at_min, "Adaptive timeout minimum (sec)");
module_param(at_max, int, 0644);
MODULE_PARM_DESC(at_max, "Adaptive timeout maximum (sec)");
module_param(at_history, int, 0644);
MODULE_PARM_DESC(at_history,
		 "Adaptive timeouts remember the slowest event that took place within this period (sec)");
module_param(at_early_margin, int, 0644);
MODULE_PARM_DESC(at_early_margin, "How soon before an RPC deadline to send an early reply");
module_param(at_extra, int, 0644);
MODULE_PARM_DESC(at_extra, "How much extra time to give with each early reply");

/* forward ref */
static int ptlrpc_server_post_idle_rqbds(struct ptlrpc_service_part *svcpt);
static void ptlrpc_server_hpreq_fini(struct ptlrpc_request *req);
static void ptlrpc_at_remove_timed(struct ptlrpc_request *req);

/** Holds a list of all PTLRPC services */
LIST_HEAD(ptlrpc_all_services);
/** Used to protect the \e ptlrpc_all_services list */
struct mutex ptlrpc_all_services_mutex;

static struct ptlrpc_request_buffer_desc *
ptlrpc_alloc_rqbd(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_service *svc = svcpt->scp_service;
	struct ptlrpc_request_buffer_desc *rqbd;

	rqbd = kzalloc_node(sizeof(*rqbd), GFP_NOFS,
			    cfs_cpt_spread_node(svc->srv_cptable,
						svcpt->scp_cpt));
	if (!rqbd)
		return NULL;

	rqbd->rqbd_svcpt = svcpt;
	rqbd->rqbd_refcount = 0;
	rqbd->rqbd_cbid.cbid_fn = request_in_callback;
	rqbd->rqbd_cbid.cbid_arg = rqbd;
	INIT_LIST_HEAD(&rqbd->rqbd_reqs);
	rqbd->rqbd_buffer = kvzalloc_node(svc->srv_buf_size, GFP_KERNEL,
					  cfs_cpt_spread_node(svc->srv_cptable,
							      svcpt->scp_cpt));

	if (!rqbd->rqbd_buffer) {
		kfree(rqbd);
		return NULL;
	}

	spin_lock(&svcpt->scp_lock);
	list_add(&rqbd->rqbd_list, &svcpt->scp_rqbd_idle);
	svcpt->scp_nrqbds_total++;
	spin_unlock(&svcpt->scp_lock);

	return rqbd;
}

static void
ptlrpc_free_rqbd(struct ptlrpc_request_buffer_desc *rqbd)
{
	struct ptlrpc_service_part *svcpt = rqbd->rqbd_svcpt;

	LASSERT(rqbd->rqbd_refcount == 0);
	LASSERT(list_empty(&rqbd->rqbd_reqs));

	spin_lock(&svcpt->scp_lock);
	list_del(&rqbd->rqbd_list);
	svcpt->scp_nrqbds_total--;
	spin_unlock(&svcpt->scp_lock);

	kvfree(rqbd->rqbd_buffer);
	kfree(rqbd);
}

static int
ptlrpc_grow_req_bufs(struct ptlrpc_service_part *svcpt, int post)
{
	struct ptlrpc_service *svc = svcpt->scp_service;
	struct ptlrpc_request_buffer_desc *rqbd;
	int rc = 0;
	int i;

	if (svcpt->scp_rqbd_allocating)
		goto try_post;

	spin_lock(&svcpt->scp_lock);
	/* check again with lock */
	if (svcpt->scp_rqbd_allocating) {
		/* NB: we might allow more than one thread in the future */
		LASSERT(svcpt->scp_rqbd_allocating == 1);
		spin_unlock(&svcpt->scp_lock);
		goto try_post;
	}

	svcpt->scp_rqbd_allocating++;
	spin_unlock(&svcpt->scp_lock);

	for (i = 0; i < svc->srv_nbuf_per_group; i++) {
		/* NB: another thread might have recycled enough rqbds, we
		 * need to make sure it wouldn't over-allocate, see LU-1212.
		 */
		if (svcpt->scp_nrqbds_posted >= svc->srv_nbuf_per_group)
			break;

		rqbd = ptlrpc_alloc_rqbd(svcpt);

		if (!rqbd) {
			CERROR("%s: Can't allocate request buffer\n",
			       svc->srv_name);
			rc = -ENOMEM;
			break;
		}
	}

	spin_lock(&svcpt->scp_lock);

	LASSERT(svcpt->scp_rqbd_allocating == 1);
	svcpt->scp_rqbd_allocating--;

	spin_unlock(&svcpt->scp_lock);

	CDEBUG(D_RPCTRACE,
	       "%s: allocate %d new %d-byte reqbufs (%d/%d left), rc = %d\n",
	       svc->srv_name, i, svc->srv_buf_size, svcpt->scp_nrqbds_posted,
	       svcpt->scp_nrqbds_total, rc);

 try_post:
	if (post && rc == 0)
		rc = ptlrpc_server_post_idle_rqbds(svcpt);

	return rc;
}

struct ptlrpc_hr_partition;

struct ptlrpc_hr_thread {
	int				hrt_id;		/* thread ID */
	spinlock_t			hrt_lock;
	wait_queue_head_t			hrt_waitq;
	struct list_head			hrt_queue;	/* RS queue */
	struct ptlrpc_hr_partition	*hrt_partition;
};

struct ptlrpc_hr_partition {
	/* # of started threads */
	atomic_t			hrp_nstarted;
	/* # of stopped threads */
	atomic_t			hrp_nstopped;
	/* cpu partition id */
	int				hrp_cpt;
	/* round-robin rotor for choosing thread */
	int				hrp_rotor;
	/* total number of threads on this partition */
	int				hrp_nthrs;
	/* threads table */
	struct ptlrpc_hr_thread		*hrp_thrs;
};

#define HRT_RUNNING 0
#define HRT_STOPPING 1

struct ptlrpc_hr_service {
	/* CPU partition table, it's just cfs_cpt_table for now */
	struct cfs_cpt_table		*hr_cpt_table;
	/** controller sleep waitq */
	wait_queue_head_t			hr_waitq;
	unsigned int			hr_stopping;
	/** roundrobin rotor for non-affinity service */
	unsigned int			hr_rotor;
	/* partition data */
	struct ptlrpc_hr_partition	**hr_partitions;
};

/** reply handling service. */
static struct ptlrpc_hr_service		ptlrpc_hr;

/**
 * Choose an hr thread to dispatch requests to.
 */
static struct ptlrpc_hr_thread *
ptlrpc_hr_select(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_hr_partition *hrp;
	unsigned int rotor;

	if (svcpt->scp_cpt >= 0 &&
	    svcpt->scp_service->srv_cptable == ptlrpc_hr.hr_cpt_table) {
		/* directly match partition */
		hrp = ptlrpc_hr.hr_partitions[svcpt->scp_cpt];

	} else {
		rotor = ptlrpc_hr.hr_rotor++;
		rotor %= cfs_cpt_number(ptlrpc_hr.hr_cpt_table);

		hrp = ptlrpc_hr.hr_partitions[rotor];
	}

	rotor = hrp->hrp_rotor++;
	return &hrp->hrp_thrs[rotor % hrp->hrp_nthrs];
}

/**
 * Put reply state into a queue for processing because we received
 * ACK from the client
 */
void ptlrpc_dispatch_difficult_reply(struct ptlrpc_reply_state *rs)
{
	struct ptlrpc_hr_thread *hrt;

	LASSERT(list_empty(&rs->rs_list));

	hrt = ptlrpc_hr_select(rs->rs_svcpt);

	spin_lock(&hrt->hrt_lock);
	list_add_tail(&rs->rs_list, &hrt->hrt_queue);
	spin_unlock(&hrt->hrt_lock);

	wake_up(&hrt->hrt_waitq);
}

void
ptlrpc_schedule_difficult_reply(struct ptlrpc_reply_state *rs)
{
	assert_spin_locked(&rs->rs_svcpt->scp_rep_lock);
	assert_spin_locked(&rs->rs_lock);
	LASSERT(rs->rs_difficult);
	rs->rs_scheduled_ever = 1;  /* flag any notification attempt */

	if (rs->rs_scheduled) {     /* being set up or already notified */
		return;
	}

	rs->rs_scheduled = 1;
	list_del_init(&rs->rs_list);
	ptlrpc_dispatch_difficult_reply(rs);
}
EXPORT_SYMBOL(ptlrpc_schedule_difficult_reply);

static int
ptlrpc_server_post_idle_rqbds(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_request_buffer_desc *rqbd;
	int rc;
	int posted = 0;

	for (;;) {
		spin_lock(&svcpt->scp_lock);

		if (list_empty(&svcpt->scp_rqbd_idle)) {
			spin_unlock(&svcpt->scp_lock);
			return posted;
		}

		rqbd = list_entry(svcpt->scp_rqbd_idle.next,
				  struct ptlrpc_request_buffer_desc,
				  rqbd_list);
		list_del(&rqbd->rqbd_list);

		/* assume we will post successfully */
		svcpt->scp_nrqbds_posted++;
		list_add(&rqbd->rqbd_list, &svcpt->scp_rqbd_posted);

		spin_unlock(&svcpt->scp_lock);

		rc = ptlrpc_register_rqbd(rqbd);
		if (rc != 0)
			break;

		posted = 1;
	}

	spin_lock(&svcpt->scp_lock);

	svcpt->scp_nrqbds_posted--;
	list_del(&rqbd->rqbd_list);
	list_add_tail(&rqbd->rqbd_list, &svcpt->scp_rqbd_idle);

	/* Don't complain if no request buffers are posted right now; LNET
	 * won't drop requests because we set the portal lazy!
	 */

	spin_unlock(&svcpt->scp_lock);

	return -1;
}

static void ptlrpc_at_timer(struct timer_list *t)
{
	struct ptlrpc_service_part *svcpt;

	svcpt = from_timer(svcpt, t, scp_at_timer);

	svcpt->scp_at_check = 1;
	svcpt->scp_at_checktime = cfs_time_current();
	wake_up(&svcpt->scp_waitq);
}

static void
ptlrpc_server_nthreads_check(struct ptlrpc_service *svc,
			     struct ptlrpc_service_conf *conf)
{
	struct ptlrpc_service_thr_conf *tc = &conf->psc_thr;
	unsigned int init;
	unsigned int total;
	unsigned int nthrs;
	int weight;

	/*
	 * Common code for estimating & validating threads number.
	 * CPT affinity service could have percpt thread-pool instead
	 * of a global thread-pool, which means user might not always
	 * get the threads number they give it in conf::tc_nthrs_user
	 * even they did set. It's because we need to validate threads
	 * number for each CPT to guarantee each pool will have enough
	 * threads to keep the service healthy.
	 */
	init = PTLRPC_NTHRS_INIT + (svc->srv_ops.so_hpreq_handler != NULL);
	init = max_t(int, init, tc->tc_nthrs_init);

	/* NB: please see comments in lustre_lnet.h for definition
	 * details of these members
	 */
	LASSERT(tc->tc_nthrs_max != 0);

	if (tc->tc_nthrs_user != 0) {
		/* In case there is a reason to test a service with many
		 * threads, we give a less strict check here, it can
		 * be up to 8 * nthrs_max
		 */
		total = min(tc->tc_nthrs_max * 8, tc->tc_nthrs_user);
		nthrs = total / svc->srv_ncpts;
		init = max(init, nthrs);
		goto out;
	}

	total = tc->tc_nthrs_max;
	if (tc->tc_nthrs_base == 0) {
		/* don't care about base threads number per partition,
		 * this is most for non-affinity service
		 */
		nthrs = total / svc->srv_ncpts;
		goto out;
	}

	nthrs = tc->tc_nthrs_base;
	if (svc->srv_ncpts == 1) {
		int i;

		/* NB: Increase the base number if it's single partition
		 * and total number of cores/HTs is larger or equal to 4.
		 * result will always < 2 * nthrs_base
		 */
		weight = cfs_cpt_weight(svc->srv_cptable, CFS_CPT_ANY);
		for (i = 1; (weight >> (i + 1)) != 0 && /* >= 4 cores/HTs */
			    (tc->tc_nthrs_base >> i) != 0; i++)
			nthrs += tc->tc_nthrs_base >> i;
	}

	if (tc->tc_thr_factor != 0) {
		int factor = tc->tc_thr_factor;
		const int fade = 4;

		/*
		 * User wants to increase number of threads with for
		 * each CPU core/HT, most likely the factor is larger then
		 * one thread/core because service threads are supposed to
		 * be blocked by lock or wait for IO.
		 */
		/*
		 * Amdahl's law says that adding processors wouldn't give
		 * a linear increasing of parallelism, so it's nonsense to
		 * have too many threads no matter how many cores/HTs
		 * there are.
		 */
		/* weight is # of HTs */
		if (cpumask_weight(topology_sibling_cpumask(0)) > 1) {
			/* depress thread factor for hyper-thread */
			factor = factor - (factor >> 1) + (factor >> 3);
		}

		weight = cfs_cpt_weight(svc->srv_cptable, 0);
		LASSERT(weight > 0);

		for (; factor > 0 && weight > 0; factor--, weight -= fade)
			nthrs += min(weight, fade) * factor;
	}

	if (nthrs * svc->srv_ncpts > tc->tc_nthrs_max) {
		nthrs = max(tc->tc_nthrs_base,
			    tc->tc_nthrs_max / svc->srv_ncpts);
	}
 out:
	nthrs = max(nthrs, tc->tc_nthrs_init);
	svc->srv_nthrs_cpt_limit = nthrs;
	svc->srv_nthrs_cpt_init = init;

	if (nthrs * svc->srv_ncpts > tc->tc_nthrs_max) {
		CDEBUG(D_OTHER, "%s: This service may have more threads (%d) than the given soft limit (%d)\n",
		       svc->srv_name, nthrs * svc->srv_ncpts,
		       tc->tc_nthrs_max);
	}
}

/**
 * Initialize percpt data for a service
 */
static int
ptlrpc_service_part_init(struct ptlrpc_service *svc,
			 struct ptlrpc_service_part *svcpt, int cpt)
{
	struct ptlrpc_at_array	*array;
	int size;
	int index;
	int rc;

	svcpt->scp_cpt = cpt;
	INIT_LIST_HEAD(&svcpt->scp_threads);

	/* rqbd and incoming request queue */
	spin_lock_init(&svcpt->scp_lock);
	INIT_LIST_HEAD(&svcpt->scp_rqbd_idle);
	INIT_LIST_HEAD(&svcpt->scp_rqbd_posted);
	INIT_LIST_HEAD(&svcpt->scp_req_incoming);
	init_waitqueue_head(&svcpt->scp_waitq);
	/* history request & rqbd list */
	INIT_LIST_HEAD(&svcpt->scp_hist_reqs);
	INIT_LIST_HEAD(&svcpt->scp_hist_rqbds);

	/* active requests and hp requests */
	spin_lock_init(&svcpt->scp_req_lock);

	/* reply states */
	spin_lock_init(&svcpt->scp_rep_lock);
	INIT_LIST_HEAD(&svcpt->scp_rep_active);
	INIT_LIST_HEAD(&svcpt->scp_rep_idle);
	init_waitqueue_head(&svcpt->scp_rep_waitq);
	atomic_set(&svcpt->scp_nreps_difficult, 0);

	/* adaptive timeout */
	spin_lock_init(&svcpt->scp_at_lock);
	array = &svcpt->scp_at_array;

	size = at_est2timeout(at_max);
	array->paa_size = size;
	array->paa_count = 0;
	array->paa_deadline = -1;

	/* allocate memory for scp_at_array (ptlrpc_at_array) */
	array->paa_reqs_array =
		kzalloc_node(sizeof(struct list_head) * size, GFP_NOFS,
			     cfs_cpt_spread_node(svc->srv_cptable, cpt));
	if (!array->paa_reqs_array)
		return -ENOMEM;

	for (index = 0; index < size; index++)
		INIT_LIST_HEAD(&array->paa_reqs_array[index]);

	array->paa_reqs_count =
		kzalloc_node(sizeof(__u32) * size, GFP_NOFS,
			     cfs_cpt_spread_node(svc->srv_cptable, cpt));
	if (!array->paa_reqs_count)
		goto free_reqs_array;

	timer_setup(&svcpt->scp_at_timer, ptlrpc_at_timer, 0);

	/* At SOW, service time should be quick; 10s seems generous. If client
	 * timeout is less than this, we'll be sending an early reply.
	 */
	at_init(&svcpt->scp_at_estimate, 10, 0);

	/* assign this before call ptlrpc_grow_req_bufs */
	svcpt->scp_service = svc;
	/* Now allocate the request buffers, but don't post them now */
	rc = ptlrpc_grow_req_bufs(svcpt, 0);
	/* We shouldn't be under memory pressure at startup, so
	 * fail if we can't allocate all our buffers at this time.
	 */
	if (rc != 0)
		goto free_reqs_count;

	return 0;

free_reqs_count:
	kfree(array->paa_reqs_count);
	array->paa_reqs_count = NULL;
free_reqs_array:
	kfree(array->paa_reqs_array);
	array->paa_reqs_array = NULL;

	return -ENOMEM;
}

/**
 * Initialize service on a given portal.
 * This includes starting serving threads , allocating and posting rqbds and
 * so on.
 */
struct ptlrpc_service *
ptlrpc_register_service(struct ptlrpc_service_conf *conf,
			struct kset *parent,
			struct dentry *debugfs_entry)
{
	struct ptlrpc_service_cpt_conf *cconf = &conf->psc_cpt;
	struct ptlrpc_service *service;
	struct ptlrpc_service_part *svcpt;
	struct cfs_cpt_table *cptable;
	__u32 *cpts = NULL;
	int ncpts;
	int cpt;
	int rc;
	int i;

	LASSERT(conf->psc_buf.bc_nbufs > 0);
	LASSERT(conf->psc_buf.bc_buf_size >=
		conf->psc_buf.bc_req_max_size + SPTLRPC_MAX_PAYLOAD);
	LASSERT(conf->psc_thr.tc_ctx_tags != 0);

	cptable = cconf->cc_cptable;
	if (!cptable)
		cptable = cfs_cpt_table;

	if (!conf->psc_thr.tc_cpu_affinity) {
		ncpts = 1;
	} else {
		ncpts = cfs_cpt_number(cptable);
		if (cconf->cc_pattern) {
			struct cfs_expr_list *el;

			rc = cfs_expr_list_parse(cconf->cc_pattern,
						 strlen(cconf->cc_pattern),
						 0, ncpts - 1, &el);
			if (rc != 0) {
				CERROR("%s: invalid CPT pattern string: %s",
				       conf->psc_name, cconf->cc_pattern);
				return ERR_PTR(-EINVAL);
			}

			rc = cfs_expr_list_values(el, ncpts, &cpts);
			cfs_expr_list_free(el);
			if (rc <= 0) {
				CERROR("%s: failed to parse CPT array %s: %d\n",
				       conf->psc_name, cconf->cc_pattern, rc);
				kfree(cpts);
				return ERR_PTR(rc < 0 ? rc : -EINVAL);
			}
			ncpts = rc;
		}
	}

	service = kzalloc(offsetof(struct ptlrpc_service, srv_parts[ncpts]),
			  GFP_NOFS);
	if (!service) {
		kfree(cpts);
		return ERR_PTR(-ENOMEM);
	}

	service->srv_cptable = cptable;
	service->srv_cpts = cpts;
	service->srv_ncpts = ncpts;

	service->srv_cpt_bits = 0; /* it's zero already, easy to read... */
	while ((1 << service->srv_cpt_bits) < cfs_cpt_number(cptable))
		service->srv_cpt_bits++;

	/* public members */
	spin_lock_init(&service->srv_lock);
	service->srv_name = conf->psc_name;
	service->srv_watchdog_factor = conf->psc_watchdog_factor;
	INIT_LIST_HEAD(&service->srv_list); /* for safety of cleanup */

	/* buffer configuration */
	service->srv_nbuf_per_group = test_req_buffer_pressure ?
					  1 : conf->psc_buf.bc_nbufs;
	service->srv_max_req_size = conf->psc_buf.bc_req_max_size +
					  SPTLRPC_MAX_PAYLOAD;
	service->srv_buf_size = conf->psc_buf.bc_buf_size;
	service->srv_rep_portal	= conf->psc_buf.bc_rep_portal;
	service->srv_req_portal	= conf->psc_buf.bc_req_portal;

	/* Increase max reply size to next power of two */
	service->srv_max_reply_size = 1;
	while (service->srv_max_reply_size <
	       conf->psc_buf.bc_rep_max_size + SPTLRPC_MAX_PAYLOAD)
		service->srv_max_reply_size <<= 1;

	service->srv_thread_name = conf->psc_thr.tc_thr_name;
	service->srv_ctx_tags = conf->psc_thr.tc_ctx_tags;
	service->srv_hpreq_ratio = PTLRPC_SVC_HP_RATIO;
	service->srv_ops = conf->psc_ops;

	for (i = 0; i < ncpts; i++) {
		if (!conf->psc_thr.tc_cpu_affinity)
			cpt = CFS_CPT_ANY;
		else
			cpt = cpts ? cpts[i] : i;

		svcpt = kzalloc_node(sizeof(*svcpt), GFP_NOFS,
				     cfs_cpt_spread_node(cptable, cpt));
		if (!svcpt) {
			rc = -ENOMEM;
			goto failed;
		}

		service->srv_parts[i] = svcpt;
		rc = ptlrpc_service_part_init(service, svcpt, cpt);
		if (rc != 0)
			goto failed;
	}

	ptlrpc_server_nthreads_check(service, conf);

	rc = LNetSetLazyPortal(service->srv_req_portal);
	LASSERT(rc == 0);

	mutex_lock(&ptlrpc_all_services_mutex);
	list_add(&service->srv_list, &ptlrpc_all_services);
	mutex_unlock(&ptlrpc_all_services_mutex);

	if (parent) {
		rc = ptlrpc_sysfs_register_service(parent, service);
		if (rc)
			goto failed;
	}

	if (!IS_ERR_OR_NULL(debugfs_entry))
		ptlrpc_ldebugfs_register_service(debugfs_entry, service);

	rc = ptlrpc_service_nrs_setup(service);
	if (rc != 0)
		goto failed;

	CDEBUG(D_NET, "%s: Started, listening on portal %d\n",
	       service->srv_name, service->srv_req_portal);

	rc = ptlrpc_start_threads(service);
	if (rc != 0) {
		CERROR("Failed to start threads for service %s: %d\n",
		       service->srv_name, rc);
		goto failed;
	}

	return service;
failed:
	ptlrpc_unregister_service(service);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL(ptlrpc_register_service);

/**
 * to actually free the request, must be called without holding svc_lock.
 * note it's caller's responsibility to unlink req->rq_list.
 */
static void ptlrpc_server_free_request(struct ptlrpc_request *req)
{
	LASSERT(atomic_read(&req->rq_refcount) == 0);
	LASSERT(list_empty(&req->rq_timed_list));

	 /* DEBUG_REQ() assumes the reply state of a request with a valid
	  * ref will not be destroyed until that reference is dropped.
	  */
	ptlrpc_req_drop_rs(req);

	sptlrpc_svc_ctx_decref(req);

	if (req != &req->rq_rqbd->rqbd_req) {
		/* NB request buffers use an embedded
		 * req if the incoming req unlinked the
		 * MD; this isn't one of them!
		 */
		ptlrpc_request_cache_free(req);
	}
}

/**
 * drop a reference count of the request. if it reaches 0, we either
 * put it into history list, or free it immediately.
 */
static void ptlrpc_server_drop_request(struct ptlrpc_request *req)
{
	struct ptlrpc_request_buffer_desc *rqbd = req->rq_rqbd;
	struct ptlrpc_service_part *svcpt = rqbd->rqbd_svcpt;
	struct ptlrpc_service *svc = svcpt->scp_service;
	int refcount;

	if (!atomic_dec_and_test(&req->rq_refcount))
		return;

	if (req->rq_at_linked) {
		spin_lock(&svcpt->scp_at_lock);
		/* recheck with lock, in case it's unlinked by
		 * ptlrpc_at_check_timed()
		 */
		if (likely(req->rq_at_linked))
			ptlrpc_at_remove_timed(req);
		spin_unlock(&svcpt->scp_at_lock);
	}

	LASSERT(list_empty(&req->rq_timed_list));

	/* finalize request */
	if (req->rq_export) {
		class_export_put(req->rq_export);
		req->rq_export = NULL;
	}

	spin_lock(&svcpt->scp_lock);

	list_add(&req->rq_list, &rqbd->rqbd_reqs);

	refcount = --(rqbd->rqbd_refcount);
	if (refcount == 0) {
		/* request buffer is now idle: add to history */
		list_del(&rqbd->rqbd_list);

		list_add_tail(&rqbd->rqbd_list, &svcpt->scp_hist_rqbds);
		svcpt->scp_hist_nrqbds++;

		/* cull some history?
		 * I expect only about 1 or 2 rqbds need to be recycled here
		 */
		while (svcpt->scp_hist_nrqbds > svc->srv_hist_nrqbds_cpt_max) {
			rqbd = list_entry(svcpt->scp_hist_rqbds.next,
					  struct ptlrpc_request_buffer_desc,
					  rqbd_list);

			list_del(&rqbd->rqbd_list);
			svcpt->scp_hist_nrqbds--;

			/* remove rqbd's reqs from svc's req history while
			 * I've got the service lock
			 */
			list_for_each_entry(req, &rqbd->rqbd_reqs, rq_list) {
				/* Track the highest culled req seq */
				if (req->rq_history_seq >
				    svcpt->scp_hist_seq_culled) {
					svcpt->scp_hist_seq_culled =
						req->rq_history_seq;
				}
				list_del(&req->rq_history_list);
			}

			spin_unlock(&svcpt->scp_lock);

			while ((req = list_first_entry_or_null(
					&rqbd->rqbd_reqs,
					struct ptlrpc_request, rq_list))) {
				list_del(&req->rq_list);
				ptlrpc_server_free_request(req);
			}

			spin_lock(&svcpt->scp_lock);
			/*
			 * now all reqs including the embedded req has been
			 * disposed, schedule request buffer for re-use.
			 */
			LASSERT(atomic_read(&rqbd->rqbd_req.rq_refcount) ==
				0);
			list_add_tail(&rqbd->rqbd_list, &svcpt->scp_rqbd_idle);
		}

		spin_unlock(&svcpt->scp_lock);
	} else if (req->rq_reply_state && req->rq_reply_state->rs_prealloc) {
		/* If we are low on memory, we are not interested in history */
		list_del(&req->rq_list);
		list_del_init(&req->rq_history_list);

		/* Track the highest culled req seq */
		if (req->rq_history_seq > svcpt->scp_hist_seq_culled)
			svcpt->scp_hist_seq_culled = req->rq_history_seq;

		spin_unlock(&svcpt->scp_lock);

		ptlrpc_server_free_request(req);
	} else {
		spin_unlock(&svcpt->scp_lock);
	}
}

/**
 * to finish a request: stop sending more early replies, and release
 * the request.
 */
static void ptlrpc_server_finish_request(struct ptlrpc_service_part *svcpt,
					 struct ptlrpc_request *req)
{
	ptlrpc_server_hpreq_fini(req);

	if (req->rq_session.lc_thread) {
		lu_context_exit(&req->rq_session);
		lu_context_fini(&req->rq_session);
	}

	ptlrpc_server_drop_request(req);
}

/**
 * to finish a active request: stop sending more early replies, and release
 * the request. should be called after we finished handling the request.
 */
static void ptlrpc_server_finish_active_request(
					struct ptlrpc_service_part *svcpt,
					struct ptlrpc_request *req)
{
	spin_lock(&svcpt->scp_req_lock);
	ptlrpc_nrs_req_stop_nolock(req);
	svcpt->scp_nreqs_active--;
	if (req->rq_hp)
		svcpt->scp_nhreqs_active--;
	spin_unlock(&svcpt->scp_req_lock);

	ptlrpc_nrs_req_finalize(req);

	if (req->rq_export)
		class_export_rpc_dec(req->rq_export);

	ptlrpc_server_finish_request(svcpt, req);
}

/**
 * Sanity check request \a req.
 * Return 0 if all is ok, error code otherwise.
 */
static int ptlrpc_check_req(struct ptlrpc_request *req)
{
	struct obd_device *obd = req->rq_export->exp_obd;
	int rc = 0;

	if (unlikely(lustre_msg_get_conn_cnt(req->rq_reqmsg) <
		     req->rq_export->exp_conn_cnt)) {
		DEBUG_REQ(D_RPCTRACE, req,
			  "DROPPING req from old connection %d < %d",
			  lustre_msg_get_conn_cnt(req->rq_reqmsg),
			  req->rq_export->exp_conn_cnt);
		return -EEXIST;
	}
	if (unlikely(!obd || obd->obd_fail)) {
		/*
		 * Failing over, don't handle any more reqs, send
		 * error response instead.
		 */
		CDEBUG(D_RPCTRACE, "Dropping req %p for failed obd %s\n",
		       req, obd ? obd->obd_name : "unknown");
		rc = -ENODEV;
	} else if (lustre_msg_get_flags(req->rq_reqmsg) &
		   (MSG_REPLAY | MSG_REQ_REPLAY_DONE)) {
		DEBUG_REQ(D_ERROR, req, "Invalid replay without recovery");
		class_fail_export(req->rq_export);
		rc = -ENODEV;
	} else if (lustre_msg_get_transno(req->rq_reqmsg) != 0) {
		DEBUG_REQ(D_ERROR, req,
			  "Invalid req with transno %llu without recovery",
			  lustre_msg_get_transno(req->rq_reqmsg));
		class_fail_export(req->rq_export);
		rc = -ENODEV;
	}

	if (unlikely(rc < 0)) {
		req->rq_status = rc;
		ptlrpc_error(req);
	}
	return rc;
}

static void ptlrpc_at_set_timer(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_at_array *array = &svcpt->scp_at_array;
	__s32 next;

	if (array->paa_count == 0) {
		del_timer(&svcpt->scp_at_timer);
		return;
	}

	/* Set timer for closest deadline */
	next = (__s32)(array->paa_deadline - ktime_get_real_seconds() -
		       at_early_margin);
	if (next <= 0) {
		ptlrpc_at_timer(&svcpt->scp_at_timer);
	} else {
		mod_timer(&svcpt->scp_at_timer, cfs_time_shift(next));
		CDEBUG(D_INFO, "armed %s at %+ds\n",
		       svcpt->scp_service->srv_name, next);
	}
}

/* Add rpc to early reply check list */
static int ptlrpc_at_add_timed(struct ptlrpc_request *req)
{
	struct ptlrpc_service_part *svcpt = req->rq_rqbd->rqbd_svcpt;
	struct ptlrpc_at_array *array = &svcpt->scp_at_array;
	struct ptlrpc_request *rq = NULL;
	__u32 index;

	if (AT_OFF)
		return 0;

	if (req->rq_no_reply)
		return 0;

	if ((lustre_msghdr_get_flags(req->rq_reqmsg) & MSGHDR_AT_SUPPORT) == 0)
		return -ENOSYS;

	spin_lock(&svcpt->scp_at_lock);
	LASSERT(list_empty(&req->rq_timed_list));

	div_u64_rem(req->rq_deadline, array->paa_size, &index);
	if (array->paa_reqs_count[index] > 0) {
		/* latest rpcs will have the latest deadlines in the list,
		 * so search backward.
		 */
		list_for_each_entry_reverse(rq, &array->paa_reqs_array[index],
					    rq_timed_list) {
			if (req->rq_deadline >= rq->rq_deadline) {
				list_add(&req->rq_timed_list,
					 &rq->rq_timed_list);
				break;
			}
		}
	}

	/* Add the request at the head of the list */
	if (list_empty(&req->rq_timed_list))
		list_add(&req->rq_timed_list, &array->paa_reqs_array[index]);

	spin_lock(&req->rq_lock);
	req->rq_at_linked = 1;
	spin_unlock(&req->rq_lock);
	req->rq_at_index = index;
	array->paa_reqs_count[index]++;
	array->paa_count++;
	if (array->paa_count == 1 || array->paa_deadline > req->rq_deadline) {
		array->paa_deadline = req->rq_deadline;
		ptlrpc_at_set_timer(svcpt);
	}
	spin_unlock(&svcpt->scp_at_lock);

	return 0;
}

static void
ptlrpc_at_remove_timed(struct ptlrpc_request *req)
{
	struct ptlrpc_at_array *array;

	array = &req->rq_rqbd->rqbd_svcpt->scp_at_array;

	/* NB: must call with hold svcpt::scp_at_lock */
	LASSERT(!list_empty(&req->rq_timed_list));
	list_del_init(&req->rq_timed_list);

	spin_lock(&req->rq_lock);
	req->rq_at_linked = 0;
	spin_unlock(&req->rq_lock);

	array->paa_reqs_count[req->rq_at_index]--;
	array->paa_count--;
}

/*
 * Attempt to extend the request deadline by sending an early reply to the
 * client.
 */
static int ptlrpc_at_send_early_reply(struct ptlrpc_request *req)
{
	struct ptlrpc_service_part *svcpt = req->rq_rqbd->rqbd_svcpt;
	struct ptlrpc_request *reqcopy;
	struct lustre_msg *reqmsg;
	long olddl = req->rq_deadline - ktime_get_real_seconds();
	time64_t newdl;
	int rc;

	/* deadline is when the client expects us to reply, margin is the
	 * difference between clients' and servers' expectations
	 */
	DEBUG_REQ(D_ADAPTTO, req,
		  "%ssending early reply (deadline %+lds, margin %+lds) for %d+%d",
		  AT_OFF ? "AT off - not " : "",
		  olddl, olddl - at_get(&svcpt->scp_at_estimate),
		  at_get(&svcpt->scp_at_estimate), at_extra);

	if (AT_OFF)
		return 0;

	if (olddl < 0) {
		DEBUG_REQ(D_WARNING, req, "Already past deadline (%+lds), not sending early reply. Consider increasing at_early_margin (%d)?",
			  olddl, at_early_margin);

		/* Return an error so we're not re-added to the timed list. */
		return -ETIMEDOUT;
	}

	if (!(lustre_msghdr_get_flags(req->rq_reqmsg) & MSGHDR_AT_SUPPORT)) {
		DEBUG_REQ(D_INFO, req, "Wanted to ask client for more time, but no AT support");
		return -ENOSYS;
	}

	/*
	 * We want to extend the request deadline by at_extra seconds,
	 * so we set our service estimate to reflect how much time has
	 * passed since this request arrived plus an additional
	 * at_extra seconds. The client will calculate the new deadline
	 * based on this service estimate (plus some additional time to
	 * account for network latency). See ptlrpc_at_recv_early_reply
	 */
	at_measured(&svcpt->scp_at_estimate, at_extra +
		    ktime_get_real_seconds() - req->rq_arrival_time.tv_sec);
	newdl = req->rq_arrival_time.tv_sec + at_get(&svcpt->scp_at_estimate);

	/* Check to see if we've actually increased the deadline -
	 * we may be past adaptive_max
	 */
	if (req->rq_deadline >= newdl) {
		DEBUG_REQ(D_WARNING, req, "Couldn't add any time (%ld/%lld), not sending early reply\n",
			  olddl, newdl - ktime_get_real_seconds());
		return -ETIMEDOUT;
	}

	reqcopy = ptlrpc_request_cache_alloc(GFP_NOFS);
	if (!reqcopy)
		return -ENOMEM;
	reqmsg = kvzalloc(req->rq_reqlen, GFP_NOFS);
	if (!reqmsg) {
		rc = -ENOMEM;
		goto out_free;
	}

	*reqcopy = *req;
	reqcopy->rq_reply_state = NULL;
	reqcopy->rq_rep_swab_mask = 0;
	reqcopy->rq_pack_bulk = 0;
	reqcopy->rq_pack_udesc = 0;
	reqcopy->rq_packed_final = 0;
	sptlrpc_svc_ctx_addref(reqcopy);
	/* We only need the reqmsg for the magic */
	reqcopy->rq_reqmsg = reqmsg;
	memcpy(reqmsg, req->rq_reqmsg, req->rq_reqlen);

	LASSERT(atomic_read(&req->rq_refcount));
	/** if it is last refcount then early reply isn't needed */
	if (atomic_read(&req->rq_refcount) == 1) {
		DEBUG_REQ(D_ADAPTTO, reqcopy, "Normal reply already sent out, abort sending early reply\n");
		rc = -EINVAL;
		goto out;
	}

	/* Connection ref */
	reqcopy->rq_export = class_conn2export(
				     lustre_msg_get_handle(reqcopy->rq_reqmsg));
	if (!reqcopy->rq_export) {
		rc = -ENODEV;
		goto out;
	}

	/* RPC ref */
	class_export_rpc_inc(reqcopy->rq_export);
	if (reqcopy->rq_export->exp_obd &&
	    reqcopy->rq_export->exp_obd->obd_fail) {
		rc = -ENODEV;
		goto out_put;
	}

	rc = lustre_pack_reply_flags(reqcopy, 1, NULL, NULL, LPRFL_EARLY_REPLY);
	if (rc)
		goto out_put;

	rc = ptlrpc_send_reply(reqcopy, PTLRPC_REPLY_EARLY);

	if (!rc) {
		/* Adjust our own deadline to what we told the client */
		req->rq_deadline = newdl;
		req->rq_early_count++; /* number sent, server side */
	} else {
		DEBUG_REQ(D_ERROR, req, "Early reply send failed %d", rc);
	}

	/* Free the (early) reply state from lustre_pack_reply.
	 * (ptlrpc_send_reply takes it's own rs ref, so this is safe here)
	 */
	ptlrpc_req_drop_rs(reqcopy);

out_put:
	class_export_rpc_dec(reqcopy->rq_export);
	class_export_put(reqcopy->rq_export);
out:
	sptlrpc_svc_ctx_decref(reqcopy);
	kvfree(reqmsg);
out_free:
	ptlrpc_request_cache_free(reqcopy);
	return rc;
}

/* Send early replies to everybody expiring within at_early_margin
 * asking for at_extra time
 */
static void ptlrpc_at_check_timed(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_at_array *array = &svcpt->scp_at_array;
	struct ptlrpc_request *rq, *n;
	struct list_head work_list;
	__u32 index, count;
	time64_t deadline;
	time64_t now = ktime_get_real_seconds();
	long delay;
	int first, counter = 0;

	spin_lock(&svcpt->scp_at_lock);
	if (svcpt->scp_at_check == 0) {
		spin_unlock(&svcpt->scp_at_lock);
		return;
	}
	delay = cfs_time_sub(cfs_time_current(), svcpt->scp_at_checktime);
	svcpt->scp_at_check = 0;

	if (array->paa_count == 0) {
		spin_unlock(&svcpt->scp_at_lock);
		return;
	}

	/* The timer went off, but maybe the nearest rpc already completed. */
	first = array->paa_deadline - now;
	if (first > at_early_margin) {
		/* We've still got plenty of time.  Reset the timer. */
		ptlrpc_at_set_timer(svcpt);
		spin_unlock(&svcpt->scp_at_lock);
		return;
	}

	/* We're close to a timeout, and we don't know how much longer the
	 * server will take. Send early replies to everyone expiring soon.
	 */
	INIT_LIST_HEAD(&work_list);
	deadline = -1;
	div_u64_rem(array->paa_deadline, array->paa_size, &index);
	count = array->paa_count;
	while (count > 0) {
		count -= array->paa_reqs_count[index];
		list_for_each_entry_safe(rq, n, &array->paa_reqs_array[index],
					 rq_timed_list) {
			if (rq->rq_deadline > now + at_early_margin) {
				/* update the earliest deadline */
				if (deadline == -1 ||
				    rq->rq_deadline < deadline)
					deadline = rq->rq_deadline;
				break;
			}

			ptlrpc_at_remove_timed(rq);
			/**
			 * ptlrpc_server_drop_request() may drop
			 * refcount to 0 already. Let's check this and
			 * don't add entry to work_list
			 */
			if (likely(atomic_inc_not_zero(&rq->rq_refcount)))
				list_add(&rq->rq_timed_list, &work_list);
			counter++;
		}

		if (++index >= array->paa_size)
			index = 0;
	}
	array->paa_deadline = deadline;
	/* we have a new earliest deadline, restart the timer */
	ptlrpc_at_set_timer(svcpt);

	spin_unlock(&svcpt->scp_at_lock);

	CDEBUG(D_ADAPTTO, "timeout in %+ds, asking for %d secs on %d early replies\n",
	       first, at_extra, counter);
	if (first < 0) {
		/* We're already past request deadlines before we even get a
		 * chance to send early replies
		 */
		LCONSOLE_WARN("%s: This server is not able to keep up with request traffic (cpu-bound).\n",
			      svcpt->scp_service->srv_name);
		CWARN("earlyQ=%d reqQ=%d recA=%d, svcEst=%d, delay=%ld(jiff)\n",
		      counter, svcpt->scp_nreqs_incoming,
		      svcpt->scp_nreqs_active,
		      at_get(&svcpt->scp_at_estimate), delay);
	}

	/* we took additional refcount so entries can't be deleted from list, no
	 * locking is needed
	 */
	while (!list_empty(&work_list)) {
		rq = list_entry(work_list.next, struct ptlrpc_request,
				rq_timed_list);
		list_del_init(&rq->rq_timed_list);

		if (ptlrpc_at_send_early_reply(rq) == 0)
			ptlrpc_at_add_timed(rq);

		ptlrpc_server_drop_request(rq);
	}
}

/**
 * Put the request to the export list if the request may become
 * a high priority one.
 */
static int ptlrpc_server_hpreq_init(struct ptlrpc_service_part *svcpt,
				    struct ptlrpc_request *req)
{
	int rc = 0;

	if (svcpt->scp_service->srv_ops.so_hpreq_handler) {
		rc = svcpt->scp_service->srv_ops.so_hpreq_handler(req);
		if (rc < 0)
			return rc;
		LASSERT(rc == 0);
	}
	if (req->rq_export && req->rq_ops) {
		/* Perform request specific check. We should do this check
		 * before the request is added into exp_hp_rpcs list otherwise
		 * it may hit swab race at LU-1044.
		 */
		if (req->rq_ops->hpreq_check) {
			rc = req->rq_ops->hpreq_check(req);
			if (rc == -ESTALE) {
				req->rq_status = rc;
				ptlrpc_error(req);
			}
			/** can only return error,
			 * 0 for normal request,
			 *  or 1 for high priority request
			 */
			LASSERT(rc <= 1);
		}

		spin_lock_bh(&req->rq_export->exp_rpc_lock);
		list_add(&req->rq_exp_list, &req->rq_export->exp_hp_rpcs);
		spin_unlock_bh(&req->rq_export->exp_rpc_lock);
	}

	ptlrpc_nrs_req_initialize(svcpt, req, rc);

	return rc;
}

/** Remove the request from the export list. */
static void ptlrpc_server_hpreq_fini(struct ptlrpc_request *req)
{
	if (req->rq_export && req->rq_ops) {
		/* refresh lock timeout again so that client has more
		 * room to send lock cancel RPC.
		 */
		if (req->rq_ops->hpreq_fini)
			req->rq_ops->hpreq_fini(req);

		spin_lock_bh(&req->rq_export->exp_rpc_lock);
		list_del_init(&req->rq_exp_list);
		spin_unlock_bh(&req->rq_export->exp_rpc_lock);
	}
}

static int ptlrpc_server_request_add(struct ptlrpc_service_part *svcpt,
				     struct ptlrpc_request *req)
{
	int	rc;

	rc = ptlrpc_server_hpreq_init(svcpt, req);
	if (rc < 0)
		return rc;

	ptlrpc_nrs_req_add(svcpt, req, !!rc);

	return 0;
}

/**
 * Allow to handle high priority request
 * User can call it w/o any lock but need to hold
 * ptlrpc_service_part::scp_req_lock to get reliable result
 */
static bool ptlrpc_server_allow_high(struct ptlrpc_service_part *svcpt,
				     bool force)
{
	int running = svcpt->scp_nthrs_running;

	if (!nrs_svcpt_has_hp(svcpt))
		return false;

	if (force)
		return true;

	if (unlikely(svcpt->scp_service->srv_req_portal == MDS_REQUEST_PORTAL &&
		     CFS_FAIL_PRECHECK(OBD_FAIL_PTLRPC_CANCEL_RESEND))) {
		/* leave just 1 thread for normal RPCs */
		running = PTLRPC_NTHRS_INIT;
		if (svcpt->scp_service->srv_ops.so_hpreq_handler)
			running += 1;
	}

	if (svcpt->scp_nreqs_active >= running - 1)
		return false;

	if (svcpt->scp_nhreqs_active == 0)
		return true;

	return !ptlrpc_nrs_req_pending_nolock(svcpt, false) ||
	       svcpt->scp_hreq_count < svcpt->scp_service->srv_hpreq_ratio;
}

static bool ptlrpc_server_high_pending(struct ptlrpc_service_part *svcpt,
				       bool force)
{
	return ptlrpc_server_allow_high(svcpt, force) &&
	       ptlrpc_nrs_req_pending_nolock(svcpt, true);
}

/**
 * Only allow normal priority requests on a service that has a high-priority
 * queue if forced (i.e. cleanup), if there are other high priority requests
 * already being processed (i.e. those threads can service more high-priority
 * requests), or if there are enough idle threads that a later thread can do
 * a high priority request.
 * User can call it w/o any lock but need to hold
 * ptlrpc_service_part::scp_req_lock to get reliable result
 */
static bool ptlrpc_server_allow_normal(struct ptlrpc_service_part *svcpt,
				       bool force)
{
	int running = svcpt->scp_nthrs_running;

	if (unlikely(svcpt->scp_service->srv_req_portal == MDS_REQUEST_PORTAL &&
		     CFS_FAIL_PRECHECK(OBD_FAIL_PTLRPC_CANCEL_RESEND))) {
		/* leave just 1 thread for normal RPCs */
		running = PTLRPC_NTHRS_INIT;
		if (svcpt->scp_service->srv_ops.so_hpreq_handler)
			running += 1;
	}

	if (force ||
	    svcpt->scp_nreqs_active < running - 2)
		return true;

	if (svcpt->scp_nreqs_active >= running - 1)
		return false;

	return svcpt->scp_nhreqs_active > 0 || !nrs_svcpt_has_hp(svcpt);
}

static bool ptlrpc_server_normal_pending(struct ptlrpc_service_part *svcpt,
					 bool force)
{
	return ptlrpc_server_allow_normal(svcpt, force) &&
	       ptlrpc_nrs_req_pending_nolock(svcpt, false);
}

/**
 * Returns true if there are requests available in incoming
 * request queue for processing and it is allowed to fetch them.
 * User can call it w/o any lock but need to hold ptlrpc_service::scp_req_lock
 * to get reliable result
 * \see ptlrpc_server_allow_normal
 * \see ptlrpc_server_allow high
 */
static inline bool
ptlrpc_server_request_pending(struct ptlrpc_service_part *svcpt, bool force)
{
	return ptlrpc_server_high_pending(svcpt, force) ||
	       ptlrpc_server_normal_pending(svcpt, force);
}

/**
 * Fetch a request for processing from queue of unprocessed requests.
 * Favors high-priority requests.
 * Returns a pointer to fetched request.
 */
static struct ptlrpc_request *
ptlrpc_server_request_get(struct ptlrpc_service_part *svcpt, bool force)
{
	struct ptlrpc_request *req = NULL;

	spin_lock(&svcpt->scp_req_lock);

	if (ptlrpc_server_high_pending(svcpt, force)) {
		req = ptlrpc_nrs_req_get_nolock(svcpt, true, force);
		if (req) {
			svcpt->scp_hreq_count++;
			goto got_request;
		}
	}

	if (ptlrpc_server_normal_pending(svcpt, force)) {
		req = ptlrpc_nrs_req_get_nolock(svcpt, false, force);
		if (req) {
			svcpt->scp_hreq_count = 0;
			goto got_request;
		}
	}

	spin_unlock(&svcpt->scp_req_lock);
	return NULL;

got_request:
	svcpt->scp_nreqs_active++;
	if (req->rq_hp)
		svcpt->scp_nhreqs_active++;

	spin_unlock(&svcpt->scp_req_lock);

	if (likely(req->rq_export))
		class_export_rpc_inc(req->rq_export);

	return req;
}

/**
 * Handle freshly incoming reqs, add to timed early reply list,
 * pass on to regular request queue.
 * All incoming requests pass through here before getting into
 * ptlrpc_server_handle_req later on.
 */
static int
ptlrpc_server_handle_req_in(struct ptlrpc_service_part *svcpt,
			    struct ptlrpc_thread *thread)
{
	struct ptlrpc_service *svc = svcpt->scp_service;
	struct ptlrpc_request *req;
	__u32 deadline;
	int rc;

	spin_lock(&svcpt->scp_lock);
	if (list_empty(&svcpt->scp_req_incoming)) {
		spin_unlock(&svcpt->scp_lock);
		return 0;
	}

	req = list_entry(svcpt->scp_req_incoming.next,
			 struct ptlrpc_request, rq_list);
	list_del_init(&req->rq_list);
	svcpt->scp_nreqs_incoming--;
	/* Consider this still a "queued" request as far as stats are
	 * concerned
	 */
	spin_unlock(&svcpt->scp_lock);

	/* go through security check/transform */
	rc = sptlrpc_svc_unwrap_request(req);
	switch (rc) {
	case SECSVC_OK:
		break;
	case SECSVC_COMPLETE:
		target_send_reply(req, 0, OBD_FAIL_MDS_ALL_REPLY_NET);
		goto err_req;
	case SECSVC_DROP:
		goto err_req;
	default:
		LBUG();
	}

	/*
	 * for null-flavored rpc, msg has been unpacked by sptlrpc, although
	 * redo it wouldn't be harmful.
	 */
	if (SPTLRPC_FLVR_POLICY(req->rq_flvr.sf_rpc) != SPTLRPC_POLICY_NULL) {
		rc = ptlrpc_unpack_req_msg(req, req->rq_reqlen);
		if (rc != 0) {
			CERROR("error unpacking request: ptl %d from %s x%llu\n",
			       svc->srv_req_portal, libcfs_id2str(req->rq_peer),
			       req->rq_xid);
			goto err_req;
		}
	}

	rc = lustre_unpack_req_ptlrpc_body(req, MSG_PTLRPC_BODY_OFF);
	if (rc) {
		CERROR("error unpacking ptlrpc body: ptl %d from %s x%llu\n",
		       svc->srv_req_portal, libcfs_id2str(req->rq_peer),
		       req->rq_xid);
		goto err_req;
	}

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_DROP_REQ_OPC) &&
	    lustre_msg_get_opc(req->rq_reqmsg) == cfs_fail_val) {
		CERROR("drop incoming rpc opc %u, x%llu\n",
		       cfs_fail_val, req->rq_xid);
		goto err_req;
	}

	rc = -EINVAL;
	if (lustre_msg_get_type(req->rq_reqmsg) != PTL_RPC_MSG_REQUEST) {
		CERROR("wrong packet type received (type=%u) from %s\n",
		       lustre_msg_get_type(req->rq_reqmsg),
		       libcfs_id2str(req->rq_peer));
		goto err_req;
	}

	switch (lustre_msg_get_opc(req->rq_reqmsg)) {
	case MDS_WRITEPAGE:
	case OST_WRITE:
		req->rq_bulk_write = 1;
		break;
	case MDS_READPAGE:
	case OST_READ:
	case MGS_CONFIG_READ:
		req->rq_bulk_read = 1;
		break;
	}

	CDEBUG(D_RPCTRACE, "got req x%llu\n", req->rq_xid);

	req->rq_export = class_conn2export(
		lustre_msg_get_handle(req->rq_reqmsg));
	if (req->rq_export) {
		rc = ptlrpc_check_req(req);
		if (rc == 0) {
			rc = sptlrpc_target_export_check(req->rq_export, req);
			if (rc)
				DEBUG_REQ(D_ERROR, req, "DROPPING req with illegal security flavor,");
		}

		if (rc)
			goto err_req;
	}

	/* req_in handling should/must be fast */
	if (ktime_get_real_seconds() - req->rq_arrival_time.tv_sec > 5)
		DEBUG_REQ(D_WARNING, req, "Slow req_in handling %llds",
			  (s64)(ktime_get_real_seconds() -
				req->rq_arrival_time.tv_sec));

	/* Set rpc server deadline and add it to the timed list */
	deadline = (lustre_msghdr_get_flags(req->rq_reqmsg) &
		    MSGHDR_AT_SUPPORT) ?
		   /* The max time the client expects us to take */
		   lustre_msg_get_timeout(req->rq_reqmsg) : obd_timeout;
	req->rq_deadline = req->rq_arrival_time.tv_sec + deadline;
	if (unlikely(deadline == 0)) {
		DEBUG_REQ(D_ERROR, req, "Dropping request with 0 timeout");
		goto err_req;
	}

	req->rq_svc_thread = thread;
	if (thread) {
		/* initialize request session, it is needed for request
		 * processing by target
		 */
		rc = lu_context_init(&req->rq_session,
				     LCT_SERVER_SESSION | LCT_NOREF);
		if (rc) {
			CERROR("%s: failure to initialize session: rc = %d\n",
			       thread->t_name, rc);
			goto err_req;
		}
		req->rq_session.lc_thread = thread;
		lu_context_enter(&req->rq_session);
		req->rq_svc_thread->t_env->le_ses = &req->rq_session;
	}

	ptlrpc_at_add_timed(req);

	/* Move it over to the request processing queue */
	rc = ptlrpc_server_request_add(svcpt, req);
	if (rc)
		goto err_req;

	wake_up(&svcpt->scp_waitq);
	return 1;

err_req:
	ptlrpc_server_finish_request(svcpt, req);

	return 1;
}

/**
 * Main incoming request handling logic.
 * Calls handler function from service to do actual processing.
 */
static int
ptlrpc_server_handle_request(struct ptlrpc_service_part *svcpt,
			     struct ptlrpc_thread *thread)
{
	struct ptlrpc_service *svc = svcpt->scp_service;
	struct ptlrpc_request *request;
	struct timespec64 work_start;
	struct timespec64 work_end;
	struct timespec64 timediff;
	struct timespec64 arrived;
	unsigned long timediff_usecs;
	unsigned long arrived_usecs;
	int fail_opc = 0;

	request = ptlrpc_server_request_get(svcpt, false);
	if (!request)
		return 0;

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_HPREQ_NOTIMEOUT))
		fail_opc = OBD_FAIL_PTLRPC_HPREQ_NOTIMEOUT;
	else if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_HPREQ_TIMEOUT))
		fail_opc = OBD_FAIL_PTLRPC_HPREQ_TIMEOUT;

	if (unlikely(fail_opc)) {
		if (request->rq_export && request->rq_ops)
			OBD_FAIL_TIMEOUT(fail_opc, 4);
	}

	ptlrpc_rqphase_move(request, RQ_PHASE_INTERPRET);

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_DUMP_LOG))
		libcfs_debug_dumplog();

	ktime_get_real_ts64(&work_start);
	timediff = timespec64_sub(work_start, request->rq_arrival_time);
	timediff_usecs = timediff.tv_sec * USEC_PER_SEC +
			 timediff.tv_nsec / NSEC_PER_USEC;
	if (likely(svc->srv_stats)) {
		lprocfs_counter_add(svc->srv_stats, PTLRPC_REQWAIT_CNTR,
				    timediff_usecs);
		lprocfs_counter_add(svc->srv_stats, PTLRPC_REQQDEPTH_CNTR,
				    svcpt->scp_nreqs_incoming);
		lprocfs_counter_add(svc->srv_stats, PTLRPC_REQACTIVE_CNTR,
				    svcpt->scp_nreqs_active);
		lprocfs_counter_add(svc->srv_stats, PTLRPC_TIMEOUT,
				    at_get(&svcpt->scp_at_estimate));
	}

	if (likely(request->rq_export)) {
		if (unlikely(ptlrpc_check_req(request)))
			goto put_conn;
	}

	/* Discard requests queued for longer than the deadline.
	 * The deadline is increased if we send an early reply.
	 */
	if (ktime_get_real_seconds() > request->rq_deadline) {
		DEBUG_REQ(D_ERROR, request, "Dropping timed-out request from %s: deadline %lld:%llds ago\n",
			  libcfs_id2str(request->rq_peer),
			  request->rq_deadline -
			  request->rq_arrival_time.tv_sec,
			  ktime_get_real_seconds() - request->rq_deadline);
		goto put_conn;
	}

	CDEBUG(D_RPCTRACE, "Handling RPC pname:cluuid+ref:pid:xid:nid:opc %s:%s+%d:%d:x%llu:%s:%d\n",
	       current_comm(),
	       (request->rq_export ?
		(char *)request->rq_export->exp_client_uuid.uuid : "0"),
	       (request->rq_export ?
		atomic_read(&request->rq_export->exp_refcount) : -99),
	       lustre_msg_get_status(request->rq_reqmsg), request->rq_xid,
	       libcfs_id2str(request->rq_peer),
	       lustre_msg_get_opc(request->rq_reqmsg));

	if (lustre_msg_get_opc(request->rq_reqmsg) != OBD_PING)
		CFS_FAIL_TIMEOUT_MS(OBD_FAIL_PTLRPC_PAUSE_REQ, cfs_fail_val);

	CDEBUG(D_NET, "got req %llu\n", request->rq_xid);

	/* re-assign request and sesson thread to the current one */
	request->rq_svc_thread = thread;
	if (thread) {
		LASSERT(request->rq_session.lc_thread);
		request->rq_session.lc_thread = thread;
		request->rq_session.lc_cookie = 0x55;
		thread->t_env->le_ses = &request->rq_session;
	}
	svc->srv_ops.so_req_handler(request);

	ptlrpc_rqphase_move(request, RQ_PHASE_COMPLETE);

put_conn:
	if (unlikely(ktime_get_real_seconds() > request->rq_deadline)) {
		DEBUG_REQ(D_WARNING, request,
			  "Request took longer than estimated (%lld:%llds); "
			  "client may timeout.",
			  (s64)request->rq_deadline -
			       request->rq_arrival_time.tv_sec,
			  (s64)ktime_get_real_seconds() - request->rq_deadline);
	}

	ktime_get_real_ts64(&work_end);
	timediff = timespec64_sub(work_end, work_start);
	timediff_usecs = timediff.tv_sec * USEC_PER_SEC +
			 timediff.tv_nsec / NSEC_PER_USEC;
	arrived = timespec64_sub(work_end, request->rq_arrival_time);
	arrived_usecs = arrived.tv_sec * USEC_PER_SEC +
			 arrived.tv_nsec / NSEC_PER_USEC;
	CDEBUG(D_RPCTRACE, "Handled RPC pname:cluuid+ref:pid:xid:nid:opc %s:%s+%d:%d:x%llu:%s:%d Request processed in %ldus (%ldus total) trans %llu rc %d/%d\n",
	       current_comm(),
	       (request->rq_export ?
		(char *)request->rq_export->exp_client_uuid.uuid : "0"),
	       (request->rq_export ?
		atomic_read(&request->rq_export->exp_refcount) : -99),
	       lustre_msg_get_status(request->rq_reqmsg),
	       request->rq_xid,
	       libcfs_id2str(request->rq_peer),
	       lustre_msg_get_opc(request->rq_reqmsg),
	       timediff_usecs,
	       arrived_usecs,
	       (request->rq_repmsg ?
		lustre_msg_get_transno(request->rq_repmsg) :
		request->rq_transno),
	       request->rq_status,
	       (request->rq_repmsg ?
		lustre_msg_get_status(request->rq_repmsg) : -999));
	if (likely(svc->srv_stats && request->rq_reqmsg)) {
		__u32 op = lustre_msg_get_opc(request->rq_reqmsg);
		int opc = opcode_offset(op);

		if (opc > 0 && !(op == LDLM_ENQUEUE || op == MDS_REINT)) {
			LASSERT(opc < LUSTRE_MAX_OPCODES);
			lprocfs_counter_add(svc->srv_stats,
					    opc + EXTRA_MAX_OPCODES,
					    timediff_usecs);
		}
	}
	if (unlikely(request->rq_early_count)) {
		DEBUG_REQ(D_ADAPTTO, request,
			  "sent %d early replies before finishing in %llds",
			  request->rq_early_count,
			  (s64)work_end.tv_sec -
			  request->rq_arrival_time.tv_sec);
	}

	ptlrpc_server_finish_active_request(svcpt, request);

	return 1;
}

/**
 * An internal function to process a single reply state object.
 */
static int
ptlrpc_handle_rs(struct ptlrpc_reply_state *rs)
{
	struct ptlrpc_service_part *svcpt = rs->rs_svcpt;
	struct ptlrpc_service *svc = svcpt->scp_service;
	struct obd_export *exp;
	int nlocks;
	int been_handled;

	exp = rs->rs_export;

	LASSERT(rs->rs_difficult);
	LASSERT(rs->rs_scheduled);
	LASSERT(list_empty(&rs->rs_list));

	spin_lock(&exp->exp_lock);
	/* Noop if removed already */
	list_del_init(&rs->rs_exp_list);
	spin_unlock(&exp->exp_lock);

	/* The disk commit callback holds exp_uncommitted_replies_lock while it
	 * iterates over newly committed replies, removing them from
	 * exp_uncommitted_replies.  It then drops this lock and schedules the
	 * replies it found for handling here.
	 *
	 * We can avoid contention for exp_uncommitted_replies_lock between the
	 * HRT threads and further commit callbacks by checking rs_committed
	 * which is set in the commit callback while it holds both
	 * rs_lock and exp_uncommitted_reples.
	 *
	 * If we see rs_committed clear, the commit callback _may_ not have
	 * handled this reply yet and we race with it to grab
	 * exp_uncommitted_replies_lock before removing the reply from
	 * exp_uncommitted_replies.  Note that if we lose the race and the
	 * reply has already been removed, list_del_init() is a noop.
	 *
	 * If we see rs_committed set, we know the commit callback is handling,
	 * or has handled this reply since store reordering might allow us to
	 * see rs_committed set out of sequence.  But since this is done
	 * holding rs_lock, we can be sure it has all completed once we hold
	 * rs_lock, which we do right next.
	 */
	if (!rs->rs_committed) {
		spin_lock(&exp->exp_uncommitted_replies_lock);
		list_del_init(&rs->rs_obd_list);
		spin_unlock(&exp->exp_uncommitted_replies_lock);
	}

	spin_lock(&rs->rs_lock);

	been_handled = rs->rs_handled;
	rs->rs_handled = 1;

	nlocks = rs->rs_nlocks;		 /* atomic "steal", but */
	rs->rs_nlocks = 0;		      /* locks still on rs_locks! */

	if (nlocks == 0 && !been_handled) {
		/* If we see this, we should already have seen the warning
		 * in mds_steal_ack_locks()
		 */
		CDEBUG(D_HA, "All locks stolen from rs %p x%lld.t%lld o%d NID %s\n",
		       rs,
		       rs->rs_xid, rs->rs_transno, rs->rs_opc,
		       libcfs_nid2str(exp->exp_connection->c_peer.nid));
	}

	if ((!been_handled && rs->rs_on_net) || nlocks > 0) {
		spin_unlock(&rs->rs_lock);

		if (!been_handled && rs->rs_on_net) {
			LNetMDUnlink(rs->rs_md_h);
			/* Ignore return code; we're racing with completion */
		}

		while (nlocks-- > 0)
			ldlm_lock_decref(&rs->rs_locks[nlocks],
					 rs->rs_modes[nlocks]);

		spin_lock(&rs->rs_lock);
	}

	rs->rs_scheduled = 0;

	if (!rs->rs_on_net) {
		/* Off the net */
		spin_unlock(&rs->rs_lock);

		class_export_put(exp);
		rs->rs_export = NULL;
		ptlrpc_rs_decref(rs);
		if (atomic_dec_and_test(&svcpt->scp_nreps_difficult) &&
		    svc->srv_is_stopping)
			wake_up_all(&svcpt->scp_waitq);
		return 1;
	}

	/* still on the net; callback will schedule */
	spin_unlock(&rs->rs_lock);
	return 1;
}

static void
ptlrpc_check_rqbd_pool(struct ptlrpc_service_part *svcpt)
{
	int avail = svcpt->scp_nrqbds_posted;
	int low_water = test_req_buffer_pressure ? 0 :
			svcpt->scp_service->srv_nbuf_per_group / 2;

	/* NB I'm not locking; just looking. */

	/* CAVEAT EMPTOR: We might be allocating buffers here because we've
	 * allowed the request history to grow out of control.  We could put a
	 * sanity check on that here and cull some history if we need the
	 * space.
	 */

	if (avail <= low_water)
		ptlrpc_grow_req_bufs(svcpt, 1);

	if (svcpt->scp_service->srv_stats) {
		lprocfs_counter_add(svcpt->scp_service->srv_stats,
				    PTLRPC_REQBUF_AVAIL_CNTR, avail);
	}
}

static inline int
ptlrpc_threads_enough(struct ptlrpc_service_part *svcpt)
{
	return svcpt->scp_nreqs_active <
	       svcpt->scp_nthrs_running - 1 -
	       (svcpt->scp_service->srv_ops.so_hpreq_handler != NULL);
}

/**
 * allowed to create more threads
 * user can call it w/o any lock but need to hold
 * ptlrpc_service_part::scp_lock to get reliable result
 */
static inline int
ptlrpc_threads_increasable(struct ptlrpc_service_part *svcpt)
{
	return svcpt->scp_nthrs_running +
	       svcpt->scp_nthrs_starting <
	       svcpt->scp_service->srv_nthrs_cpt_limit;
}

/**
 * too many requests and allowed to create more threads
 */
static inline int
ptlrpc_threads_need_create(struct ptlrpc_service_part *svcpt)
{
	return !ptlrpc_threads_enough(svcpt) &&
		ptlrpc_threads_increasable(svcpt);
}

static inline int
ptlrpc_thread_stopping(struct ptlrpc_thread *thread)
{
	return thread_is_stopping(thread) ||
	       thread->t_svcpt->scp_service->srv_is_stopping;
}

static inline int
ptlrpc_rqbd_pending(struct ptlrpc_service_part *svcpt)
{
	return !list_empty(&svcpt->scp_rqbd_idle) &&
	       svcpt->scp_rqbd_timeout == 0;
}

static inline int
ptlrpc_at_check(struct ptlrpc_service_part *svcpt)
{
	return svcpt->scp_at_check;
}

/**
 * requests wait on preprocessing
 * user can call it w/o any lock but need to hold
 * ptlrpc_service_part::scp_lock to get reliable result
 */
static inline int
ptlrpc_server_request_incoming(struct ptlrpc_service_part *svcpt)
{
	return !list_empty(&svcpt->scp_req_incoming);
}

/* We perfer lifo queuing, but kernel doesn't provide that yet. */
#ifndef wait_event_idle_exclusive_lifo
#define wait_event_idle_exclusive_lifo wait_event_idle_exclusive
#define wait_event_idle_exclusive_lifo_timeout wait_event_idle_exclusive_timeout
#endif

static __attribute__((__noinline__)) int
ptlrpc_wait_event(struct ptlrpc_service_part *svcpt,
		  struct ptlrpc_thread *thread)
{
	/* Don't exit while there are replies to be handled */

	/* XXX: Add this back when libcfs watchdog is merged upstream
	lc_watchdog_disable(thread->t_watchdog);
	 */

	cond_resched();

	if (svcpt->scp_rqbd_timeout == 0)
		wait_event_idle_exclusive_lifo(
			svcpt->scp_waitq,
			ptlrpc_thread_stopping(thread) ||
			ptlrpc_server_request_incoming(svcpt) ||
			ptlrpc_server_request_pending(svcpt,
						      false) ||
			ptlrpc_rqbd_pending(svcpt) ||
			ptlrpc_at_check(svcpt));
	else if (0 == wait_event_idle_exclusive_lifo_timeout(
			 svcpt->scp_waitq,
			 ptlrpc_thread_stopping(thread) ||
			 ptlrpc_server_request_incoming(svcpt) ||
			 ptlrpc_server_request_pending(svcpt,
						       false) ||
			 ptlrpc_rqbd_pending(svcpt) ||
			 ptlrpc_at_check(svcpt),
			 svcpt->scp_rqbd_timeout))
		svcpt->scp_rqbd_timeout = 0;

	if (ptlrpc_thread_stopping(thread))
		return -EINTR;

	/*
	lc_watchdog_touch(thread->t_watchdog,
			  ptlrpc_server_get_timeout(svcpt));
	 */
	return 0;
}

/**
 * Main thread body for service threads.
 * Waits in a loop waiting for new requests to process to appear.
 * Every time an incoming requests is added to its queue, a waitq
 * is woken up and one of the threads will handle it.
 */
static int ptlrpc_main(void *arg)
{
	struct ptlrpc_thread *thread = arg;
	struct ptlrpc_service_part *svcpt = thread->t_svcpt;
	struct ptlrpc_service *svc = svcpt->scp_service;
	struct ptlrpc_reply_state *rs;
	struct group_info *ginfo = NULL;
	struct lu_env *env;
	int counter = 0, rc = 0;

	thread->t_pid = current_pid();
	unshare_fs_struct();

	/* NB: we will call cfs_cpt_bind() for all threads, because we
	 * might want to run lustre server only on a subset of system CPUs,
	 * in that case ->scp_cpt is CFS_CPT_ANY
	 */
	rc = cfs_cpt_bind(svc->srv_cptable, svcpt->scp_cpt);
	if (rc != 0) {
		CWARN("%s: failed to bind %s on CPT %d\n",
		      svc->srv_name, thread->t_name, svcpt->scp_cpt);
	}

	ginfo = groups_alloc(0);
	if (!ginfo) {
		rc = -ENOMEM;
		goto out;
	}

	set_current_groups(ginfo);
	put_group_info(ginfo);

	if (svc->srv_ops.so_thr_init) {
		rc = svc->srv_ops.so_thr_init(thread);
		if (rc)
			goto out;
	}

	env = kzalloc(sizeof(*env), GFP_KERNEL);
	if (!env) {
		rc = -ENOMEM;
		goto out_srv_fini;
	}

	rc = lu_context_init(&env->le_ctx,
			     svc->srv_ctx_tags | LCT_REMEMBER | LCT_NOREF);
	if (rc)
		goto out_srv_fini;

	thread->t_env = env;
	env->le_ctx.lc_thread = thread;
	env->le_ctx.lc_cookie = 0x6;

	while (!list_empty(&svcpt->scp_rqbd_idle)) {
		rc = ptlrpc_server_post_idle_rqbds(svcpt);
		if (rc >= 0)
			continue;

		CERROR("Failed to post rqbd for %s on CPT %d: %d\n",
		       svc->srv_name, svcpt->scp_cpt, rc);
		goto out_srv_fini;
	}

	/* Alloc reply state structure for this one */
	rs = kvzalloc(svc->srv_max_reply_size, GFP_KERNEL);
	if (!rs) {
		rc = -ENOMEM;
		goto out_srv_fini;
	}

	spin_lock(&svcpt->scp_lock);

	LASSERT(thread_is_starting(thread));
	thread_clear_flags(thread, SVC_STARTING);

	LASSERT(svcpt->scp_nthrs_starting == 1);
	svcpt->scp_nthrs_starting--;

	/* SVC_STOPPING may already be set here if someone else is trying
	 * to stop the service while this new thread has been dynamically
	 * forked. We still set SVC_RUNNING to let our creator know that
	 * we are now running, however we will exit as soon as possible
	 */
	thread_add_flags(thread, SVC_RUNNING);
	svcpt->scp_nthrs_running++;
	spin_unlock(&svcpt->scp_lock);

	/* wake up our creator in case he's still waiting. */
	wake_up(&thread->t_ctl_waitq);

	/*
	thread->t_watchdog = lc_watchdog_add(ptlrpc_server_get_timeout(svcpt),
					     NULL, NULL);
	 */

	spin_lock(&svcpt->scp_rep_lock);
	list_add(&rs->rs_list, &svcpt->scp_rep_idle);
	wake_up(&svcpt->scp_rep_waitq);
	spin_unlock(&svcpt->scp_rep_lock);

	CDEBUG(D_NET, "service thread %d (#%d) started\n", thread->t_id,
	       svcpt->scp_nthrs_running);

	/* XXX maintain a list of all managed devices: insert here */
	while (!ptlrpc_thread_stopping(thread)) {
		if (ptlrpc_wait_event(svcpt, thread))
			break;

		ptlrpc_check_rqbd_pool(svcpt);

		if (ptlrpc_threads_need_create(svcpt)) {
			/* Ignore return code - we tried... */
			ptlrpc_start_thread(svcpt, 0);
		}

		/* Process all incoming reqs before handling any */
		if (ptlrpc_server_request_incoming(svcpt)) {
			lu_context_enter(&env->le_ctx);
			env->le_ses = NULL;
			ptlrpc_server_handle_req_in(svcpt, thread);
			lu_context_exit(&env->le_ctx);

			/* but limit ourselves in case of flood */
			if (counter++ < 100)
				continue;
			counter = 0;
		}

		if (ptlrpc_at_check(svcpt))
			ptlrpc_at_check_timed(svcpt);

		if (ptlrpc_server_request_pending(svcpt, false)) {
			lu_context_enter(&env->le_ctx);
			ptlrpc_server_handle_request(svcpt, thread);
			lu_context_exit(&env->le_ctx);
		}

		if (ptlrpc_rqbd_pending(svcpt) &&
		    ptlrpc_server_post_idle_rqbds(svcpt) < 0) {
			/* I just failed to repost request buffers.
			 * Wait for a timeout (unless something else
			 * happens) before I try again
			 */
			svcpt->scp_rqbd_timeout = HZ / 10;
			CDEBUG(D_RPCTRACE, "Posted buffers: %d\n",
			       svcpt->scp_nrqbds_posted);
		}
	}

	/*
	lc_watchdog_delete(thread->t_watchdog);
	thread->t_watchdog = NULL;
	*/

out_srv_fini:
	/*
	 * deconstruct service specific state created by ptlrpc_start_thread()
	 */
	if (svc->srv_ops.so_thr_done)
		svc->srv_ops.so_thr_done(thread);

	if (env) {
		lu_context_fini(&env->le_ctx);
		kfree(env);
	}
out:
	CDEBUG(D_RPCTRACE, "service thread [ %p : %u ] %d exiting: rc %d\n",
	       thread, thread->t_pid, thread->t_id, rc);

	spin_lock(&svcpt->scp_lock);
	if (thread_test_and_clear_flags(thread, SVC_STARTING))
		svcpt->scp_nthrs_starting--;

	if (thread_test_and_clear_flags(thread, SVC_RUNNING)) {
		/* must know immediately */
		svcpt->scp_nthrs_running--;
	}

	thread->t_id = rc;
	thread_add_flags(thread, SVC_STOPPED);

	wake_up(&thread->t_ctl_waitq);
	spin_unlock(&svcpt->scp_lock);

	return rc;
}

static int hrt_dont_sleep(struct ptlrpc_hr_thread *hrt,
			  struct list_head *replies)
{
	int result;

	spin_lock(&hrt->hrt_lock);

	list_splice_init(&hrt->hrt_queue, replies);
	result = ptlrpc_hr.hr_stopping || !list_empty(replies);

	spin_unlock(&hrt->hrt_lock);
	return result;
}

/**
 * Main body of "handle reply" function.
 * It processes acked reply states
 */
static int ptlrpc_hr_main(void *arg)
{
	struct ptlrpc_hr_thread	*hrt = arg;
	struct ptlrpc_hr_partition *hrp = hrt->hrt_partition;
	LIST_HEAD(replies);
	char threadname[20];
	int rc;

	snprintf(threadname, sizeof(threadname), "ptlrpc_hr%02d_%03d",
		 hrp->hrp_cpt, hrt->hrt_id);
	unshare_fs_struct();

	rc = cfs_cpt_bind(ptlrpc_hr.hr_cpt_table, hrp->hrp_cpt);
	if (rc != 0) {
		CWARN("Failed to bind %s on CPT %d of CPT table %p: rc = %d\n",
		      threadname, hrp->hrp_cpt, ptlrpc_hr.hr_cpt_table, rc);
	}

	atomic_inc(&hrp->hrp_nstarted);
	wake_up(&ptlrpc_hr.hr_waitq);

	while (!ptlrpc_hr.hr_stopping) {
		wait_event_idle(hrt->hrt_waitq, hrt_dont_sleep(hrt, &replies));

		while (!list_empty(&replies)) {
			struct ptlrpc_reply_state *rs;

			rs = list_entry(replies.prev, struct ptlrpc_reply_state,
					rs_list);
			list_del_init(&rs->rs_list);
			ptlrpc_handle_rs(rs);
		}
	}

	atomic_inc(&hrp->hrp_nstopped);
	wake_up(&ptlrpc_hr.hr_waitq);

	return 0;
}

static void ptlrpc_stop_hr_threads(void)
{
	struct ptlrpc_hr_partition *hrp;
	int i;
	int j;

	ptlrpc_hr.hr_stopping = 1;

	cfs_percpt_for_each(hrp, i, ptlrpc_hr.hr_partitions) {
		if (!hrp->hrp_thrs)
			continue; /* uninitialized */
		for (j = 0; j < hrp->hrp_nthrs; j++)
			wake_up_all(&hrp->hrp_thrs[j].hrt_waitq);
	}

	cfs_percpt_for_each(hrp, i, ptlrpc_hr.hr_partitions) {
		if (!hrp->hrp_thrs)
			continue; /* uninitialized */
		wait_event(ptlrpc_hr.hr_waitq,
			   atomic_read(&hrp->hrp_nstopped) ==
			   atomic_read(&hrp->hrp_nstarted));
	}
}

static int ptlrpc_start_hr_threads(void)
{
	struct ptlrpc_hr_partition *hrp;
	int i;
	int j;

	cfs_percpt_for_each(hrp, i, ptlrpc_hr.hr_partitions) {
		int rc = 0;

		for (j = 0; j < hrp->hrp_nthrs; j++) {
			struct	ptlrpc_hr_thread *hrt = &hrp->hrp_thrs[j];
			struct task_struct *task;

			task = kthread_run(ptlrpc_hr_main,
					   &hrp->hrp_thrs[j],
					   "ptlrpc_hr%02d_%03d",
					   hrp->hrp_cpt, hrt->hrt_id);
			if (IS_ERR(task)) {
				rc = PTR_ERR(task);
				break;
			}
		}
		wait_event(ptlrpc_hr.hr_waitq,
			   atomic_read(&hrp->hrp_nstarted) == j);

		if (rc < 0) {
			CERROR("cannot start reply handler thread %d:%d: rc = %d\n",
			       i, j, rc);
			ptlrpc_stop_hr_threads();
			return rc;
		}
	}
	return 0;
}

static void ptlrpc_svcpt_stop_threads(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_thread *thread;
	LIST_HEAD(zombie);

	CDEBUG(D_INFO, "Stopping threads for service %s\n",
	       svcpt->scp_service->srv_name);

	spin_lock(&svcpt->scp_lock);
	/* let the thread know that we would like it to stop asap */
	list_for_each_entry(thread, &svcpt->scp_threads, t_link) {
		CDEBUG(D_INFO, "Stopping thread %s #%u\n",
		       svcpt->scp_service->srv_thread_name, thread->t_id);
		thread_add_flags(thread, SVC_STOPPING);
	}

	wake_up_all(&svcpt->scp_waitq);

	while (!list_empty(&svcpt->scp_threads)) {
		thread = list_entry(svcpt->scp_threads.next,
				    struct ptlrpc_thread, t_link);
		if (thread_is_stopped(thread)) {
			list_del(&thread->t_link);
			list_add(&thread->t_link, &zombie);
			continue;
		}
		spin_unlock(&svcpt->scp_lock);

		CDEBUG(D_INFO, "waiting for stopping-thread %s #%u\n",
		       svcpt->scp_service->srv_thread_name, thread->t_id);
		wait_event_idle(thread->t_ctl_waitq,
				thread_is_stopped(thread));

		spin_lock(&svcpt->scp_lock);
	}

	spin_unlock(&svcpt->scp_lock);

	while (!list_empty(&zombie)) {
		thread = list_entry(zombie.next,
				    struct ptlrpc_thread, t_link);
		list_del(&thread->t_link);
		kfree(thread);
	}
}

/**
 * Stops all threads of a particular service \a svc
 */
static void ptlrpc_stop_all_threads(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part *svcpt;
	int i;

	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if (svcpt->scp_service)
			ptlrpc_svcpt_stop_threads(svcpt);
	}
}

int ptlrpc_start_threads(struct ptlrpc_service *svc)
{
	int rc = 0;
	int i;
	int j;

	/* We require 2 threads min, see note in ptlrpc_server_handle_request */
	LASSERT(svc->srv_nthrs_cpt_init >= PTLRPC_NTHRS_INIT);

	for (i = 0; i < svc->srv_ncpts; i++) {
		for (j = 0; j < svc->srv_nthrs_cpt_init; j++) {
			rc = ptlrpc_start_thread(svc->srv_parts[i], 1);
			if (rc == 0)
				continue;

			if (rc != -EMFILE)
				goto failed;
			/* We have enough threads, don't start more. b=15759 */
			break;
		}
	}

	return 0;
 failed:
	CERROR("cannot start %s thread #%d_%d: rc %d\n",
	       svc->srv_thread_name, i, j, rc);
	ptlrpc_stop_all_threads(svc);
	return rc;
}

int ptlrpc_start_thread(struct ptlrpc_service_part *svcpt, int wait)
{
	struct ptlrpc_thread *thread;
	struct ptlrpc_service *svc;
	struct task_struct *task;
	int rc;

	svc = svcpt->scp_service;

	CDEBUG(D_RPCTRACE, "%s[%d] started %d min %d max %d\n",
	       svc->srv_name, svcpt->scp_cpt, svcpt->scp_nthrs_running,
	       svc->srv_nthrs_cpt_init, svc->srv_nthrs_cpt_limit);

 again:
	if (unlikely(svc->srv_is_stopping))
		return -ESRCH;

	if (!ptlrpc_threads_increasable(svcpt) ||
	    (OBD_FAIL_CHECK(OBD_FAIL_TGT_TOOMANY_THREADS) &&
	     svcpt->scp_nthrs_running == svc->srv_nthrs_cpt_init - 1))
		return -EMFILE;

	thread = kzalloc_node(sizeof(*thread), GFP_NOFS,
			      cfs_cpt_spread_node(svc->srv_cptable,
						  svcpt->scp_cpt));
	if (!thread)
		return -ENOMEM;
	init_waitqueue_head(&thread->t_ctl_waitq);

	spin_lock(&svcpt->scp_lock);
	if (!ptlrpc_threads_increasable(svcpt)) {
		spin_unlock(&svcpt->scp_lock);
		kfree(thread);
		return -EMFILE;
	}

	if (svcpt->scp_nthrs_starting != 0) {
		/* serialize starting because some modules (obdfilter)
		 * might require unique and contiguous t_id
		 */
		LASSERT(svcpt->scp_nthrs_starting == 1);
		spin_unlock(&svcpt->scp_lock);
		kfree(thread);
		if (wait) {
			CDEBUG(D_INFO, "Waiting for creating thread %s #%d\n",
			       svc->srv_thread_name, svcpt->scp_thr_nextid);
			schedule();
			goto again;
		}

		CDEBUG(D_INFO, "Creating thread %s #%d race, retry later\n",
		       svc->srv_thread_name, svcpt->scp_thr_nextid);
		return -EAGAIN;
	}

	svcpt->scp_nthrs_starting++;
	thread->t_id = svcpt->scp_thr_nextid++;
	thread_add_flags(thread, SVC_STARTING);
	thread->t_svcpt = svcpt;

	list_add(&thread->t_link, &svcpt->scp_threads);
	spin_unlock(&svcpt->scp_lock);

	if (svcpt->scp_cpt >= 0) {
		snprintf(thread->t_name, sizeof(thread->t_name), "%s%02d_%03d",
			 svc->srv_thread_name, svcpt->scp_cpt, thread->t_id);
	} else {
		snprintf(thread->t_name, sizeof(thread->t_name), "%s_%04d",
			 svc->srv_thread_name, thread->t_id);
	}

	CDEBUG(D_RPCTRACE, "starting thread '%s'\n", thread->t_name);
	task = kthread_run(ptlrpc_main, thread, "%s", thread->t_name);
	if (IS_ERR(task)) {
		rc = PTR_ERR(task);
		CERROR("cannot start thread '%s': rc = %d\n",
		       thread->t_name, rc);
		spin_lock(&svcpt->scp_lock);
		--svcpt->scp_nthrs_starting;
		if (thread_is_stopping(thread)) {
			/* this ptlrpc_thread is being handled
			 * by ptlrpc_svcpt_stop_threads now
			 */
			thread_add_flags(thread, SVC_STOPPED);
			wake_up(&thread->t_ctl_waitq);
			spin_unlock(&svcpt->scp_lock);
		} else {
			list_del(&thread->t_link);
			spin_unlock(&svcpt->scp_lock);
			kfree(thread);
		}
		return rc;
	}

	if (!wait)
		return 0;

	wait_event_idle(thread->t_ctl_waitq,
			thread_is_running(thread) || thread_is_stopped(thread));

	rc = thread_is_stopped(thread) ? thread->t_id : 0;
	return rc;
}

int ptlrpc_hr_init(void)
{
	struct ptlrpc_hr_partition *hrp;
	struct ptlrpc_hr_thread	*hrt;
	int rc;
	int i;
	int j;
	int weight;

	memset(&ptlrpc_hr, 0, sizeof(ptlrpc_hr));
	ptlrpc_hr.hr_cpt_table = cfs_cpt_table;

	ptlrpc_hr.hr_partitions = cfs_percpt_alloc(ptlrpc_hr.hr_cpt_table,
						   sizeof(*hrp));
	if (!ptlrpc_hr.hr_partitions)
		return -ENOMEM;

	init_waitqueue_head(&ptlrpc_hr.hr_waitq);

	weight = cpumask_weight(topology_sibling_cpumask(0));

	cfs_percpt_for_each(hrp, i, ptlrpc_hr.hr_partitions) {
		hrp->hrp_cpt = i;

		atomic_set(&hrp->hrp_nstarted, 0);
		atomic_set(&hrp->hrp_nstopped, 0);

		hrp->hrp_nthrs = cfs_cpt_weight(ptlrpc_hr.hr_cpt_table, i);
		hrp->hrp_nthrs /= weight;
		if (hrp->hrp_nthrs == 0)
			hrp->hrp_nthrs = 1;

		hrp->hrp_thrs =
			kzalloc_node(hrp->hrp_nthrs * sizeof(*hrt), GFP_NOFS,
				     cfs_cpt_spread_node(ptlrpc_hr.hr_cpt_table,
							 i));
		if (!hrp->hrp_thrs) {
			rc = -ENOMEM;
			goto out;
		}

		for (j = 0; j < hrp->hrp_nthrs; j++) {
			hrt = &hrp->hrp_thrs[j];

			hrt->hrt_id = j;
			hrt->hrt_partition = hrp;
			init_waitqueue_head(&hrt->hrt_waitq);
			spin_lock_init(&hrt->hrt_lock);
			INIT_LIST_HEAD(&hrt->hrt_queue);
		}
	}

	rc = ptlrpc_start_hr_threads();
out:
	if (rc != 0)
		ptlrpc_hr_fini();
	return rc;
}

void ptlrpc_hr_fini(void)
{
	struct ptlrpc_hr_partition *hrp;
	int i;

	if (!ptlrpc_hr.hr_partitions)
		return;

	ptlrpc_stop_hr_threads();

	cfs_percpt_for_each(hrp, i, ptlrpc_hr.hr_partitions) {
		kfree(hrp->hrp_thrs);
	}

	cfs_percpt_free(ptlrpc_hr.hr_partitions);
	ptlrpc_hr.hr_partitions = NULL;
}

/**
 * Wait until all already scheduled replies are processed.
 */
static void ptlrpc_wait_replies(struct ptlrpc_service_part *svcpt)
{
	while (1) {
		int rc;

		rc = wait_event_idle_timeout(
			svcpt->scp_waitq,
			atomic_read(&svcpt->scp_nreps_difficult) == 0,
			10 * HZ);
		if (rc > 0)
			break;
		CWARN("Unexpectedly long timeout %s %p\n",
		      svcpt->scp_service->srv_name, svcpt->scp_service);
	}
}

static void
ptlrpc_service_del_atimer(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part *svcpt;
	int i;

	/* early disarm AT timer... */
	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if (svcpt->scp_service)
			del_timer(&svcpt->scp_at_timer);
	}
}

static void
ptlrpc_service_unlink_rqbd(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part *svcpt;
	struct ptlrpc_request_buffer_desc *rqbd;
	int cnt;
	int rc;
	int i;

	/* All history will be culled when the next request buffer is
	 * freed in ptlrpc_service_purge_all()
	 */
	svc->srv_hist_nrqbds_cpt_max = 0;

	rc = LNetClearLazyPortal(svc->srv_req_portal);
	LASSERT(rc == 0);

	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if (!svcpt->scp_service)
			break;

		/* Unlink all the request buffers.  This forces a 'final'
		 * event with its 'unlink' flag set for each posted rqbd
		 */
		list_for_each_entry(rqbd, &svcpt->scp_rqbd_posted,
				    rqbd_list) {
			rc = LNetMDUnlink(rqbd->rqbd_md_h);
			LASSERT(rc == 0 || rc == -ENOENT);
		}
	}

	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if (!svcpt->scp_service)
			break;

		/* Wait for the network to release any buffers
		 * it's currently filling
		 */
		spin_lock(&svcpt->scp_lock);
		while (svcpt->scp_nrqbds_posted != 0) {
			spin_unlock(&svcpt->scp_lock);
			/* Network access will complete in finite time but
			 * the HUGE timeout lets us CWARN for visibility
			 * of sluggish LNDs
			 */
			cnt = 0;
			while (cnt < LONG_UNLINK &&
			       (rc = wait_event_idle_timeout(svcpt->scp_waitq,
							     svcpt->scp_nrqbds_posted == 0,
							     HZ)) == 0)
				cnt++;
			if (rc == 0) {
				CWARN("Service %s waiting for request buffers\n",
				      svcpt->scp_service->srv_name);
			}
			spin_lock(&svcpt->scp_lock);
		}
		spin_unlock(&svcpt->scp_lock);
	}
}

static void
ptlrpc_service_purge_all(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part *svcpt;
	struct ptlrpc_request_buffer_desc *rqbd;
	struct ptlrpc_request *req;
	struct ptlrpc_reply_state *rs;
	int i;

	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if (!svcpt->scp_service)
			break;

		spin_lock(&svcpt->scp_rep_lock);
		while (!list_empty(&svcpt->scp_rep_active)) {
			rs = list_entry(svcpt->scp_rep_active.next,
					struct ptlrpc_reply_state, rs_list);
			spin_lock(&rs->rs_lock);
			ptlrpc_schedule_difficult_reply(rs);
			spin_unlock(&rs->rs_lock);
		}
		spin_unlock(&svcpt->scp_rep_lock);

		/* purge the request queue.  NB No new replies (rqbds
		 * all unlinked) and no service threads, so I'm the only
		 * thread noodling the request queue now
		 */
		while (!list_empty(&svcpt->scp_req_incoming)) {
			req = list_entry(svcpt->scp_req_incoming.next,
					 struct ptlrpc_request, rq_list);

			list_del(&req->rq_list);
			svcpt->scp_nreqs_incoming--;
			ptlrpc_server_finish_request(svcpt, req);
		}

		while (ptlrpc_server_request_pending(svcpt, true)) {
			req = ptlrpc_server_request_get(svcpt, true);
			ptlrpc_server_finish_active_request(svcpt, req);
		}

		LASSERT(list_empty(&svcpt->scp_rqbd_posted));
		LASSERT(svcpt->scp_nreqs_incoming == 0);
		LASSERT(svcpt->scp_nreqs_active == 0);
		/* history should have been culled by
		 * ptlrpc_server_finish_request
		 */
		LASSERT(svcpt->scp_hist_nrqbds == 0);

		/* Now free all the request buffers since nothing
		 * references them any more...
		 */

		while (!list_empty(&svcpt->scp_rqbd_idle)) {
			rqbd = list_entry(svcpt->scp_rqbd_idle.next,
					  struct ptlrpc_request_buffer_desc,
					  rqbd_list);
			ptlrpc_free_rqbd(rqbd);
		}
		ptlrpc_wait_replies(svcpt);

		while (!list_empty(&svcpt->scp_rep_idle)) {
			rs = list_entry(svcpt->scp_rep_idle.next,
					struct ptlrpc_reply_state,
					rs_list);
			list_del(&rs->rs_list);
			kvfree(rs);
		}
	}
}

static void
ptlrpc_service_free(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part *svcpt;
	struct ptlrpc_at_array *array;
	int i;

	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if (!svcpt->scp_service)
			break;

		/* In case somebody rearmed this in the meantime */
		del_timer(&svcpt->scp_at_timer);
		array = &svcpt->scp_at_array;

		kfree(array->paa_reqs_array);
		array->paa_reqs_array = NULL;
		kfree(array->paa_reqs_count);
		array->paa_reqs_count = NULL;
	}

	ptlrpc_service_for_each_part(svcpt, i, svc)
		kfree(svcpt);

	if (svc->srv_cpts)
		cfs_expr_list_values_free(svc->srv_cpts, svc->srv_ncpts);

	kfree(svc);
}

int ptlrpc_unregister_service(struct ptlrpc_service *service)
{
	CDEBUG(D_NET, "%s: tearing down\n", service->srv_name);

	service->srv_is_stopping = 1;

	mutex_lock(&ptlrpc_all_services_mutex);
	list_del_init(&service->srv_list);
	mutex_unlock(&ptlrpc_all_services_mutex);

	ptlrpc_service_del_atimer(service);
	ptlrpc_stop_all_threads(service);

	ptlrpc_service_unlink_rqbd(service);
	ptlrpc_service_purge_all(service);
	ptlrpc_service_nrs_cleanup(service);

	ptlrpc_lprocfs_unregister_service(service);
	ptlrpc_sysfs_unregister_service(service);

	ptlrpc_service_free(service);

	return 0;
}
EXPORT_SYMBOL(ptlrpc_unregister_service);
