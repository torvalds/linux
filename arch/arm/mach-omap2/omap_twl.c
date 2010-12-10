/**
 * OMAP and TWL PMIC specific intializations.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated.
 * Thara Gopinath
 * Copyright (C) 2009 Texas Instruments Incorporated.
 * Nishanth Menon
 * Copyright (C) 2009 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include <plat/voltage.h>

#define OMAP3_SRI2C_SLAVE_ADDR		0x12
#define OMAP3_VDD_MPU_SR_CONTROL_REG	0x00
#define OMAP3_VDD_CORE_SR_CONTROL_REG	0x01
#define OMAP3_VP_CONFIG_ERROROFFSET	0x00
#define OMAP3_VP_VSTEPMIN_VSTEPMIN	0x1
#define OMAP3_VP_VSTEPMAX_VSTEPMAX	0x04
#define OMAP3_VP_VLIMITTO_TIMEOUT_US	200

#define OMAP3430_VP1_VLIMITTO_VDDMIN	0x14
#define OMAP3430_VP1_VLIMITTO_VDDMAX	0x42
#define OMAP3430_VP2_VLIMITTO_VDDMIN	0x18
#define OMAP3430_VP2_VLIMITTO_VDDMAX	0x2c

#define OMAP3630_VP1_VLIMITTO_VDDMIN	0x18
#define OMAP3630_VP1_VLIMITTO_VDDMAX	0x3c
#define OMAP3630_VP2_VLIMITTO_VDDMIN	0x18
#define OMAP3630_VP2_VLIMITTO_VDDMAX	0x30

unsigned long twl4030_vsel_to_uv(const u8 vsel)
{
	return (((vsel * 125) + 6000)) * 100;
}

u8 twl4030_uv_to_vsel(unsigned long uv)
{
	return DIV_ROUND_UP(uv - 600000, 12500);
}

static struct omap_volt_pmic_info omap3_mpu_volt_info = {
	.slew_rate		= 4000,
	.step_size		= 12500,
	.on_volt		= 1200000,
	.onlp_volt		= 1000000,
	.ret_volt		= 975000,
	.off_volt		= 600000,
	.volt_setup_time	= 0xfff,
	.vp_erroroffset		= OMAP3_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP3_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP3_VP_VSTEPMAX_VSTEPMAX,
	.vp_vddmin		= OMAP3430_VP1_VLIMITTO_VDDMIN,
	.vp_vddmax		= OMAP3430_VP1_VLIMITTO_VDDMAX,
	.vp_timeout_us		= OMAP3_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP3_SRI2C_SLAVE_ADDR,
	.pmic_reg		= OMAP3_VDD_MPU_SR_CONTROL_REG,
	.vsel_to_uv		= twl4030_vsel_to_uv,
	.uv_to_vsel		= twl4030_uv_to_vsel,
};

static struct omap_volt_pmic_info omap3_core_volt_info = {
	.slew_rate		= 4000,
	.step_size		= 12500,
	.on_volt                = 1200000,
	.onlp_volt              = 1000000,
	.ret_volt               = 975000,
	.off_volt               = 600000,
	.volt_setup_time        = 0xfff,
	.vp_erroroffset		= OMAP3_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP3_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP3_VP_VSTEPMAX_VSTEPMAX,
	.vp_vddmin		= OMAP3430_VP2_VLIMITTO_VDDMIN,
	.vp_vddmax		= OMAP3430_VP2_VLIMITTO_VDDMAX,
	.vp_timeout_us		= OMAP3_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP3_SRI2C_SLAVE_ADDR,
	.pmic_reg		= OMAP3_VDD_CORE_SR_CONTROL_REG,
	.vsel_to_uv		= twl4030_vsel_to_uv,
	.uv_to_vsel		= twl4030_uv_to_vsel,
};

int __init omap3_twl_init(void)
{
	struct voltagedomain *voltdm;

	if (!cpu_is_omap34xx())
		return -ENODEV;

	if (cpu_is_omap3630()) {
		omap3_mpu_volt_info.vp_vddmin = OMAP3630_VP1_VLIMITTO_VDDMIN;
		omap3_mpu_volt_info.vp_vddmax = OMAP3630_VP1_VLIMITTO_VDDMAX;
		omap3_core_volt_info.vp_vddmin = OMAP3630_VP2_VLIMITTO_VDDMIN;
		omap3_core_volt_info.vp_vddmax = OMAP3630_VP2_VLIMITTO_VDDMAX;
	}

	voltdm = omap_voltage_domain_lookup("mpu");
	omap_voltage_register_pmic(voltdm, &omap3_mpu_volt_info);

	voltdm = omap_voltage_domain_lookup("core");
	omap_voltage_register_pmic(voltdm, &omap3_core_volt_info);

	return 0;
}
