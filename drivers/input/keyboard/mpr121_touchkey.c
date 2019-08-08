// SPDX-License-Identifier: GPL-2.0-only
/*
 * Touchkey driver for Freescale MPR121 Controllor
 *
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 * Author: Zhang Jiejing <jiejing.zhang@freescale.com>
 *
 * Based on mcs_touchkey.c
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/* Register definitions */
#define ELE_TOUCH_STATUS_0_ADDR	0x0
#define ELE_TOUCH_STATUS_1_ADDR	0X1
#define MHD_RISING_ADDR		0x2b
#define NHD_RISING_ADDR		0x2c
#define NCL_RISING_ADDR		0x2d
#define FDL_RISING_ADDR		0x2e
#define MHD_FALLING_ADDR	0x2f
#define NHD_FALLING_ADDR	0x30
#define NCL_FALLING_ADDR	0x31
#define FDL_FALLING_ADDR	0x32
#define ELE0_TOUCH_THRESHOLD_ADDR	0x41
#define ELE0_RELEASE_THRESHOLD_ADDR	0x42
#define AFE_CONF_ADDR			0x5c
#define FILTER_CONF_ADDR		0x5d

/*
 * ELECTRODE_CONF_ADDR: This register configures the number of
 * enabled capacitance sensing inputs and its run/suspend mode.
 */
#define ELECTRODE_CONF_ADDR		0x5e
#define ELECTRODE_CONF_QUICK_CHARGE	0x80
#define AUTO_CONFIG_CTRL_ADDR		0x7b
#define AUTO_CONFIG_USL_ADDR		0x7d
#define AUTO_CONFIG_LSL_ADDR		0x7e
#define AUTO_CONFIG_TL_ADDR		0x7f

/* Threshold of touch/release trigger */
#define TOUCH_THRESHOLD			0x08
#define RELEASE_THRESHOLD		0x05
/* Masks for touch and release triggers */
#define TOUCH_STATUS_MASK		0xfff
/* MPR121 has 12 keys */
#define MPR121_MAX_KEY_COUNT		12

struct mpr121_touchkey {
	struct i2c_client	*client;
	struct input_dev	*input_dev;
	unsigned int		statusbits;
	unsigned int		keycount;
	u32			keycodes[MPR121_MAX_KEY_COUNT];
};

struct mpr121_init_register {
	int addr;
	u8 val;
};

static const struct mpr121_init_register init_reg_table[] = {
	{ MHD_RISING_ADDR,	0x1 },
	{ NHD_RISING_ADDR,	0x1 },
	{ MHD_FALLING_ADDR,	0x1 },
	{ NHD_FALLING_ADDR,	0x1 },
	{ NCL_FALLING_ADDR,	0xff },
	{ FDL_FALLING_ADDR,	0x02 },
	{ FILTER_CONF_ADDR,	0x04 },
	{ AFE_CONF_ADDR,	0x0b },
	{ AUTO_CONFIG_CTRL_ADDR, 0x0b },
};

static void mpr121_vdd_supply_disable(void *data)
{
	struct regulator *vdd_supply = data;

	regulator_disable(vdd_supply);
}

static struct regulator *mpr121_vdd_supply_init(struct device *dev)
{
	struct regulator *vdd_supply;
	int err;

	vdd_supply = devm_regulator_get(dev, "vdd");
	if (IS_ERR(vdd_supply)) {
		dev_err(dev, "failed to get vdd regulator: %ld\n",
			PTR_ERR(vdd_supply));
		return vdd_supply;
	}

	err = regulator_enable(vdd_supply);
	if (err) {
		dev_err(dev, "failed to enable vdd regulator: %d\n", err);
		return ERR_PTR(err);
	}

	err = devm_add_action(dev, mpr121_vdd_supply_disable, vdd_supply);
	if (err) {
		regulator_disable(vdd_supply);
		dev_err(dev, "failed to add disable regulator action: %d\n",
			err);
		return ERR_PTR(err);
	}

	return vdd_supply;
}

static irqreturn_t mpr_touchkey_interrupt(int irq, void *dev_id)
{
	struct mpr121_touchkey *mpr121 = dev_id;
	struct i2c_client *client = mpr121->client;
	struct input_dev *input = mpr121->input_dev;
	unsigned long bit_changed;
	unsigned int key_num;
	int reg;

	reg = i2c_smbus_read_byte_data(client, ELE_TOUCH_STATUS_1_ADDR);
	if (reg < 0) {
		dev_err(&client->dev, "i2c read error [%d]\n", reg);
		goto out;
	}

	reg <<= 8;
	reg |= i2c_smbus_read_byte_data(client, ELE_TOUCH_STATUS_0_ADDR);
	if (reg < 0) {
		dev_err(&client->dev, "i2c read error [%d]\n", reg);
		goto out;
	}

	reg &= TOUCH_STATUS_MASK;
	/* use old press bit to figure out which bit changed */
	bit_changed = reg ^ mpr121->statusbits;
	mpr121->statusbits = reg;
	for_each_set_bit(key_num, &bit_changed, mpr121->keycount) {
		unsigned int key_val, pressed;

		pressed = reg & BIT(key_num);
		key_val = mpr121->keycodes[key_num];

		input_event(input, EV_MSC, MSC_SCAN, key_num);
		input_report_key(input, key_val, pressed);

		dev_dbg(&client->dev, "key %d %d %s\n", key_num, key_val,
			pressed ? "pressed" : "released");

	}
	input_sync(input);

out:
	return IRQ_HANDLED;
}

static int mpr121_phys_init(struct mpr121_touchkey *mpr121,
			    struct i2c_client *client, int vdd_uv)
{
	const struct mpr121_init_register *reg;
	unsigned char usl, lsl, tl, eleconf;
	int i, t, vdd, ret;

	/* Set up touch/release threshold for ele0-ele11 */
	for (i = 0; i <= MPR121_MAX_KEY_COUNT; i++) {
		t = ELE0_TOUCH_THRESHOLD_ADDR + (i * 2);
		ret = i2c_smbus_write_byte_data(client, t, TOUCH_THRESHOLD);
		if (ret < 0)
			goto err_i2c_write;
		ret = i2c_smbus_write_byte_data(client, t + 1,
						RELEASE_THRESHOLD);
		if (ret < 0)
			goto err_i2c_write;
	}

	/* Set up init register */
	for (i = 0; i < ARRAY_SIZE(init_reg_table); i++) {
		reg = &init_reg_table[i];
		ret = i2c_smbus_write_byte_data(client, reg->addr, reg->val);
		if (ret < 0)
			goto err_i2c_write;
	}


	/*
	 * Capacitance on sensing input varies and needs to be compensated.
	 * The internal MPR121-auto-configuration can do this if it's
	 * registers are set properly (based on vdd_uv).
	 */
	vdd = vdd_uv / 1000;
	usl = ((vdd - 700) * 256) / vdd;
	lsl = (usl * 65) / 100;
	tl = (usl * 90) / 100;
	ret = i2c_smbus_write_byte_data(client, AUTO_CONFIG_USL_ADDR, usl);
	ret |= i2c_smbus_write_byte_data(client, AUTO_CONFIG_LSL_ADDR, lsl);
	ret |= i2c_smbus_write_byte_data(client, AUTO_CONFIG_TL_ADDR, tl);

	/*
	 * Quick charge bit will let the capacitive charge to ready
	 * state quickly, or the buttons may not function after system
	 * boot.
	 */
	eleconf = mpr121->keycount | ELECTRODE_CONF_QUICK_CHARGE;
	ret |= i2c_smbus_write_byte_data(client, ELECTRODE_CONF_ADDR,
					 eleconf);
	if (ret != 0)
		goto err_i2c_write;

	dev_dbg(&client->dev, "set up with %x keys.\n", mpr121->keycount);

	return 0;

err_i2c_write:
	dev_err(&client->dev, "i2c write error: %d\n", ret);
	return ret;
}

static int mpr_touchkey_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct regulator *vdd_supply;
	int vdd_uv;
	struct mpr121_touchkey *mpr121;
	struct input_dev *input_dev;
	int error;
	int i;

	if (!client->irq) {
		dev_err(dev, "irq number should not be zero\n");
		return -EINVAL;
	}

	vdd_supply = mpr121_vdd_supply_init(dev);
	if (IS_ERR(vdd_supply))
		return PTR_ERR(vdd_supply);

	vdd_uv = regulator_get_voltage(vdd_supply);

	mpr121 = devm_kzalloc(dev, sizeof(*mpr121), GFP_KERNEL);
	if (!mpr121)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(dev);
	if (!input_dev)
		return -ENOMEM;

	mpr121->client = client;
	mpr121->input_dev = input_dev;
	mpr121->keycount = device_property_read_u32_array(dev, "linux,keycodes",
							  NULL, 0);
	if (mpr121->keycount > MPR121_MAX_KEY_COUNT) {
		dev_err(dev, "too many keys defined (%d)\n", mpr121->keycount);
		return -EINVAL;
	}

	error = device_property_read_u32_array(dev, "linux,keycodes",
					       mpr121->keycodes,
					       mpr121->keycount);
	if (error) {
		dev_err(dev,
			"failed to read linux,keycode property: %d\n", error);
		return error;
	}

	input_dev->name = "Freescale MPR121 Touchkey";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = dev;
	if (device_property_read_bool(dev, "autorepeat"))
		__set_bit(EV_REP, input_dev->evbit);
	input_set_capability(input_dev, EV_MSC, MSC_SCAN);

	input_dev->keycode = mpr121->keycodes;
	input_dev->keycodesize = sizeof(mpr121->keycodes[0]);
	input_dev->keycodemax = mpr121->keycount;

	for (i = 0; i < mpr121->keycount; i++)
		input_set_capability(input_dev, EV_KEY, mpr121->keycodes[i]);

	error = mpr121_phys_init(mpr121, client, vdd_uv);
	if (error) {
		dev_err(dev, "Failed to init register\n");
		return error;
	}

	error = devm_request_threaded_irq(dev, client->irq, NULL,
					  mpr_touchkey_interrupt,
					  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					  dev->driver->name, mpr121);
	if (error) {
		dev_err(dev, "Failed to register interrupt\n");
		return error;
	}

	error = input_register_device(input_dev);
	if (error)
		return error;

	i2c_set_clientdata(client, mpr121);
	device_init_wakeup(dev,
			device_property_read_bool(dev, "wakeup-source"));

	return 0;
}

static int __maybe_unused mpr_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	i2c_smbus_write_byte_data(client, ELECTRODE_CONF_ADDR, 0x00);

	return 0;
}

static int __maybe_unused mpr_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mpr121_touchkey *mpr121 = i2c_get_clientdata(client);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	i2c_smbus_write_byte_data(client, ELECTRODE_CONF_ADDR,
				  mpr121->keycount);

	return 0;
}

static SIMPLE_DEV_PM_OPS(mpr121_touchkey_pm_ops, mpr_suspend, mpr_resume);

static const struct i2c_device_id mpr121_id[] = {
	{ "mpr121_touchkey", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mpr121_id);

#ifdef CONFIG_OF
static const struct of_device_id mpr121_touchkey_dt_match_table[] = {
	{ .compatible = "fsl,mpr121-touchkey" },
	{ },
};
MODULE_DEVICE_TABLE(of, mpr121_touchkey_dt_match_table);
#endif

static struct i2c_driver mpr_touchkey_driver = {
	.driver = {
		.name	= "mpr121",
		.pm	= &mpr121_touchkey_pm_ops,
		.of_match_table = of_match_ptr(mpr121_touchkey_dt_match_table),
	},
	.id_table	= mpr121_id,
	.probe		= mpr_touchkey_probe,
};

module_i2c_driver(mpr_touchkey_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhang Jiejing <jiejing.zhang@freescale.com>");
MODULE_DESCRIPTION("Touch Key driver for Freescale MPR121 Chip");
