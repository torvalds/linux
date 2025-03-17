// SPDX-License-Identifier: GPL-2.0+
/*
 * AD7124 SPI ADC driver
 *
 * Copyright 2018 Analog Devices Inc.
 */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

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
#define AD7124_ERROR_EN		0x07
#define AD7124_MCLK_COUNT		0x08
#define AD7124_CHANNEL(x)		(0x09 + (x))
#define AD7124_CONFIG(x)		(0x19 + (x))
#define AD7124_FILTER(x)		(0x21 + (x))
#define AD7124_OFFSET(x)		(0x29 + (x))
#define AD7124_GAIN(x)			(0x31 + (x))

/* AD7124_STATUS */
#define AD7124_STATUS_POR_FLAG_MSK	BIT(4)

/* AD7124_ADC_CONTROL */
#define AD7124_ADC_STATUS_EN_MSK	BIT(10)
#define AD7124_ADC_STATUS_EN(x)		FIELD_PREP(AD7124_ADC_STATUS_EN_MSK, x)
#define AD7124_ADC_CTRL_REF_EN_MSK	BIT(8)
#define AD7124_ADC_CTRL_REF_EN(x)	FIELD_PREP(AD7124_ADC_CTRL_REF_EN_MSK, x)
#define AD7124_ADC_CTRL_PWR_MSK	GENMASK(7, 6)
#define AD7124_ADC_CTRL_PWR(x)		FIELD_PREP(AD7124_ADC_CTRL_PWR_MSK, x)
#define AD7124_ADC_CTRL_MODE_MSK	GENMASK(5, 2)
#define AD7124_ADC_CTRL_MODE(x)	FIELD_PREP(AD7124_ADC_CTRL_MODE_MSK, x)

#define AD7124_MODE_CAL_INT_ZERO	0x5 /* Internal Zero-Scale Calibration */
#define AD7124_MODE_CAL_INT_FULL	0x6 /* Internal Full-Scale Calibration */
#define AD7124_MODE_CAL_SYS_ZERO	0x7 /* System Zero-Scale Calibration */
#define AD7124_MODE_CAL_SYS_FULL	0x8 /* System Full-Scale Calibration */

/* AD7124 ID */
#define AD7124_DEVICE_ID_MSK		GENMASK(7, 4)
#define AD7124_DEVICE_ID_GET(x)		FIELD_GET(AD7124_DEVICE_ID_MSK, x)
#define AD7124_SILICON_REV_MSK		GENMASK(3, 0)
#define AD7124_SILICON_REV_GET(x)	FIELD_GET(AD7124_SILICON_REV_MSK, x)

#define CHIPID_AD7124_4			0x0
#define CHIPID_AD7124_8			0x1

/* AD7124_CHANNEL_X */
#define AD7124_CHANNEL_EN_MSK		BIT(15)
#define AD7124_CHANNEL_EN(x)		FIELD_PREP(AD7124_CHANNEL_EN_MSK, x)
#define AD7124_CHANNEL_SETUP_MSK	GENMASK(14, 12)
#define AD7124_CHANNEL_SETUP(x)	FIELD_PREP(AD7124_CHANNEL_SETUP_MSK, x)
#define AD7124_CHANNEL_AINP_MSK	GENMASK(9, 5)
#define AD7124_CHANNEL_AINP(x)		FIELD_PREP(AD7124_CHANNEL_AINP_MSK, x)
#define AD7124_CHANNEL_AINM_MSK	GENMASK(4, 0)
#define AD7124_CHANNEL_AINM(x)		FIELD_PREP(AD7124_CHANNEL_AINM_MSK, x)

/* AD7124_CONFIG_X */
#define AD7124_CONFIG_BIPOLAR_MSK	BIT(11)
#define AD7124_CONFIG_BIPOLAR(x)	FIELD_PREP(AD7124_CONFIG_BIPOLAR_MSK, x)
#define AD7124_CONFIG_REF_SEL_MSK	GENMASK(4, 3)
#define AD7124_CONFIG_REF_SEL(x)	FIELD_PREP(AD7124_CONFIG_REF_SEL_MSK, x)
#define AD7124_CONFIG_PGA_MSK		GENMASK(2, 0)
#define AD7124_CONFIG_PGA(x)		FIELD_PREP(AD7124_CONFIG_PGA_MSK, x)
#define AD7124_CONFIG_IN_BUFF_MSK	GENMASK(6, 5)
#define AD7124_CONFIG_IN_BUFF(x)	FIELD_PREP(AD7124_CONFIG_IN_BUFF_MSK, x)

/* AD7124_FILTER_X */
#define AD7124_FILTER_FS_MSK		GENMASK(10, 0)
#define AD7124_FILTER_FS(x)		FIELD_PREP(AD7124_FILTER_FS_MSK, x)
#define AD7124_FILTER_TYPE_MSK		GENMASK(23, 21)
#define AD7124_FILTER_TYPE_SEL(x)	FIELD_PREP(AD7124_FILTER_TYPE_MSK, x)

#define AD7124_SINC3_FILTER 2
#define AD7124_SINC4_FILTER 0

#define AD7124_CONF_ADDR_OFFSET	20
#define AD7124_MAX_CONFIGS	8
#define AD7124_MAX_CHANNELS	16

/* AD7124 input sources */
#define AD7124_INPUT_TEMPSENSOR	16
#define AD7124_INPUT_AVSS	17

enum ad7124_ids {
	ID_AD7124_4,
	ID_AD7124_8,
};

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
	[AD7124_LOW_POWER] = 76800,
	[AD7124_MID_POWER] = 153600,
	[AD7124_FULL_POWER] = 614400,
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

struct ad7124_channel_config {
	bool live;
	unsigned int cfg_slot;
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
		unsigned int odr;
		unsigned int odr_sel_bits;
		unsigned int filter_type;
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
	struct clk *mclk;
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

static struct ad7124_chip_info ad7124_chip_info_tbl[] = {
	[ID_AD7124_4] = {
		.name = "ad7124-4",
		.chip_id = CHIPID_AD7124_4,
		.num_inputs = 8,
	},
	[ID_AD7124_8] = {
		.name = "ad7124-8",
		.chip_id = CHIPID_AD7124_8,
		.num_inputs = 16,
	},
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

	st->adc_control &= ~AD7124_ADC_CTRL_MODE_MSK;
	st->adc_control |= AD7124_ADC_CTRL_MODE(mode);

	return ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, st->adc_control);
}

static void ad7124_set_channel_odr(struct ad7124_state *st, unsigned int channel, unsigned int odr)
{
	unsigned int fclk, odr_sel_bits;

	fclk = clk_get_rate(st->mclk);
	/*
	 * FS[10:0] = fCLK / (fADC x 32) where:
	 * fADC is the output data rate
	 * fCLK is the master clock frequency
	 * FS[10:0] are the bits in the filter register
	 * FS[10:0] can have a value from 1 to 2047
	 */
	odr_sel_bits = DIV_ROUND_CLOSEST(fclk, odr * 32);
	if (odr_sel_bits < 1)
		odr_sel_bits = 1;
	else if (odr_sel_bits > 2047)
		odr_sel_bits = 2047;

	if (odr_sel_bits != st->channels[channel].cfg.odr_sel_bits)
		st->channels[channel].cfg.live = false;

	/* fADC = fCLK / (FS[10:0] x 32) */
	st->channels[channel].cfg.odr = DIV_ROUND_CLOSEST(fclk, odr_sel_bits * 32);
	st->channels[channel].cfg.odr_sel_bits = odr_sel_bits;
}

static int ad7124_get_3db_filter_freq(struct ad7124_state *st,
				      unsigned int channel)
{
	unsigned int fadc;

	fadc = st->channels[channel].cfg.odr;

	switch (st->channels[channel].cfg.filter_type) {
	case AD7124_SINC3_FILTER:
		return DIV_ROUND_CLOSEST(fadc * 272, 1000);
	case AD7124_SINC4_FILTER:
		return DIV_ROUND_CLOSEST(fadc * 230, 1000);
	default:
		return -EINVAL;
	}
}

static void ad7124_set_3db_filter_freq(struct ad7124_state *st, unsigned int channel,
				       unsigned int freq)
{
	unsigned int sinc4_3db_odr;
	unsigned int sinc3_3db_odr;
	unsigned int new_filter;
	unsigned int new_odr;

	sinc4_3db_odr = DIV_ROUND_CLOSEST(freq * 1000, 230);
	sinc3_3db_odr = DIV_ROUND_CLOSEST(freq * 1000, 262);

	if (sinc4_3db_odr > sinc3_3db_odr) {
		new_filter = AD7124_SINC3_FILTER;
		new_odr = sinc4_3db_odr;
	} else {
		new_filter = AD7124_SINC4_FILTER;
		new_odr = sinc3_3db_odr;
	}

	if (new_odr != st->channels[channel].cfg.odr)
		st->channels[channel].cfg.live = false;

	st->channels[channel].cfg.filter_type = new_filter;
	st->channels[channel].cfg.odr = new_odr;
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
				     unsigned int odr;
				     unsigned int odr_sel_bits;
				     unsigned int filter_type;
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
		    cfg->odr == cfg_aux->odr &&
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
		st->adc_control &= ~AD7124_ADC_CTRL_REF_EN_MSK;
		st->adc_control |= AD7124_ADC_CTRL_REF_EN(1);
		return 0;
	default:
		return dev_err_probe(dev, -EINVAL, "Invalid reference %d\n", refsel);
	}
}

static int ad7124_write_config(struct ad7124_state *st, struct ad7124_channel_config *cfg,
			       unsigned int cfg_slot)
{
	unsigned int tmp;
	unsigned int val;
	int ret;

	cfg->cfg_slot = cfg_slot;

	ret = ad_sd_write_reg(&st->sd, AD7124_OFFSET(cfg->cfg_slot), 3, cfg->calibration_offset);
	if (ret)
		return ret;

	ret = ad_sd_write_reg(&st->sd, AD7124_GAIN(cfg->cfg_slot), 3, cfg->calibration_gain);
	if (ret)
		return ret;

	tmp = (cfg->buf_positive << 1) + cfg->buf_negative;
	val = AD7124_CONFIG_BIPOLAR(cfg->bipolar) | AD7124_CONFIG_REF_SEL(cfg->refsel) |
	      AD7124_CONFIG_IN_BUFF(tmp) | AD7124_CONFIG_PGA(cfg->pga_bits);

	ret = ad_sd_write_reg(&st->sd, AD7124_CONFIG(cfg->cfg_slot), 2, val);
	if (ret < 0)
		return ret;

	tmp = AD7124_FILTER_TYPE_SEL(cfg->filter_type) |
	      AD7124_FILTER_FS(cfg->odr_sel_bits);
	return ad7124_spi_write_mask(st, AD7124_FILTER(cfg->cfg_slot),
				     AD7124_FILTER_TYPE_MSK | AD7124_FILTER_FS_MSK,
				     tmp, 3);
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
			      AD7124_CHANNEL_SETUP(ch->cfg.cfg_slot) | AD7124_CHANNEL_EN(1));
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

	adc_control &= ~AD7124_ADC_STATUS_EN_MSK;
	adc_control |= AD7124_ADC_STATUS_EN(append);

	ret = ad_sd_write_reg(&st->sd, AD7124_ADC_CONTROL, 2, adc_control);
	if (ret < 0)
		return ret;

	st->adc_control = adc_control;

	return 0;
}

static int ad7124_disable_one(struct ad_sigma_delta *sd, unsigned int chan)
{
	struct ad7124_state *st = container_of(sd, struct ad7124_state, sd);

	/* The relevant thing here is that AD7124_CHANNEL_EN_MSK is cleared. */
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

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&st->cfgs_lock);
		*val = st->channels[chan->address].cfg.odr;
		mutex_unlock(&st->cfgs_lock);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		mutex_lock(&st->cfgs_lock);
		*val = ad7124_get_3db_filter_freq(st, chan->scan_index);
		mutex_unlock(&st->cfgs_lock);

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad7124_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	unsigned int res, gain, full_scale, vref;
	int ret = 0;

	mutex_lock(&st->cfgs_lock);

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2 != 0 || val == 0) {
			ret = -EINVAL;
			break;
		}

		ad7124_set_channel_odr(st, chan->address, val);
		break;
	case IIO_CHAN_INFO_SCALE:
		if (val != 0) {
			ret = -EINVAL;
			break;
		}

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
		break;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		if (val2 != 0) {
			ret = -EINVAL;
			break;
		}

		ad7124_set_3db_filter_freq(st, chan->address, val);
		break;
	default:
		ret =  -EINVAL;
	}

	mutex_unlock(&st->cfgs_lock);
	return ret;
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

static IIO_CONST_ATTR(in_voltage_scale_available,
	"0.000001164 0.000002328 0.000004656 0.000009313 0.000018626 0.000037252 0.000074505 0.000149011 0.000298023");

static struct attribute *ad7124_attributes[] = {
	&iio_const_attr_in_voltage_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7124_attrs_group = {
	.attrs = ad7124_attributes,
};

static int ad7124_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad7124_state *st = iio_priv(indio_dev);
	bool bit_set;
	int ret;
	int i;

	mutex_lock(&st->cfgs_lock);
	for (i = 0; i < st->num_channels; i++) {
		bit_set = test_bit(i, scan_mask);
		if (bit_set)
			ret = __ad7124_set_channel(&st->sd, i);
		else
			ret = ad7124_spi_write_mask(st, AD7124_CHANNEL(i), AD7124_CHANNEL_EN_MSK,
						    0, 2);
		if (ret < 0) {
			mutex_unlock(&st->cfgs_lock);

			return ret;
		}
	}

	mutex_unlock(&st->cfgs_lock);

	return 0;
}

static const struct iio_info ad7124_info = {
	.read_raw = ad7124_read_raw,
	.write_raw = ad7124_write_raw,
	.debugfs_reg_access = &ad7124_reg_access,
	.validate_trigger = ad_sd_validate_trigger,
	.update_scan_mode = ad7124_update_scan_mode,
	.attrs = &ad7124_attrs_group,
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

		if (!(readval & AD7124_STATUS_POR_FLAG_MSK))
			break;

		/* The AD7124 requires typically 2ms to power up and settle */
		usleep_range(100, 2000);
	} while (--timeout);

	if (readval & AD7124_STATUS_POR_FLAG_MSK)
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

	chip_id = AD7124_DEVICE_ID_GET(readval);
	silicon_rev = AD7124_SILICON_REV_GET(readval);

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
	struct ad7124_channel *ch = &st->channels[chan->channel];
	int ret;

	if (ch->syscalib_mode == AD7124_SYSCALIB_ZERO_SCALE) {
		ch->cfg.calibration_offset = 0x800000;

		ret = ad_sd_calibrate(&st->sd, AD7124_MODE_CAL_SYS_ZERO,
				      chan->address);
		if (ret < 0)
			return ret;

		ret = ad_sd_read_reg(&st->sd, AD7124_OFFSET(ch->cfg.cfg_slot), 3,
				     &ch->cfg.calibration_offset);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "offset for channel %d after zero-scale calibration: 0x%x\n",
			chan->channel, ch->cfg.calibration_offset);
	} else {
		ch->cfg.calibration_gain = st->gain_default;

		ret = ad_sd_calibrate(&st->sd, AD7124_MODE_CAL_SYS_FULL,
				      chan->address);
		if (ret < 0)
			return ret;

		ret = ad_sd_read_reg(&st->sd, AD7124_GAIN(ch->cfg.cfg_slot), 3,
				     &ch->cfg.calibration_gain);
		if (ret < 0)
			return ret;

		dev_dbg(dev, "gain for channel %d after full-scale calibration: 0x%x\n",
			chan->channel, ch->cfg.calibration_gain);
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

	st->channels[chan->channel].syscalib_mode = mode;

	return 0;
}

static int ad7124_get_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan)
{
	struct ad7124_state *st = iio_priv(indio_dev);

	return st->channels[chan->channel].syscalib_mode;
}

static const struct iio_enum ad7124_syscalib_mode_enum = {
	.items = ad7124_syscalib_modes,
	.num_items = ARRAY_SIZE(ad7124_syscalib_modes),
	.set = ad7124_set_syscalib_mode,
	.get = ad7124_get_syscalib_mode
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

	return ain <= FIELD_MAX(AD7124_CHANNEL_AINM_MSK);
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
		st->channels[channel].ain = AD7124_CHANNEL_AINP(ain[0]) |
						  AD7124_CHANNEL_AINM(ain[1]);

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
			.ain = AD7124_CHANNEL_AINP(AD7124_INPUT_TEMPSENSOR) |
				AD7124_CHANNEL_AINM(AD7124_INPUT_AVSS),
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
	unsigned int fclk, power_mode;
	int i, ret;

	fclk = clk_get_rate(st->mclk);
	if (!fclk)
		return dev_err_probe(dev, -EINVAL, "Failed to get mclk rate\n");

	/* The power mode changes the master clock frequency */
	power_mode = ad7124_find_closest_match(ad7124_master_clk_freq_hz,
					ARRAY_SIZE(ad7124_master_clk_freq_hz),
					fclk);
	if (fclk != ad7124_master_clk_freq_hz[power_mode]) {
		ret = clk_set_rate(st->mclk, fclk);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to set mclk rate\n");
	}

	/* Set the power mode */
	st->adc_control &= ~AD7124_ADC_CTRL_PWR_MSK;
	st->adc_control |= AD7124_ADC_CTRL_PWR(power_mode);

	st->adc_control &= ~AD7124_ADC_CTRL_MODE_MSK;
	st->adc_control |= AD7124_ADC_CTRL_MODE(AD_SD_MODE_IDLE);

	mutex_init(&st->cfgs_lock);
	INIT_KFIFO(st->live_cfgs_fifo);
	for (i = 0; i < st->num_channels; i++) {

		ret = ad7124_init_config_vref(st, &st->channels[i].cfg);
		if (ret < 0)
			return ret;

		/*
		 * 9.38 SPS is the minimum output data rate supported
		 * regardless of the selected power mode. Round it up to 10 and
		 * set all channels to this default value.
		 */
		ad7124_set_channel_odr(st, i, 10);
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

		if (indio_dev->channels[i].type != IIO_VOLTAGE)
			continue;

		/*
		 * For calibration the OFFSET register should hold its reset default
		 * value. For the GAIN register there is no such requirement but
		 * for gain 1 it should hold the reset default value, too. So to
		 * simplify matters use the reset default value for both.
		 */
		st->channels[i].cfg.calibration_offset = 0x800000;
		st->channels[i].cfg.calibration_gain = st->gain_default;

		/*
		 * Full-scale calibration isn't supported at gain 1, so skip in
		 * that case. Note that untypically full-scale calibration has
		 * to happen before zero-scale calibration. This only applies to
		 * the internal calibration. For system calibration it's as
		 * usual: first zero-scale then full-scale calibration.
		 */
		if (st->channels[i].cfg.pga_bits > 0) {
			ret = ad_sd_calibrate(&st->sd, AD7124_MODE_CAL_INT_FULL, i);
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

		ret = ad_sd_calibrate(&st->sd, AD7124_MODE_CAL_INT_ZERO, i);
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
	if (FIELD_GET(AD7124_ADC_CTRL_PWR_MSK, adc_control) >= AD7124_FULL_POWER) {
		st->adc_control &= ~AD7124_ADC_CTRL_PWR_MSK;
		st->adc_control |= AD7124_ADC_CTRL_PWR(AD7124_MID_POWER);
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
			return dev_err_probe(dev, ret, "Failed to register disable handler for regulator #%d\n", i);
	}

	st->mclk = devm_clk_get_enabled(&spi->dev, "mclk");
	if (IS_ERR(st->mclk))
		return dev_err_probe(dev, PTR_ERR(st->mclk), "Failed to get mclk\n");

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
	{ .compatible = "adi,ad7124-4",
		.data = &ad7124_chip_info_tbl[ID_AD7124_4], },
	{ .compatible = "adi,ad7124-8",
		.data = &ad7124_chip_info_tbl[ID_AD7124_8], },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7124_of_match);

static const struct spi_device_id ad71124_ids[] = {
	{ "ad7124-4", (kernel_ulong_t)&ad7124_chip_info_tbl[ID_AD7124_4] },
	{ "ad7124-8", (kernel_ulong_t)&ad7124_chip_info_tbl[ID_AD7124_8] },
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
