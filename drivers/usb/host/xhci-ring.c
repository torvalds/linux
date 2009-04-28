/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Ring initialization rules:
 * 1. Each segment is initialized to zero, except for link TRBs.
 * 2. Ring cycle state = 0.  This represents Producer Cycle State (PCS) or
 *    Consumer Cycle State (CCS), depending on ring function.
 * 3. Enqueue pointer = dequeue pointer = address of first TRB in the segment.
 *
 * Ring behavior rules:
 * 1. A ring is empty if enqueue == dequeue.  This means there will always be at
 *    least one free TRB in the ring.  This is useful if you want to turn that
 *    into a link TRB and expand the ring.
 * 2. When incrementing an enqueue or dequeue pointer, if the next TRB is a
 *    link TRB, then load the pointer with the address in the link TRB.  If the
 *    link TRB had its toggle bit set, you may need to update the ring cycle
 *    state (see cycle bit rules).  You may have to do this multiple times
 *    until you reach a non-link TRB.
 * 3. A ring is full if enqueue++ (for the definition of increment above)
 *    equals the dequeue pointer.
 *
 * Cycle bit rules:
 * 1. When a consumer increments a dequeue pointer and encounters a toggle bit
 *    in a link TRB, it must toggle the ring cycle state.
 * 2. When a producer increments an enqueue pointer and encounters a toggle bit
 *    in a link TRB, it must toggle the ring cycle state.
 *
 * Producer rules:
 * 1. Check if ring is full before you enqueue.
 * 2. Write the ring cycle state to the cycle bit in the TRB you're enqueuing.
 *    Update enqueue pointer between each write (which may update the ring
 *    cycle state).
 * 3. Notify consumer.  If SW is producer, it rings the doorbell for command
 *    and endpoint rings.  If HC is the producer for the event ring,
 *    and it generates an interrupt according to interrupt modulation rules.
 *
 * Consumer rules:
 * 1. Check if TRB belongs to you.  If the cycle bit == your ring cycle state,
 *    the TRB is owned by the consumer.
 * 2. Update dequeue pointer (which may update the ring cycle state) and
 *    continue processing TRBs until you reach a TRB which is not owned by you.
 * 3. Notify the producer.  SW is the consumer for the event ring, and it
 *   updates event ring dequeue pointer.  HC is the consumer for the command and
 *   endpoint rings; it generates events on the event ring for these.
 */

#include "xhci.h"

/*
 * Returns zero if the TRB isn't in this segment, otherwise it returns the DMA
 * address of the TRB.
 */
dma_addr_t trb_virt_to_dma(struct xhci_segment *seg,
		union xhci_trb *trb)
{
	unsigned int offset;

	if (!seg || !trb || (void *) trb < (void *) seg->trbs)
		return 0;
	/* offset in bytes, since these are byte-addressable */
	offset = (unsigned int) trb - (unsigned int) seg->trbs;
	/* SEGMENT_SIZE in bytes, trbs are 16-byte aligned */
	if (offset > SEGMENT_SIZE || (offset % sizeof(*trb)) != 0)
		return 0;
	return seg->dma + offset;
}

/* Does this link TRB point to the first segment in a ring,
 * or was the previous TRB the last TRB on the last segment in the ERST?
 */
static inline bool last_trb_on_last_seg(struct xhci_hcd *xhci, struct xhci_ring *ring,
		struct xhci_segment *seg, union xhci_trb *trb)
{
	if (ring == xhci->event_ring)
		return (trb == &seg->trbs[TRBS_PER_SEGMENT]) &&
			(seg->next == xhci->event_ring->first_seg);
	else
		return trb->link.control & LINK_TOGGLE;
}

/* Is this TRB a link TRB or was the last TRB the last TRB in this event ring
 * segment?  I.e. would the updated event TRB pointer step off the end of the
 * event seg?
 */
static inline int last_trb(struct xhci_hcd *xhci, struct xhci_ring *ring,
		struct xhci_segment *seg, union xhci_trb *trb)
{
	if (ring == xhci->event_ring)
		return trb == &seg->trbs[TRBS_PER_SEGMENT];
	else
		return (trb->link.control & TRB_TYPE_BITMASK) == TRB_TYPE(TRB_LINK);
}

/*
 * See Cycle bit rules. SW is the consumer for the event ring only.
 * Don't make a ring full of link TRBs.  That would be dumb and this would loop.
 */
static void inc_deq(struct xhci_hcd *xhci, struct xhci_ring *ring, bool consumer)
{
	union xhci_trb *next = ++(ring->dequeue);

	ring->deq_updates++;
	/* Update the dequeue pointer further if that was a link TRB or we're at
	 * the end of an event ring segment (which doesn't have link TRBS)
	 */
	while (last_trb(xhci, ring, ring->deq_seg, next)) {
		if (consumer && last_trb_on_last_seg(xhci, ring, ring->deq_seg, next)) {
			ring->cycle_state = (ring->cycle_state ? 0 : 1);
			if (!in_interrupt())
				xhci_dbg(xhci, "Toggle cycle state for ring 0x%x = %i\n",
						(unsigned int) ring,
						(unsigned int) ring->cycle_state);
		}
		ring->deq_seg = ring->deq_seg->next;
		ring->dequeue = ring->deq_seg->trbs;
		next = ring->dequeue;
	}
}

/*
 * See Cycle bit rules. SW is the consumer for the event ring only.
 * Don't make a ring full of link TRBs.  That would be dumb and this would loop.
 *
 * If we've just enqueued a TRB that is in the middle of a TD (meaning the
 * chain bit is set), then set the chain bit in all the following link TRBs.
 * If we've enqueued the last TRB in a TD, make sure the following link TRBs
 * have their chain bit cleared (so that each Link TRB is a separate TD).
 *
 * Section 6.4.4.1 of the 0.95 spec says link TRBs cannot have the chain bit
 * set, but other sections talk about dealing with the chain bit set.
 * Assume section 6.4.4.1 is wrong, and the chain bit can be set in a Link TRB.
 */
static void inc_enq(struct xhci_hcd *xhci, struct xhci_ring *ring, bool consumer)
{
	u32 chain;
	union xhci_trb *next;

	chain = ring->enqueue->generic.field[3] & TRB_CHAIN;
	next = ++(ring->enqueue);

	ring->enq_updates++;
	/* Update the dequeue pointer further if that was a link TRB or we're at
	 * the end of an event ring segment (which doesn't have link TRBS)
	 */
	while (last_trb(xhci, ring, ring->enq_seg, next)) {
		if (!consumer) {
			if (ring != xhci->event_ring) {
				/* Give this link TRB to the hardware */
				if (next->link.control & TRB_CYCLE)
					next->link.control &= (u32) ~TRB_CYCLE;
				else
					next->link.control |= (u32) TRB_CYCLE;
				next->link.control &= TRB_CHAIN;
				next->link.control |= chain;
			}
			/* Toggle the cycle bit after the last ring segment. */
			if (last_trb_on_last_seg(xhci, ring, ring->enq_seg, next)) {
				ring->cycle_state = (ring->cycle_state ? 0 : 1);
				if (!in_interrupt())
					xhci_dbg(xhci, "Toggle cycle state for ring 0x%x = %i\n",
							(unsigned int) ring,
							(unsigned int) ring->cycle_state);
			}
		}
		ring->enq_seg = ring->enq_seg->next;
		ring->enqueue = ring->enq_seg->trbs;
		next = ring->enqueue;
	}
}

/*
 * Check to see if there's room to enqueue num_trbs on the ring.  See rules
 * above.
 * FIXME: this would be simpler and faster if we just kept track of the number
 * of free TRBs in a ring.
 */
static int room_on_ring(struct xhci_hcd *xhci, struct xhci_ring *ring,
		unsigned int num_trbs)
{
	int i;
	union xhci_trb *enq = ring->enqueue;
	struct xhci_segment *enq_seg = ring->enq_seg;

	/* Check if ring is empty */
	if (enq == ring->dequeue)
		return 1;
	/* Make sure there's an extra empty TRB available */
	for (i = 0; i <= num_trbs; ++i) {
		if (enq == ring->dequeue)
			return 0;
		enq++;
		while (last_trb(xhci, ring, enq_seg, enq)) {
			enq_seg = enq_seg->next;
			enq = enq_seg->trbs;
		}
	}
	return 1;
}

void set_hc_event_deq(struct xhci_hcd *xhci)
{
	u32 temp;
	dma_addr_t deq;

	deq = trb_virt_to_dma(xhci->event_ring->deq_seg,
			xhci->event_ring->dequeue);
	if (deq == 0 && !in_interrupt())
		xhci_warn(xhci, "WARN something wrong with SW event ring "
				"dequeue ptr.\n");
	/* Update HC event ring dequeue pointer */
	temp = xhci_readl(xhci, &xhci->ir_set->erst_dequeue[0]);
	temp &= ERST_PTR_MASK;
	if (!in_interrupt())
		xhci_dbg(xhci, "// Write event ring dequeue pointer\n");
	xhci_writel(xhci, 0, &xhci->ir_set->erst_dequeue[1]);
	xhci_writel(xhci, (deq & ~ERST_PTR_MASK) | temp,
			&xhci->ir_set->erst_dequeue[0]);
}

/* Ring the host controller doorbell after placing a command on the ring */
void ring_cmd_db(struct xhci_hcd *xhci)
{
	u32 temp;

	xhci_dbg(xhci, "// Ding dong!\n");
	temp = xhci_readl(xhci, &xhci->dba->doorbell[0]) & DB_MASK;
	xhci_writel(xhci, temp | DB_TARGET_HOST, &xhci->dba->doorbell[0]);
	/* Flush PCI posted writes */
	xhci_readl(xhci, &xhci->dba->doorbell[0]);
}

static void handle_cmd_completion(struct xhci_hcd *xhci,
		struct xhci_event_cmd *event)
{
	int slot_id = TRB_TO_SLOT_ID(event->flags);
	u64 cmd_dma;
	dma_addr_t cmd_dequeue_dma;

	cmd_dma = (((u64) event->cmd_trb[1]) << 32) + event->cmd_trb[0];
	cmd_dequeue_dma = trb_virt_to_dma(xhci->cmd_ring->deq_seg,
			xhci->cmd_ring->dequeue);
	/* Is the command ring deq ptr out of sync with the deq seg ptr? */
	if (cmd_dequeue_dma == 0) {
		xhci->error_bitmask |= 1 << 4;
		return;
	}
	/* Does the DMA address match our internal dequeue pointer address? */
	if (cmd_dma != (u64) cmd_dequeue_dma) {
		xhci->error_bitmask |= 1 << 5;
		return;
	}
	switch (xhci->cmd_ring->dequeue->generic.field[3] & TRB_TYPE_BITMASK) {
	case TRB_TYPE(TRB_ENABLE_SLOT):
		if (GET_COMP_CODE(event->status) == COMP_SUCCESS)
			xhci->slot_id = slot_id;
		else
			xhci->slot_id = 0;
		complete(&xhci->addr_dev);
		break;
	case TRB_TYPE(TRB_DISABLE_SLOT):
		if (xhci->devs[slot_id])
			xhci_free_virt_device(xhci, slot_id);
		break;
	case TRB_TYPE(TRB_CONFIG_EP):
		xhci->devs[slot_id]->cmd_status = GET_COMP_CODE(event->status);
		complete(&xhci->devs[slot_id]->cmd_completion);
		break;
	case TRB_TYPE(TRB_ADDR_DEV):
		xhci->devs[slot_id]->cmd_status = GET_COMP_CODE(event->status);
		complete(&xhci->addr_dev);
		break;
	case TRB_TYPE(TRB_CMD_NOOP):
		++xhci->noops_handled;
		break;
	default:
		/* Skip over unknown commands on the event ring */
		xhci->error_bitmask |= 1 << 6;
		break;
	}
	inc_deq(xhci, xhci->cmd_ring, false);
}

static void handle_port_status(struct xhci_hcd *xhci,
		union xhci_trb *event)
{
	u32 port_id;

	/* Port status change events always have a successful completion code */
	if (GET_COMP_CODE(event->generic.field[2]) != COMP_SUCCESS) {
		xhci_warn(xhci, "WARN: xHC returned failed port status event\n");
		xhci->error_bitmask |= 1 << 8;
	}
	/* FIXME: core doesn't care about all port link state changes yet */
	port_id = GET_PORT_ID(event->generic.field[0]);
	xhci_dbg(xhci, "Port Status Change Event for port %d\n", port_id);

	/* Update event ring dequeue pointer before dropping the lock */
	inc_deq(xhci, xhci->event_ring, true);
	set_hc_event_deq(xhci);

	spin_unlock(&xhci->lock);
	/* Pass this up to the core */
	usb_hcd_poll_rh_status(xhci_to_hcd(xhci));
	spin_lock(&xhci->lock);
}

/*
 * This TD is defined by the TRBs starting at start_trb in start_seg and ending
 * at end_trb, which may be in another segment.  If the suspect DMA address is a
 * TRB in this TD, this function returns that TRB's segment.  Otherwise it
 * returns 0.
 */
static struct xhci_segment *trb_in_td(
		struct xhci_segment *start_seg,
		union xhci_trb	*start_trb,
		union xhci_trb	*end_trb,
		dma_addr_t	suspect_dma)
{
	dma_addr_t start_dma;
	dma_addr_t end_seg_dma;
	dma_addr_t end_trb_dma;
	struct xhci_segment *cur_seg;

	start_dma = trb_virt_to_dma(start_seg, start_trb);
	cur_seg = start_seg;

	do {
		/*
		 * Last TRB is a link TRB (unless we start inserting links in
		 * the middle, FIXME if you do)
		 */
		end_seg_dma = trb_virt_to_dma(cur_seg, &start_seg->trbs[TRBS_PER_SEGMENT - 2]);
		/* If the end TRB isn't in this segment, this is set to 0 */
		end_trb_dma = trb_virt_to_dma(cur_seg, end_trb);

		if (end_trb_dma > 0) {
			/* The end TRB is in this segment, so suspect should be here */
			if (start_dma <= end_trb_dma) {
				if (suspect_dma >= start_dma && suspect_dma <= end_trb_dma)
					return cur_seg;
			} else {
				/* Case for one segment with
				 * a TD wrapped around to the top
				 */
				if ((suspect_dma >= start_dma &&
							suspect_dma <= end_seg_dma) ||
						(suspect_dma >= cur_seg->dma &&
						 suspect_dma <= end_trb_dma))
					return cur_seg;
			}
			return 0;
		} else {
			/* Might still be somewhere in this segment */
			if (suspect_dma >= start_dma && suspect_dma <= end_seg_dma)
				return cur_seg;
		}
		cur_seg = cur_seg->next;
		start_dma = trb_virt_to_dma(cur_seg, &cur_seg->trbs[0]);
	} while (1);

}

/*
 * If this function returns an error condition, it means it got a Transfer
 * event with a corrupted Slot ID, Endpoint ID, or TRB DMA address.
 * At this point, the host controller is probably hosed and should be reset.
 */
static int handle_tx_event(struct xhci_hcd *xhci,
		struct xhci_transfer_event *event)
{
	struct xhci_virt_device *xdev;
	struct xhci_ring *ep_ring;
	int ep_index;
	struct xhci_td *td = 0;
	dma_addr_t event_dma;
	struct xhci_segment *event_seg;
	union xhci_trb *event_trb;
	struct urb *urb;
	int status = -EINPROGRESS;

	xdev = xhci->devs[TRB_TO_SLOT_ID(event->flags)];
	if (!xdev) {
		xhci_err(xhci, "ERROR Transfer event pointed to bad slot\n");
		return -ENODEV;
	}

	/* Endpoint ID is 1 based, our index is zero based */
	ep_index = TRB_TO_EP_ID(event->flags) - 1;
	ep_ring = xdev->ep_rings[ep_index];
	if (!ep_ring || (xdev->out_ctx->ep[ep_index].ep_info & EP_STATE_MASK) == EP_STATE_DISABLED) {
		xhci_err(xhci, "ERROR Transfer event pointed to disabled endpoint\n");
		return -ENODEV;
	}

	event_dma = event->buffer[0];
	if (event->buffer[1] != 0)
		xhci_warn(xhci, "WARN ignoring upper 32-bits of 64-bit TRB dma address\n");

	/* This TRB should be in the TD at the head of this ring's TD list */
	if (list_empty(&ep_ring->td_list)) {
		xhci_warn(xhci, "WARN Event TRB for slot %d ep %d with no TDs queued?\n",
				TRB_TO_SLOT_ID(event->flags), ep_index);
		xhci_dbg(xhci, "Event TRB with TRB type ID %u\n",
				(unsigned int) (event->flags & TRB_TYPE_BITMASK)>>10);
		xhci_print_trb_offsets(xhci, (union xhci_trb *) event);
		urb = NULL;
		goto cleanup;
	}
	td = list_entry(ep_ring->td_list.next, struct xhci_td, td_list);

	/* Is this a TRB in the currently executing TD? */
	event_seg = trb_in_td(ep_ring->deq_seg, ep_ring->dequeue,
			td->last_trb, event_dma);
	if (!event_seg) {
		/* HC is busted, give up! */
		xhci_err(xhci, "ERROR Transfer event TRB DMA ptr not part of current TD\n");
		return -ESHUTDOWN;
	}
	event_trb = &event_seg->trbs[(event_dma - event_seg->dma) / sizeof(*event_trb)];
	xhci_dbg(xhci, "Event TRB with TRB type ID %u\n",
			(unsigned int) (event->flags & TRB_TYPE_BITMASK)>>10);
	xhci_dbg(xhci, "Offset 0x00 (buffer[0]) = 0x%x\n",
			(unsigned int) event->buffer[0]);
	xhci_dbg(xhci, "Offset 0x04 (buffer[0]) = 0x%x\n",
			(unsigned int) event->buffer[1]);
	xhci_dbg(xhci, "Offset 0x08 (transfer length) = 0x%x\n",
			(unsigned int) event->transfer_len);
	xhci_dbg(xhci, "Offset 0x0C (flags) = 0x%x\n",
			(unsigned int) event->flags);

	/* Look for common error cases */
	switch (GET_COMP_CODE(event->transfer_len)) {
	/* Skip codes that require special handling depending on
	 * transfer type
	 */
	case COMP_SUCCESS:
	case COMP_SHORT_TX:
		break;
	case COMP_STALL:
		xhci_warn(xhci, "WARN: Stalled endpoint\n");
		status = -EPIPE;
		break;
	case COMP_TRB_ERR:
		xhci_warn(xhci, "WARN: TRB error on endpoint\n");
		status = -EILSEQ;
		break;
	case COMP_TX_ERR:
		xhci_warn(xhci, "WARN: transfer error on endpoint\n");
		status = -EPROTO;
		break;
	case COMP_DB_ERR:
		xhci_warn(xhci, "WARN: HC couldn't access mem fast enough\n");
		status = -ENOSR;
		break;
	default:
		xhci_warn(xhci, "ERROR Unknown event condition, HC probably busted\n");
		urb = NULL;
		goto cleanup;
	}
	/* Now update the urb's actual_length and give back to the core */
	/* Was this a control transfer? */
	if (usb_endpoint_xfer_control(&td->urb->ep->desc)) {
		xhci_debug_trb(xhci, xhci->event_ring->dequeue);
		switch (GET_COMP_CODE(event->transfer_len)) {
		case COMP_SUCCESS:
			if (event_trb == ep_ring->dequeue) {
				xhci_warn(xhci, "WARN: Success on ctrl setup TRB without IOC set??\n");
				status = -ESHUTDOWN;
			} else if (event_trb != td->last_trb) {
				xhci_warn(xhci, "WARN: Success on ctrl data TRB without IOC set??\n");
				status = -ESHUTDOWN;
			} else {
				xhci_dbg(xhci, "Successful control transfer!\n");
				status = 0;
			}
			break;
		case COMP_SHORT_TX:
			xhci_warn(xhci, "WARN: short transfer on control ep\n");
			status = -EREMOTEIO;
			break;
		default:
			/* Others already handled above */
			break;
		}
		/*
		 * Did we transfer any data, despite the errors that might have
		 * happened?  I.e. did we get past the setup stage?
		 */
		if (event_trb != ep_ring->dequeue) {
			/* The event was for the status stage */
			if (event_trb == td->last_trb) {
				td->urb->actual_length = td->urb->transfer_buffer_length;
			} else {
			/* The event was for the data stage */
				td->urb->actual_length = td->urb->transfer_buffer_length -
					TRB_LEN(event->transfer_len);
			}
		}
	} else {
		switch (GET_COMP_CODE(event->transfer_len)) {
		case COMP_SUCCESS:
			/* Double check that the HW transferred everything. */
			if (event_trb != td->last_trb) {
				xhci_warn(xhci, "WARN Successful completion "
						"on short TX\n");
				if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
					status = -EREMOTEIO;
				else
					status = 0;
			} else {
				xhci_dbg(xhci, "Successful bulk transfer!\n");
				status = 0;
			}
			break;
		case COMP_SHORT_TX:
			if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
				status = -EREMOTEIO;
			else
				status = 0;
			break;
		default:
			/* Others already handled above */
			break;
		}
		dev_dbg(&td->urb->dev->dev,
				"ep %#x - asked for %d bytes, "
				"%d bytes untransferred\n",
				td->urb->ep->desc.bEndpointAddress,
				td->urb->transfer_buffer_length,
				TRB_LEN(event->transfer_len));
		/* Fast path - was this the last TRB in the TD for this URB? */
		if (event_trb == td->last_trb) {
			if (TRB_LEN(event->transfer_len) != 0) {
				td->urb->actual_length =
					td->urb->transfer_buffer_length -
					TRB_LEN(event->transfer_len);
				if (td->urb->actual_length < 0) {
					xhci_warn(xhci, "HC gave bad length "
							"of %d bytes left\n",
							TRB_LEN(event->transfer_len));
					td->urb->actual_length = 0;
				}
				if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
					status = -EREMOTEIO;
				else
					status = 0;
			} else {
				td->urb->actual_length = td->urb->transfer_buffer_length;
				/* Ignore a short packet completion if the
				 * untransferred length was zero.
				 */
				status = 0;
			}
		} else {
			/* Slow path - walk the list, starting from the first
			 * TRB to get the actual length transferred
			 */
			td->urb->actual_length = 0;
			while (ep_ring->dequeue != event_trb) {
				td->urb->actual_length += TRB_LEN(ep_ring->dequeue->generic.field[2]);
				inc_deq(xhci, ep_ring, false);
			}
			td->urb->actual_length += TRB_LEN(ep_ring->dequeue->generic.field[2]) -
				TRB_LEN(event->transfer_len);

		}
	}
	/* Update ring dequeue pointer */
	while (ep_ring->dequeue != td->last_trb)
		inc_deq(xhci, ep_ring, false);
	inc_deq(xhci, ep_ring, false);

	/* Clean up the endpoint's TD list */
	urb = td->urb;
	list_del(&td->td_list);
	kfree(td);
	urb->hcpriv = NULL;
cleanup:
	inc_deq(xhci, xhci->event_ring, true);
	set_hc_event_deq(xhci);

	/* FIXME for multi-TD URBs (who have buffers bigger than 64MB) */
	if (urb) {
		usb_hcd_unlink_urb_from_ep(xhci_to_hcd(xhci), urb);
		spin_unlock(&xhci->lock);
		usb_hcd_giveback_urb(xhci_to_hcd(xhci), urb, status);
		spin_lock(&xhci->lock);
	}
	return 0;
}

/*
 * This function handles all OS-owned events on the event ring.  It may drop
 * xhci->lock between event processing (e.g. to pass up port status changes).
 */
void handle_event(struct xhci_hcd *xhci)
{
	union xhci_trb *event;
	int update_ptrs = 1;
	int ret;

	if (!xhci->event_ring || !xhci->event_ring->dequeue) {
		xhci->error_bitmask |= 1 << 1;
		return;
	}

	event = xhci->event_ring->dequeue;
	/* Does the HC or OS own the TRB? */
	if ((event->event_cmd.flags & TRB_CYCLE) !=
			xhci->event_ring->cycle_state) {
		xhci->error_bitmask |= 1 << 2;
		return;
	}

	/* FIXME: Handle more event types. */
	switch ((event->event_cmd.flags & TRB_TYPE_BITMASK)) {
	case TRB_TYPE(TRB_COMPLETION):
		handle_cmd_completion(xhci, &event->event_cmd);
		break;
	case TRB_TYPE(TRB_PORT_STATUS):
		handle_port_status(xhci, event);
		update_ptrs = 0;
		break;
	case TRB_TYPE(TRB_TRANSFER):
		ret = handle_tx_event(xhci, &event->trans_event);
		if (ret < 0)
			xhci->error_bitmask |= 1 << 9;
		else
			update_ptrs = 0;
		break;
	default:
		xhci->error_bitmask |= 1 << 3;
	}

	if (update_ptrs) {
		/* Update SW and HC event ring dequeue pointer */
		inc_deq(xhci, xhci->event_ring, true);
		set_hc_event_deq(xhci);
	}
	/* Are there more items on the event ring? */
	handle_event(xhci);
}

/****		Endpoint Ring Operations	****/

/*
 * Generic function for queueing a TRB on a ring.
 * The caller must have checked to make sure there's room on the ring.
 */
static void queue_trb(struct xhci_hcd *xhci, struct xhci_ring *ring,
		bool consumer,
		u32 field1, u32 field2, u32 field3, u32 field4)
{
	struct xhci_generic_trb *trb;

	trb = &ring->enqueue->generic;
	trb->field[0] = field1;
	trb->field[1] = field2;
	trb->field[2] = field3;
	trb->field[3] = field4;
	inc_enq(xhci, ring, consumer);
}

/*
 * Does various checks on the endpoint ring, and makes it ready to queue num_trbs.
 * FIXME allocate segments if the ring is full.
 */
static int prepare_ring(struct xhci_hcd *xhci, struct xhci_ring *ep_ring,
		u32 ep_state, unsigned int num_trbs, gfp_t mem_flags)
{
	/* Make sure the endpoint has been added to xHC schedule */
	xhci_dbg(xhci, "Endpoint state = 0x%x\n", ep_state);
	switch (ep_state) {
	case EP_STATE_DISABLED:
		/*
		 * USB core changed config/interfaces without notifying us,
		 * or hardware is reporting the wrong state.
		 */
		xhci_warn(xhci, "WARN urb submitted to disabled ep\n");
		return -ENOENT;
	case EP_STATE_HALTED:
	case EP_STATE_ERROR:
		xhci_warn(xhci, "WARN waiting for halt or error on ep "
				"to be cleared\n");
		/* FIXME event handling code for error needs to clear it */
		/* XXX not sure if this should be -ENOENT or not */
		return -EINVAL;
	case EP_STATE_STOPPED:
	case EP_STATE_RUNNING:
		break;
	default:
		xhci_err(xhci, "ERROR unknown endpoint state for ep\n");
		/*
		 * FIXME issue Configure Endpoint command to try to get the HC
		 * back into a known state.
		 */
		return -EINVAL;
	}
	if (!room_on_ring(xhci, ep_ring, num_trbs)) {
		/* FIXME allocate more room */
		xhci_err(xhci, "ERROR no room on ep ring\n");
		return -ENOMEM;
	}
	return 0;
}

int xhci_prepare_transfer(struct xhci_hcd *xhci,
		struct xhci_virt_device *xdev,
		unsigned int ep_index,
		unsigned int num_trbs,
		struct urb *urb,
		struct xhci_td **td,
		gfp_t mem_flags)
{
	int ret;

	ret = prepare_ring(xhci, xdev->ep_rings[ep_index],
			xdev->out_ctx->ep[ep_index].ep_info & EP_STATE_MASK,
			num_trbs, mem_flags);
	if (ret)
		return ret;
	*td = kzalloc(sizeof(struct xhci_td), mem_flags);
	if (!*td)
		return -ENOMEM;
	INIT_LIST_HEAD(&(*td)->td_list);

	ret = usb_hcd_link_urb_to_ep(xhci_to_hcd(xhci), urb);
	if (unlikely(ret)) {
		kfree(*td);
		return ret;
	}

	(*td)->urb = urb;
	urb->hcpriv = (void *) (*td);
	/* Add this TD to the tail of the endpoint ring's TD list */
	list_add_tail(&(*td)->td_list, &xdev->ep_rings[ep_index]->td_list);

	return 0;
}

/* This is very similar to what ehci-q.c qtd_fill() does */
int queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ring *ep_ring;
	struct xhci_td *td;
	int num_trbs;
	struct xhci_generic_trb *start_trb;
	bool first_trb;
	int start_cycle;
	u32 field;

	int running_total, trb_buff_len, ret;
	u64 addr;

	ep_ring = xhci->devs[slot_id]->ep_rings[ep_index];

	num_trbs = 0;
	/* How much data is (potentially) left before the 64KB boundary? */
	running_total = TRB_MAX_BUFF_SIZE -
		(urb->transfer_dma & ((1 << TRB_MAX_BUFF_SHIFT) - 1));

	/* If there's some data on this 64KB chunk, or we have to send a
	 * zero-length transfer, we need at least one TRB
	 */
	if (running_total != 0 || urb->transfer_buffer_length == 0)
		num_trbs++;
	/* How many more 64KB chunks to transfer, how many more TRBs? */
	while (running_total < urb->transfer_buffer_length) {
		num_trbs++;
		running_total += TRB_MAX_BUFF_SIZE;
	}
	/* FIXME: this doesn't deal with URB_ZERO_PACKET - need one more */

	if (!in_interrupt())
		dev_dbg(&urb->dev->dev, "ep %#x - urb len = %d, addr = %#x, num_trbs = %d\n",
				urb->ep->desc.bEndpointAddress,
				urb->transfer_buffer_length, urb->transfer_dma,
				num_trbs);
	ret = xhci_prepare_transfer(xhci, xhci->devs[slot_id], ep_index,
			num_trbs, urb, &td, mem_flags);
	if (ret < 0)
		return ret;

	/*
	 * Don't give the first TRB to the hardware (by toggling the cycle bit)
	 * until we've finished creating all the other TRBs.  The ring's cycle
	 * state may change as we enqueue the other TRBs, so save it too.
	 */
	start_trb = &ep_ring->enqueue->generic;
	start_cycle = ep_ring->cycle_state;

	running_total = 0;
	/* How much data is in the first TRB? */
	addr = (u64) urb->transfer_dma;
	trb_buff_len = TRB_MAX_BUFF_SIZE -
		(urb->transfer_dma & ((1 << TRB_MAX_BUFF_SHIFT) - 1));
	if (urb->transfer_buffer_length < trb_buff_len)
		trb_buff_len = urb->transfer_buffer_length;

	first_trb = true;

	/* Queue the first TRB, even if it's zero-length */
	do {
		field = 0;

		/* Don't change the cycle bit of the first TRB until later */
		if (first_trb)
			first_trb = false;
		else
			field |= ep_ring->cycle_state;

		/* Chain all the TRBs together; clear the chain bit in the last
		 * TRB to indicate it's the last TRB in the chain.
		 */
		if (num_trbs > 1) {
			field |= TRB_CHAIN;
		} else {
			/* FIXME - add check for ZERO_PACKET flag before this */
			td->last_trb = ep_ring->enqueue;
			field |= TRB_IOC;
		}
		queue_trb(xhci, ep_ring, false,
				(u32) addr,
				(u32) ((u64) addr >> 32),
				TRB_LEN(trb_buff_len) | TRB_INTR_TARGET(0),
				/* We always want to know if the TRB was short,
				 * or we won't get an event when it completes.
				 * (Unless we use event data TRBs, which are a
				 * waste of space and HC resources.)
				 */
				field | TRB_ISP | TRB_TYPE(TRB_NORMAL));
		--num_trbs;
		running_total += trb_buff_len;

		/* Calculate length for next transfer */
		addr += trb_buff_len;
		trb_buff_len = urb->transfer_buffer_length - running_total;
		if (trb_buff_len > TRB_MAX_BUFF_SIZE)
			trb_buff_len = TRB_MAX_BUFF_SIZE;
	} while (running_total < urb->transfer_buffer_length);

	if (num_trbs != 0)
		dev_dbg(&urb->dev->dev, "%s - ep %#x - Miscalculated number of "
				"TRBs, %d left\n", __FUNCTION__,
				urb->ep->desc.bEndpointAddress, num_trbs);
	/*
	 * Pass all the TRBs to the hardware at once and make sure this write
	 * isn't reordered.
	 */
	wmb();
	start_trb->field[3] |= start_cycle;
	field = xhci_readl(xhci, &xhci->dba->doorbell[slot_id]) & DB_MASK;
	xhci_writel(xhci, field | EPI_TO_DB(ep_index), &xhci->dba->doorbell[slot_id]);
	/* Flush PCI posted writes */
	xhci_readl(xhci, &xhci->dba->doorbell[slot_id]);

	return 0;
}

/* Caller must have locked xhci->lock */
int queue_ctrl_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ring *ep_ring;
	int num_trbs;
	int ret;
	struct usb_ctrlrequest *setup;
	struct xhci_generic_trb *start_trb;
	int start_cycle;
	u32 field;
	struct xhci_td *td;

	ep_ring = xhci->devs[slot_id]->ep_rings[ep_index];

	/*
	 * Need to copy setup packet into setup TRB, so we can't use the setup
	 * DMA address.
	 */
	if (!urb->setup_packet)
		return -EINVAL;

	if (!in_interrupt())
		xhci_dbg(xhci, "Queueing ctrl tx for slot id %d, ep %d\n",
				slot_id, ep_index);
	/* 1 TRB for setup, 1 for status */
	num_trbs = 2;
	/*
	 * Don't need to check if we need additional event data and normal TRBs,
	 * since data in control transfers will never get bigger than 16MB
	 * XXX: can we get a buffer that crosses 64KB boundaries?
	 */
	if (urb->transfer_buffer_length > 0)
		num_trbs++;
	ret = xhci_prepare_transfer(xhci, xhci->devs[slot_id], ep_index, num_trbs,
			urb, &td, mem_flags);
	if (ret < 0)
		return ret;

	/*
	 * Don't give the first TRB to the hardware (by toggling the cycle bit)
	 * until we've finished creating all the other TRBs.  The ring's cycle
	 * state may change as we enqueue the other TRBs, so save it too.
	 */
	start_trb = &ep_ring->enqueue->generic;
	start_cycle = ep_ring->cycle_state;

	/* Queue setup TRB - see section 6.4.1.2.1 */
	/* FIXME better way to translate setup_packet into two u32 fields? */
	setup = (struct usb_ctrlrequest *) urb->setup_packet;
	queue_trb(xhci, ep_ring, false,
			/* FIXME endianness is probably going to bite my ass here. */
			setup->bRequestType | setup->bRequest << 8 | setup->wValue << 16,
			setup->wIndex | setup->wLength << 16,
			TRB_LEN(8) | TRB_INTR_TARGET(0),
			/* Immediate data in pointer */
			TRB_IDT | TRB_TYPE(TRB_SETUP));

	/* If there's data, queue data TRBs */
	field = 0;
	if (urb->transfer_buffer_length > 0) {
		if (setup->bRequestType & USB_DIR_IN)
			field |= TRB_DIR_IN;
		queue_trb(xhci, ep_ring, false,
				lower_32_bits(urb->transfer_dma),
				upper_32_bits(urb->transfer_dma),
				TRB_LEN(urb->transfer_buffer_length) | TRB_INTR_TARGET(0),
				/* Event on short tx */
				field | TRB_ISP | TRB_TYPE(TRB_DATA) | ep_ring->cycle_state);
	}

	/* Save the DMA address of the last TRB in the TD */
	td->last_trb = ep_ring->enqueue;

	/* Queue status TRB - see Table 7 and sections 4.11.2.2 and 6.4.1.2.3 */
	/* If the device sent data, the status stage is an OUT transfer */
	if (urb->transfer_buffer_length > 0 && setup->bRequestType & USB_DIR_IN)
		field = 0;
	else
		field = TRB_DIR_IN;
	queue_trb(xhci, ep_ring, false,
			0,
			0,
			TRB_INTR_TARGET(0),
			/* Event on completion */
			field | TRB_IOC | TRB_TYPE(TRB_STATUS) | ep_ring->cycle_state);

	/*
	 * Pass all the TRBs to the hardware at once and make sure this write
	 * isn't reordered.
	 */
	wmb();
	start_trb->field[3] |= start_cycle;
	field = xhci_readl(xhci, &xhci->dba->doorbell[slot_id]) & DB_MASK;
	xhci_writel(xhci, field | EPI_TO_DB(ep_index), &xhci->dba->doorbell[slot_id]);
	/* Flush PCI posted writes */
	xhci_readl(xhci, &xhci->dba->doorbell[slot_id]);

	return 0;
}

/****		Command Ring Operations		****/

/* Generic function for queueing a command TRB on the command ring */
static int queue_command(struct xhci_hcd *xhci, u32 field1, u32 field2, u32 field3, u32 field4)
{
	if (!room_on_ring(xhci, xhci->cmd_ring, 1)) {
		if (!in_interrupt())
			xhci_err(xhci, "ERR: No room for command on command ring\n");
		return -ENOMEM;
	}
	queue_trb(xhci, xhci->cmd_ring, false, field1, field2, field3,
			field4 | xhci->cmd_ring->cycle_state);
	return 0;
}

/* Queue a no-op command on the command ring */
static int queue_cmd_noop(struct xhci_hcd *xhci)
{
	return queue_command(xhci, 0, 0, 0, TRB_TYPE(TRB_CMD_NOOP));
}

/*
 * Place a no-op command on the command ring to test the command and
 * event ring.
 */
void *setup_one_noop(struct xhci_hcd *xhci)
{
	if (queue_cmd_noop(xhci) < 0)
		return NULL;
	xhci->noops_submitted++;
	return ring_cmd_db;
}

/* Queue a slot enable or disable request on the command ring */
int queue_slot_control(struct xhci_hcd *xhci, u32 trb_type, u32 slot_id)
{
	return queue_command(xhci, 0, 0, 0,
			TRB_TYPE(trb_type) | SLOT_ID_FOR_TRB(slot_id));
}

/* Queue an address device command TRB */
int queue_address_device(struct xhci_hcd *xhci, dma_addr_t in_ctx_ptr, u32 slot_id)
{
	return queue_command(xhci, in_ctx_ptr, 0, 0,
			TRB_TYPE(TRB_ADDR_DEV) | SLOT_ID_FOR_TRB(slot_id));
}

/* Queue a configure endpoint command TRB */
int queue_configure_endpoint(struct xhci_hcd *xhci, dma_addr_t in_ctx_ptr, u32 slot_id)
{
	return queue_command(xhci, in_ctx_ptr, 0, 0,
			TRB_TYPE(TRB_CONFIG_EP) | SLOT_ID_FOR_TRB(slot_id));
}
