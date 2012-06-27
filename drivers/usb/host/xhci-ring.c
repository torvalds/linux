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

#include <linux/scatterlist.h>
#include <linux/slab.h>
#include "xhci.h"

static int handle_cmd_in_cmd_wait_list(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct xhci_event_cmd *event);

/*
 * Returns zero if the TRB isn't in this segment, otherwise it returns the DMA
 * address of the TRB.
 */
dma_addr_t xhci_trb_virt_to_dma(struct xhci_segment *seg,
		union xhci_trb *trb)
{
	unsigned long segment_offset;

	if (!seg || !trb || trb < seg->trbs)
		return 0;
	/* offset in TRBs */
	segment_offset = trb - seg->trbs;
	if (segment_offset > TRBS_PER_SEGMENT)
		return 0;
	return seg->dma + (segment_offset * sizeof(*trb));
}

/* Does this link TRB point to the first segment in a ring,
 * or was the previous TRB the last TRB on the last segment in the ERST?
 */
static bool last_trb_on_last_seg(struct xhci_hcd *xhci, struct xhci_ring *ring,
		struct xhci_segment *seg, union xhci_trb *trb)
{
	if (ring == xhci->event_ring)
		return (trb == &seg->trbs[TRBS_PER_SEGMENT]) &&
			(seg->next == xhci->event_ring->first_seg);
	else
		return le32_to_cpu(trb->link.control) & LINK_TOGGLE;
}

/* Is this TRB a link TRB or was the last TRB the last TRB in this event ring
 * segment?  I.e. would the updated event TRB pointer step off the end of the
 * event seg?
 */
static int last_trb(struct xhci_hcd *xhci, struct xhci_ring *ring,
		struct xhci_segment *seg, union xhci_trb *trb)
{
	if (ring == xhci->event_ring)
		return trb == &seg->trbs[TRBS_PER_SEGMENT];
	else
		return (le32_to_cpu(trb->link.control) & TRB_TYPE_BITMASK)
			== TRB_TYPE(TRB_LINK);
}

static int enqueue_is_link_trb(struct xhci_ring *ring)
{
	struct xhci_link_trb *link = &ring->enqueue->link;
	return ((le32_to_cpu(link->control) & TRB_TYPE_BITMASK) ==
		TRB_TYPE(TRB_LINK));
}

/* Updates trb to point to the next TRB in the ring, and updates seg if the next
 * TRB is in a new segment.  This does not skip over link TRBs, and it does not
 * effect the ring dequeue or enqueue pointers.
 */
static void next_trb(struct xhci_hcd *xhci,
		struct xhci_ring *ring,
		struct xhci_segment **seg,
		union xhci_trb **trb)
{
	if (last_trb(xhci, ring, *seg, *trb)) {
		*seg = (*seg)->next;
		*trb = ((*seg)->trbs);
	} else {
		(*trb)++;
	}
}

/*
 * See Cycle bit rules. SW is the consumer for the event ring only.
 * Don't make a ring full of link TRBs.  That would be dumb and this would loop.
 */
static void inc_deq(struct xhci_hcd *xhci, struct xhci_ring *ring, bool consumer)
{
	unsigned long long addr;

	ring->deq_updates++;

	do {
		/*
		 * Update the dequeue pointer further if that was a link TRB or
		 * we're at the end of an event ring segment (which doesn't have
		 * link TRBS)
		 */
		if (last_trb(xhci, ring, ring->deq_seg, ring->dequeue)) {
			if (consumer && last_trb_on_last_seg(xhci, ring,
						ring->deq_seg, ring->dequeue)) {
				if (!in_interrupt())
					xhci_dbg(xhci, "Toggle cycle state "
							"for ring %p = %i\n",
							ring,
							(unsigned int)
							ring->cycle_state);
				ring->cycle_state = (ring->cycle_state ? 0 : 1);
			}
			ring->deq_seg = ring->deq_seg->next;
			ring->dequeue = ring->deq_seg->trbs;
		} else {
			ring->dequeue++;
		}
	} while (last_trb(xhci, ring, ring->deq_seg, ring->dequeue));

	addr = (unsigned long long) xhci_trb_virt_to_dma(ring->deq_seg, ring->dequeue);
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
 * set, but other sections talk about dealing with the chain bit set.  This was
 * fixed in the 0.96 specification errata, but we have to assume that all 0.95
 * xHCI hardware can't handle the chain bit being cleared on a link TRB.
 *
 * @more_trbs_coming:	Will you enqueue more TRBs before calling
 *			prepare_transfer()?
 */
static void inc_enq(struct xhci_hcd *xhci, struct xhci_ring *ring,
		bool consumer, bool more_trbs_coming, bool isoc)
{
	u32 chain;
	union xhci_trb *next;
	unsigned long long addr;

	chain = le32_to_cpu(ring->enqueue->generic.field[3]) & TRB_CHAIN;
	next = ++(ring->enqueue);

	ring->enq_updates++;
	/* Update the dequeue pointer further if that was a link TRB or we're at
	 * the end of an event ring segment (which doesn't have link TRBS)
	 */
	while (last_trb(xhci, ring, ring->enq_seg, next)) {
		if (!consumer) {
			if (ring != xhci->event_ring) {
				/*
				 * If the caller doesn't plan on enqueueing more
				 * TDs before ringing the doorbell, then we
				 * don't want to give the link TRB to the
				 * hardware just yet.  We'll give the link TRB
				 * back in prepare_ring() just before we enqueue
				 * the TD at the top of the ring.
				 */
				if (!chain && !more_trbs_coming)
					break;

				/* If we're not dealing with 0.95 hardware or
				 * isoc rings on AMD 0.96 host,
				 * carry over the chain bit of the previous TRB
				 * (which may mean the chain bit is cleared).
				 */
				if (!(isoc && (xhci->quirks & XHCI_AMD_0x96_HOST))
						&& !xhci_link_trb_quirk(xhci)) {
					next->link.control &=
						cpu_to_le32(~TRB_CHAIN);
					next->link.control |=
						cpu_to_le32(chain);
				}
				/* Give this link TRB to the hardware */
				wmb();
				next->link.control ^= cpu_to_le32(TRB_CYCLE);
			}
			/* Toggle the cycle bit after the last ring segment. */
			if (last_trb_on_last_seg(xhci, ring, ring->enq_seg, next)) {
				ring->cycle_state = (ring->cycle_state ? 0 : 1);
				if (!in_interrupt())
					xhci_dbg(xhci, "Toggle cycle state for ring %p = %i\n",
							ring,
							(unsigned int) ring->cycle_state);
			}
		}
		ring->enq_seg = ring->enq_seg->next;
		ring->enqueue = ring->enq_seg->trbs;
		next = ring->enqueue;
	}
	addr = (unsigned long long) xhci_trb_virt_to_dma(ring->enq_seg, ring->enqueue);
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
	struct xhci_segment *cur_seg;
	unsigned int left_on_ring;

	/* If we are currently pointing to a link TRB, advance the
	 * enqueue pointer before checking for space */
	while (last_trb(xhci, ring, enq_seg, enq)) {
		enq_seg = enq_seg->next;
		enq = enq_seg->trbs;
	}

	/* Check if ring is empty */
	if (enq == ring->dequeue) {
		/* Can't use link trbs */
		left_on_ring = TRBS_PER_SEGMENT - 1;
		for (cur_seg = enq_seg->next; cur_seg != enq_seg;
				cur_seg = cur_seg->next)
			left_on_ring += TRBS_PER_SEGMENT - 1;

		/* Always need one TRB free in the ring. */
		left_on_ring -= 1;
		if (num_trbs > left_on_ring) {
			xhci_warn(xhci, "Not enough room on ring; "
					"need %u TRBs, %u TRBs left\n",
					num_trbs, left_on_ring);
			return 0;
		}
		return 1;
	}
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

/* Ring the host controller doorbell after placing a command on the ring */
void xhci_ring_cmd_db(struct xhci_hcd *xhci)
{
	if (!(xhci->cmd_ring_state & CMD_RING_STATE_RUNNING))
		return;

	xhci_dbg(xhci, "// Ding dong!\n");
	xhci_writel(xhci, DB_VALUE_HOST, &xhci->dba->doorbell[0]);
	/* Flush PCI posted writes */
	xhci_readl(xhci, &xhci->dba->doorbell[0]);
}

static int xhci_abort_cmd_ring(struct xhci_hcd *xhci)
{
	u64 temp_64;
	int ret;

	xhci_dbg(xhci, "Abort command ring\n");

	if (!(xhci->cmd_ring_state & CMD_RING_STATE_RUNNING)) {
		xhci_dbg(xhci, "The command ring isn't running, "
				"Have the command ring been stopped?\n");
		return 0;
	}

	temp_64 = xhci_read_64(xhci, &xhci->op_regs->cmd_ring);
	if (!(temp_64 & CMD_RING_RUNNING)) {
		xhci_dbg(xhci, "Command ring had been stopped\n");
		return 0;
	}
	xhci->cmd_ring_state = CMD_RING_STATE_ABORTED;
	xhci_write_64(xhci, temp_64 | CMD_RING_ABORT,
			&xhci->op_regs->cmd_ring);

	/* Section 4.6.1.2 of xHCI 1.0 spec says software should
	 * time the completion od all xHCI commands, including
	 * the Command Abort operation. If software doesn't see
	 * CRR negated in a timely manner (e.g. longer than 5
	 * seconds), then it should assume that the there are
	 * larger problems with the xHC and assert HCRST.
	 */
	ret = handshake(xhci, &xhci->op_regs->cmd_ring,
			CMD_RING_RUNNING, 0, 5 * 1000 * 1000);
	if (ret < 0) {
		xhci_err(xhci, "Stopped the command ring failed, "
				"maybe the host is dead\n");
		xhci->xhc_state |= XHCI_STATE_DYING;
		xhci_quiesce(xhci);
		xhci_halt(xhci);
		return -ESHUTDOWN;
	}

	return 0;
}

static int xhci_queue_cd(struct xhci_hcd *xhci,
		struct xhci_command *command,
		union xhci_trb *cmd_trb)
{
	struct xhci_cd *cd;
	cd = kzalloc(sizeof(struct xhci_cd), GFP_ATOMIC);
	if (!cd)
		return -ENOMEM;
	INIT_LIST_HEAD(&cd->cancel_cmd_list);

	cd->command = command;
	cd->cmd_trb = cmd_trb;
	list_add_tail(&cd->cancel_cmd_list, &xhci->cancel_cmd_list);

	return 0;
}

/*
 * Cancel the command which has issue.
 *
 * Some commands may hang due to waiting for acknowledgement from
 * usb device. It is outside of the xHC's ability to control and
 * will cause the command ring is blocked. When it occurs software
 * should intervene to recover the command ring.
 * See Section 4.6.1.1 and 4.6.1.2
 */
int xhci_cancel_cmd(struct xhci_hcd *xhci, struct xhci_command *command,
		union xhci_trb *cmd_trb)
{
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&xhci->lock, flags);

	if (xhci->xhc_state & XHCI_STATE_DYING) {
		xhci_warn(xhci, "Abort the command ring,"
				" but the xHCI is dead.\n");
		retval = -ESHUTDOWN;
		goto fail;
	}

	/* queue the cmd desriptor to cancel_cmd_list */
	retval = xhci_queue_cd(xhci, command, cmd_trb);
	if (retval) {
		xhci_warn(xhci, "Queuing command descriptor failed.\n");
		goto fail;
	}

	/* abort command ring */
	retval = xhci_abort_cmd_ring(xhci);
	if (retval) {
		xhci_err(xhci, "Abort command ring failed\n");
		if (unlikely(retval == -ESHUTDOWN)) {
			spin_unlock_irqrestore(&xhci->lock, flags);
			usb_hc_died(xhci_to_hcd(xhci)->primary_hcd);
			xhci_dbg(xhci, "xHCI host controller is dead.\n");
			return retval;
		}
	}

fail:
	spin_unlock_irqrestore(&xhci->lock, flags);
	return retval;
}

void xhci_ring_ep_doorbell(struct xhci_hcd *xhci,
		unsigned int slot_id,
		unsigned int ep_index,
		unsigned int stream_id)
{
	__le32 __iomem *db_addr = &xhci->dba->doorbell[slot_id];
	struct xhci_virt_ep *ep = &xhci->devs[slot_id]->eps[ep_index];
	unsigned int ep_state = ep->ep_state;

	/* Don't ring the doorbell for this endpoint if there are pending
	 * cancellations because we don't want to interrupt processing.
	 * We don't want to restart any stream rings if there's a set dequeue
	 * pointer command pending because the device can choose to start any
	 * stream once the endpoint is on the HW schedule.
	 * FIXME - check all the stream rings for pending cancellations.
	 */
	if ((ep_state & EP_HALT_PENDING) || (ep_state & SET_DEQ_PENDING) ||
	    (ep_state & EP_HALTED))
		return;
	xhci_writel(xhci, DB_VALUE(ep_index, stream_id), db_addr);
	/* The CPU has better things to do at this point than wait for a
	 * write-posting flush.  It'll get there soon enough.
	 */
}

/* Ring the doorbell for any rings with pending URBs */
static void ring_doorbell_for_active_rings(struct xhci_hcd *xhci,
		unsigned int slot_id,
		unsigned int ep_index)
{
	unsigned int stream_id;
	struct xhci_virt_ep *ep;

	ep = &xhci->devs[slot_id]->eps[ep_index];

	/* A ring has pending URBs if its TD list is not empty */
	if (!(ep->ep_state & EP_HAS_STREAMS)) {
		if (!(list_empty(&ep->ring->td_list)))
			xhci_ring_ep_doorbell(xhci, slot_id, ep_index, 0);
		return;
	}

	for (stream_id = 1; stream_id < ep->stream_info->num_streams;
			stream_id++) {
		struct xhci_stream_info *stream_info = ep->stream_info;
		if (!list_empty(&stream_info->stream_rings[stream_id]->td_list))
			xhci_ring_ep_doorbell(xhci, slot_id, ep_index,
						stream_id);
	}
}

/*
 * Find the segment that trb is in.  Start searching in start_seg.
 * If we must move past a segment that has a link TRB with a toggle cycle state
 * bit set, then we will toggle the value pointed at by cycle_state.
 */
static struct xhci_segment *find_trb_seg(
		struct xhci_segment *start_seg,
		union xhci_trb	*trb, int *cycle_state)
{
	struct xhci_segment *cur_seg = start_seg;
	struct xhci_generic_trb *generic_trb;

	while (cur_seg->trbs > trb ||
			&cur_seg->trbs[TRBS_PER_SEGMENT - 1] < trb) {
		generic_trb = &cur_seg->trbs[TRBS_PER_SEGMENT - 1].generic;
		if (le32_to_cpu(generic_trb->field[3]) & LINK_TOGGLE)
			*cycle_state ^= 0x1;
		cur_seg = cur_seg->next;
		if (cur_seg == start_seg)
			/* Looped over the entire list.  Oops! */
			return NULL;
	}
	return cur_seg;
}


static struct xhci_ring *xhci_triad_to_transfer_ring(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		unsigned int stream_id)
{
	struct xhci_virt_ep *ep;

	ep = &xhci->devs[slot_id]->eps[ep_index];
	/* Common case: no streams */
	if (!(ep->ep_state & EP_HAS_STREAMS))
		return ep->ring;

	if (stream_id == 0) {
		xhci_warn(xhci,
				"WARN: Slot ID %u, ep index %u has streams, "
				"but URB has no stream ID.\n",
				slot_id, ep_index);
		return NULL;
	}

	if (stream_id < ep->stream_info->num_streams)
		return ep->stream_info->stream_rings[stream_id];

	xhci_warn(xhci,
			"WARN: Slot ID %u, ep index %u has "
			"stream IDs 1 to %u allocated, "
			"but stream ID %u is requested.\n",
			slot_id, ep_index,
			ep->stream_info->num_streams - 1,
			stream_id);
	return NULL;
}

/* Get the right ring for the given URB.
 * If the endpoint supports streams, boundary check the URB's stream ID.
 * If the endpoint doesn't support streams, return the singular endpoint ring.
 */
static struct xhci_ring *xhci_urb_to_transfer_ring(struct xhci_hcd *xhci,
		struct urb *urb)
{
	return xhci_triad_to_transfer_ring(xhci, urb->dev->slot_id,
		xhci_get_endpoint_index(&urb->ep->desc), urb->stream_id);
}

/*
 * Move the xHC's endpoint ring dequeue pointer past cur_td.
 * Record the new state of the xHC's endpoint ring dequeue segment,
 * dequeue pointer, and new consumer cycle state in state.
 * Update our internal representation of the ring's dequeue pointer.
 *
 * We do this in three jumps:
 *  - First we update our new ring state to be the same as when the xHC stopped.
 *  - Then we traverse the ring to find the segment that contains
 *    the last TRB in the TD.  We toggle the xHC's new cycle state when we pass
 *    any link TRBs with the toggle cycle bit set.
 *  - Finally we move the dequeue state one TRB further, toggling the cycle bit
 *    if we've moved it past a link TRB with the toggle cycle bit set.
 *
 * Some of the uses of xhci_generic_trb are grotty, but if they're done
 * with correct __le32 accesses they should work fine.  Only users of this are
 * in here.
 */
void xhci_find_new_dequeue_state(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		unsigned int stream_id, struct xhci_td *cur_td,
		struct xhci_dequeue_state *state)
{
	struct xhci_virt_device *dev = xhci->devs[slot_id];
	struct xhci_ring *ep_ring;
	struct xhci_generic_trb *trb;
	struct xhci_ep_ctx *ep_ctx;
	dma_addr_t addr;

	ep_ring = xhci_triad_to_transfer_ring(xhci, slot_id,
			ep_index, stream_id);
	if (!ep_ring) {
		xhci_warn(xhci, "WARN can't find new dequeue state "
				"for invalid stream ID %u.\n",
				stream_id);
		return;
	}
	state->new_cycle_state = 0;
	xhci_dbg(xhci, "Finding segment containing stopped TRB.\n");
	state->new_deq_seg = find_trb_seg(cur_td->start_seg,
			dev->eps[ep_index].stopped_trb,
			&state->new_cycle_state);
	if (!state->new_deq_seg) {
		WARN_ON(1);
		return;
	}

	/* Dig out the cycle state saved by the xHC during the stop ep cmd */
	xhci_dbg(xhci, "Finding endpoint context\n");
	ep_ctx = xhci_get_ep_ctx(xhci, dev->out_ctx, ep_index);
	state->new_cycle_state = 0x1 & le64_to_cpu(ep_ctx->deq);

	state->new_deq_ptr = cur_td->last_trb;
	xhci_dbg(xhci, "Finding segment containing last TRB in TD.\n");
	state->new_deq_seg = find_trb_seg(state->new_deq_seg,
			state->new_deq_ptr,
			&state->new_cycle_state);
	if (!state->new_deq_seg) {
		WARN_ON(1);
		return;
	}

	trb = &state->new_deq_ptr->generic;
	if ((le32_to_cpu(trb->field[3]) & TRB_TYPE_BITMASK) ==
	    TRB_TYPE(TRB_LINK) && (le32_to_cpu(trb->field[3]) & LINK_TOGGLE))
		state->new_cycle_state ^= 0x1;
	next_trb(xhci, ep_ring, &state->new_deq_seg, &state->new_deq_ptr);

	/*
	 * If there is only one segment in a ring, find_trb_seg()'s while loop
	 * will not run, and it will return before it has a chance to see if it
	 * needs to toggle the cycle bit.  It can't tell if the stalled transfer
	 * ended just before the link TRB on a one-segment ring, or if the TD
	 * wrapped around the top of the ring, because it doesn't have the TD in
	 * question.  Look for the one-segment case where stalled TRB's address
	 * is greater than the new dequeue pointer address.
	 */
	if (ep_ring->first_seg == ep_ring->first_seg->next &&
			state->new_deq_ptr < dev->eps[ep_index].stopped_trb)
		state->new_cycle_state ^= 0x1;
	xhci_dbg(xhci, "Cycle state = 0x%x\n", state->new_cycle_state);

	/* Don't update the ring cycle state for the producer (us). */
	xhci_dbg(xhci, "New dequeue segment = %p (virtual)\n",
			state->new_deq_seg);
	addr = xhci_trb_virt_to_dma(state->new_deq_seg, state->new_deq_ptr);
	xhci_dbg(xhci, "New dequeue pointer = 0x%llx (DMA)\n",
			(unsigned long long) addr);
}

/* flip_cycle means flip the cycle bit of all but the first and last TRB.
 * (The last TRB actually points to the ring enqueue pointer, which is not part
 * of this TD.)  This is used to remove partially enqueued isoc TDs from a ring.
 */
static void td_to_noop(struct xhci_hcd *xhci, struct xhci_ring *ep_ring,
		struct xhci_td *cur_td, bool flip_cycle)
{
	struct xhci_segment *cur_seg;
	union xhci_trb *cur_trb;

	for (cur_seg = cur_td->start_seg, cur_trb = cur_td->first_trb;
			true;
			next_trb(xhci, ep_ring, &cur_seg, &cur_trb)) {
		if ((le32_to_cpu(cur_trb->generic.field[3]) & TRB_TYPE_BITMASK)
		    == TRB_TYPE(TRB_LINK)) {
			/* Unchain any chained Link TRBs, but
			 * leave the pointers intact.
			 */
			cur_trb->generic.field[3] &= cpu_to_le32(~TRB_CHAIN);
			/* Flip the cycle bit (link TRBs can't be the first
			 * or last TRB).
			 */
			if (flip_cycle)
				cur_trb->generic.field[3] ^=
					cpu_to_le32(TRB_CYCLE);
			xhci_dbg(xhci, "Cancel (unchain) link TRB\n");
			xhci_dbg(xhci, "Address = %p (0x%llx dma); "
					"in seg %p (0x%llx dma)\n",
					cur_trb,
					(unsigned long long)xhci_trb_virt_to_dma(cur_seg, cur_trb),
					cur_seg,
					(unsigned long long)cur_seg->dma);
		} else {
			cur_trb->generic.field[0] = 0;
			cur_trb->generic.field[1] = 0;
			cur_trb->generic.field[2] = 0;
			/* Preserve only the cycle bit of this TRB */
			cur_trb->generic.field[3] &= cpu_to_le32(TRB_CYCLE);
			/* Flip the cycle bit except on the first or last TRB */
			if (flip_cycle && cur_trb != cur_td->first_trb &&
					cur_trb != cur_td->last_trb)
				cur_trb->generic.field[3] ^=
					cpu_to_le32(TRB_CYCLE);
			cur_trb->generic.field[3] |= cpu_to_le32(
				TRB_TYPE(TRB_TR_NOOP));
			xhci_dbg(xhci, "Cancel TRB %p (0x%llx dma) "
					"in seg %p (0x%llx dma)\n",
					cur_trb,
					(unsigned long long)xhci_trb_virt_to_dma(cur_seg, cur_trb),
					cur_seg,
					(unsigned long long)cur_seg->dma);
		}
		if (cur_trb == cur_td->last_trb)
			break;
	}
}

static int queue_set_tr_deq(struct xhci_hcd *xhci, int slot_id,
		unsigned int ep_index, unsigned int stream_id,
		struct xhci_segment *deq_seg,
		union xhci_trb *deq_ptr, u32 cycle_state);

void xhci_queue_new_dequeue_state(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		unsigned int stream_id,
		struct xhci_dequeue_state *deq_state)
{
	struct xhci_virt_ep *ep = &xhci->devs[slot_id]->eps[ep_index];

	xhci_dbg(xhci, "Set TR Deq Ptr cmd, new deq seg = %p (0x%llx dma), "
			"new deq ptr = %p (0x%llx dma), new cycle = %u\n",
			deq_state->new_deq_seg,
			(unsigned long long)deq_state->new_deq_seg->dma,
			deq_state->new_deq_ptr,
			(unsigned long long)xhci_trb_virt_to_dma(deq_state->new_deq_seg, deq_state->new_deq_ptr),
			deq_state->new_cycle_state);
	queue_set_tr_deq(xhci, slot_id, ep_index, stream_id,
			deq_state->new_deq_seg,
			deq_state->new_deq_ptr,
			(u32) deq_state->new_cycle_state);
	/* Stop the TD queueing code from ringing the doorbell until
	 * this command completes.  The HC won't set the dequeue pointer
	 * if the ring is running, and ringing the doorbell starts the
	 * ring running.
	 */
	ep->ep_state |= SET_DEQ_PENDING;
}

static void xhci_stop_watchdog_timer_in_irq(struct xhci_hcd *xhci,
		struct xhci_virt_ep *ep)
{
	ep->ep_state &= ~EP_HALT_PENDING;
	/* Can't del_timer_sync in interrupt, so we attempt to cancel.  If the
	 * timer is running on another CPU, we don't decrement stop_cmds_pending
	 * (since we didn't successfully stop the watchdog timer).
	 */
	if (del_timer(&ep->stop_cmd_timer))
		ep->stop_cmds_pending--;
}

/* Must be called with xhci->lock held in interrupt context */
static void xhci_giveback_urb_in_irq(struct xhci_hcd *xhci,
		struct xhci_td *cur_td, int status, char *adjective)
{
	struct usb_hcd *hcd;
	struct urb	*urb;
	struct urb_priv	*urb_priv;

	urb = cur_td->urb;
	urb_priv = urb->hcpriv;
	urb_priv->td_cnt++;
	hcd = bus_to_hcd(urb->dev->bus);

	/* Only giveback urb when this is the last td in urb */
	if (urb_priv->td_cnt == urb_priv->length) {
		if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
			xhci_to_hcd(xhci)->self.bandwidth_isoc_reqs--;
			if (xhci_to_hcd(xhci)->self.bandwidth_isoc_reqs	== 0) {
				if (xhci->quirks & XHCI_AMD_PLL_FIX)
					usb_amd_quirk_pll_enable();
			}
		}
		usb_hcd_unlink_urb_from_ep(hcd, urb);

		spin_unlock(&xhci->lock);
		usb_hcd_giveback_urb(hcd, urb, status);
		xhci_urb_free_priv(xhci, urb_priv);
		spin_lock(&xhci->lock);
	}
}

/*
 * When we get a command completion for a Stop Endpoint Command, we need to
 * unlink any cancelled TDs from the ring.  There are two ways to do that:
 *
 *  1. If the HW was in the middle of processing the TD that needs to be
 *     cancelled, then we must move the ring's dequeue pointer past the last TRB
 *     in the TD with a Set Dequeue Pointer Command.
 *  2. Otherwise, we turn all the TRBs in the TD into No-op TRBs (with the chain
 *     bit cleared) so that the HW will skip over them.
 */
static void handle_stopped_endpoint(struct xhci_hcd *xhci,
		union xhci_trb *trb, struct xhci_event_cmd *event)
{
	unsigned int slot_id;
	unsigned int ep_index;
	struct xhci_virt_device *virt_dev;
	struct xhci_ring *ep_ring;
	struct xhci_virt_ep *ep;
	struct list_head *entry;
	struct xhci_td *cur_td = NULL;
	struct xhci_td *last_unlinked_td;

	struct xhci_dequeue_state deq_state;

	if (unlikely(TRB_TO_SUSPEND_PORT(
			     le32_to_cpu(xhci->cmd_ring->dequeue->generic.field[3])))) {
		slot_id = TRB_TO_SLOT_ID(
			le32_to_cpu(xhci->cmd_ring->dequeue->generic.field[3]));
		virt_dev = xhci->devs[slot_id];
		if (virt_dev)
			handle_cmd_in_cmd_wait_list(xhci, virt_dev,
				event);
		else
			xhci_warn(xhci, "Stop endpoint command "
				"completion for disabled slot %u\n",
				slot_id);
		return;
	}

	memset(&deq_state, 0, sizeof(deq_state));
	slot_id = TRB_TO_SLOT_ID(le32_to_cpu(trb->generic.field[3]));
	ep_index = TRB_TO_EP_INDEX(le32_to_cpu(trb->generic.field[3]));
	ep = &xhci->devs[slot_id]->eps[ep_index];

	if (list_empty(&ep->cancelled_td_list)) {
		xhci_stop_watchdog_timer_in_irq(xhci, ep);
		ep->stopped_td = NULL;
		ep->stopped_trb = NULL;
		ring_doorbell_for_active_rings(xhci, slot_id, ep_index);
		return;
	}

	/* Fix up the ep ring first, so HW stops executing cancelled TDs.
	 * We have the xHCI lock, so nothing can modify this list until we drop
	 * it.  We're also in the event handler, so we can't get re-interrupted
	 * if another Stop Endpoint command completes
	 */
	list_for_each(entry, &ep->cancelled_td_list) {
		cur_td = list_entry(entry, struct xhci_td, cancelled_td_list);
		xhci_dbg(xhci, "Cancelling TD starting at %p, 0x%llx (dma).\n",
				cur_td->first_trb,
				(unsigned long long)xhci_trb_virt_to_dma(cur_td->start_seg, cur_td->first_trb));
		ep_ring = xhci_urb_to_transfer_ring(xhci, cur_td->urb);
		if (!ep_ring) {
			/* This shouldn't happen unless a driver is mucking
			 * with the stream ID after submission.  This will
			 * leave the TD on the hardware ring, and the hardware
			 * will try to execute it, and may access a buffer
			 * that has already been freed.  In the best case, the
			 * hardware will execute it, and the event handler will
			 * ignore the completion event for that TD, since it was
			 * removed from the td_list for that endpoint.  In
			 * short, don't muck with the stream ID after
			 * submission.
			 */
			xhci_warn(xhci, "WARN Cancelled URB %p "
					"has invalid stream ID %u.\n",
					cur_td->urb,
					cur_td->urb->stream_id);
			goto remove_finished_td;
		}
		/*
		 * If we stopped on the TD we need to cancel, then we have to
		 * move the xHC endpoint ring dequeue pointer past this TD.
		 */
		if (cur_td == ep->stopped_td)
			xhci_find_new_dequeue_state(xhci, slot_id, ep_index,
					cur_td->urb->stream_id,
					cur_td, &deq_state);
		else
			td_to_noop(xhci, ep_ring, cur_td, false);
remove_finished_td:
		/*
		 * The event handler won't see a completion for this TD anymore,
		 * so remove it from the endpoint ring's TD list.  Keep it in
		 * the cancelled TD list for URB completion later.
		 */
		list_del_init(&cur_td->td_list);
	}
	last_unlinked_td = cur_td;
	xhci_stop_watchdog_timer_in_irq(xhci, ep);

	/* If necessary, queue a Set Transfer Ring Dequeue Pointer command */
	if (deq_state.new_deq_ptr && deq_state.new_deq_seg) {
		xhci_queue_new_dequeue_state(xhci,
				slot_id, ep_index,
				ep->stopped_td->urb->stream_id,
				&deq_state);
		xhci_ring_cmd_db(xhci);
	} else {
		/* Otherwise ring the doorbell(s) to restart queued transfers */
		ring_doorbell_for_active_rings(xhci, slot_id, ep_index);
	}
	ep->stopped_td = NULL;
	ep->stopped_trb = NULL;

	/*
	 * Drop the lock and complete the URBs in the cancelled TD list.
	 * New TDs to be cancelled might be added to the end of the list before
	 * we can complete all the URBs for the TDs we already unlinked.
	 * So stop when we've completed the URB for the last TD we unlinked.
	 */
	do {
		cur_td = list_entry(ep->cancelled_td_list.next,
				struct xhci_td, cancelled_td_list);
		list_del_init(&cur_td->cancelled_td_list);

		/* Clean up the cancelled URB */
		/* Doesn't matter what we pass for status, since the core will
		 * just overwrite it (because the URB has been unlinked).
		 */
		xhci_giveback_urb_in_irq(xhci, cur_td, 0, "cancelled");

		/* Stop processing the cancelled list if the watchdog timer is
		 * running.
		 */
		if (xhci->xhc_state & XHCI_STATE_DYING)
			return;
	} while (cur_td != last_unlinked_td);

	/* Return to the event handler with xhci->lock re-acquired */
}

/* Watchdog timer function for when a stop endpoint command fails to complete.
 * In this case, we assume the host controller is broken or dying or dead.  The
 * host may still be completing some other events, so we have to be careful to
 * let the event ring handler and the URB dequeueing/enqueueing functions know
 * through xhci->state.
 *
 * The timer may also fire if the host takes a very long time to respond to the
 * command, and the stop endpoint command completion handler cannot delete the
 * timer before the timer function is called.  Another endpoint cancellation may
 * sneak in before the timer function can grab the lock, and that may queue
 * another stop endpoint command and add the timer back.  So we cannot use a
 * simple flag to say whether there is a pending stop endpoint command for a
 * particular endpoint.
 *
 * Instead we use a combination of that flag and a counter for the number of
 * pending stop endpoint commands.  If the timer is the tail end of the last
 * stop endpoint command, and the endpoint's command is still pending, we assume
 * the host is dying.
 */
void xhci_stop_endpoint_command_watchdog(unsigned long arg)
{
	struct xhci_hcd *xhci;
	struct xhci_virt_ep *ep;
	struct xhci_virt_ep *temp_ep;
	struct xhci_ring *ring;
	struct xhci_td *cur_td;
	int ret, i, j;
	unsigned long flags;

	ep = (struct xhci_virt_ep *) arg;
	xhci = ep->xhci;

	spin_lock_irqsave(&xhci->lock, flags);

	ep->stop_cmds_pending--;
	if (xhci->xhc_state & XHCI_STATE_DYING) {
		xhci_dbg(xhci, "Stop EP timer ran, but another timer marked "
				"xHCI as DYING, exiting.\n");
		spin_unlock_irqrestore(&xhci->lock, flags);
		return;
	}
	if (!(ep->stop_cmds_pending == 0 && (ep->ep_state & EP_HALT_PENDING))) {
		xhci_dbg(xhci, "Stop EP timer ran, but no command pending, "
				"exiting.\n");
		spin_unlock_irqrestore(&xhci->lock, flags);
		return;
	}

	xhci_warn(xhci, "xHCI host not responding to stop endpoint command.\n");
	xhci_warn(xhci, "Assuming host is dying, halting host.\n");
	/* Oops, HC is dead or dying or at least not responding to the stop
	 * endpoint command.
	 */
	xhci->xhc_state |= XHCI_STATE_DYING;
	/* Disable interrupts from the host controller and start halting it */
	xhci_quiesce(xhci);
	spin_unlock_irqrestore(&xhci->lock, flags);

	ret = xhci_halt(xhci);

	spin_lock_irqsave(&xhci->lock, flags);
	if (ret < 0) {
		/* This is bad; the host is not responding to commands and it's
		 * not allowing itself to be halted.  At least interrupts are
		 * disabled. If we call usb_hc_died(), it will attempt to
		 * disconnect all device drivers under this host.  Those
		 * disconnect() methods will wait for all URBs to be unlinked,
		 * so we must complete them.
		 */
		xhci_warn(xhci, "Non-responsive xHCI host is not halting.\n");
		xhci_warn(xhci, "Completing active URBs anyway.\n");
		/* We could turn all TDs on the rings to no-ops.  This won't
		 * help if the host has cached part of the ring, and is slow if
		 * we want to preserve the cycle bit.  Skip it and hope the host
		 * doesn't touch the memory.
		 */
	}
	for (i = 0; i < MAX_HC_SLOTS; i++) {
		if (!xhci->devs[i])
			continue;
		for (j = 0; j < 31; j++) {
			temp_ep = &xhci->devs[i]->eps[j];
			ring = temp_ep->ring;
			if (!ring)
				continue;
			xhci_dbg(xhci, "Killing URBs for slot ID %u, "
					"ep index %u\n", i, j);
			while (!list_empty(&ring->td_list)) {
				cur_td = list_first_entry(&ring->td_list,
						struct xhci_td,
						td_list);
				list_del_init(&cur_td->td_list);
				if (!list_empty(&cur_td->cancelled_td_list))
					list_del_init(&cur_td->cancelled_td_list);
				xhci_giveback_urb_in_irq(xhci, cur_td,
						-ESHUTDOWN, "killed");
			}
			while (!list_empty(&temp_ep->cancelled_td_list)) {
				cur_td = list_first_entry(
						&temp_ep->cancelled_td_list,
						struct xhci_td,
						cancelled_td_list);
				list_del_init(&cur_td->cancelled_td_list);
				xhci_giveback_urb_in_irq(xhci, cur_td,
						-ESHUTDOWN, "killed");
			}
		}
	}
	spin_unlock_irqrestore(&xhci->lock, flags);
	xhci_dbg(xhci, "Calling usb_hc_died()\n");
	usb_hc_died(xhci_to_hcd(xhci)->primary_hcd);
	xhci_dbg(xhci, "xHCI host controller is dead.\n");
}

/*
 * When we get a completion for a Set Transfer Ring Dequeue Pointer command,
 * we need to clear the set deq pending flag in the endpoint ring state, so that
 * the TD queueing code can ring the doorbell again.  We also need to ring the
 * endpoint doorbell to restart the ring, but only if there aren't more
 * cancellations pending.
 */
static void handle_set_deq_completion(struct xhci_hcd *xhci,
		struct xhci_event_cmd *event,
		union xhci_trb *trb)
{
	unsigned int slot_id;
	unsigned int ep_index;
	unsigned int stream_id;
	struct xhci_ring *ep_ring;
	struct xhci_virt_device *dev;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_slot_ctx *slot_ctx;

	slot_id = TRB_TO_SLOT_ID(le32_to_cpu(trb->generic.field[3]));
	ep_index = TRB_TO_EP_INDEX(le32_to_cpu(trb->generic.field[3]));
	stream_id = TRB_TO_STREAM_ID(le32_to_cpu(trb->generic.field[2]));
	dev = xhci->devs[slot_id];

	ep_ring = xhci_stream_id_to_ring(dev, ep_index, stream_id);
	if (!ep_ring) {
		xhci_warn(xhci, "WARN Set TR deq ptr command for "
				"freed stream ID %u\n",
				stream_id);
		/* XXX: Harmless??? */
		dev->eps[ep_index].ep_state &= ~SET_DEQ_PENDING;
		return;
	}

	ep_ctx = xhci_get_ep_ctx(xhci, dev->out_ctx, ep_index);
	slot_ctx = xhci_get_slot_ctx(xhci, dev->out_ctx);

	if (GET_COMP_CODE(le32_to_cpu(event->status)) != COMP_SUCCESS) {
		unsigned int ep_state;
		unsigned int slot_state;

		switch (GET_COMP_CODE(le32_to_cpu(event->status))) {
		case COMP_TRB_ERR:
			xhci_warn(xhci, "WARN Set TR Deq Ptr cmd invalid because "
					"of stream ID configuration\n");
			break;
		case COMP_CTX_STATE:
			xhci_warn(xhci, "WARN Set TR Deq Ptr cmd failed due "
					"to incorrect slot or ep state.\n");
			ep_state = le32_to_cpu(ep_ctx->ep_info);
			ep_state &= EP_STATE_MASK;
			slot_state = le32_to_cpu(slot_ctx->dev_state);
			slot_state = GET_SLOT_STATE(slot_state);
			xhci_dbg(xhci, "Slot state = %u, EP state = %u\n",
					slot_state, ep_state);
			break;
		case COMP_EBADSLT:
			xhci_warn(xhci, "WARN Set TR Deq Ptr cmd failed because "
					"slot %u was not enabled.\n", slot_id);
			break;
		default:
			xhci_warn(xhci, "WARN Set TR Deq Ptr cmd with unknown "
					"completion code of %u.\n",
				  GET_COMP_CODE(le32_to_cpu(event->status)));
			break;
		}
		/* OK what do we do now?  The endpoint state is hosed, and we
		 * should never get to this point if the synchronization between
		 * queueing, and endpoint state are correct.  This might happen
		 * if the device gets disconnected after we've finished
		 * cancelling URBs, which might not be an error...
		 */
	} else {
		xhci_dbg(xhci, "Successful Set TR Deq Ptr cmd, deq = @%08llx\n",
			 le64_to_cpu(ep_ctx->deq));
		if (xhci_trb_virt_to_dma(dev->eps[ep_index].queued_deq_seg,
					 dev->eps[ep_index].queued_deq_ptr) ==
		    (le64_to_cpu(ep_ctx->deq) & ~(EP_CTX_CYCLE_MASK))) {
			/* Update the ring's dequeue segment and dequeue pointer
			 * to reflect the new position.
			 */
			ep_ring->deq_seg = dev->eps[ep_index].queued_deq_seg;
			ep_ring->dequeue = dev->eps[ep_index].queued_deq_ptr;
		} else {
			xhci_warn(xhci, "Mismatch between completed Set TR Deq "
					"Ptr command & xHCI internal state.\n");
			xhci_warn(xhci, "ep deq seg = %p, deq ptr = %p\n",
					dev->eps[ep_index].queued_deq_seg,
					dev->eps[ep_index].queued_deq_ptr);
		}
	}

	dev->eps[ep_index].ep_state &= ~SET_DEQ_PENDING;
	dev->eps[ep_index].queued_deq_seg = NULL;
	dev->eps[ep_index].queued_deq_ptr = NULL;
	/* Restart any rings with pending URBs */
	ring_doorbell_for_active_rings(xhci, slot_id, ep_index);
}

static void handle_reset_ep_completion(struct xhci_hcd *xhci,
		struct xhci_event_cmd *event,
		union xhci_trb *trb)
{
	int slot_id;
	unsigned int ep_index;

	slot_id = TRB_TO_SLOT_ID(le32_to_cpu(trb->generic.field[3]));
	ep_index = TRB_TO_EP_INDEX(le32_to_cpu(trb->generic.field[3]));
	/* This command will only fail if the endpoint wasn't halted,
	 * but we don't care.
	 */
	xhci_dbg(xhci, "Ignoring reset ep completion code of %u\n",
		 (unsigned int) GET_COMP_CODE(le32_to_cpu(event->status)));

	/* HW with the reset endpoint quirk needs to have a configure endpoint
	 * command complete before the endpoint can be used.  Queue that here
	 * because the HW can't handle two commands being queued in a row.
	 */
	if (xhci->quirks & XHCI_RESET_EP_QUIRK) {
		xhci_dbg(xhci, "Queueing configure endpoint command\n");
		xhci_queue_configure_endpoint(xhci,
				xhci->devs[slot_id]->in_ctx->dma, slot_id,
				false);
		xhci_ring_cmd_db(xhci);
	} else {
		/* Clear our internal halted state and restart the ring(s) */
		xhci->devs[slot_id]->eps[ep_index].ep_state &= ~EP_HALTED;
		ring_doorbell_for_active_rings(xhci, slot_id, ep_index);
	}
}

/* Complete the command and detele it from the devcie's command queue.
 */
static void xhci_complete_cmd_in_cmd_wait_list(struct xhci_hcd *xhci,
		struct xhci_command *command, u32 status)
{
	command->status = status;
	list_del(&command->cmd_list);
	if (command->completion)
		complete(command->completion);
	else
		xhci_free_command(xhci, command);
}


/* Check to see if a command in the device's command queue matches this one.
 * Signal the completion or free the command, and return 1.  Return 0 if the
 * completed command isn't at the head of the command list.
 */
static int handle_cmd_in_cmd_wait_list(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct xhci_event_cmd *event)
{
	struct xhci_command *command;

	if (list_empty(&virt_dev->cmd_list))
		return 0;

	command = list_entry(virt_dev->cmd_list.next,
			struct xhci_command, cmd_list);
	if (xhci->cmd_ring->dequeue != command->command_trb)
		return 0;

	xhci_complete_cmd_in_cmd_wait_list(xhci, command,
			GET_COMP_CODE(le32_to_cpu(event->status)));
	return 1;
}

/*
 * Finding the command trb need to be cancelled and modifying it to
 * NO OP command. And if the command is in device's command wait
 * list, finishing and freeing it.
 *
 * If we can't find the command trb, we think it had already been
 * executed.
 */
static void xhci_cmd_to_noop(struct xhci_hcd *xhci, struct xhci_cd *cur_cd)
{
	struct xhci_segment *cur_seg;
	union xhci_trb *cmd_trb;
	u32 cycle_state;

	if (xhci->cmd_ring->dequeue == xhci->cmd_ring->enqueue)
		return;

	/* find the current segment of command ring */
	cur_seg = find_trb_seg(xhci->cmd_ring->first_seg,
			xhci->cmd_ring->dequeue, &cycle_state);

	/* find the command trb matched by cd from command ring */
	for (cmd_trb = xhci->cmd_ring->dequeue;
			cmd_trb != xhci->cmd_ring->enqueue;
			next_trb(xhci, xhci->cmd_ring, &cur_seg, &cmd_trb)) {
		/* If the trb is link trb, continue */
		if (TRB_TYPE_LINK_LE32(cmd_trb->generic.field[3]))
			continue;

		if (cur_cd->cmd_trb == cmd_trb) {

			/* If the command in device's command list, we should
			 * finish it and free the command structure.
			 */
			if (cur_cd->command)
				xhci_complete_cmd_in_cmd_wait_list(xhci,
					cur_cd->command, COMP_CMD_STOP);

			/* get cycle state from the origin command trb */
			cycle_state = le32_to_cpu(cmd_trb->generic.field[3])
				& TRB_CYCLE;

			/* modify the command trb to NO OP command */
			cmd_trb->generic.field[0] = 0;
			cmd_trb->generic.field[1] = 0;
			cmd_trb->generic.field[2] = 0;
			cmd_trb->generic.field[3] = cpu_to_le32(
					TRB_TYPE(TRB_CMD_NOOP) | cycle_state);
			break;
		}
	}
}

static void xhci_cancel_cmd_in_cd_list(struct xhci_hcd *xhci)
{
	struct xhci_cd *cur_cd, *next_cd;

	if (list_empty(&xhci->cancel_cmd_list))
		return;

	list_for_each_entry_safe(cur_cd, next_cd,
			&xhci->cancel_cmd_list, cancel_cmd_list) {
		xhci_cmd_to_noop(xhci, cur_cd);
		list_del(&cur_cd->cancel_cmd_list);
		kfree(cur_cd);
	}
}

/*
 * traversing the cancel_cmd_list. If the command descriptor according
 * to cmd_trb is found, the function free it and return 1, otherwise
 * return 0.
 */
static int xhci_search_cmd_trb_in_cd_list(struct xhci_hcd *xhci,
		union xhci_trb *cmd_trb)
{
	struct xhci_cd *cur_cd, *next_cd;

	if (list_empty(&xhci->cancel_cmd_list))
		return 0;

	list_for_each_entry_safe(cur_cd, next_cd,
			&xhci->cancel_cmd_list, cancel_cmd_list) {
		if (cur_cd->cmd_trb == cmd_trb) {
			if (cur_cd->command)
				xhci_complete_cmd_in_cmd_wait_list(xhci,
					cur_cd->command, COMP_CMD_STOP);
			list_del(&cur_cd->cancel_cmd_list);
			kfree(cur_cd);
			return 1;
		}
	}

	return 0;
}

/*
 * If the cmd_trb_comp_code is COMP_CMD_ABORT, we just check whether the
 * trb pointed by the command ring dequeue pointer is the trb we want to
 * cancel or not. And if the cmd_trb_comp_code is COMP_CMD_STOP, we will
 * traverse the cancel_cmd_list to trun the all of the commands according
 * to command descriptor to NO-OP trb.
 */
static int handle_stopped_cmd_ring(struct xhci_hcd *xhci,
		int cmd_trb_comp_code)
{
	int cur_trb_is_good = 0;

	/* Searching the cmd trb pointed by the command ring dequeue
	 * pointer in command descriptor list. If it is found, free it.
	 */
	cur_trb_is_good = xhci_search_cmd_trb_in_cd_list(xhci,
			xhci->cmd_ring->dequeue);

	if (cmd_trb_comp_code == COMP_CMD_ABORT)
		xhci->cmd_ring_state = CMD_RING_STATE_STOPPED;
	else if (cmd_trb_comp_code == COMP_CMD_STOP) {
		/* traversing the cancel_cmd_list and canceling
		 * the command according to command descriptor
		 */
		xhci_cancel_cmd_in_cd_list(xhci);

		xhci->cmd_ring_state = CMD_RING_STATE_RUNNING;
		/*
		 * ring command ring doorbell again to restart the
		 * command ring
		 */
		if (xhci->cmd_ring->dequeue != xhci->cmd_ring->enqueue)
			xhci_ring_cmd_db(xhci);
	}
	return cur_trb_is_good;
}

static void handle_cmd_completion(struct xhci_hcd *xhci,
		struct xhci_event_cmd *event)
{
	int slot_id = TRB_TO_SLOT_ID(le32_to_cpu(event->flags));
	u64 cmd_dma;
	dma_addr_t cmd_dequeue_dma;
	struct xhci_input_control_ctx *ctrl_ctx;
	struct xhci_virt_device *virt_dev;
	unsigned int ep_index;
	struct xhci_ring *ep_ring;
	unsigned int ep_state;

	cmd_dma = le64_to_cpu(event->cmd_trb);
	cmd_dequeue_dma = xhci_trb_virt_to_dma(xhci->cmd_ring->deq_seg,
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

	if ((GET_COMP_CODE(le32_to_cpu(event->status)) == COMP_CMD_ABORT) ||
		(GET_COMP_CODE(le32_to_cpu(event->status)) == COMP_CMD_STOP)) {
		/* If the return value is 0, we think the trb pointed by
		 * command ring dequeue pointer is a good trb. The good
		 * trb means we don't want to cancel the trb, but it have
		 * been stopped by host. So we should handle it normally.
		 * Otherwise, driver should invoke inc_deq() and return.
		 */
		if (handle_stopped_cmd_ring(xhci,
				GET_COMP_CODE(le32_to_cpu(event->status)))) {
			inc_deq(xhci, xhci->cmd_ring, false);
			return;
		}
	}

	switch (le32_to_cpu(xhci->cmd_ring->dequeue->generic.field[3])
		& TRB_TYPE_BITMASK) {
	case TRB_TYPE(TRB_ENABLE_SLOT):
		if (GET_COMP_CODE(le32_to_cpu(event->status)) == COMP_SUCCESS)
			xhci->slot_id = slot_id;
		else
			xhci->slot_id = 0;
		complete(&xhci->addr_dev);
		break;
	case TRB_TYPE(TRB_DISABLE_SLOT):
		if (xhci->devs[slot_id]) {
			if (xhci->quirks & XHCI_EP_LIMIT_QUIRK)
				/* Delete default control endpoint resources */
				xhci_free_device_endpoint_resources(xhci,
						xhci->devs[slot_id], true);
			xhci_free_virt_device(xhci, slot_id);
		}
		break;
	case TRB_TYPE(TRB_CONFIG_EP):
		virt_dev = xhci->devs[slot_id];
		if (handle_cmd_in_cmd_wait_list(xhci, virt_dev, event))
			break;
		/*
		 * Configure endpoint commands can come from the USB core
		 * configuration or alt setting changes, or because the HW
		 * needed an extra configure endpoint command after a reset
		 * endpoint command or streams were being configured.
		 * If the command was for a halted endpoint, the xHCI driver
		 * is not waiting on the configure endpoint command.
		 */
		ctrl_ctx = xhci_get_input_control_ctx(xhci,
				virt_dev->in_ctx);
		/* Input ctx add_flags are the endpoint index plus one */
		ep_index = xhci_last_valid_endpoint(le32_to_cpu(ctrl_ctx->add_flags)) - 1;
		/* A usb_set_interface() call directly after clearing a halted
		 * condition may race on this quirky hardware.  Not worth
		 * worrying about, since this is prototype hardware.  Not sure
		 * if this will work for streams, but streams support was
		 * untested on this prototype.
		 */
		if (xhci->quirks & XHCI_RESET_EP_QUIRK &&
				ep_index != (unsigned int) -1 &&
		    le32_to_cpu(ctrl_ctx->add_flags) - SLOT_FLAG ==
		    le32_to_cpu(ctrl_ctx->drop_flags)) {
			ep_ring = xhci->devs[slot_id]->eps[ep_index].ring;
			ep_state = xhci->devs[slot_id]->eps[ep_index].ep_state;
			if (!(ep_state & EP_HALTED))
				goto bandwidth_change;
			xhci_dbg(xhci, "Completed config ep cmd - "
					"last ep index = %d, state = %d\n",
					ep_index, ep_state);
			/* Clear internal halted state and restart ring(s) */
			xhci->devs[slot_id]->eps[ep_index].ep_state &=
				~EP_HALTED;
			ring_doorbell_for_active_rings(xhci, slot_id, ep_index);
			break;
		}
bandwidth_change:
		xhci_dbg(xhci, "Completed config ep cmd\n");
		xhci->devs[slot_id]->cmd_status =
			GET_COMP_CODE(le32_to_cpu(event->status));
		complete(&xhci->devs[slot_id]->cmd_completion);
		break;
	case TRB_TYPE(TRB_EVAL_CONTEXT):
		virt_dev = xhci->devs[slot_id];
		if (handle_cmd_in_cmd_wait_list(xhci, virt_dev, event))
			break;
		xhci->devs[slot_id]->cmd_status = GET_COMP_CODE(le32_to_cpu(event->status));
		complete(&xhci->devs[slot_id]->cmd_completion);
		break;
	case TRB_TYPE(TRB_ADDR_DEV):
		xhci->devs[slot_id]->cmd_status = GET_COMP_CODE(le32_to_cpu(event->status));
		complete(&xhci->addr_dev);
		break;
	case TRB_TYPE(TRB_STOP_RING):
		handle_stopped_endpoint(xhci, xhci->cmd_ring->dequeue, event);
		break;
	case TRB_TYPE(TRB_SET_DEQ):
		handle_set_deq_completion(xhci, event, xhci->cmd_ring->dequeue);
		break;
	case TRB_TYPE(TRB_CMD_NOOP):
		break;
	case TRB_TYPE(TRB_RESET_EP):
		handle_reset_ep_completion(xhci, event, xhci->cmd_ring->dequeue);
		break;
	case TRB_TYPE(TRB_RESET_DEV):
		xhci_dbg(xhci, "Completed reset device command.\n");
		slot_id = TRB_TO_SLOT_ID(
			le32_to_cpu(xhci->cmd_ring->dequeue->generic.field[3]));
		virt_dev = xhci->devs[slot_id];
		if (virt_dev)
			handle_cmd_in_cmd_wait_list(xhci, virt_dev, event);
		else
			xhci_warn(xhci, "Reset device command completion "
					"for disabled slot %u\n", slot_id);
		break;
	case TRB_TYPE(TRB_NEC_GET_FW):
		if (!(xhci->quirks & XHCI_NEC_HOST)) {
			xhci->error_bitmask |= 1 << 6;
			break;
		}
		xhci_dbg(xhci, "NEC firmware version %2x.%02x\n",
			 NEC_FW_MAJOR(le32_to_cpu(event->status)),
			 NEC_FW_MINOR(le32_to_cpu(event->status)));
		break;
	default:
		/* Skip over unknown commands on the event ring */
		xhci->error_bitmask |= 1 << 6;
		break;
	}
	inc_deq(xhci, xhci->cmd_ring, false);
}

static void handle_vendor_event(struct xhci_hcd *xhci,
		union xhci_trb *event)
{
	u32 trb_type;

	trb_type = TRB_FIELD_TO_TYPE(le32_to_cpu(event->generic.field[3]));
	xhci_dbg(xhci, "Vendor specific event TRB type = %u\n", trb_type);
	if (trb_type == TRB_NEC_CMD_COMP && (xhci->quirks & XHCI_NEC_HOST))
		handle_cmd_completion(xhci, &event->event_cmd);
}

/* @port_id: the one-based port ID from the hardware (indexed from array of all
 * port registers -- USB 3.0 and USB 2.0).
 *
 * Returns a zero-based port number, which is suitable for indexing into each of
 * the split roothubs' port arrays and bus state arrays.
 * Add one to it in order to call xhci_find_slot_id_by_port.
 */
static unsigned int find_faked_portnum_from_hw_portnum(struct usb_hcd *hcd,
		struct xhci_hcd *xhci, u32 port_id)
{
	unsigned int i;
	unsigned int num_similar_speed_ports = 0;

	/* port_id from the hardware is 1-based, but port_array[], usb3_ports[],
	 * and usb2_ports are 0-based indexes.  Count the number of similar
	 * speed ports, up to 1 port before this port.
	 */
	for (i = 0; i < (port_id - 1); i++) {
		u8 port_speed = xhci->port_array[i];

		/*
		 * Skip ports that don't have known speeds, or have duplicate
		 * Extended Capabilities port speed entries.
		 */
		if (port_speed == 0 || port_speed == DUPLICATE_ENTRY)
			continue;

		/*
		 * USB 3.0 ports are always under a USB 3.0 hub.  USB 2.0 and
		 * 1.1 ports are under the USB 2.0 hub.  If the port speed
		 * matches the device speed, it's a similar speed port.
		 */
		if ((port_speed == 0x03) == (hcd->speed == HCD_USB3))
			num_similar_speed_ports++;
	}
	return num_similar_speed_ports;
}

static void handle_port_status(struct xhci_hcd *xhci,
		union xhci_trb *event)
{
	struct usb_hcd *hcd;
	u32 port_id;
	u32 temp, temp1;
	int max_ports;
	int slot_id;
	unsigned int faked_port_index;
	u8 major_revision;
	struct xhci_bus_state *bus_state;
	__le32 __iomem **port_array;
	bool bogus_port_status = false;

	/* Port status change events always have a successful completion code */
	if (GET_COMP_CODE(le32_to_cpu(event->generic.field[2])) != COMP_SUCCESS) {
		xhci_warn(xhci, "WARN: xHC returned failed port status event\n");
		xhci->error_bitmask |= 1 << 8;
	}
	port_id = GET_PORT_ID(le32_to_cpu(event->generic.field[0]));
	xhci_dbg(xhci, "Port Status Change Event for port %d\n", port_id);

	max_ports = HCS_MAX_PORTS(xhci->hcs_params1);
	if ((port_id <= 0) || (port_id > max_ports)) {
		xhci_warn(xhci, "Invalid port id %d\n", port_id);
		bogus_port_status = true;
		goto cleanup;
	}

	/* Figure out which usb_hcd this port is attached to:
	 * is it a USB 3.0 port or a USB 2.0/1.1 port?
	 */
	major_revision = xhci->port_array[port_id - 1];
	if (major_revision == 0) {
		xhci_warn(xhci, "Event for port %u not in "
				"Extended Capabilities, ignoring.\n",
				port_id);
		bogus_port_status = true;
		goto cleanup;
	}
	if (major_revision == DUPLICATE_ENTRY) {
		xhci_warn(xhci, "Event for port %u duplicated in"
				"Extended Capabilities, ignoring.\n",
				port_id);
		bogus_port_status = true;
		goto cleanup;
	}

	/*
	 * Hardware port IDs reported by a Port Status Change Event include USB
	 * 3.0 and USB 2.0 ports.  We want to check if the port has reported a
	 * resume event, but we first need to translate the hardware port ID
	 * into the index into the ports on the correct split roothub, and the
	 * correct bus_state structure.
	 */
	/* Find the right roothub. */
	hcd = xhci_to_hcd(xhci);
	if ((major_revision == 0x03) != (hcd->speed == HCD_USB3))
		hcd = xhci->shared_hcd;
	bus_state = &xhci->bus_state[hcd_index(hcd)];
	if (hcd->speed == HCD_USB3)
		port_array = xhci->usb3_ports;
	else
		port_array = xhci->usb2_ports;
	/* Find the faked port hub number */
	faked_port_index = find_faked_portnum_from_hw_portnum(hcd, xhci,
			port_id);

	temp = xhci_readl(xhci, port_array[faked_port_index]);
	if (hcd->state == HC_STATE_SUSPENDED) {
		xhci_dbg(xhci, "resume root hub\n");
		usb_hcd_resume_root_hub(hcd);
	}

	if ((temp & PORT_PLC) && (temp & PORT_PLS_MASK) == XDEV_RESUME) {
		xhci_dbg(xhci, "port resume event for port %d\n", port_id);

		temp1 = xhci_readl(xhci, &xhci->op_regs->command);
		if (!(temp1 & CMD_RUN)) {
			xhci_warn(xhci, "xHC is not running.\n");
			goto cleanup;
		}

		if (DEV_SUPERSPEED(temp)) {
			xhci_dbg(xhci, "resume SS port %d\n", port_id);
			temp = xhci_port_state_to_neutral(temp);
			temp &= ~PORT_PLS_MASK;
			temp |= PORT_LINK_STROBE | XDEV_U0;
			xhci_writel(xhci, temp, port_array[faked_port_index]);
			slot_id = xhci_find_slot_id_by_port(hcd, xhci,
					faked_port_index + 1);
			if (!slot_id) {
				xhci_dbg(xhci, "slot_id is zero\n");
				goto cleanup;
			}
			xhci_ring_device(xhci, slot_id);
			xhci_dbg(xhci, "resume SS port %d finished\n", port_id);
			/* Clear PORT_PLC */
			xhci_test_and_clear_bit(xhci, port_array,
						faked_port_index, PORT_PLC);
		} else {
			xhci_dbg(xhci, "resume HS port %d\n", port_id);
			bus_state->resume_done[faked_port_index] = jiffies +
				msecs_to_jiffies(20);
			mod_timer(&hcd->rh_timer,
				  bus_state->resume_done[faked_port_index]);
			/* Do the rest in GetPortStatus */
		}
	}

	if (hcd->speed != HCD_USB3)
		xhci_test_and_clear_bit(xhci, port_array, faked_port_index,
					PORT_PLC);

cleanup:
	/* Update event ring dequeue pointer before dropping the lock */
	inc_deq(xhci, xhci->event_ring, true);

	/* Don't make the USB core poll the roothub if we got a bad port status
	 * change event.  Besides, at that point we can't tell which roothub
	 * (USB 2.0 or USB 3.0) to kick.
	 */
	if (bogus_port_status)
		return;

	spin_unlock(&xhci->lock);
	/* Pass this up to the core */
	usb_hcd_poll_rh_status(hcd);
	spin_lock(&xhci->lock);
}

/*
 * This TD is defined by the TRBs starting at start_trb in start_seg and ending
 * at end_trb, which may be in another segment.  If the suspect DMA address is a
 * TRB in this TD, this function returns that TRB's segment.  Otherwise it
 * returns 0.
 */
struct xhci_segment *trb_in_td(struct xhci_segment *start_seg,
		union xhci_trb	*start_trb,
		union xhci_trb	*end_trb,
		dma_addr_t	suspect_dma)
{
	dma_addr_t start_dma;
	dma_addr_t end_seg_dma;
	dma_addr_t end_trb_dma;
	struct xhci_segment *cur_seg;

	start_dma = xhci_trb_virt_to_dma(start_seg, start_trb);
	cur_seg = start_seg;

	do {
		if (start_dma == 0)
			return NULL;
		/* We may get an event for a Link TRB in the middle of a TD */
		end_seg_dma = xhci_trb_virt_to_dma(cur_seg,
				&cur_seg->trbs[TRBS_PER_SEGMENT - 1]);
		/* If the end TRB isn't in this segment, this is set to 0 */
		end_trb_dma = xhci_trb_virt_to_dma(cur_seg, end_trb);

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
			return NULL;
		} else {
			/* Might still be somewhere in this segment */
			if (suspect_dma >= start_dma && suspect_dma <= end_seg_dma)
				return cur_seg;
		}
		cur_seg = cur_seg->next;
		start_dma = xhci_trb_virt_to_dma(cur_seg, &cur_seg->trbs[0]);
	} while (cur_seg != start_seg);

	return NULL;
}

static void xhci_cleanup_halted_endpoint(struct xhci_hcd *xhci,
		unsigned int slot_id, unsigned int ep_index,
		unsigned int stream_id,
		struct xhci_td *td, union xhci_trb *event_trb)
{
	struct xhci_virt_ep *ep = &xhci->devs[slot_id]->eps[ep_index];
	ep->ep_state |= EP_HALTED;
	ep->stopped_td = td;
	ep->stopped_trb = event_trb;
	ep->stopped_stream = stream_id;

	xhci_queue_reset_ep(xhci, slot_id, ep_index);
	xhci_cleanup_stalled_ring(xhci, td->urb->dev, ep_index);

	ep->stopped_td = NULL;
	ep->stopped_trb = NULL;
	ep->stopped_stream = 0;

	xhci_ring_cmd_db(xhci);
}

/* Check if an error has halted the endpoint ring.  The class driver will
 * cleanup the halt for a non-default control endpoint if we indicate a stall.
 * However, a babble and other errors also halt the endpoint ring, and the class
 * driver won't clear the halt in that case, so we need to issue a Set Transfer
 * Ring Dequeue Pointer command manually.
 */
static int xhci_requires_manual_halt_cleanup(struct xhci_hcd *xhci,
		struct xhci_ep_ctx *ep_ctx,
		unsigned int trb_comp_code)
{
	/* TRB completion codes that may require a manual halt cleanup */
	if (trb_comp_code == COMP_TX_ERR ||
			trb_comp_code == COMP_BABBLE ||
			trb_comp_code == COMP_SPLIT_ERR)
		/* The 0.96 spec says a babbling control endpoint
		 * is not halted. The 0.96 spec says it is.  Some HW
		 * claims to be 0.95 compliant, but it halts the control
		 * endpoint anyway.  Check if a babble halted the
		 * endpoint.
		 */
		if ((le32_to_cpu(ep_ctx->ep_info) & EP_STATE_MASK) == EP_STATE_HALTED)
			return 1;

	return 0;
}

int xhci_is_vendor_info_code(struct xhci_hcd *xhci, unsigned int trb_comp_code)
{
	if (trb_comp_code >= 224 && trb_comp_code <= 255) {
		/* Vendor defined "informational" completion code,
		 * treat as not-an-error.
		 */
		xhci_dbg(xhci, "Vendor defined info completion code %u\n",
				trb_comp_code);
		xhci_dbg(xhci, "Treating code as success.\n");
		return 1;
	}
	return 0;
}

/*
 * Finish the td processing, remove the td from td list;
 * Return 1 if the urb can be given back.
 */
static int finish_td(struct xhci_hcd *xhci, struct xhci_td *td,
	union xhci_trb *event_trb, struct xhci_transfer_event *event,
	struct xhci_virt_ep *ep, int *status, bool skip)
{
	struct xhci_virt_device *xdev;
	struct xhci_ring *ep_ring;
	unsigned int slot_id;
	int ep_index;
	struct urb *urb = NULL;
	struct xhci_ep_ctx *ep_ctx;
	int ret = 0;
	struct urb_priv	*urb_priv;
	u32 trb_comp_code;

	slot_id = TRB_TO_SLOT_ID(le32_to_cpu(event->flags));
	xdev = xhci->devs[slot_id];
	ep_index = TRB_TO_EP_ID(le32_to_cpu(event->flags)) - 1;
	ep_ring = xhci_dma_to_transfer_ring(ep, le64_to_cpu(event->buffer));
	ep_ctx = xhci_get_ep_ctx(xhci, xdev->out_ctx, ep_index);
	trb_comp_code = GET_COMP_CODE(le32_to_cpu(event->transfer_len));

	if (skip)
		goto td_cleanup;

	if (trb_comp_code == COMP_STOP_INVAL ||
			trb_comp_code == COMP_STOP) {
		/* The Endpoint Stop Command completion will take care of any
		 * stopped TDs.  A stopped TD may be restarted, so don't update
		 * the ring dequeue pointer or take this TD off any lists yet.
		 */
		ep->stopped_td = td;
		ep->stopped_trb = event_trb;
		return 0;
	} else {
		if (trb_comp_code == COMP_STALL) {
			/* The transfer is completed from the driver's
			 * perspective, but we need to issue a set dequeue
			 * command for this stalled endpoint to move the dequeue
			 * pointer past the TD.  We can't do that here because
			 * the halt condition must be cleared first.  Let the
			 * USB class driver clear the stall later.
			 */
			ep->stopped_td = td;
			ep->stopped_trb = event_trb;
			ep->stopped_stream = ep_ring->stream_id;
		} else if (xhci_requires_manual_halt_cleanup(xhci,
					ep_ctx, trb_comp_code)) {
			/* Other types of errors halt the endpoint, but the
			 * class driver doesn't call usb_reset_endpoint() unless
			 * the error is -EPIPE.  Clear the halted status in the
			 * xHCI hardware manually.
			 */
			xhci_cleanup_halted_endpoint(xhci,
					slot_id, ep_index, ep_ring->stream_id,
					td, event_trb);
		} else {
			/* Update ring dequeue pointer */
			while (ep_ring->dequeue != td->last_trb)
				inc_deq(xhci, ep_ring, false);
			inc_deq(xhci, ep_ring, false);
		}

td_cleanup:
		/* Clean up the endpoint's TD list */
		urb = td->urb;
		urb_priv = urb->hcpriv;

		/* Do one last check of the actual transfer length.
		 * If the host controller said we transferred more data than
		 * the buffer length, urb->actual_length will be a very big
		 * number (since it's unsigned).  Play it safe and say we didn't
		 * transfer anything.
		 */
		if (urb->actual_length > urb->transfer_buffer_length) {
			xhci_warn(xhci, "URB transfer length is wrong, "
					"xHC issue? req. len = %u, "
					"act. len = %u\n",
					urb->transfer_buffer_length,
					urb->actual_length);
			urb->actual_length = 0;
			if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
				*status = -EREMOTEIO;
			else
				*status = 0;
		}
		list_del_init(&td->td_list);
		/* Was this TD slated to be cancelled but completed anyway? */
		if (!list_empty(&td->cancelled_td_list))
			list_del_init(&td->cancelled_td_list);

		urb_priv->td_cnt++;
		/* Giveback the urb when all the tds are completed */
		if (urb_priv->td_cnt == urb_priv->length) {
			ret = 1;
			if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
				xhci_to_hcd(xhci)->self.bandwidth_isoc_reqs--;
				if (xhci_to_hcd(xhci)->self.bandwidth_isoc_reqs
					== 0) {
					if (xhci->quirks & XHCI_AMD_PLL_FIX)
						usb_amd_quirk_pll_enable();
				}
			}
		}
	}

	return ret;
}

/*
 * Process control tds, update urb status and actual_length.
 */
static int process_ctrl_td(struct xhci_hcd *xhci, struct xhci_td *td,
	union xhci_trb *event_trb, struct xhci_transfer_event *event,
	struct xhci_virt_ep *ep, int *status)
{
	struct xhci_virt_device *xdev;
	struct xhci_ring *ep_ring;
	unsigned int slot_id;
	int ep_index;
	struct xhci_ep_ctx *ep_ctx;
	u32 trb_comp_code;

	slot_id = TRB_TO_SLOT_ID(le32_to_cpu(event->flags));
	xdev = xhci->devs[slot_id];
	ep_index = TRB_TO_EP_ID(le32_to_cpu(event->flags)) - 1;
	ep_ring = xhci_dma_to_transfer_ring(ep, le64_to_cpu(event->buffer));
	ep_ctx = xhci_get_ep_ctx(xhci, xdev->out_ctx, ep_index);
	trb_comp_code = GET_COMP_CODE(le32_to_cpu(event->transfer_len));

	xhci_debug_trb(xhci, xhci->event_ring->dequeue);
	switch (trb_comp_code) {
	case COMP_SUCCESS:
		if (event_trb == ep_ring->dequeue) {
			xhci_warn(xhci, "WARN: Success on ctrl setup TRB "
					"without IOC set??\n");
			*status = -ESHUTDOWN;
		} else if (event_trb != td->last_trb) {
			xhci_warn(xhci, "WARN: Success on ctrl data TRB "
					"without IOC set??\n");
			*status = -ESHUTDOWN;
		} else {
			*status = 0;
		}
		break;
	case COMP_SHORT_TX:
		xhci_warn(xhci, "WARN: short transfer on control ep\n");
		if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
			*status = -EREMOTEIO;
		else
			*status = 0;
		break;
	case COMP_STOP_INVAL:
	case COMP_STOP:
		return finish_td(xhci, td, event_trb, event, ep, status, false);
	default:
		if (!xhci_requires_manual_halt_cleanup(xhci,
					ep_ctx, trb_comp_code))
			break;
		xhci_dbg(xhci, "TRB error code %u, "
				"halted endpoint index = %u\n",
				trb_comp_code, ep_index);
		/* else fall through */
	case COMP_STALL:
		/* Did we transfer part of the data (middle) phase? */
		if (event_trb != ep_ring->dequeue &&
				event_trb != td->last_trb)
			td->urb->actual_length =
				td->urb->transfer_buffer_length
				- TRB_LEN(le32_to_cpu(event->transfer_len));
		else
			td->urb->actual_length = 0;

		xhci_cleanup_halted_endpoint(xhci,
			slot_id, ep_index, 0, td, event_trb);
		return finish_td(xhci, td, event_trb, event, ep, status, true);
	}
	/*
	 * Did we transfer any data, despite the errors that might have
	 * happened?  I.e. did we get past the setup stage?
	 */
	if (event_trb != ep_ring->dequeue) {
		/* The event was for the status stage */
		if (event_trb == td->last_trb) {
			if (td->urb->actual_length != 0) {
				/* Don't overwrite a previously set error code
				 */
				if ((*status == -EINPROGRESS || *status == 0) &&
						(td->urb->transfer_flags
						 & URB_SHORT_NOT_OK))
					/* Did we already see a short data
					 * stage? */
					*status = -EREMOTEIO;
			} else {
				td->urb->actual_length =
					td->urb->transfer_buffer_length;
			}
		} else {
		/* Maybe the event was for the data stage? */
			td->urb->actual_length =
				td->urb->transfer_buffer_length -
				TRB_LEN(le32_to_cpu(event->transfer_len));
			xhci_dbg(xhci, "Waiting for status "
					"stage event\n");
			return 0;
		}
	}

	return finish_td(xhci, td, event_trb, event, ep, status, false);
}

/*
 * Process isochronous tds, update urb packet status and actual_length.
 */
static int process_isoc_td(struct xhci_hcd *xhci, struct xhci_td *td,
	union xhci_trb *event_trb, struct xhci_transfer_event *event,
	struct xhci_virt_ep *ep, int *status)
{
	struct xhci_ring *ep_ring;
	struct urb_priv *urb_priv;
	int idx;
	int len = 0;
	union xhci_trb *cur_trb;
	struct xhci_segment *cur_seg;
	struct usb_iso_packet_descriptor *frame;
	u32 trb_comp_code;
	bool skip_td = false;

	ep_ring = xhci_dma_to_transfer_ring(ep, le64_to_cpu(event->buffer));
	trb_comp_code = GET_COMP_CODE(le32_to_cpu(event->transfer_len));
	urb_priv = td->urb->hcpriv;
	idx = urb_priv->td_cnt;
	frame = &td->urb->iso_frame_desc[idx];

	/* handle completion code */
	switch (trb_comp_code) {
	case COMP_SUCCESS:
		if (TRB_LEN(le32_to_cpu(event->transfer_len)) == 0) {
			frame->status = 0;
			break;
		}
		if ((xhci->quirks & XHCI_TRUST_TX_LENGTH))
			trb_comp_code = COMP_SHORT_TX;
	case COMP_SHORT_TX:
		frame->status = td->urb->transfer_flags & URB_SHORT_NOT_OK ?
				-EREMOTEIO : 0;
		break;
	case COMP_BW_OVER:
		frame->status = -ECOMM;
		skip_td = true;
		break;
	case COMP_BUFF_OVER:
	case COMP_BABBLE:
		frame->status = -EOVERFLOW;
		skip_td = true;
		break;
	case COMP_DEV_ERR:
	case COMP_STALL:
	case COMP_TX_ERR:
		frame->status = -EPROTO;
		skip_td = true;
		break;
	case COMP_STOP:
	case COMP_STOP_INVAL:
		break;
	default:
		frame->status = -1;
		break;
	}

	if (trb_comp_code == COMP_SUCCESS || skip_td) {
		frame->actual_length = frame->length;
		td->urb->actual_length += frame->length;
	} else {
		for (cur_trb = ep_ring->dequeue,
		     cur_seg = ep_ring->deq_seg; cur_trb != event_trb;
		     next_trb(xhci, ep_ring, &cur_seg, &cur_trb)) {
			if ((le32_to_cpu(cur_trb->generic.field[3]) &
			 TRB_TYPE_BITMASK) != TRB_TYPE(TRB_TR_NOOP) &&
			    (le32_to_cpu(cur_trb->generic.field[3]) &
			 TRB_TYPE_BITMASK) != TRB_TYPE(TRB_LINK))
				len += TRB_LEN(le32_to_cpu(cur_trb->generic.field[2]));
		}
		len += TRB_LEN(le32_to_cpu(cur_trb->generic.field[2])) -
			TRB_LEN(le32_to_cpu(event->transfer_len));

		if (trb_comp_code != COMP_STOP_INVAL) {
			frame->actual_length = len;
			td->urb->actual_length += len;
		}
	}

	return finish_td(xhci, td, event_trb, event, ep, status, false);
}

static int skip_isoc_td(struct xhci_hcd *xhci, struct xhci_td *td,
			struct xhci_transfer_event *event,
			struct xhci_virt_ep *ep, int *status)
{
	struct xhci_ring *ep_ring;
	struct urb_priv *urb_priv;
	struct usb_iso_packet_descriptor *frame;
	int idx;

	ep_ring = xhci_dma_to_transfer_ring(ep, le64_to_cpu(event->buffer));
	urb_priv = td->urb->hcpriv;
	idx = urb_priv->td_cnt;
	frame = &td->urb->iso_frame_desc[idx];

	/* The transfer is partly done. */
	frame->status = -EXDEV;

	/* calc actual length */
	frame->actual_length = 0;

	/* Update ring dequeue pointer */
	while (ep_ring->dequeue != td->last_trb)
		inc_deq(xhci, ep_ring, false);
	inc_deq(xhci, ep_ring, false);

	return finish_td(xhci, td, NULL, event, ep, status, true);
}

/*
 * Process bulk and interrupt tds, update urb status and actual_length.
 */
static int process_bulk_intr_td(struct xhci_hcd *xhci, struct xhci_td *td,
	union xhci_trb *event_trb, struct xhci_transfer_event *event,
	struct xhci_virt_ep *ep, int *status)
{
	struct xhci_ring *ep_ring;
	union xhci_trb *cur_trb;
	struct xhci_segment *cur_seg;
	u32 trb_comp_code;

	ep_ring = xhci_dma_to_transfer_ring(ep, le64_to_cpu(event->buffer));
	trb_comp_code = GET_COMP_CODE(le32_to_cpu(event->transfer_len));

	switch (trb_comp_code) {
	case COMP_SUCCESS:
		/* Double check that the HW transferred everything. */
		if (event_trb != td->last_trb ||
				TRB_LEN(le32_to_cpu(event->transfer_len)) != 0) {
			xhci_warn(xhci, "WARN Successful completion "
					"on short TX\n");
			if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
				*status = -EREMOTEIO;
			else
				*status = 0;
			if ((xhci->quirks & XHCI_TRUST_TX_LENGTH))
				trb_comp_code = COMP_SHORT_TX;
		} else {
			*status = 0;
		}
		break;
	case COMP_SHORT_TX:
		if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
			*status = -EREMOTEIO;
		else
			*status = 0;
		break;
	default:
		/* Others already handled above */
		break;
	}
	if (trb_comp_code == COMP_SHORT_TX)
		xhci_dbg(xhci, "ep %#x - asked for %d bytes, "
				"%d bytes untransferred\n",
				td->urb->ep->desc.bEndpointAddress,
				td->urb->transfer_buffer_length,
				TRB_LEN(le32_to_cpu(event->transfer_len)));
	/* Fast path - was this the last TRB in the TD for this URB? */
	if (event_trb == td->last_trb) {
		if (TRB_LEN(le32_to_cpu(event->transfer_len)) != 0) {
			td->urb->actual_length =
				td->urb->transfer_buffer_length -
				TRB_LEN(le32_to_cpu(event->transfer_len));
			if (td->urb->transfer_buffer_length <
					td->urb->actual_length) {
				xhci_warn(xhci, "HC gave bad length "
						"of %d bytes left\n",
					  TRB_LEN(le32_to_cpu(event->transfer_len)));
				td->urb->actual_length = 0;
				if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
					*status = -EREMOTEIO;
				else
					*status = 0;
			}
			/* Don't overwrite a previously set error code */
			if (*status == -EINPROGRESS) {
				if (td->urb->transfer_flags & URB_SHORT_NOT_OK)
					*status = -EREMOTEIO;
				else
					*status = 0;
			}
		} else {
			td->urb->actual_length =
				td->urb->transfer_buffer_length;
			/* Ignore a short packet completion if the
			 * untransferred length was zero.
			 */
			if (*status == -EREMOTEIO)
				*status = 0;
		}
	} else {
		/* Slow path - walk the list, starting from the dequeue
		 * pointer, to get the actual length transferred.
		 */
		td->urb->actual_length = 0;
		for (cur_trb = ep_ring->dequeue, cur_seg = ep_ring->deq_seg;
				cur_trb != event_trb;
				next_trb(xhci, ep_ring, &cur_seg, &cur_trb)) {
			if ((le32_to_cpu(cur_trb->generic.field[3]) &
			 TRB_TYPE_BITMASK) != TRB_TYPE(TRB_TR_NOOP) &&
			    (le32_to_cpu(cur_trb->generic.field[3]) &
			 TRB_TYPE_BITMASK) != TRB_TYPE(TRB_LINK))
				td->urb->actual_length +=
					TRB_LEN(le32_to_cpu(cur_trb->generic.field[2]));
		}
		/* If the ring didn't stop on a Link or No-op TRB, add
		 * in the actual bytes transferred from the Normal TRB
		 */
		if (trb_comp_code != COMP_STOP_INVAL)
			td->urb->actual_length +=
				TRB_LEN(le32_to_cpu(cur_trb->generic.field[2])) -
				TRB_LEN(le32_to_cpu(event->transfer_len));
	}

	return finish_td(xhci, td, event_trb, event, ep, status, false);
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
	struct xhci_virt_ep *ep;
	struct xhci_ring *ep_ring;
	unsigned int slot_id;
	int ep_index;
	struct xhci_td *td = NULL;
	dma_addr_t event_dma;
	struct xhci_segment *event_seg;
	union xhci_trb *event_trb;
	struct urb *urb = NULL;
	int status = -EINPROGRESS;
	struct urb_priv *urb_priv;
	struct xhci_ep_ctx *ep_ctx;
	struct list_head *tmp;
	u32 trb_comp_code;
	int ret = 0;
	int td_num = 0;

	slot_id = TRB_TO_SLOT_ID(le32_to_cpu(event->flags));
	xdev = xhci->devs[slot_id];
	if (!xdev) {
		xhci_err(xhci, "ERROR Transfer event pointed to bad slot\n");
		return -ENODEV;
	}

	/* Endpoint ID is 1 based, our index is zero based */
	ep_index = TRB_TO_EP_ID(le32_to_cpu(event->flags)) - 1;
	ep = &xdev->eps[ep_index];
	ep_ring = xhci_dma_to_transfer_ring(ep, le64_to_cpu(event->buffer));
	ep_ctx = xhci_get_ep_ctx(xhci, xdev->out_ctx, ep_index);
	if (!ep_ring ||
	    (le32_to_cpu(ep_ctx->ep_info) & EP_STATE_MASK) ==
	    EP_STATE_DISABLED) {
		xhci_err(xhci, "ERROR Transfer event for disabled endpoint "
				"or incorrect stream ring\n");
		return -ENODEV;
	}

	/* Count current td numbers if ep->skip is set */
	if (ep->skip) {
		list_for_each(tmp, &ep_ring->td_list)
			td_num++;
	}

	event_dma = le64_to_cpu(event->buffer);
	trb_comp_code = GET_COMP_CODE(le32_to_cpu(event->transfer_len));
	/* Look for common error cases */
	switch (trb_comp_code) {
	/* Skip codes that require special handling depending on
	 * transfer type
	 */
	case COMP_SUCCESS:
		if (TRB_LEN(le32_to_cpu(event->transfer_len)) == 0)
			break;
		if (xhci->quirks & XHCI_TRUST_TX_LENGTH)
			trb_comp_code = COMP_SHORT_TX;
		else
			xhci_warn(xhci, "WARN Successful completion on short TX: "
					"needs XHCI_TRUST_TX_LENGTH quirk?\n");
	case COMP_SHORT_TX:
		break;
	case COMP_STOP:
		xhci_dbg(xhci, "Stopped on Transfer TRB\n");
		break;
	case COMP_STOP_INVAL:
		xhci_dbg(xhci, "Stopped on No-op or Link TRB\n");
		break;
	case COMP_STALL:
		xhci_warn(xhci, "WARN: Stalled endpoint\n");
		ep->ep_state |= EP_HALTED;
		status = -EPIPE;
		break;
	case COMP_TRB_ERR:
		xhci_warn(xhci, "WARN: TRB error on endpoint\n");
		status = -EILSEQ;
		break;
	case COMP_SPLIT_ERR:
	case COMP_TX_ERR:
		xhci_warn(xhci, "WARN: transfer error on endpoint\n");
		status = -EPROTO;
		break;
	case COMP_BABBLE:
		xhci_warn(xhci, "WARN: babble error on endpoint\n");
		status = -EOVERFLOW;
		break;
	case COMP_DB_ERR:
		xhci_warn(xhci, "WARN: HC couldn't access mem fast enough\n");
		status = -ENOSR;
		break;
	case COMP_BW_OVER:
		xhci_warn(xhci, "WARN: bandwidth overrun event on endpoint\n");
		break;
	case COMP_BUFF_OVER:
		xhci_warn(xhci, "WARN: buffer overrun event on endpoint\n");
		break;
	case COMP_UNDERRUN:
		/*
		 * When the Isoch ring is empty, the xHC will generate
		 * a Ring Overrun Event for IN Isoch endpoint or Ring
		 * Underrun Event for OUT Isoch endpoint.
		 */
		xhci_dbg(xhci, "underrun event on endpoint\n");
		if (!list_empty(&ep_ring->td_list))
			xhci_dbg(xhci, "Underrun Event for slot %d ep %d "
					"still with TDs queued?\n",
				 TRB_TO_SLOT_ID(le32_to_cpu(event->flags)),
				 ep_index);
		goto cleanup;
	case COMP_OVERRUN:
		xhci_dbg(xhci, "overrun event on endpoint\n");
		if (!list_empty(&ep_ring->td_list))
			xhci_dbg(xhci, "Overrun Event for slot %d ep %d "
					"still with TDs queued?\n",
				 TRB_TO_SLOT_ID(le32_to_cpu(event->flags)),
				 ep_index);
		goto cleanup;
	case COMP_DEV_ERR:
		xhci_warn(xhci, "WARN: detect an incompatible device");
		status = -EPROTO;
		break;
	case COMP_MISSED_INT:
		/*
		 * When encounter missed service error, one or more isoc tds
		 * may be missed by xHC.
		 * Set skip flag of the ep_ring; Complete the missed tds as
		 * short transfer when process the ep_ring next time.
		 */
		ep->skip = true;
		xhci_dbg(xhci, "Miss service interval error, set skip flag\n");
		goto cleanup;
	default:
		if (xhci_is_vendor_info_code(xhci, trb_comp_code)) {
			status = 0;
			break;
		}
		xhci_warn(xhci, "ERROR Unknown event condition, HC probably "
				"busted\n");
		goto cleanup;
	}

	do {
		/* This TRB should be in the TD at the head of this ring's
		 * TD list.
		 */
		if (list_empty(&ep_ring->td_list)) {
			xhci_warn(xhci, "WARN Event TRB for slot %d ep %d "
					"with no TDs queued?\n",
				  TRB_TO_SLOT_ID(le32_to_cpu(event->flags)),
				  ep_index);
			xhci_dbg(xhci, "Event TRB with TRB type ID %u\n",
				 (unsigned int) (le32_to_cpu(event->flags)
						 & TRB_TYPE_BITMASK)>>10);
			xhci_print_trb_offsets(xhci, (union xhci_trb *) event);
			if (ep->skip) {
				ep->skip = false;
				xhci_dbg(xhci, "td_list is empty while skip "
						"flag set. Clear skip flag.\n");
			}
			ret = 0;
			goto cleanup;
		}

		/* We've skipped all the TDs on the ep ring when ep->skip set */
		if (ep->skip && td_num == 0) {
			ep->skip = false;
			xhci_dbg(xhci, "All tds on the ep_ring skipped. "
						"Clear skip flag.\n");
			ret = 0;
			goto cleanup;
		}

		td = list_entry(ep_ring->td_list.next, struct xhci_td, td_list);
		if (ep->skip)
			td_num--;

		/* Is this a TRB in the currently executing TD? */
		event_seg = trb_in_td(ep_ring->deq_seg, ep_ring->dequeue,
				td->last_trb, event_dma);

		/*
		 * Skip the Force Stopped Event. The event_trb(event_dma) of FSE
		 * is not in the current TD pointed by ep_ring->dequeue because
		 * that the hardware dequeue pointer still at the previous TRB
		 * of the current TD. The previous TRB maybe a Link TD or the
		 * last TRB of the previous TD. The command completion handle
		 * will take care the rest.
		 */
		if (!event_seg && trb_comp_code == COMP_STOP_INVAL) {
			ret = 0;
			goto cleanup;
		}

		if (!event_seg) {
			if (!ep->skip ||
			    !usb_endpoint_xfer_isoc(&td->urb->ep->desc)) {
				/* Some host controllers give a spurious
				 * successful event after a short transfer.
				 * Ignore it.
				 */
				if ((xhci->quirks & XHCI_SPURIOUS_SUCCESS) && 
						ep_ring->last_td_was_short) {
					ep_ring->last_td_was_short = false;
					ret = 0;
					goto cleanup;
				}
				/* HC is busted, give up! */
				xhci_err(xhci,
					"ERROR Transfer event TRB DMA ptr not "
					"part of current TD\n");
				return -ESHUTDOWN;
			}

			ret = skip_isoc_td(xhci, td, event, ep, &status);
			goto cleanup;
		}
		if (trb_comp_code == COMP_SHORT_TX)
			ep_ring->last_td_was_short = true;
		else
			ep_ring->last_td_was_short = false;

		if (ep->skip) {
			xhci_dbg(xhci, "Found td. Clear skip flag.\n");
			ep->skip = false;
		}

		event_trb = &event_seg->trbs[(event_dma - event_seg->dma) /
						sizeof(*event_trb)];
		/*
		 * No-op TRB should not trigger interrupts.
		 * If event_trb is a no-op TRB, it means the
		 * corresponding TD has been cancelled. Just ignore
		 * the TD.
		 */
		if ((le32_to_cpu(event_trb->generic.field[3])
			     & TRB_TYPE_BITMASK)
				 == TRB_TYPE(TRB_TR_NOOP)) {
			xhci_dbg(xhci,
				 "event_trb is a no-op TRB. Skip it\n");
			goto cleanup;
		}

		/* Now update the urb's actual_length and give back to
		 * the core
		 */
		if (usb_endpoint_xfer_control(&td->urb->ep->desc))
			ret = process_ctrl_td(xhci, td, event_trb, event, ep,
						 &status);
		else if (usb_endpoint_xfer_isoc(&td->urb->ep->desc))
			ret = process_isoc_td(xhci, td, event_trb, event, ep,
						 &status);
		else
			ret = process_bulk_intr_td(xhci, td, event_trb, event,
						 ep, &status);

cleanup:
		/*
		 * Do not update event ring dequeue pointer if ep->skip is set.
		 * Will roll back to continue process missed tds.
		 */
		if (trb_comp_code == COMP_MISSED_INT || !ep->skip) {
			inc_deq(xhci, xhci->event_ring, true);
		}

		if (ret) {
			urb = td->urb;
			urb_priv = urb->hcpriv;
			/* Leave the TD around for the reset endpoint function
			 * to use(but only if it's not a control endpoint,
			 * since we already queued the Set TR dequeue pointer
			 * command for stalled control endpoints).
			 */
			if (usb_endpoint_xfer_control(&urb->ep->desc) ||
				(trb_comp_code != COMP_STALL &&
					trb_comp_code != COMP_BABBLE))
				xhci_urb_free_priv(xhci, urb_priv);

			usb_hcd_unlink_urb_from_ep(bus_to_hcd(urb->dev->bus), urb);
			if ((urb->actual_length != urb->transfer_buffer_length &&
						(urb->transfer_flags &
						 URB_SHORT_NOT_OK)) ||
					status != 0)
				xhci_dbg(xhci, "Giveback URB %p, len = %d, "
						"expected = %x, status = %d\n",
						urb, urb->actual_length,
						urb->transfer_buffer_length,
						status);
			spin_unlock(&xhci->lock);
			/* EHCI, UHCI, and OHCI always unconditionally set the
			 * urb->status of an isochronous endpoint to 0.
			 */
			if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
				status = 0;
			usb_hcd_giveback_urb(bus_to_hcd(urb->dev->bus), urb, status);
			spin_lock(&xhci->lock);
		}

	/*
	 * If ep->skip is set, it means there are missed tds on the
	 * endpoint ring need to take care of.
	 * Process them as short transfer until reach the td pointed by
	 * the event.
	 */
	} while (ep->skip && trb_comp_code != COMP_MISSED_INT);

	return 0;
}

/*
 * This function handles all OS-owned events on the event ring.  It may drop
 * xhci->lock between event processing (e.g. to pass up port status changes).
 * Returns >0 for "possibly more events to process" (caller should call again),
 * otherwise 0 if done.  In future, <0 returns should indicate error code.
 */
static int xhci_handle_event(struct xhci_hcd *xhci)
{
	union xhci_trb *event;
	int update_ptrs = 1;
	int ret;

	if (!xhci->event_ring || !xhci->event_ring->dequeue) {
		xhci->error_bitmask |= 1 << 1;
		return 0;
	}

	event = xhci->event_ring->dequeue;
	/* Does the HC or OS own the TRB? */
	if ((le32_to_cpu(event->event_cmd.flags) & TRB_CYCLE) !=
	    xhci->event_ring->cycle_state) {
		xhci->error_bitmask |= 1 << 2;
		return 0;
	}

	/*
	 * Barrier between reading the TRB_CYCLE (valid) flag above and any
	 * speculative reads of the event's flags/data below.
	 */
	rmb();
	/* FIXME: Handle more event types. */
	switch ((le32_to_cpu(event->event_cmd.flags) & TRB_TYPE_BITMASK)) {
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
		if ((le32_to_cpu(event->event_cmd.flags) & TRB_TYPE_BITMASK) >=
		    TRB_TYPE(48))
			handle_vendor_event(xhci, event);
		else
			xhci->error_bitmask |= 1 << 3;
	}
	/* Any of the above functions may drop and re-acquire the lock, so check
	 * to make sure a watchdog timer didn't mark the host as non-responsive.
	 */
	if (xhci->xhc_state & XHCI_STATE_DYING) {
		xhci_dbg(xhci, "xHCI host dying, returning from "
				"event handler.\n");
		return 0;
	}

	if (update_ptrs)
		/* Update SW event ring dequeue pointer */
		inc_deq(xhci, xhci->event_ring, true);

	/* Are there more items on the event ring?  Caller will call us again to
	 * check.
	 */
	return 1;
}

/*
 * xHCI spec says we can get an interrupt, and if the HC has an error condition,
 * we might get bad data out of the event ring.  Section 4.10.2.7 has a list of
 * indicators of an event TRB error, but we check the status *first* to be safe.
 */
irqreturn_t xhci_irq(struct usb_hcd *hcd)
{
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	u32 status;
	union xhci_trb *trb;
	u64 temp_64;
	union xhci_trb *event_ring_deq;
	dma_addr_t deq;

	spin_lock(&xhci->lock);
	trb = xhci->event_ring->dequeue;
	/* Check if the xHC generated the interrupt, or the irq is shared */
	status = xhci_readl(xhci, &xhci->op_regs->status);
	if (status == 0xffffffff)
		goto hw_died;

	if (!(status & STS_EINT)) {
		spin_unlock(&xhci->lock);
		return IRQ_NONE;
	}
	if (status & STS_FATAL) {
		xhci_warn(xhci, "WARNING: Host System Error\n");
		xhci_halt(xhci);
hw_died:
		spin_unlock(&xhci->lock);
		return -ESHUTDOWN;
	}

	/*
	 * Clear the op reg interrupt status first,
	 * so we can receive interrupts from other MSI-X interrupters.
	 * Write 1 to clear the interrupt status.
	 */
	status |= STS_EINT;
	xhci_writel(xhci, status, &xhci->op_regs->status);
	/* FIXME when MSI-X is supported and there are multiple vectors */
	/* Clear the MSI-X event interrupt status */

	if (hcd->irq != -1) {
		u32 irq_pending;
		/* Acknowledge the PCI interrupt */
		irq_pending = xhci_readl(xhci, &xhci->ir_set->irq_pending);
		irq_pending |= IMAN_IP;
		xhci_writel(xhci, irq_pending, &xhci->ir_set->irq_pending);
	}

	if (xhci->xhc_state & XHCI_STATE_DYING) {
		xhci_dbg(xhci, "xHCI dying, ignoring interrupt. "
				"Shouldn't IRQs be disabled?\n");
		/* Clear the event handler busy flag (RW1C);
		 * the event ring should be empty.
		 */
		temp_64 = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
		xhci_write_64(xhci, temp_64 | ERST_EHB,
				&xhci->ir_set->erst_dequeue);
		spin_unlock(&xhci->lock);

		return IRQ_HANDLED;
	}

	event_ring_deq = xhci->event_ring->dequeue;
	/* FIXME this should be a delayed service routine
	 * that clears the EHB.
	 */
	while (xhci_handle_event(xhci) > 0) {}

	temp_64 = xhci_read_64(xhci, &xhci->ir_set->erst_dequeue);
	/* If necessary, update the HW's version of the event ring deq ptr. */
	if (event_ring_deq != xhci->event_ring->dequeue) {
		deq = xhci_trb_virt_to_dma(xhci->event_ring->deq_seg,
				xhci->event_ring->dequeue);
		if (deq == 0)
			xhci_warn(xhci, "WARN something wrong with SW event "
					"ring dequeue ptr.\n");
		/* Update HC event ring dequeue pointer */
		temp_64 &= ERST_PTR_MASK;
		temp_64 |= ((u64) deq & (u64) ~ERST_PTR_MASK);
	}

	/* Clear the event handler busy flag (RW1C); event ring is empty. */
	temp_64 |= ERST_EHB;
	xhci_write_64(xhci, temp_64, &xhci->ir_set->erst_dequeue);

	spin_unlock(&xhci->lock);

	return IRQ_HANDLED;
}

irqreturn_t xhci_msi_irq(int irq, struct usb_hcd *hcd)
{
	irqreturn_t ret;
	struct xhci_hcd *xhci;

	xhci = hcd_to_xhci(hcd);
	set_bit(HCD_FLAG_SAW_IRQ, &hcd->flags);
	if (xhci->shared_hcd)
		set_bit(HCD_FLAG_SAW_IRQ, &xhci->shared_hcd->flags);

	ret = xhci_irq(hcd);

	return ret;
}

/****		Endpoint Ring Operations	****/

/*
 * Generic function for queueing a TRB on a ring.
 * The caller must have checked to make sure there's room on the ring.
 *
 * @more_trbs_coming:	Will you enqueue more TRBs before calling
 *			prepare_transfer()?
 */
static void queue_trb(struct xhci_hcd *xhci, struct xhci_ring *ring,
		bool consumer, bool more_trbs_coming, bool isoc,
		u32 field1, u32 field2, u32 field3, u32 field4)
{
	struct xhci_generic_trb *trb;

	trb = &ring->enqueue->generic;
	trb->field[0] = cpu_to_le32(field1);
	trb->field[1] = cpu_to_le32(field2);
	trb->field[2] = cpu_to_le32(field3);
	trb->field[3] = cpu_to_le32(field4);
	inc_enq(xhci, ring, consumer, more_trbs_coming, isoc);
}

/*
 * Does various checks on the endpoint ring, and makes it ready to queue num_trbs.
 * FIXME allocate segments if the ring is full.
 */
static int prepare_ring(struct xhci_hcd *xhci, struct xhci_ring *ep_ring,
		u32 ep_state, unsigned int num_trbs, bool isoc, gfp_t mem_flags)
{
	/* Make sure the endpoint has been added to xHC schedule */
	switch (ep_state) {
	case EP_STATE_DISABLED:
		/*
		 * USB core changed config/interfaces without notifying us,
		 * or hardware is reporting the wrong state.
		 */
		xhci_warn(xhci, "WARN urb submitted to disabled ep\n");
		return -ENOENT;
	case EP_STATE_ERROR:
		xhci_warn(xhci, "WARN waiting for error on ep to be cleared\n");
		/* FIXME event handling code for error needs to clear it */
		/* XXX not sure if this should be -ENOENT or not */
		return -EINVAL;
	case EP_STATE_HALTED:
		xhci_dbg(xhci, "WARN halted endpoint, queueing URB anyway.\n");
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

	if (enqueue_is_link_trb(ep_ring)) {
		struct xhci_ring *ring = ep_ring;
		union xhci_trb *next;

		next = ring->enqueue;

		while (last_trb(xhci, ring, ring->enq_seg, next)) {
			/* If we're not dealing with 0.95 hardware or isoc rings
			 * on AMD 0.96 host, clear the chain bit.
			 */
			if (!xhci_link_trb_quirk(xhci) && !(isoc &&
					(xhci->quirks & XHCI_AMD_0x96_HOST)))
				next->link.control &= cpu_to_le32(~TRB_CHAIN);
			else
				next->link.control |= cpu_to_le32(TRB_CHAIN);

			wmb();
			next->link.control ^= cpu_to_le32((u32) TRB_CYCLE);

			/* Toggle the cycle bit after the last ring segment. */
			if (last_trb_on_last_seg(xhci, ring, ring->enq_seg, next)) {
				ring->cycle_state = (ring->cycle_state ? 0 : 1);
				if (!in_interrupt()) {
					xhci_dbg(xhci, "queue_trb: Toggle cycle "
						"state for ring %p = %i\n",
						ring, (unsigned int)ring->cycle_state);
				}
			}
			ring->enq_seg = ring->enq_seg->next;
			ring->enqueue = ring->enq_seg->trbs;
			next = ring->enqueue;
		}
	}

	return 0;
}

static int prepare_transfer(struct xhci_hcd *xhci,
		struct xhci_virt_device *xdev,
		unsigned int ep_index,
		unsigned int stream_id,
		unsigned int num_trbs,
		struct urb *urb,
		unsigned int td_index,
		bool isoc,
		gfp_t mem_flags)
{
	int ret;
	struct urb_priv *urb_priv;
	struct xhci_td	*td;
	struct xhci_ring *ep_ring;
	struct xhci_ep_ctx *ep_ctx = xhci_get_ep_ctx(xhci, xdev->out_ctx, ep_index);

	ep_ring = xhci_stream_id_to_ring(xdev, ep_index, stream_id);
	if (!ep_ring) {
		xhci_dbg(xhci, "Can't prepare ring for bad stream ID %u\n",
				stream_id);
		return -EINVAL;
	}

	ret = prepare_ring(xhci, ep_ring,
			   le32_to_cpu(ep_ctx->ep_info) & EP_STATE_MASK,
			   num_trbs, isoc, mem_flags);
	if (ret)
		return ret;

	urb_priv = urb->hcpriv;
	td = urb_priv->td[td_index];

	INIT_LIST_HEAD(&td->td_list);
	INIT_LIST_HEAD(&td->cancelled_td_list);

	if (td_index == 0) {
		ret = usb_hcd_link_urb_to_ep(bus_to_hcd(urb->dev->bus), urb);
		if (unlikely(ret))
			return ret;
	}

	td->urb = urb;
	/* Add this TD to the tail of the endpoint ring's TD list */
	list_add_tail(&td->td_list, &ep_ring->td_list);
	td->start_seg = ep_ring->enq_seg;
	td->first_trb = ep_ring->enqueue;

	urb_priv->td[td_index] = td;

	return 0;
}

static unsigned int count_sg_trbs_needed(struct xhci_hcd *xhci, struct urb *urb)
{
	int num_sgs, num_trbs, running_total, temp, i;
	struct scatterlist *sg;

	sg = NULL;
	num_sgs = urb->num_mapped_sgs;
	temp = urb->transfer_buffer_length;

	xhci_dbg(xhci, "count sg list trbs: \n");
	num_trbs = 0;
	for_each_sg(urb->sg, sg, num_sgs, i) {
		unsigned int previous_total_trbs = num_trbs;
		unsigned int len = sg_dma_len(sg);

		/* Scatter gather list entries may cross 64KB boundaries */
		running_total = TRB_MAX_BUFF_SIZE -
			(sg_dma_address(sg) & (TRB_MAX_BUFF_SIZE - 1));
		running_total &= TRB_MAX_BUFF_SIZE - 1;
		if (running_total != 0)
			num_trbs++;

		/* How many more 64KB chunks to transfer, how many more TRBs? */
		while (running_total < sg_dma_len(sg) && running_total < temp) {
			num_trbs++;
			running_total += TRB_MAX_BUFF_SIZE;
		}
		xhci_dbg(xhci, " sg #%d: dma = %#llx, len = %#x (%d), num_trbs = %d\n",
				i, (unsigned long long)sg_dma_address(sg),
				len, len, num_trbs - previous_total_trbs);

		len = min_t(int, len, temp);
		temp -= len;
		if (temp == 0)
			break;
	}
	xhci_dbg(xhci, "\n");
	if (!in_interrupt())
		xhci_dbg(xhci, "ep %#x - urb len = %d, sglist used, "
				"num_trbs = %d\n",
				urb->ep->desc.bEndpointAddress,
				urb->transfer_buffer_length,
				num_trbs);
	return num_trbs;
}

static void check_trb_math(struct urb *urb, int num_trbs, int running_total)
{
	if (num_trbs != 0)
		dev_err(&urb->dev->dev, "%s - ep %#x - Miscalculated number of "
				"TRBs, %d left\n", __func__,
				urb->ep->desc.bEndpointAddress, num_trbs);
	if (running_total != urb->transfer_buffer_length)
		dev_err(&urb->dev->dev, "%s - ep %#x - Miscalculated tx length, "
				"queued %#x (%d), asked for %#x (%d)\n",
				__func__,
				urb->ep->desc.bEndpointAddress,
				running_total, running_total,
				urb->transfer_buffer_length,
				urb->transfer_buffer_length);
}

static void giveback_first_trb(struct xhci_hcd *xhci, int slot_id,
		unsigned int ep_index, unsigned int stream_id, int start_cycle,
		struct xhci_generic_trb *start_trb)
{
	/*
	 * Pass all the TRBs to the hardware at once and make sure this write
	 * isn't reordered.
	 */
	wmb();
	if (start_cycle)
		start_trb->field[3] |= cpu_to_le32(start_cycle);
	else
		start_trb->field[3] &= cpu_to_le32(~TRB_CYCLE);
	xhci_ring_ep_doorbell(xhci, slot_id, ep_index, stream_id);
}

/*
 * xHCI uses normal TRBs for both bulk and interrupt.  When the interrupt
 * endpoint is to be serviced, the xHC will consume (at most) one TD.  A TD
 * (comprised of sg list entries) can take several service intervals to
 * transmit.
 */
int xhci_queue_intr_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ep_ctx *ep_ctx = xhci_get_ep_ctx(xhci,
			xhci->devs[slot_id]->out_ctx, ep_index);
	int xhci_interval;
	int ep_interval;

	xhci_interval = EP_INTERVAL_TO_UFRAMES(le32_to_cpu(ep_ctx->ep_info));
	ep_interval = urb->interval;
	/* Convert to microframes */
	if (urb->dev->speed == USB_SPEED_LOW ||
			urb->dev->speed == USB_SPEED_FULL)
		ep_interval *= 8;
	/* FIXME change this to a warning and a suggestion to use the new API
	 * to set the polling interval (once the API is added).
	 */
	if (xhci_interval != ep_interval) {
		if (printk_ratelimit())
			dev_dbg(&urb->dev->dev, "Driver uses different interval"
					" (%d microframe%s) than xHCI "
					"(%d microframe%s)\n",
					ep_interval,
					ep_interval == 1 ? "" : "s",
					xhci_interval,
					xhci_interval == 1 ? "" : "s");
		urb->interval = xhci_interval;
		/* Convert back to frames for LS/FS devices */
		if (urb->dev->speed == USB_SPEED_LOW ||
				urb->dev->speed == USB_SPEED_FULL)
			urb->interval /= 8;
	}
	return xhci_queue_bulk_tx(xhci, GFP_ATOMIC, urb, slot_id, ep_index);
}

/*
 * The TD size is the number of bytes remaining in the TD (including this TRB),
 * right shifted by 10.
 * It must fit in bits 21:17, so it can't be bigger than 31.
 */
static u32 xhci_td_remainder(unsigned int remainder)
{
	u32 max = (1 << (21 - 17 + 1)) - 1;

	if ((remainder >> 10) >= max)
		return max << 17;
	else
		return (remainder >> 10) << 17;
}

/*
 * For xHCI 1.0 host controllers, TD size is the number of packets remaining in
 * the TD (*not* including this TRB).
 *
 * Total TD packet count = total_packet_count =
 *     roundup(TD size in bytes / wMaxPacketSize)
 *
 * Packets transferred up to and including this TRB = packets_transferred =
 *     rounddown(total bytes transferred including this TRB / wMaxPacketSize)
 *
 * TD size = total_packet_count - packets_transferred
 *
 * It must fit in bits 21:17, so it can't be bigger than 31.
 */

static u32 xhci_v1_0_td_remainder(int running_total, int trb_buff_len,
		unsigned int total_packet_count, struct urb *urb)
{
	int packets_transferred;

	/* One TRB with a zero-length data packet. */
	if (running_total == 0 && trb_buff_len == 0)
		return 0;

	/* All the TRB queueing functions don't count the current TRB in
	 * running_total.
	 */
	packets_transferred = (running_total + trb_buff_len) /
		le16_to_cpu(urb->ep->desc.wMaxPacketSize);

	return xhci_td_remainder(total_packet_count - packets_transferred);
}

static int queue_bulk_sg_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ring *ep_ring;
	unsigned int num_trbs;
	struct urb_priv *urb_priv;
	struct xhci_td *td;
	struct scatterlist *sg;
	int num_sgs;
	int trb_buff_len, this_sg_len, running_total;
	unsigned int total_packet_count;
	bool first_trb;
	u64 addr;
	bool more_trbs_coming;

	struct xhci_generic_trb *start_trb;
	int start_cycle;

	ep_ring = xhci_urb_to_transfer_ring(xhci, urb);
	if (!ep_ring)
		return -EINVAL;

	num_trbs = count_sg_trbs_needed(xhci, urb);
	num_sgs = urb->num_mapped_sgs;
	total_packet_count = roundup(urb->transfer_buffer_length,
			le16_to_cpu(urb->ep->desc.wMaxPacketSize));

	trb_buff_len = prepare_transfer(xhci, xhci->devs[slot_id],
			ep_index, urb->stream_id,
			num_trbs, urb, 0, false, mem_flags);
	if (trb_buff_len < 0)
		return trb_buff_len;

	urb_priv = urb->hcpriv;
	td = urb_priv->td[0];

	/*
	 * Don't give the first TRB to the hardware (by toggling the cycle bit)
	 * until we've finished creating all the other TRBs.  The ring's cycle
	 * state may change as we enqueue the other TRBs, so save it too.
	 */
	start_trb = &ep_ring->enqueue->generic;
	start_cycle = ep_ring->cycle_state;

	running_total = 0;
	/*
	 * How much data is in the first TRB?
	 *
	 * There are three forces at work for TRB buffer pointers and lengths:
	 * 1. We don't want to walk off the end of this sg-list entry buffer.
	 * 2. The transfer length that the driver requested may be smaller than
	 *    the amount of memory allocated for this scatter-gather list.
	 * 3. TRBs buffers can't cross 64KB boundaries.
	 */
	sg = urb->sg;
	addr = (u64) sg_dma_address(sg);
	this_sg_len = sg_dma_len(sg);
	trb_buff_len = TRB_MAX_BUFF_SIZE - (addr & (TRB_MAX_BUFF_SIZE - 1));
	trb_buff_len = min_t(int, trb_buff_len, this_sg_len);
	if (trb_buff_len > urb->transfer_buffer_length)
		trb_buff_len = urb->transfer_buffer_length;
	xhci_dbg(xhci, "First length to xfer from 1st sglist entry = %u\n",
			trb_buff_len);

	first_trb = true;
	/* Queue the first TRB, even if it's zero-length */
	do {
		u32 field = 0;
		u32 length_field = 0;
		u32 remainder = 0;

		/* Don't change the cycle bit of the first TRB until later */
		if (first_trb) {
			first_trb = false;
			if (start_cycle == 0)
				field |= 0x1;
		} else
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

		/* Only set interrupt on short packet for IN endpoints */
		if (usb_urb_dir_in(urb))
			field |= TRB_ISP;

		xhci_dbg(xhci, " sg entry: dma = %#x, len = %#x (%d), "
				"64KB boundary at %#x, end dma = %#x\n",
				(unsigned int) addr, trb_buff_len, trb_buff_len,
				(unsigned int) (addr + TRB_MAX_BUFF_SIZE) & ~(TRB_MAX_BUFF_SIZE - 1),
				(unsigned int) addr + trb_buff_len);
		if (TRB_MAX_BUFF_SIZE -
				(addr & (TRB_MAX_BUFF_SIZE - 1)) < trb_buff_len) {
			xhci_warn(xhci, "WARN: sg dma xfer crosses 64KB boundaries!\n");
			xhci_dbg(xhci, "Next boundary at %#x, end dma = %#x\n",
					(unsigned int) (addr + TRB_MAX_BUFF_SIZE) & ~(TRB_MAX_BUFF_SIZE - 1),
					(unsigned int) addr + trb_buff_len);
		}

		/* Set the TRB length, TD size, and interrupter fields. */
		if (xhci->hci_version < 0x100) {
			remainder = xhci_td_remainder(
					urb->transfer_buffer_length -
					running_total);
		} else {
			remainder = xhci_v1_0_td_remainder(running_total,
					trb_buff_len, total_packet_count, urb);
		}
		length_field = TRB_LEN(trb_buff_len) |
			remainder |
			TRB_INTR_TARGET(0);

		if (num_trbs > 1)
			more_trbs_coming = true;
		else
			more_trbs_coming = false;
		queue_trb(xhci, ep_ring, false, more_trbs_coming, false,
				lower_32_bits(addr),
				upper_32_bits(addr),
				length_field,
				field | TRB_TYPE(TRB_NORMAL));
		--num_trbs;
		running_total += trb_buff_len;

		/* Calculate length for next transfer --
		 * Are we done queueing all the TRBs for this sg entry?
		 */
		this_sg_len -= trb_buff_len;
		if (this_sg_len == 0) {
			--num_sgs;
			if (num_sgs == 0)
				break;
			sg = sg_next(sg);
			addr = (u64) sg_dma_address(sg);
			this_sg_len = sg_dma_len(sg);
		} else {
			addr += trb_buff_len;
		}

		trb_buff_len = TRB_MAX_BUFF_SIZE -
			(addr & (TRB_MAX_BUFF_SIZE - 1));
		trb_buff_len = min_t(int, trb_buff_len, this_sg_len);
		if (running_total + trb_buff_len > urb->transfer_buffer_length)
			trb_buff_len =
				urb->transfer_buffer_length - running_total;
	} while (running_total < urb->transfer_buffer_length);

	check_trb_math(urb, num_trbs, running_total);
	giveback_first_trb(xhci, slot_id, ep_index, urb->stream_id,
			start_cycle, start_trb);
	return 0;
}

/* This is very similar to what ehci-q.c qtd_fill() does */
int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ring *ep_ring;
	struct urb_priv *urb_priv;
	struct xhci_td *td;
	int num_trbs;
	struct xhci_generic_trb *start_trb;
	bool first_trb;
	bool more_trbs_coming;
	int start_cycle;
	u32 field, length_field;

	int running_total, trb_buff_len, ret;
	unsigned int total_packet_count;
	u64 addr;

	if (urb->num_sgs)
		return queue_bulk_sg_tx(xhci, mem_flags, urb, slot_id, ep_index);

	ep_ring = xhci_urb_to_transfer_ring(xhci, urb);
	if (!ep_ring)
		return -EINVAL;

	num_trbs = 0;
	/* How much data is (potentially) left before the 64KB boundary? */
	running_total = TRB_MAX_BUFF_SIZE -
		(urb->transfer_dma & (TRB_MAX_BUFF_SIZE - 1));
	running_total &= TRB_MAX_BUFF_SIZE - 1;

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
		xhci_dbg(xhci, "ep %#x - urb len = %#x (%d), "
				"addr = %#llx, num_trbs = %d\n",
				urb->ep->desc.bEndpointAddress,
				urb->transfer_buffer_length,
				urb->transfer_buffer_length,
				(unsigned long long)urb->transfer_dma,
				num_trbs);

	ret = prepare_transfer(xhci, xhci->devs[slot_id],
			ep_index, urb->stream_id,
			num_trbs, urb, 0, false, mem_flags);
	if (ret < 0)
		return ret;

	urb_priv = urb->hcpriv;
	td = urb_priv->td[0];

	/*
	 * Don't give the first TRB to the hardware (by toggling the cycle bit)
	 * until we've finished creating all the other TRBs.  The ring's cycle
	 * state may change as we enqueue the other TRBs, so save it too.
	 */
	start_trb = &ep_ring->enqueue->generic;
	start_cycle = ep_ring->cycle_state;

	running_total = 0;
	total_packet_count = roundup(urb->transfer_buffer_length,
			le16_to_cpu(urb->ep->desc.wMaxPacketSize));
	/* How much data is in the first TRB? */
	addr = (u64) urb->transfer_dma;
	trb_buff_len = TRB_MAX_BUFF_SIZE -
		(urb->transfer_dma & (TRB_MAX_BUFF_SIZE - 1));
	if (trb_buff_len > urb->transfer_buffer_length)
		trb_buff_len = urb->transfer_buffer_length;

	first_trb = true;

	/* Queue the first TRB, even if it's zero-length */
	do {
		u32 remainder = 0;
		field = 0;

		/* Don't change the cycle bit of the first TRB until later */
		if (first_trb) {
			first_trb = false;
			if (start_cycle == 0)
				field |= 0x1;
		} else
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

		/* Only set interrupt on short packet for IN endpoints */
		if (usb_urb_dir_in(urb))
			field |= TRB_ISP;

		/* Set the TRB length, TD size, and interrupter fields. */
		if (xhci->hci_version < 0x100) {
			remainder = xhci_td_remainder(
					urb->transfer_buffer_length -
					running_total);
		} else {
			remainder = xhci_v1_0_td_remainder(running_total,
					trb_buff_len, total_packet_count, urb);
		}
		length_field = TRB_LEN(trb_buff_len) |
			remainder |
			TRB_INTR_TARGET(0);

		if (num_trbs > 1)
			more_trbs_coming = true;
		else
			more_trbs_coming = false;
		queue_trb(xhci, ep_ring, false, more_trbs_coming, false,
				lower_32_bits(addr),
				upper_32_bits(addr),
				length_field,
				field | TRB_TYPE(TRB_NORMAL));
		--num_trbs;
		running_total += trb_buff_len;

		/* Calculate length for next transfer */
		addr += trb_buff_len;
		trb_buff_len = urb->transfer_buffer_length - running_total;
		if (trb_buff_len > TRB_MAX_BUFF_SIZE)
			trb_buff_len = TRB_MAX_BUFF_SIZE;
	} while (running_total < urb->transfer_buffer_length);

	check_trb_math(urb, num_trbs, running_total);
	giveback_first_trb(xhci, slot_id, ep_index, urb->stream_id,
			start_cycle, start_trb);
	return 0;
}

/* Caller must have locked xhci->lock */
int xhci_queue_ctrl_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ring *ep_ring;
	int num_trbs;
	int ret;
	struct usb_ctrlrequest *setup;
	struct xhci_generic_trb *start_trb;
	int start_cycle;
	u32 field, length_field;
	struct urb_priv *urb_priv;
	struct xhci_td *td;

	ep_ring = xhci_urb_to_transfer_ring(xhci, urb);
	if (!ep_ring)
		return -EINVAL;

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
	ret = prepare_transfer(xhci, xhci->devs[slot_id],
			ep_index, urb->stream_id,
			num_trbs, urb, 0, false, mem_flags);
	if (ret < 0)
		return ret;

	urb_priv = urb->hcpriv;
	td = urb_priv->td[0];

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
	field = 0;
	field |= TRB_IDT | TRB_TYPE(TRB_SETUP);
	if (start_cycle == 0)
		field |= 0x1;

	/* xHCI 1.0 6.4.1.2.1: Transfer Type field */
	if (xhci->hci_version == 0x100) {
		if (urb->transfer_buffer_length > 0) {
			if (setup->bRequestType & USB_DIR_IN)
				field |= TRB_TX_TYPE(TRB_DATA_IN);
			else
				field |= TRB_TX_TYPE(TRB_DATA_OUT);
		}
	}

	queue_trb(xhci, ep_ring, false, true, false,
		  setup->bRequestType | setup->bRequest << 8 | le16_to_cpu(setup->wValue) << 16,
		  le16_to_cpu(setup->wIndex) | le16_to_cpu(setup->wLength) << 16,
		  TRB_LEN(8) | TRB_INTR_TARGET(0),
		  /* Immediate data in pointer */
		  field);

	/* If there's data, queue data TRBs */
	/* Only set interrupt on short packet for IN endpoints */
	if (usb_urb_dir_in(urb))
		field = TRB_ISP | TRB_TYPE(TRB_DATA);
	else
		field = TRB_TYPE(TRB_DATA);

	length_field = TRB_LEN(urb->transfer_buffer_length) |
		xhci_td_remainder(urb->transfer_buffer_length) |
		TRB_INTR_TARGET(0);
	if (urb->transfer_buffer_length > 0) {
		if (setup->bRequestType & USB_DIR_IN)
			field |= TRB_DIR_IN;
		queue_trb(xhci, ep_ring, false, true, false,
				lower_32_bits(urb->transfer_dma),
				upper_32_bits(urb->transfer_dma),
				length_field,
				field | ep_ring->cycle_state);
	}

	/* Save the DMA address of the last TRB in the TD */
	td->last_trb = ep_ring->enqueue;

	/* Queue status TRB - see Table 7 and sections 4.11.2.2 and 6.4.1.2.3 */
	/* If the device sent data, the status stage is an OUT transfer */
	if (urb->transfer_buffer_length > 0 && setup->bRequestType & USB_DIR_IN)
		field = 0;
	else
		field = TRB_DIR_IN;
	queue_trb(xhci, ep_ring, false, false, false,
			0,
			0,
			TRB_INTR_TARGET(0),
			/* Event on completion */
			field | TRB_IOC | TRB_TYPE(TRB_STATUS) | ep_ring->cycle_state);

	giveback_first_trb(xhci, slot_id, ep_index, 0,
			start_cycle, start_trb);
	return 0;
}

static int count_isoc_trbs_needed(struct xhci_hcd *xhci,
		struct urb *urb, int i)
{
	int num_trbs = 0;
	u64 addr, td_len;

	addr = (u64) (urb->transfer_dma + urb->iso_frame_desc[i].offset);
	td_len = urb->iso_frame_desc[i].length;

	num_trbs = DIV_ROUND_UP(td_len + (addr & (TRB_MAX_BUFF_SIZE - 1)),
			TRB_MAX_BUFF_SIZE);
	if (num_trbs == 0)
		num_trbs++;

	return num_trbs;
}

/*
 * The transfer burst count field of the isochronous TRB defines the number of
 * bursts that are required to move all packets in this TD.  Only SuperSpeed
 * devices can burst up to bMaxBurst number of packets per service interval.
 * This field is zero based, meaning a value of zero in the field means one
 * burst.  Basically, for everything but SuperSpeed devices, this field will be
 * zero.  Only xHCI 1.0 host controllers support this field.
 */
static unsigned int xhci_get_burst_count(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct urb *urb, unsigned int total_packet_count)
{
	unsigned int max_burst;

	if (xhci->hci_version < 0x100 || udev->speed != USB_SPEED_SUPER)
		return 0;

	max_burst = urb->ep->ss_ep_comp.bMaxBurst;
	return roundup(total_packet_count, max_burst + 1) - 1;
}

/*
 * Returns the number of packets in the last "burst" of packets.  This field is
 * valid for all speeds of devices.  USB 2.0 devices can only do one "burst", so
 * the last burst packet count is equal to the total number of packets in the
 * TD.  SuperSpeed endpoints can have up to 3 bursts.  All but the last burst
 * must contain (bMaxBurst + 1) number of packets, but the last burst can
 * contain 1 to (bMaxBurst + 1) packets.
 */
static unsigned int xhci_get_last_burst_packet_count(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct urb *urb, unsigned int total_packet_count)
{
	unsigned int max_burst;
	unsigned int residue;

	if (xhci->hci_version < 0x100)
		return 0;

	switch (udev->speed) {
	case USB_SPEED_SUPER:
		/* bMaxBurst is zero based: 0 means 1 packet per burst */
		max_burst = urb->ep->ss_ep_comp.bMaxBurst;
		residue = total_packet_count % (max_burst + 1);
		/* If residue is zero, the last burst contains (max_burst + 1)
		 * number of packets, but the TLBPC field is zero-based.
		 */
		if (residue == 0)
			return max_burst;
		return residue - 1;
	default:
		if (total_packet_count == 0)
			return 0;
		return total_packet_count - 1;
	}
}

/* This is for isoc transfer */
static int xhci_queue_isoc_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_ring *ep_ring;
	struct urb_priv *urb_priv;
	struct xhci_td *td;
	int num_tds, trbs_per_td;
	struct xhci_generic_trb *start_trb;
	bool first_trb;
	int start_cycle;
	u32 field, length_field;
	int running_total, trb_buff_len, td_len, td_remain_len, ret;
	u64 start_addr, addr;
	int i, j;
	bool more_trbs_coming;

	ep_ring = xhci->devs[slot_id]->eps[ep_index].ring;

	num_tds = urb->number_of_packets;
	if (num_tds < 1) {
		xhci_dbg(xhci, "Isoc URB with zero packets?\n");
		return -EINVAL;
	}

	if (!in_interrupt())
		xhci_dbg(xhci, "ep %#x - urb len = %#x (%d),"
				" addr = %#llx, num_tds = %d\n",
				urb->ep->desc.bEndpointAddress,
				urb->transfer_buffer_length,
				urb->transfer_buffer_length,
				(unsigned long long)urb->transfer_dma,
				num_tds);

	start_addr = (u64) urb->transfer_dma;
	start_trb = &ep_ring->enqueue->generic;
	start_cycle = ep_ring->cycle_state;

	urb_priv = urb->hcpriv;
	/* Queue the first TRB, even if it's zero-length */
	for (i = 0; i < num_tds; i++) {
		unsigned int total_packet_count;
		unsigned int burst_count;
		unsigned int residue;

		first_trb = true;
		running_total = 0;
		addr = start_addr + urb->iso_frame_desc[i].offset;
		td_len = urb->iso_frame_desc[i].length;
		td_remain_len = td_len;
		total_packet_count = roundup(td_len,
				le16_to_cpu(urb->ep->desc.wMaxPacketSize));
		/* A zero-length transfer still involves at least one packet. */
		if (total_packet_count == 0)
			total_packet_count++;
		burst_count = xhci_get_burst_count(xhci, urb->dev, urb,
				total_packet_count);
		residue = xhci_get_last_burst_packet_count(xhci,
				urb->dev, urb, total_packet_count);

		trbs_per_td = count_isoc_trbs_needed(xhci, urb, i);

		ret = prepare_transfer(xhci, xhci->devs[slot_id], ep_index,
				urb->stream_id, trbs_per_td, urb, i, true,
				mem_flags);
		if (ret < 0) {
			if (i == 0)
				return ret;
			goto cleanup;
		}

		td = urb_priv->td[i];
		for (j = 0; j < trbs_per_td; j++) {
			u32 remainder = 0;
			field = TRB_TBC(burst_count) | TRB_TLBPC(residue);

			if (first_trb) {
				/* Queue the isoc TRB */
				field |= TRB_TYPE(TRB_ISOC);
				/* Assume URB_ISO_ASAP is set */
				field |= TRB_SIA;
				if (i == 0) {
					if (start_cycle == 0)
						field |= 0x1;
				} else
					field |= ep_ring->cycle_state;
				first_trb = false;
			} else {
				/* Queue other normal TRBs */
				field |= TRB_TYPE(TRB_NORMAL);
				field |= ep_ring->cycle_state;
			}

			/* Only set interrupt on short packet for IN EPs */
			if (usb_urb_dir_in(urb))
				field |= TRB_ISP;

			/* Chain all the TRBs together; clear the chain bit in
			 * the last TRB to indicate it's the last TRB in the
			 * chain.
			 */
			if (j < trbs_per_td - 1) {
				field |= TRB_CHAIN;
				more_trbs_coming = true;
			} else {
				td->last_trb = ep_ring->enqueue;
				field |= TRB_IOC;
				if (xhci->hci_version == 0x100 &&
						!(xhci->quirks &
							XHCI_AVOID_BEI)) {
					/* Set BEI bit except for the last td */
					if (i < num_tds - 1)
						field |= TRB_BEI;
				}
				more_trbs_coming = false;
			}

			/* Calculate TRB length */
			trb_buff_len = TRB_MAX_BUFF_SIZE -
				(addr & ((1 << TRB_MAX_BUFF_SHIFT) - 1));
			if (trb_buff_len > td_remain_len)
				trb_buff_len = td_remain_len;

			/* Set the TRB length, TD size, & interrupter fields. */
			if (xhci->hci_version < 0x100) {
				remainder = xhci_td_remainder(
						td_len - running_total);
			} else {
				remainder = xhci_v1_0_td_remainder(
						running_total, trb_buff_len,
						total_packet_count, urb);
			}
			length_field = TRB_LEN(trb_buff_len) |
				remainder |
				TRB_INTR_TARGET(0);

			queue_trb(xhci, ep_ring, false, more_trbs_coming, true,
				lower_32_bits(addr),
				upper_32_bits(addr),
				length_field,
				field);
			running_total += trb_buff_len;

			addr += trb_buff_len;
			td_remain_len -= trb_buff_len;
		}

		/* Check TD length */
		if (running_total != td_len) {
			xhci_err(xhci, "ISOC TD length unmatch\n");
			ret = -EINVAL;
			goto cleanup;
		}
	}

	if (xhci_to_hcd(xhci)->self.bandwidth_isoc_reqs == 0) {
		if (xhci->quirks & XHCI_AMD_PLL_FIX)
			usb_amd_quirk_pll_disable();
	}
	xhci_to_hcd(xhci)->self.bandwidth_isoc_reqs++;

	giveback_first_trb(xhci, slot_id, ep_index, urb->stream_id,
			start_cycle, start_trb);
	return 0;
cleanup:
	/* Clean up a partially enqueued isoc transfer. */

	for (i--; i >= 0; i--)
		list_del_init(&urb_priv->td[i]->td_list);

	/* Use the first TD as a temporary variable to turn the TDs we've queued
	 * into No-ops with a software-owned cycle bit. That way the hardware
	 * won't accidentally start executing bogus TDs when we partially
	 * overwrite them.  td->first_trb and td->start_seg are already set.
	 */
	urb_priv->td[0]->last_trb = ep_ring->enqueue;
	/* Every TRB except the first & last will have its cycle bit flipped. */
	td_to_noop(xhci, ep_ring, urb_priv->td[0], true);

	/* Reset the ring enqueue back to the first TRB and its cycle bit. */
	ep_ring->enqueue = urb_priv->td[0]->first_trb;
	ep_ring->enq_seg = urb_priv->td[0]->start_seg;
	ep_ring->cycle_state = start_cycle;
	usb_hcd_unlink_urb_from_ep(bus_to_hcd(urb->dev->bus), urb);
	return ret;
}

/*
 * Check transfer ring to guarantee there is enough room for the urb.
 * Update ISO URB start_frame and interval.
 * Update interval as xhci_queue_intr_tx does. Just use xhci frame_index to
 * update the urb->start_frame by now.
 * Always assume URB_ISO_ASAP set, and NEVER use urb->start_frame as input.
 */
int xhci_queue_isoc_tx_prepare(struct xhci_hcd *xhci, gfp_t mem_flags,
		struct urb *urb, int slot_id, unsigned int ep_index)
{
	struct xhci_virt_device *xdev;
	struct xhci_ring *ep_ring;
	struct xhci_ep_ctx *ep_ctx;
	int start_frame;
	int xhci_interval;
	int ep_interval;
	int num_tds, num_trbs, i;
	int ret;

	xdev = xhci->devs[slot_id];
	ep_ring = xdev->eps[ep_index].ring;
	ep_ctx = xhci_get_ep_ctx(xhci, xdev->out_ctx, ep_index);

	num_trbs = 0;
	num_tds = urb->number_of_packets;
	for (i = 0; i < num_tds; i++)
		num_trbs += count_isoc_trbs_needed(xhci, urb, i);

	/* Check the ring to guarantee there is enough room for the whole urb.
	 * Do not insert any td of the urb to the ring if the check failed.
	 */
	ret = prepare_ring(xhci, ep_ring, le32_to_cpu(ep_ctx->ep_info) & EP_STATE_MASK,
			   num_trbs, true, mem_flags);
	if (ret)
		return ret;

	start_frame = xhci_readl(xhci, &xhci->run_regs->microframe_index);
	start_frame &= 0x3fff;

	urb->start_frame = start_frame;
	if (urb->dev->speed == USB_SPEED_LOW ||
			urb->dev->speed == USB_SPEED_FULL)
		urb->start_frame >>= 3;

	xhci_interval = EP_INTERVAL_TO_UFRAMES(le32_to_cpu(ep_ctx->ep_info));
	ep_interval = urb->interval;
	/* Convert to microframes */
	if (urb->dev->speed == USB_SPEED_LOW ||
			urb->dev->speed == USB_SPEED_FULL)
		ep_interval *= 8;
	/* FIXME change this to a warning and a suggestion to use the new API
	 * to set the polling interval (once the API is added).
	 */
	if (xhci_interval != ep_interval) {
		if (printk_ratelimit())
			dev_dbg(&urb->dev->dev, "Driver uses different interval"
					" (%d microframe%s) than xHCI "
					"(%d microframe%s)\n",
					ep_interval,
					ep_interval == 1 ? "" : "s",
					xhci_interval,
					xhci_interval == 1 ? "" : "s");
		urb->interval = xhci_interval;
		/* Convert back to frames for LS/FS devices */
		if (urb->dev->speed == USB_SPEED_LOW ||
				urb->dev->speed == USB_SPEED_FULL)
			urb->interval /= 8;
	}
	return xhci_queue_isoc_tx(xhci, GFP_ATOMIC, urb, slot_id, ep_index);
}

/****		Command Ring Operations		****/

/* Generic function for queueing a command TRB on the command ring.
 * Check to make sure there's room on the command ring for one command TRB.
 * Also check that there's room reserved for commands that must not fail.
 * If this is a command that must not fail, meaning command_must_succeed = TRUE,
 * then only check for the number of reserved spots.
 * Don't decrement xhci->cmd_ring_reserved_trbs after we've queued the TRB
 * because the command event handler may want to resubmit a failed command.
 */
static int queue_command(struct xhci_hcd *xhci, u32 field1, u32 field2,
		u32 field3, u32 field4, bool command_must_succeed)
{
	int reserved_trbs = xhci->cmd_ring_reserved_trbs;
	int ret;

	if (!command_must_succeed)
		reserved_trbs++;

	ret = prepare_ring(xhci, xhci->cmd_ring, EP_STATE_RUNNING,
			reserved_trbs, false, GFP_ATOMIC);
	if (ret < 0) {
		xhci_err(xhci, "ERR: No room for command on command ring\n");
		if (command_must_succeed)
			xhci_err(xhci, "ERR: Reserved TRB counting for "
					"unfailable commands failed.\n");
		return ret;
	}
	queue_trb(xhci, xhci->cmd_ring, false, false, false, field1, field2,
			field3,	field4 | xhci->cmd_ring->cycle_state);
	return 0;
}

/* Queue a slot enable or disable request on the command ring */
int xhci_queue_slot_control(struct xhci_hcd *xhci, u32 trb_type, u32 slot_id)
{
	return queue_command(xhci, 0, 0, 0,
			TRB_TYPE(trb_type) | SLOT_ID_FOR_TRB(slot_id), false);
}

/* Queue an address device command TRB */
int xhci_queue_address_device(struct xhci_hcd *xhci, dma_addr_t in_ctx_ptr,
		u32 slot_id)
{
	return queue_command(xhci, lower_32_bits(in_ctx_ptr),
			upper_32_bits(in_ctx_ptr), 0,
			TRB_TYPE(TRB_ADDR_DEV) | SLOT_ID_FOR_TRB(slot_id),
			false);
}

int xhci_queue_vendor_command(struct xhci_hcd *xhci,
		u32 field1, u32 field2, u32 field3, u32 field4)
{
	return queue_command(xhci, field1, field2, field3, field4, false);
}

/* Queue a reset device command TRB */
int xhci_queue_reset_device(struct xhci_hcd *xhci, u32 slot_id)
{
	return queue_command(xhci, 0, 0, 0,
			TRB_TYPE(TRB_RESET_DEV) | SLOT_ID_FOR_TRB(slot_id),
			false);
}

/* Queue a configure endpoint command TRB */
int xhci_queue_configure_endpoint(struct xhci_hcd *xhci, dma_addr_t in_ctx_ptr,
		u32 slot_id, bool command_must_succeed)
{
	return queue_command(xhci, lower_32_bits(in_ctx_ptr),
			upper_32_bits(in_ctx_ptr), 0,
			TRB_TYPE(TRB_CONFIG_EP) | SLOT_ID_FOR_TRB(slot_id),
			command_must_succeed);
}

/* Queue an evaluate context command TRB */
int xhci_queue_evaluate_context(struct xhci_hcd *xhci, dma_addr_t in_ctx_ptr,
		u32 slot_id)
{
	return queue_command(xhci, lower_32_bits(in_ctx_ptr),
			upper_32_bits(in_ctx_ptr), 0,
			TRB_TYPE(TRB_EVAL_CONTEXT) | SLOT_ID_FOR_TRB(slot_id),
			false);
}

/*
 * Suspend is set to indicate "Stop Endpoint Command" is being issued to stop
 * activity on an endpoint that is about to be suspended.
 */
int xhci_queue_stop_endpoint(struct xhci_hcd *xhci, int slot_id,
		unsigned int ep_index, int suspend)
{
	u32 trb_slot_id = SLOT_ID_FOR_TRB(slot_id);
	u32 trb_ep_index = EP_ID_FOR_TRB(ep_index);
	u32 type = TRB_TYPE(TRB_STOP_RING);
	u32 trb_suspend = SUSPEND_PORT_FOR_TRB(suspend);

	return queue_command(xhci, 0, 0, 0,
			trb_slot_id | trb_ep_index | type | trb_suspend, false);
}

/* Set Transfer Ring Dequeue Pointer command.
 * This should not be used for endpoints that have streams enabled.
 */
static int queue_set_tr_deq(struct xhci_hcd *xhci, int slot_id,
		unsigned int ep_index, unsigned int stream_id,
		struct xhci_segment *deq_seg,
		union xhci_trb *deq_ptr, u32 cycle_state)
{
	dma_addr_t addr;
	u32 trb_slot_id = SLOT_ID_FOR_TRB(slot_id);
	u32 trb_ep_index = EP_ID_FOR_TRB(ep_index);
	u32 trb_stream_id = STREAM_ID_FOR_TRB(stream_id);
	u32 type = TRB_TYPE(TRB_SET_DEQ);
	struct xhci_virt_ep *ep;

	addr = xhci_trb_virt_to_dma(deq_seg, deq_ptr);
	if (addr == 0) {
		xhci_warn(xhci, "WARN Cannot submit Set TR Deq Ptr\n");
		xhci_warn(xhci, "WARN deq seg = %p, deq pt = %p\n",
				deq_seg, deq_ptr);
		return 0;
	}
	ep = &xhci->devs[slot_id]->eps[ep_index];
	if ((ep->ep_state & SET_DEQ_PENDING)) {
		xhci_warn(xhci, "WARN Cannot submit Set TR Deq Ptr\n");
		xhci_warn(xhci, "A Set TR Deq Ptr command is pending.\n");
		return 0;
	}
	ep->queued_deq_seg = deq_seg;
	ep->queued_deq_ptr = deq_ptr;
	return queue_command(xhci, lower_32_bits(addr) | cycle_state,
			upper_32_bits(addr), trb_stream_id,
			trb_slot_id | trb_ep_index | type, false);
}

int xhci_queue_reset_ep(struct xhci_hcd *xhci, int slot_id,
		unsigned int ep_index)
{
	u32 trb_slot_id = SLOT_ID_FOR_TRB(slot_id);
	u32 trb_ep_index = EP_ID_FOR_TRB(ep_index);
	u32 type = TRB_TYPE(TRB_RESET_EP);

	return queue_command(xhci, 0, 0, 0, trb_slot_id | trb_ep_index | type,
			false);
}
