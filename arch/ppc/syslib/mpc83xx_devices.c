/*
 * arch/ppc/platforms/83xx/mpc83xx_devices.c
 *
 * MPC83xx Device descriptions
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2005 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/serial_8250.h>
#include <linux/fsl_devices.h>
#include <asm/mpc83xx.h>
#include <asm/irq.h>
#include <asm/ppc_sys.h>
#include <asm/machdep.h>

/* We use offsets for IORESOURCE_MEM since we do not know at compile time
 * what IMMRBAR is, will get fixed up by mach_mpc83xx_fixup
 */

struct gianfar_mdio_data mpc83xx_mdio_pdata = {
	.paddr = 0x24520,
};

static struct gianfar_platform_data mpc83xx_tsec1_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR,
};

static struct gianfar_platform_data mpc83xx_tsec2_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR,
};

static struct fsl_i2c_platform_data mpc83xx_fsl_i2c1_pdata = {
	.device_flags = FSL_I2C_DEV_SEPARATE_DFSRR,
};

static struct fsl_i2c_platform_data mpc83xx_fsl_i2c2_pdata = {
	.device_flags = FSL_I2C_DEV_SEPARATE_DFSRR,
};

static struct plat_serial8250_port serial_platform_data[] = {
	[0] = {
		.mapbase	= 0x4500,
		.irq		= MPC83xx_IRQ_UART1,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	[1] = {
		.mapbase	= 0x4600,
		.irq		= MPC83xx_IRQ_UART2,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
	},
	{ },
};

struct platform_device ppc_sys_platform_devices[] = {
	[MPC83xx_TSEC1] = {
		.name = "fsl-gianfar",
		.id	= 1,
		.dev.platform_data = &mpc83xx_tsec1_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= 0x24000,
				.end	= 0x24fff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC83xx_IRQ_TSEC1_TX,
				.end	= MPC83xx_IRQ_TSEC1_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC83xx_IRQ_TSEC1_RX,
				.end	= MPC83xx_IRQ_TSEC1_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC83xx_IRQ_TSEC1_ERROR,
				.end	= MPC83xx_IRQ_TSEC1_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_TSEC2] = {
		.name = "fsl-gianfar",
		.id	= 2,
		.dev.platform_data = &mpc83xx_tsec2_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= 0x25000,
				.end	= 0x25fff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC83xx_IRQ_TSEC2_TX,
				.end	= MPC83xx_IRQ_TSEC2_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC83xx_IRQ_TSEC2_RX,
				.end	= MPC83xx_IRQ_TSEC2_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC83xx_IRQ_TSEC2_ERROR,
				.end	= MPC83xx_IRQ_TSEC2_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_IIC1] = {
		.name = "fsl-i2c",
		.id	= 1,
		.dev.platform_data = &mpc83xx_fsl_i2c1_pdata,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x3000,
				.end	= 0x30ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC83xx_IRQ_IIC1,
				.end	= MPC83xx_IRQ_IIC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_IIC2] = {
		.name = "fsl-i2c",
		.id	= 2,
		.dev.platform_data = &mpc83xx_fsl_i2c2_pdata,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x3100,
				.end	= 0x31ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC83xx_IRQ_IIC2,
				.end	= MPC83xx_IRQ_IIC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_DUART] = {
		.name = "serial8250",
		.id	= PLAT8250_DEV_PLATFORM,
		.dev.platform_data = serial_platform_data,
	},
	[MPC83xx_SEC2] = {
		.name = "fsl-sec2",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x30000,
				.end	= 0x3ffff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC83xx_IRQ_SEC2,
				.end	= MPC83xx_IRQ_SEC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_USB2_DR] = {
		.name = "fsl-usb2-dr",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x23000,
				.end	= 0x23fff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC83xx_IRQ_USB2_DR,
				.end	= MPC83xx_IRQ_USB2_DR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_USB2_MPH] = {
		.name = "fsl-usb2-mph",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x22000,
				.end	= 0x22fff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC83xx_IRQ_USB2_MPH,
				.end	= MPC83xx_IRQ_USB2_MPH,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC83xx_MDIO] = {
		.name = "fsl-gianfar_mdio",
		.id = 0,
		.dev.platform_data = &mpc83xx_mdio_pdata,
		.num_resources = 0,
	},
};

static int __init mach_mpc83xx_fixup(struct platform_device *pdev)
{
	ppc_sys_fixup_mem_resource(pdev, immrbar);
	return 0;
}

static int __init mach_mpc83xx_init(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mach_mpc83xx_init:enter", 0);
	ppc_sys_device_fixup = mach_mpc83xx_fixup;
	return 0;
}

postcore_initcall(mach_mpc83xx_init);
