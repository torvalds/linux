// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog Devices AD9739a SPI DAC driver
 *
 * Copyright 2015-2024 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#include <linux/iio/backend.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>

#define AD9739A_REG_MODE		0
#define   AD9739A_RESET_MASK		BIT(5)
#define AD9739A_REG_FSC_1		0x06
#define AD9739A_REG_FSC_2		0x07
#define   AD9739A_FSC_MSB		GENMASK(1, 0)
#define AD9739A_REG_DEC_CNT		0x8
#define   AD9739A_NORMAL_MODE		0
#define   AD9739A_MIXED_MODE		2
#define   AD9739A_DAC_DEC		GENMASK(1, 0)
#define AD9739A_REG_LVDS_REC_CNT1	0x10
#define   AD9739A_RCVR_LOOP_EN_MASK	GENMASK(1, 0)
#define AD9739A_REG_LVDS_REC_CNT4	0x13
#define   AD9739A_FINE_DEL_SKW_MASK	GENMASK(3, 0)
#define AD9739A_REG_LVDS_REC_STAT9	0x21
#define   AD9739A_RCVR_TRACK_AND_LOCK	(BIT(3) | BIT(0))
#define AD9739A_REG_CROSS_CNT1		0x22
#define AD9739A_REG_CROSS_CNT2		0x23
#define AD9739A_REG_PHS_DET		0x24
#define AD9739A_REG_MU_DUTY		0x25
#define AD9739A_REG_MU_CNT1		0x26
#define   AD9739A_MU_EN_MASK		BIT(0)
#define   AD9739A_MU_GAIN_MASK		BIT(1)
#define AD9739A_REG_MU_CNT2		0x27
#define AD9739A_REG_MU_CNT3		0x28
#define AD9739A_REG_MU_CNT4		0x29
#define   AD9739A_MU_CNT4_DEFAULT	0xcb
#define AD9739A_REG_MU_STAT1		0x2A
#define   AD9739A_MU_LOCK_MASK		BIT(0)
#define AD9739A_REG_ANA_CNT_1		0x32
#define AD9739A_REG_ID			0x35

#define AD9739A_ID			0x24
#define AD9739A_REG_IS_RESERVED(reg)	\
	((reg) == 0x5 || (reg) == 0x9 || (reg) == 0x0E || (reg) == 0x0D || \
	 (reg) == 0x2B || (reg) == 0x2C || (reg) == 0x34)

#define AD9739A_FSC_MIN		8580
#define AD9739A_FSC_MAX		31700
#define AD9739A_FSC_RANGE	(AD9739A_FSC_MAX - AD9739A_FSC_MIN + 1)

#define AD9739A_MIN_DAC_CLK	(1600 * MEGA)
#define AD9739A_MAX_DAC_CLK	(2500 * MEGA)
#define AD9739A_DAC_CLK_RANGE	(AD9739A_MAX_DAC_CLK - AD9739A_MIN_DAC_CLK + 1)
/* as recommended by the datasheet */
#define AD9739A_LOCK_N_TRIES	3

struct ad9739a_state {
	struct iio_backend *back;
	struct regmap *regmap;
	unsigned long sample_rate;
};

static int ad9739a_oper_mode_get(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan)
{
	struct ad9739a_state *st = iio_priv(indio_dev);
	u32 mode;
	int ret;

	ret = regmap_read(st->regmap, AD9739A_REG_DEC_CNT, &mode);
	if (ret)
		return ret;

	mode = FIELD_GET(AD9739A_DAC_DEC, mode);
	/* sanity check we get valid values from the HW */
	if (mode != AD9739A_NORMAL_MODE && mode != AD9739A_MIXED_MODE)
		return -EIO;
	if (!mode)
		return AD9739A_NORMAL_MODE;

	/*
	 * We get 2 from the device but for IIO modes, that means 1. Hence the
	 * minus 1.
	 */
	return AD9739A_MIXED_MODE - 1;
}

static int ad9739a_oper_mode_set(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan, u32 mode)
{
	struct ad9739a_state *st = iio_priv(indio_dev);

	/*
	 * On the IIO interface we have 0 and 1 for mode. But for mixed_mode, we
	 * need to write 2 in the device. That's what the below check is about.
	 */
	if (mode == AD9739A_MIXED_MODE - 1)
		mode = AD9739A_MIXED_MODE;

	return regmap_update_bits(st->regmap, AD9739A_REG_DEC_CNT,
				  AD9739A_DAC_DEC, mode);
}

static int ad9739a_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct ad9739a_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->sample_rate;
		*val2 = 0;
		return IIO_VAL_INT_64;
	default:
		return -EINVAL;
	}
}

static int ad9739a_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad9739a_state *st = iio_priv(indio_dev);

	return iio_backend_data_source_set(st->back, 0, IIO_BACKEND_EXTERNAL);
}

static int ad9739a_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad9739a_state *st = iio_priv(indio_dev);

	return iio_backend_data_source_set(st->back, 0,
					   IIO_BACKEND_INTERNAL_CONTINUOUS_WAVE);
}

static bool ad9739a_reg_accessible(struct device *dev, unsigned int reg)
{
	if (AD9739A_REG_IS_RESERVED(reg))
		return false;
	if (reg > AD9739A_REG_MU_STAT1 && reg < AD9739A_REG_ANA_CNT_1)
		return false;

	return true;
}

static int ad9739a_reset(struct device *dev, const struct ad9739a_state *st)
{
	struct gpio_desc *gpio;
	int ret;

	gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);
	if (gpio) {
		/* minimum pulse width of 40ns */
		ndelay(40);
		gpiod_set_value_cansleep(gpio, 0);
		return 0;
	}

	/* bring all registers to their default state */
	ret = regmap_set_bits(st->regmap, AD9739A_REG_MODE, AD9739A_RESET_MASK);
	if (ret)
		return ret;

	ndelay(40);

	return regmap_clear_bits(st->regmap, AD9739A_REG_MODE,
				 AD9739A_RESET_MASK);
}

/*
 * Recommended values (as per datasheet) for the dac clk common mode voltage
 * and Mu controller. Look at table 29.
 */
static const struct reg_sequence ad9739a_clk_mu_ctrl[] = {
	/* DAC clk common mode voltage */
	{ AD9739A_REG_CROSS_CNT1, 0x0f },
	{ AD9739A_REG_CROSS_CNT2, 0x0f },
	/* Mu controller configuration */
	{ AD9739A_REG_PHS_DET, 0x30 },
	{ AD9739A_REG_MU_DUTY, 0x80 },
	{ AD9739A_REG_MU_CNT2, 0x44 },
	{ AD9739A_REG_MU_CNT3, 0x6c },
};

static int ad9739a_init(struct device *dev, const struct ad9739a_state *st)
{
	unsigned int i = 0, lock, fsc;
	u32 fsc_raw;
	int ret;

	ret = regmap_multi_reg_write(st->regmap, ad9739a_clk_mu_ctrl,
				     ARRAY_SIZE(ad9739a_clk_mu_ctrl));
	if (ret)
		return ret;

	/*
	 * Try to get the Mu lock. Repeat the below steps AD9739A_LOCK_N_TRIES
	 * (as specified by the datasheet) until we get the lock.
	 */
	do {
		ret = regmap_write(st->regmap, AD9739A_REG_MU_CNT4,
				   AD9739A_MU_CNT4_DEFAULT);
		if (ret)
			return ret;

		/* Enable the Mu controller search and track mode. */
		ret = regmap_write(st->regmap, AD9739A_REG_MU_CNT1,
				   AD9739A_MU_EN_MASK | AD9739A_MU_GAIN_MASK);
		if (ret)
			return ret;

		/* Ensure the DLL loop is locked */
		ret = regmap_read_poll_timeout(st->regmap, AD9739A_REG_MU_STAT1,
					       lock, lock & AD9739A_MU_LOCK_MASK,
					       0, 1000);
		if (ret && ret != -ETIMEDOUT)
			return ret;
	} while (ret && ++i < AD9739A_LOCK_N_TRIES);

	if (i == AD9739A_LOCK_N_TRIES)
		return dev_err_probe(dev, ret, "Mu lock timeout\n");

	/* Receiver tracking and lock. Same deal as the Mu controller */
	i = 0;
	do {
		ret = regmap_update_bits(st->regmap, AD9739A_REG_LVDS_REC_CNT4,
					 AD9739A_FINE_DEL_SKW_MASK,
					 FIELD_PREP(AD9739A_FINE_DEL_SKW_MASK, 2));
		if (ret)
			return ret;

		/* Disable the receiver and the loop. */
		ret = regmap_write(st->regmap, AD9739A_REG_LVDS_REC_CNT1, 0);
		if (ret)
			return ret;

		/*
		 * Re-enable the loop so it falls out of lock and begins the
		 * search/track routine again.
		 */
		ret = regmap_set_bits(st->regmap, AD9739A_REG_LVDS_REC_CNT1,
				      AD9739A_RCVR_LOOP_EN_MASK);
		if (ret)
			return ret;

		/* Ensure the DLL loop is locked */
		ret = regmap_read_poll_timeout(st->regmap,
					       AD9739A_REG_LVDS_REC_STAT9, lock,
					       lock == AD9739A_RCVR_TRACK_AND_LOCK,
					       0, 1000);
		if (ret && ret != -ETIMEDOUT)
			return ret;
	} while (ret && ++i < AD9739A_LOCK_N_TRIES);

	if (i == AD9739A_LOCK_N_TRIES)
		return dev_err_probe(dev, ret, "Receiver lock timeout\n");

	ret = device_property_read_u32(dev, "adi,full-scale-microamp", &fsc);
	if (ret && ret == -EINVAL)
		return 0;
	if (ret)
		return ret;
	if (!in_range(fsc, AD9739A_FSC_MIN, AD9739A_FSC_RANGE))
		return dev_err_probe(dev, -EINVAL,
				     "Invalid full scale current(%u) [%u %u]\n",
				     fsc, AD9739A_FSC_MIN, AD9739A_FSC_MAX);
	/*
	 * IOUTFS is given by
	 *	Ioutfs = 0.0226 * FSC + 8.58
	 * and is given in mA. Hence we'll have to multiply by 10 * MILLI in
	 * order to get rid of the fractional.
	 */
	fsc_raw = DIV_ROUND_CLOSEST(fsc * 10 - 85800, 226);

	ret = regmap_write(st->regmap, AD9739A_REG_FSC_1, fsc_raw & 0xff);
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, AD9739A_REG_FSC_2,
				  AD9739A_FSC_MSB, fsc_raw >> 8);
}

static const char * const ad9739a_modes_avail[] = { "normal", "mixed-mode" };

static const struct iio_enum ad9739a_modes = {
	.items = ad9739a_modes_avail,
	.num_items = ARRAY_SIZE(ad9739a_modes_avail),
	.get = ad9739a_oper_mode_get,
	.set = ad9739a_oper_mode_set,
};

static const struct iio_chan_spec_ext_info ad9739a_ext_info[] = {
	IIO_ENUM_AVAILABLE("operating_mode", IIO_SEPARATE, &ad9739a_modes),
	IIO_ENUM("operating_mode", IIO_SEPARATE, &ad9739a_modes),
	{ }
};

/*
 * The reason for having two different channels is because we have, in reality,
 * two sources of data:
 *   ALTVOLTAGE: It's a Continuous Wave that's internally generated by the
 *               backend device.
 *   VOLTAGE: It's the typical data we can have in a DAC device and the source
 *            of it has nothing to do with the backend. The backend will only
 *            forward it into our data interface to be sent out.
 */
static struct iio_chan_spec ad9739a_channels[] = {
	{
		.type = IIO_ALTVOLTAGE,
		.indexed = 1,
		.output = 1,
		.scan_index = -1,
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.output = 1,
		.ext_info = ad9739a_ext_info,
		.scan_type = {
			.sign = 's',
			.storagebits = 16,
			.realbits = 16,
		},
	}
};

static const struct iio_info ad9739a_info = {
	.read_raw = ad9739a_read_raw,
};

static const struct iio_buffer_setup_ops ad9739a_buffer_setup_ops = {
	.preenable = &ad9739a_buffer_preenable,
	.postdisable = &ad9739a_buffer_postdisable,
};

static const struct regmap_config ad9739a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.readable_reg = ad9739a_reg_accessible,
	.writeable_reg = ad9739a_reg_accessible,
	.max_register = AD9739A_REG_ID,
};

static int ad9739a_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad9739a_state *st;
	unsigned int id;
	struct clk *clk;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Could not get clkin\n");

	st->sample_rate = clk_get_rate(clk);
	if (!in_range(st->sample_rate, AD9739A_MIN_DAC_CLK,
		      AD9739A_DAC_CLK_RANGE))
		return dev_err_probe(dev, -EINVAL,
				     "Invalid dac clk range(%lu) [%lu %lu]\n",
				     st->sample_rate, AD9739A_MIN_DAC_CLK,
				     AD9739A_MAX_DAC_CLK);

	st->regmap = devm_regmap_init_spi(spi, &ad9739a_regmap_config);
	if (IS_ERR(st->regmap))
		return PTR_ERR(st->regmap);

	ret = regmap_read(st->regmap, AD9739A_REG_ID, &id);
	if (ret)
		return ret;

	if (id != AD9739A_ID)
		dev_warn(dev, "Unrecognized CHIP_ID 0x%X", id);

	ret = ad9739a_reset(dev, st);
	if (ret)
		return ret;

	ret = ad9739a_init(dev, st);
	if (ret)
		return ret;

	st->back = devm_iio_backend_get(dev, NULL);
	if (IS_ERR(st->back))
		return PTR_ERR(st->back);

	ret = devm_iio_backend_request_buffer(dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = iio_backend_extend_chan_spec(st->back, &ad9739a_channels[0]);
	if (ret)
		return ret;

	ret = iio_backend_set_sampling_freq(st->back, 0, st->sample_rate);
	if (ret)
		return ret;

	ret = devm_iio_backend_enable(dev, st->back);
	if (ret)
		return ret;

	indio_dev->name = "ad9739a";
	indio_dev->info = &ad9739a_info;
	indio_dev->channels = ad9739a_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad9739a_channels);
	indio_dev->setup_ops = &ad9739a_buffer_setup_ops;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return ret;

	iio_backend_debugfs_add(st->back, indio_dev);

	return 0;
}

static const struct of_device_id ad9739a_of_match[] = {
	{ .compatible = "adi,ad9739a" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad9739a_of_match);

static const struct spi_device_id ad9739a_id[] = {
	{"ad9739a"},
	{ }
};
MODULE_DEVICE_TABLE(spi, ad9739a_id);

static struct spi_driver ad9739a_driver = {
	.driver = {
		.name = "ad9739a",
		.of_match_table = ad9739a_of_match,
	},
	.probe = ad9739a_probe,
	.id_table = ad9739a_id,
};
module_spi_driver(ad9739a_driver);

MODULE_AUTHOR("Dragos Bogdan <dragos.bogdan@analog.com>");
MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD9739 DAC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BACKEND");
