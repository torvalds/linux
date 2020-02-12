// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */
#define pr_fmt(fmt)	"qti-flash: %s: " fmt, __func__

#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "leds.h"

#define FLASH_LED_STATUS1		0x06

#define FLASH_LED_STATUS2		0x07
#define  FLASH_LED_VPH_PWR_LOW	BIT(0)

#define FLASH_INT_RT_STS		0x10
#define  FLASH_LED_FAULT_RT_STS		BIT(0)
#define  FLASH_LED_ALL_RAMP_DN_DONE_RT_STS		BIT(3)
#define  FLASH_LED_ALL_RAMP_UP_DONE_RT_STS		BIT(4)

#define FLASH_LED_SAFETY_TIMER(id)		(0x3E + id)
#define  FLASH_LED_SAFETY_TIMER_EN_MASK		BIT(7)
#define  FLASH_LED_SAFETY_TIMER_EN		BIT(7)
#define  SAFETY_TIMER_MAX_TIMEOUT_MS		1280
#define  SAFETY_TIMER_MIN_TIMEOUT_MS		10
#define  SAFETY_TIMER_STEP_SIZE		10

/* Default timer duration is 200ms */
#define  SAFETY_TIMER_DEFAULT_DURATION		 0x13

#define FLASH_LED_ITARGET(id)		(0x42 + id)
#define  FLASH_LED_ITARGET_MASK		GENMASK(6, 0)

#define FLASH_ENABLE_CONTROL		0x46
#define  FLASH_MODULE_ENABLE		BIT(7)
#define  FLASH_MODULE_DISABLE		0x0

#define FLASH_LED_IRESOLUTION		0x49
#define  FLASH_LED_IRESOLUTION_MASK(id)		BIT(id)

#define FLASH_LED_STROBE_CTRL(id)	(0x4A + id)
#define  FLASH_LED_STROBE_CFG_MASK		GENMASK(6, 4)
#define  FLASH_LED_STROBE_CFG_SHIFT		4
#define  FLASH_LED_HW_SW_STROBE_SEL		BIT(2)
#define  FLASH_LED_STROBE_SEL_SHIFT		2

#define FLASH_EN_LED_CTRL		0x4E
#define  FLASH_LED_ENABLE(id)			BIT(id)
#define  FLASH_LED_DISABLE		0

#define MAX_IRES_LEVELS		2
#define IRES_12P5_MAX_CURR_MA	1500
#define IRES_5P0_MAX_CURR_MA		640
#define TORCH_MAX_CURR_MA		500
#define IRES_12P5_UA		12500
#define IRES_5P0_UA		5000
#define IRES_DEFAULT_UA		IRES_12P5_UA

enum flash_led_type {
	FLASH_LED_TYPE_UNKNOWN,
	FLASH_LED_TYPE_FLASH,
	FLASH_LED_TYPE_TORCH,
};

enum strobe_type {
	SW_STROBE = 0,
	HW_STROBE,
};

/* Configurations for each individual flash or torch device */
struct flash_node_data {
	struct qti_flash_led		*led;
	struct led_classdev_flash		fdev;
	u32				ires_ua;
	u32				default_ires_ua;
	u32				current_ma;
	u32				max_current;
	u8				duration;
	u8				id;
	u8				updated_ires_idx;
	u8				ires_idx;
	u8				strobe_config;
	u8				strobe_sel;
	enum flash_led_type		type;
	bool				configured;
	bool				enabled;
};

struct flash_switch_data {
	struct qti_flash_led		*led;
	struct led_classdev		cdev;
	u32				led_mask;
	bool				enabled;
	bool				symmetry_en;
};

/**
 * struct qti_flash_led: Main Flash LED data structure
 * @pdev		: Pointer for platform device
 * @regmap		: Pointer for regmap structure
 * @fnode		: Pointer for array of child LED devices
 * @snode		: Pointer for array of child switch devices
 * @lock		: Spinlock to be used for critical section
 * @num_fnodes		: Number of flash/torch nodes defined in device tree
 * @num_snodes		: Number of switch nodes defined in device tree
 * @hw_strobe_gpio		: Pointer for array of GPIOs for HW strobing
 * @all_ramp_up_done_irq		: IRQ number for all ramp up interrupt
 * @all_ramp_down_done_irq		: IRQ number for all ramp down interrupt
 * @led_fault_irq		: IRQ number for LED fault interrupt
 * @base		: Base address of the flash LED module
 * @max_channels	: Maximum number of channels supported by flash module
 * @ref_count		: Reference count used to enable/disable flash LED
 */
struct qti_flash_led {
	struct platform_device		*pdev;
	struct regmap		*regmap;
	struct flash_node_data		*fnode;
	struct flash_switch_data		*snode;
	spinlock_t		lock;
	u32			num_fnodes;
	u32			num_snodes;
	int			*hw_strobe_gpio;
	int			all_ramp_up_done_irq;
	int			all_ramp_down_done_irq;
	int			led_fault_irq;
	u16			base;
	u8		max_channels;
	u8		ref_count;
};

static const u32 flash_led_max_ires_values[MAX_IRES_LEVELS] = {
	IRES_5P0_MAX_CURR_MA, IRES_12P5_MAX_CURR_MA
};

static int timeout_to_code(u32 timeout)
{
	if (timeout < SAFETY_TIMER_MIN_TIMEOUT_MS ||
		timeout > SAFETY_TIMER_MAX_TIMEOUT_MS)
		return -EINVAL;

	return DIV_ROUND_CLOSEST(timeout, SAFETY_TIMER_STEP_SIZE) - 1;
}

static int get_ires_idx(u32 ires_ua)
{
	if (ires_ua == IRES_5P0_UA)
		return 0;
	else if (ires_ua == IRES_12P5_UA)
		return 1;
	else
		return -EINVAL;
}

static int current_to_code(u32 target_curr_ma, u32 ires_ua)
{
	if (!ires_ua || !target_curr_ma ||
		(target_curr_ma < DIV_ROUND_CLOSEST(ires_ua, 1000)))
		return 0;

	return DIV_ROUND_CLOSEST(target_curr_ma * 1000, ires_ua) - 1;
}

static int qti_flash_led_read(struct qti_flash_led *led, u16 offset,
				u8 *data, u8 len)
{
	int rc;
	u32 val;

	rc = regmap_bulk_read(led->regmap, (led->base + offset), &val, len);
	if (rc < 0) {
		pr_err("Failed to read from 0x%04X rc = %d\n",
			(led->base + offset), rc);
	} else {
		pr_debug("Read 0x%02X from addr 0x%04X\n", val,
			(led->base + offset));
		*data = (u8)val;
	}

	return rc;
}

static int qti_flash_led_write(struct qti_flash_led *led, u16 offset,
				u8 *data, u8 len)
{
	int rc;

	rc = regmap_bulk_write(led->regmap, (led->base + offset), data,
			len);
	if (rc < 0)
		pr_err("Failed to write to 0x%04X rc = %d\n",
			(led->base + offset), rc);
	else
		pr_debug("Wrote 0x%02X to addr 0x%04X\n", data,
			(led->base + offset));

	return rc;
}

static int qti_flash_led_masked_write(struct qti_flash_led *led,
					u16 offset, u8 mask, u8 data)
{
	int rc;

	rc = regmap_update_bits(led->regmap, (led->base + offset),
			mask, data);
	if (rc < 0)
		pr_err("Failed to update bits from 0x%04X, rc = %d\n",
			(led->base + offset), rc);
	else
		pr_debug("Wrote 0x%02X to addr 0x%04X\n", data,
			(led->base + offset));

	return rc;
}

static int qti_flash_led_strobe(struct flash_node_data *fnode,
				bool enable)
{
	struct qti_flash_led *led = fnode->led;
	int rc;
	u8 val;

	if (fnode->enabled == enable)
		return 0;

	spin_lock(&led->lock);

	if (enable) {
		if (!led->ref_count) {
			val = FLASH_MODULE_ENABLE;
			rc = qti_flash_led_write(led, FLASH_ENABLE_CONTROL,
					&val, 1);
			if (rc < 0)
				goto error;
		}
		led->ref_count++;

		rc = qti_flash_led_masked_write(led, FLASH_EN_LED_CTRL,
			FLASH_LED_ENABLE(fnode->id),
			FLASH_LED_ENABLE(fnode->id));
		if (rc < 0)
			goto error;
	} else {
		rc = qti_flash_led_masked_write(led, FLASH_EN_LED_CTRL,
			FLASH_LED_ENABLE(fnode->id),
			FLASH_LED_DISABLE);
		if (rc < 0)
			goto error;

		fnode->configured = false;

		if (led->ref_count)
			led->ref_count--;

		if (!led->ref_count) {
			val = FLASH_MODULE_DISABLE;
			rc = qti_flash_led_write(led, FLASH_ENABLE_CONTROL,
					&val, 1);
			if (rc < 0)
				goto error;
		}
	}

	if (!rc)
		fnode->enabled = enable;
error:
	spin_unlock(&led->lock);

	return rc;
}

static int qti_flash_led_enable(struct flash_node_data *fnode)
{
	struct qti_flash_led *led = fnode->led;
	int rc;
	u8 val, addr_offset;

	addr_offset = fnode->id;

	spin_lock(&led->lock);
	val = (fnode->updated_ires_idx ? 0 : 1) << fnode->id;
	rc = qti_flash_led_masked_write(led, FLASH_LED_IRESOLUTION,
		FLASH_LED_IRESOLUTION_MASK(fnode->id), val);
	if (rc < 0)
		goto out;

	rc = qti_flash_led_masked_write(led,
		FLASH_LED_ITARGET(addr_offset), FLASH_LED_ITARGET_MASK,
		current_to_code(fnode->current_ma, fnode->ires_ua));
	if (rc < 0)
		goto out;

	/*
	 * For dynamic brightness control of Torch LEDs,
	 * just configure the target current.
	 */
	if (fnode->type == FLASH_LED_TYPE_TORCH && fnode->enabled) {
		spin_unlock(&led->lock);
		return 0;
	}

	if (fnode->type == FLASH_LED_TYPE_FLASH && fnode->duration) {
		val = fnode->duration | FLASH_LED_SAFETY_TIMER_EN;
		rc = qti_flash_led_write(led,
			FLASH_LED_SAFETY_TIMER(addr_offset), &val, 1);
		if (rc < 0)
			goto out;
	} else {
		rc = qti_flash_led_masked_write(led,
			FLASH_LED_SAFETY_TIMER(addr_offset),
			FLASH_LED_SAFETY_TIMER_EN_MASK, 0);
		if (rc < 0)
			goto out;
	}

	fnode->configured = true;

	if ((fnode->strobe_sel == HW_STROBE) &&
		gpio_is_valid(led->hw_strobe_gpio[fnode->id]))
		gpio_set_value(led->hw_strobe_gpio[fnode->id], 1);

out:
	spin_unlock(&led->lock);
	return rc;
}

static int qti_flash_led_disable(struct flash_node_data *fnode)
{
	struct qti_flash_led *led = fnode->led;
	int rc;

	spin_lock(&led->lock);
	if ((fnode->strobe_sel == HW_STROBE) &&
		gpio_is_valid(led->hw_strobe_gpio[fnode->id]))
		gpio_set_value(led->hw_strobe_gpio[fnode->id], 0);

	rc = qti_flash_led_masked_write(led,
		FLASH_LED_ITARGET(fnode->id), FLASH_LED_ITARGET_MASK, 0);
	if (rc < 0)
		goto out;

	rc = qti_flash_led_masked_write(led,
		FLASH_LED_SAFETY_TIMER(fnode->id),
		FLASH_LED_SAFETY_TIMER_EN_MASK, 0);
	if (rc < 0)
		goto out;

	fnode->current_ma = 0;

out:
	spin_unlock(&led->lock);
	return rc;
}

static enum led_brightness qti_flash_led_brightness_get(
						struct led_classdev *led_cdev)
{
	return led_cdev->brightness;
}

static void qti_flash_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct qti_flash_led *led = NULL;
	struct flash_node_data *fnode = NULL;
	struct led_classdev_flash *fdev = NULL;
	int rc;
	u32 current_ma = brightness;
	u32 min_current_ma;

	fdev = container_of(led_cdev, struct led_classdev_flash, led_cdev);
	fnode = container_of(fdev, struct flash_node_data, fdev);
	led = fnode->led;

	if (brightness <= 0) {
		rc = qti_flash_led_disable(fnode);
		if (rc < 0)
			pr_err("Failed to set brightness %d to LED\n",
				brightness);
		return;
	}

	min_current_ma = DIV_ROUND_CLOSEST(fnode->ires_ua, 1000);
	if (current_ma < min_current_ma)
		current_ma = min_current_ma;

	fnode->updated_ires_idx = fnode->ires_idx;
	fnode->ires_ua = fnode->default_ires_ua;

	current_ma = min(current_ma, fnode->max_current);
	if (current_ma > flash_led_max_ires_values[fnode->ires_idx]) {
		if (current_ma > IRES_5P0_MAX_CURR_MA)
			fnode->ires_ua = IRES_12P5_UA;
		else
			fnode->ires_ua = IRES_5P0_UA;
		fnode->ires_idx = get_ires_idx(fnode->ires_ua);
	}

	fnode->current_ma = current_ma;
	led_cdev->brightness = current_ma;

	rc = qti_flash_led_enable(fnode);
	if (rc < 0)
		pr_err("Failed to set brightness %d to LED\n", brightness);
}

static int qti_flash_led_symmetry_config(
				struct flash_switch_data *snode)
{
	struct qti_flash_led *led = snode->led;
	int i, total_curr_ma = 0, symmetric_leds = 0, per_led_curr_ma;
	enum flash_led_type type = FLASH_LED_TYPE_UNKNOWN;

	/* Determine which LED type has triggered switch ON */
	for (i = 0; i < led->num_fnodes; i++) {
		if ((snode->led_mask & BIT(led->fnode[i].id)) &&
			(led->fnode[i].configured))
			type = led->fnode[i].type;
	}

	if (type == FLASH_LED_TYPE_UNKNOWN) {
		pr_err("Error in symmetry configuration for switch device\n");
		return -EINVAL;
	}

	for (i = 0; i < led->num_fnodes; i++) {
		if ((snode->led_mask & BIT(led->fnode[i].id)) &&
			(led->fnode[i].type == type)) {
			total_curr_ma += led->fnode[i].current_ma;
			symmetric_leds++;
		}
	}

	if (symmetric_leds > 0 && total_curr_ma > 0) {
		per_led_curr_ma = total_curr_ma / symmetric_leds;
	} else {
		pr_err("Incorrect configuration, symmetric_leds: %d total_curr_ma: %d\n",
			symmetric_leds, total_curr_ma);
		return -EINVAL;
	}

	if (per_led_curr_ma == 0) {
		pr_warn("per_led_curr_ma cannot be 0\n");
		return 0;
	}

	pr_debug("symmetric_leds: %d total: %d per_led_curr_ma: %d\n",
		symmetric_leds, total_curr_ma, per_led_curr_ma);

	for (i = 0; i < led->num_fnodes; i++) {
		if (snode->led_mask & BIT(led->fnode[i].id) &&
			led->fnode[i].type == type) {
			qti_flash_led_brightness_set(
				&led->fnode[i].fdev.led_cdev, per_led_curr_ma);
		}
	}

	return 0;
}

static void qti_flash_led_switch_brightness_set(
		struct led_classdev *led_cdev, enum led_brightness value)
{
	struct qti_flash_led *led = NULL;
	struct flash_switch_data *snode = NULL;
	int rc = 0, i;
	bool state = value > 0;

	snode = container_of(led_cdev, struct flash_switch_data, cdev);

	if (snode->enabled == state) {
		pr_debug("Switch  is already %s!\n",
			state ? "enabled" : "disabled");
		return;
	}

	led = snode->led;

	/* If symmetry enabled switch, then turn ON all its LEDs */
	if (state && snode->symmetry_en) {
		rc = qti_flash_led_symmetry_config(snode);
		if (rc < 0) {
			pr_err("Failed to configure switch symmetrically, rc=%d\n",
				rc);
			return;
		}
	}

	for (i = 0; i < led->num_fnodes; i++) {
		if (!(snode->led_mask & BIT(led->fnode[i].id)) ||
			!led->fnode[i].configured)
			continue;

		rc = qti_flash_led_strobe(&led->fnode[i], state);
		if (rc < 0) {
			pr_err("Failed to %s LED%d\n",
				state ? "strobe" : "destrobe",
				&led->fnode[i].id);
			break;
		}

		if (!state) {
			rc = qti_flash_led_disable(&led->fnode[i]);
			if (rc < 0) {
				pr_err("Failed to disable LED%d\n",
					&led->fnode[i].id);
				break;
			}
		}
	}

	if (!rc)
		snode->enabled = state;
}

static int qti_flash_brightness_set_blocking(
		struct led_classdev *led_cdev, enum led_brightness value)
{
	qti_flash_led_brightness_set(led_cdev, value);

	return 0;
}

static int qti_flash_brightness_set(
		struct led_classdev_flash *fdev, u32 brightness)
{
	qti_flash_led_brightness_set(&fdev->led_cdev, brightness);

	return 0;
}

static int qti_flash_brightness_get(
		struct led_classdev_flash *fdev, u32 *brightness)
{
	*brightness = qti_flash_led_brightness_get(&fdev->led_cdev);

	return 0;
}

static int qti_flash_strobe_set(struct led_classdev_flash *fdev,
				bool state)
{
	struct flash_node_data *fnode;

	fnode = container_of(fdev, struct flash_node_data, fdev);

	return qti_flash_led_strobe(fnode, state);
}

static int qti_flash_strobe_get(struct led_classdev_flash *fdev,
				bool *state)
{
	struct flash_node_data *fnode = container_of(fdev,
			struct flash_node_data, fdev);

	*state = fnode->enabled;

	return 0;
}

static int qti_flash_timeout_set(struct led_classdev_flash *fdev,
				u32 timeout)
{
	struct qti_flash_led *led;
	struct flash_node_data *fnode;
	int rc = 0;
	u8 val;

	if (timeout < SAFETY_TIMER_MIN_TIMEOUT_MS ||
		timeout > SAFETY_TIMER_MAX_TIMEOUT_MS)
		return -EINVAL;

	fnode = container_of(fdev, struct flash_node_data, fdev);
	led = fnode->led;

	rc = timeout_to_code(timeout);
	if (rc < 0)
		return rc;
	fnode->duration = rc;
	val = fnode->duration | FLASH_LED_SAFETY_TIMER_EN;
	rc = qti_flash_led_write(led,
			FLASH_LED_SAFETY_TIMER(fnode->id), &val, 1);

	return rc;
}

static const struct led_flash_ops flash_ops = {
	.flash_brightness_set	= qti_flash_brightness_set,
	.flash_brightness_get	= qti_flash_brightness_get,
	.strobe_set			= qti_flash_strobe_set,
	.strobe_get			= qti_flash_strobe_get,
	.timeout_set			= qti_flash_timeout_set,
};

static int qti_flash_led_setup(struct qti_flash_led *led)
{
	int rc = 0, i, addr_offset;
	u8 val, mask;

	for (i = 0; i < led->num_fnodes; i++) {
		addr_offset = led->fnode[i].id;
		rc = qti_flash_led_masked_write(led,
			FLASH_LED_SAFETY_TIMER(addr_offset),
			FLASH_LED_SAFETY_TIMER_EN_MASK, 0);
		if (rc < 0)
			return rc;

		val = (led->fnode[i].strobe_config <<
				FLASH_LED_STROBE_CFG_SHIFT) |
				(led->fnode[i].strobe_sel <<
				FLASH_LED_STROBE_SEL_SHIFT);
		mask = FLASH_LED_STROBE_CFG_MASK | FLASH_LED_HW_SW_STROBE_SEL;
		rc = qti_flash_led_masked_write(led,
			FLASH_LED_STROBE_CTRL(addr_offset), mask, val);
		if (rc < 0)
			return rc;
	}

	return rc;
}

static irqreturn_t qti_flash_led_irq_handler(int irq, void *_led)
{
	struct qti_flash_led *led = _led;
	int rc;
	u8 irq_status, led_status1, led_status2;

	rc = qti_flash_led_read(led, FLASH_INT_RT_STS, &irq_status, 1);
	if (rc < 0)
		goto exit;

	if (irq == led->led_fault_irq) {
		if (irq_status & FLASH_LED_FAULT_RT_STS)
			pr_debug("Led fault open/short/vreg_not_ready detected\n");
	} else if (irq == led->all_ramp_down_done_irq) {
		if (irq_status & FLASH_LED_ALL_RAMP_DN_DONE_RT_STS)
			pr_debug("All LED channels ramp down done detected\n");
	} else if (irq == led->all_ramp_up_done_irq) {
		if (irq_status & FLASH_LED_ALL_RAMP_UP_DONE_RT_STS)
			pr_debug("All LED channels ramp up done detected\n");
	}

	rc = qti_flash_led_read(led, FLASH_LED_STATUS1, &led_status1, 1);
	if (rc < 0)
		goto exit;

	if (led_status1)
		pr_debug("LED channel open/short fault detected\n");

	rc = qti_flash_led_read(led, FLASH_LED_STATUS2, &led_status2, 1);
	if (rc < 0)
		goto exit;

	if (led_status2 & FLASH_LED_VPH_PWR_LOW)
		pr_debug("LED vph_droop fault detected!\n");

	pr_debug("LED irq handled, irq_status=%02x led_status1=%02x led_status2=%02x\n",
			irq_status, led_status1, led_status2);

exit:
	return IRQ_HANDLED;
}

static int qti_flash_led_register_interrupts(struct qti_flash_led *led)
{
	int rc;

	rc = devm_request_threaded_irq(&led->pdev->dev,
		led->all_ramp_up_done_irq, NULL, qti_flash_led_irq_handler,
		IRQF_ONESHOT, "flash_all_ramp_up", led);
	if (rc < 0) {
		pr_err("Failed to request all_ramp_up_done(%d) IRQ(err:%d)\n",
			led->all_ramp_up_done_irq, rc);
		return rc;
	}

	rc = devm_request_threaded_irq(&led->pdev->dev,
		led->all_ramp_down_done_irq, NULL, qti_flash_led_irq_handler,
		IRQF_ONESHOT, "flash_all_ramp_down", led);
	if (rc < 0) {
		pr_err("Failed to request all_ramp_down_done(%d) IRQ(err:%d)\n",
			led->all_ramp_down_done_irq,
			rc);
		return rc;
	}

	rc = devm_request_threaded_irq(&led->pdev->dev,
		led->led_fault_irq, NULL, qti_flash_led_irq_handler,
		IRQF_ONESHOT, "flash_fault", led);
	if (rc < 0) {
		pr_err("Failed to request led_fault(%d) IRQ(err:%d)\n",
			led->led_fault_irq, rc);
		return rc;
	}

	return 0;
}

static int register_switch_device(struct qti_flash_led *led,
		struct flash_switch_data *snode, struct device_node *node)
{
	int rc;

	rc = of_property_read_string(node, "qcom,led-name",
				&snode->cdev.name);
	if (rc < 0) {
		pr_err("Failed to read switch node name, rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_string(node, "qcom,default-led-trigger",
					&snode->cdev.default_trigger);
	if (rc < 0) {
		pr_err("Failed to read trigger name, rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_u32(node, "qcom,led-mask", &snode->led_mask);
	if (rc < 0) {
		pr_err("Failed to read led mask rc=%d\n", rc);
		return rc;
	}
	if ((snode->led_mask > ((1 << led->max_channels) - 1))) {
		pr_err("Error, Invalid value for led-mask mask=0x%x\n",
			snode->led_mask);
		return -EINVAL;
	}

	snode->symmetry_en = of_property_read_bool(node, "qcom,symmetry-en");

	snode->led = led;
	snode->cdev.brightness_set = qti_flash_led_switch_brightness_set;
	snode->cdev.brightness_get = qti_flash_led_brightness_get;

	rc = devm_led_classdev_register(&led->pdev->dev, &snode->cdev);
	if (rc < 0) {
		pr_err("Failed to register led switch device:%s\n",
			snode->cdev.name);
		return rc;
	}

	return 0;
}

static int register_flash_device(struct qti_flash_led *led,
			struct flash_node_data *fnode, struct device_node *node)
{
	struct led_flash_setting *setting;
	const char *temp_string;
	int rc;
	u32 val, default_curr_ma;

	rc = of_property_read_string(node, "qcom,led-name",
					&fnode->fdev.led_cdev.name);
	if (rc < 0) {
		pr_err("Failed to read flash LED names\n");
		return rc;
	}

	rc = of_property_read_string(node, "label", &temp_string);
	if (rc < 0) {
		pr_err("Failed to read flash LED label\n");
		return rc;
	}

	if (!strcmp(temp_string, "flash")) {
		fnode->type = FLASH_LED_TYPE_FLASH;
	} else if (!strcmp(temp_string, "torch")) {
		fnode->type = FLASH_LED_TYPE_TORCH;
	} else {
		pr_err("Incorrect flash LED type %s\n", temp_string);
		return rc;
	}

	rc = of_property_read_u32(node, "qcom,id", &val);
	if (rc < 0) {
		pr_err("Failed to read flash LED ID\n");
		return rc;
	}
	fnode->id = (u8)val;

	rc = of_property_read_string(node, "qcom,default-led-trigger",
				&fnode->fdev.led_cdev.default_trigger);
	if (rc < 0) {
		pr_err("Failed to read trigger name\n");
		return rc;
	}

	rc = of_property_read_u32(node, "qcom,ires-ua", &val);
	if (rc < 0) {
		pr_err("Failed to read current resolution, rc=%d\n", rc);
		return rc;
	} else if (!rc) {
		rc = get_ires_idx(val);
		if (rc < 0) {
			pr_err("Incorrect ires-ua configured, ires-ua=%u\n",
				val);
			return rc;
		}
		fnode->default_ires_ua = fnode->ires_ua = val;
		fnode->updated_ires_idx = fnode->ires_idx = rc;
	}

	rc = of_property_read_u32(node, "qcom,max-current-ma", &val);
	if (rc < 0) {
		pr_err("Failed to read max current, rc=%d\n", rc);
		return rc;
	}

	if (fnode->type == FLASH_LED_TYPE_FLASH &&
		(val > IRES_12P5_MAX_CURR_MA)) {
		pr_err("Incorrect max-current-ma for flash %u\n",
				val);
		return -EINVAL;
	}

	if (fnode->type == FLASH_LED_TYPE_TORCH &&
		(val > TORCH_MAX_CURR_MA)) {
		pr_err("Incorrect max-current-ma for torch %u\n",
				val);
		return -EINVAL;
	}

	fnode->max_current = val;
	fnode->fdev.led_cdev.max_brightness = val;

	fnode->duration = SAFETY_TIMER_DEFAULT_DURATION;
	rc = of_property_read_u32(node, "qcom,duration-ms", &val);
	if (!rc) {
		rc = timeout_to_code(val);
		if (rc < 0) {
			pr_err("Incorrect timeout configured %u\n", val);
			return rc;
		}
		fnode->duration = rc;
	}

	fnode->strobe_sel = SW_STROBE;
	rc = of_property_read_u32(node, "qcom,strobe-sel", &val);
	if (!rc)
		fnode->strobe_sel = (u8)val;

	if (fnode->strobe_sel == HW_STROBE) {
		rc = of_property_read_u32(node, "qcom,strobe-config", &val);
		if (!rc) {
			fnode->strobe_config = (u8)val;
		} else {
			pr_err("Failed to read qcom,strobe-config property\n");
			return rc;
		}

	}

	fnode->led = led;
	fnode->fdev.led_cdev.brightness_set = qti_flash_led_brightness_set;
	fnode->fdev.led_cdev.brightness_get = qti_flash_led_brightness_get;
	fnode->enabled = false;
	fnode->configured = false;
	fnode->fdev.ops = &flash_ops;

	if (fnode->type == FLASH_LED_TYPE_FLASH) {
		fnode->fdev.led_cdev.flags = LED_DEV_CAP_FLASH;
		fnode->fdev.led_cdev.brightness_set_blocking =
				qti_flash_brightness_set_blocking;
	}

	default_curr_ma = DIV_ROUND_CLOSEST(fnode->ires_ua, 1000);
	setting = &fnode->fdev.brightness;
	setting->min = 0;
	setting->max = fnode->max_current;
	setting->step = 1;
	setting->val = default_curr_ma;

	setting = &fnode->fdev.timeout;
	setting->min = SAFETY_TIMER_MIN_TIMEOUT_MS;
	setting->max = SAFETY_TIMER_MAX_TIMEOUT_MS;
	setting->step = 1;
	setting->val = SAFETY_TIMER_DEFAULT_DURATION;

	rc = led_classdev_flash_register(&led->pdev->dev, &fnode->fdev);
	if (rc < 0) {
		pr_err("Failed to register flash led device:%s\n",
			fnode->fdev.led_cdev.name);
		return rc;
	}

	return 0;
}

static int qti_flash_led_register_device(struct qti_flash_led *led,
				struct device_node *node)
{
	struct device_node *temp;
	char buffer[20];
	const char *label;
	int rc, i = 0, j = 0;
	u32 val;

	rc = of_property_read_u32(node, "reg", &val);
	if (rc < 0) {
		pr_err("Failed to find reg in node %s, rc = %d\n",
			node->full_name, rc);
		return rc;
	}
	led->base = val;

	led->hw_strobe_gpio = devm_kcalloc(&led->pdev->dev,
			led->max_channels, sizeof(u32), GFP_KERNEL);
	if (!led->hw_strobe_gpio)
		return -ENOMEM;

	for (i = 0; i < led->max_channels; i++) {

		led->hw_strobe_gpio[i] = -EINVAL;

		rc = of_get_named_gpio(node, "hw-strobe-gpios", i);
		if (rc < 0) {
			pr_debug("Failed to get hw strobe gpio, rc = %d\n", rc);
			continue;
		}

		if (!gpio_is_valid(rc)) {
			pr_err("Error, Invalid gpio specified\n");
			return -EINVAL;
		}
		led->hw_strobe_gpio[i] = rc;

		scnprintf(buffer, sizeof(buffer), "hw_strobe_gpio%d", i);
		rc = devm_gpio_request_one(&led->pdev->dev,
				led->hw_strobe_gpio[i], GPIOF_DIR_OUT,
				buffer);
		if (rc < 0) {
			pr_err("Failed to acquire gpio rc = %d\n", rc);
			return rc;
		}

		gpio_direction_output(led->hw_strobe_gpio[i], 0);

	}

	led->all_ramp_up_done_irq = of_irq_get_byname(node,
			"all-ramp-up-done-irq");
	if (led->all_ramp_up_done_irq < 0)
		pr_debug("all-ramp-up-done-irq not used\n");

	led->all_ramp_down_done_irq = of_irq_get_byname(node,
			"all-ramp-down-done-irq");
	if (led->all_ramp_down_done_irq < 0)
		pr_debug("all-ramp-down-done-irq not used\n");

	led->led_fault_irq = of_irq_get_byname(node,
			"led-fault-irq");
	if (led->led_fault_irq < 0)
		pr_debug("led-fault-irq not used\n");

	for_each_available_child_of_node(node, temp) {
		rc = of_property_read_string(temp, "label", &label);
		if (rc < 0) {
			pr_err("Failed to parse label, rc=%d\n", rc);
			of_node_put(temp);
			return rc;
		}

		if (!strcmp("flash", label) || !strcmp("torch", label)) {
			led->num_fnodes++;
		} else if (!strcmp("switch", label)) {
			led->num_snodes++;
		} else {
			pr_err("Invalid label for led node label=%s\n",
					label);
			of_node_put(temp);
			return -EINVAL;
		}
	}

	if (!led->num_fnodes) {
		pr_err("No flash/torch devices defined\n");
		return -ECHILD;
	}

	if (!led->num_snodes) {
		pr_err("No switch devices defined\n");
		return -ECHILD;
	}

	led->fnode = devm_kcalloc(&led->pdev->dev, led->num_fnodes,
				sizeof(*led->fnode), GFP_KERNEL);
	led->snode = devm_kcalloc(&led->pdev->dev, led->num_snodes,
				sizeof(*led->snode), GFP_KERNEL);
	if ((!led->fnode) || (!led->snode))
		return -ENOMEM;

	i = 0;
	for_each_available_child_of_node(node, temp) {
		rc = of_property_read_string(temp, "label", &label);
		if (rc < 0) {
			pr_err("Failed to parse label, rc=%d\n", rc);
			of_node_put(temp);
			return rc;
		}

		if (!strcmp("flash", label) || !strcmp("torch", label)) {
			rc = register_flash_device(led, &led->fnode[i], temp);
			if (rc < 0) {
				pr_err("Failed to register flash device %s rc=%d\n",
					led->fnode[i].fdev.led_cdev.name, rc);
				of_node_put(temp);
				goto unreg_led;
			}
			led->fnode[i++].fdev.led_cdev.dev->of_node = temp;
		} else {
			rc = register_switch_device(led, &led->snode[j], temp);
			if (rc < 0) {
				pr_err("Failed to register switch device %s rc=%d\n",
					led->snode[j].cdev.name, rc);
				i--;
				of_node_put(temp);
				goto unreg_led;
			}
			led->snode[j++].cdev.dev->of_node = temp;
		}
	}

	return 0;

unreg_led:
	while (i >= 0)
		led_classdev_flash_unregister(&led->fnode[i--].fdev);

	return rc;
}

static int qti_flash_led_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct qti_flash_led *led;
	int rc;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!led->regmap) {
		pr_err("Failed to get parent's regmap\n");
		return -EINVAL;
	}

	led->max_channels = (u8)(uintptr_t)of_device_get_match_data(&pdev->dev);
	if (!led->max_channels) {
		pr_err("Failed to get max supported led channels\n");
		return -EINVAL;
	}

	led->pdev = pdev;
	spin_lock_init(&led->lock);

	rc = qti_flash_led_register_device(led, node);
	if (rc < 0) {
		pr_err("Failed to parse and register LED devices rc=%d\n", rc);
		return rc;
	}

	rc = qti_flash_led_setup(led);
	if (rc < 0) {
		pr_err("Failed to initialize flash LED, rc=%d\n", rc);
		return rc;
	}

	rc = qti_flash_led_register_interrupts(led);
	if (rc < 0) {
		pr_err("Failed to register LED interrupts rc=%d\n", rc);
		return rc;
	}

	dev_set_drvdata(&pdev->dev, led);

	return 0;
}

static int qti_flash_led_remove(struct platform_device *pdev)
{
	struct qti_flash_led *led = dev_get_drvdata(&pdev->dev);
	int i;

	for (i = 0; (i < led->num_snodes); i++)
		led_classdev_unregister(&led->snode[i].cdev);

	for (i = 0; (i < led->num_fnodes); i++)
		led_classdev_flash_unregister(&led->fnode[i].fdev);

	return 0;
}

static const struct of_device_id qti_flash_led_match_table[] = {
	{ .compatible = "qcom,pm8350c-flash-led", .data = (void *)4, },
	{ },
};

static struct platform_driver qti_flash_led_driver = {
	.driver = {
		.name = "leds-qti-flash",
		.of_match_table = qti_flash_led_match_table,
	},
	.probe = qti_flash_led_probe,
	.remove = qti_flash_led_remove,
};

module_platform_driver(qti_flash_led_driver);

MODULE_DESCRIPTION("QTI Flash LED driver");
MODULE_LICENSE("GPL v2");
