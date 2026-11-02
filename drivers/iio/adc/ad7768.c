// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices AD7768 ADC driver
 *
 * Copyright 2018-2026 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/units.h>

#include <linux/iio/backend.h>
#include <linux/iio/iio.h>

#define AD7768_REG_CH_STANDBY				0x00

#define AD7768_REG_CH_MODE(x)				(0x01 + (x))
#define   AD7768_CH_MODE_FILTER_TYPE_MSK		BIT(3)
#define   AD7768_CH_MODE_DEC_RATE_MSK			GENMASK(2, 0)

#define AD7768_REG_CH_MODE_SEL				0x03

#define AD7768_REG_POWER_MODE				0x04
#define   AD7768_SLEEP_MODE_MSK				BIT(7)
#define   AD7768_POWER_MODE_POWER_MODE_MSK		GENMASK(5, 4)
#define     AD7768_POWER_MODE_POWER_MODE_LOW		0x0
#define     AD7768_POWER_MODE_POWER_MODE_MEDIAN		0x2
#define     AD7768_POWER_MODE_POWER_MODE_FAST		0x3
#define   AD7768_POWER_MODE_LVDS_ENABLE			BIT(3)
#define   AD7768_POWER_MODE_MCLK_DIV_MSK		GENMASK(1, 0)

#define AD7768_REG_GENERAL_CONFIG			0x05
#define   AD7768_GEN_CONFIG_VCM_SEL_MSK			GENMASK(1, 0)
#define   AD7768_GEN_CONFIG_VCM_PD			BIT(4)

#define AD7768_REG_DATA_CONTROL				0x06
#define   AD7768_DATA_CONTROL_SPI_RESET_1		0x03
#define   AD7768_DATA_CONTROL_SPI_RESET_2		0x02
#define   AD7768_DATA_CONTROL_SPI_SYNC			BIT(7)

#define AD7768_REG_INTERFACE_CFG			0x07
#define   AD7768_INTERFACE_CFG_CRC_SELECT_MSK		GENMASK(3, 2)
#define     AD7768_INTERFACE_CFG_CRC_SELECT_4		0x1
#define   AD7768_INTERFACE_CFG_DCLK_DIV_MSK		GENMASK(1, 0)
#define     AD7768_INTERFACE_CFG_DCLK_DIV_8		0x0
#define     AD7768_INTERFACE_CFG_DCLK_DIV_4		0x1
#define     AD7768_INTERFACE_CFG_DCLK_DIV_2		0x2
#define     AD7768_INTERFACE_CFG_DCLK_DIV_1		0x3

#define AD7768_REG_REV_ID				0x0A
#define   AD7768_REV_ID_VAL				0x06

#define AD7768_REG_GPIO_CONTROL				0x0E
#define   AD7768_GPIO_UGPIO_ENABLE			BIT(7)
#define   AD7768_GPIO4_OUTPUT_ENABLE			BIT(4)

#define AD7768_REG_GPIO_WRITE				0x0F

#define AD7768_REG_PRECHARGE_BUF1			0x11
#define AD7768_REG_PRECHARGE_BUF2			0x12
#define   AD7768_PREBUF_POS_EN(ch)			BIT((ch) * 2 + 0)
#define   AD7768_PREBUF_NEG_EN(ch)			BIT((ch) * 2 + 1)

#define AD7768_REG_REFP_BUF				0x13
#define AD7768_REG_REFN_BUF				0x14

#define AD7768_REG_OFFSET_BASE				0x1E
#define AD7768_REG_GAIN_BASE				0x36
#define AD7768_REG_PHASE_BASE				0x4E
#define AD7768_REG_OFFSET(ch)	(AD7768_REG_OFFSET_BASE + 3 * (ch))
#define AD7768_REG_GAIN(ch)	(AD7768_REG_GAIN_BASE + 3 * (ch))
#define AD7768_REG_PHASE(ch)	(AD7768_REG_PHASE_BASE + (ch))

#define AD7768_REG_DIAGNOSTIC_RX			0x56

#define AD7768_REG_CHOP_CTRL				0x59

#define AD7768_SPI_READ_CMD				BIT(15)
#define AD7768_SPI_REG_MASK				GENMASK(14, 8)
#define AD7768_SPI_DATA_MASK				GENMASK(7, 0)

#define AD7768_SAMPLE_SIZE				32
#define AD7768_MAX_DCLK_DIV				8
#define AD7768_MIN_MCLK_FREQ_HZ				(1150 * HZ_PER_KHZ)
#define AD7768_MIN_XTAL_FREQ_HZ				(8 * HZ_PER_MHZ)
#define AD7768_MAX_MCLK_FREQ_HZ				(34 * HZ_PER_MHZ)
#define AD7768_MAX_FREQ_PER_MODE			6
#define AD7768_MAX_CHANNEL				8
#define AD7768_NUM_CHANNEL_MODES			2
#define AD7768_CALIB_SIGN_BIT				23
#define AD7768_CALIB_REG_MSK				GENMASK(AD7768_CALIB_SIGN_BIT, 0)
#define AD7768_WIDEBAND_SETTLING_SAMPLES		68
#define AD7768_SINC5_SETTLING_SAMPLES			7

enum ad7768_filter_type {
	AD7768_FILTER_TYPE_WIDEBAND,
	AD7768_FILTER_TYPE_SINC5,
};

enum ad7768_clock_source {
	AD7768_CLOCK_SOURCE_MCLK,
	AD7768_CLOCK_SOURCE_XTAL,
	AD7768_CLOCK_SOURCE_LVDS,
};

struct ad7768_power_mode_info {
	unsigned int mode;
	unsigned int mclk_div;
};

static const struct ad7768_power_mode_info ad7768_power_modes[] = {
	{ .mode = AD7768_POWER_MODE_POWER_MODE_LOW, .mclk_div = 32 },
	{ .mode = AD7768_POWER_MODE_POWER_MODE_MEDIAN, .mclk_div = 8 },
	{ .mode = AD7768_POWER_MODE_POWER_MODE_FAST, .mclk_div = 4 },
};

#define AD7768_MAX_FREQS \
	(AD7768_MAX_FREQ_PER_MODE + ARRAY_SIZE(ad7768_power_modes))

struct ad7768_precharge_config {
	bool prebufp_en;
	bool prebufn_en;
	bool refbufp;
	bool refbufn;
};

struct ad7768_freq_config {
	unsigned int freq_hz;
	unsigned int dec_rate;
};

struct ad7768_convdelay_params {
	unsigned int shift;
	unsigned int max_raw;
	u64 step_ps;
};

struct ad7768_avail_freq {
	unsigned int n_freqs;
	struct ad7768_freq_config freq_cfg[AD7768_MAX_FREQ_PER_MODE];
};

struct ad7768_chip_info {
	const char *name;
	const struct regmap_config *regmap_config;
	const unsigned int *available_datalines;
	unsigned int num_datalines;
	unsigned int num_channels;
	const u8 *chan_map;
	u8 prebuf_split;
};

struct ad7768_state {
	struct regmap *regmap;
	/* Protects device register access and configuration. */
	struct mutex lock;
	struct clk *mclk;
	unsigned int datalines;
	enum ad7768_clock_source clock_source;
	const struct ad7768_chip_info *chip_info;
	struct ad7768_avail_freq avail_freq[ARRAY_SIZE(ad7768_power_modes)];
	unsigned int n_freqs;
	int freqs[AD7768_MAX_FREQS];
	u64 ch_convdelay_ps[AD7768_MAX_CHANNEL];
	unsigned int ch_freq[AD7768_MAX_CHANNEL];
	unsigned int power_mode_idx;
	enum ad7768_filter_type ch_filter[AD7768_MAX_CHANNEL];
	struct iio_backend *back;
	struct regulator_dev *vcm_rdev;
	unsigned int avdd1_uV;
	unsigned int vref_uV[2];

	/* Used only in regmap_read() callback, hence guarded by regmap lock. */
	__be16 d16 __aligned(IIO_DMA_MINALIGN);
};

static const unsigned int ad7768_vcm_voltage_table[] = {
	0, 1650000, 2500000, 2140000,
};

static int ad7768_vcm_list_voltage(struct regulator_dev *rdev,
				   unsigned int selector)
{
	struct ad7768_state *st = rdev_get_drvdata(rdev);

	if (selector >= ARRAY_SIZE(ad7768_vcm_voltage_table))
		return -EINVAL;

	if (!selector)
		return DIV_ROUND_CLOSEST(st->avdd1_uV, 2);

	return ad7768_vcm_voltage_table[selector];
}

static int ad7768_vcm_enable(struct regulator_dev *rdev)
{
	struct ad7768_state *st = rdev_get_drvdata(rdev);
	int ret;

	ret = pm_runtime_resume_and_get(regmap_get_device(st->regmap));
	if (ret < 0)
		return ret;

	ret = regmap_clear_bits(st->regmap, AD7768_REG_GENERAL_CONFIG,
				AD7768_GEN_CONFIG_VCM_PD);
	if (ret)
		pm_runtime_put_autosuspend(regmap_get_device(st->regmap));

	return ret;
}

static int ad7768_vcm_disable(struct regulator_dev *rdev)
{
	struct ad7768_state *st = rdev_get_drvdata(rdev);
	int ret;

	ret = regmap_set_bits(st->regmap, AD7768_REG_GENERAL_CONFIG,
			      AD7768_GEN_CONFIG_VCM_PD);
	if (ret)
		return ret;

	pm_runtime_put_autosuspend(regmap_get_device(st->regmap));

	return 0;
}

static int ad7768_vcm_is_enabled(struct regulator_dev *rdev)
{
	struct ad7768_state *st = rdev_get_drvdata(rdev);
	int ret;

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(st->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	ret = regmap_test_bits(st->regmap, AD7768_REG_GENERAL_CONFIG,
			       AD7768_GEN_CONFIG_VCM_PD);
	if (ret < 0)
		return ret;

	return !ret;
}

static int ad7768_vcm_set_voltage_sel(struct regulator_dev *rdev,
				      unsigned int selector)
{
	struct ad7768_state *st = rdev_get_drvdata(rdev);
	int ret;

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(st->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, AD7768_REG_GENERAL_CONFIG,
				  AD7768_GEN_CONFIG_VCM_SEL_MSK,
				  FIELD_PREP(AD7768_GEN_CONFIG_VCM_SEL_MSK,
					     selector));
}

static int ad7768_vcm_get_voltage_sel(struct regulator_dev *rdev)
{
	struct ad7768_state *st = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(st->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD7768_REG_GENERAL_CONFIG, &val);
	if (ret)
		return ret;

	return FIELD_GET(AD7768_GEN_CONFIG_VCM_SEL_MSK, val);
}

static const struct regulator_ops ad7768_vcm_ops = {
	.enable = ad7768_vcm_enable,
	.disable = ad7768_vcm_disable,
	.is_enabled = ad7768_vcm_is_enabled,
	.list_voltage = ad7768_vcm_list_voltage,
	.set_voltage_sel = ad7768_vcm_set_voltage_sel,
	.get_voltage_sel = ad7768_vcm_get_voltage_sel,
};

static const struct regulator_desc ad7768_vcm_desc = {
	.name = "vcm",
	.of_match = "vcm-output",
	.regulators_node = "regulators",
	.n_voltages = ARRAY_SIZE(ad7768_vcm_voltage_table),
	.ops = &ad7768_vcm_ops,
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
};

static int ad7768_register_vcm_regulator(struct device *dev,
					 struct ad7768_state *st)
{
	struct regulator_config config = {
		.dev = dev,
		.driver_data = st,
	};
	int ret;

	/*
	 * Start with VCM disabled so each enabled state is paired with the
	 * runtime PM reference acquired by ad7768_vcm_enable().
	 */
	ret = regmap_set_bits(st->regmap, AD7768_REG_GENERAL_CONFIG,
			      AD7768_GEN_CONFIG_VCM_PD);
	if (ret)
		return ret;

	st->vcm_rdev = devm_regulator_register(dev, &ad7768_vcm_desc, &config);
	return PTR_ERR_OR_ZERO(st->vcm_rdev);
}

static const unsigned int ad7768_dec_rate[AD7768_MAX_FREQ_PER_MODE] = {
	32, 64, 128, 256, 512, 1024,
};

static const unsigned int ad7768_available_datalines[] = {
	1, 2, 8,
};

static const unsigned int ad7768_4_available_datalines[] = {
	1, 4,
};

static const u8 ad7768_chan_map[] = {
	0, 1, 2, 3, 4, 5, 6, 7,
};

static const u8 ad7768_4_chan_map[] = {
	0, 1, 4, 5,
};

static const char * const ad7768_supply_names[] = {
	"avdd2", "iovdd",
};

static const char * const ad7768_vref_supply_names[][2] = {
	{ "ref1p", "ref1n" },
	{ "ref2p", "ref2n" },
};

static const char * const ad7768_clock_names[] = {
	[AD7768_CLOCK_SOURCE_MCLK] = "mclk",
	[AD7768_CLOCK_SOURCE_XTAL] = "xtal",
	[AD7768_CLOCK_SOURCE_LVDS] = "lvds",
};

static u8 ad7768_channel_mask(const struct ad7768_state *st, u8 ch)
{
	return BIT(st->chip_info->chan_map[ch]);
}

static u8 ad7768_channel_mode_mask(const struct ad7768_state *st, u8 ch)
{
	/*
	 * AD7768-4 channels 2 and 3 use bits 4 and 5 for CH_x_MODE, while
	 * their CH_x_MDE_EN fields are in bits 2 and 3 and must be programmed
	 * to the same value. On AD7768, chan_map[] is an identity map, so the
	 * OR produces a single bit.
	 */
	return BIT(ch) | ad7768_channel_mask(st, ch);
}

static u8 ad7768_all_standby_mask(const struct ad7768_state *st)
{
	return GENMASK(st->chip_info->num_channels - 1, 0);
}

static void ad7768_keep_xtal_channel_active(const struct ad7768_state *st,
					    unsigned long *standby_mask)
{
	/*
	 * Crystal excitation requires channel 4 on AD7768 or channel 2 on
	 * AD7768-4 to remain active.
	 */
	if (st->clock_source == AD7768_CLOCK_SOURCE_XTAL)
		__clear_bit(st->chip_info->num_channels / 2, standby_mask);
}

static unsigned int ad7768_offset_reg(const struct ad7768_state *st,
				      unsigned int ch)
{
	return AD7768_REG_OFFSET(st->chip_info->chan_map[ch]);
}

static unsigned int ad7768_gain_reg(const struct ad7768_state *st,
				    unsigned int ch)
{
	return AD7768_REG_GAIN(st->chip_info->chan_map[ch]);
}

static unsigned int ad7768_phase_reg(const struct ad7768_state *st,
				     unsigned int ch)
{
	return AD7768_REG_PHASE(st->chip_info->chan_map[ch]);
}

static u8 ad7768_precharge_buf1_mask(const struct ad7768_state *st, u16 val)
{
	return val & GENMASK(st->chip_info->prebuf_split - 1, 0);
}

static u8 ad7768_precharge_buf2_mask(const struct ad7768_state *st, u16 val)
{
	unsigned int split = st->chip_info->prebuf_split;

	return field_get(GENMASK(2 * split - 1, split), val);
}

static int ad7768_regmap_read(void *context, const void *reg_buf,
			      size_t reg_size, void *val_buf, size_t val_size)
{
	struct ad7768_state *st = spi_get_drvdata(context);
	struct spi_device *spi = context;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->d16,
			.len = sizeof(st->d16),
			.cs_change = 1,
		}, {
			/*
			 * Register responses are delayed by one CS frame. While
			 * receiving the response to this read, the device also
			 * decodes another command on SDI. Repeat the read
			 * command to avoid sending an unspecified dummy
			 * command.
			 */
			.tx_buf = &st->d16,
			.rx_buf = &st->d16,
			.len = sizeof(st->d16),
		},
	};
	u8 *data_val = val_buf;
	unsigned int reg;
	int ret;

	reg = *(const u8 *)reg_buf;

	st->d16 = be16_replace_bits(cpu_to_be16(AD7768_SPI_READ_CMD), reg,
				    AD7768_SPI_REG_MASK);

	ret = spi_sync_transfer(spi, t, ARRAY_SIZE(t));
	if (ret)
		return ret;

	*data_val = be16_get_bits(st->d16, AD7768_SPI_DATA_MASK);

	return 0;
}

static int ad7768_regmap_write(void *spi, const void *data, size_t count)
{
	return spi_write(spi, data, count);
}

static const struct regmap_bus ad7768_regmap_bus = {
	.read = ad7768_regmap_read,
	.write = ad7768_regmap_write,
	.reg_format_endian_default = REGMAP_ENDIAN_BIG,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static bool ad7768_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AD7768_REG_CH_STANDBY ... AD7768_REG_REV_ID:
	case AD7768_REG_GPIO_CONTROL ... AD7768_REG_REFN_BUF:
	case AD7768_REG_OFFSET(0) ... AD7768_REG_OFFSET(7) + 2:
	case AD7768_REG_GAIN(0) ... AD7768_REG_GAIN(7) + 2:
	case AD7768_REG_PHASE(0) ... AD7768_REG_PHASE(7):
	case AD7768_REG_DIAGNOSTIC_RX ... AD7768_REG_CHOP_CTRL:
		return true;
	default:
		return false;
	}
}

static bool ad7768_4_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AD7768_REG_CH_STANDBY ... AD7768_REG_REV_ID:
	case AD7768_REG_GPIO_CONTROL ... AD7768_REG_REFN_BUF:
	case AD7768_REG_OFFSET(0) ... AD7768_REG_OFFSET(1) + 2:
	case AD7768_REG_OFFSET(4) ... AD7768_REG_OFFSET(5) + 2:
	case AD7768_REG_GAIN(0) ... AD7768_REG_GAIN(1) + 2:
	case AD7768_REG_GAIN(4) ... AD7768_REG_GAIN(5) + 2:
	case AD7768_REG_PHASE(0) ... AD7768_REG_PHASE(1):
	case AD7768_REG_PHASE(4) ... AD7768_REG_PHASE(5):
	case AD7768_REG_DIAGNOSTIC_RX ... AD7768_REG_CHOP_CTRL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ad7768_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AD7768_REG_CHOP_CTRL,
	/*
	 * Regmap bulk transfers use one starting address, while each register
	 * access requires a separate 16-bit SPI frame and reads use an off-frame
	 * response. Split bulk transfers into individual register accesses.
	 */
	.use_single_read = true,
	.use_single_write = true,
	.readable_reg = ad7768_readable_reg,
};

static const struct regmap_config ad7768_4_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AD7768_REG_CHOP_CTRL,
	/*
	 * Regmap bulk transfers use one starting address, while each register
	 * access requires a separate 16-bit SPI frame and reads use an off-frame
	 * response. Split bulk transfers into individual register accesses. Do
	 * not use a register cache because some readable registers report live
	 * hardware state.
	 */
	.use_single_read = true,
	.use_single_write = true,
	.readable_reg = ad7768_4_readable_reg,
};

static unsigned int ad7768_get_calib_reg_base(struct ad7768_state *st,
					      const struct iio_chan_spec *chan,
					      bool is_gain)
{
	if (is_gain)
		return ad7768_gain_reg(st, chan->channel);

	return ad7768_offset_reg(st, chan->channel);
}

static int ad7768_read_calib_value(struct ad7768_state *st,
				   unsigned int base_reg, bool is_signed,
				   int *val)
{
	unsigned int regval;
	u8 data[3];
	int ret;

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(st->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	ret = regmap_bulk_read(st->regmap, base_reg, data, sizeof(data));
	if (ret)
		return ret;

	regval = get_unaligned_be24(data);
	*val = is_signed ? sign_extend32(regval, AD7768_CALIB_SIGN_BIT) : regval;

	return 0;
}

static int ad7768_write_calib_value(struct ad7768_state *st,
				    unsigned int base_reg, bool is_signed,
				    int val)
{
	u8 data[3];
	int ret;

	if (is_signed &&
	    val != sign_extend32(val, AD7768_CALIB_SIGN_BIT))
		return -EINVAL;

	if (!is_signed && (val < 0 || val > AD7768_CALIB_REG_MSK))
		return -EINVAL;

	put_unaligned_be24(val, data);

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(st->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);

	return regmap_bulk_write(st->regmap, base_reg, data, sizeof(data));
}

static int ad7768_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(st->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int ad7768_sync(struct ad7768_state *st)
{
	int ret;

	ret = regmap_clear_bits(st->regmap, AD7768_REG_DATA_CONTROL,
				AD7768_DATA_CONTROL_SPI_SYNC);
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, AD7768_REG_DATA_CONTROL,
			       AD7768_DATA_CONTROL_SPI_SYNC);
}

static int ad7768_set_power_mode(struct ad7768_state *st,
				 unsigned int mode_idx)
{
	const struct ad7768_power_mode_info *mode_info;
	int ret;

	mode_info = &ad7768_power_modes[mode_idx];
	ret = regmap_update_bits(st->regmap, AD7768_REG_POWER_MODE,
				 AD7768_POWER_MODE_POWER_MODE_MSK |
				 AD7768_POWER_MODE_MCLK_DIV_MSK,
				 FIELD_PREP(AD7768_POWER_MODE_POWER_MODE_MSK,
					    mode_info->mode) |
				 FIELD_PREP(AD7768_POWER_MODE_MCLK_DIV_MSK,
					    mode_info->mode));
	if (ret)
		return ret;

	ret = ad7768_sync(st);
	if (ret)
		return ret;

	st->power_mode_idx = mode_idx;

	return 0;
}

static const struct ad7768_freq_config *
ad7768_find_freq_config(const struct ad7768_state *st,
			unsigned int mode_idx, unsigned int freq)
{
	const struct ad7768_avail_freq *avail_freq = &st->avail_freq[mode_idx];

	for (unsigned int i = 0; i < avail_freq->n_freqs; i++) {
		if (freq == avail_freq->freq_cfg[i].freq_hz)
			return &avail_freq->freq_cfg[i];
	}

	return NULL;
}

static int ad7768_set_clk_divs(struct ad7768_state *st, unsigned int freq)
{
	const struct ad7768_freq_config *freq_cfg;
	unsigned int mclk, dclk, dclk_div;
	unsigned int chan_per_doutx;
	unsigned int dclk_div_reg;

	freq_cfg = ad7768_find_freq_config(st, st->power_mode_idx, freq);
	if (!freq_cfg)
		return -EINVAL;

	mclk = clk_get_rate(st->mclk);
	chan_per_doutx = st->chip_info->num_channels / st->datalines;
	if (!chan_per_doutx)
		return -EINVAL;

	dclk = freq_cfg->freq_hz * AD7768_SAMPLE_SIZE * chan_per_doutx;
	if (dclk > mclk)
		return -EINVAL;

	dclk_div = DIV_ROUND_CLOSEST(mclk, dclk);

	/* Set the divider to the next-lowest supported power of two. */
	dclk_div = rounddown_pow_of_two(min(dclk_div, AD7768_MAX_DCLK_DIV));

	switch (dclk_div) {
	case 1:
		dclk_div_reg = AD7768_INTERFACE_CFG_DCLK_DIV_1;
		break;
	case 2:
		dclk_div_reg = AD7768_INTERFACE_CFG_DCLK_DIV_2;
		break;
	case 4:
		dclk_div_reg = AD7768_INTERFACE_CFG_DCLK_DIV_4;
		break;
	case 8:
		dclk_div_reg = AD7768_INTERFACE_CFG_DCLK_DIV_8;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(st->regmap, AD7768_REG_INTERFACE_CFG,
				  AD7768_INTERFACE_CFG_DCLK_DIV_MSK,
				  FIELD_PREP(AD7768_INTERFACE_CFG_DCLK_DIV_MSK,
					     dclk_div_reg));
}

static int ad7768_set_mode_decimation(struct ad7768_state *st,
				      unsigned int freq, unsigned int mode)
{
	const struct ad7768_freq_config *freq_cfg;

	freq_cfg = ad7768_find_freq_config(st, st->power_mode_idx, freq);
	if (!freq_cfg)
		return -EINVAL;

	return regmap_update_bits(st->regmap, AD7768_REG_CH_MODE(mode),
				  AD7768_CH_MODE_DEC_RATE_MSK,
				  FIELD_PREP(AD7768_CH_MODE_DEC_RATE_MSK,
					     freq_cfg->dec_rate));
}

static int ad7768_set_lowest_noise_mode(struct ad7768_state *st,
					const unsigned long *scan_mask)
{
	unsigned int mode_idx = ARRAY_SIZE(ad7768_power_modes);

	/*
	 * The output data rate ranges overlap between the power modes. At a
	 * common ODR, the faster mode has lower noise, so prefer the fastest
	 * mode that supports every enabled channel.
	 */
	while (mode_idx--) {
		unsigned int channel;

		for_each_set_bit(channel, scan_mask, st->chip_info->num_channels) {
			if (!ad7768_find_freq_config(st, mode_idx,
						     st->ch_freq[channel]))
				break;
		}

		/* All enabled channels support their requested frequency. */
		if (channel == st->chip_info->num_channels)
			return ad7768_set_power_mode(st, mode_idx);
	}

	return -EINVAL;
}

static bool ad7768_freq_supported_in_any_mode(const struct ad7768_state *st,
					      unsigned int freq)
{
	for (unsigned int mode_idx = 0;
	     mode_idx < ARRAY_SIZE(ad7768_power_modes); mode_idx++) {
		if (ad7768_find_freq_config(st, mode_idx, freq))
			return true;
	}

	return false;
}

static int ad7768_set_sampling_freq(struct iio_dev *indio_dev,
				    unsigned int freq, unsigned int channel)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	if (!freq || !ad7768_freq_supported_in_any_mode(st, freq))
		return -EINVAL;

	guard(mutex)(&st->lock);
	st->ch_freq[channel] = freq;

	return 0;
}

static int ad7768_set_filter_mode(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  unsigned int mode)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	guard(mutex)(&st->lock);
	st->ch_filter[chan->channel] = mode;

	return 0;
}

static int ad7768_get_filter_mode(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	return st->ch_filter[chan->channel];
}

static const char *const ad7768_filter_types[] = {
	[AD7768_FILTER_TYPE_WIDEBAND] = "wideband",
	[AD7768_FILTER_TYPE_SINC5] = "sinc5",
};

static const struct iio_enum ad7768_filter_types_enum = {
	.items = ad7768_filter_types,
	.num_items = ARRAY_SIZE(ad7768_filter_types),
	.set = ad7768_set_filter_mode,
	.get = ad7768_get_filter_mode,
};

static struct iio_chan_spec_ext_info ad7768_ext_info[] = {
	IIO_ENUM("filter_type", IIO_SEPARATE, &ad7768_filter_types_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SEPARATE, &ad7768_filter_types_enum),
	{ }
};

static int ad7768_find_matching_mode(const bool *mode_used,
				     const unsigned int *mode_freq,
				     const enum ad7768_filter_type *mode_filter,
				     unsigned int freq,
				     enum ad7768_filter_type filter)
{
	for (unsigned int mode = 0; mode < AD7768_NUM_CHANNEL_MODES; mode++) {
		if (!mode_used[mode] ||
		    (mode_freq[mode] == freq && mode_filter[mode] == filter))
			return mode;
	}

	return -EINVAL;
}

static int ad7768_get_convdelay_params(struct ad7768_state *st, unsigned int ch,
				       struct ad7768_convdelay_params *params)
{
	const struct ad7768_freq_config *freq_cfg;
	unsigned int dec_rate;
	unsigned int mclk_div;
	unsigned long mclk;
	u64 mult;

	freq_cfg = ad7768_find_freq_config(st, st->power_mode_idx,
					   st->ch_freq[ch]);
	if (!freq_cfg)
		return -EINVAL;

	dec_rate = ad7768_dec_rate[freq_cfg->dec_rate];
	switch (dec_rate) {
	case 32:
		params->shift = 3;
		params->max_raw = 31;
		mult = 1;
		break;
	case 64:
		params->shift = 2;
		params->max_raw = 63;
		mult = 1;
		break;
	case 128:
		params->shift = 1;
		params->max_raw = 127;
		mult = 1;
		break;
	case 256:
		params->shift = 0;
		params->max_raw = 255;
		mult = 1;
		break;
	case 512:
		params->shift = 0;
		params->max_raw = 255;
		mult = 2;
		break;
	case 1024:
		params->shift = 0;
		params->max_raw = 255;
		mult = 4;
		break;
	default:
		return -EINVAL;
	}

	mclk = clk_get_rate(st->mclk);
	if (!mclk || mclk > U32_MAX)
		return -EINVAL;

	mclk_div = ad7768_power_modes[st->power_mode_idx].mclk_div;
	params->step_ps =
		DIV_ROUND_CLOSEST_ULL(mult * PSEC_PER_SEC * mclk_div, (u32)mclk);
	if (params->step_ps > U32_MAX)
		return -EINVAL;

	return 0;
}

static int ad7768_set_channel_convdelay(struct ad7768_state *st,
					unsigned int ch)
{
	u64 delay_ps = st->ch_convdelay_ps[ch];
	struct ad7768_convdelay_params params;
	u64 max_delay_ps;
	u64 raw;
	int ret;

	ret = ad7768_get_convdelay_params(st, ch, &params);
	if (ret)
		return ret;

	/*
	 * Validate the requested delay before rounding so an out-of-range value
	 * cannot round down into the valid range. Then verify that the rounded
	 * value fits in the hardware register.
	 */
	max_delay_ps = (u64)params.max_raw * params.step_ps;
	if (delay_ps > max_delay_ps)
		return -EINVAL;

	raw = DIV_ROUND_CLOSEST_ULL(delay_ps, (u32)params.step_ps);
	if (raw > params.max_raw)
		return -EINVAL;

	return regmap_write(st->regmap, ad7768_phase_reg(st, ch),
			    raw << params.shift);
}

static void ad7768_filter_wait(const unsigned int *mode_freq,
			       const enum ad7768_filter_type *mode_filter,
			       const bool *mode_used)
{
	unsigned int t_settle_us = 0;

	for (unsigned int mode = 0; mode < AD7768_NUM_CHANNEL_MODES; mode++) {
		unsigned int t_mode_us;
		u8 settling_samples;

		if (!mode_used[mode] || !mode_freq[mode])
			continue;

		if (mode_filter[mode] == AD7768_FILTER_TYPE_SINC5)
			settling_samples = AD7768_SINC5_SETTLING_SAMPLES;
		else
			settling_samples = AD7768_WIDEBAND_SETTLING_SAMPLES;

		t_mode_us = DIV_ROUND_UP(settling_samples * USEC_PER_SEC,
					 mode_freq[mode]);
		t_settle_us = max(t_settle_us, t_mode_us);
	}

	if (t_settle_us)
		fsleep(t_settle_us);
}

static int ad7768_apply_channel_modes(struct iio_dev *indio_dev,
				      const unsigned long *scan_mask)
{
	enum ad7768_filter_type mode_filter[AD7768_NUM_CHANNEL_MODES] = { };
	unsigned int mode_freq[AD7768_NUM_CHANNEL_MODES] = { };
	bool mode_used[AD7768_NUM_CHANNEL_MODES] = { };
	struct ad7768_state *st = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	unsigned long channel_mask;
	unsigned long standby_mask;
	unsigned int max_freq;
	unsigned int c;
	int ret;

	guard(mutex)(&st->lock);

	ret = ad7768_set_lowest_noise_mode(st, scan_mask);
	if (ret == -EINVAL)
		return dev_err_probe(dev, ret,
				     "No power mode supports all ODRs\n");
	if (ret)
		return ret;

	channel_mask = ad7768_all_standby_mask(st);
	standby_mask = channel_mask & ~*scan_mask;
	ad7768_keep_xtal_channel_active(st, &standby_mask);

	for_each_set_bit(c, scan_mask, st->chip_info->num_channels) {
		unsigned int mask;
		int mode;

		mode = ad7768_find_matching_mode(mode_used, mode_freq,
						 mode_filter, st->ch_freq[c],
						 st->ch_filter[c]);
		if (mode < 0)
			return dev_err_probe(dev, -EINVAL,
					     "Over %d channel modes required\n",
					     AD7768_NUM_CHANNEL_MODES);

		mode_freq[mode] = st->ch_freq[c];
		mode_filter[mode] = st->ch_filter[c];
		mode_used[mode] = true;

		mask = ad7768_channel_mode_mask(st, c);
		ret = regmap_assign_bits(st->regmap, AD7768_REG_CH_MODE_SEL,
					 mask, mode);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(st->regmap, AD7768_REG_CH_STANDBY,
				 channel_mask, standby_mask);
	if (ret)
		return ret;

	max_freq = 0;
	for (unsigned int mode = 0; mode < AD7768_NUM_CHANNEL_MODES; mode++) {
		if (!mode_used[mode])
			continue;

		ret = ad7768_set_mode_decimation(st, mode_freq[mode], mode);
		if (ret)
			return ret;

		ret = regmap_assign_bits(st->regmap, AD7768_REG_CH_MODE(mode),
					 AD7768_CH_MODE_FILTER_TYPE_MSK,
					 mode_filter[mode]);
		if (ret)
			return ret;

		max_freq = max(max_freq, mode_freq[mode]);
	}

	for_each_set_bit(c, scan_mask, st->chip_info->num_channels) {
		ret = ad7768_set_channel_convdelay(st, c);
		if (ret == -EINVAL)
			return dev_err_probe(regmap_get_device(st->regmap), ret,
					     "Invalid conversion delay for channel %u\n",
					     c);
		if (ret)
			return ret;
	}

	ret = ad7768_set_clk_divs(st, max_freq);
	if (ret)
		return ret;

	ret = ad7768_sync(st);
	if (ret)
		return ret;

	/* Apply a filter settling time (datasheet Tables 28 and 29). */
	ad7768_filter_wait(mode_freq, mode_filter, mode_used);

	return 0;
}

static int ad7768_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad7768_apply_channel_modes(indio_dev, scan_mask);
	if (ret)
		return ret;

	for (unsigned int ch = 0; ch < st->chip_info->num_channels; ch++) {
		if (test_bit(ch, scan_mask))
			ret = iio_backend_chan_enable(st->back, ch);
		else
			ret = iio_backend_chan_disable(st->back, ch);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct ad7768_chip_info ad7768_chip_info = {
	.name = "ad7768",
	.num_channels = 8,
	.regmap_config = &ad7768_regmap_config,
	.available_datalines = ad7768_available_datalines,
	.num_datalines = ARRAY_SIZE(ad7768_available_datalines),
	.chan_map = ad7768_chan_map,
	.prebuf_split = 8,
};

static const struct ad7768_chip_info ad7768_4_chip_info = {
	.name = "ad7768-4",
	.num_channels = 4,
	.regmap_config = &ad7768_4_regmap_config,
	.available_datalines = ad7768_4_available_datalines,
	.num_datalines = ARRAY_SIZE(ad7768_4_available_datalines),
	.chan_map = ad7768_4_chan_map,
	.prebuf_split = 4,
};

static int ad7768_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	return pm_runtime_resume_and_get(regmap_get_device(st->regmap));
}

static int ad7768_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	pm_runtime_put_autosuspend(regmap_get_device(st->regmap));

	return 0;
}

static const struct iio_buffer_setup_ops ad7768_buffer_ops = {
	.preenable = ad7768_buffer_preenable,
	.postdisable = ad7768_buffer_postdisable,
};

static int ad7768_read_scale(struct ad7768_state *st,
			     const struct iio_chan_spec *chan,
			     int *val, int *val2)
{
	unsigned int vref_idx;

	vref_idx = chan->channel >= st->chip_info->num_channels / 2;
	*val = 2 * st->vref_uV[vref_idx] / (MICRO / MILLI);
	*val2 = chan->scan_type.realbits;

	return IIO_VAL_FRACTIONAL_LOG2;
}

static int ad7768_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		return ad7768_read_scale(st, chan, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ: {
		guard(mutex)(&st->lock);
		*val = st->ch_freq[chan->channel];

		return IIO_VAL_INT;
	}

	case IIO_CHAN_INFO_CALIBBIAS:
	case IIO_CHAN_INFO_CALIBSCALE: {
		bool is_gain = info == IIO_CHAN_INFO_CALIBSCALE;
		unsigned int base_reg;

		base_reg = ad7768_get_calib_reg_base(st, chan, is_gain);
		ret = ad7768_read_calib_value(st, base_reg, !is_gain, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_CONVDELAY: {
		guard(mutex)(&st->lock);
		iio_val_s64_decompose(st->ch_convdelay_ps[chan->channel],
				      val, val2);

		return IIO_VAL_DECIMAL64_PICO;
	}
	default:
		return -EINVAL;
	}
}

static int ad7768_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
	case IIO_CHAN_INFO_CALIBBIAS:
	case IIO_CHAN_INFO_CALIBSCALE:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_CONVDELAY:
		return IIO_VAL_DECIMAL64_PICO;
	default:
		return -EINVAL;
	}
}

static int ad7768_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	unsigned int base_reg;
	s64 delay_ps;

	IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad7768_set_sampling_freq(indio_dev, val, chan->channel);
	case IIO_CHAN_INFO_CALIBBIAS:
		base_reg = ad7768_get_calib_reg_base(st, chan, false);
		return ad7768_write_calib_value(st, base_reg, true, val);
	case IIO_CHAN_INFO_CALIBSCALE:
		base_reg = ad7768_get_calib_reg_base(st, chan, true);
		return ad7768_write_calib_value(st, base_reg, false, val);
	case IIO_CHAN_INFO_CONVDELAY: {
		delay_ps = iio_val_s64_compose(val, val2);
		if (delay_ps < 0)
			return -EINVAL;

		guard(mutex)(&st->lock);
		st->ch_convdelay_ps[chan->channel] = delay_ps;
		return 0;
	}
	default:
		return -EINVAL;
	}
}

static int ad7768_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	struct ad7768_state *st = iio_priv(indio_dev);

	if (info != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	*vals = st->freqs;
	*type = IIO_VAL_INT;
	*length = st->n_freqs;

	return IIO_AVAIL_LIST;
}

static const struct iio_info ad7768_info = {
	.debugfs_reg_access = ad7768_reg_access,
	.read_raw = ad7768_read_raw,
	.write_raw_get_fmt = ad7768_write_raw_get_fmt,
	.write_raw = ad7768_write_raw,
	.read_avail = ad7768_read_avail,
	.update_scan_mode = ad7768_update_scan_mode,
};

static int ad7768_gpio_adev_init(struct ad7768_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct spi_device *spi = to_spi_device(dev);
	struct auxiliary_device *adev;
	int id;

	if (!device_property_read_bool(dev, "gpio-controller"))
		return 0;

	/*
	 * Use the SPI bus number and chip select to derive a stable per-device
	 * ID.
	 */
	id = (spi->controller->bus_num << 8) | spi_get_chipselect(spi, 0);
	adev = __devm_auxiliary_device_create(dev, KBUILD_MODNAME, "gpio",
					      NULL, id);
	if (!adev)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to create GPIO auxiliary device\n");

	return 0;
}

static int ad7768_configure_precharge_buffers(struct iio_dev *indio_dev,
					      struct ad7768_precharge_config *precharge_cfg)
{
	struct ad7768_state *st = iio_priv(indio_dev);
	u8 prebuf1_val, prebuf2_val;
	u16 prebuf_mask = 0;
	u8 refbufp_val = 0;
	u8 refbufn_val = 0;
	int ret;

	for (unsigned int ch = 0; ch < indio_dev->num_channels; ch++) {
		u8 channel = indio_dev->channels[ch].channel;

		if (precharge_cfg[channel].prebufp_en)
			prebuf_mask |= AD7768_PREBUF_POS_EN(channel);

		if (precharge_cfg[channel].prebufn_en)
			prebuf_mask |= AD7768_PREBUF_NEG_EN(channel);

		if (precharge_cfg[channel].refbufp)
			refbufp_val |= ad7768_channel_mask(st, channel);

		if (precharge_cfg[channel].refbufn)
			refbufn_val |= ad7768_channel_mask(st, channel);
	}

	prebuf1_val = ad7768_precharge_buf1_mask(st, ~prebuf_mask);
	prebuf2_val = ad7768_precharge_buf2_mask(st, ~prebuf_mask);

	ret = regmap_write(st->regmap, AD7768_REG_PRECHARGE_BUF1, prebuf1_val);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD7768_REG_PRECHARGE_BUF2, prebuf2_val);
	if (ret)
		return ret;

	ret = regmap_write(st->regmap, AD7768_REG_REFP_BUF, refbufp_val);
	if (ret)
		return ret;

	return regmap_write(st->regmap, AD7768_REG_REFN_BUF, refbufn_val);
}

static void ad7768_set_available_sampling_freqs(struct ad7768_state *st)
{
	unsigned int n_power_modes = ARRAY_SIZE(ad7768_power_modes);
	unsigned int mclk = clk_get_rate(st->mclk);

	for (unsigned int mode_idx = 0; mode_idx < n_power_modes; mode_idx++) {
		unsigned int dec = ARRAY_SIZE(ad7768_dec_rate);
		struct ad7768_avail_freq *avail_freq;
		unsigned int div;

		avail_freq = &st->avail_freq[mode_idx];
		div = ad7768_power_modes[mode_idx].mclk_div;
		while (dec--) {
			struct ad7768_freq_config *freq_cfg;

			freq_cfg = &avail_freq->freq_cfg[avail_freq->n_freqs++];
			freq_cfg->dec_rate = dec;
			freq_cfg->freq_hz = mclk / (ad7768_dec_rate[dec] * div);
		}
	}

	/* One DOUT line cannot carry the AD7768 fast-mode x32 output rate. */
	if (st->datalines == 1 &&
	    st->chip_info->num_channels == AD7768_MAX_CHANNEL)
		st->avail_freq[n_power_modes - 1].n_freqs--;

	for (unsigned int mode_idx = 0; mode_idx < n_power_modes; mode_idx++) {
		const struct ad7768_avail_freq *avail_freq;

		avail_freq = &st->avail_freq[mode_idx];
		for (unsigned int i = 0; i < avail_freq->n_freqs; i++) {
			unsigned int freq = avail_freq->freq_cfg[i].freq_hz;

			if (st->n_freqs && st->freqs[st->n_freqs - 1] >= freq)
				continue;

			st->freqs[st->n_freqs++] = freq;
		}
	}
}

static int ad7768_init_sampling_freqs(struct ad7768_state *st)
{
	const struct ad7768_avail_freq *avail_freq;
	unsigned int default_freq;
	int ret;

	ret = ad7768_set_power_mode(st, ARRAY_SIZE(ad7768_power_modes) - 1);
	if (ret)
		return ret;

	avail_freq = &st->avail_freq[st->power_mode_idx];
	default_freq = avail_freq->freq_cfg[avail_freq->n_freqs - 1].freq_hz;
	for (unsigned int channel = 0;
	     channel < st->chip_info->num_channels; channel++) {
		st->ch_freq[channel] = default_freq;
		st->ch_filter[channel] = AD7768_FILTER_TYPE_WIDEBAND;
	}

	return 0;
}

static int ad7768_parse_config(struct iio_dev *indio_dev,
			       struct device *dev)
{
	struct ad7768_precharge_config precharge_cfg[AD7768_MAX_CHANNEL] = { };
	struct ad7768_state *st = iio_priv(indio_dev);
	const unsigned int *available_datalines;
	const char *propname;
	bool datalines_valid = false;
	struct iio_chan_spec *chan;
	unsigned int num_channels;
	unsigned long standby_mask;
	unsigned int len;
	int chan_idx = 0;
	int ret;

	propname = "adi,data-lines-number";
	num_channels = device_get_named_child_node_count(dev, "channel");
	if (num_channels == 0)
		return dev_err_probe(dev, -ENOENT, "No channel specified\n");
	if (num_channels > st->chip_info->num_channels)
		return dev_err_probe(dev, -ENOSPC, "Invalid number of channels\n");

	chan = devm_kcalloc(dev, num_channels, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	indio_dev->channels = chan;
	indio_dev->num_channels = num_channels;

	standby_mask = ad7768_all_standby_mask(st);
	ad7768_keep_xtal_channel_active(st, &standby_mask);

	ret = regmap_write(st->regmap, AD7768_REG_CH_STANDBY, standby_mask);
	if (ret)
		return ret;

	device_for_each_named_child_node_scoped(dev, child, "channel") {
		u32 channel;

		ret = fwnode_property_read_u32(child, "reg", &channel);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to parse reg of %pfwP\n",
					     child);

		if (channel >= st->chip_info->num_channels)
			return dev_err_probe(dev, -ECHRNG,
					     "Invalid channel %u in firmware\n",
					     channel);

		ret = regmap_clear_bits(st->regmap, AD7768_REG_CH_STANDBY,
					BIT(channel));
		if (ret)
			return ret;

		precharge_cfg[channel].prebufp_en =
			fwnode_property_read_bool(child, "adi,prechargebuf-pos-enable");
		precharge_cfg[channel].prebufn_en =
			fwnode_property_read_bool(child, "adi,prechargebuf-neg-enable");
		precharge_cfg[channel].refbufp =
			fwnode_property_read_bool(child, "adi,refbuf-pos-enable");
		precharge_cfg[channel].refbufn =
			fwnode_property_read_bool(child, "adi,refbuf-neg-enable");

		chan[chan_idx++] = (struct iio_chan_spec) {
			.type = IIO_VOLTAGE,
			.info_mask_separate = BIT(IIO_CHAN_INFO_SCALE) |
					      BIT(IIO_CHAN_INFO_CALIBBIAS) |
					      BIT(IIO_CHAN_INFO_CALIBSCALE) |
					      BIT(IIO_CHAN_INFO_CONVDELAY) |
					      BIT(IIO_CHAN_INFO_SAMP_FREQ),
			.info_mask_separate_available =
				BIT(IIO_CHAN_INFO_SAMP_FREQ),
			.indexed = 1,
			.channel = channel,
			.scan_index = channel,
			.scan_type = {
				.sign = 's',
				.realbits = 24,
				.storagebits = 32,
			},
			.ext_info = ad7768_ext_info,
		};
	}

	ret = ad7768_configure_precharge_buffers(indio_dev, precharge_cfg);
	if (ret)
		return ret;

	available_datalines = st->chip_info->available_datalines;
	len = st->chip_info->num_datalines;
	if (device_property_present(dev, propname)) {
		ret = device_property_read_u32(dev, propname, &st->datalines);
		if (ret)
			return dev_err_probe(dev, ret, "Invalid %s property\n",
					     propname);
	} else {
		st->datalines = available_datalines[len - 1];
	}

	for (unsigned int i = 0; i < len; i++) {
		if (available_datalines[i] == st->datalines) {
			datalines_valid = true;
			break;
		}
	}

	if (!datalines_valid)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid %s %u for %s\n", propname,
				     st->datalines, st->chip_info->name);

	ad7768_set_available_sampling_freqs(st);

	ret = ad7768_init_sampling_freqs(st);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set power mode\n");

	return 0;
}

static int ad7768_reset(struct ad7768_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	struct reset_control *reset_ctrl;
	unsigned long reset_low_us;
	unsigned long mclk;
	int ret;

	reset_ctrl = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(reset_ctrl))
		return PTR_ERR(reset_ctrl);

	if (reset_ctrl) {
		mclk = clk_get_rate(st->mclk);
		if (!mclk)
			return -EINVAL;

		/*
		 * Minimum RESET low pulse width: 2 x tMCLK
		 * (datasheet Table 1).
		 */
		reset_low_us = DIV_ROUND_UP_ULL(2ULL * USEC_PER_SEC, mclk);

		ret = reset_control_assert(reset_ctrl);
		if (ret)
			return ret;

		fsleep(max(1UL, reset_low_us));

		ret = reset_control_deassert(reset_ctrl);
		if (ret)
			return ret;
	} else {
		ret = regmap_write(st->regmap, AD7768_REG_DATA_CONTROL,
				   AD7768_DATA_CONTROL_SPI_RESET_1);
		if (ret)
			return ret;

		ret = regmap_write(st->regmap, AD7768_REG_DATA_CONTROL,
				   AD7768_DATA_CONTROL_SPI_RESET_2);
		if (ret)
			return ret;
	}

	/* ADC start-up time after reset: 1.66 ms max (datasheet Table 1) */
	fsleep(1660);

	return 0;
}

static void ad7768_disable_clk(void *clk)
{
	clk_disable_unprepare(clk);
}

static int ad7768_configure_xtal_clock(struct ad7768_state *st)
{
	int ret;

	ret = regmap_set_bits(st->regmap, AD7768_REG_GPIO_WRITE, BIT(4));
	if (ret)
		return ret;

	return regmap_set_bits(st->regmap, AD7768_REG_GPIO_CONTROL,
			       AD7768_GPIO_UGPIO_ENABLE |
			       AD7768_GPIO4_OUTPUT_ENABLE);
}

static int ad7768_enable_lvds_clock(struct ad7768_state *st)
{
	struct device *dev = regmap_get_device(st->regmap);
	int ret;

	ret = regmap_clear_bits(st->regmap, AD7768_REG_GPIO_WRITE, BIT(4));
	if (ret)
		return ret;

	ret = regmap_set_bits(st->regmap, AD7768_REG_GPIO_CONTROL,
			      AD7768_GPIO_UGPIO_ENABLE |
			      AD7768_GPIO4_OUTPUT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_set_bits(st->regmap, AD7768_REG_POWER_MODE,
			      AD7768_POWER_MODE_LVDS_ENABLE);
	if (ret)
		return ret;

	ret = clk_prepare_enable(st->mclk);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, ad7768_disable_clk, st->mclk);
}

static int ad7768_get_enable_vref(struct device *dev, unsigned int index)
{
	const char * const *supply = ad7768_vref_supply_names[index];
	int refp_uV;
	int refn_uV;

	refp_uV = devm_regulator_get_enable_read_voltage(dev, supply[0]);
	if (refp_uV < 0)
		return dev_err_probe(dev, refp_uV,
				     "Failed to get %s supply voltage\n", supply[0]);

	refn_uV = devm_regulator_get_enable_read_voltage(dev, supply[1]);
	if (refn_uV == -ENODEV)
		refn_uV = 0;
	else if (refn_uV < 0)
		return dev_err_probe(dev, refn_uV,
				     "Failed to get %s supply voltage\n", supply[1]);

	if (refp_uV <= refn_uV)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid reference %u voltage\n", index + 1);

	return refp_uV - refn_uV;
}

static int ad7768_enter_sleep(struct ad7768_state *st)
{
	return regmap_set_bits(st->regmap, AD7768_REG_POWER_MODE,
			       AD7768_SLEEP_MODE_MSK);
}

static void ad7768_power_off(void *data)
{
	struct ad7768_state *st = data;
	struct device *dev = regmap_get_device(st->regmap);
	int ret;

	ret = ad7768_enter_sleep(st);
	if (ret)
		dev_err(dev, "Failed to put device to sleep\n");
}

static int ad7768_validate_mclk_rate(struct device *dev,
				     const struct ad7768_state *st)
{
	unsigned long min_rate = AD7768_MIN_MCLK_FREQ_HZ;
	unsigned long rate = clk_get_rate(st->mclk);

	if (st->clock_source == AD7768_CLOCK_SOURCE_XTAL)
		min_rate = AD7768_MIN_XTAL_FREQ_HZ;

	if (rate < min_rate || rate > AD7768_MAX_MCLK_FREQ_HZ)
		return dev_err_probe(dev, -EINVAL,
				     "MCLK rate %lu Hz outside %lu-%lu Hz\n",
				     rate, min_rate, AD7768_MAX_MCLK_FREQ_HZ);

	return 0;
}

static int ad7768_probe(struct spi_device *spi)
{
	unsigned int spi_readback, rev_id;
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad7768_state *st;
	const char *clock_name;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, st);

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return dev_err_probe(dev, -ENODATA, "Failed to get match data\n");

	ret = devm_regulator_get_enable_optional(dev, "avss");
	if (ret == -ENODEV) {
		/* AVSS may be connected directly to ground instead of a regulator. */
	} else if (ret) {
		return dev_err_probe(dev, ret, "Failed to enable AVSS supply\n");
	}

	ret = devm_regulator_get_enable_read_voltage(dev, "avdd1");
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to enable AVDD1 supply\n");

	st->avdd1_uV = ret;

	ret = devm_regulator_bulk_get_enable(dev,
					     ARRAY_SIZE(ad7768_supply_names),
					     ad7768_supply_names);
	if (ret)
		return ret;

	for (unsigned int i = 0; i < ARRAY_SIZE(ad7768_vref_supply_names); i++) {
		ret = ad7768_get_enable_vref(dev, i);
		if (ret < 0)
			return ret;

		st->vref_uV[i] = ret;
	}

	ret = device_property_match_property_string(dev, "clock-names",
						    ad7768_clock_names,
						    ARRAY_SIZE(ad7768_clock_names));
	if (ret < 0)
		return dev_err_probe(dev, ret, "Invalid clock source\n");

	st->clock_source = ret;
	clock_name = ad7768_clock_names[st->clock_source];

	/*
	 * The device must start on its internal clock. Keep the LVDS clock
	 * disabled until GPIO4 is driven low and the LVDS input is enabled in
	 * the power mode register.
	 */
	switch (st->clock_source) {
	case AD7768_CLOCK_SOURCE_MCLK:
	case AD7768_CLOCK_SOURCE_XTAL:
		st->mclk = devm_clk_get_enabled(dev, clock_name);
		break;
	case AD7768_CLOCK_SOURCE_LVDS:
		st->mclk = devm_clk_get(dev, clock_name);
		break;
	}

	if (IS_ERR(st->mclk))
		return dev_err_probe(dev, PTR_ERR(st->mclk),
				     "Failed to get master clock\n");

	ret = ad7768_validate_mclk_rate(dev, st);
	if (ret)
		return ret;

	st->regmap = devm_regmap_init(dev, &ad7768_regmap_bus, spi,
				      st->chip_info->regmap_config);
	if (IS_ERR(st->regmap))
		return PTR_ERR(st->regmap);

	ret = ad7768_reset(st);
	if (ret)
		return ret;

	/* Discard the reset response with a dummy SPI register read. */
	ret = regmap_read(st->regmap, AD7768_REG_REV_ID, &spi_readback);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD7768_REG_REV_ID, &rev_id);
	if (ret)
		return ret;

	if (rev_id != AD7768_REV_ID_VAL)
		dev_info(dev, "Unexpected revision ID 0x%02x\n", rev_id);

	switch (st->clock_source) {
	case AD7768_CLOCK_SOURCE_MCLK:
		break;
	case AD7768_CLOCK_SOURCE_XTAL:
		ret = ad7768_configure_xtal_clock(st);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to configure crystal clock\n");
		break;
	case AD7768_CLOCK_SOURCE_LVDS:
		ret = ad7768_enable_lvds_clock(st);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to enable LVDS clock\n");
		break;
	}

	ret = devm_add_action_or_reset(dev, ad7768_power_off, st);
	if (ret)
		return ret;

	ret = ad7768_parse_config(indio_dev, dev);
	if (ret)
		return ret;

	/*
	 * Hardware supports CRC every 4 or 16 samples; the backend supports only
	 * 4-sample CRC.
	 */
	ret = regmap_update_bits(st->regmap, AD7768_REG_INTERFACE_CFG,
				 AD7768_INTERFACE_CFG_CRC_SELECT_MSK,
				 FIELD_PREP(AD7768_INTERFACE_CFG_CRC_SELECT_MSK,
					    AD7768_INTERFACE_CFG_CRC_SELECT_4));
	if (ret)
		return ret;

	indio_dev->name = st->chip_info->name;
	indio_dev->info = &ad7768_info;
	indio_dev->setup_ops = &ad7768_buffer_ops;

	st->back = devm_iio_backend_get(dev, NULL);
	if (IS_ERR(st->back))
		return PTR_ERR(st->back);

	ret = devm_iio_backend_request_buffer(dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = iio_backend_num_lanes_set(st->back, st->datalines);
	if (ret)
		return ret;

	ret = iio_backend_crc_enable(st->back);
	if (ret)
		return ret;

	ret = devm_iio_backend_enable(dev, st->back);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_set_active_enabled(dev);
	if (ret)
		return ret;

	ret = ad7768_register_vcm_regulator(dev, st);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to register VCM regulator\n");

	ret = ad7768_gpio_adev_init(st);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static int ad7768_runtime_suspend(struct device *dev)
{
	struct ad7768_state *st = dev_get_drvdata(dev);

	return ad7768_enter_sleep(st);
}

static int ad7768_runtime_resume(struct device *dev)
{
	struct ad7768_state *st = dev_get_drvdata(dev);
	int ret;

	ret = regmap_clear_bits(st->regmap, AD7768_REG_POWER_MODE,
				AD7768_SLEEP_MODE_MSK);
	if (ret)
		return ret;

	/*
	 * The datasheet does not specify a wake-up time. Allow 20 ms for the
	 * ADC and digital clocks to restart.
	 */
	fsleep(20 * USEC_PER_MSEC);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ad7768_pm_ops, ad7768_runtime_suspend,
				 ad7768_runtime_resume, NULL);

static const struct of_device_id ad7768_of_match[] = {
	{ .compatible = "adi,ad7768", .data = &ad7768_chip_info },
	{ .compatible = "adi,ad7768-4", .data = &ad7768_4_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7768_of_match);

static const struct spi_device_id ad7768_spi_id[] = {
	{ .name = "ad7768", .driver_data = (kernel_ulong_t)&ad7768_chip_info },
	{ .name = "ad7768-4", .driver_data = (kernel_ulong_t)&ad7768_4_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7768_spi_id);

static struct spi_driver ad7768_driver = {
	.probe = ad7768_probe,
	.driver = {
		.name = "ad7768",
		.of_match_table = ad7768_of_match,
		.pm = pm_ptr(&ad7768_pm_ops),
	},
	.id_table = ad7768_spi_id,
};
module_spi_driver(ad7768_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_AUTHOR("Janani Sunil <janani.sunil@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7768 ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BACKEND");
