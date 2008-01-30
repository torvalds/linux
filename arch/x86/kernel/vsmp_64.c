/*
 * vSMPowered(tm) systems specific initialization
 * Copyright (C) 2005 ScaleMP Inc.
 *
 * Use of this code is subject to the terms and conditions of the
 * GNU general public license version 2. See "COPYING" or
 * http://www.gnu.org/licenses/gpl.html
 *
 * Ravikiran Thirumalai <kiran@scalemp.com>,
 * Shai Fultheim <shai@scalemp.com>
 */

#include <linux/init.h>
#include <linux/pci_ids.h>
#include <linux/pci_regs.h>
#include <asm/pci-direct.h>
#include <asm/io.h>

static int __init vsmp_init(void)
{
	void *address;
	unsigned int cap, ctl;

	if (!early_pci_allowed())
		return 0;

	/* Check if we are running on a ScaleMP vSMP box */
	if ((read_pci_config_16(0, 0x1f, 0, PCI_VENDOR_ID) !=
	     PCI_VENDOR_ID_SCALEMP) ||
	    (read_pci_config_16(0, 0x1f, 0, PCI_DEVICE_ID) !=
	     PCI_DEVICE_ID_SCALEMP_VSMP_CTL))
		return 0;

	/* set vSMP magic bits to indicate vSMP capable kernel */
	address = ioremap(read_pci_config(0, 0x1f, 0, PCI_BASE_ADDRESS_0), 8);
	cap = readl(address);
	ctl = readl(address + 4);
	printk(KERN_INFO "vSMP CTL: capabilities:0x%08x  control:0x%08x\n",
	       cap, ctl);
	if (cap & ctl & (1 << 4)) {
		/* Turn on vSMP IRQ fastpath handling (see system.h) */
		ctl &= ~(1 << 4);
		writel(ctl, address + 4);
		ctl = readl(address + 4);
		printk(KERN_INFO "vSMP CTL: control set to:0x%08x\n", ctl);
	}

	iounmap(address);
	return 0;
}

core_initcall(vsmp_init);
