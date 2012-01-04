/*
 * Pb1550 board platform device registration
 *
 * Copyright (C) 2009 Manuel Lauss
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-pb1x00/pb1550.h>
#include <asm/mach-db1x00/bcsr.h>

#include "../platform.h"

static int pb1550_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 12) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 12) {
		switch (pin) {
		case 1: return AU1500_PCI_INTB;
		case 2: return AU1500_PCI_INTC;
		case 3: return AU1500_PCI_INTD;
		case 4: return AU1500_PCI_INTA;
		}
	}
	if (slot == 13) {
		switch (pin) {
		case 1: return AU1500_PCI_INTA;
		case 2: return AU1500_PCI_INTB;
		case 3: return AU1500_PCI_INTC;
		case 4: return AU1500_PCI_INTD;
		}
	}
	return -1;
}

static struct resource alchemy_pci_host_res[] = {
	[0] = {
		.start	= AU1500_PCI_PHYS_ADDR,
		.end	= AU1500_PCI_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct alchemy_pci_platdata pb1550_pci_pd = {
	.board_map_irq	= pb1550_map_pci_irq,
};

static struct platform_device pb1550_pci_host = {
	.dev.platform_data = &pb1550_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

static struct resource au1550_psc2_res[] = {
	[0] = {
		.start	= AU1550_PSC2_PHYS_ADDR,
		.end	= AU1550_PSC2_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1550_PSC2_INT,
		.end	= AU1550_PSC2_INT,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= AU1550_DSCR_CMD0_PSC2_TX,
		.end	= AU1550_DSCR_CMD0_PSC2_TX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= AU1550_DSCR_CMD0_PSC2_RX,
		.end	= AU1550_DSCR_CMD0_PSC2_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device pb1550_i2c_dev = {
	.name		= "au1xpsc_smbus",
	.id		= 0,	/* bus number */
	.num_resources	= ARRAY_SIZE(au1550_psc2_res),
	.resource	= au1550_psc2_res,
};

static int __init pb1550_dev_init(void)
{
	int swapped;

	/* Pb1550, like all others, also has statuschange irqs; however they're
	* wired up on one of the Au1550's shared GPIO201_205 line, which also
	* services the PCMCIA card interrupts.  So we ignore statuschange and
	* use the GPIO201_205 exclusively for card interrupts, since a) pcmcia
	* drivers are used to shared irqs and b) statuschange isn't really use-
	* ful anyway.
	*/
	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		AU1550_GPIO201_205_INT, AU1550_GPIO0_INT, 0, 0, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x008000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x008400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x008000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x008400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x008000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x008010000 - 1,
		AU1550_GPIO201_205_INT, AU1550_GPIO1_INT, 0, 0, 1);

	swapped = bcsr_read(BCSR_STATUS) & BCSR_STATUS_PB1550_SWAPBOOT;
	db1x_register_norflash(128 * 1024 * 1024, 4, swapped);
	platform_device_register(&pb1550_pci_host);
	platform_device_register(&pb1550_i2c_dev);

	return 0;
}
arch_initcall(pb1550_dev_init);
