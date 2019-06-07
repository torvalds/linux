// SPDX-License-Identifier: GPL-2.0+
//
// wm8350.c  --  Voltage and current regulation for the Wolfson WM8350 PMIC
//
// Copyright 2007, 2008 Wolfson Microelectronics PLC.
//
// Author: Liam Girdwood
//         linux@wolfsonmicro.com

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/pmic.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

/* Maximum value possible for VSEL */
#define WM8350_DCDC_MAX_VSEL 0x66

/* Microamps */
static const unsigned int isink_cur[] = {
	4,
	5,
	6,
	7,
	8,
	10,
	11,
	14,
	16,
	19,
	23,
	27,
	32,
	39,
	46,
	54,
	65,
	77,
	92,
	109,
	130,
	154,
	183,
	218,
	259,
	308,
	367,
	436,
	518,
	616,
	733,
	872,
	1037,
	1233,
	1466,
	1744,
	2073,
	2466,
	2933,
	3487,
	4147,
	4932,
	5865,
	6975,
	8294,
	9864,
	11730,
	13949,
	16589,
	19728,
	23460,
	27899,
	33178,
	39455,
	46920,
	55798,
	66355,
	78910,
	93840,
	111596,
	132710,
	157820,
	187681,
	223191
};

/* turn on ISINK followed by DCDC */
static int wm8350_isink_enable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int isink = rdev_get_id(rdev);

	switch (isink) {
	case WM8350_ISINK_A:
		switch (wm8350->pmic.isink_A_dcdc) {
		case WM8350_DCDC_2:
		case WM8350_DCDC_5:
			wm8350_set_bits(wm8350, WM8350_POWER_MGMT_7,
					WM8350_CS1_ENA);
			wm8350_set_bits(wm8350, WM8350_CSA_FLASH_CONTROL,
					WM8350_CS1_DRIVE);
			wm8350_set_bits(wm8350, WM8350_DCDC_LDO_REQUESTED,
					1 << (wm8350->pmic.isink_A_dcdc -
					      WM8350_DCDC_1));
			break;
		default:
			return -EINVAL;
		}
		break;
	case WM8350_ISINK_B:
		switch (wm8350->pmic.isink_B_dcdc) {
		case WM8350_DCDC_2:
		case WM8350_DCDC_5:
			wm8350_set_bits(wm8350, WM8350_POWER_MGMT_7,
					WM8350_CS2_ENA);
			wm8350_set_bits(wm8350, WM8350_CSB_FLASH_CONTROL,
					WM8350_CS2_DRIVE);
			wm8350_set_bits(wm8350, WM8350_DCDC_LDO_REQUESTED,
					1 << (wm8350->pmic.isink_B_dcdc -
					      WM8350_DCDC_1));
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int wm8350_isink_disable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int isink = rdev_get_id(rdev);

	switch (isink) {
	case WM8350_ISINK_A:
		switch (wm8350->pmic.isink_A_dcdc) {
		case WM8350_DCDC_2:
		case WM8350_DCDC_5:
			wm8350_clear_bits(wm8350, WM8350_DCDC_LDO_REQUESTED,
					  1 << (wm8350->pmic.isink_A_dcdc -
						WM8350_DCDC_1));
			wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_7,
					  WM8350_CS1_ENA);
			break;
		default:
			return -EINVAL;
		}
		break;
	case WM8350_ISINK_B:
		switch (wm8350->pmic.isink_B_dcdc) {
		case WM8350_DCDC_2:
		case WM8350_DCDC_5:
			wm8350_clear_bits(wm8350, WM8350_DCDC_LDO_REQUESTED,
					  1 << (wm8350->pmic.isink_B_dcdc -
						WM8350_DCDC_1));
			wm8350_clear_bits(wm8350, WM8350_POWER_MGMT_7,
					  WM8350_CS2_ENA);
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int wm8350_isink_is_enabled(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int isink = rdev_get_id(rdev);

	switch (isink) {
	case WM8350_ISINK_A:
		return wm8350_reg_read(wm8350, WM8350_CURRENT_SINK_DRIVER_A) &
		    0x8000;
	case WM8350_ISINK_B:
		return wm8350_reg_read(wm8350, WM8350_CURRENT_SINK_DRIVER_B) &
		    0x8000;
	}
	return -EINVAL;
}

static int wm8350_isink_enable_time(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int isink = rdev_get_id(rdev);
	int reg;

	switch (isink) {
	case WM8350_ISINK_A:
		reg = wm8350_reg_read(wm8350, WM8350_CSA_FLASH_CONTROL);
		break;
	case WM8350_ISINK_B:
		reg = wm8350_reg_read(wm8350, WM8350_CSB_FLASH_CONTROL);
		break;
	default:
		return -EINVAL;
	}

	if (reg & WM8350_CS1_FLASH_MODE) {
		switch (reg & WM8350_CS1_ON_RAMP_MASK) {
		case 0:
			return 0;
		case 1:
			return 1950;
		case 2:
			return 3910;
		case 3:
			return 7800;
		}
	} else {
		switch (reg & WM8350_CS1_ON_RAMP_MASK) {
		case 0:
			return 0;
		case 1:
			return 250000;
		case 2:
			return 500000;
		case 3:
			return 1000000;
		}
	}

	return -EINVAL;
}


int wm8350_isink_set_flash(struct wm8350 *wm8350, int isink, u16 mode,
			   u16 trigger, u16 duration, u16 on_ramp, u16 off_ramp,
			   u16 drive)
{
	switch (isink) {
	case WM8350_ISINK_A:
		wm8350_reg_write(wm8350, WM8350_CSA_FLASH_CONTROL,
				 (mode ? WM8350_CS1_FLASH_MODE : 0) |
				 (trigger ? WM8350_CS1_TRIGSRC : 0) |
				 duration | on_ramp | off_ramp | drive);
		break;
	case WM8350_ISINK_B:
		wm8350_reg_write(wm8350, WM8350_CSB_FLASH_CONTROL,
				 (mode ? WM8350_CS2_FLASH_MODE : 0) |
				 (trigger ? WM8350_CS2_TRIGSRC : 0) |
				 duration | on_ramp | off_ramp | drive);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_isink_set_flash);

static int wm8350_dcdc_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int sel, volt_reg, dcdc = rdev_get_id(rdev);
	u16 val;

	dev_dbg(wm8350->dev, "%s %d mV %d\n", __func__, dcdc, uV / 1000);

	switch (dcdc) {
	case WM8350_DCDC_1:
		volt_reg = WM8350_DCDC1_LOW_POWER;
		break;
	case WM8350_DCDC_3:
		volt_reg = WM8350_DCDC3_LOW_POWER;
		break;
	case WM8350_DCDC_4:
		volt_reg = WM8350_DCDC4_LOW_POWER;
		break;
	case WM8350_DCDC_6:
		volt_reg = WM8350_DCDC6_LOW_POWER;
		break;
	case WM8350_DCDC_2:
	case WM8350_DCDC_5:
	default:
		return -EINVAL;
	}

	sel = regulator_map_voltage_linear(rdev, uV, uV);
	if (sel < 0)
		return sel;

	/* all DCDCs have same mV bits */
	val = wm8350_reg_read(wm8350, volt_reg) & ~WM8350_DC1_VSEL_MASK;
	wm8350_reg_write(wm8350, volt_reg, val | sel);
	return 0;
}

static int wm8350_dcdc_set_suspend_enable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 val;

	switch (dcdc) {
	case WM8350_DCDC_1:
		val = wm8350_reg_read(wm8350, WM8350_DCDC1_LOW_POWER)
			& ~WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC1_LOW_POWER,
			val | wm8350->pmic.dcdc1_hib_mode);
		break;
	case WM8350_DCDC_3:
		val = wm8350_reg_read(wm8350, WM8350_DCDC3_LOW_POWER)
			& ~WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC3_LOW_POWER,
			val | wm8350->pmic.dcdc3_hib_mode);
		break;
	case WM8350_DCDC_4:
		val = wm8350_reg_read(wm8350, WM8350_DCDC4_LOW_POWER)
			& ~WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC4_LOW_POWER,
			val | wm8350->pmic.dcdc4_hib_mode);
		break;
	case WM8350_DCDC_6:
		val = wm8350_reg_read(wm8350, WM8350_DCDC6_LOW_POWER)
			& ~WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC6_LOW_POWER,
			val | wm8350->pmic.dcdc6_hib_mode);
		break;
	case WM8350_DCDC_2:
	case WM8350_DCDC_5:
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8350_dcdc_set_suspend_disable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 val;

	switch (dcdc) {
	case WM8350_DCDC_1:
		val = wm8350_reg_read(wm8350, WM8350_DCDC1_LOW_POWER);
		wm8350->pmic.dcdc1_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC1_LOW_POWER,
				 val | WM8350_DCDC_HIB_MODE_DIS);
		break;
	case WM8350_DCDC_3:
		val = wm8350_reg_read(wm8350, WM8350_DCDC3_LOW_POWER);
		wm8350->pmic.dcdc3_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC3_LOW_POWER,
				 val | WM8350_DCDC_HIB_MODE_DIS);
		break;
	case WM8350_DCDC_4:
		val = wm8350_reg_read(wm8350, WM8350_DCDC4_LOW_POWER);
		wm8350->pmic.dcdc4_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC4_LOW_POWER,
				 val | WM8350_DCDC_HIB_MODE_DIS);
		break;
	case WM8350_DCDC_6:
		val = wm8350_reg_read(wm8350, WM8350_DCDC6_LOW_POWER);
		wm8350->pmic.dcdc6_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC6_LOW_POWER,
				 val | WM8350_DCDC_HIB_MODE_DIS);
		break;
	case WM8350_DCDC_2:
	case WM8350_DCDC_5:
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8350_dcdc25_set_suspend_enable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 val;

	switch (dcdc) {
	case WM8350_DCDC_2:
		val = wm8350_reg_read(wm8350, WM8350_DCDC2_CONTROL)
		    & ~WM8350_DC2_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC2_CONTROL, val |
		    (WM8350_DC2_HIB_MODE_ACTIVE << WM8350_DC2_HIB_MODE_SHIFT));
		break;
	case WM8350_DCDC_5:
		val = wm8350_reg_read(wm8350, WM8350_DCDC5_CONTROL)
		    & ~WM8350_DC5_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC5_CONTROL, val |
		    (WM8350_DC5_HIB_MODE_ACTIVE << WM8350_DC5_HIB_MODE_SHIFT));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int wm8350_dcdc25_set_suspend_disable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 val;

	switch (dcdc) {
	case WM8350_DCDC_2:
		val = wm8350_reg_read(wm8350, WM8350_DCDC2_CONTROL)
		    & ~WM8350_DC2_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC2_CONTROL, val |
		    (WM8350_DC2_HIB_MODE_DISABLE << WM8350_DC2_HIB_MODE_SHIFT));
		break;
	case WM8350_DCDC_5:
		val = wm8350_reg_read(wm8350, WM8350_DCDC5_CONTROL)
		    & ~WM8350_DC5_HIB_MODE_MASK;
		wm8350_reg_write(wm8350, WM8350_DCDC5_CONTROL, val |
		    (WM8350_DC5_HIB_MODE_DISABLE << WM8350_DC5_HIB_MODE_SHIFT));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int wm8350_dcdc_set_suspend_mode(struct regulator_dev *rdev,
	unsigned int mode)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 *hib_mode;

	switch (dcdc) {
	case WM8350_DCDC_1:
		hib_mode = &wm8350->pmic.dcdc1_hib_mode;
		break;
	case WM8350_DCDC_3:
		hib_mode = &wm8350->pmic.dcdc3_hib_mode;
		break;
	case WM8350_DCDC_4:
		hib_mode = &wm8350->pmic.dcdc4_hib_mode;
		break;
	case WM8350_DCDC_6:
		hib_mode = &wm8350->pmic.dcdc6_hib_mode;
		break;
	case WM8350_DCDC_2:
	case WM8350_DCDC_5:
	default:
		return -EINVAL;
	}

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		*hib_mode = WM8350_DCDC_HIB_MODE_IMAGE;
		break;
	case REGULATOR_MODE_IDLE:
		*hib_mode = WM8350_DCDC_HIB_MODE_STANDBY;
		break;
	case REGULATOR_MODE_STANDBY:
		*hib_mode = WM8350_DCDC_HIB_MODE_LDO_IM;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct regulator_linear_range wm8350_ldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(900000, 0, 15, 50000),
	REGULATOR_LINEAR_RANGE(1800000, 16, 31, 100000),
};

static int wm8350_ldo_set_suspend_voltage(struct regulator_dev *rdev, int uV)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int sel, volt_reg, ldo = rdev_get_id(rdev);
	u16 val;

	dev_dbg(wm8350->dev, "%s %d mV %d\n", __func__, ldo, uV / 1000);

	switch (ldo) {
	case WM8350_LDO_1:
		volt_reg = WM8350_LDO1_LOW_POWER;
		break;
	case WM8350_LDO_2:
		volt_reg = WM8350_LDO2_LOW_POWER;
		break;
	case WM8350_LDO_3:
		volt_reg = WM8350_LDO3_LOW_POWER;
		break;
	case WM8350_LDO_4:
		volt_reg = WM8350_LDO4_LOW_POWER;
		break;
	default:
		return -EINVAL;
	}

	sel = regulator_map_voltage_linear_range(rdev, uV, uV);
	if (sel < 0)
		return sel;

	/* all LDOs have same mV bits */
	val = wm8350_reg_read(wm8350, volt_reg) & ~WM8350_LDO1_VSEL_MASK;
	wm8350_reg_write(wm8350, volt_reg, val | sel);
	return 0;
}

static int wm8350_ldo_set_suspend_enable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int volt_reg, ldo = rdev_get_id(rdev);
	u16 val;

	switch (ldo) {
	case WM8350_LDO_1:
		volt_reg = WM8350_LDO1_LOW_POWER;
		break;
	case WM8350_LDO_2:
		volt_reg = WM8350_LDO2_LOW_POWER;
		break;
	case WM8350_LDO_3:
		volt_reg = WM8350_LDO3_LOW_POWER;
		break;
	case WM8350_LDO_4:
		volt_reg = WM8350_LDO4_LOW_POWER;
		break;
	default:
		return -EINVAL;
	}

	/* all LDOs have same mV bits */
	val = wm8350_reg_read(wm8350, volt_reg) & ~WM8350_LDO1_HIB_MODE_MASK;
	wm8350_reg_write(wm8350, volt_reg, val);
	return 0;
}

static int wm8350_ldo_set_suspend_disable(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int volt_reg, ldo = rdev_get_id(rdev);
	u16 val;

	switch (ldo) {
	case WM8350_LDO_1:
		volt_reg = WM8350_LDO1_LOW_POWER;
		break;
	case WM8350_LDO_2:
		volt_reg = WM8350_LDO2_LOW_POWER;
		break;
	case WM8350_LDO_3:
		volt_reg = WM8350_LDO3_LOW_POWER;
		break;
	case WM8350_LDO_4:
		volt_reg = WM8350_LDO4_LOW_POWER;
		break;
	default:
		return -EINVAL;
	}

	/* all LDOs have same mV bits */
	val = wm8350_reg_read(wm8350, volt_reg) & ~WM8350_LDO1_HIB_MODE_MASK;
	wm8350_reg_write(wm8350, volt_reg, val | WM8350_LDO1_HIB_MODE_DIS);
	return 0;
}

int wm8350_dcdc_set_slot(struct wm8350 *wm8350, int dcdc, u16 start,
			 u16 stop, u16 fault)
{
	int slot_reg;
	u16 val;

	dev_dbg(wm8350->dev, "%s %d start %d stop %d\n",
		__func__, dcdc, start, stop);

	/* slot valid ? */
	if (start > 15 || stop > 15)
		return -EINVAL;

	switch (dcdc) {
	case WM8350_DCDC_1:
		slot_reg = WM8350_DCDC1_TIMEOUTS;
		break;
	case WM8350_DCDC_2:
		slot_reg = WM8350_DCDC2_TIMEOUTS;
		break;
	case WM8350_DCDC_3:
		slot_reg = WM8350_DCDC3_TIMEOUTS;
		break;
	case WM8350_DCDC_4:
		slot_reg = WM8350_DCDC4_TIMEOUTS;
		break;
	case WM8350_DCDC_5:
		slot_reg = WM8350_DCDC5_TIMEOUTS;
		break;
	case WM8350_DCDC_6:
		slot_reg = WM8350_DCDC6_TIMEOUTS;
		break;
	default:
		return -EINVAL;
	}

	val = wm8350_reg_read(wm8350, slot_reg) &
	    ~(WM8350_DC1_ENSLOT_MASK | WM8350_DC1_SDSLOT_MASK |
	      WM8350_DC1_ERRACT_MASK);
	wm8350_reg_write(wm8350, slot_reg,
			 val | (start << WM8350_DC1_ENSLOT_SHIFT) |
			 (stop << WM8350_DC1_SDSLOT_SHIFT) |
			 (fault << WM8350_DC1_ERRACT_SHIFT));

	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_dcdc_set_slot);

int wm8350_ldo_set_slot(struct wm8350 *wm8350, int ldo, u16 start, u16 stop)
{
	int slot_reg;
	u16 val;

	dev_dbg(wm8350->dev, "%s %d start %d stop %d\n",
		__func__, ldo, start, stop);

	/* slot valid ? */
	if (start > 15 || stop > 15)
		return -EINVAL;

	switch (ldo) {
	case WM8350_LDO_1:
		slot_reg = WM8350_LDO1_TIMEOUTS;
		break;
	case WM8350_LDO_2:
		slot_reg = WM8350_LDO2_TIMEOUTS;
		break;
	case WM8350_LDO_3:
		slot_reg = WM8350_LDO3_TIMEOUTS;
		break;
	case WM8350_LDO_4:
		slot_reg = WM8350_LDO4_TIMEOUTS;
		break;
	default:
		return -EINVAL;
	}

	val = wm8350_reg_read(wm8350, slot_reg) & ~WM8350_LDO1_SDSLOT_MASK;
	wm8350_reg_write(wm8350, slot_reg, val | ((start << 10) | (stop << 6)));
	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_ldo_set_slot);

int wm8350_dcdc25_set_mode(struct wm8350 *wm8350, int dcdc, u16 mode,
			   u16 ilim, u16 ramp, u16 feedback)
{
	u16 val;

	dev_dbg(wm8350->dev, "%s %d mode: %s %s\n", __func__, dcdc,
		mode ? "normal" : "boost", ilim ? "low" : "normal");

	switch (dcdc) {
	case WM8350_DCDC_2:
		val = wm8350_reg_read(wm8350, WM8350_DCDC2_CONTROL)
		    & ~(WM8350_DC2_MODE_MASK | WM8350_DC2_ILIM_MASK |
			WM8350_DC2_RMP_MASK | WM8350_DC2_FBSRC_MASK);
		wm8350_reg_write(wm8350, WM8350_DCDC2_CONTROL, val |
				 (mode << WM8350_DC2_MODE_SHIFT) |
				 (ilim << WM8350_DC2_ILIM_SHIFT) |
				 (ramp << WM8350_DC2_RMP_SHIFT) |
				 (feedback << WM8350_DC2_FBSRC_SHIFT));
		break;
	case WM8350_DCDC_5:
		val = wm8350_reg_read(wm8350, WM8350_DCDC5_CONTROL)
		    & ~(WM8350_DC5_MODE_MASK | WM8350_DC5_ILIM_MASK |
			WM8350_DC5_RMP_MASK | WM8350_DC5_FBSRC_MASK);
		wm8350_reg_write(wm8350, WM8350_DCDC5_CONTROL, val |
				 (mode << WM8350_DC5_MODE_SHIFT) |
				 (ilim << WM8350_DC5_ILIM_SHIFT) |
				 (ramp << WM8350_DC5_RMP_SHIFT) |
				 (feedback << WM8350_DC5_FBSRC_SHIFT));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_dcdc25_set_mode);

static int force_continuous_enable(struct wm8350 *wm8350, int dcdc, int enable)
{
	int reg = 0, ret;

	switch (dcdc) {
	case WM8350_DCDC_1:
		reg = WM8350_DCDC1_FORCE_PWM;
		break;
	case WM8350_DCDC_3:
		reg = WM8350_DCDC3_FORCE_PWM;
		break;
	case WM8350_DCDC_4:
		reg = WM8350_DCDC4_FORCE_PWM;
		break;
	case WM8350_DCDC_6:
		reg = WM8350_DCDC6_FORCE_PWM;
		break;
	default:
		return -EINVAL;
	}

	if (enable)
		ret = wm8350_set_bits(wm8350, reg,
			WM8350_DCDC1_FORCE_PWM_ENA);
	else
		ret = wm8350_clear_bits(wm8350, reg,
			WM8350_DCDC1_FORCE_PWM_ENA);
	return ret;
}

static int wm8350_dcdc_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 val;

	if (dcdc < WM8350_DCDC_1 || dcdc > WM8350_DCDC_6)
		return -EINVAL;

	if (dcdc == WM8350_DCDC_2 || dcdc == WM8350_DCDC_5)
		return -EINVAL;

	val = 1 << (dcdc - WM8350_DCDC_1);

	switch (mode) {
	case REGULATOR_MODE_FAST:
		/* force continuous mode */
		wm8350_set_bits(wm8350, WM8350_DCDC_ACTIVE_OPTIONS, val);
		wm8350_clear_bits(wm8350, WM8350_DCDC_SLEEP_OPTIONS, val);
		force_continuous_enable(wm8350, dcdc, 1);
		break;
	case REGULATOR_MODE_NORMAL:
		/* active / pulse skipping */
		wm8350_set_bits(wm8350, WM8350_DCDC_ACTIVE_OPTIONS, val);
		wm8350_clear_bits(wm8350, WM8350_DCDC_SLEEP_OPTIONS, val);
		force_continuous_enable(wm8350, dcdc, 0);
		break;
	case REGULATOR_MODE_IDLE:
		/* standby mode */
		force_continuous_enable(wm8350, dcdc, 0);
		wm8350_clear_bits(wm8350, WM8350_DCDC_SLEEP_OPTIONS, val);
		wm8350_clear_bits(wm8350, WM8350_DCDC_ACTIVE_OPTIONS, val);
		break;
	case REGULATOR_MODE_STANDBY:
		/* LDO mode */
		force_continuous_enable(wm8350, dcdc, 0);
		wm8350_set_bits(wm8350, WM8350_DCDC_SLEEP_OPTIONS, val);
		break;
	}

	return 0;
}

static unsigned int wm8350_dcdc_get_mode(struct regulator_dev *rdev)
{
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);
	int dcdc = rdev_get_id(rdev);
	u16 mask, sleep, active, force;
	int mode = REGULATOR_MODE_NORMAL;
	int reg;

	switch (dcdc) {
	case WM8350_DCDC_1:
		reg = WM8350_DCDC1_FORCE_PWM;
		break;
	case WM8350_DCDC_3:
		reg = WM8350_DCDC3_FORCE_PWM;
		break;
	case WM8350_DCDC_4:
		reg = WM8350_DCDC4_FORCE_PWM;
		break;
	case WM8350_DCDC_6:
		reg = WM8350_DCDC6_FORCE_PWM;
		break;
	default:
		return -EINVAL;
	}

	mask = 1 << (dcdc - WM8350_DCDC_1);
	active = wm8350_reg_read(wm8350, WM8350_DCDC_ACTIVE_OPTIONS) & mask;
	force = wm8350_reg_read(wm8350, reg) & WM8350_DCDC1_FORCE_PWM_ENA;
	sleep = wm8350_reg_read(wm8350, WM8350_DCDC_SLEEP_OPTIONS) & mask;

	dev_dbg(wm8350->dev, "mask %x active %x sleep %x force %x",
		mask, active, sleep, force);

	if (active && !sleep) {
		if (force)
			mode = REGULATOR_MODE_FAST;
		else
			mode = REGULATOR_MODE_NORMAL;
	} else if (!active && !sleep)
		mode = REGULATOR_MODE_IDLE;
	else if (sleep)
		mode = REGULATOR_MODE_STANDBY;

	return mode;
}

static unsigned int wm8350_ldo_get_mode(struct regulator_dev *rdev)
{
	return REGULATOR_MODE_NORMAL;
}

struct wm8350_dcdc_efficiency {
	int uA_load_min;
	int uA_load_max;
	unsigned int mode;
};

static const struct wm8350_dcdc_efficiency dcdc1_6_efficiency[] = {
	{0, 10000, REGULATOR_MODE_STANDBY},       /* 0 - 10mA - LDO */
	{10000, 100000, REGULATOR_MODE_IDLE},     /* 10mA - 100mA - Standby */
	{100000, 1000000, REGULATOR_MODE_NORMAL}, /* > 100mA - Active */
	{-1, -1, REGULATOR_MODE_NORMAL},
};

static const struct wm8350_dcdc_efficiency dcdc3_4_efficiency[] = {
	{0, 10000, REGULATOR_MODE_STANDBY},      /* 0 - 10mA - LDO */
	{10000, 100000, REGULATOR_MODE_IDLE},    /* 10mA - 100mA - Standby */
	{100000, 800000, REGULATOR_MODE_NORMAL}, /* > 100mA - Active */
	{-1, -1, REGULATOR_MODE_NORMAL},
};

static unsigned int get_mode(int uA, const struct wm8350_dcdc_efficiency *eff)
{
	int i = 0;

	while (eff[i].uA_load_min != -1) {
		if (uA >= eff[i].uA_load_min && uA <= eff[i].uA_load_max)
			return eff[i].mode;
		i++;
	}
	return REGULATOR_MODE_NORMAL;
}

/* Query the regulator for it's most efficient mode @ uV,uA
 * WM8350 regulator efficiency is pretty similar over
 * different input and output uV.
 */
static unsigned int wm8350_dcdc_get_optimum_mode(struct regulator_dev *rdev,
						 int input_uV, int output_uV,
						 int output_uA)
{
	int dcdc = rdev_get_id(rdev), mode;

	switch (dcdc) {
	case WM8350_DCDC_1:
	case WM8350_DCDC_6:
		mode = get_mode(output_uA, dcdc1_6_efficiency);
		break;
	case WM8350_DCDC_3:
	case WM8350_DCDC_4:
		mode = get_mode(output_uA, dcdc3_4_efficiency);
		break;
	default:
		mode = REGULATOR_MODE_NORMAL;
		break;
	}
	return mode;
}

static const struct regulator_ops wm8350_dcdc_ops = {
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_mode = wm8350_dcdc_get_mode,
	.set_mode = wm8350_dcdc_set_mode,
	.get_optimum_mode = wm8350_dcdc_get_optimum_mode,
	.set_suspend_voltage = wm8350_dcdc_set_suspend_voltage,
	.set_suspend_enable = wm8350_dcdc_set_suspend_enable,
	.set_suspend_disable = wm8350_dcdc_set_suspend_disable,
	.set_suspend_mode = wm8350_dcdc_set_suspend_mode,
};

static const struct regulator_ops wm8350_dcdc2_5_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_suspend_enable = wm8350_dcdc25_set_suspend_enable,
	.set_suspend_disable = wm8350_dcdc25_set_suspend_disable,
};

static const struct regulator_ops wm8350_ldo_ops = {
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear_range,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_mode = wm8350_ldo_get_mode,
	.set_suspend_voltage = wm8350_ldo_set_suspend_voltage,
	.set_suspend_enable = wm8350_ldo_set_suspend_enable,
	.set_suspend_disable = wm8350_ldo_set_suspend_disable,
};

static const struct regulator_ops wm8350_isink_ops = {
	.set_current_limit = regulator_set_current_limit_regmap,
	.get_current_limit = regulator_get_current_limit_regmap,
	.enable = wm8350_isink_enable,
	.disable = wm8350_isink_disable,
	.is_enabled = wm8350_isink_is_enabled,
	.enable_time = wm8350_isink_enable_time,
};

static const struct regulator_desc wm8350_reg[NUM_WM8350_REGULATORS] = {
	{
		.name = "DCDC1",
		.id = WM8350_DCDC_1,
		.ops = &wm8350_dcdc_ops,
		.irq = WM8350_IRQ_UV_DC1,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_DCDC_MAX_VSEL + 1,
		.min_uV = 850000,
		.uV_step = 25000,
		.vsel_reg = WM8350_DCDC1_CONTROL,
		.vsel_mask = WM8350_DC1_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_DC1_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC2",
		.id = WM8350_DCDC_2,
		.ops = &wm8350_dcdc2_5_ops,
		.irq = WM8350_IRQ_UV_DC2,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_DC2_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC3",
		.id = WM8350_DCDC_3,
		.ops = &wm8350_dcdc_ops,
		.irq = WM8350_IRQ_UV_DC3,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_DCDC_MAX_VSEL + 1,
		.min_uV = 850000,
		.uV_step = 25000,
		.vsel_reg = WM8350_DCDC3_CONTROL,
		.vsel_mask = WM8350_DC3_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_DC3_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC4",
		.id = WM8350_DCDC_4,
		.ops = &wm8350_dcdc_ops,
		.irq = WM8350_IRQ_UV_DC4,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_DCDC_MAX_VSEL + 1,
		.min_uV = 850000,
		.uV_step = 25000,
		.vsel_reg = WM8350_DCDC4_CONTROL,
		.vsel_mask = WM8350_DC4_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_DC4_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "DCDC5",
		.id = WM8350_DCDC_5,
		.ops = &wm8350_dcdc2_5_ops,
		.irq = WM8350_IRQ_UV_DC5,
		.type = REGULATOR_VOLTAGE,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_DC5_ENA,
		.owner = THIS_MODULE,
	 },
	{
		.name = "DCDC6",
		.id = WM8350_DCDC_6,
		.ops = &wm8350_dcdc_ops,
		.irq = WM8350_IRQ_UV_DC6,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_DCDC_MAX_VSEL + 1,
		.min_uV = 850000,
		.uV_step = 25000,
		.vsel_reg = WM8350_DCDC6_CONTROL,
		.vsel_mask = WM8350_DC6_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_DC6_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO1",
		.id = WM8350_LDO_1,
		.ops = &wm8350_ldo_ops,
		.irq = WM8350_IRQ_UV_LDO1,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_LDO1_VSEL_MASK + 1,
		.linear_ranges = wm8350_ldo_ranges,
		.n_linear_ranges = ARRAY_SIZE(wm8350_ldo_ranges),
		.vsel_reg = WM8350_LDO1_CONTROL,
		.vsel_mask = WM8350_LDO1_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_LDO1_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO2",
		.id = WM8350_LDO_2,
		.ops = &wm8350_ldo_ops,
		.irq = WM8350_IRQ_UV_LDO2,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_LDO2_VSEL_MASK + 1,
		.linear_ranges = wm8350_ldo_ranges,
		.n_linear_ranges = ARRAY_SIZE(wm8350_ldo_ranges),
		.vsel_reg = WM8350_LDO2_CONTROL,
		.vsel_mask = WM8350_LDO2_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_LDO2_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO3",
		.id = WM8350_LDO_3,
		.ops = &wm8350_ldo_ops,
		.irq = WM8350_IRQ_UV_LDO3,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_LDO3_VSEL_MASK + 1,
		.linear_ranges = wm8350_ldo_ranges,
		.n_linear_ranges = ARRAY_SIZE(wm8350_ldo_ranges),
		.vsel_reg = WM8350_LDO3_CONTROL,
		.vsel_mask = WM8350_LDO3_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_LDO3_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "LDO4",
		.id = WM8350_LDO_4,
		.ops = &wm8350_ldo_ops,
		.irq = WM8350_IRQ_UV_LDO4,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = WM8350_LDO4_VSEL_MASK + 1,
		.linear_ranges = wm8350_ldo_ranges,
		.n_linear_ranges = ARRAY_SIZE(wm8350_ldo_ranges),
		.vsel_reg = WM8350_LDO4_CONTROL,
		.vsel_mask = WM8350_LDO4_VSEL_MASK,
		.enable_reg = WM8350_DCDC_LDO_REQUESTED,
		.enable_mask = WM8350_LDO4_ENA,
		.owner = THIS_MODULE,
	},
	{
		.name = "ISINKA",
		.id = WM8350_ISINK_A,
		.ops = &wm8350_isink_ops,
		.irq = WM8350_IRQ_CS1,
		.type = REGULATOR_CURRENT,
		.owner = THIS_MODULE,
		.curr_table = isink_cur,
		.n_current_limits = ARRAY_SIZE(isink_cur),
		.csel_reg = WM8350_CURRENT_SINK_DRIVER_A,
		.csel_mask = WM8350_CS1_ISEL_MASK,
	 },
	{
		.name = "ISINKB",
		.id = WM8350_ISINK_B,
		.ops = &wm8350_isink_ops,
		.irq = WM8350_IRQ_CS2,
		.type = REGULATOR_CURRENT,
		.owner = THIS_MODULE,
		.curr_table = isink_cur,
		.n_current_limits = ARRAY_SIZE(isink_cur),
		.csel_reg = WM8350_CURRENT_SINK_DRIVER_B,
		.csel_mask = WM8350_CS2_ISEL_MASK,
	 },
};

static irqreturn_t pmic_uv_handler(int irq, void *data)
{
	struct regulator_dev *rdev = (struct regulator_dev *)data;

	regulator_lock(rdev);
	if (irq == WM8350_IRQ_CS1 || irq == WM8350_IRQ_CS2)
		regulator_notifier_call_chain(rdev,
					      REGULATOR_EVENT_REGULATION_OUT,
					      NULL);
	else
		regulator_notifier_call_chain(rdev,
					      REGULATOR_EVENT_UNDER_VOLTAGE,
					      NULL);
	regulator_unlock(rdev);

	return IRQ_HANDLED;
}

static int wm8350_regulator_probe(struct platform_device *pdev)
{
	struct wm8350 *wm8350 = dev_get_drvdata(&pdev->dev);
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int ret;
	u16 val;

	if (pdev->id < WM8350_DCDC_1 || pdev->id > WM8350_ISINK_B)
		return -ENODEV;

	/* do any regulatior specific init */
	switch (pdev->id) {
	case WM8350_DCDC_1:
		val = wm8350_reg_read(wm8350, WM8350_DCDC1_LOW_POWER);
		wm8350->pmic.dcdc1_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		break;
	case WM8350_DCDC_3:
		val = wm8350_reg_read(wm8350, WM8350_DCDC3_LOW_POWER);
		wm8350->pmic.dcdc3_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		break;
	case WM8350_DCDC_4:
		val = wm8350_reg_read(wm8350, WM8350_DCDC4_LOW_POWER);
		wm8350->pmic.dcdc4_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		break;
	case WM8350_DCDC_6:
		val = wm8350_reg_read(wm8350, WM8350_DCDC6_LOW_POWER);
		wm8350->pmic.dcdc6_hib_mode = val & WM8350_DCDC_HIB_MODE_MASK;
		break;
	}

	config.dev = &pdev->dev;
	config.init_data = dev_get_platdata(&pdev->dev);
	config.driver_data = dev_get_drvdata(&pdev->dev);
	config.regmap = wm8350->regmap;

	/* register regulator */
	rdev = devm_regulator_register(&pdev->dev, &wm8350_reg[pdev->id],
				       &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "failed to register %s\n",
			wm8350_reg[pdev->id].name);
		return PTR_ERR(rdev);
	}

	/* register regulator IRQ */
	ret = wm8350_register_irq(wm8350, wm8350_reg[pdev->id].irq,
				  pmic_uv_handler, 0, "UV", rdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register regulator %s IRQ\n",
			wm8350_reg[pdev->id].name);
		return ret;
	}

	return 0;
}

static int wm8350_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);
	struct wm8350 *wm8350 = rdev_get_drvdata(rdev);

	wm8350_free_irq(wm8350, wm8350_reg[pdev->id].irq, rdev);

	return 0;
}

int wm8350_register_regulator(struct wm8350 *wm8350, int reg,
			      struct regulator_init_data *initdata)
{
	struct platform_device *pdev;
	int ret;
	if (reg < 0 || reg >= NUM_WM8350_REGULATORS)
		return -EINVAL;

	if (wm8350->pmic.pdev[reg])
		return -EBUSY;

	if (reg >= WM8350_DCDC_1 && reg <= WM8350_DCDC_6 &&
	    reg > wm8350->pmic.max_dcdc)
		return -ENODEV;
	if (reg >= WM8350_ISINK_A && reg <= WM8350_ISINK_B &&
	    reg > wm8350->pmic.max_isink)
		return -ENODEV;

	pdev = platform_device_alloc("wm8350-regulator", reg);
	if (!pdev)
		return -ENOMEM;

	wm8350->pmic.pdev[reg] = pdev;

	initdata->driver_data = wm8350;

	pdev->dev.platform_data = initdata;
	pdev->dev.parent = wm8350->dev;
	platform_set_drvdata(pdev, wm8350);

	ret = platform_device_add(pdev);

	if (ret != 0) {
		dev_err(wm8350->dev, "Failed to register regulator %d: %d\n",
			reg, ret);
		platform_device_put(pdev);
		wm8350->pmic.pdev[reg] = NULL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wm8350_register_regulator);

/**
 * wm8350_register_led - Register a WM8350 LED output
 *
 * @param wm8350 The WM8350 device to configure.
 * @param lednum LED device index to create.
 * @param dcdc The DCDC to use for the LED.
 * @param isink The ISINK to use for the LED.
 * @param pdata Configuration for the LED.
 *
 * The WM8350 supports the use of an ISINK together with a DCDC to
 * provide a power-efficient LED driver.  This function registers the
 * regulators and instantiates the platform device for a LED.  The
 * operating modes for the LED regulators must be configured using
 * wm8350_isink_set_flash(), wm8350_dcdc25_set_mode() and
 * wm8350_dcdc_set_slot() prior to calling this function.
 */
int wm8350_register_led(struct wm8350 *wm8350, int lednum, int dcdc, int isink,
			struct wm8350_led_platform_data *pdata)
{
	struct wm8350_led *led;
	struct platform_device *pdev;
	int ret;

	if (lednum >= ARRAY_SIZE(wm8350->pmic.led) || lednum < 0) {
		dev_err(wm8350->dev, "Invalid LED index %d\n", lednum);
		return -ENODEV;
	}

	led = &wm8350->pmic.led[lednum];

	if (led->pdev) {
		dev_err(wm8350->dev, "LED %d already allocated\n", lednum);
		return -EINVAL;
	}

	pdev = platform_device_alloc("wm8350-led", lednum);
	if (pdev == NULL) {
		dev_err(wm8350->dev, "Failed to allocate LED %d\n", lednum);
		return -ENOMEM;
	}

	led->isink_consumer.dev_name = dev_name(&pdev->dev);
	led->isink_consumer.supply = "led_isink";
	led->isink_init.num_consumer_supplies = 1;
	led->isink_init.consumer_supplies = &led->isink_consumer;
	led->isink_init.constraints.min_uA = 0;
	led->isink_init.constraints.max_uA = pdata->max_uA;
	led->isink_init.constraints.valid_ops_mask
		= REGULATOR_CHANGE_CURRENT | REGULATOR_CHANGE_STATUS;
	led->isink_init.constraints.valid_modes_mask = REGULATOR_MODE_NORMAL;
	ret = wm8350_register_regulator(wm8350, isink, &led->isink_init);
	if (ret != 0) {
		platform_device_put(pdev);
		return ret;
	}

	led->dcdc_consumer.dev_name = dev_name(&pdev->dev);
	led->dcdc_consumer.supply = "led_vcc";
	led->dcdc_init.num_consumer_supplies = 1;
	led->dcdc_init.consumer_supplies = &led->dcdc_consumer;
	led->dcdc_init.constraints.valid_modes_mask = REGULATOR_MODE_NORMAL;
	led->dcdc_init.constraints.valid_ops_mask =  REGULATOR_CHANGE_STATUS;
	ret = wm8350_register_regulator(wm8350, dcdc, &led->dcdc_init);
	if (ret != 0) {
		platform_device_put(pdev);
		return ret;
	}

	switch (isink) {
	case WM8350_ISINK_A:
		wm8350->pmic.isink_A_dcdc = dcdc;
		break;
	case WM8350_ISINK_B:
		wm8350->pmic.isink_B_dcdc = dcdc;
		break;
	}

	pdev->dev.platform_data = pdata;
	pdev->dev.parent = wm8350->dev;
	ret = platform_device_add(pdev);
	if (ret != 0) {
		dev_err(wm8350->dev, "Failed to register LED %d: %d\n",
			lednum, ret);
		platform_device_put(pdev);
		return ret;
	}

	led->pdev = pdev;

	return 0;
}
EXPORT_SYMBOL_GPL(wm8350_register_led);

static struct platform_driver wm8350_regulator_driver = {
	.probe = wm8350_regulator_probe,
	.remove = wm8350_regulator_remove,
	.driver		= {
		.name	= "wm8350-regulator",
	},
};

static int __init wm8350_regulator_init(void)
{
	return platform_driver_register(&wm8350_regulator_driver);
}
subsys_initcall(wm8350_regulator_init);

static void __exit wm8350_regulator_exit(void)
{
	platform_driver_unregister(&wm8350_regulator_driver);
}
module_exit(wm8350_regulator_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood");
MODULE_DESCRIPTION("WM8350 voltage and current regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8350-regulator");
