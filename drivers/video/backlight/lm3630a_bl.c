// SPDX-License-Identifier: GPL-2.0-only
/*
* Simple driver for Texas Instruments LM3630A Backlight driver chip
* Copyright (C) 2012 Texas Instruments
*/
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/pwm.h>
#include <linux/platform_data/lm3630a_bl.h>

#define REG_CTRL	0x00
#define REG_BOOST	0x02
#define REG_CONFIG	0x01
#define REG_BRT_A	0x03
#define REG_BRT_B	0x04
#define REG_I_A		0x05
#define REG_I_B		0x06
#define REG_INT_STATUS	0x09
#define REG_INT_EN	0x0A
#define REG_FAULT	0x0B
#define REG_PWM_OUTLOW	0x12
#define REG_PWM_OUTHIGH	0x13
#define REG_FILTER_STRENGTH	0x50
#define REG_MAX		0x50

#define INT_DEBOUNCE_MSEC	10

#define LM3630A_BANK_0		0
#define LM3630A_BANK_1		1

#define LM3630A_NUM_SINKS	2
#define LM3630A_SINK_0		0
#define LM3630A_SINK_1		1

struct lm3630a_chip {
	struct device *dev;
	struct delayed_work work;

	int irq;
	struct workqueue_struct *irqthread;
	struct lm3630a_platform_data *pdata;
	struct backlight_device *bleda;
	struct backlight_device *bledb;
	struct gpio_desc *enable_gpio;
	struct regmap *regmap;
	struct pwm_device *pwmd;
};

/* i2c access */
static int lm3630a_read(struct lm3630a_chip *pchip, unsigned int reg)
{
	int rval;
	unsigned int reg_val;

	rval = regmap_read(pchip->regmap, reg, &reg_val);
	if (rval < 0)
		return rval;
	return reg_val & 0xFF;
}

static int lm3630a_write(struct lm3630a_chip *pchip,
			 unsigned int reg, unsigned int data)
{
	return regmap_write(pchip->regmap, reg, data);
}

static int lm3630a_update(struct lm3630a_chip *pchip,
			  unsigned int reg, unsigned int mask,
			  unsigned int data)
{
	return regmap_update_bits(pchip->regmap, reg, mask, data);
}

/* initialize chip */
static int lm3630a_chip_init(struct lm3630a_chip *pchip)
{
	int rval;
	struct lm3630a_platform_data *pdata = pchip->pdata;

	usleep_range(1000, 2000);
	/* set Filter Strength Register */
	rval = lm3630a_write(pchip, REG_FILTER_STRENGTH, 0x03);
	/* set Cofig. register */
	rval |= lm3630a_update(pchip, REG_CONFIG, 0x07, pdata->pwm_ctrl);
	/* set boost control */
	rval |= lm3630a_write(pchip, REG_BOOST, 0x38);
	/* set current A */
	rval |= lm3630a_update(pchip, REG_I_A, 0x1F, 0x1F);
	/* set current B */
	rval |= lm3630a_write(pchip, REG_I_B, 0x1F);
	/* set control */
	rval |= lm3630a_update(pchip, REG_CTRL, 0x14, pdata->leda_ctrl);
	rval |= lm3630a_update(pchip, REG_CTRL, 0x0B, pdata->ledb_ctrl);
	usleep_range(1000, 2000);
	/* set brightness A and B */
	rval |= lm3630a_write(pchip, REG_BRT_A, pdata->leda_init_brt);
	rval |= lm3630a_write(pchip, REG_BRT_B, pdata->ledb_init_brt);

	if (rval < 0)
		dev_err(pchip->dev, "i2c failed to access register\n");
	return rval;
}

/* interrupt handling */
static void lm3630a_delayed_func(struct work_struct *work)
{
	int rval;
	struct lm3630a_chip *pchip;

	pchip = container_of(work, struct lm3630a_chip, work.work);

	rval = lm3630a_read(pchip, REG_INT_STATUS);
	if (rval < 0) {
		dev_err(pchip->dev,
			"i2c failed to access REG_INT_STATUS Register\n");
		return;
	}

	dev_info(pchip->dev, "REG_INT_STATUS Register is 0x%x\n", rval);
}

static irqreturn_t lm3630a_isr_func(int irq, void *chip)
{
	int rval;
	struct lm3630a_chip *pchip = chip;
	unsigned long delay = msecs_to_jiffies(INT_DEBOUNCE_MSEC);

	queue_delayed_work(pchip->irqthread, &pchip->work, delay);

	rval = lm3630a_update(pchip, REG_CTRL, 0x80, 0x00);
	if (rval < 0) {
		dev_err(pchip->dev, "i2c failed to access register\n");
		return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

static int lm3630a_intr_config(struct lm3630a_chip *pchip)
{
	int rval;

	rval = lm3630a_write(pchip, REG_INT_EN, 0x87);
	if (rval < 0)
		return rval;

	INIT_DELAYED_WORK(&pchip->work, lm3630a_delayed_func);
	pchip->irqthread = create_singlethread_workqueue("lm3630a-irqthd");
	if (!pchip->irqthread) {
		dev_err(pchip->dev, "create irq thread fail\n");
		return -ENOMEM;
	}
	if (request_threaded_irq
	    (pchip->irq, NULL, lm3630a_isr_func,
	     IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "lm3630a_irq", pchip)) {
		dev_err(pchip->dev, "request threaded irq fail\n");
		destroy_workqueue(pchip->irqthread);
		return -ENOMEM;
	}
	return rval;
}

static void lm3630a_pwm_ctrl(struct lm3630a_chip *pchip, int br, int br_max)
{
	unsigned int period = pchip->pdata->pwm_period;
	unsigned int duty = br * period / br_max;

	pwm_config(pchip->pwmd, duty, period);
	if (duty)
		pwm_enable(pchip->pwmd);
	else
		pwm_disable(pchip->pwmd);
}

/* update and get brightness */
static int lm3630a_bank_a_update_status(struct backlight_device *bl)
{
	int ret;
	struct lm3630a_chip *pchip = bl_get_data(bl);
	enum lm3630a_pwm_ctrl pwm_ctrl = pchip->pdata->pwm_ctrl;

	/* pwm control */
	if ((pwm_ctrl & LM3630A_PWM_BANK_A) != 0) {
		lm3630a_pwm_ctrl(pchip, bl->props.brightness,
				 bl->props.max_brightness);
		return bl->props.brightness;
	}

	/* disable sleep */
	ret = lm3630a_update(pchip, REG_CTRL, 0x80, 0x00);
	if (ret < 0)
		goto out_i2c_err;
	usleep_range(1000, 2000);
	/* minimum brightness is 0x04 */
	ret = lm3630a_write(pchip, REG_BRT_A, bl->props.brightness);
	if (bl->props.brightness < 0x4)
		ret |= lm3630a_update(pchip, REG_CTRL, LM3630A_LEDA_ENABLE, 0);
	else
		ret |= lm3630a_update(pchip, REG_CTRL,
				      LM3630A_LEDA_ENABLE, LM3630A_LEDA_ENABLE);
	if (ret < 0)
		goto out_i2c_err;
	return 0;

out_i2c_err:
	dev_err(pchip->dev, "i2c failed to access\n");
	return bl->props.brightness;
}

static int lm3630a_bank_a_get_brightness(struct backlight_device *bl)
{
	int brightness, rval;
	struct lm3630a_chip *pchip = bl_get_data(bl);
	enum lm3630a_pwm_ctrl pwm_ctrl = pchip->pdata->pwm_ctrl;

	if ((pwm_ctrl & LM3630A_PWM_BANK_A) != 0) {
		rval = lm3630a_read(pchip, REG_PWM_OUTHIGH);
		if (rval < 0)
			goto out_i2c_err;
		brightness = (rval & 0x01) << 8;
		rval = lm3630a_read(pchip, REG_PWM_OUTLOW);
		if (rval < 0)
			goto out_i2c_err;
		brightness |= rval;
		goto out;
	}

	/* disable sleep */
	rval = lm3630a_update(pchip, REG_CTRL, 0x80, 0x00);
	if (rval < 0)
		goto out_i2c_err;
	usleep_range(1000, 2000);
	rval = lm3630a_read(pchip, REG_BRT_A);
	if (rval < 0)
		goto out_i2c_err;
	brightness = rval;

out:
	bl->props.brightness = brightness;
	return bl->props.brightness;
out_i2c_err:
	dev_err(pchip->dev, "i2c failed to access register\n");
	return 0;
}

static const struct backlight_ops lm3630a_bank_a_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lm3630a_bank_a_update_status,
	.get_brightness = lm3630a_bank_a_get_brightness,
};

/* update and get brightness */
static int lm3630a_bank_b_update_status(struct backlight_device *bl)
{
	int ret;
	struct lm3630a_chip *pchip = bl_get_data(bl);
	enum lm3630a_pwm_ctrl pwm_ctrl = pchip->pdata->pwm_ctrl;

	/* pwm control */
	if ((pwm_ctrl & LM3630A_PWM_BANK_B) != 0) {
		lm3630a_pwm_ctrl(pchip, bl->props.brightness,
				 bl->props.max_brightness);
		return bl->props.brightness;
	}

	/* disable sleep */
	ret = lm3630a_update(pchip, REG_CTRL, 0x80, 0x00);
	if (ret < 0)
		goto out_i2c_err;
	usleep_range(1000, 2000);
	/* minimum brightness is 0x04 */
	ret = lm3630a_write(pchip, REG_BRT_B, bl->props.brightness);
	if (bl->props.brightness < 0x4)
		ret |= lm3630a_update(pchip, REG_CTRL, LM3630A_LEDB_ENABLE, 0);
	else
		ret |= lm3630a_update(pchip, REG_CTRL,
				      LM3630A_LEDB_ENABLE, LM3630A_LEDB_ENABLE);
	if (ret < 0)
		goto out_i2c_err;
	return 0;

out_i2c_err:
	dev_err(pchip->dev, "i2c failed to access REG_CTRL\n");
	return bl->props.brightness;
}

static int lm3630a_bank_b_get_brightness(struct backlight_device *bl)
{
	int brightness, rval;
	struct lm3630a_chip *pchip = bl_get_data(bl);
	enum lm3630a_pwm_ctrl pwm_ctrl = pchip->pdata->pwm_ctrl;

	if ((pwm_ctrl & LM3630A_PWM_BANK_B) != 0) {
		rval = lm3630a_read(pchip, REG_PWM_OUTHIGH);
		if (rval < 0)
			goto out_i2c_err;
		brightness = (rval & 0x01) << 8;
		rval = lm3630a_read(pchip, REG_PWM_OUTLOW);
		if (rval < 0)
			goto out_i2c_err;
		brightness |= rval;
		goto out;
	}

	/* disable sleep */
	rval = lm3630a_update(pchip, REG_CTRL, 0x80, 0x00);
	if (rval < 0)
		goto out_i2c_err;
	usleep_range(1000, 2000);
	rval = lm3630a_read(pchip, REG_BRT_B);
	if (rval < 0)
		goto out_i2c_err;
	brightness = rval;

out:
	bl->props.brightness = brightness;
	return bl->props.brightness;
out_i2c_err:
	dev_err(pchip->dev, "i2c failed to access register\n");
	return 0;
}

static const struct backlight_ops lm3630a_bank_b_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lm3630a_bank_b_update_status,
	.get_brightness = lm3630a_bank_b_get_brightness,
};

static int lm3630a_backlight_register(struct lm3630a_chip *pchip)
{
	struct lm3630a_platform_data *pdata = pchip->pdata;
	struct backlight_properties props;
	const char *label;

	props.type = BACKLIGHT_RAW;
	if (pdata->leda_ctrl != LM3630A_LEDA_DISABLE) {
		props.brightness = pdata->leda_init_brt;
		props.max_brightness = pdata->leda_max_brt;
		label = pdata->leda_label ? pdata->leda_label : "lm3630a_leda";
		pchip->bleda =
		    devm_backlight_device_register(pchip->dev, label,
						   pchip->dev, pchip,
						   &lm3630a_bank_a_ops, &props);
		if (IS_ERR(pchip->bleda))
			return PTR_ERR(pchip->bleda);
	}

	if ((pdata->ledb_ctrl != LM3630A_LEDB_DISABLE) &&
	    (pdata->ledb_ctrl != LM3630A_LEDB_ON_A)) {
		props.brightness = pdata->ledb_init_brt;
		props.max_brightness = pdata->ledb_max_brt;
		label = pdata->ledb_label ? pdata->ledb_label : "lm3630a_ledb";
		pchip->bledb =
		    devm_backlight_device_register(pchip->dev, label,
						   pchip->dev, pchip,
						   &lm3630a_bank_b_ops, &props);
		if (IS_ERR(pchip->bledb))
			return PTR_ERR(pchip->bledb);
	}
	return 0;
}

static const struct regmap_config lm3630a_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int lm3630a_parse_led_sources(struct fwnode_handle *node,
				     int default_led_sources)
{
	u32 sources[LM3630A_NUM_SINKS];
	int ret, num_sources, i;

	num_sources = fwnode_property_count_u32(node, "led-sources");
	if (num_sources < 0)
		return default_led_sources;
	else if (num_sources > ARRAY_SIZE(sources))
		return -EINVAL;

	ret = fwnode_property_read_u32_array(node, "led-sources", sources,
					     num_sources);
	if (ret)
		return ret;

	for (i = 0; i < num_sources; i++) {
		if (sources[i] != LM3630A_SINK_0 && sources[i] != LM3630A_SINK_1)
			return -EINVAL;

		ret |= BIT(sources[i]);
	}

	return ret;
}

static int lm3630a_parse_bank(struct lm3630a_platform_data *pdata,
			      struct fwnode_handle *node, int *seen_led_sources)
{
	int led_sources, ret;
	const char *label;
	u32 bank, val;
	bool linear;

	ret = fwnode_property_read_u32(node, "reg", &bank);
	if (ret)
		return ret;

	if (bank != LM3630A_BANK_0 && bank != LM3630A_BANK_1)
		return -EINVAL;

	led_sources = lm3630a_parse_led_sources(node, BIT(bank));
	if (led_sources < 0)
		return led_sources;

	if (*seen_led_sources & led_sources)
		return -EINVAL;

	*seen_led_sources |= led_sources;

	linear = fwnode_property_read_bool(node,
					   "ti,linear-mapping-mode");
	if (bank) {
		if (led_sources & BIT(LM3630A_SINK_0) ||
		    !(led_sources & BIT(LM3630A_SINK_1)))
			return -EINVAL;

		pdata->ledb_ctrl = linear ?
			LM3630A_LEDB_ENABLE_LINEAR :
			LM3630A_LEDB_ENABLE;
	} else {
		if (!(led_sources & BIT(LM3630A_SINK_0)))
			return -EINVAL;

		pdata->leda_ctrl = linear ?
			LM3630A_LEDA_ENABLE_LINEAR :
			LM3630A_LEDA_ENABLE;

		if (led_sources & BIT(LM3630A_SINK_1))
			pdata->ledb_ctrl = LM3630A_LEDB_ON_A;
	}

	ret = fwnode_property_read_string(node, "label", &label);
	if (!ret) {
		if (bank)
			pdata->ledb_label = label;
		else
			pdata->leda_label = label;
	}

	ret = fwnode_property_read_u32(node, "default-brightness",
				       &val);
	if (!ret) {
		if (bank)
			pdata->ledb_init_brt = val;
		else
			pdata->leda_init_brt = val;
	}

	ret = fwnode_property_read_u32(node, "max-brightness", &val);
	if (!ret) {
		if (bank)
			pdata->ledb_max_brt = val;
		else
			pdata->leda_max_brt = val;
	}

	return 0;
}

static int lm3630a_parse_node(struct lm3630a_chip *pchip,
			      struct lm3630a_platform_data *pdata)
{
	int ret = -ENODEV, seen_led_sources = 0;
	struct fwnode_handle *node;

	device_for_each_child_node(pchip->dev, node) {
		ret = lm3630a_parse_bank(pdata, node, &seen_led_sources);
		if (ret)
			return ret;
	}

	return ret;
}

static int lm3630a_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct lm3630a_platform_data *pdata = dev_get_platdata(&client->dev);
	struct lm3630a_chip *pchip;
	int rval;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "fail : i2c functionality check\n");
		return -EOPNOTSUPP;
	}

	pchip = devm_kzalloc(&client->dev, sizeof(struct lm3630a_chip),
			     GFP_KERNEL);
	if (!pchip)
		return -ENOMEM;
	pchip->dev = &client->dev;

	pchip->regmap = devm_regmap_init_i2c(client, &lm3630a_regmap);
	if (IS_ERR(pchip->regmap)) {
		rval = PTR_ERR(pchip->regmap);
		dev_err(&client->dev, "fail : allocate reg. map: %d\n", rval);
		return rval;
	}

	i2c_set_clientdata(client, pchip);
	if (pdata == NULL) {
		pdata = devm_kzalloc(pchip->dev,
				     sizeof(struct lm3630a_platform_data),
				     GFP_KERNEL);
		if (pdata == NULL)
			return -ENOMEM;

		/* default values */
		pdata->leda_max_brt = LM3630A_MAX_BRIGHTNESS;
		pdata->ledb_max_brt = LM3630A_MAX_BRIGHTNESS;
		pdata->leda_init_brt = LM3630A_MAX_BRIGHTNESS;
		pdata->ledb_init_brt = LM3630A_MAX_BRIGHTNESS;

		rval = lm3630a_parse_node(pchip, pdata);
		if (rval) {
			dev_err(&client->dev, "fail : parse node\n");
			return rval;
		}
	}
	pchip->pdata = pdata;

	pchip->enable_gpio = devm_gpiod_get_optional(&client->dev, "enable",
						GPIOD_OUT_HIGH);
	if (IS_ERR(pchip->enable_gpio)) {
		rval = PTR_ERR(pchip->enable_gpio);
		return rval;
	}

	/* chip initialize */
	rval = lm3630a_chip_init(pchip);
	if (rval < 0) {
		dev_err(&client->dev, "fail : init chip\n");
		return rval;
	}
	/* backlight register */
	rval = lm3630a_backlight_register(pchip);
	if (rval < 0) {
		dev_err(&client->dev, "fail : backlight register.\n");
		return rval;
	}
	/* pwm */
	if (pdata->pwm_ctrl != LM3630A_PWM_DISABLE) {
		pchip->pwmd = devm_pwm_get(pchip->dev, "lm3630a-pwm");
		if (IS_ERR(pchip->pwmd)) {
			dev_err(&client->dev, "fail : get pwm device\n");
			return PTR_ERR(pchip->pwmd);
		}

		/*
		 * FIXME: pwm_apply_args() should be removed when switching to
		 * the atomic PWM API.
		 */
		pwm_apply_args(pchip->pwmd);
	}

	/* interrupt enable  : irq 0 is not allowed */
	pchip->irq = client->irq;
	if (pchip->irq) {
		rval = lm3630a_intr_config(pchip);
		if (rval < 0)
			return rval;
	}
	dev_info(&client->dev, "LM3630A backlight register OK.\n");
	return 0;
}

static int lm3630a_remove(struct i2c_client *client)
{
	int rval;
	struct lm3630a_chip *pchip = i2c_get_clientdata(client);

	rval = lm3630a_write(pchip, REG_BRT_A, 0);
	if (rval < 0)
		dev_err(pchip->dev, "i2c failed to access register\n");

	rval = lm3630a_write(pchip, REG_BRT_B, 0);
	if (rval < 0)
		dev_err(pchip->dev, "i2c failed to access register\n");

	if (pchip->irq) {
		free_irq(pchip->irq, pchip);
		flush_workqueue(pchip->irqthread);
		destroy_workqueue(pchip->irqthread);
	}
	return 0;
}

static const struct i2c_device_id lm3630a_id[] = {
	{LM3630A_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, lm3630a_id);

static const struct of_device_id lm3630a_match_table[] = {
	{ .compatible = "ti,lm3630a", },
	{ },
};

MODULE_DEVICE_TABLE(of, lm3630a_match_table);

static struct i2c_driver lm3630a_i2c_driver = {
	.driver = {
		   .name = LM3630A_NAME,
		   .of_match_table = lm3630a_match_table,
		   },
	.probe = lm3630a_probe,
	.remove = lm3630a_remove,
	.id_table = lm3630a_id,
};

module_i2c_driver(lm3630a_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments Backlight driver for LM3630A");
MODULE_AUTHOR("Daniel Jeong <gshark.jeong@gmail.com>");
MODULE_AUTHOR("LDD MLP <ldd-mlp@list.ti.com>");
MODULE_LICENSE("GPL v2");
