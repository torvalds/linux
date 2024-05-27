// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * jc42.c - driver for Jedec JC42.4 compliant temperature sensors
 *
 * Copyright (c) 2010  Ericsson AB.
 *
 * Derived from lm77.c by Andras BALI <drewie@freemail.hu>.
 *
 * JC42.4 compliant temperature sensors are typically used on memory modules.
 */

#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, I2C_CLIENT_END };

/* JC42 registers. All registers are 16 bit. */
#define JC42_REG_CAP		0x00
#define JC42_REG_CONFIG		0x01
#define JC42_REG_TEMP_UPPER	0x02
#define JC42_REG_TEMP_LOWER	0x03
#define JC42_REG_TEMP_CRITICAL	0x04
#define JC42_REG_TEMP		0x05
#define JC42_REG_MANID		0x06
#define JC42_REG_DEVICEID	0x07
#define JC42_REG_SMBUS		0x22 /* NXP and Atmel, possibly others? */

/* Status bits in temperature register */
#define JC42_ALARM_CRIT		BIT(15)
#define JC42_ALARM_MAX		BIT(14)
#define JC42_ALARM_MIN		BIT(13)

/* Configuration register defines */
#define JC42_CFG_CRIT_ONLY	BIT(2)
#define JC42_CFG_TCRIT_LOCK	BIT(6)
#define JC42_CFG_EVENT_LOCK	BIT(7)
#define JC42_CFG_SHUTDOWN	BIT(8)
#define JC42_CFG_HYST_MASK	GENMASK(10, 9)

/* Capabilities */
#define JC42_CAP_RANGE		BIT(2)

/* Manufacturer IDs */
#define ADT_MANID		0x11d4  /* Analog Devices */
#define ATMEL_MANID		0x001f  /* Atmel */
#define ATMEL_MANID2		0x1114	/* Atmel */
#define MAX_MANID		0x004d  /* Maxim */
#define IDT_MANID		0x00b3  /* IDT */
#define MCP_MANID		0x0054  /* Microchip */
#define NXP_MANID		0x1131  /* NXP Semiconductors */
#define ONS_MANID		0x1b09  /* ON Semiconductor */
#define STM_MANID		0x104a  /* ST Microelectronics */
#define GT_MANID		0x1c68	/* Giantec */
#define GT_MANID2		0x132d	/* Giantec, 2nd mfg ID */
#define SI_MANID		0x1c85	/* Seiko Instruments */

/* SMBUS register */
#define SMBUS_STMOUT		BIT(7)  /* SMBus time-out, active low */

/* Supported chips */

/* Analog Devices */
#define ADT7408_DEVID		0x0801
#define ADT7408_DEVID_MASK	0xffff

/* Atmel */
#define AT30TS00_DEVID		0x8201
#define AT30TS00_DEVID_MASK	0xffff

#define AT30TSE004_DEVID	0x2200
#define AT30TSE004_DEVID_MASK	0xffff

/* Giantec */
#define GT30TS00_DEVID		0x2200
#define GT30TS00_DEVID_MASK	0xff00

#define GT34TS02_DEVID		0x3300
#define GT34TS02_DEVID_MASK	0xff00

/* IDT */
#define TSE2004_DEVID		0x2200
#define TSE2004_DEVID_MASK	0xff00

#define TS3000_DEVID		0x2900  /* Also matches TSE2002 */
#define TS3000_DEVID_MASK	0xff00

#define TS3001_DEVID		0x3000
#define TS3001_DEVID_MASK	0xff00

/* Maxim */
#define MAX6604_DEVID		0x3e00
#define MAX6604_DEVID_MASK	0xffff

/* Microchip */
#define MCP9804_DEVID		0x0200
#define MCP9804_DEVID_MASK	0xfffc

#define MCP9808_DEVID		0x0400
#define MCP9808_DEVID_MASK	0xfffc

#define MCP98242_DEVID		0x2000
#define MCP98242_DEVID_MASK	0xfffc

#define MCP98243_DEVID		0x2100
#define MCP98243_DEVID_MASK	0xfffc

#define MCP98244_DEVID		0x2200
#define MCP98244_DEVID_MASK	0xfffc

#define MCP9843_DEVID		0x0000	/* Also matches mcp9805 */
#define MCP9843_DEVID_MASK	0xfffe

/* NXP */
#define SE97_DEVID		0xa200
#define SE97_DEVID_MASK		0xfffc

#define SE98_DEVID		0xa100
#define SE98_DEVID_MASK		0xfffc

/* ON Semiconductor */
#define CAT6095_DEVID		0x0800	/* Also matches CAT34TS02 */
#define CAT6095_DEVID_MASK	0xffe0

#define CAT34TS02C_DEVID	0x0a00
#define CAT34TS02C_DEVID_MASK	0xfff0

#define CAT34TS04_DEVID		0x2200
#define CAT34TS04_DEVID_MASK	0xfff0

#define N34TS04_DEVID		0x2230
#define N34TS04_DEVID_MASK	0xfff0

/* ST Microelectronics */
#define STTS424_DEVID		0x0101
#define STTS424_DEVID_MASK	0xffff

#define STTS424E_DEVID		0x0000
#define STTS424E_DEVID_MASK	0xfffe

#define STTS2002_DEVID		0x0300
#define STTS2002_DEVID_MASK	0xffff

#define STTS2004_DEVID		0x2201
#define STTS2004_DEVID_MASK	0xffff

#define STTS3000_DEVID		0x0200
#define STTS3000_DEVID_MASK	0xffff

/* Seiko Instruments */
#define S34TS04A_DEVID		0x2221
#define S34TS04A_DEVID_MASK	0xffff

static u16 jc42_hysteresis[] = { 0, 1500, 3000, 6000 };

struct jc42_chips {
	u16 manid;
	u16 devid;
	u16 devid_mask;
};

static struct jc42_chips jc42_chips[] = {
	{ ADT_MANID, ADT7408_DEVID, ADT7408_DEVID_MASK },
	{ ATMEL_MANID, AT30TS00_DEVID, AT30TS00_DEVID_MASK },
	{ ATMEL_MANID2, AT30TSE004_DEVID, AT30TSE004_DEVID_MASK },
	{ GT_MANID, GT30TS00_DEVID, GT30TS00_DEVID_MASK },
	{ GT_MANID2, GT34TS02_DEVID, GT34TS02_DEVID_MASK },
	{ IDT_MANID, TSE2004_DEVID, TSE2004_DEVID_MASK },
	{ IDT_MANID, TS3000_DEVID, TS3000_DEVID_MASK },
	{ IDT_MANID, TS3001_DEVID, TS3001_DEVID_MASK },
	{ MAX_MANID, MAX6604_DEVID, MAX6604_DEVID_MASK },
	{ MCP_MANID, MCP9804_DEVID, MCP9804_DEVID_MASK },
	{ MCP_MANID, MCP9808_DEVID, MCP9808_DEVID_MASK },
	{ MCP_MANID, MCP98242_DEVID, MCP98242_DEVID_MASK },
	{ MCP_MANID, MCP98243_DEVID, MCP98243_DEVID_MASK },
	{ MCP_MANID, MCP98244_DEVID, MCP98244_DEVID_MASK },
	{ MCP_MANID, MCP9843_DEVID, MCP9843_DEVID_MASK },
	{ NXP_MANID, SE97_DEVID, SE97_DEVID_MASK },
	{ ONS_MANID, CAT6095_DEVID, CAT6095_DEVID_MASK },
	{ ONS_MANID, CAT34TS02C_DEVID, CAT34TS02C_DEVID_MASK },
	{ ONS_MANID, CAT34TS04_DEVID, CAT34TS04_DEVID_MASK },
	{ ONS_MANID, N34TS04_DEVID, N34TS04_DEVID_MASK },
	{ NXP_MANID, SE98_DEVID, SE98_DEVID_MASK },
	{ SI_MANID,  S34TS04A_DEVID, S34TS04A_DEVID_MASK },
	{ STM_MANID, STTS424_DEVID, STTS424_DEVID_MASK },
	{ STM_MANID, STTS424E_DEVID, STTS424E_DEVID_MASK },
	{ STM_MANID, STTS2002_DEVID, STTS2002_DEVID_MASK },
	{ STM_MANID, STTS2004_DEVID, STTS2004_DEVID_MASK },
	{ STM_MANID, STTS3000_DEVID, STTS3000_DEVID_MASK },
};

/* Each client has this additional data */
struct jc42_data {
	struct mutex	update_lock;	/* protect register access */
	struct regmap	*regmap;
	bool		extended;	/* true if extended range supported */
	bool		valid;
	u16		orig_config;	/* original configuration */
	u16		config;		/* current configuration */
};

#define JC42_TEMP_MIN_EXTENDED	(-40000)
#define JC42_TEMP_MIN		0
#define JC42_TEMP_MAX		125000

static u16 jc42_temp_to_reg(long temp, bool extended)
{
	int ntemp = clamp_val(temp,
			      extended ? JC42_TEMP_MIN_EXTENDED :
			      JC42_TEMP_MIN, JC42_TEMP_MAX);

	/* convert from 0.001 to 0.0625 resolution */
	return (ntemp * 2 / 125) & 0x1fff;
}

static int jc42_temp_from_reg(s16 reg)
{
	reg = sign_extend32(reg, 12);

	/* convert from 0.0625 to 0.001 resolution */
	return reg * 125 / 2;
}

static int jc42_read(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long *val)
{
	struct jc42_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int ret, temp, hyst;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_temp_input:
		ret = regmap_read(data->regmap, JC42_REG_TEMP, &regval);
		if (ret)
			break;

		*val = jc42_temp_from_reg(regval);
		break;
	case hwmon_temp_min:
		ret = regmap_read(data->regmap, JC42_REG_TEMP_LOWER, &regval);
		if (ret)
			break;

		*val = jc42_temp_from_reg(regval);
		break;
	case hwmon_temp_max:
		ret = regmap_read(data->regmap, JC42_REG_TEMP_UPPER, &regval);
		if (ret)
			break;

		*val = jc42_temp_from_reg(regval);
		break;
	case hwmon_temp_crit:
		ret = regmap_read(data->regmap, JC42_REG_TEMP_CRITICAL,
				  &regval);
		if (ret)
			break;

		*val = jc42_temp_from_reg(regval);
		break;
	case hwmon_temp_max_hyst:
		ret = regmap_read(data->regmap, JC42_REG_TEMP_UPPER, &regval);
		if (ret)
			break;

		temp = jc42_temp_from_reg(regval);
		hyst = jc42_hysteresis[FIELD_GET(JC42_CFG_HYST_MASK,
						 data->config)];
		*val = temp - hyst;
		break;
	case hwmon_temp_crit_hyst:
		ret = regmap_read(data->regmap, JC42_REG_TEMP_CRITICAL,
				  &regval);
		if (ret)
			break;

		temp = jc42_temp_from_reg(regval);
		hyst = jc42_hysteresis[FIELD_GET(JC42_CFG_HYST_MASK,
						 data->config)];
		*val = temp - hyst;
		break;
	case hwmon_temp_min_alarm:
		ret = regmap_read(data->regmap, JC42_REG_TEMP, &regval);
		if (ret)
			break;

		*val = FIELD_GET(JC42_ALARM_MIN, regval);
		break;
	case hwmon_temp_max_alarm:
		ret = regmap_read(data->regmap, JC42_REG_TEMP, &regval);
		if (ret)
			break;

		*val = FIELD_GET(JC42_ALARM_MAX, regval);
		break;
	case hwmon_temp_crit_alarm:
		ret = regmap_read(data->regmap, JC42_REG_TEMP, &regval);
		if (ret)
			break;

		*val = FIELD_GET(JC42_ALARM_CRIT, regval);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&data->update_lock);

	return ret;
}

static int jc42_write(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long val)
{
	struct jc42_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	int diff, hyst;
	int ret;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_temp_min:
		ret = regmap_write(data->regmap, JC42_REG_TEMP_LOWER,
				   jc42_temp_to_reg(val, data->extended));
		break;
	case hwmon_temp_max:
		ret = regmap_write(data->regmap, JC42_REG_TEMP_UPPER,
				   jc42_temp_to_reg(val, data->extended));
		break;
	case hwmon_temp_crit:
		ret = regmap_write(data->regmap, JC42_REG_TEMP_CRITICAL,
				   jc42_temp_to_reg(val, data->extended));
		break;
	case hwmon_temp_crit_hyst:
		ret = regmap_read(data->regmap, JC42_REG_TEMP_CRITICAL,
				  &regval);
		if (ret)
			break;

		/*
		 * JC42.4 compliant chips only support four hysteresis values.
		 * Pick best choice and go from there.
		 */
		val = clamp_val(val, (data->extended ? JC42_TEMP_MIN_EXTENDED
						     : JC42_TEMP_MIN) - 6000,
				JC42_TEMP_MAX);
		diff = jc42_temp_from_reg(regval) - val;
		hyst = 0;
		if (diff > 0) {
			if (diff < 2250)
				hyst = 1;	/* 1.5 degrees C */
			else if (diff < 4500)
				hyst = 2;	/* 3.0 degrees C */
			else
				hyst = 3;	/* 6.0 degrees C */
		}
		data->config = (data->config & ~JC42_CFG_HYST_MASK) |
				FIELD_PREP(JC42_CFG_HYST_MASK, hyst);
		ret = regmap_write(data->regmap, JC42_REG_CONFIG,
				   data->config);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&data->update_lock);

	return ret;
}

static umode_t jc42_is_visible(const void *_data, enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	const struct jc42_data *data = _data;
	unsigned int config = data->config;
	umode_t mode = 0444;

	switch (attr) {
	case hwmon_temp_min:
	case hwmon_temp_max:
		if (!(config & JC42_CFG_EVENT_LOCK))
			mode |= 0200;
		break;
	case hwmon_temp_crit:
		if (!(config & JC42_CFG_TCRIT_LOCK))
			mode |= 0200;
		break;
	case hwmon_temp_crit_hyst:
		if (!(config & (JC42_CFG_EVENT_LOCK | JC42_CFG_TCRIT_LOCK)))
			mode |= 0200;
		break;
	case hwmon_temp_input:
	case hwmon_temp_max_hyst:
	case hwmon_temp_min_alarm:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
		break;
	default:
		mode = 0;
		break;
	}
	return mode;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int jc42_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int i, config, cap, manid, devid;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	cap = i2c_smbus_read_word_swapped(client, JC42_REG_CAP);
	config = i2c_smbus_read_word_swapped(client, JC42_REG_CONFIG);
	manid = i2c_smbus_read_word_swapped(client, JC42_REG_MANID);
	devid = i2c_smbus_read_word_swapped(client, JC42_REG_DEVICEID);

	if (cap < 0 || config < 0 || manid < 0 || devid < 0)
		return -ENODEV;

	if ((cap & 0xff00) || (config & 0xf800))
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(jc42_chips); i++) {
		struct jc42_chips *chip = &jc42_chips[i];
		if (manid == chip->manid &&
		    (devid & chip->devid_mask) == chip->devid) {
			strscpy(info->type, "jc42", I2C_NAME_SIZE);
			return 0;
		}
	}
	return -ENODEV;
}

static const struct hwmon_channel_info * const jc42_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT_HYST | HWMON_T_MIN_ALARM |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM),
	NULL
};

static const struct hwmon_ops jc42_hwmon_ops = {
	.is_visible = jc42_is_visible,
	.read = jc42_read,
	.write = jc42_write,
};

static const struct hwmon_chip_info jc42_chip_info = {
	.ops = &jc42_hwmon_ops,
	.info = jc42_info,
};

static bool jc42_readable_reg(struct device *dev, unsigned int reg)
{
	return (reg >= JC42_REG_CAP && reg <= JC42_REG_DEVICEID) ||
		reg == JC42_REG_SMBUS;
}

static bool jc42_writable_reg(struct device *dev, unsigned int reg)
{
	return (reg >= JC42_REG_CONFIG && reg <= JC42_REG_TEMP_CRITICAL) ||
		reg == JC42_REG_SMBUS;
}

static bool jc42_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == JC42_REG_CONFIG || reg == JC42_REG_TEMP;
}

static const struct regmap_config jc42_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = JC42_REG_SMBUS,
	.writeable_reg = jc42_writable_reg,
	.readable_reg = jc42_readable_reg,
	.volatile_reg = jc42_volatile_reg,
	.cache_type = REGCACHE_MAPLE,
};

static int jc42_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	unsigned int config, cap;
	struct jc42_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct jc42_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &jc42_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	ret = regmap_read(data->regmap, JC42_REG_CAP, &cap);
	if (ret)
		return ret;

	data->extended = !!(cap & JC42_CAP_RANGE);

	if (device_property_read_bool(dev, "smbus-timeout-disable")) {
		/*
		 * Not all chips support this register, but from a
		 * quick read of various datasheets no chip appears
		 * incompatible with the below attempt to disable
		 * the timeout. And the whole thing is opt-in...
		 */
		ret = regmap_set_bits(data->regmap, JC42_REG_SMBUS,
				      SMBUS_STMOUT);
		if (ret)
			return ret;
	}

	ret = regmap_read(data->regmap, JC42_REG_CONFIG, &config);
	if (ret)
		return ret;

	data->orig_config = config;
	if (config & JC42_CFG_SHUTDOWN) {
		config &= ~JC42_CFG_SHUTDOWN;
		regmap_write(data->regmap, JC42_REG_CONFIG, config);
	}
	data->config = config;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "jc42",
							 data, &jc42_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static void jc42_remove(struct i2c_client *client)
{
	struct jc42_data *data = i2c_get_clientdata(client);

	/* Restore original configuration except hysteresis */
	if ((data->config & ~JC42_CFG_HYST_MASK) !=
	    (data->orig_config & ~JC42_CFG_HYST_MASK)) {
		int config;

		config = (data->orig_config & ~JC42_CFG_HYST_MASK)
		  | (data->config & JC42_CFG_HYST_MASK);
		regmap_write(data->regmap, JC42_REG_CONFIG, config);
	}
}

#ifdef CONFIG_PM

static int jc42_suspend(struct device *dev)
{
	struct jc42_data *data = dev_get_drvdata(dev);

	data->config |= JC42_CFG_SHUTDOWN;
	regmap_write(data->regmap, JC42_REG_CONFIG, data->config);

	regcache_cache_only(data->regmap, true);
	regcache_mark_dirty(data->regmap);

	return 0;
}

static int jc42_resume(struct device *dev)
{
	struct jc42_data *data = dev_get_drvdata(dev);

	regcache_cache_only(data->regmap, false);

	data->config &= ~JC42_CFG_SHUTDOWN;
	regmap_write(data->regmap, JC42_REG_CONFIG, data->config);

	/* Restore cached register values to hardware */
	return regcache_sync(data->regmap);
}

static const struct dev_pm_ops jc42_dev_pm_ops = {
	.suspend = jc42_suspend,
	.resume = jc42_resume,
};

#define JC42_DEV_PM_OPS (&jc42_dev_pm_ops)
#else
#define JC42_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id jc42_id[] = {
	{ "jc42" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jc42_id);

#ifdef CONFIG_OF
static const struct of_device_id jc42_of_ids[] = {
	{ .compatible = "jedec,jc-42.4-temp", },
	{ }
};
MODULE_DEVICE_TABLE(of, jc42_of_ids);
#endif

static struct i2c_driver jc42_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "jc42",
		.pm = JC42_DEV_PM_OPS,
		.of_match_table = of_match_ptr(jc42_of_ids),
	},
	.probe		= jc42_probe,
	.remove		= jc42_remove,
	.id_table	= jc42_id,
	.detect		= jc42_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(jc42_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("JC42 driver");
MODULE_LICENSE("GPL");
