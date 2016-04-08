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
 * Copyright (c) 2010, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
/** \defgroup PtlRPC Portal RPC and networking module.
 *
 * PortalRPC is the layer used by rest of lustre code to achieve network
 * communications: establish connections with corresponding export and import
 * states, listen for a service, send and receive RPCs.
 * PortalRPC also includes base recovery framework: packet resending and
 * replaying, reconnections, pinger.
 *
 * PortalRPC utilizes LNet as its transport layer.
 *
 * @{
 */

#ifndef _LUSTRE_NET_H
#define _LUSTRE_NET_H

/** \defgroup net net
 *
 * @{
 */

#include "../../include/linux/libcfs/libcfs.h"
#include "../../include/linux/lnet/nidstr.h"
#include "../../include/linux/lnet/api.h"
#include "lustre/lustre_idl.h"
#include "lustre_ha.h"
#include "lustre_sec.h"
#include "lustre_import.h"
#include "lprocfs_status.h"
#include "lu_object.h"
#include "lustre_req_layout.h"

#include "obd_support.h"
#include "lustre_ver.h"

/* MD flags we _always_ use */
#define PTLRPC_MD_OPTIONS  0

/**
 * Max # of bulk operations in one request.
 * In order for the client and server to properly negotiate the maximum
 * possible transfer size, PTLRPC_BULK_OPS_COUNT must be a power-of-two
 * value.  The client is free to limit the actual RPC size for any bulk
 * transfer via cl_max_pages_per_rpc to some non-power-of-two value.
 */
#define PTLRPC_BULK_OPS_BITS	2
#define PTLRPC_BULK_OPS_COUNT	(1U << PTLRPC_BULK_OPS_BITS)
/**
 * PTLRPC_BULK_OPS_MASK is for the convenience of the client only, and
 * should not be used on the server at all.  Otherwise, it imposes a
 * protocol limitation on the maximum RPC size that can be used by any
 * RPC sent to that server in the future.  Instead, the server should
 * use the negotiated per-client ocd_brw_size to determine the bulk
 * RPC count.
 */
#define PTLRPC_BULK_OPS_MASK	(~((__u64)PTLRPC_BULK_OPS_COUNT - 1))

/**
 * Define maxima for bulk I/O.
 *
 * A single PTLRPC BRW request is sent via up to PTLRPC_BULK_OPS_COUNT
 * of LNET_MTU sized RDMA transfers.  Clients and servers negotiate the
 * currently supported maximum between peers at connect via ocd_brw_size.
 */
#define PTLRPC_MAX_BRW_BITS	(LNET_MTU_BITS + PTLRPC_BULK_OPS_BITS)
#define PTLRPC_MAX_BRW_SIZE	(1 << PTLRPC_MAX_BRW_BITS)
#define PTLRPC_MAX_BRW_PAGES	(PTLRPC_MAX_BRW_SIZE >> PAGE_CACHE_SHIFT)

#define ONE_MB_BRW_SIZE		(1 << LNET_MTU_BITS)
#define MD_MAX_BRW_SIZE		(1 << LNET_MTU_BITS)
#define MD_MAX_BRW_PAGES	(MD_MAX_BRW_SIZE >> PAGE_CACHE_SHIFT)
#define DT_MAX_BRW_SIZE		PTLRPC_MAX_BRW_SIZE
#define DT_MAX_BRW_PAGES	(DT_MAX_BRW_SIZE >> PAGE_CACHE_SHIFT)
#define OFD_MAX_BRW_SIZE	(1 << LNET_MTU_BITS)

/* When PAGE_SIZE is a constant, we can check our arithmetic here with cpp! */
# if ((PTLRPC_MAX_BRW_PAGES & (PTLRPC_MAX_BRW_PAGES - 1)) != 0)
#  error "PTLRPC_MAX_BRW_PAGES isn't a power of two"
# endif
# if (PTLRPC_MAX_BRW_SIZE != (PTLRPC_MAX_BRW_PAGES * PAGE_CACHE_SIZE))
#  error "PTLRPC_MAX_BRW_SIZE isn't PTLRPC_MAX_BRW_PAGES * PAGE_CACHE_SIZE"
# endif
# if (PTLRPC_MAX_BRW_SIZE > LNET_MTU * PTLRPC_BULK_OPS_COUNT)
#  error "PTLRPC_MAX_BRW_SIZE too big"
# endif
# if (PTLRPC_MAX_BRW_PAGES > LNET_MAX_IOV * PTLRPC_BULK_OPS_COUNT)
#  error "PTLRPC_MAX_BRW_PAGES too big"
# endif

#define PTLRPC_NTHRS_INIT	2

/**
 * Buffer Constants
 *
 * Constants determine how memory is used to buffer incoming service requests.
 *
 * ?_NBUFS	      # buffers to allocate when growing the pool
 * ?_BUFSIZE	    # bytes in a single request buffer
 * ?_MAXREQSIZE	 # maximum request service will receive
 *
 * When fewer than ?_NBUFS/2 buffers are posted for receive, another chunk
 * of ?_NBUFS is added to the pool.
 *
 * Messages larger than ?_MAXREQSIZE are dropped.  Request buffers are
 * considered full when less than ?_MAXREQSIZE is left in them.
 */
/**
 * Thread Constants
 *
 * Constants determine how threads are created for ptlrpc service.
 *
 * ?_NTHRS_INIT		# threads to create for each service partition on
 *			  initializing. If it's non-affinity service and
 *			  there is only one partition, it's the overall #
 *			  threads for the service while initializing.
 * ?_NTHRS_BASE		# threads should be created at least for each
 *			  ptlrpc partition to keep the service healthy.
 *			  It's the low-water mark of threads upper-limit
 *			  for each partition.
 * ?_THR_FACTOR	 # threads can be added on threads upper-limit for
 *			  each CPU core. This factor is only for reference,
 *			  we might decrease value of factor if number of cores
 *			  per CPT is above a limit.
 * ?_NTHRS_MAX		# overall threads can be created for a service,
 *			  it's a soft limit because if service is running
 *			  on machine with hundreds of cores and tens of
 *			  CPU partitions, we need to guarantee each partition
 *			  has ?_NTHRS_BASE threads, which means total threads
 *			  will be ?_NTHRS_BASE * number_of_cpts which can
 *			  exceed ?_NTHRS_MAX.
 *
 * Examples
 *
 * #define MDS_NTHRS_INIT	2
 * #define MDS_NTHRS_BASE	64
 * #define MDS_NTHRS_FACTOR	8
 * #define MDS_NTHRS_MAX	1024
 *
 * Example 1):
 * ---------------------------------------------------------------------
 * Server(A) has 16 cores, user configured it to 4 partitions so each
 * partition has 4 cores, then actual number of service threads on each
 * partition is:
 *     MDS_NTHRS_BASE(64) + cores(4) * MDS_NTHRS_FACTOR(8) = 96
 *
 * Total number of threads for the service is:
 *     96 * partitions(4) = 384
 *
 * Example 2):
 * ---------------------------------------------------------------------
 * Server(B) has 32 cores, user configured it to 4 partitions so each
 * partition has 8 cores, then actual number of service threads on each
 * partition is:
 *     MDS_NTHRS_BASE(64) + cores(8) * MDS_NTHRS_FACTOR(8) = 128
 *
 * Total number of threads for the service is:
 *     128 * partitions(4) = 512
 *
 * Example 3):
 * ---------------------------------------------------------------------
 * Server(B) has 96 cores, user configured it to 8 partitions so each
 * partition has 12 cores, then actual number of service threads on each
 * partition is:
 *     MDS_NTHRS_BASE(64) + cores(12) * MDS_NTHRS_FACTOR(8) = 160
 *
 * Total number of threads for the service is:
 *     160 * partitions(8) = 1280
 *
 * However, it's above the soft limit MDS_NTHRS_MAX, so we choose this number
 * as upper limit of threads number for each partition:
 *     MDS_NTHRS_MAX(1024) / partitions(8) = 128
 *
 * Example 4):
 * ---------------------------------------------------------------------
 * Server(C) have a thousand of cores and user configured it to 32 partitions
 *     MDS_NTHRS_BASE(64) * 32 = 2048
 *
 * which is already above soft limit MDS_NTHRS_MAX(1024), but we still need
 * to guarantee that each partition has at least MDS_NTHRS_BASE(64) threads
 * to keep service healthy, so total number of threads will just be 2048.
 *
 * NB: we don't suggest to choose server with that many cores because backend
 *     filesystem itself, buffer cache, or underlying network stack might
 *     have some SMP scalability issues at that large scale.
 *
 *     If user already has a fat machine with hundreds or thousands of cores,
 *     there are two choices for configuration:
 *     a) create CPU table from subset of all CPUs and run Lustre on
 *	top of this subset
 *     b) bind service threads on a few partitions, see modparameters of
 *	MDS and OSS for details
*
 * NB: these calculations (and examples below) are simplified to help
 *     understanding, the real implementation is a little more complex,
 *     please see ptlrpc_server_nthreads_check() for details.
 *
 */

 /*
  * LDLM threads constants:
  *
  * Given 8 as factor and 24 as base threads number
  *
  * example 1)
  * On 4-core machine we will have 24 + 8 * 4 = 56 threads.
  *
  * example 2)
  * On 8-core machine with 2 partitions we will have 24 + 4 * 8 = 56
  * threads for each partition and total threads number will be 112.
  *
  * example 3)
  * On 64-core machine with 8 partitions we will need LDLM_NTHRS_BASE(24)
  * threads for each partition to keep service healthy, so total threads
  * number should be 24 * 8 = 192.
  *
  * So with these constants, threads number will be at the similar level
  * of old versions, unless target machine has over a hundred cores
  */
#define LDLM_THR_FACTOR		8
#define LDLM_NTHRS_INIT		PTLRPC_NTHRS_INIT
#define LDLM_NTHRS_BASE		24
#define LDLM_NTHRS_MAX		(num_online_cpus() == 1 ? 64 : 128)

#define LDLM_BL_THREADS   LDLM_NTHRS_AUTO_INIT
#define LDLM_CLIENT_NBUFS 1
#define LDLM_SERVER_NBUFS 64
#define LDLM_BUFSIZE      (8 * 1024)
#define LDLM_MAXREQSIZE   (5 * 1024)
#define LDLM_MAXREPSIZE   (1024)

#define MDS_MAXREQSIZE		(5 * 1024)	/* >= 4736 */

#define OST_MAXREQSIZE		(5 * 1024)

/* Macro to hide a typecast. */
#define ptlrpc_req_async_args(req) ((void *)&req->rq_async_args)

/**
 * Structure to single define portal connection.
 */
struct ptlrpc_connection {
	/** linkage for connections hash table */
	struct hlist_node	c_hash;
	/** Our own lnet nid for this connection */
	lnet_nid_t	      c_self;
	/** Remote side nid for this connection */
	lnet_process_id_t       c_peer;
	/** UUID of the other side */
	struct obd_uuid	 c_remote_uuid;
	/** reference counter for this connection */
	atomic_t	    c_refcount;
};

/** Client definition for PortalRPC */
struct ptlrpc_client {
	/** What lnet portal does this client send messages to by default */
	__u32		   cli_request_portal;
	/** What portal do we expect replies on */
	__u32		   cli_reply_portal;
	/** Name of the client */
	char		   *cli_name;
};

/** state flags of requests */
/* XXX only ones left are those used by the bulk descs as well! */
#define PTL_RPC_FL_INTR      (1 << 0)  /* reply wait was interrupted by user */
#define PTL_RPC_FL_TIMEOUT   (1 << 7)  /* request timed out waiting for reply */

#define REQ_MAX_ACK_LOCKS 8

union ptlrpc_async_args {
	/**
	 * Scratchpad for passing args to completion interpreter. Users
	 * cast to the struct of their choosing, and CLASSERT that this is
	 * big enough.  For _tons_ of context, kmalloc a struct and store
	 * a pointer to it here.  The pointer_arg ensures this struct is at
	 * least big enough for that.
	 */
	void      *pointer_arg[11];
	__u64      space[7];
};

struct ptlrpc_request_set;
typedef int (*set_interpreter_func)(struct ptlrpc_request_set *, void *, int);
typedef int (*set_producer_func)(struct ptlrpc_request_set *, void *);

/**
 * Definition of request set structure.
 * Request set is a list of requests (not necessary to the same target) that
 * once populated with RPCs could be sent in parallel.
 * There are two kinds of request sets. General purpose and with dedicated
 * serving thread. Example of the latter is ptlrpcd set.
 * For general purpose sets once request set started sending it is impossible
 * to add new requests to such set.
 * Provides a way to call "completion callbacks" when all requests in the set
 * returned.
 */
struct ptlrpc_request_set {
	atomic_t	  set_refcount;
	/** number of in queue requests */
	atomic_t	  set_new_count;
	/** number of uncompleted requests */
	atomic_t	  set_remaining;
	/** wait queue to wait on for request events */
	wait_queue_head_t	   set_waitq;
	wait_queue_head_t	  *set_wakeup_ptr;
	/** List of requests in the set */
	struct list_head	    set_requests;
	/**
	 * List of completion callbacks to be called when the set is completed
	 * This is only used if \a set_interpret is NULL.
	 * Links struct ptlrpc_set_cbdata.
	 */
	struct list_head	    set_cblist;
	/** Completion callback, if only one. */
	set_interpreter_func  set_interpret;
	/** opaq argument passed to completion \a set_interpret callback. */
	void		 *set_arg;
	/**
	 * Lock for \a set_new_requests manipulations
	 * locked so that any old caller can communicate requests to
	 * the set holder who can then fold them into the lock-free set
	 */
	spinlock_t		set_new_req_lock;
	/** List of new yet unsent requests. Only used with ptlrpcd now. */
	struct list_head	    set_new_requests;

	/** rq_status of requests that have been freed already */
	int		   set_rc;
	/** Additional fields used by the flow control extension */
	/** Maximum number of RPCs in flight */
	int		   set_max_inflight;
	/** Callback function used to generate RPCs */
	set_producer_func     set_producer;
	/** opaq argument passed to the producer callback */
	void		 *set_producer_arg;
};

/**
 * Description of a single ptrlrpc_set callback
 */
struct ptlrpc_set_cbdata {
	/** List linkage item */
	struct list_head	      psc_item;
	/** Pointer to interpreting function */
	set_interpreter_func    psc_interpret;
	/** Opaq argument to pass to the callback */
	void		   *psc_data;
};

struct ptlrpc_bulk_desc;
struct ptlrpc_service_part;
struct ptlrpc_service;

/**
 * ptlrpc callback & work item stuff
 */
struct ptlrpc_cb_id {
	void   (*cbid_fn)(lnet_event_t *ev);     /* specific callback fn */
	void    *cbid_arg;		      /* additional arg */
};

/** Maximum number of locks to fit into reply state */
#define RS_MAX_LOCKS 8
#define RS_DEBUG     0

/**
 * Structure to define reply state on the server
 * Reply state holds various reply message information. Also for "difficult"
 * replies (rep-ack case) we store the state after sending reply and wait
 * for the client to acknowledge the reception. In these cases locks could be
 * added to the state for replay/failover consistency guarantees.
 */
struct ptlrpc_reply_state {
	/** Callback description */
	struct ptlrpc_cb_id    rs_cb_id;
	/** Linkage for list of all reply states in a system */
	struct list_head	     rs_list;
	/** Linkage for list of all reply states on same export */
	struct list_head	     rs_exp_list;
	/** Linkage for list of all reply states for same obd */
	struct list_head	     rs_obd_list;
#if RS_DEBUG
	struct list_head	     rs_debug_list;
#endif
	/** A spinlock to protect the reply state flags */
	spinlock_t		rs_lock;
	/** Reply state flags */
	unsigned long	  rs_difficult:1; /* ACK/commit stuff */
	unsigned long	  rs_no_ack:1;    /* no ACK, even for
					   * difficult requests
					   */
	unsigned long	  rs_scheduled:1;     /* being handled? */
	unsigned long	  rs_scheduled_ever:1;/* any schedule attempts? */
	unsigned long	  rs_handled:1;  /* been handled yet? */
	unsigned long	  rs_on_net:1;   /* reply_out_callback pending? */
	unsigned long	  rs_prealloc:1; /* rs from prealloc list */
	unsigned long	  rs_committed:1;/* the transaction was committed
					  * and the rs was dispatched
					  */
	/** Size of the state */
	int		    rs_size;
	/** opcode */
	__u32		  rs_opc;
	/** Transaction number */
	__u64		  rs_transno;
	/** xid */
	__u64		  rs_xid;
	struct obd_export     *rs_export;
	struct ptlrpc_service_part *rs_svcpt;
	/** Lnet metadata handle for the reply */
	lnet_handle_md_t       rs_md_h;
	atomic_t	   rs_refcount;

	/** Context for the service thread */
	struct ptlrpc_svc_ctx *rs_svc_ctx;
	/** Reply buffer (actually sent to the client), encoded if needed */
	struct lustre_msg     *rs_repbuf;       /* wrapper */
	/** Size of the reply buffer */
	int		    rs_repbuf_len;   /* wrapper buf length */
	/** Size of the reply message */
	int		    rs_repdata_len;  /* wrapper msg length */
	/**
	 * Actual reply message. Its content is encrypted (if needed) to
	 * produce reply buffer for actual sending. In simple case
	 * of no network encryption we just set \a rs_repbuf to \a rs_msg
	 */
	struct lustre_msg     *rs_msg;	  /* reply message */

	/** Number of locks awaiting client ACK */
	int		    rs_nlocks;
	/** Handles of locks awaiting client reply ACK */
	struct lustre_handle   rs_locks[RS_MAX_LOCKS];
	/** Lock modes of locks in \a rs_locks */
	enum ldlm_mode	    rs_modes[RS_MAX_LOCKS];
};

struct ptlrpc_thread;

/** RPC stages */
enum rq_phase {
	RQ_PHASE_NEW	    = 0xebc0de00,
	RQ_PHASE_RPC	    = 0xebc0de01,
	RQ_PHASE_BULK	   = 0xebc0de02,
	RQ_PHASE_INTERPRET      = 0xebc0de03,
	RQ_PHASE_COMPLETE       = 0xebc0de04,
	RQ_PHASE_UNREGISTERING  = 0xebc0de05,
	RQ_PHASE_UNDEFINED      = 0xebc0de06
};

/** Type of request interpreter call-back */
typedef int (*ptlrpc_interpterer_t)(const struct lu_env *env,
				    struct ptlrpc_request *req,
				    void *arg, int rc);

/**
 * Definition of request pool structure.
 * The pool is used to store empty preallocated requests for the case
 * when we would actually need to send something without performing
 * any allocations (to avoid e.g. OOM).
 */
struct ptlrpc_request_pool {
	/** Locks the list */
	spinlock_t prp_lock;
	/** list of ptlrpc_request structs */
	struct list_head prp_req_list;
	/** Maximum message size that would fit into a request from this pool */
	int prp_rq_size;
	/** Function to allocate more requests for this pool */
	int (*prp_populate)(struct ptlrpc_request_pool *, int);
};

struct lu_context;
struct lu_env;

struct ldlm_lock;

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
 * ORR policy operations
 */
enum nrs_ctl_orr {
	NRS_CTL_ORR_RD_QUANTUM = PTLRPC_NRS_CTL_1ST_POL_SPEC,
	NRS_CTL_ORR_WR_QUANTUM,
	NRS_CTL_ORR_RD_OFF_TYPE,
	NRS_CTL_ORR_WR_OFF_TYPE,
	NRS_CTL_ORR_RD_SUPP_REQ,
	NRS_CTL_ORR_WR_SUPP_REQ,
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
	int	(*op_policy_init) (struct ptlrpc_nrs_policy *policy);
	/**
	 * Called during policy unregistration; this operation is optional.
	 *
	 * \param[in,out] policy The policy being unregistered/finalized
	 */
	void	(*op_policy_fini) (struct ptlrpc_nrs_policy *policy);
	/**
	 * Called when activating a policy via lprocfs; policies allocate and
	 * initialize their resources here; this operation is optional.
	 *
	 * \param[in,out] policy The policy being started
	 *
	 * \see nrs_policy_start_locked()
	 */
	int	(*op_policy_start) (struct ptlrpc_nrs_policy *policy);
	/**
	 * Called when deactivating a policy via lprocfs; policies deallocate
	 * their resources here; this operation is optional
	 *
	 * \param[in,out] policy The policy being stopped
	 *
	 * \see nrs_policy_stop0()
	 */
	void	(*op_policy_stop) (struct ptlrpc_nrs_policy *policy);
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
	int	(*op_policy_ctl) (struct ptlrpc_nrs_policy *policy,
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
	 */
	int	(*op_res_get) (struct ptlrpc_nrs_policy *policy,
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
	 */
	void	(*op_res_put) (struct ptlrpc_nrs_policy *policy,
			       const struct ptlrpc_nrs_resource *res);

	/**
	 * Obtains a request for handling from the policy, and optionally
	 * removes the request from the policy; this operation is mandatory.
	 *
	 * \param[in,out] policy The policy to poll
	 * \param[in]	  peek	 When set, signifies that we just want to
	 *			 examine the request, and not handle it, so the
	 *			 request is not removed from the policy.
	 * \param[in]	  force	 When set, it will force a policy to return a
	 *			 request if it has one queued.
	 *
	 * \retval NULL No request available for handling
	 * \retval valid-pointer The request polled for handling
	 *
	 * \see ptlrpc_nrs_req_get_nolock()
	 */
	struct ptlrpc_nrs_request *
		(*op_req_get) (struct ptlrpc_nrs_policy *policy, bool peek,
			       bool force);
	/**
	 * Called when attempting to add a request to a policy for later
	 * handling; this operation is mandatory.
	 *
	 * \param[in,out] policy  The policy on which to enqueue \a nrq
	 * \param[in,out] nrq The request to enqueue
	 *
	 * \retval 0	success
	 * \retval != 0	error
	 *
	 * \see ptlrpc_nrs_req_add_nolock()
	 */
	int	(*op_req_enqueue) (struct ptlrpc_nrs_policy *policy,
				   struct ptlrpc_nrs_request *nrq);
	/**
	 * Removes a request from the policy's set of pending requests. Normally
	 * called after a request has been polled successfully from the policy
	 * for handling; this operation is mandatory.
	 *
	 * \param[in,out] policy The policy the request \a nrq belongs to
	 * \param[in,out] nrq    The request to dequeue
	 */
	void	(*op_req_dequeue) (struct ptlrpc_nrs_policy *policy,
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
	void	(*op_req_stop) (struct ptlrpc_nrs_policy *policy,
				struct ptlrpc_nrs_request *nrq);
	/**
	 * Registers the policy's lprocfs interface with a PTLRPC service.
	 *
	 * \param[in] svc The service
	 *
	 * \retval 0	success
	 * \retval != 0	error
	 */
	int	(*op_lprocfs_init) (struct ptlrpc_service *svc);
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
	void	(*op_lprocfs_fini) (struct ptlrpc_service *svc);
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
	PTLRPC_NRS_FL_FALLBACK		= (1 << 0),
	/**
	 * Start policy immediately after registering.
	 */
	PTLRPC_NRS_FL_REG_START		= (1 << 1),
	/**
	 * This is a policy registering from a module different to the one NRS
	 * core ships in (currently ptlrpc).
	 */
	PTLRPC_NRS_FL_REG_EXTERN	= (1 << 2),
};

/**
 * NRS queue type.
 *
 * Denotes whether an NRS instance is for handling normal or high-priority
 * RPCs, or whether an operation pertains to one or both of the NRS instances
 * in a service.
 */
enum ptlrpc_nrs_queue_type {
	PTLRPC_NRS_QUEUE_REG	= (1 << 0),
	PTLRPC_NRS_QUEUE_HP	= (1 << 1),
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
	struct list_head			nrs_policy_list;
	/**
	 * List of policies with queued requests. Policies that have any
	 * outstanding requests are queued here, and this list is queried
	 * in a round-robin manner from NRS core when obtaining a request
	 * for handling. This ensures that requests from policies that at some
	 * point transition away from the
	 * ptlrpc_nrs_pol_state::NRS_POL_STATE_STARTED state are drained.
	 */
	struct list_head			nrs_policy_queued;
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
	unsigned			nrs_num_pols;
	/**
	 * This NRS head is in progress of starting a policy
	 */
	unsigned			nrs_policy_starting:1;
	/**
	 * In progress of shutting down the whole NRS head; used during
	 * unregistration
	 */
	unsigned			nrs_stopping:1;
};

#define NRS_POL_NAME_MAX		16

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
typedef bool (*nrs_pol_desc_compat_t) (const struct ptlrpc_service *svc,
				       const struct ptlrpc_nrs_pol_desc *desc);

struct ptlrpc_nrs_pol_conf {
	/**
	 * Human-readable policy name
	 */
	char				   nc_name[NRS_POL_NAME_MAX];
	/**
	 * NRS operations for this policy
	 */
	const struct ptlrpc_nrs_pol_ops	  *nc_ops;
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
	unsigned			   nc_flags;
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
	struct list_head				pd_list;
	/**
	 * NRS operations for this policy
	 */
	const struct ptlrpc_nrs_pol_ops	       *pd_ops;
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
	 *   syscall, so as long as proc entries are removed during unregistration time,
	 *   then unregistration and lprocfs operations will be properly
	 *   serialized.
	 */
	struct module			       *pd_owner;
	/**
	 * Bitmask of \e nrs_policy_flags
	 */
	unsigned				pd_flags;
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
	struct list_head			pol_list;
	/**
	 * Linkage into the NRS head's list of policies with enqueued
	 * requests ptlrpc_nrs:nrs_policy_queued
	 */
	struct list_head			pol_list_queued;
	/**
	 * Current state of this policy
	 */
	enum ptlrpc_nrs_pol_state	pol_state;
	/**
	 * Bitmask of nrs_policy_flags
	 */
	unsigned			pol_flags;
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

/* \name fifo
 *
 * FIFO policy
 *
 * This policy is a logical wrapper around previous, non-NRS functionality.
 * It dispatches RPCs in the same order as they arrive from the network. This
 * policy is currently used as the fallback policy, and the only enabled policy
 * on all NRS heads of all PTLRPC service partitions.
 * @{
 */

/**
 * Private data structure for the FIFO policy
 */
struct nrs_fifo_head {
	/**
	 * Resource object for policy instance.
	 */
	struct ptlrpc_nrs_resource	fh_res;
	/**
	 * List of queued requests.
	 */
	struct list_head			fh_list;
	/**
	 * For debugging purposes.
	 */
	__u64				fh_sequence;
};

struct nrs_fifo_req {
	struct list_head		fr_list;
	__u64			fr_sequence;
};

/** @} fifo */

/**
 * NRS request
 *
 * Instances of this object exist embedded within ptlrpc_request; the main
 * purpose of this object is to hold references to the request's resources
 * for the lifetime of the request, and to hold properties that policies use
 * use for determining the request's scheduling priority.
 */
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
	unsigned			nr_res_idx;
	unsigned			nr_initialized:1;
	unsigned			nr_enqueued:1;
	unsigned			nr_started:1;
	unsigned			nr_finalized:1;

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

/**
 * Basic request prioritization operations structure.
 * The whole idea is centered around locks and RPCs that might affect locks.
 * When a lock is contended we try to give priority to RPCs that might lead
 * to fastest release of that lock.
 * Currently only implemented for OSTs only in a way that makes all
 * IO and truncate RPCs that are coming from a locked region where a lock is
 * contended a priority over other requests.
 */
struct ptlrpc_hpreq_ops {
	/**
	 * Check if the lock handle of the given lock is the same as
	 * taken from the request.
	 */
	int  (*hpreq_lock_match)(struct ptlrpc_request *, struct ldlm_lock *);
	/**
	 * Check if the request is a high priority one.
	 */
	int  (*hpreq_check)(struct ptlrpc_request *);
	/**
	 * Called after the request has been handled.
	 */
	void (*hpreq_fini)(struct ptlrpc_request *);
};

/**
 * Represents remote procedure call.
 *
 * This is a staple structure used by everybody wanting to send a request
 * in Lustre.
 */
struct ptlrpc_request {
	/* Request type: one of PTL_RPC_MSG_* */
	int rq_type;
	/** Result of request processing */
	int rq_status;
	/**
	 * Linkage item through which this request is included into
	 * sending/delayed lists on client and into rqbd list on server
	 */
	struct list_head rq_list;
	/**
	 * Server side list of incoming unserved requests sorted by arrival
	 * time.  Traversed from time to time to notice about to expire
	 * requests and sent back "early replies" to clients to let them
	 * know server is alive and well, just very busy to service their
	 * requests in time
	 */
	struct list_head rq_timed_list;
	/** server-side history, used for debugging purposes. */
	struct list_head rq_history_list;
	/** server-side per-export list */
	struct list_head rq_exp_list;
	/** server-side hp handlers */
	struct ptlrpc_hpreq_ops *rq_ops;

	/** initial thread servicing this request */
	struct ptlrpc_thread *rq_svc_thread;

	/** history sequence # */
	__u64 rq_history_seq;
	/** \addtogroup  nrs
	 * @{
	 */
	/** stub for NRS request */
	struct ptlrpc_nrs_request rq_nrq;
	/** @} nrs */
	/** the index of service's srv_at_array into which request is linked */
	u32 rq_at_index;
	/** Lock to protect request flags and some other important bits, like
	 * rq_list
	 */
	spinlock_t rq_lock;
	/** client-side flags are serialized by rq_lock */
	unsigned int rq_intr:1, rq_replied:1, rq_err:1,
		rq_timedout:1, rq_resend:1, rq_restart:1,
		/**
		 * when ->rq_replay is set, request is kept by the client even
		 * after server commits corresponding transaction. This is
		 * used for operations that require sequence of multiple
		 * requests to be replayed. The only example currently is file
		 * open/close. When last request in such a sequence is
		 * committed, ->rq_replay is cleared on all requests in the
		 * sequence.
		 */
		rq_replay:1,
		rq_no_resend:1, rq_waiting:1, rq_receiving_reply:1,
		rq_no_delay:1, rq_net_err:1, rq_wait_ctx:1,
		rq_early:1,
		rq_req_unlink:1, rq_reply_unlink:1,
		rq_memalloc:1,      /* req originated from "kswapd" */
		/* server-side flags */
		rq_packed_final:1,  /* packed final reply */
		rq_hp:1,	    /* high priority RPC */
		rq_at_linked:1,     /* link into service's srv_at_array */
		rq_reply_truncate:1,
		rq_committed:1,
		/* whether the "rq_set" is a valid one */
		rq_invalid_rqset:1,
		rq_generation_set:1,
		/* do not resend request on -EINPROGRESS */
		rq_no_retry_einprogress:1,
		/* allow the req to be sent if the import is in recovery
		 * status
		 */
		rq_allow_replay:1;

	unsigned int rq_nr_resend;

	enum rq_phase rq_phase; /* one of RQ_PHASE_* */
	enum rq_phase rq_next_phase; /* one of RQ_PHASE_* to be used next */
	atomic_t rq_refcount; /* client-side refcount for SENT race,
			       * server-side refcount for multiple replies
			       */

	/** Portal to which this request would be sent */
	short rq_request_portal;  /* XXX FIXME bug 249 */
	/** Portal where to wait for reply and where reply would be sent */
	short rq_reply_portal;    /* XXX FIXME bug 249 */

	/**
	 * client-side:
	 * !rq_truncate : # reply bytes actually received,
	 *  rq_truncate : required repbuf_len for resend
	 */
	int rq_nob_received;
	/** Request length */
	int rq_reqlen;
	/** Reply length */
	int rq_replen;
	/** Request message - what client sent */
	struct lustre_msg *rq_reqmsg;
	/** Reply message - server response */
	struct lustre_msg *rq_repmsg;
	/** Transaction number */
	__u64 rq_transno;
	/** xid */
	__u64 rq_xid;
	/**
	 * List item to for replay list. Not yet committed requests get linked
	 * there.
	 * Also see \a rq_replay comment above.
	 */
	struct list_head rq_replay_list;

	/**
	 * security and encryption data
	 * @{
	 */
	struct ptlrpc_cli_ctx   *rq_cli_ctx;     /**< client's half ctx */
	struct ptlrpc_svc_ctx   *rq_svc_ctx;     /**< server's half ctx */
	struct list_head	       rq_ctx_chain;   /**< link to waited ctx */

	struct sptlrpc_flavor    rq_flvr;	/**< for client & server */
	enum lustre_sec_part     rq_sp_from;

	/* client/server security flags */
	unsigned int
				 rq_ctx_init:1,      /* context initiation */
				 rq_ctx_fini:1,      /* context destroy */
				 rq_bulk_read:1,     /* request bulk read */
				 rq_bulk_write:1,    /* request bulk write */
				 /* server authentication flags */
				 rq_auth_gss:1,      /* authenticated by gss */
				 rq_auth_remote:1,   /* authed as remote user */
				 rq_auth_usr_root:1, /* authed as root */
				 rq_auth_usr_mdt:1,  /* authed as mdt */
				 rq_auth_usr_ost:1,  /* authed as ost */
				 /* security tfm flags */
				 rq_pack_udesc:1,
				 rq_pack_bulk:1,
				 /* doesn't expect reply FIXME */
				 rq_no_reply:1,
				 rq_pill_init:1;     /* pill initialized */

	uid_t		    rq_auth_uid;	/* authed uid */
	uid_t		    rq_auth_mapped_uid; /* authed uid mapped to */

	/* (server side), pointed directly into req buffer */
	struct ptlrpc_user_desc *rq_user_desc;

	/* various buffer pointers */
	struct lustre_msg       *rq_reqbuf;      /* req wrapper */
	char		    *rq_repbuf;      /* rep buffer */
	struct lustre_msg       *rq_repdata;     /* rep wrapper msg */
	struct lustre_msg       *rq_clrbuf;      /* only in priv mode */
	int		      rq_reqbuf_len;  /* req wrapper buf len */
	int		      rq_reqdata_len; /* req wrapper msg len */
	int		      rq_repbuf_len;  /* rep buffer len */
	int		      rq_repdata_len; /* rep wrapper msg len */
	int		      rq_clrbuf_len;  /* only in priv mode */
	int		      rq_clrdata_len; /* only in priv mode */

	/** early replies go to offset 0, regular replies go after that */
	unsigned int	     rq_reply_off;

	/** @} */

	/** Fields that help to see if request and reply were swabbed or not */
	__u32 rq_req_swab_mask;
	__u32 rq_rep_swab_mask;

	/** What was import generation when this request was sent */
	int rq_import_generation;
	enum lustre_imp_state rq_send_state;

	/** how many early replies (for stats) */
	int rq_early_count;

	/** client+server request */
	lnet_handle_md_t     rq_req_md_h;
	struct ptlrpc_cb_id  rq_req_cbid;
	/** optional time limit for send attempts */
	long       rq_delay_limit;
	/** time request was first queued */
	unsigned long	   rq_queued_time;

	/* server-side... */
	/** request arrival time */
	struct timespec64	rq_arrival_time;
	/** separated reply state */
	struct ptlrpc_reply_state *rq_reply_state;
	/** incoming request buffer */
	struct ptlrpc_request_buffer_desc *rq_rqbd;

	/** client-only incoming reply */
	lnet_handle_md_t     rq_reply_md_h;
	wait_queue_head_t	  rq_reply_waitq;
	struct ptlrpc_cb_id  rq_reply_cbid;

	/** our LNet NID */
	lnet_nid_t	   rq_self;
	/** Peer description (the other side) */
	lnet_process_id_t    rq_peer;
	/** Server-side, export on which request was received */
	struct obd_export   *rq_export;
	/** Client side, import where request is being sent */
	struct obd_import   *rq_import;

	/** Replay callback, called after request is replayed at recovery */
	void (*rq_replay_cb)(struct ptlrpc_request *);
	/**
	 * Commit callback, called when request is committed and about to be
	 * freed.
	 */
	void (*rq_commit_cb)(struct ptlrpc_request *);
	/** Opaq data for replay and commit callbacks. */
	void  *rq_cb_data;

	/** For bulk requests on client only: bulk descriptor */
	struct ptlrpc_bulk_desc *rq_bulk;

	/** client outgoing req */
	/**
	 * when request/reply sent (secs), or time when request should be sent
	 */
	time64_t rq_sent;
	/** time for request really sent out */
	time64_t rq_real_sent;

	/** when request must finish. volatile
	 * so that servers' early reply updates to the deadline aren't
	 * kept in per-cpu cache
	 */
	volatile time64_t rq_deadline;
	/** when req reply unlink must finish. */
	time64_t rq_reply_deadline;
	/** when req bulk unlink must finish. */
	time64_t rq_bulk_deadline;
	/**
	 * service time estimate (secs)
	 * If the requestsis not served by this time, it is marked as timed out.
	 */
	int    rq_timeout;

	/** Multi-rpc bits */
	/** Per-request waitq introduced by bug 21938 for recovery waiting */
	wait_queue_head_t rq_set_waitq;
	/** Link item for request set lists */
	struct list_head  rq_set_chain;
	/** Link back to the request set */
	struct ptlrpc_request_set *rq_set;
	/** Async completion handler, called when reply is received */
	ptlrpc_interpterer_t rq_interpret_reply;
	/** Async completion context */
	union ptlrpc_async_args rq_async_args;

	/** Pool if request is from preallocated list */
	struct ptlrpc_request_pool *rq_pool;

	struct lu_context	   rq_session;
	struct lu_context	   rq_recov_session;

	/** request format description */
	struct req_capsule	  rq_pill;
};

/**
 * Call completion handler for rpc if any, return it's status or original
 * rc if there was no handler defined for this request.
 */
static inline int ptlrpc_req_interpret(const struct lu_env *env,
				       struct ptlrpc_request *req, int rc)
{
	if (req->rq_interpret_reply) {
		req->rq_status = req->rq_interpret_reply(env, req,
							 &req->rq_async_args,
							 rc);
		return req->rq_status;
	}
	return rc;
}

/*
 * Can the request be moved from the regular NRS head to the high-priority NRS
 * head (of the same PTLRPC service partition), if any?
 *
 * For a reliable result, this should be checked under svcpt->scp_req lock.
 */
static inline bool ptlrpc_nrs_req_can_move(struct ptlrpc_request *req)
{
	struct ptlrpc_nrs_request *nrq = &req->rq_nrq;

	/**
	 * LU-898: Check ptlrpc_nrs_request::nr_enqueued to make sure the
	 * request has been enqueued first, and ptlrpc_nrs_request::nr_started
	 * to make sure it has not been scheduled yet (analogous to previous
	 * (non-NRS) checking of !list_empty(&ptlrpc_request::rq_list).
	 */
	return nrq->nr_enqueued && !nrq->nr_started && !req->rq_hp;
}

/** @} nrs */

/**
 * Returns 1 if request buffer at offset \a index was already swabbed
 */
static inline int lustre_req_swabbed(struct ptlrpc_request *req, int index)
{
	LASSERT(index < sizeof(req->rq_req_swab_mask) * 8);
	return req->rq_req_swab_mask & (1 << index);
}

/**
 * Returns 1 if request reply buffer at offset \a index was already swabbed
 */
static inline int lustre_rep_swabbed(struct ptlrpc_request *req, int index)
{
	LASSERT(index < sizeof(req->rq_rep_swab_mask) * 8);
	return req->rq_rep_swab_mask & (1 << index);
}

/**
 * Returns 1 if request needs to be swabbed into local cpu byteorder
 */
static inline int ptlrpc_req_need_swab(struct ptlrpc_request *req)
{
	return lustre_req_swabbed(req, MSG_PTLRPC_HEADER_OFF);
}

/**
 * Returns 1 if request reply needs to be swabbed into local cpu byteorder
 */
static inline int ptlrpc_rep_need_swab(struct ptlrpc_request *req)
{
	return lustre_rep_swabbed(req, MSG_PTLRPC_HEADER_OFF);
}

/**
 * Mark request buffer at offset \a index that it was already swabbed
 */
static inline void lustre_set_req_swabbed(struct ptlrpc_request *req, int index)
{
	LASSERT(index < sizeof(req->rq_req_swab_mask) * 8);
	LASSERT((req->rq_req_swab_mask & (1 << index)) == 0);
	req->rq_req_swab_mask |= 1 << index;
}

/**
 * Mark request reply buffer at offset \a index that it was already swabbed
 */
static inline void lustre_set_rep_swabbed(struct ptlrpc_request *req, int index)
{
	LASSERT(index < sizeof(req->rq_rep_swab_mask) * 8);
	LASSERT((req->rq_rep_swab_mask & (1 << index)) == 0);
	req->rq_rep_swab_mask |= 1 << index;
}

/**
 * Convert numerical request phase value \a phase into text string description
 */
static inline const char *
ptlrpc_phase2str(enum rq_phase phase)
{
	switch (phase) {
	case RQ_PHASE_NEW:
		return "New";
	case RQ_PHASE_RPC:
		return "Rpc";
	case RQ_PHASE_BULK:
		return "Bulk";
	case RQ_PHASE_INTERPRET:
		return "Interpret";
	case RQ_PHASE_COMPLETE:
		return "Complete";
	case RQ_PHASE_UNREGISTERING:
		return "Unregistering";
	default:
		return "?Phase?";
	}
}

/**
 * Convert numerical request phase of the request \a req into text stringi
 * description
 */
static inline const char *
ptlrpc_rqphase2str(struct ptlrpc_request *req)
{
	return ptlrpc_phase2str(req->rq_phase);
}

/**
 * Debugging functions and helpers to print request structure into debug log
 * @{
 */
/* Spare the preprocessor, spoil the bugs. */
#define FLAG(field, str) (field ? str : "")

/** Convert bit flags into a string */
#define DEBUG_REQ_FLAGS(req)						    \
	ptlrpc_rqphase2str(req),						\
	FLAG(req->rq_intr, "I"), FLAG(req->rq_replied, "R"),		    \
	FLAG(req->rq_err, "E"),						 \
	FLAG(req->rq_timedout, "X") /* eXpired */, FLAG(req->rq_resend, "S"),   \
	FLAG(req->rq_restart, "T"), FLAG(req->rq_replay, "P"),		  \
	FLAG(req->rq_no_resend, "N"),					   \
	FLAG(req->rq_waiting, "W"),					     \
	FLAG(req->rq_wait_ctx, "C"), FLAG(req->rq_hp, "H"),		     \
	FLAG(req->rq_committed, "M")

#define REQ_FLAGS_FMT "%s:%s%s%s%s%s%s%s%s%s%s%s%s"

void _debug_req(struct ptlrpc_request *req,
		struct libcfs_debug_msg_data *data, const char *fmt, ...)
	__printf(3, 4);

/**
 * Helper that decides if we need to print request according to current debug
 * level settings
 */
#define debug_req(msgdata, mask, cdls, req, fmt, a...)			\
do {									  \
	CFS_CHECK_STACK(msgdata, mask, cdls);				 \
									      \
	if (((mask) & D_CANTMASK) != 0 ||				     \
	    ((libcfs_debug & (mask)) != 0 &&				  \
	     (libcfs_subsystem_debug & DEBUG_SUBSYSTEM) != 0))		\
		_debug_req((req), msgdata, fmt, ##a);			 \
} while (0)

/**
 * This is the debug print function you need to use to print request structure
 * content into lustre debug log.
 * for most callers (level is a constant) this is resolved at compile time
 */
#define DEBUG_REQ(level, req, fmt, args...)				   \
do {									  \
	if ((level) & (D_ERROR | D_WARNING)) {				\
		static struct cfs_debug_limit_state cdls;			  \
		LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, level, &cdls);	    \
		debug_req(&msgdata, level, &cdls, req, "@@@ "fmt" ", ## args);\
	} else {							      \
		LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, level, NULL);	     \
		debug_req(&msgdata, level, NULL, req, "@@@ "fmt" ", ## args); \
	}								     \
} while (0)
/** @} */

/**
 * Structure that defines a single page of a bulk transfer
 */
struct ptlrpc_bulk_page {
	/** Linkage to list of pages in a bulk */
	struct list_head       bp_link;
	/**
	 * Number of bytes in a page to transfer starting from \a bp_pageoffset
	 */
	int	      bp_buflen;
	/** offset within a page */
	int	      bp_pageoffset;
	/** The page itself */
	struct page     *bp_page;
};

#define BULK_GET_SOURCE   0
#define BULK_PUT_SINK     1
#define BULK_GET_SINK     2
#define BULK_PUT_SOURCE   3

/**
 * Definition of bulk descriptor.
 * Bulks are special "Two phase" RPCs where initial request message
 * is sent first and it is followed bt a transfer (o receiving) of a large
 * amount of data to be settled into pages referenced from the bulk descriptors.
 * Bulks transfers (the actual data following the small requests) are done
 * on separate LNet portals.
 * In lustre we use bulk transfers for READ and WRITE transfers from/to OSTs.
 *  Another user is readpage for MDT.
 */
struct ptlrpc_bulk_desc {
	/** completed with failure */
	unsigned long bd_failure:1;
	/** {put,get}{source,sink} */
	unsigned long bd_type:2;
	/** client side */
	unsigned long bd_registered:1;
	/** For serialization with callback */
	spinlock_t bd_lock;
	/** Import generation when request for this bulk was sent */
	int bd_import_generation;
	/** LNet portal for this bulk */
	__u32 bd_portal;
	/** Server side - export this bulk created for */
	struct obd_export *bd_export;
	/** Client side - import this bulk was sent on */
	struct obd_import *bd_import;
	/** Back pointer to the request */
	struct ptlrpc_request *bd_req;
	wait_queue_head_t	    bd_waitq;	/* server side only WQ */
	int		    bd_iov_count;    /* # entries in bd_iov */
	int		    bd_max_iov;      /* allocated size of bd_iov */
	int		    bd_nob;	  /* # bytes covered */
	int		    bd_nob_transferred; /* # bytes GOT/PUT */

	__u64		  bd_last_xid;

	struct ptlrpc_cb_id    bd_cbid;	 /* network callback info */
	lnet_nid_t	     bd_sender;       /* stash event::sender */
	int			bd_md_count;	/* # valid entries in bd_mds */
	int			bd_md_max_brw;	/* max entries in bd_mds */
	/** array of associated MDs */
	lnet_handle_md_t	bd_mds[PTLRPC_BULK_OPS_COUNT];

	/*
	 * encrypt iov, size is either 0 or bd_iov_count.
	 */
	lnet_kiov_t	   *bd_enc_iov;

	lnet_kiov_t	    bd_iov[0];
};

enum {
	SVC_STOPPED     = 1 << 0,
	SVC_STOPPING    = 1 << 1,
	SVC_STARTING    = 1 << 2,
	SVC_RUNNING     = 1 << 3,
	SVC_EVENT       = 1 << 4,
	SVC_SIGNAL      = 1 << 5,
};

#define PTLRPC_THR_NAME_LEN		32
/**
 * Definition of server service thread structure
 */
struct ptlrpc_thread {
	/**
	 * List of active threads in svc->srv_threads
	 */
	struct list_head t_link;
	/**
	 * thread-private data (preallocated memory)
	 */
	void *t_data;
	__u32 t_flags;
	/**
	 * service thread index, from ptlrpc_start_threads
	 */
	unsigned int t_id;
	/**
	 * service thread pid
	 */
	pid_t t_pid;
	/**
	 * put watchdog in the structure per thread b=14840
	 *
	 * Lustre watchdog is removed for client in the hope
	 * of a generic watchdog can be merged in kernel.
	 * When that happens, we should add below back.
	 *
	 * struct lc_watchdog *t_watchdog;
	 */
	/**
	 * the svc this thread belonged to b=18582
	 */
	struct ptlrpc_service_part	*t_svcpt;
	wait_queue_head_t			t_ctl_waitq;
	struct lu_env			*t_env;
	char				t_name[PTLRPC_THR_NAME_LEN];
};

static inline int thread_is_init(struct ptlrpc_thread *thread)
{
	return thread->t_flags == 0;
}

static inline int thread_is_stopped(struct ptlrpc_thread *thread)
{
	return !!(thread->t_flags & SVC_STOPPED);
}

static inline int thread_is_stopping(struct ptlrpc_thread *thread)
{
	return !!(thread->t_flags & SVC_STOPPING);
}

static inline int thread_is_starting(struct ptlrpc_thread *thread)
{
	return !!(thread->t_flags & SVC_STARTING);
}

static inline int thread_is_running(struct ptlrpc_thread *thread)
{
	return !!(thread->t_flags & SVC_RUNNING);
}

static inline int thread_is_event(struct ptlrpc_thread *thread)
{
	return !!(thread->t_flags & SVC_EVENT);
}

static inline int thread_is_signal(struct ptlrpc_thread *thread)
{
	return !!(thread->t_flags & SVC_SIGNAL);
}

static inline void thread_clear_flags(struct ptlrpc_thread *thread, __u32 flags)
{
	thread->t_flags &= ~flags;
}

static inline void thread_set_flags(struct ptlrpc_thread *thread, __u32 flags)
{
	thread->t_flags = flags;
}

static inline void thread_add_flags(struct ptlrpc_thread *thread, __u32 flags)
{
	thread->t_flags |= flags;
}

static inline int thread_test_and_clear_flags(struct ptlrpc_thread *thread,
					      __u32 flags)
{
	if (thread->t_flags & flags) {
		thread->t_flags &= ~flags;
		return 1;
	}
	return 0;
}

/**
 * Request buffer descriptor structure.
 * This is a structure that contains one posted request buffer for service.
 * Once data land into a buffer, event callback creates actual request and
 * notifies wakes one of the service threads to process new incoming request.
 * More than one request can fit into the buffer.
 */
struct ptlrpc_request_buffer_desc {
	/** Link item for rqbds on a service */
	struct list_head	     rqbd_list;
	/** History of requests for this buffer */
	struct list_head	     rqbd_reqs;
	/** Back pointer to service for which this buffer is registered */
	struct ptlrpc_service_part *rqbd_svcpt;
	/** LNet descriptor */
	lnet_handle_md_t       rqbd_md_h;
	int		    rqbd_refcount;
	/** The buffer itself */
	char		  *rqbd_buffer;
	struct ptlrpc_cb_id    rqbd_cbid;
	/**
	 * This "embedded" request structure is only used for the
	 * last request to fit into the buffer
	 */
	struct ptlrpc_request  rqbd_req;
};

typedef int  (*svc_handler_t)(struct ptlrpc_request *req);

struct ptlrpc_service_ops {
	/**
	 * if non-NULL called during thread creation (ptlrpc_start_thread())
	 * to initialize service specific per-thread state.
	 */
	int		(*so_thr_init)(struct ptlrpc_thread *thr);
	/**
	 * if non-NULL called during thread shutdown (ptlrpc_main()) to
	 * destruct state created by ->srv_init().
	 */
	void		(*so_thr_done)(struct ptlrpc_thread *thr);
	/**
	 * Handler function for incoming requests for this service
	 */
	int		(*so_req_handler)(struct ptlrpc_request *req);
	/**
	 * function to determine priority of the request, it's called
	 * on every new request
	 */
	int		(*so_hpreq_handler)(struct ptlrpc_request *);
	/**
	 * service-specific print fn
	 */
	void		(*so_req_printer)(void *, struct ptlrpc_request *);
};

#ifndef __cfs_cacheline_aligned
/* NB: put it here for reducing patche dependence */
# define __cfs_cacheline_aligned
#endif

/**
 * How many high priority requests to serve before serving one normal
 * priority request
 */
#define PTLRPC_SVC_HP_RATIO 10

/**
 * Definition of PortalRPC service.
 * The service is listening on a particular portal (like tcp port)
 * and perform actions for a specific server like IO service for OST
 * or general metadata service for MDS.
 */
struct ptlrpc_service {
	/** serialize sysfs operations */
	spinlock_t			srv_lock;
	/** most often accessed fields */
	/** chain thru all services */
	struct list_head		      srv_list;
	/** service operations table */
	struct ptlrpc_service_ops	srv_ops;
	/** only statically allocated strings here; we don't clean them */
	char			   *srv_name;
	/** only statically allocated strings here; we don't clean them */
	char			   *srv_thread_name;
	/** service thread list */
	struct list_head		      srv_threads;
	/** threads # should be created for each partition on initializing */
	int				srv_nthrs_cpt_init;
	/** limit of threads number for each partition */
	int				srv_nthrs_cpt_limit;
	/** Root of debugfs dir tree for this service */
	struct dentry		   *srv_debugfs_entry;
	/** Pointer to statistic data for this service */
	struct lprocfs_stats	   *srv_stats;
	/** # hp per lp reqs to handle */
	int			     srv_hpreq_ratio;
	/** biggest request to receive */
	int			     srv_max_req_size;
	/** biggest reply to send */
	int			     srv_max_reply_size;
	/** size of individual buffers */
	int			     srv_buf_size;
	/** # buffers to allocate in 1 group */
	int			     srv_nbuf_per_group;
	/** Local portal on which to receive requests */
	__u32			   srv_req_portal;
	/** Portal on the client to send replies to */
	__u32			   srv_rep_portal;
	/**
	 * Tags for lu_context associated with this thread, see struct
	 * lu_context.
	 */
	__u32			   srv_ctx_tags;
	/** soft watchdog timeout multiplier */
	int			     srv_watchdog_factor;
	/** under unregister_service */
	unsigned			srv_is_stopping:1;

	/** max # request buffers in history per partition */
	int				srv_hist_nrqbds_cpt_max;
	/** number of CPTs this service bound on */
	int				srv_ncpts;
	/** CPTs array this service bound on */
	__u32				*srv_cpts;
	/** 2^srv_cptab_bits >= cfs_cpt_numbert(srv_cptable) */
	int				srv_cpt_bits;
	/** CPT table this service is running over */
	struct cfs_cpt_table		*srv_cptable;

	/* sysfs object */
	struct kobject			 srv_kobj;
	struct completion		 srv_kobj_unregister;
	/**
	 * partition data for ptlrpc service
	 */
	struct ptlrpc_service_part	*srv_parts[0];
};

/**
 * Definition of PortalRPC service partition data.
 * Although a service only has one instance of it right now, but we
 * will have multiple instances very soon (instance per CPT).
 *
 * it has four locks:
 * \a scp_lock
 *    serialize operations on rqbd and requests waiting for preprocess
 * \a scp_req_lock
 *    serialize operations active requests sent to this portal
 * \a scp_at_lock
 *    serialize adaptive timeout stuff
 * \a scp_rep_lock
 *    serialize operations on RS list (reply states)
 *
 * We don't have any use-case to take two or more locks at the same time
 * for now, so there is no lock order issue.
 */
struct ptlrpc_service_part {
	/** back reference to owner */
	struct ptlrpc_service		*scp_service __cfs_cacheline_aligned;
	/* CPT id, reserved */
	int				scp_cpt;
	/** always increasing number */
	int				scp_thr_nextid;
	/** # of starting threads */
	int				scp_nthrs_starting;
	/** # of stopping threads, reserved for shrinking threads */
	int				scp_nthrs_stopping;
	/** # running threads */
	int				scp_nthrs_running;
	/** service threads list */
	struct list_head			scp_threads;

	/**
	 * serialize the following fields, used for protecting
	 * rqbd list and incoming requests waiting for preprocess,
	 * threads starting & stopping are also protected by this lock.
	 */
	spinlock_t scp_lock __cfs_cacheline_aligned;
	/** total # req buffer descs allocated */
	int				scp_nrqbds_total;
	/** # posted request buffers for receiving */
	int				scp_nrqbds_posted;
	/** in progress of allocating rqbd */
	int				scp_rqbd_allocating;
	/** # incoming reqs */
	int				scp_nreqs_incoming;
	/** request buffers to be reposted */
	struct list_head			scp_rqbd_idle;
	/** req buffers receiving */
	struct list_head			scp_rqbd_posted;
	/** incoming reqs */
	struct list_head			scp_req_incoming;
	/** timeout before re-posting reqs, in tick */
	long			scp_rqbd_timeout;
	/**
	 * all threads sleep on this. This wait-queue is signalled when new
	 * incoming request arrives and when difficult reply has to be handled.
	 */
	wait_queue_head_t			scp_waitq;

	/** request history */
	struct list_head			scp_hist_reqs;
	/** request buffer history */
	struct list_head			scp_hist_rqbds;
	/** # request buffers in history */
	int				scp_hist_nrqbds;
	/** sequence number for request */
	__u64				scp_hist_seq;
	/** highest seq culled from history */
	__u64				scp_hist_seq_culled;

	/**
	 * serialize the following fields, used for processing requests
	 * sent to this portal
	 */
	spinlock_t			scp_req_lock __cfs_cacheline_aligned;
	/** # reqs in either of the NRS heads below */
	/** # reqs being served */
	int				scp_nreqs_active;
	/** # HPreqs being served */
	int				scp_nhreqs_active;
	/** # hp requests handled */
	int				scp_hreq_count;

	/** NRS head for regular requests */
	struct ptlrpc_nrs		scp_nrs_reg;
	/** NRS head for HP requests; this is only valid for services that can
	 *  handle HP requests
	 */
	struct ptlrpc_nrs	       *scp_nrs_hp;

	/** AT stuff */
	/** @{ */
	/**
	 * serialize the following fields, used for changes on
	 * adaptive timeout
	 */
	spinlock_t			scp_at_lock __cfs_cacheline_aligned;
	/** estimated rpc service time */
	struct adaptive_timeout		scp_at_estimate;
	/** reqs waiting for replies */
	struct ptlrpc_at_array		scp_at_array;
	/** early reply timer */
	struct timer_list		scp_at_timer;
	/** debug */
	unsigned long			scp_at_checktime;
	/** check early replies */
	unsigned			scp_at_check;
	/** @} */

	/**
	 * serialize the following fields, used for processing
	 * replies for this portal
	 */
	spinlock_t			scp_rep_lock __cfs_cacheline_aligned;
	/** all the active replies */
	struct list_head			scp_rep_active;
	/** List of free reply_states */
	struct list_head			scp_rep_idle;
	/** waitq to run, when adding stuff to srv_free_rs_list */
	wait_queue_head_t			scp_rep_waitq;
	/** # 'difficult' replies */
	atomic_t			scp_nreps_difficult;
};

#define ptlrpc_service_for_each_part(part, i, svc)			\
	for (i = 0;							\
	     i < (svc)->srv_ncpts &&					\
	     (svc)->srv_parts &&					\
	     ((part) = (svc)->srv_parts[i]); i++)

/**
 * Declaration of ptlrpcd control structure
 */
struct ptlrpcd_ctl {
	/**
	 * Ptlrpc thread control flags (LIOD_START, LIOD_STOP, LIOD_FORCE)
	 */
	unsigned long			pc_flags;
	/**
	 * Thread lock protecting structure fields.
	 */
	spinlock_t			pc_lock;
	/**
	 * Start completion.
	 */
	struct completion		pc_starting;
	/**
	 * Stop completion.
	 */
	struct completion		pc_finishing;
	/**
	 * Thread requests set.
	 */
	struct ptlrpc_request_set  *pc_set;
	/**
	 * Thread name used in kthread_run()
	 */
	char			pc_name[16];
	/**
	 * Environment for request interpreters to run in.
	 */
	struct lu_env	       pc_env;
	/**
	 * CPT the thread is bound on.
	 */
	int				pc_cpt;
	/**
	 * Index of ptlrpcd thread in the array.
	 */
	int				pc_index;
	/**
	 * Pointer to the array of partners' ptlrpcd_ctl structure.
	 */
	struct ptlrpcd_ctl	**pc_partners;
	/**
	 * Number of the ptlrpcd's partners.
	 */
	int				pc_npartners;
	/**
	 * Record the partner index to be processed next.
	 */
	int			 pc_cursor;
	/**
	 * Error code if the thread failed to fully start.
	 */
	int				pc_error;
};

/* Bits for pc_flags */
enum ptlrpcd_ctl_flags {
	/**
	 * Ptlrpc thread start flag.
	 */
	LIOD_START       = 1 << 0,
	/**
	 * Ptlrpc thread stop flag.
	 */
	LIOD_STOP	= 1 << 1,
	/**
	 * Ptlrpc thread force flag (only stop force so far).
	 * This will cause aborting any inflight rpcs handled
	 * by thread if LIOD_STOP is specified.
	 */
	LIOD_FORCE       = 1 << 2,
	/**
	 * This is a recovery ptlrpc thread.
	 */
	LIOD_RECOVERY    = 1 << 3,
};

/**
 * \addtogroup nrs
 * @{
 *
 * Service compatibility function; the policy is compatible with all services.
 *
 * \param[in] svc  The service the policy is attempting to register with.
 * \param[in] desc The policy descriptor
 *
 * \retval true The policy is compatible with the service
 *
 * \see ptlrpc_nrs_pol_desc::pd_compat()
 */
static inline bool nrs_policy_compat_all(const struct ptlrpc_service *svc,
					 const struct ptlrpc_nrs_pol_desc *desc)
{
	return true;
}

/**
 * Service compatibility function; the policy is compatible with only a specific
 * service which is identified by its human-readable name at
 * ptlrpc_service::srv_name.
 *
 * \param[in] svc  The service the policy is attempting to register with.
 * \param[in] desc The policy descriptor
 *
 * \retval false The policy is not compatible with the service
 * \retval true	 The policy is compatible with the service
 *
 * \see ptlrpc_nrs_pol_desc::pd_compat()
 */
static inline bool nrs_policy_compat_one(const struct ptlrpc_service *svc,
					 const struct ptlrpc_nrs_pol_desc *desc)
{
	return strcmp(svc->srv_name, desc->pd_compat_svc_name) == 0;
}

/** @} nrs */

/* ptlrpc/events.c */
extern lnet_handle_eq_t ptlrpc_eq_h;
int ptlrpc_uuid_to_peer(struct obd_uuid *uuid,
			lnet_process_id_t *peer, lnet_nid_t *self);
/**
 * These callbacks are invoked by LNet when something happened to
 * underlying buffer
 * @{
 */
void request_out_callback(lnet_event_t *ev);
void reply_in_callback(lnet_event_t *ev);
void client_bulk_callback(lnet_event_t *ev);
void request_in_callback(lnet_event_t *ev);
void reply_out_callback(lnet_event_t *ev);
/** @} */

/* ptlrpc/connection.c */
struct ptlrpc_connection *ptlrpc_connection_get(lnet_process_id_t peer,
						lnet_nid_t self,
						struct obd_uuid *uuid);
int ptlrpc_connection_put(struct ptlrpc_connection *c);
struct ptlrpc_connection *ptlrpc_connection_addref(struct ptlrpc_connection *);
int ptlrpc_connection_init(void);
void ptlrpc_connection_fini(void);

/* ptlrpc/niobuf.c */
/**
 * Actual interfacing with LNet to put/get/register/unregister stuff
 * @{
 */

int ptlrpc_unregister_bulk(struct ptlrpc_request *req, int async);

static inline int ptlrpc_client_bulk_active(struct ptlrpc_request *req)
{
	struct ptlrpc_bulk_desc *desc;
	int		      rc;

	desc = req->rq_bulk;

	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_BULK_UNLINK) &&
	    req->rq_bulk_deadline > ktime_get_real_seconds())
		return 1;

	if (!desc)
		return 0;

	spin_lock(&desc->bd_lock);
	rc = desc->bd_md_count;
	spin_unlock(&desc->bd_lock);
	return rc;
}

#define PTLRPC_REPLY_MAYBE_DIFFICULT 0x01
#define PTLRPC_REPLY_EARLY	   0x02
int ptlrpc_send_reply(struct ptlrpc_request *req, int flags);
int ptlrpc_reply(struct ptlrpc_request *req);
int ptlrpc_send_error(struct ptlrpc_request *req, int difficult);
int ptlrpc_error(struct ptlrpc_request *req);
void ptlrpc_resend_req(struct ptlrpc_request *request);
int ptlrpc_at_get_net_latency(struct ptlrpc_request *req);
int ptl_send_rpc(struct ptlrpc_request *request, int noreply);
int ptlrpc_register_rqbd(struct ptlrpc_request_buffer_desc *rqbd);
/** @} */

/* ptlrpc/client.c */
/**
 * Client-side portals API. Everything to send requests, receive replies,
 * request queues, request management, etc.
 * @{
 */
void ptlrpc_request_committed(struct ptlrpc_request *req, int force);

void ptlrpc_init_client(int req_portal, int rep_portal, char *name,
			struct ptlrpc_client *);
struct ptlrpc_connection *ptlrpc_uuid_to_connection(struct obd_uuid *uuid);

int ptlrpc_queue_wait(struct ptlrpc_request *req);
int ptlrpc_replay_req(struct ptlrpc_request *req);
int ptlrpc_unregister_reply(struct ptlrpc_request *req, int async);
void ptlrpc_abort_inflight(struct obd_import *imp);
void ptlrpc_abort_set(struct ptlrpc_request_set *set);

struct ptlrpc_request_set *ptlrpc_prep_set(void);
struct ptlrpc_request_set *ptlrpc_prep_fcset(int max, set_producer_func func,
					     void *arg);
int ptlrpc_set_next_timeout(struct ptlrpc_request_set *);
int ptlrpc_check_set(const struct lu_env *env, struct ptlrpc_request_set *set);
int ptlrpc_set_wait(struct ptlrpc_request_set *);
int ptlrpc_expired_set(void *data);
void ptlrpc_interrupted_set(void *data);
void ptlrpc_mark_interrupted(struct ptlrpc_request *req);
void ptlrpc_set_destroy(struct ptlrpc_request_set *);
void ptlrpc_set_add_req(struct ptlrpc_request_set *, struct ptlrpc_request *);
void ptlrpc_set_add_new_req(struct ptlrpcd_ctl *pc,
			    struct ptlrpc_request *req);

void ptlrpc_free_rq_pool(struct ptlrpc_request_pool *pool);
int ptlrpc_add_rqs_to_pool(struct ptlrpc_request_pool *pool, int num_rq);

struct ptlrpc_request_pool *
ptlrpc_init_rq_pool(int, int,
		    int (*populate_pool)(struct ptlrpc_request_pool *, int));

void ptlrpc_at_set_req_timeout(struct ptlrpc_request *req);
struct ptlrpc_request *ptlrpc_request_alloc(struct obd_import *imp,
					    const struct req_format *format);
struct ptlrpc_request *ptlrpc_request_alloc_pool(struct obd_import *imp,
						 struct ptlrpc_request_pool *,
						 const struct req_format *);
void ptlrpc_request_free(struct ptlrpc_request *request);
int ptlrpc_request_pack(struct ptlrpc_request *request,
			__u32 version, int opcode);
struct ptlrpc_request *ptlrpc_request_alloc_pack(struct obd_import *,
						 const struct req_format *,
						 __u32, int);
int ptlrpc_request_bufs_pack(struct ptlrpc_request *request,
			     __u32 version, int opcode, char **bufs,
			     struct ptlrpc_cli_ctx *ctx);
void ptlrpc_req_finished(struct ptlrpc_request *request);
struct ptlrpc_request *ptlrpc_request_addref(struct ptlrpc_request *req);
struct ptlrpc_bulk_desc *ptlrpc_prep_bulk_imp(struct ptlrpc_request *req,
					      unsigned npages, unsigned max_brw,
					      unsigned type, unsigned portal);
void __ptlrpc_free_bulk(struct ptlrpc_bulk_desc *bulk, int pin);
static inline void ptlrpc_free_bulk_pin(struct ptlrpc_bulk_desc *bulk)
{
	__ptlrpc_free_bulk(bulk, 1);
}

static inline void ptlrpc_free_bulk_nopin(struct ptlrpc_bulk_desc *bulk)
{
	__ptlrpc_free_bulk(bulk, 0);
}

void __ptlrpc_prep_bulk_page(struct ptlrpc_bulk_desc *desc,
			     struct page *page, int pageoffset, int len, int);
static inline void ptlrpc_prep_bulk_page_pin(struct ptlrpc_bulk_desc *desc,
					     struct page *page, int pageoffset,
					     int len)
{
	__ptlrpc_prep_bulk_page(desc, page, pageoffset, len, 1);
}

static inline void ptlrpc_prep_bulk_page_nopin(struct ptlrpc_bulk_desc *desc,
					       struct page *page, int pageoffset,
					       int len)
{
	__ptlrpc_prep_bulk_page(desc, page, pageoffset, len, 0);
}

void ptlrpc_retain_replayable_request(struct ptlrpc_request *req,
				      struct obd_import *imp);
__u64 ptlrpc_next_xid(void);
__u64 ptlrpc_sample_next_xid(void);
__u64 ptlrpc_req_xid(struct ptlrpc_request *request);

/* Set of routines to run a function in ptlrpcd context */
void *ptlrpcd_alloc_work(struct obd_import *imp,
			 int (*cb)(const struct lu_env *, void *), void *data);
void ptlrpcd_destroy_work(void *handler);
int ptlrpcd_queue_work(void *handler);

/** @} */
struct ptlrpc_service_buf_conf {
	/* nbufs is buffers # to allocate when growing the pool */
	unsigned int			bc_nbufs;
	/* buffer size to post */
	unsigned int			bc_buf_size;
	/* portal to listed for requests on */
	unsigned int			bc_req_portal;
	/* portal of where to send replies to */
	unsigned int			bc_rep_portal;
	/* maximum request size to be accepted for this service */
	unsigned int			bc_req_max_size;
	/* maximum reply size this service can ever send */
	unsigned int			bc_rep_max_size;
};

struct ptlrpc_service_thr_conf {
	/* threadname should be 8 characters or less - 6 will be added on */
	char				*tc_thr_name;
	/* threads increasing factor for each CPU */
	unsigned int			tc_thr_factor;
	/* service threads # to start on each partition while initializing */
	unsigned int			tc_nthrs_init;
	/*
	 * low water of threads # upper-limit on each partition while running,
	 * service availability may be impacted if threads number is lower
	 * than this value. It can be ZERO if the service doesn't require
	 * CPU affinity or there is only one partition.
	 */
	unsigned int			tc_nthrs_base;
	/* "soft" limit for total threads number */
	unsigned int			tc_nthrs_max;
	/* user specified threads number, it will be validated due to
	 * other members of this structure.
	 */
	unsigned int			tc_nthrs_user;
	/* set NUMA node affinity for service threads */
	unsigned int			tc_cpu_affinity;
	/* Tags for lu_context associated with service thread */
	__u32				tc_ctx_tags;
};

struct ptlrpc_service_cpt_conf {
	struct cfs_cpt_table		*cc_cptable;
	/* string pattern to describe CPTs for a service */
	char				*cc_pattern;
};

struct ptlrpc_service_conf {
	/* service name */
	char				*psc_name;
	/* soft watchdog timeout multiplifier to print stuck service traces */
	unsigned int			psc_watchdog_factor;
	/* buffer information */
	struct ptlrpc_service_buf_conf	psc_buf;
	/* thread information */
	struct ptlrpc_service_thr_conf	psc_thr;
	/* CPU partition information */
	struct ptlrpc_service_cpt_conf	psc_cpt;
	/* function table */
	struct ptlrpc_service_ops	psc_ops;
};

/* ptlrpc/service.c */
/**
 * Server-side services API. Register/unregister service, request state
 * management, service thread management
 *
 * @{
 */
void ptlrpc_dispatch_difficult_reply(struct ptlrpc_reply_state *rs);
void ptlrpc_schedule_difficult_reply(struct ptlrpc_reply_state *rs);
struct ptlrpc_service *ptlrpc_register_service(struct ptlrpc_service_conf *conf,
					       struct kset *parent,
					       struct dentry *debugfs_entry);

int ptlrpc_start_threads(struct ptlrpc_service *svc);
int ptlrpc_unregister_service(struct ptlrpc_service *service);

int ptlrpc_hr_init(void);
void ptlrpc_hr_fini(void);

/** @} */

/* ptlrpc/import.c */
/**
 * Import API
 * @{
 */
int ptlrpc_connect_import(struct obd_import *imp);
int ptlrpc_init_import(struct obd_import *imp);
int ptlrpc_disconnect_import(struct obd_import *imp, int noclose);
int ptlrpc_import_recovery_state_machine(struct obd_import *imp);

/* ptlrpc/pack_generic.c */
int ptlrpc_reconnect_import(struct obd_import *imp);
/** @} */

/**
 * ptlrpc msg buffer and swab interface
 *
 * @{
 */
int ptlrpc_buf_need_swab(struct ptlrpc_request *req, const int inout,
			 int index);
void ptlrpc_buf_set_swabbed(struct ptlrpc_request *req, const int inout,
			    int index);
int ptlrpc_unpack_rep_msg(struct ptlrpc_request *req, int len);
int ptlrpc_unpack_req_msg(struct ptlrpc_request *req, int len);

void lustre_init_msg_v2(struct lustre_msg_v2 *msg, int count, __u32 *lens,
			char **bufs);
int lustre_pack_request(struct ptlrpc_request *, __u32 magic, int count,
			__u32 *lens, char **bufs);
int lustre_pack_reply(struct ptlrpc_request *, int count, __u32 *lens,
		      char **bufs);
int lustre_pack_reply_v2(struct ptlrpc_request *req, int count,
			 __u32 *lens, char **bufs, int flags);
#define LPRFL_EARLY_REPLY 1
int lustre_pack_reply_flags(struct ptlrpc_request *, int count, __u32 *lens,
			    char **bufs, int flags);
int lustre_shrink_msg(struct lustre_msg *msg, int segment,
		      unsigned int newlen, int move_data);
void lustre_free_reply_state(struct ptlrpc_reply_state *rs);
int __lustre_unpack_msg(struct lustre_msg *m, int len);
int lustre_msg_hdr_size(__u32 magic, int count);
int lustre_msg_size(__u32 magic, int count, __u32 *lengths);
int lustre_msg_size_v2(int count, __u32 *lengths);
int lustre_packed_msg_size(struct lustre_msg *msg);
int lustre_msg_early_size(void);
void *lustre_msg_buf_v2(struct lustre_msg_v2 *m, int n, int min_size);
void *lustre_msg_buf(struct lustre_msg *m, int n, int minlen);
int lustre_msg_buflen(struct lustre_msg *m, int n);
int lustre_msg_bufcount(struct lustre_msg *m);
char *lustre_msg_string(struct lustre_msg *m, int n, int max_len);
__u32 lustre_msghdr_get_flags(struct lustre_msg *msg);
void lustre_msghdr_set_flags(struct lustre_msg *msg, __u32 flags);
__u32 lustre_msg_get_flags(struct lustre_msg *msg);
void lustre_msg_add_flags(struct lustre_msg *msg, int flags);
void lustre_msg_set_flags(struct lustre_msg *msg, int flags);
void lustre_msg_clear_flags(struct lustre_msg *msg, int flags);
__u32 lustre_msg_get_op_flags(struct lustre_msg *msg);
void lustre_msg_add_op_flags(struct lustre_msg *msg, int flags);
struct lustre_handle *lustre_msg_get_handle(struct lustre_msg *msg);
__u32 lustre_msg_get_type(struct lustre_msg *msg);
void lustre_msg_add_version(struct lustre_msg *msg, int version);
__u32 lustre_msg_get_opc(struct lustre_msg *msg);
__u64 lustre_msg_get_last_committed(struct lustre_msg *msg);
__u64 *lustre_msg_get_versions(struct lustre_msg *msg);
__u64 lustre_msg_get_transno(struct lustre_msg *msg);
__u64 lustre_msg_get_slv(struct lustre_msg *msg);
__u32 lustre_msg_get_limit(struct lustre_msg *msg);
void lustre_msg_set_slv(struct lustre_msg *msg, __u64 slv);
void lustre_msg_set_limit(struct lustre_msg *msg, __u64 limit);
int lustre_msg_get_status(struct lustre_msg *msg);
__u32 lustre_msg_get_conn_cnt(struct lustre_msg *msg);
__u32 lustre_msg_get_magic(struct lustre_msg *msg);
__u32 lustre_msg_get_timeout(struct lustre_msg *msg);
__u32 lustre_msg_get_service_time(struct lustre_msg *msg);
__u32 lustre_msg_get_cksum(struct lustre_msg *msg);
__u32 lustre_msg_calc_cksum(struct lustre_msg *msg);
void lustre_msg_set_handle(struct lustre_msg *msg,
			   struct lustre_handle *handle);
void lustre_msg_set_type(struct lustre_msg *msg, __u32 type);
void lustre_msg_set_opc(struct lustre_msg *msg, __u32 opc);
void lustre_msg_set_versions(struct lustre_msg *msg, __u64 *versions);
void lustre_msg_set_transno(struct lustre_msg *msg, __u64 transno);
void lustre_msg_set_status(struct lustre_msg *msg, __u32 status);
void lustre_msg_set_conn_cnt(struct lustre_msg *msg, __u32 conn_cnt);
void ptlrpc_request_set_replen(struct ptlrpc_request *req);
void lustre_msg_set_timeout(struct lustre_msg *msg, __u32 timeout);
void lustre_msg_set_service_time(struct lustre_msg *msg, __u32 service_time);
void lustre_msg_set_jobid(struct lustre_msg *msg, char *jobid);
void lustre_msg_set_cksum(struct lustre_msg *msg, __u32 cksum);

static inline void
lustre_shrink_reply(struct ptlrpc_request *req, int segment,
		    unsigned int newlen, int move_data)
{
	LASSERT(req->rq_reply_state);
	LASSERT(req->rq_repmsg);
	req->rq_replen = lustre_shrink_msg(req->rq_repmsg, segment,
					   newlen, move_data);
}

#ifdef CONFIG_LUSTRE_TRANSLATE_ERRNOS

static inline int ptlrpc_status_hton(int h)
{
	/*
	 * Positive errnos must be network errnos, such as LUSTRE_EDEADLK,
	 * ELDLM_LOCK_ABORTED, etc.
	 */
	if (h < 0)
		return -lustre_errno_hton(-h);
	else
		return h;
}

static inline int ptlrpc_status_ntoh(int n)
{
	/*
	 * See the comment in ptlrpc_status_hton().
	 */
	if (n < 0)
		return -lustre_errno_ntoh(-n);
	else
		return n;
}

#else

#define ptlrpc_status_hton(h) (h)
#define ptlrpc_status_ntoh(n) (n)

#endif
/** @} */

/** Change request phase of \a req to \a new_phase */
static inline void
ptlrpc_rqphase_move(struct ptlrpc_request *req, enum rq_phase new_phase)
{
	if (req->rq_phase == new_phase)
		return;

	if (new_phase == RQ_PHASE_UNREGISTERING) {
		req->rq_next_phase = req->rq_phase;
		if (req->rq_import)
			atomic_inc(&req->rq_import->imp_unregistering);
	}

	if (req->rq_phase == RQ_PHASE_UNREGISTERING) {
		if (req->rq_import)
			atomic_dec(&req->rq_import->imp_unregistering);
	}

	DEBUG_REQ(D_INFO, req, "move req \"%s\" -> \"%s\"",
		  ptlrpc_rqphase2str(req), ptlrpc_phase2str(new_phase));

	req->rq_phase = new_phase;
}

/**
 * Returns true if request \a req got early reply and hard deadline is not met
 */
static inline int
ptlrpc_client_early(struct ptlrpc_request *req)
{
	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK) &&
	    req->rq_reply_deadline > ktime_get_real_seconds())
		return 0;
	return req->rq_early;
}

/**
 * Returns true if we got real reply from server for this request
 */
static inline int
ptlrpc_client_replied(struct ptlrpc_request *req)
{
	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK) &&
	    req->rq_reply_deadline > ktime_get_real_seconds())
		return 0;
	return req->rq_replied;
}

/** Returns true if request \a req is in process of receiving server reply */
static inline int
ptlrpc_client_recv(struct ptlrpc_request *req)
{
	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK) &&
	    req->rq_reply_deadline > ktime_get_real_seconds())
		return 1;
	return req->rq_receiving_reply;
}

static inline int
ptlrpc_client_recv_or_unlink(struct ptlrpc_request *req)
{
	int rc;

	spin_lock(&req->rq_lock);
	if (OBD_FAIL_CHECK(OBD_FAIL_PTLRPC_LONG_REPL_UNLINK) &&
	    req->rq_reply_deadline > ktime_get_real_seconds()) {
		spin_unlock(&req->rq_lock);
		return 1;
	}
	rc = req->rq_receiving_reply;
	rc = rc || req->rq_req_unlink || req->rq_reply_unlink;
	spin_unlock(&req->rq_lock);
	return rc;
}

static inline void
ptlrpc_client_wake_req(struct ptlrpc_request *req)
{
	if (!req->rq_set)
		wake_up(&req->rq_reply_waitq);
	else
		wake_up(&req->rq_set->set_waitq);
}

static inline void
ptlrpc_rs_addref(struct ptlrpc_reply_state *rs)
{
	LASSERT(atomic_read(&rs->rs_refcount) > 0);
	atomic_inc(&rs->rs_refcount);
}

static inline void
ptlrpc_rs_decref(struct ptlrpc_reply_state *rs)
{
	LASSERT(atomic_read(&rs->rs_refcount) > 0);
	if (atomic_dec_and_test(&rs->rs_refcount))
		lustre_free_reply_state(rs);
}

/* Should only be called once per req */
static inline void ptlrpc_req_drop_rs(struct ptlrpc_request *req)
{
	if (!req->rq_reply_state)
		return; /* shouldn't occur */
	ptlrpc_rs_decref(req->rq_reply_state);
	req->rq_reply_state = NULL;
	req->rq_repmsg = NULL;
}

static inline __u32 lustre_request_magic(struct ptlrpc_request *req)
{
	return lustre_msg_get_magic(req->rq_reqmsg);
}

static inline int ptlrpc_req_get_repsize(struct ptlrpc_request *req)
{
	switch (req->rq_reqmsg->lm_magic) {
	case LUSTRE_MSG_MAGIC_V2:
		return req->rq_reqmsg->lm_repsize;
	default:
		LASSERTF(0, "incorrect message magic: %08x\n",
			 req->rq_reqmsg->lm_magic);
		return -EFAULT;
	}
}

static inline int ptlrpc_send_limit_expired(struct ptlrpc_request *req)
{
	if (req->rq_delay_limit != 0 &&
	    time_before(cfs_time_add(req->rq_queued_time,
				     cfs_time_seconds(req->rq_delay_limit)),
			cfs_time_current())) {
		return 1;
	}
	return 0;
}

static inline int ptlrpc_no_resend(struct ptlrpc_request *req)
{
	if (!req->rq_no_resend && ptlrpc_send_limit_expired(req)) {
		spin_lock(&req->rq_lock);
		req->rq_no_resend = 1;
		spin_unlock(&req->rq_lock);
	}
	return req->rq_no_resend;
}

static inline int
ptlrpc_server_get_timeout(struct ptlrpc_service_part *svcpt)
{
	int at = AT_OFF ? 0 : at_get(&svcpt->scp_at_estimate);

	return svcpt->scp_service->srv_watchdog_factor *
	       max_t(int, at, obd_timeout);
}

static inline struct ptlrpc_service *
ptlrpc_req2svc(struct ptlrpc_request *req)
{
	return req->rq_rqbd->rqbd_svcpt->scp_service;
}

/* ldlm/ldlm_lib.c */
/**
 * Target client logic
 * @{
 */
int client_obd_setup(struct obd_device *obddev, struct lustre_cfg *lcfg);
int client_obd_cleanup(struct obd_device *obddev);
int client_connect_import(const struct lu_env *env,
			  struct obd_export **exp, struct obd_device *obd,
			  struct obd_uuid *cluuid, struct obd_connect_data *,
			  void *localdata);
int client_disconnect_export(struct obd_export *exp);
int client_import_add_conn(struct obd_import *imp, struct obd_uuid *uuid,
			   int priority);
int client_import_del_conn(struct obd_import *imp, struct obd_uuid *uuid);
int client_import_find_conn(struct obd_import *imp, lnet_nid_t peer,
			    struct obd_uuid *uuid);
int import_set_conn_priority(struct obd_import *imp, struct obd_uuid *uuid);
void client_destroy_import(struct obd_import *imp);
/** @} */

/* ptlrpc/pinger.c */
/**
 * Pinger API (client side only)
 * @{
 */
enum timeout_event {
	TIMEOUT_GRANT = 1
};

struct timeout_item;
typedef int (*timeout_cb_t)(struct timeout_item *, void *);
int ptlrpc_pinger_add_import(struct obd_import *imp);
int ptlrpc_pinger_del_import(struct obd_import *imp);
int ptlrpc_add_timeout_client(int time, enum timeout_event event,
			      timeout_cb_t cb, void *data,
			      struct list_head *obd_list);
int ptlrpc_del_timeout_client(struct list_head *obd_list,
			      enum timeout_event event);
struct ptlrpc_request *ptlrpc_prep_ping(struct obd_import *imp);
int ptlrpc_obd_ping(struct obd_device *obd);
void ptlrpc_pinger_ir_up(void);
void ptlrpc_pinger_ir_down(void);
/** @} */
int ptlrpc_pinger_suppress_pings(void);

/* ptlrpc/ptlrpcd.c */
void ptlrpcd_stop(struct ptlrpcd_ctl *pc, int force);
void ptlrpcd_free(struct ptlrpcd_ctl *pc);
void ptlrpcd_wake(struct ptlrpc_request *req);
void ptlrpcd_add_req(struct ptlrpc_request *req);
int ptlrpcd_addref(void);
void ptlrpcd_decref(void);

/* ptlrpc/lproc_ptlrpc.c */
/**
 * procfs output related functions
 * @{
 */
const char *ll_opcode2str(__u32 opcode);
void ptlrpc_lprocfs_register_obd(struct obd_device *obd);
void ptlrpc_lprocfs_unregister_obd(struct obd_device *obd);
void ptlrpc_lprocfs_brw(struct ptlrpc_request *req, int bytes);
/** @} */

/* ptlrpc/llog_client.c */
extern struct llog_operations llog_client_ops;
/** @} net */

#endif
/** @} PtlRPC */
