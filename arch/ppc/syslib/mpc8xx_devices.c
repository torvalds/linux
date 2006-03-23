/*
 * MPC8xx Device descriptions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 MontaVista Software, Inc. by Vitaly Bordug<vbordug@ru.mvista.com>
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
#include <linux/mii.h>
#include <asm/commproc.h>
#include <asm/mpc8xx.h>
#include <asm/irq.h>
#include <asm/ppc_sys.h>

/* We use offsets for IORESOURCE_MEM to do not set dependences at compile time.
 * They will get fixed up by mach_mpc8xx_fixup
 */

struct platform_device ppc_sys_platform_devices[] = {
	[MPC8xx_CPM_FEC1] =	{
		.name = "fsl-cpm-fec",
		.id	= 1,
		.num_resources = 2,
		.resource = (struct resource[])	{
			{
				.name 	= "regs",
				.start	= 0xe00,
				.end	= 0xe88,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_FEC1,
				.end	= MPC8xx_INT_FEC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_FEC2] =	{
		.name = "fsl-cpm-fec",
		.id	= 2,
		.num_resources = 2,
		.resource = (struct resource[])	{
			{
				.name	= "regs",
				.start	= 0x1e00,
				.end	= 0x1e88,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_FEC2,
				.end	= MPC8xx_INT_FEC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_SCC1] = {
		.name = "fsl-cpm-scc",
		.id	= 1,
		.num_resources = 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0xa00,
				.end	= 0xa18,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name 	= "pram",
				.start 	= 0x3c00,
				.end 	= 0x3c80,
				.flags 	= IORESOURCE_MEM,
			},
			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_SCC1,
				.end	= MPC8xx_INT_SCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_SCC2] = {
		.name = "fsl-cpm-scc",
		.id	= 2,
		.num_resources	= 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0xa20,
				.end	= 0xa38,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name 	= "pram",
				.start 	= 0x3d00,
				.end 	= 0x3d80,
				.flags 	= IORESOURCE_MEM,
			},

			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_SCC2,
				.end	= MPC8xx_INT_SCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_SCC3] = {
		.name = "fsl-cpm-scc",
		.id	= 3,
		.num_resources	= 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0xa40,
				.end	= 0xa58,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name 	= "pram",
				.start 	= 0x3e00,
				.end 	= 0x3e80,
				.flags 	= IORESOURCE_MEM,
			},

			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_SCC3,
				.end	= MPC8xx_INT_SCC3,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_SCC4] = {
		.name = "fsl-cpm-scc",
		.id	= 4,
		.num_resources	= 3,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0xa60,
				.end	= 0xa78,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name 	= "pram",
				.start 	= 0x3f00,
				.end 	= 0x3f80,
				.flags 	= IORESOURCE_MEM,
			},

			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_SCC4,
				.end	= MPC8xx_INT_SCC4,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_SMC1] = {
		.name = "fsl-cpm-smc",
		.id	= 1,
		.num_resources	= 2,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0xa82,
				.end	= 0xa91,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_SMC1,
				.end	= MPC8xx_INT_SMC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC8xx_CPM_SMC2] = {
		.name = "fsl-cpm-smc",
		.id	= 2,
		.num_resources	= 2,
		.resource = (struct resource[]) {
			{
				.name	= "regs",
				.start	= 0xa92,
				.end	= 0xaa1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "interrupt",
				.start	= MPC8xx_INT_SMC2,
				.end	= MPC8xx_INT_SMC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
};

static int __init mach_mpc8xx_fixup(struct platform_device *pdev)
{
	ppc_sys_fixup_mem_resource (pdev, IMAP_ADDR);
	return 0;
}

static int __init mach_mpc8xx_init(void)
{
	ppc_sys_device_fixup = mach_mpc8xx_fixup;
	return 0;
}

postcore_initcall(mach_mpc8xx_init);
