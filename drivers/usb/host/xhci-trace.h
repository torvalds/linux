/*
 * xHCI host controller driver
 *
 * Copyright (C) 2013 Xenia Ragiadakou
 *
 * Author: Xenia Ragiadakou
 * Email : burzalodowa@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xhci-hcd

/*
 * The TRACE_SYSTEM_VAR defaults to TRACE_SYSTEM, but must be a
 * legitimate C variable. It is not exported to user space.
 */
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR xhci_hcd

#if !defined(__XHCI_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __XHCI_TRACE_H

#include <linux/tracepoint.h>
#include "xhci.h"

#define XHCI_MSG_MAX	500

DECLARE_EVENT_CLASS(xhci_log_msg,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf),
	TP_STRUCT__entry(__dynamic_array(char, msg, XHCI_MSG_MAX)),
	TP_fast_assign(
		vsnprintf(__get_str(msg), XHCI_MSG_MAX, vaf->fmt, *vaf->va);
	),
	TP_printk("%s", __get_str(msg))
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_address,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_context_change,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_quirks,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_reset_ep,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_cancel_urb,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_init,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DEFINE_EVENT(xhci_log_msg, xhci_dbg_ring_expansion,
	TP_PROTO(struct va_format *vaf),
	TP_ARGS(vaf)
);

DECLARE_EVENT_CLASS(xhci_log_ctx,
	TP_PROTO(struct xhci_hcd *xhci, struct xhci_container_ctx *ctx,
		 unsigned int ep_num),
	TP_ARGS(xhci, ctx, ep_num),
	TP_STRUCT__entry(
		__field(int, ctx_64)
		__field(unsigned, ctx_type)
		__field(dma_addr_t, ctx_dma)
		__field(u8 *, ctx_va)
		__field(unsigned, ctx_ep_num)
		__field(int, slot_id)
		__dynamic_array(u32, ctx_data,
			((HCC_64BYTE_CONTEXT(xhci->hcc_params) + 1) * 8) *
			((ctx->type == XHCI_CTX_TYPE_INPUT) + ep_num + 1))
	),
	TP_fast_assign(
		struct usb_device *udev;

		udev = to_usb_device(xhci_to_hcd(xhci)->self.controller);
		__entry->ctx_64 = HCC_64BYTE_CONTEXT(xhci->hcc_params);
		__entry->ctx_type = ctx->type;
		__entry->ctx_dma = ctx->dma;
		__entry->ctx_va = ctx->bytes;
		__entry->slot_id = udev->slot_id;
		__entry->ctx_ep_num = ep_num;
		memcpy(__get_dynamic_array(ctx_data), ctx->bytes,
			((HCC_64BYTE_CONTEXT(xhci->hcc_params) + 1) * 32) *
			((ctx->type == XHCI_CTX_TYPE_INPUT) + ep_num + 1));
	),
	TP_printk("ctx_64=%d, ctx_type=%u, ctx_dma=@%llx, ctx_va=@%p",
			__entry->ctx_64, __entry->ctx_type,
			(unsigned long long) __entry->ctx_dma, __entry->ctx_va
	)
);

DEFINE_EVENT(xhci_log_ctx, xhci_address_ctx,
	TP_PROTO(struct xhci_hcd *xhci, struct xhci_container_ctx *ctx,
		 unsigned int ep_num),
	TP_ARGS(xhci, ctx, ep_num)
);

DECLARE_EVENT_CLASS(xhci_log_trb,
	TP_PROTO(struct xhci_ring *ring, struct xhci_generic_trb *trb),
	TP_ARGS(ring, trb),
	TP_STRUCT__entry(
		__field(u32, type)
		__field(u32, field0)
		__field(u32, field1)
		__field(u32, field2)
		__field(u32, field3)
	),
	TP_fast_assign(
		__entry->type = ring->type;
		__entry->field0 = le32_to_cpu(trb->field[0]);
		__entry->field1 = le32_to_cpu(trb->field[1]);
		__entry->field2 = le32_to_cpu(trb->field[2]);
		__entry->field3 = le32_to_cpu(trb->field[3]);
	),
	TP_printk("%s: %s", xhci_ring_type_string(__entry->type),
			xhci_decode_trb(__entry->field0, __entry->field1,
					__entry->field2, __entry->field3)
	)
);

DEFINE_EVENT(xhci_log_trb, xhci_handle_event,
	TP_PROTO(struct xhci_ring *ring, struct xhci_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(xhci_log_trb, xhci_handle_command,
	TP_PROTO(struct xhci_ring *ring, struct xhci_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(xhci_log_trb, xhci_handle_transfer,
	TP_PROTO(struct xhci_ring *ring, struct xhci_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DEFINE_EVENT(xhci_log_trb, xhci_queue_trb,
	TP_PROTO(struct xhci_ring *ring, struct xhci_generic_trb *trb),
	TP_ARGS(ring, trb)
);

DECLARE_EVENT_CLASS(xhci_log_virt_dev,
	TP_PROTO(struct xhci_virt_device *vdev),
	TP_ARGS(vdev),
	TP_STRUCT__entry(
		__field(void *, vdev)
		__field(unsigned long long, out_ctx)
		__field(unsigned long long, in_ctx)
		__field(int, devnum)
		__field(int, state)
		__field(int, speed)
		__field(u8, portnum)
		__field(u8, level)
		__field(int, slot_id)
	),
	TP_fast_assign(
		__entry->vdev = vdev;
		__entry->in_ctx = (unsigned long long) vdev->in_ctx->dma;
		__entry->out_ctx = (unsigned long long) vdev->out_ctx->dma;
		__entry->devnum = vdev->udev->devnum;
		__entry->state = vdev->udev->state;
		__entry->speed = vdev->udev->speed;
		__entry->portnum = vdev->udev->portnum;
		__entry->level = vdev->udev->level;
		__entry->slot_id = vdev->udev->slot_id;
	),
	TP_printk("vdev %p ctx %llx | %llx num %d state %d speed %d port %d level %d slot %d",
		__entry->vdev, __entry->in_ctx, __entry->out_ctx,
		__entry->devnum, __entry->state, __entry->speed,
		__entry->portnum, __entry->level, __entry->slot_id
	)
);

DEFINE_EVENT(xhci_log_virt_dev, xhci_alloc_virt_device,
	TP_PROTO(struct xhci_virt_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(xhci_log_virt_dev, xhci_free_virt_device,
	TP_PROTO(struct xhci_virt_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(xhci_log_virt_dev, xhci_setup_device,
	TP_PROTO(struct xhci_virt_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(xhci_log_virt_dev, xhci_setup_addressable_virt_device,
	TP_PROTO(struct xhci_virt_device *vdev),
	TP_ARGS(vdev)
);

DEFINE_EVENT(xhci_log_virt_dev, xhci_stop_device,
	TP_PROTO(struct xhci_virt_device *vdev),
	TP_ARGS(vdev)
);

DECLARE_EVENT_CLASS(xhci_log_urb,
	TP_PROTO(struct urb *urb),
	TP_ARGS(urb),
	TP_STRUCT__entry(
		__field(void *, urb)
		__field(unsigned int, pipe)
		__field(unsigned int, stream)
		__field(int, status)
		__field(unsigned int, flags)
		__field(int, num_mapped_sgs)
		__field(int, num_sgs)
		__field(int, length)
		__field(int, actual)
		__field(int, epnum)
		__field(int, dir_in)
		__field(int, type)
	),
	TP_fast_assign(
		__entry->urb = urb;
		__entry->pipe = urb->pipe;
		__entry->stream = urb->stream_id;
		__entry->status = urb->status;
		__entry->flags = urb->transfer_flags;
		__entry->num_mapped_sgs = urb->num_mapped_sgs;
		__entry->num_sgs = urb->num_sgs;
		__entry->length = urb->transfer_buffer_length;
		__entry->actual = urb->actual_length;
		__entry->epnum = usb_endpoint_num(&urb->ep->desc);
		__entry->dir_in = usb_endpoint_dir_in(&urb->ep->desc);
		__entry->type = usb_endpoint_type(&urb->ep->desc);
	),
	TP_printk("ep%d%s-%s: urb %p pipe %u length %d/%d sgs %d/%d stream %d flags %08x",
			__entry->epnum, __entry->dir_in ? "in" : "out",
			({ char *s;
			switch (__entry->type) {
			case USB_ENDPOINT_XFER_INT:
				s = "intr";
				break;
			case USB_ENDPOINT_XFER_CONTROL:
				s = "control";
				break;
			case USB_ENDPOINT_XFER_BULK:
				s = "bulk";
				break;
			case USB_ENDPOINT_XFER_ISOC:
				s = "isoc";
				break;
			default:
				s = "UNKNOWN";
			} s; }), __entry->urb, __entry->pipe, __entry->actual,
			__entry->length, __entry->num_mapped_sgs,
			__entry->num_sgs, __entry->stream, __entry->flags
		)
);

DEFINE_EVENT(xhci_log_urb, xhci_urb_enqueue,
	TP_PROTO(struct urb *urb),
	TP_ARGS(urb)
);

DEFINE_EVENT(xhci_log_urb, xhci_urb_giveback,
	TP_PROTO(struct urb *urb),
	TP_ARGS(urb)
);

DEFINE_EVENT(xhci_log_urb, xhci_urb_dequeue,
	TP_PROTO(struct urb *urb),
	TP_ARGS(urb)
);

#endif /* __XHCI_TRACE_H */

/* this part must be outside header guard */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE xhci-trace

#include <trace/define_trace.h>
