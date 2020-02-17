/*
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 *
 * Data common for AM335x and AM43x
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

#ifndef __ARCH_ARM_MACH_OMAP2_OMAP_HWMOD_33XX_43XX_COMMON_DATA_H
#define __ARCH_ARM_MACH_OMAP2_OMAP_HWMOD_33XX_43XX_COMMON_DATA_H

extern struct omap_hwmod_ocp_if am33xx_mpu__l3_main;
extern struct omap_hwmod_ocp_if am33xx_l3_main__l3_s;
extern struct omap_hwmod_ocp_if am33xx_l3_s__l4_ls;
extern struct omap_hwmod_ocp_if am33xx_l3_s__l4_wkup;
extern struct omap_hwmod_ocp_if am33xx_l3_main__l3_instr;
extern struct omap_hwmod_ocp_if am33xx_mpu__prcm;
extern struct omap_hwmod_ocp_if am33xx_l3_s__l3_main;
extern struct omap_hwmod_ocp_if am33xx_pruss__l3_main;
extern struct omap_hwmod_ocp_if am33xx_gfx__l3_main;
extern struct omap_hwmod_ocp_if am33xx_l3_main__gfx;
extern struct omap_hwmod_ocp_if am33xx_l4_wkup__rtc;
extern struct omap_hwmod_ocp_if am33xx_l3_s__gpmc;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer2;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tpcc;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tptc0;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tptc1;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tptc2;
extern struct omap_hwmod_ocp_if am33xx_l3_main__ocmc;

extern struct omap_hwmod am33xx_l3_main_hwmod;
extern struct omap_hwmod am33xx_l3_s_hwmod;
extern struct omap_hwmod am33xx_l3_instr_hwmod;
extern struct omap_hwmod am33xx_l4_ls_hwmod;
extern struct omap_hwmod am33xx_l4_wkup_hwmod;
extern struct omap_hwmod am33xx_mpu_hwmod;
extern struct omap_hwmod am33xx_pruss_hwmod;
extern struct omap_hwmod am33xx_gfx_hwmod;
extern struct omap_hwmod am33xx_prcm_hwmod;
extern struct omap_hwmod am33xx_ocmcram_hwmod;
extern struct omap_hwmod am33xx_smartreflex0_hwmod;
extern struct omap_hwmod am33xx_smartreflex1_hwmod;
extern struct omap_hwmod am33xx_gpmc_hwmod;
extern struct omap_hwmod am33xx_rtc_hwmod;
extern struct omap_hwmod am33xx_timer1_hwmod;
extern struct omap_hwmod am33xx_timer2_hwmod;
extern struct omap_hwmod am33xx_tpcc_hwmod;
extern struct omap_hwmod am33xx_tptc0_hwmod;
extern struct omap_hwmod am33xx_tptc1_hwmod;
extern struct omap_hwmod am33xx_tptc2_hwmod;

extern struct omap_hwmod_class am33xx_emif_hwmod_class;
extern struct omap_hwmod_class am33xx_l4_hwmod_class;
extern struct omap_hwmod_class am33xx_wkup_m3_hwmod_class;
extern struct omap_hwmod_class am33xx_control_hwmod_class;
extern struct omap_hwmod_class am33xx_timer_hwmod_class;
extern struct omap_hwmod_class am33xx_ehrpwm_hwmod_class;
extern struct omap_hwmod_class am33xx_spi_hwmod_class;

void omap_hwmod_am33xx_reg(void);
void omap_hwmod_am43xx_reg(void);

#endif
