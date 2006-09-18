/*
 * arch/arm/mach-iop33x/iq80332-pci.c
 *
 * PCI support for the Intel IQ80332 reference board
 *
 * Author: Dave Jiang <dave.jiang@intel.com>
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <asm/mach-types.h>

/*
 * The following macro is used to lookup irqs in a standard table
 * format for those systems that do not already have PCI
 * interrupts properly routed.  We assume 1 <= pin <= 4
 */
#define PCI_IRQ_TABLE_LOOKUP(minid,maxid)	\
({ int _ctl_ = -1;				\
   unsigned int _idsel = idsel - minid;		\
   if (_idsel <= maxid)				\
      _ctl_ = pci_irq_table[_idsel][pin-1];	\
   _ctl_; })

#define INTA	IRQ_IQ80332_INTA
#define INTB	IRQ_IQ80332_INTB
#define INTC	IRQ_IQ80332_INTC
#define INTD	IRQ_IQ80332_INTD

//#define INTE	IRQ_IQ80332_I82544

static inline int __init
iq80332_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	static int pci_irq_table[][8] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 * A       B       C       D
		 */
		{-1,   -1,   -1,   -1},
		{-1,   -1,   -1,   -1},
		{-1,   -1,   -1,   -1},
		{INTA, INTB, INTC, INTD}, /* PCI-X Slot */
		{-1,   -1,   -1,   -1},
		{INTC, INTC, INTC, INTC}, /* GigE  */
		{-1,   -1,   -1,   -1},
		{-1,   -1,   -1,   -1},
	};

	BUG_ON(pin < 1 || pin > 4);

	return PCI_IRQ_TABLE_LOOKUP(1, 7);
}

static struct hw_pci iq80332_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.scan		= iop3xx_pci_scan_bus,
	.preinit	= iop3xx_pci_preinit,
	.map_irq	= iq80332_map_irq
};

static int __init iq80332_pci_init(void)
{
	if (machine_is_iq80332())
		pci_common_init(&iq80332_pci);
	return 0;
}

subsys_initcall(iq80332_pci_init);




