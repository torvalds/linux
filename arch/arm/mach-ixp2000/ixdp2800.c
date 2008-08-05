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
#include <mach/hardware.h>
#include <asm/mach-types.h>

#include <asm/mach/pci.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/flash.h>
#include <asm/mach/arch.h>

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
static void __init ixdp2800_slave_disable_pci_master(void)
{
	*IXP2000_PCI_CMDSTAT &= ~(PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
}

static void __init ixdp2800_master_wait_for_slave(void)
{
	volatile u32 *addr;

	printk(KERN_INFO "IXDP2800: waiting for slave NPU to configure "
			 "its BAR sizes\n");

	addr = ixp2000_pci_config_addr(0, IXDP2X00_SLAVE_NPU_DEVFN,
					PCI_BASE_ADDRESS_1);
	do {
		*addr = 0xffffffff;
		cpu_relax();
	} while (*addr != 0xfe000008);

	addr = ixp2000_pci_config_addr(0, IXDP2X00_SLAVE_NPU_DEVFN,
					PCI_BASE_ADDRESS_2);
	do {
		*addr = 0xffffffff;
		cpu_relax();
	} while (*addr != 0xc0000008);

	/*
	 * Configure the slave's SDRAM BAR by hand.
	 */
	*addr = 0x40000008;
}

static void __init ixdp2800_slave_wait_for_master_enable(void)
{
	printk(KERN_INFO "IXDP2800: waiting for master NPU to enable us\n");

	while ((*IXP2000_PCI_CMDSTAT & PCI_COMMAND_MASTER) == 0)
		cpu_relax();
}

void __init ixdp2800_pci_preinit(void)
{
	printk("ixdp2x00_pci_preinit called\n");

	*IXP2000_PCI_ADDR_EXT = 0x0001e000;

	if (!ixdp2x00_master_npu())
		ixdp2800_slave_disable_pci_master();

	*IXP2000_PCI_SRAM_BASE_ADDR_MASK = (0x2000000 - 1) & ~0x3ffff;
	*IXP2000_PCI_DRAM_BASE_ADDR_MASK = (0x40000000 - 1) & ~0xfffff;

	ixp2000_pci_preinit();

	if (ixdp2x00_master_npu()) {
		/*
		 * Wait until the slave set its SRAM/SDRAM BAR sizes
		 * correctly before we proceed to scan and enumerate
		 * the bus.
		 */
		ixdp2800_master_wait_for_slave();

		/*
		 * We configure the SDRAM BARs by hand because they
		 * are 1G and fall outside of the regular allocated
		 * PCI address space.
		 */
		*IXP2000_PCI_SDRAM_BAR = 0x00000008;
	} else {
		/*
		 * Wait for the master to complete scanning the bus
		 * and assigning resources before we proceed to scan
		 * the bus ourselves.  Set pci=firmware to honor the
		 * master's resource assignment.
		 */
		ixdp2800_slave_wait_for_master_enable();
		pcibios_setup("firmware");
	}
}

/*
 * We assign the SDRAM BARs for the two IXP2800 CPUs by hand, outside
 * of the regular PCI window, because there's only 512M of outbound PCI
 * memory window on each IXP, while we need 1G for each of the BARs.
 */
static void __devinit ixp2800_pci_fixup(struct pci_dev *dev)
{
	if (machine_is_ixdp2800()) {
		dev->resource[2].start = 0;
		dev->resource[2].end   = 0;
		dev->resource[2].flags = 0;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_IXP2800, ixp2800_pci_fixup);

static int __init ixdp2800_pci_setup(int nr, struct pci_sys_data *sys)
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

static void __init ixdp2800_master_enable_slave(void)
{
	volatile u32 *addr;

	printk(KERN_INFO "IXDP2800: enabling slave NPU\n");

	addr = (volatile u32 *)ixp2000_pci_config_addr(0,
					IXDP2X00_SLAVE_NPU_DEVFN,
					PCI_COMMAND);

	*addr |= PCI_COMMAND_MASTER;
}

static void __init ixdp2800_master_wait_for_slave_bus_scan(void)
{
	volatile u32 *addr;

	printk(KERN_INFO "IXDP2800: waiting for slave to finish bus scan\n");

	addr = (volatile u32 *)ixp2000_pci_config_addr(0,
					IXDP2X00_SLAVE_NPU_DEVFN,
					PCI_COMMAND);
	while ((*addr & PCI_COMMAND_MEMORY) == 0)
		cpu_relax();
}

static void __init ixdp2800_slave_signal_bus_scan_completion(void)
{
	printk(KERN_INFO "IXDP2800: bus scan done, signaling master\n");
	*IXP2000_PCI_CMDSTAT |= PCI_COMMAND_MEMORY;
}

static void __init ixdp2800_pci_postinit(void)
{
	if (!ixdp2x00_master_npu()) {
		ixdp2x00_slave_pci_postinit();
		ixdp2800_slave_signal_bus_scan_completion();
	}
}

struct __initdata hw_pci ixdp2800_pci __initdata = {
	.nr_controllers	= 1,
	.setup		= ixdp2800_pci_setup,
	.preinit	= ixdp2800_pci_preinit,
	.postinit	= ixdp2800_pci_postinit,
	.scan		= ixp2000_pci_scan_bus,
	.map_irq	= ixdp2800_pci_map_irq,
};

int __init ixdp2800_pci_init(void)
{
	if (machine_is_ixdp2800()) {
		struct pci_dev *dev;

		pci_common_init(&ixdp2800_pci);
		if (ixdp2x00_master_npu()) {
			dev = pci_get_bus_and_slot(1, IXDP2800_SLAVE_ENET_DEVFN);
			pci_remove_bus_device(dev);
			pci_dev_put(dev);

			ixdp2800_master_enable_slave();
			ixdp2800_master_wait_for_slave_bus_scan();
		} else {
			dev = pci_get_bus_and_slot(1, IXDP2800_MASTER_ENET_DEVFN);
			pci_remove_bus_device(dev);
			pci_dev_put(dev);
		}
	}

	return 0;
}

subsys_initcall(ixdp2800_pci_init);

void __init ixdp2800_init_irq(void)
{
	ixdp2x00_init_irq(IXDP2800_CPLD_INT_STAT, IXDP2800_CPLD_INT_MASK, IXDP2800_NR_IRQS);
}

MACHINE_START(IXDP2800, "Intel IXDP2800 Development Platform")
	/* Maintainer: MontaVista Software, Inc. */
	.phys_io	= IXP2000_UART_PHYS_BASE,
	.io_pg_offst	= ((IXP2000_UART_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.map_io		= ixdp2x00_map_io,
	.init_irq	= ixdp2800_init_irq,
	.timer		= &ixdp2800_timer,
	.init_machine	= ixdp2x00_init_machine,
MACHINE_END

