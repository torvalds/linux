/*
 * arch/ppc/platforms/mpc5200.c
 *
 * OCP Definitions for the boards based on MPC5200 processor. Contains
 * definitions for every common peripherals. (Mostly all but PSCs)
 * 
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Copyright 2004 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <asm/ocp.h>
#include <asm/mpc52xx.h>


static struct ocp_fs_i2c_data mpc5200_i2c_def = {
        .flags  = FS_I2C_CLOCK_5200,
};


/* Here is the core_ocp struct.
 * With all the devices common to all board. Even if port multiplexing is
 * not setup for them (if the user don't want them, just don't select the
 * config option). The potentially conflicting devices (like PSCs) goes in
 * board specific file.
 */
struct ocp_def core_ocp[] = {
	{
		.vendor         = OCP_VENDOR_FREESCALE,
		.function       = OCP_FUNC_IIC,
		.index          = 0,
		.paddr          = MPC52xx_I2C1,
		.irq            = OCP_IRQ_NA,   /* MPC52xx_IRQ_I2C1 - Buggy */
		.pm             = OCP_CPM_NA,
		.additions      = &mpc5200_i2c_def,
	},
	{
		.vendor         = OCP_VENDOR_FREESCALE,
		.function       = OCP_FUNC_IIC,
		.index          = 1,
		.paddr          = MPC52xx_I2C2,
		.irq            = OCP_IRQ_NA,   /* MPC52xx_IRQ_I2C2 - Buggy */
		.pm             = OCP_CPM_NA,
		.additions      = &mpc5200_i2c_def,
	},
	{	/* Terminating entry */
		.vendor		= OCP_VENDOR_INVALID
	}
};
