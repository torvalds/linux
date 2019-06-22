// SPDX-License-Identifier: GPL-2.0-only
/*
 * AM43xx Clock domains framework
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 */

#include <linux/kernel.h>
#include <linux/io.h>

#include "clockdomain.h"
#include "prcm44xx.h"
#include "prcm43xx.h"

static struct clockdomain l4_cefuse_43xx_clkdm = {
	.name		  = "l4_cefuse_clkdm",
	.pwrdm		  = { .name = "cefuse_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_CEFUSE_INST,
	.clkdm_offs	  = AM43XX_CM_CEFUSE_CEFUSE_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain mpu_43xx_clkdm = {
	.name		  = "mpu_clkdm",
	.pwrdm		  = { .name = "mpu_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_MPU_INST,
	.clkdm_offs	  = AM43XX_CM_MPU_MPU_CDOFFS,
	.flags		  = CLKDM_CAN_HWSUP_SWSUP,
};

static struct clockdomain l4ls_43xx_clkdm = {
	.name		  = "l4ls_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_L4LS_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain tamper_43xx_clkdm = {
	.name		  = "tamper_clkdm",
	.pwrdm		  = { .name = "tamper_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_TAMPER_INST,
	.clkdm_offs	  = AM43XX_CM_TAMPER_TAMPER_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain l4_rtc_43xx_clkdm = {
	.name		  = "l4_rtc_clkdm",
	.pwrdm		  = { .name = "rtc_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_RTC_INST,
	.clkdm_offs	  = AM43XX_CM_RTC_RTC_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain pruss_ocp_43xx_clkdm = {
	.name		  = "pruss_ocp_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_ICSS_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain ocpwp_l3_43xx_clkdm = {
	.name		  = "ocpwp_l3_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_OCPWP_L3_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain l3s_tsc_43xx_clkdm = {
	.name		  = "l3s_tsc_clkdm",
	.pwrdm		  = { .name = "wkup_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_WKUP_INST,
	.clkdm_offs	  = AM43XX_CM_WKUP_L3S_TSC_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain dss_43xx_clkdm = {
	.name		  = "dss_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_DSS_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain l3_aon_43xx_clkdm = {
	.name		  = "l3_aon_clkdm",
	.pwrdm		  = { .name = "wkup_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_WKUP_INST,
	.clkdm_offs	  = AM43XX_CM_WKUP_L3_AON_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain emif_43xx_clkdm = {
	.name		  = "emif_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_EMIF_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain l4_wkup_aon_43xx_clkdm = {
	.name		  = "l4_wkup_aon_clkdm",
	.pwrdm		  = { .name = "wkup_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_WKUP_INST,
	.clkdm_offs	  = AM43XX_CM_WKUP_L4_WKUP_AON_CDOFFS,
};

static struct clockdomain l3_43xx_clkdm = {
	.name		  = "l3_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_L3_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain l4_wkup_43xx_clkdm = {
	.name		  = "l4_wkup_clkdm",
	.pwrdm		  = { .name = "wkup_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_WKUP_INST,
	.clkdm_offs	  = AM43XX_CM_WKUP_WKUP_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain cpsw_125mhz_43xx_clkdm = {
	.name		  = "cpsw_125mhz_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_CPSW_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain gfx_l3_43xx_clkdm = {
	.name		  = "gfx_l3_clkdm",
	.pwrdm		  = { .name = "gfx_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_GFX_INST,
	.clkdm_offs	  = AM43XX_CM_GFX_GFX_L3_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain l3s_43xx_clkdm = {
	.name		  = "l3s_clkdm",
	.pwrdm		  = { .name = "per_pwrdm" },
	.prcm_partition	  = AM43XX_CM_PARTITION,
	.cm_inst	  = AM43XX_CM_PER_INST,
	.clkdm_offs	  = AM43XX_CM_PER_L3S_CDOFFS,
	.flags		  = CLKDM_CAN_SWSUP,
};

static struct clockdomain *clockdomains_am43xx[] __initdata = {
	&l4_cefuse_43xx_clkdm,
	&mpu_43xx_clkdm,
	&l4ls_43xx_clkdm,
	&tamper_43xx_clkdm,
	&l4_rtc_43xx_clkdm,
	&pruss_ocp_43xx_clkdm,
	&ocpwp_l3_43xx_clkdm,
	&l3s_tsc_43xx_clkdm,
	&dss_43xx_clkdm,
	&l3_aon_43xx_clkdm,
	&emif_43xx_clkdm,
	&l4_wkup_aon_43xx_clkdm,
	&l3_43xx_clkdm,
	&l4_wkup_43xx_clkdm,
	&cpsw_125mhz_43xx_clkdm,
	&gfx_l3_43xx_clkdm,
	&l3s_43xx_clkdm,
	NULL
};

void __init am43xx_clockdomains_init(void)
{
	clkdm_register_platform_funcs(&am43xx_clkdm_operations);
	clkdm_register_clkdms(clockdomains_am43xx);
	clkdm_complete_init();
}
