/*
 * OMAP3 powerdomain definitions
 *
 * Copyright (C) 2007-2008, 2011 Texas Instruments, Inc.
 * Copyright (C) 2007-2011 Nokia Corporation
 *
 * Paul Walmsley, Jouni Högander
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>

#include "soc.h"
#include "powerdomain.h"
#include "powerdomains2xxx_3xxx_data.h"
#include "prcm-common.h"
#include "prm2xxx_3xxx.h"
#include "prm-regbits-34xx.h"
#include "cm2xxx_3xxx.h"
#include "cm-regbits-34xx.h"

/*
 * 34XX-specific powerdomains, dependencies
 */

/*
 * Powerdomains
 */

static struct powerdomain iva2_pwrdm = {
	.name		  = "iva2_pwrdm",
	.prcm_offs	  = OMAP3430_IVA2_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 4,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,
		[1] = PWRSTS_OFF_RET,
		[2] = PWRSTS_OFF_RET,
		[3] = PWRSTS_OFF_RET,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,
		[1] = PWRSTS_ON,
		[2] = PWRSTS_OFF_ON,
		[3] = PWRSTS_ON,
	},
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain mpu_3xxx_pwrdm = {
	.name		  = "mpu_pwrdm",
	.prcm_offs	  = MPU_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.flags		  = PWRDM_HAS_MPU_QUIRK,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_ON,
	},
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain mpu_am35x_pwrdm = {
	.name		  = "mpu_pwrdm",
	.prcm_offs	  = MPU_MOD,
	.pwrsts		  = PWRSTS_ON,
	.pwrsts_logic_ret = PWRSTS_ON,
	.flags		  = PWRDM_HAS_MPU_QUIRK,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_ON,
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,
	},
	.voltdm		  = { .name = "mpu_iva" },
};

/*
 * The USBTLL Save-and-Restore mechanism is broken on
 * 3430s up to ES3.0 and 3630ES1.0. Hence this feature
 * needs to be disabled on these chips.
 * Refer: 3430 errata ID i459 and 3630 errata ID i579
 *
 * Note: setting the SAR flag could help for errata ID i478
 *  which applies to 3430 <= ES3.1, but since the SAR feature
 *  is broken, do not use it.
 */
static struct powerdomain core_3xxx_pre_es3_1_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,	 /* MEM1RETSTATE */
		[1] = PWRSTS_OFF_RET,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_RET_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_OFF_RET_ON, /* MEM2ONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain core_3xxx_es3_1_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	/*
	 * Setting the SAR flag for errata ID i478 which applies
	 *  to 3430 <= ES3.1
	 */
	.flags		  = PWRDM_HAS_HDWR_SAR, /* for USBTLL only */
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_OFF_RET,	 /* MEM1RETSTATE */
		[1] = PWRSTS_OFF_RET,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_OFF_RET_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_OFF_RET_ON, /* MEM2ONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain core_am35x_pwrdm = {
	.name		  = "core_pwrdm",
	.prcm_offs	  = CORE_MOD,
	.pwrsts		  = PWRSTS_ON,
	.pwrsts_logic_ret = PWRSTS_ON,
	.banks		  = 2,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_ON,	 /* MEM1RETSTATE */
		[1] = PWRSTS_ON,	 /* MEM2RETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON, /* MEM1ONSTATE */
		[1] = PWRSTS_ON, /* MEM2ONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain dss_pwrdm = {
	.name		  = "dss_pwrdm",
	.prcm_offs	  = OMAP3430_DSS_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain dss_am35x_pwrdm = {
	.name		  = "dss_pwrdm",
	.prcm_offs	  = OMAP3430_DSS_MOD,
	.pwrsts		  = PWRSTS_ON,
	.pwrsts_logic_ret = PWRSTS_ON,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_ON, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

/*
 * Although the 34XX TRM Rev K Table 4-371 notes that retention is a
 * possible SGX powerstate, the SGX device itself does not support
 * retention.
 */
static struct powerdomain sgx_pwrdm = {
	.name		  = "sgx_pwrdm",
	.prcm_offs	  = OMAP3430ES2_SGX_MOD,
	/* XXX This is accurate for 3430 SGX, but what about GFX? */
	.pwrsts		  = PWRSTS_OFF_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain sgx_am35x_pwrdm = {
	.name		  = "sgx_pwrdm",
	.prcm_offs	  = OMAP3430ES2_SGX_MOD,
	.pwrsts		  = PWRSTS_ON,
	.pwrsts_logic_ret = PWRSTS_ON,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_ON, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain cam_pwrdm = {
	.name		  = "cam_pwrdm",
	.prcm_offs	  = OMAP3430_CAM_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain per_pwrdm = {
	.name		  = "per_pwrdm",
	.prcm_offs	  = OMAP3430_PER_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_OFF_RET,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain per_am35x_pwrdm = {
	.name		  = "per_pwrdm",
	.prcm_offs	  = OMAP3430_PER_MOD,
	.pwrsts		  = PWRSTS_ON,
	.pwrsts_logic_ret = PWRSTS_ON,
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_ON, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain emu_pwrdm = {
	.name		= "emu_pwrdm",
	.prcm_offs	= OMAP3430_EMU_MOD,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain neon_pwrdm = {
	.name		  = "neon_pwrdm",
	.prcm_offs	  = OMAP3430_NEON_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain neon_am35x_pwrdm = {
	.name		  = "neon_pwrdm",
	.prcm_offs	  = OMAP3430_NEON_MOD,
	.pwrsts		  = PWRSTS_ON,
	.pwrsts_logic_ret = PWRSTS_ON,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain usbhost_pwrdm = {
	.name		  = "usbhost_pwrdm",
	.prcm_offs	  = OMAP3430ES2_USBHOST_MOD,
	.pwrsts		  = PWRSTS_OFF_RET_ON,
	.pwrsts_logic_ret = PWRSTS_RET,
	/*
	 * REVISIT: Enabling usb host save and restore mechanism seems to
	 * leave the usb host domain permanently in ACTIVE mode after
	 * changing the usb host power domain state from OFF to active once.
	 * Disabling for now.
	 */
	/*.flags	  = PWRDM_HAS_HDWR_SAR,*/ /* for USBHOST ctrlr only */
	.banks		  = 1,
	.pwrsts_mem_ret	  = {
		[0] = PWRSTS_RET, /* MEMRETSTATE */
	},
	.pwrsts_mem_on	  = {
		[0] = PWRSTS_ON,  /* MEMONSTATE */
	},
	.voltdm		  = { .name = "core" },
};

static struct powerdomain dpll1_pwrdm = {
	.name		= "dpll1_pwrdm",
	.prcm_offs	= MPU_MOD,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain dpll2_pwrdm = {
	.name		= "dpll2_pwrdm",
	.prcm_offs	= OMAP3430_IVA2_MOD,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain dpll3_pwrdm = {
	.name		= "dpll3_pwrdm",
	.prcm_offs	= PLL_MOD,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain dpll4_pwrdm = {
	.name		= "dpll4_pwrdm",
	.prcm_offs	= PLL_MOD,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain dpll5_pwrdm = {
	.name		= "dpll5_pwrdm",
	.prcm_offs	= PLL_MOD,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain alwon_81xx_pwrdm = {
	.name		  = "alwon_pwrdm",
	.prcm_offs	  = TI81XX_PRM_ALWON_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain device_81xx_pwrdm = {
	.name		  = "device_pwrdm",
	.prcm_offs	  = TI81XX_PRM_DEVICE_MOD,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain gem_814x_pwrdm = {
	.name		= "gem_pwrdm",
	.prcm_offs	= TI814X_PRM_DSP_MOD,
	.pwrsts		= PWRSTS_OFF_ON,
	.voltdm		= { .name = "dsp" },
};

static struct powerdomain ivahd_814x_pwrdm = {
	.name		= "ivahd_pwrdm",
	.prcm_offs	= TI814X_PRM_HDVICP_MOD,
	.pwrsts		= PWRSTS_OFF_ON,
	.voltdm		= { .name = "iva" },
};

static struct powerdomain hdvpss_814x_pwrdm = {
	.name		= "hdvpss_pwrdm",
	.prcm_offs	= TI814X_PRM_HDVPSS_MOD,
	.pwrsts		= PWRSTS_OFF_ON,
	.voltdm		= { .name = "dsp" },
};

static struct powerdomain sgx_814x_pwrdm = {
	.name		= "sgx_pwrdm",
	.prcm_offs	= TI814X_PRM_GFX_MOD,
	.pwrsts		= PWRSTS_OFF_ON,
	.voltdm		= { .name = "core" },
};

static struct powerdomain isp_814x_pwrdm = {
	.name		= "isp_pwrdm",
	.prcm_offs	= TI814X_PRM_ISP_MOD,
	.pwrsts		= PWRSTS_OFF_ON,
	.voltdm		= { .name = "core" },
};

static struct powerdomain active_81xx_pwrdm = {
	.name		  = "active_pwrdm",
	.prcm_offs	  = TI816X_PRM_ACTIVE_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain default_81xx_pwrdm = {
	.name		  = "default_pwrdm",
	.prcm_offs	  = TI81XX_PRM_DEFAULT_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "core" },
};

static struct powerdomain ivahd0_816x_pwrdm = {
	.name		  = "ivahd0_pwrdm",
	.prcm_offs	  = TI816X_PRM_IVAHD0_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain ivahd1_816x_pwrdm = {
	.name		  = "ivahd1_pwrdm",
	.prcm_offs	  = TI816X_PRM_IVAHD1_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain ivahd2_816x_pwrdm = {
	.name		  = "ivahd2_pwrdm",
	.prcm_offs	  = TI816X_PRM_IVAHD2_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "mpu_iva" },
};

static struct powerdomain sgx_816x_pwrdm = {
	.name		  = "sgx_pwrdm",
	.prcm_offs	  = TI816X_PRM_SGX_MOD,
	.pwrsts		  = PWRSTS_OFF_ON,
	.voltdm		  = { .name = "core" },
};

/* As powerdomains are added or removed above, this list must also be changed */
static struct powerdomain *powerdomains_omap3430_common[] __initdata = {
	&wkup_omap2_pwrdm,
	&iva2_pwrdm,
	&mpu_3xxx_pwrdm,
	&neon_pwrdm,
	&cam_pwrdm,
	&dss_pwrdm,
	&per_pwrdm,
	&emu_pwrdm,
	&dpll1_pwrdm,
	&dpll2_pwrdm,
	&dpll3_pwrdm,
	&dpll4_pwrdm,
	NULL
};

static struct powerdomain *powerdomains_omap3430es1[] __initdata = {
	&gfx_omap2_pwrdm,
	&core_3xxx_pre_es3_1_pwrdm,
	NULL
};

/* also includes 3630ES1.0 */
static struct powerdomain *powerdomains_omap3430es2_es3_0[] __initdata = {
	&core_3xxx_pre_es3_1_pwrdm,
	&sgx_pwrdm,
	&usbhost_pwrdm,
	&dpll5_pwrdm,
	NULL
};

/* also includes 3630ES1.1+ */
static struct powerdomain *powerdomains_omap3430es3_1plus[] __initdata = {
	&core_3xxx_es3_1_pwrdm,
	&sgx_pwrdm,
	&usbhost_pwrdm,
	&dpll5_pwrdm,
	NULL
};

static struct powerdomain *powerdomains_am35x[] __initdata = {
	&wkup_omap2_pwrdm,
	&mpu_am35x_pwrdm,
	&neon_am35x_pwrdm,
	&core_am35x_pwrdm,
	&sgx_am35x_pwrdm,
	&dss_am35x_pwrdm,
	&per_am35x_pwrdm,
	&emu_pwrdm,
	&dpll1_pwrdm,
	&dpll3_pwrdm,
	&dpll4_pwrdm,
	&dpll5_pwrdm,
	NULL
};

static struct powerdomain *powerdomains_ti814x[] __initdata = {
	&alwon_81xx_pwrdm,
	&device_81xx_pwrdm,
	&active_81xx_pwrdm,
	&default_81xx_pwrdm,
	&gem_814x_pwrdm,
	&ivahd_814x_pwrdm,
	&hdvpss_814x_pwrdm,
	&sgx_814x_pwrdm,
	&isp_814x_pwrdm,
	NULL
};

static struct powerdomain *powerdomains_ti816x[] __initdata = {
	&alwon_81xx_pwrdm,
	&device_81xx_pwrdm,
	&active_81xx_pwrdm,
	&default_81xx_pwrdm,
	&ivahd0_816x_pwrdm,
	&ivahd1_816x_pwrdm,
	&ivahd2_816x_pwrdm,
	&sgx_816x_pwrdm,
	NULL
};

/* TI81XX specific ops */
#define TI81XX_PM_PWSTCTRL				0x0000
#define TI81XX_RM_RSTCTRL				0x0010
#define TI81XX_PM_PWSTST				0x0004

static int ti81xx_pwrdm_set_next_pwrst(struct powerdomain *pwrdm, u8 pwrst)
{
	omap2_prm_rmw_mod_reg_bits(OMAP_POWERSTATE_MASK,
				   (pwrst << OMAP_POWERSTATE_SHIFT),
				   pwrdm->prcm_offs, TI81XX_PM_PWSTCTRL);
	return 0;
}

static int ti81xx_pwrdm_read_next_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
					     TI81XX_PM_PWSTCTRL,
					     OMAP_POWERSTATE_MASK);
}

static int ti81xx_pwrdm_read_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
		(pwrdm->prcm_offs == TI814X_PRM_GFX_MOD) ? TI81XX_RM_RSTCTRL :
					     TI81XX_PM_PWSTST,
					     OMAP_POWERSTATEST_MASK);
}

static int ti81xx_pwrdm_read_logic_pwrst(struct powerdomain *pwrdm)
{
	return omap2_prm_read_mod_bits_shift(pwrdm->prcm_offs,
		(pwrdm->prcm_offs == TI814X_PRM_GFX_MOD) ? TI81XX_RM_RSTCTRL :
					     TI81XX_PM_PWSTST,
					     OMAP3430_LOGICSTATEST_MASK);
}

static int ti81xx_pwrdm_wait_transition(struct powerdomain *pwrdm)
{
	u32 c = 0;

	while ((omap2_prm_read_mod_reg(pwrdm->prcm_offs,
		(pwrdm->prcm_offs == TI814X_PRM_GFX_MOD) ? TI81XX_RM_RSTCTRL :
				       TI81XX_PM_PWSTST) &
		OMAP_INTRANSITION_MASK) &&
		(c++ < PWRDM_TRANSITION_BAILOUT))
			udelay(1);

	if (c > PWRDM_TRANSITION_BAILOUT) {
		pr_err("powerdomain: %s timeout waiting for transition\n",
		       pwrdm->name);
		return -EAGAIN;
	}

	pr_debug("powerdomain: completed transition in %d loops\n", c);

	return 0;
}

/* For dm814x we need to fix up fix GFX pwstst and rstctrl reg offsets */
static struct pwrdm_ops ti81xx_pwrdm_operations = {
	.pwrdm_set_next_pwrst	= ti81xx_pwrdm_set_next_pwrst,
	.pwrdm_read_next_pwrst	= ti81xx_pwrdm_read_next_pwrst,
	.pwrdm_read_pwrst	= ti81xx_pwrdm_read_pwrst,
	.pwrdm_read_logic_pwrst	= ti81xx_pwrdm_read_logic_pwrst,
	.pwrdm_wait_transition	= ti81xx_pwrdm_wait_transition,
};

void __init omap3xxx_powerdomains_init(void)
{
	unsigned int rev;

	if (!cpu_is_omap34xx() && !cpu_is_ti81xx())
		return;

	/* Only 81xx needs custom pwrdm_operations */
	if (!cpu_is_ti81xx())
		pwrdm_register_platform_funcs(&omap3_pwrdm_operations);

	rev = omap_rev();

	if (rev == AM35XX_REV_ES1_0 || rev == AM35XX_REV_ES1_1) {
		pwrdm_register_pwrdms(powerdomains_am35x);
	} else if (rev == TI8148_REV_ES1_0 || rev == TI8148_REV_ES2_0 ||
		   rev == TI8148_REV_ES2_1) {
		pwrdm_register_platform_funcs(&ti81xx_pwrdm_operations);
		pwrdm_register_pwrdms(powerdomains_ti814x);
	} else if (rev == TI8168_REV_ES1_0 || rev == TI8168_REV_ES1_1
			|| rev == TI8168_REV_ES2_0 || rev == TI8168_REV_ES2_1) {
		pwrdm_register_platform_funcs(&ti81xx_pwrdm_operations);
		pwrdm_register_pwrdms(powerdomains_ti816x);
	} else {
		pwrdm_register_pwrdms(powerdomains_omap3430_common);

		switch (rev) {
		case OMAP3430_REV_ES1_0:
			pwrdm_register_pwrdms(powerdomains_omap3430es1);
			break;
		case OMAP3430_REV_ES2_0:
		case OMAP3430_REV_ES2_1:
		case OMAP3430_REV_ES3_0:
		case OMAP3630_REV_ES1_0:
			pwrdm_register_pwrdms(powerdomains_omap3430es2_es3_0);
			break;
		case OMAP3430_REV_ES3_1:
		case OMAP3430_REV_ES3_1_2:
		case OMAP3630_REV_ES1_1:
		case OMAP3630_REV_ES1_2:
			pwrdm_register_pwrdms(powerdomains_omap3430es3_1plus);
			break;
		default:
			WARN(1, "OMAP3 powerdomain init: unknown chip type\n");
		}
	}

	pwrdm_complete_init();
}
