/*
 * arch/arm/mach-ixp2000/ixdp2400.c
 *
 * IXDP2400 platform support
 *
 * Original Author: Naeem Afzal <naeem.m.afzal@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>

#include <asm/mach/pci.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/flash.h>
#include <asm/mach/arch.h>

/*************************************************************************
 * IXDP2400 timer tick
 *************************************************************************/
static void __init ixdp2400_timer_init(void)
{
	int numerator, denominator;
	int denom_array[] = {2, 4, 8, 16, 1, 2, 4, 8};

	numerator = (*(IXDP2400_CPLD_SYS_CLK_M) & 0xFF) *2;
	denominator = denom_array[(*(IXDP2400_CPLD_SYS_CLK_N) & 0x7)];

	ixp2000_init_time(((3125000 * numerator) / (denominator)) / 2);
}

static struct sys_timer ixdp2400_timer = {
	.init		= ixdp2400_timer_init,
	.offset		= ixp2000_gettimeoffset,
};

/*************************************************************************
 * IXDP2400 PCI
 *************************************************************************/
void __init ixdp2400_pci_preinit(void)
{
	ixp2000_reg_write(IXP2000_PCI_ADDR_EXT, 0x00100000);
	ixp2000_pci_preinit();
	pcibios_setup("firmware");
}

int ixdp2400_pci_setup(int nr, struct pci_sys_data *sys)
{
	sys->mem_offset = 0xe0000000;

	ixp2000_pci_setup(nr, sys);

	return 1;
}

static int __init ixdp2400_pci_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	if (ixdp2x00_master_npu()) {

		/*
		 * Root bus devices.  Slave NPU is only one with interrupt.
		 * Everything else, we just return -1 b/c nothing else
		 * on the root bus has interrupts.
		 */
		if(!dev->bus->self) {
			if(dev->devfn == IXDP2X00_SLAVE_NPU_DEVFN )
				return IRQ_IXDP2400_INGRESS_NPU;

			return -1;
		}

		/*
		 * Bridge behind the PMC slot.
		 * NOTE: Only INTA from the PMC slot is routed. VERY BAD.
		 */
		if(dev->bus->self->devfn == IXDP2X00_PMC_DEVFN &&
			dev->bus->parent->self->devfn == IXDP2X00_P2P_DEVFN &&
			!dev->bus->parent->self->bus->parent)
				  return IRQ_IXDP2400_PMC;

		/*
		 * Device behind the first bridge
		 */
		if(dev->bus->self->devfn == IXDP2X00_P2P_DEVFN) {
			switch(dev->devfn) {
				case IXDP2400_MASTER_ENET_DEVFN:	
					return IRQ_IXDP2400_ENET;	
			
				case IXDP2400_MEDIA_DEVFN:
					return IRQ_IXDP2400_MEDIA_PCI;

				case IXDP2400_SWITCH_FABRIC_DEVFN:
					return IRQ_IXDP2400_SF_PCI;

				case IXDP2X00_PMC_DEVFN:
					return IRQ_IXDP2400_PMC;
			}
		}

		return -1;
	} else return IRQ_IXP2000_PCIB; /* Slave NIC interrupt */
}


static void ixdp2400_pci_postinit(void)
{
	struct pci_dev *dev;

	if (ixdp2x00_master_npu()) {
		dev = pci_get_bus_and_slot(1, IXDP2400_SLAVE_ENET_DEVFN);
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);
	} else {
		dev = pci_get_bus_and_slot(1, IXDP2400_MASTER_ENET_DEVFN);
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);

		ixdp2x00_slave_pci_postinit();
	}
}

static struct hw_pci ixdp2400_pci __initdata = {
	.nr_controllers	= 1,
	.setup		= ixdp2400_pci_setup,
	.preinit	= ixdp2400_pci_preinit,
	.postinit	= ixdp2400_pci_postinit,
	.scan		= ixp2000_pci_scan_bus,
	.map_irq	= ixdp2400_pci_map_irq,
};

int __init ixdp2400_pci_init(void)
{
	if (machine_is_ixdp2400())
		pci_common_init(&ixdp2400_pci);

	return 0;
}

subsys_initcall(ixdp2400_pci_init);

void __init ixdp2400_init_irq(void)
{
	ixdp2x00_init_irq(IXDP2400_CPLD_INT_STAT, IXDP2400_CPLD_INT_MASK, IXDP2400_NR_IRQS);
}

MACHINE_START(IXDP2400, "Intel IXDP2400 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.atag_offset	= 0x100,
	.map_io		= ixdp2x00_map_io,
	.init_irq	= ixdp2400_init_irq,
	.timer		= &ixdp2400_timer,
	.init_machine	= ixdp2x00_init_machine,
	.restart	= ixp2000_restart,
MACHINE_END

