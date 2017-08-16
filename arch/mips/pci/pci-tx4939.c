/*
 * Based on linux/arch/mips/txx9/rbtx4939/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * Copyright 2001, 2003-2005 MontaVista Software Inc.
 * Copyright (C) 2004 by Ralf Baechle (ralf@linux-mips.org)
 * (C) Copyright TOSHIBA CORPORATION 2000-2001, 2004-2007
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/tx4939.h>

int __init tx4939_report_pciclk(void)
{
	int pciclk = 0;

	pr_info("PCIC --%s PCICLK:",
		(__raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_PCI66) ?
		" PCI66" : "");
	if (__raw_readq(&tx4939_ccfgptr->pcfg) & TX4939_PCFG_PCICLKEN_ALL) {
		pciclk = txx9_master_clock * 20 / 6;
		if (!(__raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_PCI66))
			pciclk /= 2;
		pr_cont("Internal(%u.%uMHz)",
			(pciclk + 50000) / 1000000,
			((pciclk + 50000) / 100000) % 10);
	} else {
		pr_cont("External");
		pciclk = -1;
	}
	pr_cont("\n");
	return pciclk;
}

void __init tx4939_report_pci1clk(void)
{
	unsigned int pciclk = txx9_master_clock * 20 / 6;

	pr_info("PCIC1 -- PCICLK:%u.%uMHz\n",
		(pciclk + 50000) / 1000000,
		((pciclk + 50000) / 100000) % 10);
}

int __init tx4939_pcic1_map_irq(const struct pci_dev *dev, u8 slot)
{
	if (get_tx4927_pcicptr(dev->bus->sysdata) == tx4939_pcic1ptr) {
		switch (slot) {
		case TX4927_PCIC_IDSEL_AD_TO_SLOT(31):
			if (__raw_readq(&tx4939_ccfgptr->pcfg) &
			    TX4939_PCFG_ET0MODE)
				return TXX9_IRQ_BASE + TX4939_IR_ETH(0);
			break;
		case TX4927_PCIC_IDSEL_AD_TO_SLOT(30):
			if (__raw_readq(&tx4939_ccfgptr->pcfg) &
			    TX4939_PCFG_ET1MODE)
				return TXX9_IRQ_BASE + TX4939_IR_ETH(1);
			break;
		}
		return 0;
	}
	return -1;
}

int __init tx4939_pci_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	int irq = tx4939_pcic1_map_irq(dev, slot);

	if (irq >= 0)
		return irq;
	irq = pin;
	/* IRQ rotation */
	irq--;	/* 0-3 */
	irq = (irq + 33 - slot) % 4;
	irq++;	/* 1-4 */

	switch (irq) {
	case 1:
		irq = TXX9_IRQ_BASE + TX4939_IR_INTA;
		break;
	case 2:
		irq = TXX9_IRQ_BASE + TX4939_IR_INTB;
		break;
	case 3:
		irq = TXX9_IRQ_BASE + TX4939_IR_INTC;
		break;
	case 4:
		irq = TXX9_IRQ_BASE + TX4939_IR_INTD;
		break;
	}
	return irq;
}

void __init tx4939_setup_pcierr_irq(void)
{
	if (request_irq(TXX9_IRQ_BASE + TX4939_IR_PCIERR,
			tx4927_pcierr_interrupt,
			0, "PCI error",
			(void *)TX4939_PCIC_REG))
		pr_warn("Failed to request irq for PCIERR\n");
}
