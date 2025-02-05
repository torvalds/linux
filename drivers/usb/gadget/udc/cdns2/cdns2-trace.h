/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USBHS-DEV device controller driver.
 * Trace support header file.
 *
 * Copyright (C) 2023 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cdns2-dev

/*
 * The TRACE_SYSTEM_VAR defaults to TRACE_SYSTEM, but must be a
 * legitimate C variable. It is not exported to user space.
 */
#undef TRACE_SYSTEM_VAR
#define TRACE_SYSTEM_VAR cdns2_dev

#if !defined(__LINUX_CDNS2_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __LINUX_CDNS2_TRACE

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <asm/byteorder.h>
#include <linux/usb/ch9.h>
#include "cdns2-gadget.h"
#include "cdns2-debug.h"

#define CDNS2_MSG_MAX	500

DECLARE_EVENT_CLASS(cdns2_log_enable_disable,
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

DEFINE_EVENT(cdns2_log_enable_disable, cdns2_pullup,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DEFINE_EVENT(cdns2_log_enable_disable, cdns2_lpm,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DEFINE_EVENT(cdns2_log_enable_disable, cdns2_may_wakeup,
	TP_PROTO(int set),
	TP_ARGS(set)
);

DECLARE_EVENT_CLASS(cdns2_log_simple,
	TP_PROTO(char *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
		__string(text, msg)
	),
	TP_fast_assign(
		__assign_str(text);
	),
	TP_printk("%s", __get_str(text))
);

DEFINE_EVENT(cdns2_log_simple, cdns2_no_room_on_ring,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdns2_log_simple, cdns2_ep0_status_stage,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdns2_log_simple, cdns2_ep0_set_config,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdns2_log_simple, cdns2_ep0_setup,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

DEFINE_EVENT(cdns2_log_simple, cdns2_device_state,
	TP_PROTO(char *msg),
	TP_ARGS(msg)
);

TRACE_EVENT(cdns2_ep_halt,
	TP_PROTO(struct cdns2_endpoint *ep_priv, u8 halt, u8 flush),
	TP_ARGS(ep_priv, halt, flush),
	TP_STRUCT__entry(
		__string(name, ep_priv->name)
		__field(u8, halt)
		__field(u8, flush)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->halt = halt;
		__entry->flush = flush;
	),
	TP_printk("Halt %s for %s: %s", __entry->flush ? " and flush" : "",
		  __get_str(name), __entry->halt ? "set" : "cleared")
);

TRACE_EVENT(cdns2_wa1,
	TP_PROTO(struct cdns2_endpoint *ep_priv, char *msg),
	TP_ARGS(ep_priv, msg),
	TP_STRUCT__entry(
		__string(ep_name, ep_priv->name)
		__string(msg, msg)
	),
	TP_fast_assign(
		__assign_str(ep_name);
		__assign_str(msg);
	),
	TP_printk("WA1: %s %s", __get_str(ep_name), __get_str(msg))
);

DECLARE_EVENT_CLASS(cdns2_log_doorbell,
	TP_PROTO(struct cdns2_endpoint *pep, u32 ep_trbaddr),
	TP_ARGS(pep, ep_trbaddr),
	TP_STRUCT__entry(
		__string(name, pep->num ? pep->name :
				(pep->dir ? "ep0in" : "ep0out"))
		__field(u32, ep_trbaddr)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->ep_trbaddr = ep_trbaddr;
	),
	TP_printk("%s, ep_trbaddr %08x", __get_str(name),
		  __entry->ep_trbaddr)
);

DEFINE_EVENT(cdns2_log_doorbell, cdns2_doorbell_ep0,
	TP_PROTO(struct cdns2_endpoint *pep, u32 ep_trbaddr),
	TP_ARGS(pep, ep_trbaddr)
);

DEFINE_EVENT(cdns2_log_doorbell, cdns2_doorbell_epx,
	TP_PROTO(struct cdns2_endpoint *pep, u32 ep_trbaddr),
	TP_ARGS(pep, ep_trbaddr)
);

DECLARE_EVENT_CLASS(cdns2_log_usb_irq,
	TP_PROTO(u32 usb_irq, u32 ext_irq),
	TP_ARGS(usb_irq, ext_irq),
	TP_STRUCT__entry(
		__field(u32, usb_irq)
		__field(u32, ext_irq)
	),
	TP_fast_assign(
		__entry->usb_irq = usb_irq;
		__entry->ext_irq = ext_irq;
	),
	TP_printk("%s", cdns2_decode_usb_irq(__get_buf(CDNS2_MSG_MAX),
					     CDNS2_MSG_MAX,
					     __entry->usb_irq,
					     __entry->ext_irq))
);

DEFINE_EVENT(cdns2_log_usb_irq, cdns2_usb_irq,
	TP_PROTO(u32 usb_irq, u32 ext_irq),
	TP_ARGS(usb_irq, ext_irq)
);

TRACE_EVENT(cdns2_dma_ep_ists,
	TP_PROTO(u32 dma_ep_ists),
	TP_ARGS(dma_ep_ists),
	TP_STRUCT__entry(
		__field(u32, dma_ep_ists)
	),
	TP_fast_assign(
		__entry->dma_ep_ists = dma_ep_ists;
	),
	TP_printk("OUT: 0x%04x, IN: 0x%04x", (u16)__entry->dma_ep_ists,
		  __entry->dma_ep_ists >> 16)
);

DECLARE_EVENT_CLASS(cdns2_log_epx_irq,
	TP_PROTO(struct cdns2_device *pdev, struct cdns2_endpoint *pep),
	TP_ARGS(pdev, pep),
	TP_STRUCT__entry(
		__string(ep_name, pep->name)
		__field(u32, ep_sts)
		__field(u32, ep_ists)
		__field(u32, ep_traddr)
	),
	TP_fast_assign(
		__assign_str(ep_name);
		__entry->ep_sts = readl(&pdev->adma_regs->ep_sts);
		__entry->ep_ists = readl(&pdev->adma_regs->ep_ists);
		__entry->ep_traddr = readl(&pdev->adma_regs->ep_traddr);
	),
	TP_printk("%s, ep_traddr: %08x",
		  cdns2_decode_epx_irq(__get_buf(CDNS2_MSG_MAX), CDNS2_MSG_MAX,
				       __get_str(ep_name),
				       __entry->ep_ists, __entry->ep_sts),
		  __entry->ep_traddr)
);

DEFINE_EVENT(cdns2_log_epx_irq, cdns2_epx_irq,
	TP_PROTO(struct cdns2_device *pdev, struct cdns2_endpoint *pep),
	TP_ARGS(pdev, pep)
);

DECLARE_EVENT_CLASS(cdns2_log_ep0_irq,
	TP_PROTO(struct cdns2_device *pdev),
	TP_ARGS(pdev),
	TP_STRUCT__entry(
		__field(int, ep_dir)
		__field(u32, ep_ists)
		__field(u32, ep_sts)
	),
	TP_fast_assign(
		__entry->ep_dir = pdev->selected_ep;
		__entry->ep_ists = readl(&pdev->adma_regs->ep_ists);
		__entry->ep_sts = readl(&pdev->adma_regs->ep_sts);
	),
	TP_printk("%s", cdns2_decode_ep0_irq(__get_buf(CDNS2_MSG_MAX),
					     CDNS2_MSG_MAX,
					     __entry->ep_ists, __entry->ep_sts,
					     __entry->ep_dir))
);

DEFINE_EVENT(cdns2_log_ep0_irq, cdns2_ep0_irq,
	TP_PROTO(struct cdns2_device *pdev),
	TP_ARGS(pdev)
);

DECLARE_EVENT_CLASS(cdns2_log_ctrl,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl),
	TP_STRUCT__entry(
		__field(u8, bRequestType)
		__field(u8, bRequest)
		__field(u16, wValue)
		__field(u16, wIndex)
		__field(u16, wLength)
	),
	TP_fast_assign(
		__entry->bRequestType = ctrl->bRequestType;
		__entry->bRequest = ctrl->bRequest;
		__entry->wValue = le16_to_cpu(ctrl->wValue);
		__entry->wIndex = le16_to_cpu(ctrl->wIndex);
		__entry->wLength = le16_to_cpu(ctrl->wLength);
	),
	TP_printk("%s", usb_decode_ctrl(__get_buf(CDNS2_MSG_MAX), CDNS2_MSG_MAX,
					__entry->bRequestType,
					__entry->bRequest, __entry->wValue,
					__entry->wIndex, __entry->wLength)
	)
);

DEFINE_EVENT(cdns2_log_ctrl, cdns2_ctrl_req,
	TP_PROTO(struct usb_ctrlrequest *ctrl),
	TP_ARGS(ctrl)
);

DECLARE_EVENT_CLASS(cdns2_log_request,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq),
	TP_STRUCT__entry(
		__string(name, preq->pep->name)
		__field(struct usb_request *, request)
		__field(struct cdns2_request *, preq)
		__field(void *, buf)
		__field(unsigned int, actual)
		__field(unsigned int, length)
		__field(int, status)
		__field(dma_addr_t, dma)
		__field(int, zero)
		__field(int, short_not_ok)
		__field(int, no_interrupt)
		__field(struct scatterlist*, sg)
		__field(unsigned int, num_sgs)
		__field(unsigned int, num_mapped_sgs)
		__field(int, start_trb)
		__field(int, end_trb)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->request = &preq->request;
		__entry->preq = preq;
		__entry->buf = preq->request.buf;
		__entry->actual = preq->request.actual;
		__entry->length = preq->request.length;
		__entry->status = preq->request.status;
		__entry->dma = preq->request.dma;
		__entry->zero = preq->request.zero;
		__entry->short_not_ok = preq->request.short_not_ok;
		__entry->no_interrupt = preq->request.no_interrupt;
		__entry->sg = preq->request.sg;
		__entry->num_sgs = preq->request.num_sgs;
		__entry->num_mapped_sgs = preq->request.num_mapped_sgs;
		__entry->start_trb = preq->start_trb;
		__entry->end_trb = preq->end_trb;
	),
	TP_printk("%s: req: %p, preq: %p, req buf: %p, length: %u/%u, status: %d,"
		  "buf dma: (%pad), %s%s%s, sg: %p, num_sgs: %d, num_m_sgs: %d,"
		  "trb: [start: %d, end: %d]",
		  __get_str(name), __entry->request, __entry->preq,
		  __entry->buf, __entry->actual, __entry->length,
		  __entry->status, &__entry->dma,
		  __entry->zero ? "Z" : "z",
		  __entry->short_not_ok ? "S" : "s",
		  __entry->no_interrupt ? "I" : "i",
		  __entry->sg, __entry->num_sgs, __entry->num_mapped_sgs,
		  __entry->start_trb,
		  __entry->end_trb
	)
);

DEFINE_EVENT(cdns2_log_request, cdns2_request_enqueue,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

DEFINE_EVENT(cdns2_log_request, cdns2_request_enqueue_error,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

DEFINE_EVENT(cdns2_log_request, cdns2_alloc_request,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

DEFINE_EVENT(cdns2_log_request, cdns2_free_request,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

DEFINE_EVENT(cdns2_log_request, cdns2_ep_queue,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

DEFINE_EVENT(cdns2_log_request, cdns2_request_dequeue,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

DEFINE_EVENT(cdns2_log_request, cdns2_request_giveback,
	TP_PROTO(struct cdns2_request *preq),
	TP_ARGS(preq)
);

TRACE_EVENT(cdns2_ep0_enqueue,
	TP_PROTO(struct cdns2_device *dev_priv, struct usb_request *request),
	TP_ARGS(dev_priv, request),
	TP_STRUCT__entry(
		__field(int, dir)
		__field(int, length)
	),
	TP_fast_assign(
		__entry->dir = dev_priv->eps[0].dir;
		__entry->length = request->length;
	),
	TP_printk("Queue to ep0%s length: %u", __entry->dir ? "in" : "out",
		  __entry->length)
);

DECLARE_EVENT_CLASS(cdns2_log_map_request,
	TP_PROTO(struct cdns2_request *priv_req),
	TP_ARGS(priv_req),
	TP_STRUCT__entry(
		__string(name, priv_req->pep->name)
		__field(struct usb_request *, req)
		__field(void *, buf)
		__field(dma_addr_t, dma)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->req = &priv_req->request;
		__entry->buf = priv_req->request.buf;
		__entry->dma = priv_req->request.dma;
	),
	TP_printk("%s: req: %p, req buf %p, dma %p",
		  __get_str(name), __entry->req, __entry->buf, &__entry->dma
	)
);

DEFINE_EVENT(cdns2_log_map_request, cdns2_map_request,
	     TP_PROTO(struct cdns2_request *req),
	     TP_ARGS(req)
);
DEFINE_EVENT(cdns2_log_map_request, cdns2_mapped_request,
	     TP_PROTO(struct cdns2_request *req),
	     TP_ARGS(req)
);

DECLARE_EVENT_CLASS(cdns2_log_trb,
	TP_PROTO(struct cdns2_endpoint *pep, struct cdns2_trb *trb),
	TP_ARGS(pep, trb),
	TP_STRUCT__entry(
		__string(name, pep->name)
		__field(struct cdns2_trb *, trb)
		__field(u32, buffer)
		__field(u32, length)
		__field(u32, control)
		__field(u32, type)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->trb = trb;
		__entry->buffer = le32_to_cpu(trb->buffer);
		__entry->length = le32_to_cpu(trb->length);
		__entry->control = le32_to_cpu(trb->control);
		__entry->type = usb_endpoint_type(pep->endpoint.desc);
	),
	TP_printk("%s: trb V: %p, dma buf: P: 0x%08x, %s",
		 __get_str(name), __entry->trb, __entry->buffer,
		 cdns2_decode_trb(__get_buf(CDNS2_MSG_MAX), CDNS2_MSG_MAX,
				  __entry->control, __entry->length,
				  __entry->buffer))
);

DEFINE_EVENT(cdns2_log_trb, cdns2_queue_trb,
	TP_PROTO(struct cdns2_endpoint *pep, struct cdns2_trb *trb),
	TP_ARGS(pep, trb)
);

DEFINE_EVENT(cdns2_log_trb, cdns2_complete_trb,
	TP_PROTO(struct cdns2_endpoint *pep, struct cdns2_trb *trb),
	TP_ARGS(pep, trb)
);

DECLARE_EVENT_CLASS(cdns2_log_ring,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep),
	TP_STRUCT__entry(
		__dynamic_array(u8, tr_seg, TR_SEG_SIZE)
		__dynamic_array(u8, pep, sizeof(struct cdns2_endpoint))
		__dynamic_array(char, buffer,
				(TRBS_PER_SEGMENT * 65) + CDNS2_MSG_MAX)
	),
	TP_fast_assign(
		memcpy(__get_dynamic_array(pep), pep,
		       sizeof(struct cdns2_endpoint));
		memcpy(__get_dynamic_array(tr_seg), pep->ring.trbs,
		       TR_SEG_SIZE);
	),

	TP_printk("%s",
		  cdns2_raw_ring((struct cdns2_endpoint *)__get_str(pep),
				    (struct cdns2_trb *)__get_str(tr_seg),
				    __get_str(buffer),
				    (TRBS_PER_SEGMENT * 65) + CDNS2_MSG_MAX))
);

DEFINE_EVENT(cdns2_log_ring, cdns2_ring,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep)
);

DECLARE_EVENT_CLASS(cdns2_log_ep,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep),
	TP_STRUCT__entry(
		__string(name, pep->name)
		__field(unsigned int, maxpacket)
		__field(unsigned int, maxpacket_limit)
		__field(unsigned int, flags)
		__field(unsigned int, dir)
		__field(u8, enqueue)
		__field(u8, dequeue)
	),
	TP_fast_assign(
		__assign_str(name);
		__entry->maxpacket = pep->endpoint.maxpacket;
		__entry->maxpacket_limit = pep->endpoint.maxpacket_limit;
		__entry->flags = pep->ep_state;
		__entry->dir = pep->dir;
		__entry->enqueue = pep->ring.enqueue;
		__entry->dequeue = pep->ring.dequeue;
	),
	TP_printk("%s: mps: %d/%d, enq idx: %d, deq idx: %d, "
		  "flags: %s%s%s%s, dir: %s",
		__get_str(name), __entry->maxpacket,
		__entry->maxpacket_limit, __entry->enqueue,
		__entry->dequeue,
		__entry->flags & EP_ENABLED ? "EN | " : "",
		__entry->flags & EP_STALLED ? "STALLED | " : "",
		__entry->flags & EP_WEDGE ? "WEDGE | " : "",
		__entry->flags & EP_RING_FULL ? "RING FULL |" : "",
		__entry->dir ? "IN" : "OUT"
	)
);

DEFINE_EVENT(cdns2_log_ep, cdns2_gadget_ep_enable,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep)
);

DEFINE_EVENT(cdns2_log_ep, cdns2_gadget_ep_disable,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep)
);

DEFINE_EVENT(cdns2_log_ep, cdns2_iso_out_ep_disable,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep)
);

DEFINE_EVENT(cdns2_log_ep, cdns2_ep_busy_try_halt_again,
	TP_PROTO(struct cdns2_endpoint *pep),
	TP_ARGS(pep)
);

DECLARE_EVENT_CLASS(cdns2_log_request_handled,
	TP_PROTO(struct cdns2_request *priv_req, int current_index,
		 int handled),
	TP_ARGS(priv_req, current_index, handled),
	TP_STRUCT__entry(
		__field(struct cdns2_request *, priv_req)
		__field(unsigned int, dma_position)
		__field(unsigned int, handled)
		__field(unsigned int, dequeue_idx)
		__field(unsigned int, enqueue_idx)
		__field(unsigned int, start_trb)
		__field(unsigned int, end_trb)
	),
	TP_fast_assign(
		__entry->priv_req = priv_req;
		__entry->dma_position = current_index;
		__entry->handled = handled;
		__entry->dequeue_idx = priv_req->pep->ring.dequeue;
		__entry->enqueue_idx = priv_req->pep->ring.enqueue;
		__entry->start_trb = priv_req->start_trb;
		__entry->end_trb = priv_req->end_trb;
	),
	TP_printk("Req: %p %s, DMA pos: %d, ep deq: %d, ep enq: %d,"
		  " start trb: %d, end trb: %d",
		__entry->priv_req,
		__entry->handled ? "handled" : "not handled",
		__entry->dma_position, __entry->dequeue_idx,
		__entry->enqueue_idx, __entry->start_trb,
		__entry->end_trb
	)
);

DEFINE_EVENT(cdns2_log_request_handled, cdns2_request_handled,
	TP_PROTO(struct cdns2_request *priv_req, int current_index,
		 int handled),
	TP_ARGS(priv_req, current_index, handled)
);

DECLARE_EVENT_CLASS(cdns2_log_epx_reg_config,
	TP_PROTO(struct cdns2_device *pdev, struct cdns2_endpoint *pep),
	TP_ARGS(pdev, pep),
	TP_STRUCT__entry(
		__string(ep_name, pep->name)
		__field(u8, burst_size)
		__field(__le16, maxpack_reg)
		__field(__u8, con_reg)
		__field(u32, ep_sel_reg)
		__field(u32, ep_sts_en_reg)
		__field(u32, ep_cfg_reg)
	),
	TP_fast_assign(
		__assign_str(ep_name);
		__entry->burst_size = pep->trb_burst_size;
		__entry->maxpack_reg = pep->dir ? readw(&pdev->epx_regs->txmaxpack[pep->num - 1]) :
						  readw(&pdev->epx_regs->rxmaxpack[pep->num - 1]);
		__entry->con_reg = pep->dir ? readb(&pdev->epx_regs->ep[pep->num - 1].txcon) :
					      readb(&pdev->epx_regs->ep[pep->num - 1].rxcon);
		__entry->ep_sel_reg = readl(&pdev->adma_regs->ep_sel);
		__entry->ep_sts_en_reg = readl(&pdev->adma_regs->ep_sts_en);
		__entry->ep_cfg_reg = readl(&pdev->adma_regs->ep_cfg);
	),

	TP_printk("%s, maxpack: %d, con: %02x, dma_ep_sel: %08x, dma_ep_sts_en: %08x"
		  " dma_ep_cfg %08x",
		  __get_str(ep_name), __entry->maxpack_reg, __entry->con_reg,
		  __entry->ep_sel_reg, __entry->ep_sts_en_reg,
		  __entry->ep_cfg_reg
	)
);

DEFINE_EVENT(cdns2_log_epx_reg_config, cdns2_epx_hw_cfg,
	TP_PROTO(struct cdns2_device *pdev, struct cdns2_endpoint *pep),
	TP_ARGS(pdev, pep)
);

#endif /* __LINUX_CDNS2_TRACE */

/* This part must be outside header guard. */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cdns2-trace

#include <trace/define_trace.h>
