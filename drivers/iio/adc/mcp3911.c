// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Microchip MCP3911, Two-channel Analog Front End
 *
 * Copyright (C) 2018 Marcus Folkesson <marcus.folkesson@gmail.com>
 * Copyright (C) 2018 Kent Gustavsson <kent@minoris.se>
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/trigger.h>

#include <asm/unaligned.h>

#define MCP3911_REG_CHANNEL0		0x00
#define MCP3911_REG_CHANNEL1		0x03
#define MCP3911_REG_MOD			0x06
#define MCP3911_REG_PHASE		0x07
#define MCP3911_REG_GAIN		0x09

#define MCP3911_REG_STATUSCOM		0x0a
#define MCP3911_STATUSCOM_DRHIZ         BIT(12)
#define MCP3911_STATUSCOM_READ		GENMASK(7, 6)
#define MCP3911_STATUSCOM_CH1_24WIDTH	BIT(4)
#define MCP3911_STATUSCOM_CH0_24WIDTH	BIT(3)
#define MCP3911_STATUSCOM_EN_OFFCAL	BIT(2)
#define MCP3911_STATUSCOM_EN_GAINCAL	BIT(1)

#define MCP3911_REG_CONFIG		0x0c
#define MCP3911_CONFIG_CLKEXT		BIT(1)
#define MCP3911_CONFIG_VREFEXT		BIT(2)
#define MCP3911_CONFIG_OSR		GENMASK(13, 11)

#define MCP3911_REG_OFFCAL_CH0		0x0e
#define MCP3911_REG_GAINCAL_CH0		0x11
#define MCP3911_REG_OFFCAL_CH1		0x14
#define MCP3911_REG_GAINCAL_CH1		0x17
#define MCP3911_REG_VREFCAL		0x1a

#define MCP3911_CHANNEL(x)		(MCP3911_REG_CHANNEL0 + x * 3)
#define MCP3911_OFFCAL(x)		(MCP3911_REG_OFFCAL_CH0 + x * 6)

/* Internal voltage reference in mV */
#define MCP3911_INT_VREF_MV		1200

#define MCP3911_REG_READ(reg, id)	((((reg) << 1) | ((id) << 6) | (1 << 0)) & 0xff)
#define MCP3911_REG_WRITE(reg, id)	((((reg) << 1) | ((id) << 6) | (0 << 0)) & 0xff)
#define MCP3911_REG_MASK		GENMASK(4, 1)

#define MCP3911_NUM_CHANNELS		2

static const int mcp3911_osr_table[] = { 32, 64, 128, 256, 512, 1024, 2048, 4096 };

struct mcp3911 {
	struct spi_device *spi;
	struct mutex lock;
	struct regulator *vref;
	struct clk *clki;
	u32 dev_addr;
	struct iio_trigger *trig;
	struct {
		u32 channels[MCP3911_NUM_CHANNELS];
		s64 ts __aligned(8);
	} scan;

	u8 tx_buf __aligned(IIO_DMA_MINALIGN);
	u8 rx_buf[MCP3911_NUM_CHANNELS * 3];
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
	dev_dbg(&adc->spi->dev, "reading 0x%x from register 0x%lx\n", *val,
		FIELD_GET(MCP3911_REG_MASK, reg));
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

static int mcp3911_write_raw_get_fmt(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return IIO_VAL_INT;
	default:
		return IIO_VAL_INT_PLUS_NANO;
	}
}

static int mcp3911_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	switch (info) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*type = IIO_VAL_INT;
		*vals = mcp3911_osr_table;
		*length = ARRAY_SIZE(mcp3911_osr_table);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
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

		*val = sign_extend32(*val, 23);

		ret = IIO_VAL_INT;
		break;

	case IIO_CHAN_INFO_OFFSET:
		ret = mcp3911_read(adc,
				   MCP3911_OFFCAL(channel->channel), val, 3);
		if (ret)
			goto out;

		ret = IIO_VAL_INT;
		break;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = mcp3911_read(adc, MCP3911_REG_CONFIG, val, 2);
		if (ret)
			goto out;

		*val = FIELD_GET(MCP3911_CONFIG_OSR, *val);
		*val = 32 << *val;
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
			*val = MCP3911_INT_VREF_MV;
		}

		/*
		 * For 24bit Conversion
		 * Raw = ((Voltage)/(Vref) * 2^23 * Gain * 1.5
		 * Voltage = Raw * (Vref)/(2^23 * Gain * 1.5)
		 */

		/* val2 = (2^23 * 1.5) */
		*val2 = 12582912;
		ret = IIO_VAL_FRACTIONAL;
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

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		for (int i = 0; i < ARRAY_SIZE(mcp3911_osr_table); i++) {
			if (val == mcp3911_osr_table[i]) {
				val = FIELD_PREP(MCP3911_CONFIG_OSR, i);
				ret = mcp3911_update(adc, MCP3911_REG_CONFIG, MCP3911_CONFIG_OSR,
						val, 2);
				break;
			}
		}
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
		.scan_index = idx,				\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(IIO_CHAN_INFO_OFFSET) |		\
			BIT(IIO_CHAN_INFO_SCALE),		\
		.info_mask_shared_by_type_available =		\
			BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),	\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 24,				\
			.storagebits = 32,			\
			.endianness = IIO_BE,			\
		},						\
}

static const struct iio_chan_spec mcp3911_channels[] = {
	MCP3911_CHAN(0),
	MCP3911_CHAN(1),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static irqreturn_t mcp3911_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mcp3911 *adc = iio_priv(indio_dev);
	struct spi_transfer xfer[] = {
		{
			.tx_buf = &adc->tx_buf,
			.len = 1,
		}, {
			.rx_buf = adc->rx_buf,
			.len = sizeof(adc->rx_buf),
		},
	};
	int scan_index;
	int i = 0;
	int ret;

	mutex_lock(&adc->lock);
	adc->tx_buf = MCP3911_REG_READ(MCP3911_CHANNEL(0), adc->dev_addr);
	ret = spi_sync_transfer(adc->spi, xfer, ARRAY_SIZE(xfer));
	if (ret < 0) {
		dev_warn(&adc->spi->dev,
				"failed to get conversion data\n");
		goto out;
	}

	for_each_set_bit(scan_index, indio_dev->active_scan_mask, indio_dev->masklength) {
		const struct iio_chan_spec *scan_chan = &indio_dev->channels[scan_index];

		adc->scan.channels[i] = get_unaligned_be24(&adc->rx_buf[scan_chan->channel * 3]);
		i++;
	}
	iio_push_to_buffers_with_timestamp(indio_dev, &adc->scan,
					   iio_get_time_ns(indio_dev));
out:
	mutex_unlock(&adc->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_info mcp3911_info = {
	.read_raw = mcp3911_read_raw,
	.write_raw = mcp3911_write_raw,
	.read_avail = mcp3911_read_avail,
	.write_raw_get_fmt = mcp3911_write_raw_get_fmt,
};

static int mcp3911_config(struct mcp3911 *adc)
{
	struct device *dev = &adc->spi->dev;
	u32 regval;
	int ret;

	ret = device_property_read_u32(dev, "microchip,device-addr", &adc->dev_addr);

	/*
	 * Fallback to "device-addr" due to historical mismatch between
	 * dt-bindings and implementation
	 */
	if (ret)
		device_property_read_u32(dev, "device-addr", &adc->dev_addr);
	if (adc->dev_addr > 3) {
		dev_err(&adc->spi->dev,
			"invalid device address (%i). Must be in range 0-3.\n",
			adc->dev_addr);
		return -EINVAL;
	}
	dev_dbg(&adc->spi->dev, "use device address %i\n", adc->dev_addr);

	ret = mcp3911_read(adc, MCP3911_REG_CONFIG, &regval, 2);
	if (ret)
		return ret;

	regval &= ~MCP3911_CONFIG_VREFEXT;
	if (adc->vref) {
		dev_dbg(&adc->spi->dev, "use external voltage reference\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_VREFEXT, 1);
	} else {
		dev_dbg(&adc->spi->dev,
			"use internal voltage reference (1.2V)\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_VREFEXT, 0);
	}

	regval &= ~MCP3911_CONFIG_CLKEXT;
	if (adc->clki) {
		dev_dbg(&adc->spi->dev, "use external clock as clocksource\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_CLKEXT, 1);
	} else {
		dev_dbg(&adc->spi->dev,
			"use crystal oscillator as clocksource\n");
		regval |= FIELD_PREP(MCP3911_CONFIG_CLKEXT, 0);
	}

	ret = mcp3911_write(adc, MCP3911_REG_CONFIG, regval, 2);
	if (ret)
		return ret;

	ret = mcp3911_read(adc, MCP3911_REG_STATUSCOM, &regval, 2);
	if (ret)
		return ret;

	/* Address counter incremented, cycle through register types */
	regval &= ~MCP3911_STATUSCOM_READ;
	regval |= FIELD_PREP(MCP3911_STATUSCOM_READ, 0x02);

	return  mcp3911_write(adc, MCP3911_REG_STATUSCOM, regval, 2);
}

static void mcp3911_cleanup_regulator(void *vref)
{
	regulator_disable(vref);
}

static int mcp3911_set_trigger_state(struct iio_trigger *trig, bool enable)
{
	struct mcp3911 *adc = iio_trigger_get_drvdata(trig);

	if (enable)
		enable_irq(adc->spi->irq);
	else
		disable_irq(adc->spi->irq);

	return 0;
}

static const struct iio_trigger_ops mcp3911_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
	.set_trigger_state = mcp3911_set_trigger_state,
};

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

		ret = devm_add_action_or_reset(&spi->dev,
				mcp3911_cleanup_regulator, adc->vref);
		if (ret)
			return ret;
	}

	adc->clki = devm_clk_get_enabled(&adc->spi->dev, NULL);
	if (IS_ERR(adc->clki)) {
		if (PTR_ERR(adc->clki) == -ENOENT) {
			adc->clki = NULL;
		} else {
			dev_err(&adc->spi->dev,
				"failed to get adc clk (%ld)\n",
				PTR_ERR(adc->clki));
			return PTR_ERR(adc->clki);
		}
	}

	ret = mcp3911_config(adc);
	if (ret)
		return ret;

	if (device_property_read_bool(&adc->spi->dev, "microchip,data-ready-hiz"))
		ret = mcp3911_update(adc, MCP3911_REG_STATUSCOM, MCP3911_STATUSCOM_DRHIZ,
				0, 2);
	else
		ret = mcp3911_update(adc, MCP3911_REG_STATUSCOM, MCP3911_STATUSCOM_DRHIZ,
				MCP3911_STATUSCOM_DRHIZ, 2);
	if (ret)
		return ret;

	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &mcp3911_info;
	spi_set_drvdata(spi, indio_dev);

	indio_dev->channels = mcp3911_channels;
	indio_dev->num_channels = ARRAY_SIZE(mcp3911_channels);

	mutex_init(&adc->lock);

	if (spi->irq > 0) {
		adc->trig = devm_iio_trigger_alloc(&spi->dev, "%s-dev%d",
				indio_dev->name,
				iio_device_id(indio_dev));
		if (!adc->trig)
			return -ENOMEM;

		adc->trig->ops = &mcp3911_trigger_ops;
		iio_trigger_set_drvdata(adc->trig, adc);
		ret = devm_iio_trigger_register(&spi->dev, adc->trig);
		if (ret)
			return ret;

		/*
		 * The device generates interrupts as long as it is powered up.
		 * Some platforms might not allow the option to power it down so
		 * don't enable the interrupt to avoid extra load on the system.
		 */
		ret = devm_request_irq(&spi->dev, spi->irq,
				&iio_trigger_generic_data_rdy_poll, IRQF_NO_AUTOEN | IRQF_ONESHOT,
				indio_dev->name, adc->trig);
		if (ret)
			return ret;
	}

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
			NULL,
			mcp3911_trigger_handler, NULL);
	if (ret)
		return ret;

	return devm_iio_device_register(&adc->spi->dev, indio_dev);
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
	.id_table = mcp3911_id,
};
module_spi_driver(mcp3911_driver);

MODULE_AUTHOR("Marcus Folkesson <marcus.folkesson@gmail.com>");
MODULE_AUTHOR("Kent Gustavsson <kent@minoris.se>");
MODULE_DESCRIPTION("Microchip Technology MCP3911");
MODULE_LICENSE("GPL v2");
