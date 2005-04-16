/*
 * arch/ppc/syslib/mpc52xx_devices.c
 *
 * Freescale MPC52xx device descriptions
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Copyright (C) 2005 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/fsl_devices.h>
#include <linux/resource.h>
#include <asm/mpc52xx.h>
#include <asm/ppc_sys.h>


static u64 mpc52xx_dma_mask = 0xffffffffULL;

static struct fsl_i2c_platform_data mpc52xx_fsl_i2c_pdata = {
	.device_flags = FSL_I2C_DEV_CLOCK_5200,
};


/* We use relative offsets for IORESOURCE_MEM to be independent from the
 * MBAR location at compile time
 */

/* TODO Add the BestComm initiator channel to the device definitions,
   possibly using IORESOURCE_DMA. But that's when BestComm is ready ... */

struct platform_device ppc_sys_platform_devices[] = {
	[MPC52xx_MSCAN1] = {
		.name		= "mpc52xx-mscan",
		.id		= 0,
		.num_resources	= 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x0900,
				.end	= 0x097f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_MSCAN1_IRQ,
				.end	= MPC52xx_MSCAN1_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_MSCAN2] = {
		.name		= "mpc52xx-mscan",
		.id		= 1,
		.num_resources	= 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x0980,
				.end	= 0x09ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_MSCAN2_IRQ,
				.end	= MPC52xx_MSCAN2_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_SPI] = {
		.name		= "mpc52xx-spi",
		.id		= -1,
		.num_resources	= 3,
		.resource	= (struct resource[]) {
			{
				.start	= 0x0f00,
				.end	= 0x0f1f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "modf",
				.start	= MPC52xx_SPI_MODF_IRQ,
				.end	= MPC52xx_SPI_MODF_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "spif",
				.start	= MPC52xx_SPI_SPIF_IRQ,
				.end	= MPC52xx_SPI_SPIF_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_USB] = {
		.name		= "ppc-soc-ohci",
		.id		= -1,
		.num_resources	= 2,
		.dev.dma_mask	= &mpc52xx_dma_mask,
		.dev.coherent_dma_mask = 0xffffffffULL,
		.resource	= (struct resource[]) {
			{
				.start	= 0x1000,
				.end	= 0x10ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_USB_IRQ,
				.end	= MPC52xx_USB_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_BDLC] = {
		.name		= "mpc52xx-bdlc",
		.id		= -1,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x1300,
				.end	= 0x130f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_BDLC_IRQ,
				.end	= MPC52xx_BDLC_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_PSC1] = {
		.name		= "mpc52xx-psc",
		.id		= 0,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x2000,
				.end	= 0x209f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_PSC1_IRQ,
				.end	= MPC52xx_PSC1_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_PSC2] = {
		.name		= "mpc52xx-psc",
		.id		= 1,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x2200,
				.end	= 0x229f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_PSC2_IRQ,
				.end	= MPC52xx_PSC2_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_PSC3] = {
		.name		= "mpc52xx-psc",
		.id		= 2,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x2400,
				.end	= 0x249f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_PSC3_IRQ,
				.end	= MPC52xx_PSC3_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_PSC4] = {
		.name		= "mpc52xx-psc",
		.id		= 3,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x2600,
				.end	= 0x269f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_PSC4_IRQ,
				.end	= MPC52xx_PSC4_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_PSC5] = {
		.name		= "mpc52xx-psc",
		.id		= 4,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x2800,
				.end	= 0x289f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_PSC5_IRQ,
				.end	= MPC52xx_PSC5_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_PSC6] = {
		.name		= "mpc52xx-psc",
		.id		= 5,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x2c00,
				.end	= 0x2c9f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_PSC6_IRQ,
				.end	= MPC52xx_PSC6_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_FEC] = {
		.name		= "mpc52xx-fec",
		.id		= -1,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x3000,
				.end	= 0x33ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_FEC_IRQ,
				.end	= MPC52xx_FEC_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_ATA] = {
		.name		= "mpc52xx-ata",
		.id		= -1,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x3a00,
				.end	= 0x3aff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_ATA_IRQ,
				.end	= MPC52xx_ATA_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_I2C1] = {
		.name		= "fsl-i2c",
		.id		= 0,
		.dev.platform_data = &mpc52xx_fsl_i2c_pdata,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x3d00,
				.end	= 0x3d1f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_I2C1_IRQ,
				.end	= MPC52xx_I2C1_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC52xx_I2C2] = {
		.name		= "fsl-i2c",
		.id		= 1,
		.dev.platform_data = &mpc52xx_fsl_i2c_pdata,
		.num_resources	= 2,
		.resource	= (struct resource[]) {
			{
				.start	= 0x3d40,
				.end	= 0x3d5f,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC52xx_I2C2_IRQ,
				.end	= MPC52xx_I2C2_IRQ,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
};


static int __init mach_mpc52xx_fixup(struct platform_device *pdev)
{
	ppc_sys_fixup_mem_resource(pdev, MPC52xx_MBAR);
	return 0;
}

static int __init mach_mpc52xx_init(void)
{
	ppc_sys_device_fixup = mach_mpc52xx_fixup;
	return 0;
}

postcore_initcall(mach_mpc52xx_init);
