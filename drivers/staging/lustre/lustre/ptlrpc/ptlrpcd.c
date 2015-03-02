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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/ptlrpc/ptlrpcd.c
 */

/** \defgroup ptlrpcd PortalRPC daemon
 *
 * ptlrpcd is a special thread with its own set where other user might add
 * requests when they don't want to wait for their completion.
 * PtlRPCD will take care of sending such requests and then processing their
 * replies and calling completion callbacks as necessary.
 * The callbacks are called directly from ptlrpcd context.
 * It is important to never significantly block (esp. on RPCs!) within such
 * completion handler or a deadlock might occur where ptlrpcd enters some
 * callback that attempts to send another RPC and wait for it to return,
 * during which time ptlrpcd is completely blocked, so e.g. if import
 * fails, recovery cannot progress because connection requests are also
 * sent by ptlrpcd.
 *
 * @{
 */

#define DEBUG_SUBSYSTEM S_RPC

#include "../../include/linux/libcfs/libcfs.h"

#include "../include/lustre_net.h"
#include "../include/lustre_lib.h"
#include "../include/lustre_ha.h"
#include "../include/obd_class.h"	/* for obd_zombie */
#include "../include/obd_support.h"	/* for OBD_FAIL_CHECK */
#include "../include/cl_object.h"	/* cl_env_{get,put}() */
#include "../include/lprocfs_status.h"

#include "ptlrpc_internal.h"

struct ptlrpcd {
	int		pd_size;
	int		pd_index;
	int		pd_nthreads;
	struct ptlrpcd_ctl pd_thread_rcv;
	struct ptlrpcd_ctl pd_threads[0];
};

static int max_ptlrpcds;
module_param(max_ptlrpcds, int, 0644);
MODULE_PARM_DESC(max_ptlrpcds, "Max ptlrpcd thread count to be started.");

static int ptlrpcd_bind_policy = PDB_POLICY_PAIR;
module_param(ptlrpcd_bind_policy, int, 0644);
MODULE_PARM_DESC(ptlrpcd_bind_policy, "Ptlrpcd threads binding mode.");
static struct ptlrpcd *ptlrpcds;

struct mutex ptlrpcd_mutex;
static int ptlrpcd_users;

void ptlrpcd_wake(struct ptlrpc_request *req)
{
	struct ptlrpc_request_set *rq_set = req->rq_set;

	LASSERT(rq_set != NULL);

	wake_up(&rq_set->set_waitq);
}
EXPORT_SYMBOL(ptlrpcd_wake);

static struct ptlrpcd_ctl *
ptlrpcd_select_pc(struct ptlrpc_request *req, pdl_policy_t policy, int index)
{
	int idx = 0;

	if (req != NULL && req->rq_send_state != LUSTRE_IMP_FULL)
		return &ptlrpcds->pd_thread_rcv;

	switch (policy) {
	case PDL_POLICY_SAME:
		idx = smp_processor_id() % ptlrpcds->pd_nthreads;
		break;
	case PDL_POLICY_LOCAL:
		/* Before CPU partition patches available, process it the same
		 * as "PDL_POLICY_ROUND". */
# ifdef CFS_CPU_MODE_NUMA
# warning "fix this code to use new CPU partition APIs"
# endif
		/* Fall through to PDL_POLICY_ROUND until the CPU
		 * CPU partition patches are available. */
		index = -1;
	case PDL_POLICY_PREFERRED:
		if (index >= 0 && index < num_online_cpus()) {
			idx = index % ptlrpcds->pd_nthreads;
			break;
		}
		/* Fall through to PDL_POLICY_ROUND for bad index. */
	default:
		/* Fall through to PDL_POLICY_ROUND for unknown policy. */
	case PDL_POLICY_ROUND:
		/* We do not care whether it is strict load balance. */
		idx = ptlrpcds->pd_index + 1;
		if (idx == smp_processor_id())
			idx++;
		idx %= ptlrpcds->pd_nthreads;
		ptlrpcds->pd_index = idx;
		break;
	}

	return &ptlrpcds->pd_threads[idx];
}

/**
 * Move all request from an existing request set to the ptlrpcd queue.
 * All requests from the set must be in phase RQ_PHASE_NEW.
 */
void ptlrpcd_add_rqset(struct ptlrpc_request_set *set)
{
	struct list_head *tmp, *pos;
	struct ptlrpcd_ctl *pc;
	struct ptlrpc_request_set *new;
	int count, i;

	pc = ptlrpcd_select_pc(NULL, PDL_POLICY_LOCAL, -1);
	new = pc->pc_set;

	list_for_each_safe(pos, tmp, &set->set_requests) {
		struct ptlrpc_request *req =
			list_entry(pos, struct ptlrpc_request,
				       rq_set_chain);

		LASSERT(req->rq_phase == RQ_PHASE_NEW);
		req->rq_set = new;
		req->rq_queued_time = cfs_time_current();
	}

	spin_lock(&new->set_new_req_lock);
	list_splice_init(&set->set_requests, &new->set_new_requests);
	i = atomic_read(&set->set_remaining);
	count = atomic_add_return(i, &new->set_new_count);
	atomic_set(&set->set_remaining, 0);
	spin_unlock(&new->set_new_req_lock);
	if (count == i) {
		wake_up(&new->set_waitq);

		/* XXX: It maybe unnecessary to wakeup all the partners. But to
		 *      guarantee the async RPC can be processed ASAP, we have
		 *      no other better choice. It maybe fixed in future. */
		for (i = 0; i < pc->pc_npartners; i++)
			wake_up(&pc->pc_partners[i]->pc_set->set_waitq);
	}
}
EXPORT_SYMBOL(ptlrpcd_add_rqset);

/**
 * Return transferred RPCs count.
 */
static int ptlrpcd_steal_rqset(struct ptlrpc_request_set *des,
			       struct ptlrpc_request_set *src)
{
	struct list_head *tmp, *pos;
	struct ptlrpc_request *req;
	int rc = 0;

	spin_lock(&src->set_new_req_lock);
	if (likely(!list_empty(&src->set_new_requests))) {
		list_for_each_safe(pos, tmp, &src->set_new_requests) {
			req = list_entry(pos, struct ptlrpc_request,
					     rq_set_chain);
			req->rq_set = des;
		}
		list_splice_init(&src->set_new_requests,
				     &des->set_requests);
		rc = atomic_read(&src->set_new_count);
		atomic_add(rc, &des->set_remaining);
		atomic_set(&src->set_new_count, 0);
	}
	spin_unlock(&src->set_new_req_lock);
	return rc;
}

/**
 * Requests that are added to the ptlrpcd queue are sent via
 * ptlrpcd_check->ptlrpc_check_set().
 */
void ptlrpcd_add_req(struct ptlrpc_request *req, pdl_policy_t policy, int idx)
{
	struct ptlrpcd_ctl *pc;

	if (req->rq_reqmsg)
		lustre_msg_set_jobid(req->rq_reqmsg, NULL);

	spin_lock(&req->rq_lock);
	if (req->rq_invalid_rqset) {
		struct l_wait_info lwi = LWI_TIMEOUT(cfs_time_seconds(5),
						     back_to_sleep, NULL);

		req->rq_invalid_rqset = 0;
		spin_unlock(&req->rq_lock);
		l_wait_event(req->rq_set_waitq, (req->rq_set == NULL), &lwi);
	} else if (req->rq_set) {
		/* If we have a valid "rq_set", just reuse it to avoid double
		 * linked. */
		LASSERT(req->rq_phase == RQ_PHASE_NEW);
		LASSERT(req->rq_send_state == LUSTRE_IMP_REPLAY);

		/* ptlrpc_check_set will decrease the count */
		atomic_inc(&req->rq_set->set_remaining);
		spin_unlock(&req->rq_lock);
		wake_up(&req->rq_set->set_waitq);
		return;
	} else {
		spin_unlock(&req->rq_lock);
	}

	pc = ptlrpcd_select_pc(req, policy, idx);

	DEBUG_REQ(D_INFO, req, "add req [%p] to pc [%s:%d]",
		  req, pc->pc_name, pc->pc_index);

	ptlrpc_set_add_new_req(pc, req);
}
EXPORT_SYMBOL(ptlrpcd_add_req);

static inline void ptlrpc_reqset_get(struct ptlrpc_request_set *set)
{
	atomic_inc(&set->set_refcount);
}

/**
 * Check if there is more work to do on ptlrpcd set.
 * Returns 1 if yes.
 */
static int ptlrpcd_check(struct lu_env *env, struct ptlrpcd_ctl *pc)
{
	struct list_head *tmp, *pos;
	struct ptlrpc_request *req;
	struct ptlrpc_request_set *set = pc->pc_set;
	int rc = 0;
	int rc2;

	if (atomic_read(&set->set_new_count)) {
		spin_lock(&set->set_new_req_lock);
		if (likely(!list_empty(&set->set_new_requests))) {
			list_splice_init(&set->set_new_requests,
					     &set->set_requests);
			atomic_add(atomic_read(&set->set_new_count),
				       &set->set_remaining);
			atomic_set(&set->set_new_count, 0);
			/*
			 * Need to calculate its timeout.
			 */
			rc = 1;
		}
		spin_unlock(&set->set_new_req_lock);
	}

	/* We should call lu_env_refill() before handling new requests to make
	 * sure that env key the requests depending on really exists.
	 */
	rc2 = lu_env_refill(env);
	if (rc2 != 0) {
		/*
		 * XXX This is very awkward situation, because
		 * execution can neither continue (request
		 * interpreters assume that env is set up), nor repeat
		 * the loop (as this potentially results in a tight
		 * loop of -ENOMEM's).
		 *
		 * Fortunately, refill only ever does something when
		 * new modules are loaded, i.e., early during boot up.
		 */
		CERROR("Failure to refill session: %d\n", rc2);
		return rc;
	}

	if (atomic_read(&set->set_remaining))
		rc |= ptlrpc_check_set(env, set);

	/* NB: ptlrpc_check_set has already moved completed request at the
	 * head of seq::set_requests */
	list_for_each_safe(pos, tmp, &set->set_requests) {
		req = list_entry(pos, struct ptlrpc_request, rq_set_chain);
		if (req->rq_phase != RQ_PHASE_COMPLETE)
			break;

		list_del_init(&req->rq_set_chain);
		req->rq_set = NULL;
		ptlrpc_req_finished(req);
	}

	if (rc == 0) {
		/*
		 * If new requests have been added, make sure to wake up.
		 */
		rc = atomic_read(&set->set_new_count);

		/* If we have nothing to do, check whether we can take some
		 * work from our partner threads. */
		if (rc == 0 && pc->pc_npartners > 0) {
			struct ptlrpcd_ctl *partner;
			struct ptlrpc_request_set *ps;
			int first = pc->pc_cursor;

			do {
				partner = pc->pc_partners[pc->pc_cursor++];
				if (pc->pc_cursor >= pc->pc_npartners)
					pc->pc_cursor = 0;
				if (partner == NULL)
					continue;

				spin_lock(&partner->pc_lock);
				ps = partner->pc_set;
				if (ps == NULL) {
					spin_unlock(&partner->pc_lock);
					continue;
				}

				ptlrpc_reqset_get(ps);
				spin_unlock(&partner->pc_lock);

				if (atomic_read(&ps->set_new_count)) {
					rc = ptlrpcd_steal_rqset(set, ps);
					if (rc > 0)
						CDEBUG(D_RPCTRACE, "transfer %d async RPCs [%d->%d]\n",
						       rc, partner->pc_index,
						       pc->pc_index);
				}
				ptlrpc_reqset_put(ps);
			} while (rc == 0 && pc->pc_cursor != first);
		}
	}

	return rc;
}

/**
 * Main ptlrpcd thread.
 * ptlrpc's code paths like to execute in process context, so we have this
 * thread which spins on a set which contains the rpcs and sends them.
 *
 */
static int ptlrpcd(void *arg)
{
	struct ptlrpcd_ctl *pc = arg;
	struct ptlrpc_request_set *set = pc->pc_set;
	struct lu_env env = { .le_ses = NULL };
	int rc, exit = 0;

	unshare_fs_struct();
#if defined(CONFIG_SMP)
	if (test_bit(LIOD_BIND, &pc->pc_flags)) {
		int index = pc->pc_index;

		if (index >= 0 && index < num_possible_cpus()) {
			while (!cpu_online(index)) {
				if (++index >= num_possible_cpus())
					index = 0;
			}
			set_cpus_allowed_ptr(current,
					cpumask_of_node(cpu_to_node(index)));
		}
	}
#endif
	/*
	 * XXX So far only "client" ptlrpcd uses an environment. In
	 * the future, ptlrpcd thread (or a thread-set) has to given
	 * an argument, describing its "scope".
	 */
	rc = lu_context_init(&env.le_ctx,
			     LCT_CL_THREAD|LCT_REMEMBER|LCT_NOREF);
	complete(&pc->pc_starting);

	if (rc != 0)
		return rc;

	/*
	 * This mainloop strongly resembles ptlrpc_set_wait() except that our
	 * set never completes.  ptlrpcd_check() calls ptlrpc_check_set() when
	 * there are requests in the set. New requests come in on the set's
	 * new_req_list and ptlrpcd_check() moves them into the set.
	 */
	do {
		struct l_wait_info lwi;
		int timeout;

		timeout = ptlrpc_set_next_timeout(set);
		lwi = LWI_TIMEOUT(cfs_time_seconds(timeout ? timeout : 1),
				  ptlrpc_expired_set, set);

		lu_context_enter(&env.le_ctx);
		l_wait_event(set->set_waitq,
			     ptlrpcd_check(&env, pc), &lwi);
		lu_context_exit(&env.le_ctx);

		/*
		 * Abort inflight rpcs for forced stop case.
		 */
		if (test_bit(LIOD_STOP, &pc->pc_flags)) {
			if (test_bit(LIOD_FORCE, &pc->pc_flags))
				ptlrpc_abort_set(set);
			exit++;
		}

		/*
		 * Let's make one more loop to make sure that ptlrpcd_check()
		 * copied all raced new rpcs into the set so we can kill them.
		 */
	} while (exit < 2);

	/*
	 * Wait for inflight requests to drain.
	 */
	if (!list_empty(&set->set_requests))
		ptlrpc_set_wait(set);
	lu_context_fini(&env.le_ctx);

	complete(&pc->pc_finishing);

	return 0;
}

/* XXX: We want multiple CPU cores to share the async RPC load. So we start many
 *      ptlrpcd threads. We also want to reduce the ptlrpcd overhead caused by
 *      data transfer cross-CPU cores. So we bind ptlrpcd thread to specified
 *      CPU core. But binding all ptlrpcd threads maybe cause response delay
 *      because of some CPU core(s) busy with other loads.
 *
 *      For example: "ls -l", some async RPCs for statahead are assigned to
 *      ptlrpcd_0, and ptlrpcd_0 is bound to CPU_0, but CPU_0 may be quite busy
 *      with other non-ptlrpcd, like "ls -l" itself (we want to the "ls -l"
 *      thread, statahead thread, and ptlrpcd thread can run in parallel), under
 *      such case, the statahead async RPCs can not be processed in time, it is
 *      unexpected. If ptlrpcd_0 can be re-scheduled on other CPU core, it may
 *      be better. But it breaks former data transfer policy.
 *
 *      So we shouldn't be blind for avoiding the data transfer. We make some
 *      compromise: divide the ptlrpcd threads pool into two parts. One part is
 *      for bound mode, each ptlrpcd thread in this part is bound to some CPU
 *      core. The other part is for free mode, all the ptlrpcd threads in the
 *      part can be scheduled on any CPU core. We specify some partnership
 *      between bound mode ptlrpcd thread(s) and free mode ptlrpcd thread(s),
 *      and the async RPC load within the partners are shared.
 *
 *      It can partly avoid data transfer cross-CPU (if the bound mode ptlrpcd
 *      thread can be scheduled in time), and try to guarantee the async RPC
 *      processed ASAP (as long as the free mode ptlrpcd thread can be scheduled
 *      on any CPU core).
 *
 *      As for how to specify the partnership between bound mode ptlrpcd
 *      thread(s) and free mode ptlrpcd thread(s), the simplest way is to use
 *      <free bound> pair. In future, we can specify some more complex
 *      partnership based on the patches for CPU partition. But before such
 *      patches are available, we prefer to use the simplest one.
 */
# ifdef CFS_CPU_MODE_NUMA
# warning "fix ptlrpcd_bind() to use new CPU partition APIs"
# endif
static int ptlrpcd_bind(int index, int max)
{
	struct ptlrpcd_ctl *pc;
	int rc = 0;
#if defined(CONFIG_NUMA)
	cpumask_t mask;
#endif

	LASSERT(index <= max - 1);
	pc = &ptlrpcds->pd_threads[index];
	switch (ptlrpcd_bind_policy) {
	case PDB_POLICY_NONE:
		pc->pc_npartners = -1;
		break;
	case PDB_POLICY_FULL:
		pc->pc_npartners = 0;
		set_bit(LIOD_BIND, &pc->pc_flags);
		break;
	case PDB_POLICY_PAIR:
		LASSERT(max % 2 == 0);
		pc->pc_npartners = 1;
		break;
	case PDB_POLICY_NEIGHBOR:
#if defined(CONFIG_NUMA)
	{
		int i;
		cpumask_copy(&mask, cpumask_of_node(cpu_to_node(index)));
		for (i = max; i < num_online_cpus(); i++)
			cpumask_clear_cpu(i, &mask);
		pc->pc_npartners = cpumask_weight(&mask) - 1;
		set_bit(LIOD_BIND, &pc->pc_flags);
	}
#else
		LASSERT(max >= 3);
		pc->pc_npartners = 2;
#endif
		break;
	default:
		CERROR("unknown ptlrpcd bind policy %d\n", ptlrpcd_bind_policy);
		rc = -EINVAL;
	}

	if (rc == 0 && pc->pc_npartners > 0) {
		OBD_ALLOC(pc->pc_partners,
			  sizeof(struct ptlrpcd_ctl *) * pc->pc_npartners);
		if (pc->pc_partners == NULL) {
			pc->pc_npartners = 0;
			rc = -ENOMEM;
		} else {
			switch (ptlrpcd_bind_policy) {
			case PDB_POLICY_PAIR:
				if (index & 0x1) {
					set_bit(LIOD_BIND, &pc->pc_flags);
					pc->pc_partners[0] = &ptlrpcds->
						pd_threads[index - 1];
					ptlrpcds->pd_threads[index - 1].
						pc_partners[0] = pc;
				}
				break;
			case PDB_POLICY_NEIGHBOR:
#if defined(CONFIG_NUMA)
			{
				struct ptlrpcd_ctl *ppc;
				int i, pidx;
				/* partners are cores in the same NUMA node.
				 * setup partnership only with ptlrpcd threads
				 * that are already initialized
				 */
				for (pidx = 0, i = 0; i < index; i++) {
					if (cpumask_test_cpu(i, &mask)) {
						ppc = &ptlrpcds->pd_threads[i];
						pc->pc_partners[pidx++] = ppc;
						ppc->pc_partners[ppc->
							  pc_npartners++] = pc;
					}
				}
				/* adjust number of partners to the number
				 * of partnership really setup */
				pc->pc_npartners = pidx;
			}
#else
				if (index & 0x1)
					set_bit(LIOD_BIND, &pc->pc_flags);
				if (index > 0) {
					pc->pc_partners[0] = &ptlrpcds->
						pd_threads[index - 1];
					ptlrpcds->pd_threads[index - 1].
						pc_partners[1] = pc;
					if (index == max - 1) {
						pc->pc_partners[1] =
						&ptlrpcds->pd_threads[0];
						ptlrpcds->pd_threads[0].
						pc_partners[0] = pc;
					}
				}
#endif
				break;
			}
		}
	}

	return rc;
}


int ptlrpcd_start(int index, int max, const char *name, struct ptlrpcd_ctl *pc)
{
	int rc;

	/*
	 * Do not allow start second thread for one pc.
	 */
	if (test_and_set_bit(LIOD_START, &pc->pc_flags)) {
		CWARN("Starting second thread (%s) for same pc %p\n",
		      name, pc);
		return 0;
	}

	pc->pc_index = index;
	init_completion(&pc->pc_starting);
	init_completion(&pc->pc_finishing);
	spin_lock_init(&pc->pc_lock);
	strlcpy(pc->pc_name, name, sizeof(pc->pc_name));
	pc->pc_set = ptlrpc_prep_set();
	if (pc->pc_set == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * So far only "client" ptlrpcd uses an environment. In the future,
	 * ptlrpcd thread (or a thread-set) has to be given an argument,
	 * describing its "scope".
	 */
	rc = lu_context_init(&pc->pc_env.le_ctx, LCT_CL_THREAD|LCT_REMEMBER);
	if (rc != 0)
		goto out_set;

	{
		struct task_struct *task;
		if (index >= 0) {
			rc = ptlrpcd_bind(index, max);
			if (rc < 0)
				goto out_env;
		}

		task = kthread_run(ptlrpcd, pc, "%s", pc->pc_name);
		if (IS_ERR(task)) {
			rc = PTR_ERR(task);
			goto out_env;
		}

		wait_for_completion(&pc->pc_starting);
	}
	return 0;

out_env:
	lu_context_fini(&pc->pc_env.le_ctx);

out_set:
	if (pc->pc_set != NULL) {
		struct ptlrpc_request_set *set = pc->pc_set;

		spin_lock(&pc->pc_lock);
		pc->pc_set = NULL;
		spin_unlock(&pc->pc_lock);
		ptlrpc_set_destroy(set);
	}
	clear_bit(LIOD_BIND, &pc->pc_flags);

out:
	clear_bit(LIOD_START, &pc->pc_flags);
	return rc;
}

void ptlrpcd_stop(struct ptlrpcd_ctl *pc, int force)
{
	if (!test_bit(LIOD_START, &pc->pc_flags)) {
		CWARN("Thread for pc %p was not started\n", pc);
		return;
	}

	set_bit(LIOD_STOP, &pc->pc_flags);
	if (force)
		set_bit(LIOD_FORCE, &pc->pc_flags);
	wake_up(&pc->pc_set->set_waitq);
}

void ptlrpcd_free(struct ptlrpcd_ctl *pc)
{
	struct ptlrpc_request_set *set = pc->pc_set;

	if (!test_bit(LIOD_START, &pc->pc_flags)) {
		CWARN("Thread for pc %p was not started\n", pc);
		goto out;
	}

	wait_for_completion(&pc->pc_finishing);
	lu_context_fini(&pc->pc_env.le_ctx);

	spin_lock(&pc->pc_lock);
	pc->pc_set = NULL;
	spin_unlock(&pc->pc_lock);
	ptlrpc_set_destroy(set);

	clear_bit(LIOD_START, &pc->pc_flags);
	clear_bit(LIOD_STOP, &pc->pc_flags);
	clear_bit(LIOD_FORCE, &pc->pc_flags);
	clear_bit(LIOD_BIND, &pc->pc_flags);

out:
	if (pc->pc_npartners > 0) {
		LASSERT(pc->pc_partners != NULL);

		OBD_FREE(pc->pc_partners,
			 sizeof(struct ptlrpcd_ctl *) * pc->pc_npartners);
		pc->pc_partners = NULL;
	}
	pc->pc_npartners = 0;
}

static void ptlrpcd_fini(void)
{
	int i;

	if (ptlrpcds != NULL) {
		for (i = 0; i < ptlrpcds->pd_nthreads; i++)
			ptlrpcd_stop(&ptlrpcds->pd_threads[i], 0);
		for (i = 0; i < ptlrpcds->pd_nthreads; i++)
			ptlrpcd_free(&ptlrpcds->pd_threads[i]);
		ptlrpcd_stop(&ptlrpcds->pd_thread_rcv, 0);
		ptlrpcd_free(&ptlrpcds->pd_thread_rcv);
		OBD_FREE(ptlrpcds, ptlrpcds->pd_size);
		ptlrpcds = NULL;
	}
}

static int ptlrpcd_init(void)
{
	int nthreads = num_online_cpus();
	char name[16];
	int size, i = -1, j, rc = 0;

	if (max_ptlrpcds > 0 && max_ptlrpcds < nthreads)
		nthreads = max_ptlrpcds;
	if (nthreads < 2)
		nthreads = 2;
	if (nthreads < 3 && ptlrpcd_bind_policy == PDB_POLICY_NEIGHBOR)
		ptlrpcd_bind_policy = PDB_POLICY_PAIR;
	else if (nthreads % 2 != 0 && ptlrpcd_bind_policy == PDB_POLICY_PAIR)
		nthreads &= ~1; /* make sure it is even */

	size = offsetof(struct ptlrpcd, pd_threads[nthreads]);
	OBD_ALLOC(ptlrpcds, size);
	if (ptlrpcds == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	snprintf(name, sizeof(name), "ptlrpcd_rcv");
	set_bit(LIOD_RECOVERY, &ptlrpcds->pd_thread_rcv.pc_flags);
	rc = ptlrpcd_start(-1, nthreads, name, &ptlrpcds->pd_thread_rcv);
	if (rc < 0)
		goto out;

	/* XXX: We start nthreads ptlrpc daemons. Each of them can process any
	 *      non-recovery async RPC to improve overall async RPC efficiency.
	 *
	 *      But there are some issues with async I/O RPCs and async non-I/O
	 *      RPCs processed in the same set under some cases. The ptlrpcd may
	 *      be blocked by some async I/O RPC(s), then will cause other async
	 *      non-I/O RPC(s) can not be processed in time.
	 *
	 *      Maybe we should distinguish blocked async RPCs from non-blocked
	 *      async RPCs, and process them in different ptlrpcd sets to avoid
	 *      unnecessary dependency. But how to distribute async RPCs load
	 *      among all the ptlrpc daemons becomes another trouble. */
	for (i = 0; i < nthreads; i++) {
		snprintf(name, sizeof(name), "ptlrpcd_%d", i);
		rc = ptlrpcd_start(i, nthreads, name, &ptlrpcds->pd_threads[i]);
		if (rc < 0)
			goto out;
	}

	ptlrpcds->pd_size = size;
	ptlrpcds->pd_index = 0;
	ptlrpcds->pd_nthreads = nthreads;

out:
	if (rc != 0 && ptlrpcds != NULL) {
		for (j = 0; j <= i; j++)
			ptlrpcd_stop(&ptlrpcds->pd_threads[j], 0);
		for (j = 0; j <= i; j++)
			ptlrpcd_free(&ptlrpcds->pd_threads[j]);
		ptlrpcd_stop(&ptlrpcds->pd_thread_rcv, 0);
		ptlrpcd_free(&ptlrpcds->pd_thread_rcv);
		OBD_FREE(ptlrpcds, size);
		ptlrpcds = NULL;
	}

	return 0;
}

int ptlrpcd_addref(void)
{
	int rc = 0;

	mutex_lock(&ptlrpcd_mutex);
	if (++ptlrpcd_users == 1)
		rc = ptlrpcd_init();
	mutex_unlock(&ptlrpcd_mutex);
	return rc;
}
EXPORT_SYMBOL(ptlrpcd_addref);

void ptlrpcd_decref(void)
{
	mutex_lock(&ptlrpcd_mutex);
	if (--ptlrpcd_users == 0)
		ptlrpcd_fini();
	mutex_unlock(&ptlrpcd_mutex);
}
EXPORT_SYMBOL(ptlrpcd_decref);
/** @} ptlrpcd */
