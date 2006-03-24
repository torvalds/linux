/*
 * MPC83xx System descriptions
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
		.ppc_sys_name	= "8349E",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80500000,
		.num_devices	= 9,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART, MPC83xx_SEC2,
			MPC83xx_USB2_DR, MPC83xx_USB2_MPH, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8349",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80510000,
		.num_devices	= 8,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART,
			MPC83xx_USB2_DR, MPC83xx_USB2_MPH, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8347E",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80520000,
		.num_devices	= 9,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART, MPC83xx_SEC2,
			MPC83xx_USB2_DR, MPC83xx_USB2_MPH, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8347",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80530000,
		.num_devices	= 8,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART,
			MPC83xx_USB2_DR, MPC83xx_USB2_MPH, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8347E",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80540000,
		.num_devices	= 9,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART, MPC83xx_SEC2,
			MPC83xx_USB2_DR, MPC83xx_USB2_MPH, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8347",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80550000,
		.num_devices	= 8,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART,
			MPC83xx_USB2_DR, MPC83xx_USB2_MPH, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8343E",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80560000,
		.num_devices	= 8,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART, MPC83xx_SEC2,
			MPC83xx_USB2_DR, MPC83xx_MDIO
		},
	},
	{
		.ppc_sys_name	= "8343",
		.mask 		= 0xFFFF0000,
		.value 		= 0x80570000,
		.num_devices	= 7,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC83xx_TSEC1, MPC83xx_TSEC2, MPC83xx_IIC1,
			MPC83xx_IIC2, MPC83xx_DUART,
			MPC83xx_USB2_DR, MPC83xx_MDIO
		},
	},
	{	/* default match */
		.ppc_sys_name	= "",
		.mask 		= 0x00000000,
		.value 		= 0x00000000,
	},
};
