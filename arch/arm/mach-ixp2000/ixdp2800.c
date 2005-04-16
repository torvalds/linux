/*
 * arch/arm/mach-ixp2000/ixdp2800.c
 *
 * IXDP2800 platform support
 *
 * Original Author: Jeffrey Daly <jeffrey.daly@intel.com>
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
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>

#include <asm/mach/pci.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/flash.h>
#include <asm/mach/arch.h>


void ixdp2400_init_irq(void)
{
	ixdp2x00_init_irq(IXDP2800_CPLD_INT_STAT, IXDP2800_CPLD_INT_MASK, IXDP2400_NR_IRQS);
}

/*************************************************************************
 * IXDP2800 timer tick
 *************************************************************************/

static void __init ixdp2800_timer_init(void)
{
	ixp2000_init_time(50000000);
}

static struct sys_timer ixdp2800_timer = {
	.init		= ixdp2800_timer_init,
	.offset		= ixp2000_gettimeoffset,
};

/*************************************************************************
 * IXDP2800 PCI
 *************************************************************************/
void __init ixdp2800_pci_preinit(void)
{
	printk("ixdp2x00_pci_preinit called\n");

	*IXP2000_PCI_ADDR_EXT =  0x0000e000;

	*IXP2000_PCI_DRAM_BASE_ADDR_MASK = (0x40000000 - 1) & ~0xfffff;
	*IXP2000_PCI_SRAM_BASE_ADDR_MASK = (0x2000000 - 1) & ~0x3ffff;

	ixp2000_pci_preinit();
}

int ixdp2800_pci_setup(int nr, struct pci_sys_data *sys)
{
	sys->mem_offset = 0x00000000;

	ixp2000_pci_setup(nr, sys);

	return 1;
}

static int __init ixdp2800_pci_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (ixdp2x00_master_npu()) {

		/*
		 * Root bus devices.  Slave NPU is only one with interrupt.
		 * Everything else, we just return -1 which is invalid.
		 */
		if(!dev->bus->self) {
			if(dev->devfn == IXDP2X00_SLAVE_NPU_DEVFN )
				return IRQ_IXDP2800_INGRESS_NPU;

			return -1;
		}

		/*
		 * Bridge behind the PMC slot.
		 */
		if(dev->bus->self->devfn == IXDP2X00_PMC_DEVFN &&
			dev->bus->parent->self->devfn == IXDP2X00_P2P_DEVFN &&
			!dev->bus->parent->self->bus->parent)
				  return IRQ_IXDP2800_PMC;

		/*
		 * Device behind the first bridge
		 */
		if(dev->bus->self->devfn == IXDP2X00_P2P_DEVFN) {
			switch(dev->devfn) {
				case IXDP2X00_PMC_DEVFN:
					return IRQ_IXDP2800_PMC;	
			
				case IXDP2800_MASTER_ENET_DEVFN:
					return IRQ_IXDP2800_EGRESS_ENET;

				case IXDP2800_SWITCH_FABRIC_DEVFN:
					return IRQ_IXDP2800_FABRIC;
			}
		}

		return -1;
	} else return IRQ_IXP2000_PCIB; /* Slave NIC interrupt */
}

static void ixdp2800_pci_postinit(void)
{
	struct pci_dev *dev;

	if (ixdp2x00_master_npu()) {
		dev = pci_find_slot(1, IXDP2800_SLAVE_ENET_DEVFN);
		pci_remove_bus_device(dev);
	} else {
		dev = pci_find_slot(1, IXDP2800_MASTER_ENET_DEVFN);
		pci_remove_bus_device(dev);

		ixdp2x00_slave_pci_postinit();
	}
}

struct hw_pci ixdp2800_pci __initdata = {
	.nr_controllers	= 1,
	.setup		= ixdp2800_pci_setup,
	.preinit	= ixdp2800_pci_preinit,
	.postinit	= ixdp2800_pci_postinit,
	.scan		= ixp2000_pci_scan_bus,
	.map_irq	= ixdp2800_pci_map_irq,
};

int __init ixdp2800_pci_init(void)
{
	if (machine_is_ixdp2800())
		pci_common_init(&ixdp2800_pci);

	return 0;
}

subsys_initcall(ixdp2800_pci_init);

void ixdp2800_init_irq(void)
{
	ixdp2x00_init_irq(IXDP2800_CPLD_INT_STAT, IXDP2800_CPLD_INT_MASK, IXDP2800_NR_IRQS);
}

MACHINE_START(IXDP2800, "Intel IXDP2800 Development Platform")
	MAINTAINER("MontaVista Software, Inc.")
	BOOT_MEM(0x00000000, IXP2000_UART_PHYS_BASE, IXP2000_UART_VIRT_BASE)
	BOOT_PARAMS(0x00000100)
	MAPIO(ixdp2x00_map_io)
	INITIRQ(ixdp2800_init_irq)
	.timer		= &ixdp2800_timer,
	INIT_MACHINE(ixdp2x00_init_machine)
MACHINE_END

