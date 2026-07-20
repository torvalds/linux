// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/media/i2c/lm3560.c
 * General device driver for TI lm3559, lm3560, FLASH LED Driver
 *
 * Copyright (C) 2013 Texas Instruments
 *
 * Contact: Daniel Jeong <gshark.jeong@gmail.com>
 *			Ldd-Mlp <ldd-mlp@list.ti.com>
 */

#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

/* registers definitions */
#define REG_ENABLE		0x10
#define REG_TORCH_BR	0xa0
#define REG_FLASH_BR	0xb0
#define REG_FLASH_TOUT	0xc0
#define REG_FLAG		0xd0
#define REG_CONFIG1		0xe0

/* fault mask */
#define FAULT_TIMEOUT	(1<<0)
#define FAULT_OVERTEMP	(1<<1)
#define FAULT_SHORT_CIRCUIT	(1<<2)

#define LM3560_FLASH_TOUT_MIN			32
#define LM3560_FLASH_TOUT_STEP			32
#define LM3560_FLASH_TOUT_MAX			1024
#define LM3560_FLASH_TOUT_ms_TO_REG(a)		\
	((a) < LM3560_FLASH_TOUT_MIN ? 0 :	\
	 (((a) - LM3560_FLASH_TOUT_MIN) / LM3560_FLASH_TOUT_STEP))
#define LM3560_FLASH_TOUT_REG_TO_ms(a)		\
	((a) * LM3560_FLASH_TOUT_STEP + LM3560_FLASH_TOUT_MIN)

enum lm3560_led_id {
	LM3560_LED0 = 0,
	LM3560_LED1,
	LM3560_LED_MAX
};

enum lm3560_peak_current {
	LM3560_PEAK_1600mA = 0x00,
	LM3560_PEAK_2300mA = 0x20,
	LM3560_PEAK_3000mA = 0x40,
	LM3560_PEAK_3600mA = 0x60
};

enum led_enable {
	MODE_SHDN = 0x0,
	MODE_TORCH = 0x2,
	MODE_FLASH = 0x3,
};

struct lm3560_flash_config {
	u32 flash_brt_min_ua;
	u32 flash_brt_max_ua;
	u32 flash_brt_step_ua;

	u32 torch_brt_min_ua;
	u32 torch_brt_max_ua;
	u32 torch_brt_step_ua;
};

/**
 * struct lm3560_flash
 *
 * @dev: pointer to &struct device
 * @regmap: reg. map for i2c
 * @lock: muxtex for serial access.
 * @hwen_gpio: line connected to HWEN pin
 * @vin_supply: line connected to IN supply (2.5V - 5.5V)
 * @led_mode: V4L2 LED mode
 * @ctrls_led: V4L2 controls
 * @subdev_led: V4L2 subdev
 * @led_id: LED status holder
 * @peak: peak current
 * @max_flash_timeout: flash timeout
 * @max_flash_brt: flash mode led brightness
 * @max_torch_brt: torch mode led brightness
 * @config: device specific current configuration
 */
struct lm3560_flash {
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;

	struct gpio_desc *hwen_gpio;
	struct regulator *vin_supply;

	enum v4l2_flash_led_mode led_mode;
	struct v4l2_ctrl_handler ctrls_led[LM3560_LED_MAX];
	struct v4l2_subdev subdev_led[LM3560_LED_MAX];

	DECLARE_BITMAP(led_id, LM3560_LED_MAX);

	enum lm3560_peak_current peak;
	u32 max_flash_timeout;

	u32 max_flash_brt[LM3560_LED_MAX];
	u32 max_torch_brt[LM3560_LED_MAX];

	const struct lm3560_flash_config *config;
};

#define to_lm3560_flash(_ctrl, _no)	\
	container_of(_ctrl->handler, struct lm3560_flash, ctrls_led[_no])

/* enable mode control */
static int lm3560_mode_ctrl(struct lm3560_flash *flash)
{
	int rval = -EINVAL;

	switch (flash->led_mode) {
	case V4L2_FLASH_LED_MODE_NONE:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x03, MODE_SHDN);
		break;
	case V4L2_FLASH_LED_MODE_TORCH:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x03, MODE_TORCH);
		break;
	case V4L2_FLASH_LED_MODE_FLASH:
		rval = regmap_update_bits(flash->regmap,
					  REG_ENABLE, 0x03, MODE_FLASH);
		break;
	}
	return rval;
}

/* led1/2 enable/disable */
static int lm3560_enable_ctrl(struct lm3560_flash *flash,
			      enum lm3560_led_id led_no, bool on)
{
	int rval;

	if (led_no == LM3560_LED0) {
		if (on)
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x08, 0x08);
		else
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x08, 0x00);
	} else {
		if (on)
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x10, 0x10);
		else
			rval = regmap_update_bits(flash->regmap,
						  REG_ENABLE, 0x10, 0x00);
	}
	return rval;
}

/* torch1/2 brightness control */
static int lm3560_torch_brt_ctrl(struct lm3560_flash *flash,
				 enum lm3560_led_id led_no, unsigned int brt)
{
	const struct lm3560_flash_config *config = flash->config;
	int rval;
	u32 br_bits;

	if (brt < config->torch_brt_min_ua)
		return lm3560_enable_ctrl(flash, led_no, false);
	else
		rval = lm3560_enable_ctrl(flash, led_no, true);

	br_bits = clamp(brt, config->torch_brt_min_ua,
			config->torch_brt_max_ua);
	br_bits = (br_bits - config->torch_brt_min_ua) /
		  config->torch_brt_step_ua;

	if (led_no == LM3560_LED0)
		rval = regmap_update_bits(flash->regmap,
					  REG_TORCH_BR, 0x07, br_bits);
	else
		rval = regmap_update_bits(flash->regmap,
					  REG_TORCH_BR, 0x38, br_bits << 3);

	return rval;
}

/* flash1/2 brightness control */
static int lm3560_flash_brt_ctrl(struct lm3560_flash *flash,
				 enum lm3560_led_id led_no, unsigned int brt)
{
	const struct lm3560_flash_config *config = flash->config;
	int rval;
	u32 br_bits;

	if (brt < config->flash_brt_min_ua)
		return lm3560_enable_ctrl(flash, led_no, false);
	else
		rval = lm3560_enable_ctrl(flash, led_no, true);

	br_bits = clamp(brt, config->flash_brt_min_ua,
			config->flash_brt_max_ua);
	br_bits = (br_bits - config->flash_brt_min_ua) /
		  config->flash_brt_step_ua;

	if (led_no == LM3560_LED0)
		rval = regmap_update_bits(flash->regmap,
					  REG_FLASH_BR, 0x0f, br_bits);
	else
		rval = regmap_update_bits(flash->regmap,
					  REG_FLASH_BR, 0xf0, br_bits << 4);

	return rval;
}

/* v4l2 controls  */
static int lm3560_get_ctrl(struct v4l2_ctrl *ctrl, enum lm3560_led_id led_no)
{
	struct lm3560_flash *flash = to_lm3560_flash(ctrl, led_no);
	int rval = -EINVAL;

	if (!pm_runtime_get_if_active(flash->dev))
		return 0;

	if (ctrl->id == V4L2_CID_FLASH_FAULT) {
		s32 fault = 0;
		unsigned int reg_val;
		rval = regmap_read(flash->regmap, REG_FLAG, &reg_val);
		if (rval < 0) {
			pm_runtime_put(flash->dev);
			return rval;
		}
		if (reg_val & FAULT_SHORT_CIRCUIT)
			fault |= V4L2_FLASH_FAULT_SHORT_CIRCUIT;
		if (reg_val & FAULT_OVERTEMP)
			fault |= V4L2_FLASH_FAULT_OVER_TEMPERATURE;
		if (reg_val & FAULT_TIMEOUT)
			fault |= V4L2_FLASH_FAULT_TIMEOUT;
		ctrl->cur.val = fault;
	}

	pm_runtime_put(flash->dev);

	return rval;
}

static int lm3560_set_ctrl(struct v4l2_ctrl *ctrl, enum lm3560_led_id led_no)
{
	struct lm3560_flash *flash = to_lm3560_flash(ctrl, led_no);
	u8 tout_bits;
	int rval = -EINVAL;

	if (!pm_runtime_get_if_active(flash->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
		flash->led_mode = ctrl->val;
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			rval = lm3560_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_STROBE_SOURCE:
		rval = regmap_update_bits(flash->regmap,
					  REG_CONFIG1, 0x04, (ctrl->val) << 2);
		break;

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			break;
		}
		flash->led_mode = V4L2_FLASH_LED_MODE_FLASH;
		rval = lm3560_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH) {
			rval = -EBUSY;
			break;
		}
		flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
		rval = lm3560_mode_ctrl(flash);
		break;

	case V4L2_CID_FLASH_TIMEOUT:
		tout_bits = LM3560_FLASH_TOUT_ms_TO_REG(ctrl->val);
		rval = regmap_update_bits(flash->regmap,
					  REG_FLASH_TOUT, 0x1f, tout_bits);
		break;

	case V4L2_CID_FLASH_INTENSITY:
		rval = lm3560_flash_brt_ctrl(flash, led_no, ctrl->val);
		break;

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		rval = lm3560_torch_brt_ctrl(flash, led_no, ctrl->val);
		break;
	}

	pm_runtime_put(flash->dev);

	return rval;
}

static int lm3560_led1_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return lm3560_get_ctrl(ctrl, LM3560_LED1);
}

static int lm3560_led1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return lm3560_set_ctrl(ctrl, LM3560_LED1);
}

static int lm3560_led0_get_ctrl(struct v4l2_ctrl *ctrl)
{
	return lm3560_get_ctrl(ctrl, LM3560_LED0);
}

static int lm3560_led0_set_ctrl(struct v4l2_ctrl *ctrl)
{
	return lm3560_set_ctrl(ctrl, LM3560_LED0);
}

static const struct v4l2_ctrl_ops lm3560_led_ctrl_ops[LM3560_LED_MAX] = {
	[LM3560_LED0] = {
			 .g_volatile_ctrl = lm3560_led0_get_ctrl,
			 .s_ctrl = lm3560_led0_set_ctrl,
			 },
	[LM3560_LED1] = {
			 .g_volatile_ctrl = lm3560_led1_get_ctrl,
			 .s_ctrl = lm3560_led1_set_ctrl,
			 }
};

static int lm3560_init_controls(struct lm3560_flash *flash,
				enum lm3560_led_id led_no)
{
	struct v4l2_ctrl *fault;
	u32 max_flash_brt = flash->max_flash_brt[led_no];
	u32 max_torch_brt = flash->max_torch_brt[led_no];
	struct v4l2_ctrl_handler *hdl = &flash->ctrls_led[led_no];
	const struct v4l2_ctrl_ops *ops = &lm3560_led_ctrl_ops[led_no];
	const struct lm3560_flash_config *config = flash->config;

	v4l2_ctrl_handler_init(hdl, 8);

	/* flash mode */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_LED_MODE,
			       V4L2_FLASH_LED_MODE_TORCH, ~0x7,
			       V4L2_FLASH_LED_MODE_NONE);
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;

	/* flash source */
	v4l2_ctrl_new_std_menu(hdl, ops, V4L2_CID_FLASH_STROBE_SOURCE,
			       0x1, ~0x3, V4L2_FLASH_STROBE_SOURCE_SOFTWARE);

	/* flash strobe */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE, 0, 0, 0, 0);

	/* flash strobe stop */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_STROBE_STOP, 0, 0, 0, 0);

	/* flash strobe timeout */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TIMEOUT,
			  LM3560_FLASH_TOUT_MIN,
			  flash->max_flash_timeout,
			  LM3560_FLASH_TOUT_STEP,
			  flash->max_flash_timeout);

	/* flash brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_INTENSITY,
			  config->flash_brt_min_ua, max_flash_brt,
			  config->flash_brt_step_ua, max_flash_brt);

	/* torch brt */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_TORCH_INTENSITY,
			  config->torch_brt_min_ua, max_torch_brt,
			  config->torch_brt_step_ua, max_torch_brt);

	/* fault */
	fault = v4l2_ctrl_new_std(hdl, ops, V4L2_CID_FLASH_FAULT, 0,
				  V4L2_FLASH_FAULT_OVER_VOLTAGE
				  | V4L2_FLASH_FAULT_OVER_TEMPERATURE
				  | V4L2_FLASH_FAULT_SHORT_CIRCUIT
				  | V4L2_FLASH_FAULT_TIMEOUT, 0, 0);
	if (fault != NULL)
		fault->flags |= V4L2_CTRL_FLAG_VOLATILE;

	hdl->lock = &flash->lock;

	if (hdl->error)
		return hdl->error;

	flash->subdev_led[led_no].ctrl_handler = hdl;
	return 0;
}

/* initialize device */
static const struct v4l2_subdev_ops lm3560_ops = {
	.core = NULL,
};

static const struct regmap_config lm3560_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xFF,
};

static int lm3560_subdev_init(struct lm3560_flash *flash,
			      enum lm3560_led_id led_no,
			      struct fwnode_handle *fwnode)
{
	struct i2c_client *client = to_i2c_client(flash->dev);
	int rval;

	v4l2_i2c_subdev_init(&flash->subdev_led[led_no], client, &lm3560_ops);
	flash->subdev_led[led_no].flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(flash->subdev_led[led_no].name,
		 sizeof(flash->subdev_led[led_no].name),
		 "lm3560-led%d", led_no);
	flash->subdev_led[led_no].fwnode = fwnode_handle_get(fwnode);
	rval = lm3560_init_controls(flash, led_no);
	if (rval)
		goto err_out;
	rval = media_entity_pads_init(&flash->subdev_led[led_no].entity, 0, NULL);
	if (rval < 0)
		goto err_out;
	flash->subdev_led[led_no].entity.function = MEDIA_ENT_F_FLASH;
	flash->subdev_led[led_no].state_lock = &flash->lock;

	rval = v4l2_async_register_subdev(&flash->subdev_led[led_no]);
	if (rval < 0) {
		dev_err(flash->dev, "failed to register V4L2 subdev");
		goto error_out_media;
	}

	return rval;
error_out_media:
	media_entity_cleanup(&flash->subdev_led[led_no].entity);
err_out:
	v4l2_ctrl_handler_free(&flash->ctrls_led[led_no]);
	return rval;
}

static int lm3560_init_device(struct lm3560_flash *flash)
{
	int rval;
	unsigned int reg_val;

	/* set peak current */
	rval = regmap_update_bits(flash->regmap,
				  REG_FLASH_TOUT, 0x60, flash->peak);
	if (rval < 0)
		return rval;
	/* output disable */
	flash->led_mode = V4L2_FLASH_LED_MODE_NONE;
	rval = lm3560_mode_ctrl(flash);
	if (rval < 0)
		return rval;
	/* reset faults */
	rval = regmap_read(flash->regmap, REG_FLAG, &reg_val);
	return rval;
}

static int __maybe_unused lm3560_power_off(struct device *dev)
{
	struct lm3560_flash *flash = dev_get_drvdata(dev);

	gpiod_set_value_cansleep(flash->hwen_gpio, 0);
	regulator_disable(flash->vin_supply);

	return 0;
}

static int __maybe_unused lm3560_power_on(struct device *dev)
{
	struct lm3560_flash *flash = dev_get_drvdata(dev);
	int rval;

	rval = regulator_enable(flash->vin_supply);
	if (rval < 0) {
		dev_err(flash->dev, "failed to enable vin power supply\n");
		return rval;
	}

	gpiod_set_value_cansleep(flash->hwen_gpio, 1);

	rval = lm3560_init_device(flash);
	if (rval < 0) {
		lm3560_power_off(dev);
		return rval;
	}

	return 0;
}

static void lm3560_subdev_cleanup(struct lm3560_flash *flash)
{
	int led_no;

	for_each_set_bit(led_no, flash->led_id, LM3560_LED_MAX) {
		v4l2_async_unregister_subdev(&flash->subdev_led[led_no]);
		v4l2_ctrl_handler_free(&flash->ctrls_led[led_no]);
		media_entity_cleanup(&flash->subdev_led[led_no].entity);
	}
}

static int lm3560_probe(struct i2c_client *client)
{
	struct lm3560_flash *flash;
	u32 peak_ua;
	int rval;

	flash = devm_kzalloc(&client->dev, sizeof(*flash), GFP_KERNEL);
	if (flash == NULL)
		return -ENOMEM;

	flash->config = device_get_match_data(&client->dev);
	if (!flash->config)
		return -ENODEV;

	flash->regmap = devm_regmap_init_i2c(client, &lm3560_regmap);
	if (IS_ERR(flash->regmap)) {
		rval = PTR_ERR(flash->regmap);
		return rval;
	}

	flash->dev = &client->dev;
	mutex_init(&flash->lock);

	bitmap_zero(flash->led_id, LM3560_LED_MAX);

	flash->hwen_gpio = devm_gpiod_get_optional(flash->dev, "enable",
						   GPIOD_OUT_LOW);
	if (IS_ERR(flash->hwen_gpio))
		return dev_err_probe(flash->dev, PTR_ERR(flash->hwen_gpio),
				     "failed to get hwen gpio\n");

	flash->vin_supply = devm_regulator_get(flash->dev, "vin");
	if (IS_ERR(flash->vin_supply))
		return dev_err_probe(flash->dev, PTR_ERR(flash->vin_supply),
				     "failed to get vin-supply\n");

	flash->peak = LM3560_PEAK_1600mA;
	rval = device_property_read_u32(flash->dev,
					"ti,peak-current-microamp", &peak_ua);
	if (!rval) {
		/*
		 * LM3559 has lower peak current limit, but
		 * bit configuration matches LM3560.
		 * Correct current restrictions are enforced
		 * by the LM3560 schema.
		 */
		switch (peak_ua) {
		case 1400000:
		case 1600000:
			flash->peak = LM3560_PEAK_1600mA;
			break;
		case 2100000:
		case 2300000:
			flash->peak = LM3560_PEAK_2300mA;
			break;
		case 2700000:
		case 3000000:
			flash->peak = LM3560_PEAK_3000mA;
			break;
		case 3200000:
		case 3600000:
			flash->peak = LM3560_PEAK_3600mA;
			break;
		default:
			return -EINVAL;
		}
	}

	flash->max_flash_timeout = LM3560_FLASH_TOUT_MIN * 1000;
	device_property_read_u32(flash->dev, "flash-max-timeout-us",
				 &flash->max_flash_timeout);
	flash->max_flash_timeout /= 1000;

	rval = regulator_enable(flash->vin_supply);
	if (rval < 0)
		return dev_err_probe(flash->dev, rval,
				     "failed to enable vin power supply\n");

	gpiod_set_value_cansleep(flash->hwen_gpio, 1);

	rval = lm3560_init_device(flash);
	if (rval < 0)
		goto error_disable;

	pm_runtime_set_active(flash->dev);
	pm_runtime_enable(flash->dev);

	device_for_each_child_node_scoped(flash->dev, node) {
		const struct lm3560_flash_config *config = flash->config;
		u32 reg;

		rval = fwnode_property_read_u32(node, "reg", &reg);
		if (rval < 0)
			/* We care only about nodes with reg property */
			continue;

		if (reg == LM3560_LED0 || reg == LM3560_LED1) {
			flash->max_flash_brt[reg] = config->flash_brt_min_ua;
			fwnode_property_read_u32(node, "flash-max-microamp",
						 &flash->max_flash_brt[reg]);

			flash->max_torch_brt[reg] = config->torch_brt_min_ua;
			fwnode_property_read_u32(node, "led-max-microamp",
						 &flash->max_torch_brt[reg]);

			rval = lm3560_subdev_init(flash, reg, node);
			if (rval < 0) {
				dev_err(flash->dev,
					"failed to register led%d: %d\n",
					reg, rval);
				goto error_clean;
			}

			set_bit(reg, flash->led_id);
		}
	}

	i2c_set_clientdata(client, flash);

	pm_runtime_set_autosuspend_delay(flash->dev, 1000);
	pm_runtime_use_autosuspend(flash->dev);
	pm_runtime_idle(flash->dev);

	return 0;

error_clean:
	pm_runtime_disable(flash->dev);
	pm_runtime_set_suspended(flash->dev);

	lm3560_subdev_cleanup(flash);

error_disable:
	gpiod_set_value_cansleep(flash->hwen_gpio, 0);
	regulator_disable(flash->vin_supply);

	return rval;
}

static void lm3560_remove(struct i2c_client *client)
{
	struct lm3560_flash *flash = i2c_get_clientdata(client);

	lm3560_subdev_cleanup(flash);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		lm3560_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static const struct dev_pm_ops lm3560_pm_ops = {
	SET_RUNTIME_PM_OPS(lm3560_power_off, lm3560_power_on, NULL)
};

static const struct lm3560_flash_config lm3559_config = {
	.flash_brt_min_ua = 56250,
	.flash_brt_max_ua = 900000,
	.flash_brt_step_ua = 56250,

	.torch_brt_min_ua = 28125,
	.torch_brt_max_ua = 225000,
	.torch_brt_step_ua = 28125,
};

static const struct lm3560_flash_config lm3560_config = {
	.flash_brt_min_ua = 62500,
	.flash_brt_max_ua = 1000000,
	.flash_brt_step_ua = 62500,

	.torch_brt_min_ua = 31250,
	.torch_brt_max_ua = 250000,
	.torch_brt_step_ua = 31250,
};

static const struct of_device_id lm3560_of_match[] = {
	{ .compatible = "ti,lm3559", .data = &lm3559_config },
	{ .compatible = "ti,lm3560", .data = &lm3560_config },
	{ }
};
MODULE_DEVICE_TABLE(of, lm3560_of_match);

static const struct i2c_device_id lm3560_id_table[] = {
	{ .name = "lm3559", .driver_data = (kernel_ulong_t)&lm3559_config },
	{ .name = "lm3560", .driver_data = (kernel_ulong_t)&lm3560_config },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3560_id_table);

static struct i2c_driver lm3560_i2c_driver = {
	.driver = {
		   .name = "lm3560",
		   .pm = pm_ptr(&lm3560_pm_ops),
		   .of_match_table = lm3560_of_match,
		   },
	.probe = lm3560_probe,
	.remove = lm3560_remove,
	.id_table = lm3560_id_table,
};

module_i2c_driver(lm3560_i2c_driver);

MODULE_AUTHOR("Daniel Jeong <gshark.jeong@gmail.com>");
MODULE_AUTHOR("Ldd Mlp <ldd-mlp@list.ti.com>");
MODULE_DESCRIPTION("Texas Instruments LM3560 LED flash driver");
MODULE_LICENSE("GPL");
