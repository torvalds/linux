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
#include <linux/i2c/twl.h>

#include "soc.h"
#include "voltage.h"

#include "pm.h"

#define OMAP3_SRI2C_SLAVE_ADDR		0x12
#define OMAP3_VDD_MPU_SR_CONTROL_REG	0x00
#define OMAP3_VDD_CORE_SR_CONTROL_REG	0x01
#define OMAP3_VP_CONFIG_ERROROFFSET	0x00
#define OMAP3_VP_VSTEPMIN_VSTEPMIN	0x1
#define OMAP3_VP_VSTEPMAX_VSTEPMAX	0x04
#define OMAP3_VP_VLIMITTO_TIMEOUT_US	200

#define OMAP4_SRI2C_SLAVE_ADDR		0x12
#define OMAP4_VDD_MPU_SR_VOLT_REG	0x55
#define OMAP4_VDD_MPU_SR_CMD_REG	0x56
#define OMAP4_VDD_IVA_SR_VOLT_REG	0x5B
#define OMAP4_VDD_IVA_SR_CMD_REG	0x5C
#define OMAP4_VDD_CORE_SR_VOLT_REG	0x61
#define OMAP4_VDD_CORE_SR_CMD_REG	0x62

#define OMAP4_VP_CONFIG_ERROROFFSET	0x00
#define OMAP4_VP_VSTEPMIN_VSTEPMIN	0x01
#define OMAP4_VP_VSTEPMAX_VSTEPMAX	0x04
#define OMAP4_VP_VLIMITTO_TIMEOUT_US	200

static bool is_offset_valid;
static u8 smps_offset;

#define REG_SMPS_OFFSET         0xE0

static unsigned long twl4030_vsel_to_uv(const u8 vsel)
{
	return (((vsel * 125) + 6000)) * 100;
}

static u8 twl4030_uv_to_vsel(unsigned long uv)
{
	return DIV_ROUND_UP(uv - 600000, 12500);
}

static unsigned long twl6030_vsel_to_uv(const u8 vsel)
{
	/*
	 * In TWL6030 depending on the value of SMPS_OFFSET
	 * efuse register the voltage range supported in
	 * standard mode can be either between 0.6V - 1.3V or
	 * 0.7V - 1.4V. In TWL6030 ES1.0 SMPS_OFFSET efuse
	 * is programmed to all 0's where as starting from
	 * TWL6030 ES1.1 the efuse is programmed to 1
	 */
	if (!is_offset_valid) {
		twl_i2c_read_u8(TWL6030_MODULE_ID0, &smps_offset,
				REG_SMPS_OFFSET);
		is_offset_valid = true;
	}

	if (!vsel)
		return 0;
	/*
	 * There is no specific formula for voltage to vsel
	 * conversion above 1.3V. There are special hardcoded
	 * values for voltages above 1.3V. Currently we are
	 * hardcoding only for 1.35 V which is used for 1GH OPP for
	 * OMAP4430.
	 */
	if (vsel == 0x3A)
		return 1350000;

	if (smps_offset & 0x8)
		return ((((vsel - 1) * 1266) + 70900)) * 10;
	else
		return ((((vsel - 1) * 1266) + 60770)) * 10;
}

static u8 twl6030_uv_to_vsel(unsigned long uv)
{
	/*
	 * In TWL6030 depending on the value of SMPS_OFFSET
	 * efuse register the voltage range supported in
	 * standard mode can be either between 0.6V - 1.3V or
	 * 0.7V - 1.4V. In TWL6030 ES1.0 SMPS_OFFSET efuse
	 * is programmed to all 0's where as starting from
	 * TWL6030 ES1.1 the efuse is programmed to 1
	 */
	if (!is_offset_valid) {
		twl_i2c_read_u8(TWL6030_MODULE_ID0, &smps_offset,
				REG_SMPS_OFFSET);
		is_offset_valid = true;
	}

	if (!uv)
		return 0x00;
	/*
	 * There is no specific formula for voltage to vsel
	 * conversion above 1.3V. There are special hardcoded
	 * values for voltages above 1.3V. Currently we are
	 * hardcoding only for 1.35 V which is used for 1GH OPP for
	 * OMAP4430.
	 */
	if (uv > twl6030_vsel_to_uv(0x39)) {
		if (uv == 1350000)
			return 0x3A;
		pr_err("%s:OUT OF RANGE! non mapped vsel for %ld Vs max %ld\n",
			__func__, uv, twl6030_vsel_to_uv(0x39));
		return 0x3A;
	}

	if (smps_offset & 0x8)
		return DIV_ROUND_UP(uv - 709000, 12660) + 1;
	else
		return DIV_ROUND_UP(uv - 607700, 12660) + 1;
}

static struct omap_voltdm_pmic omap3_mpu_pmic = {
	.slew_rate		= 4000,
	.step_size		= 12500,
	.vp_erroroffset		= OMAP3_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP3_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP3_VP_VSTEPMAX_VSTEPMAX,
	.vddmin			= 600000,
	.vddmax			= 1450000,
	.vp_timeout_us		= OMAP3_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP3_SRI2C_SLAVE_ADDR,
	.volt_reg_addr		= OMAP3_VDD_MPU_SR_CONTROL_REG,
	.i2c_high_speed		= true,
	.vsel_to_uv		= twl4030_vsel_to_uv,
	.uv_to_vsel		= twl4030_uv_to_vsel,
};

static struct omap_voltdm_pmic omap3_core_pmic = {
	.slew_rate		= 4000,
	.step_size		= 12500,
	.vp_erroroffset		= OMAP3_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP3_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP3_VP_VSTEPMAX_VSTEPMAX,
	.vddmin			= 600000,
	.vddmax			= 1450000,
	.vp_timeout_us		= OMAP3_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP3_SRI2C_SLAVE_ADDR,
	.volt_reg_addr		= OMAP3_VDD_CORE_SR_CONTROL_REG,
	.i2c_high_speed		= true,
	.vsel_to_uv		= twl4030_vsel_to_uv,
	.uv_to_vsel		= twl4030_uv_to_vsel,
};

static struct omap_voltdm_pmic omap4_mpu_pmic = {
	.slew_rate		= 4000,
	.step_size		= 12660,
	.vp_erroroffset		= OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin			= 0,
	.vddmax			= 2100000,
	.vp_timeout_us		= OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP4_SRI2C_SLAVE_ADDR,
	.volt_reg_addr		= OMAP4_VDD_MPU_SR_VOLT_REG,
	.cmd_reg_addr		= OMAP4_VDD_MPU_SR_CMD_REG,
	.i2c_high_speed		= true,
	.i2c_pad_load		= 3,
	.vsel_to_uv		= twl6030_vsel_to_uv,
	.uv_to_vsel		= twl6030_uv_to_vsel,
};

static struct omap_voltdm_pmic omap4_iva_pmic = {
	.slew_rate		= 4000,
	.step_size		= 12660,
	.vp_erroroffset		= OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin			= 0,
	.vddmax			= 2100000,
	.vp_timeout_us		= OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP4_SRI2C_SLAVE_ADDR,
	.volt_reg_addr		= OMAP4_VDD_IVA_SR_VOLT_REG,
	.cmd_reg_addr		= OMAP4_VDD_IVA_SR_CMD_REG,
	.i2c_high_speed		= true,
	.i2c_pad_load		= 3,
	.vsel_to_uv		= twl6030_vsel_to_uv,
	.uv_to_vsel		= twl6030_uv_to_vsel,
};

static struct omap_voltdm_pmic omap4_core_pmic = {
	.slew_rate		= 4000,
	.step_size		= 12660,
	.vp_erroroffset		= OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin		= OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax		= OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin			= 0,
	.vddmax			= 2100000,
	.vp_timeout_us		= OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr		= OMAP4_SRI2C_SLAVE_ADDR,
	.volt_reg_addr		= OMAP4_VDD_CORE_SR_VOLT_REG,
	.cmd_reg_addr		= OMAP4_VDD_CORE_SR_CMD_REG,
	.i2c_high_speed		= true,
	.i2c_pad_load		= 3,
	.vsel_to_uv		= twl6030_vsel_to_uv,
	.uv_to_vsel		= twl6030_uv_to_vsel,
};

int __init omap4_twl_init(void)
{
	struct voltagedomain *voltdm;

	if (!cpu_is_omap44xx())
		return -ENODEV;

	voltdm = voltdm_lookup("mpu");
	omap_voltage_register_pmic(voltdm, &omap4_mpu_pmic);

	voltdm = voltdm_lookup("iva");
	omap_voltage_register_pmic(voltdm, &omap4_iva_pmic);

	voltdm = voltdm_lookup("core");
	omap_voltage_register_pmic(voltdm, &omap4_core_pmic);

	return 0;
}

int __init omap3_twl_init(void)
{
	struct voltagedomain *voltdm;

	if (!cpu_is_omap34xx())
		return -ENODEV;

	voltdm = voltdm_lookup("mpu_iva");
	omap_voltage_register_pmic(voltdm, &omap3_mpu_pmic);

	voltdm = voltdm_lookup("core");
	omap_voltage_register_pmic(voltdm, &omap3_core_pmic);

	return 0;
}
