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

#include <math.h>
#include <stdlib.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_log.h>
#include <vendor/osm_vendor.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_mad_pool.h>

#if defined(OSM_VENDOR_INTF_MTL) | defined(OSM_VENDOR_INTF_TS)

#include <vendor/osm_vendor_mtl_transaction_mgr.h>
#ifdef OSM_VENDOR_INTF_MTL
#include <vendor/osm_mtl_bind.h>
#endif

/* this is the callback function of the timer */
void __osm_transaction_mgr_callback(IN void *context)
{
	osm_transaction_mgr_t *trans_mgr_p;
	osm_vendor_t *p_vend = (osm_vendor_t *) context;
	cl_list_item_t *p_list_item;
	cl_list_item_t *p_list_next_item;
	osm_madw_req_t *osm_madw_req_p;
	uint64_t current_time;	/*  [usec] */
	uint32_t new_timeout;	/*  [msec] */
	cl_status_t cl_status;
	ib_mad_t *p_mad;
#ifdef OSM_VENDOR_INTF_MTL
	osm_mtl_bind_info_t *p_bind;
#else
	osm_ts_bind_info_t *p_bind;
#endif
	cl_list_t tmp_madw_p_list;	/*  this list will include all the madw_p that should be removed. */
	cl_list_t retry_madw_p_list;	/*  this list will include all the madw_p that were retried and need to be removed. */
	osm_madw_t *madw_p;

	OSM_LOG_ENTER(p_vend->p_log);

	trans_mgr_p = (osm_transaction_mgr_t *) p_vend->p_transaction_mgr;

	/*  initialize the tmp_madw_p_list */
	cl_list_construct(&tmp_madw_p_list);
	cl_status = cl_list_init(&tmp_madw_p_list, 50);
	if (cl_status != CL_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_transaction_mgr_callback : ERROR 1000: "
			"Failed to create tmp_madw_p_list\n");
	}

	cl_list_construct(&retry_madw_p_list);
	cl_status = cl_list_init(&retry_madw_p_list, 50);
	if (cl_status != CL_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_transaction_mgr_callback : ERROR 1000: "
			"Failed to create retry_madw_p_list\n");
	}

	current_time = cl_get_time_stamp();
	cl_spinlock_acquire(&(trans_mgr_p->transaction_mgr_lock));
	p_list_item = cl_qlist_head(trans_mgr_p->madw_reqs_list_p);
	if (p_list_item == cl_qlist_end(trans_mgr_p->madw_reqs_list_p)) {
		/*  the list is empty - nothing to do */
		cl_spinlock_release(&trans_mgr_p->transaction_mgr_lock);
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"__osm_transaction_mgr_callback : Nothing to do\n");
		goto Exit;
	}

	/*  non empty list: */

	/*  get the osm_madw_req_p  */
	osm_madw_req_p = PARENT_STRUCT(p_list_item, osm_madw_req_t, list_item);

	while (osm_madw_req_p->waking_time <= current_time) {
		/*  this object was supposed to have gotten a response */
		/*  we need to decide if we need to retry or done with it. */
		if (osm_madw_req_p->retry_cnt > 0) {
			/*  add to the list of the retrys : */
			cl_list_insert_tail(&retry_madw_p_list, osm_madw_req_p);

			/*  update wakeup time and retry count */
			osm_madw_req_p->waking_time =
			    p_vend->timeout * 1000 + cl_get_time_stamp();
			osm_madw_req_p->retry_cnt--;

			/*  make sure we will get some timer call if not earlier */
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"__osm_transaction_mgr_callback : Timer restart:%u\n",
				p_vend->timeout);

			cl_status =
			    cl_timer_start(&trans_mgr_p->madw_list_timer,
					   p_vend->timeout);

			/*  go to the next object and check if it also needs to be removed - didn't receive response */
			/*  we need to do it before we move current item to the end of the list */
			p_list_next_item = cl_qlist_next(p_list_item);

			/*  remove from the head */
			cl_qlist_remove_item(trans_mgr_p->madw_reqs_list_p,
					     &(osm_madw_req_p->list_item));

			/*  insert the object to the qlist and the qmap */
			cl_qlist_insert_tail(trans_mgr_p->madw_reqs_list_p,
					     &(osm_madw_req_p->list_item));

		} else {
			/*  go to the next object and check if it also needs to be removed - didn't receive response */
			p_list_next_item = cl_qlist_next(p_list_item);

			/*  remove from the head */
			cl_qlist_remove_item(trans_mgr_p->madw_reqs_list_p,
					     &(osm_madw_req_p->list_item));

			/*  add it to the tmp_madw_p_list to be removed */
			cl_list_insert_tail(&tmp_madw_p_list,
					    osm_madw_req_p->p_madw);
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"__osm_transaction_mgr_callback : Found failed transaction madw: %p\n",
				osm_madw_req_p->p_madw);
		}

		/*  Advance */
		p_list_item = p_list_next_item;
		if (p_list_item == cl_qlist_end(trans_mgr_p->madw_reqs_list_p)) {
			/*  the list is empty - nothing to do */
			break;
		}

		/*  get the osm_madw_req_p  */
		osm_madw_req_p =
		    PARENT_STRUCT(p_list_item, osm_madw_req_t, list_item);
	}

	/*  look at the current p_list_item. If it is not the end item - then we need to  */
	/*  re-start the timer */
	if (p_list_item != cl_qlist_end(trans_mgr_p->madw_reqs_list_p)) {
		/*  get the osm_madw_req_p  */
		osm_madw_req_p =
		    PARENT_STRUCT(p_list_item, osm_madw_req_t, list_item);

		/*  we have the object that still didn't get response - re-start the timer */
		/*  start the timer to the timeout (in miliseconds) */
		new_timeout =
		    (osm_madw_req_p->waking_time - cl_get_time_stamp()) / 1000 +
		    1;
		cl_status =
		    cl_timer_start(&trans_mgr_p->madw_list_timer, new_timeout);
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"__osm_transaction_mgr_callback : Timer restart:%u\n",
			new_timeout);

		if (cl_status != CL_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"__osm_transaction_mgr_callback : ERROR 1000: "
				"Failed to start timer\n");
		}
	}
	/* if not empty - retry on retry list: */
	if (!cl_is_list_empty(&retry_madw_p_list)) {

		/*  remove all elements that were retried: */
		osm_madw_req_p =
		    (osm_madw_req_t
		     *) (cl_list_remove_head(&retry_madw_p_list));
		while (osm_madw_req_p != NULL) {

			/*  resend: */
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"__osm_transaction_mgr_callback : "
				"Retry %d of madw %p\n",
				OSM_DEFAULT_RETRY_COUNT -
				osm_madw_req_p->retry_cnt,
				osm_madw_req_p->p_madw);

			/*  actually send it */
#ifdef OSM_VENDOR_INTF_MTL
			osm_mtl_send_mad((osm_mtl_bind_info_t *)
					 osm_madw_req_p->p_bind,
					 osm_madw_req_p->p_madw);
#else
			ib_api_status_t
			    osm_ts_send_mad(osm_ts_bind_info_t * p_bind,
					    osm_madw_t * const p_madw);
			osm_ts_send_mad((osm_ts_bind_info_t *) osm_madw_req_p->
					p_bind, osm_madw_req_p->p_madw);
#endif
			/*  next one */
			osm_madw_req_p =
			    (osm_madw_req_t
			     *) (cl_list_remove_head(&retry_madw_p_list));
		}
	}

	/*  if the tmp_madw_p_list has elements - need to call the send_err_callback */
	madw_p = (osm_madw_t *) (cl_list_remove_head(&tmp_madw_p_list));
	while (madw_p != NULL) {
		/*  need to remove it from pool */

		/* obtain the madw_p stored as the wrid in the send call */
		p_mad = osm_madw_get_mad_ptr(madw_p);
		p_bind = madw_p->h_bind;
		/*
		   Return any wrappers to the pool that may have been
		   pre-emptively allocated to handle a receive.
		 */
		if (madw_p->vend_wrap.p_resp_madw) {
#ifdef OSM_VENDOR_INTF_MTL
			osm_mad_pool_put(p_bind->p_osm_pool,
					 madw_p->vend_wrap.p_resp_madw);
#else
			osm_mad_pool_put(p_bind->p_osm_pool,
					 madw_p->vend_wrap.p_resp_madw);
#endif
			madw_p->vend_wrap.p_resp_madw = NULL;
		}

		/* invoke the CB */
		(*(osm_vend_mad_send_err_callback_t)
		 (p_bind->send_err_callback)) (p_bind->client_context, madw_p);
		madw_p = (osm_madw_t *) (cl_list_remove_head(&tmp_madw_p_list));
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);

}

/*
 * Construct and Initialize
 */

void osm_transaction_mgr_init(IN osm_vendor_t * const p_vend)
{
	cl_status_t cl_status;
	osm_transaction_mgr_t *trans_mgr_p;
	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend->p_transaction_mgr == NULL);

	(osm_transaction_mgr_t *) p_vend->p_transaction_mgr =
	    (osm_transaction_mgr_t *) malloc(sizeof(osm_transaction_mgr_t));

	trans_mgr_p = (osm_transaction_mgr_t *) p_vend->p_transaction_mgr;

	/*  construct lock object  */
	cl_spinlock_construct(&(trans_mgr_p->transaction_mgr_lock));
	CL_ASSERT(cl_spinlock_init(&(trans_mgr_p->transaction_mgr_lock)) ==
		  CL_SUCCESS);

	/*  initialize the qlist */
	trans_mgr_p->madw_reqs_list_p =
	    (cl_qlist_t *) malloc(sizeof(cl_qlist_t));
	cl_qlist_init(trans_mgr_p->madw_reqs_list_p);

	/*  initialize the qmap */
	trans_mgr_p->madw_by_tid_map_p =
	    (cl_qmap_t *) malloc(sizeof(cl_qmap_t));
	cl_qmap_init(trans_mgr_p->madw_by_tid_map_p);

	/*  create the timer used by the madw_req_list */
	cl_timer_construct(&(trans_mgr_p->madw_list_timer));

	/*  init the timer with timeout. */
	cl_status = cl_timer_init(&trans_mgr_p->madw_list_timer,
				  __osm_transaction_mgr_callback, p_vend);

	if (cl_status != CL_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_transaction_mgr_init : ERROR 1000: "
			"Failed to initialize madw_reqs_list timer\n");
	}
	OSM_LOG_EXIT(p_vend->p_log);
}

void osm_transaction_mgr_destroy(IN osm_vendor_t * const p_vend)
{
	osm_transaction_mgr_t *trans_mgr_p;
	cl_list_item_t *p_list_item;
	cl_map_item_t *p_map_item;
	osm_madw_req_t *osm_madw_req_p;

	OSM_LOG_ENTER(p_vend->p_log);

	trans_mgr_p = (osm_transaction_mgr_t *) p_vend->p_transaction_mgr;

	if (p_vend->p_transaction_mgr != NULL) {
		/* we need to get a lock */
		cl_spinlock_acquire(&trans_mgr_p->transaction_mgr_lock);

		/* go over all the items in the list and remove them */
		p_list_item =
		    cl_qlist_remove_head(trans_mgr_p->madw_reqs_list_p);
		while (p_list_item !=
		       cl_qlist_end(trans_mgr_p->madw_reqs_list_p)) {
			osm_madw_req_p = (osm_madw_req_t *) p_list_item;

			if (osm_madw_req_p->p_madw->p_mad)
				osm_log(p_vend->p_log, OSM_LOG_DEBUG,
					"osm_transaction_mgr_destroy: "
					"Found outstanding MADW:%p  TID:<0x%"
					PRIx64 ">.\n", osm_madw_req_p->p_madw,
					osm_madw_req_p->p_madw->p_mad->
					trans_id);
			else
				osm_log(p_vend->p_log, OSM_LOG_DEBUG,
					"osm_transaction_mgr_destroy: "
					"Found outstanding MADW:%p  TID:UNDEFINED.\n",
					osm_madw_req_p->p_madw);

			/*  each item - remove it from the map */
			p_map_item = &(osm_madw_req_p->map_item);
			cl_qmap_remove_item(trans_mgr_p->madw_by_tid_map_p,
					    p_map_item);
			/*  free the item */
			free(osm_madw_req_p);
			p_list_item =
			    cl_qlist_remove_head(trans_mgr_p->madw_reqs_list_p);
		}
		/*  free the qlist and qmap */
		free(trans_mgr_p->madw_reqs_list_p);
		free(trans_mgr_p->madw_by_tid_map_p);
		/*  reliease and destroy the lock */
		cl_spinlock_release(&trans_mgr_p->transaction_mgr_lock);
		cl_spinlock_destroy(&(trans_mgr_p->transaction_mgr_lock));
		/*  destroy the timer */
		cl_timer_trim(&trans_mgr_p->madw_list_timer, 1);
		cl_timer_destroy(&trans_mgr_p->madw_list_timer);
		/*  free the transaction_manager object */
		free(trans_mgr_p);
		trans_mgr_p = NULL;
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

ib_api_status_t
osm_transaction_mgr_insert_madw(IN osm_bind_handle_t * const p_bind,
				IN osm_madw_t * p_madw)
{
#ifdef OSM_VENDOR_INTF_MTL
	osm_vendor_t *const p_vend = ((osm_mtl_bind_info_t *) p_bind)->p_vend;
#else
	osm_vendor_t *const p_vend = ((osm_ts_bind_info_t *) p_bind)->p_vend;
#endif
	osm_transaction_mgr_t *trans_mgr_p;
	osm_madw_req_t *osm_madw_req_p;
	uint64_t timeout;
	uint64_t waking_time;
	cl_status_t cl_status;
	uint64_t key;
	const ib_mad_t *mad_p = p_madw->p_mad;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(mad_p);

	trans_mgr_p = (osm_transaction_mgr_t *) p_vend->p_transaction_mgr;

	timeout = (uint64_t) (p_vend->timeout) * 1000;	/* change the miliseconds value of timeout to microseconds. */
	waking_time = timeout + cl_get_time_stamp();

	osm_madw_req_p = (osm_madw_req_t *) malloc(sizeof(osm_madw_req_t));

	osm_madw_req_p->p_madw = p_madw;
	osm_madw_req_p->waking_time = waking_time;
	osm_madw_req_p->retry_cnt = OSM_DEFAULT_RETRY_COUNT;
	osm_madw_req_p->p_bind = p_bind;

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_transaction_mgr_insert_madw: "
		"Inserting MADW:%p with waking_time: <0x%" PRIx64 ">  TID:<0x%"
		PRIx64 ">.\n", p_madw, waking_time, p_madw->p_mad->trans_id);

	/* Get the lock on the manager */
	cl_spinlock_acquire(&(trans_mgr_p->transaction_mgr_lock));
	/* If the list is empty - need to start the timer with timer of timeout (in miliseconds) */
	if (cl_is_qlist_empty(trans_mgr_p->madw_reqs_list_p)) {
		/*  stop the timer if it is running */
		cl_timer_stop(&trans_mgr_p->madw_list_timer);

		/*  start the timer to the timeout (in miliseconds) */
		cl_status = cl_timer_start(&trans_mgr_p->madw_list_timer,
					   p_vend->timeout);
		if (cl_status != CL_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_transaction_mgr_insert_madw : ERROR 1000: "
				"Failed to start timer\n");
		}
	}

	/*  insert the object to the qlist and the qmap */
	cl_qlist_insert_tail(trans_mgr_p->madw_reqs_list_p,
			     &(osm_madw_req_p->list_item));
	/*  get the key */
	key = (uint64_t) mad_p->trans_id;
	cl_qmap_insert(trans_mgr_p->madw_by_tid_map_p, key,
		       &(osm_madw_req_p->map_item));
	cl_spinlock_release(&trans_mgr_p->transaction_mgr_lock);

	OSM_LOG_EXIT(p_vend->p_log);

	return (IB_SUCCESS);
}

ib_api_status_t
osm_transaction_mgr_erase_madw(IN osm_vendor_t * const p_vend,
			       IN ib_mad_t * p_mad)
{
	osm_transaction_mgr_t *trans_mgr_p;
	osm_madw_req_t *osm_madw_req_p;
	uint64_t key;
	cl_map_item_t *p_map_item;
	OSM_LOG_ENTER(p_vend->p_log);

	trans_mgr_p = (osm_transaction_mgr_t *) p_vend->p_transaction_mgr;

	key = (uint64_t) p_mad->trans_id;
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_transaction_mgr_erase_madw: "
		"Removing TID:<0x%" PRIx64 ">.\n", p_mad->trans_id);

	cl_spinlock_acquire(&trans_mgr_p->transaction_mgr_lock);
	p_map_item = cl_qmap_get(trans_mgr_p->madw_by_tid_map_p, key);
	if (p_map_item != cl_qmap_end(trans_mgr_p->madw_by_tid_map_p)) {
		/*  we found such an item.  */
		/*  get the osm_madw_req_p  */
		osm_madw_req_p =
		    PARENT_STRUCT(p_map_item, osm_madw_req_t, map_item);

		/*  remove the item from the qlist */
		cl_qlist_remove_item(trans_mgr_p->madw_reqs_list_p,
				     &(osm_madw_req_p->list_item));
		/*  remove the item from the qmap */
		cl_qmap_remove_item(trans_mgr_p->madw_by_tid_map_p,
				    &(osm_madw_req_p->map_item));

		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_transaction_mgr_erase_madw: "
			"Removed TID:<0x%" PRIx64 ">.\n", p_mad->trans_id);

		/*  free the item */
		free(osm_madw_req_p);
	} else {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_transaction_mgr_erase_madw: "
			"osm_transaction_mgr_erase_madw:<0x%" PRIx64
			"> NOT FOUND.\n", p_mad->trans_id);
	}
	cl_spinlock_release(&trans_mgr_p->transaction_mgr_lock);
	OSM_LOG_EXIT(p_vend->p_log);

	return (IB_SUCCESS);
}

ib_api_status_t
osm_transaction_mgr_get_madw_for_tid(IN osm_vendor_t * const p_vend,
				     IN ib_mad_t * const p_mad,
				     OUT osm_madw_t ** req_madw_p)
{
	osm_transaction_mgr_t *trans_mgr_p;
	osm_madw_req_t *osm_madw_req_p;
	cl_map_item_t *p_map_item;
	uint64_t key;
	OSM_LOG_ENTER(p_vend->p_log);

	trans_mgr_p = (osm_transaction_mgr_t *) p_vend->p_transaction_mgr;

	*req_madw_p = NULL;

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_transaction_mgr_get_madw_for_tid: "
		"Looking for TID:<0x%" PRIx64 ">.\n", p_mad->trans_id);

	key = (uint64_t) p_mad->trans_id;
	cl_spinlock_acquire(&(trans_mgr_p->transaction_mgr_lock));
	p_map_item = cl_qmap_get(trans_mgr_p->madw_by_tid_map_p, key);
	if (p_map_item != cl_qmap_end(trans_mgr_p->madw_by_tid_map_p)) {
		/*  we found such an item.  */
		/*  get the osm_madw_req_p  */
		osm_madw_req_p =
		    PARENT_STRUCT(p_map_item, osm_madw_req_t, map_item);

		/*  Since the Transaction was looked up and provided for */
		/*  processing we retire it */
		cl_qlist_remove_item(trans_mgr_p->madw_reqs_list_p,
				     &(osm_madw_req_p->list_item));
		/*  remove the item from the qmap */
		cl_qmap_remove_item(trans_mgr_p->madw_by_tid_map_p,
				    &(osm_madw_req_p->map_item));

		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_transaction_mgr_get_madw_for_tid: "
			"Removed TID:<0x%" PRIx64 ">.\n", p_mad->trans_id);

		*req_madw_p = osm_madw_req_p->p_madw;
	}

	cl_spinlock_release(&(trans_mgr_p->transaction_mgr_lock));
	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"osm_transaction_mgr_get_madw_for_tid: "
		"Got MADW:%p.\n", *req_madw_p);
	OSM_LOG_EXIT(p_vend->p_log);
	return (IB_SUCCESS);
}

#endif
