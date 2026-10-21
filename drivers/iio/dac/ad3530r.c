// SPDX-License-Identifier: GPL-2.0
/*
 * AD3530R/AD3530 8-channel, 16-bit Voltage Output DAC Driver
 * AD3531R/AD3531 4-channel, 16-bit Voltage Output DAC Driver
 * AD3532R/AD3532 16-channel, 16-bit Voltage Output DAC Driver
 *
 * Copyright 2025 Analog Devices Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/units.h>

#define AD3530R_INTERFACE_CONFIG_A		0x00
#define AD3530R_OUTPUT_OPERATING_MODE_0		0x20
#define AD3530R_OUTPUT_OPERATING_MODE_1		0x21
#define AD3530R_OUTPUT_CONTROL_0		0x2A
#define AD3530R_REFERENCE_CONTROL_0		0x3C
#define AD3530R_SW_LDAC_TRIG_A			0xE5
#define AD3530R_INPUT_CH			0xEB
#define AD3530R_MAX_REG_ADDR			0xF9

#define AD3531R_SW_LDAC_TRIG_A			0xDD
#define AD3531R_INPUT_CH			0xE3

/* AD3532R/AD3532 bank 0 registers (channels 0-7) */
#define AD3532R_INTERFACE_CONFIG_A_0		0x1000
#define AD3532R_OUTPUT_OPERATING_MODE_0		0x1020
#define AD3532R_OUTPUT_OPERATING_MODE_1		0x1021
#define AD3532R_OUTPUT_CONTROL_0		0x102A
#define AD3532R_REFERENCE_CONTROL_0		0x103C
#define AD3532R_SW_LDAC_TRIG_0			0x10E5
#define AD3532R_INPUT_CH_0			0x10EB

/* AD3532R/AD3532 bank 1 registers (channels 8-15) */
#define AD3532R_INTERFACE_CONFIG_A_1		0x3000
#define AD3532R_OUTPUT_OPERATING_MODE_2		0x3020
#define AD3532R_OUTPUT_OPERATING_MODE_3		0x3021
#define AD3532R_OUTPUT_CONTROL_1		0x302A
#define AD3532R_REFERENCE_CONTROL_1		0x303C
#define AD3532R_SW_LDAC_TRIG_1			0x30E5
#define AD3532R_INPUT_CH_1			0x30EB
#define AD3532R_MAX_REG_ADDR			0x30F9

#define AD3530R_SLD_TRIG_A			BIT(7)
#define AD3530R_OUTPUT_CONTROL_RANGE		BIT(2)
#define AD3530R_REFERENCE_CONTROL_SEL		BIT(0)
#define AD3530R_REG_VAL_MASK			GENMASK(15, 0)
#define AD3530R_OP_MODE_CHAN_MSK(chan)		(GENMASK(1, 0) << 2 * (chan))

#define AD3530R_SW_RESET			(BIT(7) | BIT(0))
#define AD3530R_INTERNAL_VREF_mV		2500
#define AD3530R_LDAC_PULSE_US			100

#define AD3530R_DAC_MAX_VAL			GENMASK(15, 0)
#define AD3530R_CH_PER_REG			4
#define AD3530R_CH_PER_BANK			8
#define AD3531R_MAX_CHANNELS			4
#define AD3532R_MAX_CHANNELS			16

enum ad3530r_mode {
	AD3530R_NORMAL_OP,
	AD3530R_POWERDOWN_1K,
	AD3530R_POWERDOWN_7K7,
	AD3530R_POWERDOWN_32K,
};

struct ad3530r_chan {
	enum ad3530r_mode powerdown_mode;
	bool powerdown;
};

struct ad3530r_chip_info {
	const char *name;
	const struct iio_chan_spec *channels;
	const struct regmap_config *regmap_config;
	int (*input_ch_reg)(unsigned int channel);
	int (*sw_ldac_trig_reg)(unsigned int channel);
	const unsigned int *interface_config_a;
	const unsigned int *output_control;
	const unsigned int *reference_control;
	const unsigned int *op_mode;
	unsigned int num_channels;
	unsigned int num_banks;
	unsigned int num_op_mode_regs;
	bool internal_ref_support;
};

struct ad3530r_state {
	struct regmap *regmap;
	/* lock to protect against multiple access to the device and shared data */
	struct mutex lock;
	struct ad3530r_chan chan[AD3532R_MAX_CHANNELS];
	const struct ad3530r_chip_info *chip_info;
	struct gpio_desc *ldac_gpio;
	int vref_mV;
	/*
	 * DMA (thus cache coherency maintenance) may require the transfer
	 * buffers to live in their own cache lines.
	 */
	__be16 buf __aligned(IIO_DMA_MINALIGN);
};

static int ad3530r_input_ch_reg(unsigned int channel)
{
	return 2 * channel + AD3530R_INPUT_CH;
}

static int ad3531r_input_ch_reg(unsigned int channel)
{
	return 2 * channel + AD3531R_INPUT_CH;
}

static int ad3532r_input_ch_reg(unsigned int channel)
{
	unsigned int bank = channel / AD3530R_CH_PER_BANK;
	unsigned int local_ch = channel % AD3530R_CH_PER_BANK;

	return 2 * local_ch + (bank ? AD3532R_INPUT_CH_1 : AD3532R_INPUT_CH_0);
}

static const char * const ad3530r_powerdown_modes[] = {
	"1kohm_to_gnd",
	"7.7kohm_to_gnd",
	"32kohm_to_gnd",
};

static const char * const ad3531r_powerdown_modes[] = {
	"500ohm_to_gnd",
	"3.85kohm_to_gnd",
	"16kohm_to_gnd",
};

static const char * const ad3532r_powerdown_modes[] = {
	"1kohm_to_gnd",
	"10kohm_to_gnd",
	"three_state",
};

static int ad3530r_get_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	return st->chan[chan->channel].powerdown_mode - 1;
}

static int ad3530r_set_powerdown_mode(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      unsigned int mode)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	st->chan[chan->channel].powerdown_mode = mode + 1;

	return 0;
}

static const struct iio_enum ad3530r_powerdown_mode_enum = {
	.items = ad3530r_powerdown_modes,
	.num_items = ARRAY_SIZE(ad3530r_powerdown_modes),
	.get = ad3530r_get_powerdown_mode,
	.set = ad3530r_set_powerdown_mode,
};

static const struct iio_enum ad3531r_powerdown_mode_enum = {
	.items = ad3531r_powerdown_modes,
	.num_items = ARRAY_SIZE(ad3531r_powerdown_modes),
	.get = ad3530r_get_powerdown_mode,
	.set = ad3530r_set_powerdown_mode,
};

static const struct iio_enum ad3532r_powerdown_mode_enum = {
	.items = ad3532r_powerdown_modes,
	.num_items = ARRAY_SIZE(ad3532r_powerdown_modes),
	.get = ad3530r_get_powerdown_mode,
	.set = ad3530r_set_powerdown_mode,
};

static ssize_t ad3530r_get_dac_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 char *buf)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);
	return sysfs_emit(buf, "%d\n", st->chan[chan->channel].powerdown);
}

static ssize_t ad3530r_set_dac_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 const char *buf, size_t len)
{
	struct ad3530r_state *st = iio_priv(indio_dev);
	int ret;
	unsigned int reg, pdmode, mask, val;
	bool powerdown;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	guard(mutex)(&st->lock);
	reg = chan->channel < AD3531R_MAX_CHANNELS ?
	      AD3530R_OUTPUT_OPERATING_MODE_0 :
	      AD3530R_OUTPUT_OPERATING_MODE_1;
	pdmode = powerdown ? st->chan[chan->channel].powerdown_mode : 0;
	mask = chan->channel < AD3531R_MAX_CHANNELS ?
	       AD3530R_OP_MODE_CHAN_MSK(chan->channel) :
	       AD3530R_OP_MODE_CHAN_MSK(chan->channel - 4);
	val = field_prep(mask, pdmode);

	ret = regmap_update_bits(st->regmap, reg, mask, val);
	if (ret)
		return ret;

	st->chan[chan->channel].powerdown = powerdown;

	return len;
}

static ssize_t ad3532r_set_dac_powerdown(struct iio_dev *indio_dev,
					 uintptr_t private,
					 const struct iio_chan_spec *chan,
					 const char *buf, size_t len)
{
	struct ad3530r_state *st = iio_priv(indio_dev);
	unsigned int bank, local_ch, reg_in_bank, ch_in_reg;
	unsigned int reg, mask, val;
	bool powerdown;
	int ret;

	ret = kstrtobool(buf, &powerdown);
	if (ret)
		return ret;

	bank = chan->channel / AD3530R_CH_PER_BANK;
	local_ch = chan->channel % AD3530R_CH_PER_BANK;
	reg_in_bank = local_ch / AD3530R_CH_PER_REG;
	ch_in_reg = local_ch % AD3530R_CH_PER_REG;

	reg = reg_in_bank + (bank ? AD3532R_OUTPUT_OPERATING_MODE_2 :
				    AD3532R_OUTPUT_OPERATING_MODE_0);
	mask = AD3530R_OP_MODE_CHAN_MSK(ch_in_reg);

	guard(mutex)(&st->lock);
	if (powerdown) {
		val = field_prep(mask, st->chan[chan->channel].powerdown_mode);
		ret = regmap_update_bits(st->regmap, reg, mask, val);
	} else {
		ret = regmap_clear_bits(st->regmap, reg, mask);
	}
	if (ret)
		return ret;

	st->chan[chan->channel].powerdown = powerdown;

	return len;
}

static int ad3530r_trigger_sw_ldac_reg(unsigned int channel)
{
	return AD3530R_SW_LDAC_TRIG_A;
}

static int ad3531r_trigger_sw_ldac_reg(unsigned int channel)
{
	return AD3531R_SW_LDAC_TRIG_A;
}

static int ad3532r_trigger_sw_ldac_reg(unsigned int channel)
{
	unsigned int bank = channel / AD3530R_CH_PER_BANK;

	return bank ? AD3532R_SW_LDAC_TRIG_1 : AD3532R_SW_LDAC_TRIG_0;
}

static int ad3530r_trigger_hw_ldac(struct gpio_desc *ldac_gpio)
{
	gpiod_set_value_cansleep(ldac_gpio, 1);
	fsleep(AD3530R_LDAC_PULSE_US);
	gpiod_set_value_cansleep(ldac_gpio, 0);

	return 0;
}

static int ad3530r_dac_write(struct ad3530r_state *st, unsigned int chan,
			     unsigned int val)
{
	int ret;

	guard(mutex)(&st->lock);
	st->buf = cpu_to_be16(val);

	ret = regmap_bulk_write(st->regmap, st->chip_info->input_ch_reg(chan),
				&st->buf, sizeof(st->buf));
	if (ret)
		return ret;

	if (st->ldac_gpio)
		return ad3530r_trigger_hw_ldac(st->ldac_gpio);

	return regmap_set_bits(st->regmap, st->chip_info->sw_ldac_trig_reg(chan),
			       AD3530R_SLD_TRIG_A);
}

static int ad3530r_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct ad3530r_state *st = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&st->lock);
	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(st->regmap,
				       st->chip_info->input_ch_reg(chan->channel),
				       &st->buf, sizeof(st->buf));
		if (ret)
			return ret;

		*val = FIELD_GET(AD3530R_REG_VAL_MASK, be16_to_cpu(st->buf));

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_mV;
		*val2 = 16;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int ad3530r_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long info)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (val < 0 || val > AD3530R_DAC_MAX_VAL)
			return -EINVAL;

		return ad3530r_dac_write(st, chan->channel, val);
	default:
		return -EINVAL;
	}
}

static int ad3530r_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct ad3530r_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static const struct iio_chan_spec_ext_info ad3530r_ext_info[] = {
	{
		.name = "powerdown",
		.shared = IIO_SEPARATE,
		.read = ad3530r_get_dac_powerdown,
		.write = ad3530r_set_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad3530r_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE,
			   &ad3530r_powerdown_mode_enum),
	{ }
};

static const struct iio_chan_spec_ext_info ad3531r_ext_info[] = {
	{
		.name = "powerdown",
		.shared = IIO_SEPARATE,
		.read = ad3530r_get_dac_powerdown,
		.write = ad3530r_set_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad3531r_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE,
			   &ad3531r_powerdown_mode_enum),
	{ }
};

static const struct iio_chan_spec_ext_info ad3532r_ext_info[] = {
	{
		.name = "powerdown",
		.shared = IIO_SEPARATE,
		.read = ad3530r_get_dac_powerdown,
		.write = ad3532r_set_dac_powerdown,
	},
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad3532r_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE,
			   &ad3532r_powerdown_mode_enum),
	{ }
};

#define AD3530R_CHAN(_chan, _ext_info)				\
{								\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = _chan,					\
	.output = 1,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.ext_info = _ext_info,					\
}

static const struct iio_chan_spec ad3530r_channels[] = {
	AD3530R_CHAN(0, ad3530r_ext_info),
	AD3530R_CHAN(1, ad3530r_ext_info),
	AD3530R_CHAN(2, ad3530r_ext_info),
	AD3530R_CHAN(3, ad3530r_ext_info),
	AD3530R_CHAN(4, ad3530r_ext_info),
	AD3530R_CHAN(5, ad3530r_ext_info),
	AD3530R_CHAN(6, ad3530r_ext_info),
	AD3530R_CHAN(7, ad3530r_ext_info),
};

static const struct iio_chan_spec ad3531r_channels[] = {
	AD3530R_CHAN(0, ad3531r_ext_info),
	AD3530R_CHAN(1, ad3531r_ext_info),
	AD3530R_CHAN(2, ad3531r_ext_info),
	AD3530R_CHAN(3, ad3531r_ext_info),
};

static const struct iio_chan_spec ad3532r_channels[] = {
	AD3530R_CHAN(0, ad3532r_ext_info),
	AD3530R_CHAN(1, ad3532r_ext_info),
	AD3530R_CHAN(2, ad3532r_ext_info),
	AD3530R_CHAN(3, ad3532r_ext_info),
	AD3530R_CHAN(4, ad3532r_ext_info),
	AD3530R_CHAN(5, ad3532r_ext_info),
	AD3530R_CHAN(6, ad3532r_ext_info),
	AD3530R_CHAN(7, ad3532r_ext_info),
	AD3530R_CHAN(8, ad3532r_ext_info),
	AD3530R_CHAN(9, ad3532r_ext_info),
	AD3530R_CHAN(10, ad3532r_ext_info),
	AD3530R_CHAN(11, ad3532r_ext_info),
	AD3530R_CHAN(12, ad3532r_ext_info),
	AD3530R_CHAN(13, ad3532r_ext_info),
	AD3530R_CHAN(14, ad3532r_ext_info),
	AD3530R_CHAN(15, ad3532r_ext_info),
};

static const unsigned int ad3530r_if_config[] = {
	AD3530R_INTERFACE_CONFIG_A,
};

static const unsigned int ad3530r_out_ctrl[] = {
	AD3530R_OUTPUT_CONTROL_0,
};

static const unsigned int ad3530r_ref_ctrl[] = {
	AD3530R_REFERENCE_CONTROL_0,
};

static const unsigned int ad3530r_op_mode[] = {
	AD3530R_OUTPUT_OPERATING_MODE_0,
	AD3530R_OUTPUT_OPERATING_MODE_1,
};

static const unsigned int ad3531r_op_mode[] = {
	AD3530R_OUTPUT_OPERATING_MODE_0,
};

static const unsigned int ad3532r_if_config[] = {
	AD3532R_INTERFACE_CONFIG_A_0,
	AD3532R_INTERFACE_CONFIG_A_1,
};

static const unsigned int ad3532r_out_ctrl[] = {
	AD3532R_OUTPUT_CONTROL_0,
	AD3532R_OUTPUT_CONTROL_1,
};

static const unsigned int ad3532r_ref_ctrl[] = {
	AD3532R_REFERENCE_CONTROL_0,
	AD3532R_REFERENCE_CONTROL_1,
};

static const unsigned int ad3532r_op_mode[] = {
	AD3532R_OUTPUT_OPERATING_MODE_0,
	AD3532R_OUTPUT_OPERATING_MODE_1,
	AD3532R_OUTPUT_OPERATING_MODE_2,
	AD3532R_OUTPUT_OPERATING_MODE_3,
};

static const struct regmap_config ad3530r_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = AD3530R_MAX_REG_ADDR,
};

static const struct regmap_config ad3532r_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = AD3532R_MAX_REG_ADDR,
};

static const struct ad3530r_chip_info ad3530_chip = {
	.name = "ad3530",
	.channels = ad3530r_channels,
	.regmap_config = &ad3530r_regmap_config,
	.num_channels = ARRAY_SIZE(ad3530r_channels),
	.sw_ldac_trig_reg = ad3530r_trigger_sw_ldac_reg,
	.input_ch_reg = ad3530r_input_ch_reg,
	.interface_config_a = ad3530r_if_config,
	.output_control = ad3530r_out_ctrl,
	.reference_control = ad3530r_ref_ctrl,
	.op_mode = ad3530r_op_mode,
	.num_banks = ARRAY_SIZE(ad3530r_if_config),
	.num_op_mode_regs = ARRAY_SIZE(ad3530r_op_mode),
	.internal_ref_support = false,
};

static const struct ad3530r_chip_info ad3530r_chip = {
	.name = "ad3530r",
	.channels = ad3530r_channels,
	.regmap_config = &ad3530r_regmap_config,
	.num_channels = ARRAY_SIZE(ad3530r_channels),
	.sw_ldac_trig_reg = ad3530r_trigger_sw_ldac_reg,
	.input_ch_reg = ad3530r_input_ch_reg,
	.interface_config_a = ad3530r_if_config,
	.output_control = ad3530r_out_ctrl,
	.reference_control = ad3530r_ref_ctrl,
	.op_mode = ad3530r_op_mode,
	.num_banks = ARRAY_SIZE(ad3530r_if_config),
	.num_op_mode_regs = ARRAY_SIZE(ad3530r_op_mode),
	.internal_ref_support = true,
};

static const struct ad3530r_chip_info ad3531_chip = {
	.name = "ad3531",
	.channels = ad3531r_channels,
	.regmap_config = &ad3530r_regmap_config,
	.num_channels = ARRAY_SIZE(ad3531r_channels),
	.sw_ldac_trig_reg = ad3531r_trigger_sw_ldac_reg,
	.input_ch_reg = ad3531r_input_ch_reg,
	.interface_config_a = ad3530r_if_config,
	.output_control = ad3530r_out_ctrl,
	.reference_control = ad3530r_ref_ctrl,
	.op_mode = ad3531r_op_mode,
	.num_banks = ARRAY_SIZE(ad3530r_if_config),
	.num_op_mode_regs = ARRAY_SIZE(ad3531r_op_mode),
	.internal_ref_support = false,
};

static const struct ad3530r_chip_info ad3531r_chip = {
	.name = "ad3531r",
	.channels = ad3531r_channels,
	.regmap_config = &ad3530r_regmap_config,
	.num_channels = ARRAY_SIZE(ad3531r_channels),
	.sw_ldac_trig_reg = ad3531r_trigger_sw_ldac_reg,
	.input_ch_reg = ad3531r_input_ch_reg,
	.interface_config_a = ad3530r_if_config,
	.output_control = ad3530r_out_ctrl,
	.reference_control = ad3530r_ref_ctrl,
	.op_mode = ad3531r_op_mode,
	.num_banks = ARRAY_SIZE(ad3530r_if_config),
	.num_op_mode_regs = ARRAY_SIZE(ad3531r_op_mode),
	.internal_ref_support = true,
};

static const struct ad3530r_chip_info ad3532_chip = {
	.name = "ad3532",
	.channels = ad3532r_channels,
	.regmap_config = &ad3532r_regmap_config,
	.num_channels = ARRAY_SIZE(ad3532r_channels),
	.sw_ldac_trig_reg = ad3532r_trigger_sw_ldac_reg,
	.input_ch_reg = ad3532r_input_ch_reg,
	.interface_config_a = ad3532r_if_config,
	.output_control = ad3532r_out_ctrl,
	.reference_control = ad3532r_ref_ctrl,
	.op_mode = ad3532r_op_mode,
	.num_banks = ARRAY_SIZE(ad3532r_if_config),
	.num_op_mode_regs = ARRAY_SIZE(ad3532r_op_mode),
	.internal_ref_support = false,
};

static const struct ad3530r_chip_info ad3532r_chip = {
	.name = "ad3532r",
	.channels = ad3532r_channels,
	.regmap_config = &ad3532r_regmap_config,
	.num_channels = ARRAY_SIZE(ad3532r_channels),
	.sw_ldac_trig_reg = ad3532r_trigger_sw_ldac_reg,
	.input_ch_reg = ad3532r_input_ch_reg,
	.interface_config_a = ad3532r_if_config,
	.output_control = ad3532r_out_ctrl,
	.reference_control = ad3532r_ref_ctrl,
	.op_mode = ad3532r_op_mode,
	.num_banks = ARRAY_SIZE(ad3532r_if_config),
	.num_op_mode_regs = ARRAY_SIZE(ad3532r_op_mode),
	.internal_ref_support = true,
};

static int ad3530r_set_reg_bank_bits(const struct ad3530r_state *st,
				     const unsigned int *regs,
				     unsigned int num_regs,
				     unsigned int mask)
{
	int ret;

	for (unsigned int i = 0; i < num_regs; i++) {
		ret = regmap_set_bits(st->regmap, regs[i], mask);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad3530r_write_reg_banks(const struct ad3530r_state *st,
				   const unsigned int *regs,
				   unsigned int num_regs,
				   unsigned int val)
{
	int ret;

	for (unsigned int i = 0; i < num_regs; i++) {
		ret = regmap_write(st->regmap, regs[i], val);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad3530r_setup(struct ad3530r_state *st, int external_vref_uV)
{
	const struct ad3530r_chip_info *chip_info = st->chip_info;
	struct device *dev = regmap_get_device(st->regmap);
	struct gpio_desc *reset_gpio;
	u8 range_multiplier, val;
	int ret;

	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(dev, PTR_ERR(reset_gpio),
				     "Failed to get reset GPIO\n");

	if (reset_gpio) {
		/* Perform hardware reset */
		fsleep(1 * USEC_PER_MSEC);
		gpiod_set_value_cansleep(reset_gpio, 0);
	} else {
		/* Perform software reset */
		ret = ad3530r_set_reg_bank_bits(st, chip_info->interface_config_a,
						chip_info->num_banks,
						AD3530R_SW_RESET);
		if (ret)
			return ret;
	}

	fsleep(10 * USEC_PER_MSEC);

	range_multiplier = 1;
	if (device_property_read_bool(dev, "adi,range-double")) {
		ret = ad3530r_set_reg_bank_bits(st, chip_info->output_control,
						chip_info->num_banks,
						AD3530R_OUTPUT_CONTROL_RANGE);
		if (ret)
			return ret;

		range_multiplier = 2;
	}

	if (external_vref_uV) {
		st->vref_mV = range_multiplier * external_vref_uV / MILLI;
	} else {
		ret = ad3530r_set_reg_bank_bits(st, chip_info->reference_control,
						chip_info->num_banks,
						AD3530R_REFERENCE_CONTROL_SEL);
		if (ret)
			return ret;

		st->vref_mV = range_multiplier * AD3530R_INTERNAL_VREF_mV;
	}

	/* Set normal operating mode for all channels */
	val = FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(0), AD3530R_NORMAL_OP) |
	      FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(1), AD3530R_NORMAL_OP) |
	      FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(2), AD3530R_NORMAL_OP) |
	      FIELD_PREP(AD3530R_OP_MODE_CHAN_MSK(3), AD3530R_NORMAL_OP);

	ret = ad3530r_write_reg_banks(st, st->chip_info->op_mode,
				      st->chip_info->num_op_mode_regs, val);
	if (ret)
		return ret;

	for (unsigned int i = 0; i < st->chip_info->num_channels; i++)
		st->chan[i].powerdown_mode = AD3530R_POWERDOWN_32K;

	st->ldac_gpio = devm_gpiod_get_optional(dev, "ldac", GPIOD_OUT_LOW);
	if (IS_ERR(st->ldac_gpio))
		return dev_err_probe(dev, PTR_ERR(st->ldac_gpio),
				     "Failed to get ldac GPIO\n");

	return 0;
}

static const struct iio_info ad3530r_info = {
	.read_raw = ad3530r_read_raw,
	.write_raw = ad3530r_write_raw,
	.debugfs_reg_access = ad3530r_reg_access,
};

static int ad3530r_probe(struct spi_device *spi)
{
	static const char * const regulators[] = { "vdd", "iovdd" };
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad3530r_state *st;
	int ret, external_vref_uV;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->chip_info = spi_get_device_match_data(spi);
	if (!st->chip_info)
		return -ENODEV;

	st->regmap = devm_regmap_init_spi(spi, st->chip_info->regmap_config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "Failed to init regmap");

	ret = devm_mutex_init(dev, &st->lock);
	if (ret)
		return ret;

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(regulators),
					     regulators);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	external_vref_uV = devm_regulator_get_enable_read_voltage(dev, "ref");
	if (external_vref_uV < 0 && external_vref_uV != -ENODEV)
		return external_vref_uV;

	if (external_vref_uV == -ENODEV)
		external_vref_uV = 0;

	if (!st->chip_info->internal_ref_support && external_vref_uV == 0)
		return -ENODEV;

	ret = ad3530r_setup(st, external_vref_uV);
	if (ret)
		return ret;

	indio_dev->name = st->chip_info->name;
	indio_dev->info = &ad3530r_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id ad3530r_id[] = {
	{ .name = "ad3530", .driver_data = (kernel_ulong_t)&ad3530_chip },
	{ .name = "ad3530r", .driver_data = (kernel_ulong_t)&ad3530r_chip },
	{ .name = "ad3531", .driver_data = (kernel_ulong_t)&ad3531_chip },
	{ .name = "ad3531r", .driver_data = (kernel_ulong_t)&ad3531r_chip },
	{ .name = "ad3532", .driver_data = (kernel_ulong_t)&ad3532_chip },
	{ .name = "ad3532r", .driver_data = (kernel_ulong_t)&ad3532r_chip },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad3530r_id);

static const struct of_device_id ad3530r_of_match[] = {
	{ .compatible = "adi,ad3530", .data = &ad3530_chip },
	{ .compatible = "adi,ad3530r", .data = &ad3530r_chip },
	{ .compatible = "adi,ad3531", .data = &ad3531_chip },
	{ .compatible = "adi,ad3531r", .data = &ad3531r_chip },
	{ .compatible = "adi,ad3532", .data = &ad3532_chip },
	{ .compatible = "adi,ad3532r", .data = &ad3532r_chip },
	{ }
};
MODULE_DEVICE_TABLE(of, ad3530r_of_match);

static struct spi_driver ad3530r_driver = {
	.driver = {
		.name = "ad3530r",
		.of_match_table = ad3530r_of_match,
	},
	.probe = ad3530r_probe,
	.id_table = ad3530r_id,
};
module_spi_driver(ad3530r_driver);

MODULE_AUTHOR("Kim Seer Paller <kimseer.paller@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD3530R and Similar DACs Driver");
MODULE_LICENSE("GPL");
