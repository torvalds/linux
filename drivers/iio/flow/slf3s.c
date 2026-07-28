// SPDX-License-Identifier: GPL-2.0
/*
 * Sensirion SLF3S liquid flow sensor driver.
 *
 * Supports the SLF3S-0600F, SLF3S-1300F and SLF3S-4000B liquid-flow
 * sensors over I2C.  Each measurement frame returns a 16-bit signed
 * flow value, a 16-bit signed temperature value and a status word,
 * each protected by a CRC-8 byte.
 *
 * The active calibration medium (water or isopropyl alcohol) is
 * runtime-switchable via the in_volumeflow_medium sysfs attribute and
 * defaults to water.
 *
 * Datasheet: https://sensirion.com/products/catalog/SLF3S-0600F/
 *
 * Copyright (C) 2026 CMBlu Energy GmbH
 * Author: Wadim Mueller <wafgo01@gmail.com>
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#include <linux/iio/iio.h>

#define SLF3S_CRC8_POLY			0x31
#define SLF3S_CRC8_INIT			0xff

#define SLF3S_PRODUCT_ID_LEN		18
#define SLF3S_PRODUCT_FAMILY_BYTE	1
#define SLF3S_PRODUCT_SUBTYPE_BYTE	3
#define SLF3S_PRODUCT_FAMILY_ID		0x03

/* Datasheet section 2.2: tPU = 25 ms max from power-on to first cmd. */
#define SLF3S_POWER_UP_DELAY_US		(25 * USEC_PER_MSEC)
/* Datasheet section 2.2: tw = 60 ms typical until first valid sample. */
#define SLF3S_MEAS_START_DELAY_US	(60 * USEC_PER_MSEC)

static const u8 slf3s_cmd_prep_pid[]	= { 0x36, 0x7c };
static const u8 slf3s_cmd_read_pid[]	= { 0xe1, 0x02 };
static const u8 slf3s_cmd_start_water[]	= { 0x36, 0x08 };
static const u8 slf3s_cmd_start_ipa[]	= { 0x36, 0x15 };
static const u8 slf3s_cmd_stop_meas[]	= { 0x3f, 0xf9 };

enum slf3s_medium {
	SLF3S_MEDIUM_WATER,
	SLF3S_MEDIUM_IPA,
};

static const char * const slf3s_medium_modes[] = {
	[SLF3S_MEDIUM_WATER]	= "water",
	[SLF3S_MEDIUM_IPA]	= "ipa",
};

enum slf3s_variant_id {
	SLF3S_0600F,
	SLF3S_1300F,
	SLF3S_4000B,
};

/**
 * struct slf3s_variant - per-variant calibration constants
 * @sub_type:	product-info sub-type byte returned by the sensor
 * @name:	name reported via @iio_dev.name
 * @scale:	flow scale in l/s per LSB
 */
struct slf3s_variant {
	u8 sub_type;
	const char *name;
	struct s32_fract scale;
};

static const struct slf3s_variant slf3s_variants[] = {
	[SLF3S_0600F] = {
		.sub_type	= 0x03,
		.name		= "slf3s-0600f",
		.scale		= { .numerator = 1, .denominator = 600 * MICRO },
	},
	[SLF3S_1300F] = {
		.sub_type	= 0x02,
		.name		= "slf3s-1300f",
		.scale		= { .numerator = 1, .denominator = 30 * MICRO },
	},
	[SLF3S_4000B] = {
		.sub_type	= 0x05,
		.name		= "slf3s-4000b",
		.scale		= { .numerator = 1, .denominator = 1920 * MILLI },
	},
};

/**
 * struct slf3s_data - per-device state
 * @client:	I2C client this instance is bound to
 * @vdd:	supply regulator, disabled while suspended
 * @variant:	pointer into @slf3s_variants for the detected device
 * @medium:	currently active calibration medium
 * @lock:	serialises the multi-step command/response exchanges
 * @crc_table:	pre-computed CRC-8 lookup table for SLF3S_CRC8_POLY
 */
struct slf3s_data {
	struct i2c_client *client;
	struct regulator *vdd;
	const struct slf3s_variant *variant;
	enum slf3s_medium medium;
	struct mutex lock;
	u8 crc_table[CRC8_TABLE_SIZE];
};

static int slf3s_send_cmd(struct i2c_client *client, const u8 *cmd)
{
	int ret;

	ret = i2c_master_send(client, cmd, 2);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	return 0;
}

/* Start continuous measurement and wait until the first sample is valid. */
static int slf3s_start_meas(struct slf3s_data *sf, enum slf3s_medium medium)
{
	const u8 *cmd = (medium == SLF3S_MEDIUM_IPA) ? slf3s_cmd_start_ipa
						     : slf3s_cmd_start_water;
	int ret;

	ret = slf3s_send_cmd(sf->client, cmd);
	if (ret)
		return ret;

	fsleep(SLF3S_MEAS_START_DELAY_US);

	return 0;
}

static bool slf3s_crc_valid(const struct slf3s_data *sf, const u8 *block)
{
	return crc8(sf->crc_table, block, 2, SLF3S_CRC8_INIT) == block[2];
}

/*
 * Read the product-info block and pick the matching variant.  The
 * sub-type byte returned by the sensor is the source of truth; a
 * DT-supplied compatible only seeds an initial guess and is overridden
 * on mismatch (with an informational message so misconfigured device
 * trees are easy to spot).
 *
 * Bus / CRC failures are real errors and fail probe.  An unknown
 * sub-type byte falls back to the variant named in the device tree /
 * I2C table, so a drop-in replacement part that lists one of the known
 * compatibles keeps working on an older kernel that does not know its
 * sub-type yet.  Without any match data probe fails since no
 * meaningful scale can be published.
 */
static int slf3s_detect_variant(struct slf3s_data *sf)
{
	struct i2c_client *client = sf->client;
	u8 buf[SLF3S_PRODUCT_ID_LEN];
	int ret;

	ret = slf3s_send_cmd(client, slf3s_cmd_prep_pid);
	if (ret)
		return ret;

	ret = slf3s_send_cmd(client, slf3s_cmd_read_pid);
	if (ret)
		return ret;

	ret = i2c_master_recv(client, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	for (unsigned int i = 0; i < SLF3S_PRODUCT_ID_LEN; i += 3) {
		if (!slf3s_crc_valid(sf, &buf[i]))
			return -EIO;
	}

	if (buf[SLF3S_PRODUCT_FAMILY_BYTE] != SLF3S_PRODUCT_FAMILY_ID)
		dev_info(&client->dev,
			 "unexpected family byte 0x%02x (expected 0x%02x)\n",
			 buf[SLF3S_PRODUCT_FAMILY_BYTE],
			 SLF3S_PRODUCT_FAMILY_ID);

	for (unsigned int i = 0; i < ARRAY_SIZE(slf3s_variants); i++) {
		if (buf[SLF3S_PRODUCT_SUBTYPE_BYTE] !=
		    slf3s_variants[i].sub_type)
			continue;

		if (sf->variant && sf->variant != &slf3s_variants[i])
			dev_info(&client->dev,
				 "DT compatible says %s but sensor reports %s; using the latter\n",
				 sf->variant->name,
				 slf3s_variants[i].name);

		sf->variant = &slf3s_variants[i];

		return 0;
	}

	if (sf->variant) {
		dev_warn(&client->dev,
			 "unknown SLF3S sub-type 0x%02x, assuming %s\n",
			 buf[SLF3S_PRODUCT_SUBTYPE_BYTE], sf->variant->name);
		return 0;
	}

	dev_err(&client->dev, "unknown SLF3S sub-type 0x%02x\n",
		buf[SLF3S_PRODUCT_SUBTYPE_BYTE]);

	return -ENODEV;
}

static int slf3s_read_sample(struct slf3s_data *sf, int *flow, int *temp)
{
	/*
	 * A measurement frame is flow, temperature and a signaling-flags
	 * word, each followed by a CRC byte.  Only flow and temperature are
	 * used, so the read is stopped after their two words (6 bytes).
	 */
	u8 buf[6];
	int ret;

	ret = i2c_master_recv(sf->client, buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(buf))
		return -EIO;

	for (unsigned int i = 0; i < sizeof(buf); i += 3) {
		if (!slf3s_crc_valid(sf, &buf[i]))
			return -EIO;
	}

	*flow = sign_extend32(get_unaligned_be16(&buf[0]), 15);
	*temp = sign_extend32(get_unaligned_be16(&buf[3]), 15);

	return 0;
}

static int slf3s_get_medium(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan)
{
	struct slf3s_data *sf = iio_priv(indio_dev);

	return sf->medium;
}

static int slf3s_set_medium(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan, unsigned int mode)
{
	struct slf3s_data *sf = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&sf->lock);

	ret = slf3s_send_cmd(sf->client, slf3s_cmd_stop_meas);
	if (ret)
		return ret;

	ret = slf3s_start_meas(sf, mode);
	if (ret) {
		/*
		 * Try to restart with the previous medium so the sensor is
		 * not left idle, which would fail all subsequent reads.
		 */
		if (slf3s_start_meas(sf, sf->medium))
			dev_warn(&sf->client->dev,
				 "failed to restart measurement, reads will fail until a medium is set\n");
		return ret;
	}

	sf->medium = mode;

	return 0;
}

static const struct iio_enum slf3s_medium_enum = {
	.items		= slf3s_medium_modes,
	.num_items	= ARRAY_SIZE(slf3s_medium_modes),
	.get		= slf3s_get_medium,
	.set		= slf3s_set_medium,
};

static const struct iio_chan_spec_ext_info slf3s_ext_info[] = {
	IIO_ENUM("medium", IIO_SHARED_BY_TYPE, &slf3s_medium_enum),
	IIO_ENUM_AVAILABLE("medium", IIO_SHARED_BY_TYPE, &slf3s_medium_enum),
	{ }
};

static const struct iio_chan_spec slf3s_channels[] = {
	{
		.type = IIO_VOLUMEFLOW,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.ext_info = slf3s_ext_info,
	},
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int slf3s_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, int *val,
			  int *val2, long mask)
{
	struct slf3s_data *sf = iio_priv(indio_dev);
	int flow, temp, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		scoped_guard(mutex, &sf->lock)
			ret = slf3s_read_sample(sf, &flow, &temp);
		if (ret)
			return ret;

		*val = (chan->type == IIO_VOLUMEFLOW) ? flow : temp;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_VOLUMEFLOW) {
			/*
			 * The variant scale is the flow per LSB in l/s, but
			 * IIO reports volume flow in m^3/s (1 l = 1e-3 m^3).
			 * These values are tiny (~1.67e-12 m^3/s for the
			 * SLF3S-0600F), so emit a 64-bit fixed-point value with
			 * femto (1e-15) resolution to preserve precision.
			 * Converting l/s to m^3/s (/ MILLI) and scaling to femto
			 * (* FEMTO) leaves a net * (FEMTO / MILLI) factor.
			 */
			const struct slf3s_variant *v = sf->variant;
			s64 num = (s64)v->scale.numerator * (FEMTO / MILLI);
			s64 scale = DIV_S64_ROUND_CLOSEST(num,
							  v->scale.denominator);

			iio_val_s64_decompose(scale, val, val2);

			return IIO_VAL_DECIMAL64_FEMTO;
		}
		/* Temperature LSB = 1/200 degC; IIO_TEMP wants milli-degC. */
		*val = MILLIDEGREE_PER_DEGREE / 200;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info slf3s_info = {
	.read_raw = slf3s_read_raw,
};

static void slf3s_stop_meas(void *data)
{
	struct slf3s_data *sf = data;

	slf3s_send_cmd(sf->client, slf3s_cmd_stop_meas);
}

static void slf3s_disable_vdd(void *data)
{
	struct slf3s_data *sf = data;

	regulator_disable(sf->vdd);
}

static int slf3s_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct slf3s_data *sf;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*sf));
	if (!indio_dev)
		return -ENOMEM;

	sf = iio_priv(indio_dev);
	sf->client = client;
	i2c_set_clientdata(client, indio_dev);
	sf->variant = i2c_get_match_data(client);
	sf->medium = SLF3S_MEDIUM_WATER;
	crc8_populate_msb(sf->crc_table, SLF3S_CRC8_POLY);

	ret = devm_mutex_init(dev, &sf->lock);
	if (ret)
		return ret;

	sf->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(sf->vdd))
		return dev_err_probe(dev, PTR_ERR(sf->vdd),
				     "failed to get vdd supply\n");

	ret = regulator_enable(sf->vdd);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable vdd supply\n");

	ret = devm_add_action_or_reset(dev, slf3s_disable_vdd, sf);
	if (ret)
		return ret;

	fsleep(SLF3S_POWER_UP_DELAY_US);

	/*
	 * The sensor may still be in continuous measurement mode from a
	 * previous boot (warm reboot / kexec); in that case it would NACK
	 * the product-id command below.  Stop it first and ignore the error
	 * if it was already idle.
	 */
	slf3s_send_cmd(client, slf3s_cmd_stop_meas);

	ret = slf3s_detect_variant(sf);
	if (ret)
		return dev_err_probe(dev, ret, "product info read failed\n");

	ret = slf3s_start_meas(sf, sf->medium);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to start measurement\n");

	ret = devm_add_action_or_reset(dev, slf3s_stop_meas, sf);
	if (ret)
		return ret;

	indio_dev->name = sf->variant->name;
	indio_dev->channels = slf3s_channels;
	indio_dev->num_channels = ARRAY_SIZE(slf3s_channels);
	indio_dev->info = &slf3s_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(dev, indio_dev);
}

/*
 * The sensor has no low-power state of its own, so stop the measurement
 * and cut the supply while suspended.  Resume powers it back up, waits
 * out the power-up time and restarts with the medium that was active
 * before.
 */
static int slf3s_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct slf3s_data *sf = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&sf->lock);

	ret = slf3s_send_cmd(sf->client, slf3s_cmd_stop_meas);
	if (ret)
		return ret;

	return regulator_disable(sf->vdd);
}

static int slf3s_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct slf3s_data *sf = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&sf->lock);

	ret = regulator_enable(sf->vdd);
	if (ret)
		return ret;

	fsleep(SLF3S_POWER_UP_DELAY_US);

	return slf3s_start_meas(sf, sf->medium);
}

static DEFINE_SIMPLE_DEV_PM_OPS(slf3s_pm_ops, slf3s_suspend, slf3s_resume);

static const struct i2c_device_id slf3s_id[] = {
	{
		.name = "slf3s-0600f",
		.driver_data = (kernel_ulong_t)&slf3s_variants[SLF3S_0600F],
	},
	{
		.name = "slf3s-1300f",
		.driver_data = (kernel_ulong_t)&slf3s_variants[SLF3S_1300F],
	},
	{
		.name = "slf3s-4000b",
		.driver_data = (kernel_ulong_t)&slf3s_variants[SLF3S_4000B],
	},
	{ }
};
MODULE_DEVICE_TABLE(i2c, slf3s_id);

static const struct of_device_id slf3s_of_match[] = {
	{
		.compatible = "sensirion,slf3s-0600f",
		.data = &slf3s_variants[SLF3S_0600F],
	},
	{
		.compatible = "sensirion,slf3s-1300f",
		.data = &slf3s_variants[SLF3S_1300F],
	},
	{
		.compatible = "sensirion,slf3s-4000b",
		.data = &slf3s_variants[SLF3S_4000B],
	},
	{ }
};
MODULE_DEVICE_TABLE(of, slf3s_of_match);

static struct i2c_driver slf3s_driver = {
	.driver = {
		.name		= "slf3s",
		.of_match_table	= slf3s_of_match,
		.pm		= pm_sleep_ptr(&slf3s_pm_ops),
	},
	.probe		= slf3s_probe,
	.id_table	= slf3s_id,
};
module_i2c_driver(slf3s_driver);

MODULE_AUTHOR("Wadim Mueller <wafgo01@gmail.com>");
MODULE_DESCRIPTION("Sensirion SLF3S liquid flow sensor driver");
MODULE_LICENSE("GPL");
