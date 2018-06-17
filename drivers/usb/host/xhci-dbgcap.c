// SPDX-License-Identifier: GPL-2.0
/**
 * xhci-dbgcap.c - xHCI debug capability support
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/nls.h>

#include "xhci.h"
#include "xhci-trace.h"
#include "xhci-dbgcap.h"

static inline void *
dbc_dma_alloc_coherent(struct xhci_hcd *xhci, size_t size,
		       dma_addr_t *dma_handle, gfp_t flags)
{
	void		*vaddr;

	vaddr = dma_alloc_coherent(xhci_to_hcd(xhci)->self.sysdev,
				   size, dma_handle, flags);
	memset(vaddr, 0, size);
	return vaddr;
}

static inline void
dbc_dma_free_coherent(struct xhci_hcd *xhci, size_t size,
		      void *cpu_addr, dma_addr_t dma_handle)
{
	if (cpu_addr)
		dma_free_coherent(xhci_to_hcd(xhci)->self.sysdev,
				  size, cpu_addr, dma_handle);
}

static u32 xhci_dbc_populate_strings(struct dbc_str_descs *strings)
{
	struct usb_string_descriptor	*s_desc;
	u32				string_length;

	/* Serial string: */
	s_desc = (struct usb_string_descriptor *)strings->serial;
	utf8s_to_utf16s(DBC_STRING_SERIAL, strlen(DBC_STRING_SERIAL),
			UTF16_LITTLE_ENDIAN, (wchar_t *)s_desc->wData,
			DBC_MAX_STRING_LENGTH);

	s_desc->bLength		= (strlen(DBC_STRING_SERIAL) + 1) * 2;
	s_desc->bDescriptorType	= USB_DT_STRING;
	string_length		= s_desc->bLength;
	string_length		<<= 8;

	/* Product string: */
	s_desc = (struct usb_string_descriptor *)strings->product;
	utf8s_to_utf16s(DBC_STRING_PRODUCT, strlen(DBC_STRING_PRODUCT),
			UTF16_LITTLE_ENDIAN, (wchar_t *)s_desc->wData,
			DBC_MAX_STRING_LENGTH);

	s_desc->bLength		= (strlen(DBC_STRING_PRODUCT) + 1) * 2;
	s_desc->bDescriptorType	= USB_DT_STRING;
	string_length		+= s_desc->bLength;
	string_length		<<= 8;

	/* Manufacture string: */
	s_desc = (struct usb_string_descriptor *)strings->manufacturer;
	utf8s_to_utf16s(DBC_STRING_MANUFACTURER,
			strlen(DBC_STRING_MANUFACTURER),
			UTF16_LITTLE_ENDIAN, (wchar_t *)s_desc->wData,
			DBC_MAX_STRING_LENGTH);

	s_desc->bLength		= (strlen(DBC_STRING_MANUFACTURER) + 1) * 2;
	s_desc->bDescriptorType	= USB_DT_STRING;
	string_length		+= s_desc->bLength;
	string_length		<<= 8;

	/* String0: */
	strings->string0[0]	= 4;
	strings->string0[1]	= USB_DT_STRING;
	strings->string0[2]	= 0x09;
	strings->string0[3]	= 0x04;
	string_length		+= 4;

	return string_length;
}

static void xhci_dbc_init_contexts(struct xhci_hcd *xhci, u32 string_length)
{
	struct xhci_dbc		*dbc;
	struct dbc_info_context	*info;
	struct xhci_ep_ctx	*ep_ctx;
	u32			dev_info;
	dma_addr_t		deq, dma;
	unsigned int		max_burst;

	dbc = xhci->dbc;
	if (!dbc)
		return;

	/* Populate info Context: */
	info			= (struct dbc_info_context *)dbc->ctx->bytes;
	dma			= dbc->string_dma;
	info->string0		= cpu_to_le64(dma);
	info->manufacturer	= cpu_to_le64(dma + DBC_MAX_STRING_LENGTH);
	info->product		= cpu_to_le64(dma + DBC_MAX_STRING_LENGTH * 2);
	info->serial		= cpu_to_le64(dma + DBC_MAX_STRING_LENGTH * 3);
	info->length		= cpu_to_le32(string_length);

	/* Populate bulk out endpoint context: */
	ep_ctx			= dbc_bulkout_ctx(dbc);
	max_burst		= DBC_CTRL_MAXBURST(readl(&dbc->regs->control));
	deq			= dbc_bulkout_enq(dbc);
	ep_ctx->ep_info		= 0;
	ep_ctx->ep_info2	= dbc_epctx_info2(BULK_OUT_EP, 1024, max_burst);
	ep_ctx->deq		= cpu_to_le64(deq | dbc->ring_out->cycle_state);

	/* Populate bulk in endpoint context: */
	ep_ctx			= dbc_bulkin_ctx(dbc);
	deq			= dbc_bulkin_enq(dbc);
	ep_ctx->ep_info		= 0;
	ep_ctx->ep_info2	= dbc_epctx_info2(BULK_IN_EP, 1024, max_burst);
	ep_ctx->deq		= cpu_to_le64(deq | dbc->ring_in->cycle_state);

	/* Set DbC context and info registers: */
	xhci_write_64(xhci, dbc->ctx->dma, &dbc->regs->dccp);

	dev_info = cpu_to_le32((DBC_VENDOR_ID << 16) | DBC_PROTOCOL);
	writel(dev_info, &dbc->regs->devinfo1);

	dev_info = cpu_to_le32((DBC_DEVICE_REV << 16) | DBC_PRODUCT_ID);
	writel(dev_info, &dbc->regs->devinfo2);
}

static void xhci_dbc_giveback(struct dbc_request *req, int status)
	__releases(&dbc->lock)
	__acquires(&dbc->lock)
{
	struct dbc_ep		*dep = req->dep;
	struct xhci_dbc		*dbc = dep->dbc;
	struct xhci_hcd		*xhci = dbc->xhci;
	struct device		*dev = xhci_to_hcd(dbc->xhci)->self.sysdev;

	list_del_init(&req->list_pending);
	req->trb_dma = 0;
	req->trb = NULL;

	if (req->status == -EINPROGRESS)
		req->status = status;

	trace_xhci_dbc_giveback_request(req);

	dma_unmap_single(dev,
			 req->dma,
			 req->length,
			 dbc_ep_dma_direction(dep));

	/* Give back the transfer request: */
	spin_unlock(&dbc->lock);
	req->complete(xhci, req);
	spin_lock(&dbc->lock);
}

static void xhci_dbc_flush_single_request(struct dbc_request *req)
{
	union xhci_trb	*trb = req->trb;

	trb->generic.field[0]	= 0;
	trb->generic.field[1]	= 0;
	trb->generic.field[2]	= 0;
	trb->generic.field[3]	&= cpu_to_le32(TRB_CYCLE);
	trb->generic.field[3]	|= cpu_to_le32(TRB_TYPE(TRB_TR_NOOP));

	xhci_dbc_giveback(req, -ESHUTDOWN);
}

static void xhci_dbc_flush_endpoint_requests(struct dbc_ep *dep)
{
	struct dbc_request	*req, *tmp;

	list_for_each_entry_safe(req, tmp, &dep->list_pending, list_pending)
		xhci_dbc_flush_single_request(req);
}

static void xhci_dbc_flush_reqests(struct xhci_dbc *dbc)
{
	xhci_dbc_flush_endpoint_requests(&dbc->eps[BULK_OUT]);
	xhci_dbc_flush_endpoint_requests(&dbc->eps[BULK_IN]);
}

struct dbc_request *
dbc_alloc_request(struct dbc_ep *dep, gfp_t gfp_flags)
{
	struct dbc_request	*req;

	req = kzalloc(sizeof(*req), gfp_flags);
	if (!req)
		return NULL;

	req->dep = dep;
	INIT_LIST_HEAD(&req->list_pending);
	INIT_LIST_HEAD(&req->list_pool);
	req->direction = dep->direction;

	trace_xhci_dbc_alloc_request(req);

	return req;
}

void
dbc_free_request(struct dbc_ep *dep, struct dbc_request *req)
{
	trace_xhci_dbc_free_request(req);

	kfree(req);
}

static void
xhci_dbc_queue_trb(struct xhci_ring *ring, u32 field1,
		   u32 field2, u32 field3, u32 field4)
{
	union xhci_trb		*trb, *next;

	trb = ring->enqueue;
	trb->generic.field[0]	= cpu_to_le32(field1);
	trb->generic.field[1]	= cpu_to_le32(field2);
	trb->generic.field[2]	= cpu_to_le32(field3);
	trb->generic.field[3]	= cpu_to_le32(field4);

	trace_xhci_dbc_gadget_ep_queue(ring, &trb->generic);

	ring->num_trbs_free--;
	next = ++(ring->enqueue);
	if (TRB_TYPE_LINK_LE32(next->link.control)) {
		next->link.control ^= cpu_to_le32(TRB_CYCLE);
		ring->enqueue = ring->enq_seg->trbs;
		ring->cycle_state ^= 1;
	}
}

static int xhci_dbc_queue_bulk_tx(struct dbc_ep *dep,
				  struct dbc_request *req)
{
	u64			addr;
	union xhci_trb		*trb;
	unsigned int		num_trbs;
	struct xhci_dbc		*dbc = dep->dbc;
	struct xhci_ring	*ring = dep->ring;
	u32			length, control, cycle;

	num_trbs = count_trbs(req->dma, req->length);
	WARN_ON(num_trbs != 1);
	if (ring->num_trbs_free < num_trbs)
		return -EBUSY;

	addr	= req->dma;
	trb	= ring->enqueue;
	cycle	= ring->cycle_state;
	length	= TRB_LEN(req->length);
	control	= TRB_TYPE(TRB_NORMAL) | TRB_IOC;

	if (cycle)
		control &= cpu_to_le32(~TRB_CYCLE);
	else
		control |= cpu_to_le32(TRB_CYCLE);

	req->trb = ring->enqueue;
	req->trb_dma = xhci_trb_virt_to_dma(ring->enq_seg, ring->enqueue);
	xhci_dbc_queue_trb(ring,
			   lower_32_bits(addr),
			   upper_32_bits(addr),
			   length, control);

	/*
	 * Add a barrier between writes of trb fields and flipping
	 * the cycle bit:
	 */
	wmb();

	if (cycle)
		trb->generic.field[3] |= cpu_to_le32(TRB_CYCLE);
	else
		trb->generic.field[3] &= cpu_to_le32(~TRB_CYCLE);

	writel(DBC_DOOR_BELL_TARGET(dep->direction), &dbc->regs->doorbell);

	return 0;
}

static int
dbc_ep_do_queue(struct dbc_ep *dep, struct dbc_request *req)
{
	int			ret;
	struct device		*dev;
	struct xhci_dbc		*dbc = dep->dbc;
	struct xhci_hcd		*xhci = dbc->xhci;

	dev = xhci_to_hcd(xhci)->self.sysdev;

	if (!req->length || !req->buf)
		return -EINVAL;

	req->actual		= 0;
	req->status		= -EINPROGRESS;

	req->dma = dma_map_single(dev,
				  req->buf,
				  req->length,
				  dbc_ep_dma_direction(dep));
	if (dma_mapping_error(dev, req->dma)) {
		xhci_err(xhci, "failed to map buffer\n");
		return -EFAULT;
	}

	ret = xhci_dbc_queue_bulk_tx(dep, req);
	if (ret) {
		xhci_err(xhci, "failed to queue trbs\n");
		dma_unmap_single(dev,
				 req->dma,
				 req->length,
				 dbc_ep_dma_direction(dep));
		return -EFAULT;
	}

	list_add_tail(&req->list_pending, &dep->list_pending);

	return 0;
}

int dbc_ep_queue(struct dbc_ep *dep, struct dbc_request *req,
		 gfp_t gfp_flags)
{
	unsigned long		flags;
	struct xhci_dbc		*dbc = dep->dbc;
	int			ret = -ESHUTDOWN;

	spin_lock_irqsave(&dbc->lock, flags);
	if (dbc->state == DS_CONFIGURED)
		ret = dbc_ep_do_queue(dep, req);
	spin_unlock_irqrestore(&dbc->lock, flags);

	mod_delayed_work(system_wq, &dbc->event_work, 0);

	trace_xhci_dbc_queue_request(req);

	return ret;
}

static inline void xhci_dbc_do_eps_init(struct xhci_hcd *xhci, bool direction)
{
	struct dbc_ep		*dep;
	struct xhci_dbc		*dbc = xhci->dbc;

	dep			= &dbc->eps[direction];
	dep->dbc		= dbc;
	dep->direction		= direction;
	dep->ring		= direction ? dbc->ring_in : dbc->ring_out;

	INIT_LIST_HEAD(&dep->list_pending);
}

static void xhci_dbc_eps_init(struct xhci_hcd *xhci)
{
	xhci_dbc_do_eps_init(xhci, BULK_OUT);
	xhci_dbc_do_eps_init(xhci, BULK_IN);
}

static void xhci_dbc_eps_exit(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;

	memset(dbc->eps, 0, sizeof(struct dbc_ep) * ARRAY_SIZE(dbc->eps));
}

static int xhci_dbc_mem_init(struct xhci_hcd *xhci, gfp_t flags)
{
	int			ret;
	dma_addr_t		deq;
	u32			string_length;
	struct xhci_dbc		*dbc = xhci->dbc;

	/* Allocate various rings for events and transfers: */
	dbc->ring_evt = xhci_ring_alloc(xhci, 1, 1, TYPE_EVENT, 0, flags);
	if (!dbc->ring_evt)
		goto evt_fail;

	dbc->ring_in = xhci_ring_alloc(xhci, 1, 1, TYPE_BULK, 0, flags);
	if (!dbc->ring_in)
		goto in_fail;

	dbc->ring_out = xhci_ring_alloc(xhci, 1, 1, TYPE_BULK, 0, flags);
	if (!dbc->ring_out)
		goto out_fail;

	/* Allocate and populate ERST: */
	ret = xhci_alloc_erst(xhci, dbc->ring_evt, &dbc->erst, flags);
	if (ret)
		goto erst_fail;

	/* Allocate context data structure: */
	dbc->ctx = xhci_alloc_container_ctx(xhci, XHCI_CTX_TYPE_DEVICE, flags);
	if (!dbc->ctx)
		goto ctx_fail;

	/* Allocate the string table: */
	dbc->string_size = sizeof(struct dbc_str_descs);
	dbc->string = dbc_dma_alloc_coherent(xhci,
					     dbc->string_size,
					     &dbc->string_dma,
					     flags);
	if (!dbc->string)
		goto string_fail;

	/* Setup ERST register: */
	writel(dbc->erst.erst_size, &dbc->regs->ersts);
	xhci_write_64(xhci, dbc->erst.erst_dma_addr, &dbc->regs->erstba);
	deq = xhci_trb_virt_to_dma(dbc->ring_evt->deq_seg,
				   dbc->ring_evt->dequeue);
	xhci_write_64(xhci, deq, &dbc->regs->erdp);

	/* Setup strings and contexts: */
	string_length = xhci_dbc_populate_strings(dbc->string);
	xhci_dbc_init_contexts(xhci, string_length);

	mmiowb();

	xhci_dbc_eps_init(xhci);
	dbc->state = DS_INITIALIZED;

	return 0;

string_fail:
	xhci_free_container_ctx(xhci, dbc->ctx);
	dbc->ctx = NULL;
ctx_fail:
	xhci_free_erst(xhci, &dbc->erst);
erst_fail:
	xhci_ring_free(xhci, dbc->ring_out);
	dbc->ring_out = NULL;
out_fail:
	xhci_ring_free(xhci, dbc->ring_in);
	dbc->ring_in = NULL;
in_fail:
	xhci_ring_free(xhci, dbc->ring_evt);
	dbc->ring_evt = NULL;
evt_fail:
	return -ENOMEM;
}

static void xhci_dbc_mem_cleanup(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;

	if (!dbc)
		return;

	xhci_dbc_eps_exit(xhci);

	if (dbc->string) {
		dbc_dma_free_coherent(xhci,
				      dbc->string_size,
				      dbc->string, dbc->string_dma);
		dbc->string = NULL;
	}

	xhci_free_container_ctx(xhci, dbc->ctx);
	dbc->ctx = NULL;

	xhci_free_erst(xhci, &dbc->erst);
	xhci_ring_free(xhci, dbc->ring_out);
	xhci_ring_free(xhci, dbc->ring_in);
	xhci_ring_free(xhci, dbc->ring_evt);
	dbc->ring_in = NULL;
	dbc->ring_out = NULL;
	dbc->ring_evt = NULL;
}

static int xhci_do_dbc_start(struct xhci_hcd *xhci)
{
	int			ret;
	u32			ctrl;
	struct xhci_dbc		*dbc = xhci->dbc;

	if (dbc->state != DS_DISABLED)
		return -EINVAL;

	writel(0, &dbc->regs->control);
	ret = xhci_handshake(&dbc->regs->control,
			     DBC_CTRL_DBC_ENABLE,
			     0, 1000);
	if (ret)
		return ret;

	ret = xhci_dbc_mem_init(xhci, GFP_ATOMIC);
	if (ret)
		return ret;

	ctrl = readl(&dbc->regs->control);
	writel(ctrl | DBC_CTRL_DBC_ENABLE | DBC_CTRL_PORT_ENABLE,
	       &dbc->regs->control);
	ret = xhci_handshake(&dbc->regs->control,
			     DBC_CTRL_DBC_ENABLE,
			     DBC_CTRL_DBC_ENABLE, 1000);
	if (ret)
		return ret;

	dbc->state = DS_ENABLED;

	return 0;
}

static void xhci_do_dbc_stop(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;

	if (dbc->state == DS_DISABLED)
		return;

	writel(0, &dbc->regs->control);
	xhci_dbc_mem_cleanup(xhci);
	dbc->state = DS_DISABLED;
}

static int xhci_dbc_start(struct xhci_hcd *xhci)
{
	int			ret;
	unsigned long		flags;
	struct xhci_dbc		*dbc = xhci->dbc;

	WARN_ON(!dbc);

	pm_runtime_get_sync(xhci_to_hcd(xhci)->self.controller);

	spin_lock_irqsave(&dbc->lock, flags);
	ret = xhci_do_dbc_start(xhci);
	spin_unlock_irqrestore(&dbc->lock, flags);

	if (ret) {
		pm_runtime_put(xhci_to_hcd(xhci)->self.controller);
		return ret;
	}

	return mod_delayed_work(system_wq, &dbc->event_work, 1);
}

static void xhci_dbc_stop(struct xhci_hcd *xhci)
{
	unsigned long		flags;
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;

	WARN_ON(!dbc);

	cancel_delayed_work_sync(&dbc->event_work);

	if (port->registered)
		xhci_dbc_tty_unregister_device(xhci);

	spin_lock_irqsave(&dbc->lock, flags);
	xhci_do_dbc_stop(xhci);
	spin_unlock_irqrestore(&dbc->lock, flags);

	pm_runtime_put_sync(xhci_to_hcd(xhci)->self.controller);
}

static void
dbc_handle_port_status(struct xhci_hcd *xhci, union xhci_trb *event)
{
	u32			portsc;
	struct xhci_dbc		*dbc = xhci->dbc;

	portsc = readl(&dbc->regs->portsc);
	if (portsc & DBC_PORTSC_CONN_CHANGE)
		xhci_info(xhci, "DbC port connect change\n");

	if (portsc & DBC_PORTSC_RESET_CHANGE)
		xhci_info(xhci, "DbC port reset change\n");

	if (portsc & DBC_PORTSC_LINK_CHANGE)
		xhci_info(xhci, "DbC port link status change\n");

	if (portsc & DBC_PORTSC_CONFIG_CHANGE)
		xhci_info(xhci, "DbC config error change\n");

	/* Port reset change bit will be cleared in other place: */
	writel(portsc & ~DBC_PORTSC_RESET_CHANGE, &dbc->regs->portsc);
}

static void dbc_handle_xfer_event(struct xhci_hcd *xhci, union xhci_trb *event)
{
	struct dbc_ep		*dep;
	struct xhci_ring	*ring;
	int			ep_id;
	int			status;
	u32			comp_code;
	size_t			remain_length;
	struct dbc_request	*req = NULL, *r;

	comp_code	= GET_COMP_CODE(le32_to_cpu(event->generic.field[2]));
	remain_length	= EVENT_TRB_LEN(le32_to_cpu(event->generic.field[2]));
	ep_id		= TRB_TO_EP_ID(le32_to_cpu(event->generic.field[3]));
	dep		= (ep_id == EPID_OUT) ?
				get_out_ep(xhci) : get_in_ep(xhci);
	ring		= dep->ring;

	switch (comp_code) {
	case COMP_SUCCESS:
		remain_length = 0;
	/* FALLTHROUGH */
	case COMP_SHORT_PACKET:
		status = 0;
		break;
	case COMP_TRB_ERROR:
	case COMP_BABBLE_DETECTED_ERROR:
	case COMP_USB_TRANSACTION_ERROR:
	case COMP_STALL_ERROR:
		xhci_warn(xhci, "tx error %d detected\n", comp_code);
		status = -comp_code;
		break;
	default:
		xhci_err(xhci, "unknown tx error %d\n", comp_code);
		status = -comp_code;
		break;
	}

	/* Match the pending request: */
	list_for_each_entry(r, &dep->list_pending, list_pending) {
		if (r->trb_dma == event->trans_event.buffer) {
			req = r;
			break;
		}
	}

	if (!req) {
		xhci_warn(xhci, "no matched request\n");
		return;
	}

	trace_xhci_dbc_handle_transfer(ring, &req->trb->generic);

	ring->num_trbs_free++;
	req->actual = req->length - remain_length;
	xhci_dbc_giveback(req, status);
}

static enum evtreturn xhci_dbc_do_handle_events(struct xhci_dbc *dbc)
{
	dma_addr_t		deq;
	struct dbc_ep		*dep;
	union xhci_trb		*evt;
	u32			ctrl, portsc;
	struct xhci_hcd		*xhci = dbc->xhci;
	bool			update_erdp = false;

	/* DbC state machine: */
	switch (dbc->state) {
	case DS_DISABLED:
	case DS_INITIALIZED:

		return EVT_ERR;
	case DS_ENABLED:
		portsc = readl(&dbc->regs->portsc);
		if (portsc & DBC_PORTSC_CONN_STATUS) {
			dbc->state = DS_CONNECTED;
			xhci_info(xhci, "DbC connected\n");
		}

		return EVT_DONE;
	case DS_CONNECTED:
		ctrl = readl(&dbc->regs->control);
		if (ctrl & DBC_CTRL_DBC_RUN) {
			dbc->state = DS_CONFIGURED;
			xhci_info(xhci, "DbC configured\n");
			portsc = readl(&dbc->regs->portsc);
			writel(portsc, &dbc->regs->portsc);
			return EVT_GSER;
		}

		return EVT_DONE;
	case DS_CONFIGURED:
		/* Handle cable unplug event: */
		portsc = readl(&dbc->regs->portsc);
		if (!(portsc & DBC_PORTSC_PORT_ENABLED) &&
		    !(portsc & DBC_PORTSC_CONN_STATUS)) {
			xhci_info(xhci, "DbC cable unplugged\n");
			dbc->state = DS_ENABLED;
			xhci_dbc_flush_reqests(dbc);

			return EVT_DISC;
		}

		/* Handle debug port reset event: */
		if (portsc & DBC_PORTSC_RESET_CHANGE) {
			xhci_info(xhci, "DbC port reset\n");
			writel(portsc, &dbc->regs->portsc);
			dbc->state = DS_ENABLED;
			xhci_dbc_flush_reqests(dbc);

			return EVT_DISC;
		}

		/* Handle endpoint stall event: */
		ctrl = readl(&dbc->regs->control);
		if ((ctrl & DBC_CTRL_HALT_IN_TR) ||
		    (ctrl & DBC_CTRL_HALT_OUT_TR)) {
			xhci_info(xhci, "DbC Endpoint stall\n");
			dbc->state = DS_STALLED;

			if (ctrl & DBC_CTRL_HALT_IN_TR) {
				dep = get_in_ep(xhci);
				xhci_dbc_flush_endpoint_requests(dep);
			}

			if (ctrl & DBC_CTRL_HALT_OUT_TR) {
				dep = get_out_ep(xhci);
				xhci_dbc_flush_endpoint_requests(dep);
			}

			return EVT_DONE;
		}

		/* Clear DbC run change bit: */
		if (ctrl & DBC_CTRL_DBC_RUN_CHANGE) {
			writel(ctrl, &dbc->regs->control);
			ctrl = readl(&dbc->regs->control);
		}

		break;
	case DS_STALLED:
		ctrl = readl(&dbc->regs->control);
		if (!(ctrl & DBC_CTRL_HALT_IN_TR) &&
		    !(ctrl & DBC_CTRL_HALT_OUT_TR) &&
		    (ctrl & DBC_CTRL_DBC_RUN)) {
			dbc->state = DS_CONFIGURED;
			break;
		}

		return EVT_DONE;
	default:
		xhci_err(xhci, "Unknown DbC state %d\n", dbc->state);
		break;
	}

	/* Handle the events in the event ring: */
	evt = dbc->ring_evt->dequeue;
	while ((le32_to_cpu(evt->event_cmd.flags) & TRB_CYCLE) ==
			dbc->ring_evt->cycle_state) {
		/*
		 * Add a barrier between reading the cycle flag and any
		 * reads of the event's flags/data below:
		 */
		rmb();

		trace_xhci_dbc_handle_event(dbc->ring_evt, &evt->generic);

		switch (le32_to_cpu(evt->event_cmd.flags) & TRB_TYPE_BITMASK) {
		case TRB_TYPE(TRB_PORT_STATUS):
			dbc_handle_port_status(xhci, evt);
			break;
		case TRB_TYPE(TRB_TRANSFER):
			dbc_handle_xfer_event(xhci, evt);
			break;
		default:
			break;
		}

		inc_deq(xhci, dbc->ring_evt);
		evt = dbc->ring_evt->dequeue;
		update_erdp = true;
	}

	/* Update event ring dequeue pointer: */
	if (update_erdp) {
		deq = xhci_trb_virt_to_dma(dbc->ring_evt->deq_seg,
					   dbc->ring_evt->dequeue);
		xhci_write_64(xhci, deq, &dbc->regs->erdp);
	}

	return EVT_DONE;
}

static void xhci_dbc_handle_events(struct work_struct *work)
{
	int			ret;
	enum evtreturn		evtr;
	struct xhci_dbc		*dbc;
	unsigned long		flags;
	struct xhci_hcd		*xhci;

	dbc = container_of(to_delayed_work(work), struct xhci_dbc, event_work);
	xhci = dbc->xhci;

	spin_lock_irqsave(&dbc->lock, flags);
	evtr = xhci_dbc_do_handle_events(dbc);
	spin_unlock_irqrestore(&dbc->lock, flags);

	switch (evtr) {
	case EVT_GSER:
		ret = xhci_dbc_tty_register_device(xhci);
		if (ret) {
			xhci_err(xhci, "failed to alloc tty device\n");
			break;
		}

		xhci_info(xhci, "DbC now attached to /dev/ttyDBC0\n");
		break;
	case EVT_DISC:
		xhci_dbc_tty_unregister_device(xhci);
		break;
	case EVT_DONE:
		break;
	default:
		xhci_info(xhci, "stop handling dbc events\n");
		return;
	}

	mod_delayed_work(system_wq, &dbc->event_work, 1);
}

static void xhci_do_dbc_exit(struct xhci_hcd *xhci)
{
	unsigned long		flags;

	spin_lock_irqsave(&xhci->lock, flags);
	kfree(xhci->dbc);
	xhci->dbc = NULL;
	spin_unlock_irqrestore(&xhci->lock, flags);
}

static int xhci_do_dbc_init(struct xhci_hcd *xhci)
{
	u32			reg;
	struct xhci_dbc		*dbc;
	unsigned long		flags;
	void __iomem		*base;
	int			dbc_cap_offs;

	base = &xhci->cap_regs->hc_capbase;
	dbc_cap_offs = xhci_find_next_ext_cap(base, 0, XHCI_EXT_CAPS_DEBUG);
	if (!dbc_cap_offs)
		return -ENODEV;

	dbc = kzalloc(sizeof(*dbc), GFP_KERNEL);
	if (!dbc)
		return -ENOMEM;

	dbc->regs = base + dbc_cap_offs;

	/* We will avoid using DbC in xhci driver if it's in use. */
	reg = readl(&dbc->regs->control);
	if (reg & DBC_CTRL_DBC_ENABLE) {
		kfree(dbc);
		return -EBUSY;
	}

	spin_lock_irqsave(&xhci->lock, flags);
	if (xhci->dbc) {
		spin_unlock_irqrestore(&xhci->lock, flags);
		kfree(dbc);
		return -EBUSY;
	}
	xhci->dbc = dbc;
	spin_unlock_irqrestore(&xhci->lock, flags);

	dbc->xhci = xhci;
	INIT_DELAYED_WORK(&dbc->event_work, xhci_dbc_handle_events);
	spin_lock_init(&dbc->lock);

	return 0;
}

static ssize_t dbc_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	const char		*p;
	struct xhci_dbc		*dbc;
	struct xhci_hcd		*xhci;

	xhci = hcd_to_xhci(dev_get_drvdata(dev));
	dbc = xhci->dbc;

	switch (dbc->state) {
	case DS_DISABLED:
		p = "disabled";
		break;
	case DS_INITIALIZED:
		p = "initialized";
		break;
	case DS_ENABLED:
		p = "enabled";
		break;
	case DS_CONNECTED:
		p = "connected";
		break;
	case DS_CONFIGURED:
		p = "configured";
		break;
	case DS_STALLED:
		p = "stalled";
		break;
	default:
		p = "unknown";
	}

	return sprintf(buf, "%s\n", p);
}

static ssize_t dbc_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct xhci_dbc		*dbc;
	struct xhci_hcd		*xhci;

	xhci = hcd_to_xhci(dev_get_drvdata(dev));
	dbc = xhci->dbc;

	if (!strncmp(buf, "enable", 6))
		xhci_dbc_start(xhci);
	else if (!strncmp(buf, "disable", 7))
		xhci_dbc_stop(xhci);
	else
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(dbc);

int xhci_dbc_init(struct xhci_hcd *xhci)
{
	int			ret;
	struct device		*dev = xhci_to_hcd(xhci)->self.controller;

	ret = xhci_do_dbc_init(xhci);
	if (ret)
		goto init_err3;

	ret = xhci_dbc_tty_register_driver(xhci);
	if (ret)
		goto init_err2;

	ret = device_create_file(dev, &dev_attr_dbc);
	if (ret)
		goto init_err1;

	return 0;

init_err1:
	xhci_dbc_tty_unregister_driver();
init_err2:
	xhci_do_dbc_exit(xhci);
init_err3:
	return ret;
}

void xhci_dbc_exit(struct xhci_hcd *xhci)
{
	struct device		*dev = xhci_to_hcd(xhci)->self.controller;

	if (!xhci->dbc)
		return;

	device_remove_file(dev, &dev_attr_dbc);
	xhci_dbc_tty_unregister_driver();
	xhci_dbc_stop(xhci);
	xhci_do_dbc_exit(xhci);
}

#ifdef CONFIG_PM
int xhci_dbc_suspend(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;

	if (!dbc)
		return 0;

	if (dbc->state == DS_CONFIGURED)
		dbc->resume_required = 1;

	xhci_dbc_stop(xhci);

	return 0;
}

int xhci_dbc_resume(struct xhci_hcd *xhci)
{
	int			ret = 0;
	struct xhci_dbc		*dbc = xhci->dbc;

	if (!dbc)
		return 0;

	if (dbc->resume_required) {
		dbc->resume_required = 0;
		xhci_dbc_start(xhci);
	}

	return ret;
}
#endif /* CONFIG_PM */
