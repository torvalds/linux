// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LED driver for Mediatek MT6323 PMIC
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 */
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/*
 * Register field for TOP_CKPDN0 to enable
 * 32K clock common for LED device.
 */
#define RG_DRV_32K_CK_PDN		BIT(11)
#define RG_DRV_32K_CK_PDN_MASK		BIT(11)

/* 32K/1M/6M clock common for WLED device */
#define RG_VWLED_1M_CK_PDN		BIT(0)
#define RG_VWLED_32K_CK_PDN		BIT(12)
#define RG_VWLED_6M_CK_PDN		BIT(13)

/*
 * Register field for TOP_CKPDN2 to enable
 * individual clock for LED device.
 */
#define RG_ISINK_CK_PDN(i)	BIT(i)
#define RG_ISINK_CK_PDN_MASK(i)	BIT(i)

/*
 * Register field for TOP_CKCON1 to select
 * clock source.
 */
#define RG_ISINK_CK_SEL_MASK(i)	(BIT(10) << (i))

#define ISINK_CON(r, i)		(r + 0x8 * (i))

/* ISINK_CON0: Register to setup the duty cycle of the blink. */
#define ISINK_DIM_DUTY_MASK	(0x1f << 8)
#define ISINK_DIM_DUTY(i)	(((i) << 8) & ISINK_DIM_DUTY_MASK)

/* ISINK_CON1: Register to setup the period of the blink. */
#define ISINK_DIM_FSEL_MASK	(0xffff)
#define ISINK_DIM_FSEL(i)	((i) & ISINK_DIM_FSEL_MASK)

/* ISINK_CON2: Register to control the brightness. */
#define ISINK_CH_STEP_SHIFT	12
#define ISINK_CH_STEP_MASK	(0x7 << 12)
#define ISINK_CH_STEP(i)	(((i) << 12) & ISINK_CH_STEP_MASK)
#define ISINK_SFSTR0_TC_MASK	(0x3 << 1)
#define ISINK_SFSTR0_TC(i)	(((i) << 1) & ISINK_SFSTR0_TC_MASK)
#define ISINK_SFSTR0_EN_MASK	BIT(0)
#define ISINK_SFSTR0_EN		BIT(0)

/* Register to LED channel enablement. */
#define ISINK_CH_EN_MASK(i)	BIT(i)
#define ISINK_CH_EN(i)		BIT(i)

#define MAX_SUPPORTED_LEDS		8

struct mt6323_leds;

/**
 * struct mt6323_led - state container for the LED device
 * @id:			the identifier in MT6323 LED device
 * @parent:		the pointer to MT6323 LED controller
 * @cdev:		LED class device for this LED device
 * @current_brightness: current state of the LED device
 */
struct mt6323_led {
	int			id;
	struct mt6323_leds	*parent;
	struct led_classdev	cdev;
	enum led_brightness	current_brightness;
};

/**
 * struct mt6323_regs - register spec for the LED device
 * @top_ckpdn:		Offset to ISINK_CKPDN[0..x] registers
 * @num_top_ckpdn:	Number of ISINK_CKPDN registers
 * @top_ckcon:		Offset to ISINK_CKCON[0..x] registers
 * @num_top_ckcon:	Number of ISINK_CKCON registers
 * @isink_con:		Offset to ISINKx_CON[0..x] registers
 * @num_isink_con:	Number of ISINKx_CON registers
 * @isink_max_regs:	Number of ISINK[0..x] registers
 * @isink_en_ctrl:	Offset to ISINK_EN_CTRL register
 * @iwled_en_ctrl:	Offset to IWLED_EN_CTRL register
 */
struct mt6323_regs {
	const u16 *top_ckpdn;
	u8 num_top_ckpdn;
	const u16 *top_ckcon;
	u8 num_top_ckcon;
	const u16 *isink_con;
	u8 num_isink_con;
	u8 isink_max_regs;
	u16 isink_en_ctrl;
	u16 iwled_en_ctrl;
};

/**
 * struct mt6323_hwspec - hardware specific parameters
 * @max_period:		Maximum period for all LEDs
 * @max_leds:		Maximum number of supported LEDs
 * @max_wleds:		Maximum number of WLEDs
 * @max_brightness:	Maximum brightness for all LEDs
 * @unit_duty:		Steps of duty per period
 */
struct mt6323_hwspec {
	u16 max_period;
	u8 max_leds;
	u8 max_wleds;
	u16 max_brightness;
	u16 unit_duty;
};

/**
 * struct mt6323_data - device specific data
 * @regs:		Register spec for this device
 * @spec:		Hardware specific parameters
 */
struct mt6323_data {
	const struct mt6323_regs *regs;
	const struct mt6323_hwspec *spec;
};

/**
 * struct mt6323_leds -	state container for holding LED controller
 *			of the driver
 * @dev:		the device pointer
 * @hw:			the underlying hardware providing shared
 *			bus for the register operations
 * @pdata:		device specific data
 * @lock:		the lock among process context
 * @led:		the array that contains the state of individual
 *			LED device
 */
struct mt6323_leds {
	struct device		*dev;
	struct mt6397_chip	*hw;
	const struct mt6323_data *pdata;
	/* protect among process context */
	struct mutex		lock;
	struct mt6323_led	*led[MAX_SUPPORTED_LEDS];
};

static int mt6323_led_hw_brightness(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	u32 con2_mask = 0, con2_val = 0;
	int ret;

	/*
	 * Setup current output for the corresponding
	 * brightness level.
	 */
	con2_mask |= ISINK_CH_STEP_MASK |
		     ISINK_SFSTR0_TC_MASK |
		     ISINK_SFSTR0_EN_MASK;
	con2_val |=  ISINK_CH_STEP(brightness - 1) |
		     ISINK_SFSTR0_TC(2) |
		     ISINK_SFSTR0_EN;

	ret = regmap_update_bits(regmap, ISINK_CON(regs->isink_con[2], led->id),
				 con2_mask, con2_val);
	return ret;
}

static int mt6323_led_hw_off(struct led_classdev *cdev)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	unsigned int status;
	int ret;

	status = ISINK_CH_EN(led->id);
	ret = regmap_update_bits(regmap, regs->isink_en_ctrl,
				 ISINK_CH_EN_MASK(led->id), ~status);
	if (ret < 0)
		return ret;

	usleep_range(100, 300);
	ret = regmap_update_bits(regmap, regs->top_ckpdn[2],
				 RG_ISINK_CK_PDN_MASK(led->id),
				 RG_ISINK_CK_PDN(led->id));
	if (ret < 0)
		return ret;

	return 0;
}

static enum led_brightness
mt6323_get_led_hw_brightness(struct led_classdev *cdev)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	unsigned int status;
	int ret;

	ret = regmap_read(regmap, regs->top_ckpdn[2], &status);
	if (ret < 0)
		return ret;

	if (status & RG_ISINK_CK_PDN_MASK(led->id))
		return 0;

	ret = regmap_read(regmap, regs->isink_en_ctrl, &status);
	if (ret < 0)
		return ret;

	if (!(status & ISINK_CH_EN(led->id)))
		return 0;

	ret = regmap_read(regmap, ISINK_CON(regs->isink_con[2], led->id), &status);
	if (ret < 0)
		return ret;

	return  ((status & ISINK_CH_STEP_MASK)
		  >> ISINK_CH_STEP_SHIFT) + 1;
}

static int mt6323_led_hw_on(struct led_classdev *cdev,
			    enum led_brightness brightness)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	unsigned int status;
	int ret;

	/*
	 * Setup required clock source, enable the corresponding
	 * clock and channel and let work with continuous blink as
	 * the default.
	 */
	ret = regmap_update_bits(regmap, regs->top_ckcon[1],
				 RG_ISINK_CK_SEL_MASK(led->id), 0);
	if (ret < 0)
		return ret;

	status = RG_ISINK_CK_PDN(led->id);
	ret = regmap_update_bits(regmap, regs->top_ckpdn[2],
				 RG_ISINK_CK_PDN_MASK(led->id),
				 ~status);
	if (ret < 0)
		return ret;

	usleep_range(100, 300);

	ret = regmap_update_bits(regmap, regs->isink_en_ctrl,
				 ISINK_CH_EN_MASK(led->id),
				 ISINK_CH_EN(led->id));
	if (ret < 0)
		return ret;

	ret = mt6323_led_hw_brightness(cdev, brightness);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, ISINK_CON(regs->isink_con[0], led->id),
				 ISINK_DIM_DUTY_MASK,
				 ISINK_DIM_DUTY(31));
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, ISINK_CON(regs->isink_con[1], led->id),
				 ISINK_DIM_FSEL_MASK,
				 ISINK_DIM_FSEL(1000));
	if (ret < 0)
		return ret;

	return 0;
}

static int mt6323_led_set_blink(struct led_classdev *cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	const struct mt6323_hwspec *spec = leds->pdata->spec;
	struct regmap *regmap = leds->hw->regmap;
	unsigned long period;
	u8 duty_hw;
	int ret;

	/*
	 * LED subsystem requires a default user
	 * friendly blink pattern for the LED so using
	 * 1Hz duty cycle 50% here if without specific
	 * value delay_on and delay off being assigned.
	 */
	if (!*delay_on && !*delay_off) {
		*delay_on = 500;
		*delay_off = 500;
	}

	/*
	 * Units are in ms, if over the hardware able
	 * to support, fallback into software blink
	 */
	period = *delay_on + *delay_off;

	if (period > spec->max_period)
		return -EINVAL;

	/*
	 * Calculate duty_hw based on the percentage of period during
	 * which the led is ON.
	 */
	duty_hw = DIV_ROUND_CLOSEST(*delay_on * 100000ul, period * spec->unit_duty);

	/* hardware doesn't support zero duty cycle. */
	if (!duty_hw)
		return -EINVAL;

	mutex_lock(&leds->lock);
	/*
	 * Set max_brightness as the software blink behavior
	 * when no blink brightness.
	 */
	if (!led->current_brightness) {
		ret = mt6323_led_hw_on(cdev, cdev->max_brightness);
		if (ret < 0)
			goto out;
		led->current_brightness = cdev->max_brightness;
	}

	ret = regmap_update_bits(regmap, ISINK_CON(regs->isink_con[0], led->id),
				 ISINK_DIM_DUTY_MASK,
				 ISINK_DIM_DUTY(duty_hw - 1));
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(regmap, ISINK_CON(regs->isink_con[1], led->id),
				 ISINK_DIM_FSEL_MASK,
				 ISINK_DIM_FSEL(period - 1));
out:
	mutex_unlock(&leds->lock);

	return ret;
}

static int mt6323_led_set_brightness(struct led_classdev *cdev,
				     enum led_brightness brightness)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	int ret;

	mutex_lock(&leds->lock);

	if (!led->current_brightness && brightness) {
		ret = mt6323_led_hw_on(cdev, brightness);
		if (ret < 0)
			goto out;
	} else if (brightness) {
		ret = mt6323_led_hw_brightness(cdev, brightness);
		if (ret < 0)
			goto out;
	} else {
		ret = mt6323_led_hw_off(cdev);
		if (ret < 0)
			goto out;
	}

	led->current_brightness = brightness;
out:
	mutex_unlock(&leds->lock);

	return ret;
}

static int mtk_wled_hw_on(struct led_classdev *cdev)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	int ret;

	ret = regmap_clear_bits(regmap, regs->top_ckpdn[0], RG_VWLED_32K_CK_PDN);
	if (ret)
		return ret;

	ret = regmap_clear_bits(regmap, regs->top_ckpdn[0], RG_VWLED_6M_CK_PDN);
	if (ret)
		return ret;

	ret = regmap_clear_bits(regmap, regs->top_ckpdn[0], RG_VWLED_1M_CK_PDN);
	if (ret)
		return ret;

	usleep_range(5000, 6000);

	/* Enable WLED channel pair */
	ret = regmap_set_bits(regmap, regs->iwled_en_ctrl, BIT(led->id));
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, regs->iwled_en_ctrl, BIT(led->id + 1));
	if (ret)
		return ret;

	return 0;
}

static int mtk_wled_hw_off(struct led_classdev *cdev)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	int ret;

	ret = regmap_clear_bits(regmap, regs->iwled_en_ctrl, BIT(led->id + 1));
	if (ret)
		return ret;

	ret = regmap_clear_bits(regmap, regs->iwled_en_ctrl, BIT(led->id));
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, regs->top_ckpdn[0], RG_VWLED_32K_CK_PDN);
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, regs->top_ckpdn[0], RG_VWLED_6M_CK_PDN);
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, regs->top_ckpdn[0], RG_VWLED_1M_CK_PDN);
	if (ret)
		return ret;

	return 0;
}

static enum led_brightness mt6323_get_wled_brightness(struct led_classdev *cdev)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	const struct mt6323_regs *regs = leds->pdata->regs;
	struct regmap *regmap = leds->hw->regmap;
	unsigned int status;
	int ret;

	ret = regmap_read(regmap, regs->iwled_en_ctrl, &status);
	if (ret)
		return 0;

	/* Always two channels per WLED */
	status &= BIT(led->id) | BIT(led->id + 1);

	return status ? led->current_brightness : 0;
}

static int mt6323_wled_set_brightness(struct led_classdev *cdev,
				      enum led_brightness brightness)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	struct mt6323_leds *leds = led->parent;
	int ret = 0;

	mutex_lock(&leds->lock);

	if (brightness) {
		if (!led->current_brightness)
			ret = mtk_wled_hw_on(cdev);
		if (ret)
			goto out;
	} else {
		ret = mtk_wled_hw_off(cdev);
		if (ret)
			goto out;
	}

	led->current_brightness = brightness;
out:
	mutex_unlock(&leds->lock);

	return ret;
}

static int mt6323_led_set_dt_default(struct led_classdev *cdev,
				     struct device_node *np)
{
	struct mt6323_led *led = container_of(cdev, struct mt6323_led, cdev);
	enum led_default_state state;
	int ret = 0;

	state = led_init_default_state_get(of_fwnode_handle(np));
	switch (state) {
	case LEDS_DEFSTATE_ON:
		ret = mt6323_led_set_brightness(cdev, cdev->max_brightness);
		break;
	case LEDS_DEFSTATE_KEEP:
		ret = mt6323_get_led_hw_brightness(cdev);
		if (ret < 0)
			return ret;
		led->current_brightness = ret;
		ret = 0;
		break;
	default:
		ret = mt6323_led_set_brightness(cdev, LED_OFF);
	}

	return ret;
}

static int mt6323_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct mt6397_chip *hw = dev_get_drvdata(dev->parent);
	struct mt6323_leds *leds;
	struct mt6323_led *led;
	const struct mt6323_regs *regs;
	const struct mt6323_hwspec *spec;
	int ret;
	unsigned int status;
	u32 reg;
	u8 max_leds;

	leds = devm_kzalloc(dev, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	platform_set_drvdata(pdev, leds);
	leds->dev = dev;
	leds->pdata = device_get_match_data(dev);
	regs = leds->pdata->regs;
	spec = leds->pdata->spec;
	max_leds = spec->max_leds + spec->max_wleds;

	/*
	 * leds->hw points to the underlying bus for the register
	 * controlled.
	 */
	leds->hw = hw;
	mutex_init(&leds->lock);

	status = RG_DRV_32K_CK_PDN;
	ret = regmap_update_bits(leds->hw->regmap, regs->top_ckpdn[0],
				 RG_DRV_32K_CK_PDN_MASK, ~status);
	if (ret < 0) {
		dev_err(leds->dev,
			"Failed to update TOP_CKPDN0 Register\n");
		return ret;
	}

	for_each_available_child_of_node_scoped(np, child) {
		struct led_init_data init_data = {};
		bool is_wled;

		ret = of_property_read_u32(child, "reg", &reg);
		if (ret) {
			dev_err(dev, "Failed to read led 'reg' property\n");
			return ret;
		}

		if (reg >= max_leds || reg >= MAX_SUPPORTED_LEDS ||
		    leds->led[reg]) {
			dev_err(dev, "Invalid led reg %u\n", reg);
			return -EINVAL;
		}

		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		is_wled = of_property_read_bool(child, "mediatek,is-wled");

		leds->led[reg] = led;
		leds->led[reg]->id = reg;
		leds->led[reg]->cdev.max_brightness = spec->max_brightness;

		if (is_wled) {
			leds->led[reg]->cdev.brightness_set_blocking =
						mt6323_wled_set_brightness;
			leds->led[reg]->cdev.brightness_get =
						mt6323_get_wled_brightness;
		} else {
			leds->led[reg]->cdev.brightness_set_blocking =
						mt6323_led_set_brightness;
			leds->led[reg]->cdev.blink_set = mt6323_led_set_blink;
			leds->led[reg]->cdev.brightness_get =
						mt6323_get_led_hw_brightness;
		}
		leds->led[reg]->parent = leds;

		ret = mt6323_led_set_dt_default(&leds->led[reg]->cdev, child);
		if (ret < 0) {
			dev_err(leds->dev,
				"Failed to LED set default from devicetree\n");
			return ret;
		}

		init_data.fwnode = of_fwnode_handle(child);

		ret = devm_led_classdev_register_ext(dev, &leds->led[reg]->cdev,
						     &init_data);
		if (ret) {
			dev_err(dev, "Failed to register LED: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static void mt6323_led_remove(struct platform_device *pdev)
{
	struct mt6323_leds *leds = platform_get_drvdata(pdev);
	const struct mt6323_regs *regs = leds->pdata->regs;
	int i;

	/* Turn the LEDs off on driver removal. */
	for (i = 0 ; leds->led[i] ; i++)
		mt6323_led_hw_off(&leds->led[i]->cdev);

	regmap_update_bits(leds->hw->regmap, regs->top_ckpdn[0],
			   RG_DRV_32K_CK_PDN_MASK,
			   RG_DRV_32K_CK_PDN);

	mutex_destroy(&leds->lock);
}

static const struct mt6323_regs mt6323_registers = {
	.top_ckpdn = (const u16[]){ 0x102, 0x106, 0x10e },
	.num_top_ckpdn = 3,
	.top_ckcon = (const u16[]){ 0x120, 0x126 },
	.num_top_ckcon = 2,
	.isink_con = (const u16[]){ 0x330, 0x332, 0x334 },
	.num_isink_con = 3,
	.isink_max_regs = 4, /* ISINK[0..3] */
	.isink_en_ctrl = 0x356,
};

static const struct mt6323_regs mt6331_registers = {
	.top_ckpdn = (const u16[]){ 0x138, 0x13e, 0x144 },
	.num_top_ckpdn = 3,
	.top_ckcon = (const u16[]){ 0x14c, 0x14a },
	.num_top_ckcon = 2,
	.isink_con = (const u16[]){ 0x40c, 0x40e, 0x410, 0x412, 0x414 },
	.num_isink_con = 5,
	.isink_max_regs = 4, /* ISINK[0..3] */
	.isink_en_ctrl = 0x43a,
};

static const struct mt6323_regs mt6332_registers = {
	.top_ckpdn = (const u16[]){ 0x8094, 0x809a, 0x80a0 },
	.num_top_ckpdn = 3,
	.top_ckcon = (const u16[]){ 0x80a6, 0x80ac },
	.num_top_ckcon = 2,
	.isink_con = (const u16[]){ 0x8cd4 },
	.num_isink_con = 1,
	.isink_max_regs = 12, /* IWLED[0..2, 3..9] */
	.iwled_en_ctrl = 0x8cda,
};

static const struct mt6323_hwspec mt6323_spec = {
	.max_period = 10000,
	.max_leds = 4,
	.max_brightness = 6,
	.unit_duty = 3125,
};

static const struct mt6323_hwspec mt6332_spec = {
	/* There are no LEDs in MT6332. Only WLEDs are present. */
	.max_leds = 0,
	.max_wleds = 1,
	.max_brightness = 1024,
};

static const struct mt6323_data mt6323_pdata = {
	.regs = &mt6323_registers,
	.spec = &mt6323_spec,
};

static const struct mt6323_data mt6331_pdata = {
	.regs = &mt6331_registers,
	.spec = &mt6323_spec,
};

static const struct mt6323_data mt6332_pdata = {
	.regs = &mt6332_registers,
	.spec = &mt6332_spec,
};

static const struct of_device_id mt6323_led_dt_match[] = {
	{ .compatible = "mediatek,mt6323-led", .data = &mt6323_pdata},
	{ .compatible = "mediatek,mt6331-led", .data = &mt6331_pdata },
	{ .compatible = "mediatek,mt6332-led", .data = &mt6332_pdata },
	{},
};
MODULE_DEVICE_TABLE(of, mt6323_led_dt_match);

static struct platform_driver mt6323_led_driver = {
	.probe		= mt6323_led_probe,
	.remove_new	= mt6323_led_remove,
	.driver		= {
		.name	= "mt6323-led",
		.of_match_table = mt6323_led_dt_match,
	},
};

module_platform_driver(mt6323_led_driver);

MODULE_DESCRIPTION("LED driver for Mediatek MT6323 PMIC");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("GPL");
