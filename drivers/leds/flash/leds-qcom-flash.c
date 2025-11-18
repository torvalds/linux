// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/leds.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <media/v4l2-flash-led-class.h>

/* registers definitions */
#define FLASH_REVISION_REG		0x00
#define FLASH_4CH_REVISION_V0P1		0x01

#define FLASH_TYPE_REG			0x04
#define FLASH_TYPE_VAL			0x18

#define FLASH_SUBTYPE_REG		0x05
#define FLASH_SUBTYPE_3CH_PM8150_VAL	0x04
#define FLASH_SUBTYPE_3CH_PMI8998_VAL	0x03
#define FLASH_SUBTYPE_4CH_VAL		0x07

#define FLASH_STS_3CH_OTST1		BIT(0)
#define FLASH_STS_3CH_OTST2		BIT(1)
#define FLASH_STS_3CH_OTST3		BIT(2)
#define FLASH_STS_3CH_BOB_THM_OVERLOAD	BIT(3)
#define FLASH_STS_3CH_VPH_DROOP		BIT(4)
#define FLASH_STS_3CH_BOB_ILIM_S1	BIT(5)
#define FLASH_STS_3CH_BOB_ILIM_S2	BIT(6)
#define FLASH_STS_3CH_BCL_IBAT		BIT(7)

#define FLASH_STS_4CH_VPH_LOW		BIT(0)
#define FLASH_STS_4CH_BCL_IBAT		BIT(1)
#define FLASH_STS_4CH_BOB_ILIM_S1	BIT(2)
#define FLASH_STS_4CH_BOB_ILIM_S2	BIT(3)
#define FLASH_STS_4CH_OTST2		BIT(4)
#define FLASH_STS_4CH_OTST1		BIT(5)
#define FLASH_STS_4CHG_BOB_THM_OVERLOAD	BIT(6)

#define FLASH_TIMER_EN_BIT		BIT(7)
#define FLASH_TIMER_VAL_MASK		GENMASK(6, 0)
#define FLASH_TIMER_STEP_MS		10

#define FLASH_STROBE_HW_SW_SEL_BIT	BIT(2)
#define SW_STROBE_VAL			0
#define HW_STROBE_VAL			1
#define FLASH_HW_STROBE_TRIGGER_SEL_BIT	BIT(1)
#define STROBE_LEVEL_TRIGGER_VAL	0
#define STROBE_EDGE_TRIGGER_VAL		1
#define FLASH_STROBE_POLARITY_BIT	BIT(0)
#define STROBE_ACTIVE_HIGH_VAL		1

#define FLASH_IRES_MASK_4CH		BIT(0)
#define FLASH_IRES_MASK_3CH		GENMASK(1, 0)
#define FLASH_IRES_12P5MA_VAL		0
#define FLASH_IRES_5MA_VAL_4CH		1
#define FLASH_IRES_5MA_VAL_3CH		3

/* constants */
#define FLASH_CURRENT_MAX_UA		1500000
#define TORCH_CURRENT_MAX_UA		500000
#define FLASH_TOTAL_CURRENT_MAX_UA	2000000
#define FLASH_CURRENT_DEFAULT_UA	1000000
#define TORCH_CURRENT_DEFAULT_UA	200000

#define TORCH_IRES_UA			5000
#define FLASH_IRES_UA			12500

#define FLASH_TIMEOUT_MAX_US		1280000
#define FLASH_TIMEOUT_STEP_US		10000

#define UA_PER_MA			1000

/* thermal threshold constants */
#define OTST_3CH_MIN_VAL		3
#define OTST1_4CH_MIN_VAL		0
#define OTST1_4CH_V0P1_MIN_VAL		3
#define OTST2_4CH_MIN_VAL		0

#define OTST1_MAX_CURRENT_MA		1000
#define OTST2_MAX_CURRENT_MA		500
#define OTST3_MAX_CURRENT_MA		200

enum hw_type {
	QCOM_MVFLASH_3CH,
	QCOM_MVFLASH_4CH,
};

enum led_mode {
	FLASH_MODE,
	TORCH_MODE,
};

enum led_strobe {
	SW_STROBE,
	HW_STROBE,
};

enum {
	REG_STATUS1,
	REG_STATUS2,
	REG_STATUS3,
	REG_CHAN_TIMER,
	REG_ITARGET,
	REG_MODULE_EN,
	REG_IRESOLUTION,
	REG_CHAN_STROBE,
	REG_CHAN_EN,
	REG_THERM_THRSH1,
	REG_THERM_THRSH2,
	REG_THERM_THRSH3,
	REG_TORCH_CLAMP,
	REG_MAX_COUNT,
};

static const struct reg_field mvflash_3ch_pmi8998_regs[REG_MAX_COUNT] = {
	[REG_STATUS1]		= REG_FIELD(0x08, 0, 5),
	[REG_STATUS2]		= REG_FIELD(0x09, 0, 7),
	[REG_STATUS3]		= REG_FIELD(0x0a, 0, 7),
	[REG_CHAN_TIMER]	= REG_FIELD_ID(0x40, 0, 7, 3, 1),
	[REG_ITARGET]		= REG_FIELD_ID(0x43, 0, 6, 3, 1),
	[REG_MODULE_EN]		= REG_FIELD(0x46, 7, 7),
	[REG_IRESOLUTION]	= REG_FIELD(0x47, 0, 5),
	[REG_CHAN_STROBE]	= REG_FIELD_ID(0x49, 0, 2, 3, 1),
	[REG_CHAN_EN]		= REG_FIELD(0x4c, 0, 2),
	[REG_THERM_THRSH1]	= REG_FIELD(0x56, 0, 2),
	[REG_THERM_THRSH2]	= REG_FIELD(0x57, 0, 2),
	[REG_THERM_THRSH3]	= REG_FIELD(0x58, 0, 2),
	[REG_TORCH_CLAMP]	= REG_FIELD(0xea, 0, 6),
};

static const struct reg_field mvflash_3ch_regs[REG_MAX_COUNT] = {
	[REG_STATUS1]		= REG_FIELD(0x08, 0, 7),
	[REG_STATUS2]		= REG_FIELD(0x09, 0, 7),
	[REG_STATUS3]		= REG_FIELD(0x0a, 0, 7),
	[REG_CHAN_TIMER]	= REG_FIELD_ID(0x40, 0, 7, 3, 1),
	[REG_ITARGET]		= REG_FIELD_ID(0x43, 0, 6, 3, 1),
	[REG_MODULE_EN]		= REG_FIELD(0x46, 7, 7),
	[REG_IRESOLUTION]	= REG_FIELD(0x47, 0, 5),
	[REG_CHAN_STROBE]	= REG_FIELD_ID(0x49, 0, 2, 3, 1),
	[REG_CHAN_EN]		= REG_FIELD(0x4c, 0, 2),
	[REG_THERM_THRSH1]	= REG_FIELD(0x56, 0, 2),
	[REG_THERM_THRSH2]	= REG_FIELD(0x57, 0, 2),
	[REG_THERM_THRSH3]	= REG_FIELD(0x58, 0, 2),
	[REG_TORCH_CLAMP]	= REG_FIELD(0xec, 0, 6),
};

static const struct reg_field mvflash_4ch_regs[REG_MAX_COUNT] = {
	[REG_STATUS1]		= REG_FIELD(0x06, 0, 7),
	[REG_STATUS2]		= REG_FIELD(0x07, 0, 6),
	[REG_STATUS3]		= REG_FIELD(0x09, 0, 7),
	[REG_CHAN_TIMER]	= REG_FIELD_ID(0x3e, 0, 7, 4, 1),
	[REG_ITARGET]		= REG_FIELD_ID(0x42, 0, 6, 4, 1),
	[REG_MODULE_EN]		= REG_FIELD(0x46, 7, 7),
	[REG_IRESOLUTION]	= REG_FIELD(0x49, 0, 3),
	[REG_CHAN_STROBE]	= REG_FIELD_ID(0x4a, 0, 6, 4, 1),
	[REG_CHAN_EN]		= REG_FIELD(0x4e, 0, 3),
	[REG_THERM_THRSH1]	= REG_FIELD(0x7a, 0, 2),
	[REG_THERM_THRSH2]	= REG_FIELD(0x78, 0, 2),
	[REG_TORCH_CLAMP]	= REG_FIELD(0xed, 0, 6),
};

struct qcom_flash_data {
	struct v4l2_flash	**v4l2_flash;
	struct regmap_field     *r_fields[REG_MAX_COUNT];
	struct mutex		lock;
	enum hw_type		hw_type;
	u32			total_ma;
	u8			leds_count;
	u8			max_channels;
	u8			chan_en_bits;
	u8			revision;
	u8			torch_clamp;
};

struct qcom_flash_led {
	struct qcom_flash_data		*flash_data;
	struct led_classdev_flash	flash;
	u32				max_flash_current_ma;
	u32				max_torch_current_ma;
	u32				max_timeout_ms;
	u32				flash_current_ma;
	u32				flash_timeout_ms;
	u32				current_in_use_ma;
	u8				*chan_id;
	u8				chan_count;
	bool				enabled;
};

static int set_flash_module_en(struct qcom_flash_led *led, bool en)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	u8 led_mask = 0, enable;
	int i, rc;

	for (i = 0; i < led->chan_count; i++)
		led_mask |= BIT(led->chan_id[i]);

	mutex_lock(&flash_data->lock);
	if (en)
		flash_data->chan_en_bits |= led_mask;
	else
		flash_data->chan_en_bits &= ~led_mask;

	enable = !!flash_data->chan_en_bits;
	rc = regmap_field_write(flash_data->r_fields[REG_MODULE_EN], enable);
	if (rc)
		dev_err(led->flash.led_cdev.dev, "write module_en failed, rc=%d\n", rc);
	mutex_unlock(&flash_data->lock);

	return rc;
}

static int update_allowed_flash_current(struct qcom_flash_led *led, u32 *current_ma, bool strobe)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	u32 therm_ma, avail_ma, thrsh[3], min_thrsh, sts;
	int rc = 0;

	mutex_lock(&flash_data->lock);
	/*
	 * Put previously allocated current into allowed budget in either of these two cases:
	 * 1) LED is disabled;
	 * 2) LED is enabled repeatedly
	 */
	if (!strobe || led->current_in_use_ma != 0) {
		if (flash_data->total_ma >= led->current_in_use_ma)
			flash_data->total_ma -= led->current_in_use_ma;
		else
			flash_data->total_ma = 0;

		led->current_in_use_ma = 0;
		if (!strobe)
			goto unlock;
	}

	/*
	 * Cache the default thermal threshold settings, and set them to the lowest levels before
	 * reading over-temp real time status. If over-temp has been triggered at the lowest
	 * threshold, it's very likely that it would be triggered at a higher (default) threshold
	 * when more flash current is requested. Prevent device from triggering over-temp condition
	 * by limiting the flash current for the new request.
	 */
	rc = regmap_field_read(flash_data->r_fields[REG_THERM_THRSH1], &thrsh[0]);
	if (rc < 0)
		goto unlock;

	rc = regmap_field_read(flash_data->r_fields[REG_THERM_THRSH2], &thrsh[1]);
	if (rc < 0)
		goto unlock;

	if (flash_data->hw_type == QCOM_MVFLASH_3CH) {
		rc = regmap_field_read(flash_data->r_fields[REG_THERM_THRSH3], &thrsh[2]);
		if (rc < 0)
			goto unlock;
	}

	min_thrsh = OTST_3CH_MIN_VAL;
	if (flash_data->hw_type == QCOM_MVFLASH_4CH)
		min_thrsh = (flash_data->revision == FLASH_4CH_REVISION_V0P1) ?
			OTST1_4CH_V0P1_MIN_VAL : OTST1_4CH_MIN_VAL;

	rc = regmap_field_write(flash_data->r_fields[REG_THERM_THRSH1], min_thrsh);
	if (rc < 0)
		goto unlock;

	if (flash_data->hw_type == QCOM_MVFLASH_4CH)
		min_thrsh = OTST2_4CH_MIN_VAL;

	/*
	 * The default thermal threshold settings have been updated hence
	 * restore them if any fault happens starting from here.
	 */
	rc = regmap_field_write(flash_data->r_fields[REG_THERM_THRSH2], min_thrsh);
	if (rc < 0)
		goto restore;

	if (flash_data->hw_type == QCOM_MVFLASH_3CH) {
		rc = regmap_field_write(flash_data->r_fields[REG_THERM_THRSH3], min_thrsh);
		if (rc < 0)
			goto restore;
	}

	/* Read thermal level status to get corresponding derating flash current */
	rc = regmap_field_read(flash_data->r_fields[REG_STATUS2], &sts);
	if (rc)
		goto restore;

	therm_ma = FLASH_TOTAL_CURRENT_MAX_UA / 1000;
	if (flash_data->hw_type == QCOM_MVFLASH_3CH) {
		if (sts & FLASH_STS_3CH_OTST3)
			therm_ma = OTST3_MAX_CURRENT_MA;
		else if (sts & FLASH_STS_3CH_OTST2)
			therm_ma = OTST2_MAX_CURRENT_MA;
		else if (sts & FLASH_STS_3CH_OTST1)
			therm_ma = OTST1_MAX_CURRENT_MA;
	} else {
		if (sts & FLASH_STS_4CH_OTST2)
			therm_ma = OTST2_MAX_CURRENT_MA;
		else if (sts & FLASH_STS_4CH_OTST1)
			therm_ma = OTST1_MAX_CURRENT_MA;
	}

	/* Calculate the allowed flash current for the request */
	if (therm_ma <= flash_data->total_ma)
		avail_ma = 0;
	else
		avail_ma = therm_ma - flash_data->total_ma;

	*current_ma = min_t(u32, *current_ma, avail_ma);
	led->current_in_use_ma = *current_ma;
	flash_data->total_ma += led->current_in_use_ma;

	dev_dbg(led->flash.led_cdev.dev, "allowed flash current: %dmA, total current: %dmA\n",
					led->current_in_use_ma, flash_data->total_ma);

restore:
	/* Restore to default thermal threshold settings */
	rc = regmap_field_write(flash_data->r_fields[REG_THERM_THRSH1], thrsh[0]);
	if (rc < 0)
		goto unlock;

	rc = regmap_field_write(flash_data->r_fields[REG_THERM_THRSH2], thrsh[1]);
	if (rc < 0)
		goto unlock;

	if (flash_data->hw_type == QCOM_MVFLASH_3CH)
		rc = regmap_field_write(flash_data->r_fields[REG_THERM_THRSH3], thrsh[2]);

unlock:
	mutex_unlock(&flash_data->lock);
	return rc;
}

static int set_flash_current(struct qcom_flash_led *led, u32 current_ma, enum led_mode mode)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	u32 itarg_ua, ires_ua;
	u8 shift, ires_mask = 0, ires_val = 0, chan_id;
	int i, rc;

	/*
	 * Split the current across the channels and set the
	 * IRESOLUTION and ITARGET registers accordingly.
	 */
	itarg_ua = (current_ma * UA_PER_MA) / led->chan_count + 1;
	ires_ua = (mode == FLASH_MODE) ? FLASH_IRES_UA : TORCH_IRES_UA;

	for (i = 0; i < led->chan_count; i++) {
		u8 itarget = 0;

		if (itarg_ua > ires_ua)
			itarget = itarg_ua / ires_ua - 1;

		chan_id = led->chan_id[i];

		rc = regmap_fields_write(flash_data->r_fields[REG_ITARGET], chan_id, itarget);
		if (rc)
			return rc;

		if (flash_data->hw_type == QCOM_MVFLASH_3CH) {
			shift = chan_id * 2;
			ires_mask |= FLASH_IRES_MASK_3CH << shift;
			ires_val |= ((mode == FLASH_MODE) ?
				(FLASH_IRES_12P5MA_VAL << shift) :
				(FLASH_IRES_5MA_VAL_3CH << shift));
		} else if (flash_data->hw_type == QCOM_MVFLASH_4CH) {
			shift = chan_id;
			ires_mask |= FLASH_IRES_MASK_4CH << shift;
			ires_val |= ((mode == FLASH_MODE) ?
				(FLASH_IRES_12P5MA_VAL << shift) :
				(FLASH_IRES_5MA_VAL_4CH << shift));
		} else {
			dev_err(led->flash.led_cdev.dev,
					"HW type %d is not supported\n", flash_data->hw_type);
			return -EOPNOTSUPP;
		}
	}

	return regmap_field_update_bits(flash_data->r_fields[REG_IRESOLUTION], ires_mask, ires_val);
}

static int set_flash_timeout(struct qcom_flash_led *led, u32 timeout_ms)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	u8 timer, chan_id;
	int rc, i;

	/* set SAFETY_TIMER for all the channels connected to the same LED */
	timeout_ms = min_t(u32, timeout_ms, led->max_timeout_ms);

	for (i = 0; i < led->chan_count; i++) {
		chan_id = led->chan_id[i];

		timer = timeout_ms / FLASH_TIMER_STEP_MS;
		timer = clamp_t(u8, timer, 0, FLASH_TIMER_VAL_MASK);

		if (timeout_ms)
			timer |= FLASH_TIMER_EN_BIT;

		rc = regmap_fields_write(flash_data->r_fields[REG_CHAN_TIMER], chan_id, timer);
		if (rc)
			return rc;
	}

	return 0;
}

static int set_flash_strobe(struct qcom_flash_led *led, enum led_strobe strobe, bool state)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	u8 strobe_sel, chan_en, chan_id, chan_mask = 0;
	int rc, i;

	/* Set SW strobe config for all channels connected to the LED */
	for (i = 0; i < led->chan_count; i++) {
		chan_id = led->chan_id[i];

		if (strobe == SW_STROBE)
			strobe_sel = FIELD_PREP(FLASH_STROBE_HW_SW_SEL_BIT, SW_STROBE_VAL);
		else
			strobe_sel = FIELD_PREP(FLASH_STROBE_HW_SW_SEL_BIT, HW_STROBE_VAL);

		strobe_sel |=
			FIELD_PREP(FLASH_HW_STROBE_TRIGGER_SEL_BIT, STROBE_LEVEL_TRIGGER_VAL) |
			FIELD_PREP(FLASH_STROBE_POLARITY_BIT, STROBE_ACTIVE_HIGH_VAL);

		rc = regmap_fields_write(
				flash_data->r_fields[REG_CHAN_STROBE], chan_id, strobe_sel);
		if (rc)
			return rc;

		chan_mask |= BIT(chan_id);
	}

	/* Enable/disable flash channels */
	chan_en = state ? chan_mask : 0;
	rc = regmap_field_update_bits(flash_data->r_fields[REG_CHAN_EN], chan_mask, chan_en);
	if (rc)
		return rc;

	led->enabled = state;
	return 0;
}

static inline struct qcom_flash_led *flcdev_to_qcom_fled(struct led_classdev_flash *flcdev)
{
	return container_of(flcdev, struct qcom_flash_led, flash);
}

static int qcom_flash_brightness_set(struct led_classdev_flash *fled_cdev, u32 brightness)
{
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);

	led->flash_current_ma = min_t(u32, led->max_flash_current_ma, brightness / UA_PER_MA);
	return 0;
}

static int qcom_flash_timeout_set(struct led_classdev_flash *fled_cdev, u32 timeout)
{
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);

	led->flash_timeout_ms = timeout / USEC_PER_MSEC;
	return 0;
}

static int qcom_flash_strobe_set(struct led_classdev_flash *fled_cdev, bool state)
{
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);
	int rc;

	rc = set_flash_strobe(led, SW_STROBE, false);
	if (rc)
		return rc;

	rc = update_allowed_flash_current(led, &led->flash_current_ma, state);
	if (rc < 0)
		return rc;

	rc = set_flash_current(led, led->flash_current_ma, FLASH_MODE);
	if (rc)
		return rc;

	rc = set_flash_timeout(led, led->flash_timeout_ms);
	if (rc)
		return rc;

	rc = set_flash_module_en(led, state);
	if (rc)
		return rc;

	return set_flash_strobe(led, SW_STROBE, state);
}

static int qcom_flash_strobe_get(struct led_classdev_flash *fled_cdev, bool *state)
{
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);

	*state = led->enabled;
	return 0;
}

static int qcom_flash_fault_get(struct led_classdev_flash *fled_cdev, u32 *fault)
{
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);
	struct qcom_flash_data *flash_data = led->flash_data;
	u8 shift, chan_id, chan_mask = 0;
	u8 ot_mask = 0, oc_mask = 0, uv_mask = 0;
	u32 val, fault_sts = 0;
	int i, rc;

	rc = regmap_field_read(flash_data->r_fields[REG_STATUS1], &val);
	if (rc)
		return rc;

	for (i = 0; i < led->chan_count; i++) {
		chan_id = led->chan_id[i];
		shift = chan_id * 2;

		if (val & BIT(shift))
			fault_sts |= LED_FAULT_SHORT_CIRCUIT;

		chan_mask |= BIT(chan_id);
	}

	rc = regmap_field_read(flash_data->r_fields[REG_STATUS2], &val);
	if (rc)
		return rc;

	if (flash_data->hw_type == QCOM_MVFLASH_3CH) {
		ot_mask = FLASH_STS_3CH_OTST1 |
			  FLASH_STS_3CH_OTST2 |
			  FLASH_STS_3CH_OTST3 |
			  FLASH_STS_3CH_BOB_THM_OVERLOAD;
		oc_mask = FLASH_STS_3CH_BOB_ILIM_S1 |
			  FLASH_STS_3CH_BOB_ILIM_S2 |
			  FLASH_STS_3CH_BCL_IBAT;
		uv_mask = FLASH_STS_3CH_VPH_DROOP;
	} else if (flash_data->hw_type == QCOM_MVFLASH_4CH) {
		ot_mask = FLASH_STS_4CH_OTST2 |
			  FLASH_STS_4CH_OTST1 |
			  FLASH_STS_4CHG_BOB_THM_OVERLOAD;
		oc_mask = FLASH_STS_4CH_BCL_IBAT |
			  FLASH_STS_4CH_BOB_ILIM_S1 |
			  FLASH_STS_4CH_BOB_ILIM_S2;
		uv_mask = FLASH_STS_4CH_VPH_LOW;
	}

	if (val & ot_mask)
		fault_sts |= LED_FAULT_OVER_TEMPERATURE;

	if (val & oc_mask)
		fault_sts |= LED_FAULT_OVER_CURRENT;

	if (val & uv_mask)
		fault_sts |= LED_FAULT_INPUT_VOLTAGE;

	rc = regmap_field_read(flash_data->r_fields[REG_STATUS3], &val);
	if (rc)
		return rc;

	if (flash_data->hw_type == QCOM_MVFLASH_3CH) {
		if (val & chan_mask)
			fault_sts |= LED_FAULT_TIMEOUT;
	} else if (flash_data->hw_type == QCOM_MVFLASH_4CH) {
		for (i = 0; i < led->chan_count; i++) {
			chan_id = led->chan_id[i];
			shift = chan_id * 2;

			if (val & BIT(shift))
				fault_sts |= LED_FAULT_TIMEOUT;
		}
	}

	*fault = fault_sts;
	return 0;
}

static int qcom_flash_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);
	u32 current_ma = brightness * led->max_torch_current_ma / LED_FULL;
	bool enable = !!brightness;
	int rc;

	rc = set_flash_strobe(led, SW_STROBE, false);
	if (rc)
		return rc;

	rc = set_flash_module_en(led, false);
	if (rc)
		return rc;

	rc = update_allowed_flash_current(led, &current_ma, enable);
	if (rc < 0)
		return rc;

	rc = set_flash_current(led, current_ma, TORCH_MODE);
	if (rc)
		return rc;

	/* Disable flash timeout for torch LED */
	rc = set_flash_timeout(led, 0);
	if (rc)
		return rc;

	rc = set_flash_module_en(led, enable);
	if (rc)
		return rc;

	return set_flash_strobe(led, SW_STROBE, enable);
}

static const struct led_flash_ops qcom_flash_ops = {
	.flash_brightness_set = qcom_flash_brightness_set,
	.strobe_set = qcom_flash_strobe_set,
	.strobe_get = qcom_flash_strobe_get,
	.timeout_set = qcom_flash_timeout_set,
	.fault_get = qcom_flash_fault_get,
};

#if IS_ENABLED(CONFIG_V4L2_FLASH_LED_CLASS)
static int qcom_flash_external_strobe_set(struct v4l2_flash *v4l2_flash, bool enable)
{
	struct led_classdev_flash *fled_cdev = v4l2_flash->fled_cdev;
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);
	int rc;

	rc = set_flash_module_en(led, enable);
	if (rc)
		return rc;

	if (enable)
		return set_flash_strobe(led, HW_STROBE, true);
	else
		return set_flash_strobe(led, SW_STROBE, false);
}

static enum led_brightness
qcom_flash_intensity_to_led_brightness(struct v4l2_flash *v4l2_flash, s32 intensity)
{
	struct led_classdev_flash *fled_cdev = v4l2_flash->fled_cdev;
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);
	u32 current_ma = intensity / UA_PER_MA;

	current_ma = min_t(u32, current_ma, led->max_torch_current_ma);
	if (!current_ma)
		return LED_OFF;

	return (current_ma * LED_FULL) / led->max_torch_current_ma;
}

static s32 qcom_flash_brightness_to_led_intensity(struct v4l2_flash *v4l2_flash,
					enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = v4l2_flash->fled_cdev;
	struct qcom_flash_led *led = flcdev_to_qcom_fled(fled_cdev);

	return (brightness * led->max_torch_current_ma * UA_PER_MA) / LED_FULL;
}

static const struct v4l2_flash_ops qcom_v4l2_flash_ops = {
	.external_strobe_set = qcom_flash_external_strobe_set,
	.intensity_to_led_brightness = qcom_flash_intensity_to_led_brightness,
	.led_brightness_to_intensity = qcom_flash_brightness_to_led_intensity,
};

static int
qcom_flash_v4l2_init(struct device *dev, struct qcom_flash_led *led, struct fwnode_handle *fwnode)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	struct v4l2_flash_config v4l2_cfg = { 0 };
	struct led_flash_setting *intensity = &v4l2_cfg.intensity;
	struct v4l2_flash *v4l2_flash;

	if (!(led->flash.led_cdev.flags & LED_DEV_CAP_FLASH))
		return 0;

	intensity->min = intensity->step = TORCH_IRES_UA * led->chan_count;
	intensity->max = led->max_torch_current_ma * UA_PER_MA;
	intensity->val = min_t(u32, intensity->max, TORCH_CURRENT_DEFAULT_UA);

	strscpy(v4l2_cfg.dev_name, led->flash.led_cdev.dev->kobj.name,
					sizeof(v4l2_cfg.dev_name));

	v4l2_cfg.has_external_strobe = true;
	v4l2_cfg.flash_faults = LED_FAULT_INPUT_VOLTAGE |
				LED_FAULT_OVER_CURRENT |
				LED_FAULT_SHORT_CIRCUIT |
				LED_FAULT_OVER_TEMPERATURE |
				LED_FAULT_TIMEOUT;

	v4l2_flash = v4l2_flash_init(dev, fwnode, &led->flash, &qcom_v4l2_flash_ops, &v4l2_cfg);
	if (IS_ERR(v4l2_flash))
		return PTR_ERR(v4l2_flash);

	flash_data->v4l2_flash[flash_data->leds_count] = v4l2_flash;
	return 0;
}
# else
static int
qcom_flash_v4l2_init(struct device *dev, struct qcom_flash_led *led, struct fwnode_handle *fwnode)
{
	return 0;
}
#endif

static int qcom_flash_register_led_device(struct device *dev,
		struct fwnode_handle *node, struct qcom_flash_led *led)
{
	struct qcom_flash_data *flash_data = led->flash_data;
	struct led_init_data init_data;
	struct led_classdev_flash *flash = &led->flash;
	struct led_flash_setting *brightness, *timeout;
	u32 current_ua, timeout_us;
	u32 channels[4];
	int i, rc, count;
	u8 torch_clamp;

	count = fwnode_property_count_u32(node, "led-sources");
	if (count <= 0) {
		dev_err(dev, "No led-sources specified\n");
		return -ENODEV;
	}

	if (count > flash_data->max_channels) {
		dev_err(dev, "led-sources count %u exceeds maximum channel count %u\n",
				count, flash_data->max_channels);
		return -EINVAL;
	}

	rc = fwnode_property_read_u32_array(node, "led-sources", channels, count);
	if (rc < 0) {
		dev_err(dev, "Failed to read led-sources property, rc=%d\n", rc);
		return rc;
	}

	led->chan_count = count;
	led->chan_id = devm_kcalloc(dev, count, sizeof(u8), GFP_KERNEL);
	if (!led->chan_id)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		if ((channels[i] == 0) || (channels[i] > flash_data->max_channels)) {
			dev_err(dev, "led-source out of HW support range [1-%u]\n",
					flash_data->max_channels);
			return -EINVAL;
		}

		/* Make chan_id indexing from 0 */
		led->chan_id[i] = channels[i] - 1;
	}

	rc = fwnode_property_read_u32(node, "led-max-microamp", &current_ua);
	if (rc < 0) {
		dev_err(dev, "Failed to read led-max-microamp property, rc=%d\n", rc);
		return rc;
	}

	if (current_ua == 0) {
		dev_err(dev, "led-max-microamp shouldn't be 0\n");
		return -EINVAL;
	}

	current_ua = min_t(u32, current_ua, TORCH_CURRENT_MAX_UA * led->chan_count);
	led->max_torch_current_ma = current_ua / UA_PER_MA;

	torch_clamp = (current_ua / led->chan_count) / TORCH_IRES_UA;
	if (torch_clamp != 0)
		torch_clamp--;

	flash_data->torch_clamp = max_t(u8, flash_data->torch_clamp, torch_clamp);

	if (fwnode_property_present(node, "flash-max-microamp")) {
		flash->led_cdev.flags |= LED_DEV_CAP_FLASH;

		rc = fwnode_property_read_u32(node, "flash-max-microamp", &current_ua);
		if (rc < 0) {
			dev_err(dev, "Failed to read flash-max-microamp property, rc=%d\n",
					rc);
			return rc;
		}

		current_ua = min_t(u32, current_ua, FLASH_CURRENT_MAX_UA * led->chan_count);
		current_ua = min_t(u32, current_ua, FLASH_TOTAL_CURRENT_MAX_UA);

		/* Initialize flash class LED device brightness settings */
		brightness = &flash->brightness;
		brightness->min = brightness->step = FLASH_IRES_UA * led->chan_count;
		brightness->max = current_ua;
		brightness->val = min_t(u32, current_ua, FLASH_CURRENT_DEFAULT_UA);

		led->max_flash_current_ma = current_ua / UA_PER_MA;
		led->flash_current_ma = brightness->val / UA_PER_MA;

		rc = fwnode_property_read_u32(node, "flash-max-timeout-us", &timeout_us);
		if (rc < 0) {
			dev_err(dev, "Failed to read flash-max-timeout-us property, rc=%d\n",
					rc);
			return rc;
		}

		timeout_us = min_t(u32, timeout_us, FLASH_TIMEOUT_MAX_US);

		/* Initialize flash class LED device timeout settings */
		timeout = &flash->timeout;
		timeout->min = timeout->step = FLASH_TIMEOUT_STEP_US;
		timeout->val = timeout->max = timeout_us;

		led->max_timeout_ms = led->flash_timeout_ms = timeout_us / USEC_PER_MSEC;

		flash->ops = &qcom_flash_ops;
	}

	flash->led_cdev.brightness_set_blocking = qcom_flash_led_brightness_set;

	init_data.fwnode = node;
	init_data.devicename = NULL;
	init_data.default_label = NULL;
	init_data.devname_mandatory = false;

	rc = devm_led_classdev_flash_register_ext(dev, flash, &init_data);
	if (rc < 0) {
		dev_err(dev, "Register flash LED classdev failed, rc=%d\n", rc);
		return rc;
	}

	return qcom_flash_v4l2_init(dev, led, node);
}

static int qcom_flash_led_probe(struct platform_device *pdev)
{
	struct qcom_flash_data *flash_data;
	struct qcom_flash_led *led;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	struct reg_field *regs;
	int count, i, rc;
	u32 val, reg_base;

	flash_data = devm_kzalloc(dev, sizeof(*flash_data), GFP_KERNEL);
	if (!flash_data)
		return -ENOMEM;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "Failed to get parent regmap\n");
		return -EINVAL;
	}

	rc = fwnode_property_read_u32(dev->fwnode, "reg", &reg_base);
	if (rc < 0) {
		dev_err(dev, "Failed to get register base address, rc=%d\n", rc);
		return rc;
	}

	rc = regmap_read(regmap, reg_base + FLASH_TYPE_REG, &val);
	if (rc < 0) {
		dev_err(dev, "Read flash LED module type failed, rc=%d\n", rc);
		return rc;
	}

	if (val != FLASH_TYPE_VAL) {
		dev_err(dev, "type %#x is not a flash LED module\n", val);
		return -ENODEV;
	}

	rc = regmap_read(regmap, reg_base + FLASH_SUBTYPE_REG, &val);
	if (rc < 0) {
		dev_err(dev, "Read flash LED module subtype failed, rc=%d\n", rc);
		return rc;
	}

	if (val == FLASH_SUBTYPE_3CH_PM8150_VAL) {
		flash_data->hw_type = QCOM_MVFLASH_3CH;
		flash_data->max_channels = 3;
		regs = devm_kmemdup(dev, mvflash_3ch_regs, sizeof(mvflash_3ch_regs),
				    GFP_KERNEL);
		if (!regs)
			return -ENOMEM;
	} else if (val == FLASH_SUBTYPE_3CH_PMI8998_VAL) {
		flash_data->hw_type = QCOM_MVFLASH_3CH;
		flash_data->max_channels = 3;
		regs = devm_kmemdup(dev, mvflash_3ch_pmi8998_regs,
				    sizeof(mvflash_3ch_pmi8998_regs), GFP_KERNEL);
		if (!regs)
			return -ENOMEM;
	} else if (val == FLASH_SUBTYPE_4CH_VAL) {
		flash_data->hw_type = QCOM_MVFLASH_4CH;
		flash_data->max_channels = 4;
		regs = devm_kmemdup(dev, mvflash_4ch_regs, sizeof(mvflash_4ch_regs),
				    GFP_KERNEL);
		if (!regs)
			return -ENOMEM;

		rc = regmap_read(regmap, reg_base + FLASH_REVISION_REG, &val);
		if (rc < 0) {
			dev_err(dev, "Failed to read flash LED module revision, rc=%d\n", rc);
			return rc;
		}

		flash_data->revision = val;
	} else {
		dev_err(dev, "flash LED subtype %#x is not yet supported\n", val);
		return -ENODEV;
	}

	for (i = 0; i < REG_MAX_COUNT; i++)
		regs[i].reg += reg_base;

	rc = devm_regmap_field_bulk_alloc(dev, regmap, flash_data->r_fields, regs, REG_MAX_COUNT);
	if (rc < 0) {
		dev_err(dev, "Failed to allocate regmap field, rc=%d\n", rc);
		return rc;
	}
	devm_kfree(dev, regs); /* devm_regmap_field_bulk_alloc() makes copies */

	platform_set_drvdata(pdev, flash_data);
	mutex_init(&flash_data->lock);

	count = device_get_child_node_count(dev);
	if (count == 0 || count > flash_data->max_channels) {
		dev_err(dev, "No child or child count exceeds %d\n", flash_data->max_channels);
		return -EINVAL;
	}

	flash_data->v4l2_flash = devm_kcalloc(dev, count,
			sizeof(*flash_data->v4l2_flash), GFP_KERNEL);
	if (!flash_data->v4l2_flash)
		return -ENOMEM;

	device_for_each_child_node_scoped(dev, child) {
		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led) {
			rc = -ENOMEM;
			goto release;
		}

		led->flash_data = flash_data;
		rc = qcom_flash_register_led_device(dev, child, led);
		if (rc < 0)
			goto release;

		flash_data->leds_count++;
	}

	return regmap_field_write(flash_data->r_fields[REG_TORCH_CLAMP], flash_data->torch_clamp);
release:
	while (flash_data->v4l2_flash[flash_data->leds_count] && flash_data->leds_count)
		v4l2_flash_release(flash_data->v4l2_flash[flash_data->leds_count--]);
	return rc;
}

static void qcom_flash_led_remove(struct platform_device *pdev)
{
	struct qcom_flash_data *flash_data = platform_get_drvdata(pdev);

	while (flash_data->v4l2_flash[flash_data->leds_count] && flash_data->leds_count)
		v4l2_flash_release(flash_data->v4l2_flash[flash_data->leds_count--]);

	mutex_destroy(&flash_data->lock);
}

static const struct of_device_id qcom_flash_led_match_table[] = {
	{ .compatible = "qcom,spmi-flash-led" },
	{ }
};

MODULE_DEVICE_TABLE(of, qcom_flash_led_match_table);
static struct platform_driver qcom_flash_led_driver = {
	.driver = {
		.name = "leds-qcom-flash",
		.of_match_table = qcom_flash_led_match_table,
	},
	.probe = qcom_flash_led_probe,
	.remove = qcom_flash_led_remove,
};

module_platform_driver(qcom_flash_led_driver);

MODULE_DESCRIPTION("QCOM Flash LED driver");
MODULE_LICENSE("GPL");
