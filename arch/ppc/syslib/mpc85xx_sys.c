/*
 * arch/ppc/platforms/85xx/mpc85xx_sys.c
 *
 * MPC85xx System descriptions
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
#include <asm/ppc_sys.h>

struct ppc_sys_spec *cur_ppc_sys_spec;
struct ppc_sys_spec ppc_sys_specs[] = {
	{
		.ppc_sys_name	= "8540",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80300000,
		.num_devices	= 11,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_TSEC1, MPC85xx_TSEC2, MPC85xx_FEC, MPC85xx_IIC1,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8560",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80700000,
		.num_devices	= 20,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_TSEC1, MPC85xx_TSEC2, MPC85xx_IIC1,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON,
			MPC85xx_CPM_SPI, MPC85xx_CPM_I2C, MPC85xx_CPM_SCC1,
			MPC85xx_CPM_SCC2, MPC85xx_CPM_SCC3, MPC85xx_CPM_SCC4,
			MPC85xx_CPM_FCC1, MPC85xx_CPM_FCC2, MPC85xx_CPM_FCC3,
			MPC85xx_CPM_MCC1, MPC85xx_CPM_MCC2, MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8541",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80720000,
		.num_devices	= 14,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_TSEC1, MPC85xx_TSEC2, MPC85xx_IIC1,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART,
			MPC85xx_CPM_SPI, MPC85xx_CPM_I2C,
			MPC85xx_CPM_FCC1, MPC85xx_CPM_FCC2,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8541E",
		.mask 		= 0xFFFF0000,
		.value 		= 0x807A0000,
		.num_devices	= 15,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_TSEC1, MPC85xx_TSEC2, MPC85xx_IIC1,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_SEC2,
			MPC85xx_CPM_SPI, MPC85xx_CPM_I2C,
			MPC85xx_CPM_FCC1, MPC85xx_CPM_FCC2,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8555",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80710000,
		.num_devices	= 20,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_TSEC1, MPC85xx_TSEC2, MPC85xx_IIC1,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART,
			MPC85xx_CPM_SPI, MPC85xx_CPM_I2C, MPC85xx_CPM_SCC1,
			MPC85xx_CPM_SCC2, MPC85xx_CPM_SCC3,
			MPC85xx_CPM_FCC1, MPC85xx_CPM_FCC2,
			MPC85xx_CPM_SMC1, MPC85xx_CPM_SMC2,
			MPC85xx_CPM_USB,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8555E",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80790000,
		.num_devices	= 21,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_TSEC1, MPC85xx_TSEC2, MPC85xx_IIC1,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_SEC2,
			MPC85xx_CPM_SPI, MPC85xx_CPM_I2C, MPC85xx_CPM_SCC1,
			MPC85xx_CPM_SCC2, MPC85xx_CPM_SCC3,
			MPC85xx_CPM_FCC1, MPC85xx_CPM_FCC2,
			MPC85xx_CPM_SMC1, MPC85xx_CPM_SMC2,
			MPC85xx_CPM_USB,
			MPC85xx_MDIO,
		},
	},
	/* SVRs on 8548 rev1.0 matches for 8548/8547/8545 */
	{
		.ppc_sys_name	= "8548E",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80390010,
		.num_devices	= 14,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2, MPC85xx_eTSEC3,
			MPC85xx_eTSEC4, MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_SEC2,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8548",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80310010,
		.num_devices	= 13,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2, MPC85xx_eTSEC3,
			MPC85xx_eTSEC4, MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8547E",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80390010,
		.num_devices	= 14,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2, MPC85xx_eTSEC3,
			MPC85xx_eTSEC4, MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_SEC2,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8547",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80310010,
		.num_devices	= 13,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2, MPC85xx_eTSEC3,
			MPC85xx_eTSEC4, MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8545E",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80390010,
		.num_devices	= 12,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2,
			MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_SEC2,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8545",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80310010,
		.num_devices	= 11,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2,
			MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8543E",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x803A0010,
		.num_devices	= 12,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2,
			MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART, MPC85xx_SEC2,
			MPC85xx_MDIO,
		},
	},
	{
		.ppc_sys_name	= "8543",
		.mask 		= 0xFFFF00F0,
		.value 		= 0x80320010,
		.num_devices	= 11,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC85xx_eTSEC1, MPC85xx_eTSEC2,
			MPC85xx_IIC1, MPC85xx_IIC2,
			MPC85xx_DMA0, MPC85xx_DMA1, MPC85xx_DMA2, MPC85xx_DMA3,
			MPC85xx_PERFMON, MPC85xx_DUART,
			MPC85xx_MDIO,
		},
	},
	{	/* default match */
		.ppc_sys_name	= "",
		.mask 		= 0x00000000,
		.value 		= 0x00000000,
	},
};
