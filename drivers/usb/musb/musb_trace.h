// SPDX-License-Identifier: GPL-2.0
/*
 * musb_trace.h - MUSB Controller Trace Support
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com
 *
 * Author: Bin Liu <b-liu@ti.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM musb

#if !defined(__MUSB_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MUSB_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/usb.h>
#include "musb_core.h"
#ifdef CONFIG_USB_TI_CPPI41_DMA
#include "cppi_dma.h"
#endif

#define MUSB_MSG_MAX   500

TRACE_EVENT(musb_log,
	TP_PROTO(struct musb *musb, struct va_format *vaf),
	TP_ARGS(musb, vaf),
	TP_STRUCT__entry(
		__string(name, dev_name(musb->controller))
		__dynamic_array(char, msg, MUSB_MSG_MAX)
	),
	TP_fast_assign(
		__assign_str(name, dev_name(musb->controller));
		vsnprintf(__get_str(msg), MUSB_MSG_MAX, vaf->fmt, *vaf->va);
	),
	TP_printk("%s: %s", __get_str(name), __get_str(msg))
);

DECLARE_EVENT_CLASS(musb_regb,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u8 data),
	TP_ARGS(caller, addr, offset, data),
	TP_STRUCT__entry(
		__field(void *, caller)
		__field(const void *, addr)
		__field(unsigned int, offset)
		__field(u8, data)
	),
	TP_fast_assign(
		__entry->caller = caller;
		__entry->addr = addr;
		__entry->offset = offset;
		__entry->data = data;
	),
	TP_printk("%pS: %p + %04x: %02x",
		__entry->caller, __entry->addr, __entry->offset, __entry->data)
);

DEFINE_EVENT(musb_regb, musb_readb,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u8 data),
	TP_ARGS(caller, addr, offset, data)
);

DEFINE_EVENT(musb_regb, musb_writeb,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u8 data),
	TP_ARGS(caller, addr, offset, data)
);

DECLARE_EVENT_CLASS(musb_regw,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u16 data),
	TP_ARGS(caller, addr, offset, data),
	TP_STRUCT__entry(
		__field(void *, caller)
		__field(const void *, addr)
		__field(unsigned int, offset)
		__field(u16, data)
	),
	TP_fast_assign(
		__entry->caller = caller;
		__entry->addr = addr;
		__entry->offset = offset;
		__entry->data = data;
	),
	TP_printk("%pS: %p + %04x: %04x",
		__entry->caller, __entry->addr, __entry->offset, __entry->data)
);

DEFINE_EVENT(musb_regw, musb_readw,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u16 data),
	TP_ARGS(caller, addr, offset, data)
);

DEFINE_EVENT(musb_regw, musb_writew,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u16 data),
	TP_ARGS(caller, addr, offset, data)
);

DECLARE_EVENT_CLASS(musb_regl,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u32 data),
	TP_ARGS(caller, addr, offset, data),
	TP_STRUCT__entry(
		__field(void *, caller)
		__field(const void *, addr)
		__field(unsigned int, offset)
		__field(u32, data)
	),
	TP_fast_assign(
		__entry->caller = caller;
		__entry->addr = addr;
		__entry->offset = offset;
		__entry->data = data;
	),
	TP_printk("%pS: %p + %04x: %08x",
		__entry->caller, __entry->addr, __entry->offset, __entry->data)
);

DEFINE_EVENT(musb_regl, musb_readl,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u32 data),
	TP_ARGS(caller, addr, offset, data)
);

DEFINE_EVENT(musb_regl, musb_writel,
	TP_PROTO(void *caller, const void *addr, unsigned int offset, u32 data),
	TP_ARGS(caller, addr, offset, data)
);

TRACE_EVENT(musb_isr,
	TP_PROTO(struct musb *musb),
	TP_ARGS(musb),
	TP_STRUCT__entry(
		__string(name, dev_name(musb->controller))
		__field(u8, int_usb)
		__field(u16, int_tx)
		__field(u16, int_rx)
	),
	TP_fast_assign(
		__assign_str(name, dev_name(musb->controller));
		__entry->int_usb = musb->int_usb;
		__entry->int_tx = musb->int_tx;
		__entry->int_rx = musb->int_rx;
	),
	TP_printk("%s: usb %02x, tx %04x, rx %04x",
		__get_str(name), __entry->int_usb,
		__entry->int_tx, __entry->int_rx
	)
);

DECLARE_EVENT_CLASS(musb_urb,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb),
	TP_STRUCT__entry(
		__string(name, dev_name(musb->controller))
		__field(struct urb *, urb)
		__field(unsigned int, pipe)
		__field(int, status)
		__field(unsigned int, flag)
		__field(u32, buf_len)
		__field(u32, actual_len)
	),
	TP_fast_assign(
		__assign_str(name, dev_name(musb->controller));
		__entry->urb = urb;
		__entry->pipe = urb->pipe;
		__entry->status = urb->status;
		__entry->flag = urb->transfer_flags;
		__entry->buf_len = urb->transfer_buffer_length;
		__entry->actual_len = urb->actual_length;
	),
	TP_printk("%s: %p, dev%d ep%d%s, flag 0x%x, len %d/%d, status %d",
			__get_str(name), __entry->urb,
			usb_pipedevice(__entry->pipe),
			usb_pipeendpoint(__entry->pipe),
			usb_pipein(__entry->pipe) ? "in" : "out",
			__entry->flag,
			__entry->actual_len, __entry->buf_len,
			__entry->status
	)
);

DEFINE_EVENT(musb_urb, musb_urb_start,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb)
);

DEFINE_EVENT(musb_urb, musb_urb_gb,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb)
);

DEFINE_EVENT(musb_urb, musb_urb_rx,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb)
);

DEFINE_EVENT(musb_urb, musb_urb_tx,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb)
);

DEFINE_EVENT(musb_urb, musb_urb_enq,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb)
);

DEFINE_EVENT(musb_urb, musb_urb_deq,
	TP_PROTO(struct musb *musb, struct urb *urb),
	TP_ARGS(musb, urb)
);

DECLARE_EVENT_CLASS(musb_req,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__field(struct usb_request *, req)
		__field(u8, is_tx)
		__field(u8, epnum)
		__field(int, status)
		__field(unsigned int, buf_len)
		__field(unsigned int, actual_len)
		__field(unsigned int, zero)
		__field(unsigned int, short_not_ok)
		__field(unsigned int, no_interrupt)
	),
	TP_fast_assign(
		__entry->req = &req->request;
		__entry->is_tx = req->tx;
		__entry->epnum = req->epnum;
		__entry->status = req->request.status;
		__entry->buf_len = req->request.length;
		__entry->actual_len = req->request.actual;
		__entry->zero = req->request.zero;
		__entry->short_not_ok = req->request.short_not_ok;
		__entry->no_interrupt = req->request.no_interrupt;
	),
	TP_printk("%p, ep%d %s, %s%s%s, len %d/%d, status %d",
			__entry->req, __entry->epnum,
			__entry->is_tx ? "tx/IN" : "rx/OUT",
			__entry->zero ? "Z" : "z",
			__entry->short_not_ok ? "S" : "s",
			__entry->no_interrupt ? "I" : "i",
			__entry->actual_len, __entry->buf_len,
			__entry->status
	)
);

DEFINE_EVENT(musb_req, musb_req_gb,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_tx,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_rx,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_alloc,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_free,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_start,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_enq,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

DEFINE_EVENT(musb_req, musb_req_deq,
	TP_PROTO(struct musb_request *req),
	TP_ARGS(req)
);

#ifdef CONFIG_USB_TI_CPPI41_DMA
DECLARE_EVENT_CLASS(musb_cppi41,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch),
	TP_STRUCT__entry(
		__field(struct cppi41_dma_channel *, ch)
		__string(name, dev_name(ch->hw_ep->musb->controller))
		__field(u8, hwep)
		__field(u8, port)
		__field(u8, is_tx)
		__field(u32, len)
		__field(u32, prog_len)
		__field(u32, xferred)
	),
	TP_fast_assign(
		__entry->ch = ch;
		__assign_str(name, dev_name(ch->hw_ep->musb->controller));
		__entry->hwep = ch->hw_ep->epnum;
		__entry->port = ch->port_num;
		__entry->is_tx = ch->is_tx;
		__entry->len = ch->total_len;
		__entry->prog_len = ch->prog_len;
		__entry->xferred = ch->transferred;
	),
	TP_printk("%s: %p, hwep%d ch%d%s, prog_len %d, len %d/%d",
			__get_str(name), __entry->ch, __entry->hwep,
			__entry->port, __entry->is_tx ? "tx" : "rx",
			__entry->prog_len, __entry->xferred, __entry->len
	)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_done,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_gb,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_config,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_cont,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_alloc,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_abort,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);

DEFINE_EVENT(musb_cppi41, musb_cppi41_free,
	TP_PROTO(struct cppi41_dma_channel *ch),
	TP_ARGS(ch)
);
#endif /* CONFIG_USB_TI_CPPI41_DMA */

#endif /* __MUSB_TRACE_H */

/* this part has to be here */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE musb_trace

#include <trace/define_trace.h>
