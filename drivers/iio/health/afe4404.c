/*
 * AFE4404 Heart Rate Monitors and Low-Cost Pulse Oximeters
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#include "afe440x.h"

#define AFE4404_DRIVER_NAME		"afe4404"

/* AFE4404 registers */
#define AFE4404_TIA_GAIN_SEP		0x20
#define AFE4404_TIA_GAIN		0x21
#define AFE4404_PROG_TG_STC		0x34
#define AFE4404_PROG_TG_ENDC		0x35
#define AFE4404_LED3LEDSTC		0x36
#define AFE4404_LED3LEDENDC		0x37
#define AFE4404_CLKDIV_PRF		0x39
#define AFE4404_OFFDAC			0x3a
#define AFE4404_DEC			0x3d
#define AFE4404_AVG_LED2_ALED2VAL	0x3f
#define AFE4404_AVG_LED1_ALED1VAL	0x40

/* AFE4404 CONTROL2 register fields */
#define AFE440X_CONTROL2_OSC_ENABLE	BIT(9)

enum afe4404_fields {
	/* Gains */
	F_TIA_GAIN_SEP, F_TIA_CF_SEP,
	F_TIA_GAIN, TIA_CF,

	/* LED Current */
	F_ILED1, F_ILED2, F_ILED3,

	/* Offset DAC */
	F_OFFDAC_AMB2, F_OFFDAC_LED1, F_OFFDAC_AMB1, F_OFFDAC_LED2,

	/* sentinel */
	F_MAX_FIELDS
};

static const struct reg_field afe4404_reg_fields[] = {
	/* Gains */
	[F_TIA_GAIN_SEP]	= REG_FIELD(AFE4404_TIA_GAIN_SEP, 0, 2),
	[F_TIA_CF_SEP]		= REG_FIELD(AFE4404_TIA_GAIN_SEP, 3, 5),
	[F_TIA_GAIN]		= REG_FIELD(AFE4404_TIA_GAIN, 0, 2),
	[TIA_CF]		= REG_FIELD(AFE4404_TIA_GAIN, 3, 5),
	/* LED Current */
	[F_ILED1]		= REG_FIELD(AFE440X_LEDCNTRL, 0, 5),
	[F_ILED2]		= REG_FIELD(AFE440X_LEDCNTRL, 6, 11),
	[F_ILED3]		= REG_FIELD(AFE440X_LEDCNTRL, 12, 17),
	/* Offset DAC */
	[F_OFFDAC_AMB2]		= REG_FIELD(AFE4404_OFFDAC, 0, 4),
	[F_OFFDAC_LED1]		= REG_FIELD(AFE4404_OFFDAC, 5, 9),
	[F_OFFDAC_AMB1]		= REG_FIELD(AFE4404_OFFDAC, 10, 14),
	[F_OFFDAC_LED2]		= REG_FIELD(AFE4404_OFFDAC, 15, 19),
};

/**
 * struct afe4404_data - AFE4404 device instance data
 * @dev: Device structure
 * @regmap: Register map of the device
 * @fields: Register fields of the device
 * @regulator: Pointer to the regulator for the IC
 * @trig: IIO trigger for this device
 * @irq: ADC_RDY line interrupt number
 */
struct afe4404_data {
	struct device *dev;
	struct regmap *regmap;
	struct regmap_field *fields[F_MAX_FIELDS];
	struct regulator *regulator;
	struct iio_trigger *trig;
	int irq;
};

enum afe4404_chan_id {
	LED2 = 1,
	ALED2,
	LED1,
	ALED1,
	LED2_ALED2,
	LED1_ALED1,
};

static const unsigned int afe4404_channel_values[] = {
	[LED2] = AFE440X_LED2VAL,
	[ALED2] = AFE440X_ALED2VAL,
	[LED1] = AFE440X_LED1VAL,
	[ALED1] = AFE440X_ALED1VAL,
	[LED2_ALED2] = AFE440X_LED2_ALED2VAL,
	[LED1_ALED1] = AFE440X_LED1_ALED1VAL,
};

static const unsigned int afe4404_channel_leds[] = {
	[LED2] = F_ILED2,
	[ALED2] = F_ILED3,
	[LED1] = F_ILED1,
};

static const unsigned int afe4404_channel_offdacs[] = {
	[LED2] = F_OFFDAC_LED2,
	[ALED2] = F_OFFDAC_AMB2,
	[LED1] = F_OFFDAC_LED1,
	[ALED1] = F_OFFDAC_AMB1,
};

static const struct iio_chan_spec afe4404_channels[] = {
	/* ADC values */
	AFE440X_INTENSITY_CHAN(LED2, BIT(IIO_CHAN_INFO_OFFSET)),
	AFE440X_INTENSITY_CHAN(ALED2, BIT(IIO_CHAN_INFO_OFFSET)),
	AFE440X_INTENSITY_CHAN(LED1, BIT(IIO_CHAN_INFO_OFFSET)),
	AFE440X_INTENSITY_CHAN(ALED1, BIT(IIO_CHAN_INFO_OFFSET)),
	AFE440X_INTENSITY_CHAN(LED2_ALED2, 0),
	AFE440X_INTENSITY_CHAN(LED1_ALED1, 0),
	/* LED current */
	AFE440X_CURRENT_CHAN(LED2),
	AFE440X_CURRENT_CHAN(ALED2),
	AFE440X_CURRENT_CHAN(LED1),
};

static const struct afe440x_val_table afe4404_res_table[] = {
	{ .integer = 500000, .fract = 0 },
	{ .integer = 250000, .fract = 0 },
	{ .integer = 100000, .fract = 0 },
	{ .integer = 50000, .fract = 0 },
	{ .integer = 25000, .fract = 0 },
	{ .integer = 10000, .fract = 0 },
	{ .integer = 1000000, .fract = 0 },
	{ .integer = 2000000, .fract = 0 },
};
AFE440X_TABLE_ATTR(in_intensity_resistance_available, afe4404_res_table);

static const struct afe440x_val_table afe4404_cap_table[] = {
	{ .integer = 0, .fract = 5000 },
	{ .integer = 0, .fract = 2500 },
	{ .integer = 0, .fract = 10000 },
	{ .integer = 0, .fract = 7500 },
	{ .integer = 0, .fract = 20000 },
	{ .integer = 0, .fract = 17500 },
	{ .integer = 0, .fract = 25000 },
	{ .integer = 0, .fract = 22500 },
};
AFE440X_TABLE_ATTR(in_intensity_capacitance_available, afe4404_cap_table);

static ssize_t afe440x_show_register(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct afe4404_data *afe = iio_priv(indio_dev);
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
	struct afe4404_data *afe = iio_priv(indio_dev);
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

static AFE440X_ATTR(in_intensity1_resistance, F_TIA_GAIN_SEP, afe4404_res_table);
static AFE440X_ATTR(in_intensity1_capacitance, F_TIA_CF_SEP, afe4404_cap_table);

static AFE440X_ATTR(in_intensity2_resistance, F_TIA_GAIN_SEP, afe4404_res_table);
static AFE440X_ATTR(in_intensity2_capacitance, F_TIA_CF_SEP, afe4404_cap_table);

static AFE440X_ATTR(in_intensity3_resistance, F_TIA_GAIN, afe4404_res_table);
static AFE440X_ATTR(in_intensity3_capacitance, TIA_CF, afe4404_cap_table);

static AFE440X_ATTR(in_intensity4_resistance, F_TIA_GAIN, afe4404_res_table);
static AFE440X_ATTR(in_intensity4_capacitance, TIA_CF, afe4404_cap_table);

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

static int afe4404_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct afe4404_data *afe = iio_priv(indio_dev);
	unsigned int value_reg = afe4404_channel_values[chan->address];
	unsigned int led_field = afe4404_channel_leds[chan->address];
	unsigned int offdac_field = afe4404_channel_offdacs[chan->address];
	int ret;

	switch (chan->type) {
	case IIO_INTENSITY:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = regmap_read(afe->regmap, value_reg, val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		case IIO_CHAN_INFO_OFFSET:
			ret = regmap_field_read(afe->fields[offdac_field], val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		}
		break;
	case IIO_CURRENT:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = regmap_field_read(afe->fields[led_field], val);
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

static int afe4404_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct afe4404_data *afe = iio_priv(indio_dev);
	unsigned int led_field = afe4404_channel_leds[chan->address];
	unsigned int offdac_field = afe4404_channel_offdacs[chan->address];

	switch (chan->type) {
	case IIO_INTENSITY:
		switch (mask) {
		case IIO_CHAN_INFO_OFFSET:
			return regmap_field_write(afe->fields[offdac_field], val);
		}
		break;
	case IIO_CURRENT:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			return regmap_field_write(afe->fields[led_field], val);
		}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info afe4404_iio_info = {
	.attrs = &afe440x_attribute_group,
	.read_raw = afe4404_read_raw,
	.write_raw = afe4404_write_raw,
	.driver_module = THIS_MODULE,
};

static irqreturn_t afe4404_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct afe4404_data *afe = iio_priv(indio_dev);
	int ret, bit, i = 0;
	s32 buffer[10];

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->masklength) {
		ret = regmap_read(afe->regmap, afe4404_channel_values[bit],
				  &buffer[i++]);
		if (ret)
			goto err;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, buffer, pf->timestamp);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops afe4404_trigger_ops = {
	.owner = THIS_MODULE,
};

/* Default timings from data-sheet */
#define AFE4404_TIMING_PAIRS			\
	{ AFE440X_PRPCOUNT,	39999	},	\
	{ AFE440X_LED2LEDSTC,	0	},	\
	{ AFE440X_LED2LEDENDC,	398	},	\
	{ AFE440X_LED2STC,	80	},	\
	{ AFE440X_LED2ENDC,	398	},	\
	{ AFE440X_ADCRSTSTCT0,	5600	},	\
	{ AFE440X_ADCRSTENDCT0,	5606	},	\
	{ AFE440X_LED2CONVST,	5607	},	\
	{ AFE440X_LED2CONVEND,	6066	},	\
	{ AFE4404_LED3LEDSTC,	400	},	\
	{ AFE4404_LED3LEDENDC,	798	},	\
	{ AFE440X_ALED2STC,	480	},	\
	{ AFE440X_ALED2ENDC,	798	},	\
	{ AFE440X_ADCRSTSTCT1,	6068	},	\
	{ AFE440X_ADCRSTENDCT1,	6074	},	\
	{ AFE440X_ALED2CONVST,	6075	},	\
	{ AFE440X_ALED2CONVEND,	6534	},	\
	{ AFE440X_LED1LEDSTC,	800	},	\
	{ AFE440X_LED1LEDENDC,	1198	},	\
	{ AFE440X_LED1STC,	880	},	\
	{ AFE440X_LED1ENDC,	1198	},	\
	{ AFE440X_ADCRSTSTCT2,	6536	},	\
	{ AFE440X_ADCRSTENDCT2,	6542	},	\
	{ AFE440X_LED1CONVST,	6543	},	\
	{ AFE440X_LED1CONVEND,	7003	},	\
	{ AFE440X_ALED1STC,	1280	},	\
	{ AFE440X_ALED1ENDC,	1598	},	\
	{ AFE440X_ADCRSTSTCT3,	7005	},	\
	{ AFE440X_ADCRSTENDCT3,	7011	},	\
	{ AFE440X_ALED1CONVST,	7012	},	\
	{ AFE440X_ALED1CONVEND,	7471	},	\
	{ AFE440X_PDNCYCLESTC,	7671	},	\
	{ AFE440X_PDNCYCLEENDC,	39199	}

static const struct reg_sequence afe4404_reg_sequences[] = {
	AFE4404_TIMING_PAIRS,
	{ AFE440X_CONTROL1, AFE440X_CONTROL1_TIMEREN },
	{ AFE4404_TIA_GAIN_SEP, AFE440X_TIAGAIN_ENSEPGAIN },
	{ AFE440X_CONTROL2, AFE440X_CONTROL2_OSC_ENABLE	},
};

static const struct regmap_range afe4404_yes_ranges[] = {
	regmap_reg_range(AFE440X_LED2VAL, AFE440X_LED1_ALED1VAL),
	regmap_reg_range(AFE4404_AVG_LED2_ALED2VAL, AFE4404_AVG_LED1_ALED1VAL),
};

static const struct regmap_access_table afe4404_volatile_table = {
	.yes_ranges = afe4404_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(afe4404_yes_ranges),
};

static const struct regmap_config afe4404_regmap_config = {
	.reg_bits = 8,
	.val_bits = 24,

	.max_register = AFE4404_AVG_LED1_ALED1VAL,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &afe4404_volatile_table,
};

static const struct of_device_id afe4404_of_match[] = {
	{ .compatible = "ti,afe4404", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, afe4404_of_match);

static int __maybe_unused afe4404_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct afe4404_data *afe = iio_priv(indio_dev);
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

static int __maybe_unused afe4404_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct afe4404_data *afe = iio_priv(indio_dev);
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

static SIMPLE_DEV_PM_OPS(afe4404_pm_ops, afe4404_suspend, afe4404_resume);

static int afe4404_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct afe4404_data *afe;
	int i, ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*afe));
	if (!indio_dev)
		return -ENOMEM;

	afe = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	afe->dev = &client->dev;
	afe->irq = client->irq;

	afe->regmap = devm_regmap_init_i2c(client, &afe4404_regmap_config);
	if (IS_ERR(afe->regmap)) {
		dev_err(afe->dev, "Unable to allocate register map\n");
		return PTR_ERR(afe->regmap);
	}

	for (i = 0; i < F_MAX_FIELDS; i++) {
		afe->fields[i] = devm_regmap_field_alloc(afe->dev, afe->regmap,
							 afe4404_reg_fields[i]);
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
		goto disable_reg;
	}

	ret = regmap_multi_reg_write(afe->regmap, afe4404_reg_sequences,
				     ARRAY_SIZE(afe4404_reg_sequences));
	if (ret) {
		dev_err(afe->dev, "Unable to set register defaults\n");
		goto disable_reg;
	}

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = afe->dev;
	indio_dev->channels = afe4404_channels;
	indio_dev->num_channels = ARRAY_SIZE(afe4404_channels);
	indio_dev->name = AFE4404_DRIVER_NAME;
	indio_dev->info = &afe4404_iio_info;

	if (afe->irq > 0) {
		afe->trig = devm_iio_trigger_alloc(afe->dev,
						   "%s-dev%d",
						   indio_dev->name,
						   indio_dev->id);
		if (!afe->trig) {
			dev_err(afe->dev, "Unable to allocate IIO trigger\n");
			ret = -ENOMEM;
			goto disable_reg;
		}

		iio_trigger_set_drvdata(afe->trig, indio_dev);

		afe->trig->ops = &afe4404_trigger_ops;
		afe->trig->dev.parent = afe->dev;

		ret = iio_trigger_register(afe->trig);
		if (ret) {
			dev_err(afe->dev, "Unable to register IIO trigger\n");
			goto disable_reg;
		}

		ret = devm_request_threaded_irq(afe->dev, afe->irq,
						iio_trigger_generic_data_rdy_poll,
						NULL, IRQF_ONESHOT,
						AFE4404_DRIVER_NAME,
						afe->trig);
		if (ret) {
			dev_err(afe->dev, "Unable to request IRQ\n");
			goto disable_reg;
		}
	}

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
					 afe4404_trigger_handler, NULL);
	if (ret) {
		dev_err(afe->dev, "Unable to setup buffer\n");
		goto unregister_trigger;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(afe->dev, "Unable to register IIO device\n");
		goto unregister_triggered_buffer;
	}

	return 0;

unregister_triggered_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
unregister_trigger:
	if (afe->irq > 0)
		iio_trigger_unregister(afe->trig);
disable_reg:
	regulator_disable(afe->regulator);

	return ret;
}

static int afe4404_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct afe4404_data *afe = iio_priv(indio_dev);
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

static const struct i2c_device_id afe4404_ids[] = {
	{ "afe4404", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, afe4404_ids);

static struct i2c_driver afe4404_i2c_driver = {
	.driver = {
		.name = AFE4404_DRIVER_NAME,
		.of_match_table = afe4404_of_match,
		.pm = &afe4404_pm_ops,
	},
	.probe = afe4404_probe,
	.remove = afe4404_remove,
	.id_table = afe4404_ids,
};
module_i2c_driver(afe4404_i2c_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TI AFE4404 Heart Rate Monitor and Pulse Oximeter AFE");
MODULE_LICENSE("GPL v2");
