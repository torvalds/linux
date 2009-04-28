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

#include "xhci.h"

#define XHCI_INIT_VALUE 0x0

/* Add verbose debugging later, just print everything for now */

void xhci_dbg_regs(struct xhci_hcd *xhci)
{
	u32 temp;

	xhci_dbg(xhci, "// xHCI capability registers at 0x%x:\n",
			(unsigned int) xhci->cap_regs);
	temp = xhci_readl(xhci, &xhci->cap_regs->hc_capbase);
	xhci_dbg(xhci, "// @%x = 0x%x (CAPLENGTH AND HCIVERSION)\n",
			(unsigned int) &xhci->cap_regs->hc_capbase,
			(unsigned int) temp);
	xhci_dbg(xhci, "//   CAPLENGTH: 0x%x\n",
			(unsigned int) HC_LENGTH(temp));
#if 0
	xhci_dbg(xhci, "//   HCIVERSION: 0x%x\n",
			(unsigned int) HC_VERSION(temp));
#endif

	xhci_dbg(xhci, "// xHCI operational registers at 0x%x:\n",
			(unsigned int) xhci->op_regs);

	temp = xhci_readl(xhci, &xhci->cap_regs->run_regs_off);
	xhci_dbg(xhci, "// @%x = 0x%x RTSOFF\n",
			(unsigned int) &xhci->cap_regs->run_regs_off,
			(unsigned int) temp & RTSOFF_MASK);
	xhci_dbg(xhci, "// xHCI runtime registers at 0x%x:\n",
			(unsigned int) xhci->run_regs);

	temp = xhci_readl(xhci, &xhci->cap_regs->db_off);
	xhci_dbg(xhci, "// @%x = 0x%x DBOFF\n",
			(unsigned int) &xhci->cap_regs->db_off, temp);
	xhci_dbg(xhci, "// Doorbell array at 0x%x:\n",
			(unsigned int) xhci->dba);
}

void xhci_print_cap_regs(struct xhci_hcd *xhci)
{
	u32 temp;

	xhci_dbg(xhci, "xHCI capability registers at 0x%x:\n",
			(unsigned int) xhci->cap_regs);

	temp = xhci_readl(xhci, &xhci->cap_regs->hc_capbase);
	xhci_dbg(xhci, "CAPLENGTH AND HCIVERSION 0x%x:\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "CAPLENGTH: 0x%x\n",
			(unsigned int) HC_LENGTH(temp));
	xhci_dbg(xhci, "HCIVERSION: 0x%x\n",
			(unsigned int) HC_VERSION(temp));

	temp = xhci_readl(xhci, &xhci->cap_regs->hcs_params1);
	xhci_dbg(xhci, "HCSPARAMS 1: 0x%x\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "  Max device slots: %u\n",
			(unsigned int) HCS_MAX_SLOTS(temp));
	xhci_dbg(xhci, "  Max interrupters: %u\n",
			(unsigned int) HCS_MAX_INTRS(temp));
	xhci_dbg(xhci, "  Max ports: %u\n",
			(unsigned int) HCS_MAX_PORTS(temp));

	temp = xhci_readl(xhci, &xhci->cap_regs->hcs_params2);
	xhci_dbg(xhci, "HCSPARAMS 2: 0x%x\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "  Isoc scheduling threshold: %u\n",
			(unsigned int) HCS_IST(temp));
	xhci_dbg(xhci, "  Maximum allowed segments in event ring: %u\n",
			(unsigned int) HCS_ERST_MAX(temp));

	temp = xhci_readl(xhci, &xhci->cap_regs->hcs_params3);
	xhci_dbg(xhci, "HCSPARAMS 3 0x%x:\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "  Worst case U1 device exit latency: %u\n",
			(unsigned int) HCS_U1_LATENCY(temp));
	xhci_dbg(xhci, "  Worst case U2 device exit latency: %u\n",
			(unsigned int) HCS_U2_LATENCY(temp));

	temp = xhci_readl(xhci, &xhci->cap_regs->hcc_params);
	xhci_dbg(xhci, "HCC PARAMS 0x%x:\n", (unsigned int) temp);
	xhci_dbg(xhci, "  HC generates %s bit addresses\n",
			HCC_64BIT_ADDR(temp) ? "64" : "32");
	/* FIXME */
	xhci_dbg(xhci, "  FIXME: more HCCPARAMS debugging\n");

	temp = xhci_readl(xhci, &xhci->cap_regs->run_regs_off);
	xhci_dbg(xhci, "RTSOFF 0x%x:\n", temp & RTSOFF_MASK);
}

void xhci_print_command_reg(struct xhci_hcd *xhci)
{
	u32 temp;

	temp = xhci_readl(xhci, &xhci->op_regs->command);
	xhci_dbg(xhci, "USBCMD 0x%x:\n", temp);
	xhci_dbg(xhci, "  HC is %s\n",
			(temp & CMD_RUN) ? "running" : "being stopped");
	xhci_dbg(xhci, "  HC has %sfinished hard reset\n",
			(temp & CMD_RESET) ? "not " : "");
	xhci_dbg(xhci, "  Event Interrupts %s\n",
			(temp & CMD_EIE) ? "enabled " : "disabled");
	xhci_dbg(xhci, "  Host System Error Interrupts %s\n",
			(temp & CMD_EIE) ? "enabled " : "disabled");
	xhci_dbg(xhci, "  HC has %sfinished light reset\n",
			(temp & CMD_LRESET) ? "not " : "");
}

void xhci_print_status(struct xhci_hcd *xhci)
{
	u32 temp;

	temp = xhci_readl(xhci, &xhci->op_regs->status);
	xhci_dbg(xhci, "USBSTS 0x%x:\n", temp);
	xhci_dbg(xhci, "  Event ring is %sempty\n",
			(temp & STS_EINT) ? "not " : "");
	xhci_dbg(xhci, "  %sHost System Error\n",
			(temp & STS_FATAL) ? "WARNING: " : "No ");
	xhci_dbg(xhci, "  HC is %s\n",
			(temp & STS_HALT) ? "halted" : "running");
}

void xhci_print_op_regs(struct xhci_hcd *xhci)
{
	xhci_dbg(xhci, "xHCI operational registers at 0x%x:\n",
			(unsigned int) xhci->op_regs);
	xhci_print_command_reg(xhci);
	xhci_print_status(xhci);
}

void xhci_print_ports(struct xhci_hcd *xhci)
{
	u32 __iomem *addr;
	int i, j;
	int ports;
	char *names[NUM_PORT_REGS] = {
		"status",
		"power",
		"link",
		"reserved",
	};

	ports = HCS_MAX_PORTS(xhci->hcs_params1);
	addr = &xhci->op_regs->port_status_base;
	for (i = 0; i < ports; i++) {
		for (j = 0; j < NUM_PORT_REGS; ++j) {
			xhci_dbg(xhci, "0x%x port %s reg = 0x%x\n",
					(unsigned int) addr,
					names[j],
					(unsigned int) xhci_readl(xhci, addr));
			addr++;
		}
	}
}

void xhci_print_ir_set(struct xhci_hcd *xhci, struct intr_reg *ir_set, int set_num)
{
	void *addr;
	u32 temp;

	addr = &ir_set->irq_pending;
	temp = xhci_readl(xhci, addr);
	if (temp == XHCI_INIT_VALUE)
		return;

	xhci_dbg(xhci, "  0x%x: ir_set[%i]\n", (unsigned int) ir_set, set_num);

	xhci_dbg(xhci, "  0x%x: ir_set.pending = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->irq_control;
	temp = xhci_readl(xhci, addr);
	xhci_dbg(xhci, "  0x%x: ir_set.control = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->erst_size;
	temp = xhci_readl(xhci, addr);
	xhci_dbg(xhci, "  0x%x: ir_set.erst_size = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->rsvd;
	temp = xhci_readl(xhci, addr);
	if (temp != XHCI_INIT_VALUE)
		xhci_dbg(xhci, "  WARN: 0x%x: ir_set.rsvd = 0x%x\n",
				(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->erst_base[0];
	temp = xhci_readl(xhci, addr);
	xhci_dbg(xhci, "  0x%x: ir_set.erst_base[0] = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->erst_base[1];
	temp = xhci_readl(xhci, addr);
	xhci_dbg(xhci, "  0x%x: ir_set.erst_base[1] = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->erst_dequeue[0];
	temp = xhci_readl(xhci, addr);
	xhci_dbg(xhci, "  0x%x: ir_set.erst_dequeue[0] = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);

	addr = &ir_set->erst_dequeue[1];
	temp = xhci_readl(xhci, addr);
	xhci_dbg(xhci, "  0x%x: ir_set.erst_dequeue[1] = 0x%x\n",
			(unsigned int) addr, (unsigned int) temp);
}

void xhci_print_run_regs(struct xhci_hcd *xhci)
{
	u32 temp;
	int i;

	xhci_dbg(xhci, "xHCI runtime registers at 0x%x:\n",
			(unsigned int) xhci->run_regs);
	temp = xhci_readl(xhci, &xhci->run_regs->microframe_index);
	xhci_dbg(xhci, "  0x%x: Microframe index = 0x%x\n",
			(unsigned int) &xhci->run_regs->microframe_index,
			(unsigned int) temp);
	for (i = 0; i < 7; ++i) {
		temp = xhci_readl(xhci, &xhci->run_regs->rsvd[i]);
		if (temp != XHCI_INIT_VALUE)
			xhci_dbg(xhci, "  WARN: 0x%x: Rsvd[%i] = 0x%x\n",
					(unsigned int) &xhci->run_regs->rsvd[i],
					i, (unsigned int) temp);
	}
}

void xhci_print_registers(struct xhci_hcd *xhci)
{
	xhci_print_cap_regs(xhci);
	xhci_print_op_regs(xhci);
	xhci_print_ports(xhci);
}

void xhci_print_trb_offsets(struct xhci_hcd *xhci, union xhci_trb *trb)
{
	int i;
	for (i = 0; i < 4; ++i)
		xhci_dbg(xhci, "Offset 0x%x = 0x%x\n",
				i*4, trb->generic.field[i]);
}

/**
 * Debug a transfer request block (TRB).
 */
void xhci_debug_trb(struct xhci_hcd *xhci, union xhci_trb *trb)
{
	u64	address;
	u32	type = xhci_readl(xhci, &trb->link.control) & TRB_TYPE_BITMASK;

	switch (type) {
	case TRB_TYPE(TRB_LINK):
		xhci_dbg(xhci, "Link TRB:\n");
		xhci_print_trb_offsets(xhci, trb);

		address = trb->link.segment_ptr[0] +
			(((u64) trb->link.segment_ptr[1]) << 32);
		xhci_dbg(xhci, "Next ring segment DMA address = 0x%llx\n", address);

		xhci_dbg(xhci, "Interrupter target = 0x%x\n",
				GET_INTR_TARGET(trb->link.intr_target));
		xhci_dbg(xhci, "Cycle bit = %u\n",
				(unsigned int) (trb->link.control & TRB_CYCLE));
		xhci_dbg(xhci, "Toggle cycle bit = %u\n",
				(unsigned int) (trb->link.control & LINK_TOGGLE));
		xhci_dbg(xhci, "No Snoop bit = %u\n",
				(unsigned int) (trb->link.control & TRB_NO_SNOOP));
		break;
	case TRB_TYPE(TRB_TRANSFER):
		address = trb->trans_event.buffer[0] +
			(((u64) trb->trans_event.buffer[1]) << 32);
		/*
		 * FIXME: look at flags to figure out if it's an address or if
		 * the data is directly in the buffer field.
		 */
		xhci_dbg(xhci, "DMA address or buffer contents= %llu\n", address);
		break;
	case TRB_TYPE(TRB_COMPLETION):
		address = trb->event_cmd.cmd_trb[0] +
			(((u64) trb->event_cmd.cmd_trb[1]) << 32);
		xhci_dbg(xhci, "Command TRB pointer = %llu\n", address);
		xhci_dbg(xhci, "Completion status = %u\n",
				(unsigned int) GET_COMP_CODE(trb->event_cmd.status));
		xhci_dbg(xhci, "Flags = 0x%x\n", (unsigned int) trb->event_cmd.flags);
		break;
	default:
		xhci_dbg(xhci, "Unknown TRB with TRB type ID %u\n",
				(unsigned int) type>>10);
		xhci_print_trb_offsets(xhci, trb);
		break;
	}
}

/**
 * Debug a segment with an xHCI ring.
 *
 * @return The Link TRB of the segment, or NULL if there is no Link TRB
 * (which is a bug, since all segments must have a Link TRB).
 *
 * Prints out all TRBs in the segment, even those after the Link TRB.
 *
 * XXX: should we print out TRBs that the HC owns?  As long as we don't
 * write, that should be fine...  We shouldn't expect that the memory pointed to
 * by the TRB is valid at all.  Do we care about ones the HC owns?  Probably,
 * for HC debugging.
 */
void xhci_debug_segment(struct xhci_hcd *xhci, struct xhci_segment *seg)
{
	int i;
	u32 addr = (u32) seg->dma;
	union xhci_trb *trb = seg->trbs;

	for (i = 0; i < TRBS_PER_SEGMENT; ++i) {
		trb = &seg->trbs[i];
		xhci_dbg(xhci, "@%08x %08x %08x %08x %08x\n", addr,
				(unsigned int) trb->link.segment_ptr[0],
				(unsigned int) trb->link.segment_ptr[1],
				(unsigned int) trb->link.intr_target,
				(unsigned int) trb->link.control);
		addr += sizeof(*trb);
	}
}

void xhci_dbg_ring_ptrs(struct xhci_hcd *xhci, struct xhci_ring *ring)
{
	xhci_dbg(xhci, "Ring deq = 0x%x (virt), 0x%x (dma)\n",
			(unsigned int) ring->dequeue,
			trb_virt_to_dma(ring->deq_seg, ring->dequeue));
	xhci_dbg(xhci, "Ring deq updated %u times\n",
			ring->deq_updates);
	xhci_dbg(xhci, "Ring enq = 0x%x (virt), 0x%x (dma)\n",
			(unsigned int) ring->enqueue,
			trb_virt_to_dma(ring->enq_seg, ring->enqueue));
	xhci_dbg(xhci, "Ring enq updated %u times\n",
			ring->enq_updates);
}

/**
 * Debugging for an xHCI ring, which is a queue broken into multiple segments.
 *
 * Print out each segment in the ring.  Check that the DMA address in
 * each link segment actually matches the segment's stored DMA address.
 * Check that the link end bit is only set at the end of the ring.
 * Check that the dequeue and enqueue pointers point to real data in this ring
 * (not some other ring).
 */
void xhci_debug_ring(struct xhci_hcd *xhci, struct xhci_ring *ring)
{
	/* FIXME: Throw an error if any segment doesn't have a Link TRB */
	struct xhci_segment *seg;
	struct xhci_segment *first_seg = ring->first_seg;
	xhci_debug_segment(xhci, first_seg);

	if (!ring->enq_updates && !ring->deq_updates) {
		xhci_dbg(xhci, "  Ring has not been updated\n");
		return;
	}
	for (seg = first_seg->next; seg != first_seg; seg = seg->next)
		xhci_debug_segment(xhci, seg);
}

void xhci_dbg_erst(struct xhci_hcd *xhci, struct xhci_erst *erst)
{
	u32 addr = (u32) erst->erst_dma_addr;
	int i;
	struct xhci_erst_entry *entry;

	for (i = 0; i < erst->num_entries; ++i) {
		entry = &erst->entries[i];
		xhci_dbg(xhci, "@%08x %08x %08x %08x %08x\n",
				(unsigned int) addr,
				(unsigned int) entry->seg_addr[0],
				(unsigned int) entry->seg_addr[1],
				(unsigned int) entry->seg_size,
				(unsigned int) entry->rsvd);
		addr += sizeof(*entry);
	}
}

void xhci_dbg_cmd_ptrs(struct xhci_hcd *xhci)
{
	u32 val;

	val = xhci_readl(xhci, &xhci->op_regs->cmd_ring[0]);
	xhci_dbg(xhci, "// xHC command ring deq ptr low bits + flags = 0x%x\n", val);
	val = xhci_readl(xhci, &xhci->op_regs->cmd_ring[1]);
	xhci_dbg(xhci, "// xHC command ring deq ptr high bits = 0x%x\n", val);
}
