/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2014, Intel Corporation.
 *
 * Copyright 2012 Xyratex Technology Limited
 */
/*
 *
 * Network Request Scheduler (NRS)
 *
 */

#ifndef _LUSTRE_NRS_H
#define _LUSTRE_NRS_H

/**
 * \defgroup nrs Network Request Scheduler
 * @{
 */
struct ptlrpc_nrs_policy;
struct ptlrpc_nrs_resource;
struct ptlrpc_nrs_request;

/**
 * NRS control operations.
 *
 * These are common for all policies.
 */
enum ptlrpc_nrs_ctl {
	/**
	 * Not a valid opcode.
	 */
	PTLRPC_NRS_CTL_INVALID,
	/**
	 * Activate the policy.
	 */
	PTLRPC_NRS_CTL_START,
	/**
	 * Reserved for multiple primary policies, which may be a possibility
	 * in the future.
	 */
	PTLRPC_NRS_CTL_STOP,
	/**
	 * Policies can start using opcodes from this value and onwards for
	 * their own purposes; the assigned value itself is arbitrary.
	 */
	PTLRPC_NRS_CTL_1ST_POL_SPEC = 0x20,
};

/**
 * NRS policy operations.
 *
 * These determine the behaviour of a policy, and are called in response to
 * NRS core events.
 */
struct ptlrpc_nrs_pol_ops {
	/**
	 * Called during policy registration; this operation is optional.
	 *
	 * \param[in,out] policy The policy being initialized
	 */
	int	(*op_policy_init)(struct ptlrpc_nrs_policy *policy);
	/**
	 * Called during policy unregistration; this operation is optional.
	 *
	 * \param[in,out] policy The policy being unregistered/finalized
	 */
	void	(*op_policy_fini)(struct ptlrpc_nrs_policy *policy);
	/**
	 * Called when activating a policy via lprocfs; policies allocate and
	 * initialize their resources here; this operation is optional.
	 *
	 * \param[in,out] policy The policy being started
	 *
	 * \see nrs_policy_start_locked()
	 */
	int	(*op_policy_start)(struct ptlrpc_nrs_policy *policy);
	/**
	 * Called when deactivating a policy via lprocfs; policies deallocate
	 * their resources here; this operation is optional
	 *
	 * \param[in,out] policy The policy being stopped
	 *
	 * \see nrs_policy_stop0()
	 */
	void	(*op_policy_stop)(struct ptlrpc_nrs_policy *policy);
	/**
	 * Used for policy-specific operations; i.e. not generic ones like
	 * \e PTLRPC_NRS_CTL_START and \e PTLRPC_NRS_CTL_GET_INFO; analogous
	 * to an ioctl; this operation is optional.
	 *
	 * \param[in,out]	 policy The policy carrying out operation \a opc
	 * \param[in]	  opc	 The command operation being carried out
	 * \param[in,out] arg	 An generic buffer for communication between the
	 *			 user and the control operation
	 *
	 * \retval -ve error
	 * \retval   0 success
	 *
	 * \see ptlrpc_nrs_policy_control()
	 */
	int	(*op_policy_ctl)(struct ptlrpc_nrs_policy *policy,
				 enum ptlrpc_nrs_ctl opc, void *arg);

	/**
	 * Called when obtaining references to the resources of the resource
	 * hierarchy for a request that has arrived for handling at the PTLRPC
	 * service. Policies should return -ve for requests they do not wish
	 * to handle. This operation is mandatory.
	 *
	 * \param[in,out] policy  The policy we're getting resources for.
	 * \param[in,out] nrq	  The request we are getting resources for.
	 * \param[in]	  parent  The parent resource of the resource being
	 *			  requested; set to NULL if none.
	 * \param[out]	  resp	  The resource is to be returned here; the
	 *			  fallback policy in an NRS head should
	 *			  \e always return a non-NULL pointer value.
	 * \param[in]  moving_req When set, signifies that this is an attempt
	 *			  to obtain resources for a request being moved
	 *			  to the high-priority NRS head by
	 *			  ldlm_lock_reorder_req().
	 *			  This implies two things:
	 *			  1. We are under obd_export::exp_rpc_lock and
	 *			  so should not sleep.
	 *			  2. We should not perform non-idempotent or can
	 *			  skip performing idempotent operations that
	 *			  were carried out when resources were first
	 *			  taken for the request when it was initialized
	 *			  in ptlrpc_nrs_req_initialize().
	 *
	 * \retval 0, +ve The level of the returned resource in the resource
	 *		  hierarchy; currently only 0 (for a non-leaf resource)
	 *		  and 1 (for a leaf resource) are supported by the
	 *		  framework.
	 * \retval -ve	  error
	 *
	 * \see ptlrpc_nrs_req_initialize()
	 * \see ptlrpc_nrs_hpreq_add_nolock()
	 * \see ptlrpc_nrs_req_hp_move()
	 */
	int	(*op_res_get)(struct ptlrpc_nrs_policy *policy,
			      struct ptlrpc_nrs_request *nrq,
			      const struct ptlrpc_nrs_resource *parent,
			      struct ptlrpc_nrs_resource **resp,
			      bool moving_req);
	/**
	 * Called when releasing references taken for resources in the resource
	 * hierarchy for the request; this operation is optional.
	 *
	 * \param[in,out] policy The policy the resource belongs to
	 * \param[in] res	 The resource to be freed
	 *
	 * \see ptlrpc_nrs_req_finalize()
	 * \see ptlrpc_nrs_hpreq_add_nolock()
	 * \see ptlrpc_nrs_req_hp_move()
	 */
	void	(*op_res_put)(struct ptlrpc_nrs_policy *policy,
			      const struct ptlrpc_nrs_resource *res);

	/**
	 * Obtains a request for handling from the policy, and optionally
	 * removes the request from the policy; this operation is mandatory.
	 *
	 * \param[in,out] policy The policy to poll
	 * \param[in]	  peek	 When set, signifies that we just want to
	 *			 examine the request, and not handle it, so the
	 *			 request is not removed from the policy.
	 * \param[in]	  force  When set, it will force a policy to return a
	 *			 request if it has one queued.
	 *
	 * \retval NULL No request available for handling
	 * \retval valid-pointer The request polled for handling
	 *
	 * \see ptlrpc_nrs_req_get_nolock()
	 */
	struct ptlrpc_nrs_request *
		(*op_req_get)(struct ptlrpc_nrs_policy *policy, bool peek,
			      bool force);
	/**
	 * Called when attempting to add a request to a policy for later
	 * handling; this operation is mandatory.
	 *
	 * \param[in,out] policy  The policy on which to enqueue \a nrq
	 * \param[in,out] nrq The request to enqueue
	 *
	 * \retval 0	success
	 * \retval != 0 error
	 *
	 * \see ptlrpc_nrs_req_add_nolock()
	 */
	int	(*op_req_enqueue)(struct ptlrpc_nrs_policy *policy,
				  struct ptlrpc_nrs_request *nrq);
	/**
	 * Removes a request from the policy's set of pending requests. Normally
	 * called after a request has been polled successfully from the policy
	 * for handling; this operation is mandatory.
	 *
	 * \param[in,out] policy The policy the request \a nrq belongs to
	 * \param[in,out] nrq	 The request to dequeue
	 *
	 * \see ptlrpc_nrs_req_del_nolock()
	 */
	void	(*op_req_dequeue)(struct ptlrpc_nrs_policy *policy,
				  struct ptlrpc_nrs_request *nrq);
	/**
	 * Called after the request being carried out. Could be used for
	 * job/resource control; this operation is optional.
	 *
	 * \param[in,out] policy The policy which is stopping to handle request
	 *			 \a nrq
	 * \param[in,out] nrq	 The request
	 *
	 * \pre assert_spin_locked(&svcpt->scp_req_lock)
	 *
	 * \see ptlrpc_nrs_req_stop_nolock()
	 */
	void	(*op_req_stop)(struct ptlrpc_nrs_policy *policy,
			       struct ptlrpc_nrs_request *nrq);
	/**
	 * Registers the policy's lprocfs interface with a PTLRPC service.
	 *
	 * \param[in] svc The service
	 *
	 * \retval 0	success
	 * \retval != 0 error
	 */
	int	(*op_lprocfs_init)(struct ptlrpc_service *svc);
	/**
	 * Unegisters the policy's lprocfs interface with a PTLRPC service.
	 *
	 * In cases of failed policy registration in
	 * \e ptlrpc_nrs_policy_register(), this function may be called for a
	 * service which has not registered the policy successfully, so
	 * implementations of this method should make sure their operations are
	 * safe in such cases.
	 *
	 * \param[in] svc The service
	 */
	void	(*op_lprocfs_fini)(struct ptlrpc_service *svc);
};

/**
 * Policy flags
 */
enum nrs_policy_flags {
	/**
	 * Fallback policy, use this flag only on a single supported policy per
	 * service. The flag cannot be used on policies that use
	 * \e PTLRPC_NRS_FL_REG_EXTERN
	 */
	PTLRPC_NRS_FL_FALLBACK		= BIT(0),
	/**
	 * Start policy immediately after registering.
	 */
	PTLRPC_NRS_FL_REG_START		= BIT(1),
	/**
	 * This is a policy registering from a module different to the one NRS
	 * core ships in (currently ptlrpc).
	 */
	PTLRPC_NRS_FL_REG_EXTERN	= BIT(2),
};

/**
 * NRS queue type.
 *
 * Denotes whether an NRS instance is for handling normal or high-priority
 * RPCs, or whether an operation pertains to one or both of the NRS instances
 * in a service.
 */
enum ptlrpc_nrs_queue_type {
	PTLRPC_NRS_QUEUE_REG	= BIT(0),
	PTLRPC_NRS_QUEUE_HP	= BIT(1),
	PTLRPC_NRS_QUEUE_BOTH	= (PTLRPC_NRS_QUEUE_REG | PTLRPC_NRS_QUEUE_HP)
};

/**
 * NRS head
 *
 * A PTLRPC service has at least one NRS head instance for handling normal
 * priority RPCs, and may optionally have a second NRS head instance for
 * handling high-priority RPCs. Each NRS head maintains a list of available
 * policies, of which one and only one policy is acting as the fallback policy,
 * and optionally a different policy may be acting as the primary policy. For
 * all RPCs handled by this NRS head instance, NRS core will first attempt to
 * enqueue the RPC using the primary policy (if any). The fallback policy is
 * used in the following cases:
 * - when there was no primary policy in the
 *   ptlrpc_nrs_pol_state::NRS_POL_STATE_STARTED state at the time the request
 *   was initialized.
 * - when the primary policy that was at the
 *   ptlrpc_nrs_pol_state::PTLRPC_NRS_POL_STATE_STARTED state at the time the
 *   RPC was initialized, denoted it did not wish, or for some other reason was
 *   not able to handle the request, by returning a non-valid NRS resource
 *   reference.
 * - when the primary policy that was at the
 *   ptlrpc_nrs_pol_state::PTLRPC_NRS_POL_STATE_STARTED state at the time the
 *   RPC was initialized, fails later during the request enqueueing stage.
 *
 * \see nrs_resource_get_safe()
 * \see nrs_request_enqueue()
 */
struct ptlrpc_nrs {
	spinlock_t			nrs_lock;
	/** XXX Possibly replace svcpt->scp_req_lock with another lock here. */
	/**
	 * List of registered policies
	 */
	struct list_head		nrs_policy_list;
	/**
	 * List of policies with queued requests. Policies that have any
	 * outstanding requests are queued here, and this list is queried
	 * in a round-robin manner from NRS core when obtaining a request
	 * for handling. This ensures that requests from policies that at some
	 * point transition away from the
	 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STARTED state are drained.
	 */
	struct list_head		nrs_policy_queued;
	/**
	 * Service partition for this NRS head
	 */
	struct ptlrpc_service_part     *nrs_svcpt;
	/**
	 * Primary policy, which is the preferred policy for handling RPCs
	 */
	struct ptlrpc_nrs_policy       *nrs_policy_primary;
	/**
	 * Fallback policy, which is the backup policy for handling RPCs
	 */
	struct ptlrpc_nrs_policy       *nrs_policy_fallback;
	/**
	 * This NRS head handles either HP or regular requests
	 */
	enum ptlrpc_nrs_queue_type	nrs_queue_type;
	/**
	 * # queued requests from all policies in this NRS head
	 */
	unsigned long			nrs_req_queued;
	/**
	 * # scheduled requests from all policies in this NRS head
	 */
	unsigned long			nrs_req_started;
	/**
	 * # policies on this NRS
	 */
	unsigned int			nrs_num_pols;
	/**
	 * This NRS head is in progress of starting a policy
	 */
	unsigned int			nrs_policy_starting:1;
	/**
	 * In progress of shutting down the whole NRS head; used during
	 * unregistration
	 */
	unsigned int			nrs_stopping:1;
	/**
	 * NRS policy is throttling request
	 */
	unsigned int			nrs_throttling:1;
};

#define NRS_POL_NAME_MAX		16
#define NRS_POL_ARG_MAX			16

struct ptlrpc_nrs_pol_desc;

/**
 * Service compatibility predicate; this determines whether a policy is adequate
 * for handling RPCs of a particular PTLRPC service.
 *
 * XXX:This should give the same result during policy registration and
 * unregistration, and for all partitions of a service; so the result should not
 * depend on temporal service or other properties, that may influence the
 * result.
 */
typedef bool (*nrs_pol_desc_compat_t)(const struct ptlrpc_service *svc,
				      const struct ptlrpc_nrs_pol_desc *desc);

struct ptlrpc_nrs_pol_conf {
	/**
	 * Human-readable policy name
	 */
	char				   nc_name[NRS_POL_NAME_MAX];
	/**
	 * NRS operations for this policy
	 */
	const struct ptlrpc_nrs_pol_ops   *nc_ops;
	/**
	 * Service compatibility predicate
	 */
	nrs_pol_desc_compat_t		   nc_compat;
	/**
	 * Set for policies that support a single ptlrpc service, i.e. ones that
	 * have \a pd_compat set to nrs_policy_compat_one(). The variable value
	 * depicts the name of the single service that such policies are
	 * compatible with.
	 */
	const char			  *nc_compat_svc_name;
	/**
	 * Owner module for this policy descriptor; policies registering from a
	 * different module to the one the NRS framework is held within
	 * (currently ptlrpc), should set this field to THIS_MODULE.
	 */
	struct module			  *nc_owner;
	/**
	 * Policy registration flags; a bitmask of \e nrs_policy_flags
	 */
	unsigned int			   nc_flags;
};

/**
 * NRS policy registering descriptor
 *
 * Is used to hold a description of a policy that can be passed to NRS core in
 * order to register the policy with NRS heads in different PTLRPC services.
 */
struct ptlrpc_nrs_pol_desc {
	/**
	 * Human-readable policy name
	 */
	char					pd_name[NRS_POL_NAME_MAX];
	/**
	 * Link into nrs_core::nrs_policies
	 */
	struct list_head			pd_list;
	/**
	 * NRS operations for this policy
	 */
	const struct ptlrpc_nrs_pol_ops        *pd_ops;
	/**
	 * Service compatibility predicate
	 */
	nrs_pol_desc_compat_t			pd_compat;
	/**
	 * Set for policies that are compatible with only one PTLRPC service.
	 *
	 * \see ptlrpc_nrs_pol_conf::nc_compat_svc_name
	 */
	const char			       *pd_compat_svc_name;
	/**
	 * Owner module for this policy descriptor.
	 *
	 * We need to hold a reference to the module whenever we might make use
	 * of any of the module's contents, i.e.
	 * - If one or more instances of the policy are at a state where they
	 *   might be handling a request, i.e.
	 *   ptlrpc_nrs_pol_state::NRS_POL_STATE_STARTED or
	 *   ptlrpc_nrs_pol_state::NRS_POL_STATE_STOPPING as we will have to
	 *   call into the policy's ptlrpc_nrs_pol_ops() handlers. A reference
	 *   is taken on the module when
	 *   \e ptlrpc_nrs_pol_desc::pd_refs becomes 1, and released when it
	 *   becomes 0, so that we hold only one reference to the module maximum
	 *   at any time.
	 *
	 *   We do not need to hold a reference to the module, even though we
	 *   might use code and data from the module, in the following cases:
	 * - During external policy registration, because this should happen in
	 *   the module's init() function, in which case the module is safe from
	 *   removal because a reference is being held on the module by the
	 *   kernel, and iirc kmod (and I guess module-init-tools also) will
	 *   serialize any racing processes properly anyway.
	 * - During external policy unregistration, because this should happen
	 *   in a module's exit() function, and any attempts to start a policy
	 *   instance would need to take a reference on the module, and this is
	 *   not possible once we have reached the point where the exit()
	 *   handler is called.
	 * - During service registration and unregistration, as service setup
	 *   and cleanup, and policy registration, unregistration and policy
	 *   instance starting, are serialized by \e nrs_core::nrs_mutex, so
	 *   as long as users adhere to the convention of registering policies
	 *   in init() and unregistering them in module exit() functions, there
	 *   should not be a race between these operations.
	 * - During any policy-specific lprocfs operations, because a reference
	 *   is held by the kernel on a proc entry that has been entered by a
	 *   syscall, so as long as proc entries are removed during
	 *   unregistration time, then unregistration and lprocfs operations
	 *   will be properly serialized.
	 */
	struct module			       *pd_owner;
	/**
	 * Bitmask of \e nrs_policy_flags
	 */
	unsigned int				pd_flags;
	/**
	 * # of references on this descriptor
	 */
	atomic_t				pd_refs;
};

/**
 * NRS policy state
 *
 * Policies transition from one state to the other during their lifetime
 */
enum ptlrpc_nrs_pol_state {
	/**
	 * Not a valid policy state.
	 */
	NRS_POL_STATE_INVALID,
	/**
	 * Policies are at this state either at the start of their life, or
	 * transition here when the user selects a different policy to act
	 * as the primary one.
	 */
	NRS_POL_STATE_STOPPED,
	/**
	 * Policy is progress of stopping
	 */
	NRS_POL_STATE_STOPPING,
	/**
	 * Policy is in progress of starting
	 */
	NRS_POL_STATE_STARTING,
	/**
	 * A policy is in this state in two cases:
	 * - it is the fallback policy, which is always in this state.
	 * - it has been activated by the user; i.e. it is the primary policy,
	 */
	NRS_POL_STATE_STARTED,
};

/**
 * NRS policy information
 *
 * Used for obtaining information for the status of a policy via lprocfs
 */
struct ptlrpc_nrs_pol_info {
	/**
	 * Policy name
	 */
	char				pi_name[NRS_POL_NAME_MAX];
	/**
	 * Policy argument
	 */
	char				pi_arg[NRS_POL_ARG_MAX];
	/**
	 * Current policy state
	 */
	enum ptlrpc_nrs_pol_state	pi_state;
	/**
	 * # RPCs enqueued for later dispatching by the policy
	 */
	long				pi_req_queued;
	/**
	 * # RPCs started for dispatch by the policy
	 */
	long				pi_req_started;
	/**
	 * Is this a fallback policy?
	 */
	unsigned			pi_fallback:1;
};

/**
 * NRS policy
 *
 * There is one instance of this for each policy in each NRS head of each
 * PTLRPC service partition.
 */
struct ptlrpc_nrs_policy {
	/**
	 * Linkage into the NRS head's list of policies,
	 * ptlrpc_nrs:nrs_policy_list
	 */
	struct list_head		pol_list;
	/**
	 * Linkage into the NRS head's list of policies with enqueued
	 * requests ptlrpc_nrs:nrs_policy_queued
	 */
	struct list_head		pol_list_queued;
	/**
	 * Current state of this policy
	 */
	enum ptlrpc_nrs_pol_state	pol_state;
	/**
	 * Bitmask of nrs_policy_flags
	 */
	unsigned int			pol_flags;
	/**
	 * # RPCs enqueued for later dispatching by the policy
	 */
	long				pol_req_queued;
	/**
	 * # RPCs started for dispatch by the policy
	 */
	long				pol_req_started;
	/**
	 * Usage Reference count taken on the policy instance
	 */
	long				pol_ref;
	/**
	 * Human-readable policy argument
	 */
	char				pol_arg[NRS_POL_ARG_MAX];
	/**
	 * The NRS head this policy has been created at
	 */
	struct ptlrpc_nrs	       *pol_nrs;
	/**
	 * Private policy data; varies by policy type
	 */
	void			       *pol_private;
	/**
	 * Policy descriptor for this policy instance.
	 */
	struct ptlrpc_nrs_pol_desc     *pol_desc;
};

/**
 * NRS resource
 *
 * Resources are embedded into two types of NRS entities:
 * - Inside NRS policies, in the policy's private data in
 *   ptlrpc_nrs_policy::pol_private
 * - In objects that act as prime-level scheduling entities in different NRS
 *   policies; e.g. on a policy that performs round robin or similar order
 *   scheduling across client NIDs, there would be one NRS resource per unique
 *   client NID. On a policy which performs round robin scheduling across
 *   backend filesystem objects, there would be one resource associated with
 *   each of the backend filesystem objects partaking in the scheduling
 *   performed by the policy.
 *
 * NRS resources share a parent-child relationship, in which resources embedded
 * in policy instances are the parent entities, with all scheduling entities
 * a policy schedules across being the children, thus forming a simple resource
 * hierarchy. This hierarchy may be extended with one or more levels in the
 * future if the ability to have more than one primary policy is added.
 *
 * Upon request initialization, references to the then active NRS policies are
 * taken and used to later handle the dispatching of the request with one of
 * these policies.
 *
 * \see nrs_resource_get_safe()
 * \see ptlrpc_nrs_req_add()
 */
struct ptlrpc_nrs_resource {
	/**
	 * This NRS resource's parent; is NULL for resources embedded in NRS
	 * policy instances; i.e. those are top-level ones.
	 */
	struct ptlrpc_nrs_resource     *res_parent;
	/**
	 * The policy associated with this resource.
	 */
	struct ptlrpc_nrs_policy       *res_policy;
};

enum {
	NRS_RES_FALLBACK,
	NRS_RES_PRIMARY,
	NRS_RES_MAX
};

#include "lustre_nrs_fifo.h"

/**
 * NRS request
 *
 * Instances of this object exist embedded within ptlrpc_request; the main
 * purpose of this object is to hold references to the request's resources
 * for the lifetime of the request, and to hold properties that policies use
 * use for determining the request's scheduling priority.
 **/
struct ptlrpc_nrs_request {
	/**
	 * The request's resource hierarchy.
	 */
	struct ptlrpc_nrs_resource     *nr_res_ptrs[NRS_RES_MAX];
	/**
	 * Index into ptlrpc_nrs_request::nr_res_ptrs of the resource of the
	 * policy that was used to enqueue the request.
	 *
	 * \see nrs_request_enqueue()
	 */
	unsigned int			nr_res_idx;
	unsigned int			nr_initialized:1;
	unsigned int			nr_enqueued:1;
	unsigned int			nr_started:1;
	unsigned int			nr_finalized:1;

	/**
	 * Policy-specific fields, used for determining a request's scheduling
	 * priority, and other supporting functionality.
	 */
	union {
		/**
		 * Fields for the FIFO policy
		 */
		struct nrs_fifo_req	fifo;
	} nr_u;
	/**
	 * Externally-registering policies may want to use this to allocate
	 * their own request properties.
	 */
	void			       *ext;
};

/** @} nrs */
#endif
