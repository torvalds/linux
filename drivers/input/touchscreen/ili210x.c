// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <asm/unaligned.h>

#define ILI210X_TOUCHES		2
#define ILI251X_TOUCHES		10
#define DEFAULT_POLL_PERIOD	20

/* Touchscreen commands */
#define REG_TOUCHDATA		0x10
#define REG_PANEL_INFO		0x20
#define REG_FIRMWARE_VERSION	0x40
#define REG_CALIBRATE		0xcc

struct firmware_version {
	u8 id;
	u8 major;
	u8 minor;
} __packed;

enum ili2xxx_model {
	MODEL_ILI210X,
	MODEL_ILI251X,
};

struct ili210x {
	struct i2c_client *client;
	struct input_dev *input;
	unsigned int poll_period;
	struct delayed_work dwork;
	struct gpio_desc *reset_gpio;
	struct touchscreen_properties prop;
	enum ili2xxx_model model;
	unsigned int max_touches;
};

static int ili210x_read_reg(struct i2c_client *client, u8 reg, void *buf,
			    size_t len)
{
	struct ili210x *priv = i2c_get_clientdata(client);
	struct i2c_msg msg[2] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &reg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	if (priv->model == MODEL_ILI251X) {
		if (i2c_transfer(client->adapter, msg, 1) != 1) {
			dev_err(&client->dev, "i2c transfer failed\n");
			return -EIO;
		}

		usleep_range(5000, 5500);

		if (i2c_transfer(client->adapter, msg + 1, 1) != 1) {
			dev_err(&client->dev, "i2c transfer failed\n");
			return -EIO;
		}
	} else {
		if (i2c_transfer(client->adapter, msg, 2) != 2) {
			dev_err(&client->dev, "i2c transfer failed\n");
			return -EIO;
		}
	}

	return 0;
}

static int ili210x_read(struct i2c_client *client, void *buf, size_t len)
{
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= I2C_M_RD,
		.len	= len,
		.buf	= buf,
	};

	if (i2c_transfer(client->adapter, &msg, 1) != 1) {
		dev_err(&client->dev, "i2c transfer failed\n");
		return -EIO;
	}

	return 0;
}

static bool ili210x_touchdata_to_coords(struct ili210x *priv, u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y)
{
	if (finger >= ILI210X_TOUCHES)
		return false;

	if (touchdata[0] & BIT(finger))
		return false;

	*x = get_unaligned_be16(touchdata + 1 + (finger * 4) + 0);
	*y = get_unaligned_be16(touchdata + 1 + (finger * 4) + 2);

	return true;
}

static bool ili251x_touchdata_to_coords(struct ili210x *priv, u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y)
{
	if (finger >= ILI251X_TOUCHES)
		return false;

	*x = get_unaligned_be16(touchdata + 1 + (finger * 5) + 0);
	if (!(*x & BIT(15)))	/* Touch indication */
		return false;

	*x &= 0x3fff;
	*y = get_unaligned_be16(touchdata + 1 + (finger * 5) + 2);

	return true;
}

static bool ili210x_report_events(struct ili210x *priv, u8 *touchdata)
{
	struct input_dev *input = priv->input;
	int i;
	bool contact = false, touch = false;
	unsigned int x = 0, y = 0;

	for (i = 0; i < priv->max_touches; i++) {
		if (priv->model == MODEL_ILI210X) {
			touch = ili210x_touchdata_to_coords(priv, touchdata,
							    i, &x, &y);
		} else if (priv->model == MODEL_ILI251X) {
			touch = ili251x_touchdata_to_coords(priv, touchdata,
							    i, &x, &y);
			if (touch)
				contact = true;
		}

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (!touch)
			continue;
		touchscreen_report_pos(input, &priv->prop, x, y,
				       true);
	}

	input_mt_report_pointer_emulation(input, false);
	input_sync(input);

	if (priv->model == MODEL_ILI210X)
		contact = touchdata[0] & 0xf3;

	return contact;
}

static void ili210x_work(struct work_struct *work)
{
	struct ili210x *priv = container_of(work, struct ili210x,
					    dwork.work);
	struct i2c_client *client = priv->client;
	u8 touchdata[64] = { 0 };
	bool touch;
	int error = -EINVAL;

	if (priv->model == MODEL_ILI210X) {
		error = ili210x_read_reg(client, REG_TOUCHDATA,
					 touchdata, sizeof(touchdata));
	} else if (priv->model == MODEL_ILI251X) {
		error = ili210x_read_reg(client, REG_TOUCHDATA,
					 touchdata, 31);
		if (!error && touchdata[0] == 2)
			error = ili210x_read(client, &touchdata[31], 20);
	}

	if (error) {
		dev_err(&client->dev,
			"Unable to get touchdata, err = %d\n", error);
		return;
	}

	touch = ili210x_report_events(priv, touchdata);

	if (touch)
		schedule_delayed_work(&priv->dwork,
				      msecs_to_jiffies(priv->poll_period));
}

static irqreturn_t ili210x_irq(int irq, void *irq_data)
{
	struct ili210x *priv = irq_data;

	schedule_delayed_work(&priv->dwork, 0);

	return IRQ_HANDLED;
}

static ssize_t ili210x_calibrate(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	unsigned long calibrate;
	int rc;
	u8 cmd = REG_CALIBRATE;

	if (kstrtoul(buf, 10, &calibrate))
		return -EINVAL;

	if (calibrate > 1)
		return -EINVAL;

	if (calibrate) {
		rc = i2c_master_send(priv->client, &cmd, sizeof(cmd));
		if (rc != sizeof(cmd))
			return -EIO;
	}

	return count;
}
static DEVICE_ATTR(calibrate, S_IWUSR, NULL, ili210x_calibrate);

static struct attribute *ili210x_attributes[] = {
	&dev_attr_calibrate.attr,
	NULL,
};

static const struct attribute_group ili210x_attr_group = {
	.attrs = ili210x_attributes,
};

static void ili210x_power_down(void *data)
{
	struct gpio_desc *reset_gpio = data;

	gpiod_set_value_cansleep(reset_gpio, 1);
}

static void ili210x_cancel_work(void *data)
{
	struct ili210x *priv = data;

	cancel_delayed_work_sync(&priv->dwork);
}

static int ili210x_i2c_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ili210x *priv;
	struct gpio_desc *reset_gpio;
	struct input_dev *input;
	struct firmware_version firmware;
	enum ili2xxx_model model;
	int error;

	model = (enum ili2xxx_model)id->driver_data;

	dev_dbg(dev, "Probing for ILI210X I2C Touschreen driver");

	if (client->irq <= 0) {
		dev_err(dev, "No IRQ!\n");
		return -EINVAL;
	}

	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	if (reset_gpio) {
		error = devm_add_action_or_reset(dev, ili210x_power_down,
						 reset_gpio);
		if (error)
			return error;

		usleep_range(50, 100);
		gpiod_set_value_cansleep(reset_gpio, 0);
		msleep(100);
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	priv->client = client;
	priv->input = input;
	priv->poll_period = DEFAULT_POLL_PERIOD;
	INIT_DELAYED_WORK(&priv->dwork, ili210x_work);
	priv->reset_gpio = reset_gpio;
	priv->model = model;
	if (model == MODEL_ILI210X)
		priv->max_touches = ILI210X_TOUCHES;
	if (model == MODEL_ILI251X)
		priv->max_touches = ILI251X_TOUCHES;

	i2c_set_clientdata(client, priv);

	/* Get firmware version */
	error = ili210x_read_reg(client, REG_FIRMWARE_VERSION,
				 &firmware, sizeof(firmware));
	if (error) {
		dev_err(dev, "Failed to get firmware version, err: %d\n",
			error);
		return error;
	}

	/* Setup input device */
	input->name = "ILI210x Touchscreen";
	input->id.bustype = BUS_I2C;
	input->dev.parent = dev;

	/* Multi touch */
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, 0xffff, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, 0xffff, 0, 0);
	touchscreen_parse_properties(input, true, &priv->prop);
	input_mt_init_slots(input, priv->max_touches, INPUT_MT_DIRECT);

	error = devm_add_action(dev, ili210x_cancel_work, priv);
	if (error)
		return error;

	error = devm_request_irq(dev, client->irq, ili210x_irq, 0,
				 client->name, priv);
	if (error) {
		dev_err(dev, "Unable to request touchscreen IRQ, err: %d\n",
			error);
		return error;
	}

	error = devm_device_add_group(dev, &ili210x_attr_group);
	if (error) {
		dev_err(dev, "Unable to create sysfs attributes, err: %d\n",
			error);
		return error;
	}

	error = input_register_device(priv->input);
	if (error) {
		dev_err(dev, "Cannot register input device, err: %d\n", error);
		return error;
	}

	device_init_wakeup(dev, 1);

	dev_dbg(dev,
		"ILI210x initialized (IRQ: %d), firmware version %d.%d.%d",
		client->irq, firmware.id, firmware.major, firmware.minor);

	return 0;
}

static int __maybe_unused ili210x_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int __maybe_unused ili210x_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ili210x_i2c_pm,
			 ili210x_i2c_suspend, ili210x_i2c_resume);

static const struct i2c_device_id ili210x_i2c_id[] = {
	{ "ili210x", MODEL_ILI210X },
	{ "ili251x", MODEL_ILI251X },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ili210x_i2c_id);

static const struct of_device_id ili210x_dt_ids[] = {
	{ .compatible = "ilitek,ili210x", .data = (void *)MODEL_ILI210X },
	{ .compatible = "ilitek,ili251x", .data = (void *)MODEL_ILI251X },
	{ },
};
MODULE_DEVICE_TABLE(of, ili210x_dt_ids);

static struct i2c_driver ili210x_ts_driver = {
	.driver = {
		.name = "ili210x_i2c",
		.pm = &ili210x_i2c_pm,
		.of_match_table = ili210x_dt_ids,
	},
	.id_table = ili210x_i2c_id,
	.probe = ili210x_i2c_probe,
};

module_i2c_driver(ili210x_ts_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_DESCRIPTION("ILI210X I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
