/*
 * AFE4403 Heart Rate Monitors and Low-Cost Pulse Oximeters
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
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

#include "afe440x.h"

#define AFE4403_DRIVER_NAME		"afe4403"

/* AFE4403 Registers */
#define AFE4403_TIAGAIN			0x20
#define AFE4403_TIA_AMB_GAIN		0x21

/* AFE4403 GAIN register fields */
#define AFE4403_TIAGAIN_RES_MASK	GENMASK(2, 0)
#define AFE4403_TIAGAIN_RES_SHIFT	0
#define AFE4403_TIAGAIN_CAP_MASK	GENMASK(7, 3)
#define AFE4403_TIAGAIN_CAP_SHIFT	3

/* AFE4403 LEDCNTRL register fields */
#define AFE440X_LEDCNTRL_LED1_MASK		GENMASK(15, 8)
#define AFE440X_LEDCNTRL_LED1_SHIFT		8
#define AFE440X_LEDCNTRL_LED2_MASK		GENMASK(7, 0)
#define AFE440X_LEDCNTRL_LED2_SHIFT		0
#define AFE440X_LEDCNTRL_LED_RANGE_MASK		GENMASK(17, 16)
#define AFE440X_LEDCNTRL_LED_RANGE_SHIFT	16

/* AFE4403 CONTROL2 register fields */
#define AFE440X_CONTROL2_PWR_DWN_TX	BIT(2)
#define AFE440X_CONTROL2_EN_SLOW_DIAG	BIT(8)
#define AFE440X_CONTROL2_DIAG_OUT_TRI	BIT(10)
#define AFE440X_CONTROL2_TX_BRDG_MOD	BIT(11)
#define AFE440X_CONTROL2_TX_REF_MASK	GENMASK(18, 17)
#define AFE440X_CONTROL2_TX_REF_SHIFT	17

/* AFE4404 NULL fields */
#define NULL_MASK	0
#define NULL_SHIFT	0

/* AFE4403 LEDCNTRL values */
#define AFE440X_LEDCNTRL_RANGE_TX_HALF	0x1
#define AFE440X_LEDCNTRL_RANGE_TX_FULL	0x2
#define AFE440X_LEDCNTRL_RANGE_TX_OFF	0x3

/* AFE4403 CONTROL2 values */
#define AFE440X_CONTROL2_TX_REF_025	0x0
#define AFE440X_CONTROL2_TX_REF_050	0x1
#define AFE440X_CONTROL2_TX_REF_100	0x2
#define AFE440X_CONTROL2_TX_REF_075	0x3

/* AFE4403 CONTROL3 values */
#define AFE440X_CONTROL3_CLK_DIV_2	0x0
#define AFE440X_CONTROL3_CLK_DIV_4	0x2
#define AFE440X_CONTROL3_CLK_DIV_6	0x3
#define AFE440X_CONTROL3_CLK_DIV_8	0x4
#define AFE440X_CONTROL3_CLK_DIV_12	0x5
#define AFE440X_CONTROL3_CLK_DIV_1	0x7

/* AFE4403 TIAGAIN_CAP values */
#define AFE4403_TIAGAIN_CAP_5_P		0x0
#define AFE4403_TIAGAIN_CAP_10_P	0x1
#define AFE4403_TIAGAIN_CAP_20_P	0x2
#define AFE4403_TIAGAIN_CAP_30_P	0x3
#define AFE4403_TIAGAIN_CAP_55_P	0x8
#define AFE4403_TIAGAIN_CAP_155_P	0x10

/* AFE4403 TIAGAIN_RES values */
#define AFE4403_TIAGAIN_RES_500_K	0x0
#define AFE4403_TIAGAIN_RES_250_K	0x1
#define AFE4403_TIAGAIN_RES_100_K	0x2
#define AFE4403_TIAGAIN_RES_50_K	0x3
#define AFE4403_TIAGAIN_RES_25_K	0x4
#define AFE4403_TIAGAIN_RES_10_K	0x5
#define AFE4403_TIAGAIN_RES_1_M		0x6
#define AFE4403_TIAGAIN_RES_NONE	0x7

/**
 * struct afe4403_data
 * @dev - Device structure
 * @spi - SPI device handle
 * @regmap - Register map of the device
 * @regulator - Pointer to the regulator for the IC
 * @trig - IIO trigger for this device
 * @irq - ADC_RDY line interrupt number
 */
struct afe4403_data {
	struct device *dev;
	struct spi_device *spi;
	struct regmap *regmap;
	struct regulator *regulator;
	struct iio_trigger *trig;
	int irq;
};

enum afe4403_chan_id {
	LED1,
	ALED1,
	LED2,
	ALED2,
	LED1_ALED1,
	LED2_ALED2,
	ILED1,
	ILED2,
};

static const struct afe440x_reg_info afe4403_reg_info[] = {
	[LED1] = AFE440X_REG_INFO(AFE440X_LED1VAL, 0, NULL),
	[ALED1] = AFE440X_REG_INFO(AFE440X_ALED1VAL, 0, NULL),
	[LED2] = AFE440X_REG_INFO(AFE440X_LED2VAL, 0, NULL),
	[ALED2] = AFE440X_REG_INFO(AFE440X_ALED2VAL, 0, NULL),
	[LED1_ALED1] = AFE440X_REG_INFO(AFE440X_LED1_ALED1VAL, 0, NULL),
	[LED2_ALED2] = AFE440X_REG_INFO(AFE440X_LED2_ALED2VAL, 0, NULL),
	[ILED1] = AFE440X_REG_INFO(AFE440X_LEDCNTRL, 0, AFE440X_LEDCNTRL_LED1),
	[ILED2] = AFE440X_REG_INFO(AFE440X_LEDCNTRL, 0, AFE440X_LEDCNTRL_LED2),
};

static const struct iio_chan_spec afe4403_channels[] = {
	/* ADC values */
	AFE440X_INTENSITY_CHAN(LED1, "led1", 0),
	AFE440X_INTENSITY_CHAN(ALED1, "led1_ambient", 0),
	AFE440X_INTENSITY_CHAN(LED2, "led2", 0),
	AFE440X_INTENSITY_CHAN(ALED2, "led2_ambient", 0),
	AFE440X_INTENSITY_CHAN(LED1_ALED1, "led1-led1_ambient", 0),
	AFE440X_INTENSITY_CHAN(LED2_ALED2, "led2-led2_ambient", 0),
	/* LED current */
	AFE440X_CURRENT_CHAN(ILED1, "led1"),
	AFE440X_CURRENT_CHAN(ILED2, "led2"),
};

static const struct afe440x_val_table afe4403_res_table[] = {
	{ 500000 }, { 250000 }, { 100000 }, { 50000 },
	{ 25000 }, { 10000 }, { 1000000 }, { 0 },
};
AFE440X_TABLE_ATTR(tia_resistance_available, afe4403_res_table);

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
AFE440X_TABLE_ATTR(tia_capacitance_available, afe4403_cap_table);

static ssize_t afe440x_show_register(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct afe4403_data *afe = iio_priv(indio_dev);
	struct afe440x_attr *afe440x_attr = to_afe440x_attr(attr);
	unsigned int reg_val, type;
	int vals[2];
	int ret, val_len;

	ret = regmap_read(afe->regmap, afe440x_attr->reg, &reg_val);
	if (ret)
		return ret;

	reg_val &= afe440x_attr->mask;
	reg_val >>= afe440x_attr->shift;

	switch (afe440x_attr->type) {
	case SIMPLE:
		type = IIO_VAL_INT;
		val_len = 1;
		vals[0] = reg_val;
		break;
	case RESISTANCE:
	case CAPACITANCE:
		type = IIO_VAL_INT_PLUS_MICRO;
		val_len = 2;
		if (reg_val < afe440x_attr->table_size) {
			vals[0] = afe440x_attr->val_table[reg_val].integer;
			vals[1] = afe440x_attr->val_table[reg_val].fract;
			break;
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}

	return iio_format_value(buf, type, val_len, vals);
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

	switch (afe440x_attr->type) {
	case SIMPLE:
		val = integer;
		break;
	case RESISTANCE:
	case CAPACITANCE:
		for (val = 0; val < afe440x_attr->table_size; val++)
			if (afe440x_attr->val_table[val].integer == integer &&
			    afe440x_attr->val_table[val].fract == fract)
				break;
		if (val == afe440x_attr->table_size)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(afe->regmap, afe440x_attr->reg,
				 afe440x_attr->mask,
				 (val << afe440x_attr->shift));
	if (ret)
		return ret;

	return count;
}

static AFE440X_ATTR(tia_separate_en, AFE4403_TIAGAIN, AFE440X_TIAGAIN_ENSEPGAIN, SIMPLE, NULL, 0);

static AFE440X_ATTR(tia_resistance1, AFE4403_TIAGAIN, AFE4403_TIAGAIN_RES, RESISTANCE, afe4403_res_table, ARRAY_SIZE(afe4403_res_table));
static AFE440X_ATTR(tia_capacitance1, AFE4403_TIAGAIN, AFE4403_TIAGAIN_CAP, CAPACITANCE, afe4403_cap_table, ARRAY_SIZE(afe4403_cap_table));

static AFE440X_ATTR(tia_resistance2, AFE4403_TIA_AMB_GAIN, AFE4403_TIAGAIN_RES, RESISTANCE, afe4403_res_table, ARRAY_SIZE(afe4403_res_table));
static AFE440X_ATTR(tia_capacitance2, AFE4403_TIA_AMB_GAIN, AFE4403_TIAGAIN_RES, CAPACITANCE, afe4403_cap_table, ARRAY_SIZE(afe4403_cap_table));

static struct attribute *afe440x_attributes[] = {
	&afe440x_attr_tia_separate_en.dev_attr.attr,
	&afe440x_attr_tia_resistance1.dev_attr.attr,
	&afe440x_attr_tia_capacitance1.dev_attr.attr,
	&afe440x_attr_tia_resistance2.dev_attr.attr,
	&afe440x_attr_tia_capacitance2.dev_attr.attr,
	&dev_attr_tia_resistance_available.attr,
	&dev_attr_tia_capacitance_available.attr,
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

	ret = spi_write_then_read(afe->spi, &reg, 1, rx, 3);
	if (ret)
		return ret;

	*val = (rx[0] << 16) |
		(rx[1] << 8) |
		(rx[2]);

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
	const struct afe440x_reg_info reg_info = afe4403_reg_info[chan->address];
	int ret;

	switch (chan->type) {
	case IIO_INTENSITY:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = afe4403_read(afe, reg_info.reg, val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		case IIO_CHAN_INFO_OFFSET:
			ret = regmap_read(afe->regmap, reg_info.offreg,
					  val);
			if (ret)
				return ret;
			*val &= reg_info.mask;
			*val >>= reg_info.shift;
			return IIO_VAL_INT;
		}
		break;
	case IIO_CURRENT:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			ret = regmap_read(afe->regmap, reg_info.reg, val);
			if (ret)
				return ret;
			*val &= reg_info.mask;
			*val >>= reg_info.shift;
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
	const struct afe440x_reg_info reg_info = afe4403_reg_info[chan->address];

	switch (chan->type) {
	case IIO_INTENSITY:
		switch (mask) {
		case IIO_CHAN_INFO_OFFSET:
			return regmap_update_bits(afe->regmap,
				reg_info.offreg,
				reg_info.mask,
				(val << reg_info.shift));
		}
		break;
	case IIO_CURRENT:
		switch (mask) {
		case IIO_CHAN_INFO_RAW:
			return regmap_update_bits(afe->regmap,
				reg_info.reg,
				reg_info.mask,
				(val << reg_info.shift));
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
	.driver_module = THIS_MODULE,
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
					  &afe4403_reg_info[bit].reg, 1,
					  rx, 3);
		if (ret)
			goto err;

		buffer[i++] = (rx[0] << 16) |
				(rx[1] << 8) |
				(rx[2]);
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
	.owner = THIS_MODULE,
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
	{ AFE440X_CONTROL1, AFE440X_CONTROL1_TIMEREN | 0x000007},
	{ AFE4403_TIA_AMB_GAIN, AFE4403_TIAGAIN_RES_1_M },
	{ AFE440X_LEDCNTRL, (0x14 << AFE440X_LEDCNTRL_LED1_SHIFT) |
			    (0x14 << AFE440X_LEDCNTRL_LED2_SHIFT) },
	{ AFE440X_CONTROL2, AFE440X_CONTROL2_TX_REF_050 <<
			    AFE440X_CONTROL2_TX_REF_SHIFT },
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

#ifdef CONFIG_OF
static const struct of_device_id afe4403_of_match[] = {
	{ .compatible = "ti,afe4403", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, afe4403_of_match);
#endif

static int __maybe_unused afe4403_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
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
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
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
	int ret;

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
		.of_match_table = of_match_ptr(afe4403_of_match),
		.pm = &afe4403_pm_ops,
	},
	.probe = afe4403_probe,
	.remove = afe4403_remove,
	.id_table = afe4403_ids,
};
module_spi_driver(afe4403_spi_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TI AFE4403 Heart Rate and Pulse Oximeter");
MODULE_LICENSE("GPL v2");
