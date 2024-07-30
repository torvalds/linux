// SPDX-License-Identifier: GPL-2.0-only
/*
 * LP5521 LED chip driver.
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2012 Texas Instruments
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *          Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "leds-lp55xx-common.h"

#define LP5521_MAX_LEDS			3
#define LP5521_CMD_DIRECT		0x3F

/* Registers */
#define LP5521_REG_ENABLE		0x00
#define LP5521_REG_OP_MODE		0x01
#define LP5521_REG_R_PWM		0x02
#define LP5521_REG_G_PWM		0x03
#define LP5521_REG_B_PWM		0x04
#define LP5521_REG_R_CURRENT		0x05
#define LP5521_REG_G_CURRENT		0x06
#define LP5521_REG_B_CURRENT		0x07
#define LP5521_REG_CONFIG		0x08
#define LP5521_REG_STATUS		0x0C
#define LP5521_REG_RESET		0x0D
#define LP5521_REG_R_PROG_MEM		0x10
#define LP5521_REG_G_PROG_MEM		0x30
#define LP5521_REG_B_PROG_MEM		0x50

/* Base register to set LED current */
#define LP5521_REG_LED_CURRENT_BASE	LP5521_REG_R_CURRENT
/* Base register to set the brightness */
#define LP5521_REG_LED_PWM_BASE		LP5521_REG_R_PWM

/* Bits in ENABLE register */
#define LP5521_MASTER_ENABLE		0x40	/* Chip master enable */
#define LP5521_LOGARITHMIC_PWM		0x80	/* Logarithmic PWM adjustment */
#define LP5521_EXEC_RUN			0x2A
#define LP5521_ENABLE_DEFAULT	\
	(LP5521_MASTER_ENABLE | LP5521_LOGARITHMIC_PWM)
#define LP5521_ENABLE_RUN_PROGRAM	\
	(LP5521_ENABLE_DEFAULT | LP5521_EXEC_RUN)

/* CONFIG register */
#define LP5521_PWM_HF			0x40	/* PWM: 0 = 256Hz, 1 = 558Hz */
#define LP5521_PWRSAVE_EN		0x20	/* 1 = Power save mode */
#define LP5521_CP_MODE_MASK		0x18	/* Charge pump mode */
#define LP5521_CP_MODE_SHIFT		3
#define LP5521_R_TO_BATT		0x04	/* R out: 0 = CP, 1 = Vbat */
#define LP5521_CLK_INT			0x01	/* Internal clock */
#define LP5521_DEFAULT_CFG		(LP5521_PWM_HF | LP5521_PWRSAVE_EN)

/* Status */
#define LP5521_EXT_CLK_USED		0x08

/* default R channel current register value */
#define LP5521_REG_R_CURR_DEFAULT	0xAF

/* Reset register value */
#define LP5521_RESET			0xFF

static inline void lp5521_wait_opmode_done(void)
{
	/* operation mode change needs to be longer than 153 us */
	usleep_range(200, 300);
}

static inline void lp5521_wait_enable_done(void)
{
	/* it takes more 488 us to update ENABLE register */
	usleep_range(500, 600);
}

static void lp5521_run_engine(struct lp55xx_chip *chip, bool start)
{
	int ret;

	/* stop engine */
	if (!start) {
		lp55xx_stop_engine(chip);
		lp55xx_write(chip, LP5521_REG_OP_MODE, LP5521_CMD_DIRECT);
		lp5521_wait_opmode_done();
		return;
	}

	ret = lp55xx_run_engine_common(chip);
	if (!ret)
		lp5521_wait_enable_done();
}

static int lp5521_post_init_device(struct lp55xx_chip *chip)
{
	int ret;
	u8 val;

	/*
	 * Make sure that the chip is reset by reading back the r channel
	 * current reg. This is dummy read is required on some platforms -
	 * otherwise further access to the R G B channels in the
	 * LP5521_REG_ENABLE register will not have any effect - strange!
	 */
	ret = lp55xx_read(chip, LP5521_REG_R_CURRENT, &val);
	if (ret) {
		dev_err(&chip->cl->dev, "error in resetting chip\n");
		return ret;
	}
	if (val != LP5521_REG_R_CURR_DEFAULT) {
		dev_err(&chip->cl->dev,
			"unexpected data in register (expected 0x%x got 0x%x)\n",
			LP5521_REG_R_CURR_DEFAULT, val);
		ret = -EINVAL;
		return ret;
	}
	usleep_range(10000, 20000);

	/* Set all PWMs to direct control mode */
	ret = lp55xx_write(chip, LP5521_REG_OP_MODE, LP5521_CMD_DIRECT);
	if (ret)
		return ret;

	/* Update configuration for the clock setting */
	val = LP5521_DEFAULT_CFG;
	if (!lp55xx_is_extclk_used(chip))
		val |= LP5521_CLK_INT;

	val |= (chip->pdata->charge_pump_mode << LP5521_CP_MODE_SHIFT) & LP5521_CP_MODE_MASK;

	ret = lp55xx_write(chip, LP5521_REG_CONFIG, val);
	if (ret)
		return ret;

	/* Initialize all channels PWM to zero -> leds off */
	lp55xx_write(chip, LP5521_REG_R_PWM, 0);
	lp55xx_write(chip, LP5521_REG_G_PWM, 0);
	lp55xx_write(chip, LP5521_REG_B_PWM, 0);

	/* Set engines are set to run state when OP_MODE enables engines */
	ret = lp55xx_write(chip, LP5521_REG_ENABLE, LP5521_ENABLE_RUN_PROGRAM);
	if (ret)
		return ret;

	lp5521_wait_enable_done();

	return 0;
}

static int lp5521_run_selftest(struct lp55xx_chip *chip, char *buf)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	int ret;
	u8 status;

	ret = lp55xx_read(chip, LP5521_REG_STATUS, &status);
	if (ret < 0)
		return ret;

	if (pdata->clock_mode != LP55XX_CLOCK_EXT)
		return 0;

	/* Check that ext clock is really in use if requested */
	if  ((status & LP5521_EXT_CLK_USED) == 0)
		return -EIO;

	return 0;
}

static ssize_t lp5521_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	int ret;

	guard(mutex)(&chip->lock);

	ret = lp5521_run_selftest(chip, buf);

	return sysfs_emit(buf, "%s\n", ret ? "FAIL" : "OK");
}

/* device attributes */
LP55XX_DEV_ATTR_ENGINE_MODE(1);
LP55XX_DEV_ATTR_ENGINE_MODE(2);
LP55XX_DEV_ATTR_ENGINE_MODE(3);
LP55XX_DEV_ATTR_ENGINE_LOAD(1);
LP55XX_DEV_ATTR_ENGINE_LOAD(2);
LP55XX_DEV_ATTR_ENGINE_LOAD(3);
static LP55XX_DEV_ATTR_RO(selftest, lp5521_selftest);

static struct attribute *lp5521_attributes[] = {
	&dev_attr_engine1_mode.attr,
	&dev_attr_engine2_mode.attr,
	&dev_attr_engine3_mode.attr,
	&dev_attr_engine1_load.attr,
	&dev_attr_engine2_load.attr,
	&dev_attr_engine3_load.attr,
	&dev_attr_selftest.attr,
	NULL
};

static const struct attribute_group lp5521_group = {
	.attrs = lp5521_attributes,
};

/* Chip specific configurations */
static struct lp55xx_device_config lp5521_cfg = {
	.reg_op_mode = {
		.addr = LP5521_REG_OP_MODE,
	},
	.reg_exec = {
		.addr = LP5521_REG_ENABLE,
	},
	.reset = {
		.addr = LP5521_REG_RESET,
		.val  = LP5521_RESET,
	},
	.enable = {
		.addr = LP5521_REG_ENABLE,
		.val  = LP5521_ENABLE_DEFAULT,
	},
	.prog_mem_base = {
		.addr = LP5521_REG_R_PROG_MEM,
	},
	.reg_led_pwm_base = {
		.addr = LP5521_REG_LED_PWM_BASE,
	},
	.reg_led_current_base = {
		.addr = LP5521_REG_LED_CURRENT_BASE,
	},
	.max_channel  = LP5521_MAX_LEDS,
	.post_init_device   = lp5521_post_init_device,
	.brightness_fn      = lp55xx_led_brightness,
	.multicolor_brightness_fn = lp55xx_multicolor_brightness,
	.set_led_current    = lp55xx_set_led_current,
	.firmware_cb        = lp55xx_firmware_loaded_cb,
	.run_engine         = lp5521_run_engine,
	.dev_attr_group     = &lp5521_group,
};

static const struct i2c_device_id lp5521_id[] = {
	{ "lp5521", .driver_data = (kernel_ulong_t)&lp5521_cfg, }, /* Three channel chip */
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp5521_id);

static const struct of_device_id of_lp5521_leds_match[] = {
	{ .compatible = "national,lp5521", .data = &lp5521_cfg, },
	{},
};

MODULE_DEVICE_TABLE(of, of_lp5521_leds_match);

static struct i2c_driver lp5521_driver = {
	.driver = {
		.name	= "lp5521",
		.of_match_table = of_lp5521_leds_match,
	},
	.probe		= lp55xx_probe,
	.remove		= lp55xx_remove,
	.id_table	= lp5521_id,
};

module_i2c_driver(lp5521_driver);

MODULE_AUTHOR("Mathias Nyman, Yuri Zaporozhets, Samu Onkalo");
MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP5521 LED engine");
MODULE_LICENSE("GPL v2");
