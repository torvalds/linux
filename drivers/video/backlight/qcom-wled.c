// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015, Sony Mobile Communications, AB.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

/* From DT binding */
#define WLED_MAX_STRINGS				4
#define MOD_A						0
#define MOD_B						1

#define WLED_DEFAULT_BRIGHTNESS				2048
#define WLED_SOFT_START_DLY_US				10000
#define WLED3_SINK_REG_BRIGHT_MAX			0xFFF
#define WLED5_SINK_REG_BRIGHT_MAX_12B			0xFFF
#define WLED5_SINK_REG_BRIGHT_MAX_15B			0x7FFF

/* WLED3/WLED4 control registers */
#define WLED3_CTRL_REG_FAULT_STATUS			0x08
#define  WLED3_CTRL_REG_ILIM_FAULT_BIT			BIT(0)
#define  WLED3_CTRL_REG_OVP_FAULT_BIT			BIT(1)
#define  WLED4_CTRL_REG_SC_FAULT_BIT			BIT(2)
#define  WLED5_CTRL_REG_OVP_PRE_ALARM_BIT		BIT(4)

#define WLED3_CTRL_REG_INT_RT_STS			0x10
#define  WLED3_CTRL_REG_OVP_FAULT_STATUS		BIT(1)

#define WLED3_CTRL_REG_MOD_EN				0x46
#define  WLED3_CTRL_REG_MOD_EN_MASK			BIT(7)
#define  WLED3_CTRL_REG_MOD_EN_SHIFT			7

#define WLED3_CTRL_REG_FEEDBACK_CONTROL			0x48

#define WLED3_CTRL_REG_FREQ				0x4c
#define  WLED3_CTRL_REG_FREQ_MASK			GENMASK(3, 0)

#define WLED3_CTRL_REG_OVP				0x4d
#define  WLED3_CTRL_REG_OVP_MASK			GENMASK(1, 0)
#define  WLED5_CTRL_REG_OVP_MASK			GENMASK(3, 0)

#define WLED3_CTRL_REG_ILIMIT				0x4e
#define  WLED3_CTRL_REG_ILIMIT_MASK			GENMASK(2, 0)

/* WLED3/WLED4 sink registers */
#define WLED3_SINK_REG_SYNC				0x47
#define  WLED3_SINK_REG_SYNC_CLEAR			0x00

#define WLED3_SINK_REG_CURR_SINK			0x4f
#define  WLED3_SINK_REG_CURR_SINK_MASK			GENMASK(7, 5)
#define  WLED3_SINK_REG_CURR_SINK_SHFT			5

/* WLED3 specific per-'string' registers below */
#define WLED3_SINK_REG_BRIGHT(n)			(0x40 + n)

#define WLED3_SINK_REG_STR_MOD_EN(n)			(0x60 + (n * 0x10))
#define  WLED3_SINK_REG_STR_MOD_MASK			BIT(7)

#define WLED3_SINK_REG_STR_FULL_SCALE_CURR(n)		(0x62 + (n * 0x10))
#define  WLED3_SINK_REG_STR_FULL_SCALE_CURR_MASK	GENMASK(4, 0)

#define WLED3_SINK_REG_STR_MOD_SRC(n)			(0x63 + (n * 0x10))
#define  WLED3_SINK_REG_STR_MOD_SRC_MASK		BIT(0)
#define  WLED3_SINK_REG_STR_MOD_SRC_INT			0x00
#define  WLED3_SINK_REG_STR_MOD_SRC_EXT			0x01

#define WLED3_SINK_REG_STR_CABC(n)			(0x66 + (n * 0x10))
#define  WLED3_SINK_REG_STR_CABC_MASK			BIT(7)

/* WLED4 specific control registers */
#define WLED4_CTRL_REG_SHORT_PROTECT			0x5e
#define  WLED4_CTRL_REG_SHORT_EN_MASK			BIT(7)

#define WLED4_CTRL_REG_SEC_ACCESS			0xd0
#define  WLED4_CTRL_REG_SEC_UNLOCK			0xa5

#define WLED4_CTRL_REG_TEST1				0xe2
#define  WLED4_CTRL_REG_TEST1_EXT_FET_DTEST2		0x09

/* WLED4 specific sink registers */
#define WLED4_SINK_REG_CURR_SINK			0x46
#define  WLED4_SINK_REG_CURR_SINK_MASK			GENMASK(7, 4)
#define  WLED4_SINK_REG_CURR_SINK_SHFT			4

/* WLED4 specific per-'string' registers below */
#define WLED4_SINK_REG_STR_MOD_EN(n)			(0x50 + (n * 0x10))
#define  WLED4_SINK_REG_STR_MOD_MASK			BIT(7)

#define WLED4_SINK_REG_STR_FULL_SCALE_CURR(n)		(0x52 + (n * 0x10))
#define  WLED4_SINK_REG_STR_FULL_SCALE_CURR_MASK	GENMASK(3, 0)

#define WLED4_SINK_REG_STR_MOD_SRC(n)			(0x53 + (n * 0x10))
#define  WLED4_SINK_REG_STR_MOD_SRC_MASK		BIT(0)
#define  WLED4_SINK_REG_STR_MOD_SRC_INT			0x00
#define  WLED4_SINK_REG_STR_MOD_SRC_EXT			0x01

#define WLED4_SINK_REG_STR_CABC(n)			(0x56 + (n * 0x10))
#define  WLED4_SINK_REG_STR_CABC_MASK			BIT(7)

#define WLED4_SINK_REG_BRIGHT(n)			(0x57 + (n * 0x10))

/* WLED5 specific control registers */
#define WLED5_CTRL_REG_OVP_INT_CTL			0x5f
#define  WLED5_CTRL_REG_OVP_INT_TIMER_MASK		GENMASK(2, 0)

/* WLED5 specific sink registers */
#define WLED5_SINK_REG_MOD_A_EN				0x50
#define WLED5_SINK_REG_MOD_B_EN				0x60
#define  WLED5_SINK_REG_MOD_EN_MASK			BIT(7)

#define WLED5_SINK_REG_MOD_A_SRC_SEL			0x51
#define WLED5_SINK_REG_MOD_B_SRC_SEL			0x61
#define  WLED5_SINK_REG_MOD_SRC_SEL_HIGH		0
#define  WLED5_SINK_REG_MOD_SRC_SEL_EXT			0x03
#define  WLED5_SINK_REG_MOD_SRC_SEL_MASK		GENMASK(1, 0)

#define WLED5_SINK_REG_MOD_A_BRIGHTNESS_WIDTH_SEL	0x52
#define WLED5_SINK_REG_MOD_B_BRIGHTNESS_WIDTH_SEL	0x62
#define  WLED5_SINK_REG_BRIGHTNESS_WIDTH_12B		0
#define  WLED5_SINK_REG_BRIGHTNESS_WIDTH_15B		1

#define WLED5_SINK_REG_MOD_A_BRIGHTNESS_LSB		0x53
#define WLED5_SINK_REG_MOD_A_BRIGHTNESS_MSB		0x54
#define WLED5_SINK_REG_MOD_B_BRIGHTNESS_LSB		0x63
#define WLED5_SINK_REG_MOD_B_BRIGHTNESS_MSB		0x64

#define WLED5_SINK_REG_MOD_SYNC_BIT			0x65
#define  WLED5_SINK_REG_SYNC_MOD_A_BIT			BIT(0)
#define  WLED5_SINK_REG_SYNC_MOD_B_BIT			BIT(1)
#define  WLED5_SINK_REG_SYNC_MASK			GENMASK(1, 0)

/* WLED5 specific per-'string' registers below */
#define WLED5_SINK_REG_STR_FULL_SCALE_CURR(n)		(0x72 + (n * 0x10))

#define WLED5_SINK_REG_STR_SRC_SEL(n)			(0x73 + (n * 0x10))
#define  WLED5_SINK_REG_SRC_SEL_MOD_A			0
#define  WLED5_SINK_REG_SRC_SEL_MOD_B			1
#define  WLED5_SINK_REG_SRC_SEL_MASK			GENMASK(1, 0)

struct wled_var_cfg {
	const u32 *values;
	u32 (*fn)(u32);
	int size;
};

struct wled_u32_opts {
	const char *name;
	u32 *val_ptr;
	const struct wled_var_cfg *cfg;
};

struct wled_bool_opts {
	const char *name;
	bool *val_ptr;
};

struct wled_config {
	u32 boost_i_limit;
	u32 ovp;
	u32 switch_freq;
	u32 num_strings;
	u32 string_i_limit;
	u32 enabled_strings[WLED_MAX_STRINGS];
	u32 mod_sel;
	u32 cabc_sel;
	bool cs_out_en;
	bool ext_gen;
	bool cabc;
	bool external_pfet;
	bool auto_detection_enabled;
};

struct wled {
	const char *name;
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;	/* Lock to avoid race from thread irq handler */
	ktime_t last_short_event;
	ktime_t start_ovp_fault_time;
	u16 ctrl_addr;
	u16 sink_addr;
	u16 max_string_count;
	u16 auto_detection_ovp_count;
	u32 brightness;
	u32 max_brightness;
	u32 short_count;
	u32 auto_detect_count;
	u32 version;
	bool disabled_by_short;
	bool has_short_detect;
	bool cabc_disabled;
	int short_irq;
	int ovp_irq;

	struct wled_config cfg;
	struct delayed_work ovp_work;

	/* Configures the brightness. Applicable for wled3, wled4 and wled5 */
	int (*wled_set_brightness)(struct wled *wled, u16 brightness);

	/* Configures the cabc register. Applicable for wled4 and wled5 */
	int (*wled_cabc_config)(struct wled *wled, bool enable);

	/*
	 * Toggles the sync bit for the brightness update to take place.
	 * Applicable for WLED3, WLED4 and WLED5.
	 */
	int (*wled_sync_toggle)(struct wled *wled);

	/*
	 * Time to wait before checking the OVP status after wled module enable.
	 * Applicable for WLED4 and WLED5.
	 */
	int (*wled_ovp_delay)(struct wled *wled);

	/*
	 * Determines if the auto string detection is required.
	 * Applicable for WLED4 and WLED5
	 */
	bool (*wled_auto_detection_required)(struct wled *wled);
};

static int wled3_set_brightness(struct wled *wled, u16 brightness)
{
	int rc, i;
	u8 v[2];

	v[0] = brightness & 0xff;
	v[1] = (brightness >> 8) & 0xf;

	for (i = 0;  i < wled->cfg.num_strings; ++i) {
		rc = regmap_bulk_write(wled->regmap, wled->ctrl_addr +
				       WLED3_SINK_REG_BRIGHT(i), v, 2);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wled4_set_brightness(struct wled *wled, u16 brightness)
{
	int rc, i;
	u16 low_limit = wled->max_brightness * 4 / 1000;
	u8 v[2];

	/* WLED4's lower limit of operation is 0.4% */
	if (brightness > 0 && brightness < low_limit)
		brightness = low_limit;

	v[0] = brightness & 0xff;
	v[1] = (brightness >> 8) & 0xf;

	for (i = 0;  i < wled->cfg.num_strings; ++i) {
		rc = regmap_bulk_write(wled->regmap, wled->sink_addr +
				       WLED4_SINK_REG_BRIGHT(i), v, 2);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wled5_set_brightness(struct wled *wled, u16 brightness)
{
	int rc, offset;
	u16 low_limit = wled->max_brightness * 1 / 1000;
	u8 v[2];

	/* WLED5's lower limit is 0.1% */
	if (brightness < low_limit)
		brightness = low_limit;

	v[0] = brightness & 0xff;
	v[1] = (brightness >> 8) & 0x7f;

	offset = (wled->cfg.mod_sel == MOD_A) ?
		  WLED5_SINK_REG_MOD_A_BRIGHTNESS_LSB :
		  WLED5_SINK_REG_MOD_B_BRIGHTNESS_LSB;

	rc = regmap_bulk_write(wled->regmap, wled->sink_addr + offset,
			       v, 2);
	return rc;
}

static void wled_ovp_work(struct work_struct *work)
{
	struct wled *wled = container_of(work,
					 struct wled, ovp_work.work);
	enable_irq(wled->ovp_irq);
}

static int wled_module_enable(struct wled *wled, int val)
{
	int rc;

	if (wled->disabled_by_short)
		return -ENXIO;

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
				WLED3_CTRL_REG_MOD_EN,
				WLED3_CTRL_REG_MOD_EN_MASK,
				val << WLED3_CTRL_REG_MOD_EN_SHIFT);
	if (rc < 0)
		return rc;

	if (wled->ovp_irq > 0) {
		if (val) {
			/*
			 * The hardware generates a storm of spurious OVP
			 * interrupts during soft start operations. So defer
			 * enabling the IRQ for 10ms to ensure that the
			 * soft start is complete.
			 */
			schedule_delayed_work(&wled->ovp_work, HZ / 100);
		} else {
			if (!cancel_delayed_work_sync(&wled->ovp_work))
				disable_irq(wled->ovp_irq);
		}
	}

	return 0;
}

static int wled3_sync_toggle(struct wled *wled)
{
	int rc;
	unsigned int mask = GENMASK(wled->max_string_count - 1, 0);

	rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED3_SINK_REG_SYNC,
				mask, mask);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED3_SINK_REG_SYNC,
				mask, WLED3_SINK_REG_SYNC_CLEAR);

	return rc;
}

static int wled5_mod_sync_toggle(struct wled *wled)
{
	int rc;
	u8 val;

	val = (wled->cfg.mod_sel == MOD_A) ? WLED5_SINK_REG_SYNC_MOD_A_BIT :
					     WLED5_SINK_REG_SYNC_MOD_B_BIT;
	rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED5_SINK_REG_MOD_SYNC_BIT,
				WLED5_SINK_REG_SYNC_MASK, val);
	if (rc < 0)
		return rc;

	return regmap_update_bits(wled->regmap,
				  wled->sink_addr + WLED5_SINK_REG_MOD_SYNC_BIT,
				  WLED5_SINK_REG_SYNC_MASK, 0);
}

static int wled_ovp_fault_status(struct wled *wled, bool *fault_set)
{
	int rc;
	u32 int_rt_sts, fault_sts;

	*fault_set = false;
	rc = regmap_read(wled->regmap,
			wled->ctrl_addr + WLED3_CTRL_REG_INT_RT_STS,
			&int_rt_sts);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to read INT_RT_STS rc=%d\n", rc);
		return rc;
	}

	rc = regmap_read(wled->regmap,
			wled->ctrl_addr + WLED3_CTRL_REG_FAULT_STATUS,
			&fault_sts);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to read FAULT_STATUS rc=%d\n", rc);
		return rc;
	}

	if (int_rt_sts & WLED3_CTRL_REG_OVP_FAULT_STATUS)
		*fault_set = true;

	if (wled->version == 4 && (fault_sts & WLED3_CTRL_REG_OVP_FAULT_BIT))
		*fault_set = true;

	if (wled->version == 5 && (fault_sts & (WLED3_CTRL_REG_OVP_FAULT_BIT |
				   WLED5_CTRL_REG_OVP_PRE_ALARM_BIT)))
		*fault_set = true;

	if (*fault_set)
		dev_dbg(wled->dev, "WLED OVP fault detected, int_rt_sts=0x%x fault_sts=0x%x\n",
			int_rt_sts, fault_sts);

	return rc;
}

static int wled4_ovp_delay(struct wled *wled)
{
	return WLED_SOFT_START_DLY_US;
}

static int wled5_ovp_delay(struct wled *wled)
{
	int rc, delay_us;
	u32 val;
	u8 ovp_timer_ms[8] = {1, 2, 4, 8, 12, 16, 20, 24};

	/* For WLED5, get the delay based on OVP timer */
	rc = regmap_read(wled->regmap, wled->ctrl_addr +
			 WLED5_CTRL_REG_OVP_INT_CTL, &val);
	if (rc < 0)
		delay_us =
		ovp_timer_ms[val & WLED5_CTRL_REG_OVP_INT_TIMER_MASK] * 1000;
	else
		delay_us = 2 * WLED_SOFT_START_DLY_US;

	dev_dbg(wled->dev, "delay_time_us: %d\n", delay_us);

	return delay_us;
}

static int wled_update_status(struct backlight_device *bl)
{
	struct wled *wled = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int rc = 0;

	mutex_lock(&wled->lock);
	if (brightness) {
		rc = wled->wled_set_brightness(wled, brightness);
		if (rc < 0) {
			dev_err(wled->dev, "wled failed to set brightness rc:%d\n",
				rc);
			goto unlock_mutex;
		}

		if (wled->version < 5) {
			rc = wled->wled_sync_toggle(wled);
			if (rc < 0) {
				dev_err(wled->dev, "wled sync failed rc:%d\n", rc);
				goto unlock_mutex;
			}
		} else {
			/*
			 * For WLED5 toggling the MOD_SYNC_BIT updates the
			 * brightness
			 */
			rc = wled5_mod_sync_toggle(wled);
			if (rc < 0) {
				dev_err(wled->dev, "wled mod sync failed rc:%d\n",
					rc);
				goto unlock_mutex;
			}
		}
	}

	if (!!brightness != !!wled->brightness) {
		rc = wled_module_enable(wled, !!brightness);
		if (rc < 0) {
			dev_err(wled->dev, "wled enable failed rc:%d\n", rc);
			goto unlock_mutex;
		}
	}

	wled->brightness = brightness;

unlock_mutex:
	mutex_unlock(&wled->lock);

	return rc;
}

static int wled4_cabc_config(struct wled *wled, bool enable)
{
	int i, j, rc;
	u8 val;

	for (i = 0; i < wled->cfg.num_strings; i++) {
		j = wled->cfg.enabled_strings[i];

		val = enable ? WLED4_SINK_REG_STR_CABC_MASK : 0;
		rc = regmap_update_bits(wled->regmap, wled->sink_addr +
					WLED4_SINK_REG_STR_CABC(j),
					WLED4_SINK_REG_STR_CABC_MASK, val);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int wled5_cabc_config(struct wled *wled, bool enable)
{
	int rc, offset;
	u8 reg;

	if (wled->cabc_disabled)
		return 0;

	reg = enable ? wled->cfg.cabc_sel : 0;
	offset = (wled->cfg.mod_sel == MOD_A) ? WLED5_SINK_REG_MOD_A_SRC_SEL :
						WLED5_SINK_REG_MOD_B_SRC_SEL;

	rc = regmap_update_bits(wled->regmap, wled->sink_addr + offset,
				WLED5_SINK_REG_MOD_SRC_SEL_MASK, reg);
	if (rc < 0) {
		pr_err("Error in configuring CABC rc=%d\n", rc);
		return rc;
	}

	if (!wled->cfg.cabc_sel)
		wled->cabc_disabled = true;

	return 0;
}

#define WLED_SHORT_DLY_MS			20
#define WLED_SHORT_CNT_MAX			5
#define WLED_SHORT_RESET_CNT_DLY_US		USEC_PER_SEC

static irqreturn_t wled_short_irq_handler(int irq, void *_wled)
{
	struct wled *wled = _wled;
	int rc;
	s64 elapsed_time;

	wled->short_count++;
	mutex_lock(&wled->lock);
	rc = wled_module_enable(wled, false);
	if (rc < 0) {
		dev_err(wled->dev, "wled disable failed rc:%d\n", rc);
		goto unlock_mutex;
	}

	elapsed_time = ktime_us_delta(ktime_get(),
				      wled->last_short_event);
	if (elapsed_time > WLED_SHORT_RESET_CNT_DLY_US)
		wled->short_count = 1;

	if (wled->short_count > WLED_SHORT_CNT_MAX) {
		dev_err(wled->dev, "Short triggered %d times, disabling WLED forever!\n",
			wled->short_count);
		wled->disabled_by_short = true;
		goto unlock_mutex;
	}

	wled->last_short_event = ktime_get();

	msleep(WLED_SHORT_DLY_MS);
	rc = wled_module_enable(wled, true);
	if (rc < 0)
		dev_err(wled->dev, "wled enable failed rc:%d\n", rc);

unlock_mutex:
	mutex_unlock(&wled->lock);

	return IRQ_HANDLED;
}

#define AUTO_DETECT_BRIGHTNESS		200

static void wled_auto_string_detection(struct wled *wled)
{
	int rc = 0, i, delay_time_us;
	u32 sink_config = 0;
	u8 sink_test = 0, sink_valid = 0, val;
	bool fault_set;

	/* Read configured sink configuration */
	rc = regmap_read(wled->regmap, wled->sink_addr +
			 WLED4_SINK_REG_CURR_SINK, &sink_config);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to read SINK configuration rc=%d\n",
			rc);
		goto failed_detect;
	}

	/* Disable the module before starting detection */
	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_MOD_EN,
				WLED3_CTRL_REG_MOD_EN_MASK, 0);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to disable WLED module rc=%d\n", rc);
		goto failed_detect;
	}

	/* Set low brightness across all sinks */
	rc = wled4_set_brightness(wled, AUTO_DETECT_BRIGHTNESS);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to set brightness for auto detection rc=%d\n",
			rc);
		goto failed_detect;
	}

	if (wled->cfg.cabc) {
		rc = wled->wled_cabc_config(wled, false);
		if (rc < 0)
			goto failed_detect;
	}

	/* Disable all sinks */
	rc = regmap_write(wled->regmap,
			  wled->sink_addr + WLED4_SINK_REG_CURR_SINK, 0);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to disable all sinks rc=%d\n", rc);
		goto failed_detect;
	}

	/* Iterate through the strings one by one */
	for (i = 0; i < wled->cfg.num_strings; i++) {
		sink_test = BIT((WLED4_SINK_REG_CURR_SINK_SHFT + i));

		/* Enable feedback control */
		rc = regmap_write(wled->regmap, wled->ctrl_addr +
				  WLED3_CTRL_REG_FEEDBACK_CONTROL, i + 1);
		if (rc < 0) {
			dev_err(wled->dev, "Failed to enable feedback for SINK %d rc = %d\n",
				i + 1, rc);
			goto failed_detect;
		}

		/* Enable the sink */
		rc = regmap_write(wled->regmap, wled->sink_addr +
				  WLED4_SINK_REG_CURR_SINK, sink_test);
		if (rc < 0) {
			dev_err(wled->dev, "Failed to configure SINK %d rc=%d\n",
				i + 1, rc);
			goto failed_detect;
		}

		/* Enable the module */
		rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
					WLED3_CTRL_REG_MOD_EN,
					WLED3_CTRL_REG_MOD_EN_MASK,
					WLED3_CTRL_REG_MOD_EN_MASK);
		if (rc < 0) {
			dev_err(wled->dev, "Failed to enable WLED module rc=%d\n",
				rc);
			goto failed_detect;
		}

		delay_time_us = wled->wled_ovp_delay(wled);
		usleep_range(delay_time_us, delay_time_us + 1000);

		rc = wled_ovp_fault_status(wled, &fault_set);
		if (rc < 0) {
			dev_err(wled->dev, "Error in getting OVP fault_sts, rc=%d\n",
				rc);
			goto failed_detect;
		}

		if (fault_set)
			dev_dbg(wled->dev, "WLED OVP fault detected with SINK %d\n",
				i + 1);
		else
			sink_valid |= sink_test;

		/* Disable the module */
		rc = regmap_update_bits(wled->regmap,
					wled->ctrl_addr + WLED3_CTRL_REG_MOD_EN,
					WLED3_CTRL_REG_MOD_EN_MASK, 0);
		if (rc < 0) {
			dev_err(wled->dev, "Failed to disable WLED module rc=%d\n",
				rc);
			goto failed_detect;
		}
	}

	if (!sink_valid) {
		dev_err(wled->dev, "No valid WLED sinks found\n");
		wled->disabled_by_short = true;
		goto failed_detect;
	}

	if (sink_valid != sink_config) {
		dev_warn(wled->dev, "%x is not a valid sink configuration - using %x instead\n",
			 sink_config, sink_valid);
		sink_config = sink_valid;
	}

	/* Write the new sink configuration */
	rc = regmap_write(wled->regmap,
			  wled->sink_addr + WLED4_SINK_REG_CURR_SINK,
			  sink_config);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to reconfigure the default sink rc=%d\n",
			rc);
		goto failed_detect;
	}

	/* Enable valid sinks */
	if (wled->version == 4) {
		for (i = 0; i < wled->cfg.num_strings; i++) {
			if (sink_config &
			    BIT(WLED4_SINK_REG_CURR_SINK_SHFT + i))
				val = WLED4_SINK_REG_STR_MOD_MASK;
			else
				/* Disable modulator_en for unused sink */
				val = 0;

			rc = regmap_write(wled->regmap, wled->sink_addr +
					  WLED4_SINK_REG_STR_MOD_EN(i), val);
			if (rc < 0) {
				dev_err(wled->dev, "Failed to configure MODULATOR_EN rc=%d\n",
					rc);
				goto failed_detect;
			}
		}
	}

	/* Enable CABC */
	rc = wled->wled_cabc_config(wled, true);
	if (rc < 0)
		goto failed_detect;

	/* Restore the feedback setting */
	rc = regmap_write(wled->regmap,
			  wled->ctrl_addr + WLED3_CTRL_REG_FEEDBACK_CONTROL, 0);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to restore feedback setting rc=%d\n",
			rc);
		goto failed_detect;
	}

	/* Restore brightness */
	rc = wled4_set_brightness(wled, wled->brightness);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to set brightness after auto detection rc=%d\n",
			rc);
		goto failed_detect;
	}

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_MOD_EN,
				WLED3_CTRL_REG_MOD_EN_MASK,
				WLED3_CTRL_REG_MOD_EN_MASK);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to enable WLED module rc=%d\n", rc);
		goto failed_detect;
	}

failed_detect:
	return;
}

#define WLED_AUTO_DETECT_OVP_COUNT		5
#define WLED_AUTO_DETECT_CNT_DLY_US		USEC_PER_SEC

static bool wled4_auto_detection_required(struct wled *wled)
{
	s64 elapsed_time_us;

	if (!wled->cfg.auto_detection_enabled)
		return false;

	/*
	 * Check if the OVP fault was an occasional one
	 * or if it's firing continuously, the latter qualifies
	 * for an auto-detection check.
	 */
	if (!wled->auto_detection_ovp_count) {
		wled->start_ovp_fault_time = ktime_get();
		wled->auto_detection_ovp_count++;
	} else {
		elapsed_time_us = ktime_us_delta(ktime_get(),
						 wled->start_ovp_fault_time);
		if (elapsed_time_us > WLED_AUTO_DETECT_CNT_DLY_US)
			wled->auto_detection_ovp_count = 0;
		else
			wled->auto_detection_ovp_count++;

		if (wled->auto_detection_ovp_count >=
				WLED_AUTO_DETECT_OVP_COUNT) {
			wled->auto_detection_ovp_count = 0;
			return true;
		}
	}

	return false;
}

static bool wled5_auto_detection_required(struct wled *wled)
{
	if (!wled->cfg.auto_detection_enabled)
		return false;

	/*
	 * Unlike WLED4, WLED5 has OVP fault density interrupt configuration
	 * i.e. to count the number of OVP alarms for a certain duration before
	 * triggering OVP fault interrupt. By default, number of OVP fault
	 * events counted before an interrupt is fired is 32 and the time
	 * interval is 12 ms. If we see one OVP fault interrupt, then that
	 * should qualify for a real OVP fault condition to run auto detection
	 * algorithm.
	 */
	return true;
}

static int wled_auto_detection_at_init(struct wled *wled)
{
	int rc;
	bool fault_set;

	if (!wled->cfg.auto_detection_enabled)
		return 0;

	rc = wled_ovp_fault_status(wled, &fault_set);
	if (rc < 0) {
		dev_err(wled->dev, "Error in getting OVP fault_sts, rc=%d\n",
			rc);
		return rc;
	}

	if (fault_set) {
		mutex_lock(&wled->lock);
		wled_auto_string_detection(wled);
		mutex_unlock(&wled->lock);
	}

	return rc;
}

static irqreturn_t wled_ovp_irq_handler(int irq, void *_wled)
{
	struct wled *wled = _wled;
	int rc;
	u32 int_sts, fault_sts;

	rc = regmap_read(wled->regmap,
			 wled->ctrl_addr + WLED3_CTRL_REG_INT_RT_STS, &int_sts);
	if (rc < 0) {
		dev_err(wled->dev, "Error in reading WLED3_INT_RT_STS rc=%d\n",
			rc);
		return IRQ_HANDLED;
	}

	rc = regmap_read(wled->regmap, wled->ctrl_addr +
			 WLED3_CTRL_REG_FAULT_STATUS, &fault_sts);
	if (rc < 0) {
		dev_err(wled->dev, "Error in reading WLED_FAULT_STATUS rc=%d\n",
			rc);
		return IRQ_HANDLED;
	}

	if (fault_sts & (WLED3_CTRL_REG_OVP_FAULT_BIT |
		WLED3_CTRL_REG_ILIM_FAULT_BIT))
		dev_dbg(wled->dev, "WLED OVP fault detected, int_sts=%x fault_sts= %x\n",
			int_sts, fault_sts);

	if (fault_sts & WLED3_CTRL_REG_OVP_FAULT_BIT) {
		if (wled->wled_auto_detection_required(wled)) {
			mutex_lock(&wled->lock);
			wled_auto_string_detection(wled);
			mutex_unlock(&wled->lock);
		}
	}

	return IRQ_HANDLED;
}

static int wled3_setup(struct wled *wled)
{
	u16 addr;
	u8 sink_en = 0;
	int rc, i, j;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_OVP,
				WLED3_CTRL_REG_OVP_MASK, wled->cfg.ovp);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_ILIMIT,
				WLED3_CTRL_REG_ILIMIT_MASK,
				wled->cfg.boost_i_limit);
	if (rc)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_FREQ,
				WLED3_CTRL_REG_FREQ_MASK,
				wled->cfg.switch_freq);
	if (rc)
		return rc;

	for (i = 0; i < wled->cfg.num_strings; ++i) {
		j = wled->cfg.enabled_strings[i];
		addr = wled->ctrl_addr + WLED3_SINK_REG_STR_MOD_EN(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED3_SINK_REG_STR_MOD_MASK,
					WLED3_SINK_REG_STR_MOD_MASK);
		if (rc)
			return rc;

		if (wled->cfg.ext_gen) {
			addr = wled->ctrl_addr + WLED3_SINK_REG_STR_MOD_SRC(j);
			rc = regmap_update_bits(wled->regmap, addr,
						WLED3_SINK_REG_STR_MOD_SRC_MASK,
						WLED3_SINK_REG_STR_MOD_SRC_EXT);
			if (rc)
				return rc;
		}

		addr = wled->ctrl_addr + WLED3_SINK_REG_STR_FULL_SCALE_CURR(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED3_SINK_REG_STR_FULL_SCALE_CURR_MASK,
					wled->cfg.string_i_limit);
		if (rc)
			return rc;

		addr = wled->ctrl_addr + WLED3_SINK_REG_STR_CABC(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED3_SINK_REG_STR_CABC_MASK,
					wled->cfg.cabc ?
					WLED3_SINK_REG_STR_CABC_MASK : 0);
		if (rc)
			return rc;

		sink_en |= BIT(j + WLED3_SINK_REG_CURR_SINK_SHFT);
	}

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_SINK_REG_CURR_SINK,
				WLED3_SINK_REG_CURR_SINK_MASK, sink_en);
	if (rc)
		return rc;

	return 0;
}

static const struct wled_config wled3_config_defaults = {
	.boost_i_limit = 3,
	.string_i_limit = 20,
	.ovp = 2,
	.num_strings = 3,
	.switch_freq = 5,
	.cs_out_en = false,
	.ext_gen = false,
	.cabc = false,
	.enabled_strings = {0, 1, 2, 3},
};

static int wled4_setup(struct wled *wled)
{
	int rc, temp, i, j;
	u16 addr;
	u8 sink_en = 0;
	u32 sink_cfg;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_OVP,
				WLED3_CTRL_REG_OVP_MASK, wled->cfg.ovp);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_ILIMIT,
				WLED3_CTRL_REG_ILIMIT_MASK,
				wled->cfg.boost_i_limit);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_FREQ,
				WLED3_CTRL_REG_FREQ_MASK,
				wled->cfg.switch_freq);
	if (rc < 0)
		return rc;

	if (wled->cfg.external_pfet) {
		/* Unlock the secure register access */
		rc = regmap_write(wled->regmap, wled->ctrl_addr +
				  WLED4_CTRL_REG_SEC_ACCESS,
				  WLED4_CTRL_REG_SEC_UNLOCK);
		if (rc < 0)
			return rc;

		rc = regmap_write(wled->regmap,
				  wled->ctrl_addr + WLED4_CTRL_REG_TEST1,
				  WLED4_CTRL_REG_TEST1_EXT_FET_DTEST2);
		if (rc < 0)
			return rc;
	}

	rc = regmap_read(wled->regmap, wled->sink_addr +
			 WLED4_SINK_REG_CURR_SINK, &sink_cfg);
	if (rc < 0)
		return rc;

	for (i = 0; i < wled->cfg.num_strings; i++) {
		j = wled->cfg.enabled_strings[i];
		temp = j + WLED4_SINK_REG_CURR_SINK_SHFT;
		sink_en |= 1 << temp;
	}

	if (sink_cfg == sink_en) {
		rc = wled_auto_detection_at_init(wled);
		return rc;
	}

	rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED4_SINK_REG_CURR_SINK,
				WLED4_SINK_REG_CURR_SINK_MASK, 0);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
				WLED3_CTRL_REG_MOD_EN,
				WLED3_CTRL_REG_MOD_EN_MASK, 0);
	if (rc < 0)
		return rc;

	/* Per sink/string configuration */
	for (i = 0; i < wled->cfg.num_strings; i++) {
		j = wled->cfg.enabled_strings[i];

		addr = wled->sink_addr +
				WLED4_SINK_REG_STR_MOD_EN(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED4_SINK_REG_STR_MOD_MASK,
					WLED4_SINK_REG_STR_MOD_MASK);
		if (rc < 0)
			return rc;

		addr = wled->sink_addr +
				WLED4_SINK_REG_STR_FULL_SCALE_CURR(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED4_SINK_REG_STR_FULL_SCALE_CURR_MASK,
					wled->cfg.string_i_limit);
		if (rc < 0)
			return rc;
	}

	rc = wled4_cabc_config(wled, wled->cfg.cabc);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
				WLED3_CTRL_REG_MOD_EN,
				WLED3_CTRL_REG_MOD_EN_MASK,
				WLED3_CTRL_REG_MOD_EN_MASK);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED4_SINK_REG_CURR_SINK,
				WLED4_SINK_REG_CURR_SINK_MASK, sink_en);
	if (rc < 0)
		return rc;

	rc = wled->wled_sync_toggle(wled);
	if (rc < 0) {
		dev_err(wled->dev, "Failed to toggle sync reg rc:%d\n", rc);
		return rc;
	}

	rc = wled_auto_detection_at_init(wled);

	return rc;
}

static const struct wled_config wled4_config_defaults = {
	.boost_i_limit = 4,
	.string_i_limit = 10,
	.ovp = 1,
	.num_strings = 4,
	.switch_freq = 11,
	.cabc = false,
	.external_pfet = false,
	.auto_detection_enabled = false,
};

static int wled5_setup(struct wled *wled)
{
	int rc, temp, i, j, offset;
	u8 sink_en = 0;
	u16 addr;
	u32 val;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_OVP,
				WLED5_CTRL_REG_OVP_MASK, wled->cfg.ovp);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_ILIMIT,
				WLED3_CTRL_REG_ILIMIT_MASK,
				wled->cfg.boost_i_limit);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->ctrl_addr + WLED3_CTRL_REG_FREQ,
				WLED3_CTRL_REG_FREQ_MASK,
				wled->cfg.switch_freq);
	if (rc < 0)
		return rc;

	/* Per sink/string configuration */
	for (i = 0; i < wled->cfg.num_strings; ++i) {
		j = wled->cfg.enabled_strings[i];
		addr = wled->sink_addr +
				WLED4_SINK_REG_STR_FULL_SCALE_CURR(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED4_SINK_REG_STR_FULL_SCALE_CURR_MASK,
					wled->cfg.string_i_limit);
		if (rc < 0)
			return rc;

		addr = wled->sink_addr + WLED5_SINK_REG_STR_SRC_SEL(j);
		rc = regmap_update_bits(wled->regmap, addr,
					WLED5_SINK_REG_SRC_SEL_MASK,
					wled->cfg.mod_sel == MOD_A ?
					WLED5_SINK_REG_SRC_SEL_MOD_A :
					WLED5_SINK_REG_SRC_SEL_MOD_B);

		temp = j + WLED4_SINK_REG_CURR_SINK_SHFT;
		sink_en |= 1 << temp;
	}

	rc = wled5_cabc_config(wled, wled->cfg.cabc_sel ? true : false);
	if (rc < 0)
		return rc;

	/* Enable one of the modulators A or B based on mod_sel */
	addr = wled->sink_addr + WLED5_SINK_REG_MOD_A_EN;
	val = (wled->cfg.mod_sel == MOD_A) ? WLED5_SINK_REG_MOD_EN_MASK : 0;
	rc = regmap_update_bits(wled->regmap, addr,
				WLED5_SINK_REG_MOD_EN_MASK, val);
	if (rc < 0)
		return rc;

	addr = wled->sink_addr + WLED5_SINK_REG_MOD_B_EN;
	val = (wled->cfg.mod_sel == MOD_B) ? WLED5_SINK_REG_MOD_EN_MASK : 0;
	rc = regmap_update_bits(wled->regmap, addr,
				WLED5_SINK_REG_MOD_EN_MASK, val);
	if (rc < 0)
		return rc;

	offset = (wled->cfg.mod_sel == MOD_A) ?
		  WLED5_SINK_REG_MOD_A_BRIGHTNESS_WIDTH_SEL :
		  WLED5_SINK_REG_MOD_B_BRIGHTNESS_WIDTH_SEL;

	addr = wled->sink_addr + offset;
	val = (wled->max_brightness == WLED5_SINK_REG_BRIGHT_MAX_15B) ?
		 WLED5_SINK_REG_BRIGHTNESS_WIDTH_15B :
		 WLED5_SINK_REG_BRIGHTNESS_WIDTH_12B;
	rc = regmap_write(wled->regmap, addr, val);
	if (rc < 0)
		return rc;

	rc = regmap_update_bits(wled->regmap,
				wled->sink_addr + WLED4_SINK_REG_CURR_SINK,
				WLED4_SINK_REG_CURR_SINK_MASK, sink_en);
	if (rc < 0)
		return rc;

	/* This updates only FSC configuration in WLED5 */
	rc = wled->wled_sync_toggle(wled);
	if (rc < 0) {
		pr_err("Failed to toggle sync reg rc:%d\n", rc);
		return rc;
	}

	rc = wled_auto_detection_at_init(wled);
	if (rc < 0)
		return rc;

	return 0;
}

static const struct wled_config wled5_config_defaults = {
	.boost_i_limit = 5,
	.string_i_limit = 10,
	.ovp = 4,
	.num_strings = 4,
	.switch_freq = 11,
	.mod_sel = 0,
	.cabc_sel = 0,
	.cabc = false,
	.external_pfet = false,
	.auto_detection_enabled = false,
};

static const u32 wled3_boost_i_limit_values[] = {
	105, 385, 525, 805, 980, 1260, 1400, 1680,
};

static const struct wled_var_cfg wled3_boost_i_limit_cfg = {
	.values = wled3_boost_i_limit_values,
	.size = ARRAY_SIZE(wled3_boost_i_limit_values),
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

static const u32 wled3_ovp_values[] = {
	35, 32, 29, 27,
};

static const struct wled_var_cfg wled3_ovp_cfg = {
	.values = wled3_ovp_values,
	.size = ARRAY_SIZE(wled3_ovp_values),
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

static u32 wled3_num_strings_values_fn(u32 idx)
{
	return idx + 1;
}

static const struct wled_var_cfg wled3_num_strings_cfg = {
	.fn = wled3_num_strings_values_fn,
	.size = 3,
};

static const struct wled_var_cfg wled4_num_strings_cfg = {
	.fn = wled3_num_strings_values_fn,
	.size = 4,
};

static u32 wled3_switch_freq_values_fn(u32 idx)
{
	return 19200 / (2 * (1 + idx));
}

static const struct wled_var_cfg wled3_switch_freq_cfg = {
	.fn = wled3_switch_freq_values_fn,
	.size = 16,
};

static const struct wled_var_cfg wled3_string_i_limit_cfg = {
	.size = 26,
};

static const u32 wled4_string_i_limit_values[] = {
	0, 2500, 5000, 7500, 10000, 12500, 15000, 17500, 20000,
	22500, 25000, 27500, 30000,
};

static const struct wled_var_cfg wled4_string_i_limit_cfg = {
	.values = wled4_string_i_limit_values,
	.size = ARRAY_SIZE(wled4_string_i_limit_values),
};

static const struct wled_var_cfg wled5_mod_sel_cfg = {
	.size = 2,
};

static const struct wled_var_cfg wled5_cabc_sel_cfg = {
	.size = 4,
};

static u32 wled_values(const struct wled_var_cfg *cfg, u32 idx)
{
	if (idx >= cfg->size)
		return UINT_MAX;
	if (cfg->fn)
		return cfg->fn(idx);
	if (cfg->values)
		return cfg->values[idx];
	return idx;
}

static int wled_configure(struct wled *wled)
{
	struct wled_config *cfg = &wled->cfg;
	struct device *dev = wled->dev;
	const __be32 *prop_addr;
	u32 size, val, c;
	int rc, i, j, string_len;

	const struct wled_u32_opts *u32_opts = NULL;
	const struct wled_u32_opts wled3_opts[] = {
		{
			.name = "qcom,current-boost-limit",
			.val_ptr = &cfg->boost_i_limit,
			.cfg = &wled3_boost_i_limit_cfg,
		},
		{
			.name = "qcom,current-limit",
			.val_ptr = &cfg->string_i_limit,
			.cfg = &wled3_string_i_limit_cfg,
		},
		{
			.name = "qcom,ovp",
			.val_ptr = &cfg->ovp,
			.cfg = &wled3_ovp_cfg,
		},
		{
			.name = "qcom,switching-freq",
			.val_ptr = &cfg->switch_freq,
			.cfg = &wled3_switch_freq_cfg,
		},
		{
			.name = "qcom,num-strings",
			.val_ptr = &cfg->num_strings,
			.cfg = &wled3_num_strings_cfg,
		},
	};

	const struct wled_u32_opts wled4_opts[] = {
		{
			.name = "qcom,current-boost-limit",
			.val_ptr = &cfg->boost_i_limit,
			.cfg = &wled4_boost_i_limit_cfg,
		},
		{
			.name = "qcom,current-limit-microamp",
			.val_ptr = &cfg->string_i_limit,
			.cfg = &wled4_string_i_limit_cfg,
		},
		{
			.name = "qcom,ovp-millivolt",
			.val_ptr = &cfg->ovp,
			.cfg = &wled4_ovp_cfg,
		},
		{
			.name = "qcom,switching-freq",
			.val_ptr = &cfg->switch_freq,
			.cfg = &wled3_switch_freq_cfg,
		},
		{
			.name = "qcom,num-strings",
			.val_ptr = &cfg->num_strings,
			.cfg = &wled4_num_strings_cfg,
		},
	};

	const struct wled_u32_opts wled5_opts[] = {
		{
			.name = "qcom,current-boost-limit",
			.val_ptr = &cfg->boost_i_limit,
			.cfg = &wled5_boost_i_limit_cfg,
		},
		{
			.name = "qcom,current-limit-microamp",
			.val_ptr = &cfg->string_i_limit,
			.cfg = &wled4_string_i_limit_cfg,
		},
		{
			.name = "qcom,ovp-millivolt",
			.val_ptr = &cfg->ovp,
			.cfg = &wled5_ovp_cfg,
		},
		{
			.name = "qcom,switching-freq",
			.val_ptr = &cfg->switch_freq,
			.cfg = &wled3_switch_freq_cfg,
		},
		{
			.name = "qcom,num-strings",
			.val_ptr = &cfg->num_strings,
			.cfg = &wled4_num_strings_cfg,
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
	};

	const struct wled_bool_opts bool_opts[] = {
		{ "qcom,cs-out", &cfg->cs_out_en, },
		{ "qcom,ext-gen", &cfg->ext_gen, },
		{ "qcom,cabc", &cfg->cabc, },
		{ "qcom,external-pfet", &cfg->external_pfet, },
		{ "qcom,auto-string-detection", &cfg->auto_detection_enabled, },
	};

	prop_addr = of_get_address(dev->of_node, 0, NULL, NULL);
	if (!prop_addr) {
		dev_err(wled->dev, "invalid IO resources\n");
		return -EINVAL;
	}
	wled->ctrl_addr = be32_to_cpu(*prop_addr);

	rc = of_property_read_string(dev->of_node, "label", &wled->name);
	if (rc)
		wled->name = devm_kasprintf(dev, GFP_KERNEL, "%pOFn", dev->of_node);

	switch (wled->version) {
	case 3:
		u32_opts = wled3_opts;
		size = ARRAY_SIZE(wled3_opts);
		*cfg = wled3_config_defaults;
		wled->wled_set_brightness = wled3_set_brightness;
		wled->wled_sync_toggle = wled3_sync_toggle;
		wled->max_string_count = 3;
		wled->sink_addr = wled->ctrl_addr;
		break;

	case 4:
		u32_opts = wled4_opts;
		size = ARRAY_SIZE(wled4_opts);
		*cfg = wled4_config_defaults;
		wled->wled_set_brightness = wled4_set_brightness;
		wled->wled_sync_toggle = wled3_sync_toggle;
		wled->wled_cabc_config = wled4_cabc_config;
		wled->wled_ovp_delay = wled4_ovp_delay;
		wled->wled_auto_detection_required =
					wled4_auto_detection_required;
		wled->max_string_count = 4;

		prop_addr = of_get_address(dev->of_node, 1, NULL, NULL);
		if (!prop_addr) {
			dev_err(wled->dev, "invalid IO resources\n");
			return -EINVAL;
		}
		wled->sink_addr = be32_to_cpu(*prop_addr);
		break;

	case 5:
		u32_opts = wled5_opts;
		size = ARRAY_SIZE(wled5_opts);
		*cfg = wled5_config_defaults;
		wled->wled_set_brightness = wled5_set_brightness;
		wled->wled_sync_toggle = wled3_sync_toggle;
		wled->wled_cabc_config = wled5_cabc_config;
		wled->wled_ovp_delay = wled5_ovp_delay;
		wled->wled_auto_detection_required =
					wled5_auto_detection_required;
		wled->max_string_count = 4;

		prop_addr = of_get_address(dev->of_node, 1, NULL, NULL);
		if (!prop_addr) {
			dev_err(wled->dev, "invalid IO resources\n");
			return -EINVAL;
		}
		wled->sink_addr = be32_to_cpu(*prop_addr);
		break;

	default:
		dev_err(wled->dev, "Invalid WLED version\n");
		return -EINVAL;
	}

	for (i = 0; i < size; ++i) {
		rc = of_property_read_u32(dev->of_node, u32_opts[i].name, &val);
		if (rc == -EINVAL) {
			continue;
		} else if (rc) {
			dev_err(dev, "error reading '%s'\n", u32_opts[i].name);
			return rc;
		}

		c = UINT_MAX;
		for (j = 0; c != val; j++) {
			c = wled_values(u32_opts[i].cfg, j);
			if (c == UINT_MAX) {
				dev_err(dev, "invalid value for '%s'\n",
					u32_opts[i].name);
				return -EINVAL;
			}

			if (c == val)
				break;
		}

		dev_dbg(dev, "'%s' = %u\n", u32_opts[i].name, c);
		*u32_opts[i].val_ptr = j;
	}

	for (i = 0; i < ARRAY_SIZE(bool_opts); ++i) {
		if (of_property_read_bool(dev->of_node, bool_opts[i].name))
			*bool_opts[i].val_ptr = true;
	}

	cfg->num_strings = cfg->num_strings + 1;

	string_len = of_property_count_elems_of_size(dev->of_node,
						     "qcom,enabled-strings",
						     sizeof(u32));
	if (string_len > 0)
		of_property_read_u32_array(dev->of_node,
						"qcom,enabled-strings",
						wled->cfg.enabled_strings,
						sizeof(u32));

	return 0;
}

static int wled_configure_short_irq(struct wled *wled,
				    struct platform_device *pdev)
{
	int rc;

	if (!wled->has_short_detect)
		return 0;

	rc = regmap_update_bits(wled->regmap, wled->ctrl_addr +
				WLED4_CTRL_REG_SHORT_PROTECT,
				WLED4_CTRL_REG_SHORT_EN_MASK,
				WLED4_CTRL_REG_SHORT_EN_MASK);
	if (rc < 0)
		return rc;

	wled->short_irq = platform_get_irq_byname(pdev, "short");
	if (wled->short_irq < 0) {
		dev_dbg(&pdev->dev, "short irq is not used\n");
		return 0;
	}

	rc = devm_request_threaded_irq(wled->dev, wled->short_irq,
				       NULL, wled_short_irq_handler,
				       IRQF_ONESHOT,
				       "wled_short_irq", wled);
	if (rc < 0)
		dev_err(wled->dev, "Unable to request short_irq (err:%d)\n",
			rc);

	return rc;
}

static int wled_configure_ovp_irq(struct wled *wled,
				  struct platform_device *pdev)
{
	int rc;
	u32 val;

	wled->ovp_irq = platform_get_irq_byname(pdev, "ovp");
	if (wled->ovp_irq < 0) {
		dev_dbg(&pdev->dev, "OVP IRQ not found - disabling automatic string detection\n");
		return 0;
	}

	rc = devm_request_threaded_irq(wled->dev, wled->ovp_irq, NULL,
				       wled_ovp_irq_handler, IRQF_ONESHOT,
				       "wled_ovp_irq", wled);
	if (rc < 0) {
		dev_err(wled->dev, "Unable to request ovp_irq (err:%d)\n",
			rc);
		wled->ovp_irq = 0;
		return 0;
	}

	rc = regmap_read(wled->regmap, wled->ctrl_addr +
			 WLED3_CTRL_REG_MOD_EN, &val);
	if (rc < 0)
		return rc;

	/* Keep OVP irq disabled until module is enabled */
	if (!(val & WLED3_CTRL_REG_MOD_EN_MASK))
		disable_irq(wled->ovp_irq);

	return 0;
}

static const struct backlight_ops wled_ops = {
	.update_status = wled_update_status,
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
		dev_err(&pdev->dev, "Unable to get regmap\n");
		return -EINVAL;
	}

	wled = devm_kzalloc(&pdev->dev, sizeof(*wled), GFP_KERNEL);
	if (!wled)
		return -ENOMEM;

	wled->regmap = regmap;
	wled->dev = &pdev->dev;

	wled->version = (uintptr_t)of_device_get_match_data(&pdev->dev);
	if (!wled->version) {
		dev_err(&pdev->dev, "Unknown device version\n");
		return -ENODEV;
	}

	mutex_init(&wled->lock);
	rc = wled_configure(wled);
	if (rc)
		return rc;

	val = WLED3_SINK_REG_BRIGHT_MAX;
	of_property_read_u32(pdev->dev.of_node, "max-brightness", &val);
	wled->max_brightness = val;

	switch (wled->version) {
	case 3:
		wled->cfg.auto_detection_enabled = false;
		rc = wled3_setup(wled);
		if (rc) {
			dev_err(&pdev->dev, "wled3_setup failed\n");
			return rc;
		}
		break;

	case 4:
		wled->has_short_detect = true;
		rc = wled4_setup(wled);
		if (rc) {
			dev_err(&pdev->dev, "wled4_setup failed\n");
			return rc;
		}
		break;

	case 5:
		wled->has_short_detect = true;
		if (wled->cfg.cabc_sel)
			wled->max_brightness = WLED5_SINK_REG_BRIGHT_MAX_12B;

		rc = wled5_setup(wled);
		if (rc) {
			dev_err(&pdev->dev, "wled5_setup failed\n");
			return rc;
		}
		break;

	default:
		dev_err(wled->dev, "Invalid WLED version\n");
		break;
	}

	INIT_DELAYED_WORK(&wled->ovp_work, wled_ovp_work);

	rc = wled_configure_short_irq(wled, pdev);
	if (rc < 0)
		return rc;

	rc = wled_configure_ovp_irq(wled, pdev);
	if (rc < 0)
		return rc;

	val = WLED_DEFAULT_BRIGHTNESS;
	of_property_read_u32(pdev->dev.of_node, "default-brightness", &val);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.brightness = val;
	props.max_brightness = wled->max_brightness;
	bl = devm_backlight_device_register(&pdev->dev, wled->name,
					    &pdev->dev, wled,
					    &wled_ops, &props);
	return PTR_ERR_OR_ZERO(bl);
};

static int wled_remove(struct platform_device *pdev)
{
	struct wled *wled = platform_get_drvdata(pdev);

	mutex_destroy(&wled->lock);
	cancel_delayed_work_sync(&wled->ovp_work);
	disable_irq(wled->short_irq);
	disable_irq(wled->ovp_irq);

	return 0;
}

static const struct of_device_id wled_match_table[] = {
	{ .compatible = "qcom,pm8941-wled", .data = (void *)3 },
	{ .compatible = "qcom,pmi8998-wled", .data = (void *)4 },
	{ .compatible = "qcom,pm660l-wled", .data = (void *)4 },
	{ .compatible = "qcom,pm8150l-wled", .data = (void *)5 },
	{}
};
MODULE_DEVICE_TABLE(of, wled_match_table);

static struct platform_driver wled_driver = {
	.probe = wled_probe,
	.remove = wled_remove,
	.driver	= {
		.name = "qcom,wled",
		.of_match_table	= wled_match_table,
	},
};

module_platform_driver(wled_driver);

MODULE_DESCRIPTION("Qualcomm WLED driver");
MODULE_LICENSE("GPL v2");
