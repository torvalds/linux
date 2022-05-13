// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Microchip MCP3911, Two-channel Analog Front End
 *
 * Copyright (C) 2018 Marcus Folkesson <marcus.folkesson@gmail.com>
 * Copyright (C) 2018 Kent Gustavsson <kent@minoris.se>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#define MCP3911_REG_CHANNEL0		0x00
#define MCP3911_REG_CHANNEL1		0x03
#define MCP3911_REG_MOD			0x06
#define MCP3911_REG_PHASE		0x07
#define MCP3911_REG_GAIN		0x09

#define MCP3911_REG_STATUSCOM		0x0a
#define MCP3911_STATUSCOM_CH1_24WIDTH	BIT(4)
#define MCP3911_STATUSCOM_CH0_24WIDTH	BIT(3)
#define MCP3911_STATUSCOM_EN_OFFCAL	BIT(2)
#define MCP3911_STATUSCOM_EN_GAINCAL	BIT(1)

#define MCP3911_REG_CONFIG		0x0c
#define MCP3911_CONFIG_CLKEXT		BIT(1)
#define MCP3911_CONFIG_VREFEXT		BIT(2)

#define MCP3911_REG_OFFCAL_CH0		0x0e
#define MCP3911_REG_GAINCAL_CH0		0x11
#define MCP3911_REG_OFFCAL_CH1		0x14
#define MCP3911_REG_GAINCAL_CH1		0x17
#define MCP3911_REG_VREFCAL		0x1a

#define MCP3911_CHANNEL(x)		(MCP3911_REG_CHANNEL0 + x * 3)
#define MCP3911_OFFCAL(x)		(MCP3911_REG_OFFCAL_CH0 + x * 6)

/* Internal voltage reference in uV */
#define MCP3911_INT_VREF_UV		1200000

#define MCP3911_REG_READ(reg, id)	((((reg) << 1) | ((id) << 5) | (1 << 0)) & 0xff)
#define MCP3911_REG_WRITE(reg, id)	((((reg) << 1) | ((id) << 5) | (0 << 0)) & 0xff)

#define MCP3911_NUM_CHANNELS		2

struct mcp3911 {
	struct spi_device *spi;
	struct mutex lock;
	struct regulator *vref;
	struct clk *clki;
	u32 dev_addr;
};

static int mcp3911_read(struct mcp3911 *adc, u8 reg, u32 *val, u8 len)
{
	int ret;

	reg = MCP3911_REG_READ(reg, adc->dev_addr);
	ret = spi_write_then_read(adc->spi, &reg, 1, val, len);
	if (ret < 0)
		return ret;

	be32_to_cpus(val);
	*val >>= ((4 - len) * 8);
	dev_dbg(&adc->spi->dev, "reading 0x%x from register 0x%x\n", *val,
		reg >> 1);
	return ret;
}

static int mcp3911_write(struct mcp3911 *adc, u8 reg, u32 val, u8 len)
{
	dev_dbg(&adc->spi->dev, "writing 0x%x to register 0x%x\n", val, reg);

	val <<= (3 - len) * 8;
	cpu_to_be32s(&val);
	val |= MCP3911_REG_WRITE(reg, adc->dev_addr);

	return spi_write(adc->spi, &val, len + 1);
}

static int mcp3911_update(struct mcp3911 *adc, u8 reg, u32 mask,
		u32 val, u8 len)
{
	u32 tmp;
	int ret;

	ret = mcp3911_read(adc, reg, &tmp, len);
	if (ret)
		return ret;

	val &= mask;
	val |= tmp & ~mask;
	return mcp3911_write(adc, reg, val, len);
}

static int mcp3911_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int *val,
			    int *val2, long mask)
{
	struct mcp3911 *adc = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&adc->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp3911_read(adc,
				   MCP3911_CHANNEL(channel->channel), val, 3);
		if (ret)
			goto out;

		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_OFFSET:
		ret = mcp3911_read(adc,
				   MCP3911_OFFCAL(channel->channel), val, 3);
		if (ret)
			goto out;

		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_SCALE:
		if (adc->vref) {
			ret = regulator_get_voltage(adc->vref);
			if (ret < 0) {
				dev_err(indio_dev->dev.parent,
					"failed to get vref voltage: %d\n",
				       ret);
				goto out;
			}

			*val = ret / 1000;
		} else {
			*val = MCP3911_INT_VREF_UV;
		}

		*val2 = 24;
		ret = IIO_VAL_FRACTIONAL_LOG2;
		break;
	}

out:
	mutex_unlock(&adc->lock);
	return ret;
}

static int mcp3911_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *channel, int val,
			    int val2, long mask)
{
	struct mcp3911 *adc = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&adc->lock);
	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		if (val2 != 0) {
			ret = -EINVAL;
			goto out;
		}

		/* Write offset */
		ret = mcp3911_write(adc, MCP3911_OFFCAL(channel->channel), val,
				    3);
		if (ret)
			goto out;

		/* Enable offset*/
		ret = mcp3911_update(adc, MCP3911_REG_STATUSCOM,
				MCP3911_STATUSCOM_EN_OFFCAL,
				MCP3911_STATUSCOM_EN_OFFCAL, 2);
		break;
	}

out:
	mutex_unlock(&adc->lock);
	return ret;
}

#define MCP3911_CHAN(idx) {					\
		.type = IIO_VOLTAGE,				\
		.indexed = 1,					\
		.channel = idx,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(IIO_CHAN_INFO_OFFSET) |		\
			BIT(IIO_CHAN_INFO_SCALE),		\
}

static const struct iio_chan_spec mcp3911_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
};

static const struct iio_info mcp3911_info = {
	.read_raw = mcp3911_read_raw,
	.write_raw = mcp3911_write_raw,
};

static int mcp3911_config(struct mcp3911 *adc)
{
	struct device *dev = &adc->spi->dev;
	u32 configreg;
	int ret;

	device_property_read_u32(dev, "device-addr", &adc->dev_addr);
	if (adc->dev_addr > 3) {
		dev_err(&adc->spi->dev,
			"invalid device address (%i). Must be in range 0-3.\n",
			adc->dev_addr);
		return -EINVAL;
	}
	dev_dbg(&adc->spi->dev, "use device address %i\n", adc->dev_addr);

	ret = mcp3911_read(adc, MCP3911_REG_CONFIG, &configreg, 2);
	if (ret)
		return ret;

	if (adc->vref) {
		dev_dbg(&adc->spi->dev, "use external voltage reference\n");
		configreg |= MCP3911_CONFIG_VREFEXT;
	} else {
		dev_dbg(&adc->spi->dev,
			"use internal voltage reference (1.2V)\n");
		configreg &= ~MCP3911_CONFIG_VREFEXT;
	}

	if (adc->clki) {
		dev_dbg(&adc->spi->dev, "use external clock as clocksource\n");
		configreg |= MCP3911_CONFIG_CLKEXT;
	} else {
		dev_dbg(&adc->spi->dev,
			"use crystal oscillator as clocksource\n");
		configreg &= ~MCP3911_CONFIG_CLKEXT;
	}

	return  mcp3911_write(adc, MCP3911_REG_CONFIG, configreg, 2);
}

static int mcp3911_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct mcp3911 *adc;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->spi = spi;

	adc->vref = devm_regulator_get_optional(&adc->spi->dev, "vref");
	if (IS_ERR(adc->vref)) {
		if (PTR_ERR(adc->vref) == -ENODEV) {
			adc->vref = NULL;
		} else {
			dev_err(&adc->spi->dev,
				"failed to get regulator (%ld)\n",
				PTR_ERR(adc->vref));
			return PTR_ERR(adc->vref);
		}

	} else {
		ret = regulator_enable(adc->vref);
		if (ret)
			return ret;
	}

	adc->clki = devm_clk_get(&adc->spi->dev, NULL);
	if (IS_ERR(adc->clki)) {
		if (PTR_ERR(adc->clki) == -ENOENT) {
			adc->clki = NULL;
		} else {
			dev_err(&adc->spi->dev,
				"failed to get adc clk (%ld)\n",
				PTR_ERR(adc->clki));
			ret = PTR_ERR(adc->clki);
			goto reg_disable;
		}
	} else {
		ret = clk_prepare_enable(adc->clki);
		if (ret < 0) {
			dev_err(&adc->spi->dev,
				"Failed to enable clki: %d\n", ret);
			goto reg_disable;
		}
	}

	ret = mcp3911_config(adc);
	if (ret)
		goto clk_disable;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp3911_info;
	spi_set_drvdata(spi, indio_dev);

	indio_dev->channels = mcp3911_channels;
	indio_dev->num_channels = ARRAY_SIZE(mcp3911_channels);

	mutex_init(&adc->lock);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto clk_disable;

	return ret;

clk_disable:
	clk_disable_unprepare(adc->clki);
reg_disable:
	if (adc->vref)
		regulator_disable(adc->vref);

	return ret;
}

static void mcp3911_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct mcp3911 *adc = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	clk_disable_unprepare(adc->clki);
	if (adc->vref)
		regulator_disable(adc->vref);
}

static const struct of_device_id mcp3911_dt_ids[] = {
	{ .compatible = "microchip,mcp3911" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp3911_dt_ids);

static const struct spi_device_id mcp3911_id[] = {
	{ "mcp3911", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mcp3911_id);

static struct spi_driver mcp3911_driver = {
	.driver = {
		.name = "mcp3911",
		.of_match_table = mcp3911_dt_ids,
	},
	.probe = mcp3911_probe,
	.remove = mcp3911_remove,
	.id_table = mcp3911_id,
};
module_spi_driver(mcp3911_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_AUTHOR("Kent Gustavsson <kent@minoris.se>");
MODULE_DESCRIPTION("Microchip Technology MCP3911");
MODULE_LICENSE("GPL v2");
