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
}
