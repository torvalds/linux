// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Texas Instruments TPS61310/TPS61311 flash LED driver with I2C interface
 *
 * Copyright 2025 Matthias Fend <matthias.fend@emfend.at>
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/led-class-flash.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <media/v4l2-flash-led-class.h>

#define TPS6131X_REG_0				0x00
#define   TPS6131X_REG_0_RESET			BIT(7)
#define   TPS6131X_REG_0_DCLC13			GENMASK(5, 3)
#define   TPS6131X_REG_0_DCLC13_SHIFT		3
#define   TPS6131X_REG_0_DCLC2			GENMASK(2, 0)
#define   TPS6131X_REG_0_DCLC2_SHIFT		0

#define TPS6131X_REG_1				0x01
#define   TPS6131X_REG_1_MODE			GENMASK(7, 6)
#define   TPS6131X_REG_1_MODE_SHIFT		6
#define   TPS6131X_REG_1_FC2			GENMASK(5, 0)
#define   TPS6131X_REG_1_FC2_SHIFT		0

#define TPS6131X_REG_2				0x02
#define   TPS6131X_REG_2_MODE			GENMASK(7, 6)
#define   TPS6131X_REG_2_MODE_SHIFT		6
#define   TPS6131X_REG_2_ENVM			BIT(5)
#define   TPS6131X_REG_2_FC13			GENMASK(4, 0)
#define   TPS6131X_REG_2_FC13_SHIFT		0

#define TPS6131X_REG_3				0x03
#define   TPS6131X_REG_3_STIM			GENMASK(7, 5)
#define   TPS6131X_REG_3_STIM_SHIFT		5
#define   TPS6131X_REG_3_HPFL			BIT(4)
#define   TPS6131X_REG_3_SELSTIM_TO		BIT(3)
#define   TPS6131X_REG_3_STT			BIT(2)
#define   TPS6131X_REG_3_SFT			BIT(1)
#define   TPS6131X_REG_3_TXMASK			BIT(0)

#define TPS6131X_REG_4				0x04
#define   TPS6131X_REG_4_PG			BIT(7)
#define   TPS6131X_REG_4_HOTDIE_HI		BIT(6)
#define   TPS6131X_REG_4_HOTDIE_LO		BIT(5)
#define   TPS6131X_REG_4_ILIM			BIT(4)
#define   TPS6131X_REG_4_INDC			GENMASK(3, 0)
#define   TPS6131X_REG_4_INDC_SHIFT		0

#define TPS6131X_REG_5				0x05
#define   TPS6131X_REG_5_SELFCAL		BIT(7)
#define   TPS6131X_REG_5_ENPSM			BIT(6)
#define   TPS6131X_REG_5_STSTRB1_DIR		BIT(5)
#define   TPS6131X_REG_5_GPIO			BIT(4)
#define   TPS6131X_REG_5_GPIOTYPE		BIT(3)
#define   TPS6131X_REG_5_ENLED3			BIT(2)
#define   TPS6131X_REG_5_ENLED2			BIT(1)
#define   TPS6131X_REG_5_ENLED1			BIT(0)

#define TPS6131X_REG_6				0x06
#define   TPS6131X_REG_6_ENTS			BIT(7)
#define   TPS6131X_REG_6_LEDHOT			BIT(6)
#define   TPS6131X_REG_6_LEDWARN		BIT(5)
#define   TPS6131X_REG_6_LEDHDR			BIT(4)
#define   TPS6131X_REG_6_OV			GENMASK(3, 0)
#define   TPS6131X_REG_6_OV_SHIFT		0

#define TPS6131X_REG_7				0x07
#define   TPS6131X_REG_7_ENBATMON		BIT(7)
#define   TPS6131X_REG_7_BATDROOP		GENMASK(6, 4)
#define   TPS6131X_REG_7_BATDROOP_SHIFT		4
#define   TPS6131X_REG_7_REVID			GENMASK(2, 0)
#define   TPS6131X_REG_7_REVID_SHIFT		0

#define TPS6131X_MAX_CHANNELS			3

#define TPS6131X_FLASH_MAX_I_CHAN13_MA		400
#define TPS6131X_FLASH_MAX_I_CHAN2_MA		800
#define TPS6131X_FLASH_STEP_I_MA		25

#define TPS6131X_TORCH_MAX_I_CHAN13_MA		175
#define TPS6131X_TORCH_MAX_I_CHAN2_MA		175
#define TPS6131X_TORCH_STEP_I_MA		25

/* The torch watchdog timer must be refreshed within an interval of 13 seconds. */
#define TPS6131X_TORCH_REFRESH_INTERVAL_JIFFIES msecs_to_jiffies(10000)

#define UA_TO_MA(UA)				((UA) / 1000)

enum tps6131x_mode {
	TPS6131X_MODE_SHUTDOWN = 0x0,
	TPS6131X_MODE_TORCH = 0x1,
	TPS6131X_MODE_FLASH = 0x2,
};

struct tps6131x {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	/*
	 * Registers 0, 1, 2, and 3 control parts of the controller that are not completely
	 * independent of each other. Since some operations require the registers to be written in
	 * a specific order to avoid unwanted side effects, they are synchronized with a lock.
	 */
	struct mutex lock; /* Hardware access lock for register 0, 1, 2 and 3 */
	struct delayed_work torch_refresh_work;
	bool valley_current_limit;
	bool chan1_en;
	bool chan2_en;
	bool chan3_en;
	struct fwnode_handle *led_node;
	u32 max_flash_current_ma;
	u32 step_flash_current_ma;
	u32 max_torch_current_ma;
	u32 step_torch_current_ma;
	u32 max_timeout_us;
	struct led_classdev_flash fled_cdev;
	struct v4l2_flash *v4l2_flash;
};

static struct tps6131x *fled_cdev_to_tps6131x(struct led_classdev_flash *fled_cdev)
{
	return container_of(fled_cdev, struct tps6131x, fled_cdev);
}

/*
 * Register contents after a power on/reset. These values cannot be changed.
 */

#define TPS6131X_DCLC2_50MA	     2
#define TPS6131X_DCLC13_25MA	     1
#define TPS6131X_FC2_400MA	     16
#define TPS6131X_FC13_200MA	     8
#define TPS6131X_STIM_0_579MS_1_37MS 6
#define TPS6131X_SELSTIM_RANGE0	     0
#define TPS6131X_INDC_OFF	     0
#define TPS6131X_OV_4950MV	     9
#define TPS6131X_BATDROOP_150MV	     4

static const struct reg_default tps6131x_regmap_defaults[] = {
	{ TPS6131X_REG_0, (TPS6131X_DCLC13_25MA << TPS6131X_REG_0_DCLC13_SHIFT) |
				  (TPS6131X_DCLC2_50MA << TPS6131X_REG_0_DCLC2_SHIFT) },
	{ TPS6131X_REG_1, (TPS6131X_MODE_SHUTDOWN << TPS6131X_REG_1_MODE_SHIFT) |
				  (TPS6131X_FC2_400MA << TPS6131X_REG_1_FC2_SHIFT) },
	{ TPS6131X_REG_2, (TPS6131X_MODE_SHUTDOWN << TPS6131X_REG_2_MODE_SHIFT) |
				  (TPS6131X_FC13_200MA << TPS6131X_REG_2_FC13_SHIFT) },
	{ TPS6131X_REG_3, (TPS6131X_STIM_0_579MS_1_37MS << TPS6131X_REG_3_STIM_SHIFT) |
				  (TPS6131X_SELSTIM_RANGE0 << TPS6131X_REG_3_SELSTIM_TO) |
				  TPS6131X_REG_3_TXMASK },
	{ TPS6131X_REG_4, (TPS6131X_INDC_OFF << TPS6131X_REG_4_INDC_SHIFT) },
	{ TPS6131X_REG_5, TPS6131X_REG_5_ENPSM | TPS6131X_REG_5_STSTRB1_DIR |
				  TPS6131X_REG_5_GPIOTYPE | TPS6131X_REG_5_ENLED2 },
	{ TPS6131X_REG_6, (TPS6131X_OV_4950MV << TPS6131X_REG_6_OV_SHIFT) },
	{ TPS6131X_REG_7, (TPS6131X_BATDROOP_150MV << TPS6131X_REG_7_BATDROOP_SHIFT) },
};

/*
 * These registers contain flags that are reset when read.
 */
static bool tps6131x_regmap_precious(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TPS6131X_REG_3:
	case TPS6131X_REG_4:
	case TPS6131X_REG_6:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tps6131x_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPS6131X_REG_7,
	.reg_defaults = tps6131x_regmap_defaults,
	.num_reg_defaults = ARRAY_SIZE(tps6131x_regmap_defaults),
	.cache_type = REGCACHE_FLAT,
	.precious_reg = &tps6131x_regmap_precious,
};

struct tps6131x_timer_config {
	u8 val;
	u8 range;
	u32 time_us;
};

static const struct tps6131x_timer_config tps6131x_timer_configs[] = {
	{ .val = 0, .range = 1, .time_us = 5300 },
	{ .val = 1, .range = 1, .time_us = 10700 },
	{ .val = 2, .range = 1, .time_us = 16000 },
	{ .val = 3, .range = 1, .time_us = 21300 },
	{ .val = 4, .range = 1, .time_us = 26600 },
	{ .val = 5, .range = 1, .time_us = 32000 },
	{ .val = 6, .range = 1, .time_us = 37300 },
	{ .val = 0, .range = 0, .time_us = 68200 },
	{ .val = 7, .range = 1, .time_us = 71500 },
	{ .val = 1, .range = 0, .time_us = 102200 },
	{ .val = 2, .range = 0, .time_us = 136300 },
	{ .val = 3, .range = 0, .time_us = 170400 },
	{ .val = 4, .range = 0, .time_us = 204500 },
	{ .val = 5, .range = 0, .time_us = 340800 },
	{ .val = 6, .range = 0, .time_us = 579300 },
	{ .val = 7, .range = 0, .time_us = 852000 },
};

static const struct tps6131x_timer_config *tps6131x_find_closest_timer_config(u32 timeout_us)
{
	const struct tps6131x_timer_config *timer_config = &tps6131x_timer_configs[0];
	u32 diff, min_diff = U32_MAX;
	int i;

	for (i = 0; i < ARRAY_SIZE(tps6131x_timer_configs); i++) {
		diff = abs(tps6131x_timer_configs[i].time_us - timeout_us);
		if (diff < min_diff) {
			timer_config = &tps6131x_timer_configs[i];
			min_diff = diff;
			if (!min_diff)
				break;
		}
	}

	return timer_config;
}

static int tps6131x_reset_chip(struct tps6131x *tps6131x)
{
	int ret;

	if (tps6131x->reset_gpio) {
		gpiod_set_value_cansleep(tps6131x->reset_gpio, 1);
		fsleep(10);
		gpiod_set_value_cansleep(tps6131x->reset_gpio, 0);
		fsleep(100);
	} else {
		ret = regmap_update_bits(tps6131x->regmap, TPS6131X_REG_0, TPS6131X_REG_0_RESET,
					 TPS6131X_REG_0_RESET);
		if (ret)
			return ret;

		fsleep(100);

		ret = regmap_update_bits(tps6131x->regmap, TPS6131X_REG_0, TPS6131X_REG_0_RESET, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static int tps6131x_init_chip(struct tps6131x *tps6131x)
{
	u32 val;
	int ret;

	val = tps6131x->valley_current_limit ? TPS6131X_REG_4_ILIM : 0;

	ret = regmap_write(tps6131x->regmap, TPS6131X_REG_4, val);
	if (ret)
		return ret;

	val = TPS6131X_REG_5_ENPSM | TPS6131X_REG_5_STSTRB1_DIR | TPS6131X_REG_5_GPIOTYPE;

	if (tps6131x->chan1_en)
		val |= TPS6131X_REG_5_ENLED1;

	if (tps6131x->chan2_en)
		val |= TPS6131X_REG_5_ENLED2;

	if (tps6131x->chan3_en)
		val |= TPS6131X_REG_5_ENLED3;

	ret = regmap_write(tps6131x->regmap, TPS6131X_REG_5, val);
	if (ret)
		return ret;

	val = TPS6131X_REG_6_ENTS;

	ret = regmap_write(tps6131x->regmap, TPS6131X_REG_6, val);
	if (ret)
		return ret;

	return 0;
}

static int tps6131x_set_mode(struct tps6131x *tps6131x, enum tps6131x_mode mode, bool force)
{
	u8 val = mode << TPS6131X_REG_1_MODE_SHIFT;

	return regmap_update_bits_base(tps6131x->regmap, TPS6131X_REG_1, TPS6131X_REG_1_MODE, val,
				       NULL, false, force);
}

static void tps6131x_torch_refresh_handler(struct work_struct *work)
{
	struct tps6131x *tps6131x = container_of(work, struct tps6131x, torch_refresh_work.work);
	int ret;

	guard(mutex)(&tps6131x->lock);

	ret = tps6131x_set_mode(tps6131x, TPS6131X_MODE_TORCH, true);
	if (ret < 0) {
		dev_err(tps6131x->dev, "Failed to refresh torch watchdog timer\n");
		return;
	}

	schedule_delayed_work(&tps6131x->torch_refresh_work,
			      TPS6131X_TORCH_REFRESH_INTERVAL_JIFFIES);
}

static int tps6131x_brightness_set(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(cdev);
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);
	u32 num_chans, steps_chan13, steps_chan2, steps_remaining;
	u8 reg0;
	int ret;

	cancel_delayed_work_sync(&tps6131x->torch_refresh_work);

	/*
	 * The brightness parameter uses the number of current steps as the unit (not the current
	 * value itself). Since the reported step size can vary depending on the configuration,
	 * this value must be converted into actual register steps.
	 */
	steps_remaining = (brightness * tps6131x->step_torch_current_ma) / TPS6131X_TORCH_STEP_I_MA;

	num_chans = tps6131x->chan1_en + tps6131x->chan2_en + tps6131x->chan3_en;

	/*
	 * The currents are distributed as evenly as possible across the activated channels.
	 * Since channels 1 and 3 share the same register setting, they always use the same current
	 * value. Channel 2 supports higher currents and thus takes over the remaining additional
	 * portion that cannot be covered by the other channels.
	 */
	steps_chan13 = min_t(u32, steps_remaining / num_chans,
			     TPS6131X_TORCH_MAX_I_CHAN13_MA / TPS6131X_TORCH_STEP_I_MA);
	if (tps6131x->chan1_en)
		steps_remaining -= steps_chan13;
	if (tps6131x->chan3_en)
		steps_remaining -= steps_chan13;

	steps_chan2 = min_t(u32, steps_remaining,
			    TPS6131X_TORCH_MAX_I_CHAN2_MA / TPS6131X_TORCH_STEP_I_MA);

	guard(mutex)(&tps6131x->lock);

	reg0 = (steps_chan13 << TPS6131X_REG_0_DCLC13_SHIFT) |
	       (steps_chan2 << TPS6131X_REG_0_DCLC2_SHIFT);
	ret = regmap_update_bits(tps6131x->regmap, TPS6131X_REG_0,
				 TPS6131X_REG_0_DCLC13 | TPS6131X_REG_0_DCLC2, reg0);
	if (ret < 0)
		return ret;

	ret = tps6131x_set_mode(tps6131x, brightness ? TPS6131X_MODE_TORCH : TPS6131X_MODE_SHUTDOWN,
				true);
	if (ret < 0)
		return ret;

	/*
	 * In order to use both the flash and the video light functions purely via the I2C
	 * interface, STRB1 must be low. If STRB1 is low, then the video light watchdog timer
	 * is also active, which puts the device into the shutdown state after around 13 seconds.
	 * To prevent this, the mode must be refreshed within the watchdog timeout.
	 */
	if (brightness)
		schedule_delayed_work(&tps6131x->torch_refresh_work,
				      TPS6131X_TORCH_REFRESH_INTERVAL_JIFFIES);

	return 0;
}

static int tps6131x_strobe_set(struct led_classdev_flash *fled_cdev, bool state)
{
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);
	int ret;

	guard(mutex)(&tps6131x->lock);

	ret = tps6131x_set_mode(tps6131x, state ? TPS6131X_MODE_FLASH : TPS6131X_MODE_SHUTDOWN,
				true);
	if (ret < 0)
		return ret;

	if (state) {
		ret = regmap_update_bits_base(tps6131x->regmap, TPS6131X_REG_3, TPS6131X_REG_3_SFT,
					      TPS6131X_REG_3_SFT, NULL, false, true);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits_base(tps6131x->regmap, TPS6131X_REG_3, TPS6131X_REG_3_SFT, 0, NULL,
				      false, true);
	if (ret)
		return ret;

	return 0;
}

static int tps6131x_flash_brightness_set(struct led_classdev_flash *fled_cdev, u32 brightness)
{
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);
	u32 num_chans;
	u32 steps_chan13, steps_chan2;
	u32 steps_remaining;
	int ret;

	steps_remaining = brightness / TPS6131X_FLASH_STEP_I_MA;
	num_chans = tps6131x->chan1_en + tps6131x->chan2_en + tps6131x->chan3_en;
	steps_chan13 = min_t(u32, steps_remaining / num_chans,
			     TPS6131X_FLASH_MAX_I_CHAN13_MA / TPS6131X_FLASH_STEP_I_MA);
	if (tps6131x->chan1_en)
		steps_remaining -= steps_chan13;
	if (tps6131x->chan3_en)
		steps_remaining -= steps_chan13;
	steps_chan2 = min_t(u32, steps_remaining,
			    TPS6131X_FLASH_MAX_I_CHAN2_MA / TPS6131X_FLASH_STEP_I_MA);

	guard(mutex)(&tps6131x->lock);

	ret = regmap_update_bits(tps6131x->regmap, TPS6131X_REG_2, TPS6131X_REG_2_FC13,
				 steps_chan13 << TPS6131X_REG_2_FC13_SHIFT);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(tps6131x->regmap, TPS6131X_REG_1, TPS6131X_REG_1_FC2,
				 steps_chan2 << TPS6131X_REG_1_FC2_SHIFT);
	if (ret < 0)
		return ret;

	fled_cdev->brightness.val = brightness;

	return 0;
}

static int tps6131x_flash_timeout_set(struct led_classdev_flash *fled_cdev, u32 timeout_us)
{
	const struct tps6131x_timer_config *timer_config;
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);
	u8 reg3;
	int ret;

	guard(mutex)(&tps6131x->lock);

	timer_config = tps6131x_find_closest_timer_config(timeout_us);

	reg3 = timer_config->val << TPS6131X_REG_3_STIM_SHIFT;
	if (timer_config->range)
		reg3 |= TPS6131X_REG_3_SELSTIM_TO;

	ret = regmap_update_bits(tps6131x->regmap, TPS6131X_REG_3,
				 TPS6131X_REG_3_STIM | TPS6131X_REG_3_SELSTIM_TO, reg3);
	if (ret < 0)
		return ret;

	fled_cdev->timeout.val = timer_config->time_us;

	return 0;
}

static int tps6131x_strobe_get(struct led_classdev_flash *fled_cdev, bool *state)
{
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);
	unsigned int reg3;
	int ret;

	ret = regmap_read_bypassed(tps6131x->regmap, TPS6131X_REG_3, &reg3);
	if (ret)
		return ret;

	*state = !!(reg3 & TPS6131X_REG_3_SFT);

	return 0;
}

static int tps6131x_flash_fault_get(struct led_classdev_flash *fled_cdev, u32 *fault)
{
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);
	unsigned int reg3, reg4, reg6;
	int ret;

	*fault = 0;

	ret = regmap_read_bypassed(tps6131x->regmap, TPS6131X_REG_3, &reg3);
	if (ret < 0)
		return ret;

	ret = regmap_read_bypassed(tps6131x->regmap, TPS6131X_REG_4, &reg4);
	if (ret < 0)
		return ret;

	ret = regmap_read_bypassed(tps6131x->regmap, TPS6131X_REG_6, &reg6);
	if (ret < 0)
		return ret;

	if (reg3 & TPS6131X_REG_3_HPFL)
		*fault |= LED_FAULT_SHORT_CIRCUIT;

	if (reg3 & TPS6131X_REG_3_SELSTIM_TO)
		*fault |= LED_FAULT_TIMEOUT;

	if (reg4 & TPS6131X_REG_4_HOTDIE_HI)
		*fault |= LED_FAULT_OVER_TEMPERATURE;

	if (reg6 & (TPS6131X_REG_6_LEDHOT | TPS6131X_REG_6_LEDWARN))
		*fault |= LED_FAULT_LED_OVER_TEMPERATURE;

	if (!(reg6 & TPS6131X_REG_6_LEDHDR))
		*fault |= LED_FAULT_UNDER_VOLTAGE;

	if (reg6 & TPS6131X_REG_6_LEDHOT) {
		ret = regmap_update_bits_base(tps6131x->regmap, TPS6131X_REG_6,
					      TPS6131X_REG_6_LEDHOT, 0, NULL, false, true);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct led_flash_ops flash_ops = {
	.flash_brightness_set = tps6131x_flash_brightness_set,
	.strobe_set = tps6131x_strobe_set,
	.strobe_get = tps6131x_strobe_get,
	.timeout_set = tps6131x_flash_timeout_set,
	.fault_get = tps6131x_flash_fault_get,
};

static int tps6131x_parse_node(struct tps6131x *tps6131x)
{
	const struct tps6131x_timer_config *timer_config;
	struct device *dev = tps6131x->dev;
	u32 channels[TPS6131X_MAX_CHANNELS];
	u32 current_step_multiplier;
	u32 current_ua;
	u32 max_current_flash_ma, max_current_torch_ma;
	u32 timeout_us;
	int num_channels;
	int i;
	int ret;

	tps6131x->valley_current_limit = device_property_read_bool(dev, "ti,valley-current-limit");

	tps6131x->led_node = fwnode_get_next_available_child_node(dev->fwnode, NULL);
	if (!tps6131x->led_node) {
		dev_err(dev, "Missing LED node\n");
		return -EINVAL;
	}

	num_channels = fwnode_property_count_u32(tps6131x->led_node, "led-sources");
	if (num_channels <= 0) {
		dev_err(dev, "Failed to read led-sources property\n");
		return -EINVAL;
	}

	if (num_channels > TPS6131X_MAX_CHANNELS) {
		dev_err(dev, "led-sources count %u exceeds maximum channel count %u\n",
			num_channels, TPS6131X_MAX_CHANNELS);
		return -EINVAL;
	}

	ret = fwnode_property_read_u32_array(tps6131x->led_node, "led-sources", channels,
					     num_channels);
	if (ret < 0) {
		dev_err(dev, "Failed to read led-sources property\n");
		return ret;
	}

	max_current_flash_ma = 0;
	max_current_torch_ma = 0;
	for (i = 0; i < num_channels; i++) {
		switch (channels[i]) {
		case 1:
			tps6131x->chan1_en = true;
			max_current_flash_ma += TPS6131X_FLASH_MAX_I_CHAN13_MA;
			max_current_torch_ma += TPS6131X_TORCH_MAX_I_CHAN13_MA;
			break;
		case 2:
			tps6131x->chan2_en = true;
			max_current_flash_ma += TPS6131X_FLASH_MAX_I_CHAN2_MA;
			max_current_torch_ma += TPS6131X_TORCH_MAX_I_CHAN2_MA;
			break;
		case 3:
			tps6131x->chan3_en = true;
			max_current_flash_ma += TPS6131X_FLASH_MAX_I_CHAN13_MA;
			max_current_torch_ma += TPS6131X_TORCH_MAX_I_CHAN13_MA;
			break;
		default:
			dev_err(dev, "led-source out of range [1-3]\n");
			return -EINVAL;
		}
	}

	/*
	 * If only channels 1 and 3 are used, the step size is doubled because the two channels
	 * share the same current control register.
	 */
	current_step_multiplier =
		(tps6131x->chan1_en && tps6131x->chan3_en && !tps6131x->chan2_en) ? 2 : 1;
	tps6131x->step_flash_current_ma = current_step_multiplier * TPS6131X_FLASH_STEP_I_MA;
	tps6131x->step_torch_current_ma = current_step_multiplier * TPS6131X_TORCH_STEP_I_MA;

	ret = fwnode_property_read_u32(tps6131x->led_node, "led-max-microamp", &current_ua);
	if (ret < 0) {
		dev_err(dev, "Failed to read led-max-microamp property\n");
		return ret;
	}

	tps6131x->max_torch_current_ma = UA_TO_MA(current_ua);

	if (!tps6131x->max_torch_current_ma ||
	    tps6131x->max_torch_current_ma > max_current_torch_ma ||
	    (tps6131x->max_torch_current_ma % tps6131x->step_torch_current_ma)) {
		dev_err(dev, "led-max-microamp out of range or not a multiple of %u\n",
			tps6131x->step_torch_current_ma);
		return -EINVAL;
	}

	ret = fwnode_property_read_u32(tps6131x->led_node, "flash-max-microamp", &current_ua);
	if (ret < 0) {
		dev_err(dev, "Failed to read flash-max-microamp property\n");
		return ret;
	}

	tps6131x->max_flash_current_ma = UA_TO_MA(current_ua);

	if (!tps6131x->max_flash_current_ma ||
	    tps6131x->max_flash_current_ma > max_current_flash_ma ||
	    (tps6131x->max_flash_current_ma % tps6131x->step_flash_current_ma)) {
		dev_err(dev, "flash-max-microamp out of range or not a multiple of %u\n",
			tps6131x->step_flash_current_ma);
		return -EINVAL;
	}

	ret = fwnode_property_read_u32(tps6131x->led_node, "flash-max-timeout-us", &timeout_us);
	if (ret < 0) {
		dev_err(dev, "Failed to read flash-max-timeout-us property\n");
		return ret;
	}

	timer_config = tps6131x_find_closest_timer_config(timeout_us);
	tps6131x->max_timeout_us = timer_config->time_us;

	if (tps6131x->max_timeout_us != timeout_us)
		dev_warn(dev, "flash-max-timeout-us %u not supported (using %u)\n", timeout_us,
			 tps6131x->max_timeout_us);

	return 0;
}

static int tps6131x_led_class_setup(struct tps6131x *tps6131x)
{
	const struct tps6131x_timer_config *timer_config;
	struct led_classdev *led_cdev;
	struct led_flash_setting *setting;
	struct led_init_data init_data = {};
	int ret;

	tps6131x->fled_cdev.ops = &flash_ops;

	setting = &tps6131x->fled_cdev.timeout;
	timer_config = tps6131x_find_closest_timer_config(0);
	setting->min = timer_config->time_us;
	setting->max = tps6131x->max_timeout_us;
	setting->step = 1; /* Only some specific time periods are supported. No fixed step size. */
	setting->val = setting->min;

	setting = &tps6131x->fled_cdev.brightness;
	setting->min = tps6131x->step_flash_current_ma;
	setting->max = tps6131x->max_flash_current_ma;
	setting->step = tps6131x->step_flash_current_ma;
	setting->val = setting->min;

	led_cdev = &tps6131x->fled_cdev.led_cdev;
	led_cdev->brightness_set_blocking = tps6131x_brightness_set;
	led_cdev->max_brightness = tps6131x->max_torch_current_ma;
	led_cdev->flags |= LED_DEV_CAP_FLASH;

	init_data.fwnode = tps6131x->led_node;
	init_data.devicename = NULL;
	init_data.default_label = NULL;
	init_data.devname_mandatory = false;

	ret = devm_led_classdev_flash_register_ext(tps6131x->dev, &tps6131x->fled_cdev,
						   &init_data);
	if (ret)
		return ret;

	return 0;
}

static int tps6131x_flash_external_strobe_set(struct v4l2_flash *v4l2_flash, bool enable)
{
	struct led_classdev_flash *fled_cdev = v4l2_flash->fled_cdev;
	struct tps6131x *tps6131x = fled_cdev_to_tps6131x(fled_cdev);

	guard(mutex)(&tps6131x->lock);

	return tps6131x_set_mode(tps6131x, enable ? TPS6131X_MODE_FLASH : TPS6131X_MODE_SHUTDOWN,
				 false);
}

static const struct v4l2_flash_ops tps6131x_v4l2_flash_ops = {
	.external_strobe_set = tps6131x_flash_external_strobe_set,
};

static int tps6131x_v4l2_setup(struct tps6131x *tps6131x)
{
	struct v4l2_flash_config v4l2_cfg = { 0 };
	struct led_flash_setting *intensity = &v4l2_cfg.intensity;

	intensity->min = tps6131x->step_torch_current_ma;
	intensity->max = tps6131x->max_torch_current_ma;
	intensity->step = tps6131x->step_torch_current_ma;
	intensity->val = intensity->min;

	strscpy(v4l2_cfg.dev_name, tps6131x->fled_cdev.led_cdev.dev->kobj.name,
		sizeof(v4l2_cfg.dev_name));

	v4l2_cfg.has_external_strobe = true;
	v4l2_cfg.flash_faults = LED_FAULT_TIMEOUT | LED_FAULT_OVER_TEMPERATURE |
				LED_FAULT_SHORT_CIRCUIT | LED_FAULT_UNDER_VOLTAGE |
				LED_FAULT_LED_OVER_TEMPERATURE;

	tps6131x->v4l2_flash = v4l2_flash_init(tps6131x->dev, tps6131x->led_node,
					       &tps6131x->fled_cdev, &tps6131x_v4l2_flash_ops,
					       &v4l2_cfg);
	if (IS_ERR(tps6131x->v4l2_flash)) {
		dev_err(tps6131x->dev, "Failed to initialize v4l2 flash LED\n");
		return PTR_ERR(tps6131x->v4l2_flash);
	}

	return 0;
}

static int tps6131x_probe(struct i2c_client *client)
{
	struct tps6131x *tps6131x;
	int ret;

	tps6131x = devm_kzalloc(&client->dev, sizeof(*tps6131x), GFP_KERNEL);
	if (!tps6131x)
		return -ENOMEM;

	tps6131x->dev = &client->dev;
	i2c_set_clientdata(client, tps6131x);
	mutex_init(&tps6131x->lock);
	INIT_DELAYED_WORK(&tps6131x->torch_refresh_work, tps6131x_torch_refresh_handler);

	ret = tps6131x_parse_node(tps6131x);
	if (ret)
		return ret;

	tps6131x->regmap = devm_regmap_init_i2c(client, &tps6131x_regmap);
	if (IS_ERR(tps6131x->regmap)) {
		ret = PTR_ERR(tps6131x->regmap);
		return dev_err_probe(&client->dev, ret, "Failed to allocate register map\n");
	}

	tps6131x->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(tps6131x->reset_gpio)) {
		ret = PTR_ERR(tps6131x->reset_gpio);
		return dev_err_probe(&client->dev, ret, "Failed to get reset GPIO\n");
	}

	ret = tps6131x_reset_chip(tps6131x);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to reset LED controller\n");

	ret = tps6131x_init_chip(tps6131x);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to initialize LED controller\n");

	ret = tps6131x_led_class_setup(tps6131x);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to setup LED class\n");

	ret = tps6131x_v4l2_setup(tps6131x);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to setup v4l2 flash\n");

	return 0;
}

static void tps6131x_remove(struct i2c_client *client)
{
	struct tps6131x *tps6131x = i2c_get_clientdata(client);

	v4l2_flash_release(tps6131x->v4l2_flash);

	cancel_delayed_work_sync(&tps6131x->torch_refresh_work);
}

static const struct of_device_id of_tps6131x_leds_match[] = {
	{ .compatible = "ti,tps61310" },
	{}
};
MODULE_DEVICE_TABLE(of, of_tps6131x_leds_match);

static struct i2c_driver tps6131x_i2c_driver = {
	.driver = {
		.name = "tps6131x",
		.of_match_table = of_tps6131x_leds_match,
	},
	.probe = tps6131x_probe,
	.remove = tps6131x_remove,
};
module_i2c_driver(tps6131x_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments TPS6131X flash LED driver");
MODULE_AUTHOR("Matthias Fend <matthias.fend@emfend.at>");
MODULE_LICENSE("GPL");
