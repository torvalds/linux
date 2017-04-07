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

	xhci_dbg(xhci, "// xHCI capability registers at %p:\n",
			xhci->cap_regs);
	temp = readl(&xhci->cap_regs->hc_capbase);
	xhci_dbg(xhci, "// @%p = 0x%x (CAPLENGTH AND HCIVERSION)\n",
			&xhci->cap_regs->hc_capbase, temp);
	xhci_dbg(xhci, "//   CAPLENGTH: 0x%x\n",
			(unsigned int) HC_LENGTH(temp));
#if 0
	xhci_dbg(xhci, "//   HCIVERSION: 0x%x\n",
			(unsigned int) HC_VERSION(temp));
#endif

	xhci_dbg(xhci, "// xHCI operational registers at %p:\n", xhci->op_regs);

	temp = readl(&xhci->cap_regs->run_regs_off);
	xhci_dbg(xhci, "// @%p = 0x%x RTSOFF\n",
			&xhci->cap_regs->run_regs_off,
			(unsigned int) temp & RTSOFF_MASK);
	xhci_dbg(xhci, "// xHCI runtime registers at %p:\n", xhci->run_regs);

	temp = readl(&xhci->cap_regs->db_off);
	xhci_dbg(xhci, "// @%p = 0x%x DBOFF\n", &xhci->cap_regs->db_off, temp);
	xhci_dbg(xhci, "// Doorbell array at %p:\n", xhci->dba);
}

static void xhci_print_cap_regs(struct xhci_hcd *xhci)
{
	u32 temp;
	u32 hci_version;

	xhci_dbg(xhci, "xHCI capability registers at %p:\n", xhci->cap_regs);

	temp = readl(&xhci->cap_regs->hc_capbase);
	hci_version = HC_VERSION(temp);
	xhci_dbg(xhci, "CAPLENGTH AND HCIVERSION 0x%x:\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "CAPLENGTH: 0x%x\n",
			(unsigned int) HC_LENGTH(temp));
	xhci_dbg(xhci, "HCIVERSION: 0x%x\n", hci_version);

	temp = readl(&xhci->cap_regs->hcs_params1);
	xhci_dbg(xhci, "HCSPARAMS 1: 0x%x\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "  Max device slots: %u\n",
			(unsigned int) HCS_MAX_SLOTS(temp));
	xhci_dbg(xhci, "  Max interrupters: %u\n",
			(unsigned int) HCS_MAX_INTRS(temp));
	xhci_dbg(xhci, "  Max ports: %u\n",
			(unsigned int) HCS_MAX_PORTS(temp));

	temp = readl(&xhci->cap_regs->hcs_params2);
	xhci_dbg(xhci, "HCSPARAMS 2: 0x%x\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "  Isoc scheduling threshold: %u\n",
			(unsigned int) HCS_IST(temp));
	xhci_dbg(xhci, "  Maximum allowed segments in event ring: %u\n",
			(unsigned int) HCS_ERST_MAX(temp));

	temp = readl(&xhci->cap_regs->hcs_params3);
	xhci_dbg(xhci, "HCSPARAMS 3 0x%x:\n",
			(unsigned int) temp);
	xhci_dbg(xhci, "  Worst case U1 device exit latency: %u\n",
			(unsigned int) HCS_U1_LATENCY(temp));
	xhci_dbg(xhci, "  Worst case U2 device exit latency: %u\n",
			(unsigned int) HCS_U2_LATENCY(temp));

	temp = readl(&xhci->cap_regs->hcc_params);
	xhci_dbg(xhci, "HCC PARAMS 0x%x:\n", (unsigned int) temp);
	xhci_dbg(xhci, "  HC generates %s bit addresses\n",
			HCC_64BIT_ADDR(temp) ? "64" : "32");
	xhci_dbg(xhci, "  HC %s Contiguous Frame ID Capability\n",
			HCC_CFC(temp) ? "has" : "hasn't");
	xhci_dbg(xhci, "  HC %s generate Stopped - Short Package event\n",
			HCC_SPC(temp) ? "can" : "can't");
	/* FIXME */
	xhci_dbg(xhci, "  FIXME: more HCCPARAMS debugging\n");

	temp = readl(&xhci->cap_regs->run_regs_off);
	xhci_dbg(xhci, "RTSOFF 0x%x:\n", temp & RTSOFF_MASK);

	/* xhci 1.1 controllers have the HCCPARAMS2 register */
	if (hci_version > 0x100) {
		temp = readl(&xhci->cap_regs->hcc_params2);
		xhci_dbg(xhci, "HCC PARAMS2 0x%x:\n", (unsigned int) temp);
		xhci_dbg(xhci, "  HC %s Force save context capability",
			 HCC2_FSC(temp) ? "supports" : "doesn't support");
		xhci_dbg(xhci, "  HC %s Large ESIT Payload Capability",
			 HCC2_LEC(temp) ? "supports" : "doesn't support");
		xhci_dbg(xhci, "  HC %s Extended TBC capability",
			 HCC2_ETC(temp) ? "supports" : "doesn't support");
	}
}

static void xhci_print_command_reg(struct xhci_hcd *xhci)
{
	u32 temp;

	temp = readl(&xhci->op_regs->command);
	xhci_dbg(xhci, "USBCMD 0x%x:\n", temp);
	xhci_dbg(xhci, "  HC is %s\n",
			(temp & CMD_RUN) ? "running" : "being stopped");
	xhci_dbg(xhci, "  HC has %sfinished hard reset\n",
			(temp & CMD_RESET) ? "not " : "");
	xhci_dbg(xhci, "  Event Interrupts %s\n",
			(temp & CMD_EIE) ? "enabled " : "disabled");
	xhci_dbg(xhci, "  Host System Error Interrupts %s\n",
			(temp & CMD_HSEIE) ? "enabled " : "disabled");
	xhci_dbg(xhci, "  HC has %sfinished light reset\n",
			(temp & CMD_LRESET) ? "not " : "");
}

static void xhci_print_status(struct xhci_hcd *xhci)
{
	u32 temp;

	temp = readl(&xhci->op_regs->status);
	xhci_dbg(xhci, "USBSTS 0x%x:\n", temp);
	xhci_dbg(xhci, "  Event ring is %sempty\n",
			(temp & STS_EINT) ? "not " : "");
	xhci_dbg(xhci, "  %sHost System Error\n",
			(temp & STS_FATAL) ? "WARNING: " : "No ");
	xhci_dbg(xhci, "  HC is %s\n",
			(temp & STS_HALT) ? "halted" : "running");
}

static void xhci_print_op_regs(struct xhci_hcd *xhci)
{
	xhci_dbg(xhci, "xHCI operational registers at %p:\n", xhci->op_regs);
	xhci_print_command_reg(xhci);
	xhci_print_status(xhci);
}

static void xhci_print_ports(struct xhci_hcd *xhci)
{
	__le32 __iomem *addr;
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
			xhci_dbg(xhci, "%p port %s reg = 0x%x\n",
					addr, names[j],
					(unsigned int) readl(addr));
			addr++;
		}
	}
}

void xhci_print_ir_set(struct xhci_hcd *xhci, int set_num)
{
	struct xhci_intr_reg __iomem *ir_set = &xhci->run_regs->ir_set[set_num];
	void __iomem *addr;
	u32 temp;
	u64 temp_64;

	addr = &ir_set->irq_pending;
	temp = readl(addr);
	if (temp == XHCI_INIT_VALUE)
		return;

	xhci_dbg(xhci, "  %p: ir_set[%i]\n", ir_set, set_num);

	xhci_dbg(xhci, "  %p: ir_set.pending = 0x%x\n", addr,
			(unsigned int)temp);

	addr = &ir_set->irq_control;
	temp = readl(addr);
	xhci_dbg(xhci, "  %p: ir_set.control = 0x%x\n", addr,
			(unsigned int)temp);

	addr = &ir_set->erst_size;
	temp = readl(addr);
	xhci_dbg(xhci, "  %p: ir_set.erst_size = 0x%x\n", addr,
			(unsigned int)temp);

	addr = &ir_set->rsvd;
	temp = readl(addr);
	if (temp != XHCI_INIT_VALUE)
		xhci_dbg(xhci, "  WARN: %p: ir_set.rsvd = 0x%x\n",
				addr, (unsigned int)temp);

	addr = &ir_set->erst_base;
	temp_64 = xhci_read_64(xhci, addr);
	xhci_dbg(xhci, "  %p: ir_set.erst_base = @%08llx\n",
			addr, temp_64);

	addr = &ir_set->erst_dequeue;
	temp_64 = xhci_read_64(xhci, addr);
	xhci_dbg(xhci, "  %p: ir_set.erst_dequeue = @%08llx\n",
			addr, temp_64);
}

void xhci_print_run_regs(struct xhci_hcd *xhci)
{
	u32 temp;
	int i;

	xhci_dbg(xhci, "xHCI runtime registers at %p:\n", xhci->run_regs);
	temp = readl(&xhci->run_regs->microframe_index);
	xhci_dbg(xhci, "  %p: Microframe index = 0x%x\n",
			&xhci->run_regs->microframe_index,
			(unsigned int) temp);
	for (i = 0; i < 7; ++i) {
		temp = readl(&xhci->run_regs->rsvd[i]);
		if (temp != XHCI_INIT_VALUE)
			xhci_dbg(xhci, "  WARN: %p: Rsvd[%i] = 0x%x\n",
					&xhci->run_regs->rsvd[i],
					i, (unsigned int) temp);
	}
}

void xhci_print_registers(struct xhci_hcd *xhci)
{
	xhci_print_cap_regs(xhci);
	xhci_print_op_regs(xhci);
	xhci_print_ports(xhci);
}

void xhci_dbg_erst(struct xhci_hcd *xhci, struct xhci_erst *erst)
{
	u64 addr = erst->erst_dma_addr;
	int i;
	struct xhci_erst_entry *entry;

	for (i = 0; i < erst->num_entries; ++i) {
		entry = &erst->entries[i];
		xhci_dbg(xhci, "@%016llx %08x %08x %08x %08x\n",
			 addr,
			 lower_32_bits(le64_to_cpu(entry->seg_addr)),
			 upper_32_bits(le64_to_cpu(entry->seg_addr)),
			 le32_to_cpu(entry->seg_size),
			 le32_to_cpu(entry->rsvd));
		addr += sizeof(*entry);
	}
}

void xhci_dbg_cmd_ptrs(struct xhci_hcd *xhci)
{
	u64 val;

	val = xhci_read_64(xhci, &xhci->op_regs->cmd_ring);
	xhci_dbg(xhci, "// xHC command ring deq ptr low bits + flags = @%08x\n",
			lower_32_bits(val));
	xhci_dbg(xhci, "// xHC command ring deq ptr high bits = @%08x\n",
			upper_32_bits(val));
}

char *xhci_get_slot_state(struct xhci_hcd *xhci,
		struct xhci_container_ctx *ctx)
{
	struct xhci_slot_ctx *slot_ctx = xhci_get_slot_ctx(xhci, ctx);
	int state = GET_SLOT_STATE(le32_to_cpu(slot_ctx->dev_state));

	return xhci_slot_state_string(state);
}

void xhci_dbg_trace(struct xhci_hcd *xhci, void (*trace)(struct va_format *),
			const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	xhci_dbg(xhci, "%pV\n", &vaf);
	trace(&vaf);
	va_end(args);
}
EXPORT_SYMBOL_GPL(xhci_dbg_trace);
