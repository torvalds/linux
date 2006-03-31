/*
 * MPC85xx Device descriptions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
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
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <asm/ppc_sys.h>

/* We use offsets for IORESOURCE_MEM since we do not know at compile time
 * what CCSRBAR is, will get fixed up by mach_mpc85xx_fixup
 */
struct gianfar_mdio_data mpc85xx_mdio_pdata = {
};

static struct gianfar_platform_data mpc85xx_tsec1_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR,
};

static struct gianfar_platform_data mpc85xx_tsec2_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR,
};

static struct gianfar_platform_data mpc85xx_etsec1_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR |
	    FSL_GIANFAR_DEV_HAS_CSUM | FSL_GIANFAR_DEV_HAS_VLAN |
	    FSL_GIANFAR_DEV_HAS_EXTENDED_HASH,
};

static struct gianfar_platform_data mpc85xx_etsec2_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR |
	    FSL_GIANFAR_DEV_HAS_CSUM | FSL_GIANFAR_DEV_HAS_VLAN |
	    FSL_GIANFAR_DEV_HAS_EXTENDED_HASH,
};

static struct gianfar_platform_data mpc85xx_etsec3_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR |
	    FSL_GIANFAR_DEV_HAS_CSUM | FSL_GIANFAR_DEV_HAS_VLAN |
	    FSL_GIANFAR_DEV_HAS_EXTENDED_HASH,
};

static struct gianfar_platform_data mpc85xx_etsec4_pdata = {
	.device_flags = FSL_GIANFAR_DEV_HAS_GIGABIT |
	    FSL_GIANFAR_DEV_HAS_COALESCE | FSL_GIANFAR_DEV_HAS_RMON |
	    FSL_GIANFAR_DEV_HAS_MULTI_INTR |
	    FSL_GIANFAR_DEV_HAS_CSUM | FSL_GIANFAR_DEV_HAS_VLAN |
	    FSL_GIANFAR_DEV_HAS_EXTENDED_HASH,
};

static struct gianfar_platform_data mpc85xx_fec_pdata = {
	.device_flags = 0,
};

static struct fsl_i2c_platform_data mpc85xx_fsl_i2c_pdata = {
	.device_flags = FSL_I2C_DEV_SEPARATE_DFSRR,
};

static struct fsl_i2c_platform_data mpc85xx_fsl_i2c2_pdata = {
	.device_flags = FSL_I2C_DEV_SEPARATE_DFSRR,
};

static struct plat_serial8250_port serial_platform_data[] = {
	[0] = {
		.mapbase	= 0x4500,
		.irq		= MPC85xx_IRQ_DUART,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_SHARE_IRQ,
	},
	[1] = {
		.mapbase	= 0x4600,
		.irq		= MPC85xx_IRQ_DUART,
		.iotype		= UPIO_MEM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_SHARE_IRQ,
	},
	{ },
};

struct platform_device ppc_sys_platform_devices[] = {
	[MPC85xx_TSEC1] = {
		.name = "fsl-gianfar",
		.id	= 1,
		.dev.platform_data = &mpc85xx_tsec1_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_ENET1_OFFSET,
				.end	= MPC85xx_ENET1_OFFSET +
						MPC85xx_ENET1_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC85xx_IRQ_TSEC1_TX,
				.end	= MPC85xx_IRQ_TSEC1_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC85xx_IRQ_TSEC1_RX,
				.end	= MPC85xx_IRQ_TSEC1_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC85xx_IRQ_TSEC1_ERROR,
				.end	= MPC85xx_IRQ_TSEC1_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_TSEC2] = {
		.name = "fsl-gianfar",
		.id	= 2,
		.dev.platform_data = &mpc85xx_tsec2_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_ENET2_OFFSET,
				.end	= MPC85xx_ENET2_OFFSET +
						MPC85xx_ENET2_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC85xx_IRQ_TSEC2_TX,
				.end	= MPC85xx_IRQ_TSEC2_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC85xx_IRQ_TSEC2_RX,
				.end	= MPC85xx_IRQ_TSEC2_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC85xx_IRQ_TSEC2_ERROR,
				.end	= MPC85xx_IRQ_TSEC2_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_FEC] =	{
		.name = "fsl-gianfar",
		.id	= 3,
		.dev.platform_data = &mpc85xx_fec_pdata,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_ENET3_OFFSET,
				.end	= MPC85xx_ENET3_OFFSET +
						MPC85xx_ENET3_SIZE - 1,
				.flags	= IORESOURCE_MEM,

			},
			{
				.start	= MPC85xx_IRQ_FEC,
				.end	= MPC85xx_IRQ_FEC,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_IIC1] = {
		.name = "fsl-i2c",
		.id	= 1,
		.dev.platform_data = &mpc85xx_fsl_i2c_pdata,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_IIC1_OFFSET,
				.end	= MPC85xx_IIC1_OFFSET +
						MPC85xx_IIC1_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_IIC1,
				.end	= MPC85xx_IRQ_IIC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_DMA0] = {
		.name = "fsl-dma",
		.id	= 0,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_DMA0_OFFSET,
				.end	= MPC85xx_DMA0_OFFSET +
						MPC85xx_DMA0_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_DMA0,
				.end	= MPC85xx_IRQ_DMA0,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_DMA1] = {
		.name = "fsl-dma",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_DMA1_OFFSET,
				.end	= MPC85xx_DMA1_OFFSET +
						MPC85xx_DMA1_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_DMA1,
				.end	= MPC85xx_IRQ_DMA1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_DMA2] = {
		.name = "fsl-dma",
		.id	= 2,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_DMA2_OFFSET,
				.end	= MPC85xx_DMA2_OFFSET +
						MPC85xx_DMA2_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_DMA2,
				.end	= MPC85xx_IRQ_DMA2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_DMA3] = {
		.name = "fsl-dma",
		.id	= 3,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_DMA3_OFFSET,
				.end	= MPC85xx_DMA3_OFFSET +
						MPC85xx_DMA3_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_DMA3,
				.end	= MPC85xx_IRQ_DMA3,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_DUART] = {
		.name = "serial8250",
		.id	= PLAT8250_DEV_PLATFORM,
		.dev.platform_data = serial_platform_data,
	},
	[MPC85xx_PERFMON] = {
		.name = "fsl-perfmon",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_PERFMON_OFFSET,
				.end	= MPC85xx_PERFMON_OFFSET +
						MPC85xx_PERFMON_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_PERFMON,
				.end	= MPC85xx_IRQ_PERFMON,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_SEC2] = {
		.name = "fsl-sec2",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_SEC2_OFFSET,
				.end	= MPC85xx_SEC2_OFFSET +
						MPC85xx_SEC2_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_SEC2,
				.end	= MPC85xx_IRQ_SEC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_FCC1] = {
		.name = "fsl-cpm-fcc",
		.id	= 1,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.start	= 0x91300,
				.end	= 0x9131F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= 0x91380,
				.end	= 0x9139F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_FCC1,
				.end	= SIU_INT_FCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_FCC2] = {
		.name = "fsl-cpm-fcc",
		.id	= 2,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.start	= 0x91320,
				.end	= 0x9133F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= 0x913A0,
				.end	= 0x913CF,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_FCC2,
				.end	= SIU_INT_FCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_FCC3] = {
		.name = "fsl-cpm-fcc",
		.id	= 3,
		.num_resources	 = 3,
		.resource = (struct resource[]) {
			{
				.start	= 0x91340,
				.end	= 0x9135F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= 0x913D0,
				.end	= 0x913FF,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_FCC3,
				.end	= SIU_INT_FCC3,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_I2C] = {
		.name = "fsl-cpm-i2c",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91860,
				.end	= 0x918BF,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_I2C,
				.end	= SIU_INT_I2C,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SCC1] = {
		.name = "fsl-cpm-scc",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91A00,
				.end	= 0x91A1F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC1,
				.end	= SIU_INT_SCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SCC2] = {
		.name = "fsl-cpm-scc",
		.id	= 2,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91A20,
				.end	= 0x91A3F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC2,
				.end	= SIU_INT_SCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SCC3] = {
		.name = "fsl-cpm-scc",
		.id	= 3,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91A40,
				.end	= 0x91A5F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC3,
				.end	= SIU_INT_SCC3,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SCC4] = {
		.name = "fsl-cpm-scc",
		.id	= 4,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91A60,
				.end	= 0x91A7F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SCC4,
				.end	= SIU_INT_SCC4,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SPI] = {
		.name = "fsl-cpm-spi",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91AA0,
				.end	= 0x91AFF,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SPI,
				.end	= SIU_INT_SPI,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_MCC1] = {
		.name = "fsl-cpm-mcc",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91B30,
				.end	= 0x91B3F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_MCC1,
				.end	= SIU_INT_MCC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_MCC2] = {
		.name = "fsl-cpm-mcc",
		.id	= 2,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91B50,
				.end	= 0x91B5F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_MCC2,
				.end	= SIU_INT_MCC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SMC1] = {
		.name = "fsl-cpm-smc",
		.id	= 1,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91A80,
				.end	= 0x91A8F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SMC1,
				.end	= SIU_INT_SMC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_SMC2] = {
		.name = "fsl-cpm-smc",
		.id	= 2,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91A90,
				.end	= 0x91A9F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_SMC2,
				.end	= SIU_INT_SMC2,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_CPM_USB] = {
		.name = "fsl-cpm-usb",
		.id	= 2,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x91B60,
				.end	= 0x91B7F,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= SIU_INT_USB,
				.end	= SIU_INT_USB,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_eTSEC1] = {
		.name = "fsl-gianfar",
		.id	= 1,
		.dev.platform_data = &mpc85xx_etsec1_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_ENET1_OFFSET,
				.end	= MPC85xx_ENET1_OFFSET +
						MPC85xx_ENET1_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC85xx_IRQ_TSEC1_TX,
				.end	= MPC85xx_IRQ_TSEC1_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC85xx_IRQ_TSEC1_RX,
				.end	= MPC85xx_IRQ_TSEC1_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC85xx_IRQ_TSEC1_ERROR,
				.end	= MPC85xx_IRQ_TSEC1_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_eTSEC2] = {
		.name = "fsl-gianfar",
		.id	= 2,
		.dev.platform_data = &mpc85xx_etsec2_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_ENET2_OFFSET,
				.end	= MPC85xx_ENET2_OFFSET +
						MPC85xx_ENET2_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC85xx_IRQ_TSEC2_TX,
				.end	= MPC85xx_IRQ_TSEC2_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC85xx_IRQ_TSEC2_RX,
				.end	= MPC85xx_IRQ_TSEC2_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC85xx_IRQ_TSEC2_ERROR,
				.end	= MPC85xx_IRQ_TSEC2_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_eTSEC3] = {
		.name = "fsl-gianfar",
		.id	= 3,
		.dev.platform_data = &mpc85xx_etsec3_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= MPC85xx_ENET3_OFFSET,
				.end	= MPC85xx_ENET3_OFFSET +
						MPC85xx_ENET3_SIZE - 1,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC85xx_IRQ_TSEC3_TX,
				.end	= MPC85xx_IRQ_TSEC3_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC85xx_IRQ_TSEC3_RX,
				.end	= MPC85xx_IRQ_TSEC3_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC85xx_IRQ_TSEC3_ERROR,
				.end	= MPC85xx_IRQ_TSEC3_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_eTSEC4] = {
		.name = "fsl-gianfar",
		.id	= 4,
		.dev.platform_data = &mpc85xx_etsec4_pdata,
		.num_resources	 = 4,
		.resource = (struct resource[]) {
			{
				.start	= 0x27000,
				.end	= 0x27fff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.name	= "tx",
				.start	= MPC85xx_IRQ_TSEC4_TX,
				.end	= MPC85xx_IRQ_TSEC4_TX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "rx",
				.start	= MPC85xx_IRQ_TSEC4_RX,
				.end	= MPC85xx_IRQ_TSEC4_RX,
				.flags	= IORESOURCE_IRQ,
			},
			{
				.name	= "error",
				.start	= MPC85xx_IRQ_TSEC4_ERROR,
				.end	= MPC85xx_IRQ_TSEC4_ERROR,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_IIC2] = {
		.name = "fsl-i2c",
		.id	= 2,
		.dev.platform_data = &mpc85xx_fsl_i2c2_pdata,
		.num_resources	 = 2,
		.resource = (struct resource[]) {
			{
				.start	= 0x03100,
				.end	= 0x031ff,
				.flags	= IORESOURCE_MEM,
			},
			{
				.start	= MPC85xx_IRQ_IIC1,
				.end	= MPC85xx_IRQ_IIC1,
				.flags	= IORESOURCE_IRQ,
			},
		},
	},
	[MPC85xx_MDIO] = {
		.name = "fsl-gianfar_mdio",
		.id = 0,
		.dev.platform_data = &mpc85xx_mdio_pdata,
		.num_resources = 1,
		.resource = (struct resource[]) {
			{
				.start	= 0x24520,
				.end	= 0x2453f,
				.flags	= IORESOURCE_MEM,
			},
		},
	},
};

static int __init mach_mpc85xx_fixup(struct platform_device *pdev)
{
	ppc_sys_fixup_mem_resource(pdev, CCSRBAR);
	return 0;
}

static int __init mach_mpc85xx_init(void)
{
	ppc_sys_device_fixup = mach_mpc85xx_fixup;
	return 0;
}

postcore_initcall(mach_mpc85xx_init);
