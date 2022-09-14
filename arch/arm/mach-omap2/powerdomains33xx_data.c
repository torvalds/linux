// SPDX-License-Identifier: GPL-2.0-only
/*
 * AM33XX Power domain data
 *
 * Copyright (C) 2011-2012 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include "powerdomain.h"
#include "prcm-common.h"
#include "prm-regbits-33xx.h"
#include "prm33xx.h"

static struct powerdomain gfx_33xx_pwrdm = {
	.name			= "gfx_pwrdm",
	.voltdm			= { .name = "core" },
	.prcm_offs		= AM33XX_PRM_GFX_MOD,
	.pwrstctrl_offs		= AM33XX_PM_GFX_PWRSTCTRL_OFFSET,
	.pwrstst_offs		= AM33XX_PM_GFX_PWRSTST_OFFSET,
	.pwrsts			= PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret	= PWRSTS_OFF_RET,
	.flags			= PWRDM_HAS_LOWPOWERSTATECHANGE,
	.banks			= 1,
	.logicretstate_mask	= AM33XX_LOGICRETSTATE_MASK,
	.mem_on_mask		= {
		[0]		= AM33XX_GFX_MEM_ONSTATE_MASK,	/* gfx_mem */
	},
	.mem_ret_mask		= {
		[0]		= AM33XX_GFX_MEM_RETSTATE_MASK,	/* gfx_mem */
	},
	.mem_pwrst_mask		= {
		[0]		= AM33XX_GFX_MEM_STATEST_MASK,	/* gfx_mem */
	},
	.mem_retst_mask		= {
		[0]		= AM33XX_GFX_MEM_RETSTATE_MASK,	/* gfx_mem */
	},
	.pwrsts_mem_ret		= {
		[0]		= PWRSTS_OFF_RET,	/* gfx_mem */
	},
	.pwrsts_mem_on		= {
		[0]		= PWRSTS_ON,		/* gfx_mem */
	},
};

static struct powerdomain rtc_33xx_pwrdm = {
	.name			= "rtc_pwrdm",
	.voltdm			= { .name = "rtc" },
	.prcm_offs		= AM33XX_PRM_RTC_MOD,
	.pwrstctrl_offs		= AM33XX_PM_RTC_PWRSTCTRL_OFFSET,
	.pwrstst_offs		= AM33XX_PM_RTC_PWRSTST_OFFSET,
	.pwrsts			= PWRSTS_ON,
	.logicretstate_mask	= AM33XX_LOGICRETSTATE_MASK,
};

static struct powerdomain wkup_33xx_pwrdm = {
	.name			= "wkup_pwrdm",
	.voltdm			= { .name = "core" },
	.prcm_offs		= AM33XX_PRM_WKUP_MOD,
	.pwrstctrl_offs		= AM33XX_PM_WKUP_PWRSTCTRL_OFFSET,
	.pwrstst_offs		= AM33XX_PM_WKUP_PWRSTST_OFFSET,
	.pwrsts			= PWRSTS_ON,
	.logicretstate_mask	= AM33XX_LOGICRETSTATE_3_3_MASK,
};

static struct powerdomain per_33xx_pwrdm = {
	.name			= "per_pwrdm",
	.voltdm			= { .name = "core" },
	.prcm_offs		= AM33XX_PRM_PER_MOD,
	.pwrstctrl_offs		= AM33XX_PM_PER_PWRSTCTRL_OFFSET,
	.pwrstst_offs		= AM33XX_PM_PER_PWRSTST_OFFSET,
	.pwrsts			= PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret	= PWRSTS_OFF_RET,
	.flags			= PWRDM_HAS_LOWPOWERSTATECHANGE,
	.banks			= 3,
	.logicretstate_mask	= AM33XX_LOGICRETSTATE_3_3_MASK,
	.mem_on_mask		= {
		[0]		= AM33XX_PRUSS_MEM_ONSTATE_MASK, /* pruss_mem */
		[1]		= AM33XX_PER_MEM_ONSTATE_MASK,	/* per_mem */
		[2]		= AM33XX_RAM_MEM_ONSTATE_MASK,	/* ram_mem */
	},
	.mem_ret_mask		= {
		[0]		= AM33XX_PRUSS_MEM_RETSTATE_MASK, /* pruss_mem */
		[1]		= AM33XX_PER_MEM_RETSTATE_MASK,	/* per_mem */
		[2]		= AM33XX_RAM_MEM_RETSTATE_MASK,	/* ram_mem */
	},
	.mem_pwrst_mask		= {
		[0]		= AM33XX_PRUSS_MEM_STATEST_MASK, /* pruss_mem */
		[1]		= AM33XX_PER_MEM_STATEST_MASK,	/* per_mem */
		[2]		= AM33XX_RAM_MEM_STATEST_MASK,	/* ram_mem */
	},
	.mem_retst_mask		= {
		[0]		= AM33XX_PRUSS_MEM_RETSTATE_MASK, /* pruss_mem */
		[1]		= AM33XX_PER_MEM_RETSTATE_MASK,	/* per_mem */
		[2]		= AM33XX_RAM_MEM_RETSTATE_MASK,	/* ram_mem */
	},
	.pwrsts_mem_ret		= {
		[0]		= PWRSTS_OFF_RET,	/* pruss_mem */
		[1]		= PWRSTS_OFF_RET,	/* per_mem */
		[2]		= PWRSTS_OFF_RET,	/* ram_mem */
	},
	.pwrsts_mem_on		= {
		[0]		= PWRSTS_ON,		/* pruss_mem */
		[1]		= PWRSTS_ON,		/* per_mem */
		[2]		= PWRSTS_ON,		/* ram_mem */
	},
};

static struct powerdomain mpu_33xx_pwrdm = {
	.name			= "mpu_pwrdm",
	.voltdm			= { .name = "mpu" },
	.prcm_offs		= AM33XX_PRM_MPU_MOD,
	.pwrstctrl_offs		= AM33XX_PM_MPU_PWRSTCTRL_OFFSET,
	.pwrstst_offs		= AM33XX_PM_MPU_PWRSTST_OFFSET,
	.pwrsts			= PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret	= PWRSTS_OFF_RET,
	.flags			= PWRDM_HAS_LOWPOWERSTATECHANGE,
	.banks			= 3,
	.logicretstate_mask	= AM33XX_LOGICRETSTATE_MASK,
	.mem_on_mask		= {
		[0]		= AM33XX_MPU_L1_ONSTATE_MASK,	/* mpu_l1 */
		[1]		= AM33XX_MPU_L2_ONSTATE_MASK,	/* mpu_l2 */
		[2]		= AM33XX_MPU_RAM_ONSTATE_MASK,	/* mpu_ram */
	},
	.mem_ret_mask		= {
		[0]		= AM33XX_MPU_L1_RETSTATE_MASK,	/* mpu_l1 */
		[1]		= AM33XX_MPU_L2_RETSTATE_MASK,	/* mpu_l2 */
		[2]		= AM33XX_MPU_RAM_RETSTATE_MASK,	/* mpu_ram */
	},
	.mem_pwrst_mask		= {
		[0]		= AM33XX_MPU_L1_STATEST_MASK,	/* mpu_l1 */
		[1]		= AM33XX_MPU_L2_STATEST_MASK,	/* mpu_l2 */
		[2]		= AM33XX_MPU_RAM_STATEST_MASK,	/* mpu_ram */
	},
	.mem_retst_mask		= {
		[0]		= AM33XX_MPU_L1_RETSTATE_MASK,	/* mpu_l1 */
		[1]		= AM33XX_MPU_L2_RETSTATE_MASK,	/* mpu_l2 */
		[2]		= AM33XX_MPU_RAM_RETSTATE_MASK,	/* mpu_ram */
	},
	.pwrsts_mem_ret		= {
		[0]		= PWRSTS_OFF_RET,	/* mpu_l1 */
		[1]		= PWRSTS_OFF_RET,	/* mpu_l2 */
		[2]		= PWRSTS_OFF_RET,	/* mpu_ram */
	},
	.pwrsts_mem_on		= {
		[0]		= PWRSTS_ON,		/* mpu_l1 */
		[1]		= PWRSTS_ON,		/* mpu_l2 */
		[2]		= PWRSTS_ON,		/* mpu_ram */
	},
};

static struct powerdomain cefuse_33xx_pwrdm = {
	.name		= "cefuse_pwrdm",
	.voltdm		= { .name = "core" },
	.prcm_offs	= AM33XX_PRM_CEFUSE_MOD,
	.pwrstctrl_offs	= AM33XX_PM_CEFUSE_PWRSTCTRL_OFFSET,
	.pwrstst_offs	= AM33XX_PM_CEFUSE_PWRSTST_OFFSET,
	.pwrsts		= PWRSTS_OFF_ON,
};

static struct powerdomain *powerdomains_am33xx[] __initdata = {
	&gfx_33xx_pwrdm,
	&rtc_33xx_pwrdm,
	&wkup_33xx_pwrdm,
	&per_33xx_pwrdm,
	&mpu_33xx_pwrdm,
	&cefuse_33xx_pwrdm,
	NULL,
};

void __init am33xx_powerdomains_init(void)
{
	pwrdm_register_platform_funcs(&am33xx_pwrdm_operations);
	pwrdm_register_pwrdms(powerdomains_am33xx);
	pwrdm_complete_init();
}
