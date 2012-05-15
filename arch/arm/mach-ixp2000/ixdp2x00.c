/*
 * arch/arm/mach-ixp2000/ixdp2x00.c
 *
 * Code common to IXDP2400 and IXDP2800 platforms.
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
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
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

#include <mach/gpio-ixp2000.h>

/*************************************************************************
 * IXDP2x00 IRQ Initialization
 *************************************************************************/
static volatile unsigned long *board_irq_mask;
static volatile unsigned long *board_irq_stat;
static unsigned long board_irq_count;

#ifdef CONFIG_ARCH_IXDP2400
/*
 * Slowport configuration for accessing CPLD registers on IXDP2x00
 */
static struct slowport_cfg slowport_cpld_cfg = {
	.CCR =	SLOWPORT_CCR_DIV_2,
	.WTC = 0x00000070,
	.RTC = 0x00000070,
	.PCR = SLOWPORT_MODE_FLASH,
	.ADC = SLOWPORT_ADDR_WIDTH_24 | SLOWPORT_DATA_WIDTH_8
};
#endif

static void ixdp2x00_irq_mask(struct irq_data *d)
{
	unsigned long dummy;
	static struct slowport_cfg old_cfg;

	/*
	 * This is ugly in common code but really don't know
	 * of a better way to handle it. :(
	 */
#ifdef CONFIG_ARCH_IXDP2400
	if (machine_is_ixdp2400())
		ixp2000_acquire_slowport(&slowport_cpld_cfg, &old_cfg);
#endif

	dummy = *board_irq_mask;
	dummy |=  IXP2000_BOARD_IRQ_MASK(d->irq);
	ixp2000_reg_wrb(board_irq_mask, dummy);

#ifdef CONFIG_ARCH_IXDP2400
	if (machine_is_ixdp2400())
		ixp2000_release_slowport(&old_cfg);
#endif
}

static void ixdp2x00_irq_unmask(struct irq_data *d)
{
	unsigned long dummy;
	static struct slowport_cfg old_cfg;

#ifdef CONFIG_ARCH_IXDP2400
	if (machine_is_ixdp2400())
		ixp2000_acquire_slowport(&slowport_cpld_cfg, &old_cfg);
#endif

	dummy = *board_irq_mask;
	dummy &=  ~IXP2000_BOARD_IRQ_MASK(d->irq);
	ixp2000_reg_wrb(board_irq_mask, dummy);

	if (machine_is_ixdp2400()) 
		ixp2000_release_slowport(&old_cfg);
}

static void ixdp2x00_irq_handler(unsigned int irq, struct irq_desc *desc)
{
        volatile u32 ex_interrupt = 0;
	static struct slowport_cfg old_cfg;
	int i;

	desc->irq_data.chip->irq_mask(&desc->irq_data);

#ifdef CONFIG_ARCH_IXDP2400
	if (machine_is_ixdp2400())
		ixp2000_acquire_slowport(&slowport_cpld_cfg, &old_cfg);
#endif
        ex_interrupt = *board_irq_stat & 0xff;
	if (machine_is_ixdp2400())
		ixp2000_release_slowport(&old_cfg);

	if(!ex_interrupt) {
		printk(KERN_ERR "Spurious IXDP2x00 CPLD interrupt!\n");
		return;
	}

	for(i = 0; i < board_irq_count; i++) {
		if(ex_interrupt & (1 << i))  {
			int cpld_irq = IXP2000_BOARD_IRQ(0) + i;
			generic_handle_irq(cpld_irq);
		}
	}

	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

static struct irq_chip ixdp2x00_cpld_irq_chip = {
	.irq_ack	= ixdp2x00_irq_mask,
	.irq_mask	= ixdp2x00_irq_mask,
	.irq_unmask	= ixdp2x00_irq_unmask
};

void __init ixdp2x00_init_irq(volatile unsigned long *stat_reg, volatile unsigned long *mask_reg, unsigned long nr_of_irqs)
{
	unsigned int irq;

	ixp2000_init_irq();
	
	if (!ixdp2x00_master_npu())
		return;

	board_irq_stat = stat_reg;
	board_irq_mask = mask_reg;
	board_irq_count = nr_of_irqs;

	*board_irq_mask = 0xffffffff;

	for(irq = IXP2000_BOARD_IRQ(0); irq < IXP2000_BOARD_IRQ(board_irq_count); irq++) {
		irq_set_chip_and_handler(irq, &ixdp2x00_cpld_irq_chip,
					 handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	/* Hook into PCI interrupt */
	irq_set_chained_handler(IRQ_IXP2000_PCIB, ixdp2x00_irq_handler);
}

/*************************************************************************
 * IXDP2x00 memory map
 *************************************************************************/
static struct map_desc ixdp2x00_io_desc __initdata = {
	.virtual	= IXDP2X00_VIRT_CPLD_BASE, 
	.pfn		= __phys_to_pfn(IXDP2X00_PHYS_CPLD_BASE),
	.length		= IXDP2X00_CPLD_SIZE,
	.type		= MT_DEVICE
};

void __init ixdp2x00_map_io(void)
{
	ixp2000_map_io();	

	iotable_init(&ixdp2x00_io_desc, 1);
}

/*************************************************************************
 * IXDP2x00-common PCI init
 *
 * The IXDP2[48]00 has a horrid PCI bus layout. Basically the board 
 * contains two NPUs (ingress and egress) connected over PCI,  both running 
 * instances  of the kernel. So far so good. Peers on the PCI bus running 
 * Linux is a common design in telecom systems. The problem is that instead 
 * of all the devices being controlled by a single host, different
 * devices are controlled by different NPUs on the same bus, leading to
 * multiple hosts on the bus. The exact bus layout looks like:
 *
 *                   Bus 0
 *    Master NPU <-------------------+-------------------> Slave NPU
 *                                   |
 *                                   |
 *                                  P2P 
 *                                   |
 *
 *                  Bus 1            |
 *               <--+------+---------+---------+------+-->
 *                  |      |         |         |      |
 *                  |      |         |         |      |
 *             ... Dev    PMC       Media     Eth0   Eth1 ...
 *
 * The master controls all but Eth1, which is controlled by the
 * slave. What this means is that the both the master and the slave
 * have to scan the bus, but only one of them can enumerate the bus.
 * In addition, after the bus is scanned, each kernel must remove
 * the device(s) it does not control from the PCI dev list otherwise
 * a driver on each NPU will try to manage it and we will have horrible
 * conflicts. Oh..and the slave NPU needs to see the master NPU
 * for Intel's drivers to work properly. Closed source drivers...
 *
 * The way we deal with this is fairly simple but ugly:
 *
 * 1) Let master scan and enumerate the bus completely.
 * 2) Master deletes Eth1 from device list.
 * 3) Slave scans bus and then deletes all but Eth1 (Eth0 on slave)
 *    from device list.
 * 4) Find HW designers and LART them.
 *
 * The boards also do not do normal PCI IRQ routing, or any sort of 
 * sensical  swizzling, so we just need to check where on the  bus a
 * device sits and figure out to which CPLD pin the interrupt is routed.
 * See ixdp2[48]00.c files.
 *
 *************************************************************************/
void ixdp2x00_slave_pci_postinit(void)
{
	struct pci_dev *dev;

	/*
	 * Remove PMC device is there is one
	 */
	if((dev = pci_get_bus_and_slot(1, IXDP2X00_PMC_DEVFN))) {
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);
	}

	dev = pci_get_bus_and_slot(0, IXDP2X00_21555_DEVFN);
	pci_stop_and_remove_bus_device(dev);
	pci_dev_put(dev);
}

/**************************************************************************
 * IXDP2x00 Machine Setup
 *************************************************************************/
static struct flash_platform_data ixdp2x00_platform_data = {
	.map_name	= "cfi_probe",
	.width		= 1,
};

static struct ixp2000_flash_data ixdp2x00_flash_data = {
	.platform_data	= &ixdp2x00_platform_data,
	.nr_banks	= 1
};

static struct resource ixdp2x00_flash_resource = {
	.start		= 0xc4000000,
	.end		= 0xc4000000 + 0x00ffffff,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device ixdp2x00_flash = {
	.name		= "IXP2000-Flash",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp2x00_flash_data,
	},
	.num_resources	= 1,
	.resource	= &ixdp2x00_flash_resource,
};

static struct ixp2000_i2c_pins ixdp2x00_i2c_gpio_pins = {
	.sda_pin	= IXDP2X00_GPIO_SDA,
	.scl_pin	= IXDP2X00_GPIO_SCL,
};

static struct platform_device ixdp2x00_i2c_controller = {
	.name		= "IXP2000-I2C",
	.id		= 0,
	.dev		= {
		.platform_data = &ixdp2x00_i2c_gpio_pins,
	},
	.num_resources	= 0
};

static struct platform_device *ixdp2x00_devices[] __initdata = {
	&ixdp2x00_flash,
	&ixdp2x00_i2c_controller
};

void __init ixdp2x00_init_machine(void)
{
	gpio_line_set(IXDP2X00_GPIO_I2C_ENABLE, 1);
	gpio_line_config(IXDP2X00_GPIO_I2C_ENABLE, GPIO_OUT);

	platform_add_devices(ixdp2x00_devices, ARRAY_SIZE(ixdp2x00_devices));
	ixp2000_uart_init();
}

