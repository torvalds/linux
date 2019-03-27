/*
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2010 Mellanox Technologies LTD. All rights reserved.
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

/*
 * Abstract:
 *    Implementation of osm_vl15_t.
 * This object represents the VL15 Interface object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_thread.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_VL15INTF_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>

static void vl15_send_mad(osm_vl15_t * p_vl, osm_madw_t * p_madw)
{
	ib_api_status_t status;
	boolean_t resp_expected = p_madw->resp_expected;
	ib_smp_t * p_smp;
	ib_net16_t attr_id;
	uint8_t method;

	p_smp = osm_madw_get_smp_ptr(p_madw);
	method = p_smp->method;
	attr_id = p_smp->attr_id;

	/*
	   Non-response-expected mads are not throttled on the wire
	   since we can have no confirmation that they arrived
	   at their destination.
	 */
	if (resp_expected)
		/*
		   Note that other threads may not see the response MAD
		   arrive before send() even returns.
		   In that case, the wire count would temporarily go negative.
		   To avoid this confusion, preincrement the counts on the
		   assumption that send() will succeed.
		 */
		cl_atomic_inc(&p_vl->p_stats->qp0_mads_outstanding_on_wire);
	else
		cl_atomic_inc(&p_vl->p_stats->qp0_unicasts_sent);

	cl_atomic_inc(&p_vl->p_stats->qp0_mads_sent);

	status = osm_vendor_send(osm_madw_get_bind_handle(p_madw),
				 p_madw, p_madw->resp_expected);

	if (status == IB_SUCCESS) {
		OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG,
			"%u QP0 MADs on wire, %u outstanding, "
			"%u unicasts sent, %u total sent\n",
			p_vl->p_stats->qp0_mads_outstanding_on_wire,
			p_vl->p_stats->qp0_mads_outstanding,
			p_vl->p_stats->qp0_unicasts_sent,
			p_vl->p_stats->qp0_mads_sent);
		return;
	}

	OSM_LOG(p_vl->p_log, OSM_LOG_ERROR, "ERR 3E03: "
		"MAD send failed (%s)\n", ib_get_err_str(status));

	/*
	   The MAD was never successfully sent, so
	   fix up the pre-incremented count values.
	 */

	/* Decrement qp0_mads_sent that were incremented in the code above.
	   qp0_mads_outstanding will be decremented by send error callback
	   (called by osm_vendor_send() */
	cl_atomic_dec(&p_vl->p_stats->qp0_mads_sent);
	if (!resp_expected) {
		cl_atomic_dec(&p_vl->p_stats->qp0_unicasts_sent);
		return;
	}

	/* need to cause heavy-sweep if resp_expected MAD sending failed */
	OSM_LOG(p_vl->p_log, OSM_LOG_ERROR, "ERR 3E04: "
		"%s method failed for attribute 0x%X (%s)\n",
		method == IB_MAD_METHOD_SET ? "SET" : "GET",
		cl_ntoh16(attr_id), ib_get_sm_attr_str(attr_id));

	p_vl->p_subn->subnet_initialization_error = TRUE;

}

static void vl15_poller(IN void *p_ptr)
{
	ib_api_status_t status;
	osm_madw_t *p_madw;
	osm_vl15_t *p_vl = p_ptr;
	cl_qlist_t *p_fifo;
	int32_t max_smps = p_vl->max_wire_smps;
	int32_t max_smps2 = p_vl->max_wire_smps2;

	OSM_LOG_ENTER(p_vl->p_log);

	if (p_vl->thread_state == OSM_THREAD_STATE_NONE)
		p_vl->thread_state = OSM_THREAD_STATE_RUN;

	while (p_vl->thread_state == OSM_THREAD_STATE_RUN) {
		/*
		   Start servicing the FIFOs by pulling off MAD wrappers
		   and passing them to the transport interface.
		   There are lots of corner cases here so tread carefully.

		   The unicast FIFO has priority, since somebody is waiting
		   for a timely response.
		 */
		cl_spinlock_acquire(&p_vl->lock);

		if (cl_qlist_count(&p_vl->ufifo) != 0)
			p_fifo = &p_vl->ufifo;
		else
			p_fifo = &p_vl->rfifo;

		p_madw = (osm_madw_t *) cl_qlist_remove_head(p_fifo);

		cl_spinlock_release(&p_vl->lock);

		if (p_madw != (osm_madw_t *) cl_qlist_end(p_fifo)) {
			OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG,
				"Servicing p_madw = %p\n", p_madw);
			if (OSM_LOG_IS_ACTIVE_V2(p_vl->p_log, OSM_LOG_FRAMES))
				osm_dump_dr_smp_v2(p_vl->p_log,
						   osm_madw_get_smp_ptr(p_madw),
						   FILE_ID, OSM_LOG_FRAMES);

			vl15_send_mad(p_vl, p_madw);
		} else
			/*
			   The VL15 FIFO is empty, so we have nothing left to do.
			 */
			status = cl_event_wait_on(&p_vl->signal,
						  EVENT_NO_TIMEOUT, TRUE);

		while (p_vl->p_stats->qp0_mads_outstanding_on_wire >= max_smps &&
		       p_vl->thread_state == OSM_THREAD_STATE_RUN) {
			status = cl_event_wait_on(&p_vl->signal,
						  p_vl->max_smps_timeout,
						  TRUE);
			if (status == CL_TIMEOUT) {
				if (max_smps < max_smps2)
					max_smps++;
				break;
			} else if (status != CL_SUCCESS) {
				OSM_LOG(p_vl->p_log, OSM_LOG_ERROR, "ERR 3E02: "
					"Event wait failed (%s)\n",
					CL_STATUS_MSG(status));
				break;
			}
			max_smps = p_vl->max_wire_smps;
		}
	}

	/*
	   since we abort immediately when the state != OSM_THREAD_STATE_RUN
	   we might have some mads on the queues. After the thread exits
	   the vl15 destroy routine should put these mads back...
	 */

	OSM_LOG_EXIT(p_vl->p_log);
}

void osm_vl15_construct(IN osm_vl15_t * p_vl)
{
	memset(p_vl, 0, sizeof(*p_vl));
	p_vl->state = OSM_VL15_STATE_INIT;
	p_vl->thread_state = OSM_THREAD_STATE_NONE;
	cl_event_construct(&p_vl->signal);
	cl_spinlock_construct(&p_vl->lock);
	cl_qlist_init(&p_vl->rfifo);
	cl_qlist_init(&p_vl->ufifo);
	cl_thread_construct(&p_vl->poller);
}

void osm_vl15_destroy(IN osm_vl15_t * p_vl, IN struct osm_mad_pool *p_pool)
{
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_vl->p_log);

	/*
	   Signal our threads that we're leaving.
	 */
	p_vl->thread_state = OSM_THREAD_STATE_EXIT;

	/*
	   Don't trigger unless event has been initialized.
	   Destroy the thread before we tear down the other objects.
	 */
	if (p_vl->state != OSM_VL15_STATE_INIT)
		cl_event_signal(&p_vl->signal);

	cl_thread_destroy(&p_vl->poller);

	/*
	   Return the outstanding messages to the pool
	 */

	cl_spinlock_acquire(&p_vl->lock);

	while (!cl_is_qlist_empty(&p_vl->rfifo)) {
		p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_vl->rfifo);
		osm_mad_pool_put(p_pool, p_madw);
	}
	while (!cl_is_qlist_empty(&p_vl->ufifo)) {
		p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_vl->ufifo);
		osm_mad_pool_put(p_pool, p_madw);
	}

	cl_spinlock_release(&p_vl->lock);

	cl_event_destroy(&p_vl->signal);
	p_vl->state = OSM_VL15_STATE_INIT;
	cl_spinlock_destroy(&p_vl->lock);

	OSM_LOG_EXIT(p_vl->p_log);
}

ib_api_status_t osm_vl15_init(IN osm_vl15_t * p_vl, IN osm_vendor_t * p_vend,
			      IN osm_log_t * p_log, IN osm_stats_t * p_stats,
			      IN osm_subn_t * p_subn,
			      IN int32_t max_wire_smps,
			      IN int32_t max_wire_smps2,
			      IN uint32_t max_smps_timeout)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log);

	p_vl->p_vend = p_vend;
	p_vl->p_log = p_log;
	p_vl->p_stats = p_stats;
	p_vl->p_subn = p_subn;
	p_vl->max_wire_smps = max_wire_smps;
	p_vl->max_wire_smps2 = max_wire_smps2;
	p_vl->max_smps_timeout = max_wire_smps < max_wire_smps2 ?
				 max_smps_timeout : EVENT_NO_TIMEOUT;

	status = cl_event_init(&p_vl->signal, FALSE);
	if (status != IB_SUCCESS)
		goto Exit;

	p_vl->state = OSM_VL15_STATE_READY;

	status = cl_spinlock_init(&p_vl->lock);
	if (status != IB_SUCCESS)
		goto Exit;

	/*
	   Initialize the thread after all other dependent objects
	   have been initialized.
	 */
	status = cl_thread_init(&p_vl->poller, vl15_poller, p_vl,
				"opensm poller");
Exit:
	OSM_LOG_EXIT(p_log);
	return status;
}

void osm_vl15_poll(IN osm_vl15_t * p_vl)
{
	OSM_LOG_ENTER(p_vl->p_log);

	CL_ASSERT(p_vl->state == OSM_VL15_STATE_READY);

	/*
	   If we have room for more VL15 MADs on the wire,
	   then signal the poller thread.

	   This is not an airtight check, since the poller thread
	   could be just about to send another MAD as we signal
	   the event here.  To cover this rare case, the poller
	   thread checks for a spurious wake-up.
	 */
	if (p_vl->p_stats->qp0_mads_outstanding_on_wire <
	    (int32_t) p_vl->max_wire_smps) {
		OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG,
			"Signalling poller thread\n");
		cl_event_signal(&p_vl->signal);
	}

	OSM_LOG_EXIT(p_vl->p_log);
}

void osm_vl15_post(IN osm_vl15_t * p_vl, IN osm_madw_t * p_madw)
{
	OSM_LOG_ENTER(p_vl->p_log);

	CL_ASSERT(p_vl->state == OSM_VL15_STATE_READY);

	OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG, "Posting p_madw = %p\n", p_madw);

	/*
	   Determine in which fifo to place the pending madw.
	 */
	cl_spinlock_acquire(&p_vl->lock);
	if (p_madw->resp_expected == TRUE) {
		cl_qlist_insert_tail(&p_vl->rfifo, &p_madw->list_item);
		osm_stats_inc_qp0_outstanding(p_vl->p_stats);
	} else
		cl_qlist_insert_tail(&p_vl->ufifo, &p_madw->list_item);
	cl_spinlock_release(&p_vl->lock);

	OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG,
		"%u QP0 MADs on wire, %u QP0 MADs outstanding\n",
		p_vl->p_stats->qp0_mads_outstanding_on_wire,
		p_vl->p_stats->qp0_mads_outstanding);

	osm_vl15_poll(p_vl);

	OSM_LOG_EXIT(p_vl->p_log);
}

void osm_vl15_shutdown(IN osm_vl15_t * p_vl, IN osm_mad_pool_t * p_mad_pool)
{
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_vl->p_log);

	/* we only should get here after the VL15 interface was initialized */
	CL_ASSERT(p_vl->state == OSM_VL15_STATE_READY);

	/* grab a lock on the object */
	cl_spinlock_acquire(&p_vl->lock);

	/* go over all outstanding MADs and retire their transactions */

	/* first we handle the list of response MADs */
	p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_vl->ufifo);
	while (p_madw != (osm_madw_t *) cl_qlist_end(&p_vl->ufifo)) {
		OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG,
			"Releasing Response p_madw = %p\n", p_madw);

		osm_mad_pool_put(p_mad_pool, p_madw);

		p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_vl->ufifo);
	}

	/* Request MADs we send out */
	p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_vl->rfifo);
	while (p_madw != (osm_madw_t *) cl_qlist_end(&p_vl->rfifo)) {
		OSM_LOG(p_vl->p_log, OSM_LOG_DEBUG,
			"Releasing Request p_madw = %p\n", p_madw);

		osm_mad_pool_put(p_mad_pool, p_madw);
		osm_stats_dec_qp0_outstanding(p_vl->p_stats);

		p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_vl->rfifo);
	}

	/* free the lock */
	cl_spinlock_release(&p_vl->lock);

	OSM_LOG_EXIT(p_vl->p_log);
}
