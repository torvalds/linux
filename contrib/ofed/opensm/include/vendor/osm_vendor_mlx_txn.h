/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _OSMV_TXN_H_
#define _OSMV_TXN_H_

#include <sys/types.h>
#include <unistd.h>

#include <complib/cl_qmap.h>
#include <opensm/osm_madw.h>
#include <complib/cl_event_wheel.h>

#include <vendor/osm_vendor_mlx_rmpp_ctx.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

typedef enum _osmv_txn_rmpp_state {

	OSMV_TXN_RMPP_NONE = 0,	/* Not part of RMPP transaction */

	OSMV_TXN_RMPP_SENDER,
	OSMV_TXN_RMPP_RECEIVER
} osmv_txn_rmpp_state_t;

typedef struct _osmv_rmpp_txfr {

	osmv_txn_rmpp_state_t rmpp_state;
	boolean_t is_rmpp_init_by_peer;
	osmv_rmpp_send_ctx_t *p_rmpp_send_ctx;
	osmv_rmpp_recv_ctx_t *p_rmpp_recv_ctx;

} osmv_rmpp_txfr_t;

typedef struct _osmv_txn_ctx {

	/* The original Transaction ID */
	uint64_t tid;
	/* The key by which the Transaction is stored */
	uint64_t key;

	/* RMPP Send/Receive contexts, if applicable */
	osmv_rmpp_txfr_t rmpp_txfr;

	/* A MAD that was sent during the transaction (request or response) */
	osm_madw_t *p_madw;

	/* Reference to a log to enable tracing */
	osm_log_t *p_log;

} osmv_txn_ctx_t;

typedef struct _osmv_txn_mgr {

	/* Container of all the transactions */
	cl_qmap_t *p_txn_map;

	/* The timeouts DB */
	cl_event_wheel_t *p_event_wheel;

	/* Reference to a log to enable tracing */
	osm_log_t *p_log;

} osmv_txn_mgr_t;

/* *    *   *   *   *   *   osmv_txn_ctx_t functions  *    *   *   *   *   *   *   *   */

/*
 * NAME
 *   osmv_txn_init
 *
 * DESCRIPTION
 *   allocs & inits the osmv_txn_ctx obj and insert it into the db
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txn_init(IN osm_bind_handle_t h_bind,
	      IN uint64_t tid, IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn);

/*
 * NAME
 *   osmv_rmpp_txfr_init_sender
 *
 * DESCRIPTION
 *   init the rmpp send ctx in the transaction
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txn_init_rmpp_sender(IN osm_bind_handle_t h_bind,
			  IN osmv_txn_ctx_t * p_txn, IN osm_madw_t * p_madw);

/*
 * NAME
 *   osmv_rmpp_txfr_init_receiver
 *
 * DESCRIPTION
 *   init the rmpp recv ctx in the transaction
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txn_init_rmpp_receiver(IN osm_bind_handle_t h_bind,
			    IN osmv_txn_ctx_t * p_txn,
			    IN boolean_t is_init_by_peer);

/*
 * NAME
 *   osmv_txn_done
 *
 * DESCRIPTION
 *   destroys txn object and removes it from the db
 *
 * SEE ALSO
 *
 */
void
osmv_txn_done(IN osm_bind_handle_t h_bind,
	      IN uint64_t key, IN boolean_t is_in_cb);
/*
 * NAME
 *   osmv_txn_get_tid
 *
 * DESCRIPTION
 *   returns tid of the transaction
 * SEE ALSO
 *
 */
static inline uint64_t osmv_txn_get_tid(IN osmv_txn_ctx_t * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->tid;
}

/*
 * NAME
 *   osmv_txn_get_key
 *
 * DESCRIPTION
 *   returns key of the transaction
 * SEE ALSO
 *
 */

static inline uint64_t osmv_txn_get_key(IN osmv_txn_ctx_t * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->key;
}

/*
 * NAME
 *   osmv_txn_is_rmpp_init_by_peer
 *
 * DESCRIPTION
 *   returns whether the rmpp txfr was init by the peer
 *
 * SEE ALSO
 *
 */
static inline boolean_t osmv_txn_is_rmpp_init_by_peer(IN osmv_txn_ctx_t * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->rmpp_txfr.is_rmpp_init_by_peer;
}

/*
 * NAME
 *   osmv_txn_get_rmpp_send_ctx
 *
 * DESCRIPTION
 *   returns osmv_rmpp_send_ctx obj
 * SEE ALSO
 *
 */
static inline osmv_rmpp_send_ctx_t *osmv_txn_get_rmpp_send_ctx(IN osmv_txn_ctx_t
							       * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->rmpp_txfr.p_rmpp_send_ctx;
}

/*
 * NAME
 *   osmv_txn_get_rmpp_recv_ctx
 *
 * DESCRIPTION
 *   returns osmv_rmpp_recv_ctx obj
 * SEE ALSO
 *
 */
static inline osmv_rmpp_recv_ctx_t *osmv_txn_get_rmpp_recv_ctx(IN osmv_txn_ctx_t
							       * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->rmpp_txfr.p_rmpp_recv_ctx;
}

/*
 * NAME
 *   osmv_txn_get_rmpp_state
 *
 * DESCRIPTION
 *   returns the rmpp role of the transactino ( send/ recv)
 * SEE ALSO
 *
 */
static inline osmv_txn_rmpp_state_t
osmv_txn_get_rmpp_state(IN osmv_txn_ctx_t * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->rmpp_txfr.rmpp_state;
}

/*
 * NAME
 *   osmv_txn_set_rmpp_state
 *
 * DESCRIPTION
 *   sets the rmpp role of the transaction (send/ recv)
 * SEE ALSO
 *
 */
static inline void
osmv_txn_set_rmpp_state(IN osmv_txn_ctx_t * p_txn,
			IN osmv_txn_rmpp_state_t state)
{
	CL_ASSERT(NULL != p_txn);
	p_txn->rmpp_txfr.rmpp_state = state;
}

/*
 * NAME
 *   osmv_txn_get_madw
 *
 * DESCRIPTION
 *   returns the requester madw
 * SEE ALSO
 *
 */
static inline osm_madw_t *osmv_txn_get_madw(IN osmv_txn_ctx_t * p_txn)
{
	CL_ASSERT(NULL != p_txn);
	return p_txn->p_madw;
}

/*
 * NAME
 *   osmv_txn_set_madw
 *
 * DESCRIPTION
 *   sets the requester madw
 * SEE ALSO
 *
 */
static inline void
osmv_txn_set_madw(IN osmv_txn_ctx_t * p_txn, IN osm_madw_t * p_madw)
{
	CL_ASSERT(NULL != p_txn);
	p_txn->p_madw = p_madw;
}

/*
 * NAME
 *  osmv_txn_set_timeout_ev
 *
 * DESCRIPTION
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txn_set_timeout_ev(IN osm_bind_handle_t h_bind,
			IN uint64_t key, IN uint64_t msec);
/*
 * NAME
 *  osmv_txn_remove_timeout_ev
 *
 * DESCRIPTION

 * SEE ALSO
 *
 */
void osmv_txn_remove_timeout_ev(IN osm_bind_handle_t h_bind, IN uint64_t key);
/*
 * NAME
 *  osmv_txn_lookup
 *
 * DESCRIPTION
 *   get a transaction by its key
 *
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txn_lookup(IN osm_bind_handle_t h_bind,
		IN uint64_t key, OUT osmv_txn_ctx_t ** pp_txn);

void osmv_txn_abort_rmpp_txns(IN osm_bind_handle_t h_bind);

/*      *       *       *       *       *       *       *       *       *       *       *       */
/*
 * NAME
 *  osmv_txnmgr_init
 *
 * DESCRIPTION
 *  c'tor for txn mgr obj
 * SEE ALSO
 *
 */
ib_api_status_t
osmv_txnmgr_init(IN osmv_txn_mgr_t * p_tx_mgr,
		 IN osm_log_t * p_log, IN cl_spinlock_t * p_lock);

/*
 * NAME
 *  osmv_txnmgr_done
 *
 * DESCRIPTION
 *  c'tor for txn mgr obj
 * SEE ALSO
 *
 */
void osmv_txnmgr_done(IN osm_bind_handle_t h_bind);

void osmv_txn_lock(IN osm_bind_handle_t h_bind);
void osmv_txn_unlock(IN osm_bind_handle_t h_bind);

inline static uint64_t osmv_txn_uniq_key(IN uint64_t tid)
{
	uint64_t pid = getpid();

	return ((pid << 32) | (tid & 0xFFFFFFFF));
}

END_C_DECLS
#endif				/* _OSMV_TXN_H_ */
