/*
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * Interconnects common for AM335x and AM43x
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/sizes.h>
#include "omap_hwmod.h"
#include "omap_hwmod_33xx_43xx_common_data.h"

/* l3 main -> l3 s */
struct omap_hwmod_ocp_if am33xx_l3_main__l3_s = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_l3_s_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 s -> l4 per/ls */
struct omap_hwmod_ocp_if am33xx_l3_s__l4_ls = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_l4_ls_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 s -> l4 wkup */
struct omap_hwmod_ocp_if am33xx_l3_s__l4_wkup = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_l4_wkup_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 main -> l3 instr */
struct omap_hwmod_ocp_if am33xx_l3_main__l3_instr = {
	.master		= &am33xx_l3_main_hwmod,
	.slave		= &am33xx_l3_instr_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};

/* l3 s -> l3 main*/
struct omap_hwmod_ocp_if am33xx_l3_s__l3_main = {
	.master		= &am33xx_l3_s_hwmod,
	.slave		= &am33xx_l3_main_hwmod,
	.clk		= "l3s_gclk",
	.user		= OCP_USER_MPU | OCP_USER_SDMA,
};
