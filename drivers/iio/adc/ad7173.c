// SPDX-License-Identifier: GPL-2.0+
/*
 * AD717x and AD411x family SPI ADC driver
 *
 * Supported devices:
 *  AD4111/AD4112/AD4113/AD4114/AD4115/AD4116
 *  AD7172-2/AD7172-4/AD7173-8/AD7175-2
 *  AD7175-8/AD7176-2/AD7177-2
 *
 * Copyright (C) 2015, 2024 Analog Devices, Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/container_of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/units.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/iio/adc/ad_sigma_delta.h>

#define AD7173_REG_COMMS		0x00
#define AD7173_REG_ADC_MODE		0x01
#define AD7173_REG_INTERFACE_MODE	0x02
#define AD7173_REG_CRC			0x03
#define AD7173_REG_DATA			0x04
#define AD7173_REG_GPIO			0x06
#define AD7173_REG_ID			0x07
#define AD7173_REG_CH(x)		(0x10 + (x))
#define AD7173_REG_SETUP(x)		(0x20 + (x))
#define AD7173_REG_FILTER(x)		(0x28 + (x))
#define AD7173_REG_OFFSET(x)		(0x30 + (x))
#define AD7173_REG_GAIN(x)		(0x38 + (x))

#define AD7173_RESET_LENGTH		BITS_TO_BYTES(64)

#define AD7173_CH_ENABLE		BIT(15)
#define AD7173_CH_SETUP_SEL_MASK	GENMASK(14, 12)
#define AD7173_CH_SETUP_AINPOS_MASK	GENMASK(9, 5)
#define AD7173_CH_SETUP_AINNEG_MASK	GENMASK(4, 0)

#define AD7173_NO_AINS_PER_CHANNEL	2
#define AD7173_CH_ADDRESS(pos, neg) \
	(FIELD_PREP(AD7173_CH_SETUP_AINPOS_MASK, pos) | \
	 FIELD_PREP(AD7173_CH_SETUP_AINNEG_MASK, neg))
#define AD7173_AIN_TEMP_POS	17
#define AD7173_AIN_TEMP_NEG	18
#define AD7173_AIN_POW_MON_POS	19
#define AD7173_AIN_POW_MON_NEG	20
#define AD7173_AIN_REF_POS	21
#define AD7173_AIN_REF_NEG	22

#define AD7173_IS_REF_INPUT(x)		((x) == AD7173_AIN_REF_POS || \
					(x) == AD7173_AIN_REF_NEG)

#define AD7172_2_ID			0x00d0
#define AD7176_ID			0x0c90
#define AD7175_ID			0x0cd0
#define AD7175_2_ID			0x0cd0
#define AD7172_4_ID			0x2050
#define AD7173_ID			0x30d0
#define AD4111_ID			AD7173_ID
#define AD4112_ID			AD7173_ID
#define AD4114_ID			AD7173_ID
#define AD4113_ID			0x31d0
#define AD4116_ID			0x34d0
#define AD4115_ID			0x38d0
#define AD7175_8_ID			0x3cd0
#define AD7177_ID			0x4fd0
#define AD7173_ID_MASK			GENMASK(15, 4)

#define AD7173_ADC_MODE_REF_EN		BIT(15)
#define AD7173_ADC_MODE_SING_CYC	BIT(13)
#define AD7173_ADC_MODE_MODE_MASK	GENMASK(6, 4)
#define AD7173_ADC_MODE_CLOCKSEL_MASK	GENMASK(3, 2)
#define AD7173_ADC_MODE_CLOCKSEL_INT		0x0
#define AD7173_ADC_MODE_CLOCKSEL_INT_OUTPUT	0x1
#define AD7173_ADC_MODE_CLOCKSEL_EXT		0x2
#define AD7173_ADC_MODE_CLOCKSEL_XTAL		0x3

#define AD7173_GPIO_PDSW	BIT(14)
#define AD7173_GPIO_OP_EN2_3	BIT(13)
#define AD4111_GPIO_GP_OW_EN	BIT(12)
#define AD7173_GPIO_MUX_IO	BIT(12)
#define AD7173_GPIO_SYNC_EN	BIT(11)
#define AD7173_GPIO_ERR_EN	BIT(10)
#define AD7173_GPIO_ERR_DAT	BIT(9)
#define AD7173_GPIO_GP_DATA3	BIT(7)
#define AD7173_GPIO_GP_DATA2	BIT(6)
#define AD7173_GPIO_IP_EN1	BIT(5)
#define AD7173_GPIO_IP_EN0	BIT(4)
#define AD7173_GPIO_OP_EN1	BIT(3)
#define AD7173_GPIO_OP_EN0	BIT(2)
#define AD7173_GPIO_GP_DATA1	BIT(1)
#define AD7173_GPIO_GP_DATA0	BIT(0)

#define AD7173_GPO12_DATA(x)	BIT((x) + 0)
#define AD7173_GPO23_DATA(x)	BIT((x) + 4)
#define AD4111_GPO01_DATA(x)	BIT((x) + 6)
#define AD7173_GPO_DATA(x)	((x) < 2 ? AD7173_GPO12_DATA(x) : AD7173_GPO23_DATA(x))

#define AD7173_INTERFACE_DATA_STAT	BIT(6)
#define AD7173_INTERFACE_DATA_STAT_EN(x) \
	FIELD_PREP(AD7173_INTERFACE_DATA_STAT, x)

#define AD7173_SETUP_BIPOLAR		BIT(12)
#define AD7173_SETUP_AREF_BUF_MASK	GENMASK(11, 10)
#define AD7173_SETUP_AIN_BUF_MASK	GENMASK(9, 8)

#define AD7173_SETUP_REF_SEL_MASK	GENMASK(5, 4)
#define AD7173_SETUP_REF_SEL_AVDD1_AVSS	0x3
#define AD7173_SETUP_REF_SEL_INT_REF	0x2
#define AD7173_SETUP_REF_SEL_EXT_REF2	0x1
#define AD7173_SETUP_REF_SEL_EXT_REF	0x0
#define AD7173_VOLTAGE_INT_REF_uV	2500000
#define AD7173_TEMP_SENSIIVITY_uV_per_C	477
#define AD7177_ODR_START_VALUE		0x07
#define AD4111_SHUNT_RESISTOR_OHM	50
#define AD4111_DIVIDER_RATIO		10
#define AD4111_CURRENT_CHAN_CUTOFF	16
#define AD4111_VINCOM_INPUT		0x10

/* pin <  num_voltage_in is a normal voltage input */
/* pin >= num_voltage_in_div is a voltage input without a divider */
#define AD4111_IS_VINCOM_MISMATCH(pin1, pin2) ((pin1) == AD4111_VINCOM_INPUT && \
					       (pin2) < st->info->num_voltage_in && \
					       (pin2) >= st->info->num_voltage_in_div)

#define AD7173_FILTER_ODR0_MASK		GENMASK(5, 0)
#define AD7173_MAX_CONFIGS		8
#define AD4111_OW_DET_THRSH_MV		300

#define AD7173_MODE_CAL_INT_ZERO		0x4 /* Internal Zero-Scale Calibration */
#define AD7173_MODE_CAL_INT_FULL		0x5 /* Internal Full-Scale Calibration */
#define AD7173_MODE_CAL_SYS_ZERO		0x6 /* System Zero-Scale Calibration */
#define AD7173_MODE_CAL_SYS_FULL		0x7 /* System Full-Scale Calibration */

struct ad7173_device_info {
	const unsigned int *sinc5_data_rates;
	unsigned int num_sinc5_data_rates;
	unsigned int odr_start_value;
	/*
	 * AD4116 has both inputs with a voltage divider and without.
	 * These inputs cannot be mixed in the channel configuration.
	 * Does not include the VINCOM input.
	 */
	unsigned int num_voltage_in_div;
	unsigned int num_channels;
	unsigned int num_configs;
	unsigned int num_voltage_in;
	unsigned int clock;
	unsigned int id;
	char *name;
	const struct ad_sigma_delta_info *sd_info;
	bool has_current_inputs;
	bool has_vincom_input;
	bool has_temp;
	/* ((AVDD1 − AVSS)/5) */
	bool has_pow_supply_monitoring;
	bool data_reg_only_16bit;
	bool has_input_buf;
	bool has_int_ref;
	bool has_ref2;
	bool has_internal_fs_calibration;
	bool has_openwire_det;
	bool higher_gpio_bits;
	u8 num_gpios;
};

struct ad7173_channel_config {
	/* Openwire detection threshold */
	unsigned int openwire_thrsh_raw;
	int openwire_comp_chan;
	u8 cfg_slot;
	bool live;

	/*
	 * Following fields are used to compare equality. If you
	 * make adaptations in it, you most likely also have to adapt
	 * ad7173_find_live_config(), too.
	 */
	struct_group(config_props,
		bool bipolar;
		bool input_buf;
		u8 odr;
		u8 ref_sel;
	);
};

struct ad7173_channel {
	unsigned int ain;
	struct ad7173_channel_config cfg;
	u8 syscalib_mode;
	bool openwire_det_en;
};

struct ad7173_state {
	struct ad_sigma_delta sd;
	const struct ad7173_device_info *info;
	struct ad7173_channel *channels;
	struct regulator_bulk_data regulators[3];
	unsigned int adc_mode;
	unsigned int interface_mode;
	unsigned int num_channels;
	struct ida cfg_slots_status;
	unsigned long long config_usage_counter;
	unsigned long long *config_cnts;
	struct clk_hw int_clk_hw;
	struct regmap *reg_gpiocon_regmap;
	struct gpio_regmap *gpio_regmap;
};

static unsigned int ad4115_sinc5_data_rates[] = {
	24845000, 24845000, 20725000, 20725000,	/*  0-3  */
	15564000, 13841000, 10390000, 10390000,	/*  4-7  */
	4994000,  2499000,  1000000,  500000,	/*  8-11 */
	395500,   200000,   100000,   59890,	/* 12-15 */
	49920,    20000,    16660,    10000,	/* 16-19 */
	5000,	  2500,     2500,		/* 20-22 */
};

static unsigned int ad4116_sinc5_data_rates[] = {
	12422360, 12422360, 12422360, 12422360,	/*  0-3  */
	10362690, 10362690, 7782100,  6290530,	/*  4-7  */
	5194800,  2496900,  1007600,  499900,	/*  8-11 */
	390600,	  200300,   100000,   59750,	/* 12-15 */
	49840,	  20000,    16650,    10000,	/* 16-19 */
	5000,	  2500,	    1250,		/* 20-22 */
};

static const unsigned int ad7173_sinc5_data_rates[] = {
	6211000, 6211000, 6211000, 6211000, 6211000, 6211000, 5181000, 4444000,	/*  0-7  */
	3115000, 2597000, 1007000, 503800,  381000,  200300,  100500,  59520,	/*  8-15 */
	49680,	 20010,	  16333,   10000,   5000,    2500,    1250,		/* 16-22 */
};

static const unsigned int ad7175_sinc5_data_rates[] = {
	50000000, 41667000, 31250000, 27778000,	/*  0-3  */
	20833000, 17857000, 12500000, 10000000,	/*  4-7  */
	5000000,  2500000,  1000000,  500000,	/*  8-11 */
	397500,   200000,   100000,   59920,	/* 12-15 */
	49960,    20000,    16666,    10000,	/* 16-19 */
	5000,					/* 20    */
};

static unsigned int ad4111_current_channel_config[] = {
	/* Ain sel: pos        neg    */
	0x1E8, /* 15:IIN0+    8:IIN0− */
	0x1C9, /* 14:IIN1+    9:IIN1− */
	0x1AA, /* 13:IIN2+   10:IIN2− */
	0x18B, /* 12:IIN3+   11:IIN3− */
};

static const char *const ad7173_ref_sel_str[] = {
	[AD7173_SETUP_REF_SEL_EXT_REF]    = "vref",
	[AD7173_SETUP_REF_SEL_EXT_REF2]   = "vref2",
	[AD7173_SETUP_REF_SEL_INT_REF]    = "refout-avss",
	[AD7173_SETUP_REF_SEL_AVDD1_AVSS] = "avdd",
};

static const char *const ad7173_clk_sel[] = {
	"ext-clk", "xtal"
};

static const struct regmap_range ad7173_range_gpio[] = {
	regmap_reg_range(AD7173_REG_GPIO, AD7173_REG_GPIO),
};

static const struct regmap_access_table ad7173_access_table = {
	.yes_ranges = ad7173_range_gpio,
	.n_yes_ranges = ARRAY_SIZE(ad7173_range_gpio),
};

static const struct regmap_config ad7173_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.rd_table = &ad7173_access_table,
	.wr_table = &ad7173_access_table,
	.read_flag_mask = BIT(6),
};

enum {
	AD7173_SYSCALIB_ZERO_SCALE,
	AD7173_SYSCALIB_FULL_SCALE,
};

static const char * const ad7173_syscalib_modes[] = {
	[AD7173_SYSCALIB_ZERO_SCALE] = "zero_scale",
	[AD7173_SYSCALIB_FULL_SCALE] = "full_scale",
};

static int ad7173_set_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    unsigned int mode)
{
	struct ad7173_state *st = iio_priv(indio_dev);

	st->channels[chan->address].syscalib_mode = mode;

	return 0;
}

static int ad7173_get_syscalib_mode(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan)
{
	struct ad7173_state *st = iio_priv(indio_dev);

	return st->channels[chan->address].syscalib_mode;
}

static ssize_t ad7173_write_syscalib(struct iio_dev *indio_dev,
				     uintptr_t private,
				     const struct iio_chan_spec *chan,
				     const char *buf, size_t len)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	bool sys_calib;
	int ret, mode;

	ret = kstrtobool(buf, &sys_calib);
	if (ret)
		return ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	mode = st->channels[chan->address].syscalib_mode;
	if (sys_calib) {
		if (mode == AD7173_SYSCALIB_ZERO_SCALE)
			ret = ad_sd_calibrate(&st->sd, AD7173_MODE_CAL_SYS_ZERO,
					      chan->address);
		else
			ret = ad_sd_calibrate(&st->sd, AD7173_MODE_CAL_SYS_FULL,
					      chan->address);
	}

	iio_device_release_direct(indio_dev);

	return ret ? : len;
}

static const struct iio_enum ad7173_syscalib_mode_enum = {
	.items = ad7173_syscalib_modes,
	.num_items = ARRAY_SIZE(ad7173_syscalib_modes),
	.set = ad7173_set_syscalib_mode,
	.get = ad7173_get_syscalib_mode
};

static const struct iio_chan_spec_ext_info ad7173_calibsys_ext_info[] = {
	{
		.name = "sys_calibration",
		.write = ad7173_write_syscalib,
		.shared = IIO_SEPARATE,
	},
	IIO_ENUM("sys_calibration_mode", IIO_SEPARATE,
		 &ad7173_syscalib_mode_enum),
	IIO_ENUM_AVAILABLE("sys_calibration_mode", IIO_SHARED_BY_TYPE,
			   &ad7173_syscalib_mode_enum),
	{ }
};

static int ad7173_calibrate_all(struct ad7173_state *st, struct iio_dev *indio_dev)
{
	int ret;
	int i;

	for (i = 0; i < st->num_channels; i++) {
		if (indio_dev->channels[i].type != IIO_VOLTAGE)
			continue;

		ret = ad_sd_calibrate(&st->sd, AD7173_MODE_CAL_INT_ZERO, i);
		if (ret < 0)
			return ret;

		if (st->info->has_internal_fs_calibration) {
			ret = ad_sd_calibrate(&st->sd, AD7173_MODE_CAL_INT_FULL, i);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

/*
 * Associative array of channel pairs for open wire detection
 * The array is indexed by ain and gives the associated channel pair
 * to perform the open wire detection with
 * the channel pair [0] is for non differential and pair [1]
 * is for differential inputs
 */
static int openwire_ain_to_channel_pair[][2][2] = {
/*	AIN      Single      Differential */
	[0] = { { 0, 15 },  { 1, 2 }   },
	[1] = { { 1, 2 },   { 2, 1 }   },
	[2] = { { 3, 4 },   { 5, 6 }   },
	[3] = { { 5, 6 },   { 6, 5 }   },
	[4] = { { 7, 8 },   { 9, 10 }  },
	[5] = { { 9, 10 },  { 10, 9 }  },
	[6] = { { 11, 12 }, { 13, 14 } },
	[7] = { { 13, 14 }, { 14, 13 } },
};

/*
 * Openwire detection on ad4111 works by running the same input measurement
 * on two different channels and compare if the difference between the two
 * measurements exceeds a certain value (typical 300mV)
 */
static int ad4111_openwire_event(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct ad7173_channel *adchan = &st->channels[chan->address];
	struct ad7173_channel_config *cfg = &adchan->cfg;
	int ret, val1, val2;

	ret = regmap_set_bits(st->reg_gpiocon_regmap, AD7173_REG_GPIO,
			      AD4111_GPIO_GP_OW_EN);
	if (ret)
		return ret;

	adchan->cfg.openwire_comp_chan =
		openwire_ain_to_channel_pair[chan->channel][chan->differential][0];

	ret = ad_sigma_delta_single_conversion(indio_dev, chan, &val1);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			"Error running ad_sigma_delta single conversion: %d", ret);
		goto out;
	}

	adchan->cfg.openwire_comp_chan =
		openwire_ain_to_channel_pair[chan->channel][chan->differential][1];

	ret = ad_sigma_delta_single_conversion(indio_dev, chan, &val2);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			"Error running ad_sigma_delta single conversion: %d", ret);
		goto out;
	}

	if (abs(val1 - val2) > cfg->openwire_thrsh_raw)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, chan->address,
						    IIO_EV_TYPE_FAULT, IIO_EV_DIR_FAULT_OPENWIRE),
			       iio_get_time_ns(indio_dev));

out:
	adchan->cfg.openwire_comp_chan = -1;
	regmap_clear_bits(st->reg_gpiocon_regmap, AD7173_REG_GPIO,
			  AD4111_GPIO_GP_OW_EN);
	return ret;
}

static int ad7173_mask_xlate(struct gpio_regmap *gpio, unsigned int base,
			     unsigned int offset, unsigned int *reg,
			     unsigned int *mask)
{
	*mask = AD7173_GPO_DATA(offset);
	*reg = base;
	return 0;
}

static int ad4111_mask_xlate(struct gpio_regmap *gpio, unsigned int base,
			     unsigned int offset, unsigned int *reg,
			     unsigned int *mask)
{
	*mask = AD4111_GPO01_DATA(offset);
	*reg = base;
	return 0;
}

static void ad7173_gpio_disable(void *data)
{
	struct ad7173_state *st = data;
	unsigned int mask;

	mask = AD7173_GPIO_OP_EN0 | AD7173_GPIO_OP_EN1 | AD7173_GPIO_OP_EN2_3;
	regmap_update_bits(st->reg_gpiocon_regmap, AD7173_REG_GPIO, mask, ~mask);
}

static int ad7173_gpio_init(struct ad7173_state *st)
{
	struct gpio_regmap_config gpio_regmap = {};
	struct device *dev = &st->sd.spi->dev;
	unsigned int mask;
	int ret;

	st->reg_gpiocon_regmap = devm_regmap_init_spi(st->sd.spi, &ad7173_regmap_config);
	ret = PTR_ERR_OR_ZERO(st->reg_gpiocon_regmap);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to init regmap\n");

	mask = AD7173_GPIO_OP_EN0 | AD7173_GPIO_OP_EN1 | AD7173_GPIO_OP_EN2_3;
	regmap_update_bits(st->reg_gpiocon_regmap, AD7173_REG_GPIO, mask, mask);

	ret = devm_add_action_or_reset(dev, ad7173_gpio_disable, st);
	if (ret)
		return ret;

	gpio_regmap.parent = dev;
	gpio_regmap.regmap = st->reg_gpiocon_regmap;
	gpio_regmap.ngpio = st->info->num_gpios;
	gpio_regmap.reg_set_base = AD7173_REG_GPIO;
	if (st->info->higher_gpio_bits)
		gpio_regmap.reg_mask_xlate = ad4111_mask_xlate;
	else
		gpio_regmap.reg_mask_xlate = ad7173_mask_xlate;

	st->gpio_regmap = devm_gpio_regmap_register(dev, &gpio_regmap);
	ret = PTR_ERR_OR_ZERO(st->gpio_regmap);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to init gpio-regmap\n");

	return 0;
}

static struct ad7173_state *ad_sigma_delta_to_ad7173(struct ad_sigma_delta *sd)
{
	return container_of(sd, struct ad7173_state, sd);
}

static struct ad7173_state *clk_hw_to_ad7173(struct clk_hw *hw)
{
	return container_of(hw, struct ad7173_state, int_clk_hw);
}

static void ad7173_ida_destroy(void *data)
{
	struct ad7173_state *st = data;

	ida_destroy(&st->cfg_slots_status);
}

static void ad7173_reset_usage_cnts(struct ad7173_state *st)
{
	memset64(st->config_cnts, 0, st->info->num_configs);
	st->config_usage_counter = 0;
}

static struct ad7173_channel_config *
ad7173_find_live_config(struct ad7173_state *st, struct ad7173_channel_config *cfg)
{
	struct ad7173_channel_config *cfg_aux;
	int i;

	/*
	 * This is just to make sure that the comparison is adapted after
	 * struct ad7173_channel_config was changed.
	 */
	static_assert(sizeof_field(struct ad7173_channel_config, config_props) ==
		      sizeof(struct {
				     bool bipolar;
				     bool input_buf;
				     u8 odr;
				     u8 ref_sel;
			     }));

	for (i = 0; i < st->num_channels; i++) {
		cfg_aux = &st->channels[i].cfg;

		if (cfg_aux->live &&
		    cfg->bipolar == cfg_aux->bipolar &&
		    cfg->input_buf == cfg_aux->input_buf &&
		    cfg->odr == cfg_aux->odr &&
		    cfg->ref_sel == cfg_aux->ref_sel)
			return cfg_aux;
	}
	return NULL;
}

/* Could be replaced with a generic LRU implementation */
static int ad7173_free_config_slot_lru(struct ad7173_state *st)
{
	int i, lru_position = 0;

	for (i = 1; i < st->info->num_configs; i++)
		if (st->config_cnts[i] < st->config_cnts[lru_position])
			lru_position = i;

	for (i = 0; i < st->num_channels; i++)
		if (st->channels[i].cfg.cfg_slot == lru_position)
			st->channels[i].cfg.live = false;

	ida_free(&st->cfg_slots_status, lru_position);
	return ida_alloc(&st->cfg_slots_status, GFP_KERNEL);
}

/* Could be replaced with a generic LRU implementation */
static int ad7173_load_config(struct ad7173_state *st,
			      struct ad7173_channel_config *cfg)
{
	unsigned int config;
	int free_cfg_slot, ret;

	free_cfg_slot = ida_alloc_range(&st->cfg_slots_status, 0,
					st->info->num_configs - 1, GFP_KERNEL);
	if (free_cfg_slot < 0)
		free_cfg_slot = ad7173_free_config_slot_lru(st);

	cfg->cfg_slot = free_cfg_slot;
	config = FIELD_PREP(AD7173_SETUP_REF_SEL_MASK, cfg->ref_sel);

	if (cfg->bipolar)
		config |= AD7173_SETUP_BIPOLAR;

	if (cfg->input_buf)
		config |= AD7173_SETUP_AIN_BUF_MASK;

	ret = ad_sd_write_reg(&st->sd, AD7173_REG_SETUP(free_cfg_slot), 2, config);
	if (ret)
		return ret;

	return ad_sd_write_reg(&st->sd, AD7173_REG_FILTER(free_cfg_slot), 2,
			       AD7173_FILTER_ODR0_MASK & cfg->odr);
}

static int ad7173_config_channel(struct ad7173_state *st, int addr)
{
	struct ad7173_channel_config *cfg = &st->channels[addr].cfg;
	struct ad7173_channel_config *live_cfg;
	int ret;

	if (!cfg->live) {
		live_cfg = ad7173_find_live_config(st, cfg);
		if (live_cfg) {
			cfg->cfg_slot = live_cfg->cfg_slot;
		} else {
			ret = ad7173_load_config(st, cfg);
			if (ret)
				return ret;
			cfg->live = true;
		}
	}

	if (st->config_usage_counter == U64_MAX)
		ad7173_reset_usage_cnts(st);

	st->config_usage_counter++;
	st->config_cnts[cfg->cfg_slot] = st->config_usage_counter;

	return 0;
}

static int ad7173_set_channel(struct ad_sigma_delta *sd, unsigned int channel)
{
	struct ad7173_state *st = ad_sigma_delta_to_ad7173(sd);
	unsigned int val;
	int ret;

	ret = ad7173_config_channel(st, channel);
	if (ret)
		return ret;

	val = AD7173_CH_ENABLE |
	      FIELD_PREP(AD7173_CH_SETUP_SEL_MASK, st->channels[channel].cfg.cfg_slot) |
	      st->channels[channel].ain;

	if (st->channels[channel].cfg.openwire_comp_chan >= 0)
		channel = st->channels[channel].cfg.openwire_comp_chan;

	return ad_sd_write_reg(&st->sd, AD7173_REG_CH(channel), 2, val);
}

static int ad7173_set_mode(struct ad_sigma_delta *sd,
			   enum ad_sigma_delta_mode mode)
{
	struct ad7173_state *st = ad_sigma_delta_to_ad7173(sd);

	st->adc_mode &= ~AD7173_ADC_MODE_MODE_MASK;
	st->adc_mode |= FIELD_PREP(AD7173_ADC_MODE_MODE_MASK, mode);

	return ad_sd_write_reg(&st->sd, AD7173_REG_ADC_MODE, 2, st->adc_mode);
}

static int ad7173_append_status(struct ad_sigma_delta *sd, bool append)
{
	struct ad7173_state *st = ad_sigma_delta_to_ad7173(sd);
	unsigned int interface_mode = st->interface_mode;
	int ret;

	interface_mode &= ~AD7173_INTERFACE_DATA_STAT;
	interface_mode |= AD7173_INTERFACE_DATA_STAT_EN(append);
	ret = ad_sd_write_reg(&st->sd, AD7173_REG_INTERFACE_MODE, 2, interface_mode);
	if (ret)
		return ret;

	st->interface_mode = interface_mode;

	return 0;
}

static int ad7173_disable_all(struct ad_sigma_delta *sd)
{
	struct ad7173_state *st = ad_sigma_delta_to_ad7173(sd);
	int ret;
	int i;

	for (i = 0; i < st->num_channels; i++) {
		ret = ad_sd_write_reg(sd, AD7173_REG_CH(i), 2, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ad7173_disable_one(struct ad_sigma_delta *sd, unsigned int chan)
{
	struct ad7173_state *st = ad_sigma_delta_to_ad7173(sd);

	if (st->channels[chan].cfg.openwire_comp_chan >= 0)
		chan = st->channels[chan].cfg.openwire_comp_chan;

	return ad_sd_write_reg(sd, AD7173_REG_CH(chan), 2, 0);
}

static const struct ad_sigma_delta_info ad7173_sigma_delta_info_4_slots = {
	.set_channel = ad7173_set_channel,
	.append_status = ad7173_append_status,
	.disable_all = ad7173_disable_all,
	.disable_one = ad7173_disable_one,
	.set_mode = ad7173_set_mode,
	.has_registers = true,
	.has_named_irqs = true,
	.addr_shift = 0,
	.read_mask = BIT(6),
	.status_ch_mask = GENMASK(3, 0),
	.data_reg = AD7173_REG_DATA,
	.num_resetclks = 64,
	.num_slots = 4,
};

static const struct ad_sigma_delta_info ad7173_sigma_delta_info_8_slots = {
	.set_channel = ad7173_set_channel,
	.append_status = ad7173_append_status,
	.disable_all = ad7173_disable_all,
	.disable_one = ad7173_disable_one,
	.set_mode = ad7173_set_mode,
	.has_registers = true,
	.has_named_irqs = true,
	.addr_shift = 0,
	.read_mask = BIT(6),
	.status_ch_mask = GENMASK(3, 0),
	.data_reg = AD7173_REG_DATA,
	.num_resetclks = 64,
	.num_slots = 8,
};

static const struct ad_sigma_delta_info ad7173_sigma_delta_info_16_slots = {
	.set_channel = ad7173_set_channel,
	.append_status = ad7173_append_status,
	.disable_all = ad7173_disable_all,
	.disable_one = ad7173_disable_one,
	.set_mode = ad7173_set_mode,
	.has_registers = true,
	.has_named_irqs = true,
	.addr_shift = 0,
	.read_mask = BIT(6),
	.status_ch_mask = GENMASK(3, 0),
	.data_reg = AD7173_REG_DATA,
	.num_resetclks = 64,
	.num_slots = 16,
};

static const struct ad7173_device_info ad4111_device_info = {
	.name = "ad4111",
	.id = AD4111_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in_div = 8,
	.num_channels = 16,
	.num_configs = 8,
	.num_voltage_in = 8,
	.num_gpios = 2,
	.higher_gpio_bits = true,
	.has_temp = true,
	.has_vincom_input = true,
	.has_input_buf = true,
	.has_current_inputs = true,
	.has_int_ref = true,
	.has_internal_fs_calibration = true,
	.has_openwire_det = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad4112_device_info = {
	.name = "ad4112",
	.id = AD4112_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in_div = 8,
	.num_channels = 16,
	.num_configs = 8,
	.num_voltage_in = 8,
	.num_gpios = 2,
	.higher_gpio_bits = true,
	.has_vincom_input = true,
	.has_temp = true,
	.has_input_buf = true,
	.has_current_inputs = true,
	.has_int_ref = true,
	.has_internal_fs_calibration = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad4113_device_info = {
	.name = "ad4113",
	.id = AD4113_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in_div = 8,
	.num_channels = 16,
	.num_configs = 8,
	.num_voltage_in = 8,
	.num_gpios = 2,
	.data_reg_only_16bit = true,
	.higher_gpio_bits = true,
	.has_vincom_input = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad4114_device_info = {
	.name = "ad4114",
	.id = AD4114_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in_div = 16,
	.num_channels = 16,
	.num_configs = 8,
	.num_voltage_in = 16,
	.num_gpios = 4,
	.has_vincom_input = true,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_internal_fs_calibration = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad4115_device_info = {
	.name = "ad4115",
	.id = AD4115_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in_div = 16,
	.num_channels = 16,
	.num_configs = 8,
	.num_voltage_in = 16,
	.num_gpios = 4,
	.has_vincom_input = true,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_internal_fs_calibration = true,
	.clock = 8 * HZ_PER_MHZ,
	.sinc5_data_rates = ad4115_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad4115_sinc5_data_rates),
};

static const struct ad7173_device_info ad4116_device_info = {
	.name = "ad4116",
	.id = AD4116_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in_div = 11,
	.num_channels = 16,
	.num_configs = 8,
	.num_voltage_in = 16,
	.num_gpios = 4,
	.has_vincom_input = true,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_internal_fs_calibration = true,
	.clock = 4 * HZ_PER_MHZ,
	.sinc5_data_rates = ad4116_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad4116_sinc5_data_rates),
};

static const struct ad7173_device_info ad7172_2_device_info = {
	.name = "ad7172-2",
	.id = AD7172_2_ID,
	.sd_info = &ad7173_sigma_delta_info_4_slots,
	.num_voltage_in = 5,
	.num_channels = 4,
	.num_configs = 4,
	.num_gpios = 2,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_pow_supply_monitoring = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad7172_4_device_info = {
	.name = "ad7172-4",
	.id = AD7172_4_ID,
	.sd_info = &ad7173_sigma_delta_info_8_slots,
	.num_voltage_in = 9,
	.num_channels = 8,
	.num_configs = 8,
	.num_gpios = 4,
	.has_input_buf = true,
	.has_ref2 = true,
	.has_pow_supply_monitoring = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad7173_8_device_info = {
	.name = "ad7173-8",
	.id = AD7173_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in = 17,
	.num_channels = 16,
	.num_configs = 8,
	.num_gpios = 4,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_ref2 = true,
	.clock = 2 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7173_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7173_sinc5_data_rates),
};

static const struct ad7173_device_info ad7175_2_device_info = {
	.name = "ad7175-2",
	.id = AD7175_2_ID,
	.sd_info = &ad7173_sigma_delta_info_4_slots,
	.num_voltage_in = 5,
	.num_channels = 4,
	.num_configs = 4,
	.num_gpios = 2,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_pow_supply_monitoring = true,
	.clock = 16 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7175_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7175_sinc5_data_rates),
};

static const struct ad7173_device_info ad7175_8_device_info = {
	.name = "ad7175-8",
	.id = AD7175_8_ID,
	.sd_info = &ad7173_sigma_delta_info_16_slots,
	.num_voltage_in = 17,
	.num_channels = 16,
	.num_configs = 8,
	.num_gpios = 4,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_ref2 = true,
	.has_pow_supply_monitoring = true,
	.clock = 16 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7175_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7175_sinc5_data_rates),
};

static const struct ad7173_device_info ad7176_2_device_info = {
	.name = "ad7176-2",
	.id = AD7176_ID,
	.sd_info = &ad7173_sigma_delta_info_4_slots,
	.num_voltage_in = 5,
	.num_channels = 4,
	.num_configs = 4,
	.num_gpios = 2,
	.has_int_ref = true,
	.clock = 16 * HZ_PER_MHZ,
	.sinc5_data_rates = ad7175_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7175_sinc5_data_rates),
};

static const struct ad7173_device_info ad7177_2_device_info = {
	.name = "ad7177-2",
	.id = AD7177_ID,
	.sd_info = &ad7173_sigma_delta_info_4_slots,
	.num_voltage_in = 5,
	.num_channels = 4,
	.num_configs = 4,
	.num_gpios = 2,
	.has_temp = true,
	.has_input_buf = true,
	.has_int_ref = true,
	.has_pow_supply_monitoring = true,
	.clock = 16 * HZ_PER_MHZ,
	.odr_start_value = AD7177_ODR_START_VALUE,
	.sinc5_data_rates = ad7175_sinc5_data_rates,
	.num_sinc5_data_rates = ARRAY_SIZE(ad7175_sinc5_data_rates),
};

static int ad7173_setup(struct iio_dev *indio_dev)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct device *dev = &st->sd.spi->dev;
	u8 buf[AD7173_RESET_LENGTH];
	unsigned int id;
	int ret;

	/* reset the serial interface */
	memset(buf, 0xff, AD7173_RESET_LENGTH);
	ret = spi_write_then_read(st->sd.spi, buf, sizeof(buf), NULL, 0);
	if (ret < 0)
		return ret;

	/* datasheet recommends a delay of at least 500us after reset */
	fsleep(500);

	ret = ad_sd_read_reg(&st->sd, AD7173_REG_ID, 2, &id);
	if (ret)
		return ret;

	id &= AD7173_ID_MASK;
	if (id != st->info->id)
		dev_warn(dev, "Unexpected device id: 0x%04X, expected: 0x%04X\n",
			 id, st->info->id);

	st->adc_mode |= AD7173_ADC_MODE_SING_CYC;
	st->interface_mode = 0x0;

	st->config_usage_counter = 0;
	st->config_cnts = devm_kcalloc(dev, st->info->num_configs,
				       sizeof(*st->config_cnts), GFP_KERNEL);
	if (!st->config_cnts)
		return -ENOMEM;

	ret = ad7173_calibrate_all(st, indio_dev);
	if (ret)
		return ret;

	/* All channels are enabled by default after a reset */
	return ad7173_disable_all(&st->sd);
}

static unsigned int ad7173_get_ref_voltage_milli(struct ad7173_state *st,
						 u8 reference_select)
{
	int vref;

	switch (reference_select) {
	case AD7173_SETUP_REF_SEL_EXT_REF:
		vref = regulator_get_voltage(st->regulators[0].consumer);
		break;

	case AD7173_SETUP_REF_SEL_EXT_REF2:
		vref = regulator_get_voltage(st->regulators[1].consumer);
		break;

	case AD7173_SETUP_REF_SEL_INT_REF:
		vref = AD7173_VOLTAGE_INT_REF_uV;
		break;

	case AD7173_SETUP_REF_SEL_AVDD1_AVSS:
		vref = regulator_get_voltage(st->regulators[2].consumer);
		break;

	default:
		return -EINVAL;
	}

	if (vref < 0)
		return vref;

	return vref / (MICRO / MILLI);
}

static int ad7173_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct ad7173_channel *ch = &st->channels[chan->address];
	unsigned int reg;
	u64 temp;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = ad_sigma_delta_single_conversion(indio_dev, chan, val);
		if (ret < 0)
			return ret;

		if (ch->openwire_det_en) {
			ret = ad4111_openwire_event(indio_dev, chan);
			if (ret < 0)
				return ret;
		}

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:

		switch (chan->type) {
		case IIO_TEMP:
			temp = AD7173_VOLTAGE_INT_REF_uV * MILLI;
			temp /= AD7173_TEMP_SENSIIVITY_uV_per_C;
			*val = temp;
			*val2 = chan->scan_type.realbits;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_VOLTAGE:
			*val = ad7173_get_ref_voltage_milli(st, ch->cfg.ref_sel);
			*val2 = chan->scan_type.realbits - !!(ch->cfg.bipolar);

			if (chan->channel < st->info->num_voltage_in_div)
				*val *= AD4111_DIVIDER_RATIO;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_CURRENT:
			*val = ad7173_get_ref_voltage_milli(st, ch->cfg.ref_sel);
			*val /= AD4111_SHUNT_RESISTOR_OHM;
			*val2 = chan->scan_type.realbits - ch->cfg.bipolar;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:

		switch (chan->type) {
		case IIO_TEMP:
			/* 0 Kelvin -> raw sample */
			temp   = -ABSOLUTE_ZERO_MILLICELSIUS;
			temp  *= AD7173_TEMP_SENSIIVITY_uV_per_C;
			temp <<= chan->scan_type.realbits;
			temp   = DIV_U64_ROUND_CLOSEST(temp,
						       AD7173_VOLTAGE_INT_REF_uV *
						       MILLI);
			*val   = -temp;
			return IIO_VAL_INT;
		case IIO_VOLTAGE:
		case IIO_CURRENT:
			*val = -BIT(chan->scan_type.realbits - 1);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		reg = st->channels[chan->address].cfg.odr;

		*val = st->info->sinc5_data_rates[reg] / MILLI;
		*val2 = (st->info->sinc5_data_rates[reg] % MILLI) * (MICRO / MILLI);

		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int ad7173_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct ad7173_channel_config *cfg;
	unsigned int freq, i;
	int ret = 0;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	switch (info) {
	/*
	 * This attribute sets the sampling frequency for each channel individually.
	 * There are no issues for raw or buffered reads of an individual channel.
	 *
	 * When multiple channels are enabled in buffered mode, the effective
	 * sampling rate of a channel is lowered in correlation to the number
	 * of channels enabled and the sampling rate of the other channels.
	 *
	 * Example: 3 channels enabled with rates CH1:6211sps CH2,CH3:10sps
	 * While the reading of CH1 takes only 0.16ms, the reading of CH2 and CH3
	 * will take 100ms each.
	 *
	 * This will cause the reading of CH1 to be actually done once every
	 * 200.16ms, an effective rate of 4.99sps.
	 */
	case IIO_CHAN_INFO_SAMP_FREQ:
		freq = val * MILLI + val2 / MILLI;
		for (i = st->info->odr_start_value; i < st->info->num_sinc5_data_rates - 1; i++)
			if (freq >= st->info->sinc5_data_rates[i])
				break;

		cfg = &st->channels[chan->address].cfg;
		cfg->odr = i;
		cfg->live = false;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct(indio_dev);
	return ret;
}

static int ad7173_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	int i, ret;

	for (i = 0; i < indio_dev->num_channels; i++) {
		if (test_bit(i, scan_mask))
			ret = ad7173_set_channel(&st->sd, i);
		else
			ret = ad_sd_write_reg(&st->sd, AD7173_REG_CH(i), 2, 0);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ad7173_debug_reg_access(struct iio_dev *indio_dev, unsigned int reg,
				   unsigned int writeval, unsigned int *readval)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	u8 reg_size;

	if (reg == AD7173_REG_COMMS)
		reg_size = 1;
	else if (reg == AD7173_REG_CRC || reg == AD7173_REG_DATA ||
		 reg >= AD7173_REG_OFFSET(0))
		reg_size = 3;
	else
		reg_size = 2;

	if (readval)
		return ad_sd_read_reg(&st->sd, reg, reg_size, readval);

	return ad_sd_write_reg(&st->sd, reg, reg_size, writeval);
}

static int ad7173_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     bool state)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct ad7173_channel *adchan = &st->channels[chan->address];

	switch (type) {
	case IIO_EV_TYPE_FAULT:
		adchan->openwire_det_en = state;
		return 0;
	default:
		return -EINVAL;
	}
}

static int ad7173_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct ad7173_channel *adchan = &st->channels[chan->address];

	switch (type) {
	case IIO_EV_TYPE_FAULT:
		return adchan->openwire_det_en;
	default:
		return -EINVAL;
	}
}

static const struct iio_event_spec ad4111_events[] = {
	{
		.type = IIO_EV_TYPE_FAULT,
		.dir = IIO_EV_DIR_FAULT_OPENWIRE,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
		.mask_shared_by_all = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_info ad7173_info = {
	.read_raw = &ad7173_read_raw,
	.write_raw = &ad7173_write_raw,
	.debugfs_reg_access = &ad7173_debug_reg_access,
	.validate_trigger = ad_sd_validate_trigger,
	.update_scan_mode = ad7173_update_scan_mode,
	.write_event_config = ad7173_write_event_config,
	.read_event_config = ad7173_read_event_config,
};

static const struct iio_scan_type ad4113_scan_type = {
	.sign = 'u',
	.realbits = 16,
	.storagebits = 16,
	.endianness = IIO_BE,
};

static const struct iio_chan_spec ad7173_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.scan_type = {
		.sign = 'u',
		.realbits = 24,
		.storagebits = 32,
		.endianness = IIO_BE,
	},
	.ext_info = ad7173_calibsys_ext_info,
};

static const struct iio_chan_spec ad7173_temp_iio_channel_template = {
	.type = IIO_TEMP,
	.channel = AD7173_AIN_TEMP_POS,
	.channel2 = AD7173_AIN_TEMP_NEG,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
		BIT(IIO_CHAN_INFO_SCALE) | BIT(IIO_CHAN_INFO_OFFSET) |
		BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.scan_type = {
		.sign = 'u',
		.realbits = 24,
		.storagebits = 32,
		.endianness = IIO_BE,
	},
};

static void ad7173_disable_regulators(void *data)
{
	struct ad7173_state *st = data;

	regulator_bulk_disable(ARRAY_SIZE(st->regulators), st->regulators);
}

static unsigned long ad7173_sel_clk(struct ad7173_state *st,
				    unsigned int clk_sel)
{
	int ret;

	st->adc_mode &= ~AD7173_ADC_MODE_CLOCKSEL_MASK;
	st->adc_mode |= FIELD_PREP(AD7173_ADC_MODE_CLOCKSEL_MASK, clk_sel);
	ret = ad_sd_write_reg(&st->sd, AD7173_REG_ADC_MODE, 0x2, st->adc_mode);

	return ret;
}

static unsigned long ad7173_clk_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct ad7173_state *st = clk_hw_to_ad7173(hw);

	return st->info->clock / HZ_PER_KHZ;
}

static int ad7173_clk_output_is_enabled(struct clk_hw *hw)
{
	struct ad7173_state *st = clk_hw_to_ad7173(hw);
	u32 clk_sel;

	clk_sel = FIELD_GET(AD7173_ADC_MODE_CLOCKSEL_MASK, st->adc_mode);
	return clk_sel == AD7173_ADC_MODE_CLOCKSEL_INT_OUTPUT;
}

static int ad7173_clk_output_prepare(struct clk_hw *hw)
{
	struct ad7173_state *st = clk_hw_to_ad7173(hw);

	return ad7173_sel_clk(st, AD7173_ADC_MODE_CLOCKSEL_INT_OUTPUT);
}

static void ad7173_clk_output_unprepare(struct clk_hw *hw)
{
	struct ad7173_state *st = clk_hw_to_ad7173(hw);

	ad7173_sel_clk(st, AD7173_ADC_MODE_CLOCKSEL_INT);
}

static const struct clk_ops ad7173_int_clk_ops = {
	.recalc_rate = ad7173_clk_recalc_rate,
	.is_enabled = ad7173_clk_output_is_enabled,
	.prepare = ad7173_clk_output_prepare,
	.unprepare = ad7173_clk_output_unprepare,
};

static int ad7173_register_clk_provider(struct iio_dev *indio_dev)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct clk_init_data init = {};
	int ret;

	if (!IS_ENABLED(CONFIG_COMMON_CLK))
		return 0;

	init.name = fwnode_get_name(fwnode);
	init.ops = &ad7173_int_clk_ops;

	st->int_clk_hw.init = &init;
	ret = devm_clk_hw_register(dev, &st->int_clk_hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &st->int_clk_hw);
}

static int ad4111_validate_current_ain(struct ad7173_state *st,
				       const unsigned int ain[AD7173_NO_AINS_PER_CHANNEL])
{
	struct device *dev = &st->sd.spi->dev;

	if (!st->info->has_current_inputs)
		return dev_err_probe(dev, -EINVAL,
			"Model %s does not support current channels\n",
			st->info->name);

	if (ain[0] >= ARRAY_SIZE(ad4111_current_channel_config))
		return dev_err_probe(dev, -EINVAL,
			"For current channels single-channel must be <[0-3]>\n");

	return 0;
}

static int ad7173_validate_voltage_ain_inputs(struct ad7173_state *st,
					      unsigned int ain0, unsigned int ain1)
{
	struct device *dev = &st->sd.spi->dev;
	bool special_input0, special_input1;

	/* (AVDD1-AVSS)/5 power supply monitoring */
	if (ain0 == AD7173_AIN_POW_MON_POS && ain1 == AD7173_AIN_POW_MON_NEG &&
	    st->info->has_pow_supply_monitoring)
		return 0;

	special_input0 = AD7173_IS_REF_INPUT(ain0) ||
			 (ain0 == AD4111_VINCOM_INPUT && st->info->has_vincom_input);
	special_input1 = AD7173_IS_REF_INPUT(ain1) ||
			 (ain1 == AD4111_VINCOM_INPUT && st->info->has_vincom_input);

	if ((ain0 >= st->info->num_voltage_in && !special_input0) ||
	    (ain1 >= st->info->num_voltage_in && !special_input1)) {
		if (ain0 == AD4111_VINCOM_INPUT || ain1 == AD4111_VINCOM_INPUT)
			return dev_err_probe(dev, -EINVAL,
				"VINCOM not supported for %s\n", st->info->name);

		return dev_err_probe(dev, -EINVAL,
				     "Input pin number out of range for pair (%d %d).\n",
				     ain0, ain1);
	}

	if (AD4111_IS_VINCOM_MISMATCH(ain0, ain1) ||
	    AD4111_IS_VINCOM_MISMATCH(ain1, ain0))
		return dev_err_probe(dev, -EINVAL,
			"VINCOM must be paired with inputs having divider.\n");

	if (!special_input0 && !special_input1 &&
	    ((ain0 >= st->info->num_voltage_in_div) !=
	     (ain1 >= st->info->num_voltage_in_div)))
		return dev_err_probe(dev, -EINVAL,
			"Both inputs must either have a voltage divider or not have: (%d %d).\n",
			ain0, ain1);

	return 0;
}

static int ad7173_validate_reference(struct ad7173_state *st, int ref_sel)
{
	struct device *dev = &st->sd.spi->dev;
	int ret;

	if (ref_sel == AD7173_SETUP_REF_SEL_INT_REF && !st->info->has_int_ref)
		return dev_err_probe(dev, -EINVAL,
			"Internal reference is not available on current model.\n");

	if (ref_sel == AD7173_SETUP_REF_SEL_EXT_REF2 && !st->info->has_ref2)
		return dev_err_probe(dev, -EINVAL,
			"External reference 2 is not available on current model.\n");

	ret = ad7173_get_ref_voltage_milli(st, ref_sel);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot use reference %u\n",
				     ref_sel);

	return 0;
}

static int ad7173_validate_openwire_ain_inputs(struct ad7173_state *st,
					       bool differential,
					       unsigned int ain0,
					       unsigned int ain1)
{
	/*
	 * If the channel is configured as differential,
	 * the ad4111 requires specific ains to be used together
	 */
	if (differential)
		return (ain0 % 2) ? (ain0 - 1) == ain1 : (ain0 + 1) == ain1;

	return ain1 == AD4111_VINCOM_INPUT;
}

static unsigned int ad7173_calc_openwire_thrsh_raw(struct ad7173_state *st,
						   struct iio_chan_spec *chan,
						   struct ad7173_channel *chan_st_priv,
						   unsigned int thrsh_mv) {
	unsigned int thrsh_raw;

	thrsh_raw =
		BIT(chan->scan_type.realbits - !!(chan_st_priv->cfg.bipolar))
		* thrsh_mv
		/ ad7173_get_ref_voltage_milli(st, chan_st_priv->cfg.ref_sel);
	if (chan->channel < st->info->num_voltage_in_div)
		thrsh_raw /= AD4111_DIVIDER_RATIO;

	return thrsh_raw;
}

static int ad7173_fw_parse_channel_config(struct iio_dev *indio_dev)
{
	struct ad7173_channel *chans_st_arr, *chan_st_priv;
	struct ad7173_state *st = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	struct iio_chan_spec *chan_arr, *chan;
	unsigned int ain[AD7173_NO_AINS_PER_CHANNEL], chan_index = 0;
	int ref_sel, ret, num_channels;

	num_channels = device_get_child_node_count(dev);

	if (st->info->has_temp)
		num_channels++;

	if (num_channels == 0)
		return dev_err_probe(dev, -ENODATA, "No channels specified\n");

	if (num_channels > st->info->num_channels)
		return dev_err_probe(dev, -EINVAL,
			"Too many channels specified. Maximum is %d, not including temperature channel if supported.\n",
			st->info->num_channels);

	indio_dev->num_channels = num_channels;
	st->num_channels = num_channels;

	chan_arr = devm_kcalloc(dev, sizeof(*indio_dev->channels),
				st->num_channels, GFP_KERNEL);
	if (!chan_arr)
		return -ENOMEM;

	chans_st_arr = devm_kcalloc(dev, st->num_channels, sizeof(*st->channels),
				    GFP_KERNEL);
	if (!chans_st_arr)
		return -ENOMEM;

	indio_dev->channels = chan_arr;
	st->channels = chans_st_arr;

	if (st->info->has_temp) {
		chan_arr[chan_index] = ad7173_temp_iio_channel_template;
		chan_st_priv = &chans_st_arr[chan_index];
		chan_st_priv->ain =
			AD7173_CH_ADDRESS(chan_arr[chan_index].channel,
					  chan_arr[chan_index].channel2);
		chan_st_priv->cfg.bipolar = false;
		chan_st_priv->cfg.input_buf = st->info->has_input_buf;
		chan_st_priv->cfg.ref_sel = AD7173_SETUP_REF_SEL_INT_REF;
		chan_st_priv->cfg.odr = st->info->odr_start_value;
		chan_st_priv->cfg.openwire_comp_chan = -1;
		st->adc_mode |= AD7173_ADC_MODE_REF_EN;
		if (st->info->data_reg_only_16bit)
			chan_arr[chan_index].scan_type = ad4113_scan_type;

		chan_index++;
	}

	device_for_each_child_node_scoped(dev, child) {
		bool is_current_chan = false;

		chan = &chan_arr[chan_index];
		*chan = ad7173_channel_template;
		chan_st_priv = &chans_st_arr[chan_index];
		ret = fwnode_property_read_u32_array(child, "diff-channels",
						     ain, ARRAY_SIZE(ain));
		if (ret) {
			ret = fwnode_property_read_u32(child, "single-channel",
						       ain);
			if (ret)
				return dev_err_probe(dev, ret,
					"Channel must define one of diff-channels or single-channel.\n");

			is_current_chan = fwnode_property_read_bool(child, "adi,current-channel");
		} else {
			chan->differential = true;
		}

		if (is_current_chan) {
			ret = ad4111_validate_current_ain(st, ain);
			if (ret)
				return ret;
		} else {
			if (!chan->differential) {
				ret = fwnode_property_read_u32(child,
					"common-mode-channel", ain + 1);
				if (ret)
					return dev_err_probe(dev, ret,
						"common-mode-channel must be defined for single-ended channels.\n");
			}
			ret = ad7173_validate_voltage_ain_inputs(st, ain[0], ain[1]);
			if (ret)
				return ret;
		}

		ret = fwnode_property_match_property_string(child,
							    "adi,reference-select",
							    ad7173_ref_sel_str,
							    ARRAY_SIZE(ad7173_ref_sel_str));
		if (ret < 0)
			ref_sel = AD7173_SETUP_REF_SEL_INT_REF;
		else
			ref_sel = ret;

		ret = ad7173_validate_reference(st, ref_sel);
		if (ret)
			return ret;

		if (ref_sel == AD7173_SETUP_REF_SEL_INT_REF)
			st->adc_mode |= AD7173_ADC_MODE_REF_EN;
		chan_st_priv->cfg.ref_sel = ref_sel;

		chan->address = chan_index;
		chan->scan_index = chan_index;
		chan->channel = ain[0];
		chan_st_priv->cfg.input_buf = st->info->has_input_buf;
		chan_st_priv->cfg.odr = st->info->odr_start_value;
		chan_st_priv->cfg.openwire_comp_chan = -1;

		chan_st_priv->cfg.bipolar = fwnode_property_read_bool(child, "bipolar");
		if (chan_st_priv->cfg.bipolar)
			chan->info_mask_separate |= BIT(IIO_CHAN_INFO_OFFSET);

		if (is_current_chan) {
			chan->type = IIO_CURRENT;
			chan->differential = false;
			chan->channel2 = 0;
			chan_st_priv->ain = ad4111_current_channel_config[ain[0]];
		} else {
			chan_st_priv->cfg.input_buf = st->info->has_input_buf;
			chan->channel2 = ain[1];
			chan_st_priv->ain = AD7173_CH_ADDRESS(ain[0], ain[1]);
			if (st->info->has_openwire_det &&
			    ad7173_validate_openwire_ain_inputs(st, chan->differential, ain[0], ain[1])) {
				chan->event_spec = ad4111_events;
				chan->num_event_specs = ARRAY_SIZE(ad4111_events);
				chan_st_priv->cfg.openwire_thrsh_raw =
					ad7173_calc_openwire_thrsh_raw(st, chan, chan_st_priv,
								       AD4111_OW_DET_THRSH_MV);
			}
		}

		if (st->info->data_reg_only_16bit)
			chan_arr[chan_index].scan_type = ad4113_scan_type;

		chan_index++;
	}
	return 0;
}

static int ad7173_fw_parse_device_config(struct iio_dev *indio_dev)
{
	struct ad7173_state *st = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	int ret;

	st->regulators[0].supply = ad7173_ref_sel_str[AD7173_SETUP_REF_SEL_EXT_REF];
	st->regulators[1].supply = ad7173_ref_sel_str[AD7173_SETUP_REF_SEL_EXT_REF2];
	st->regulators[2].supply = ad7173_ref_sel_str[AD7173_SETUP_REF_SEL_AVDD1_AVSS];

	/*
	 * If a regulator is not available, it will be set to a dummy regulator.
	 * Each channel reference is checked with regulator_get_voltage() before
	 * setting attributes so if any channel uses a dummy supply the driver
	 * probe will fail.
	 */
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(st->regulators),
				      st->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(st->regulators), st->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	ret = devm_add_action_or_reset(dev, ad7173_disable_regulators, st);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to add regulators disable action\n");

	ret = device_property_match_property_string(dev, "clock-names",
						    ad7173_clk_sel,
						    ARRAY_SIZE(ad7173_clk_sel));
	if (ret < 0) {
		st->adc_mode |= FIELD_PREP(AD7173_ADC_MODE_CLOCKSEL_MASK,
					   AD7173_ADC_MODE_CLOCKSEL_INT);
		ad7173_register_clk_provider(indio_dev);
	} else {
		struct clk *clk;

		st->adc_mode |= FIELD_PREP(AD7173_ADC_MODE_CLOCKSEL_MASK,
					   AD7173_ADC_MODE_CLOCKSEL_EXT + ret);
		clk = devm_clk_get_enabled(dev, ad7173_clk_sel[ret]);
		if (IS_ERR(clk))
			return dev_err_probe(dev, PTR_ERR(clk),
					     "Failed to get external clock\n");
	}

	return ad7173_fw_parse_channel_config(indio_dev);
}

static int ad7173_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ad7173_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->info = spi_get_device_match_data(spi);
	if (!st->info)
		return -ENODEV;

	ida_init(&st->cfg_slots_status);
	ret = devm_add_action_or_reset(dev, ad7173_ida_destroy, st);
	if (ret)
		return ret;

	indio_dev->name = st->info->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad7173_info;

	spi->mode = SPI_MODE_3;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	ret = ad_sd_init(&st->sd, indio_dev, spi, st->info->sd_info);
	if (ret)
		return ret;

	ret = ad7173_fw_parse_device_config(indio_dev);
	if (ret)
		return ret;

	ret = devm_ad_sd_setup_buffer_and_trigger(dev, indio_dev);
	if (ret)
		return ret;

	ret = ad7173_setup(indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ret;

	return ad7173_gpio_init(st);
}

static const struct of_device_id ad7173_of_match[] = {
	{ .compatible = "adi,ad4111",	.data = &ad4111_device_info },
	{ .compatible = "adi,ad4112",	.data = &ad4112_device_info },
	{ .compatible = "adi,ad4113",	.data = &ad4113_device_info },
	{ .compatible = "adi,ad4114",	.data = &ad4114_device_info },
	{ .compatible = "adi,ad4115",	.data = &ad4115_device_info },
	{ .compatible = "adi,ad4116",	.data = &ad4116_device_info },
	{ .compatible = "adi,ad7172-2", .data = &ad7172_2_device_info },
	{ .compatible = "adi,ad7172-4", .data = &ad7172_4_device_info },
	{ .compatible = "adi,ad7173-8", .data = &ad7173_8_device_info },
	{ .compatible = "adi,ad7175-2", .data = &ad7175_2_device_info },
	{ .compatible = "adi,ad7175-8", .data = &ad7175_8_device_info },
	{ .compatible = "adi,ad7176-2", .data = &ad7176_2_device_info },
	{ .compatible = "adi,ad7177-2", .data = &ad7177_2_device_info },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7173_of_match);

static const struct spi_device_id ad7173_id_table[] = {
	{ "ad4111",   (kernel_ulong_t)&ad4111_device_info },
	{ "ad4112",   (kernel_ulong_t)&ad4112_device_info },
	{ "ad4113",   (kernel_ulong_t)&ad4113_device_info },
	{ "ad4114",   (kernel_ulong_t)&ad4114_device_info },
	{ "ad4115",   (kernel_ulong_t)&ad4115_device_info },
	{ "ad4116",   (kernel_ulong_t)&ad4116_device_info },
	{ "ad7172-2", (kernel_ulong_t)&ad7172_2_device_info },
	{ "ad7172-4", (kernel_ulong_t)&ad7172_4_device_info },
	{ "ad7173-8", (kernel_ulong_t)&ad7173_8_device_info },
	{ "ad7175-2", (kernel_ulong_t)&ad7175_2_device_info },
	{ "ad7175-8", (kernel_ulong_t)&ad7175_8_device_info },
	{ "ad7176-2", (kernel_ulong_t)&ad7176_2_device_info },
	{ "ad7177-2", (kernel_ulong_t)&ad7177_2_device_info },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7173_id_table);

static struct spi_driver ad7173_driver = {
	.driver = {
		.name	= "ad7173",
		.of_match_table = ad7173_of_match,
	},
	.probe		= ad7173_probe,
	.id_table	= ad7173_id_table,
};
module_spi_driver(ad7173_driver);

MODULE_IMPORT_NS("IIO_AD_SIGMA_DELTA");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafo.de>");
MODULE_AUTHOR("Dumitru Ceclan <dumitru.ceclan@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7173 and similar ADC driver");
MODULE_LICENSE("GPL");
