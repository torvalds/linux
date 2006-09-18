/*
 * arch/arm/mach-iop32x/iq31244-pci.c
 *
 * PCI support for the Intel IQ31244 reference board
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
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

#define INTA	IRQ_IQ31244_INTA
#define INTB	IRQ_IQ31244_INTB
#define INTC	IRQ_IQ31244_INTC
#define INTD	IRQ_IQ31244_INTD

#define INTE	IRQ_IQ31244_I82546

static inline int __init
iq31244_map_irq(struct pci_dev *dev, u8 idsel, u8 pin)
{
	static int pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 * A       B       C       D
		 */
#ifdef CONFIG_ARCH_EP80219
		{INTB, INTB, INTB, INTB}, /* CFlash */
		{INTE, INTE, INTE, INTE}, /* 82551 Pro 100 */
		{INTD, INTD, INTD, INTD}, /* PCI-X Slot */
		{INTC, INTC, INTC, INTC}, /* SATA   */
#else
		{INTB, INTB, INTB, INTB}, /* CFlash */
		{INTC, INTC, INTC, INTC}, /* SATA   */
		{INTD, INTD, INTD, INTD}, /* PCI-X Slot */
		{INTE, INTE, INTE, INTE}, /* 82546 GigE */
#endif // CONFIG_ARCH_EP80219
	};

	BUG_ON(pin < 1 || pin > 4);

	return PCI_IRQ_TABLE_LOOKUP(0, 7);
}

static struct hw_pci iq31244_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iop3xx_pci_setup,
	.scan		= iop3xx_pci_scan_bus,
	.preinit	= iop3xx_pci_preinit,
	.map_irq	= iq31244_map_irq
};

static int __init iq31244_pci_init(void)
{
	if (machine_is_iq31244())
		pci_common_init(&iq31244_pci);
	return 0;
}

subsys_initcall(iq31244_pci_init);




