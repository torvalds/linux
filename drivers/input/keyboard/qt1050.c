// SPDX-License-Identifier: GPL-2.0
/*
 *  Microchip AT42QT1050 QTouch Sensor Controller
 *
 *  Copyright (C) 2019 Pengutronix, Marco Felsch <kernel@pengutronix.de>
 *
 *  Base on AT42QT1070 driver by:
 *  Bo Shen <voice.shen@atmel.com>
 *  Copyright (C) 2011 Atmel
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

/* Chip ID */
#define QT1050_CHIP_ID		0x00
#define QT1050_CHIP_ID_VER	0x46

/* Firmware version */
#define QT1050_FW_VERSION	0x01

/* Detection status */
#define QT1050_DET_STATUS	0x02

/* Key status */
#define QT1050_KEY_STATUS	0x03

/* Key Signals */
#define QT1050_KEY_SIGNAL_0_MSB	0x06
#define QT1050_KEY_SIGNAL_0_LSB	0x07
#define QT1050_KEY_SIGNAL_1_MSB	0x08
#define QT1050_KEY_SIGNAL_1_LSB	0x09
#define QT1050_KEY_SIGNAL_2_MSB	0x0c
#define QT1050_KEY_SIGNAL_2_LSB	0x0d
#define QT1050_KEY_SIGNAL_3_MSB	0x0e
#define QT1050_KEY_SIGNAL_3_LSB	0x0f
#define QT1050_KEY_SIGNAL_4_MSB	0x10
#define QT1050_KEY_SIGNAL_4_LSB	0x11

/* Reference data */
#define QT1050_REF_DATA_0_MSB	0x14
#define QT1050_REF_DATA_0_LSB	0x15
#define QT1050_REF_DATA_1_MSB	0x16
#define QT1050_REF_DATA_1_LSB	0x17
#define QT1050_REF_DATA_2_MSB	0x1a
#define QT1050_REF_DATA_2_LSB	0x1b
#define QT1050_REF_DATA_3_MSB	0x1c
#define QT1050_REF_DATA_3_LSB	0x1d
#define QT1050_REF_DATA_4_MSB	0x1e
#define QT1050_REF_DATA_4_LSB	0x1f

/* Negative threshold level */
#define QT1050_NTHR_0		0x21
#define QT1050_NTHR_1		0x22
#define QT1050_NTHR_2		0x24
#define QT1050_NTHR_3		0x25
#define QT1050_NTHR_4		0x26

/* Pulse / Scale  */
#define QT1050_PULSE_SCALE_0	0x28
#define QT1050_PULSE_SCALE_1	0x29
#define QT1050_PULSE_SCALE_2	0x2b
#define QT1050_PULSE_SCALE_3	0x2c
#define QT1050_PULSE_SCALE_4	0x2d

/* Detection integrator counter / AKS */
#define QT1050_DI_AKS_0		0x2f
#define QT1050_DI_AKS_1		0x30
#define QT1050_DI_AKS_2		0x32
#define QT1050_DI_AKS_3		0x33
#define QT1050_DI_AKS_4		0x34

/* Charge Share Delay */
#define QT1050_CSD_0		0x36
#define QT1050_CSD_1		0x37
#define QT1050_CSD_2		0x39
#define QT1050_CSD_3		0x3a
#define QT1050_CSD_4		0x3b

/* Low Power Mode */
#define QT1050_LPMODE		0x3d

/* Calibration and Reset */
#define QT1050_RES_CAL		0x3f
#define QT1050_RES_CAL_RESET		BIT(7)
#define QT1050_RES_CAL_CALIBRATE	BIT(1)

#define QT1050_MAX_KEYS		5
#define QT1050_RESET_TIME	255

struct qt1050_key_regs {
	unsigned int nthr;
	unsigned int pulse_scale;
	unsigned int di_aks;
	unsigned int csd;
};

struct qt1050_key {
	u32 num;
	u32 charge_delay;
	u32 thr_cnt;
	u32 samples;
	u32 scale;
	u32 keycode;
};

struct qt1050_priv {
	struct i2c_client	*client;
	struct input_dev	*input;
	struct regmap		*regmap;
	struct qt1050_key	keys[QT1050_MAX_KEYS];
	unsigned short		keycodes[QT1050_MAX_KEYS];
	u8			reg_keys;
	u8			last_keys;
};

static const struct qt1050_key_regs qt1050_key_regs_data[] = {
	{
		.nthr = QT1050_NTHR_0,
		.pulse_scale = QT1050_PULSE_SCALE_0,
		.di_aks = QT1050_DI_AKS_0,
		.csd = QT1050_CSD_0,
	}, {
		.nthr = QT1050_NTHR_1,
		.pulse_scale = QT1050_PULSE_SCALE_1,
		.di_aks = QT1050_DI_AKS_1,
		.csd = QT1050_CSD_1,
	}, {
		.nthr = QT1050_NTHR_2,
		.pulse_scale = QT1050_PULSE_SCALE_2,
		.di_aks = QT1050_DI_AKS_2,
		.csd = QT1050_CSD_2,
	}, {
		.nthr = QT1050_NTHR_3,
		.pulse_scale = QT1050_PULSE_SCALE_3,
		.di_aks = QT1050_DI_AKS_3,
		.csd = QT1050_CSD_3,
	}, {
		.nthr = QT1050_NTHR_4,
		.pulse_scale = QT1050_PULSE_SCALE_4,
		.di_aks = QT1050_DI_AKS_4,
		.csd = QT1050_CSD_4,
	}
};

static bool qt1050_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case QT1050_DET_STATUS:
	case QT1050_KEY_STATUS:
	case QT1050_KEY_SIGNAL_0_MSB:
	case QT1050_KEY_SIGNAL_0_LSB:
	case QT1050_KEY_SIGNAL_1_MSB:
	case QT1050_KEY_SIGNAL_1_LSB:
	case QT1050_KEY_SIGNAL_2_MSB:
	case QT1050_KEY_SIGNAL_2_LSB:
	case QT1050_KEY_SIGNAL_3_MSB:
	case QT1050_KEY_SIGNAL_3_LSB:
	case QT1050_KEY_SIGNAL_4_MSB:
	case QT1050_KEY_SIGNAL_4_LSB:
		return true;
	default:
		return false;
	}
}

static const struct regmap_range qt1050_readable_ranges[] = {
	regmap_reg_range(QT1050_CHIP_ID, QT1050_KEY_STATUS),
	regmap_reg_range(QT1050_KEY_SIGNAL_0_MSB, QT1050_KEY_SIGNAL_1_LSB),
	regmap_reg_range(QT1050_KEY_SIGNAL_2_MSB, QT1050_KEY_SIGNAL_4_LSB),
	regmap_reg_range(QT1050_REF_DATA_0_MSB, QT1050_REF_DATA_1_LSB),
	regmap_reg_range(QT1050_REF_DATA_2_MSB, QT1050_REF_DATA_4_LSB),
	regmap_reg_range(QT1050_NTHR_0, QT1050_NTHR_1),
	regmap_reg_range(QT1050_NTHR_2, QT1050_NTHR_4),
	regmap_reg_range(QT1050_PULSE_SCALE_0, QT1050_PULSE_SCALE_1),
	regmap_reg_range(QT1050_PULSE_SCALE_2, QT1050_PULSE_SCALE_4),
	regmap_reg_range(QT1050_DI_AKS_0, QT1050_DI_AKS_1),
	regmap_reg_range(QT1050_DI_AKS_2, QT1050_DI_AKS_4),
	regmap_reg_range(QT1050_CSD_0, QT1050_CSD_1),
	regmap_reg_range(QT1050_CSD_2, QT1050_RES_CAL),
};

static const struct regmap_access_table qt1050_readable_table = {
	.yes_ranges = qt1050_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qt1050_readable_ranges),
};

static const struct regmap_range qt1050_writeable_ranges[] = {
	regmap_reg_range(QT1050_NTHR_0, QT1050_NTHR_1),
	regmap_reg_range(QT1050_NTHR_2, QT1050_NTHR_4),
	regmap_reg_range(QT1050_PULSE_SCALE_0, QT1050_PULSE_SCALE_1),
	regmap_reg_range(QT1050_PULSE_SCALE_2, QT1050_PULSE_SCALE_4),
	regmap_reg_range(QT1050_DI_AKS_0, QT1050_DI_AKS_1),
	regmap_reg_range(QT1050_DI_AKS_2, QT1050_DI_AKS_4),
	regmap_reg_range(QT1050_CSD_0, QT1050_CSD_1),
	regmap_reg_range(QT1050_CSD_2, QT1050_RES_CAL),
};

static const struct regmap_access_table qt1050_writeable_table = {
	.yes_ranges = qt1050_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(qt1050_writeable_ranges),
};

static struct regmap_config qt1050_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = QT1050_RES_CAL,

	.cache_type = REGCACHE_MAPLE,

	.wr_table = &qt1050_writeable_table,
	.rd_table = &qt1050_readable_table,
	.volatile_reg = qt1050_volatile_reg,
};

static bool qt1050_identify(struct qt1050_priv *ts)
{
	unsigned int val;
	int err;

	/* Read Chip ID */
	regmap_read(ts->regmap, QT1050_CHIP_ID, &val);
	if (val != QT1050_CHIP_ID_VER) {
		dev_err(&ts->client->dev, "ID %d not supported\n", val);
		return false;
	}

	/* Read firmware version */
	err = regmap_read(ts->regmap, QT1050_FW_VERSION, &val);
	if (err) {
		dev_err(&ts->client->dev, "could not read the firmware version\n");
		return false;
	}

	dev_info(&ts->client->dev, "AT42QT1050 firmware version %1d.%1d\n",
		 val >> 4, val & 0xf);

	return true;
}

static irqreturn_t qt1050_irq_threaded(int irq, void *dev_id)
{
	struct qt1050_priv *ts = dev_id;
	struct input_dev *input = ts->input;
	unsigned long new_keys, changed;
	unsigned int val;
	int i, err;

	/* Read the detected status register, thus clearing interrupt */
	err = regmap_read(ts->regmap, QT1050_DET_STATUS, &val);
	if (err) {
		dev_err(&ts->client->dev, "Fail to read detection status: %d\n",
			err);
		return IRQ_NONE;
	}

	/* Read which key changed, keys are not continuous */
	err = regmap_read(ts->regmap, QT1050_KEY_STATUS, &val);
	if (err) {
		dev_err(&ts->client->dev,
			"Fail to determine the key status: %d\n", err);
		return IRQ_NONE;
	}
	new_keys = (val & 0x70) >> 2 | (val & 0x6) >> 1;
	changed = ts->last_keys ^ new_keys;
	/* Report registered keys only */
	changed &= ts->reg_keys;

	for_each_set_bit(i, &changed, QT1050_MAX_KEYS)
		input_report_key(input, ts->keys[i].keycode,
				 test_bit(i, &new_keys));

	ts->last_keys = new_keys;
	input_sync(input);

	return IRQ_HANDLED;
}

static const struct qt1050_key_regs *qt1050_get_key_regs(int key_num)
{
	return &qt1050_key_regs_data[key_num];
}

static int qt1050_set_key(struct regmap *map, int number, int on)
{
	const struct qt1050_key_regs *key_regs;

	key_regs = qt1050_get_key_regs(number);

	return regmap_update_bits(map, key_regs->di_aks, 0xfc,
				  on ? BIT(4) : 0x00);
}

static int qt1050_apply_fw_data(struct qt1050_priv *ts)
{
	struct regmap *map = ts->regmap;
	struct qt1050_key *button = &ts->keys[0];
	const struct qt1050_key_regs *key_regs;
	int i, err;

	/* Disable all keys and enable only the specified ones */
	for (i = 0; i < QT1050_MAX_KEYS; i++) {
		err = qt1050_set_key(map, i, 0);
		if (err)
			return err;
	}

	for (i = 0; i < QT1050_MAX_KEYS; i++, button++) {
		/* Keep KEY_RESERVED keys off */
		if (button->keycode == KEY_RESERVED)
			continue;

		err = qt1050_set_key(map, button->num, 1);
		if (err)
			return err;

		key_regs = qt1050_get_key_regs(button->num);

		err = regmap_write(map, key_regs->pulse_scale,
				   (button->samples << 4) | (button->scale));
		if (err)
			return err;
		err = regmap_write(map, key_regs->csd, button->charge_delay);
		if (err)
			return err;
		err = regmap_write(map, key_regs->nthr, button->thr_cnt);
		if (err)
			return err;
	}

	return 0;
}

static int qt1050_parse_fw(struct qt1050_priv *ts)
{
	struct device *dev = &ts->client->dev;
	struct fwnode_handle *child;
	int nbuttons;

	nbuttons = device_get_child_node_count(dev);
	if (nbuttons == 0 || nbuttons > QT1050_MAX_KEYS)
		return -ENODEV;

	device_for_each_child_node(dev, child) {
		struct qt1050_key button;

		/* Required properties */
		if (fwnode_property_read_u32(child, "linux,code",
					     &button.keycode)) {
			dev_err(dev, "Button without keycode\n");
			goto err;
		}
		if (button.keycode >= KEY_MAX) {
			dev_err(dev, "Invalid keycode 0x%x\n",
				button.keycode);
			goto err;
		}

		if (fwnode_property_read_u32(child, "reg",
					     &button.num)) {
			dev_err(dev, "Button without pad number\n");
			goto err;
		}
		if (button.num < 0 || button.num > QT1050_MAX_KEYS - 1)
			goto err;

		ts->reg_keys |= BIT(button.num);

		/* Optional properties */
		if (fwnode_property_read_u32(child,
					     "microchip,pre-charge-time-ns",
					     &button.charge_delay)) {
			button.charge_delay = 0;
		} else {
			if (button.charge_delay % 2500 == 0)
				button.charge_delay =
					button.charge_delay / 2500;
			else
				button.charge_delay = 0;
		}

		if (fwnode_property_read_u32(child, "microchip,average-samples",
					 &button.samples)) {
			button.samples = 0;
		} else {
			if (is_power_of_2(button.samples))
				button.samples = ilog2(button.samples);
			else
				button.samples = 0;
		}

		if (fwnode_property_read_u32(child, "microchip,average-scaling",
					     &button.scale)) {
			button.scale = 0;
		} else {
			if (is_power_of_2(button.scale))
				button.scale = ilog2(button.scale);
			else
				button.scale = 0;

		}

		if (fwnode_property_read_u32(child, "microchip,threshold",
					 &button.thr_cnt)) {
			button.thr_cnt = 20;
		} else {
			if (button.thr_cnt > 255)
				button.thr_cnt = 20;
		}

		ts->keys[button.num] = button;
	}

	return 0;

err:
	fwnode_handle_put(child);
	return -EINVAL;
}

static int qt1050_probe(struct i2c_client *client)
{
	struct qt1050_priv *ts;
	struct input_dev *input;
	struct device *dev = &client->dev;
	struct regmap *map;
	unsigned int status, i;
	int err;

	/* Check basic functionality */
	err = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE);
	if (!err) {
		dev_err(&client->dev, "%s adapter not supported\n",
			dev_driver_string(&client->adapter->dev));
		return -ENODEV;
	}

	if (!client->irq) {
		dev_err(dev, "assign a irq line to this device\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	map = devm_regmap_init_i2c(client, &qt1050_regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);

	ts->client = client;
	ts->input = input;
	ts->regmap = map;

	i2c_set_clientdata(client, ts);

	/* Identify the qt1050 chip */
	if (!qt1050_identify(ts))
		return -ENODEV;

	/* Get pdata */
	err = qt1050_parse_fw(ts);
	if (err) {
		dev_err(dev, "Failed to parse firmware: %d\n", err);
		return err;
	}

	input->name = "AT42QT1050 QTouch Sensor";
	input->dev.parent = &client->dev;
	input->id.bustype = BUS_I2C;

	/* Add the keycode */
	input->keycode = ts->keycodes;
	input->keycodesize = sizeof(ts->keycodes[0]);
	input->keycodemax = QT1050_MAX_KEYS;

	__set_bit(EV_KEY, input->evbit);
	for (i = 0; i < QT1050_MAX_KEYS; i++) {
		ts->keycodes[i] = ts->keys[i].keycode;
		__set_bit(ts->keycodes[i], input->keybit);
	}

	/* Trigger re-calibration */
	err = regmap_update_bits(ts->regmap, QT1050_RES_CAL, 0x7f,
				 QT1050_RES_CAL_CALIBRATE);
	if (err) {
		dev_err(dev, "Trigger calibration failed: %d\n", err);
		return err;
	}
	err = regmap_read_poll_timeout(ts->regmap, QT1050_DET_STATUS, status,
				 status >> 7 == 1, 10000, 200000);
	if (err) {
		dev_err(dev, "Calibration failed: %d\n", err);
		return err;
	}

	/* Soft reset to set defaults */
	err = regmap_update_bits(ts->regmap, QT1050_RES_CAL,
				 QT1050_RES_CAL_RESET, QT1050_RES_CAL_RESET);
	if (err) {
		dev_err(dev, "Trigger soft reset failed: %d\n", err);
		return err;
	}
	msleep(QT1050_RESET_TIME);

	/* Set pdata */
	err = qt1050_apply_fw_data(ts);
	if (err) {
		dev_err(dev, "Failed to set firmware data: %d\n", err);
		return err;
	}

	err = devm_request_threaded_irq(dev, client->irq, NULL,
					qt1050_irq_threaded, IRQF_ONESHOT,
					"qt1050", ts);
	if (err) {
		dev_err(&client->dev, "Failed to request irq: %d\n", err);
		return err;
	}

	/* Clear #CHANGE line */
	err = regmap_read(ts->regmap, QT1050_DET_STATUS, &status);
	if (err) {
		dev_err(dev, "Failed to clear #CHANGE line level: %d\n", err);
		return err;
	}

	/* Register the input device */
	err = input_register_device(ts->input);
	if (err) {
		dev_err(&client->dev, "Failed to register input device: %d\n",
			err);
		return err;
	}

	return 0;
}

static int qt1050_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qt1050_priv *ts = i2c_get_clientdata(client);

	disable_irq(client->irq);

	/*
	 * Set measurement interval to 1s (125 x 8ms) if wakeup is allowed
	 * else turn off. The 1s interval seems to be a good compromise between
	 * low power and response time.
	 */
	return regmap_write(ts->regmap, QT1050_LPMODE,
			    device_may_wakeup(dev) ? 125 : 0);
}

static int qt1050_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qt1050_priv *ts = i2c_get_clientdata(client);

	enable_irq(client->irq);

	/* Set measurement interval back to 16ms (2 x 8ms) */
	return regmap_write(ts->regmap, QT1050_LPMODE, 2);
}

static DEFINE_SIMPLE_DEV_PM_OPS(qt1050_pm_ops, qt1050_suspend, qt1050_resume);

static const struct of_device_id __maybe_unused qt1050_of_match[] = {
	{ .compatible = "microchip,qt1050", },
	{ },
};
MODULE_DEVICE_TABLE(of, qt1050_of_match);

static struct i2c_driver qt1050_driver = {
	.driver	= {
		.name = "qt1050",
		.of_match_table = of_match_ptr(qt1050_of_match),
		.pm = pm_sleep_ptr(&qt1050_pm_ops),
	},
	.probe = qt1050_probe,
};

module_i2c_driver(qt1050_driver);

MODULE_AUTHOR("Marco Felsch <kernel@pengutronix.de");
MODULE_DESCRIPTION("Driver for AT42QT1050 QTouch sensor");
MODULE_LICENSE("GPL v2");
