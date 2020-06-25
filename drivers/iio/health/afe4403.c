// SPDX-License-Identifier: GPL-2.0-only
/*
 * AFE4403 Heart Rate Monitors and Low-Cost Pulse Oximeters
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include <asm/unaligned.h>

#include "afe440x.h"

#define AFE4403_DRIVER_NAME		"afe4403"

/* AFE4403 Registers */
#define AFE4403_TIAGAIN			0x20
#define AFE4403_TIA_AMB_GAIN		0x21

enum afe4403_fields {
	/* Gains */
	F_RF_LED1, F_CF_LED1,
	F_RF_LED, F_CF_LED,

	/* LED Current */
	F_ILED1, F_ILED2,

	/* sentinel */
	F_MAX_FIELDS
};

static const struct reg_field afe4403_reg_fields[] = {
	/* Gains */
	[F_RF_LED1]	= REG_FIELD(AFE4403_TIAGAIN, 0, 2),
	[F_CF_LED1]	= REG_FIELD(AFE4403_TIAGAIN, 3, 7),
	[F_RF_LED]	= REG_FIELD(AFE4403_TIA_AMB_GAIN, 0, 2),
	[F_CF_LED]	= REG_FIELD(AFE4403_TIA_AMB_GAIN, 3, 7),
	/* LED Current */
	[F_ILED1]	= REG_FIELD(AFE440X_LEDCNTRL, 0, 7),
	[F_ILED2]	= REG_FIELD(AFE440X_LEDCNTRL, 8, 15),
};

/**
 * struct afe4403_data - AFE4403 device instance data
 * @dev: Device structure
 * @spi: SPI device handle
 * @regmap: Register map of the device
 * @fields: Register fields of the device
 * @regulator: Pointer to the regulator for the IC
 * @trig: IIO trigger for this device
 * @irq: ADC_RDY line interrupt number
 */
struct afe4403_data {
	struct device *dev;
	struct spi_device *spi;
	struct regmap *regmap;
	struct regmap_field *fields[F_MAX_FIELDS];
	struct regulator *regulator;
	struct iio_trigger *trig;
	int irq;
};

enum afe4403_chan_id {
	LED2 = 1,
	ALED2,
	LED1,
	ALED1,
	LED2_ALED2,
	LED1_ALED1,
};

static const unsigned int afe4403_channel_values[] = {
	[LED2] = AFE440X_LED2VAL,
	[ALED2] = AFE440X_ALED2VAL,
	[LED1] = AFE440X_LED1VAL,
	[ALED1] = AFE440X_ALED1VAL,
	[LED2_ALED2] = AFE440X_LED2_ALED2VAL,
	[LED1_ALED1] = AFE440X_LED1_ALED1VAL,
};

static const unsigned int afe4403_channel_leds[] = {
	[LED2] = F_ILED2,
	[LED1] = F_ILED1,
};

static const struct iio_chan_spec afe4403_channels[] = {
	/* ADC values */
	AFE440X_INTENSITY_CHAN(LED2, 0),
	AFE440X_INTENSITY_CHAN(ALED2, 0),
	AFE440X_INTENSITY_CHAN(LED1, 0),
	AFE440X_INTENSITY_CHAN(ALED1, 0),
	AFE440X_INTENSITY_CHAN(LED2_ALED2, 0),
	AFE440X_INTENSITY_CHAN(LED1_ALED1, 0),
	/* LED current */
	AFE440X_CURRENT_CHAN(LED2),
	AFE440X_CURRENT_CHAN(LED1),
};

static const struct afe440x_val_table afe4403_res_table[] = {
	{ 500000 }, { 250000 }, { 100000 }, { 50000 },
	{ 25000 }, { 10000 }, { 1000000 }, { 0 },
};
AFE440X_TABLE_ATTR(in_intensity_resistance_available, afe4403_res_table);

static const struct afe440x_val_table afe4403_cap_table[] = {
	{ 0, 5000 }, { 0, 10000 }, { 0, 20000 }, { 0, 25000 },
	{ 0, 30000 }, { 0, 35000 }, { 0, 45000 }, { 0, 50000 },
	{ 0, 55000 }, { 0, 60000 }, { 0, 70000 }, { 0, 75000 },
	{ 0, 80000 }, { 0, 85000 }, { 0, 95000 }, { 0, 100000 },
	{ 0, 155000 }, { 0, 160000 }, { 0, 170000 }, { 0, 175000 },
	{ 0, 180000 }, { 0, 185000 }, { 0, 195000 }, { 0, 200000 },
	{ 0, 205000 }, { 0, 210000 }, { 0, 220000 }, { 0, 225000 },
	{ 0, 230000 }, { 0, 235000 }, { 0, 245000 }, { 0, 250000 },
};
AFE440X_TABLE_ATTR(in_intensity_capacitance_available, afe4403_cap_table);

static ssize_t afe440x_show_register(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct afe4403_data *afe = iio_priv(indio_dev);
	struct afe440x_attr *afe440x_attr = to_afe440x_attr(attr);
	unsigned int reg_val;
	int vals[2];
	int ret;

	ret = regmap_field_read(afe->fields[afe440x_attr->field], &reg_val);
	if (ret)
		return ret;

	if (reg_val >= afe440x_attr->table_size)
		return -EINVAL;

	vals[0] = afe440x_attr->val_table[reg_val].integer;
	vals[1] = afe440x_attr->val_table[reg_val].fract;

	return iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, 2, vals);
}

static ssize_t afe440x_store_register(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct afe4403_data *afe = iio_priv(indio_dev);
	struct afe440x_attr *afe440x_attr = to_afe440x_attr(attr);
	int val, integer, fract, ret;

	ret = iio_str_to_fixpoint(buf, 100000, &integer, &fract);
	if (ret)
		return ret;

	for (val = 0; val < afe440x_attr->table_size; val++)
		if (afe440x_attr->val_table[val].integer == integer &&
		    afe440x_attr->val_table[val].fract == fract)
			break;
	if (val == afe440x_attr->table_size)
		return -EINVAL;

	ret = regmap_field_write(afe->fields[afe440x_attr->field], val);
	if (ret)
		return ret;

	return count;
}

static AFE440X_ATTR(in_intensity1_resistance, F_RF_LED, afe4403_res_table);
static AFE440X_ATTR(in_intensity1_capacitance, F_CF_LED, afe4403_cap_table);

static AFE440X_ATTR(in_intensity2_resistance, F_RF_LED, afe4403_res_table);
static AFE440X_ATTR(in_intensity2_capacitance, F_CF_LED, afe4403_cap_table);

static AFE440X_ATTR(in_intensity3_resistance, F_RF_LED1, afe4403_res_table);
static AFE440X_ATTR(in_intensity3_capacitance, F_CF_LED1, afe4403_cap_table);

static AFE440X_ATTR(in_intensity4_resistance, F_RF_LED1, afe4403_res_table);
static AFE440X_ATTR(in_intensity4_capacitance, F_CF_LED1, afe4403_cap_table);

static struct attribute *afe440x_attributes[] = {
	&dev_attr_in_intensity_resistance_available.attr,
	&dev_attr_in_intensity_capacitance_available.attr,
	&afe440x_attr_in_intensity1_resistance.dev_attr.attr,
	&afe440x_attr_in_intensity1_capacitance.dev_attr.attr,
	&afe440x_attr_in_intensity2_resistance.dev_attr.attr,
	&afe440x_attr_in_intensity2_capacitance.dev_attr.attr,
	&afe440x_attr_in_intensity3_resistance.dev_attr.attr,
	&afe440x_attr_in_intensity3_capacitance.dev_attr.attr,
	&afe440x_attr_in_intensity4_resistance.dev_attr.attr,
	&afe440x_attr_in_intensity4_capacitance.dev_attr.attr,
	NULL
};

static const struct attribute_group afe440x_attribute_group = {
	.attrs = afe440x_attributes
};

static int afe4403_read(struct afe4403_data *afe, unsigned int reg, u32 *val)
{
	u8 tx[4] = {AFE440X_CONTROL0, 0x0, 0x0, AFE440X_CONTROL0_READ};
	u8 rx[3];
	int ret;

	/* Enable reading from the device */
	ret = spi_write_then_read(afe->spi, tx, 4, NULL, 0);
	if (ret)
		return ret;

	ret = spi_write_then_read(afe->spi, &reg, 1, rx, sizeof(rx));
	if (ret)
		return ret;

	*val = get_unaligned_be24(&rx[0]);

	/* Disable reading from the device */
	tx[3] = AFE440X_CONTROL0_WRITE;
	ret = spi_write_then_read(afe->spi, tx, 4, NULL, 0);
	if (ret)
		return ret;

	return 0;
}

static int afe4403_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct afe4403_data *afe = iio_priv(indio_dev);
	unsigned int reg = afe4403_channel_values[chan->address];
	unsigned int field = afe4403_channel_leds[chan->address];
	int ret;

	switch (chan->type) {
	case IIO_INTENSITY:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = afe4403_read(afe, reg, val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		}
		break;
	case IIO_CURRENT:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = regmap_field_read(afe->fields[field], val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		case IIO_CHAN_INFO_SCALE:
			*val = 0;
			*val2 = 800000;
			return IIO_VAL_INT_PLUS_MICRO;
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static int afe4403_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct afe4403_data *afe = iio_priv(indio_dev);
	unsigned int field = afe4403_channel_leds[chan->address];

	switch (chan->type) {
	case IIO_CURRENT:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			return regmap_field_write(afe->fields[field], val);
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info afe4403_iio_info = {
	.attrs = &afe440x_attribute_group,
	.read_raw = afe4403_read_raw,
	.write_raw = afe4403_write_raw,
};

static irqreturn_t afe4403_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct afe4403_data *afe = iio_priv(indio_dev);
	int ret, bit, i = 0;
	s32 buffer[8];
	u8 tx[4] = {AFE440X_CONTROL0, 0x0, 0x0, AFE440X_CONTROL0_READ};
	u8 rx[3];

	/* Enable reading from the device */
	ret = spi_write_then_read(afe->spi, tx, 4, NULL, 0);
	if (ret)
		goto err;

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = spi_write_then_read(afe->spi,
					  &afe4403_channel_values[bit], 1,
					  rx, sizeof(rx));
		if (ret)
			goto err;

		buffer[i++] = get_unaligned_be24(&rx[0]);
	}

	/* Disable reading from the device */
	tx[3] = AFE440X_CONTROL0_WRITE;
	ret = spi_write_then_read(afe->spi, tx, 4, NULL, 0);
	if (ret)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, buffer, pf->timestamp);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops afe4403_trigger_ops = {
};

#define AFE4403_TIMING_PAIRS			\
	{ AFE440X_LED2STC,	0x000050 },	\
	{ AFE440X_LED2ENDC,	0x0003e7 },	\
	{ AFE440X_LED1LEDSTC,	0x0007d0 },	\
	{ AFE440X_LED1LEDENDC,	0x000bb7 },	\
	{ AFE440X_ALED2STC,	0x000438 },	\
	{ AFE440X_ALED2ENDC,	0x0007cf },	\
	{ AFE440X_LED1STC,	0x000820 },	\
	{ AFE440X_LED1ENDC,	0x000bb7 },	\
	{ AFE440X_LED2LEDSTC,	0x000000 },	\
	{ AFE440X_LED2LEDENDC,	0x0003e7 },	\
	{ AFE440X_ALED1STC,	0x000c08 },	\
	{ AFE440X_ALED1ENDC,	0x000f9f },	\
	{ AFE440X_LED2CONVST,	0x0003ef },	\
	{ AFE440X_LED2CONVEND,	0x0007cf },	\
	{ AFE440X_ALED2CONVST,	0x0007d7 },	\
	{ AFE440X_ALED2CONVEND,	0x000bb7 },	\
	{ AFE440X_LED1CONVST,	0x000bbf },	\
	{ AFE440X_LED1CONVEND,	0x009c3f },	\
	{ AFE440X_ALED1CONVST,	0x000fa7 },	\
	{ AFE440X_ALED1CONVEND,	0x001387 },	\
	{ AFE440X_ADCRSTSTCT0,	0x0003e8 },	\
	{ AFE440X_ADCRSTENDCT0,	0x0003eb },	\
	{ AFE440X_ADCRSTSTCT1,	0x0007d0 },	\
	{ AFE440X_ADCRSTENDCT1,	0x0007d3 },	\
	{ AFE440X_ADCRSTSTCT2,	0x000bb8 },	\
	{ AFE440X_ADCRSTENDCT2,	0x000bbb },	\
	{ AFE440X_ADCRSTSTCT3,	0x000fa0 },	\
	{ AFE440X_ADCRSTENDCT3,	0x000fa3 },	\
	{ AFE440X_PRPCOUNT,	0x009c3f },	\
	{ AFE440X_PDNCYCLESTC,	0x001518 },	\
	{ AFE440X_PDNCYCLEENDC,	0x00991f }

static const struct reg_sequence afe4403_reg_sequences[] = {
	AFE4403_TIMING_PAIRS,
	{ AFE440X_CONTROL1, AFE440X_CONTROL1_TIMEREN },
	{ AFE4403_TIAGAIN, AFE440X_TIAGAIN_ENSEPGAIN },
};

static const struct regmap_range afe4403_yes_ranges[] = {
	regmap_reg_range(AFE440X_LED2VAL, AFE440X_LED1_ALED1VAL),
};

static const struct regmap_access_table afe4403_volatile_table = {
	.yes_ranges = afe4403_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(afe4403_yes_ranges),
};

static const struct regmap_config afe4403_regmap_config = {
	.reg_bits = 8,
	.val_bits = 24,

	.max_register = AFE440X_PDNCYCLEENDC,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &afe4403_volatile_table,
};

static const struct of_device_id afe4403_of_match[] = {
	{ .compatible = "ti,afe4403", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, afe4403_of_match);

static int __maybe_unused afe4403_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = spi_get_drvdata(to_spi_device(dev));
	struct afe4403_data *afe = iio_priv(indio_dev);
	int ret;

	ret = regmap_update_bits(afe->regmap, AFE440X_CONTROL2,
				 AFE440X_CONTROL2_PDN_AFE,
				 AFE440X_CONTROL2_PDN_AFE);
	if (ret)
		return ret;

	ret = regulator_disable(afe->regulator);
	if (ret) {
		dev_err(dev, "Unable to disable regulator\n");
		return ret;
	}

	return 0;
}

static int __maybe_unused afe4403_resume(struct device *dev)
{
	struct iio_dev *indio_dev = spi_get_drvdata(to_spi_device(dev));
	struct afe4403_data *afe = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(afe->regulator);
	if (ret) {
		dev_err(dev, "Unable to enable regulator\n");
		return ret;
	}

	ret = regmap_update_bits(afe->regmap, AFE440X_CONTROL2,
				 AFE440X_CONTROL2_PDN_AFE, 0);
	if (ret)
		return ret;

	return 0;
}

static SIMPLE_DEV_PM_OPS(afe4403_pm_ops, afe4403_suspend, afe4403_resume);

static int afe4403_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct afe4403_data *afe;
	int i, ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*afe));
	if (!indio_dev)
		return -ENOMEM;

	afe = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	afe->dev = &spi->dev;
	afe->spi = spi;
	afe->irq = spi->irq;

	afe->regmap = devm_regmap_init_spi(spi, &afe4403_regmap_config);
	if (IS_ERR(afe->regmap)) {
		dev_err(afe->dev, "Unable to allocate register map\n");
		return PTR_ERR(afe->regmap);
	}

	for (i = 0; i < F_MAX_FIELDS; i++) {
		afe->fields[i] = devm_regmap_field_alloc(afe->dev, afe->regmap,
							 afe4403_reg_fields[i]);
		if (IS_ERR(afe->fields[i])) {
			dev_err(afe->dev, "Unable to allocate regmap fields\n");
			return PTR_ERR(afe->fields[i]);
		}
	}

	afe->regulator = devm_regulator_get(afe->dev, "tx_sup");
	if (IS_ERR(afe->regulator)) {
		dev_err(afe->dev, "Unable to get regulator\n");
		return PTR_ERR(afe->regulator);
	}
	ret = regulator_enable(afe->regulator);
	if (ret) {
		dev_err(afe->dev, "Unable to enable regulator\n");
		return ret;
	}

	ret = regmap_write(afe->regmap, AFE440X_CONTROL0,
			   AFE440X_CONTROL0_SW_RESET);
	if (ret) {
		dev_err(afe->dev, "Unable to reset device\n");
		goto err_disable_reg;
	}

	ret = regmap_multi_reg_write(afe->regmap, afe4403_reg_sequences,
				     ARRAY_SIZE(afe4403_reg_sequences));
	if (ret) {
		dev_err(afe->dev, "Unable to set register defaults\n");
		goto err_disable_reg;
	}

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = afe->dev;
	indio_dev->channels = afe4403_channels;
	indio_dev->num_channels = ARRAY_SIZE(afe4403_channels);
	indio_dev->name = AFE4403_DRIVER_NAME;
	indio_dev->info = &afe4403_iio_info;

	if (afe->irq > 0) {
		afe->trig = devm_iio_trigger_alloc(afe->dev,
						   "%s-dev%d",
						   indio_dev->name,
						   indio_dev->id);
		if (!afe->trig) {
			dev_err(afe->dev, "Unable to allocate IIO trigger\n");
			ret = -ENOMEM;
			goto err_disable_reg;
		}

		iio_trigger_set_drvdata(afe->trig, indio_dev);

		afe->trig->ops = &afe4403_trigger_ops;
		afe->trig->dev.parent = afe->dev;

		ret = iio_trigger_register(afe->trig);
		if (ret) {
			dev_err(afe->dev, "Unable to register IIO trigger\n");
			goto err_disable_reg;
		}

		ret = devm_request_threaded_irq(afe->dev, afe->irq,
						iio_trigger_generic_data_rdy_poll,
						NULL, IRQF_ONESHOT,
						AFE4403_DRIVER_NAME,
						afe->trig);
		if (ret) {
			dev_err(afe->dev, "Unable to request IRQ\n");
			goto err_trig;
		}
	}

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
					 afe4403_trigger_handler, NULL);
	if (ret) {
		dev_err(afe->dev, "Unable to setup buffer\n");
		goto err_trig;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(afe->dev, "Unable to register IIO device\n");
		goto err_buff;
	}

	return 0;

err_buff:
	iio_triggered_buffer_cleanup(indio_dev);
err_trig:
	if (afe->irq > 0)
		iio_trigger_unregister(afe->trig);
err_disable_reg:
	regulator_disable(afe->regulator);

	return ret;
}

static int afe4403_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct afe4403_data *afe = iio_priv(indio_dev);
	int ret;

	iio_device_unregister(indio_dev);

	iio_triggered_buffer_cleanup(indio_dev);

	if (afe->irq > 0)
		iio_trigger_unregister(afe->trig);

	ret = regulator_disable(afe->regulator);
	if (ret) {
		dev_err(afe->dev, "Unable to disable regulator\n");
		return ret;
	}

	return 0;
}

static const struct spi_device_id afe4403_ids[] = {
	{ "afe4403", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, afe4403_ids);

static struct spi_driver afe4403_spi_driver = {
	.driver = {
		.name = AFE4403_DRIVER_NAME,
		.of_match_table = afe4403_of_match,
		.pm = &afe4403_pm_ops,
	},
	.probe = afe4403_probe,
	.remove = afe4403_remove,
	.id_table = afe4403_ids,
};
module_spi_driver(afe4403_spi_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TI AFE4403 Heart Rate Monitor and Pulse Oximeter AFE");
MODULE_LICENSE("GPL v2");
