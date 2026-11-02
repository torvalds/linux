// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kstrtox.h>
#include <linux/leds.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/units.h>

#define SLIM7X11_EC_BLOCK_READ	0x01
#define SLIM7X11_EC_BLOCK_WRITE	0x02
#define SLIM7X11_EC_RESP_LEN	6
#define SLIM7X11_EC_REQ_LEN	7

#define SLIM7X11_EC_CMD_RAM_READ	0xb0
#define SLIM7X11_EC_CMD_RAM_WRITE	0xb1
#define SLIM7X11_EC_CMD_FAN		0x46
#define SLIM7X11_EC_FAN_GET_SPEED	0x81

#define SLIM7X11_EC_CMD_EVENT	0x84
#define SLIM7X11_EC_EVT_MAX_DRAIN	64
#define SLIM7X11_EC_EVT_FN_LOCK	0x0f
#define SLIM7X11_EC_EVT_MIC_MUTE	0x18
#define SLIM7X11_EC_EVT_FLIGHT_MODE	0x19

#define SLIM7X11_EC_SCUK		0x1c
#define SLIM7X11_EC_SCUK_MEDIA	BIT(7)
#define SLIM7X11_EC_SCUK_LED_OFF	BIT(5)
#define SLIM7X11_EC_SCUK_MEDIA_MASK	(SLIM7X11_EC_SCUK_MEDIA | SLIM7X11_EC_SCUK_LED_OFF)

#define SLIM7X11_EC_KBLT		0x03
#define SLIM7X11_EC_KBLT_LEVEL	GENMASK(1, 0)
#define SLIM7X11_EC_KBLT_AUTO	3
#define SLIM7X11_EC_KBLT_MAX	2

#define SLIM7X11_EC_TEMP_FIRST	0x12
#define SLIM7X11_EC_NUM_TEMP	6
#define SLIM7X11_EC_TEMP_MAX	120

struct slim7x11_ec {
	struct i2c_client *client;
	/* serializes all EC access */
	struct mutex lock;
	unsigned long temp_present;
	struct led_classdev kbd_led;
	u8 kblt_level;
	bool kblt_restore;
	struct input_dev *keys;
};

static int slim7x11_ec_command(struct slim7x11_ec *ec, u8 cmd, u8 arg0, u8 arg1,
			       u8 resp[SLIM7X11_EC_RESP_LEN])
{
	u8 req[SLIM7X11_EC_REQ_LEN] = { cmd, arg0, arg1 };
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = i2c_smbus_write_i2c_block_data(ec->client, SLIM7X11_EC_BLOCK_WRITE, sizeof(req), req);
	if (ret < 0)
		return ret;

	usleep_range(2000, 3000);

	ret = i2c_smbus_read_i2c_block_data(ec->client, SLIM7X11_EC_BLOCK_READ,
					    SLIM7X11_EC_RESP_LEN, resp);
	if (ret < 0)
		return ret;
	if (ret != SLIM7X11_EC_RESP_LEN)
		return -EIO;

	return 0;
}

static int slim7x11_ec_ram_read(struct slim7x11_ec *ec, u8 addr, u8 *val)
{
	u8 resp[SLIM7X11_EC_RESP_LEN];
	int ret;

	ret = slim7x11_ec_command(ec, SLIM7X11_EC_CMD_RAM_READ, addr, 0, resp);
	if (ret)
		return ret;

	*val = resp[0];
	return 0;
}

static int slim7x11_ec_ram_write(struct slim7x11_ec *ec, u8 addr, u8 val)
{
	u8 resp[SLIM7X11_EC_RESP_LEN];

	return slim7x11_ec_command(ec, SLIM7X11_EC_CMD_RAM_WRITE, addr, val, resp);
}

static int slim7x11_ec_fan_read(struct slim7x11_ec *ec, long *rpm)
{
	u8 resp[SLIM7X11_EC_RESP_LEN];
	int ret;

	ret = slim7x11_ec_command(ec, SLIM7X11_EC_CMD_FAN, SLIM7X11_EC_FAN_GET_SPEED, 0, resp);
	if (ret)
		return ret;

	*rpm = resp[0] * 100;
	return 0;
}

static umode_t slim7x11_ec_is_visible(const void *drvdata, enum hwmon_sensor_types type, u32 attr,
				      int channel)
{
	const struct slim7x11_ec *ec = drvdata;

	switch (type) {
	case hwmon_temp:
		if (!(ec->temp_present & BIT(channel)))
			return 0;
		return 0444;
	case hwmon_fan:
		return 0444;
	default:
		return 0;
	}
}

static int slim7x11_ec_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			    long *val)
{
	struct slim7x11_ec *ec = dev_get_drvdata(dev);
	int ret;
	u8 raw;

	guard(mutex)(&ec->lock);

	switch (type) {
	case hwmon_temp:
		ret = slim7x11_ec_ram_read(ec, SLIM7X11_EC_TEMP_FIRST + channel, &raw);
		if (ret)
			return ret;
		if (!raw)
			return -ENODATA;
		if (raw > SLIM7X11_EC_TEMP_MAX)
			return -EIO;
		*val = raw * MILLIDEGREE_PER_DEGREE;
		return 0;
	case hwmon_fan:
		return slim7x11_ec_fan_read(ec, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const char * const slim7x11_ec_temp_labels[SLIM7X11_EC_NUM_TEMP] = {
	"Charger A",
	"CPU VR",
	"5V rail",
	"Ambient",
	"Charger B",
	"Thermistor 6",
};

static int slim7x11_ec_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				   int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = slim7x11_ec_temp_labels[channel];
		return 0;
	case hwmon_fan:
		*str = "CPU fan";
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static enum led_brightness slim7x11_ec_kbd_led_get(struct led_classdev *led)
{
	struct slim7x11_ec *ec = container_of(led, struct slim7x11_ec, kbd_led);
	u8 raw;

	guard(mutex)(&ec->lock);

	if (slim7x11_ec_ram_read(ec, SLIM7X11_EC_KBLT, &raw))
		return 0;

	raw = FIELD_GET(SLIM7X11_EC_KBLT_LEVEL, raw);

	if (raw == SLIM7X11_EC_KBLT_AUTO)
		return SLIM7X11_EC_KBLT_MAX;

	return raw;
}

static int slim7x11_ec_kbd_led_level(struct slim7x11_ec *ec, u8 level, u8 *old)
{
	u8 raw;
	int ret;

	ret = slim7x11_ec_ram_read(ec, SLIM7X11_EC_KBLT, &raw);
	if (ret)
		return ret;

	if (old)
		*old = FIELD_GET(SLIM7X11_EC_KBLT_LEVEL, raw);

	FIELD_MODIFY(SLIM7X11_EC_KBLT_LEVEL, &raw, level);

	return slim7x11_ec_ram_write(ec, SLIM7X11_EC_KBLT, raw);
}

static int slim7x11_ec_kbd_led_set(struct led_classdev *led, enum led_brightness brightness)
{
	struct slim7x11_ec *ec = container_of(led, struct slim7x11_ec, kbd_led);

	guard(mutex)(&ec->lock);

	return slim7x11_ec_kbd_led_level(ec, brightness, NULL);
}

static const struct hwmon_ops slim7x11_ec_hwmon_ops = {
	.is_visible = slim7x11_ec_is_visible,
	.read = slim7x11_ec_read,
	.read_string = slim7x11_ec_read_string,
};

static const struct hwmon_channel_info * const slim7x11_ec_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};

static const struct hwmon_chip_info slim7x11_ec_chip_info = {
	.ops = &slim7x11_ec_hwmon_ops,
	.info = slim7x11_ec_hwmon_info,
};

static int slim7x11_ec_fn_lock_toggle(struct slim7x11_ec *ec)
{
	u8 raw;
	int ret;

	ret = slim7x11_ec_ram_read(ec, SLIM7X11_EC_SCUK, &raw);
	if (ret)
		return ret;

	if (raw & SLIM7X11_EC_SCUK_MEDIA)
		raw &= ~SLIM7X11_EC_SCUK_MEDIA_MASK;
	else
		raw |= SLIM7X11_EC_SCUK_MEDIA_MASK;

	return slim7x11_ec_ram_write(ec, SLIM7X11_EC_SCUK, raw);
}

static void slim7x11_ec_report_key(struct slim7x11_ec *ec, unsigned int code)
{
	if (!ec->keys)
		return;

	input_report_key(ec->keys, code, 1);
	input_sync(ec->keys);
	input_report_key(ec->keys, code, 0);
	input_sync(ec->keys);
}

static irqreturn_t slim7x11_ec_irq(int irq, void *data)
{
	struct slim7x11_ec *ec = data;
	unsigned int i;

	guard(mutex)(&ec->lock);

	for (i = 0; i < SLIM7X11_EC_EVT_MAX_DRAIN; i++) {
		u8 resp[SLIM7X11_EC_RESP_LEN];

		if (slim7x11_ec_command(ec, SLIM7X11_EC_CMD_EVENT, 0, 0, resp))
			break;
		if (!resp[0])
			break;

		switch (resp[0]) {
		case SLIM7X11_EC_EVT_FN_LOCK:
			if (!slim7x11_ec_fn_lock_toggle(ec))
				sysfs_notify(&ec->client->dev.kobj, NULL, "fn_lock");
			break;
		case SLIM7X11_EC_EVT_MIC_MUTE:
			slim7x11_ec_report_key(ec, KEY_MICMUTE);
			break;
		case SLIM7X11_EC_EVT_FLIGHT_MODE:
			slim7x11_ec_report_key(ec, KEY_RFKILL);
			break;
		default:
			break;
		}
	}

	return IRQ_HANDLED;
}

static void slim7x11_ec_register_keys(struct slim7x11_ec *ec)
{
	struct device *dev = &ec->client->dev;
	struct input_dev *input;
	int ret;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_warn(dev, "no memory for the hotkey device\n");
		return;
	}

	input->name = "Lenovo Yoga Slim 7x hotkeys";
	input->phys = "lenovo-yoga-slim7x-gen11-ec/input0";
	input->id.bustype = BUS_I2C;
	input_set_capability(input, EV_KEY, KEY_MICMUTE);
	input_set_capability(input, EV_KEY, KEY_RFKILL);

	ret = input_register_device(input);
	if (ret) {
		dev_warn(dev, "cannot register the hotkey device: %d\n", ret);
		return;
	}

	ec->keys = input;
}

static ssize_t fn_lock_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct slim7x11_ec *ec = dev_get_drvdata(dev);
	u8 raw;
	int ret;

	scoped_guard(mutex, &ec->lock) {
		ret = slim7x11_ec_ram_read(ec, SLIM7X11_EC_SCUK, &raw);
		if (ret)
			return ret;
	}

	return sysfs_emit(buf, "%u\n", !(raw & SLIM7X11_EC_SCUK_MEDIA));
}

static ssize_t fn_lock_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct slim7x11_ec *ec = dev_get_drvdata(dev);
	bool enable;
	u8 raw;
	int ret;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	guard(mutex)(&ec->lock);

	ret = slim7x11_ec_ram_read(ec, SLIM7X11_EC_SCUK, &raw);
	if (ret)
		return ret;

	if (enable)
		raw &= ~SLIM7X11_EC_SCUK_MEDIA_MASK;
	else
		raw |= SLIM7X11_EC_SCUK_MEDIA_MASK;

	ret = slim7x11_ec_ram_write(ec, SLIM7X11_EC_SCUK, raw);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(fn_lock);

static struct attribute *slim7x11_ec_attrs[] = {
	&dev_attr_fn_lock.attr,
	NULL
};
ATTRIBUTE_GROUPS(slim7x11_ec);

static int slim7x11_ec_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon;
	struct slim7x11_ec *ec;
	unsigned int i;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return dev_err_probe(dev, -ENODEV, "adapter lacks SMBus block transfers\n");

	ec = devm_kzalloc(dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->client = client;
	i2c_set_clientdata(client, ec);

	ret = devm_mutex_init(dev, &ec->lock);
	if (ret)
		return ret;

	scoped_guard(mutex, &ec->lock) {
		for (i = 0; i < SLIM7X11_EC_NUM_TEMP; i++) {
			u8 raw;

			ret = slim7x11_ec_ram_read(ec, SLIM7X11_EC_TEMP_FIRST + i, &raw);
			if (ret)
				return dev_err_probe(dev, ret, "failed to read thermistor %u\n", i);

			if (raw > 0 && raw <= SLIM7X11_EC_TEMP_MAX)
				ec->temp_present |= BIT(i);
		}
	}

	if (!ec->temp_present)
		return dev_err_probe(dev, -ENODEV, "no thermistor reported a usable value\n");

	dev_dbg(dev, "thermistors present: %#lx\n", ec->temp_present);

	hwmon = devm_hwmon_device_register_with_info(dev, "yoga_slim7x_ec", ec,
						     &slim7x11_ec_chip_info, NULL);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	ec->kbd_led.name = "platform::kbd_backlight";
	ec->kbd_led.max_brightness = SLIM7X11_EC_KBLT_MAX;
	ec->kbd_led.brightness_get = slim7x11_ec_kbd_led_get;
	ec->kbd_led.brightness_set_blocking = slim7x11_ec_kbd_led_set;

	ret = devm_led_classdev_register(dev, &ec->kbd_led);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register keyboard backlight\n");

	if (client->irq) {
		slim7x11_ec_register_keys(ec);

		ret = devm_request_threaded_irq(dev, client->irq, NULL, slim7x11_ec_irq,
						IRQF_ONESHOT, "lenovo-yoga-slim7x-gen11-ec", ec);
		if (ret)
			return dev_err_probe(dev, ret, "cannot request the EC interrupt\n");

		scoped_guard(mutex, &ec->lock) {
			for (i = 0; i < SLIM7X11_EC_EVT_MAX_DRAIN; i++) {
				u8 resp[SLIM7X11_EC_RESP_LEN];

				if (slim7x11_ec_command(ec, SLIM7X11_EC_CMD_EVENT, 0, 0, resp))
					break;
				if (!resp[0])
					break;
			}
		}
	}

	return 0;
}

static int slim7x11_ec_suspend(struct device *dev)
{
	struct slim7x11_ec *ec = dev_get_drvdata(dev);

	guard(mutex)(&ec->lock);
	ec->kblt_restore = !slim7x11_ec_kbd_led_level(ec, 0, &ec->kblt_level);

	return 0;
}

static int slim7x11_ec_resume(struct device *dev)
{
	struct slim7x11_ec *ec = dev_get_drvdata(dev);

	guard(mutex)(&ec->lock);
	if (ec->kblt_restore) {
		ec->kblt_restore = false;
		slim7x11_ec_kbd_led_level(ec, ec->kblt_level, NULL);
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(slim7x11_ec_pm_ops, slim7x11_ec_suspend, slim7x11_ec_resume);

static const struct of_device_id slim7x11_ec_of_match[] = {
	{ .compatible = "lenovo,yoga-slim7x-gen11-ec" },
	{ }
};
MODULE_DEVICE_TABLE(of, slim7x11_ec_of_match);

static const struct i2c_device_id slim7x11_ec_i2c_id[] = {
	{ .name = "slim7x-gen11-ec" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, slim7x11_ec_i2c_id);

static struct i2c_driver slim7x11_ec_driver = {
	.driver = {
		.name = "lenovo-yoga-slim7x-gen11-ec",
		.of_match_table = slim7x11_ec_of_match,
		.dev_groups = slim7x11_ec_groups,
		.pm = pm_sleep_ptr(&slim7x11_ec_pm_ops),
	},
	.probe = slim7x11_ec_probe,
	.id_table = slim7x11_ec_i2c_id,
};
module_i2c_driver(slim7x11_ec_driver);

MODULE_AUTHOR("Oleg Keri <okerixx@gmail.com>");
MODULE_DESCRIPTION("Lenovo Yoga Slim 7x Gen 11 embedded controller");
MODULE_LICENSE("GPL");
