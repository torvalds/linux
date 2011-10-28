/*
 *  Copyright (C) 2009 Ilya Yanok, Emcraft Systems Ltd, <yanok@emcraft.com>
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
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <mach/iomux-mx3.h>

#include "devices-imx31.h"

/* FPGA defines */
#define QONG_FPGA_VERSION(major, minor, rev)	\
	(((major & 0xF) << 12) | ((minor & 0xF) << 8) | (rev & 0xFF))

#define QONG_FPGA_BASEADDR		MX31_CS1_BASE_ADDR
#define QONG_FPGA_PERIPH_SIZE		(1 << 24)

#define QONG_FPGA_CTRL_BASEADDR		QONG_FPGA_BASEADDR
#define QONG_FPGA_CTRL_SIZE		0x10
/* FPGA control registers */
#define QONG_FPGA_CTRL_VERSION		0x00

#define QONG_DNET_ID		1
#define QONG_DNET_BASEADDR	\
	(QONG_FPGA_BASEADDR + QONG_DNET_ID * QONG_FPGA_PERIPH_SIZE)
#define QONG_DNET_SIZE		0x00001000

#define QONG_FPGA_IRQ		IOMUX_TO_IRQ(MX31_PIN_DTR_DCE1)

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static int uart_pins[] = {
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1
};

static inline void __init mxc_init_imx_uart(void)
{
	mxc_iomux_setup_multiple_pins(uart_pins, ARRAY_SIZE(uart_pins),
			"uart-0");
	imx31_add_imx_uart0(&uart_pdata);
}

static struct resource dnet_resources[] = {
	{
		.name	= "dnet-memory",
		.start	= QONG_DNET_BASEADDR,
		.end	= QONG_DNET_BASEADDR + QONG_DNET_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= QONG_FPGA_IRQ,
		.end	= QONG_FPGA_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dnet_device = {
	.name			= "dnet",
	.id			= -1,
	.num_resources		= ARRAY_SIZE(dnet_resources),
	.resource		= dnet_resources,
};

static int __init qong_init_dnet(void)
{
	int ret;

	ret = platform_device_register(&dnet_device);
	return ret;
}

/* MTD NOR flash */

static struct physmap_flash_data qong_flash_data = {
	.width = 2,
};

static struct resource qong_flash_resource = {
	.start = MX31_CS0_BASE_ADDR,
	.end = MX31_CS0_BASE_ADDR + SZ_128M - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device qong_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &qong_flash_data,
		},
	.resource = &qong_flash_resource,
	.num_resources = 1,
};

static void qong_init_nor_mtd(void)
{
	(void)platform_device_register(&qong_nor_mtd_device);
}

/*
 * Hardware specific access to control-lines
 */
static void qong_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *nand_chip = mtd->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, nand_chip->IO_ADDR_W + (1 << 24));
	else
		writeb(cmd, nand_chip->IO_ADDR_W + (1 << 23));
}

/*
 * Read the Device Ready pin.
 */
static int qong_nand_device_ready(struct mtd_info *mtd)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_NFRB));
}

static void qong_nand_select_chip(struct mtd_info *mtd, int chip)
{
	if (chip >= 0)
		gpio_set_value(IOMUX_TO_GPIO(MX31_PIN_NFCE_B), 0);
	else
		gpio_set_value(IOMUX_TO_GPIO(MX31_PIN_NFCE_B), 1);
}

static struct platform_nand_data qong_nand_data = {
	.chip = {
		.nr_chips		= 1,
		.chip_delay		= 20,
		.options		= 0,
	},
	.ctrl = {
		.cmd_ctrl		= qong_nand_cmd_ctrl,
		.dev_ready		= qong_nand_device_ready,
		.select_chip		= qong_nand_select_chip,
	}
};

static struct resource qong_nand_resource = {
	.start		= MX31_CS3_BASE_ADDR,
	.end		= MX31_CS3_BASE_ADDR + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device qong_nand_device = {
	.name		= "gen_nand",
	.id		= -1,
	.dev		= {
		.platform_data = &qong_nand_data,
	},
	.num_resources	= 1,
	.resource	= &qong_nand_resource,
};

static void __init qong_init_nand_mtd(void)
{
	/* init CS */
	mx31_setup_weimcs(3, 0x00004f00, 0x20013b31, 0x00020800);
	mxc_iomux_set_gpr(MUX_SDCTL_CSD1_SEL, true);

	/* enable pin */
	mxc_iomux_mode(IOMUX_MODE(MX31_PIN_NFCE_B, IOMUX_CONFIG_GPIO));
	if (!gpio_request(IOMUX_TO_GPIO(MX31_PIN_NFCE_B), "nand_enable"))
		gpio_direction_output(IOMUX_TO_GPIO(MX31_PIN_NFCE_B), 0);

	/* ready/busy pin */
	mxc_iomux_mode(IOMUX_MODE(MX31_PIN_NFRB, IOMUX_CONFIG_GPIO));
	if (!gpio_request(IOMUX_TO_GPIO(MX31_PIN_NFRB), "nand_rdy"))
		gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_NFRB));

	/* write protect pin */
	mxc_iomux_mode(IOMUX_MODE(MX31_PIN_NFWP_B, IOMUX_CONFIG_GPIO));
	if (!gpio_request(IOMUX_TO_GPIO(MX31_PIN_NFWP_B), "nand_wp"))
		gpio_direction_input(IOMUX_TO_GPIO(MX31_PIN_NFWP_B));

	platform_device_register(&qong_nand_device);
}

static void __init qong_init_fpga(void)
{
	void __iomem *regs;
	u32 fpga_ver;

	regs = ioremap(QONG_FPGA_CTRL_BASEADDR, QONG_FPGA_CTRL_SIZE);
	if (!regs) {
		printk(KERN_ERR "%s: failed to map registers, aborting.\n",
				__func__);
		return;
	}

	fpga_ver = readl(regs + QONG_FPGA_CTRL_VERSION);
	iounmap(regs);
	printk(KERN_INFO "Qong FPGA version %d.%d.%d\n",
			(fpga_ver & 0xF000) >> 12,
			(fpga_ver & 0x0F00) >> 8, fpga_ver & 0x00FF);
	if (fpga_ver < QONG_FPGA_VERSION(0, 8, 7)) {
		printk(KERN_ERR "qong: Unexpected FPGA version, FPGA-based "
				"devices won't be registered!\n");
		return;
	}

	/* register FPGA-based devices */
	qong_init_nand_mtd();
	qong_init_dnet();
}

/*
 * Board specific initialization.
 */
static void __init qong_init(void)
{
	mxc_init_imx_uart();
	qong_init_nor_mtd();
	qong_init_fpga();
}

static void __init qong_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer qong_timer = {
	.init	= qong_timer_init,
};

MACHINE_START(QONG, "Dave/DENX QongEVB-LITE")
	/* Maintainer: DENX Software Engineering GmbH */
	.boot_params = MX3x_PHYS_OFFSET + 0x100,
	.map_io = mx31_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31_init_irq,
	.timer = &qong_timer,
	.init_machine = qong_init,
MACHINE_END
