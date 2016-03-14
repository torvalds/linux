/*
 * TI81XX Clock Domain data.
 *
 * Copyright (C) 2010 Texas Instruments, Inc. - http://www.ti.com/
 * Copyright (C) 2013 SKTB SKiT, http://www.skitlab.ru/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCKDOMAINS_81XX_H
#define __ARCH_ARM_MACH_OMAP2_CLOCKDOMAINS_81XX_H

#include <linux/kernel.h>
#include <linux/io.h>

#include "clockdomain.h"
#include "cm81xx.h"

/*
 * Note that 814x seems to have HWSUP_SWSUP for many clockdomains
 * while 816x does not. According to the TRM, 816x only has HWSUP
 * for ALWON_L3_FAST. Also note that the TI tree clockdomains81xx.h
 * seems to have the related ifdef the wrong way around claiming
 * 816x supports HWSUP while 814x does not. For now, we only set
 * HWSUP for ALWON_L3_FAST as that seems to be supported for both
 * dm814x and dm816x.
 */

/* Common for 81xx */

static struct clockdomain alwon_l3_slow_81xx_clkdm = {
	.name		= "alwon_l3s_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_ALWON_L3_SLOW_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain alwon_l3_med_81xx_clkdm = {
	.name		= "alwon_l3_med_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_ALWON_L3_MED_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain alwon_l3_fast_81xx_clkdm = {
	.name		= "alwon_l3_fast_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_ALWON_L3_FAST_CLKDM,
	.flags		= CLKDM_CAN_HWSUP_SWSUP,
};

static struct clockdomain alwon_ethernet_81xx_clkdm = {
	.name		= "alwon_ethernet_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_ETHERNET_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain mmu_81xx_clkdm = {
	.name		= "mmu_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_MMU_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain mmu_cfg_81xx_clkdm = {
	.name		= "mmu_cfg_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_MMUCFG_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain default_l3_slow_81xx_clkdm = {
	.name		= "default_l3_slow_clkdm",
	.pwrdm		= { .name = "default_pwrdm" },
	.cm_inst	= TI81XX_CM_DEFAULT_MOD,
	.clkdm_offs	= TI816X_CM_DEFAULT_L3_SLOW_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

/* 816x only */

static struct clockdomain alwon_mpu_816x_clkdm = {
	.name		= "alwon_mpu_clkdm",
	.pwrdm		= { .name = "alwon_pwrdm" },
	.cm_inst	= TI81XX_CM_ALWON_MOD,
	.clkdm_offs	= TI81XX_CM_ALWON_MPU_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain active_gem_816x_clkdm = {
	.name		= "active_gem_clkdm",
	.pwrdm		= { .name = "active_pwrdm" },
	.cm_inst	= TI81XX_CM_ACTIVE_MOD,
	.clkdm_offs	= TI816X_CM_ACTIVE_GEM_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain ivahd0_816x_clkdm = {
	.name		= "ivahd0_clkdm",
	.pwrdm		= { .name = "ivahd0_pwrdm" },
	.cm_inst	= TI816X_CM_IVAHD0_MOD,
	.clkdm_offs	= TI816X_CM_IVAHD0_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain ivahd1_816x_clkdm = {
	.name		= "ivahd1_clkdm",
	.pwrdm		= { .name = "ivahd1_pwrdm" },
	.cm_inst	= TI816X_CM_IVAHD1_MOD,
	.clkdm_offs	= TI816X_CM_IVAHD1_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain ivahd2_816x_clkdm = {
	.name		= "ivahd2_clkdm",
	.pwrdm		= { .name = "ivahd2_pwrdm" },
	.cm_inst	= TI816X_CM_IVAHD2_MOD,
	.clkdm_offs	= TI816X_CM_IVAHD2_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain sgx_816x_clkdm = {
	.name		= "sgx_clkdm",
	.pwrdm		= { .name = "sgx_pwrdm" },
	.cm_inst	= TI81XX_CM_SGX_MOD,
	.clkdm_offs	= TI816X_CM_SGX_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain default_l3_med_816x_clkdm = {
	.name		= "default_l3_med_clkdm",
	.pwrdm		= { .name = "default_pwrdm" },
	.cm_inst	= TI81XX_CM_DEFAULT_MOD,
	.clkdm_offs	= TI816X_CM_DEFAULT_L3_MED_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain default_ducati_816x_clkdm = {
	.name		= "default_ducati_clkdm",
	.pwrdm		= { .name = "default_pwrdm" },
	.cm_inst	= TI81XX_CM_DEFAULT_MOD,
	.clkdm_offs	= TI816X_CM_DEFAULT_DUCATI_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain default_pci_816x_clkdm = {
	.name		= "default_pci_clkdm",
	.pwrdm		= { .name = "default_pwrdm" },
	.cm_inst	= TI81XX_CM_DEFAULT_MOD,
	.clkdm_offs	= TI816X_CM_DEFAULT_PCI_CLKDM,
	.flags		= CLKDM_CAN_SWSUP,
};

static struct clockdomain *clockdomains_ti814x[] __initdata = {
	&alwon_l3_slow_81xx_clkdm,
	&alwon_l3_med_81xx_clkdm,
	&alwon_l3_fast_81xx_clkdm,
	&alwon_ethernet_81xx_clkdm,
	&mmu_81xx_clkdm,
	&mmu_cfg_81xx_clkdm,
	&default_l3_slow_81xx_clkdm,
	NULL,
};

void __init ti814x_clockdomains_init(void)
{
	clkdm_register_platform_funcs(&am33xx_clkdm_operations);
	clkdm_register_clkdms(clockdomains_ti814x);
	clkdm_complete_init();
}

static struct clockdomain *clockdomains_ti816x[] __initdata = {
	&alwon_mpu_816x_clkdm,
	&alwon_l3_slow_81xx_clkdm,
	&alwon_l3_med_81xx_clkdm,
	&alwon_l3_fast_81xx_clkdm,
	&alwon_ethernet_81xx_clkdm,
	&mmu_81xx_clkdm,
	&mmu_cfg_81xx_clkdm,
	&active_gem_816x_clkdm,
	&ivahd0_816x_clkdm,
	&ivahd1_816x_clkdm,
	&ivahd2_816x_clkdm,
	&sgx_816x_clkdm,
	&default_l3_med_816x_clkdm,
	&default_ducati_816x_clkdm,
	&default_pci_816x_clkdm,
	&default_l3_slow_81xx_clkdm,
	NULL,
};

void __init ti816x_clockdomains_init(void)
{
	clkdm_register_platform_funcs(&am33xx_clkdm_operations);
	clkdm_register_clkdms(clockdomains_ti816x);
	clkdm_complete_init();
}
#endif
