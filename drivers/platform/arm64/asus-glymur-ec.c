// SPDX-License-Identifier: GPL-2.0-only
/*
 * The EC firmware on the ASUS Zenbook A16 looks to be derived from the
 * 'Qualcomm Hamoa EC' reference base with many major modifications. Some of
 * the original interface is reused, albeit many of its core invariants (such
 * as the capabilities bitmap) are absent.
 *
 * It presents itself as two I2C devices:
 *  [0x5b] is the custom ASUS interface
 *  [0x76] is the Hamoa EC-derived interface
 */

#include <asm/byteorder.h>
#include <linux/cleanup.h>
#include <linux/compiler.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/types.h>
#include <linux/units.h>

/* Qualcomm-derived registers (main device @ 0x76) */
#define ASUS_QCOM_EC_EVENT_CMD		0x05
 #define ASUS_QCOM_EC_EVENT_NONE	0x00
 #define ASUS_QCOM_EC_EVENT_INVALID	0xff

#define ASUS_QCOM_EC_SUBCMD_FULL_VER	0x0f
#define ASUS_QCOM_EC_FULL_VER_LEN	7

/* This "read RAM" command isn't actually part of the Qualcomm standard */
#define ASUS_QCOM_EC_RECM_OFFS		0x52

#define ASUS_QCOM_EC_RAM_FAN_CPU	0x0602
#define ASUS_QCOM_EC_RAM_AC_STATUS	0x0609
#define ASUS_QCOM_EC_RAM_FAN_GPU	0x0624

#define ASUS_QCOM_EC_MODERN_STANDBY_CMD		0x23
#define ASUS_QCOM_EC_MODERN_STANDBY_ENTER	0x07
#define ASUS_QCOM_EC_MODERN_STANDBY_EXIT	0x08

/* ASUS-specific registers (secondary device @ 0x5b) */
#define ASUS_EC_SUBDEV_ADDR		0x5b

#define ASUS_EC_REQ_REG			0x10
#define ASUS_EC_RESP_REG		0x11

#define ASUS_EC_PAGE_MAILBOX		0xc4
 #define ASUS_EC_MBOX_REG_CMD		0x30
 #define ASUS_EC_MBOX_REG_SUBCMD	0x31
 #define ASUS_EC_MBOX_REG_DATA		0x32

#define ASUS_EC_MBOX_CMD_KBD		0x01
 #define ASUS_EC_MBOX_SUBCMD_KBD_EN	0x81
  #define ASUS_EC_MBOX_DATA_KBD_EN	0x04
 #define ASUS_EC_MBOX_SUBCMD_KBD_LVL	0x87

#define ASUS_EC_MBOX_CMD_MISC		0x02
 #define ASUS_EC_MBOX_SUBCMD_MISC_ENABLE	0x83

#define ASUS_EC_PAGE_TEMPERATURE	0xc6
 #define ASUS_EC_TEMP_REG_CPU		0xa6
 #define ASUS_EC_TEMP_REG_SOC		0x2a
 #define ASUS_EC_TEMP_MAX		0x7f

#define ASUS_EC_KBD_MAX_BRIGHTNESS	3

static const u8 asus_ec_kbd_levels[] = {
	[0] = 0x00,
	[1] = 0x55,
	[2] = 0xaa,
	[3] = 0xff,
};

enum asus_ec_event {
	ASUS_EC_EVT_CHARGE_STATE = 0x1c,
	ASUS_EC_EVT_AC_STATE = 0x1d,
	ASUS_EC_EVT_CRIT_TRIP = 0x1e,
	ASUS_EC_EVT_THERMISTOR = 0x4f,
	ASUS_EC_EVT_HOTKEY = 0xda,
	ASUS_EC_EVT_FAN_STATUS = 0xdb,
	ASUS_EC_EVT_THERMAL_TRIP = 0xe0,
	ASUS_EC_EVT_CHARGE_COMMIT = 0xe4,
};

struct asus_ec {
	struct i2c_client *client;
	struct i2c_client *subdev_client;
	/* Serializes all EC access across both I2C devices */
	struct mutex lock;
	struct device *hwmon_dev;
	struct led_classdev kbd_led;
};

/* RAM addresses are sent MSB first, while returned data is LSB first. */
struct asus_qcom_ec_ram_request {
	__be16 addr;
	u8 count;
	u8 reserved[2];
} __packed;

struct asus_qcom_ec_ram_response {
	__le16 data;
	u8 unused[3];
} __packed;

static int asus_qcom_ec_write(struct asus_ec *ec, u8 offset,
			      const u8 *buf, u8 len)
{
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = i2c_smbus_write_i2c_block_data(ec->client, offset, len, buf);
	if (ret < 0) {
		dev_dbg(&ec->client->dev,
			"Main device write off %#x len %u failed: %d\n",
			offset, len, ret);
	}

	return ret;
}

static int asus_qcom_ec_read(struct asus_ec *ec, u8 offset, u8 *buf, u8 len)
{
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = i2c_smbus_read_i2c_block_data(ec->client, offset, len, buf);
	if (ret < 0)
		return ret;
	if (ret < len) {
		dev_err(&ec->client->dev,
			"Short read off %#x: got %d of %u bytes\n",
			offset, ret, len);
		return -EIO;
	}

	return 0;
}

static int asus_qcom_ec_read_ram(struct asus_ec *ec, u16 addr, u8 count, u16 *out)
{
	struct asus_qcom_ec_ram_request req = {
		.addr = cpu_to_be16(addr),
		.count = count,
		.reserved = {},
	};
	struct asus_qcom_ec_ram_response resp = { };
	int ret;

	lockdep_assert_held(&ec->lock);

	if (count != 1 && count != 2)
		return -EINVAL;

	ret = asus_qcom_ec_write(ec, ASUS_QCOM_EC_RECM_OFFS,
				 (const u8 *)&req, sizeof(req));
	if (ret < 0)
		return ret;

	ret = asus_qcom_ec_read(ec, ASUS_QCOM_EC_RECM_OFFS,
				(u8 *)&resp, sizeof(resp));
	if (ret < 0)
		return ret;

	if (count == 2)
		*out = le16_to_cpu(resp.data);
	else
		*out = (u8)le16_to_cpu(resp.data);

	return 0;
}

static int asus_ec_readb(struct asus_ec *ec, u8 page, u8 reg, u8 *out)
{
	u8 sel[2] = { page, reg };
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = i2c_smbus_write_i2c_block_data(ec->subdev_client, ASUS_EC_REQ_REG,
					     sizeof(sel), sel);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(ec->subdev_client, ASUS_EC_RESP_REG);
	if (ret < 0)
		return ret;

	*out = ret;

	return 0;
}

static int asus_ec_writeb(struct asus_ec *ec, u8 page, u8 reg, u8 data)
{
	u8 sel[2] = { page, reg };
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = i2c_smbus_write_i2c_block_data(ec->subdev_client, ASUS_EC_REQ_REG,
					     sizeof(sel), sel);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(ec->subdev_client,
					 ASUS_EC_RESP_REG, data);
}

static int asus_ec_wait_mailbox(struct asus_ec *ec)
{
	int ret, tries;
	u8 status;

	lockdep_assert_held(&ec->lock);

	for (tries = 5; tries > 0; tries--) {
		ret = asus_ec_readb(ec, ASUS_EC_PAGE_MAILBOX,
				    ASUS_EC_MBOX_REG_CMD, &status);
		if (ret < 0)
			return ret;
		if (!status)
			return 0;
		usleep_range(5000, 6000);
	}

	return -ETIMEDOUT;
}

static int asus_ec_mailbox_cmd(struct asus_ec *ec, u8 cmd, u8 sub, u8 data)
{
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = asus_ec_wait_mailbox(ec);
	if (ret < 0)
		return ret == -ETIMEDOUT ? -EBUSY : ret;

	ret = asus_ec_writeb(ec, ASUS_EC_PAGE_MAILBOX, ASUS_EC_MBOX_REG_SUBCMD, sub);
	if (ret < 0)
		return ret;

	ret = asus_ec_writeb(ec, ASUS_EC_PAGE_MAILBOX, ASUS_EC_MBOX_REG_DATA, data);
	if (ret < 0)
		return ret;

	ret = asus_ec_writeb(ec, ASUS_EC_PAGE_MAILBOX, ASUS_EC_MBOX_REG_CMD, cmd);
	if (ret < 0)
		return ret;

	return asus_ec_wait_mailbox(ec);
}

static int asus_ec_enable_writes(struct asus_ec *ec)
{
	guard(mutex)(&ec->lock);

	return asus_ec_mailbox_cmd(ec, ASUS_EC_MBOX_CMD_MISC,
				   ASUS_EC_MBOX_SUBCMD_MISC_ENABLE, 1);
}

static int asus_ec_read_version(struct asus_ec *ec, u8 *major, u8 *minor, u8 *patch)
{
	u8 resp[ASUS_QCOM_EC_FULL_VER_LEN + 1];
	int ret;

	guard(mutex)(&ec->lock);

	ret = asus_qcom_ec_read(ec, ASUS_QCOM_EC_SUBCMD_FULL_VER,
				resp, sizeof(resp));
	if (ret < 0)
		return ret;
	if (resp[0] < ASUS_QCOM_EC_FULL_VER_LEN)
		return -EIO;

	*major = resp[3];
	*minor = resp[2];
	*patch = resp[1];

	return 0;
}

static int asus_ec_get_temp(struct asus_ec *ec, u8 reg, long *millideg)
{
	u8 raw;
	int ret;

	guard(mutex)(&ec->lock);

	ret = asus_ec_readb(ec, ASUS_EC_PAGE_TEMPERATURE, reg, &raw);
	if (ret < 0)
		return ret;
	if (raw > ASUS_EC_TEMP_MAX)
		return -ENODATA;

	*millideg = raw * MILLIDEGREE_PER_DEGREE;

	return 0;
}

static int asus_ec_get_fan_rpm(struct asus_ec *ec, u16 base, long *rpm)
{
	u16 val;
	int ret;

	guard(mutex)(&ec->lock);

	ret = asus_qcom_ec_read_ram(ec, base, 2, &val);
	if (ret < 0)
		return ret;

	*rpm = val;

	return 0;
}

static irqreturn_t asus_ec_irq(int irq, void *data)
{
	struct asus_ec *ec = data;
	struct device *dev = &ec->client->dev;
	int code;

	scoped_guard(mutex, &ec->lock)
		code = i2c_smbus_read_byte_data(ec->client, ASUS_QCOM_EC_EVENT_CMD);

	if (code < 0) {
		dev_err_ratelimited(dev, "Failed to read EC event: %d\n", code);
		return IRQ_HANDLED;
	}
	if (code == ASUS_QCOM_EC_EVENT_NONE ||
	    code == ASUS_QCOM_EC_EVENT_INVALID)
		return IRQ_HANDLED;

	switch (code) {
	case ASUS_EC_EVT_HOTKEY:
		/* Present in DSDT, don't know what triggers it */
		dev_dbg_ratelimited(dev, "EC hotkey event\n");
		break;

	case ASUS_EC_EVT_FAN_STATUS:
		dev_dbg_ratelimited(dev, "Fan status changed\n");
		break;

	/* Maybe the temperature of the EC itself? */
	case ASUS_EC_EVT_THERMAL_TRIP:
	case ASUS_EC_EVT_CRIT_TRIP:
	/* ASUS_EC_TEMP_SOC climbing above 91 *C or falling below 92 *C */
	case ASUS_EC_EVT_THERMISTOR:
		dev_dbg_ratelimited(dev, "Thermal event %#x\n", code);
		break;

	case ASUS_EC_EVT_AC_STATE: {
		u16 ac = 0;
		int ret;

		/* TODO: ping SoCCP over the pmic_glink OEM-specific interface */
		scoped_guard(mutex, &ec->lock)
			ret = asus_qcom_ec_read_ram(ec, ASUS_QCOM_EC_RAM_AC_STATUS, 1, &ac);
		if (ret < 0) {
			dev_warn_ratelimited(dev,
					     "Failed to read AC status: %d\n",
					     ret);
			break;
		}
		dev_dbg_ratelimited(dev, "AC event: 0x%x\n", ac);
		break;
	}
	case ASUS_EC_EVT_CHARGE_STATE:
	case ASUS_EC_EVT_CHARGE_COMMIT:
		break;

	default:
		dev_info_ratelimited(dev, "unknown EC event: %#x\n", code);
		break;
	}

	return IRQ_HANDLED;
}

static int asus_ec_kbd_led_set(struct led_classdev *led,
			       enum led_brightness brightness)
{
	struct asus_ec *ec = container_of(led, struct asus_ec, kbd_led);
	int ret;

	if (brightness > ASUS_EC_KBD_MAX_BRIGHTNESS)
		brightness = ASUS_EC_KBD_MAX_BRIGHTNESS;

	guard(mutex)(&ec->lock);

	ret = asus_ec_mailbox_cmd(ec, ASUS_EC_MBOX_CMD_KBD, ASUS_EC_MBOX_SUBCMD_KBD_EN,
				  ASUS_EC_MBOX_DATA_KBD_EN);
	if (ret < 0)
		return ret;

	return asus_ec_mailbox_cmd(ec, ASUS_EC_MBOX_CMD_KBD, ASUS_EC_MBOX_SUBCMD_KBD_LVL,
				   asus_ec_kbd_levels[brightness]);
}

enum asus_ec_temp_channel {
	/* External sensor? */
	ASUS_EC_TEMP_CPU,
	/* Likely SoC-internal temp data communicated over a side channel */
	ASUS_EC_TEMP_SOC,
	ASUS_EC_TEMP_COUNT,
};

static umode_t asus_ec_hwmon_is_visible(const void *data,
					enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
	case hwmon_temp:
		return 0444;
	default:
		return 0;
	}
}

static int asus_ec_hwmon_read(struct device *dev,
			      enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct asus_ec *ec = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (channel) {
		case ASUS_EC_TEMP_CPU:
			return asus_ec_get_temp(ec, ASUS_EC_TEMP_REG_CPU, val);
		case ASUS_EC_TEMP_SOC:
			return asus_ec_get_temp(ec, ASUS_EC_TEMP_REG_SOC, val);
		default:
			return -EINVAL;
		}
	case hwmon_fan:
		switch (channel) {
		case 0:
			return asus_ec_get_fan_rpm(ec, ASUS_QCOM_EC_RAM_FAN_CPU, val);
		case 1:
			return asus_ec_get_fan_rpm(ec, ASUS_QCOM_EC_RAM_FAN_GPU, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int asus_ec_hwmon_read_string(struct device *dev,
				     enum hwmon_sensor_types type,
				     u32 attr, int channel, const char **str)
{
	if (type != hwmon_temp || channel >= ASUS_EC_TEMP_COUNT)
		return -EINVAL;

	switch (channel) {
	case ASUS_EC_TEMP_CPU:
		*str = "CPU";
		break;
	case ASUS_EC_TEMP_SOC:
		*str = "SoC";
		break;
	}

	return 0;
}

static const struct hwmon_channel_info * const asus_ec_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT),
	NULL
};

static const struct hwmon_ops asus_ec_hwmon_ops = {
	.is_visible	= asus_ec_hwmon_is_visible,
	.read		= asus_ec_hwmon_read,
	.read_string	= asus_ec_hwmon_read_string,
};

static const struct hwmon_chip_info asus_ec_hwmon_chip_info = {
	.ops	= &asus_ec_hwmon_ops,
	.info	= asus_ec_hwmon_info,
};

static int asus_glymur_ec_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 major, minor, patch;
	struct asus_ec *ec;
	int ret;

	ec = devm_kzalloc(dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->client = client;
	i2c_set_clientdata(client, ec);

	ret = devm_mutex_init(dev, &ec->lock);
	if (ret)
		return ret;

	ec->subdev_client = devm_i2c_new_dummy_device(dev, client->adapter,
						      ASUS_EC_SUBDEV_ADDR);
	if (IS_ERR(ec->subdev_client)) {
		return dev_err_probe(dev, PTR_ERR(ec->subdev_client),
				     "Failed to register 0x5b EC device\n");
	}

	ret = asus_ec_enable_writes(ec);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable EC direct access: %d\n", ret);

	ret = asus_ec_read_version(ec, &major, &minor, &patch);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read EC version: %d\n", ret);

	dev_dbg(dev, "ASUS Zenbook EC version %u.%u.%u\n", major, minor, patch);

	ec->kbd_led.name = "asus::kbd_backlight";
	ec->kbd_led.max_brightness = ASUS_EC_KBD_MAX_BRIGHTNESS;
	ec->kbd_led.brightness_set_blocking = asus_ec_kbd_led_set;
	ec->kbd_led.flags = LED_CORE_SUSPENDRESUME;

	ret = devm_led_classdev_register(dev, &ec->kbd_led);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register keyboard LED\n");

	ret = devm_request_threaded_irq(dev, client->irq, NULL, asus_ec_irq,
					IRQF_ONESHOT, dev_name(dev), ec);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request IRQ\n");

	ec->hwmon_dev = devm_hwmon_device_register_with_info(dev,
							     "asus_glymur_ec",
							     ec,
							     &asus_ec_hwmon_chip_info,
							     NULL);
	if (IS_ERR(ec->hwmon_dev)) {
		return dev_err_probe(dev, PTR_ERR(ec->hwmon_dev),
				     "Failed to register hwmon\n");
	}

	return 0;
}

static int asus_glymur_ec_modern_standby(struct asus_ec *ec, u8 val)
{
	guard(mutex)(&ec->lock);

	return i2c_smbus_write_byte_data(ec->client,
					 ASUS_QCOM_EC_MODERN_STANDBY_CMD, val);
}

static int asus_glymur_ec_suspend(struct device *dev)
{
	return asus_glymur_ec_modern_standby(dev_get_drvdata(dev),
					     ASUS_QCOM_EC_MODERN_STANDBY_ENTER);
}

static int asus_glymur_ec_resume(struct device *dev)
{
	return asus_glymur_ec_modern_standby(dev_get_drvdata(dev),
					     ASUS_QCOM_EC_MODERN_STANDBY_EXIT);
}

static DEFINE_SIMPLE_DEV_PM_OPS(asus_glymur_ec_pm_ops,
				asus_glymur_ec_suspend,
				asus_glymur_ec_resume);

static const struct of_device_id asus_glymur_ec_of_match[] = {
	{ .compatible = "asus,zenbook-a14-ux3407na-ec" },
	{ .compatible = "asus,zenbook-a16-ux3607oa-ec" },
	{}
};
MODULE_DEVICE_TABLE(of, asus_glymur_ec_of_match);

static const struct i2c_device_id asus_glymur_ec_id[] = {
	{ "asus-glymur-ec" },
	{}
};
MODULE_DEVICE_TABLE(i2c, asus_glymur_ec_id);

static struct i2c_driver asus_glymur_ec_driver = {
	.driver = {
		.name		= "asus-glymur-ec",
		.of_match_table	= asus_glymur_ec_of_match,
		.pm		= pm_sleep_ptr(&asus_glymur_ec_pm_ops),
	},
	.probe		= asus_glymur_ec_probe,
	.id_table	= asus_glymur_ec_id,
};
module_i2c_driver(asus_glymur_ec_driver);

MODULE_DESCRIPTION("ASUS Zenbook A16 (Qualcomm) Embedded Controller driver");
MODULE_LICENSE("GPL");
