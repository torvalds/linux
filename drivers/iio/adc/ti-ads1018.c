// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Texas Instruments ADS1018 ADC driver
 *
 * Copyright (C) 2025 Kurt Borja <kuurtb@gmail.com>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/dev_printk.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/math.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include <asm/byteorder.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define ADS1018_CFG_OS_TRIG		BIT(15)
#define ADS1018_CFG_TS_MODE_EN		BIT(4)
#define ADS1018_CFG_PULL_UP		BIT(3)
#define ADS1018_CFG_NOP			BIT(1)
#define ADS1018_CFG_VALID		(ADS1018_CFG_PULL_UP | ADS1018_CFG_NOP)

#define ADS1018_CFG_MUX_MASK		GENMASK(14, 12)

#define ADS1018_CFG_PGA_MASK		GENMASK(11, 9)
#define ADS1018_PGA_DEFAULT		2

#define ADS1018_CFG_MODE_MASK		BIT(8)
#define ADS1018_MODE_CONTINUOUS		0
#define ADS1018_MODE_ONESHOT		1

#define ADS1018_CFG_DRATE_MASK		GENMASK(7, 5)
#define ADS1018_DRATE_DEFAULT		4

#define ADS1018_NUM_PGA_MODES		6
#define ADS1018_CHANNELS_MAX		10

struct ads1018_chan_data {
	u8 pga_mode;
	u8 data_rate_mode;
};

struct ads1018_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	unsigned long num_channels;

	/* IIO_VAL_INT */
	const u32 *data_rate_mode_to_hz;
	unsigned long num_data_rate_mode_to_hz;

	/*
	 * Let `res` be the chip's resolution and `fsr` (millivolts) be the
	 * full-scale range corresponding to the PGA mode given by the array
	 * index. Then, the gain is calculated using the following formula:
	 *
	 *     gain = |fsr| / 2^(res - 1)
	 *
	 * This value then has to be represented in IIO_VAL_INT_PLUS_NANO
	 * format. For example if:
	 *
	 *     gain = 6144 / 2^(16 - 1) = 0.1875
	 *
	 * ...then the formatted value is:
	 *
	 *     { 0, 187500000 }
	 */
	const u32 pga_mode_to_gain[ADS1018_NUM_PGA_MODES][2];

	/* IIO_VAL_INT_PLUS_MICRO */
	const u32 temp_scale[2];
};

struct ads1018 {
	struct spi_device *spi;
	struct iio_trigger *indio_trig;

	struct gpio_desc *drdy_gpiod;
	int drdy_irq;

	struct ads1018_chan_data chan_data[ADS1018_CHANNELS_MAX];
	const struct ads1018_chip_info *chip_info;

	struct spi_message msg_read;
	struct spi_transfer xfer;
	__be16 tx_buf[2] __aligned(IIO_DMA_MINALIGN);
	__be16 rx_buf[2];
};

#define ADS1018_VOLT_DIFF_CHAN(_index, _chan, _chan2, _realbits) {		\
	.type = IIO_VOLTAGE,							\
	.channel = _chan,							\
	.channel2 = _chan2,							\
	.scan_index = _index,							\
	.scan_type = {								\
		.sign = 's',							\
		.realbits = _realbits,						\
		.storagebits = 16,						\
		.shift = 16 - _realbits,					\
		.endianness = IIO_BE,						\
	},									\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE) |			\
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.indexed = true,							\
	.differential = true,							\
}

#define ADS1018_VOLT_CHAN(_index, _chan, _realbits) {				\
	.type = IIO_VOLTAGE,							\
	.channel = _chan,							\
	.scan_index = _index,							\
	.scan_type = {								\
		.sign = 's',							\
		.realbits = _realbits,						\
		.storagebits = 16,						\
		.shift = 16 - _realbits,					\
		.endianness = IIO_BE,						\
	},									\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE) |			\
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.indexed = true,							\
}

#define ADS1018_TEMP_CHAN(_index, _realbits) {					\
	.type = IIO_TEMP,							\
	.scan_index = _index,							\
	.scan_type = {								\
		.sign = 's',							\
		.realbits = _realbits,						\
		.storagebits = 16,						\
		.shift = 16 - _realbits,					\
		.endianness = IIO_BE,						\
	},									\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |				\
			      BIT(IIO_CHAN_INFO_SCALE) |			\
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.info_mask_shared_by_all_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

static const struct iio_chan_spec ads1118_iio_channels[] = {
	ADS1018_VOLT_DIFF_CHAN(0, 0, 1, 16),
	ADS1018_VOLT_DIFF_CHAN(1, 0, 3, 16),
	ADS1018_VOLT_DIFF_CHAN(2, 1, 3, 16),
	ADS1018_VOLT_DIFF_CHAN(3, 2, 3, 16),
	ADS1018_VOLT_CHAN(4, 0, 16),
	ADS1018_VOLT_CHAN(5, 1, 16),
	ADS1018_VOLT_CHAN(6, 2, 16),
	ADS1018_VOLT_CHAN(7, 3, 16),
	ADS1018_TEMP_CHAN(8, 14),
	IIO_CHAN_SOFT_TIMESTAMP(9),
};

static const struct iio_chan_spec ads1018_iio_channels[] = {
	ADS1018_VOLT_DIFF_CHAN(0, 0, 1, 12),
	ADS1018_VOLT_DIFF_CHAN(1, 0, 3, 12),
	ADS1018_VOLT_DIFF_CHAN(2, 1, 3, 12),
	ADS1018_VOLT_DIFF_CHAN(3, 2, 3, 12),
	ADS1018_VOLT_CHAN(4, 0, 12),
	ADS1018_VOLT_CHAN(5, 1, 12),
	ADS1018_VOLT_CHAN(6, 2, 12),
	ADS1018_VOLT_CHAN(7, 3, 12),
	ADS1018_TEMP_CHAN(8, 12),
	IIO_CHAN_SOFT_TIMESTAMP(9),
};

/**
 * ads1018_calc_delay - Calculates a suitable delay for a single-shot reading
 * @hz: Sampling frequency
 *
 * Calculates an appropriate delay for a single shot reading given a sampling
 * frequency.
 *
 * Return: Delay in microseconds (Always greater than 0).
 */
static u32 ads1018_calc_delay(unsigned int hz)
{
	/*
	 * Calculate the worst-case sampling rate by subtracting 10% error
	 * specified in the datasheet...
	 */
	hz -= DIV_ROUND_UP(hz, 10);

	/* ...Then calculate time per sample in microseconds. */
	return DIV_ROUND_UP(HZ_PER_MHZ, hz);
}

/**
 * ads1018_spi_read_exclusive - Reads a conversion value from the device
 * @ads1018: Device data
 * @cnv: ADC Conversion value (optional)
 * @hold_cs: Keep CS line asserted after the SPI transfer
 *
 * Reads the most recent ADC conversion value, without updating the
 * device's configuration.
 *
 * Context: Expects SPI bus *exclusive* use.
 *
 * Return: 0 on success, negative errno on error.
 */
static int ads1018_spi_read_exclusive(struct ads1018 *ads1018, __be16 *cnv,
				      bool hold_cs)
{
	int ret;

	ads1018->xfer.cs_change = hold_cs;

	ret = spi_sync_locked(ads1018->spi, &ads1018->msg_read);
	if (ret)
		return ret;

	if (cnv)
		*cnv = ads1018->rx_buf[0];

	return 0;
}

/**
 * ads1018_single_shot - Performs a one-shot reading sequence
 * @ads1018: Device data
 * @chan: Channel specification
 * @cnv: Conversion value
 *
 * Writes a new configuration, waits an appropriate delay, then reads the most
 * recent conversion.
 *
 * Context: Expects iio_device_claim_direct() is held.
 *
 * Return: 0 on success, negative errno on error.
 */
static int ads1018_single_shot(struct ads1018 *ads1018,
			       struct iio_chan_spec const *chan, u16 *cnv)
{
	u8 max_drate_mode = ads1018->chip_info->num_data_rate_mode_to_hz - 1;
	u8 drate = ads1018->chip_info->data_rate_mode_to_hz[max_drate_mode];
	u8 pga_mode = ads1018->chan_data[chan->scan_index].pga_mode;
	struct spi_transfer xfer[2] = {
		{
			.tx_buf = ads1018->tx_buf,
			.len = sizeof(ads1018->tx_buf[0]),
			.delay = {
				.value = ads1018_calc_delay(drate),
				.unit = SPI_DELAY_UNIT_USECS,
			},
			.cs_change = 1, /* 16-bit mode requires CS de-assert */
		},
		{
			.rx_buf = ads1018->rx_buf,
			.len = sizeof(ads1018->rx_buf[0]),
		},
	};
	u16 cfg;
	int ret;

	cfg = ADS1018_CFG_VALID | ADS1018_CFG_OS_TRIG;
	cfg |= FIELD_PREP(ADS1018_CFG_MUX_MASK, chan->scan_index);
	cfg |= FIELD_PREP(ADS1018_CFG_PGA_MASK, pga_mode);
	cfg |= FIELD_PREP(ADS1018_CFG_MODE_MASK, ADS1018_MODE_ONESHOT);
	cfg |= FIELD_PREP(ADS1018_CFG_DRATE_MASK, max_drate_mode);

	if (chan->type == IIO_TEMP)
		cfg |= ADS1018_CFG_TS_MODE_EN;

	ads1018->tx_buf[0] = cpu_to_be16(cfg);
	ret = spi_sync_transfer(ads1018->spi, xfer, ARRAY_SIZE(xfer));
	if (ret)
		return ret;

	*cnv = be16_to_cpu(ads1018->rx_buf[0]);

	return 0;
}

static int
ads1018_read_raw_direct_mode(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val, int *val2,
			     long mask)
{
	struct ads1018 *ads1018 = iio_priv(indio_dev);
	const struct ads1018_chip_info *chip_info = ads1018->chip_info;
	u8 addr = chan->scan_index;
	u8 pga_mode, drate_mode;
	u16 cnv;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ads1018_single_shot(ads1018, chan, &cnv);
		if (ret)
			return ret;

		cnv >>= chan->scan_type.shift;
		*val = sign_extend32(cnv, chan->scan_type.realbits - 1);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			pga_mode = ads1018->chan_data[addr].pga_mode;
			*val = chip_info->pga_mode_to_gain[pga_mode][0];
			*val2 = chip_info->pga_mode_to_gain[pga_mode][1];
			return IIO_VAL_INT_PLUS_NANO;

		case IIO_TEMP:
			*val = chip_info->temp_scale[0];
			*val2 = chip_info->temp_scale[1];
			return IIO_VAL_INT_PLUS_MICRO;

		default:
			return -EOPNOTSUPP;
		}

	case IIO_CHAN_INFO_SAMP_FREQ:
		drate_mode = ads1018->chan_data[addr].data_rate_mode;
		*val = chip_info->data_rate_mode_to_hz[drate_mode];
		return IIO_VAL_INT;

	default:
		return -EOPNOTSUPP;
	}
}

static int
ads1018_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
		 int *val, int *val2, long mask)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;
	ret = ads1018_read_raw_direct_mode(indio_dev, chan, val, val2, mask);
	iio_device_release_direct(indio_dev);

	return ret;
}

static int
ads1018_read_avail(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
		   const int **vals, int *type, int *length, long mask)
{
	struct ads1018 *ads1018 = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT_PLUS_NANO;
		*vals = (const int *)ads1018->chip_info->pga_mode_to_gain;
		*length = ADS1018_NUM_PGA_MODES * 2;
		return IIO_AVAIL_LIST;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT;
		*vals = ads1018->chip_info->data_rate_mode_to_hz;
		*length = ads1018->chip_info->num_data_rate_mode_to_hz;
		return IIO_AVAIL_LIST;

	default:
		return -EOPNOTSUPP;
	}
}

static int
ads1018_write_raw_direct_mode(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int val, int val2,
			      long mask)
{
	struct ads1018 *ads1018 = iio_priv(indio_dev);
	const struct ads1018_chip_info *info = ads1018->chip_info;
	unsigned int i;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		for (i = 0; i < ADS1018_NUM_PGA_MODES; i++) {
			if (val == info->pga_mode_to_gain[i][0] &&
			    val2 == info->pga_mode_to_gain[i][1])
				break;
		}
		if (i == ADS1018_NUM_PGA_MODES)
			return -EINVAL;

		ads1018->chan_data[chan->scan_index].pga_mode = i;
		return 0;

	case IIO_CHAN_INFO_SAMP_FREQ:
		for (i = 0; i < info->num_data_rate_mode_to_hz; i++) {
			if (val == info->data_rate_mode_to_hz[i])
				break;
		}
		if (i == info->num_data_rate_mode_to_hz)
			return -EINVAL;

		ads1018->chan_data[chan->scan_index].data_rate_mode = i;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int
ads1018_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
		  int val, int val2, long mask)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;
	ret = ads1018_write_raw_direct_mode(indio_dev, chan, val, val2, mask);
	iio_device_release_direct(indio_dev);

	return ret;
}

static int
ads1018_write_raw_get_fmt(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static const struct iio_info ads1018_iio_info = {
	.read_raw = ads1018_read_raw,
	.read_avail = ads1018_read_avail,
	.write_raw = ads1018_write_raw,
	.write_raw_get_fmt = ads1018_write_raw_get_fmt,
};

static void ads1018_set_trigger_enable(struct ads1018 *ads1018)
{
	spi_bus_lock(ads1018->spi->controller);
	ads1018_spi_read_exclusive(ads1018, NULL, true);
	enable_irq(ads1018->drdy_irq);
}

static void ads1018_set_trigger_disable(struct ads1018 *ads1018)
{
	disable_irq(ads1018->drdy_irq);
	ads1018_spi_read_exclusive(ads1018, NULL, false);
	spi_bus_unlock(ads1018->spi->controller);
}

static int ads1018_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct ads1018 *ads1018 = iio_trigger_get_drvdata(trig);

	/*
	 * We need to lock the SPI bus and tie CS low (hold_cs) to catch
	 * data-ready interrupts, otherwise the MISO line enters a Hi-Z state.
	 */

	if (state)
		ads1018_set_trigger_enable(ads1018);
	else
		ads1018_set_trigger_disable(ads1018);

	return 0;
}

static const struct iio_trigger_ops ads1018_trigger_ops = {
	.set_trigger_state = ads1018_set_trigger_state,
	.validate_device = iio_trigger_validate_own_device,
};

static int ads1018_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ads1018 *ads1018 = iio_priv(indio_dev);
	const struct ads1018_chip_info *chip_info = ads1018->chip_info;
	unsigned int pga, drate, addr;
	u16 cfg;

	addr = find_first_bit(indio_dev->active_scan_mask,
			      iio_get_masklength(indio_dev));
	pga = ads1018->chan_data[addr].pga_mode;
	drate = ads1018->chan_data[addr].data_rate_mode;

	cfg = ADS1018_CFG_VALID;
	cfg |= FIELD_PREP(ADS1018_CFG_MUX_MASK, addr);
	cfg |= FIELD_PREP(ADS1018_CFG_PGA_MASK, pga);
	cfg |= FIELD_PREP(ADS1018_CFG_MODE_MASK, ADS1018_MODE_CONTINUOUS);
	cfg |= FIELD_PREP(ADS1018_CFG_DRATE_MASK, drate);

	if (chip_info->channels[addr].type == IIO_TEMP)
		cfg |= ADS1018_CFG_TS_MODE_EN;

	ads1018->tx_buf[0] = cpu_to_be16(cfg);
	ads1018->tx_buf[1] = 0;

	return spi_write(ads1018->spi, ads1018->tx_buf, sizeof(ads1018->tx_buf));
}

static int ads1018_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ads1018 *ads1018 = iio_priv(indio_dev);
	u16 cfg;

	cfg = ADS1018_CFG_VALID;
	cfg |= FIELD_PREP(ADS1018_CFG_MODE_MASK, ADS1018_MODE_ONESHOT);

	ads1018->tx_buf[0] = cpu_to_be16(cfg);
	ads1018->tx_buf[1] = 0;

	return spi_write(ads1018->spi, ads1018->tx_buf, sizeof(ads1018->tx_buf));
}

static const struct iio_buffer_setup_ops ads1018_buffer_ops = {
	.preenable = ads1018_buffer_preenable,
	.postdisable = ads1018_buffer_postdisable,
	.validate_scan_mask = iio_validate_scan_mask_onehot,
};

static irqreturn_t ads1018_irq_handler(int irq, void *dev_id)
{
	struct ads1018 *ads1018 = dev_id;

	/*
	 * We need to check if the "drdy" pin is actually active or if it's a
	 * pending interrupt triggered by the SPI transfer.
	 */
	if (!gpiod_get_value(ads1018->drdy_gpiod))
		return IRQ_HANDLED;

	iio_trigger_poll(ads1018->indio_trig);

	return IRQ_HANDLED;
}

static irqreturn_t ads1018_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ads1018 *ads1018 = iio_priv(indio_dev);
	struct {
		__be16 conv;
		aligned_s64 ts;
	} scan = {};
	int ret;

	if (iio_trigger_using_own(indio_dev)) {
		disable_irq(ads1018->drdy_irq);
		ret = ads1018_spi_read_exclusive(ads1018, &scan.conv, true);
		enable_irq(ads1018->drdy_irq);
	} else {
		ret = spi_read(ads1018->spi, ads1018->rx_buf, sizeof(ads1018->rx_buf));
		scan.conv = ads1018->rx_buf[0];
	}

	if (!ret)
		iio_push_to_buffers_with_ts(indio_dev, &scan, sizeof(scan),
					    pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int ads1018_trigger_setup(struct iio_dev *indio_dev)
{
	struct ads1018 *ads1018 = iio_priv(indio_dev);
	struct spi_device *spi = ads1018->spi;
	struct device *dev = &spi->dev;
	const char *con_id = "drdy";
	int ret;

	ads1018->drdy_gpiod = devm_gpiod_get_optional(dev, con_id, GPIOD_IN);
	if (IS_ERR(ads1018->drdy_gpiod))
		return dev_err_probe(dev, PTR_ERR(ads1018->drdy_gpiod),
				     "Failed to get %s GPIO.\n", con_id);

	/* First try to get IRQ from SPI core, then from GPIO */
	if (spi->irq > 0)
		ads1018->drdy_irq = spi->irq;
	else if (ads1018->drdy_gpiod)
		ads1018->drdy_irq = gpiod_to_irq(ads1018->drdy_gpiod);
	if (ads1018->drdy_irq < 0)
		return dev_err_probe(dev, ads1018->drdy_irq,
				     "Failed to get IRQ from %s GPIO.\n", con_id);

	/* An IRQ line is only an optional requirement for the IIO trigger */
	if (ads1018->drdy_irq == 0)
		return 0;

	ads1018->indio_trig = devm_iio_trigger_alloc(dev, "%s-dev%d-%s",
						     indio_dev->name,
						     iio_device_id(indio_dev),
						     con_id);
	if (!ads1018->indio_trig)
		return -ENOMEM;

	iio_trigger_set_drvdata(ads1018->indio_trig, ads1018);
	ads1018->indio_trig->ops = &ads1018_trigger_ops;

	ret = devm_iio_trigger_register(dev, ads1018->indio_trig);
	if (ret)
		return ret;

	/*
	 * The "data-ready" IRQ line is shared with the MOSI pin, thus we need
	 * to keep it disabled until we actually request data.
	 */
	return devm_request_irq(dev, ads1018->drdy_irq, ads1018_irq_handler,
				IRQF_NO_AUTOEN, ads1018->chip_info->name, ads1018);
}

static int ads1018_spi_probe(struct spi_device *spi)
{
	const struct ads1018_chip_info *info = spi_get_device_match_data(spi);
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ads1018 *ads1018;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*ads1018));
	if (!indio_dev)
		return -ENOMEM;

	ads1018 = iio_priv(indio_dev);
	ads1018->spi = spi;
	ads1018->chip_info = info;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = info->name;
	indio_dev->info = &ads1018_iio_info;
	indio_dev->channels = info->channels;
	indio_dev->num_channels = info->num_channels;

	for (unsigned int i = 0; i < ADS1018_CHANNELS_MAX; i++) {
		ads1018->chan_data[i].data_rate_mode = ADS1018_DRATE_DEFAULT;
		ads1018->chan_data[i].pga_mode = ADS1018_PGA_DEFAULT;
	}

	ads1018->xfer.rx_buf = ads1018->rx_buf;
	ads1018->xfer.len = sizeof(ads1018->rx_buf);
	spi_message_init_with_transfers(&ads1018->msg_read, &ads1018->xfer, 1);

	ret = ads1018_trigger_setup(indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
					      iio_pollfunc_store_time,
					      ads1018_trigger_handler,
					      &ads1018_buffer_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const unsigned int ads1018_data_rate_table[] = {
	128, 250, 490, 920, 1600, 2400, 3300,
};

static const unsigned int ads1118_data_rate_table[] = {
	8, 16, 32, 64, 128, 250, 475, 860,
};

static const struct ads1018_chip_info ads1018_chip_info = {
	.name = "ads1018",
	.channels = ads1018_iio_channels,
	.num_channels = ARRAY_SIZE(ads1018_iio_channels),
	.data_rate_mode_to_hz = ads1018_data_rate_table,
	.num_data_rate_mode_to_hz = ARRAY_SIZE(ads1018_data_rate_table),
	.pga_mode_to_gain = {
		{ 3, 0 },		/* fsr = 6144 mV */
		{ 2, 0 },		/* fsr = 4096 mV */
		{ 1, 0 },		/* fsr = 2048 mV */
		{ 0, 500000000 },	/* fsr = 1024 mV */
		{ 0, 250000000 },	/* fsr =  512 mV */
		{ 0, 125000000 },	/* fsr =  256 mV */
	},
	.temp_scale = { 125, 0 },
};

static const struct ads1018_chip_info ads1118_chip_info = {
	.name = "ads1118",
	.channels = ads1118_iio_channels,
	.num_channels = ARRAY_SIZE(ads1118_iio_channels),
	.data_rate_mode_to_hz = ads1118_data_rate_table,
	.num_data_rate_mode_to_hz = ARRAY_SIZE(ads1118_data_rate_table),
	.pga_mode_to_gain = {
		{ 0, 187500000 },	/* fsr = 6144 mV */
		{ 0, 125000000 },	/* fsr = 4096 mV */
		{ 0, 62500000 },	/* fsr = 2048 mV */
		{ 0, 31250000 },	/* fsr = 1024 mV */
		{ 0, 15625000 },	/* fsr =  512 mV */
		{ 0, 7812500 },		/* fsr =  256 mV */
	},
	.temp_scale = { 31, 250000 },
};

static const struct of_device_id ads1018_of_match[] = {
	{ .compatible = "ti,ads1018", .data = &ads1018_chip_info },
	{ .compatible = "ti,ads1118", .data = &ads1118_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ads1018_of_match);

static const struct spi_device_id ads1018_spi_match[] = {
	{ "ads1018", (kernel_ulong_t)&ads1018_chip_info },
	{ "ads1118", (kernel_ulong_t)&ads1118_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ads1018_spi_match);

static struct spi_driver ads1018_spi_driver = {
	.driver = {
		.name = "ads1018",
		.of_match_table = ads1018_of_match,
	},
	.probe = ads1018_spi_probe,
	.id_table = ads1018_spi_match,
};
module_spi_driver(ads1018_spi_driver);

MODULE_DESCRIPTION("Texas Instruments ADS1018 ADC Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kurt Borja <kuurtb@gmail.com>");
