// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Sony Mobile Communications, AB.
 */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"WLED: %s: " fmt, __func__

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/power_supply.h>
#include <linux/soc/qcom/battery_charger.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/leds-qpnp-flash.h>
#include <linux/iio/consumer.h>
#include "../../leds/leds.h"

/* General definitions */
#define WLED_DEFAULT_BRIGHTNESS		2048
#define  WLED_MAX_BRIGHTNESS_12B	4095
#define  WLED_MAX_BRIGHTNESS_15B	32767

#define WLED_SOFT_START_DLY_US		10000

/* WLED control registers */
#define WLED_CTRL_REVISION2		0x01

#define WLED_CTRL_FAULT_STATUS		0x08
#define  WLED_CTRL_ILIM_FAULT_BIT	BIT(0)
#define  WLED_CTRL_OVP_FAULT_BIT	BIT(1)
#define  WLED_CTRL_SC_FAULT_BIT		BIT(2)
#define  WLED5_CTRL_OVP_PRE_ALARM_BIT	BIT(4)

#define WLED_CTRL_INT_RT_STS		0x10
#define  WLED_CTRL_OVP_FLT_RT_STS_BIT	BIT(1)

#define WLED_CTRL_MOD_ENABLE		0x46
#define  WLED_CTRL_MOD_EN_MASK		BIT(7)
#define  WLED_CTRL_MODULE_EN_SHIFT	7

#define WLED_CTRL_FDBK_OP		0x48

#define WLED_CTRL_SWITCH_FREQ		0x4c
#define  WLED_CTRL_SWITCH_FREQ_MASK	GENMASK(3, 0)

#define WLED_CTRL_OVP			0x4d
#define  WLED_CTRL_OVP_MASK		GENMASK(1, 0)
#define  WLED5_CTRL_OVP_MASK		GENMASK(3, 0)

#define WLED_CTRL_ILIM			0x4e
#define  WLED_CTRL_ILIM_MASK		GENMASK(2, 0)

#define WLED_CTRL_SHORT_PROTECT		0x5e
#define  WLED_CTRL_SHORT_EN_MASK	BIT(7)

#define WLED_CTRL_SEC_ACCESS		0xd0
#define  WLED_CTRL_SEC_UNLOCK		0xa5

#define WLED_CTRL_TEST1			0xe2
#define  WLED_EXT_FET_DTEST2		0x09

/* WLED sink registers */
#define WLED_SINK_CURR_SINK_EN		0x46
#define  WLED_SINK_CURR_SINK_MASK	GENMASK(7, 4)
#define  WLED_SINK_CURR_SINK_SHFT	0x04

#define WLED_SINK_SYNC			0x47
#define  WLED_SINK_SYNC_MASK		GENMASK(3, 0)
#define  WLED_SINK_SYNC_LED1		BIT(0)
#define  WLED_SINK_SYNC_LED2		BIT(1)
#define  WLED_SINK_SYNC_LED3		BIT(2)
#define  WLED_SINK_SYNC_LED4		BIT(3)
#define  WLED_SINK_SYNC_CLEAR		0x00

#define WLED_SINK_MOD_EN_REG(n)		(0x50 + (n * 0x10))
#define  WLED_SINK_REG_STR_MOD_MASK	BIT(7)
#define  WLED_SINK_REG_STR_MOD_EN	BIT(7)

#define WLED_SINK_SYNC_DLY_REG(n)	(0x51 + (n * 0x10))
#define WLED_SINK_FS_CURR_REG(n)	(0x52 + (n * 0x10))
#define  WLED_SINK_FS_MASK		GENMASK(3, 0)

#define WLED_SINK_CABC_REG(n)		(0x56 + (n * 0x10))
#define  WLED_SINK_CABC_MASK		BIT(7)
#define  WLED_SINK_CABC_EN		BIT(7)

#define WLED_SINK_BRIGHT_LSB_REG(n)	(0x57 + (n * 0x10))
#define WLED_SINK_BRIGHT_MSB_REG(n)	(0x58 + (n * 0x10))

/* WLED5 specific control registers */
#define WLED5_CTRL_STATUS		0x07

#define WLED5_CTRL_SH_FOR_SOFTSTART_REG	0x58
#define  WLED5_SOFTSTART_EN_SH_SS	BIT(0)

#define WLED5_CTRL_OVP_INT_CTL_REG	0x5f
#define  WLED5_OVP_INT_N_MASK		GENMASK(6, 4)
#define  WLED5_OVP_INT_N_SHIFT		4
#define  WLED5_OVP_INT_TIMER_MASK	GENMASK(2, 0)

#define WLED5_CTRL_PRE_FLASH_BRT_REG	0x61
#define WLED5_CTRL_PRE_FLASH_SYNC_REG	0x62
#define WLED5_CTRL_FLASH_BRT_REG	0x63
#define WLED5_CTRL_FLASH_SYNC_REG	0x64

#define WLED5_CTRL_FLASH_STEP_CTL_REG	0x65
#define  WLED5_CTRL_FLASH_STEP_MASK	GENMASK(2, 0)

#define WLED5_CTRL_FLASH_HDRM_REG	0x69

#define WLED5_CTRL_TEST4_REG		0xe5
#define  WLED5_TEST4_EN_SH_SS		BIT(5)

#define WLED5_CTRL_PBUS_WRITE_SYNC_CTL	0xef

/* WLED5 specific sink registers */
#define WLED5_SINK_MOD_A_EN_REG		0x50
#define WLED5_SINK_MOD_B_EN_REG		0x60
#define  WLED5_SINK_MOD_EN		BIT(7)

#define WLED5_SINK_MOD_A_SRC_SEL_REG	0x51
#define WLED5_SINK_MOD_B_SRC_SEL_REG	0x61
#define  WLED5_SINK_MOD_SRC_SEL_HIGH	0
#define  WLED5_SINK_MOD_SRC_SEL_CABC1	BIT(0)
#define  WLED5_SINK_MOD_SRC_SEL_CABC2	BIT(1)
#define  WLED5_SINK_MOD_SRC_SEL_EXT	0x03
#define  WLED5_SINK_MOD_SRC_SEL_MASK	GENMASK(1, 0)

#define WLED5_SINK_MOD_A_BR_WID_SEL_REG	0x52
#define WLED5_SINK_MOD_B_BR_WID_SEL_REG	0x62
#define  WLED5_SINK_BRT_WIDTH_12B	0
#define  WLED5_SINK_BRT_WIDTH_15B	1

#define WLED5_SINK_MOD_A_BRT_LSB_REG	0x53
#define WLED5_SINK_MOD_A_BRT_MSB_REG	0x54
#define WLED5_SINK_MOD_B_BRT_LSB_REG	0x63
#define WLED5_SINK_MOD_B_BRT_MSB_REG	0x64

#define WLED5_SINK_CABC_STRETCH_CTL_REG	0x57
#define WLED5_SINK_EN_CABC_STRETCH	BIT(7)

#define WLED5_SINK_MOD_SYNC_BIT_REG	0x65
#define  WLED5_SINK_SYNC_MODA_BIT	BIT(0)
#define  WLED5_SINK_SYNC_MODB_BIT	BIT(1)
#define  WLED5_SINK_SYNC_MASK		GENMASK(1, 0)

#define WLED5_SINK_FS_CURR_REG(n)	(0x72 + (n * 0x10))

#define WLED5_SINK_SRC_SEL_REG(n)	(0x73 + (n * 0x10))
#define  WLED5_SINK_SRC_SEL_MODA	0
#define  WLED5_SINK_SRC_SEL_MODB	1
#define  WLED5_SINK_SRC_SEL_MASK	GENMASK(1, 0)

#define WLED5_SINK_FLASH_CTL_REG	0xb0
#define  WLED5_SINK_FLASH_EN		BIT(7)
#define  WLED5_SINK_PRE_FLASH_EN	BIT(6)

#define WLED5_SINK_FLASH_SINK_EN_REG	0xb1

#define WLED5_SINK_FLASH_FSC_REG	0xb2
#define  WLED5_SINK_FLASH_FSC_MASK	GENMASK(3, 0)

#define  WLED5_SINK_FLASH_SYNC_BIT_REG	0xb3
#define  WLED5_SINK_FLASH_FSC_SYNC_EN	BIT(0)

#define  WLED5_SINK_FLASH_TIMER_CTL_REG	0xb5
#define   WLED5_DIS_PRE_FLASH_TIMER	BIT(7)
#define   WLED5_PRE_FLASH_SAFETY_TIME	GENMASK(6, 4)
#define   WLED5_PRE_FLASH_SAFETY_SHIFT	4
#define   WLED5_DIS_FLASH_TIMER		BIT(3)
#define   WLED5_FLASH_SAFETY_TIME	GENMASK(2, 0)

#define  WLED5_SINK_FLASH_SHDN_CLR_REG	0xb6

#define WLED5_SINK_BRIGHTNESS_SLEW_RATE_CTL_REG	0xb8
#define WLED5_EN_SLEW_CTL		BIT(7)
#define WLED5_EN_EXP_LUT		BIT(6)
#define WLED5_SLEW_RAMP_TIME_SEL	GENMASK(3, 0)

#define  WLED5_SINK_DIG_HYS_FILT_REG	0xbc
#define  WLED5_SINK_LSB_NOISE_THRESH	GENMASK(2, 0)

#define WLED5_SINK_DIMMING_EXP_LUT0_LSB_REG	0xc0
#define WLED5_SINK_DIMMING_EXP_LUT0_MSB_REG	0xc1
#define WLED5_SINK_DIMMING_EXP_LUT1_LSB_REG	0xc2
#define WLED5_SINK_DIMMING_EXP_LUT1_MSB_REG	0xc3
#define WLED5_SINK_DIMMING_EXP_LUT_LSB_MASK	GENMASK(7, 0)
#define WLED5_SINK_DIMMING_EXP_LUT_MSB_MASK	GENMASK(6, 0)

#define WLED5_SINK_EXP_LUT_INDEX_REG	0xc9

#define EXP_DIMMING_TABLE_SIZE	256

enum wled_version {
	WLED_PMI8998 = 4,
	WLED_PM660L,
	WLED_PM8150L,
	WLED_PM7325B,
};

enum wled_flash_mode {
	WLED_FLASH_OFF,
	WLED_PRE_FLASH,
	WLED_FLASH,
};

static const int version_table[] = {
	[0] = WLED_PMI8998,
	[1] = WLED_PM660L,
	[2] = WLED_PM8150L,
	[3] = WLED_PM7325B,
};

struct wled_config {
	int boost_i_limit;
	int ovp;
	int switch_freq;
	int fs_current;
	int string_cfg;
	int mod_sel;
	int cabc_sel;
	int slew_ramp_time;
	bool en_cabc;
	bool ext_pfet_sc_pro_en;
	bool auto_calib_enabled;
	bool use_exp_dimming;
};

struct wled_flash_config {
	int fs_current;
	int step_delay;
	int safety_timer;
};

struct wled {
	const char *name;
	struct platform_device *pdev;
	struct regmap *regmap;
	struct iio_channel **iio_channels;
	struct power_supply *batt_psy;
	struct mutex lock;
	struct wled_config cfg;
	ktime_t last_sc_event_time;
	ktime_t start_ovp_fault_time;
	u16 sink_addr;
	u16 ctrl_addr;
	u16 auto_calibration_ovp_count;
	u32 brightness;
	u32 max_brightness;
	u32 sc_count;
	u32 rev2;
	const int *version;
	int sc_irq;
	int ovp_irq;
	int flash_irq;
	int pre_flash_irq;
	bool prev_state;
	bool ovp_irq_disabled;
	bool auto_calib_done;
	bool force_mod_disable;
	bool cabc_disabled;
	bool use_psy;
	int (*cabc_config)(struct wled *wled, bool enable);

	struct led_classdev flash_cdev;
	struct led_classdev torch_cdev;
	struct led_classdev switch_cdev;
	struct wled_flash_config fparams;
	struct wled_flash_config tparams;
	spinlock_t flash_lock;
	enum wled_flash_mode flash_mode;
	u8 num_strings;
	u32 leds_per_string;
	u32 exp_map[EXP_DIMMING_TABLE_SIZE];
};

enum wled5_mod_sel {
	MOD_A,
	MOD_B,
	MOD_MAX,
};

static const u8 wled5_brt_reg[MOD_MAX] = {
	[MOD_A] = WLED5_SINK_MOD_A_BRT_LSB_REG,
	[MOD_B] = WLED5_SINK_MOD_B_BRT_LSB_REG,
};

static const u8 wled5_src_sel_reg[MOD_MAX] = {
	[MOD_A] = WLED5_SINK_MOD_A_SRC_SEL_REG,
	[MOD_B] = WLED5_SINK_MOD_B_SRC_SEL_REG,
};

static const u8 wled5_brt_wid_sel_reg[MOD_MAX] = {
	[MOD_A] = WLED5_SINK_MOD_A_BR_WID_SEL_REG,
	[MOD_B] = WLED5_SINK_MOD_B_BR_WID_SEL_REG,
};

enum wled_iio_props {
	RBATT,
	OCV,
	IBAT,
};

static const char *const wled_iio_prop_names[] = {
	[RBATT] = "rbatt",
	[OCV] = "voltage_ocv",
	[IBAT] = "current_now",
};

static int wled_flash_setup(struct wled *wled);

static inline bool is_wled4(struct wled *wled)
{
	if (*wled->version == WLED_PMI8998 || *wled->version == WLED_PM660L)
		return true;

	return false;
}

static inline bool is_wled5(struct wled *wled)
{
	if (*wled->version == WLED_PM8150L || *wled->version == WLED_PM7325B)
		return true;

	return false;
}

static int wled_module_enable(struct wled *wled, int val)
{
	int rc;
	int reg;

	if (wled->force_mod_disable)
		return 0;

	/* Force HFRC off */
	if (*wled->version == WLED_PM8150L) {
		reg = val ? 0 : 3;
		rc = regmap_write(wled->regmap, wled->ctrl_addr +
				  WLED5_CTRL_PBUS_WRITE_SYNC_CTL, reg);
		if (rc < 0)
			return rc;
	}

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
			WLED_CTRL_MOD_ENABLE, WLED_CTRL_MOD_EN_MASK,
			val << WLED_CTRL_MODULE_EN_SHIFT);
	if (rc < 0)
		return rc;

	/* Force HFRC off */
	if ((*wled->version == WLED_PM8150L) && val) {
		rc = regmap_write(wled->regmap, wled->sink_addr +
				  WLED5_SINK_FLASH_SHDN_CLR_REG, 0);
		if (rc < 0)
			return rc;
	}

	/*
	 * Wait for at least 10ms before enabling OVP fault interrupt after
	 * enabling the module so that soft start is completed. Keep the OVP
	 * interrupt disabled when the module is disabled.
	 */
	if (val) {
		usleep_range(WLED_SOFT_START_DLY_US,
				WLED_SOFT_START_DLY_US + 1000);

		if (wled->ovp_irq > 0 && wled->ovp_irq_disabled) {
			enable_irq(wled->ovp_irq);
			wled->ovp_irq_disabled = false;
		}
	} else {
		if (wled->ovp_irq > 0 && !wled->ovp_irq_disabled) {
			disable_irq(wled->ovp_irq);
			wled->ovp_irq_disabled = true;
		}
	}

	return rc;
}

static int wled_get_brightness(struct backlight_device *bl)
{
	struct wled *wled = bl_get_data(bl);

	return wled->brightness;
}

static int wled_sync_toggle(struct wled *wled)
{
	int rc;

	rc = regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED_SINK_SYNC,
			WLED_SINK_SYNC_MASK, WLED_SINK_SYNC_CLEAR);
	if (rc < 0)
		return rc;

	return regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED_SINK_SYNC,
			WLED_SINK_SYNC_MASK, WLED_SINK_SYNC_MASK);
}

static int wled5_sample_hold_control(struct wled *wled, u16 brightness,
					bool enable)
{
	int rc;
	u16 offset, threshold;
	u8 val, mask;

	/*
	 * Control S_H only when module was disabled and a lower brightness
	 * of < 1% is set.
	 */
	if (wled->prev_state)
		return 0;

	/* If CABC is enabled, then don't do anything for now */
	if (!wled->cabc_disabled)
		return 0;

	/* 1 % threshold to enable the workaround */
	threshold = DIV_ROUND_UP(wled->max_brightness, 100);

	/* If brightness is > 1%, don't do anything */
	if (brightness > threshold)
		return 0;

	/* Wait for ~5ms before enabling S_H */
	if (enable)
		usleep_range(5000, 5010);

	/* Disable S_H if brightness is < 1% */
	if (wled->rev2 >= 6) {
		offset = WLED5_CTRL_SH_FOR_SOFTSTART_REG;
		val = enable ? WLED5_SOFTSTART_EN_SH_SS : 0;
		mask = WLED5_SOFTSTART_EN_SH_SS;
	} else {
		offset = WLED5_CTRL_TEST4_REG;
		val = enable ? WLED5_TEST4_EN_SH_SS : 0;
		mask = WLED5_TEST4_EN_SH_SS;
	}

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + offset, mask, val);
	if (rc < 0)
		pr_err("Error in writing offset 0x%02X rc=%d\n", offset, rc);

	return rc;
}

static int wled5_set_brightness(struct wled *wled, u16 brightness)
{
	int rc, offset;
	u16 low_limit = wled->max_brightness * 1 / 1000;
	u8 val, v[2], brightness_msb_mask;

	/* WLED5's lower limit is 0.1% */
	if (brightness > 0 && brightness < low_limit)
		brightness = low_limit;

	brightness_msb_mask = 0xf;
	if (wled->max_brightness == WLED_MAX_BRIGHTNESS_15B)
		brightness_msb_mask = 0x7f;

	v[0] = brightness & 0xff;
	v[1] = (brightness >> 8) & brightness_msb_mask;

	offset = wled5_brt_reg[wled->cfg.mod_sel];
	rc = regmap_bulk_write(wled->regmap, wled->sink_addr + offset,
			v, 2);
	if (rc < 0)
		return rc;

	val = 0;
	rc = regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED5_SINK_MOD_SYNC_BIT_REG,
			WLED_SINK_SYNC_MASK, val);
	/* Update brightness values to modulator in WLED5 */
	if (rc < 0)
		return rc;

	val = (wled->cfg.mod_sel == MOD_A) ? WLED5_SINK_SYNC_MODA_BIT :
		WLED5_SINK_SYNC_MODB_BIT;
	return regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED5_SINK_MOD_SYNC_BIT_REG,
			WLED5_SINK_SYNC_MASK, val);
}

static int wled4_set_brightness(struct wled *wled, u16 brightness)
{
	int rc, i;
	u16 low_limit = wled->max_brightness * 4 / 1000;
	u8 string_cfg = wled->cfg.string_cfg;
	u8 v[2];

	/* WLED4's lower limit of operation is 0.4% */
	if (brightness > 0 && brightness < low_limit)
		brightness = low_limit;

	v[0] = brightness & 0xff;
	v[1] = (brightness >> 8) & 0xf;

	for (i = 0; (string_cfg >> i) != 0; i++) {
		rc = regmap_bulk_write(wled->regmap, wled->sink_addr +
				WLED_SINK_BRIGHT_LSB_REG(i), v, 2);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wled_set_brightness(struct wled *wled, u16 brightness)
{
	if (is_wled4(wled))
		return wled4_set_brightness(wled, brightness);
	else if (is_wled5(wled))
		return wled5_set_brightness(wled, brightness);

	return 0;
}

static bool wled_exp_dimming_supported(struct wled *wled)
{
	if (*wled->version == WLED_PM7325B)
		return true;

	dev_dbg(&wled->pdev->dev, "Exponential dimming not supported for WLED version %d\n",
				*wled->version);
	return false;
}

static int wled_update_status(struct backlight_device *bl)
{
	struct wled *wled = bl_get_data(bl);
	u16 brightness = bl->props.brightness;
	int rc;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	mutex_lock(&wled->lock);
	if (brightness) {
		rc = wled_set_brightness(wled, brightness);
		if (rc < 0) {
			pr_err("wled failed to set brightness rc:%d\n", rc);
			goto unlock_mutex;
		}

		if (is_wled5(wled)) {
			rc = wled5_sample_hold_control(wled, brightness, false);
			if (rc < 0) {
				pr_err("wled disabling sample and hold failed rc:%d\n",
					rc);
				goto unlock_mutex;
			}
		}

		if (!!brightness != wled->prev_state) {
			rc = wled_module_enable(wled, !!brightness);
			if (rc < 0) {
				pr_err("wled enable failed rc:%d\n", rc);
				goto unlock_mutex;
			}

			if (wled_exp_dimming_supported(wled)) {
				rc = regmap_update_bits(wled->regmap,
					wled->sink_addr + WLED5_SINK_BRIGHTNESS_SLEW_RATE_CTL_REG,
					WLED5_SLEW_RAMP_TIME_SEL,
					wled->cfg.slew_ramp_time);
				if (rc < 0) {
					pr_err("Failed to write to SLEW_RATE_REGISTER rc:%d\n", rc);
					goto unlock_mutex;
				}
			}
		}

		if (is_wled5(wled)) {
			rc = wled5_sample_hold_control(wled, brightness, true);
			if (rc < 0) {
				pr_err("wled enabling sample and hold failed rc:%d\n",
					rc);
				goto unlock_mutex;
			}
		}
	} else {
		if (wled_exp_dimming_supported(wled)) {
			rc = regmap_update_bits(wled->regmap,
					wled->sink_addr + WLED5_SINK_BRIGHTNESS_SLEW_RATE_CTL_REG,
					WLED5_SLEW_RAMP_TIME_SEL, 0);
			if (rc < 0) {
				pr_err("Failed to write to SLEW_RATE_REGISTER rc:%d\n", rc);
				goto unlock_mutex;
			}
		}

		rc = wled_module_enable(wled, brightness);
		if (rc < 0) {
			pr_err("wled disable failed rc:%d\n", rc);
			goto unlock_mutex;
		}
	}

	wled->prev_state = !!brightness;

	if (is_wled4(wled)) {
		rc = wled_sync_toggle(wled);
		if (rc < 0) {
			pr_err("wled sync failed rc:%d\n", rc);
			goto unlock_mutex;
		}
	}

	wled->brightness = brightness;

unlock_mutex:
	mutex_unlock(&wled->lock);
	return rc;
}

#define WLED_SC_DLY_MS			20
#define WLED_SC_CNT_MAX			5
#define WLED_SC_RESET_CNT_DLY_US	1000000
static irqreturn_t wled_sc_irq_handler(int irq, void *_wled)
{
	struct wled *wled = _wled;
	int rc;
	u32 val;
	s64 elapsed_time;

	rc = regmap_read(wled->regmap,
		wled->ctrl_addr + WLED_CTRL_FAULT_STATUS, &val);
	if (rc < 0) {
		pr_err("Error in reading WLED_FAULT_STATUS rc=%d\n", rc);
		return IRQ_HANDLED;
	}

	wled->sc_count++;
	pr_err("WLED short circuit detected %d times fault_status=%x\n",
		wled->sc_count, val);
	mutex_lock(&wled->lock);
	rc = wled_module_enable(wled, false);
	if (rc < 0) {
		pr_err("wled disable failed rc:%d\n", rc);
		goto unlock_mutex;
	}

	elapsed_time = ktime_us_delta(ktime_get(),
				wled->last_sc_event_time);
	if (elapsed_time > WLED_SC_RESET_CNT_DLY_US) {
		wled->sc_count = 0;
	} else if (wled->sc_count > WLED_SC_CNT_MAX) {
		pr_err("SC trigged %d times, disabling WLED forever!\n",
			wled->sc_count);
		goto unlock_mutex;
	}

	wled->last_sc_event_time = ktime_get();

	msleep(WLED_SC_DLY_MS);
	rc = wled_module_enable(wled, true);
	if (rc < 0)
		pr_err("wled enable failed rc:%d\n", rc);

unlock_mutex:
	mutex_unlock(&wled->lock);

	return IRQ_HANDLED;
}

static int wled5_cabc_config(struct wled *wled, bool enable)
{
	int rc, offset;
	u8 reg;

	if (wled->cabc_disabled)
		return 0;

	reg = enable ? wled->cfg.cabc_sel : 0;
	offset = wled5_src_sel_reg[wled->cfg.mod_sel];
	rc = regmap_update_bits(wled->regmap, wled->sink_addr + offset,
			WLED5_SINK_MOD_SRC_SEL_MASK, reg);
	if (rc < 0) {
		pr_err("Error in configuring CABC rc=%d\n", rc);
		return rc;
	}

	if (!wled->cfg.cabc_sel) {
		wled->cabc_disabled = true;
		if (wled_exp_dimming_supported(wled)) {
			rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED5_SINK_CABC_STRETCH_CTL_REG,
				WLED5_SINK_EN_CABC_STRETCH, 0);
			if (rc < 0)
				return rc;

			rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED5_SINK_DIG_HYS_FILT_REG,
				WLED5_SINK_LSB_NOISE_THRESH, 0);
			if (rc < 0)
				return rc;
		}
	}

	return 0;
}

static int wled4_cabc_config(struct wled *wled, bool enable)
{
	int i, rc;
	u8 reg;

	if (wled->cabc_disabled)
		return 0;

	for (i = 0; (wled->cfg.string_cfg >> i) != 0; i++) {
		reg = enable ? WLED_SINK_CABC_EN : 0;
		rc = regmap_update_bits(wled->regmap, wled->sink_addr +
				WLED_SINK_CABC_REG(i),
				WLED_SINK_CABC_MASK, reg);
		if (rc < 0)
			return rc;
	}

	if (!wled->cfg.en_cabc)
		wled->cabc_disabled = true;

	return 0;
}

static int wled_get_ovp_fault_status(struct wled *wled, bool *fault_set)
{
	int rc;
	u32 int_rt_sts, fault_sts;

	*fault_set = false;
	rc = regmap_read(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_INT_RT_STS,
			&int_rt_sts);
	if (rc < 0) {
		pr_err("Failed to read INT_RT_STS rc=%d\n", rc);
		return rc;
	}

	rc = regmap_read(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_FAULT_STATUS,
			&fault_sts);
	if (rc < 0) {
		pr_err("Failed to read FAULT_STATUS rc=%d\n", rc);
		return rc;
	}

	if (int_rt_sts & WLED_CTRL_OVP_FLT_RT_STS_BIT)
		*fault_set = true;

	if (is_wled4(wled) && (fault_sts & WLED_CTRL_OVP_FAULT_BIT))
		*fault_set = true;
	else if (is_wled5(wled) && (fault_sts & (WLED_CTRL_OVP_FAULT_BIT |
					WLED5_CTRL_OVP_PRE_ALARM_BIT)))
		*fault_set = true;

	if (*fault_set)
		pr_debug("WLED OVP fault detected, int_rt_sts=0x%x fault_sts=0x%x\n",
			int_rt_sts, fault_sts);

	return rc;
}

static void wled_get_ovp_delay(struct wled *wled, int *delay_time_us)
{
	int rc;
	u32 val;
	u8 ovp_timer_ms[8] = {1, 2, 4, 8, 12, 16, 20, 24};

	if (is_wled4(wled)) {
		*delay_time_us = WLED_SOFT_START_DLY_US;
		return;
	}

	/* For WLED5, get the delay based on OVP timer */
	rc = regmap_read(wled->regmap, wled->ctrl_addr +
		WLED5_CTRL_OVP_INT_CTL_REG, &val);
	if (!rc)
		*delay_time_us =
			ovp_timer_ms[val & WLED5_OVP_INT_TIMER_MASK] * 1000;
	else
		*delay_time_us = 2 * WLED_SOFT_START_DLY_US;

	pr_debug("delay_time_us: %d\n", *delay_time_us);
}

#define AUTO_CALIB_BRIGHTNESS		512
static int wled_auto_calibrate(struct wled *wled)
{
	int rc = 0, i, delay_time_us;
	u32 sink_config = 0;
	u8 reg = 0, sink_test = 0, sink_valid = 0;
	u8 string_cfg = wled->cfg.string_cfg;
	bool fault_set;

	if (wled->auto_calib_done)
		return 0;

	/* read configured sink configuration */
	rc = regmap_read(wled->regmap, wled->sink_addr +
			WLED_SINK_CURR_SINK_EN, &sink_config);
	if (rc < 0) {
		pr_err("Failed to read SINK configuration rc=%d\n", rc);
		goto failed_calib;
	}

	/* disable the module before starting calibration */
	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_MOD_ENABLE,
			WLED_CTRL_MOD_EN_MASK, 0);
	if (rc < 0) {
		pr_err("Failed to disable WLED module rc=%d\n", rc);
		goto failed_calib;
	}

	/* set low brightness across all sinks */
	rc = wled_set_brightness(wled, AUTO_CALIB_BRIGHTNESS);
	if (rc < 0) {
		pr_err("Failed to set brightness for calibration rc=%d\n", rc);
		goto failed_calib;
	}

	/* Disable CABC if it is enabled */
	rc = wled->cabc_config(wled, false);
	if (rc < 0)
		goto failed_calib;

	/* disable all sinks */
	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED_SINK_CURR_SINK_EN, 0);
	if (rc < 0) {
		pr_err("Failed to disable all sinks rc=%d\n", rc);
		goto failed_calib;
	}

	/* iterate through the strings one by one */
	for (i = 0; (string_cfg >> i) != 0; i++) {
		sink_test = 1 << (WLED_SINK_CURR_SINK_SHFT + i);

		/* Enable feedback control */
		rc = regmap_write(wled->regmap, wled->ctrl_addr +
				WLED_CTRL_FDBK_OP, i + 1);
		if (rc < 0) {
			pr_err("Failed to enable feedback for SINK %d rc = %d\n",
				i + 1, rc);
			goto failed_calib;
		}

		/* enable the sink */
		rc = regmap_write(wled->regmap, wled->sink_addr +
				WLED_SINK_CURR_SINK_EN, sink_test);
		if (rc < 0) {
			pr_err("Failed to configure SINK %d rc=%d\n",
						i + 1, rc);
			goto failed_calib;
		}

		/* Enable the module */
		rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
				WLED_CTRL_MOD_ENABLE,
				WLED_CTRL_MOD_EN_MASK,
				WLED_CTRL_MOD_EN_MASK);
		if (rc < 0) {
			pr_err("Failed to enable WLED module rc=%d\n", rc);
			goto failed_calib;
		}

		wled_get_ovp_delay(wled, &delay_time_us);
		if (delay_time_us < 20000)
			usleep_range(delay_time_us, delay_time_us + 1000);
		else
			msleep(delay_time_us / 1000);

		rc = wled_get_ovp_fault_status(wled, &fault_set);
		if (rc < 0) {
			pr_err("Error in getting OVP fault_sts, rc=%d\n", rc);
			goto failed_calib;
		}

		if (fault_set)
			pr_debug("WLED OVP fault detected with SINK %d\n",
						i + 1);
		else
			sink_valid |= sink_test;

		/* Disable the module */
		rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED_CTRL_MOD_ENABLE,
				WLED_CTRL_MOD_EN_MASK, 0);
		if (rc < 0) {
			pr_err("Failed to disable WLED module rc=%d\n", rc);
			goto failed_calib;
		}
	}

	if (sink_valid == sink_config) {
		pr_debug("WLED auto-calibration complete, default sink-config=%x OK!\n",
						sink_config);
	} else {
		pr_warn("Invalid WLED default sink config=%x changing it to=%x\n",
						sink_config, sink_valid);
		sink_config = sink_valid;
	}

	if (!sink_config) {
		pr_err("No valid WLED sinks found\n");
		wled->force_mod_disable = true;
		goto failed_calib;
	}

	/* write the new sink configuration */
	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED_SINK_CURR_SINK_EN,
			sink_config);
	if (rc < 0) {
		pr_err("Failed to reconfigure the default sink rc=%d\n", rc);
		goto failed_calib;
	}

	if (is_wled5(wled)) {
		/* Update the flash sink configuration as well */
		rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED5_SINK_FLASH_SINK_EN_REG,
				WLED_SINK_CURR_SINK_MASK, sink_config);
		if (rc < 0)
			return rc;
	}

	/* MODULATOR_EN setting for valid sinks */
	if (is_wled4(wled)) {
		for (i = 0; (string_cfg >> i) != 0; i++) {
			/* disable modulator_en for unused sink */
			if (sink_config & (1 << (WLED_SINK_CURR_SINK_SHFT + i)))
				reg = WLED_SINK_REG_STR_MOD_EN;
			else
				reg = 0x0;

			rc = regmap_write(wled->regmap, wled->sink_addr +
					WLED_SINK_MOD_EN_REG(i), reg);
			if (rc < 0) {
				pr_err("Failed to configure MODULATOR_EN rc=%d\n",
					rc);
				goto failed_calib;
			}
		}
	}

	/* Enable CABC if it needs to be enabled */
	rc = wled->cabc_config(wled, true);
	if (rc < 0)
		goto failed_calib;

	/* restore the feedback setting */
	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_FDBK_OP, 0);
	if (rc < 0) {
		pr_err("Failed to restore feedback setting rc=%d\n", rc);
		goto failed_calib;
	}

	/* restore  brightness */
	rc = wled_set_brightness(wled, wled->brightness);
	if (rc < 0) {
		pr_err("Failed to set brightness after calibration rc=%d\n",
			rc);
		goto failed_calib;
	}

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_MOD_ENABLE,
			WLED_CTRL_MOD_EN_MASK,
			WLED_CTRL_MOD_EN_MASK);
	if (rc < 0) {
		pr_err("Failed to enable WLED module rc=%d\n", rc);
		goto failed_calib;
	}

	/* delay for WLED soft-start */
	usleep_range(WLED_SOFT_START_DLY_US,
		     WLED_SOFT_START_DLY_US + 1000);

	wled->auto_calib_done = true;

failed_calib:
	return rc;
}

#define WLED_AUTO_CAL_OVP_COUNT		5
#define WLED_AUTO_CAL_CNT_DLY_US	1000000	/* 1 second */
static bool wled_auto_cal_required(struct wled *wled)
{
	s64 elapsed_time_us;

	/*
	 * Check if the OVP fault was an occasional one
	 * or if its firing continuously, the latter qualifies
	 * for an auto-calibration check.
	 */
	if (!wled->auto_calibration_ovp_count) {
		wled->start_ovp_fault_time = ktime_get();
		wled->auto_calibration_ovp_count++;
		return false;
	}

	if (is_wled5(wled)) {
		/*
		 * WLED5 has OVP fault density interrupt configuration i.e. to
		 * count the number of OVP alarms for a certain duration before
		 * triggering OVP fault interrupt. By default, number of OVP
		 * fault events counted before an interrupt is fired is 32 and
		 * the time interval is 12 ms. If we see more than one OVP fault
		 * interrupt, then that should qualify for a real OVP fault
		 * condition to run auto calibration algorithm.
		 */

		if (wled->auto_calibration_ovp_count > 1) {
			elapsed_time_us = ktime_us_delta(ktime_get(),
					wled->start_ovp_fault_time);
			wled->auto_calibration_ovp_count = 0;
			pr_debug("Elapsed time: %lld us\n", elapsed_time_us);
			return true;
		}
		wled->auto_calibration_ovp_count++;
	} else if (is_wled4(wled)) {
		elapsed_time_us = ktime_us_delta(ktime_get(),
				wled->start_ovp_fault_time);
		if (elapsed_time_us > WLED_AUTO_CAL_CNT_DLY_US)
			wled->auto_calibration_ovp_count = 0;
		else
			wled->auto_calibration_ovp_count++;

		if (wled->auto_calibration_ovp_count >=
				WLED_AUTO_CAL_OVP_COUNT) {
			wled->auto_calibration_ovp_count = 0;
			return true;
		}
	}

	return false;
}

static int wled_auto_calibrate_at_init(struct wled *wled)
{
	int rc, delay_time_us;
	bool fault_set;

	if (!wled->cfg.auto_calib_enabled)
		return 0;

	rc = wled_get_ovp_fault_status(wled, &fault_set);
	if (rc < 0) {
		pr_err("Error in getting OVP fault_sts, rc=%d\n", rc);
		return rc;
	}

	if (fault_set) {
		wled_get_ovp_delay(wled, &delay_time_us);

		if (delay_time_us < 20000)
			usleep_range(delay_time_us, delay_time_us + 1000);
		else
			msleep(delay_time_us / 1000);

		rc = wled_get_ovp_fault_status(wled, &fault_set);
		if (rc < 0) {
			pr_err("Error in getting OVP fault_sts, rc=%d\n", rc);
			return rc;
		}
		if (!fault_set) {
			pr_debug("WLED OVP fault cleared, not running auto calibration\n");
			return rc;
		}

		mutex_lock(&wled->lock);
		rc = wled_auto_calibrate(wled);
		mutex_unlock(&wled->lock);
	}

	return rc;
}

static void handle_ovp_fault(struct wled *wled)
{
	int rc;

	if (!wled->cfg.auto_calib_enabled)
		return;

	mutex_lock(&wled->lock);
	if (wled->auto_calib_done) {
		pr_warn("Disabling module since OVP persists\n");
		rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED_CTRL_MOD_ENABLE,
				WLED_CTRL_MOD_EN_MASK, 0);
		if (!rc)
			wled->force_mod_disable = true;
		mutex_unlock(&wled->lock);
		return;
	}

	if (wled->ovp_irq > 0 && !wled->ovp_irq_disabled) {
		disable_irq_nosync(wled->ovp_irq);
		wled->ovp_irq_disabled = true;
	}

	if (wled_auto_cal_required(wled))
		wled_auto_calibrate(wled);

	if (wled->ovp_irq > 0 && wled->ovp_irq_disabled) {
		enable_irq(wled->ovp_irq);
		wled->ovp_irq_disabled = false;
	}
	mutex_unlock(&wled->lock);
}

static irqreturn_t wled_ovp_irq_handler(int irq, void *_wled)
{
	struct wled *wled = _wled;
	int rc;
	bool fault_set;

	rc = wled_get_ovp_fault_status(wled, &fault_set);
	if (rc < 0) {
		pr_err("Error in getting OVP fault_sts, rc=%d\n", rc);
		return rc;
	}

	if (fault_set)
		handle_ovp_fault(wled);

	return IRQ_HANDLED;
}

static irqreturn_t wled_flash_irq_handler(int irq, void *_wled)
{
	struct wled *wled = _wled;
	int rc;
	u32 val;

	if (irq == wled->flash_irq)
		pr_debug("flash irq fired\n");
	else if (irq == wled->pre_flash_irq)
		pr_debug("pre_flash irq fired\n");

	rc = regmap_read(wled->regmap,
		wled->ctrl_addr + WLED_CTRL_FAULT_STATUS, &val);
	if (!rc)
		pr_debug("WLED_FAULT_STATUS: 0x%x\n", val);

	rc = regmap_read(wled->regmap,
		wled->ctrl_addr + WLED5_CTRL_STATUS, &val);
	if (!rc)
		pr_debug("WLED_STATUS: 0x%x\n", val);

	return IRQ_HANDLED;
}

static inline u8 get_wled_safety_time(int time_ms)
{
	int i, table[8] = {50, 100, 200, 400, 600, 800, 1000, 1200};

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		if (time_ms == table[i])
			return i;
	}

	return 0;
}

static int wled_read_exp_dimming_map(struct device_node *node, struct wled *wled)
{
	int rc, len;

	if (!wled_exp_dimming_supported(wled))
		return 0;

	len = of_property_count_elems_of_size(node, "qcom,exp-dimming-map", sizeof(u32));
	if (len != EXP_DIMMING_TABLE_SIZE) {
		dev_err(&wled->pdev->dev, "Invalid exponential map length: %d, must be %d bytes length\n",
						len, EXP_DIMMING_TABLE_SIZE);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, "qcom,exp-dimming-map",
					wled->exp_map, EXP_DIMMING_TABLE_SIZE);
	if (rc < 0)
		dev_err(&wled->pdev->dev, "Error in reading qcom,exp-dimming-map, rc=%d\n",
								rc);
	return rc;
}

static int wled_program_exp_dimming(struct wled *wled)
{
	int rc, i;
	u8 val[4];

	if (!wled_exp_dimming_supported(wled))
		return 0;

	rc = wled_module_enable(wled, 0);
	if (rc < 0) {
		dev_err(&wled->pdev->dev, "wled disable failed rc:%d\n", rc);
		return rc;
	}

	rc = regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED5_SINK_BRIGHTNESS_SLEW_RATE_CTL_REG,
			WLED5_EN_EXP_LUT, WLED5_EN_EXP_LUT);
	if (rc < 0)
		goto wled_enable;

	for (i = 0; i < EXP_DIMMING_TABLE_SIZE / 2; i++) {
		val[0] = wled->exp_map[2 * i] & WLED5_SINK_DIMMING_EXP_LUT_LSB_MASK;
		val[1] = (wled->exp_map[2 * i] >> 8) & WLED5_SINK_DIMMING_EXP_LUT_MSB_MASK;
		val[2] = wled->exp_map[2 * i + 1] & WLED5_SINK_DIMMING_EXP_LUT_LSB_MASK;
		val[3] = (wled->exp_map[2 * i + 1] >> 8) & WLED5_SINK_DIMMING_EXP_LUT_MSB_MASK;

		rc = regmap_write(wled->regmap, wled->sink_addr + WLED5_SINK_EXP_LUT_INDEX_REG, i);
		if (rc < 0)
			goto exp_dimm_fail;

		rc = regmap_bulk_write(wled->regmap,
				wled->sink_addr + WLED5_SINK_DIMMING_EXP_LUT0_LSB_REG,
				val, sizeof(val));
		if (rc < 0)
			goto exp_dimm_fail;
	}

	wled_module_enable(wled, 1);
	return rc;

exp_dimm_fail:
	rc = regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED5_SINK_BRIGHTNESS_SLEW_RATE_CTL_REG,
			WLED5_EN_EXP_LUT, 0);
wled_enable:
	wled_module_enable(wled, 1);
	return rc;
}

static int wled5_setup(struct wled *wled)
{
	int rc, temp, i;
	u8 sink_en = 0;
	u16 addr;
	u32 val;
	u8 string_cfg = wled->cfg.string_cfg;

	rc = wled_flash_setup(wled);
	if (rc < 0)
		dev_err(&wled->pdev->dev, "failed to setup WLED flash/torch rc:%d\n",
			rc);

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_OVP,
			WLED5_CTRL_OVP_MASK, wled->cfg.ovp);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_ILIM,
			WLED_CTRL_ILIM_MASK, wled->cfg.boost_i_limit);
	if (rc < 0)
		return rc;

	if (wled->cfg.switch_freq != -EINVAL) {
		rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED_CTRL_SWITCH_FREQ,
				WLED_CTRL_SWITCH_FREQ_MASK,
				wled->cfg.switch_freq);
		if (rc < 0)
			return rc;
	}

	/* Per sink/string configuration */
	for (i = 0; (string_cfg >> i) != 0; i++) {
		if (string_cfg & BIT(i)) {
			addr = wled->sink_addr +
					WLED5_SINK_FS_CURR_REG(i);
			rc = regmap_update_bits(wled->regmap, addr,
					WLED_SINK_FS_MASK,
					wled->cfg.fs_current);
			if (rc < 0)
				return rc;

			addr = wled->sink_addr +
					WLED5_SINK_SRC_SEL_REG(i);
			rc = regmap_update_bits(wled->regmap, addr,
					WLED5_SINK_SRC_SEL_MASK,
					wled->cfg.mod_sel == MOD_A ?
					WLED5_SINK_SRC_SEL_MODA :
					WLED5_SINK_SRC_SEL_MODB);

			temp = i + WLED_SINK_CURR_SINK_SHFT;
			sink_en |= 1 << temp;
		}
	}

	rc = wled5_cabc_config(wled, wled->cfg.cabc_sel ? true : false);
	if (rc < 0)
		return rc;

	/* Enable one of the modulators A or B based on mod_sel */
	addr = wled->sink_addr + WLED5_SINK_MOD_A_EN_REG;
	val = (wled->cfg.mod_sel == MOD_A) ? WLED5_SINK_MOD_EN : 0;
	rc = regmap_update_bits(wled->regmap, addr,
			WLED5_SINK_MOD_EN, val);
	if (rc < 0)
		return rc;

	addr = wled->sink_addr + WLED5_SINK_MOD_B_EN_REG;
	val = (wled->cfg.mod_sel == MOD_B) ? WLED5_SINK_MOD_EN : 0;
	rc = regmap_update_bits(wled->regmap, addr,
			WLED5_SINK_MOD_EN, val);
	if (rc < 0)
		return rc;

	addr = wled->sink_addr + wled5_brt_wid_sel_reg[wled->cfg.mod_sel];
	val = (wled->max_brightness == WLED_MAX_BRIGHTNESS_15B)
		? WLED5_SINK_BRT_WIDTH_15B : WLED5_SINK_BRT_WIDTH_12B;
	rc = regmap_write(wled->regmap, addr, val);
	if (rc < 0)
		return rc;

	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED_SINK_CURR_SINK_EN, sink_en);
	if (rc < 0)
		return rc;

	/* This updates only FSC configuration in WLED5 */
	rc = wled_sync_toggle(wled);
	if (rc < 0) {
		pr_err("Failed to toggle sync reg rc:%d\n", rc);
		return rc;
	}

	rc = wled_auto_calibrate_at_init(wled);
	if (rc < 0)
		return rc;

	if (wled_exp_dimming_supported(wled)) {
		val = WLED5_EN_SLEW_CTL | wled->cfg.slew_ramp_time;
		rc = regmap_update_bits(wled->regmap,
			wled->sink_addr +
			WLED5_SINK_BRIGHTNESS_SLEW_RATE_CTL_REG,
			WLED5_EN_SLEW_CTL | WLED5_SLEW_RAMP_TIME_SEL,
			val);
		if (rc < 0)
			return rc;
	}

	if (wled->cfg.use_exp_dimming) {
		rc = wled_program_exp_dimming(wled);
		if (rc < 0) {
			dev_err(&wled->pdev->dev, "Programming exponential dimming map failed, rc=%d\n",
				rc);
			return rc;
		}
	}

	if (wled->ovp_irq >= 0) {
		rc = devm_request_threaded_irq(&wled->pdev->dev, wled->ovp_irq,
				NULL, wled_ovp_irq_handler, IRQF_ONESHOT,
				"wled_ovp_irq", wled);
		if (rc < 0) {
			pr_err("Unable to request ovp(%d) IRQ(err:%d)\n",
				wled->ovp_irq, rc);
			return rc;
		}

		rc = regmap_read(wled->regmap, wled->ctrl_addr +
				WLED_CTRL_MOD_ENABLE, &val);
		/* disable the OVP irq only if the module is not enabled */
		if (!rc && !(val & WLED_CTRL_MOD_EN_MASK)) {
			disable_irq(wled->ovp_irq);
			wled->ovp_irq_disabled = true;
		}
	}

	if (wled->flash_irq >= 0) {
		rc = devm_request_threaded_irq(&wled->pdev->dev,
				wled->flash_irq, NULL, wled_flash_irq_handler,
				IRQF_ONESHOT, "wled_flash_irq", wled);
		if (rc < 0)
			pr_err("Unable to request flash(%d) IRQ(err:%d)\n",
				wled->flash_irq, rc);
	}

	if (wled->pre_flash_irq >= 0) {
		rc = devm_request_threaded_irq(&wled->pdev->dev,
				wled->pre_flash_irq, NULL,
				wled_flash_irq_handler, IRQF_ONESHOT,
				"wled_pre_flash_irq", wled);
		if (rc < 0)
			pr_err("Unable to request pre_flash(%d) IRQ(err:%d)\n",
				wled->pre_flash_irq, rc);
	}
	return 0;
}

static int wled4_setup(struct wled *wled)
{
	int rc, temp, i;
	u8 sink_en = 0;
	u32 val;
	u8 string_cfg = wled->cfg.string_cfg;

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_OVP,
			WLED_CTRL_OVP_MASK, wled->cfg.ovp);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_ILIM,
			WLED_CTRL_ILIM_MASK, wled->cfg.boost_i_limit);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
			wled->ctrl_addr + WLED_CTRL_SWITCH_FREQ,
			WLED_CTRL_SWITCH_FREQ_MASK, wled->cfg.switch_freq);
	if (rc < 0)
		return rc;

	/* Per sink/string configuration */
	for (i = 0; (string_cfg >> i) != 0; i++) {
		if (string_cfg & BIT(i)) {
			u16 addr = wled->sink_addr +
					WLED_SINK_MOD_EN_REG(i);

			rc = regmap_update_bits(wled->regmap, addr,
					WLED_SINK_REG_STR_MOD_MASK,
					WLED_SINK_REG_STR_MOD_EN);
			if (rc < 0)
				return rc;

			addr = wled->sink_addr +
					WLED_SINK_FS_CURR_REG(i);
			rc = regmap_update_bits(wled->regmap, addr,
					WLED_SINK_FS_MASK,
					wled->cfg.fs_current);
			if (rc < 0)
				return rc;

			temp = i + WLED_SINK_CURR_SINK_SHFT;
			sink_en |= 1 << temp;
		}
	}

	rc = wled4_cabc_config(wled, wled->cfg.en_cabc);
	if (rc < 0)
		return rc;

	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED_SINK_CURR_SINK_EN, sink_en);
	if (rc < 0)
		return rc;

	rc = wled_sync_toggle(wled);
	if (rc < 0) {
		pr_err("Failed to toggle sync reg rc:%d\n", rc);
		return rc;
	}

	rc = wled_auto_calibrate_at_init(wled);
	if (rc < 0)
		return rc;

	if (wled->sc_irq >= 0) {
		rc = devm_request_threaded_irq(&wled->pdev->dev, wled->sc_irq,
				NULL, wled_sc_irq_handler, IRQF_ONESHOT,
				"wled_sc_irq", wled);
		if (rc < 0) {
			pr_err("Unable to request sc(%d) IRQ(err:%d)\n",
				wled->sc_irq, rc);
			return rc;
		}

		rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED_CTRL_SHORT_PROTECT,
				WLED_CTRL_SHORT_EN_MASK,
				WLED_CTRL_SHORT_EN_MASK);
		if (rc < 0)
			return rc;
	}

	if (wled->cfg.ext_pfet_sc_pro_en) {
		/* unlock the secure access register */
		rc = regmap_write(wled->regmap, wled->ctrl_addr +
				WLED_CTRL_SEC_ACCESS,
				WLED_CTRL_SEC_UNLOCK);
		if (rc < 0)
			return rc;

		rc = regmap_write(wled->regmap,
				wled->ctrl_addr + WLED_CTRL_TEST1,
				WLED_EXT_FET_DTEST2);
		if (rc < 0)
			return rc;
	}

	if (wled->ovp_irq >= 0) {
		rc = devm_request_threaded_irq(&wled->pdev->dev, wled->ovp_irq,
				NULL, wled_ovp_irq_handler, IRQF_ONESHOT,
				"wled_ovp_irq", wled);
		if (rc < 0) {
			pr_err("Unable to request ovp(%d) IRQ(err:%d)\n",
				wled->ovp_irq, rc);
			return rc;
		}

		rc = regmap_read(wled->regmap, wled->ctrl_addr +
				WLED_CTRL_MOD_ENABLE, &val);
		/* disable the OVP irq only if the module is not enabled */
		if (!rc && !(val & WLED_CTRL_MOD_EN_MASK)) {
			disable_irq(wled->ovp_irq);
			wled->ovp_irq_disabled = true;
		}
	}

	return 0;
}

static const struct wled_config wled4_config_defaults = {
	.boost_i_limit = 4,
	.fs_current = 10,
	.ovp = 1,
	.switch_freq = 11,
	.string_cfg = 0xf,
	.mod_sel = -EINVAL,
	.cabc_sel = -EINVAL,
	.slew_ramp_time = -EINVAL,
	.en_cabc = 0,
	.ext_pfet_sc_pro_en = 0,
	.auto_calib_enabled = 0,
};

static const struct wled_config wled5_config_defaults = {
	.boost_i_limit = 5,
	.fs_current = 10,	/* 25 mA */
	.ovp = 6,
	.switch_freq = -EINVAL,
	.string_cfg = 0xf,
	.mod_sel = 0,
	.cabc_sel = 0,
	.slew_ramp_time = 6,	/* 256 ms */
	.en_cabc = 0,
	.ext_pfet_sc_pro_en = 0,
	.auto_calib_enabled = 0,
};

struct wled_var_cfg {
	const u32 *values;
	u32 (*fn)(u32 idx);
	int size;
};

static const u32 wled4_boost_i_limit_values[] = {
	105, 280, 450, 620, 970, 1150, 1300, 1500,
};

static const struct wled_var_cfg wled4_boost_i_limit_cfg = {
	.values = wled4_boost_i_limit_values,
	.size = ARRAY_SIZE(wled4_boost_i_limit_values),
};

static inline u32 wled5_boost_i_limit_values_fn(u32 idx)
{
	return 525 + (idx * 175);
}

static const struct wled_var_cfg wled5_boost_i_limit_cfg = {
	.fn = wled5_boost_i_limit_values_fn,
	.size = 8,
};

static const u32 wled_fs_current_values[] = {
	0, 2500, 5000, 7500, 10000, 12500, 15000, 17500, 20000,
	22500, 25000, 27500, 30000,
};

static const struct wled_var_cfg wled_fs_current_cfg = {
	.values = wled_fs_current_values,
	.size = ARRAY_SIZE(wled_fs_current_values),
};

static const u32 wled4_ovp_values[] = {
	31100, 29600, 19600, 18100,
};

static const struct wled_var_cfg wled4_ovp_cfg = {
	.values = wled4_ovp_values,
	.size = ARRAY_SIZE(wled4_ovp_values),
};

static inline u32 wled5_ovp_values_fn(u32 idx)
{
	/*
	 * 0000 - 38.5 V
	 * 0001 - 37 V ..
	 * 1111 - 16 V
	 */
	return 38500 - (idx * 1500);
}

static const struct wled_var_cfg wled5_ovp_cfg = {
	.fn = wled5_ovp_values_fn,
	.size = 16,
};

static inline u32 wled_switch_freq_values_fn(u32 idx)
{
	return 9600 / (1 + idx);
}

static const struct wled_var_cfg wled_switch_freq_cfg = {
	.fn = wled_switch_freq_values_fn,
	.size = 16,
};

static const struct wled_var_cfg wled_string_cfg = {
	.size = 16,
};

static const struct wled_var_cfg wled5_mod_sel_cfg = {
	.size = 2,
};

static const struct wled_var_cfg wled5_cabc_sel_cfg = {
	.size = 4,
};

/* Applicable only for PM7325B */
static const u32 wled5_slew_ramp_time_values[] = {
	2, 4, 8, 64, 128, 192, 256, 320, 384, 448, 512, 704,
	896, 1024, 2048, 4096,
};

static const struct wled_var_cfg wled5_slew_ramp_time_cfg = {
	.values = wled5_slew_ramp_time_values,
	.size = ARRAY_SIZE(wled5_slew_ramp_time_values),
};

static u32 wled_values(const struct wled_var_cfg *cfg, u32 idx)
{
	if (!cfg)
		return UINT_MAX;
	if (idx >= cfg->size)
		return UINT_MAX;
	if (cfg->fn)
		return cfg->fn(idx);
	if (cfg->values)
		return cfg->values[idx];
	return idx;
}

static int wled_get_max_current(struct led_classdev *led_cdev,
					int *max_current)
{
	struct wled *wled;
	bool flash;

	if (!strcmp(led_cdev->name, "wled_flash")) {
		wled = container_of(led_cdev, struct wled, flash_cdev);
		flash = true;
	} else if (!strcmp(led_cdev->name, "wled_torch")) {
		wled = container_of(led_cdev, struct wled, torch_cdev);
		flash = false;
	} else {
		return -ENODEV;
	}

	if (flash)
		*max_current = wled->flash_cdev.max_brightness;
	else
		*max_current = wled->torch_cdev.max_brightness;

	return 0;
}

static int wled_get_iio_chan(struct wled *wled,
				   enum wled_iio_props chan)
{
	int rc = 0;

	/*
	 * if the channel pointer is not-NULL and has a ERR value it has
	 * already been queried upon earlier, hence return from here.
	 */
	if (IS_ERR(wled->iio_channels[chan]))
		return -EINVAL;

	if (!wled->iio_channels[chan]) {
		wled->iio_channels[chan] = devm_iio_channel_get(&wled->pdev->dev,
						  wled_iio_prop_names[chan]);
		if (IS_ERR(wled->iio_channels[chan])) {
			rc = PTR_ERR(wled->iio_channels[chan]);
			if (rc == -EPROBE_DEFER) {
				wled->iio_channels[chan] = NULL;
				return rc;
			}
			pr_err("%s channel unavailable %d\n",
			       wled_iio_prop_names[chan], rc);
			return rc;
		}
	}

	return 0;
}

static int wled_iio_get_prop(struct wled *wled,
				  enum wled_iio_props chan, int *data)
{
	int rc = 0;

	rc = wled_get_iio_chan(wled, chan);
	if (rc < 0)
		return rc;

	rc = iio_read_channel_processed(wled->iio_channels[chan], data);
	if (rc < 0)
		pr_err("Error in reading IIO channel data rc = %d\n", rc);

	return rc;
}

#define V_HDRM_MV		400
#define V_DROOP_MV		400
#define V_LED_MV		3100
#define I_FLASH_MAX_MA		60
#define EFF_FACTOR		700
static int wled_get_max_avail_current(struct led_classdev *led_cdev,
					int *max_current)
{
	struct wled *wled;
	int rc, ocv_mv, r_bat_mohms, i_bat_ma, i_sink_ma = 0, max_fsc_ma;
	int64_t p_out_string, p_out, p_in, v_safe_mv, i_flash_ma, v_ph_mv;
	union power_supply_propval prop = {};

	if (!strcmp(led_cdev->name, "wled_switch"))
		wled = container_of(led_cdev, struct wled, switch_cdev);
	else
		return -ENODEV;

	max_fsc_ma = max(wled->flash_cdev.max_brightness,
			wled->torch_cdev.max_brightness);
	if (!wled->leds_per_string || (wled->num_strings == 2 &&
					wled->leds_per_string == 8)) {
		/* Allow max for 8s2p */
		*max_current = max_fsc_ma;
		return 0;
	}

	if (wled->use_psy) {
		if (!wled->batt_psy)
			wled->batt_psy = power_supply_get_by_name("battery");

		if (!wled->batt_psy) {
			pr_err_ratelimited("Failed to get battery power supply\n");
			return -ENODEV;
		}

		rc = power_supply_get_property(wled->batt_psy, POWER_SUPPLY_PROP_VOLTAGE_OCV,
				&prop);
		if (rc < 0) {
			pr_err("Failed to get battery OCV, rc=%d\n", rc);
			return rc;
		}
		ocv_mv = prop.intval/1000;

		rc = power_supply_get_property(wled->batt_psy, POWER_SUPPLY_PROP_CURRENT_NOW,
				&prop);
		if (rc < 0) {
			pr_err("Failed to get battery current, rc=%d\n", rc);
			return rc;
		}
		i_bat_ma = -prop.intval/1000;

		rc = qti_battery_charger_get_prop("battery", BATTERY_RESISTANCE, &r_bat_mohms);
		if (rc < 0) {
			pr_err("Failed to get battery resistance, rc=%d\n", rc);
			return rc;
		}
		r_bat_mohms /= 1000;
	} else {
		rc = wled_iio_get_prop(wled, OCV, &ocv_mv);
		if (rc < 0) {
			pr_err("Error in getting OCV rc=%d\n", rc);
			return rc;
		}
		ocv_mv /= 1000;

		rc = wled_iio_get_prop(wled, IBAT, &i_bat_ma);
		if (rc < 0) {
			pr_err("Error in getting I_BAT rc=%d\n", rc);
			return rc;
		}
		i_bat_ma /= 1000;

		rc = wled_iio_get_prop(wled, RBATT, &r_bat_mohms);
		if (rc < 0) {
			pr_err("Error in getting R_BAT rc=%d\n", rc);
			return rc;
		}
		r_bat_mohms /= 1000;
	}

	pr_debug("ocv: %d i_bat: %d r_bat: %d\n", ocv_mv, i_bat_ma,
		r_bat_mohms);

	p_out_string = ((wled->leds_per_string * V_LED_MV) + V_HDRM_MV) *
			I_FLASH_MAX_MA;
	p_out = p_out_string * wled->num_strings;
	p_in = (p_out * 1000) / EFF_FACTOR;

	pr_debug("p_out_string: %lld, p_out: %lld, p_in: %lld\n", p_out_string,
		p_out, p_in);

	v_safe_mv = ocv_mv - V_DROOP_MV - ((i_bat_ma * r_bat_mohms) / 1000);
	if (v_safe_mv <= 0) {
		pr_err("V_safe_mv: %lld, cannot support flash\n", v_safe_mv);
		*max_current = 0;
		return 0;
	}

	i_flash_ma = p_in / v_safe_mv;
	v_ph_mv = ocv_mv - ((i_bat_ma + i_flash_ma) * r_bat_mohms) / 1000;

	pr_debug("v_safe: %lld, i_flash: %lld, v_ph: %lld\n", v_safe_mv,
		i_flash_ma, v_ph_mv);

	i_sink_ma = max_fsc_ma;
	if (wled->num_strings == 3 && wled->leds_per_string == 8) {
		if (v_ph_mv < 3410) {
			/* For 8s3p, I_sink(mA) = 25.396 * Vph(V) - 26.154 */
			i_sink_ma = (((25396 * v_ph_mv) / 1000) - 26154) / 1000;
			i_sink_ma *= wled->num_strings;
		}
	} else if (wled->num_strings == 3 && wled->leds_per_string == 6) {
		if (v_ph_mv < 2800) {
			/* For 6s3p, I_sink(mA) = 41.311 * Vph(V) - 52.334 */
			i_sink_ma = (((41311 * v_ph_mv) / 1000) - 52334) / 1000;
			i_sink_ma *= wled->num_strings;
		}
	} else if (wled->num_strings == 4 && wled->leds_per_string == 6) {
		if (v_ph_mv < 3400) {
			/* For 6s4p, I_sink(mA) = 26.24 * Vph(V) - 24.834 */
			i_sink_ma = (((26240 * v_ph_mv) / 1000) - 24834) / 1000;
			i_sink_ma *= wled->num_strings;
		}
	} else if (v_ph_mv < 3200) {
		i_sink_ma = max_fsc_ma / 2;
	}

	/* Clamp the sink current to maximum FSC */
	*max_current = min(i_sink_ma, max_fsc_ma);

	pr_debug("i_sink_ma: %d\n", i_sink_ma);
	return 0;
}

static ssize_t wled_flash_max_avail_current_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	int rc, max_current = 0;

	rc = wled_get_max_avail_current(led_cdev, &max_current);
	if (rc < 0)
		pr_err("query max current failed, rc=%d\n", rc);

	return scnprintf(buf, PAGE_SIZE, "%d\n", max_current);
}

static struct device_attribute wled_flash_attrs[] = {
	__ATTR(max_avail_current, 0664, wled_flash_max_avail_current_show,
		NULL),
};

static struct led_classdev *trigger_to_lcdev(struct led_trigger *trig)
{
	struct led_classdev *led_cdev;

	spin_lock(&trig->leddev_list_lock);
	list_for_each_entry(led_cdev, &trig->led_cdevs, trig_list) {
		if (!strcmp(led_cdev->default_trigger, trig->name)) {
			spin_unlock(&trig->leddev_list_lock);
			return led_cdev;
		}
	}

	spin_unlock(&trig->leddev_list_lock);
	return NULL;
}

int wled_flash_led_prepare(struct led_trigger *trig, int options,
				int *max_current)
{
	struct led_classdev *led_cdev;
	int rc;

	if (!(options & FLASH_LED_PREPARE_OPTIONS_MASK)) {
		pr_err("Invalid options %d\n", options);
		return -EINVAL;
	}

	if (!trig) {
		pr_err("Invalid led_trigger provided\n");
		return -EINVAL;
	}

	led_cdev = trigger_to_lcdev(trig);
	if (!led_cdev) {
		pr_err("Invalid led_cdev in trigger %s\n", trig->name);
		return -EINVAL;
	}

	switch (options) {
	case QUERY_MAX_CURRENT:
		rc = wled_get_max_current(led_cdev, max_current);
		if (rc < 0) {
			pr_err("Error in getting max_current for %s\n",
				led_cdev->name);
			return rc;
		}
		break;
	case QUERY_MAX_AVAIL_CURRENT:
		rc = wled_get_max_avail_current(led_cdev, max_current);
		if (rc < 0) {
			pr_err("Error in getting max_avail_current for %s\n",
				led_cdev->name);
			return rc;
		}
		break;
	case ENABLE_REGULATOR:
	case DISABLE_REGULATOR:
		/* Not supported */
		return 0;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(wled_flash_led_prepare);

static int wled_flash_set_step_delay(struct wled *wled, int step_delay)
{
	int rc, table[8] = {50, 100, 150, 200, 250, 300, 350, 400};
	u8 val;

	if (step_delay < table[0])
		val = 0;
	else if (step_delay > table[7])
		val = 7;
	else
		val = DIV_ROUND_CLOSEST(step_delay, 50) - 1;

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
			WLED5_CTRL_FLASH_STEP_CTL_REG,
			WLED5_CTRL_FLASH_STEP_MASK, val);
	if (rc < 0)
		pr_err("Error in configuring step delay, rc:%d\n", rc);

	return rc;
}

static int wled_flash_set_fsc(struct wled *wled, enum led_brightness brightness,
				int fs_current_max)
{
	int rc, fs_current;
	u8 val;

	if (!wled->num_strings) {
		pr_err("Incorrect number of strings\n");
		return -EINVAL;
	}

	fs_current = (int)brightness / wled->num_strings;
	if (fs_current > fs_current_max)
		fs_current = fs_current_max;

	/* Each LSB is 5 mA */
	val = DIV_ROUND_CLOSEST(fs_current, 5);
	rc = regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED5_SINK_FLASH_FSC_REG,
			WLED5_SINK_FLASH_FSC_MASK, val);
	if (rc < 0) {
		pr_err("Error in configuring flash_fsc, rc:%d\n", rc);
		return rc;
	}

	/* Write 0 followed by 1 to sync FSC */
	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED5_SINK_FLASH_SYNC_BIT_REG, 0);
	if (rc < 0) {
		pr_err("Error in configuring flash_sync, rc:%d\n", rc);
		return rc;
	}

	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED5_SINK_FLASH_SYNC_BIT_REG,
			WLED5_SINK_FLASH_FSC_SYNC_EN);
	if (rc < 0)
		pr_err("Error in configuring flash_sync, rc:%d\n", rc);

	return rc;
}

static void wled_flash_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct wled *wled = container_of(cdev, struct wled, flash_cdev);
	int rc;

	spin_lock(&wled->flash_lock);
	if (brightness) {
		rc = wled_flash_set_step_delay(wled, wled->fparams.step_delay);
		if (rc < 0)
			goto out;
	}

	rc = wled_flash_set_fsc(wled, brightness, wled->fparams.fs_current);
	if (rc < 0)
		goto out;

	wled->flash_mode = brightness ? WLED_FLASH : WLED_FLASH_OFF;
out:
	spin_unlock(&wled->flash_lock);
}

static void wled_torch_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct wled *wled = container_of(cdev, struct wled, torch_cdev);
	int rc;

	spin_lock(&wled->flash_lock);
	if (brightness) {
		rc = wled_flash_set_step_delay(wled, wled->tparams.step_delay);
		if (rc < 0)
			goto out;
	}

	rc = wled_flash_set_fsc(wled, brightness, wled->tparams.fs_current);
	if (rc < 0)
		goto out;

	wled->flash_mode = brightness ? WLED_PRE_FLASH : WLED_FLASH_OFF;
out:
	spin_unlock(&wled->flash_lock);
}

static void wled_switch_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct wled *wled = container_of(cdev, struct wled, switch_cdev);
	int rc;
	u8 val;

	if (brightness && wled->flash_mode == WLED_FLASH_OFF)
		return;

	spin_lock(&wled->flash_lock);
	if (wled->flash_mode == WLED_PRE_FLASH)
		val = brightness ? WLED5_SINK_PRE_FLASH_EN : 0;
	else
		val = brightness ? WLED5_SINK_FLASH_EN : 0;

	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED5_SINK_FLASH_CTL_REG, val);
	if (rc < 0)
		pr_err("Error in configuring flash_ctl, rc:%d\n", rc);

	if (!brightness) {
		rc = regmap_write(wled->regmap,
				wled->sink_addr + WLED5_SINK_FLASH_SHDN_CLR_REG,
				1);
		if (rc < 0)
			pr_err("Error in configuring flash_shdn_clr, rc:%d\n",
				rc);
	}

	spin_unlock(&wled->flash_lock);
}

static int wled_flash_device_register(struct wled *wled)
{
	int rc, i, max_brightness = 0;

	/* Not supported */
	if (is_wled4(wled))
		return 0;

	spin_lock_init(&wled->flash_lock);

	/* flash */
	for (i = 0; (wled->cfg.string_cfg >> i) != 0; i++)
		max_brightness += wled->fparams.fs_current;

	wled->flash_cdev.name = "wled_flash";
	wled->flash_cdev.max_brightness = max_brightness;
	wled->flash_cdev.brightness_set = wled_flash_brightness_set;
	rc = devm_led_classdev_register(&wled->pdev->dev, &wled->flash_cdev);
	if (rc < 0)
		return rc;

	/* torch */
	for (max_brightness = 0, i = 0; (wled->cfg.string_cfg >> i) != 0; i++)
		max_brightness += wled->tparams.fs_current;

	wled->torch_cdev.name = "wled_torch";
	wled->torch_cdev.max_brightness = max_brightness;
	wled->torch_cdev.brightness_set = wled_torch_brightness_set;
	rc = devm_led_classdev_register(&wled->pdev->dev, &wled->torch_cdev);
	if (rc < 0)
		return rc;

	/* switch */
	wled->switch_cdev.name = "wled_switch";
	wled->switch_cdev.brightness_set = wled_switch_brightness_set;
	rc = devm_led_classdev_register(&wled->pdev->dev, &wled->switch_cdev);
	if (rc < 0)
		return rc;

	for (i = 0; i < ARRAY_SIZE(wled_flash_attrs); i++) {
		rc = sysfs_create_file(&wled->switch_cdev.dev->kobj,
				&wled_flash_attrs[i].attr);
		if (rc < 0) {
			pr_err("sysfs creation failed, rc=%d\n", rc);
			goto sysfs_fail;
		}
	}

	return 0;

sysfs_fail:
	for (--i; i >= 0; i--)
		sysfs_remove_file(&wled->switch_cdev.dev->kobj,
				&wled_flash_attrs[i].attr);

	return rc;
}

static int wled_flash_configure(struct wled *wled)
{
	int rc;
	struct device_node *temp;
	struct device *dev = &wled->pdev->dev;
	const char *cdev_name;

	/* Not supported */
	if (is_wled4(wled))
		return 0;

	of_property_read_u32(wled->pdev->dev.of_node, "qcom,leds-per-string",
		&wled->leds_per_string);

	for_each_available_child_of_node(wled->pdev->dev.of_node, temp) {
		rc = of_property_read_string(temp, "label", &cdev_name);
		if (rc < 0)
			continue;

		if (!strcmp(cdev_name, "flash")) {
			/* Value read in mA */
			wled->fparams.fs_current = 50;
			rc = of_property_read_u32(temp, "qcom,wled-flash-fsc",
						&wled->fparams.fs_current);
			if (!rc) {
				if (wled->fparams.fs_current <= 0 ||
					wled->fparams.fs_current > 60) {
					dev_err(dev, "Incorrect WLED flash FSC rc:%d\n",
						rc);
					return rc;
				}
			}

			/* Value read in us */
			wled->fparams.step_delay = 200;
			rc = of_property_read_u32(temp, "qcom,wled-flash-step",
						&wled->fparams.step_delay);
			if (!rc) {
				if (wled->fparams.step_delay < 50 ||
					wled->fparams.step_delay > 400) {
					dev_err(dev, "Incorrect WLED flash step delay rc:%d\n",
						rc);
					return rc;
				}
			}

			/* Value read in ms */
			wled->fparams.safety_timer = 100;
			rc = of_property_read_u32(temp, "qcom,wled-flash-timer",
						&wled->fparams.safety_timer);
			if (!rc) {
				if (wled->fparams.safety_timer < 50 ||
					wled->fparams.safety_timer > 1200) {
					dev_err(dev, "Incorrect WLED flash safety time rc:%d\n",
						rc);
					return rc;
				}
			}

			rc = of_property_read_string(temp,
					"qcom,default-led-trigger",
					&wled->flash_cdev.default_trigger);
			if (rc < 0)
				wled->flash_cdev.default_trigger = "wled_flash";
		} else if (!strcmp(cdev_name, "torch")) {
			/* Value read in mA */
			wled->tparams.fs_current = 50;
			rc = of_property_read_u32(temp, "qcom,wled-torch-fsc",
						&wled->tparams.fs_current);
			if (!rc) {
				if (wled->tparams.fs_current <= 0 ||
					wled->tparams.fs_current > 60) {
					dev_err(dev, "Incorrect WLED torch FSC rc:%d\n",
						rc);
					return rc;
				}
			}

			/* Value read in us */
			wled->tparams.step_delay = 200;
			rc = of_property_read_u32(temp, "qcom,wled-torch-step",
						&wled->tparams.step_delay);
			if (!rc) {
				if (wled->tparams.step_delay < 50 ||
					wled->tparams.step_delay > 400) {
					dev_err(dev, "Incorrect WLED torch step delay rc:%d\n",
						rc);
					return rc;
				}
			}

			/* Value read in ms */
			wled->tparams.safety_timer = 600;
			rc = of_property_read_u32(temp, "qcom,wled-torch-timer",
						&wled->tparams.safety_timer);
			if (!rc) {
				if (wled->tparams.safety_timer < 50 ||
					wled->tparams.safety_timer > 1200) {
					dev_err(dev, "Incorrect WLED torch safety time rc:%d\n",
						rc);
					return rc;
				}
			}

			rc = of_property_read_string(temp,
					"qcom,default-led-trigger",
					&wled->torch_cdev.default_trigger);
			if (rc < 0)
				wled->torch_cdev.default_trigger = "wled_torch";
		} else if (!strcmp(cdev_name, "switch")) {
			rc = of_property_read_string(temp,
					"qcom,default-led-trigger",
					&wled->switch_cdev.default_trigger);
			if (rc < 0)
				wled->switch_cdev.default_trigger =
					"wled_switch";
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

static int wled_flash_setup(struct wled *wled)
{
	int rc, i;
	u8 val;

	/* Set FLASH_VREF_ADIM_HDIM to maximum */
	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_FLASH_HDRM_REG, 0xF);
	if (rc < 0)
		return rc;

	/* Write a full brightness value */
	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_PRE_FLASH_BRT_REG, 0xFF);
	if (rc < 0)
		return rc;

	/* Sync the brightness by writing a 0 followed by 1 */
	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_PRE_FLASH_SYNC_REG, 0);
	if (rc < 0)
		return rc;

	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_PRE_FLASH_SYNC_REG, 1);
	if (rc < 0)
		return rc;

	/* Write a full brightness value */
	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_FLASH_BRT_REG, 0xFF);
	if (rc < 0)
		return rc;

	/* Sync the brightness by writing a 0 followed by 1 */
	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_FLASH_SYNC_REG, 0);
	if (rc < 0)
		return rc;

	rc = regmap_write(wled->regmap,
			wled->ctrl_addr + WLED5_CTRL_FLASH_SYNC_REG, 1);
	if (rc < 0)
		return rc;

	for (val = 0, i = 0; (wled->cfg.string_cfg >> i) != 0; i++) {
		if (wled->cfg.string_cfg & BIT(i)) {
			val |= 1 << (i + WLED_SINK_CURR_SINK_SHFT);
			wled->num_strings++;
		}
	}

	/* Enable current sinks for flash */
	rc = regmap_update_bits(wled->regmap,
			wled->sink_addr + WLED5_SINK_FLASH_SINK_EN_REG,
			WLED_SINK_CURR_SINK_MASK, val);
	if (rc < 0)
		return rc;

	/* Enable flash and pre_flash safety timers */
	val = get_wled_safety_time(wled->tparams.safety_timer) <<
			WLED5_PRE_FLASH_SAFETY_SHIFT;
	val |= get_wled_safety_time(wled->fparams.safety_timer);
	rc = regmap_write(wled->regmap,
			wled->sink_addr + WLED5_SINK_FLASH_TIMER_CTL_REG, val);
	if (rc < 0)
		return rc;

	return 0;
}

static int wled_configure(struct wled *wled, struct device *dev)
{
	struct wled_config *cfg = &wled->cfg;
	const __be32 *prop_addr;
	u32 val, c;
	int rc, i, j, size;
	struct wled_u32_opts {
		const char *name;
		u32 *val_ptr;
		const struct wled_var_cfg *cfg;
	};

	const struct wled_u32_opts *u32_opts;
	const struct wled_u32_opts wled4_opts[] = {
		{
			.name = "qcom,boost-current-limit",
			.val_ptr = &cfg->boost_i_limit,
			.cfg = &wled4_boost_i_limit_cfg,
		},
		{
			.name = "qcom,fs-current-limit",
			.val_ptr = &cfg->fs_current,
			.cfg = &wled_fs_current_cfg,
		},
		{
			.name = "qcom,ovp",
			.val_ptr = &cfg->ovp,
			.cfg = &wled4_ovp_cfg,
		},
		{
			.name = "qcom,switching-freq",
			.val_ptr = &cfg->switch_freq,
			.cfg = &wled_switch_freq_cfg,
		},
		{
			.name = "qcom,string-cfg",
			.val_ptr = &cfg->string_cfg,
			.cfg = &wled_string_cfg,
		},
	};

	const struct wled_u32_opts wled5_opts[] = {
		{
			.name = "qcom,boost-current-limit",
			.val_ptr = &cfg->boost_i_limit,
			.cfg = &wled5_boost_i_limit_cfg,
		},
		{
			.name = "qcom,fs-current-limit",
			.val_ptr = &cfg->fs_current,
			.cfg = &wled_fs_current_cfg,
		},
		{
			.name = "qcom,ovp",
			.val_ptr = &cfg->ovp,
			.cfg = &wled5_ovp_cfg,
		},
		{
			.name = "qcom,switching-freq",
			.val_ptr = &cfg->switch_freq,
			.cfg = &wled_switch_freq_cfg,
		},
		{
			.name = "qcom,string-cfg",
			.val_ptr = &cfg->string_cfg,
			.cfg = &wled_string_cfg,
		},
		{
			.name = "qcom,modulator-sel",
			.val_ptr = &cfg->mod_sel,
			.cfg = &wled5_mod_sel_cfg,
		},
		{
			.name = "qcom,cabc-sel",
			.val_ptr = &cfg->cabc_sel,
			.cfg = &wled5_cabc_sel_cfg,
		},
		{
			.name = "qcom,slew-ramp-time",
			.val_ptr = &cfg->slew_ramp_time,
			.cfg = &wled5_slew_ramp_time_cfg,
		},
	};

	const struct {
		const char *name;
		bool *val_ptr;
	} bool_opts[] = {
		{ "qcom,en-cabc", &cfg->en_cabc, },
		{ "qcom,ext-pfet-sc-pro", &cfg->ext_pfet_sc_pro_en, },
		{ "qcom,auto-calibration", &cfg->auto_calib_enabled, },
		{ "qcom,use-exp-dimming", &cfg->use_exp_dimming, },
	};

	prop_addr = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!prop_addr) {
		pr_err("invalid IO resources\n");
		return -EINVAL;
	}
	wled->ctrl_addr = be32_to_cpu(*prop_addr);

	rc = regmap_read(wled->regmap,
		wled->ctrl_addr + WLED_CTRL_REVISION2, &wled->rev2);
	if (rc < 0) {
		pr_err("Error in reading WLED_CTRL_REVISION2 rc=%d\n", rc);
		return -EINVAL;
	}

	prop_addr = of_get_address(dev->of_node, 1, NULL, NULL);
	if (!prop_addr) {
		pr_err("invalid IO resources\n");
		return -EINVAL;
	}
	wled->sink_addr = be32_to_cpu(*prop_addr);
	rc = of_property_read_string(dev->of_node, "label", &wled->name);
	if (rc < 0)
		wled->name = dev->of_node->name;

	if (is_wled5(wled)) {
		u32_opts = wled5_opts;
		size = ARRAY_SIZE(wled5_opts);
		*cfg = wled5_config_defaults;
		wled->cabc_config = wled5_cabc_config;
	} else if (is_wled4(wled)) {
		u32_opts = wled4_opts;
		size = ARRAY_SIZE(wled4_opts);
		*cfg = wled4_config_defaults;
		wled->cabc_config = wled4_cabc_config;
	} else {
		pr_err("Unknown WLED version %d\n", *wled->version);
		return -EINVAL;
	}

	for (i = 0; i < size; ++i) {
		rc = of_property_read_u32(dev->of_node, u32_opts[i].name, &val);
		if (rc == -EINVAL) {
			continue;
		} else if (rc < 0) {
			pr_err("error reading '%s'\n", u32_opts[i].name);
			return rc;
		}

		c = UINT_MAX;
		for (j = 0; c != val; j++) {
			c = wled_values(u32_opts[i].cfg, j);
			if (c == UINT_MAX) {
				pr_err("invalid value for '%s'\n",
					u32_opts[i].name);
				return -EINVAL;
			}

			if (c == val)
				break;
		}

		pr_debug("'%s' = %u\n", u32_opts[i].name, c);
		*u32_opts[i].val_ptr = j;
	}

	for (i = 0; i < ARRAY_SIZE(bool_opts); ++i) {
		if (of_property_read_bool(dev->of_node, bool_opts[i].name))
			*bool_opts[i].val_ptr = true;
	}

	if (wled->cfg.use_exp_dimming) {
		rc = wled_read_exp_dimming_map(dev->of_node, wled);
		if (rc < 0) {
			dev_err(&wled->pdev->dev,
				"Reading exponential dimming map failed, rc=%d\n", rc);
			return rc;
		}
	}

	wled->sc_irq = platform_get_irq_byname(wled->pdev, "sc-irq");
	if (wled->sc_irq < 0)
		dev_dbg(&wled->pdev->dev, "sc irq is not used\n");

	wled->ovp_irq = platform_get_irq_byname(wled->pdev, "ovp-irq");
	if (wled->ovp_irq < 0)
		dev_dbg(&wled->pdev->dev, "ovp irq is not used\n");

	wled->flash_irq = platform_get_irq_byname(wled->pdev, "flash-irq");
	if (wled->flash_irq < 0)
		dev_dbg(&wled->pdev->dev, "flash irq is not used\n");

	wled->pre_flash_irq = platform_get_irq_byname(wled->pdev,
				"pre-flash-irq");
	if (wled->pre_flash_irq < 0)
		dev_dbg(&wled->pdev->dev, "pre_flash irq is not used\n");

	return 0;
}

static const struct backlight_ops wled_ops = {
	.update_status = wled_update_status,
	.get_brightness = wled_get_brightness,
};

static int wled_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bl;
	struct wled *wled;
	struct regmap *regmap;
	u32 val;
	int rc;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		pr_err("Unable to get regmap\n");
		return -EINVAL;
	}

	wled = devm_kzalloc(&pdev->dev, sizeof(*wled), GFP_KERNEL);
	if (!wled)
		return -ENOMEM;

	wled->regmap = regmap;
	wled->pdev = pdev;

	wled->version = of_device_get_match_data(&pdev->dev);
	if (!wled->version) {
		dev_err(&pdev->dev, "Unknown device version\n");
		return -ENODEV;
	}

	if (*wled->version == WLED_PM7325B)
		wled->use_psy = true;

	rc = wled_configure(wled, &pdev->dev);
	if (rc < 0) {
		dev_err(&pdev->dev, "wled configure failed rc:%d\n", rc);
		return rc;
	}

	rc = wled_flash_configure(wled);
	if (rc < 0) {
		dev_err(&pdev->dev, "wled configure failed rc:%d\n", rc);
		return rc;
	}

	mutex_init(&wled->lock);

	val = WLED_DEFAULT_BRIGHTNESS;
	of_property_read_u32(pdev->dev.of_node, "default-brightness", &val);
	wled->brightness = val;

	val = WLED_MAX_BRIGHTNESS_12B;
	of_property_read_u32(pdev->dev.of_node, "max-brightness", &val);
	wled->max_brightness = val;

	/* For WLED5, when CABC is enabled, max brightness is 4095. */
	if (is_wled5(wled) && wled->cfg.cabc_sel)
		wled->max_brightness = WLED_MAX_BRIGHTNESS_12B;

	if (is_wled4(wled))
		rc = wled4_setup(wled);
	else
		rc = wled5_setup(wled);
	if (rc < 0) {
		dev_err(&pdev->dev, "wled setup failed rc:%d\n", rc);
		return rc;
	}

	platform_set_drvdata(pdev, wled);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.brightness = val;
	props.max_brightness = wled->max_brightness;
	bl = devm_backlight_device_register(&pdev->dev, wled->name,
					    &pdev->dev, wled,
					    &wled_ops, &props);
	if (IS_ERR_OR_NULL(bl)) {
		rc = PTR_ERR_OR_ZERO(bl);
		if (!rc)
			rc = -ENODEV;
		dev_err(&pdev->dev, "failed to register backlight rc:%d\n", rc);
		return rc;
	}

	rc = wled_flash_device_register(wled);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to register WLED flash/torch rc:%d\n",
			rc);
		return rc;
	}

	wled->iio_channels = devm_kcalloc(&pdev->dev,
				ARRAY_SIZE(wled_iio_prop_names),
				sizeof(struct iio_channel *), GFP_KERNEL);
	if (!wled->iio_channels)
		return -ENOMEM;

	return rc;
}

static const struct of_device_id wled_match_table[] = {
	{ .compatible = "qcom,pm6150l-spmi-wled", .data = &version_table[2] },
	{ .compatible = "qcom,pm660l-spmi-wled",  .data = &version_table[1] },
	{ .compatible = "qcom,pm7325b-spmi-wled", .data = &version_table[3] },
	{ .compatible = "qcom,pm8150l-spmi-wled", .data = &version_table[2] },
	{ .compatible = "qcom,pmi8998-spmi-wled", .data = &version_table[0] },
	{ },
};

static struct platform_driver wled_driver = {
	.probe = wled_probe,
	.driver	= {
		.name = "qcom-spmi-wled",
		.of_match_table	= wled_match_table,
	},
};

module_platform_driver(wled_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. SPMI PMIC WLED driver");
MODULE_LICENSE("GPL");
