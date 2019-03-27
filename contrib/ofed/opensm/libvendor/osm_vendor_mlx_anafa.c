/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <sys/ioctl.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <vendor/osm_vendor_mlx.h>
#include <vendor/osm_vendor_mlx_transport.h>
#include <vendor/osm_vendor_mlx_transport_anafa.h>
#include <vendor/osm_vendor_mlx_svc.h>
#include <vendor/osm_vendor_mlx_sender.h>
#include <vendor/osm_pkt_randomizer.h>
#include <vendor/osm_ts_useraccess.h>

/**
 *      FORWARD REFERENCES
 */
static ib_api_status_t
__osmv_get_send_txn(IN osm_bind_handle_t h_bind,
		    IN osm_madw_t * const p_madw,
		    IN boolean_t is_rmpp,
		    IN boolean_t resp_expected, OUT osmv_txn_ctx_t ** pp_txn);

static void __osm_vendor_internal_unbind(osm_bind_handle_t h_bind);

/*
 *  NAME            osm_vendor_new
 *
 *  DESCRIPTION     Create and Initialize the osm_vendor_t Object
 */

osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout)
{
	ib_api_status_t status;
	osm_vendor_t *p_vend;

	OSM_LOG_ENTER(p_log);

	CL_ASSERT(p_log);

	p_vend = malloc(sizeof(*p_vend));
	if (p_vend != NULL) {
		memset(p_vend, 0, sizeof(*p_vend));
		status = osm_vendor_init(p_vend, p_log, timeout);
		if (status != IB_SUCCESS) {
			osm_vendor_delete(&p_vend);
		}
	} else {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_new: ERR 7401: "
			"Fail to allocate vendor object.\n");
	}

	OSM_LOG_EXIT(p_log);
	return (p_vend);
}

/*
 *  NAME            osm_vendor_delete
 *
 *  DESCRIPTION     Delete all the binds behind the vendor + free the vendor object
 */

void osm_vendor_delete(IN osm_vendor_t ** const pp_vend)
{
	cl_list_item_t *p_item;
	cl_list_obj_t *p_obj;
	osm_bind_handle_t bind_h;
	osm_log_t *p_log;

	OSM_LOG_ENTER((*pp_vend)->p_log);
	p_log = (*pp_vend)->p_log;

	/* go over the bind handles , unbind them and remove from list */
	/* Note that if we reached here due to problem in the init, then
	   the bind_handles list is not initialized yet */
	if ((*pp_vend)->bind_handles.state == CL_INITIALIZED) {
		p_item = cl_qlist_remove_head(&((*pp_vend)->bind_handles));
		while (p_item != cl_qlist_end(&((*pp_vend)->bind_handles))) {

			p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
			bind_h = (osm_bind_handle_t *) cl_qlist_obj(p_obj);
			osm_log(p_log, OSM_LOG_DEBUG,
				"osm_vendor_delete: unbinding bind_h:%p \n",
				bind_h);

			__osm_vendor_internal_unbind(bind_h);

			free(p_obj);
			/* removing from list */
			p_item =
			    cl_qlist_remove_head(&((*pp_vend)->bind_handles));
		}
	}

	if (NULL != ((*pp_vend)->p_transport_info)) {
		free((*pp_vend)->p_transport_info);
		(*pp_vend)->p_transport_info = NULL;
	}

	/* remove the packet randomizer object */
	if ((*pp_vend)->run_randomizer == TRUE)
		osm_pkt_randomizer_destroy(&((*pp_vend)->p_pkt_randomizer),
					   p_log);

	free(*pp_vend);
	*pp_vend = NULL;

	OSM_LOG_EXIT(p_log);
}

/*
 *  NAME            osm_vendor_init
 *
 *  DESCRIPTION     Initialize the vendor object
 */

ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	ib_api_status_t status = IB_SUCCESS;
	char device_file[16];
	int device_fd;

	OSM_LOG_ENTER(p_log);

	p_vend->p_log = p_log;
	p_vend->resp_timeout = timeout;
	p_vend->ttime_timeout = timeout * OSMV_TXN_TIMEOUT_FACTOR;

	p_vend->p_transport_info = (osmv_TOPSPIN_ANAFA_transport_info_t *)
	    malloc(sizeof(osmv_TOPSPIN_ANAFA_transport_info_t));
	if (!p_vend->p_transport_info) {
		return IB_ERROR;
	}

	memset(p_vend->p_transport_info, 0,
	       sizeof(osmv_TOPSPIN_ANAFA_transport_info_t));

	/* update the run_randomizer flag */
	if (getenv("OSM_PKT_DROP_RATE") != NULL
	    && atol(getenv("OSM_PKT_DROP_RATE")) != 0) {
		/* if the OSM_PKT_DROP_RATE global variable is defined
		   to a non-zero value -
		   then the randomizer should be called.
		   Need to create the packet randomizer object */
		p_vend->run_randomizer = TRUE;
		status =
		    osm_pkt_randomizer_init(&(p_vend->p_pkt_randomizer), p_log);
		if (status != IB_SUCCESS)
			return status;
	} else {
		p_vend->run_randomizer = FALSE;
		p_vend->p_pkt_randomizer = NULL;
	}

	/* open TopSpin file device */
	sprintf(device_file, "/dev/ts_ua0");
	device_fd = open("/dev/ts_ua0", O_RDWR);
	if (device_fd < 0) {
		fprintf(stderr, "Fatal: Fail to open the file:%s(%d)\n",
			device_file, errno);
		return IB_ERROR;
	}

	((osmv_TOPSPIN_ANAFA_transport_info_t *) p_vend->p_transport_info)->
	    device_fd = device_fd;

	cl_qlist_init(&p_vend->bind_handles);

	OSM_LOG_EXIT(p_log);
	return (IB_SUCCESS);
}

/*
 *  NAME            osm_vendor_bind
 *
 *  DESCRIPTION     Create a new bind object under the vendor object
 */

osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_bind_info,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN osm_vend_mad_send_err_callback_t send_err_callback,
		IN void *context)
{
	osmv_bind_obj_t *p_bo;
	cl_status_t cl_st;
	cl_list_obj_t *p_obj;
	uint8_t hca_idx = 0;

	if (NULL == p_vend || NULL == p_bind_info || NULL == p_mad_pool
	    || NULL == mad_recv_callback || NULL == send_err_callback) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 7402: "
			"NULL parameter passed in: p_vend=%p p_bind_info=%p p_mad_pool=%p recv_cb=%p send_err_cb=%p\n",
			p_vend, p_bind_info, p_mad_pool, mad_recv_callback,
			send_err_callback);

		return OSM_BIND_INVALID_HANDLE;
	}

	p_bo = malloc(sizeof(osmv_bind_obj_t));
	if (NULL == p_bo) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 7403: "
			"could not allocate the bind object\n");
		return OSM_BIND_INVALID_HANDLE;
	}

	memset(p_bo, 0, sizeof(osmv_bind_obj_t));
	p_bo->p_vendor = p_vend;
	p_bo->recv_cb = mad_recv_callback;
	p_bo->send_err_cb = send_err_callback;
	p_bo->cb_context = context;
	p_bo->p_osm_pool = p_mad_pool;
	p_bo->port_num = 1;	/* anafa2 has one port */
	p_bo->hca_hndl = 0;	/* only one ca on anafa system */

	/* obtain the hca name and port num from the guid */
	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"osm_vendor_bind: "
		"Finding CA and Port that owns port guid 0x%" PRIx64 ".\n",
		cl_ntoh64(p_bind_info->port_guid));

	p_bo->is_closing = FALSE;
	cl_spinlock_construct(&(p_bo->lock));
	cl_st = cl_spinlock_init(&(p_bo->lock));
	if (cl_st != CL_SUCCESS) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 7405: "
			"could not initialize the spinlock ...\n");
		free(p_bo);
		return OSM_BIND_INVALID_HANDLE;
	}

	osm_log(p_bo->p_vendor->p_log, OSM_LOG_DEBUG,
		"osm_vendor_bind: osmv_txnmgr_init ... \n");
	if (osmv_txnmgr_init(&p_bo->txn_mgr, p_vend->p_log, &(p_bo->lock)) !=
	    IB_SUCCESS) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 7406: "
			"osmv_txnmgr_init failed \n");
		cl_spinlock_destroy(&p_bo->lock);
		free(p_bo);
		return OSM_BIND_INVALID_HANDLE;
	}

	/* Do the real job! (Transport-dependent) */
	if (IB_SUCCESS !=
	    osmv_transport_init(p_bind_info, OSMV_ANAFA_ID, hca_idx, p_bo)) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 7407: "
			"osmv_transport_init failed \n");
		osmv_txnmgr_done((osm_bind_handle_t) p_bo);
		cl_spinlock_destroy(&p_bo->lock);
		free(p_bo);
		return OSM_BIND_INVALID_HANDLE;
	}

	/* insert bind handle into db */
	p_obj = malloc(sizeof(cl_list_obj_t));
	if (NULL == p_obj) {

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 7408: "
			"osm_vendor_bind: could not allocate the list object\n");

		osmv_transport_done(p_bo->p_transp_mgr);
		osmv_txnmgr_done((osm_bind_handle_t) p_bo);
		cl_spinlock_destroy(&p_bo->lock);
		free(p_bo);
		return OSM_BIND_INVALID_HANDLE;
	}
	if (p_obj)
		memset(p_obj, 0, sizeof(cl_list_obj_t));
	cl_qlist_set_obj(p_obj, p_bo);

	cl_qlist_insert_head(&p_vend->bind_handles, &p_obj->list_item);

	return (osm_bind_handle_t) p_bo;
}

/*
 *  NAME            osm_vendor_unbind
 *
 *  DESCRIPTION     Destroy the bind object and remove it from the vendor's list
 */

void osm_vendor_unbind(IN osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_log_t *p_log = p_bo->p_vendor->p_log;
	cl_list_obj_t *p_obj;
	cl_list_item_t *p_item, *p_item_tmp;
	cl_qlist_t *const p_bh_list =
	    (cl_qlist_t * const)&p_bo->p_vendor->bind_handles;

	OSM_LOG_ENTER(p_log);

	/* go over all the items in the list and remove the specific item */
	p_item = cl_qlist_head(&p_bo->p_vendor->bind_handles);
	while (p_item != cl_qlist_end(&p_bo->p_vendor->bind_handles)) {
		p_obj = PARENT_STRUCT(p_item, cl_list_obj_t, list_item);
		if (cl_qlist_obj(p_obj) == h_bind) {
			break;
		}
		p_item_tmp = cl_qlist_next(p_item);
		p_item = p_item_tmp;
	}

	CL_ASSERT(p_item != cl_qlist_end(p_bh_list));

	cl_qlist_remove_item(p_bh_list, p_item);
	free(p_obj);

	__osm_vendor_internal_unbind(h_bind);

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
}

/*
 *  NAME            osm_vendor_get
 *
 *  DESCRIPTION     Allocate the space for a new MAD
 */

ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t mad_size,
			 IN osm_vend_wrap_t * const p_vw)
{
	ib_mad_t *p_mad;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_vendor_t const *p_vend = p_bo->p_vendor;
	uint32_t act_mad_size;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);

	if (mad_size < MAD_BLOCK_SIZE) {
		/* Stupid, but the applications want that! */
		act_mad_size = MAD_BLOCK_SIZE;
	} else {
		act_mad_size = mad_size;
	}

	/* allocate it */
	p_mad = (ib_mad_t *) malloc(act_mad_size);
	if (p_mad == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get: ERR 7409: "
			"Error Obtaining MAD buffer.\n");
		goto Exit;
	}

	memset(p_mad, 0, act_mad_size);

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_get: "
			"Allocated MAD %p, size = %u.\n", p_mad, act_mad_size);
	}
	p_vw->p_mad = p_mad;

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (p_mad);
}

/*
 *  NAME            osm_vendor_send
 *
 *  DESCRIPTION     Send a MAD buffer (RMPP or simple send).
 *
 *                  Semantics:
 *                   (1) The RMPP send completes when every segment
 *                       is acknowledged (synchronous)
 *                   (2) The simple send completes when the send completion
 *                       is received (asynchronous)
 */

ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_madw_t * const p_madw, IN boolean_t const resp_expected)
{
	ib_api_status_t ret = IB_SUCCESS;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	boolean_t is_rmpp = FALSE, is_rmpp_ds = FALSE;
	osmv_txn_ctx_t *p_txn = NULL;
	ib_mad_t *p_mad;

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);

	if (NULL == h_bind || NULL == p_madw ||
	    NULL == (p_mad = osm_madw_get_mad_ptr(p_madw)) ||
	    NULL == osm_madw_get_mad_addr_ptr(p_madw)) {

		return IB_INVALID_PARAMETER;
	}

	is_rmpp = (p_madw->mad_size > MAD_BLOCK_SIZE
		   || osmv_mad_is_rmpp(p_mad));
	is_rmpp_ds = (TRUE == is_rmpp && TRUE == resp_expected);

	/* Make our operations with the send context atomic */
	osmv_txn_lock(p_bo);

	if (TRUE == p_bo->is_closing) {
		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_send: ERR 7410: "
			"The handle %p is being unbound, cannot send.\n",
			h_bind);
		ret = IB_INTERRUPTED;
		goto send_done;
	}

	if (TRUE == resp_expected || TRUE == is_rmpp) {

		/* We must run under a transaction framework.
		 * Get the transaction object (old or new) */
		ret = __osmv_get_send_txn(h_bind, p_madw, is_rmpp,
					  resp_expected, &p_txn);
		if (IB_SUCCESS != ret) {
			goto send_done;
		}
	}

	if (TRUE == is_rmpp) {
		/* Do the job - RMPP!
		 * The call returns as all the packets are ACK'ed/upon error
		 * The txn lock will be released each time the function sleeps
		 * and re-acquired when it wakes up
		 */
		ret = osmv_rmpp_send_madw(h_bind, p_madw, p_txn, is_rmpp_ds);
	} else {

		/* Do the job - single MAD!
		 * The call returns as soon as the MAD is put on the wire
		 */
		ret = osmv_simple_send_madw(h_bind, p_madw, p_txn, FALSE);	/* anafa2 */
	}

	if (IB_SUCCESS == ret) {

		if ((TRUE == is_rmpp) && (FALSE == is_rmpp_ds)) {
			/* For double-sided sends, the txn continues to live */
			osmv_txn_done(h_bind, osmv_txn_get_key(p_txn),
				      FALSE /*not in callback */ );
		}

		if (FALSE == resp_expected) {
			osm_mad_pool_put(p_bo->p_osm_pool, p_madw);
		}
	} else {
		if (NULL != p_txn) {
			osmv_txn_done(h_bind, osmv_txn_get_key(p_txn),
				      FALSE /*not in callback */ );
		}

		osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
			"osm_vendor_send: ERR 7411: failed to send MADW %p\n",
			p_madw);

		if (TRUE == resp_expected) {
			/* Change the status on the p_madw */
			p_madw->status = ret;
			/* Only the requester expects the error callback */
			p_bo->send_err_cb(p_bo->cb_context, p_madw);
		} else {
			/* put back the mad - it is useless ... */
			osm_mad_pool_put(p_bo->p_osm_pool, p_madw);
		}
	}

send_done:

	osmv_txn_unlock(p_bo);

	OSM_LOG_EXIT(p_bo->p_vendor->p_log);
	return ret;
}

/*
 *  NAME            osm_vendor_put
 *
 *  DESCRIPTION     Free the MAD's memory
 */

void
osm_vendor_put(IN osm_bind_handle_t h_bind, IN osm_vend_wrap_t * const p_vw)
{

	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_vendor_t const *p_vend = p_bo->p_vendor;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);
	CL_ASSERT(p_vw->p_mad);

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_put: " "Retiring MAD %p.\n", p_vw->p_mad);
	}

	free(p_vw->p_mad);
	p_vw->p_mad = NULL;

	OSM_LOG_EXIT(p_vend->p_log);
}

/*
 *  NAME            osm_vendor_local_lid_change
 *
 *  DESCRIPTION     Notifies the vendor transport layer that the local address
 *                  has changed.  This allows the vendor layer to perform
 *                  housekeeping functions such as address vector updates.
 */

ib_api_status_t osm_vendor_local_lid_change(IN osm_bind_handle_t h_bind)
{
	osm_vendor_t const *p_vend = ((osmv_bind_obj_t *) h_bind)->p_vendor;
	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_vendor_local_lid_change: " "Change of LID.\n");

	OSM_LOG_EXIT(p_vend->p_log);

	return (IB_SUCCESS);

}

/*
 *  NAME            osm_vendor_set_sm
 *
 *  DESCRIPTION     Modifies the port info for the bound port to set the "IS_SM" bit.
 */

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_vendor_t const *p_vend = p_bo->p_vendor;
	osmv_TOPSPIN_ANAFA_transport_mgr_t *p_mgr;
	int ioctl_ret;
	osm_ts_set_port_info_ioctl port_info;

	OSM_LOG_ENTER(p_vend->p_log);

	port_info.port = 0;	/* anafa has only 1 port */
	port_info.port_info.valid_fields = IB_PORT_IS_SM;
	port_info.port_info.is_sm = is_sm_val;

	p_mgr = (osmv_TOPSPIN_ANAFA_transport_mgr_t *) p_bo->p_transp_mgr;
	ioctl_ret = ioctl(p_mgr->device_fd, TS_IB_IOCSPORTINFO, &port_info);

	if (ioctl_ret < 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_set_sm: ERR 7412: "
			"Unable set 'IS_SM' bit to:%u in port attributes (%d). errno=%d\n",
			is_sm_val, ioctl_ret, errno);
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

/*
 *  NAME            __osm_vendor_internal_unbind
 *
 *  DESCRIPTION     Destroying a bind:
 *                    (1) Wait for the completion of the sends in flight
 *                    (2) Destroy the associated data structures
 */

static void __osm_vendor_internal_unbind(osm_bind_handle_t h_bind)
{
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	osm_log_t *p_log = p_bo->p_vendor->p_log;

	OSM_LOG_ENTER(p_log);

	/* "notifying" all that from now on no new sends can be done */
	p_bo->txn_mgr.p_event_wheel->closing = TRUE;

	osmv_txn_lock(p_bo);
	p_bo->is_closing = TRUE;

	/* notifying all sleeping rmpp sends to exit */
	osmv_txn_abort_rmpp_txns(h_bind);

	/*  frees all data in bind handle   */
	osm_log(p_log, OSM_LOG_DEBUG,
		"__osm_vendor_internal_unbind: destroying transport mgr.. \n");
	osmv_txn_unlock(p_bo);

	osmv_transport_done(h_bind);
	osm_log(p_log, OSM_LOG_DEBUG,
		"__osm_vendor_internal_unbind: destroying txn mgr.. \n");
	osmv_txn_lock(p_bo);
	osmv_txnmgr_done(h_bind);
	osm_log(p_log, OSM_LOG_DEBUG,
		"__osm_vendor_internal_unbind: destroying bind lock.. \n");

	osmv_txn_unlock(p_bo);
	/*
	   we intentionally let the p_bo and its lock leak -
	   as we did not implement a way to track active bind handles provided to
	   the client - and the client might use them

	   cl_spinlock_destroy(&p_bo->lock);
	   free(p_bo);
	 */

	OSM_LOG_EXIT(p_log);
}

/*
 *  NAME            __osmv_get_send_txn
 *
 *  DESCRIPTION     Return a transaction object that corresponds to this MAD.
 *                  Optionally, create it, if the new request (query) is sent or received.
 */

static ib_api_status_t
__osmv_get_send_txn(IN osm_bind_handle_t h_bind,
		    IN osm_madw_t * const p_madw,
		    IN boolean_t is_rmpp,
		    IN boolean_t resp_expected, OUT osmv_txn_ctx_t ** pp_txn)
{
	ib_api_status_t ret;
	uint64_t tid, key;
	osmv_bind_obj_t *p_bo = (osmv_bind_obj_t *) h_bind;
	ib_mad_t *p_mad = osm_madw_get_mad_ptr(p_madw);

	OSM_LOG_ENTER(p_bo->p_vendor->p_log);
	CL_ASSERT(NULL != pp_txn);

	key = tid = cl_ntoh64(p_mad->trans_id);
	if (TRUE == resp_expected) {
		/* Create a unique identifier at the requester side */
		key = osmv_txn_uniq_key(tid);
	}

	/* We must run under a transaction framework */
	ret = osmv_txn_lookup(h_bind, key, pp_txn);
	if (IB_NOT_FOUND == ret) {
		/* Generally, we start a new transaction */
		ret = osmv_txn_init(h_bind, tid, key, pp_txn);
		if (IB_SUCCESS != ret) {
			goto get_send_txn_done;
		}
	} else {
		CL_ASSERT(NULL != *pp_txn);
		/* The transaction context exists.
		 * This is legal only if I am going to return an
		 * (RMPP?) reply to an RMPP request sent by the other part
		 * (double-sided RMPP transfer)
		 */
		if (FALSE == is_rmpp
		    || FALSE == osmv_txn_is_rmpp_init_by_peer(*pp_txn)) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"__osmv_get_send_txn: ERR 7413: "
				"The transaction id=0x%llX is not unique. Send failed.\n",
				tid);

			ret = IB_INVALID_SETTING;
			goto get_send_txn_done;
		}

		if (TRUE == resp_expected) {
			osm_log(p_bo->p_vendor->p_log, OSM_LOG_ERROR,
				"__osmv_get_send_txn: ERR 7414: "
				"The transaction id=%llX can\'t expect a response. Send failed.\n",
				tid);

			ret = IB_INVALID_PARAMETER;
			goto get_send_txn_done;
		}
	}

	if (TRUE == is_rmpp) {
		ret = osmv_txn_init_rmpp_sender(h_bind, *pp_txn, p_madw);
		if (IB_SUCCESS != ret) {
			osmv_txn_done(h_bind, tid, FALSE);
			goto get_send_txn_done;
		}
	}

	/* Save a reference to the MAD in the txn context
	 * We'll need to match it in two cases:
	 *  (1) When the response is returned, if I am the requester
	 *  (2) In RMPP retransmissions
	 */
	osmv_txn_set_madw(*pp_txn, p_madw);

get_send_txn_done:
	OSM_LOG_EXIT(p_bo->p_vendor->p_log);

	return ret;
}

void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level)
{

}
