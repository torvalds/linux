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

#include <linux/usb.h>

#include "xhci.h"

void xhci_mem_cleanup(struct xhci_hcd *xhci)
{
	xhci->page_size = 0;
	xhci->page_shift = 0;
}

int xhci_mem_init(struct xhci_hcd *xhci, gfp_t flags)
{
	unsigned int	val, val2;
	u32 page_size;
	int i;

	page_size = xhci_readl(xhci, &xhci->op_regs->page_size);
	xhci_dbg(xhci, "Supported page size register = 0x%x\n", page_size);
	for (i = 0; i < 16; i++) {
		if ((0x1 & page_size) != 0)
			break;
		page_size = page_size >> 1;
	}
	if (i < 16)
		xhci_dbg(xhci, "Supported page size of %iK\n", (1 << (i+12)) / 1024);
	else
		xhci_warn(xhci, "WARN: no supported page size\n");
	/* Use 4K pages, since that's common and the minimum the HC supports */
	xhci->page_shift = 12;
	xhci->page_size = 1 << xhci->page_shift;
	xhci_dbg(xhci, "HCD page size set to %iK\n", xhci->page_size / 1024);

	/*
	 * Program the Number of Device Slots Enabled field in the CONFIG
	 * register with the max value of slots the HC can handle.
	 */
	val = HCS_MAX_SLOTS(xhci_readl(xhci, &xhci->cap_regs->hcs_params1));
	xhci_dbg(xhci, "// xHC can handle at most %d device slots.\n",
			(unsigned int) val);
	val2 = xhci_readl(xhci, &xhci->op_regs->config_reg);
	val |= (val2 & ~HCS_SLOTS_MASK);
	xhci_dbg(xhci, "// Setting Max device slots reg = 0x%x.\n",
			(unsigned int) val);
	xhci_writel(xhci, val, &xhci->op_regs->config_reg);

	xhci->ir_set = &xhci->run_regs->ir_set[0];

	return 0;
fail:
	xhci_warn(xhci, "Couldn't initialize memory\n");
	xhci_mem_cleanup(xhci);
	return -ENOMEM;
}
