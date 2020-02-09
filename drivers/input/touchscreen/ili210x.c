// SPDX-License-Identifier: GPL-2.0-only
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define ILI2XXX_POLL_PERIOD	20

#define ILI210X_DATA_SIZE	64
#define ILI211X_DATA_SIZE	43
#define ILI251X_DATA_SIZE1	31
#define ILI251X_DATA_SIZE2	20

/* Touchscreen commands */
#define REG_TOUCHDATA		0x10
#define REG_PANEL_INFO		0x20
#define REG_CALIBRATE		0xcc

struct ili2xxx_chip {
	int (*read_reg)(struct i2c_client *client, u8 reg,
			void *buf, size_t len);
	int (*get_touch_data)(struct i2c_client *client, u8 *data);
	bool (*parse_touch_data)(const u8 *data, unsigned int finger,
				 unsigned int *x, unsigned int *y);
	bool (*continue_polling)(const u8 *data, bool touch);
	unsigned int max_touches;
	unsigned int resolution;
	bool has_calibrate_reg;
};

struct ili210x {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct touchscreen_properties prop;
	const struct ili2xxx_chip *chip;
	bool stop;
};

static int ili210x_read_reg(struct i2c_client *client,
			    u8 reg, void *buf, size_t len)
{
	struct i2c_msg msg[] = {
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
	int error, ret;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "%s failed: %d\n", __func__, error);
		return error;
	}

	return 0;
}

static int ili210x_read_touch_data(struct i2c_client *client, u8 *data)
{
	return ili210x_read_reg(client, REG_TOUCHDATA,
				data, ILI210X_DATA_SIZE);
}

static bool ili210x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y)
{
	if (touchdata[0] & BIT(finger))
		return false;

	*x = get_unaligned_be16(touchdata + 1 + (finger * 4) + 0);
	*y = get_unaligned_be16(touchdata + 1 + (finger * 4) + 2);

	return true;
}

static bool ili210x_check_continue_polling(const u8 *data, bool touch)
{
	return data[0] & 0xf3;
}

static const struct ili2xxx_chip ili210x_chip = {
	.read_reg		= ili210x_read_reg,
	.get_touch_data		= ili210x_read_touch_data,
	.parse_touch_data	= ili210x_touchdata_to_coords,
	.continue_polling	= ili210x_check_continue_polling,
	.max_touches		= 2,
	.has_calibrate_reg	= true,
};

static int ili211x_read_touch_data(struct i2c_client *client, u8 *data)
{
	s16 sum = 0;
	int error;
	int ret;
	int i;

	ret = i2c_master_recv(client, data, ILI211X_DATA_SIZE);
	if (ret != ILI211X_DATA_SIZE) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "%s failed: %d\n", __func__, error);
		return error;
	}

	/* This chip uses custom checksum at the end of data */
	for (i = 0; i < ILI211X_DATA_SIZE - 1; i++)
		sum = (sum + data[i]) & 0xff;

	if ((-sum & 0xff) != data[ILI211X_DATA_SIZE - 1]) {
		dev_err(&client->dev,
			"CRC error (crc=0x%02x expected=0x%02x)\n",
			sum, data[ILI211X_DATA_SIZE - 1]);
		return -EIO;
	}

	return 0;
}

static bool ili211x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y)
{
	u32 data;

	data = get_unaligned_be32(touchdata + 1 + (finger * 4) + 0);
	if (data == 0xffffffff)	/* Finger up */
		return false;

	*x = ((touchdata[1 + (finger * 4) + 0] & 0xf0) << 4) |
	     touchdata[1 + (finger * 4) + 1];
	*y = ((touchdata[1 + (finger * 4) + 0] & 0x0f) << 8) |
	     touchdata[1 + (finger * 4) + 2];

	return true;
}

static bool ili211x_decline_polling(const u8 *data, bool touch)
{
	return false;
}

static const struct ili2xxx_chip ili211x_chip = {
	.read_reg		= ili210x_read_reg,
	.get_touch_data		= ili211x_read_touch_data,
	.parse_touch_data	= ili211x_touchdata_to_coords,
	.continue_polling	= ili211x_decline_polling,
	.max_touches		= 10,
	.resolution		= 2048,
};

static int ili251x_read_reg(struct i2c_client *client,
			    u8 reg, void *buf, size_t len)
{
	int error;
	int ret;

	ret = i2c_master_send(client, &reg, 1);
	if (ret == 1) {
		usleep_range(5000, 5500);

		ret = i2c_master_recv(client, buf, len);
		if (ret == len)
			return 0;
	}

	error = ret < 0 ? ret : -EIO;
	dev_err(&client->dev, "%s failed: %d\n", __func__, error);
	return ret;
}

static int ili251x_read_touch_data(struct i2c_client *client, u8 *data)
{
	int error;

	error = ili251x_read_reg(client, REG_TOUCHDATA,
				 data, ILI251X_DATA_SIZE1);
	if (!error && data[0] == 2) {
		error = i2c_master_recv(client, data + ILI251X_DATA_SIZE1,
					ILI251X_DATA_SIZE2);
		if (error >= 0 && error != ILI251X_DATA_SIZE2)
			error = -EIO;
	}

	return error;
}

static bool ili251x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y)
{
	u16 val;

	val = get_unaligned_be16(touchdata + 1 + (finger * 5) + 0);
	if (!(val & BIT(15)))	/* Touch indication */
		return false;

	*x = val & 0x3fff;
	*y = get_unaligned_be16(touchdata + 1 + (finger * 5) + 2);

	return true;
}

static bool ili251x_check_continue_polling(const u8 *data, bool touch)
{
	return touch;
}

static const struct ili2xxx_chip ili251x_chip = {
	.read_reg		= ili251x_read_reg,
	.get_touch_data		= ili251x_read_touch_data,
	.parse_touch_data	= ili251x_touchdata_to_coords,
	.continue_polling	= ili251x_check_continue_polling,
	.max_touches		= 10,
	.has_calibrate_reg	= true,
};

static bool ili210x_report_events(struct ili210x *priv, u8 *touchdata)
{
	struct input_dev *input = priv->input;
	int i;
	bool contact = false, touch;
	unsigned int x = 0, y = 0;

	for (i = 0; i < priv->chip->max_touches; i++) {
		touch = priv->chip->parse_touch_data(touchdata, i, &x, &y);

		input_mt_slot(input, i);
		if (input_mt_report_slot_state(input, MT_TOOL_FINGER, touch)) {
			touchscreen_report_pos(input, &priv->prop, x, y, true);
			contact = true;
		}
	}

	input_mt_report_pointer_emulation(input, false);
	input_sync(input);

	return contact;
}

static irqreturn_t ili210x_irq(int irq, void *irq_data)
{
	struct ili210x *priv = irq_data;
	struct i2c_client *client = priv->client;
	const struct ili2xxx_chip *chip = priv->chip;
	u8 touchdata[ILI210X_DATA_SIZE] = { 0 };
	bool keep_polling;
	bool touch;
	int error;

	do {
		error = chip->get_touch_data(client, touchdata);
		if (error) {
			dev_err(&client->dev,
				"Unable to get touch data: %d\n", error);
			break;
		}

		touch = ili210x_report_events(priv, touchdata);
		keep_polling = chip->continue_polling(touchdata, touch);
		if (keep_polling)
			msleep(ILI2XXX_POLL_PERIOD);
	} while (!priv->stop && keep_polling);

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

static umode_t ili210x_calibrate_visible(struct kobject *kobj,
					  struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);

	return priv->chip->has_calibrate_reg ? attr->mode : 0;
}

static const struct attribute_group ili210x_attr_group = {
	.attrs = ili210x_attributes,
	.is_visible = ili210x_calibrate_visible,
};

static void ili210x_power_down(void *data)
{
	struct gpio_desc *reset_gpio = data;

	gpiod_set_value_cansleep(reset_gpio, 1);
}

static void ili210x_stop(void *data)
{
	struct ili210x *priv = data;

	/* Tell ISR to quit even if there is a contact. */
	priv->stop = true;
}

static int ili210x_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	const struct ili2xxx_chip *chip;
	struct ili210x *priv;
	struct gpio_desc *reset_gpio;
	struct input_dev *input;
	int error;
	unsigned int max_xy;

	dev_dbg(dev, "Probing for ILI210X I2C Touschreen driver");

	chip = device_get_match_data(dev);
	if (!chip && id)
		chip = (const struct ili2xxx_chip *)id->driver_data;
	if (!chip) {
		dev_err(&client->dev, "unknown device model\n");
		return -ENODEV;
	}

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
	priv->reset_gpio = reset_gpio;
	priv->chip = chip;
	i2c_set_clientdata(client, priv);

	/* Setup input device */
	input->name = "ILI210x Touchscreen";
	input->id.bustype = BUS_I2C;

	/* Multi touch */
	max_xy = (chip->resolution ?: SZ_64K) - 1;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_xy, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_xy, 0, 0);
	touchscreen_parse_properties(input, true, &priv->prop);

	error = input_mt_init_slots(input, priv->chip->max_touches,
				    INPUT_MT_DIRECT);
	if (error) {
		dev_err(dev, "Unable to set up slots, err: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, client->irq, NULL, ili210x_irq,
					  IRQF_ONESHOT, client->name, priv);
	if (error) {
		dev_err(dev, "Unable to request touchscreen IRQ, err: %d\n",
			error);
		return error;
	}

	error = devm_add_action_or_reset(dev, ili210x_stop, priv);
	if (error)
		return error;

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

	return 0;
}

static const struct i2c_device_id ili210x_i2c_id[] = {
	{ "ili210x", (long)&ili210x_chip },
	{ "ili2117", (long)&ili211x_chip },
	{ "ili251x", (long)&ili251x_chip },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ili210x_i2c_id);

static const struct of_device_id ili210x_dt_ids[] = {
	{ .compatible = "ilitek,ili210x", .data = &ili210x_chip },
	{ .compatible = "ilitek,ili2117", .data = &ili211x_chip },
	{ .compatible = "ilitek,ili251x", .data = &ili251x_chip },
	{ }
};
MODULE_DEVICE_TABLE(of, ili210x_dt_ids);

static struct i2c_driver ili210x_ts_driver = {
	.driver = {
		.name = "ili210x_i2c",
		.of_match_table = ili210x_dt_ids,
	},
	.id_table = ili210x_i2c_id,
	.probe = ili210x_i2c_probe,
};

module_i2c_driver(ili210x_ts_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_DESCRIPTION("ILI210X I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
