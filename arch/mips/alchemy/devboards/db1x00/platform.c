/*
 * DBAu1xxx board platform device registration
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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1000_dma.h>
#include <asm/mach-db1x00/bcsr.h>
#include "../platform.h"

struct pci_dev;

/* DB1xxx PCMCIA interrupt sources:
 * CD0/1 	GPIO0/3
 * STSCHG0/1	GPIO1/4
 * CARD0/1	GPIO2/5
 * Db1550:	0/1, 21/22, 3/5
 */

#define DB1XXX_HAS_PCMCIA
#define F_SWAPPED (bcsr_read(BCSR_STATUS) & BCSR_STATUS_DB1000_SWAPBOOT)

#if defined(CONFIG_MIPS_DB1000)
#define DB1XXX_PCMCIA_CD0	AU1000_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1000_GPIO1_INT
#define DB1XXX_PCMCIA_CARD0	AU1000_GPIO2_INT
#define DB1XXX_PCMCIA_CD1	AU1000_GPIO3_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1000_GPIO4_INT
#define DB1XXX_PCMCIA_CARD1	AU1000_GPIO5_INT
#define BOARD_FLASH_SIZE	0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#elif defined(CONFIG_MIPS_DB1100)
#define DB1XXX_PCMCIA_CD0	AU1100_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1100_GPIO1_INT
#define DB1XXX_PCMCIA_CARD0	AU1100_GPIO2_INT
#define DB1XXX_PCMCIA_CD1	AU1100_GPIO3_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1100_GPIO4_INT
#define DB1XXX_PCMCIA_CARD1	AU1100_GPIO5_INT
#define BOARD_FLASH_SIZE	0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#elif defined(CONFIG_MIPS_DB1500)
#define DB1XXX_PCMCIA_CD0	AU1500_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1500_GPIO1_INT
#define DB1XXX_PCMCIA_CARD0	AU1500_GPIO2_INT
#define DB1XXX_PCMCIA_CD1	AU1500_GPIO3_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1500_GPIO4_INT
#define DB1XXX_PCMCIA_CARD1	AU1500_GPIO5_INT
#define BOARD_FLASH_SIZE	0x02000000 /* 32MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#elif defined(CONFIG_MIPS_DB1550)
#define DB1XXX_PCMCIA_CD0	AU1550_GPIO0_INT
#define DB1XXX_PCMCIA_STSCHG0	AU1550_GPIO21_INT
#define DB1XXX_PCMCIA_CARD0	AU1550_GPIO3_INT
#define DB1XXX_PCMCIA_CD1	AU1550_GPIO1_INT
#define DB1XXX_PCMCIA_STSCHG1	AU1550_GPIO22_INT
#define DB1XXX_PCMCIA_CARD1	AU1550_GPIO5_INT
#define BOARD_FLASH_SIZE	0x08000000 /* 128MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#else
/* other board: no PCMCIA */
#undef DB1XXX_HAS_PCMCIA
#undef F_SWAPPED
#define F_SWAPPED 0
#if defined(CONFIG_MIPS_BOSPORUS)
#define BOARD_FLASH_SIZE	0x01000000 /* 16MB */
#define BOARD_FLASH_WIDTH	2 /* 16-bits */
#elif defined(CONFIG_MIPS_MIRAGE)
#define BOARD_FLASH_SIZE	0x04000000 /* 64MB */
#define BOARD_FLASH_WIDTH	4 /* 32-bits */
#endif
#endif

#ifdef CONFIG_PCI
#ifdef CONFIG_MIPS_DB1500
static int db1xxx_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 12) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 12)
		return (pin == 1) ? AU1500_PCI_INTA : 0xff;
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
#endif

#ifdef CONFIG_MIPS_DB1550
static int db1xxx_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 11) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 11)
		return (pin == 1) ? AU1550_PCI_INTC : 0xff;
	if (slot == 12) {
		switch (pin) {
		case 1: return AU1550_PCI_INTB;
		case 2: return AU1550_PCI_INTC;
		case 3: return AU1550_PCI_INTD;
		case 4: return AU1550_PCI_INTA;
		}
	}
	if (slot == 13) {
		switch (pin) {
		case 1: return AU1550_PCI_INTA;
		case 2: return AU1550_PCI_INTB;
		case 3: return AU1550_PCI_INTC;
		case 4: return AU1550_PCI_INTD;
		}
	}
	return -1;
}
#endif

#ifdef CONFIG_MIPS_BOSPORUS
static int db1xxx_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 11) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 12)
		return (pin == 1) ? AU1500_PCI_INTA : 0xff;
	if (slot == 11) {
		switch (pin) {
		case 1: return AU1500_PCI_INTA;
		case 2: return AU1500_PCI_INTB;
		default: return 0xff;
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
#endif

#ifdef CONFIG_MIPS_MIRAGE
static int db1xxx_map_pci_irq(const struct pci_dev *d, u8 slot, u8 pin)
{
	if ((slot < 11) || (slot > 13) || pin == 0)
		return -1;
	if (slot == 11)
		return (pin == 1) ? AU1500_PCI_INTD : 0xff;
	if (slot == 12)
		return (pin == 3) ? AU1500_PCI_INTC : 0xff;
	if (slot == 13) {
		switch (pin) {
		case 1: return AU1500_PCI_INTA;
		case 2: return AU1500_PCI_INTB;
		default: return 0xff;
		}
	}
	return -1;
}
#endif

static struct resource alchemy_pci_host_res[] = {
	[0] = {
		.start	= AU1500_PCI_PHYS_ADDR,
		.end	= AU1500_PCI_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct alchemy_pci_platdata db1xxx_pci_pd = {
	.board_map_irq	= db1xxx_map_pci_irq,
};

static struct platform_device db1xxx_pci_host_dev = {
	.dev.platform_data = &db1xxx_pci_pd,
	.name		= "alchemy-pci",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(alchemy_pci_host_res),
	.resource	= alchemy_pci_host_res,
};

static int __init db15x0_pci_init(void)
{
	return platform_device_register(&db1xxx_pci_host_dev);
}
/* must be arch_initcall; MIPS PCI scans busses in a subsys_initcall */
arch_initcall(db15x0_pci_init);
#endif

#ifdef CONFIG_MIPS_DB1100
static struct resource au1100_lcd_resources[] = {
	[0] = {
		.start	= AU1100_LCD_PHYS_ADDR,
		.end	= AU1100_LCD_PHYS_ADDR + 0x800 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AU1100_LCD_INT,
		.end	= AU1100_LCD_INT,
		.flags	= IORESOURCE_IRQ,
	}
};

static u64 au1100_lcd_dmamask = DMA_BIT_MASK(32);

static struct platform_device au1100_lcd_device = {
	.name		= "au1100-lcd",
	.id		= 0,
	.dev = {
		.dma_mask		= &au1100_lcd_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(au1100_lcd_resources),
	.resource	= au1100_lcd_resources,
};
#endif

static struct resource alchemy_ac97c_res[] = {
	[0] = {
		.start	= AU1000_AC97_PHYS_ADDR,
		.end	= AU1000_AC97_PHYS_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMA_ID_AC97C_TX,
		.end	= DMA_ID_AC97C_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMA_ID_AC97C_RX,
		.end	= DMA_ID_AC97C_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device alchemy_ac97c_dev = {
	.name		= "alchemy-ac97c",
	.id		= -1,
	.resource	= alchemy_ac97c_res,
	.num_resources	= ARRAY_SIZE(alchemy_ac97c_res),
};

static struct platform_device alchemy_ac97c_dma_dev = {
	.name		= "alchemy-pcm-dma",
	.id		= 0,
};

static struct platform_device db1x00_codec_dev = {
	.name		= "ac97-codec",
	.id		= -1,
};

static struct platform_device db1x00_audio_dev = {
	.name		= "db1000-audio",
};

static int __init db1xxx_dev_init(void)
{
#ifdef DB1XXX_HAS_PCMCIA
	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x000400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x000010000 - 1,
		DB1XXX_PCMCIA_CARD0, DB1XXX_PCMCIA_CD0,
		/*DB1XXX_PCMCIA_STSCHG0*/0, 0, 0);

	db1x_register_pcmcia_socket(
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004000000,
		AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x004400000 - 1,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004000000,
		AU1000_PCMCIA_MEM_PHYS_ADDR  + 0x004400000 - 1,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004000000,
		AU1000_PCMCIA_IO_PHYS_ADDR   + 0x004010000 - 1,
		DB1XXX_PCMCIA_CARD1, DB1XXX_PCMCIA_CD1,
		/*DB1XXX_PCMCIA_STSCHG1*/0, 0, 1);
#endif
#ifdef CONFIG_MIPS_DB1100
	platform_device_register(&au1100_lcd_device);
#endif
	db1x_register_norflash(BOARD_FLASH_SIZE, BOARD_FLASH_WIDTH, F_SWAPPED);

	platform_device_register(&db1x00_codec_dev);
	platform_device_register(&alchemy_ac97c_dma_dev);
	platform_device_register(&alchemy_ac97c_dev);
	platform_device_register(&db1x00_audio_dev);

	return 0;
}
device_initcall(db1xxx_dev_init);
