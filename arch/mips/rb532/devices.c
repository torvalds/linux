/*
 *  RouterBoard 500 Platform devices
 *
 *  Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 *  Copyright (C) 2007 Florian Fainelli <florian@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <asm/bootinfo.h>

#include <asm/mach-rc32434/rc32434.h>
#include <asm/mach-rc32434/dma.h>
#include <asm/mach-rc32434/dma_v.h>
#include <asm/mach-rc32434/eth.h>
#include <asm/mach-rc32434/rb.h>
#include <asm/mach-rc32434/integ.h>
#include <asm/mach-rc32434/gpio.h>

#define ETH0_DMA_RX_IRQ   	(GROUP1_IRQ_BASE + 0)
#define ETH0_DMA_TX_IRQ   	(GROUP1_IRQ_BASE + 1)
#define ETH0_RX_OVR_IRQ   	(GROUP3_IRQ_BASE + 9)
#define ETH0_TX_UND_IRQ   	(GROUP3_IRQ_BASE + 10)

#define ETH0_RX_DMA_ADDR  (DMA0_BASE_ADDR + 0 * DMA_CHAN_OFFSET)
#define ETH0_TX_DMA_ADDR  (DMA0_BASE_ADDR + 1 * DMA_CHAN_OFFSET)

/* NAND definitions */
#define GPIO_RDY (1 << 0x08)
#define GPIO_WPX (1 << 0x09)
#define GPIO_ALE (1 << 0x0a)
#define GPIO_CLE (1 << 0x0b)

static struct resource korina_dev0_res[] = {
	{
		.name = "korina_regs",
		.start = ETH0_BASE_ADDR,
		.end = ETH0_BASE_ADDR + sizeof(struct eth_regs),
		.flags = IORESOURCE_MEM,
	 }, {
		.name = "korina_rx",
		.start = ETH0_DMA_RX_IRQ,
		.end = ETH0_DMA_RX_IRQ,
		.flags = IORESOURCE_IRQ
	}, {
		.name = "korina_tx",
		.start = ETH0_DMA_TX_IRQ,
		.end = ETH0_DMA_TX_IRQ,
		.flags = IORESOURCE_IRQ
	}, {
		.name = "korina_ovr",
		.start = ETH0_RX_OVR_IRQ,
		.end = ETH0_RX_OVR_IRQ,
		.flags = IORESOURCE_IRQ
	}, {
		.name = "korina_und",
		.start = ETH0_TX_UND_IRQ,
		.end = ETH0_TX_UND_IRQ,
		.flags = IORESOURCE_IRQ
	}, {
		.name = "korina_dma_rx",
		.start = ETH0_RX_DMA_ADDR,
		.end = ETH0_RX_DMA_ADDR + DMA_CHAN_OFFSET - 1,
		.flags = IORESOURCE_MEM,
	 }, {
		.name = "korina_dma_tx",
		.start = ETH0_TX_DMA_ADDR,
		.end = ETH0_TX_DMA_ADDR + DMA_CHAN_OFFSET - 1,
		.flags = IORESOURCE_MEM,
	 }
};

static struct korina_device korina_dev0_data = {
	.name = "korina0",
	.mac = {0xde, 0xca, 0xff, 0xc0, 0xff, 0xee}
};

static struct platform_device korina_dev0 = {
	.id = 0,
	.name = "korina",
	.dev.platform_data = &korina_dev0_data,
	.resource = korina_dev0_res,
	.num_resources = ARRAY_SIZE(korina_dev0_res),
};

#define CF_GPIO_NUM 13

static struct resource cf_slot0_res[] = {
	{
		.name = "cf_membase",
		.flags = IORESOURCE_MEM
	}, {
		.name = "cf_irq",
		.start = (8 + 4 * 32 + CF_GPIO_NUM),	/* 149 */
		.end = (8 + 4 * 32 + CF_GPIO_NUM),
		.flags = IORESOURCE_IRQ
	}
};

static struct cf_device cf_slot0_data = {
	.gpio_pin = 13
};

static struct platform_device cf_slot0 = {
	.id = 0,
	.name = "pata-rb532-cf",
	.dev.platform_data = &cf_slot0_data,
	.resource = cf_slot0_res,
	.num_resources = ARRAY_SIZE(cf_slot0_res),
};

/* Resources and device for NAND */
static int rb532_dev_ready(struct mtd_info *mtd)
{
	return readl(IDT434_REG_BASE + GPIOD) & GPIO_RDY;
}

static void rb532_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *chip = mtd->priv;
	unsigned char orbits, nandbits;

	if (ctrl & NAND_CTRL_CHANGE) {
		orbits = (ctrl & NAND_CLE) << 1;
		orbits |= (ctrl & NAND_ALE) >> 1;

		nandbits = (~ctrl & NAND_CLE) << 1;
		nandbits |= (~ctrl & NAND_ALE) >> 1;

		set_latch_u5(orbits, nandbits);
	}
	if (cmd != NAND_CMD_NONE)
		writeb(cmd, chip->IO_ADDR_W);
}

static struct resource nand_slot0_res[] = {
	[0] = {
		.name = "nand_membase",
		.flags = IORESOURCE_MEM
	}
};

static struct platform_nand_data rb532_nand_data = {
	.ctrl.dev_ready = rb532_dev_ready,
	.ctrl.cmd_ctrl	= rb532_cmd_ctrl,
};

static struct platform_device nand_slot0 = {
	.name = "gen_nand",
	.id = -1,
	.resource = nand_slot0_res,
	.num_resources = ARRAY_SIZE(nand_slot0_res),
	.dev.platform_data = &rb532_nand_data,
};

static struct mtd_partition rb532_partition_info[] = {
	{
		.name = "Routerboard NAND boot",
		.offset = 0,
		.size = 4 * 1024 * 1024,
	}, {
		.name = "rootfs",
		.offset = MTDPART_OFS_NXTBLK,
		.size = MTDPART_SIZ_FULL,
	}
};

static struct platform_device rb532_led = {
	.name = "rb532-led",
	.id = 0,
};

static struct gpio_keys_button rb532_gpio_btn[] = {
	{
		.gpio = 1,
		.code = BTN_0,
		.desc = "S1",
		.active_low = 1,
	}
};

static struct gpio_keys_platform_data rb532_gpio_btn_data = {
	.buttons = rb532_gpio_btn,
	.nbuttons = ARRAY_SIZE(rb532_gpio_btn),
};

static struct platform_device rb532_button = {
	.name 	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &rb532_gpio_btn_data,
	}
};

static struct resource rb532_wdt_res[] = {
	{
		.name = "rb532_wdt_res",
		.start = INTEG0_BASE_ADDR,
		.end = INTEG0_BASE_ADDR + sizeof(struct integ),
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device rb532_wdt = {
	.name 		= "rc32434_wdt",
	.id 		= -1,
	.resource 	= rb532_wdt_res,
	.num_resources	= ARRAY_SIZE(rb532_wdt_res),
};

static struct platform_device *rb532_devs[] = {
	&korina_dev0,
	&nand_slot0,
	&cf_slot0,
	&rb532_led,
	&rb532_button,
	&rb532_wdt
};

static void __init parse_mac_addr(char *macstr)
{
	int i, j;
	unsigned char result, value;

	for (i = 0; i < 6; i++) {
		result = 0;

		if (i != 5 && *(macstr + 2) != ':')
			return;

		for (j = 0; j < 2; j++) {
			if (isxdigit(*macstr)
			    && (value =
				isdigit(*macstr) ? *macstr -
				'0' : toupper(*macstr) - 'A' + 10) < 16) {
				result = result * 16 + value;
				macstr++;
			} else
				return;
		}

		macstr++;
		korina_dev0_data.mac[i] = result;
	}
}


/* NAND definitions */
#define NAND_CHIP_DELAY	25

static void __init rb532_nand_setup(void)
{
	switch (mips_machtype) {
	case MACH_MIKROTIK_RB532A:
		set_latch_u5(LO_FOFF | LO_CEX,
				LO_ULED | LO_ALE | LO_CLE | LO_WPX);
		break;
	default:
		set_latch_u5(LO_WPX | LO_FOFF | LO_CEX,
				LO_ULED | LO_ALE | LO_CLE);
		break;
	}

	/* Setup NAND specific settings */
	rb532_nand_data.chip.nr_chips = 1;
	rb532_nand_data.chip.nr_partitions = ARRAY_SIZE(rb532_partition_info);
	rb532_nand_data.chip.partitions = rb532_partition_info;
	rb532_nand_data.chip.chip_delay = NAND_CHIP_DELAY;
	rb532_nand_data.chip.options = NAND_NO_AUTOINCR;
}


static int __init plat_setup_devices(void)
{
	/* Look for the CF card reader */
	if (!readl(IDT434_REG_BASE + DEV1MASK))
		rb532_devs[1] = NULL;
	else {
		cf_slot0_res[0].start =
		    readl(IDT434_REG_BASE + DEV1BASE);
		cf_slot0_res[0].end = cf_slot0_res[0].start + 0x1000;
	}

	/* Read the NAND resources from the device controller */
	nand_slot0_res[0].start = readl(IDT434_REG_BASE + DEV2BASE);
	nand_slot0_res[0].end = nand_slot0_res[0].start + 0x1000;

	/* Initialise the NAND device */
	rb532_nand_setup();

	return platform_add_devices(rb532_devs, ARRAY_SIZE(rb532_devs));
}

static int __init setup_kmac(char *s)
{
	printk(KERN_INFO "korina mac = %s\n", s);
	parse_mac_addr(s);
	return 0;
}

__setup("kmac=", setup_kmac);

arch_initcall(plat_setup_devices);
