/*
 * MPC8xx System descriptions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 MontaVista Software, Inc. by Vitaly Bordug <vbordug@ru.mvista.com>
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
		.ppc_sys_name	= "MPC86X",
		.mask 		= 0xFFFFFFFF,
		.value 		= 0x00000000,
		.num_devices	= 8,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC8xx_CPM_FEC1,
			MPC8xx_CPM_SCC1,
			MPC8xx_CPM_SCC2,
			MPC8xx_CPM_SCC3,
			MPC8xx_CPM_SCC4,
			MPC8xx_CPM_SMC1,
			MPC8xx_CPM_SMC2,
			MPC8xx_MDIO_FEC,
		},
	},
	{
		.ppc_sys_name	= "MPC885",
		.mask 		= 0xFFFFFFFF,
		.value 		= 0x00000000,
		.num_devices	= 9,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC8xx_CPM_FEC1,
			MPC8xx_CPM_FEC2,
			MPC8xx_CPM_SCC1,
			MPC8xx_CPM_SCC2,
			MPC8xx_CPM_SCC3,
			MPC8xx_CPM_SCC4,
			MPC8xx_CPM_SMC1,
			MPC8xx_CPM_SMC2,
			MPC8xx_MDIO_FEC,
		},
	},
	{	/* default match */
		.ppc_sys_name	= "",
		.mask 		= 0x00000000,
		.value 		= 0x00000000,
	},
};
