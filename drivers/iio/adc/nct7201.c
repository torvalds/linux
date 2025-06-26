// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Nuvoton nct7201 and nct7202 power monitor chips.
 *
 * Copyright (c) 2024-2025 Nuvoton Technology corporation.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

#define NCT7201_REG_INTERRUPT_STATUS			0x0C
#define NCT7201_REG_VOLT_LOW_BYTE			0x0F
#define NCT7201_REG_CONFIGURATION			0x10
#define  NCT7201_BIT_CONFIGURATION_START		BIT(0)
#define  NCT7201_BIT_CONFIGURATION_ALERT_MSK		BIT(1)
#define  NCT7201_BIT_CONFIGURATION_CONV_RATE		BIT(2)
#define  NCT7201_BIT_CONFIGURATION_RESET		BIT(7)

#define NCT7201_REG_ADVANCED_CONFIGURATION		0x11
#define  NCT7201_BIT_ADVANCED_CONF_MOD_ALERT		BIT(0)
#define  NCT7201_BIT_ADVANCED_CONF_MOD_STS		BIT(1)
#define  NCT7201_BIT_ADVANCED_CONF_FAULT_QUEUE		BIT(2)
#define  NCT7201_BIT_ADVANCED_CONF_EN_DEEP_SHUTDOWN	BIT(4)
#define  NCT7201_BIT_ADVANCED_CONF_EN_SMB_TIMEOUT	BIT(5)
#define  NCT7201_BIT_ADVANCED_CONF_MOD_RSTIN		BIT(7)

#define NCT7201_REG_CHANNEL_INPUT_MODE			0x12
#define NCT7201_REG_CHANNEL_ENABLE			0x13
#define NCT7201_REG_INTERRUPT_MASK_1			0x15
#define NCT7201_REG_INTERRUPT_MASK_2			0x16
#define NCT7201_REG_BUSY_STATUS			0x1E
#define  NCT7201_BIT_BUSY				BIT(0)
#define  NCT7201_BIT_PWR_UP				BIT(1)
#define NCT7201_REG_ONE_SHOT				0x1F
#define NCT7201_REG_SMUS_ADDRESS			0xFC
#define NCT7201_REG_VIN_MASK				GENMASK(15, 3)

#define NCT7201_REG_VIN(i)				(0x00 + i)
#define NCT7201_REG_VIN_HIGH_LIMIT(i)			(0x20 + (i) * 2)
#define NCT7201_REG_VIN_LOW_LIMIT(i)			(0x21 + (i) * 2)
#define NCT7201_MAX_CHANNEL				12

static const struct regmap_range nct7201_read_reg_range[] = {
	regmap_reg_range(NCT7201_REG_INTERRUPT_STATUS, NCT7201_REG_BUSY_STATUS),
	regmap_reg_range(NCT7201_REG_SMUS_ADDRESS, NCT7201_REG_SMUS_ADDRESS),
};

static const struct regmap_access_table nct7201_readable_regs_tbl = {
	.yes_ranges = nct7201_read_reg_range,
	.n_yes_ranges = ARRAY_SIZE(nct7201_read_reg_range),
};

static const struct regmap_range nct7201_write_reg_range[] = {
	regmap_reg_range(NCT7201_REG_CONFIGURATION, NCT7201_REG_INTERRUPT_MASK_2),
	regmap_reg_range(NCT7201_REG_ONE_SHOT, NCT7201_REG_ONE_SHOT),
};

static const struct regmap_access_table nct7201_writeable_regs_tbl = {
	.yes_ranges = nct7201_write_reg_range,
	.n_yes_ranges = ARRAY_SIZE(nct7201_write_reg_range),
};

static const struct regmap_range nct7201_read_vin_reg_range[] = {
	regmap_reg_range(NCT7201_REG_VIN(0), NCT7201_REG_VIN(NCT7201_MAX_CHANNEL - 1)),
	regmap_reg_range(NCT7201_REG_VIN_HIGH_LIMIT(0),
			 NCT7201_REG_VIN_LOW_LIMIT(NCT7201_MAX_CHANNEL - 1)),
};

static const struct regmap_access_table nct7201_readable_vin_regs_tbl = {
	.yes_ranges = nct7201_read_vin_reg_range,
	.n_yes_ranges = ARRAY_SIZE(nct7201_read_vin_reg_range),
};

static const struct regmap_range nct7201_write_vin_reg_range[] = {
	regmap_reg_range(NCT7201_REG_VIN_HIGH_LIMIT(0),
			 NCT7201_REG_VIN_LOW_LIMIT(NCT7201_MAX_CHANNEL - 1)),
};

static const struct regmap_access_table nct7201_writeable_vin_regs_tbl = {
	.yes_ranges = nct7201_write_vin_reg_range,
	.n_yes_ranges = ARRAY_SIZE(nct7201_write_vin_reg_range),
};

static const struct regmap_config nct7201_regmap8_config = {
	.name = "vin-data-read-byte",
	.reg_bits = 8,
	.val_bits = 8,
	.use_single_read = true,
	.use_single_write = true,
	.max_register = 0xff,
	.rd_table = &nct7201_readable_regs_tbl,
	.wr_table = &nct7201_writeable_regs_tbl,
};

static const struct regmap_config nct7201_regmap16_config = {
	.name = "vin-data-read-word",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xff,
	.rd_table = &nct7201_readable_vin_regs_tbl,
	.wr_table = &nct7201_writeable_vin_regs_tbl,
};

struct nct7201_chip_info {
	struct regmap *regmap;
	struct regmap *regmap16;
	int num_vin_channels;
	__le16 vin_mask;
};

struct nct7201_adc_model_data {
	const char *model_name;
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	int num_vin_channels;
};

static const struct iio_event_spec nct7201_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
};

#define NCT7201_VOLTAGE_CHANNEL(num)					\
	{								\
		.type = IIO_VOLTAGE,					\
		.indexed = 1,						\
		.channel = num + 1,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.address = num,						\
		.event_spec = nct7201_events,				\
		.num_event_specs = ARRAY_SIZE(nct7201_events),		\
	}

static const struct iio_chan_spec nct7201_channels[] = {
	NCT7201_VOLTAGE_CHANNEL(0),
	NCT7201_VOLTAGE_CHANNEL(1),
	NCT7201_VOLTAGE_CHANNEL(2),
	NCT7201_VOLTAGE_CHANNEL(3),
	NCT7201_VOLTAGE_CHANNEL(4),
	NCT7201_VOLTAGE_CHANNEL(5),
	NCT7201_VOLTAGE_CHANNEL(6),
	NCT7201_VOLTAGE_CHANNEL(7),
};

static const struct iio_chan_spec nct7202_channels[] = {
	NCT7201_VOLTAGE_CHANNEL(0),
	NCT7201_VOLTAGE_CHANNEL(1),
	NCT7201_VOLTAGE_CHANNEL(2),
	NCT7201_VOLTAGE_CHANNEL(3),
	NCT7201_VOLTAGE_CHANNEL(4),
	NCT7201_VOLTAGE_CHANNEL(5),
	NCT7201_VOLTAGE_CHANNEL(6),
	NCT7201_VOLTAGE_CHANNEL(7),
	NCT7201_VOLTAGE_CHANNEL(8),
	NCT7201_VOLTAGE_CHANNEL(9),
	NCT7201_VOLTAGE_CHANNEL(10),
	NCT7201_VOLTAGE_CHANNEL(11),
};

static int nct7201_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct nct7201_chip_info *chip = iio_priv(indio_dev);
	unsigned int value;
	int err;

	if (chan->type != IIO_VOLTAGE)
		return -EOPNOTSUPP;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = regmap_read(chip->regmap16, NCT7201_REG_VIN(chan->address), &value);
		if (err)
			return err;
		*val = FIELD_GET(NCT7201_REG_VIN_MASK, value);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* From the datasheet, we have to multiply by 0.0004995 */
		*val = 0;
		*val2 = 499500;
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int nct7201_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct nct7201_chip_info *chip = iio_priv(indio_dev);
	unsigned int value;
	int err;

	if (chan->type != IIO_VOLTAGE)
		return -EOPNOTSUPP;

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	if (dir == IIO_EV_DIR_FALLING)
		err = regmap_read(chip->regmap16, NCT7201_REG_VIN_LOW_LIMIT(chan->address),
				  &value);
	else
		err = regmap_read(chip->regmap16, NCT7201_REG_VIN_HIGH_LIMIT(chan->address),
				  &value);
	if (err)
		return err;

	*val = FIELD_GET(NCT7201_REG_VIN_MASK, value);

	return IIO_VAL_INT;
}

static int nct7201_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct nct7201_chip_info *chip = iio_priv(indio_dev);
	int err;

	if (chan->type != IIO_VOLTAGE)
		return -EOPNOTSUPP;

	if (info != IIO_EV_INFO_VALUE)
		return -EOPNOTSUPP;

	if (dir == IIO_EV_DIR_FALLING)
		err = regmap_write(chip->regmap16, NCT7201_REG_VIN_LOW_LIMIT(chan->address),
				   FIELD_PREP(NCT7201_REG_VIN_MASK, val));
	else
		err = regmap_write(chip->regmap16, NCT7201_REG_VIN_HIGH_LIMIT(chan->address),
				   FIELD_PREP(NCT7201_REG_VIN_MASK, val));

	return err;
}

static int nct7201_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct nct7201_chip_info *chip = iio_priv(indio_dev);

	if (chan->type != IIO_VOLTAGE)
		return -EOPNOTSUPP;

	return !!(le16_to_cpu(chip->vin_mask) & BIT(chan->address));
}

static int nct7201_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      bool state)
{
	struct nct7201_chip_info *chip = iio_priv(indio_dev);
	__le16 mask = cpu_to_le16(BIT(chan->address));
	int err;

	if (chan->type != IIO_VOLTAGE)
		return -EOPNOTSUPP;

	if (state)
		chip->vin_mask |= mask;
	else
		chip->vin_mask &= ~mask;

	if (chip->num_vin_channels <= 8)
		err = regmap_write(chip->regmap, NCT7201_REG_CHANNEL_ENABLE,
				   le16_to_cpu(chip->vin_mask));
	else
		err = regmap_bulk_write(chip->regmap, NCT7201_REG_CHANNEL_ENABLE,
					&chip->vin_mask, sizeof(chip->vin_mask));

	return err;
}

static const struct iio_info nct7201_info = {
	.read_raw = nct7201_read_raw,
	.read_event_config = nct7201_read_event_config,
	.write_event_config = nct7201_write_event_config,
	.read_event_value = nct7201_read_event_value,
	.write_event_value = nct7201_write_event_value,
};

static const struct iio_info nct7201_info_no_irq = {
	.read_raw = nct7201_read_raw,
};

static const struct nct7201_adc_model_data nct7201_model_data = {
	.model_name = "nct7201",
	.channels = nct7201_channels,
	.num_channels = ARRAY_SIZE(nct7201_channels),
	.num_vin_channels = 8,
};

static const struct nct7201_adc_model_data nct7202_model_data = {
	.model_name = "nct7202",
	.channels = nct7202_channels,
	.num_channels = ARRAY_SIZE(nct7202_channels),
	.num_vin_channels = 12,
};

static int nct7201_init_chip(struct nct7201_chip_info *chip)
{
	struct device *dev = regmap_get_device(chip->regmap);
	__le16 data = cpu_to_le16(GENMASK(chip->num_vin_channels - 1, 0));
	unsigned int value;
	int err;

	err = regmap_write(chip->regmap, NCT7201_REG_CONFIGURATION,
			   NCT7201_BIT_CONFIGURATION_RESET);
	if (err)
		return dev_err_probe(dev, err, "Failed to reset chip\n");

	/*
	 * After about 25 msecs, the device should be ready and then the power-up
	 * bit will be set to 1.
	 */
	fsleep(25 * USEC_PER_MSEC);

	err = regmap_read(chip->regmap, NCT7201_REG_BUSY_STATUS, &value);
	if (err)
		return dev_err_probe(dev, err, "Failed to read busy status\n");
	if (!(value & NCT7201_BIT_PWR_UP))
		return dev_err_probe(dev, -EIO, "Failed to power up after reset\n");

	/* Enable Channels */
	if (chip->num_vin_channels <= 8)
		err = regmap_write(chip->regmap, NCT7201_REG_CHANNEL_ENABLE,
				   le16_to_cpu(data));
	else
		err = regmap_bulk_write(chip->regmap, NCT7201_REG_CHANNEL_ENABLE,
					&data, sizeof(data));
	if (err)
		return dev_err_probe(dev, err, "Failed to enable channels\n");

	err = regmap_bulk_read(chip->regmap, NCT7201_REG_CHANNEL_ENABLE,
			       &chip->vin_mask, sizeof(chip->vin_mask));
	if (err)
		return dev_err_probe(dev, err,
				     "Failed to read channel enable register\n");

	/* Start monitoring if needed */
	err = regmap_set_bits(chip->regmap, NCT7201_REG_CONFIGURATION,
			      NCT7201_BIT_CONFIGURATION_START);
	if (err)
		return dev_err_probe(dev, err, "Failed to start monitoring\n");

	return 0;
}

static irqreturn_t nct7201_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct nct7201_chip_info *chip = iio_priv(indio_dev);
	__le16 data;
	int err;

	err = regmap_bulk_read(chip->regmap, NCT7201_REG_INTERRUPT_STATUS,
			       &data, sizeof(data));
	if (err)
		return IRQ_NONE;

	if (data)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE,
						    0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns(indio_dev));

	return IRQ_HANDLED;
}

static int nct7201_probe(struct i2c_client *client)
{
	const struct nct7201_adc_model_data *model_data;
	struct device *dev = &client->dev;
	struct nct7201_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret;

	model_data = i2c_get_match_data(client);
	if (!model_data)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*chip));
	if (!indio_dev)
		return -ENOMEM;
	chip = iio_priv(indio_dev);

	chip->regmap = devm_regmap_init_i2c(client, &nct7201_regmap8_config);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap),
				     "Failed to init regmap\n");

	chip->regmap16 = devm_regmap_init_i2c(client, &nct7201_regmap16_config);
	if (IS_ERR(chip->regmap16))
		return dev_err_probe(dev, PTR_ERR(chip->regmap16),
				     "Failed to init regmap16\n");

	chip->num_vin_channels = model_data->num_vin_channels;

	ret = nct7201_init_chip(chip);
	if (ret)
		return ret;

	indio_dev->name = model_data->model_name;
	indio_dev->channels = model_data->channels;
	indio_dev->num_channels = model_data->num_channels;
	if (client->irq) {
		/* Enable alert function */
		ret = regmap_clear_bits(chip->regmap, NCT7201_REG_CONFIGURATION,
				      NCT7201_BIT_CONFIGURATION_ALERT_MSK);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to enable alert function\n");

		ret = devm_request_threaded_irq(dev, client->irq,
						NULL, nct7201_irq_handler,
						IRQF_TRIGGER_LOW | IRQF_ONESHOT,
						client->name, indio_dev);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to assign interrupt.\n");

		indio_dev->info = &nct7201_info;
	} else {
		indio_dev->info = &nct7201_info_no_irq;
	}
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id nct7201_id[] = {
	{ .name = "nct7201", .driver_data = (kernel_ulong_t)&nct7201_model_data },
	{ .name = "nct7202", .driver_data = (kernel_ulong_t)&nct7202_model_data },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nct7201_id);

static const struct of_device_id nct7201_of_match[] = {
	{
		.compatible = "nuvoton,nct7201",
		.data = &nct7201_model_data,
	},
	{
		.compatible = "nuvoton,nct7202",
		.data = &nct7202_model_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, nct7201_of_match);

static struct i2c_driver nct7201_driver = {
	.driver = {
		.name	= "nct7201",
		.of_match_table = nct7201_of_match,
	},
	.probe = nct7201_probe,
	.id_table = nct7201_id,
};
module_i2c_driver(nct7201_driver);

MODULE_AUTHOR("Eason Yang <j2anfernee@gmail.com>");
MODULE_DESCRIPTION("Nuvoton NCT7201 voltage monitor driver");
MODULE_LICENSE("GPL");
