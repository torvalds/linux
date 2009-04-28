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
	u64 cmd_dma;
	dma_addr_t cmd_dequeue_dma;

	/* Check completion code */
	if (GET_COMP_CODE(event->status) != COMP_SUCCESS)
		xhci_dbg(xhci, "WARN: unsuccessful no-op command\n");

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

void handle_event(struct xhci_hcd *xhci)
{
	union xhci_trb *event;

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

	/* FIXME: Only handles command completion events. */
	switch ((event->event_cmd.flags & TRB_TYPE_BITMASK)) {
	case TRB_TYPE(TRB_COMPLETION):
		handle_cmd_completion(xhci, &event->event_cmd);
		break;
	default:
		xhci->error_bitmask |= 1 << 3;
	}

	/* Update SW and HC event ring dequeue pointer */
	inc_deq(xhci, xhci->event_ring, true);
	set_hc_event_deq(xhci);
	/* Are there more items on the event ring? */
	handle_event(xhci);
}

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
