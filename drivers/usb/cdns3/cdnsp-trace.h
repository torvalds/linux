/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence CDNSP DRD Driver.
 * Trace support header file
 *
 * Copyright (C) 2020 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cdnsp-dev

/*
 * The TRACE_SYSTEM_VAR defaults to TRACE_SYSTEM, but must be a
 * legitimate C variable. It is not exported to user space.
 */
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR cdnsp_dev

#if !defined(__CDNSP_DEV_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __CDNSP_DEV_TRACE_H

#include <linux/tracepoint.h>
#include "cdnsp-gadget.h"
#include "cdnsp-debug.h"

/*
 * There is limitation for single buffer size in TRACEPOINT subsystem.
 * By default TRACE_BUF_SIZE is 1024, so no all data will be logged.
 * To show more data this must be increased. In most cases the default
 * value is sufficient.
 */
#define CDNSP_MSG_MAX 500

DECLARE_EVENT_CLASS(cdnsp_log_ep,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id),
	TP_STRUCT__entry(
		__string(name, pep->name)
		__field(unsigned int, state)
		__field(u32, stream_id)
		__field(u8, enabled)
		__field(unsigned int, num_streams)
		__field(int, td_count)
		__field(u8, first_prime_det)
		__field(u8, drbls_count)
	),
	TP_fast_assign(
		__assign_str(name, pep->name);
		__entry->state = pep->ep_state;
		__entry->stream_id = stream_id;
		__entry->enabled = pep->ep_state & EP_HAS_STREAMS;
		__entry->num_streams = pep->stream_info.num_streams;
		__entry->td_count = pep->stream_info.td_count;
		__entry->first_prime_det = pep->stream_info.first_prime_det;
		__entry->drbls_count = pep->stream_info.drbls_count;
	),
	TP_printk("%s: SID: %08x ep state: %x stream: enabled: %d num  %d "
		  "tds %d, first prime: %d drbls %d",
		  __get_str(name), __entry->state, __entry->stream_id,
		  __entry->enabled, __entry->num_streams, __entry->td_count,
		  __entry->first_prime_det, __entry->drbls_count)
);

DEFINE_EVENT(cdnsp_log_ep, cdnsp_tr_drbl,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id)
);

DEFINE_EVENT(cdnsp_log_ep, cdnsp_wait_for_prime,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id)
);

DEFINE_EVENT(cdnsp_log_ep, cdnsp_ep_list_empty_with_skip,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id)
);

DEFINE_EVENT(cdnsp_log_ep, cdnsp_ep_enable_end,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id)
);

DEFINE_EVENT(cdnsp_log_ep, cdnsp_ep_disable_end,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id)
);

DEFINE_EVENT(cdnsp_log_ep, cdnsp_ep_busy_try_halt_again,
	TP_PROTO(struct cdnsp_ep *pep, u32 stream_id),
	TP_ARGS(pep, stream_id)
);

DECLARE_EVENT_CLASS(cdnsp_log_enable_disable,
	TP_PROTO(int set),
	TP_ARGS(set),
	TP_STRUCT__entry(
		__field(int, set)
	),
	TP_fast_assign(
		__entry->set = set;
	),
	TP_printk("%s", __entry->set ? "enabled" : "disabled")
);

DEFINE_EVENT(cdnsp_log_enable_disable, cdnsp_pullup,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DEFINE_EVENT(cdnsp_log_enable_disable, cdnsp_u1,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DEFINE_EVENT(cdnsp_log_enable_disable, cdnsp_u2,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DEFINE_EVENT(cdnsp_log_enable_disable, cdnsp_lpm,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DEFINE_EVENT(cdnsp_log_enable_disable, cdnsp_may_wakeup,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DECLARE_EVENT_CLASS(cdnsp_log_simple,
	TP_PROTO(char *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
		__string(text, msg)
	),
	TP_fast_assign(
		__assign_str(text, msg);
	),
	TP_printk("%s", __get_str(text))
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_exit,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_init,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_slot_id,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_no_room_on_ring,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_ep0_status_stage,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_ep0_request,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_ep0_set_config,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_ep0_halted,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdnsp_log_simple, cdnsp_ep_halt,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

TRACE_EVENT(cdnsp_looking_trb_in_td,
	TP_PROTO(dma_addr_t suspect, dma_addr_t trb_start, dma_addr_t trb_end,
		 dma_addr_t curr_seg, dma_addr_t end_seg),
	TP_ARGS(suspect, trb_start, trb_end, curr_seg, end_seg),
	TP_STRUCT__entry(
		__field(dma_addr_t, suspect)
		__field(dma_addr_t, trb_start)
		__field(dma_addr_t, trb_end)
		__field(dma_addr_t, curr_seg)
		__field(dma_addr_t, end_seg)
	),
	TP_fast_assign(
		__entry->suspect = suspect;
		__entry->trb_start = trb_start;
		__entry->trb_end = trb_end;
		__entry->curr_seg = curr_seg;
		__entry->end_seg = end_seg;
	),
	TP_printk("DMA: suspect event: %pad, trb-start: %pad, trb-end %pad, "
		  "seg-start %pad, seg-end %pad",
		  &__entry->suspect, &__entry->trb_start, &__entry->trb_end,
		  &__entry->curr_seg, &__entry->end_seg)
);

TRACE_EVENT(cdnsp_port_info,
	TP_PROTO(__le32 __iomem *addr, u32 offset, u32 count, u32 rev),
	TP_ARGS(addr, offset, count, rev),
	TP_STRUCT__entry(
		__field(__le32 __iomem *, addr)
		__field(u32, offset)
		__field(u32, count)
		__field(u32, rev)
	),
	TP_fast_assign(
		__entry->addr = addr;
		__entry->offset = offset;
		__entry->count = count;
		__entry->rev = rev;
	),
	TP_printk("Ext Cap %p, port offset = %u, count = %u, rev = 0x%x",
		  __entry->addr, __entry->offset, __entry->count, __entry->rev)
);

DECLARE_EVENT_CLASS(cdnsp_log_deq_state,
	TP_PROTO(struct cdnsp_dequeue_state *state),
	TP_ARGS(state),
	TP_STRUCT__entry(
		__field(int, new_cycle_state)
		__field(struct cdnsp_segment *, new_deq_seg)
		__field(dma_addr_t, deq_seg_dma)
		__field(union cdnsp_trb	*, new_deq_ptr)
		__field(dma_addr_t, deq_ptr_dma)
	),
	TP_fast_assign(
		__entry->new_cycle_state = state->new_cycle_state;
		__entry->new_deq_seg = state->new_deq_seg;
		__entry->deq_seg_dma = state->new_deq_seg->dma;
		__entry->new_deq_ptr = state->new_deq_ptr,
		__entry->deq_ptr_dma = cdnsp_trb_virt_to_dma(state->new_deq_seg,
							     state->new_deq_ptr);
	),
	TP_printk("New cycle state = 0x%x, New dequeue segment = %p (0x%pad dma), "
		  "New dequeue pointer = %p (0x%pad dma)",
		  __entry->new_cycle_state, __entry->new_deq_seg,
		  &__entry->deq_seg_dma, __entry->new_deq_ptr,
		  &__entry->deq_ptr_dma
	)
);

DEFINE_EVENT(cdnsp_log_deq_state, cdnsp_new_deq_state,
	TP_PROTO(struct cdnsp_dequeue_state *state),
	TP_ARGS(state)
);

DECLARE_EVENT_CLASS(cdnsp_log_ctrl,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl),
	TP_STRUCT__entry(
		__field(u8, bRequestType)
		__field(u8, bRequest)
		__field(u16, wValue)
		__field(u16, wIndex)
		__field(u16, wLength)
		__dynamic_array(char, str, CDNSP_MSG_MAX)
	),
	TP_fast_assign(
		__entry->bRequestType = ctrl->bRequestType;
		__entry->bRequest = ctrl->bRequest;
		__entry->wValue = le16_to_cpu(ctrl->wValue);
		__entry->wIndex = le16_to_cpu(ctrl->wIndex);
		__entry->wLength = le16_to_cpu(ctrl->wLength);
	),
	TP_printk("%s", usb_decode_ctrl(__get_str(str), CDNSP_MSG_MAX,
					__entry->bRequestType,
					__entry->bRequest, __entry->wValue,
					__entry->wIndex, __entry->wLength)
	)
);

DEFINE_EVENT(cdnsp_log_ctrl, cdnsp_ctrl_req,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl)
);

DECLARE_EVENT_CLASS(cdnsp_log_bounce,
	TP_PROTO(struct cdnsp_request *preq, u32 new_buf_len, u32 offset,
		 dma_addr_t dma, unsigned int unalign),
	TP_ARGS(preq, new_buf_len, offset, dma, unalign),
	TP_STRUCT__entry(
		__string(name, preq->pep->name)
		__field(u32, new_buf_len)
		__field(u32, offset)
		__field(dma_addr_t, dma)
		__field(unsigned int, unalign)
	),
	TP_fast_assign(
		__assign_str(name, preq->pep->name);
		__entry->new_buf_len = new_buf_len;
		__entry->offset = offset;
		__entry->dma = dma;
		__entry->unalign = unalign;
	),
	TP_printk("%s buf len %d, offset %d, dma %pad, unalign %d",
		  __get_str(name), __entry->new_buf_len,
		  __entry->offset, &__entry->dma, __entry->unalign
	)
);

DEFINE_EVENT(cdnsp_log_bounce, cdnsp_bounce_align_td_split,
	TP_PROTO(struct cdnsp_request *preq, u32 new_buf_len, u32 offset,
		 dma_addr_t dma, unsigned int unalign),
	TP_ARGS(preq, new_buf_len, offset, dma, unalign)
);

DEFINE_EVENT(cdnsp_log_bounce, cdnsp_bounce_map,
	TP_PROTO(struct cdnsp_request *preq, u32 new_buf_len, u32 offset,
		 dma_addr_t dma, unsigned int unalign),
	TP_ARGS(preq, new_buf_len, offset, dma, unalign)
);

DEFINE_EVENT(cdnsp_log_bounce, cdnsp_bounce_unmap,
	TP_PROTO(struct cdnsp_request *preq, u32 new_buf_len, u32 offset,
		 dma_addr_t dma, unsigned int unalign),
	TP_ARGS(preq, new_buf_len, offset, dma, unalign)
);

DECLARE_EVENT_CLASS(cdnsp_log_trb,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb),
	TP_STRUCT__entry(
		__field(u32, type)
		__field(u32, field0)
		__field(u32, field1)
		__field(u32, field2)
		__field(u32, field3)
		__field(union cdnsp_trb *, trb)
		__field(dma_addr_t, trb_dma)
		__dynamic_array(char, str, CDNSP_MSG_MAX)
	),
	TP_fast_assign(
		__entry->type = ring->type;
		__entry->field0 = le32_to_cpu(trb->field[0]);
		__entry->field1 = le32_to_cpu(trb->field[1]);
		__entry->field2 = le32_to_cpu(trb->field[2]);
		__entry->field3 = le32_to_cpu(trb->field[3]);
		__entry->trb = (union cdnsp_trb *)trb;
		__entry->trb_dma = cdnsp_trb_virt_to_dma(ring->deq_seg,
							 (union cdnsp_trb *)trb);

	),
	TP_printk("%s: %s trb: %p(%pad)", cdnsp_ring_type_string(__entry->type),
		  cdnsp_decode_trb(__get_str(str), CDNSP_MSG_MAX,
				   __entry->field0, __entry->field1,
				   __entry->field2, __entry->field3),
				   __entry->trb, &__entry->trb_dma
	)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_handle_event,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_trb_without_td,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_handle_command,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_handle_transfer,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_queue_trb,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_cmd_wait_for_compl,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_cmd_timeout,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(cdnsp_log_trb, cdnsp_defered_event,
	TP_PROTO(struct cdnsp_ring *ring, struct cdnsp_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DECLARE_EVENT_CLASS(cdnsp_log_pdev,
	TP_PROTO(struct cdnsp_device *pdev),
	TP_ARGS(pdev),
	TP_STRUCT__entry(
		__field(struct cdnsp_device *, pdev)
		__field(struct usb_gadget *, gadget)
		__field(dma_addr_t, out_ctx)
		__field(dma_addr_t, in_ctx)
		__field(u8, port_num)
	),
	TP_fast_assign(
		__entry->pdev = pdev;
		__entry->gadget = &pdev->gadget;
		__entry->in_ctx = pdev->in_ctx.dma;
		__entry->out_ctx = pdev->out_ctx.dma;
		__entry->port_num = pdev->active_port ?
				pdev->active_port->port_num : 0xFF;
	),
	TP_printk("pdev %p gadget %p ctx %pad | %pad, port %d ",
		  __entry->pdev, __entry->gadget, &__entry->in_ctx,
		  &__entry->out_ctx, __entry->port_num
	)
);

DEFINE_EVENT(cdnsp_log_pdev, cdnsp_alloc_priv_device,
	TP_PROTO(struct cdnsp_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(cdnsp_log_pdev, cdnsp_free_priv_device,
	TP_PROTO(struct cdnsp_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(cdnsp_log_pdev, cdnsp_setup_device,
	TP_PROTO(struct cdnsp_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(cdnsp_log_pdev, cdnsp_setup_addressable_priv_device,
	TP_PROTO(struct cdnsp_device *vdev),
	TP_ARGS(vdev)
);

DECLARE_EVENT_CLASS(cdnsp_log_request,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__string(name, req->pep->name)
		__field(struct usb_request *, request)
		__field(struct cdnsp_request *, preq)
		__field(void *, buf)
		__field(unsigned int, actual)
		__field(unsigned int, length)
		__field(int, status)
		__field(dma_addr_t, dma)
		__field(unsigned int, stream_id)
		__field(unsigned int, zero)
		__field(unsigned int, short_not_ok)
		__field(unsigned int, no_interrupt)
		__field(struct scatterlist*, sg)
		__field(unsigned int, num_sgs)
		__field(unsigned int, num_mapped_sgs)

	),
	TP_fast_assign(
		__assign_str(name, req->pep->name);
		__entry->request = &req->request;
		__entry->preq = req;
		__entry->buf = req->request.buf;
		__entry->actual = req->request.actual;
		__entry->length = req->request.length;
		__entry->status = req->request.status;
		__entry->dma = req->request.dma;
		__entry->stream_id = req->request.stream_id;
		__entry->zero = req->request.zero;
		__entry->short_not_ok = req->request.short_not_ok;
		__entry->no_interrupt = req->request.no_interrupt;
		__entry->sg = req->request.sg;
		__entry->num_sgs = req->request.num_sgs;
		__entry->num_mapped_sgs = req->request.num_mapped_sgs;
	),
	TP_printk("%s; req U:%p/P:%p, req buf %p, length %u/%u, status %d, "
		  "buf dma (%pad), SID %u, %s%s%s, sg %p, num_sg %d,"
		  " num_m_sg %d",
		  __get_str(name), __entry->request, __entry->preq,
		  __entry->buf, __entry->actual, __entry->length,
		  __entry->status, &__entry->dma,
		  __entry->stream_id, __entry->zero ? "Z" : "z",
		  __entry->short_not_ok ? "S" : "s",
		  __entry->no_interrupt ? "I" : "i",
		  __entry->sg, __entry->num_sgs, __entry->num_mapped_sgs
		)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_request_enqueue,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_request_enqueue_busy,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_request_enqueue_error,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_request_dequeue,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_request_giveback,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_alloc_request,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(cdnsp_log_request, cdnsp_free_request,
	TP_PROTO(struct cdnsp_request *req),
	TP_ARGS(req)
);

DECLARE_EVENT_CLASS(cdnsp_log_ep_ctx,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(u32, info)
		__field(u32, info2)
		__field(u64, deq)
		__field(u32, tx_info)
		__dynamic_array(char, str, CDNSP_MSG_MAX)
	),
	TP_fast_assign(
		__entry->info = le32_to_cpu(ctx->ep_info);
		__entry->info2 = le32_to_cpu(ctx->ep_info2);
		__entry->deq = le64_to_cpu(ctx->deq);
		__entry->tx_info = le32_to_cpu(ctx->tx_info);
	),
	TP_printk("%s", cdnsp_decode_ep_context(__get_str(str), CDNSP_MSG_MAX,
						__entry->info, __entry->info2,
						__entry->deq, __entry->tx_info)
	)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_ep_disabled,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_ep_stopped_or_disabled,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_remove_request,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_handle_cmd_stop_ep,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_handle_cmd_flush_ep,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_handle_cmd_set_deq_ep,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_handle_cmd_reset_ep,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_ep_ctx, cdnsp_handle_cmd_config_ep,
	TP_PROTO(struct cdnsp_ep_ctx *ctx),
	TP_ARGS(ctx)
);

DECLARE_EVENT_CLASS(cdnsp_log_slot_ctx,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(u32, info)
		__field(u32, info2)
		__field(u32, int_target)
		__field(u32, state)
	),
	TP_fast_assign(
		__entry->info = le32_to_cpu(ctx->dev_info);
		__entry->info2 = le32_to_cpu(ctx->dev_port);
		__entry->int_target = le32_to_cpu(ctx->int_target);
		__entry->state = le32_to_cpu(ctx->dev_state);
	),
	TP_printk("%s", cdnsp_decode_slot_context(__entry->info,
						  __entry->info2,
						  __entry->int_target,
						  __entry->state)
	)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_slot_already_in_default,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_handle_cmd_enable_slot,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_handle_cmd_disable_slot,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_reset_device,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_setup_device_slot,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_handle_cmd_addr_dev,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_handle_cmd_reset_dev,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_handle_cmd_set_deq,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DEFINE_EVENT(cdnsp_log_slot_ctx, cdnsp_configure_endpoint,
	TP_PROTO(struct cdnsp_slot_ctx *ctx),
	TP_ARGS(ctx)
);

DECLARE_EVENT_CLASS(cdnsp_log_td_info,
	TP_PROTO(struct cdnsp_request *preq),
	TP_ARGS(preq),
	TP_STRUCT__entry(
		__string(name, preq->pep->name)
		__field(struct usb_request *, request)
		__field(struct cdnsp_request *, preq)
		__field(union cdnsp_trb *, first_trb)
		__field(union cdnsp_trb *, last_trb)
		__field(dma_addr_t, trb_dma)
	),
	TP_fast_assign(
		__assign_str(name, preq->pep->name);
		__entry->request = &preq->request;
		__entry->preq = preq;
		__entry->first_trb = preq->td.first_trb;
		__entry->last_trb = preq->td.last_trb;
		__entry->trb_dma = cdnsp_trb_virt_to_dma(preq->td.start_seg,
							 preq->td.first_trb)
	),
	TP_printk("%s req/preq:  %p/%p, first trb %p[vir]/%pad(dma), last trb %p",
		  __get_str(name), __entry->request, __entry->preq,
		  __entry->first_trb, &__entry->trb_dma,
		  __entry->last_trb
		)
);

DEFINE_EVENT(cdnsp_log_td_info, cdnsp_remove_request_td,
	TP_PROTO(struct cdnsp_request *preq),
	TP_ARGS(preq)
);

DECLARE_EVENT_CLASS(cdnsp_log_ring,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring),
	TP_STRUCT__entry(
		__field(u32, type)
		__field(void *, ring)
		__field(dma_addr_t, enq)
		__field(dma_addr_t, deq)
		__field(dma_addr_t, enq_seg)
		__field(dma_addr_t, deq_seg)
		__field(unsigned int, num_segs)
		__field(unsigned int, stream_id)
		__field(unsigned int, cycle_state)
		__field(unsigned int, num_trbs_free)
		__field(unsigned int, bounce_buf_len)
	),
	TP_fast_assign(
		__entry->ring = ring;
		__entry->type = ring->type;
		__entry->num_segs = ring->num_segs;
		__entry->stream_id = ring->stream_id;
		__entry->enq_seg = ring->enq_seg->dma;
		__entry->deq_seg = ring->deq_seg->dma;
		__entry->cycle_state = ring->cycle_state;
		__entry->num_trbs_free = ring->num_trbs_free;
		__entry->bounce_buf_len = ring->bounce_buf_len;
		__entry->enq = cdnsp_trb_virt_to_dma(ring->enq_seg,
						     ring->enqueue);
		__entry->deq = cdnsp_trb_virt_to_dma(ring->deq_seg,
						     ring->dequeue);
	),
	TP_printk("%s %p: enq %pad(%pad) deq %pad(%pad) segs %d stream %d"
		  " free_trbs %d bounce %d cycle %d",
		  cdnsp_ring_type_string(__entry->type), __entry->ring,
		  &__entry->enq, &__entry->enq_seg,
		  &__entry->deq, &__entry->deq_seg,
		  __entry->num_segs,
		  __entry->stream_id,
		  __entry->num_trbs_free,
		  __entry->bounce_buf_len,
		  __entry->cycle_state
		)
);

DEFINE_EVENT(cdnsp_log_ring, cdnsp_ring_alloc,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring)
);

DEFINE_EVENT(cdnsp_log_ring, cdnsp_ring_free,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring)
);

DEFINE_EVENT(cdnsp_log_ring, cdnsp_set_stream_ring,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring)
);

DEFINE_EVENT(cdnsp_log_ring, cdnsp_ring_expansion,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring)
);

DEFINE_EVENT(cdnsp_log_ring, cdnsp_inc_enq,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring)
);

DEFINE_EVENT(cdnsp_log_ring, cdnsp_inc_deq,
	TP_PROTO(struct cdnsp_ring *ring),
	TP_ARGS(ring)
);

DECLARE_EVENT_CLASS(cdnsp_log_portsc,
		TP_PROTO(u32 portnum, u32 portsc),
		TP_ARGS(portnum, portsc),
		TP_STRUCT__entry(
				__field(u32, portnum)
				__field(u32, portsc)
				__dynamic_array(char, str, CDNSP_MSG_MAX)
				),
		TP_fast_assign(
				__entry->portnum = portnum;
				__entry->portsc = portsc;
				),
		TP_printk("port-%d: %s",
			  __entry->portnum,
			  cdnsp_decode_portsc(__get_str(str), CDNSP_MSG_MAX,
					      __entry->portsc)
			)
);

DEFINE_EVENT(cdnsp_log_portsc, cdnsp_handle_port_status,
		TP_PROTO(u32 portnum, u32 portsc),
		TP_ARGS(portnum, portsc)
);

DEFINE_EVENT(cdnsp_log_portsc, cdnsp_link_state_changed,
		TP_PROTO(u32 portnum, u32 portsc),
		TP_ARGS(portnum, portsc)
);

TRACE_EVENT(cdnsp_stream_number,
	TP_PROTO(struct cdnsp_ep *pep, int num_stream_ctxs, int num_streams),
	TP_ARGS(pep, num_stream_ctxs, num_streams),
	TP_STRUCT__entry(
		__string(name, pep->name)
		__field(int, num_stream_ctxs)
		__field(int, num_streams)
	),
	TP_fast_assign(
		__entry->num_stream_ctxs = num_stream_ctxs;
		__entry->num_streams = num_streams;
	),
	TP_printk("%s Need %u stream ctx entries for %u stream IDs.",
		  __get_str(name), __entry->num_stream_ctxs,
		  __entry->num_streams)
);

#endif /* __CDNSP_TRACE_H */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cdnsp-trace

#include <trace/define_trace.h>
