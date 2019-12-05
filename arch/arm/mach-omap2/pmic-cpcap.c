// SPDX-License-Identifier: GPL-2.0-only
/*
 * pmic-cpcap.c - CPCAP-specific functions for the OPP code
 *
 * Adapted from Motorola Mapphone Android Linux kernel
 * Copyright (C) 2011 Motorola, Inc.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>

#include "soc.h"
#include "pm.h"
#include "voltage.h"

#include <linux/init.h>
#include "vc.h"

/**
 * omap_cpcap_vsel_to_vdc - convert CPCAP VSEL value to microvolts DC
 * @vsel: CPCAP VSEL value to convert
 *
 * Returns the microvolts DC that the CPCAP PMIC should generate when
 * programmed with @vsel.
 */
static unsigned long omap_cpcap_vsel_to_uv(unsigned char vsel)
{
	if (vsel > 0x44)
		vsel = 0x44;
	return (((vsel * 125) + 6000)) * 100;
}

/**
 * omap_cpcap_uv_to_vsel - convert microvolts DC to CPCAP VSEL value
 * @uv: microvolts DC to convert
 *
 * Returns the VSEL value necessary for the CPCAP PMIC to
 * generate an output voltage equal to or greater than @uv microvolts DC.
 */
static unsigned char omap_cpcap_uv_to_vsel(unsigned long uv)
{
	if (uv < 600000)
		uv = 600000;
	else if (uv > 1450000)
		uv = 1450000;
	return DIV_ROUND_UP(uv - 600000, 12500);
}

static struct omap_voltdm_pmic omap_cpcap_core = {
	.slew_rate = 4000,
	.step_size = 12500,
	.vp_erroroffset = OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin = OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax = OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin = 900000,
	.vddmax = 1350000,
	.vp_timeout_us = OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr = 0x02,
	.volt_reg_addr = 0x00,
	.cmd_reg_addr = 0x01,
	.i2c_high_speed = false,
	.vsel_to_uv = omap_cpcap_vsel_to_uv,
	.uv_to_vsel = omap_cpcap_uv_to_vsel,
};

static struct omap_voltdm_pmic omap_cpcap_iva = {
	.slew_rate = 4000,
	.step_size = 12500,
	.vp_erroroffset = OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin = OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax = OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin = 900000,
	.vddmax = 1350000,
	.vp_timeout_us = OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr = 0x44,
	.volt_reg_addr = 0x0,
	.cmd_reg_addr = 0x01,
	.i2c_high_speed = false,
	.vsel_to_uv = omap_cpcap_vsel_to_uv,
	.uv_to_vsel = omap_cpcap_uv_to_vsel,
};

/**
 * omap_max8952_vsel_to_vdc - convert MAX8952 VSEL value to microvolts DC
 * @vsel: MAX8952 VSEL value to convert
 *
 * Returns the microvolts DC that the MAX8952 Regulator should generate when
 * programmed with @vsel.
 */
static unsigned long omap_max8952_vsel_to_uv(unsigned char vsel)
{
	if (vsel > 0x3F)
		vsel = 0x3F;
	return (((vsel * 100) + 7700)) * 100;
}

/**
 * omap_max8952_uv_to_vsel - convert microvolts DC to MAX8952 VSEL value
 * @uv: microvolts DC to convert
 *
 * Returns the VSEL value necessary for the MAX8952 Regulator to
 * generate an output voltage equal to or greater than @uv microvolts DC.
 */
static unsigned char omap_max8952_uv_to_vsel(unsigned long uv)
{
	if (uv < 770000)
		uv = 770000;
	else if (uv > 1400000)
		uv = 1400000;
	return DIV_ROUND_UP(uv - 770000, 10000);
}

static struct omap_voltdm_pmic omap443x_max8952_mpu = {
	.slew_rate = 16000,
	.step_size = 10000,
	.vp_erroroffset = OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin = OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax = OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin = 900000,
	.vddmax = 1400000,
	.vp_timeout_us = OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr = 0x60,
	.volt_reg_addr = 0x03,
	.cmd_reg_addr = 0x03,
	.i2c_high_speed = false,
	.vsel_to_uv = omap_max8952_vsel_to_uv,
	.uv_to_vsel = omap_max8952_uv_to_vsel,
};

/**
 * omap_fan5355_vsel_to_vdc - convert FAN535503 VSEL value to microvolts DC
 * @vsel: FAN535503 VSEL value to convert
 *
 * Returns the microvolts DC that the FAN535503 Regulator should generate when
 * programmed with @vsel.
 */
static unsigned long omap_fan535503_vsel_to_uv(unsigned char vsel)
{
	/* Extract bits[5:0] */
	vsel &= 0x3F;

	return (((vsel * 125) + 7500)) * 100;
}

/**
 * omap_fan535508_vsel_to_vdc - convert FAN535508 VSEL value to microvolts DC
 * @vsel: FAN535508 VSEL value to convert
 *
 * Returns the microvolts DC that the FAN535508 Regulator should generate when
 * programmed with @vsel.
 */
static unsigned long omap_fan535508_vsel_to_uv(unsigned char vsel)
{
	/* Extract bits[5:0] */
	vsel &= 0x3F;

	if (vsel > 0x37)
		vsel = 0x37;
	return (((vsel * 125) + 7500)) * 100;
}


/**
 * omap_fan535503_uv_to_vsel - convert microvolts DC to FAN535503 VSEL value
 * @uv: microvolts DC to convert
 *
 * Returns the VSEL value necessary for the MAX8952 Regulator to
 * generate an output voltage equal to or greater than @uv microvolts DC.
 */
static unsigned char omap_fan535503_uv_to_vsel(unsigned long uv)
{
	unsigned char vsel;
	if (uv < 750000)
		uv = 750000;
	else if (uv > 1537500)
		uv = 1537500;

	vsel = DIV_ROUND_UP(uv - 750000, 12500);
	return vsel | 0xC0;
}

/**
 * omap_fan535508_uv_to_vsel - convert microvolts DC to FAN535508 VSEL value
 * @uv: microvolts DC to convert
 *
 * Returns the VSEL value necessary for the MAX8952 Regulator to
 * generate an output voltage equal to or greater than @uv microvolts DC.
 */
static unsigned char omap_fan535508_uv_to_vsel(unsigned long uv)
{
	unsigned char vsel;
	if (uv < 750000)
		uv = 750000;
	else if (uv > 1437500)
		uv = 1437500;

	vsel = DIV_ROUND_UP(uv - 750000, 12500);
	return vsel | 0xC0;
}

/* fan5335-core */
static struct omap_voltdm_pmic omap4_fan_core = {
	.slew_rate = 4000,
	.step_size = 12500,
	.vp_erroroffset = OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin = OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax = OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin = 850000,
	.vddmax = 1375000,
	.vp_timeout_us = OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr = 0x4A,
	.i2c_high_speed = false,
	.volt_reg_addr = 0x01,
	.cmd_reg_addr = 0x01,
	.vsel_to_uv = omap_fan535508_vsel_to_uv,
	.uv_to_vsel = omap_fan535508_uv_to_vsel,
};

/* fan5335 iva */
static struct omap_voltdm_pmic omap4_fan_iva = {
	.slew_rate = 4000,
	.step_size = 12500,
	.vp_erroroffset = OMAP4_VP_CONFIG_ERROROFFSET,
	.vp_vstepmin = OMAP4_VP_VSTEPMIN_VSTEPMIN,
	.vp_vstepmax = OMAP4_VP_VSTEPMAX_VSTEPMAX,
	.vddmin = 850000,
	.vddmax = 1375000,
	.vp_timeout_us = OMAP4_VP_VLIMITTO_TIMEOUT_US,
	.i2c_slave_addr = 0x48,
	.volt_reg_addr = 0x01,
	.cmd_reg_addr = 0x01,
	.i2c_high_speed = false,
	.vsel_to_uv = omap_fan535503_vsel_to_uv,
	.uv_to_vsel = omap_fan535503_uv_to_vsel,
};

int __init omap4_cpcap_init(void)
{
	struct voltagedomain *voltdm;

	if (!of_find_compatible_node(NULL, NULL, "motorola,cpcap"))
		return -ENODEV;

	voltdm = voltdm_lookup("mpu");
	omap_voltage_register_pmic(voltdm, &omap443x_max8952_mpu);

	if (of_machine_is_compatible("motorola,droid-bionic")) {
		voltdm = voltdm_lookup("mpu");
		omap_voltage_register_pmic(voltdm, &omap_cpcap_core);

		voltdm = voltdm_lookup("mpu");
		omap_voltage_register_pmic(voltdm, &omap_cpcap_iva);
	} else {
		voltdm = voltdm_lookup("core");
		omap_voltage_register_pmic(voltdm, &omap4_fan_core);

		voltdm = voltdm_lookup("iva");
		omap_voltage_register_pmic(voltdm, &omap4_fan_iva);
	}

	return 0;
}

static int __init cpcap_late_init(void)
{
	omap4_vc_set_pmic_signaling(PWRDM_POWER_RET);

	return 0;
}
omap_late_initcall(cpcap_late_init);
