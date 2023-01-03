// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Lightning Mountain SoC LED Serial Shift Output Controller driver
 *
 * Copyright (c) 2020 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/uaccess.h>

#define SSO_DEV_NAME			"lgm-sso"

#define LED_BLINK_H8_0			0x0
#define LED_BLINK_H8_1			0x4
#define GET_FREQ_OFFSET(pin, src)	(((pin) * 6) + ((src) * 2))
#define GET_SRC_OFFSET(pinc)		(((pin) * 6) + 4)

#define DUTY_CYCLE(x)			(0x8 + ((x) * 4))
#define SSO_CON0			0x2B0
#define SSO_CON0_RZFL			BIT(26)
#define SSO_CON0_BLINK_R		BIT(30)
#define SSO_CON0_SWU			BIT(31)

#define SSO_CON1			0x2B4
#define SSO_CON1_FCDSC			GENMASK(21, 20) /* Fixed Divider Shift Clock */
#define SSO_CON1_FPID			GENMASK(24, 23)
#define SSO_CON1_GPTD			GENMASK(26, 25)
#define SSO_CON1_US			GENMASK(31, 30)

#define SSO_CPU				0x2B8
#define SSO_CON2			0x2C4
#define SSO_CON3			0x2C8

/* Driver MACRO */
#define MAX_PIN_NUM_PER_BANK		SZ_32
#define MAX_GROUP_NUM			SZ_4
#define PINS_PER_GROUP			SZ_8
#define FPID_FREQ_RANK_MAX		SZ_4
#define SSO_LED_MAX_NUM			SZ_32
#define MAX_FREQ_RANK			10
#define DEF_GPTC_CLK_RATE		200000000
#define SSO_DEF_BRIGHTNESS		LED_HALF
#define DATA_CLK_EDGE			0 /* 0-rising, 1-falling */

static const u32 freq_div_tbl[] = {4000, 2000, 1000, 800};
static const int freq_tbl[] = {2, 4, 8, 10, 50000, 100000, 200000, 250000};
static const int shift_clk_freq_tbl[] = {25000000, 12500000, 6250000, 3125000};

/*
 * Update Source to update the SOUTs
 * SW - Software has to update the SWU bit
 * GPTC - General Purpose timer is used as clock source
 * FPID - Divided FSC clock (FPID) is used as clock source
 */
enum {
	US_SW = 0,
	US_GPTC = 1,
	US_FPID = 2
};

enum {
	MAX_FPID_FREQ_RANK = 5, /* 1 to 4 */
	MAX_GPTC_FREQ_RANK = 9, /* 5 to 8 */
	MAX_GPTC_HS_FREQ_RANK = 10, /* 9 to 10 */
};

enum {
	LED_GRP0_PIN_MAX = 24,
	LED_GRP1_PIN_MAX = 29,
	LED_GRP2_PIN_MAX = 32,
};

enum {
	LED_GRP0_0_23,
	LED_GRP1_24_28,
	LED_GRP2_29_31,
	LED_GROUP_MAX,
};

enum {
	CLK_SRC_FPID = 0,
	CLK_SRC_GPTC = 1,
	CLK_SRC_GPTC_HS = 2,
};

struct sso_led_priv;

struct sso_led_desc {
	const char *name;
	const char *default_trigger;
	unsigned int brightness;
	unsigned int blink_rate;
	unsigned int retain_state_suspended:1;
	unsigned int retain_state_shutdown:1;
	unsigned int panic_indicator:1;
	unsigned int hw_blink:1;
	unsigned int hw_trig:1;
	unsigned int blinking:1;
	int freq_idx;
	u32 pin;
};

struct sso_led {
	struct list_head list;
	struct led_classdev cdev;
	struct gpio_desc *gpiod;
	struct sso_led_desc desc;
	struct sso_led_priv *priv;
};

struct sso_gpio {
	struct gpio_chip chip;
	int shift_clk_freq;
	int edge;
	int freq;
	u32 pins;
	u32 alloc_bitmap;
};

struct sso_led_priv {
	struct regmap *mmap;
	struct device *dev;
	struct platform_device *pdev;
	struct clk_bulk_data clocks[2];
	u32 fpid_clkrate;
	u32 gptc_clkrate;
	u32 freq[MAX_FREQ_RANK];
	struct list_head led_list;
	struct sso_gpio gpio;
};

static int sso_get_blink_rate_idx(struct sso_led_priv *priv, u32 rate)
{
	int i;

	for (i = 0; i < MAX_FREQ_RANK; i++) {
		if (rate <= priv->freq[i])
			return i;
	}

	return -1;
}

static unsigned int sso_led_pin_to_group(u32 pin)
{
	if (pin < LED_GRP0_PIN_MAX)
		return LED_GRP0_0_23;
	else if (pin < LED_GRP1_PIN_MAX)
		return LED_GRP1_24_28;
	else
		return LED_GRP2_29_31;
}

static u32 sso_led_get_freq_src(int freq_idx)
{
	if (freq_idx < MAX_FPID_FREQ_RANK)
		return CLK_SRC_FPID;
	else if (freq_idx < MAX_GPTC_FREQ_RANK)
		return CLK_SRC_GPTC;
	else
		return CLK_SRC_GPTC_HS;
}

static u32 sso_led_pin_blink_off(u32 pin, unsigned int group)
{
	if (group == LED_GRP2_29_31)
		return pin - LED_GRP1_PIN_MAX;
	else if (group == LED_GRP1_24_28)
		return pin - LED_GRP0_PIN_MAX;
	else	/* led 0 - 23 in led 32 location */
		return SSO_LED_MAX_NUM - LED_GRP1_PIN_MAX;
}

static struct sso_led
*cdev_to_sso_led_data(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct sso_led, cdev);
}

static void sso_led_freq_set(struct sso_led_priv *priv, u32 pin, int freq_idx)
{
	u32 reg, off, freq_src, val_freq;
	u32 low, high, val;
	unsigned int group;

	if (!freq_idx)
		return;

	group = sso_led_pin_to_group(pin);
	freq_src = sso_led_get_freq_src(freq_idx);
	off = sso_led_pin_blink_off(pin, group);

	if (group == LED_GRP0_0_23)
		return;
	else if (group == LED_GRP1_24_28)
		reg = LED_BLINK_H8_0;
	else
		reg = LED_BLINK_H8_1;

	if (freq_src == CLK_SRC_FPID)
		val_freq = freq_idx - 1;
	else if (freq_src == CLK_SRC_GPTC)
		val_freq = freq_idx - MAX_FPID_FREQ_RANK;

	/* set blink rate idx */
	if (freq_src != CLK_SRC_GPTC_HS) {
		low = GET_FREQ_OFFSET(off, freq_src);
		high = low + 2;
		val = val_freq << high;
		regmap_update_bits(priv->mmap, reg, GENMASK(high, low), val);
	}

	/* select clock source */
	low = GET_SRC_OFFSET(off);
	high = low + 2;
	val = freq_src << high;
	regmap_update_bits(priv->mmap, reg, GENMASK(high, low), val);
}

static void sso_led_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct sso_led_priv *priv;
	struct sso_led_desc *desc;
	struct sso_led *led;
	int val;

	led = cdev_to_sso_led_data(led_cdev);
	priv = led->priv;
	desc = &led->desc;

	desc->brightness = brightness;
	regmap_write(priv->mmap, DUTY_CYCLE(desc->pin), brightness);

	if (brightness == LED_OFF)
		val = 0;
	else
		val = 1;

	/* HW blink off */
	if (desc->hw_blink && !val && desc->blinking) {
		desc->blinking = 0;
		regmap_update_bits(priv->mmap, SSO_CON2, BIT(desc->pin), 0);
	} else if (desc->hw_blink && val && !desc->blinking) {
		desc->blinking = 1;
		regmap_update_bits(priv->mmap, SSO_CON2, BIT(desc->pin),
				   1 << desc->pin);
	}

	if (!desc->hw_trig)
		gpiod_set_value(led->gpiod, val);
}

static enum led_brightness sso_led_brightness_get(struct led_classdev *led_cdev)
{
	struct sso_led *led = cdev_to_sso_led_data(led_cdev);

	return (enum led_brightness)led->desc.brightness;
}

static int
delay_to_freq_idx(struct sso_led *led, unsigned long *delay_on,
		  unsigned long *delay_off)
{
	struct sso_led_priv *priv = led->priv;
	unsigned long delay;
	int freq_idx;
	u32 freq;

	if (!*delay_on && !*delay_off) {
		*delay_on = *delay_off = (1000 / priv->freq[0]) / 2;
		return 0;
	}

	delay = *delay_on + *delay_off;
	freq = 1000 / delay;

	freq_idx = sso_get_blink_rate_idx(priv, freq);
	if (freq_idx == -1)
		freq_idx = MAX_FREQ_RANK - 1;

	delay = 1000 / priv->freq[freq_idx];
	*delay_on = *delay_off = delay / 2;

	if (!*delay_on)
		*delay_on = *delay_off = 1;

	return freq_idx;
}

static int
sso_led_blink_set(struct led_classdev *led_cdev, unsigned long *delay_on,
		  unsigned long *delay_off)
{
	struct sso_led_priv *priv;
	struct sso_led *led;
	int freq_idx;

	led = cdev_to_sso_led_data(led_cdev);
	priv = led->priv;
	freq_idx = delay_to_freq_idx(led, delay_on, delay_off);

	sso_led_freq_set(priv, led->desc.pin, freq_idx);
	regmap_update_bits(priv->mmap, SSO_CON2, BIT(led->desc.pin),
			   1 << led->desc.pin);
	led->desc.freq_idx = freq_idx;
	led->desc.blink_rate = priv->freq[freq_idx];
	led->desc.blinking = 1;

	return 1;
}

static void sso_led_hw_cfg(struct sso_led_priv *priv, struct sso_led *led)
{
	struct sso_led_desc *desc = &led->desc;

	/* set freq */
	if (desc->hw_blink) {
		sso_led_freq_set(priv, desc->pin, desc->freq_idx);
		regmap_update_bits(priv->mmap, SSO_CON2, BIT(desc->pin),
				   1 << desc->pin);
	}

	if (desc->hw_trig)
		regmap_update_bits(priv->mmap, SSO_CON3, BIT(desc->pin),
				   1 << desc->pin);

	/* set brightness */
	regmap_write(priv->mmap, DUTY_CYCLE(desc->pin), desc->brightness);

	/* enable output */
	if (!desc->hw_trig && desc->brightness)
		gpiod_set_value(led->gpiod, 1);
}

static int sso_create_led(struct sso_led_priv *priv, struct sso_led *led,
			  struct fwnode_handle *child)
{
	struct sso_led_desc *desc = &led->desc;
	struct led_init_data init_data;
	int err;

	init_data.fwnode = child;
	init_data.devicename = SSO_DEV_NAME;
	init_data.default_label = ":";

	led->cdev.default_trigger = desc->default_trigger;
	led->cdev.brightness_set = sso_led_brightness_set;
	led->cdev.brightness_get = sso_led_brightness_get;
	led->cdev.brightness = desc->brightness;
	led->cdev.max_brightness = LED_FULL;

	if (desc->retain_state_shutdown)
		led->cdev.flags |= LED_RETAIN_AT_SHUTDOWN;
	if (desc->retain_state_suspended)
		led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	if (desc->panic_indicator)
		led->cdev.flags |= LED_PANIC_INDICATOR;

	if (desc->hw_blink)
		led->cdev.blink_set = sso_led_blink_set;

	sso_led_hw_cfg(priv, led);

	err = devm_led_classdev_register_ext(priv->dev, &led->cdev, &init_data);
	if (err)
		return err;

	list_add(&led->list, &priv->led_list);

	return 0;
}

static void sso_init_freq(struct sso_led_priv *priv)
{
	int i;

	priv->freq[0] = 0;
	for (i = 1; i < MAX_FREQ_RANK; i++) {
		if (i < MAX_FPID_FREQ_RANK) {
			priv->freq[i] = priv->fpid_clkrate / freq_div_tbl[i - 1];
		} else if (i < MAX_GPTC_FREQ_RANK) {
			priv->freq[i] = priv->gptc_clkrate /
				freq_div_tbl[i - MAX_FPID_FREQ_RANK];
		} else if (i < MAX_GPTC_HS_FREQ_RANK) {
			priv->freq[i] = priv->gptc_clkrate;
		}
	}
}

static int sso_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct sso_led_priv *priv = gpiochip_get_data(chip);

	if (priv->gpio.alloc_bitmap & BIT(offset))
		return -EINVAL;

	priv->gpio.alloc_bitmap |= BIT(offset);
	regmap_write(priv->mmap, DUTY_CYCLE(offset), 0xFF);

	return 0;
}

static void sso_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct sso_led_priv *priv = gpiochip_get_data(chip);

	priv->gpio.alloc_bitmap &= ~BIT(offset);
	regmap_write(priv->mmap, DUTY_CYCLE(offset), 0x0);
}

static int sso_gpio_get_dir(struct gpio_chip *chip, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int
sso_gpio_dir_out(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct sso_led_priv *priv = gpiochip_get_data(chip);
	bool bit = !!value;

	regmap_update_bits(priv->mmap, SSO_CPU, BIT(offset), bit << offset);
	if (!priv->gpio.freq)
		regmap_update_bits(priv->mmap, SSO_CON0, SSO_CON0_SWU,
				   SSO_CON0_SWU);

	return 0;
}

static int sso_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sso_led_priv *priv = gpiochip_get_data(chip);
	u32 reg_val;

	regmap_read(priv->mmap, SSO_CPU, &reg_val);

	return !!(reg_val & BIT(offset));
}

static void sso_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct sso_led_priv *priv = gpiochip_get_data(chip);

	regmap_update_bits(priv->mmap, SSO_CPU, BIT(offset), value << offset);
	if (!priv->gpio.freq)
		regmap_update_bits(priv->mmap, SSO_CON0, SSO_CON0_SWU,
				   SSO_CON0_SWU);
}

static int sso_gpio_gc_init(struct device *dev, struct sso_led_priv *priv)
{
	struct gpio_chip *gc = &priv->gpio.chip;

	gc->request             = sso_gpio_request;
	gc->free                = sso_gpio_free;
	gc->get_direction       = sso_gpio_get_dir;
	gc->direction_output    = sso_gpio_dir_out;
	gc->get                 = sso_gpio_get;
	gc->set                 = sso_gpio_set;

	gc->label               = "lgm-sso";
	gc->base                = -1;
	/* To exclude pins from control, use "gpio-reserved-ranges" */
	gc->ngpio               = priv->gpio.pins;
	gc->parent              = dev;
	gc->owner               = THIS_MODULE;

	return devm_gpiochip_add_data(dev, gc, priv);
}

static int sso_gpio_get_freq_idx(int freq)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(freq_tbl); idx++) {
		if (freq <= freq_tbl[idx])
			return idx;
	}

	return -1;
}

static void sso_register_shift_clk(struct sso_led_priv *priv)
{
	int idx, size = ARRAY_SIZE(shift_clk_freq_tbl);
	u32 val = 0;

	for (idx = 0; idx < size; idx++) {
		if (shift_clk_freq_tbl[idx] <= priv->gpio.shift_clk_freq) {
			val = idx;
			break;
		}
	}

	if (idx == size)
		dev_warn(priv->dev, "%s: Invalid freq %d\n",
			 __func__, priv->gpio.shift_clk_freq);

	regmap_update_bits(priv->mmap, SSO_CON1, SSO_CON1_FCDSC,
			   FIELD_PREP(SSO_CON1_FCDSC, val));
}

static int sso_gpio_freq_set(struct sso_led_priv *priv)
{
	int freq_idx;
	u32 val;

	freq_idx = sso_gpio_get_freq_idx(priv->gpio.freq);
	if (freq_idx == -1)
		freq_idx = ARRAY_SIZE(freq_tbl) - 1;

	val = freq_idx % FPID_FREQ_RANK_MAX;

	if (!priv->gpio.freq) {
		regmap_update_bits(priv->mmap, SSO_CON0, SSO_CON0_BLINK_R, 0);
		regmap_update_bits(priv->mmap, SSO_CON1, SSO_CON1_US,
				   FIELD_PREP(SSO_CON1_US, US_SW));
	} else if (freq_idx < FPID_FREQ_RANK_MAX) {
		regmap_update_bits(priv->mmap, SSO_CON0, SSO_CON0_BLINK_R,
				   SSO_CON0_BLINK_R);
		regmap_update_bits(priv->mmap, SSO_CON1, SSO_CON1_US,
				   FIELD_PREP(SSO_CON1_US, US_FPID));
		regmap_update_bits(priv->mmap, SSO_CON1, SSO_CON1_FPID,
				   FIELD_PREP(SSO_CON1_FPID, val));
	} else {
		regmap_update_bits(priv->mmap, SSO_CON0, SSO_CON0_BLINK_R,
				   SSO_CON0_BLINK_R);
		regmap_update_bits(priv->mmap, SSO_CON1, SSO_CON1_US,
				   FIELD_PREP(SSO_CON1_US, US_GPTC));
		regmap_update_bits(priv->mmap, SSO_CON1, SSO_CON1_GPTD,
				   FIELD_PREP(SSO_CON1_GPTD, val));
	}

	return 0;
}

static int sso_gpio_hw_init(struct sso_led_priv *priv)
{
	u32 activate;
	int i, err;

	/* Clear all duty cycles */
	for (i = 0; i < priv->gpio.pins; i++) {
		err = regmap_write(priv->mmap, DUTY_CYCLE(i), 0);
		if (err)
			return err;
	}

	/* 4 groups for total 32 pins */
	for (i = 1; i <= MAX_GROUP_NUM; i++) {
		activate = !!(i * PINS_PER_GROUP <= priv->gpio.pins ||
			      priv->gpio.pins > (i - 1) * PINS_PER_GROUP);
		err = regmap_update_bits(priv->mmap, SSO_CON1, BIT(i - 1),
					 activate << (i - 1));
		if (err)
			return err;
	}

	/* NO HW directly controlled pin by default */
	err = regmap_write(priv->mmap, SSO_CON3, 0);
	if (err)
		return err;

	/* NO BLINK for all pins */
	err = regmap_write(priv->mmap, SSO_CON2, 0);
	if (err)
		return err;

	/* OUTPUT 0 by default */
	err = regmap_write(priv->mmap, SSO_CPU, 0);
	if (err)
		return err;

	/* update edge */
	err = regmap_update_bits(priv->mmap, SSO_CON0, SSO_CON0_RZFL,
				 FIELD_PREP(SSO_CON0_RZFL, priv->gpio.edge));
	if (err)
		return err;

	/* Set GPIO update rate */
	sso_gpio_freq_set(priv);

	/* Register shift clock */
	sso_register_shift_clk(priv);

	return 0;
}

static void sso_led_shutdown(struct sso_led *led)
{
	struct sso_led_priv *priv = led->priv;

	/* unregister led */
	devm_led_classdev_unregister(priv->dev, &led->cdev);

	/* clear HW control bit */
	if (led->desc.hw_trig)
		regmap_update_bits(priv->mmap, SSO_CON3, BIT(led->desc.pin), 0);

	led->priv = NULL;
}

static int
__sso_led_dt_parse(struct sso_led_priv *priv, struct fwnode_handle *fw_ssoled)
{
	struct fwnode_handle *fwnode_child;
	struct device *dev = priv->dev;
	struct sso_led_desc *desc;
	struct sso_led *led;
	const char *tmp;
	u32 prop;
	int ret;

	fwnode_for_each_child_node(fw_ssoled, fwnode_child) {
		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led) {
			ret = -ENOMEM;
			goto __dt_err;
		}

		INIT_LIST_HEAD(&led->list);
		led->priv = priv;
		desc = &led->desc;

		led->gpiod = devm_fwnode_gpiod_get(dev, fwnode_child, NULL,
						   GPIOD_ASIS, NULL);
		if (IS_ERR(led->gpiod)) {
			ret = dev_err_probe(dev, PTR_ERR(led->gpiod), "led: get gpio fail!\n");
			goto __dt_err;
		}

		fwnode_property_read_string(fwnode_child,
					    "linux,default-trigger",
					    &desc->default_trigger);

		if (fwnode_property_present(fwnode_child,
					    "retain-state-suspended"))
			desc->retain_state_suspended = 1;

		if (fwnode_property_present(fwnode_child,
					    "retain-state-shutdown"))
			desc->retain_state_shutdown = 1;

		if (fwnode_property_present(fwnode_child, "panic-indicator"))
			desc->panic_indicator = 1;

		ret = fwnode_property_read_u32(fwnode_child, "reg", &prop);
		if (ret)
			goto __dt_err;
		if (prop >= SSO_LED_MAX_NUM) {
			dev_err(dev, "invalid LED pin:%u\n", prop);
			ret = -EINVAL;
			goto __dt_err;
		}
		desc->pin = prop;

		if (fwnode_property_present(fwnode_child, "intel,sso-hw-blink"))
			desc->hw_blink = 1;

		desc->hw_trig = fwnode_property_read_bool(fwnode_child,
							  "intel,sso-hw-trigger");
		if (desc->hw_trig) {
			desc->default_trigger = NULL;
			desc->retain_state_shutdown = 0;
			desc->retain_state_suspended = 0;
			desc->panic_indicator = 0;
			desc->hw_blink = 0;
		}

		if (fwnode_property_read_u32(fwnode_child,
					     "intel,sso-blink-rate-hz", &prop)) {
			/* default first freq rate */
			desc->freq_idx = 0;
			desc->blink_rate = priv->freq[desc->freq_idx];
		} else {
			desc->freq_idx = sso_get_blink_rate_idx(priv, prop);
			if (desc->freq_idx == -1)
				desc->freq_idx = MAX_FREQ_RANK - 1;

			desc->blink_rate = priv->freq[desc->freq_idx];
		}

		if (!fwnode_property_read_string(fwnode_child, "default-state", &tmp)) {
			if (!strcmp(tmp, "on"))
				desc->brightness = LED_FULL;
		}

		ret = sso_create_led(priv, led, fwnode_child);
		if (ret)
			goto __dt_err;
	}

	return 0;

__dt_err:
	fwnode_handle_put(fwnode_child);
	/* unregister leds */
	list_for_each_entry(led, &priv->led_list, list)
		sso_led_shutdown(led);

	return ret;
}

static int sso_led_dt_parse(struct sso_led_priv *priv)
{
	struct fwnode_handle *fwnode = dev_fwnode(priv->dev);
	struct fwnode_handle *fw_ssoled;
	struct device *dev = priv->dev;
	int count;
	int ret;

	count = device_get_child_node_count(dev);
	if (!count)
		return 0;

	fw_ssoled = fwnode_get_named_child_node(fwnode, "ssoled");
	if (fw_ssoled) {
		ret = __sso_led_dt_parse(priv, fw_ssoled);
		fwnode_handle_put(fw_ssoled);
		if (ret)
			return ret;
	}

	return 0;
}

static int sso_probe_gpios(struct sso_led_priv *priv)
{
	struct device *dev = priv->dev;
	int ret;

	if (device_property_read_u32(dev, "ngpios", &priv->gpio.pins))
		priv->gpio.pins = MAX_PIN_NUM_PER_BANK;

	if (priv->gpio.pins > MAX_PIN_NUM_PER_BANK)
		return -EINVAL;

	if (device_property_read_u32(dev, "intel,sso-update-rate-hz",
				     &priv->gpio.freq))
		priv->gpio.freq = 0;

	priv->gpio.edge = DATA_CLK_EDGE;
	priv->gpio.shift_clk_freq = -1;

	ret = sso_gpio_hw_init(priv);
	if (ret)
		return ret;

	return sso_gpio_gc_init(dev, priv);
}

static void sso_clock_disable_unprepare(void *data)
{
	struct sso_led_priv *priv = data;

	clk_bulk_disable_unprepare(ARRAY_SIZE(priv->clocks), priv->clocks);
}

static int intel_sso_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sso_led_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	priv->dev = dev;

	/* gate clock */
	priv->clocks[0].id = "sso";

	/* fpid clock */
	priv->clocks[1].id = "fpid";

	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(priv->clocks), priv->clocks);
	if (ret) {
		dev_err(dev, "Getting clocks failed!\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(priv->clocks), priv->clocks);
	if (ret) {
		dev_err(dev, "Failed to prepare and enable clocks!\n");
		return ret;
	}

	ret = devm_add_action_or_reset(dev, sso_clock_disable_unprepare, priv);
	if (ret)
		return ret;

	priv->fpid_clkrate = clk_get_rate(priv->clocks[1].clk);

	priv->mmap = syscon_node_to_regmap(dev->of_node);

	priv->mmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(priv->mmap)) {
		dev_err(dev, "Failed to map iomem!\n");
		return PTR_ERR(priv->mmap);
	}

	ret = sso_probe_gpios(priv);
	if (ret) {
		regmap_exit(priv->mmap);
		return ret;
	}

	INIT_LIST_HEAD(&priv->led_list);

	platform_set_drvdata(pdev, priv);
	sso_init_freq(priv);

	priv->gptc_clkrate = DEF_GPTC_CLK_RATE;

	ret = sso_led_dt_parse(priv);
	if (ret) {
		regmap_exit(priv->mmap);
		return ret;
	}
	dev_info(priv->dev, "sso LED init success!\n");

	return 0;
}

static int intel_sso_led_remove(struct platform_device *pdev)
{
	struct sso_led_priv *priv;
	struct sso_led *led, *n;

	priv = platform_get_drvdata(pdev);

	list_for_each_entry_safe(led, n, &priv->led_list, list) {
		list_del(&led->list);
		sso_led_shutdown(led);
	}

	regmap_exit(priv->mmap);

	return 0;
}

static const struct of_device_id of_sso_led_match[] = {
	{ .compatible = "intel,lgm-ssoled" },
	{}
};

MODULE_DEVICE_TABLE(of, of_sso_led_match);

static struct platform_driver intel_sso_led_driver = {
	.probe		= intel_sso_led_probe,
	.remove		= intel_sso_led_remove,
	.driver		= {
			.name = "lgm-ssoled",
			.of_match_table = of_sso_led_match,
	},
};

module_platform_driver(intel_sso_led_driver);

MODULE_DESCRIPTION("Intel SSO LED/GPIO driver");
MODULE_LICENSE("GPL v2");
