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
extern struct omap_hwmod_ocp_if am33xx_l4_per__dcan0;
extern struct omap_hwmod_ocp_if am33xx_l4_per__dcan1;
extern struct omap_hwmod_ocp_if am33xx_l4_per__gpio1;
extern struct omap_hwmod_ocp_if am33xx_l4_per__gpio2;
extern struct omap_hwmod_ocp_if am33xx_l4_per__gpio3;
extern struct omap_hwmod_ocp_if am33xx_cpgmac0__mdio;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__elm;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__epwmss0;
extern struct omap_hwmod_ocp_if am33xx_epwmss0__ecap0;
extern struct omap_hwmod_ocp_if am33xx_epwmss0__eqep0;
extern struct omap_hwmod_ocp_if am33xx_epwmss0__ehrpwm0;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__epwmss1;
extern struct omap_hwmod_ocp_if am33xx_epwmss1__ecap1;
extern struct omap_hwmod_ocp_if am33xx_epwmss1__eqep1;
extern struct omap_hwmod_ocp_if am33xx_epwmss1__ehrpwm1;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__epwmss2;
extern struct omap_hwmod_ocp_if am33xx_epwmss2__ecap2;
extern struct omap_hwmod_ocp_if am33xx_epwmss2__eqep2;
extern struct omap_hwmod_ocp_if am33xx_epwmss2__ehrpwm2;
extern struct omap_hwmod_ocp_if am33xx_l3_s__gpmc;
extern struct omap_hwmod_ocp_if am33xx_l4_per__i2c2;
extern struct omap_hwmod_ocp_if am33xx_l4_per__i2c3;
extern struct omap_hwmod_ocp_if am33xx_l4_per__mailbox;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__spinlock;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__mcasp0;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__mcasp1;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__mmc0;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__mmc1;
extern struct omap_hwmod_ocp_if am33xx_l3_s__mmc2;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__mcspi0;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__mcspi1;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer2;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer3;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer4;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer5;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer6;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__timer7;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tpcc;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tptc0;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tptc1;
extern struct omap_hwmod_ocp_if am33xx_l3_main__tptc2;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__uart2;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__uart3;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__uart4;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__uart5;
extern struct omap_hwmod_ocp_if am33xx_l4_ls__uart6;
extern struct omap_hwmod_ocp_if am33xx_l3_main__ocmc;
extern struct omap_hwmod_ocp_if am33xx_l3_main__sha0;
extern struct omap_hwmod_ocp_if am33xx_l3_main__aes0;

extern struct omap_hwmod am33xx_l3_main_hwmod;
extern struct omap_hwmod am33xx_l3_s_hwmod;
extern struct omap_hwmod am33xx_l3_instr_hwmod;
extern struct omap_hwmod am33xx_l4_ls_hwmod;
extern struct omap_hwmod am33xx_l4_wkup_hwmod;
extern struct omap_hwmod am33xx_mpu_hwmod;
extern struct omap_hwmod am33xx_pruss_hwmod;
extern struct omap_hwmod am33xx_gfx_hwmod;
extern struct omap_hwmod am33xx_prcm_hwmod;
extern struct omap_hwmod am33xx_aes0_hwmod;
extern struct omap_hwmod am33xx_sha0_hwmod;
extern struct omap_hwmod am33xx_ocmcram_hwmod;
extern struct omap_hwmod am33xx_smartreflex0_hwmod;
extern struct omap_hwmod am33xx_smartreflex1_hwmod;
extern struct omap_hwmod am33xx_cpgmac0_hwmod;
extern struct omap_hwmod am33xx_mdio_hwmod;
extern struct omap_hwmod am33xx_dcan0_hwmod;
extern struct omap_hwmod am33xx_dcan1_hwmod;
extern struct omap_hwmod am33xx_elm_hwmod;
extern struct omap_hwmod am33xx_epwmss0_hwmod;
extern struct omap_hwmod am33xx_ecap0_hwmod;
extern struct omap_hwmod am33xx_eqep0_hwmod;
extern struct omap_hwmod am33xx_ehrpwm0_hwmod;
extern struct omap_hwmod am33xx_epwmss1_hwmod;
extern struct omap_hwmod am33xx_ecap1_hwmod;
extern struct omap_hwmod am33xx_eqep1_hwmod;
extern struct omap_hwmod am33xx_ehrpwm1_hwmod;
extern struct omap_hwmod am33xx_epwmss2_hwmod;
extern struct omap_hwmod am33xx_ecap2_hwmod;
extern struct omap_hwmod am33xx_eqep2_hwmod;
extern struct omap_hwmod am33xx_ehrpwm2_hwmod;
extern struct omap_hwmod am33xx_gpio1_hwmod;
extern struct omap_hwmod am33xx_gpio2_hwmod;
extern struct omap_hwmod am33xx_gpio3_hwmod;
extern struct omap_hwmod am33xx_gpmc_hwmod;
extern struct omap_hwmod am33xx_i2c1_hwmod;
extern struct omap_hwmod am33xx_i2c2_hwmod;
extern struct omap_hwmod am33xx_i2c3_hwmod;
extern struct omap_hwmod am33xx_mailbox_hwmod;
extern struct omap_hwmod am33xx_mcasp0_hwmod;
extern struct omap_hwmod am33xx_mcasp1_hwmod;
extern struct omap_hwmod am33xx_mmc0_hwmod;
extern struct omap_hwmod am33xx_mmc1_hwmod;
extern struct omap_hwmod am33xx_mmc2_hwmod;
extern struct omap_hwmod am33xx_rtc_hwmod;
extern struct omap_hwmod am33xx_spi0_hwmod;
extern struct omap_hwmod am33xx_spi1_hwmod;
extern struct omap_hwmod am33xx_spinlock_hwmod;
extern struct omap_hwmod am33xx_timer1_hwmod;
extern struct omap_hwmod am33xx_timer2_hwmod;
extern struct omap_hwmod am33xx_timer3_hwmod;
extern struct omap_hwmod am33xx_timer4_hwmod;
extern struct omap_hwmod am33xx_timer5_hwmod;
extern struct omap_hwmod am33xx_timer6_hwmod;
extern struct omap_hwmod am33xx_timer7_hwmod;
extern struct omap_hwmod am33xx_tpcc_hwmod;
extern struct omap_hwmod am33xx_tptc0_hwmod;
extern struct omap_hwmod am33xx_tptc1_hwmod;
extern struct omap_hwmod am33xx_tptc2_hwmod;
extern struct omap_hwmod am33xx_uart1_hwmod;
extern struct omap_hwmod am33xx_uart2_hwmod;
extern struct omap_hwmod am33xx_uart3_hwmod;
extern struct omap_hwmod am33xx_uart4_hwmod;
extern struct omap_hwmod am33xx_uart5_hwmod;
extern struct omap_hwmod am33xx_uart6_hwmod;
extern struct omap_hwmod am33xx_wd_timer1_hwmod;

extern struct omap_hwmod_class am33xx_l4_hwmod_class;
extern struct omap_hwmod_class am33xx_wkup_m3_hwmod_class;
extern struct omap_hwmod_class am33xx_control_hwmod_class;
extern struct omap_hwmod_class am33xx_gpio_hwmod_class;
extern struct omap_hwmod_class am33xx_timer_hwmod_class;
extern struct omap_hwmod_class am33xx_epwmss_hwmod_class;
extern struct omap_hwmod_class am33xx_ehrpwm_hwmod_class;
extern struct omap_hwmod_class am33xx_spi_hwmod_class;

extern struct omap_gpio_dev_attr gpio_dev_attr;
extern struct omap2_mcspi_dev_attr mcspi_attrib;

void omap_hwmod_am33xx_reg(void);
void omap_hwmod_am43xx_reg(void);

#endif
