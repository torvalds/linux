/*
 * Freescale MPC52xx system descriptions
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

#include <asm/ppc_sys.h>

struct ppc_sys_spec *cur_ppc_sys_spec;
struct ppc_sys_spec ppc_sys_specs[] = {
	{
		.ppc_sys_name	= "5200",
		.mask		= 0xffff0000,
		.value		= 0x80110000,
		.num_devices	= 15,
		.device_list	= (enum ppc_sys_devices[])
		{
			MPC52xx_MSCAN1, MPC52xx_MSCAN2, MPC52xx_SPI,
			MPC52xx_USB, MPC52xx_BDLC, MPC52xx_PSC1, MPC52xx_PSC2,
			MPC52xx_PSC3, MPC52xx_PSC4, MPC52xx_PSC5, MPC52xx_PSC6,
			MPC52xx_FEC, MPC52xx_ATA, MPC52xx_I2C1, MPC52xx_I2C2,
		},
	},
	{	/* default match */
		.ppc_sys_name	= "",
		.mask		= 0x00000000,
		.value		= 0x00000000,
	},
};
