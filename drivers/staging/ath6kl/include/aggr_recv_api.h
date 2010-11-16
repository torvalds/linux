/*
 *
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * All rights reserved.
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 *
 */

#ifndef __AGGR_RECV_API_H__
#define __AGGR_RECV_API_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (* RX_CALLBACK)(void * dev, void *osbuf);

typedef void (* ALLOC_NETBUFS)(A_NETBUF_QUEUE_T *q, A_UINT16 num);

/*
 * aggr_init:
 * Initialises the data structures, allocates data queues and 
 * os buffers. Netbuf allocator is the input param, used by the
 * aggr module for allocation of NETBUFs from driver context.
 * These NETBUFs are used for AMSDU processing.
 * Returns the context for the aggr module.
 */
void *
aggr_init(ALLOC_NETBUFS netbuf_allocator);


/*
 * aggr_register_rx_dispatcher:
 * Registers OS call back function to deliver the
 * frames to OS. This is generally the topmost layer of
 * the driver context, after which the frames go to
 * IP stack via the call back function.
 * This dispatcher is active only when aggregation is ON.
 */
void
aggr_register_rx_dispatcher(void *cntxt, void * dev,  RX_CALLBACK fn);


/*
 * aggr_process_bar:
 * When target receives BAR, it communicates to host driver
 * for modifying window parameters. Target indicates this via the 
 * event: WMI_ADDBA_REQ_EVENTID. Host will dequeue all frames
 * up to the indicated sequence number.
 */
void
aggr_process_bar(void *cntxt, A_UINT8 tid, A_UINT16 seq_no);


/*
 * aggr_recv_addba_req_evt:
 * This event is to initiate/modify the receive side window.
 * Target will send WMI_ADDBA_REQ_EVENTID event to host - to setup 
 * recv re-ordering queues. Target will negotiate ADDBA with peer, 
 * and indicate via this event after succesfully completing the 
 * negotiation. This happens in two situations:
 *  1. Initial setup of aggregation
 *  2. Renegotiation of current recv window.
 * Window size for re-ordering is limited by target buffer
 * space, which is reflected in win_sz.
 * (Re)Start the periodic timer to deliver long standing frames,
 * in hold_q to OS.
 */
void
aggr_recv_addba_req_evt(void * cntxt, A_UINT8 tid, A_UINT16 seq_no, A_UINT8 win_sz);


/*
 * aggr_recv_delba_req_evt:
 * Target indicates deletion of a BA window for a tid via the
 * WMI_DELBA_EVENTID. Host would deliver all the frames in the 
 * hold_q, reset tid config and disable the periodic timer, if 
 * aggr is not enabled on any tid.
 */
void
aggr_recv_delba_req_evt(void * cntxt, A_UINT8 tid);



/*
 * aggr_process_recv_frm:
 * Called only for data frames. When aggr is ON for a tid, the buffer 
 * is always consumed, and osbuf would be NULL. For a non-aggr case,
 * osbuf is not modified.
 * AMSDU frames are consumed and are later freed. They are sliced and 
 * diced to individual frames and dispatched to stack.
 * After consuming a osbuf(when aggr is ON), a previously registered
 * callback may be called to deliver frames in order.
 */
void
aggr_process_recv_frm(void *cntxt, A_UINT8 tid, A_UINT16 seq_no, A_BOOL is_amsdu, void **osbuf);


/*
 * aggr_module_destroy:
 * Frees up all the queues and frames in them. Releases the cntxt to OS.
 */
void
aggr_module_destroy(void *cntxt);

/*
 * Dumps the aggregation stats 
 */
void
aggr_dump_stats(void *cntxt, PACKET_LOG **log_buf);

/* 
 * aggr_reset_state -- Called when it is deemed necessary to clear the aggregate
 *  hold Q state.  Examples include when a Connect event or disconnect event is 
 *  received. 
 */
void
aggr_reset_state(void *cntxt);


#ifdef __cplusplus
}
#endif

#endif /*__AGGR_RECV_API_H__ */
