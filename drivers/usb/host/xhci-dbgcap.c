// SPDX-License-Identifier: GPL-2.0
/*
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

static void dbc_free_ctx(struct device *dev, struct xhci_container_ctx *ctx)
{
	if (!ctx)
		return;
	dma_free_coherent(dev, ctx->size, ctx->bytes, ctx->dma);
	kfree(ctx);
}

/* we use only one segment for DbC rings */
static void dbc_ring_free(struct device *dev, struct xhci_ring *ring)
{
	if (!ring)
		return;

	if (ring->first_seg && ring->first_seg->trbs) {
		dma_free_coherent(dev, TRB_SEGMENT_SIZE,
				  ring->first_seg->trbs,
				  ring->first_seg->dma);
		kfree(ring->first_seg);
	}
	kfree(ring);
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

static void xhci_dbc_init_contexts(struct xhci_dbc *dbc, u32 string_length)
{
	struct dbc_info_context	*info;
	struct xhci_ep_ctx	*ep_ctx;
	u32			dev_info;
	dma_addr_t		deq, dma;
	unsigned int		max_burst;

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
	lo_hi_writeq(dbc->ctx->dma, &dbc->regs->dccp);

	dev_info = cpu_to_le32((DBC_VENDOR_ID << 16) | DBC_PROTOCOL);
	writel(dev_info, &dbc->regs->devinfo1);

	dev_info = cpu_to_le32((DBC_DEVICE_REV << 16) | DBC_PRODUCT_ID);
	writel(dev_info, &dbc->regs->devinfo2);
}

static void xhci_dbc_giveback(struct dbc_request *req, int status)
	__releases(&dbc->lock)
	__acquires(&dbc->lock)
{
	struct xhci_dbc		*dbc = req->dbc;
	struct device		*dev = dbc->dev;

	list_del_init(&req->list_pending);
	req->trb_dma = 0;
	req->trb = NULL;

	if (req->status == -EINPROGRESS)
		req->status = status;

	trace_xhci_dbc_giveback_request(req);

	dma_unmap_single(dev,
			 req->dma,
			 req->length,
			 dbc_ep_dma_direction(req));

	/* Give back the transfer request: */
	spin_unlock(&dbc->lock);
	req->complete(dbc, req);
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

static void xhci_dbc_flush_requests(struct xhci_dbc *dbc)
{
	xhci_dbc_flush_endpoint_requests(&dbc->eps[BULK_OUT]);
	xhci_dbc_flush_endpoint_requests(&dbc->eps[BULK_IN]);
}

struct dbc_request *
dbc_alloc_request(struct xhci_dbc *dbc, unsigned int direction, gfp_t flags)
{
	struct dbc_request	*req;

	if (direction != BULK_IN &&
	    direction != BULK_OUT)
		return NULL;

	if (!dbc)
		return NULL;

	req = kzalloc(sizeof(*req), flags);
	if (!req)
		return NULL;

	req->dbc = dbc;
	INIT_LIST_HEAD(&req->list_pending);
	INIT_LIST_HEAD(&req->list_pool);
	req->direction = direction;

	trace_xhci_dbc_alloc_request(req);

	return req;
}

void
dbc_free_request(struct dbc_request *req)
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
	struct xhci_dbc		*dbc = req->dbc;
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
dbc_ep_do_queue(struct dbc_request *req)
{
	int			ret;
	struct xhci_dbc		*dbc = req->dbc;
	struct device		*dev = dbc->dev;
	struct dbc_ep		*dep = &dbc->eps[req->direction];

	if (!req->length || !req->buf)
		return -EINVAL;

	req->actual		= 0;
	req->status		= -EINPROGRESS;

	req->dma = dma_map_single(dev,
				  req->buf,
				  req->length,
				  dbc_ep_dma_direction(dep));
	if (dma_mapping_error(dev, req->dma)) {
		dev_err(dbc->dev, "failed to map buffer\n");
		return -EFAULT;
	}

	ret = xhci_dbc_queue_bulk_tx(dep, req);
	if (ret) {
		dev_err(dbc->dev, "failed to queue trbs\n");
		dma_unmap_single(dev,
				 req->dma,
				 req->length,
				 dbc_ep_dma_direction(dep));
		return -EFAULT;
	}

	list_add_tail(&req->list_pending, &dep->list_pending);

	return 0;
}

int dbc_ep_queue(struct dbc_request *req)
{
	unsigned long		flags;
	struct xhci_dbc		*dbc = req->dbc;
	int			ret = -ESHUTDOWN;

	if (!dbc)
		return -ENODEV;

	if (req->direction != BULK_IN &&
	    req->direction != BULK_OUT)
		return -EINVAL;

	spin_lock_irqsave(&dbc->lock, flags);
	if (dbc->state == DS_CONFIGURED)
		ret = dbc_ep_do_queue(req);
	spin_unlock_irqrestore(&dbc->lock, flags);

	mod_delayed_work(system_wq, &dbc->event_work, 0);

	trace_xhci_dbc_queue_request(req);

	return ret;
}

static inline void xhci_dbc_do_eps_init(struct xhci_dbc *dbc, bool direction)
{
	struct dbc_ep		*dep;

	dep			= &dbc->eps[direction];
	dep->dbc		= dbc;
	dep->direction		= direction;
	dep->ring		= direction ? dbc->ring_in : dbc->ring_out;

	INIT_LIST_HEAD(&dep->list_pending);
}

static void xhci_dbc_eps_init(struct xhci_dbc *dbc)
{
	xhci_dbc_do_eps_init(dbc, BULK_OUT);
	xhci_dbc_do_eps_init(dbc, BULK_IN);
}

static void xhci_dbc_eps_exit(struct xhci_dbc *dbc)
{
	memset(dbc->eps, 0, sizeof(struct dbc_ep) * ARRAY_SIZE(dbc->eps));
}

static int dbc_erst_alloc(struct device *dev, struct xhci_ring *evt_ring,
		    struct xhci_erst *erst, gfp_t flags)
{
	erst->entries = dma_alloc_coherent(dev, sizeof(struct xhci_erst_entry),
					   &erst->erst_dma_addr, flags);
	if (!erst->entries)
		return -ENOMEM;

	erst->num_entries = 1;
	erst->entries[0].seg_addr = cpu_to_le64(evt_ring->first_seg->dma);
	erst->entries[0].seg_size = cpu_to_le32(TRBS_PER_SEGMENT);
	erst->entries[0].rsvd = 0;
	return 0;
}

static void dbc_erst_free(struct device *dev, struct xhci_erst *erst)
{
	if (erst->entries)
		dma_free_coherent(dev, sizeof(struct xhci_erst_entry),
				  erst->entries, erst->erst_dma_addr);
	erst->entries = NULL;
}

static struct xhci_container_ctx *
dbc_alloc_ctx(struct device *dev, gfp_t flags)
{
	struct xhci_container_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), flags);
	if (!ctx)
		return NULL;

	/* xhci 7.6.9, all three contexts; info, ep-out and ep-in. Each 64 bytes*/
	ctx->size = 3 * DBC_CONTEXT_SIZE;
	ctx->bytes = dma_alloc_coherent(dev, ctx->size, &ctx->dma, flags);
	if (!ctx->bytes) {
		kfree(ctx);
		return NULL;
	}
	return ctx;
}

static struct xhci_ring *
xhci_dbc_ring_alloc(struct device *dev, enum xhci_ring_type type, gfp_t flags)
{
	struct xhci_ring *ring;
	struct xhci_segment *seg;
	dma_addr_t dma;

	ring = kzalloc(sizeof(*ring), flags);
	if (!ring)
		return NULL;

	ring->num_segs = 1;
	ring->type = type;

	seg = kzalloc(sizeof(*seg), flags);
	if (!seg)
		goto seg_fail;

	ring->first_seg = seg;
	ring->last_seg = seg;
	seg->next = seg;

	seg->trbs = dma_alloc_coherent(dev, TRB_SEGMENT_SIZE, &dma, flags);
	if (!seg->trbs)
		goto dma_fail;

	seg->dma = dma;

	/* Only event ring does not use link TRB */
	if (type != TYPE_EVENT) {
		union xhci_trb *trb = &seg->trbs[TRBS_PER_SEGMENT - 1];

		trb->link.segment_ptr = cpu_to_le64(dma);
		trb->link.control = cpu_to_le32(LINK_TOGGLE | TRB_TYPE(TRB_LINK));
	}
	INIT_LIST_HEAD(&ring->td_list);
	xhci_initialize_ring_info(ring, 1);
	return ring;
dma_fail:
	kfree(seg);
seg_fail:
	kfree(ring);
	return NULL;
}

static int xhci_dbc_mem_init(struct xhci_dbc *dbc, gfp_t flags)
{
	int			ret;
	dma_addr_t		deq;
	u32			string_length;
	struct device		*dev = dbc->dev;

	/* Allocate various rings for events and transfers: */
	dbc->ring_evt = xhci_dbc_ring_alloc(dev, TYPE_EVENT, flags);
	if (!dbc->ring_evt)
		goto evt_fail;

	dbc->ring_in = xhci_dbc_ring_alloc(dev, TYPE_BULK, flags);
	if (!dbc->ring_in)
		goto in_fail;

	dbc->ring_out = xhci_dbc_ring_alloc(dev, TYPE_BULK, flags);
	if (!dbc->ring_out)
		goto out_fail;

	/* Allocate and populate ERST: */
	ret = dbc_erst_alloc(dev, dbc->ring_evt, &dbc->erst, flags);
	if (ret)
		goto erst_fail;

	/* Allocate context data structure: */
	dbc->ctx = dbc_alloc_ctx(dev, flags); /* was sysdev, and is still */
	if (!dbc->ctx)
		goto ctx_fail;

	/* Allocate the string table: */
	dbc->string_size = sizeof(struct dbc_str_descs);
	dbc->string = dma_alloc_coherent(dev, dbc->string_size,
					 &dbc->string_dma, flags);
	if (!dbc->string)
		goto string_fail;

	/* Setup ERST register: */
	writel(dbc->erst.erst_size, &dbc->regs->ersts);

	lo_hi_writeq(dbc->erst.erst_dma_addr, &dbc->regs->erstba);
	deq = xhci_trb_virt_to_dma(dbc->ring_evt->deq_seg,
				   dbc->ring_evt->dequeue);
	lo_hi_writeq(deq, &dbc->regs->erdp);

	/* Setup strings and contexts: */
	string_length = xhci_dbc_populate_strings(dbc->string);
	xhci_dbc_init_contexts(dbc, string_length);

	xhci_dbc_eps_init(dbc);
	dbc->state = DS_INITIALIZED;

	return 0;

string_fail:
	dbc_free_ctx(dev, dbc->ctx);
	dbc->ctx = NULL;
ctx_fail:
	dbc_erst_free(dev, &dbc->erst);
erst_fail:
	dbc_ring_free(dev, dbc->ring_out);
	dbc->ring_out = NULL;
out_fail:
	dbc_ring_free(dev, dbc->ring_in);
	dbc->ring_in = NULL;
in_fail:
	dbc_ring_free(dev, dbc->ring_evt);
	dbc->ring_evt = NULL;
evt_fail:
	return -ENOMEM;
}

static void xhci_dbc_mem_cleanup(struct xhci_dbc *dbc)
{
	if (!dbc)
		return;

	xhci_dbc_eps_exit(dbc);

	if (dbc->string) {
		dma_free_coherent(dbc->dev, dbc->string_size,
				  dbc->string, dbc->string_dma);
		dbc->string = NULL;
	}

	dbc_free_ctx(dbc->dev, dbc->ctx);
	dbc->ctx = NULL;

	dbc_erst_free(dbc->dev, &dbc->erst);
	dbc_ring_free(dbc->dev, dbc->ring_out);
	dbc_ring_free(dbc->dev, dbc->ring_in);
	dbc_ring_free(dbc->dev, dbc->ring_evt);
	dbc->ring_in = NULL;
	dbc->ring_out = NULL;
	dbc->ring_evt = NULL;
}

static int xhci_do_dbc_start(struct xhci_dbc *dbc)
{
	int			ret;
	u32			ctrl;

	if (dbc->state != DS_DISABLED)
		return -EINVAL;

	writel(0, &dbc->regs->control);
	ret = xhci_handshake(&dbc->regs->control,
			     DBC_CTRL_DBC_ENABLE,
			     0, 1000);
	if (ret)
		return ret;

	ret = xhci_dbc_mem_init(dbc, GFP_ATOMIC);
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

static int xhci_do_dbc_stop(struct xhci_dbc *dbc)
{
	if (dbc->state == DS_DISABLED)
		return -1;

	writel(0, &dbc->regs->control);
	dbc->state = DS_DISABLED;

	return 0;
}

static int xhci_dbc_start(struct xhci_dbc *dbc)
{
	int			ret;
	unsigned long		flags;

	WARN_ON(!dbc);

	pm_runtime_get_sync(dbc->dev); /* note this was self.controller */

	spin_lock_irqsave(&dbc->lock, flags);
	ret = xhci_do_dbc_start(dbc);
	spin_unlock_irqrestore(&dbc->lock, flags);

	if (ret) {
		pm_runtime_put(dbc->dev); /* note this was self.controller */
		return ret;
	}

	return mod_delayed_work(system_wq, &dbc->event_work, 1);
}

static void xhci_dbc_stop(struct xhci_dbc *dbc)
{
	int ret;
	unsigned long		flags;

	WARN_ON(!dbc);

	switch (dbc->state) {
	case DS_DISABLED:
		return;
	case DS_CONFIGURED:
	case DS_STALLED:
		if (dbc->driver->disconnect)
			dbc->driver->disconnect(dbc);
		break;
	default:
		break;
	}

	cancel_delayed_work_sync(&dbc->event_work);

	spin_lock_irqsave(&dbc->lock, flags);
	ret = xhci_do_dbc_stop(dbc);
	spin_unlock_irqrestore(&dbc->lock, flags);

	if (!ret) {
		xhci_dbc_mem_cleanup(dbc);
		pm_runtime_put_sync(dbc->dev); /* note, was self.controller */
	}
}

static void
dbc_handle_port_status(struct xhci_dbc *dbc, union xhci_trb *event)
{
	u32			portsc;

	portsc = readl(&dbc->regs->portsc);
	if (portsc & DBC_PORTSC_CONN_CHANGE)
		dev_info(dbc->dev, "DbC port connect change\n");

	if (portsc & DBC_PORTSC_RESET_CHANGE)
		dev_info(dbc->dev, "DbC port reset change\n");

	if (portsc & DBC_PORTSC_LINK_CHANGE)
		dev_info(dbc->dev, "DbC port link status change\n");

	if (portsc & DBC_PORTSC_CONFIG_CHANGE)
		dev_info(dbc->dev, "DbC config error change\n");

	/* Port reset change bit will be cleared in other place: */
	writel(portsc & ~DBC_PORTSC_RESET_CHANGE, &dbc->regs->portsc);
}

static void dbc_handle_xfer_event(struct xhci_dbc *dbc, union xhci_trb *event)
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
				get_out_ep(dbc) : get_in_ep(dbc);
	ring		= dep->ring;

	switch (comp_code) {
	case COMP_SUCCESS:
		remain_length = 0;
		fallthrough;
	case COMP_SHORT_PACKET:
		status = 0;
		break;
	case COMP_TRB_ERROR:
	case COMP_BABBLE_DETECTED_ERROR:
	case COMP_USB_TRANSACTION_ERROR:
	case COMP_STALL_ERROR:
		dev_warn(dbc->dev, "tx error %d detected\n", comp_code);
		status = -comp_code;
		break;
	default:
		dev_err(dbc->dev, "unknown tx error %d\n", comp_code);
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
		dev_warn(dbc->dev, "no matched request\n");
		return;
	}

	trace_xhci_dbc_handle_transfer(ring, &req->trb->generic);

	ring->num_trbs_free++;
	req->actual = req->length - remain_length;
	xhci_dbc_giveback(req, status);
}

static void inc_evt_deq(struct xhci_ring *ring)
{
	/* If on the last TRB of the segment go back to the beginning */
	if (ring->dequeue == &ring->deq_seg->trbs[TRBS_PER_SEGMENT - 1]) {
		ring->cycle_state ^= 1;
		ring->dequeue = ring->deq_seg->trbs;
		return;
	}
	ring->dequeue++;
}

static enum evtreturn xhci_dbc_do_handle_events(struct xhci_dbc *dbc)
{
	dma_addr_t		deq;
	struct dbc_ep		*dep;
	union xhci_trb		*evt;
	u32			ctrl, portsc;
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
			dev_info(dbc->dev, "DbC connected\n");
		}

		return EVT_DONE;
	case DS_CONNECTED:
		ctrl = readl(&dbc->regs->control);
		if (ctrl & DBC_CTRL_DBC_RUN) {
			dbc->state = DS_CONFIGURED;
			dev_info(dbc->dev, "DbC configured\n");
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
			dev_info(dbc->dev, "DbC cable unplugged\n");
			dbc->state = DS_ENABLED;
			xhci_dbc_flush_requests(dbc);

			return EVT_DISC;
		}

		/* Handle debug port reset event: */
		if (portsc & DBC_PORTSC_RESET_CHANGE) {
			dev_info(dbc->dev, "DbC port reset\n");
			writel(portsc, &dbc->regs->portsc);
			dbc->state = DS_ENABLED;
			xhci_dbc_flush_requests(dbc);

			return EVT_DISC;
		}

		/* Handle endpoint stall event: */
		ctrl = readl(&dbc->regs->control);
		if ((ctrl & DBC_CTRL_HALT_IN_TR) ||
		    (ctrl & DBC_CTRL_HALT_OUT_TR)) {
			dev_info(dbc->dev, "DbC Endpoint stall\n");
			dbc->state = DS_STALLED;

			if (ctrl & DBC_CTRL_HALT_IN_TR) {
				dep = get_in_ep(dbc);
				xhci_dbc_flush_endpoint_requests(dep);
			}

			if (ctrl & DBC_CTRL_HALT_OUT_TR) {
				dep = get_out_ep(dbc);
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
		dev_err(dbc->dev, "Unknown DbC state %d\n", dbc->state);
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
			dbc_handle_port_status(dbc, evt);
			break;
		case TRB_TYPE(TRB_TRANSFER):
			dbc_handle_xfer_event(dbc, evt);
			break;
		default:
			break;
		}

		inc_evt_deq(dbc->ring_evt);

		evt = dbc->ring_evt->dequeue;
		update_erdp = true;
	}

	/* Update event ring dequeue pointer: */
	if (update_erdp) {
		deq = xhci_trb_virt_to_dma(dbc->ring_evt->deq_seg,
					   dbc->ring_evt->dequeue);
		lo_hi_writeq(deq, &dbc->regs->erdp);
	}

	return EVT_DONE;
}

static void xhci_dbc_handle_events(struct work_struct *work)
{
	enum evtreturn		evtr;
	struct xhci_dbc		*dbc;
	unsigned long		flags;

	dbc = container_of(to_delayed_work(work), struct xhci_dbc, event_work);

	spin_lock_irqsave(&dbc->lock, flags);
	evtr = xhci_dbc_do_handle_events(dbc);
	spin_unlock_irqrestore(&dbc->lock, flags);

	switch (evtr) {
	case EVT_GSER:
		if (dbc->driver->configure)
			dbc->driver->configure(dbc);
		break;
	case EVT_DISC:
		if (dbc->driver->disconnect)
			dbc->driver->disconnect(dbc);
		break;
	case EVT_DONE:
		break;
	default:
		dev_info(dbc->dev, "stop handling dbc events\n");
		return;
	}

	mod_delayed_work(system_wq, &dbc->event_work, 1);
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
	struct xhci_hcd		*xhci;
	struct xhci_dbc		*dbc;

	xhci = hcd_to_xhci(dev_get_drvdata(dev));
	dbc = xhci->dbc;

	if (!strncmp(buf, "enable", 6))
		xhci_dbc_start(dbc);
	else if (!strncmp(buf, "disable", 7))
		xhci_dbc_stop(dbc);
	else
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(dbc);

struct xhci_dbc *
xhci_alloc_dbc(struct device *dev, void __iomem *base, const struct dbc_driver *driver)
{
	struct xhci_dbc		*dbc;
	int			ret;

	dbc = kzalloc(sizeof(*dbc), GFP_KERNEL);
	if (!dbc)
		return NULL;

	dbc->regs = base;
	dbc->dev = dev;
	dbc->driver = driver;

	if (readl(&dbc->regs->control) & DBC_CTRL_DBC_ENABLE)
		goto err;

	INIT_DELAYED_WORK(&dbc->event_work, xhci_dbc_handle_events);
	spin_lock_init(&dbc->lock);

	ret = device_create_file(dev, &dev_attr_dbc);
	if (ret)
		goto err;

	return dbc;
err:
	kfree(dbc);
	return NULL;
}

/* undo what xhci_alloc_dbc() did */
void xhci_dbc_remove(struct xhci_dbc *dbc)
{
	if (!dbc)
		return;
	/* stop hw, stop wq and call dbc->ops->stop() */
	xhci_dbc_stop(dbc);

	/* remove sysfs files */
	device_remove_file(dbc->dev, &dev_attr_dbc);

	kfree(dbc);
}


int xhci_create_dbc_dev(struct xhci_hcd *xhci)
{
	struct device		*dev;
	void __iomem		*base;
	int			ret;
	int			dbc_cap_offs;

	/* create all parameters needed resembling a dbc device */
	dev = xhci_to_hcd(xhci)->self.controller;
	base = &xhci->cap_regs->hc_capbase;

	dbc_cap_offs = xhci_find_next_ext_cap(base, 0, XHCI_EXT_CAPS_DEBUG);
	if (!dbc_cap_offs)
		return -ENODEV;

	/* already allocated and in use */
	if (xhci->dbc)
		return -EBUSY;

	ret = xhci_dbc_tty_probe(dev, base + dbc_cap_offs, xhci);

	return ret;
}

void xhci_remove_dbc_dev(struct xhci_hcd *xhci)
{
	unsigned long		flags;

	if (!xhci->dbc)
		return;

	xhci_dbc_tty_remove(xhci->dbc);
	spin_lock_irqsave(&xhci->lock, flags);
	xhci->dbc = NULL;
	spin_unlock_irqrestore(&xhci->lock, flags);
}

#ifdef CONFIG_PM
int xhci_dbc_suspend(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;

	if (!dbc)
		return 0;

	if (dbc->state == DS_CONFIGURED)
		dbc->resume_required = 1;

	xhci_dbc_stop(dbc);

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
		xhci_dbc_start(dbc);
	}

	return ret;
}
#endif /* CONFIG_PM */

int xhci_dbc_init(void)
{
	return dbc_tty_init();
}

void xhci_dbc_exit(void)
{
	dbc_tty_exit();
}
