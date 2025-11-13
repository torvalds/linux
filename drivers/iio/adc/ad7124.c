// SPDX-License-Identifier: GPL-2.0+
/*
 * AD7124 SPI ADC driver
 *
 * Copyright 2018 Analog Devices Inc.
 * Copyright 2025 BayLibre, SAS
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/sprintf.h>
#include <linux/units.h>

#include <linux/iio/iio.h>
#include <linux/iio/adc/ad_sigma_delta.h>
#include <linux/iio/sysfs.h>

/* AD7124 registers */
#define AD7124_COMMS			0x00
#define AD7124_STATUS			0x00
#define AD7124_ADC_CONTROL		0x01
#define AD7124_DATA			0x02
#define AD7124_IO_CONTROL_1		0x03
#define AD7124_IO_CONTROL_2		0x04
#define AD7124_ID			0x05
#define AD7124_ERROR			0x06
#define AD7124_ERROR_EN			0x07
#define AD7124_MCLK_COUNT		0x08
#define AD7124_CHANNEL(x)		(0x09 + (x))
#define AD7124_CONFIG(x)		(0x19 + (x))
#define AD7124_FILTER(x)		(0x21 + (x))
#define AD7124_OFFSET(x)		(0x29 + (x))
#define AD7124_GAIN(x)			(0x31 + (x))

/* AD7124_STATUS */
#define AD7124_STATUS_POR_FLAG			BIT(4)

/* AD7124_ADC_CONTROL */
#define AD7124_ADC_CONTROL_CLK_SEL		GENMASK(1, 0)
#define AD7124_ADC_CONTROL_CLK_SEL_INT			0
#define AD7124_ADC_CONTROL_CLK_SEL_INT_OUT		1
#define AD7124_ADC_CONTROL_CLK_SEL_EXT			2
#define AD7124_ADC_CONTROL_CLK_SEL_EXT_DIV4		3
#define AD7124_ADC_CONTROL_MODE			GENMASK(5, 2)
#define AD7124_ADC_CONTROL_MODE_CONTINUOUS		0
#define AD7124_ADC_CONTROL_MODE_SINGLE			1
#define AD7124_ADC_CONTROL_MODE_STANDBY			2
#define AD7124_ADC_CONTROL_MODE_POWERDOWN		3
#define AD7124_ADC_CONTROL_MODE_IDLE			4
#define AD7124_ADC_CONTROL_MODE_INT_OFFSET_CALIB	5 /* Internal Zero-Scale Calibration */
#define AD7124_ADC_CONTROL_MODE_INT_GAIN_CALIB		6 /* Internal Full-Scale Calibration */
#define AD7124_ADC_CONTROL_MODE_SYS_OFFSET_CALIB	7 /* System Zero-Scale Calibration */
#define AD7124_ADC_CONTROL_MODE_SYS_GAIN_CALIB		8 /* System Full-Scale Calibration */
#define AD7124_ADC_CONTROL_POWER_MODE		GENMASK(7, 6)
#define AD7124_ADC_CONTROL_POWER_MODE_LOW		0
#define AD7124_ADC_CONTROL_POWER_MODE_MID		1
#define AD7124_ADC_CONTROL_POWER_MODE_FULL		2
#define AD7124_ADC_CONTROL_REF_EN		BIT(8)
#define AD7124_ADC_CONTROL_DATA_STATUS		BIT(10)

/* AD7124_ID */
#define AD7124_ID_SILICON_REVISION		GENMASK(3, 0)
#define AD7124_ID_DEVICE_ID			GENMASK(7, 4)
#define AD7124_ID_DEVICE_ID_AD7124_4			0x0
#define AD7124_ID_DEVICE_ID_AD7124_8			0x1

/* AD7124_CHANNEL_X */
#define AD7124_CHANNEL_ENABLE		BIT(15)
#define AD7124_CHANNEL_SETUP		GENMASK(14, 12)
#define AD7124_CHANNEL_AINP		GENMASK(9, 5)
#define AD7124_CHANNEL_AINM		GENMASK(4, 0)
#define AD7124_CHANNEL_AINx_TEMPSENSOR		16
#define AD7124_CHANNEL_AINx_AVSS		17

/* AD7124_CONFIG_X */
#define AD7124_CONFIG_BIPOLAR		BIT(11)
#define AD7124_CONFIG_IN_BUFF		GENMASK(6, 5)
#define AD7124_CONFIG_AIN_BUFP		BIT(6)
#define AD7124_CONFIG_AIN_BUFM		BIT(5)
#define AD7124_CONFIG_REF_SEL		GENMASK(4, 3)
#define AD7124_CONFIG_PGA		GENMASK(2, 0)

/* AD7124_FILTER_X */
#define AD7124_FILTER_FILTER		GENMASK(23, 21)
#define AD7124_FILTER_FILTER_SINC4		0
#define AD7124_FILTER_FILTER_SINC3		2
#define AD7124_FILTER_FILTER_SINC4_SINC1	4
#define AD7124_FILTER_FILTER_SINC3_SINC1	5
#define AD7124_FILTER_FILTER_SINC3_PF		7
#define AD7124_FILTER_REJ60		BIT(20)
#define AD7124_FILTER_POST_FILTER	GENMASK(19, 17)
#define AD7124_FILTER_POST_FILTER_47dB		2
#define AD7124_FILTER_POST_FILTER_62dB		3
#define AD7124_FILTER_POST_FILTER_86dB		5
#define AD7124_FILTER_POST_FILTER_92dB		6
#define AD7124_FILTER_SINGLE_CYCLE	BIT(16)
#define AD7124_FILTER_FS		GENMASK(10, 0)

#define AD7124_MAX_CONFIGS	8
#define AD7124_MAX_CHANNELS	16

#define AD7124_INT_CLK_HZ	614400

/* AD7124 input sources */

enum ad7124_ref_sel {
	AD7124_REFIN1,
	AD7124_REFIN2,
	AD7124_INT_REF,
	AD7124_AVDD_REF,
};

enum ad7124_power_mode {
	AD7124_LOW_POWER,
	AD7124_MID_POWER,
	AD7124_FULL_POWER,
};

static const unsigned int ad7124_gain[8] = {
	1, 2, 4, 8, 16, 32, 64, 128
};

static const unsigned int ad7124_reg_size[] = {
	1, 2, 3, 3, 2, 1, 3, 3, 1, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3
};

static const int ad7124_master_clk_freq_hz[3] = {
	[AD7124_LOW_POWER] = AD7124_INT_CLK_HZ / 8,
	[AD7124_MID_POWER] = AD7124_INT_CLK_HZ / 4,
	[AD7124_FULL_POWER] = AD7124_INT_CLK_HZ,
};

static const char * const ad7124_ref_names[] = {
	[AD7124_REFIN1] = "refin1",
	[AD7124_REFIN2] = "refin2",
	[AD7124_INT_REF] = "int",
	[AD7124_AVDD_REF] = "avdd",
};

struct ad7124_chip_info {
	const char *name;
	unsigned int chip_id;
	unsigned int num_inputs;
};

enum ad7124_filter_type {
	AD7124_FILTER_TYPE_SINC3,
	AD7124_FILTER_TYPE_SINC3_PF1,
	AD7124_FILTER_TYPE_SINC3_PF2,
	AD7124_FILTER_TYPE_SINC3_PF3,
	AD7124_FILTER_TYPE_SINC3_PF4,
	AD7124_FILTER_TYPE_SINC3_REJ60,
	AD7124_FILTER_TYPE_SINC3_SINC1,
	AD7124_FILTER_TYPE_SINC4,
	AD7124_FILTER_TYPE_SINC4_REJ60,
	AD7124_FILTER_TYPE_SINC4_SINC1,
};

struct ad7124_channel_config {
	bool live;
	unsigned int cfg_slot;
	unsigned int requested_odr;
	unsigned int requested_odr_micro;
	/*
	 * Following fields are used to compare for equality. If you
	 * make adaptations in it, you most likely also have to adapt
	 * ad7124_find_similar_live_cfg(), too.
	 */
	struct_group(config_props,
		enum ad7124_ref_sel refsel;
		bool bipolar;
		bool buf_positive;
		bool buf_negative;
		unsigned int vref_mv;
		unsigned int pga_bits;
		unsigned int odr_sel_bits;
		enum ad7124_filter_type filter_type;
		unsigned int calibration_offset;
		unsigned int calibration_gain;
	);
};

struct ad7124_channel {
	unsigned int nr;
	struct ad7124_channel_config cfg;
	unsigned int ain;
	unsigned int slot;
	u8 syscalib_mode;
};

struct ad7124_state {
	const struct ad7124_chip_info *chip_info;
	struct ad_sigma_delta sd;
	struct ad7124_channel *channels;
	struct regulator *vref[4];
	u32 clk_hz;
	unsigned int adc_control;
	unsigned int num_channels;
	struct mutex cfgs_lock; /* lock for configs access */
	unsigned long cfg_slots_status; /* bitmap with slot status (1 means it is used) */

	/*
	 * Stores the power-on reset value for the GAIN(x) registers which are
	 * needed for measurements at gain 1 (i.e. CONFIG(x).PGA == 0)
	 */
	unsigned int gain_default;
	DECLARE_KFIFO(live_cfgs_fifo, struct ad7124_channel_config *, AD7124_MAX_CONFIGS);
};

static const struct ad7124_chip_info ad7124_4_chip_info = {
	.name = "ad7124-4",
	.chip_id = AD7124_ID_DEVICE_ID_AD7124_4,
	.num_inputs = 8,
};

static const struct ad7124_chip_info ad7124_8_chip_info = {
	.name = "ad7124-8",
	.chip_id = AD7124_ID_DEVICE_ID_AD7124_8,
	.num_inputs = 16,
};

static int ad7124_find_closest_match(const int *array,
				     unsigned int size, int val)
{
	int i, idx;
	unsigned int diff_new, diff_old;

	diff_old = U32_MAX;
	idx = 0;

	for (i = 0; i < size; i++) {
		diff_new = abs(val - array[i]);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			idx = i;
		}
	}

	return idx;
}

static int ad7124_spi_write_mask(struct ad7124_state *st,
				 unsigned int addr,
				 unsigned long mask,
				 unsigned int val,
				 unsigned int bytes)
{
	unsigned int readval;
	int ret;

	ret = ad_sd_read_reg(&st->sd, addr, bytes, &readval);
	if (ret < 0)
		return ret;

	readval &= ~mask;
	readval |= val;

	return ad_sd_write_reg(&st->sd, addr, bytes, readval);
}

static int ad7124_set_mode(struct ad_sigma_delta *sd,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);

	st->adc_control &= ~AD7124_ADC_CONTROL_MODE;
	st->adc_control |= FIELD_PREP(AD7124_ADC_CONTROL_MODE, mode);

	return ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, st->adc_control);
}

static u32 ad7124_get_fclk_hz(struct ad7124_state *st)
{
	enum ad7124_power_mode power_mode;
	u32 fclk_hz;

	power_mode = FIELD_GET(AD7124_ADC_CONTROL_POWER_MODE, st->adc_control);
	fclk_hz = st->clk_hz;

	switch (power_mode) {
	case AD7124_LOW_POWER:
		fclk_hz /= 8;
		break;
	case AD7124_MID_POWER:
		fclk_hz /= 4;
		break;
	default:
		break;
	}

	return fclk_hz;
}

static u32 ad7124_get_fs_factor(struct ad7124_state *st, unsigned int channel)
{
	enum ad7124_power_mode power_mode =
		FIELD_GET(AD7124_ADC_CONTROL_POWER_MODE, st->adc_control);
	u32 avg = power_mode == AD7124_LOW_POWER ? 8 : 16;

	/*
	 * These are the "zero-latency" factors from the data sheet. For the
	 * sinc1 filters, these aren't documented, but derived by taking the
	 * single-channel formula from the sinc1 section of the data sheet and
	 * multiplying that by the sinc3/4 factor from the corresponding zero-
	 * latency sections.
	 */
	switch (st->channels[channel].cfg.filter_type) {
	case AD7124_FILTER_TYPE_SINC4:
	case AD7124_FILTER_TYPE_SINC4_REJ60:
		return 4 * 32;
	case AD7124_FILTER_TYPE_SINC4_SINC1:
		return 4 * avg * 32;
	case AD7124_FILTER_TYPE_SINC3_SINC1:
		return 3 * avg * 32;
	default:
		return 3 * 32;
	}
}

static u32 ad7124_get_fadc_divisor(struct ad7124_state *st, unsigned int channel)
{
	u32 factor = ad7124_get_fs_factor(st, channel);

	/*
	 * The output data rate (f_ADC) is f_CLK / divisor. We are returning
	 * the divisor.
	 */
	return st->channels[channel].cfg.odr_sel_bits * factor;
}

static void ad7124_set_channel_odr(struct ad7124_state *st, unsigned int channel)
{
	struct ad7124_channel_config *cfg = &st->channels[channel].cfg;
	unsigned int fclk, factor, divisor, odr_sel_bits;

	fclk = ad7124_get_fclk_hz(st);
	factor = ad7124_get_fs_factor(st, channel);

	/*
	 * FS[10:0] = fCLK / (fADC x 32 * N) where:
	 * fADC is the output data rate
	 * fCLK is the master clock frequency
	 * N is number of conversions per sample (depends on filter type)
	 * FS[10:0] are the bits in the filter register
	 * FS[10:0] can have a value from 1 to 2047
	 */
	divisor = cfg->requested_odr * factor +
		  cfg->requested_odr_micro * factor / MICRO;
	odr_sel_bits = clamp(DIV_ROUND_CLOSEST(fclk, divisor), 1, 2047);

	if (odr_sel_bits != st->channels[channel].cfg.odr_sel_bits)
		st->channels[channel].cfg.live = false;

	st->channels[channel].cfg.odr_sel_bits = odr_sel_bits;
}

static int ad7124_get_3db_filter_factor(struct ad7124_state *st,
					unsigned int channel)
{
	struct ad7124_channel_config *cfg = &st->channels[channel].cfg;

	/*
	 * 3dB point is the f_CLK rate times some factor. This functions returns
	 * the factor times 1000.
	 */
	switch (cfg->filter_type) {
	case AD7124_FILTER_TYPE_SINC3:
	case AD7124_FILTER_TYPE_SINC3_REJ60:
	case AD7124_FILTER_TYPE_SINC3_SINC1:
		return 272;
	case AD7124_FILTER_TYPE_SINC4:
	case AD7124_FILTER_TYPE_SINC4_REJ60:
	case AD7124_FILTER_TYPE_SINC4_SINC1:
		return 230;
	case AD7124_FILTER_TYPE_SINC3_PF1:
		return 633;
	case AD7124_FILTER_TYPE_SINC3_PF2:
		return 605;
	case AD7124_FILTER_TYPE_SINC3_PF3:
		return 669;
	case AD7124_FILTER_TYPE_SINC3_PF4:
		return 759;
	default:
		return -EINVAL;
	}
}

static struct ad7124_channel_config *ad7124_find_similar_live_cfg(struct ad7124_state *st,
								  struct ad7124_channel_config *cfg)
{
	struct ad7124_channel_config *cfg_aux;
	int i;

	/*
	 * This is just to make sure that the comparison is adapted after
	 * struct ad7124_channel_config was changed.
	 */
	static_assert(sizeof_field(struct ad7124_channel_config, config_props) ==
		      sizeof(struct {
				     enum ad7124_ref_sel refsel;
				     bool bipolar;
				     bool buf_positive;
				     bool buf_negative;
				     unsigned int vref_mv;
				     unsigned int pga_bits;
				     unsigned int odr_sel_bits;
				     enum ad7124_filter_type filter_type;
				     unsigned int calibration_offset;
				     unsigned int calibration_gain;
			     }));

	for (i = 0; i < st->num_channels; i++) {
		cfg_aux = &st->channels[i].cfg;

		if (cfg_aux->live &&
		    cfg->refsel == cfg_aux->refsel &&
		    cfg->bipolar == cfg_aux->bipolar &&
		    cfg->buf_positive == cfg_aux->buf_positive &&
		    cfg->buf_negative == cfg_aux->buf_negative &&
		    cfg->vref_mv == cfg_aux->vref_mv &&
		    cfg->pga_bits == cfg_aux->pga_bits &&
		    cfg->odr_sel_bits == cfg_aux->odr_sel_bits &&
		    cfg->filter_type == cfg_aux->filter_type &&
		    cfg->calibration_offset == cfg_aux->calibration_offset &&
		    cfg->calibration_gain == cfg_aux->calibration_gain)
			return cfg_aux;
	}

	return NULL;
}

static int ad7124_find_free_config_slot(struct ad7124_state *st)
{
	unsigned int free_cfg_slot;

	free_cfg_slot = find_first_zero_bit(&st->cfg_slots_status, AD7124_MAX_CONFIGS);
	if (free_cfg_slot == AD7124_MAX_CONFIGS)
		return -1;

	return free_cfg_slot;
}

/* Only called during probe, so dev_err_probe() can be used */
static int ad7124_init_config_vref(struct ad7124_state *st, struct ad7124_channel_config *cfg)
{
	struct device *dev = &st->sd.spi->dev;
	unsigned int refsel = cfg->refsel;

	switch (refsel) {
	case AD7124_REFIN1:
	case AD7124_REFIN2:
	case AD7124_AVDD_REF:
		if (IS_ERR(st->vref[refsel]))
			return dev_err_probe(dev, PTR_ERR(st->vref[refsel]),
					     "Error, trying to use external voltage reference without a %s regulator.\n",
					     ad7124_ref_names[refsel]);

		cfg->vref_mv = regulator_get_voltage(st->vref[refsel]);
		/* Conversion from uV to mV */
		cfg->vref_mv /= 1000;
		return 0;
	case AD7124_INT_REF:
		cfg->vref_mv = 2500;
		st->adc_control |= AD7124_ADC_CONTROL_REF_EN;
		return 0;
	default:
		return dev_err_probe(dev, -EINVAL, "Invalid reference %d\n", refsel);
	}
}

static int ad7124_write_config(struct ad7124_state *st, struct ad7124_channel_config *cfg,
			       unsigned int cfg_slot)
{
	unsigned int val, filter;
	unsigned int rej60 = 0;
	unsigned int post = 0;
	int ret;

	cfg->cfg_slot = cfg_slot;

	ret = ad_sd_write_reg(&st->sd, AD7124_OFFSET(cfg->cfg_slot), 3, cfg->calibration_offset);
	if (ret)
		return ret;

	ret = ad_sd_write_reg(&st->sd, AD7124_GAIN(cfg->cfg_slot), 3, cfg->calibration_gain);
	if (ret)
		return ret;

	val = FIELD_PREP(AD7124_CONFIG_BIPOLAR, cfg->bipolar) |
		FIELD_PREP(AD7124_CONFIG_REF_SEL, cfg->refsel) |
		(cfg->buf_positive ? AD7124_CONFIG_AIN_BUFP : 0) |
		(cfg->buf_negative ? AD7124_CONFIG_AIN_BUFM : 0) |
		FIELD_PREP(AD7124_CONFIG_PGA, cfg->pga_bits);

	ret = ad_sd_write_reg(&st->sd, AD7124_CONFIG(cfg->cfg_slot), 2, val);
	if (ret < 0)
		return ret;

	switch (cfg->filter_type) {
	case AD7124_FILTER_TYPE_SINC3:
		filter = AD7124_FILTER_FILTER_SINC3;
		break;
	case AD7124_FILTER_TYPE_SINC3_PF1:
		filter = AD7124_FILTER_FILTER_SINC3_PF;
		post = AD7124_FILTER_POST_FILTER_47dB;
		break;
	case AD7124_FILTER_TYPE_SINC3_PF2:
		filter = AD7124_FILTER_FILTER_SINC3_PF;
		post = AD7124_FILTER_POST_FILTER_62dB;
		break;
	case AD7124_FILTER_TYPE_SINC3_PF3:
		filter = AD7124_FILTER_FILTER_SINC3_PF;
		post = AD7124_FILTER_POST_FILTER_86dB;
		break;
	case AD7124_FILTER_TYPE_SINC3_PF4:
		filter = AD7124_FILTER_FILTER_SINC3_PF;
		post = AD7124_FILTER_POST_FILTER_92dB;
		break;
	case AD7124_FILTER_TYPE_SINC3_REJ60:
		filter = AD7124_FILTER_FILTER_SINC3;
		rej60 = 1;
		break;
	case AD7124_FILTER_TYPE_SINC3_SINC1:
		filter = AD7124_FILTER_FILTER_SINC3_SINC1;
		break;
	case AD7124_FILTER_TYPE_SINC4:
		filter = AD7124_FILTER_FILTER_SINC4;
		break;
	case AD7124_FILTER_TYPE_SINC4_REJ60:
		filter = AD7124_FILTER_FILTER_SINC4;
		rej60 = 1;
		break;
	case AD7124_FILTER_TYPE_SINC4_SINC1:
		filter = AD7124_FILTER_FILTER_SINC4_SINC1;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * NB: AD7124_FILTER_SINGLE_CYCLE is always set so that we get the same
	 * sampling frequency even when only one channel is enabled in a
	 * buffered read. If it was not set, the N in ad7124_set_channel_odr()
	 * would be 1 and we would get a faster sampling frequency than what
	 * was requested.
	 */
	return ad_sd_write_reg(&st->sd, AD7124_FILTER(cfg->cfg_slot), 3,
			       FIELD_PREP(AD7124_FILTER_FILTER, filter) |
			       FIELD_PREP(AD7124_FILTER_REJ60, rej60) |
			       FIELD_PREP(AD7124_FILTER_POST_FILTER, post) |
			       AD7124_FILTER_SINGLE_CYCLE |
			       FIELD_PREP(AD7124_FILTER_FS, cfg->odr_sel_bits));
}

static struct ad7124_channel_config *ad7124_pop_config(struct ad7124_state *st)
{
	struct ad7124_channel_config *lru_cfg;
	struct ad7124_channel_config *cfg;
	int ret;
	int i;

	/*
	 * Pop least recently used config from the fifo
	 * in order to make room for the new one
	 */
	ret = kfifo_get(&st->live_cfgs_fifo, &lru_cfg);
	if (ret <= 0)
		return NULL;

	lru_cfg->live = false;

	/* mark slot as free */
	assign_bit(lru_cfg->cfg_slot, &st->cfg_slots_status, 0);

	/* invalidate all other configs that pointed to this one */
	for (i = 0; i < st->num_channels; i++) {
		cfg = &st->channels[i].cfg;

		if (cfg->cfg_slot == lru_cfg->cfg_slot)
			cfg->live = false;
	}

	return lru_cfg;
}

static int ad7124_push_config(struct ad7124_state *st, struct ad7124_channel_config *cfg)
{
	struct ad7124_channel_config *lru_cfg;
	int free_cfg_slot;

	free_cfg_slot = ad7124_find_free_config_slot(st);
	if (free_cfg_slot >= 0) {
		/* push the new config in configs queue */
		kfifo_put(&st->live_cfgs_fifo, cfg);
	} else {
		/* pop one config to make room for the new one */
		lru_cfg = ad7124_pop_config(st);
		if (!lru_cfg)
			return -EINVAL;

		/* push the new config in configs queue */
		free_cfg_slot = lru_cfg->cfg_slot;
		kfifo_put(&st->live_cfgs_fifo, cfg);
	}

	/* mark slot as used */
	assign_bit(free_cfg_slot, &st->cfg_slots_status, 1);

	return ad7124_write_config(st, cfg, free_cfg_slot);
}

static int ad7124_enable_channel(struct ad7124_state *st, struct ad7124_channel *ch)
{
	ch->cfg.live = true;
	return ad_sd_write_reg(&st->sd, AD7124_CHANNEL(ch->nr), 2, ch->ain |
			       FIELD_PREP(AD7124_CHANNEL_SETUP, ch->cfg.cfg_slot) |
			       AD7124_CHANNEL_ENABLE);
}

static int ad7124_prepare_read(struct ad7124_state *st, int address)
{
	struct ad7124_channel_config *cfg = &st->channels[address].cfg;
	struct ad7124_channel_config *live_cfg;

	/*
	 * Before doing any reads assign the channel a configuration.
	 * Check if channel's config is on the device
	 */
	if (!cfg->live) {
		/* check if config matches another one */
		live_cfg = ad7124_find_similar_live_cfg(st, cfg);
		if (!live_cfg)
			ad7124_push_config(st, cfg);
		else
			cfg->cfg_slot = live_cfg->cfg_slot;
	}

	/* point channel to the config slot and enable */
	return ad7124_enable_channel(st, &st->channels[address]);
}

static int __ad7124_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);

	return ad7124_prepare_read(st, channel);
}

static int ad7124_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);
	int ret;

	mutex_lock(&st->cfgs_lock);
	ret = __ad7124_set_channel(sd, channel);
	mutex_unlock(&st->cfgs_lock);

	return ret;
}

static int ad7124_append_status(struct ad_sigma_delta *sd, bool append)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);
	unsigned int adc_control = st->adc_control;
	int ret;

	if (append)
		adc_control |= AD7124_ADC_CONTROL_DATA_STATUS;
	else
		adc_control &= ~AD7124_ADC_CONTROL_DATA_STATUS;

	ret = ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, adc_control);
	if (ret < 0)
		return ret;

	st->adc_control = adc_control;

	return 0;
}

static int ad7124_disable_one(struct ad_sigma_delta *sd, unsigned int chan)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);

	/* The relevant thing here is that AD7124_CHANNEL_ENABLE is cleared. */
	return ad_sd_write_reg(&st->sd, AD7124_CHANNEL(chan), 2, 0);
}

static int ad7124_disable_all(struct ad_sigma_delta *sd)
{
	int ret;
	int i;

	for (i = 0; i < 16; i++) {
		ret = ad7124_disable_one(sd, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct ad_sigma_delta_info ad7124_sigma_delta_info = {
	.set_channel = ad7124_set_channel,
	.append_status = ad7124_append_status,
	.disable_all = ad7124_disable_all,
	.disable_one = ad7124_disable_one,
	.set_mode = ad7124_set_mode,
	.has_registers = true,
	.addr_shift = 0,
	.read_mask = BIT(6),
	.status_ch_mask = GENMASK(3, 0),
	.data_reg = AD7124_DATA,
	.num_slots = 8,
	.irq_flags = IRQF_TRIGGER_FALLING,
	.num_resetclks = 64,
};

static const int ad7124_voltage_scales[][2] = {
	{ 0, 1164 },
	{ 0, 2328 },
	{ 0, 4656 },
	{ 0, 9313 },
	{ 0, 18626 },
	{ 0, 37252 },
	{ 0, 74505 },
	{ 0, 149011 },
	{ 0, 298023 },
};

static int ad7124_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length, long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (const int *)ad7124_voltage_scales;
		*type = IIO_VAL_INT_PLUS_NANO;
		*length = ARRAY_SIZE(ad7124_voltage_scales) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int ad7124_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	int idx, ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = ad_sigma_delta_single_conversion(indio_dev, chan, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			mutex_lock(&st->cfgs_lock);

			idx = st->channels[chan->address].cfg.pga_bits;
			*val = st->channels[chan->address].cfg.vref_mv;
			if (st->channels[chan->address].cfg.bipolar)
				*val2 = chan->scan_type.realbits - 1 + idx;
			else
				*val2 = chan->scan_type.realbits + idx;

			mutex_unlock(&st->cfgs_lock);
			return IIO_VAL_FRACTIONAL_LOG2;

		case IIO_TEMP:
			/*
			 * According to the data sheet
			 *   Temperature (°C)
			 * = ((Conversion − 0x800000)/13584) − 272.5
			 * = (Conversion − 0x800000 - 13584 * 272.5) / 13584
			 * = (Conversion − 12090248) / 13584
			 * So scale with 1000/13584 to yield °mC. Reduce by 8 to
			 * 125/1698.
			 */
			*val = 125;
			*val2 = 1698;
			return IIO_VAL_FRACTIONAL;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_VOLTAGE:
			mutex_lock(&st->cfgs_lock);
			if (st->channels[chan->address].cfg.bipolar)
				*val = -(1 << (chan->scan_type.realbits - 1));
			else
				*val = 0;

			mutex_unlock(&st->cfgs_lock);
			return IIO_VAL_INT;

		case IIO_TEMP:
			/* see calculation above */
			*val = -12090248;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SAMP_FREQ: {
		struct ad7124_channel_config *cfg = &st->channels[chan->address].cfg;

		guard(mutex)(&st->cfgs_lock);

		switch (cfg->filter_type) {
		case AD7124_FILTER_TYPE_SINC3:
		case AD7124_FILTER_TYPE_SINC3_REJ60:
		case AD7124_FILTER_TYPE_SINC3_SINC1:
		case AD7124_FILTER_TYPE_SINC4:
		case AD7124_FILTER_TYPE_SINC4_REJ60:
		case AD7124_FILTER_TYPE_SINC4_SINC1:
			*val = ad7124_get_fclk_hz(st);
			*val2 = ad7124_get_fadc_divisor(st, chan->address);
			return IIO_VAL_FRACTIONAL;
		/*
		 * Post filters force the chip to a fixed rate. These are the
		 * single-channel rates from the data sheet divided by 3 for
		 * the multi-channel case (data sheet doesn't explicitly state
		 * this but confirmed through testing).
		 */
		case AD7124_FILTER_TYPE_SINC3_PF1:
			*val = 300;
			*val2 = 33;
			return IIO_VAL_FRACTIONAL;
		case AD7124_FILTER_TYPE_SINC3_PF2:
			*val = 25;
			*val2 = 3;
			return IIO_VAL_FRACTIONAL;
		case AD7124_FILTER_TYPE_SINC3_PF3:
			*val = 20;
			*val2 = 3;
			return IIO_VAL_FRACTIONAL;
		case AD7124_FILTER_TYPE_SINC3_PF4:
			*val = 50;
			*val2 = 9;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	}
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY: {
		guard(mutex)(&st->cfgs_lock);

		ret = ad7124_get_3db_filter_factor(st, chan->address);
		if (ret < 0)
			return ret;

		/* 3dB point is the f_CLK rate times a fractional value */
		*val = ret * ad7124_get_fclk_hz(st);
		*val2 = MILLI * ad7124_get_fadc_divisor(st, chan->address);
		return IIO_VAL_FRACTIONAL;
	}
	default:
		return -EINVAL;
	}
}

static int ad7124_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	struct ad7124_channel_config *cfg = &st->channels[chan->address].cfg;
	unsigned int res, gain, full_scale, vref;

	guard(mutex)(&st->cfgs_lock);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2 < 0 || val < 0 || (val2 == 0 && val == 0))
			return -EINVAL;

		cfg->requested_odr = val;
		cfg->requested_odr_micro = val2;
		ad7124_set_channel_odr(st, chan->address);

		return 0;
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		if (st->channels[chan->address].cfg.bipolar)
			full_scale = 1 << (chan->scan_type.realbits - 1);
		else
			full_scale = 1 << chan->scan_type.realbits;

		vref = st->channels[chan->address].cfg.vref_mv * 1000000LL;
		res = DIV_ROUND_CLOSEST(vref, full_scale);
		gain = DIV_ROUND_CLOSEST(res, val2);
		res = ad7124_find_closest_match(ad7124_gain, ARRAY_SIZE(ad7124_gain), gain);

		if (st->channels[chan->address].cfg.pga_bits != res)
			st->channels[chan->address].cfg.live = false;

		st->channels[chan->address].cfg.pga_bits = res;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ad7124_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	int ret;

	if (reg >= ARRAY_SIZE(ad7124_reg_size))
		return -EINVAL;

	if (readval)
		ret = ad_sd_read_reg(&st->sd, reg, ad7124_reg_size[reg],
				     readval);
	else
		ret = ad_sd_write_reg(&st->sd, reg, ad7124_reg_size[reg],
				      writeval);

	return ret;
}

static int ad7124_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	bool bit_set;
	int ret;
	int i;

	guard(mutex)(&st->cfgs_lock);

	for (i = 0; i < st->num_channels; i++) {
		bit_set = test_bit(i, scan_mask);
		if (bit_set)
			ret = __ad7124_set_channel(&st->sd, i);
		else
			ret = ad7124_spi_write_mask(st, AD7124_CHANNEL(i), AD7124_CHANNEL_ENABLE,
						    0, 2);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct iio_info ad7124_info = {
	.read_avail = ad7124_read_avail,
	.read_raw = ad7124_read_raw,
	.write_raw = ad7124_write_raw,
	.debugfs_reg_access = &ad7124_reg_access,
	.validate_trigger = ad_sd_validate_trigger,
	.update_scan_mode = ad7124_update_scan_mode,
};

/* Only called during probe, so dev_err_probe() can be used */
static int ad7124_soft_reset(struct ad7124_state *st)
{
	struct device *dev = &st->sd.spi->dev;
	unsigned int readval, timeout;
	int ret;

	ret = ad_sd_reset(&st->sd);
	if (ret < 0)
		return ret;

	fsleep(200);
	timeout = 100;
	do {
		ret = ad_sd_read_reg(&st->sd, AD7124_STATUS, 1, &readval);
		if (ret < 0)
			return dev_err_probe(dev, ret, "Error reading status register\n");

		if (!(readval & AD7124_STATUS_POR_FLAG))
			break;

		/* The AD7124 requires typically 2ms to power up and settle */
		usleep_range(100, 2000);
	} while (--timeout);

	if (readval & AD7124_STATUS_POR_FLAG)
		return dev_err_probe(dev, -EIO, "Soft reset failed\n");

	ret = ad_sd_read_reg(&st->sd, AD7124_GAIN(0), 3, &st->gain_default);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Error reading gain register\n");

	dev_dbg(dev, "Reset value of GAIN register is 0x%x\n", st->gain_default);

	return 0;
}

static int ad7124_check_chip_id(struct ad7124_state *st)
{
	struct device *dev = &st->sd.spi->dev;
	unsigned int readval, chip_id, silicon_rev;
	int ret;

	ret = ad_sd_read_reg(&st->sd, AD7124_ID, 1, &readval);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failure to read ID register\n");

	chip_id = FIELD_GET(AD7124_ID_DEVICE_ID, readval);
	silicon_rev = FIELD_GET(AD7124_ID_SILICON_REVISION, readval);

	if (chip_id != st->chip_info->chip_id)
		return dev_err_probe(dev, -ENODEV,
				     "Chip ID mismatch: expected %u, got %u\n",
				     st->chip_info->chip_id, chip_id);

	if (silicon_rev == 0)
		return dev_err_probe(dev, -ENODEV,
				     "Silicon revision empty. Chip may not be present\n");

	return 0;
}

enum {
	AD7124_SYSCALIB_ZERO_SCALE,
	AD7124_SYSCALIB_FULL_SCALE,
};

static int ad7124_syscalib_locked(struct ad7124_state *st, const struct iio_chan_spec *chan)
{
	struct device *dev = &st->sd.spi->dev;
	struct ad7124_channel *ch = &st->channels[chan->address];
	int ret;

	if (ch->syscalib_mode == AD7124_SYSCALIB_ZERO_SCALE) {
		ch->cfg.calibration_offset = 0x800000;

		ret = ad_sd_calibrate(&st->sd, AD7124_ADC_CONTROL_MODE_SYS_OFFSET_CALIB,
				      chan->address);
		if (ret < 0)
			return ret;

		ret = ad_sd_read_reg(&st->sd, AD7124_OFFSET(ch->cfg.cfg_slot), 3,
				     &ch->cfg.calibration_offset);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "offset for channel %lu after zero-scale calibration: 0x%x\n",
			chan->address, ch->cfg.calibration_offset);
	} else {
		ch->cfg.calibration_gain = st->gain_default;

		ret = ad_sd_calibrate(&st->sd, AD7124_ADC_CONTROL_MODE_SYS_GAIN_CALIB,
				      chan->address);
		if (ret < 0)
			return ret;

		ret = ad_sd_read_reg(&st->sd, AD7124_GAIN(ch->cfg.cfg_slot), 3,
				     &ch->cfg.calibration_gain);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "gain for channel %lu after full-scale calibration: 0x%x\n",
			chan->address, ch->cfg.calibration_gain);
	}

	return 0;
}

static ssize_t ad7124_write_syscalib(struct iio_dev *indio_dev,
				     uintptr_t private,
				     const struct iio_chan_spec *chan,
				     const char *buf, size_t len)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	bool sys_calib;
	int ret;

	ret = kstrtobool(buf, &sys_calib);
	if (ret)
		return ret;

	if (!sys_calib)
		return len;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = ad7124_syscalib_locked(st, chan);

	iio_device_release_direct(indio_dev);

	return ret ?: len;
}

static const char * const ad7124_syscalib_modes[] = {
	[AD7124_SYSCALIB_ZERO_SCALE] = "zero_scale",
	[AD7124_SYSCALIB_FULL_SCALE] = "full_scale",
};

static int ad7124_set_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    unsigned int mode)
{
	struct ad7124_state *st = iio_priv(indio_dev);

	st->channels[chan->address].syscalib_mode = mode;

	return 0;
}

static int ad7124_get_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan)
{
	struct ad7124_state *st = iio_priv(indio_dev);

	return st->channels[chan->address].syscalib_mode;
}

static const struct iio_enum ad7124_syscalib_mode_enum = {
	.items = ad7124_syscalib_modes,
	.num_items = ARRAY_SIZE(ad7124_syscalib_modes),
	.set = ad7124_set_syscalib_mode,
	.get = ad7124_get_syscalib_mode
};

static const char * const ad7124_filter_types[] = {
	[AD7124_FILTER_TYPE_SINC3] = "sinc3",
	[AD7124_FILTER_TYPE_SINC3_PF1] = "sinc3+pf1",
	[AD7124_FILTER_TYPE_SINC3_PF2] = "sinc3+pf2",
	[AD7124_FILTER_TYPE_SINC3_PF3] = "sinc3+pf3",
	[AD7124_FILTER_TYPE_SINC3_PF4] = "sinc3+pf4",
	[AD7124_FILTER_TYPE_SINC3_REJ60] = "sinc3+rej60",
	[AD7124_FILTER_TYPE_SINC3_SINC1] = "sinc3+sinc1",
	[AD7124_FILTER_TYPE_SINC4] = "sinc4",
	[AD7124_FILTER_TYPE_SINC4_REJ60] = "sinc4+rej60",
	[AD7124_FILTER_TYPE_SINC4_SINC1] = "sinc4+sinc1",
};

static int ad7124_set_filter_type_attr(struct iio_dev *dev,
				       const struct iio_chan_spec *chan,
				       unsigned int value)
{
	struct ad7124_state *st = iio_priv(dev);
	struct ad7124_channel_config *cfg = &st->channels[chan->address].cfg;

	guard(mutex)(&st->cfgs_lock);

	cfg->live = false;
	cfg->filter_type = value;
	ad7124_set_channel_odr(st, chan->address);

	return 0;
}

static int ad7124_get_filter_type_attr(struct iio_dev *dev,
				       const struct iio_chan_spec *chan)
{
	struct ad7124_state *st = iio_priv(dev);

	guard(mutex)(&st->cfgs_lock);

	return st->channels[chan->address].cfg.filter_type;
}

static const struct iio_enum ad7124_filter_type_enum = {
	.items = ad7124_filter_types,
	.num_items = ARRAY_SIZE(ad7124_filter_types),
	.set = ad7124_set_filter_type_attr,
	.get = ad7124_get_filter_type_attr,
};

static const struct iio_chan_spec_ext_info ad7124_calibsys_ext_info[] = {
	{
		.name = "sys_calibration",
		.write = ad7124_write_syscalib,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("sys_calibration_mode", IIO_SEPARATE,
		 &ad7124_syscalib_mode_enum),
	IIO_ENUM_AVAILABLE("sys_calibration_mode", IIO_SHARED_BY_TYPE,
			   &ad7124_syscalib_mode_enum),
	IIO_ENUM("filter_type", IIO_SEPARATE, &ad7124_filter_type_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SHARED_BY_TYPE,
			   &ad7124_filter_type_enum),
	{ }
};

static const struct iio_chan_spec ad7124_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.differential = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) |
		BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),
	.scan_type = {
		.sign = 'u',
		.realbits = 24,
		.storagebits = 32,
		.endianness = IIO_BE,
	},
	.ext_info = ad7124_calibsys_ext_info,
};

/*
 * Input specifiers 8 - 15 are explicitly reserved for ad7124-4
 * while they are fine for ad7124-8. Values above 31 don't fit
 * into the register field and so are invalid for sure.
 */
static bool ad7124_valid_input_select(unsigned int ain, const struct ad7124_chip_info *info)
{
	if (ain >= info->num_inputs && ain < 16)
		return false;

	return ain <= FIELD_MAX(AD7124_CHANNEL_AINM);
}

static int ad7124_parse_channel_config(struct iio_dev *indio_dev,
				       struct device *dev)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	struct ad7124_channel_config *cfg;
	struct ad7124_channel *channels;
	struct iio_chan_spec *chan;
	unsigned int ain[2], channel = 0, tmp;
	unsigned int num_channels;
	int ret;

	num_channels = device_get_child_node_count(dev);

	/*
	 * The driver assigns each logical channel defined in the device tree
	 * statically one channel register. So only accept 16 such logical
	 * channels to not treat CONFIG_0 (i.e. the register following
	 * CHANNEL_15) as an additional channel register. The driver could be
	 * improved to lift this limitation.
	 */
	if (num_channels > AD7124_MAX_CHANNELS)
		return dev_err_probe(dev, -EINVAL, "Too many channels defined\n");

	/* Add one for temperature */
	st->num_channels = min(num_channels + 1, AD7124_MAX_CHANNELS);

	chan = devm_kcalloc(dev, st->num_channels,
			    sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	channels = devm_kcalloc(dev, st->num_channels, sizeof(*channels),
				GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	indio_dev->channels = chan;
	indio_dev->num_channels = st->num_channels;
	st->channels = channels;

	device_for_each_child_node_scoped(dev, child) {
		ret = fwnode_property_read_u32(child, "reg", &channel);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to parse reg property of %pfwP\n", child);

		if (channel >= num_channels)
			return dev_err_probe(dev, -EINVAL,
					     "Channel index >= number of channels in %pfwP\n", child);

		ret = fwnode_property_read_u32_array(child, "diff-channels",
						     ain, 2);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to parse diff-channels property of %pfwP\n", child);

		if (!ad7124_valid_input_select(ain[0], st->chip_info) ||
		    !ad7124_valid_input_select(ain[1], st->chip_info))
			return dev_err_probe(dev, -EINVAL,
					     "diff-channels property of %pfwP contains invalid data\n", child);

		st->channels[channel].nr = channel;
		st->channels[channel].ain = FIELD_PREP(AD7124_CHANNEL_AINP, ain[0]) |
			FIELD_PREP(AD7124_CHANNEL_AINM, ain[1]);

		cfg = &st->channels[channel].cfg;
		cfg->bipolar = fwnode_property_read_bool(child, "bipolar");

		ret = fwnode_property_read_u32(child, "adi,reference-select", &tmp);
		if (ret)
			cfg->refsel = AD7124_INT_REF;
		else
			cfg->refsel = tmp;

		cfg->buf_positive =
			fwnode_property_read_bool(child, "adi,buffered-positive");
		cfg->buf_negative =
			fwnode_property_read_bool(child, "adi,buffered-negative");

		chan[channel] = ad7124_channel_template;
		chan[channel].address = channel;
		chan[channel].scan_index = channel;
		chan[channel].channel = ain[0];
		chan[channel].channel2 = ain[1];
	}

	if (num_channels < AD7124_MAX_CHANNELS) {
		st->channels[num_channels] = (struct ad7124_channel) {
			.nr = num_channels,
			.ain = FIELD_PREP(AD7124_CHANNEL_AINP, AD7124_CHANNEL_AINx_TEMPSENSOR) |
				FIELD_PREP(AD7124_CHANNEL_AINM, AD7124_CHANNEL_AINx_AVSS),
			.cfg = {
				.bipolar = true,
			},
		};

		chan[num_channels] = (struct iio_chan_spec) {
			.type = IIO_TEMP,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET) |
				BIT(IIO_CHAN_INFO_SAMP_FREQ),
			.scan_type = {
				/*
				 * You might find it strange that a bipolar
				 * measurement yields an unsigned value, but
				 * this matches the device's manual.
				 */
				.sign = 'u',
				.realbits = 24,
				.storagebits = 32,
				.endianness = IIO_BE,
			},
			.address = num_channels,
			.scan_index = num_channels,
		};
	}

	return 0;
}

static int ad7124_setup(struct ad7124_state *st)
{
	struct device *dev = &st->sd.spi->dev;
	unsigned int power_mode, clk_sel;
	struct clk *mclk;
	int i, ret;

	/*
	 * Always use full power mode for max performance. If needed, the driver
	 * could be adapted to use a dynamic power mode based on the requested
	 * output data rate.
	 */
	power_mode = AD7124_ADC_CONTROL_POWER_MODE_FULL;

	/*
	 * This "mclk" business is needed for backwards compatibility with old
	 * devicetrees that specified a fake clock named "mclk" to select the
	 * power mode.
	 */
	mclk = devm_clk_get_optional_enabled(dev, "mclk");
	if (IS_ERR(mclk))
		return dev_err_probe(dev, PTR_ERR(mclk), "Failed to get mclk\n");

	if (mclk) {
		unsigned long mclk_hz;

		mclk_hz = clk_get_rate(mclk);
		if (!mclk_hz)
			return dev_err_probe(dev, -EINVAL,
					     "Failed to get mclk rate\n");

		/*
		 * This logic is a bit backwards, which is why it is only here
		 * for backwards compatibility. The driver should be able to set
		 * the power mode as it sees fit and the f_clk/mclk rate should
		 * be dynamic accordingly. But here, we are selecting a fixed
		 * power mode based on the given "mclk" rate.
		 */
		power_mode = ad7124_find_closest_match(ad7124_master_clk_freq_hz,
			ARRAY_SIZE(ad7124_master_clk_freq_hz), mclk_hz);

		if (mclk_hz != ad7124_master_clk_freq_hz[power_mode]) {
			ret = clk_set_rate(mclk, mclk_hz);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Failed to set mclk rate\n");
		}

		clk_sel = AD7124_ADC_CONTROL_CLK_SEL_INT;
		st->clk_hz = AD7124_INT_CLK_HZ;
	} else if (!device_property_present(dev, "clocks") &&
		   device_property_present(dev, "#clock-cells")) {
#ifdef CONFIG_COMMON_CLK
		struct clk_hw *clk_hw;

		const char *name __free(kfree) = kasprintf(GFP_KERNEL, "%pfwP-clk",
							   dev_fwnode(dev));
		if (!name)
			return -ENOMEM;

		clk_hw = devm_clk_hw_register_fixed_rate(dev, name, NULL, 0,
							 AD7124_INT_CLK_HZ);
		if (IS_ERR(clk_hw))
			return dev_err_probe(dev, PTR_ERR(clk_hw),
					     "Failed to register clock provider\n");

		ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
						  clk_hw);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to add clock provider\n");
#endif

		/*
		 * Treat the clock as always on. This way we don't have to deal
		 * with someone trying to enable/disable the clock while we are
		 * reading samples.
		 */
		clk_sel = AD7124_ADC_CONTROL_CLK_SEL_INT_OUT;
		st->clk_hz = AD7124_INT_CLK_HZ;
	} else {
		struct clk *clk;

		clk = devm_clk_get_optional_enabled(dev, NULL);
		if (IS_ERR(clk))
			return dev_err_probe(dev, PTR_ERR(clk),
					     "Failed to get external clock\n");

		if (clk) {
			unsigned long clk_hz;

			clk_hz = clk_get_rate(clk);
			if (!clk_hz)
				return dev_err_probe(dev, -EINVAL,
					"Failed to get external clock rate\n");

			/*
			 * The external clock may be 4x the nominal clock rate,
			 * in which case the ADC needs to be configured to
			 * divide it by 4. Using MEGA is a bit arbitrary, but
			 * the expected clock rates are either 614.4 kHz or
			 * 2.4576 MHz, so this should work.
			 */
			if (clk_hz > (1 * HZ_PER_MHZ)) {
				clk_sel = AD7124_ADC_CONTROL_CLK_SEL_EXT_DIV4;
				st->clk_hz = clk_hz / 4;
			} else {
				clk_sel = AD7124_ADC_CONTROL_CLK_SEL_EXT;
				st->clk_hz = clk_hz;
			}
		} else {
			clk_sel = AD7124_ADC_CONTROL_CLK_SEL_INT;
			st->clk_hz = AD7124_INT_CLK_HZ;
		}
	}

	st->adc_control &= ~AD7124_ADC_CONTROL_CLK_SEL;
	st->adc_control |= FIELD_PREP(AD7124_ADC_CONTROL_CLK_SEL, clk_sel);

	st->adc_control &= ~AD7124_ADC_CONTROL_POWER_MODE;
	st->adc_control |= FIELD_PREP(AD7124_ADC_CONTROL_POWER_MODE, power_mode);

	st->adc_control &= ~AD7124_ADC_CONTROL_MODE;
	st->adc_control |= FIELD_PREP(AD7124_ADC_CONTROL_MODE, AD_SD_MODE_IDLE);

	mutex_init(&st->cfgs_lock);
	INIT_KFIFO(st->live_cfgs_fifo);
	for (i = 0; i < st->num_channels; i++) {
		struct ad7124_channel_config *cfg = &st->channels[i].cfg;

		ret = ad7124_init_config_vref(st, cfg);
		if (ret < 0)
			return ret;

		/* Default filter type on the ADC after reset. */
		cfg->filter_type = AD7124_FILTER_TYPE_SINC4;

		/*
		 * 9.38 SPS is the minimum output data rate supported
		 * regardless of the selected power mode. Round it up to 10 and
		 * set all channels to this default value.
		 */
		cfg->requested_odr = 10;
		ad7124_set_channel_odr(st, i);
	}

	ad7124_disable_all(&st->sd);

	ret = ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, st->adc_control);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to setup CONTROL register\n");

	return ret;
}

static int __ad7124_calibrate_all(struct ad7124_state *st, struct iio_dev *indio_dev)
{
	struct device *dev = &st->sd.spi->dev;
	int ret, i;

	for (i = 0; i < st->num_channels; i++) {
		/*
		 * For calibration the OFFSET register should hold its reset default
		 * value. For the GAIN register there is no such requirement but
		 * for gain 1 it should hold the reset default value, too. So to
		 * simplify matters use the reset default value for both.
		 */
		st->channels[i].cfg.calibration_offset = 0x800000;
		st->channels[i].cfg.calibration_gain = st->gain_default;

		/*
		 * Only the main voltage input channels are important enough
		 * to be automatically calibrated here. For everything else,
		 * just use the default values set above.
		 */
		if (indio_dev->channels[i].type != IIO_VOLTAGE)
			continue;

		/*
		 * Full-scale calibration isn't supported at gain 1, so skip in
		 * that case. Note that untypically full-scale calibration has
		 * to happen before zero-scale calibration. This only applies to
		 * the internal calibration. For system calibration it's as
		 * usual: first zero-scale then full-scale calibration.
		 */
		if (st->channels[i].cfg.pga_bits > 0) {
			ret = ad_sd_calibrate(&st->sd, AD7124_ADC_CONTROL_MODE_INT_GAIN_CALIB, i);
			if (ret < 0)
				return ret;

			/*
			 * read out the resulting value of GAIN
			 * after full-scale calibration because the next
			 * ad_sd_calibrate() call overwrites this via
			 * ad_sigma_delta_set_channel() -> ad7124_set_channel()
			 * ... -> ad7124_enable_channel().
			 */
			ret = ad_sd_read_reg(&st->sd, AD7124_GAIN(st->channels[i].cfg.cfg_slot), 3,
					     &st->channels[i].cfg.calibration_gain);
			if (ret < 0)
				return ret;
		}

		ret = ad_sd_calibrate(&st->sd, AD7124_ADC_CONTROL_MODE_INT_OFFSET_CALIB, i);
		if (ret < 0)
			return ret;

		ret = ad_sd_read_reg(&st->sd, AD7124_OFFSET(st->channels[i].cfg.cfg_slot), 3,
				     &st->channels[i].cfg.calibration_offset);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "offset and gain for channel %d = 0x%x + 0x%x\n", i,
			st->channels[i].cfg.calibration_offset,
			st->channels[i].cfg.calibration_gain);
	}

	return 0;
}

static int ad7124_calibrate_all(struct ad7124_state *st, struct iio_dev *indio_dev)
{
	int ret;
	unsigned int adc_control = st->adc_control;

	/*
	 * Calibration isn't supported at full power, so speed down a bit.
	 * Setting .adc_control is enough here because the control register is
	 * written as part of ad_sd_calibrate() -> ad_sigma_delta_set_mode().
	 * The resulting calibration is then also valid for high-speed, so just
	 * restore adc_control afterwards.
	 */
	if (FIELD_GET(AD7124_ADC_CONTROL_POWER_MODE, adc_control) >= AD7124_FULL_POWER) {
		st->adc_control &= ~AD7124_ADC_CONTROL_POWER_MODE;
		st->adc_control |= FIELD_PREP(AD7124_ADC_CONTROL_POWER_MODE, AD7124_MID_POWER);
	}

	ret = __ad7124_calibrate_all(st, indio_dev);

	st->adc_control = adc_control;

	return ret;
}

static void ad7124_reg_disable(void *r)
{
	regulator_disable(r);
}

static int ad7124_probe(struct spi_device *spi)
{
	const struct ad7124_chip_info *info;
	struct device *dev = &spi->dev;
	struct ad7124_state *st;
	struct iio_dev *indio_dev;
	int i, ret;

	info = spi_get_device_match_data(spi);
	if (!info)
		return dev_err_probe(dev, -ENODEV, "Failed to get match data\n");

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->chip_info = info;

	indio_dev->name = st->chip_info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad7124_info;

	ret = ad_sd_init(&st->sd, indio_dev, spi, &ad7124_sigma_delta_info);
	if (ret < 0)
		return ret;

	ret = ad7124_parse_channel_config(indio_dev, &spi->dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(st->vref); i++) {
		if (i == AD7124_INT_REF)
			continue;

		st->vref[i] = devm_regulator_get_optional(&spi->dev,
						ad7124_ref_names[i]);
		if (PTR_ERR(st->vref[i]) == -ENODEV)
			continue;
		else if (IS_ERR(st->vref[i]))
			return PTR_ERR(st->vref[i]);

		ret = regulator_enable(st->vref[i]);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to enable regulator #%d\n", i);

		ret = devm_add_action_or_reset(&spi->dev, ad7124_reg_disable,
					       st->vref[i]);
		if (ret)
			return ret;
	}

	ret = ad7124_soft_reset(st);
	if (ret < 0)
		return ret;

	ret = ad7124_check_chip_id(st);
	if (ret)
		return ret;

	ret = ad7124_setup(st);
	if (ret < 0)
		return ret;

	ret = devm_ad_sd_setup_buffer_and_trigger(&spi->dev, indio_dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to setup triggers\n");

	ret = ad7124_calibrate_all(st, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to register iio device\n");

	return 0;
}

static const struct of_device_id ad7124_of_match[] = {
	{ .compatible = "adi,ad7124-4", .data = &ad7124_4_chip_info },
	{ .compatible = "adi,ad7124-8", .data = &ad7124_8_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7124_of_match);

static const struct spi_device_id ad71124_ids[] = {
	{ "ad7124-4", (kernel_ulong_t)&ad7124_4_chip_info },
	{ "ad7124-8", (kernel_ulong_t)&ad7124_8_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad71124_ids);

static struct spi_driver ad71124_driver = {
	.driver = {
		.name = "ad7124",
		.of_match_table = ad7124_of_match,
	},
	.probe = ad7124_probe,
	.id_table = ad71124_ids,
};
module_spi_driver(ad71124_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7124 SPI driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_AD_SIGMA_DELTA");
