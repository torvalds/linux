/*
 * Pb1550 board support.
 *
 * Copyright (C) 2009-2011 Manuel Lauss
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
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1550nd.h>
#include <asm/mach-au1x00/gpio.h>
#include <asm/mach-db1x00/bcsr.h>
#include "platform.h"

const char *get_system_type(void)
{
	return "PB1550";
}

void __init board_setup(void)
{
	u32 pin_func;

	bcsr_init(PB1550_BCSR_PHYS_ADDR,
		  PB1550_BCSR_PHYS_ADDR + PB1550_BCSR_HEXLED_OFS);

	alchemy_gpio2_enable();

	/*
	 * Enable PSC1 SYNC for AC'97.  Normaly done in audio driver,
	 * but it is board specific code, so put it here.
	 */
	pin_func = au_readl(SYS_PINFUNC);
	au_sync();
	pin_func |= SYS_PF_MUST_BE_SET | SYS_PF_PSC1_S1;
	au_writel(pin_func, SYS_PINFUNC);

	bcsr_write(BCSR_PCMCIA, 0);	/* turn off PCMCIA power */

	printk(KERN_INFO "AMD Alchemy Pb1550 Board\n");
}

/******************************************************************************/

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

static struct mtd_partition pb1550_nand_parts[] = {
	[0] = {
		.name	= "NAND FS 0",
		.offset	= 0,
		.size	= 8 * 1024 * 1024,
	},
	[1] = {
		.name	= "NAND FS 1",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct au1550nd_platdata pb1550_nand_pd = {
	.parts		= pb1550_nand_parts,
	.num_parts	= ARRAY_SIZE(pb1550_nand_parts),
	.devwidth	= 0,	/* x8 NAND default, needs fixing up */
};

static struct resource pb1550_nand_res[] = {
	[0] = {
		.start	= 0x20000000,
		.end	= 0x20000fff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device pb1550_nand_dev = {
	.name		= "au1550-nand",
	.id		= -1,
	.resource	= pb1550_nand_res,
	.num_resources	= ARRAY_SIZE(pb1550_nand_res),
	.dev		= {
		.platform_data	= &pb1550_nand_pd,
	},
};

static void __init pb1550_nand_setup(void)
{
	int boot_swapboot = (au_readl(MEM_STSTAT) & (0x7 << 1)) |
			    ((bcsr_read(BCSR_STATUS) >> 6) & 0x1);

	switch (boot_swapboot) {
	case 0:
	case 2:
	case 8:
	case 0xC:
	case 0xD:
		/* x16 NAND Flash */
		pb1550_nand_pd.devwidth = 1;
		/* fallthrough */
	case 1:
	case 9:
	case 3:
	case 0xE:
	case 0xF:
		/* x8 NAND, already set up */
		platform_device_register(&pb1550_nand_dev);
	}
}

static int __init pb1550_dev_init(void)
{
	int swapped;

	irq_set_irq_type(AU1550_GPIO0_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1550_GPIO1_INT, IRQF_TRIGGER_LOW);
	irq_set_irq_type(AU1550_GPIO201_205_INT, IRQF_TRIGGER_HIGH);

	/* enable both PCMCIA card irqs in the shared line */
	alchemy_gpio2_enable_int(201);
	alchemy_gpio2_enable_int(202);

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

	/* NAND setup */
	gpio_direction_input(206);	/* GPIO206 high */
	pb1550_nand_setup();

	swapped = bcsr_read(BCSR_STATUS) & BCSR_STATUS_PB1550_SWAPBOOT;
	db1x_register_norflash(128 * 1024 * 1024, 4, swapped);
	platform_device_register(&pb1550_pci_host);
	platform_device_register(&pb1550_i2c_dev);

	return 0;
}
arch_initcall(pb1550_dev_init);
