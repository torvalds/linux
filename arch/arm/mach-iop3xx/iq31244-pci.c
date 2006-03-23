/*
 * arch/arm/mach-iop3xx/iq80321-pci.c
 *
 * PCI support for the Intel IQ80321 reference board
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

static int iq31244_setup(int nr, struct pci_sys_data *sys)
{
	struct resource *res;

	if(nr != 0)
		return 0;

	res = kzalloc(sizeof(struct resource) * 2, GFP_KERNEL);
	if (!res)
		panic("PCI: unable to alloc resources");

	res[0].start = IOP321_PCI_LOWER_IO_VA;
	res[0].end   = IOP321_PCI_UPPER_IO_VA;
	res[0].name  = "IQ31244 PCI I/O Space";
	res[0].flags = IORESOURCE_IO;

	res[1].start = IOP321_PCI_LOWER_MEM_PA;
	res[1].end   = IOP321_PCI_UPPER_MEM_PA;
	res[1].name  = "IQ31244 PCI Memory Space";
	res[1].flags = IORESOURCE_MEM;

	request_resource(&ioport_resource, &res[0]);
	request_resource(&iomem_resource, &res[1]);

	sys->mem_offset = IOP321_PCI_MEM_OFFSET;
	sys->io_offset  = IOP321_PCI_IO_OFFSET;

	sys->resource[0] = &res[0];
	sys->resource[1] = &res[1];
	sys->resource[2] = NULL;

	return 1;
}

static void iq31244_preinit(void)
{
	iop321_init();
}

static struct hw_pci iq31244_pci __initdata = {
	.swizzle	= pci_std_swizzle,
	.nr_controllers = 1,
	.setup		= iq31244_setup,
	.scan		= iop321_scan_bus,
	.preinit	= iq31244_preinit,
	.map_irq	= iq31244_map_irq
};

static int __init iq31244_pci_init(void)
{
	if (machine_is_iq31244())
		pci_common_init(&iq31244_pci);
	return 0;
}

subsys_initcall(iq31244_pci_init);




