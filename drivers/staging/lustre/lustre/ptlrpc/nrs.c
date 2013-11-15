/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.  A copy is
 * included in the COPYING file that accompanied this code.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2011 Intel Corporation
 *
 * Copyright 2012 Xyratex Technology Limited
 */
/*
 * lustre/ptlrpc/nrs.c
 *
 * Network Request Scheduler (NRS)
 *
 * Allows to reorder the handling of RPCs at servers.
 *
 * Author: Liang Zhen <liang@whamcloud.com>
 * Author: Nikitas Angelinas <nikitas_angelinas@xyratex.com>
 */
/**
 * \addtogoup nrs
 * @{
 */

#define DEBUG_SUBSYSTEM S_RPC
#include <obd_support.h>
#include <obd_class.h>
#include <lustre_net.h>
#include <lprocfs_status.h>
#include <linux/libcfs/libcfs.h>
#include "ptlrpc_internal.h"

/* XXX: This is just for liblustre. Remove the #if defined directive when the
 * "cfs_" prefix is dropped from cfs_list_head. */
extern struct list_head ptlrpc_all_services;

/**
 * NRS core object.
 */
struct nrs_core nrs_core;

static int nrs_policy_init(struct ptlrpc_nrs_policy *policy)
{
	return policy->pol_desc->pd_ops->op_policy_init != NULL ?
	       policy->pol_desc->pd_ops->op_policy_init(policy) : 0;
}

static void nrs_policy_fini(struct ptlrpc_nrs_policy *policy)
{
	LASSERT(policy->pol_ref == 0);
	LASSERT(policy->pol_req_queued == 0);

	if (policy->pol_desc->pd_ops->op_policy_fini != NULL)
		policy->pol_desc->pd_ops->op_policy_fini(policy);
}

static int nrs_policy_ctl_locked(struct ptlrpc_nrs_policy *policy,
				 enum ptlrpc_nrs_ctl opc, void *arg)
{
	/**
	 * The policy may be stopped, but the lprocfs files and
	 * ptlrpc_nrs_policy instances remain present until unregistration time.
	 * Do not perform the ctl operation if the policy is stopped, as
	 * policy->pol_private will be NULL in such a case.
	 */
	if (policy->pol_state == NRS_POL_STATE_STOPPED)
		return -ENODEV;

	return policy->pol_desc->pd_ops->op_policy_ctl != NULL ?
	       policy->pol_desc->pd_ops->op_policy_ctl(policy, opc, arg) :
	       -ENOSYS;
}

static void nrs_policy_stop0(struct ptlrpc_nrs_policy *policy)
{
	struct ptlrpc_nrs *nrs = policy->pol_nrs;

	if (policy->pol_desc->pd_ops->op_policy_stop != NULL) {
		spin_unlock(&nrs->nrs_lock);

		policy->pol_desc->pd_ops->op_policy_stop(policy);

		spin_lock(&nrs->nrs_lock);
	}

	LASSERT(list_empty(&policy->pol_list_queued));
	LASSERT(policy->pol_req_queued == 0 &&
		policy->pol_req_started == 0);

	policy->pol_private = NULL;

	policy->pol_state = NRS_POL_STATE_STOPPED;

	if (atomic_dec_and_test(&policy->pol_desc->pd_refs))
		module_put(policy->pol_desc->pd_owner);
}

static int nrs_policy_stop_locked(struct ptlrpc_nrs_policy *policy)
{
	struct ptlrpc_nrs *nrs = policy->pol_nrs;

	if (nrs->nrs_policy_fallback == policy && !nrs->nrs_stopping)
		return -EPERM;

	if (policy->pol_state == NRS_POL_STATE_STARTING)
		return -EAGAIN;

	/* In progress or already stopped */
	if (policy->pol_state != NRS_POL_STATE_STARTED)
		return 0;

	policy->pol_state = NRS_POL_STATE_STOPPING;

	/* Immediately make it invisible */
	if (nrs->nrs_policy_primary == policy) {
		nrs->nrs_policy_primary = NULL;

	} else {
		LASSERT(nrs->nrs_policy_fallback == policy);
		nrs->nrs_policy_fallback = NULL;
	}

	/* I have the only refcount */
	if (policy->pol_ref == 1)
		nrs_policy_stop0(policy);

	return 0;
}

/**
 * Transitions the \a nrs NRS head's primary policy to
 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STOPPING and if the policy has no
 * pending usage references, to ptlrpc_nrs_pol_state::NRS_POL_STATE_STOPPED.
 *
 * \param[in] nrs the NRS head to carry out this operation on
 */
static void nrs_policy_stop_primary(struct ptlrpc_nrs *nrs)
{
	struct ptlrpc_nrs_policy *tmp = nrs->nrs_policy_primary;

	if (tmp == NULL) {
		return;
	}

	nrs->nrs_policy_primary = NULL;

	LASSERT(tmp->pol_state == NRS_POL_STATE_STARTED);
	tmp->pol_state = NRS_POL_STATE_STOPPING;

	if (tmp->pol_ref == 0)
		nrs_policy_stop0(tmp);
}

/**
 * Transitions a policy across the ptlrpc_nrs_pol_state range of values, in
 * response to an lprocfs command to start a policy.
 *
 * If a primary policy different to the current one is specified, this function
 * will transition the new policy to the
 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STARTING and then to
 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STARTED, and will then transition
 * the old primary policy (if there is one) to
 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STOPPING, and if there are no outstanding
 * references on the policy to ptlrpc_nrs_pol_stae::NRS_POL_STATE_STOPPED.
 *
 * If the fallback policy is specified, this is taken to indicate an instruction
 * to stop the current primary policy, without substituting it with another
 * primary policy, so the primary policy (if any) is transitioned to
 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STOPPING, and if there are no outstanding
 * references on the policy to ptlrpc_nrs_pol_stae::NRS_POL_STATE_STOPPED. In
 * this case, the fallback policy is only left active in the NRS head.
 */
static int nrs_policy_start_locked(struct ptlrpc_nrs_policy *policy)
{
	struct ptlrpc_nrs      *nrs = policy->pol_nrs;
	int			rc = 0;

	/**
	 * Don't allow multiple starting which is too complex, and has no real
	 * benefit.
	 */
	if (nrs->nrs_policy_starting)
		return -EAGAIN;

	LASSERT(policy->pol_state != NRS_POL_STATE_STARTING);

	if (policy->pol_state == NRS_POL_STATE_STOPPING)
		return -EAGAIN;

	if (policy->pol_flags & PTLRPC_NRS_FL_FALLBACK) {
		/**
		 * This is for cases in which the user sets the policy to the
		 * fallback policy (currently fifo for all services); i.e. the
		 * user is resetting the policy to the default; so we stop the
		 * primary policy, if any.
		 */
		if (policy == nrs->nrs_policy_fallback) {
			nrs_policy_stop_primary(nrs);
			return 0;
		}

		/**
		 * If we reach here, we must be setting up the fallback policy
		 * at service startup time, and only a single policy with the
		 * nrs_policy_flags::PTLRPC_NRS_FL_FALLBACK flag set can
		 * register with NRS core.
		 */
		LASSERT(nrs->nrs_policy_fallback == NULL);
	} else {
		/**
		 * Shouldn't start primary policy if w/o fallback policy.
		 */
		if (nrs->nrs_policy_fallback == NULL)
			return -EPERM;

		if (policy->pol_state == NRS_POL_STATE_STARTED)
			return 0;
	}

	/**
	 * Increase the module usage count for policies registering from other
	 * modules.
	 */
	if (atomic_inc_return(&policy->pol_desc->pd_refs) == 1 &&
	    !try_module_get(policy->pol_desc->pd_owner)) {
		atomic_dec(&policy->pol_desc->pd_refs);
		CERROR("NRS: cannot get module for policy %s; is it alive?\n",
		       policy->pol_desc->pd_name);
		return -ENODEV;
	}

	/**
	 * Serialize policy starting across the NRS head
	 */
	nrs->nrs_policy_starting = 1;

	policy->pol_state = NRS_POL_STATE_STARTING;

	if (policy->pol_desc->pd_ops->op_policy_start) {
		spin_unlock(&nrs->nrs_lock);

		rc = policy->pol_desc->pd_ops->op_policy_start(policy);

		spin_lock(&nrs->nrs_lock);
		if (rc != 0) {
			if (atomic_dec_and_test(&policy->pol_desc->pd_refs))
				module_put(policy->pol_desc->pd_owner);

			policy->pol_state = NRS_POL_STATE_STOPPED;
			GOTO(out, rc);
		}
	}

	policy->pol_state = NRS_POL_STATE_STARTED;

	if (policy->pol_flags & PTLRPC_NRS_FL_FALLBACK) {
		/**
		 * This path is only used at PTLRPC service setup time.
		 */
		nrs->nrs_policy_fallback = policy;
	} else {
		/*
		 * Try to stop the current primary policy if there is one.
		 */
		nrs_policy_stop_primary(nrs);

		/**
		 * And set the newly-started policy as the primary one.
		 */
		nrs->nrs_policy_primary = policy;
	}

out:
	nrs->nrs_policy_starting = 0;

	return rc;
}

/**
 * Increases the policy's usage reference count.
 */
static inline void nrs_policy_get_locked(struct ptlrpc_nrs_policy *policy)
{
	policy->pol_ref++;
}

/**
 * Decreases the policy's usage reference count, and stops the policy in case it
 * was already stopping and have no more outstanding usage references (which
 * indicates it has no more queued or started requests, and can be safely
 * stopped).
 */
static void nrs_policy_put_locked(struct ptlrpc_nrs_policy *policy)
{
	LASSERT(policy->pol_ref > 0);

	policy->pol_ref--;
	if (unlikely(policy->pol_ref == 0 &&
	    policy->pol_state == NRS_POL_STATE_STOPPING))
		nrs_policy_stop0(policy);
}

static void nrs_policy_put(struct ptlrpc_nrs_policy *policy)
{
	spin_lock(&policy->pol_nrs->nrs_lock);
	nrs_policy_put_locked(policy);
	spin_unlock(&policy->pol_nrs->nrs_lock);
}

/**
 * Find and return a policy by name.
 */
static struct ptlrpc_nrs_policy * nrs_policy_find_locked(struct ptlrpc_nrs *nrs,
							 char *name)
{
	struct ptlrpc_nrs_policy *tmp;

	list_for_each_entry(tmp, &nrs->nrs_policy_list, pol_list) {
		if (strncmp(tmp->pol_desc->pd_name, name,
			    NRS_POL_NAME_MAX) == 0) {
			nrs_policy_get_locked(tmp);
			return tmp;
		}
	}
	return NULL;
}

/**
 * Release references for the resource hierarchy moving upwards towards the
 * policy instance resource.
 */
static void nrs_resource_put(struct ptlrpc_nrs_resource *res)
{
	struct ptlrpc_nrs_policy *policy = res->res_policy;

	if (policy->pol_desc->pd_ops->op_res_put != NULL) {
		struct ptlrpc_nrs_resource *parent;

		for (; res != NULL; res = parent) {
			parent = res->res_parent;
			policy->pol_desc->pd_ops->op_res_put(policy, res);
		}
	}
}

/**
 * Obtains references for each resource in the resource hierarchy for request
 * \a nrq if it is to be handled by \a policy.
 *
 * \param[in] policy	  the policy
 * \param[in] nrq	  the request
 * \param[in] moving_req  denotes whether this is a call to the function by
 *			  ldlm_lock_reorder_req(), in order to move \a nrq to
 *			  the high-priority NRS head; we should not sleep when
 *			  set.
 *
 * \retval NULL		  resource hierarchy references not obtained
 * \retval valid-pointer  the bottom level of the resource hierarchy
 *
 * \see ptlrpc_nrs_pol_ops::op_res_get()
 */
static
struct ptlrpc_nrs_resource * nrs_resource_get(struct ptlrpc_nrs_policy *policy,
					      struct ptlrpc_nrs_request *nrq,
					      bool moving_req)
{
	/**
	 * Set to NULL to traverse the resource hierarchy from the top.
	 */
	struct ptlrpc_nrs_resource *res = NULL;
	struct ptlrpc_nrs_resource *tmp = NULL;
	int			    rc;

	while (1) {
		rc = policy->pol_desc->pd_ops->op_res_get(policy, nrq, res,
							  &tmp, moving_req);
		if (rc < 0) {
			if (res != NULL)
				nrs_resource_put(res);
			return NULL;
		}

		LASSERT(tmp != NULL);
		tmp->res_parent = res;
		tmp->res_policy = policy;
		res = tmp;
		tmp = NULL;
		/**
		 * Return once we have obtained a reference to the bottom level
		 * of the resource hierarchy.
		 */
		if (rc > 0)
			return res;
	}
}

/**
 * Obtains resources for the resource hierarchies and policy references for
 * the fallback and current primary policy (if any), that will later be used
 * to handle request \a nrq.
 *
 * \param[in]  nrs  the NRS head instance that will be handling request \a nrq.
 * \param[in]  nrq  the request that is being handled.
 * \param[out] resp the array where references to the resource hierarchy are
 *		    stored.
 * \param[in]  moving_req  is set when obtaining resources while moving a
 *			   request from a policy on the regular NRS head to a
 *			   policy on the HP NRS head (via
 *			   ldlm_lock_reorder_req()). It signifies that
 *			   allocations to get resources should be atomic; for
 *			   a full explanation, see comment in
 *			   ptlrpc_nrs_pol_ops::op_res_get().
 */
static void nrs_resource_get_safe(struct ptlrpc_nrs *nrs,
				  struct ptlrpc_nrs_request *nrq,
				  struct ptlrpc_nrs_resource **resp,
				  bool moving_req)
{
	struct ptlrpc_nrs_policy   *primary = NULL;
	struct ptlrpc_nrs_policy   *fallback = NULL;

	memset(resp, 0, sizeof(resp[0]) * NRS_RES_MAX);

	/**
	 * Obtain policy references.
	 */
	spin_lock(&nrs->nrs_lock);

	fallback = nrs->nrs_policy_fallback;
	nrs_policy_get_locked(fallback);

	primary = nrs->nrs_policy_primary;
	if (primary != NULL)
		nrs_policy_get_locked(primary);

	spin_unlock(&nrs->nrs_lock);

	/**
	 * Obtain resource hierarchy references.
	 */
	resp[NRS_RES_FALLBACK] = nrs_resource_get(fallback, nrq, moving_req);
	LASSERT(resp[NRS_RES_FALLBACK] != NULL);

	if (primary != NULL) {
		resp[NRS_RES_PRIMARY] = nrs_resource_get(primary, nrq,
							 moving_req);
		/**
		 * A primary policy may exist which may not wish to serve a
		 * particular request for different reasons; release the
		 * reference on the policy as it will not be used for this
		 * request.
		 */
		if (resp[NRS_RES_PRIMARY] == NULL)
			nrs_policy_put(primary);
	}
}

/**
 * Releases references to resource hierarchies and policies, because they are no
 * longer required; used when request handling has been completed, or the
 * request is moving to the high priority NRS head.
 *
 * \param resp	the resource hierarchy that is being released
 *
 * \see ptlrpcnrs_req_hp_move()
 * \see ptlrpc_nrs_req_finalize()
 */
static void nrs_resource_put_safe(struct ptlrpc_nrs_resource **resp)
{
	struct ptlrpc_nrs_policy *pols[NRS_RES_MAX];
	struct ptlrpc_nrs	 *nrs = NULL;
	int			  i;

	for (i = 0; i < NRS_RES_MAX; i++) {
		if (resp[i] != NULL) {
			pols[i] = resp[i]->res_policy;
			nrs_resource_put(resp[i]);
			resp[i] = NULL;
		} else {
			pols[i] = NULL;
		}
	}

	for (i = 0; i < NRS_RES_MAX; i++) {
		if (pols[i] == NULL)
			continue;

		if (nrs == NULL) {
			nrs = pols[i]->pol_nrs;
			spin_lock(&nrs->nrs_lock);
		}
		nrs_policy_put_locked(pols[i]);
	}

	if (nrs != NULL)
		spin_unlock(&nrs->nrs_lock);
}

/**
 * Obtains an NRS request from \a policy for handling or examination; the
 * request should be removed in the 'handling' case.
 *
 * Calling into this function implies we already know the policy has a request
 * waiting to be handled.
 *
 * \param[in] policy the policy from which a request
 * \param[in] peek   when set, signifies that we just want to examine the
 *		     request, and not handle it, so the request is not removed
 *		     from the policy.
 * \param[in] force  when set, it will force a policy to return a request if it
 *		     has one pending
 *
 * \retval the NRS request to be handled
 */
static inline
struct ptlrpc_nrs_request * nrs_request_get(struct ptlrpc_nrs_policy *policy,
					    bool peek, bool force)
{
	struct ptlrpc_nrs_request *nrq;

	LASSERT(policy->pol_req_queued > 0);

	nrq = policy->pol_desc->pd_ops->op_req_get(policy, peek, force);

	LASSERT(ergo(nrq != NULL, nrs_request_policy(nrq) == policy));

	return nrq;
}

/**
 * Enqueues request \a nrq for later handling, via one one the policies for
 * which resources where earlier obtained via nrs_resource_get_safe(). The
 * function attempts to enqueue the request first on the primary policy
 * (if any), since this is the preferred choice.
 *
 * \param nrq the request being enqueued
 *
 * \see nrs_resource_get_safe()
 */
static inline void nrs_request_enqueue(struct ptlrpc_nrs_request *nrq)
{
	struct ptlrpc_nrs_policy *policy;
	int			  rc;
	int			  i;

	/**
	 * Try in descending order, because the primary policy (if any) is
	 * the preferred choice.
	 */
	for (i = NRS_RES_MAX - 1; i >= 0; i--) {
		if (nrq->nr_res_ptrs[i] == NULL)
			continue;

		nrq->nr_res_idx = i;
		policy = nrq->nr_res_ptrs[i]->res_policy;

		rc = policy->pol_desc->pd_ops->op_req_enqueue(policy, nrq);
		if (rc == 0) {
			policy->pol_nrs->nrs_req_queued++;
			policy->pol_req_queued++;
			return;
		}
	}
	/**
	 * Should never get here, as at least the primary policy's
	 * ptlrpc_nrs_pol_ops::op_req_enqueue() implementation should always
	 * succeed.
	 */
	LBUG();
}

/**
 * Called when a request has been handled
 *
 * \param[in] nrs the request that has been handled; can be used for
 *		  job/resource control.
 *
 * \see ptlrpc_nrs_req_stop_nolock()
 */
static inline void nrs_request_stop(struct ptlrpc_nrs_request *nrq)
{
	struct ptlrpc_nrs_policy *policy = nrs_request_policy(nrq);

	if (policy->pol_desc->pd_ops->op_req_stop)
		policy->pol_desc->pd_ops->op_req_stop(policy, nrq);

	LASSERT(policy->pol_nrs->nrs_req_started > 0);
	LASSERT(policy->pol_req_started > 0);

	policy->pol_nrs->nrs_req_started--;
	policy->pol_req_started--;
}

/**
 * Handler for operations that can be carried out on policies.
 *
 * Handles opcodes that are common to all policy types within NRS core, and
 * passes any unknown opcodes to the policy-specific control function.
 *
 * \param[in]	  nrs  the NRS head this policy belongs to.
 * \param[in]	  name the human-readable policy name; should be the same as
 *		       ptlrpc_nrs_pol_desc::pd_name.
 * \param[in]	  opc  the opcode of the operation being carried out.
 * \param[in,out] arg  can be used to pass information in and out between when
 *		       carrying an operation; usually data that is private to
 *		       the policy at some level, or generic policy status
 *		       information.
 *
 * \retval -ve error condition
 * \retval   0 operation was carried out successfully
 */
static int nrs_policy_ctl(struct ptlrpc_nrs *nrs, char *name,
			  enum ptlrpc_nrs_ctl opc, void *arg)
{
	struct ptlrpc_nrs_policy       *policy;
	int				rc = 0;

	spin_lock(&nrs->nrs_lock);

	policy = nrs_policy_find_locked(nrs, name);
	if (policy == NULL)
		GOTO(out, rc = -ENOENT);

	switch (opc) {
		/**
		 * Unknown opcode, pass it down to the policy-specific control
		 * function for handling.
		 */
	default:
		rc = nrs_policy_ctl_locked(policy, opc, arg);
		break;

		/**
		 * Start \e policy
		 */
	case PTLRPC_NRS_CTL_START:
		rc = nrs_policy_start_locked(policy);
		break;
	}
out:
	if (policy != NULL)
		nrs_policy_put_locked(policy);

	spin_unlock(&nrs->nrs_lock);

	return rc;
}

/**
 * Unregisters a policy by name.
 *
 * \param[in] nrs  the NRS head this policy belongs to.
 * \param[in] name the human-readable policy name; should be the same as
 *		   ptlrpc_nrs_pol_desc::pd_name
 *
 * \retval -ve error
 * \retval   0 success
 */
static int nrs_policy_unregister(struct ptlrpc_nrs *nrs, char *name)
{
	struct ptlrpc_nrs_policy *policy = NULL;

	spin_lock(&nrs->nrs_lock);

	policy = nrs_policy_find_locked(nrs, name);
	if (policy == NULL) {
		spin_unlock(&nrs->nrs_lock);

		CERROR("Can't find NRS policy %s\n", name);
		return -ENOENT;
	}

	if (policy->pol_ref > 1) {
		CERROR("Policy %s is busy with %d references\n", name,
		       (int)policy->pol_ref);
		nrs_policy_put_locked(policy);

		spin_unlock(&nrs->nrs_lock);
		return -EBUSY;
	}

	LASSERT(policy->pol_req_queued == 0);
	LASSERT(policy->pol_req_started == 0);

	if (policy->pol_state != NRS_POL_STATE_STOPPED) {
		nrs_policy_stop_locked(policy);
		LASSERT(policy->pol_state == NRS_POL_STATE_STOPPED);
	}

	list_del(&policy->pol_list);
	nrs->nrs_num_pols--;

	nrs_policy_put_locked(policy);

	spin_unlock(&nrs->nrs_lock);

	nrs_policy_fini(policy);

	LASSERT(policy->pol_private == NULL);
	OBD_FREE_PTR(policy);

	return 0;
}

/**
 * Register a policy from \policy descriptor \a desc with NRS head \a nrs.
 *
 * \param[in] nrs   the NRS head on which the policy will be registered.
 * \param[in] desc  the policy descriptor from which the information will be
 *		    obtained to register the policy.
 *
 * \retval -ve error
 * \retval   0 success
 */
static int nrs_policy_register(struct ptlrpc_nrs *nrs,
			       struct ptlrpc_nrs_pol_desc *desc)
{
	struct ptlrpc_nrs_policy       *policy;
	struct ptlrpc_nrs_policy       *tmp;
	struct ptlrpc_service_part     *svcpt = nrs->nrs_svcpt;
	int				rc;

	LASSERT(svcpt != NULL);
	LASSERT(desc->pd_ops != NULL);
	LASSERT(desc->pd_ops->op_res_get != NULL);
	LASSERT(desc->pd_ops->op_req_get != NULL);
	LASSERT(desc->pd_ops->op_req_enqueue != NULL);
	LASSERT(desc->pd_ops->op_req_dequeue != NULL);
	LASSERT(desc->pd_compat != NULL);

	OBD_CPT_ALLOC_GFP(policy, svcpt->scp_service->srv_cptable,
			  svcpt->scp_cpt, sizeof(*policy), __GFP_IO);
	if (policy == NULL)
		return -ENOMEM;

	policy->pol_nrs     = nrs;
	policy->pol_desc    = desc;
	policy->pol_state   = NRS_POL_STATE_STOPPED;
	policy->pol_flags   = desc->pd_flags;

	INIT_LIST_HEAD(&policy->pol_list);
	INIT_LIST_HEAD(&policy->pol_list_queued);

	rc = nrs_policy_init(policy);
	if (rc != 0) {
		OBD_FREE_PTR(policy);
		return rc;
	}

	spin_lock(&nrs->nrs_lock);

	tmp = nrs_policy_find_locked(nrs, policy->pol_desc->pd_name);
	if (tmp != NULL) {
		CERROR("NRS policy %s has been registered, can't register it "
		       "for %s\n", policy->pol_desc->pd_name,
		       svcpt->scp_service->srv_name);
		nrs_policy_put_locked(tmp);

		spin_unlock(&nrs->nrs_lock);
		nrs_policy_fini(policy);
		OBD_FREE_PTR(policy);

		return -EEXIST;
	}

	list_add_tail(&policy->pol_list, &nrs->nrs_policy_list);
	nrs->nrs_num_pols++;

	if (policy->pol_flags & PTLRPC_NRS_FL_REG_START)
		rc = nrs_policy_start_locked(policy);

	spin_unlock(&nrs->nrs_lock);

	if (rc != 0)
		(void) nrs_policy_unregister(nrs, policy->pol_desc->pd_name);

	return rc;
}

/**
 * Enqueue request \a req using one of the policies its resources are referring
 * to.
 *
 * \param[in] req the request to enqueue.
 */
static void ptlrpc_nrs_req_add_nolock(struct ptlrpc_request *req)
{
	struct ptlrpc_nrs_policy       *policy;

	LASSERT(req->rq_nrq.nr_initialized);
	LASSERT(!req->rq_nrq.nr_enqueued);

	nrs_request_enqueue(&req->rq_nrq);
	req->rq_nrq.nr_enqueued = 1;

	policy = nrs_request_policy(&req->rq_nrq);
	/**
	 * Add the policy to the NRS head's list of policies with enqueued
	 * requests, if it has not been added there.
	 */
	if (unlikely(list_empty(&policy->pol_list_queued)))
		list_add_tail(&policy->pol_list_queued,
				  &policy->pol_nrs->nrs_policy_queued);
}

/**
 * Enqueue a request on the high priority NRS head.
 *
 * \param req the request to enqueue.
 */
static void ptlrpc_nrs_hpreq_add_nolock(struct ptlrpc_request *req)
{
	int	opc = lustre_msg_get_opc(req->rq_reqmsg);

	spin_lock(&req->rq_lock);
	req->rq_hp = 1;
	ptlrpc_nrs_req_add_nolock(req);
	if (opc != OBD_PING)
		DEBUG_REQ(D_NET, req, "high priority req");
	spin_unlock(&req->rq_lock);
}

/**
 * Returns a boolean predicate indicating whether the policy described by
 * \a desc is adequate for use with service \a svc.
 *
 * \param[in] svc  the service
 * \param[in] desc the policy descriptor
 *
 * \retval false the policy is not compatible with the service
 * \retval true	 the policy is compatible with the service
 */
static inline bool nrs_policy_compatible(const struct ptlrpc_service *svc,
					 const struct ptlrpc_nrs_pol_desc *desc)
{
	return desc->pd_compat(svc, desc);
}

/**
 * Registers all compatible policies in nrs_core.nrs_policies, for NRS head
 * \a nrs.
 *
 * \param[in] nrs the NRS head
 *
 * \retval -ve error
 * \retval   0 success
 *
 * \pre mutex_is_locked(&nrs_core.nrs_mutex)
 *
 * \see ptlrpc_service_nrs_setup()
 */
static int nrs_register_policies_locked(struct ptlrpc_nrs *nrs)
{
	struct ptlrpc_nrs_pol_desc *desc;
	/* for convenience */
	struct ptlrpc_service_part	 *svcpt = nrs->nrs_svcpt;
	struct ptlrpc_service		 *svc = svcpt->scp_service;
	int				  rc = -EINVAL;

	LASSERT(mutex_is_locked(&nrs_core.nrs_mutex));

	list_for_each_entry(desc, &nrs_core.nrs_policies, pd_list) {
		if (nrs_policy_compatible(svc, desc)) {
			rc = nrs_policy_register(nrs, desc);
			if (rc != 0) {
				CERROR("Failed to register NRS policy %s for "
				       "partition %d of service %s: %d\n",
				       desc->pd_name, svcpt->scp_cpt,
				       svc->srv_name, rc);
				/**
				 * Fail registration if any of the policies'
				 * registration fails.
				 */
				break;
			}
		}
	}

	return rc;
}

/**
 * Initializes NRS head \a nrs of service partition \a svcpt, and registers all
 * compatible policies in NRS core, with the NRS head.
 *
 * \param[in] nrs   the NRS head
 * \param[in] svcpt the PTLRPC service partition to setup
 *
 * \retval -ve error
 * \retval   0 success
 *
 * \pre mutex_is_locked(&nrs_core.nrs_mutex)
 */
static int nrs_svcpt_setup_locked0(struct ptlrpc_nrs *nrs,
				   struct ptlrpc_service_part *svcpt)
{
	int				rc;
	enum ptlrpc_nrs_queue_type	queue;

	LASSERT(mutex_is_locked(&nrs_core.nrs_mutex));

	if (nrs == &svcpt->scp_nrs_reg)
		queue = PTLRPC_NRS_QUEUE_REG;
	else if (nrs == svcpt->scp_nrs_hp)
		queue = PTLRPC_NRS_QUEUE_HP;
	else
		LBUG();

	nrs->nrs_svcpt = svcpt;
	nrs->nrs_queue_type = queue;
	spin_lock_init(&nrs->nrs_lock);
	INIT_LIST_HEAD(&nrs->nrs_policy_list);
	INIT_LIST_HEAD(&nrs->nrs_policy_queued);

	rc = nrs_register_policies_locked(nrs);

	return rc;
}

/**
 * Allocates a regular and optionally a high-priority NRS head (if the service
 * handles high-priority RPCs), and then registers all available compatible
 * policies on those NRS heads.
 *
 * \param[in,out] svcpt the PTLRPC service partition to setup
 *
 * \pre mutex_is_locked(&nrs_core.nrs_mutex)
 */
static int nrs_svcpt_setup_locked(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_nrs	       *nrs;
	int				rc;

	LASSERT(mutex_is_locked(&nrs_core.nrs_mutex));

	/**
	 * Initialize the regular NRS head.
	 */
	nrs = nrs_svcpt2nrs(svcpt, false);
	rc = nrs_svcpt_setup_locked0(nrs, svcpt);
	if (rc < 0)
		GOTO(out, rc);

	/**
	 * Optionally allocate a high-priority NRS head.
	 */
	if (svcpt->scp_service->srv_ops.so_hpreq_handler == NULL)
		GOTO(out, rc);

	OBD_CPT_ALLOC_PTR(svcpt->scp_nrs_hp,
			  svcpt->scp_service->srv_cptable,
			  svcpt->scp_cpt);
	if (svcpt->scp_nrs_hp == NULL)
		GOTO(out, rc = -ENOMEM);

	nrs = nrs_svcpt2nrs(svcpt, true);
	rc = nrs_svcpt_setup_locked0(nrs, svcpt);

out:
	return rc;
}

/**
 * Unregisters all policies on all available NRS heads in a service partition;
 * called at PTLRPC service unregistration time.
 *
 * \param[in] svcpt the PTLRPC service partition
 *
 * \pre mutex_is_locked(&nrs_core.nrs_mutex)
 */
static void nrs_svcpt_cleanup_locked(struct ptlrpc_service_part *svcpt)
{
	struct ptlrpc_nrs	       *nrs;
	struct ptlrpc_nrs_policy       *policy;
	struct ptlrpc_nrs_policy       *tmp;
	int				rc;
	bool				hp = false;

	LASSERT(mutex_is_locked(&nrs_core.nrs_mutex));

again:
	nrs = nrs_svcpt2nrs(svcpt, hp);
	nrs->nrs_stopping = 1;

	list_for_each_entry_safe(policy, tmp, &nrs->nrs_policy_list,
				     pol_list) {
		rc = nrs_policy_unregister(nrs, policy->pol_desc->pd_name);
		LASSERT(rc == 0);
	}

	/**
	 * If the service partition has an HP NRS head, clean that up as well.
	 */
	if (!hp && nrs_svcpt_has_hp(svcpt)) {
		hp = true;
		goto again;
	}

	if (hp)
		OBD_FREE_PTR(nrs);
}

/**
 * Returns the descriptor for a policy as identified by by \a name.
 *
 * \param[in] name the policy name
 *
 * \retval the policy descriptor
 * \retval NULL
 */
static struct ptlrpc_nrs_pol_desc *nrs_policy_find_desc_locked(const char *name)
{
	struct ptlrpc_nrs_pol_desc     *tmp;

	list_for_each_entry(tmp, &nrs_core.nrs_policies, pd_list) {
		if (strncmp(tmp->pd_name, name, NRS_POL_NAME_MAX) == 0)
			return tmp;
	}
	return NULL;
}

/**
 * Removes the policy from all supported NRS heads of all partitions of all
 * PTLRPC services.
 *
 * \param[in] desc the policy descriptor to unregister
 *
 * \retval -ve error
 * \retval  0  successfully unregistered policy on all supported NRS heads
 *
 * \pre mutex_is_locked(&nrs_core.nrs_mutex)
 * \pre mutex_is_locked(&ptlrpc_all_services_mutex)
 */
static int nrs_policy_unregister_locked(struct ptlrpc_nrs_pol_desc *desc)
{
	struct ptlrpc_nrs	       *nrs;
	struct ptlrpc_service	       *svc;
	struct ptlrpc_service_part     *svcpt;
	int				i;
	int				rc = 0;

	LASSERT(mutex_is_locked(&nrs_core.nrs_mutex));
	LASSERT(mutex_is_locked(&ptlrpc_all_services_mutex));

	list_for_each_entry(svc, &ptlrpc_all_services, srv_list) {

		if (!nrs_policy_compatible(svc, desc) ||
		    unlikely(svc->srv_is_stopping))
			continue;

		ptlrpc_service_for_each_part(svcpt, i, svc) {
			bool hp = false;

again:
			nrs = nrs_svcpt2nrs(svcpt, hp);
			rc = nrs_policy_unregister(nrs, desc->pd_name);
			/**
			 * Ignore -ENOENT as the policy may not have registered
			 * successfully on all service partitions.
			 */
			if (rc == -ENOENT) {
				rc = 0;
			} else if (rc != 0) {
				CERROR("Failed to unregister NRS policy %s for "
				       "partition %d of service %s: %d\n",
				       desc->pd_name, svcpt->scp_cpt,
				       svcpt->scp_service->srv_name, rc);
				return rc;
			}

			if (!hp && nrs_svc_has_hp(svc)) {
				hp = true;
				goto again;
			}
		}

		if (desc->pd_ops->op_lprocfs_fini != NULL)
			desc->pd_ops->op_lprocfs_fini(svc);
	}

	return rc;
}

/**
 * Registers a new policy with NRS core.
 *
 * The function will only succeed if policy registration with all compatible
 * service partitions (if any) is successful.
 *
 * N.B. This function should be called either at ptlrpc module initialization
 *	time when registering a policy that ships with NRS core, or in a
 *	module's init() function for policies registering from other modules.
 *
 * \param[in] conf configuration information for the new policy to register
 *
 * \retval -ve error
 * \retval   0 success
 */
int ptlrpc_nrs_policy_register(struct ptlrpc_nrs_pol_conf *conf)
{
	struct ptlrpc_service	       *svc;
	struct ptlrpc_nrs_pol_desc     *desc;
	int				rc = 0;

	LASSERT(conf != NULL);
	LASSERT(conf->nc_ops != NULL);
	LASSERT(conf->nc_compat != NULL);
	LASSERT(ergo(conf->nc_compat == nrs_policy_compat_one,
		conf->nc_compat_svc_name != NULL));
	LASSERT(ergo((conf->nc_flags & PTLRPC_NRS_FL_REG_EXTERN) != 0,
		     conf->nc_owner != NULL));

	conf->nc_name[NRS_POL_NAME_MAX - 1] = '\0';

	/**
	 * External policies are not allowed to start immediately upon
	 * registration, as there is a relatively higher chance that their
	 * registration might fail. In such a case, some policy instances may
	 * already have requests queued wen unregistration needs to happen as
	 * part o cleanup; since there is currently no way to drain requests
	 * from a policy unless the service is unregistering, we just disallow
	 * this.
	 */
	if ((conf->nc_flags & PTLRPC_NRS_FL_REG_EXTERN) &&
	    (conf->nc_flags & (PTLRPC_NRS_FL_FALLBACK |
			       PTLRPC_NRS_FL_REG_START))) {
		CERROR("NRS: failing to register policy %s. Please check "
		       "policy flags; external policies cannot act as fallback "
		       "policies, or be started immediately upon registration "
		       "without interaction with lprocfs\n", conf->nc_name);
		return -EINVAL;
	}

	mutex_lock(&nrs_core.nrs_mutex);

	if (nrs_policy_find_desc_locked(conf->nc_name) != NULL) {
		CERROR("NRS: failing to register policy %s which has already "
		       "been registered with NRS core!\n",
		       conf->nc_name);
		GOTO(fail, rc = -EEXIST);
	}

	OBD_ALLOC_PTR(desc);
	if (desc == NULL)
		GOTO(fail, rc = -ENOMEM);

	strncpy(desc->pd_name, conf->nc_name, NRS_POL_NAME_MAX);
	desc->pd_ops		 = conf->nc_ops;
	desc->pd_compat		 = conf->nc_compat;
	desc->pd_compat_svc_name = conf->nc_compat_svc_name;
	if ((conf->nc_flags & PTLRPC_NRS_FL_REG_EXTERN) != 0)
		desc->pd_owner	 = conf->nc_owner;
	desc->pd_flags		 = conf->nc_flags;
	atomic_set(&desc->pd_refs, 0);

	/**
	 * For policies that are held in the same module as NRS (currently
	 * ptlrpc), do not register the policy with all compatible services,
	 * as the services will not have started at this point, since we are
	 * calling from ptlrpc module initialization code. In such cases each
	 * service will register all compatible policies later, via
	 * ptlrpc_service_nrs_setup().
	 */
	if ((conf->nc_flags & PTLRPC_NRS_FL_REG_EXTERN) == 0)
		goto internal;

	/**
	 * Register the new policy on all compatible services
	 */
	mutex_lock(&ptlrpc_all_services_mutex);

	list_for_each_entry(svc, &ptlrpc_all_services, srv_list) {
		struct ptlrpc_service_part     *svcpt;
		int				i;
		int				rc2;

		if (!nrs_policy_compatible(svc, desc) ||
		    unlikely(svc->srv_is_stopping))
			continue;

		ptlrpc_service_for_each_part(svcpt, i, svc) {
			struct ptlrpc_nrs      *nrs;
			bool			hp = false;
again:
			nrs = nrs_svcpt2nrs(svcpt, hp);
			rc = nrs_policy_register(nrs, desc);
			if (rc != 0) {
				CERROR("Failed to register NRS policy %s for "
				       "partition %d of service %s: %d\n",
				       desc->pd_name, svcpt->scp_cpt,
				       svcpt->scp_service->srv_name, rc);

				rc2 = nrs_policy_unregister_locked(desc);
				/**
				 * Should not fail at this point
				 */
				LASSERT(rc2 == 0);
				mutex_unlock(&ptlrpc_all_services_mutex);
				OBD_FREE_PTR(desc);
				GOTO(fail, rc);
			}

			if (!hp && nrs_svc_has_hp(svc)) {
				hp = true;
				goto again;
			}
		}

		/**
		 * No need to take a reference to other modules here, as we
		 * will be calling from the module's init() function.
		 */
		if (desc->pd_ops->op_lprocfs_init != NULL) {
			rc = desc->pd_ops->op_lprocfs_init(svc);
			if (rc != 0) {
				rc2 = nrs_policy_unregister_locked(desc);
				/**
				 * Should not fail at this point
				 */
				LASSERT(rc2 == 0);
				mutex_unlock(&ptlrpc_all_services_mutex);
				OBD_FREE_PTR(desc);
				GOTO(fail, rc);
			}
		}
	}

	mutex_unlock(&ptlrpc_all_services_mutex);
internal:
	list_add_tail(&desc->pd_list, &nrs_core.nrs_policies);
fail:
	mutex_unlock(&nrs_core.nrs_mutex);

	return rc;
}
EXPORT_SYMBOL(ptlrpc_nrs_policy_register);

/**
 * Unregisters a previously registered policy with NRS core. All instances of
 * the policy on all NRS heads of all supported services are removed.
 *
 * N.B. This function should only be called from a module's exit() function.
 *	Although it can be used for policies that ship alongside NRS core, the
 *	function is primarily intended for policies that register externally,
 *	from other modules.
 *
 * \param[in] conf configuration information for the policy to unregister
 *
 * \retval -ve error
 * \retval   0 success
 */
int ptlrpc_nrs_policy_unregister(struct ptlrpc_nrs_pol_conf *conf)
{
	struct ptlrpc_nrs_pol_desc	*desc;
	int				 rc;

	LASSERT(conf != NULL);

	if (conf->nc_flags & PTLRPC_NRS_FL_FALLBACK) {
		CERROR("Unable to unregister a fallback policy, unless the "
		       "PTLRPC service is stopping.\n");
		return -EPERM;
	}

	conf->nc_name[NRS_POL_NAME_MAX - 1] = '\0';

	mutex_lock(&nrs_core.nrs_mutex);

	desc = nrs_policy_find_desc_locked(conf->nc_name);
	if (desc == NULL) {
		CERROR("Failing to unregister NRS policy %s which has "
		       "not been registered with NRS core!\n",
		       conf->nc_name);
		GOTO(not_exist, rc = -ENOENT);
	}

	mutex_lock(&ptlrpc_all_services_mutex);

	rc = nrs_policy_unregister_locked(desc);
	if (rc < 0) {
		if (rc == -EBUSY)
			CERROR("Please first stop policy %s on all service "
			       "partitions and then retry to unregister the "
			       "policy.\n", conf->nc_name);
		GOTO(fail, rc);
	}

	CDEBUG(D_INFO, "Unregistering policy %s from NRS core.\n",
	       conf->nc_name);

	list_del(&desc->pd_list);
	OBD_FREE_PTR(desc);

fail:
	mutex_unlock(&ptlrpc_all_services_mutex);

not_exist:
	mutex_unlock(&nrs_core.nrs_mutex);

	return rc;
}
EXPORT_SYMBOL(ptlrpc_nrs_policy_unregister);

/**
 * Setup NRS heads on all service partitions of service \a svc, and register
 * all compatible policies on those NRS heads.
 *
 * To be called from withing ptl
 * \param[in] svc the service to setup
 *
 * \retval -ve error, the calling logic should eventually call
 *		      ptlrpc_service_nrs_cleanup() to undo any work performed
 *		      by this function.
 *
 * \see ptlrpc_register_service()
 * \see ptlrpc_service_nrs_cleanup()
 */
int ptlrpc_service_nrs_setup(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part	       *svcpt;
	const struct ptlrpc_nrs_pol_desc       *desc;
	int					i;
	int					rc = 0;

	mutex_lock(&nrs_core.nrs_mutex);

	/**
	 * Initialize NRS heads on all service CPTs.
	 */
	ptlrpc_service_for_each_part(svcpt, i, svc) {
		rc = nrs_svcpt_setup_locked(svcpt);
		if (rc != 0)
			GOTO(failed, rc);
	}

	/**
	 * Set up lprocfs interfaces for all supported policies for the
	 * service.
	 */
	list_for_each_entry(desc, &nrs_core.nrs_policies, pd_list) {
		if (!nrs_policy_compatible(svc, desc))
			continue;

		if (desc->pd_ops->op_lprocfs_init != NULL) {
			rc = desc->pd_ops->op_lprocfs_init(svc);
			if (rc != 0)
				GOTO(failed, rc);
		}
	}

failed:

	mutex_unlock(&nrs_core.nrs_mutex);

	return rc;
}

/**
 * Unregisters all policies on all service partitions of service \a svc.
 *
 * \param[in] svc the PTLRPC service to unregister
 */
void ptlrpc_service_nrs_cleanup(struct ptlrpc_service *svc)
{
	struct ptlrpc_service_part	     *svcpt;
	const struct ptlrpc_nrs_pol_desc     *desc;
	int				      i;

	mutex_lock(&nrs_core.nrs_mutex);

	/**
	 * Clean up NRS heads on all service partitions
	 */
	ptlrpc_service_for_each_part(svcpt, i, svc)
		nrs_svcpt_cleanup_locked(svcpt);

	/**
	 * Clean up lprocfs interfaces for all supported policies for the
	 * service.
	 */
	list_for_each_entry(desc, &nrs_core.nrs_policies, pd_list) {
		if (!nrs_policy_compatible(svc, desc))
			continue;

		if (desc->pd_ops->op_lprocfs_fini != NULL)
			desc->pd_ops->op_lprocfs_fini(svc);
	}

	mutex_unlock(&nrs_core.nrs_mutex);
}

/**
 * Obtains NRS head resources for request \a req.
 *
 * These could be either on the regular or HP NRS head of \a svcpt; resources
 * taken on the regular head can later be swapped for HP head resources by
 * ldlm_lock_reorder_req().
 *
 * \param[in] svcpt the service partition
 * \param[in] req   the request
 * \param[in] hp    which NRS head of \a svcpt to use
 */
void ptlrpc_nrs_req_initialize(struct ptlrpc_service_part *svcpt,
			       struct ptlrpc_request *req, bool hp)
{
	struct ptlrpc_nrs	*nrs = nrs_svcpt2nrs(svcpt, hp);

	memset(&req->rq_nrq, 0, sizeof(req->rq_nrq));
	nrs_resource_get_safe(nrs, &req->rq_nrq, req->rq_nrq.nr_res_ptrs,
			      false);

	/**
	 * It is fine to access \e nr_initialized without locking as there is
	 * no contention at this early stage.
	 */
	req->rq_nrq.nr_initialized = 1;
}

/**
 * Releases resources for a request; is called after the request has been
 * handled.
 *
 * \param[in] req the request
 *
 * \see ptlrpc_server_finish_request()
 */
void ptlrpc_nrs_req_finalize(struct ptlrpc_request *req)
{
	if (req->rq_nrq.nr_initialized) {
		nrs_resource_put_safe(req->rq_nrq.nr_res_ptrs);
		/* no protection on bit nr_initialized because no
		 * contention at this late stage */
		req->rq_nrq.nr_finalized = 1;
	}
}

void ptlrpc_nrs_req_stop_nolock(struct ptlrpc_request *req)
{
	if (req->rq_nrq.nr_started)
		nrs_request_stop(&req->rq_nrq);
}

/**
 * Enqueues request \a req on either the regular or high-priority NRS head
 * of service partition \a svcpt.
 *
 * \param[in] svcpt the service partition
 * \param[in] req   the request to be enqueued
 * \param[in] hp    whether to enqueue the request on the regular or
 *		    high-priority NRS head.
 */
void ptlrpc_nrs_req_add(struct ptlrpc_service_part *svcpt,
			struct ptlrpc_request *req, bool hp)
{
	spin_lock(&svcpt->scp_req_lock);

	if (hp)
		ptlrpc_nrs_hpreq_add_nolock(req);
	else
		ptlrpc_nrs_req_add_nolock(req);

	spin_unlock(&svcpt->scp_req_lock);
}

static void nrs_request_removed(struct ptlrpc_nrs_policy *policy)
{
	LASSERT(policy->pol_nrs->nrs_req_queued > 0);
	LASSERT(policy->pol_req_queued > 0);

	policy->pol_nrs->nrs_req_queued--;
	policy->pol_req_queued--;

	/**
	 * If the policy has no more requests queued, remove it from
	 * ptlrpc_nrs::nrs_policy_queued.
	 */
	if (unlikely(policy->pol_req_queued == 0)) {
		list_del_init(&policy->pol_list_queued);

		/**
		 * If there are other policies with queued requests, move the
		 * current policy to the end so that we can round robin over
		 * all policies and drain the requests.
		 */
	} else if (policy->pol_req_queued != policy->pol_nrs->nrs_req_queued) {
		LASSERT(policy->pol_req_queued <
			policy->pol_nrs->nrs_req_queued);

		list_move_tail(&policy->pol_list_queued,
				   &policy->pol_nrs->nrs_policy_queued);
	}
}

/**
 * Obtains a request for handling from an NRS head of service partition
 * \a svcpt.
 *
 * \param[in] svcpt the service partition
 * \param[in] hp    whether to obtain a request from the regular or
 *		    high-priority NRS head.
 * \param[in] peek  when set, signifies that we just want to examine the
 *		    request, and not handle it, so the request is not removed
 *		    from the policy.
 * \param[in] force when set, it will force a policy to return a request if it
 *		    has one pending
 *
 * \retval the	request to be handled
 * \retval NULL the head has no requests to serve
 */
struct ptlrpc_request *
ptlrpc_nrs_req_get_nolock0(struct ptlrpc_service_part *svcpt, bool hp,
			   bool peek, bool force)
{
	struct ptlrpc_nrs	  *nrs = nrs_svcpt2nrs(svcpt, hp);
	struct ptlrpc_nrs_policy  *policy;
	struct ptlrpc_nrs_request *nrq;

	/**
	 * Always try to drain requests from all NRS polices even if they are
	 * inactive, because the user can change policy status at runtime.
	 */
	list_for_each_entry(policy, &nrs->nrs_policy_queued,
				pol_list_queued) {
		nrq = nrs_request_get(policy, peek, force);
		if (nrq != NULL) {
			if (likely(!peek)) {
				nrq->nr_started = 1;

				policy->pol_req_started++;
				policy->pol_nrs->nrs_req_started++;

				nrs_request_removed(policy);
			}

			return container_of(nrq, struct ptlrpc_request, rq_nrq);
		}
	}

	return NULL;
}

/**
 * Dequeues request \a req from the policy it has been enqueued on.
 *
 * \param[in] req the request
 */
void ptlrpc_nrs_req_del_nolock(struct ptlrpc_request *req)
{
	struct ptlrpc_nrs_policy *policy = nrs_request_policy(&req->rq_nrq);

	policy->pol_desc->pd_ops->op_req_dequeue(policy, &req->rq_nrq);

	req->rq_nrq.nr_enqueued = 0;

	nrs_request_removed(policy);
}

/**
 * Returns whether there are any requests currently enqueued on any of the
 * policies of service partition's \a svcpt NRS head specified by \a hp. Should
 * be called while holding ptlrpc_service_part::scp_req_lock to get a reliable
 * result.
 *
 * \param[in] svcpt the service partition to enquire.
 * \param[in] hp    whether the regular or high-priority NRS head is to be
 *		    enquired.
 *
 * \retval false the indicated NRS head has no enqueued requests.
 * \retval true	 the indicated NRS head has some enqueued requests.
 */
bool ptlrpc_nrs_req_pending_nolock(struct ptlrpc_service_part *svcpt, bool hp)
{
	struct ptlrpc_nrs *nrs = nrs_svcpt2nrs(svcpt, hp);

	return nrs->nrs_req_queued > 0;
};

/**
 * Moves request \a req from the regular to the high-priority NRS head.
 *
 * \param[in] req the request to move
 */
void ptlrpc_nrs_req_hp_move(struct ptlrpc_request *req)
{
	struct ptlrpc_service_part	*svcpt = req->rq_rqbd->rqbd_svcpt;
	struct ptlrpc_nrs_request	*nrq = &req->rq_nrq;
	struct ptlrpc_nrs_resource	*res1[NRS_RES_MAX];
	struct ptlrpc_nrs_resource	*res2[NRS_RES_MAX];

	/**
	 * Obtain the high-priority NRS head resources.
	 */
	nrs_resource_get_safe(nrs_svcpt2nrs(svcpt, true), nrq, res1, true);

	spin_lock(&svcpt->scp_req_lock);

	if (!ptlrpc_nrs_req_can_move(req))
		goto out;

	ptlrpc_nrs_req_del_nolock(req);

	memcpy(res2, nrq->nr_res_ptrs, NRS_RES_MAX * sizeof(res2[0]));
	memcpy(nrq->nr_res_ptrs, res1, NRS_RES_MAX * sizeof(res1[0]));

	ptlrpc_nrs_hpreq_add_nolock(req);

	memcpy(res1, res2, NRS_RES_MAX * sizeof(res1[0]));
out:
	spin_unlock(&svcpt->scp_req_lock);

	/**
	 * Release either the regular NRS head resources if we moved the
	 * request, or the high-priority NRS head resources if we took a
	 * reference earlier in this function and ptlrpc_nrs_req_can_move()
	 * returned false.
	 */
	nrs_resource_put_safe(res1);
}

/**
 * Carries out a control operation \a opc on the policy identified by the
 * human-readable \a name, on either all partitions, or only on the first
 * partition of service \a svc.
 *
 * \param[in]	  svc	 the service the policy belongs to.
 * \param[in]	  queue  whether to carry out the command on the policy which
 *			 belongs to the regular, high-priority, or both NRS
 *			 heads of service partitions of \a svc.
 * \param[in]	  name   the policy to act upon, by human-readable name
 * \param[in]	  opc	 the opcode of the operation to carry out
 * \param[in]	  single when set, the operation will only be carried out on the
 *			 NRS heads of the first service partition of \a svc.
 *			 This is useful for some policies which e.g. share
 *			 identical values on the same parameters of different
 *			 service partitions; when reading these parameters via
 *			 lprocfs, these policies may just want to obtain and
 *			 print out the values from the first service partition.
 *			 Storing these values centrally elsewhere then could be
 *			 another solution for this.
 * \param[in,out] arg	 can be used as a generic in/out buffer between control
 *			 operations and the user environment.
 *
 *\retval -ve error condition
 *\retval   0 operation was carried out successfully
 */
int ptlrpc_nrs_policy_control(const struct ptlrpc_service *svc,
			      enum ptlrpc_nrs_queue_type queue, char *name,
			      enum ptlrpc_nrs_ctl opc, bool single, void *arg)
{
	struct ptlrpc_service_part     *svcpt;
	int				i;
	int				rc = 0;

	LASSERT(opc != PTLRPC_NRS_CTL_INVALID);

	if ((queue & PTLRPC_NRS_QUEUE_BOTH) == 0)
		return -EINVAL;

	ptlrpc_service_for_each_part(svcpt, i, svc) {
		if ((queue & PTLRPC_NRS_QUEUE_REG) != 0) {
			rc = nrs_policy_ctl(nrs_svcpt2nrs(svcpt, false), name,
					    opc, arg);
			if (rc != 0 || (queue == PTLRPC_NRS_QUEUE_REG &&
					single))
				GOTO(out, rc);
		}

		if ((queue & PTLRPC_NRS_QUEUE_HP) != 0) {
			/**
			 * XXX: We could optionally check for
			 * nrs_svc_has_hp(svc) here, and return an error if it
			 * is false. Right now we rely on the policies' lprocfs
			 * handlers that call the present function to make this
			 * check; if they fail to do so, they might hit the
			 * assertion inside nrs_svcpt2nrs() below.
			 */
			rc = nrs_policy_ctl(nrs_svcpt2nrs(svcpt, true), name,
					    opc, arg);
			if (rc != 0 || single)
				GOTO(out, rc);
		}
	}
out:
	return rc;
}


/* ptlrpc/nrs_fifo.c */
extern struct ptlrpc_nrs_pol_conf nrs_conf_fifo;

/**
 * Adds all policies that ship with the ptlrpc module, to NRS core's list of
 * policies \e nrs_core.nrs_policies.
 *
 * \retval 0 all policies have been registered successfully
 * \retval -ve error
 */
int ptlrpc_nrs_init(void)
{
	int	rc;

	mutex_init(&nrs_core.nrs_mutex);
	INIT_LIST_HEAD(&nrs_core.nrs_policies);

	rc = ptlrpc_nrs_policy_register(&nrs_conf_fifo);
	if (rc != 0)
		GOTO(fail, rc);


	return rc;
fail:
	/**
	 * Since no PTLRPC services have been started at this point, all we need
	 * to do for cleanup is to free the descriptors.
	 */
	ptlrpc_nrs_fini();

	return rc;
}

/**
 * Removes all policy desciptors from nrs_core::nrs_policies, and frees the
 * policy descriptors.
 *
 * Since all PTLRPC services are stopped at this point, there are no more
 * instances of any policies, because each service will have stopped its policy
 * instances in ptlrpc_service_nrs_cleanup(), so we just need to free the
 * descriptors here.
 */
void ptlrpc_nrs_fini(void)
{
	struct ptlrpc_nrs_pol_desc *desc;
	struct ptlrpc_nrs_pol_desc *tmp;

	list_for_each_entry_safe(desc, tmp, &nrs_core.nrs_policies,
				     pd_list) {
		list_del_init(&desc->pd_list);
		OBD_FREE_PTR(desc);
	}
}

/** @} nrs */
