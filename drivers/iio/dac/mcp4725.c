/*
 * mcp4725.c - Support for Microchip MCP4725/6
 *
 * Copyright (C) 2012 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * Based on max517 by Roland Stigge <stigge@antcom.de>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * driver for the Microchip I2C 12-bit digital-to-analog converter (DAC)
 * (7-bit I2C slave address 0x60, the three LSBs can be configured in
 * hardware)
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/of_device.h>
#include <linux/of.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <linux/iio/dac/mcp4725.h>

#define MCP4725_DRV_NAME "mcp4725"

#define MCP472X_REF_VDD			0x00
#define MCP472X_REF_VREF_UNBUFFERED	0x02
#define MCP472X_REF_VREF_BUFFERED	0x03

struct mcp4725_data {
	struct i2c_client *client;
	int id;
	unsigned ref_mode;
	bool vref_buffered;
	u16 dac_value;
	bool powerdown;
	unsigned powerdown_mode;
	struct regulator *vdd_reg;
	struct regulator *vref_reg;
};

static int mcp4725_suspend(struct device *dev)
{
	struct mcp4725_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	u8 outbuf[2];

	outbuf[0] = (data->powerdown_mode + 1) << 4;
	outbuf[1] = 0;
	data->powerdown = true;

	return i2c_master_send(data->client, outbuf, 2);
}

static int mcp4725_resume(struct device *dev)
{
	struct mcp4725_data *data = iio_priv(i2c_get_clientdata(
		to_i2c_client(dev)));
	u8 outbuf[2];

	/* restore previous DAC value */
	outbuf[0] = (data->dac_value >> 8) & 0xf;
	outbuf[1] = data->dac_value & 0xff;
	data->powerdown = false;

	return i2c_master_send(data->client, outbuf, 2);
}

#ifdef CONFIG_PM_SLEEP
static SIMPLE_DEV_PM_OPS(mcp4725_pm_ops, mcp4725_suspend, mcp4725_resume);
#define MCP4725_PM_OPS (&mcp4725_pm_ops)
#else
#define MCP4725_PM_OPS NULL
#endif

static ssize_t mcp4725_store_eeprom(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct mcp4725_data *data = iio_priv(indio_dev);
	int tries = 20;
	u8 inoutbuf[3];
	bool state;
	int ret;

	ret = strtobool(buf, &state);
	if (ret < 0)
		return ret;

	if (!state)
		return 0;

	inoutbuf[0] = 0x60; /* write EEPROM */
	inoutbuf[0] |= data->ref_mode << 3;
	inoutbuf[1] = data->dac_value >> 4;
	inoutbuf[2] = (data->dac_value & 0xf) << 4;

	ret = i2c_master_send(data->client, inoutbuf, 3);
	if (ret < 0)
		return ret;
	else if (ret != 3)
		return -EIO;

	/* wait for write complete, takes up to 50ms */
	while (tries--) {
		msleep(20);
		ret = i2c_master_recv(data->client, inoutbuf, 3);
		if (ret < 0)
			return ret;
		else if (ret != 3)
			return -EIO;

		if (inoutbuf[0] & 0x80)
			break;
	}

	if (tries < 0) {
		dev_err(&data->client->dev,
			"mcp4725_store_eeprom() failed, incomplete\n");
		return -EIO;
	}

	return len;
}

static IIO_DEVICE_ATTR(store_eeprom, S_IWUSR, NULL, mcp4725_store_eeprom, 0);

static struct attribute *mcp4725_attributes[] = {
	&iio_dev_attr_store_eeprom.dev_attr.attr,
	NULL,
};

static const struct attribute_group mcp4725_attribute_group = {
	.attrs = mcp4725_attributes,
};

static const char * const mcp4725_powerdown_modes[] = {
	"1kohm_to_gnd",
	"100kohm_to_gnd",
	"500kohm_to_gnd"
};

static const char * const mcp4726_powerdown_modes[] = {
	"1kohm_to_gnd",
	"125kohm_to_gnd",
	"640kohm_to_gnd"
};

static int mcp4725_get_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct mcp4725_data *data = iio_priv(indio_dev);

	return data->powerdown_mode;
}

static int mcp4725_set_powerdown_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned mode)
{
	struct mcp4725_data *data = iio_priv(indio_dev);

	data->powerdown_mode = mode;

	return 0;
}

static ssize_t mcp4725_read_powerdown(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	struct mcp4725_data *data = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", data->powerdown);
}

static ssize_t mcp4725_write_powerdown(struct iio_dev *indio_dev,
	 uintptr_t private, const struct iio_chan_spec *chan,
	 const char *buf, size_t len)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	bool state;
	int ret;

	ret = strtobool(buf, &state);
	if (ret)
		return ret;

	if (state)
		ret = mcp4725_suspend(&data->client->dev);
	else
		ret = mcp4725_resume(&data->client->dev);
	if (ret < 0)
		return ret;

	return len;
}

enum chip_id {
	MCP4725,
	MCP4726,
};

static const struct iio_enum mcp472x_powerdown_mode_enum[] = {
	[MCP4725] = {
		.items = mcp4725_powerdown_modes,
		.num_items = ARRAY_SIZE(mcp4725_powerdown_modes),
		.get = mcp4725_get_powerdown_mode,
		.set = mcp4725_set_powerdown_mode,
	},
	[MCP4726] = {
		.items = mcp4726_powerdown_modes,
		.num_items = ARRAY_SIZE(mcp4726_powerdown_modes),
		.get = mcp4725_get_powerdown_mode,
		.set = mcp4725_set_powerdown_mode,
	},
};

static const struct iio_chan_spec_ext_info mcp4725_ext_info[] = {
	{
		.name = "powerdown",
		.read = mcp4725_read_powerdown,
		.write = mcp4725_write_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE,
			&mcp472x_powerdown_mode_enum[MCP4725]),
	IIO_ENUM_AVAILABLE("powerdown_mode",
			&mcp472x_powerdown_mode_enum[MCP4725]),
	{ },
};

static const struct iio_chan_spec_ext_info mcp4726_ext_info[] = {
	{
		.name = "powerdown",
		.read = mcp4725_read_powerdown,
		.write = mcp4725_write_powerdown,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE,
			&mcp472x_powerdown_mode_enum[MCP4726]),
	IIO_ENUM_AVAILABLE("powerdown_mode",
			&mcp472x_powerdown_mode_enum[MCP4726]),
	{ },
};

static const struct iio_chan_spec mcp472x_channel[] = {
	[MCP4725] = {
		.type		= IIO_VOLTAGE,
		.indexed	= 1,
		.output		= 1,
		.channel	= 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.ext_info	= mcp4725_ext_info,
	},
	[MCP4726] = {
		.type		= IIO_VOLTAGE,
		.indexed	= 1,
		.output		= 1,
		.channel	= 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.ext_info	= mcp4726_ext_info,
	},
};

static int mcp4725_set_value(struct iio_dev *indio_dev, int val)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	u8 outbuf[2];
	int ret;

	if (val >= (1 << 12) || val < 0)
		return -EINVAL;

	outbuf[0] = (val >> 8) & 0xf;
	outbuf[1] = val & 0xff;

	ret = i2c_master_send(data->client, outbuf, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EIO;
	else
		return 0;
}

static int mcp4726_set_cfg(struct iio_dev *indio_dev)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	u8 outbuf[3];
	int ret;

	outbuf[0] = 0x40;
	outbuf[0] |= data->ref_mode << 3;
	if (data->powerdown)
		outbuf[0] |= data->powerdown << 1;
	outbuf[1] = data->dac_value >> 4;
	outbuf[2] = (data->dac_value & 0xf) << 4;

	ret = i2c_master_send(data->client, outbuf, 3);
	if (ret < 0)
		return ret;
	else if (ret != 3)
		return -EIO;
	else
		return 0;
}

static int mcp4725_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = data->dac_value;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (data->ref_mode == MCP472X_REF_VDD)
			ret = regulator_get_voltage(data->vdd_reg);
		else
			ret = regulator_get_voltage(data->vref_reg);

		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	}
	return -EINVAL;
}

static int mcp4725_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct mcp4725_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp4725_set_value(indio_dev, val);
		data->dac_value = val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct iio_info mcp4725_info = {
	.read_raw = mcp4725_read_raw,
	.write_raw = mcp4725_write_raw,
	.attrs = &mcp4725_attribute_group,
};

#ifdef CONFIG_OF
static int mcp4725_probe_dt(struct device *dev,
			    struct mcp4725_platform_data *pdata)
{
	struct device_node *np = dev->of_node;

	if (!np)
		return -ENODEV;

	/* check if is the vref-supply defined */
	pdata->use_vref = of_property_read_bool(np, "vref-supply");
	pdata->vref_buffered =
		of_property_read_bool(np, "microchip,vref-buffered");

	return 0;
}
#else
static int mcp4725_probe_dt(struct device *dev,
			    struct mcp4725_platform_data *platform_data)
{
	return -ENODEV;
}
#endif

static int mcp4725_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mcp4725_data *data;
	struct iio_dev *indio_dev;
	struct mcp4725_platform_data *pdata, pdata_dt;
	u8 inbuf[4];
	u8 pd;
	u8 ref;
	int err;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (indio_dev == NULL)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	if (client->dev.of_node)
		data->id = (enum chip_id)of_device_get_match_data(&client->dev);
	else
		data->id = id->driver_data;
	pdata = dev_get_platdata(&client->dev);

	if (!pdata) {
		err = mcp4725_probe_dt(&client->dev, &pdata_dt);
		if (err) {
			dev_err(&client->dev,
				"invalid platform or devicetree data");
			return err;
		}
		pdata = &pdata_dt;
	}

	if (data->id == MCP4725 && pdata->use_vref) {
		dev_err(&client->dev,
			"external reference is unavailable on MCP4725");
		return -EINVAL;
	}

	if (!pdata->use_vref && pdata->vref_buffered) {
		dev_err(&client->dev,
			"buffering is unavailable on the internal reference");
		return -EINVAL;
	}

	if (!pdata->use_vref)
		data->ref_mode = MCP472X_REF_VDD;
	else
		data->ref_mode = pdata->vref_buffered ?
			MCP472X_REF_VREF_BUFFERED :
			MCP472X_REF_VREF_UNBUFFERED;

	data->vdd_reg = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(data->vdd_reg))
		return PTR_ERR(data->vdd_reg);

	err = regulator_enable(data->vdd_reg);
	if (err)
		return err;

	if (pdata->use_vref) {
		data->vref_reg = devm_regulator_get(&client->dev, "vref");
		if (IS_ERR(data->vref_reg)) {
			err = PTR_ERR(data->vref_reg);
			goto err_disable_vdd_reg;
		}

		err = regulator_enable(data->vref_reg);
		if (err)
			goto err_disable_vdd_reg;
	}

	indio_dev->dev.parent = &client->dev;
	indio_dev->name = id->name;
	indio_dev->info = &mcp4725_info;
	indio_dev->channels = &mcp472x_channel[id->driver_data];
	indio_dev->num_channels = 1;
	indio_dev->modes = INDIO_DIRECT_MODE;

	/* read current DAC value and settings */
	err = i2c_master_recv(client, inbuf, data->id == MCP4725 ? 3 : 4);

	if (err < 0) {
		dev_err(&client->dev, "failed to read DAC value");
		goto err_disable_vref_reg;
	}
	pd = (inbuf[0] >> 1) & 0x3;
	data->powerdown = pd > 0 ? true : false;
	data->powerdown_mode = pd ? pd - 1 : 2; /* largest resistor to gnd */
	data->dac_value = (inbuf[1] << 4) | (inbuf[2] >> 4);
	if (data->id == MCP4726)
		ref = (inbuf[3] >> 3) & 0x3;

	if (data->id == MCP4726 && ref != data->ref_mode) {
		dev_info(&client->dev,
			"voltage reference mode differs (conf: %u, eeprom: %u), setting %u",
			data->ref_mode, ref, data->ref_mode);
		err = mcp4726_set_cfg(indio_dev);
		if (err < 0)
			goto err_disable_vref_reg;
	}
 
	err = iio_device_register(indio_dev);
	if (err)
		goto err_disable_vref_reg;

	return 0;

err_disable_vref_reg:
	if (data->vref_reg)
		regulator_disable(data->vref_reg);

err_disable_vdd_reg:
	regulator_disable(data->vdd_reg);

	return err;
}

static int mcp4725_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct mcp4725_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	if (data->vref_reg)
		regulator_disable(data->vref_reg);
	regulator_disable(data->vdd_reg);

	return 0;
}

static const struct i2c_device_id mcp4725_id[] = {
	{ "mcp4725", MCP4725 },
	{ "mcp4726", MCP4726 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcp4725_id);

#ifdef CONFIG_OF
static const struct of_device_id mcp4725_of_match[] = {
	{
		.compatible = "microchip,mcp4725",
		.data = (void *)MCP4725
	},
	{
		.compatible = "microchip,mcp4726",
		.data = (void *)MCP4726
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mcp4725_of_match);
#endif

static struct i2c_driver mcp4725_driver = {
	.driver = {
		.name	= MCP4725_DRV_NAME,
		.of_match_table = of_match_ptr(mcp4725_of_match),
		.pm	= MCP4725_PM_OPS,
	},
	.probe		= mcp4725_probe,
	.remove		= mcp4725_remove,
	.id_table	= mcp4725_id,
};
module_i2c_driver(mcp4725_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("MCP4725/6 12-bit DAC");
MODULE_LICENSE("GPL");
