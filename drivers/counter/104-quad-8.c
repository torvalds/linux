// SPDX-License-Identifier: GPL-2.0
/*
 * Counter driver for the ACCES 104-QUAD-8
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This driver supports the ACCES 104-QUAD-8 and ACCES 104-QUAD-4.
 */
#include <linux/bitops.h>
#include <linux/counter.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

#define QUAD8_EXTENT 32

static unsigned int base[max_num_isa_dev(QUAD8_EXTENT)];
static unsigned int num_quad8;
module_param_array(base, uint, &num_quad8, 0);
MODULE_PARM_DESC(base, "ACCES 104-QUAD-8 base addresses");

#define QUAD8_NUM_COUNTERS 8

/**
 * struct quad8_iio - IIO device private data structure
 * @counter:		instance of the counter_device
 * @fck_prescaler:	array of filter clock prescaler configurations
 * @preset:		array of preset values
 * @count_mode:		array of count mode configurations
 * @quadrature_mode:	array of quadrature mode configurations
 * @quadrature_scale:	array of quadrature mode scale configurations
 * @ab_enable:		array of A and B inputs enable configurations
 * @preset_enable:	array of set_to_preset_on_index attribute configurations
 * @synchronous_mode:	array of index function synchronous mode configurations
 * @index_polarity:	array of index function polarity configurations
 * @cable_fault_enable:	differential encoder cable status enable configurations
 * @base:		base port address of the IIO device
 */
struct quad8_iio {
	struct mutex lock;
	struct counter_device counter;
	unsigned int fck_prescaler[QUAD8_NUM_COUNTERS];
	unsigned int preset[QUAD8_NUM_COUNTERS];
	unsigned int count_mode[QUAD8_NUM_COUNTERS];
	unsigned int quadrature_mode[QUAD8_NUM_COUNTERS];
	unsigned int quadrature_scale[QUAD8_NUM_COUNTERS];
	unsigned int ab_enable[QUAD8_NUM_COUNTERS];
	unsigned int preset_enable[QUAD8_NUM_COUNTERS];
	unsigned int synchronous_mode[QUAD8_NUM_COUNTERS];
	unsigned int index_polarity[QUAD8_NUM_COUNTERS];
	unsigned int cable_fault_enable;
	unsigned int base;
};

#define QUAD8_REG_CHAN_OP 0x11
#define QUAD8_REG_INDEX_INPUT_LEVELS 0x16
#define QUAD8_DIFF_ENCODER_CABLE_STATUS 0x17
/* Borrow Toggle flip-flop */
#define QUAD8_FLAG_BT BIT(0)
/* Carry Toggle flip-flop */
#define QUAD8_FLAG_CT BIT(1)
/* Error flag */
#define QUAD8_FLAG_E BIT(4)
/* Up/Down flag */
#define QUAD8_FLAG_UD BIT(5)
/* Reset and Load Signal Decoders */
#define QUAD8_CTR_RLD 0x00
/* Counter Mode Register */
#define QUAD8_CTR_CMR 0x20
/* Input / Output Control Register */
#define QUAD8_CTR_IOR 0x40
/* Index Control Register */
#define QUAD8_CTR_IDR 0x60
/* Reset Byte Pointer (three byte data pointer) */
#define QUAD8_RLD_RESET_BP 0x01
/* Reset Counter */
#define QUAD8_RLD_RESET_CNTR 0x02
/* Reset Borrow Toggle, Carry Toggle, Compare Toggle, and Sign flags */
#define QUAD8_RLD_RESET_FLAGS 0x04
/* Reset Error flag */
#define QUAD8_RLD_RESET_E 0x06
/* Preset Register to Counter */
#define QUAD8_RLD_PRESET_CNTR 0x08
/* Transfer Counter to Output Latch */
#define QUAD8_RLD_CNTR_OUT 0x10
/* Transfer Preset Register LSB to FCK Prescaler */
#define QUAD8_RLD_PRESET_PSC 0x18
#define QUAD8_CHAN_OP_ENABLE_COUNTERS 0x00
#define QUAD8_CHAN_OP_RESET_COUNTERS 0x01
#define QUAD8_CMR_QUADRATURE_X1 0x08
#define QUAD8_CMR_QUADRATURE_X2 0x10
#define QUAD8_CMR_QUADRATURE_X4 0x18


static int quad8_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel;
	unsigned int flags;
	unsigned int borrow;
	unsigned int carry;
	int i;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_INDEX) {
			*val = !!(inb(priv->base + QUAD8_REG_INDEX_INPUT_LEVELS)
				& BIT(chan->channel));
			return IIO_VAL_INT;
		}

		flags = inb(base_offset + 1);
		borrow = flags & QUAD8_FLAG_BT;
		carry = !!(flags & QUAD8_FLAG_CT);

		/* Borrow XOR Carry effectively doubles count range */
		*val = (borrow ^ carry) << 24;

		mutex_lock(&priv->lock);

		/* Reset Byte Pointer; transfer Counter to Output Latch */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP | QUAD8_RLD_CNTR_OUT,
		     base_offset + 1);

		for (i = 0; i < 3; i++)
			*val |= (unsigned int)inb(base_offset) << (8 * i);

		mutex_unlock(&priv->lock);

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_ENABLE:
		*val = priv->ab_enable[chan->channel];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		*val2 = priv->quadrature_scale[chan->channel];
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int quad8_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel;
	int i;
	unsigned int ior_cfg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_INDEX)
			return -EINVAL;

		/* Only 24-bit values are supported */
		if ((unsigned int)val > 0xFFFFFF)
			return -EINVAL;

		mutex_lock(&priv->lock);

		/* Reset Byte Pointer */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

		/* Counter can only be set via Preset Register */
		for (i = 0; i < 3; i++)
			outb(val >> (8 * i), base_offset);

		/* Transfer Preset Register to Counter */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_PRESET_CNTR, base_offset + 1);

		/* Reset Byte Pointer */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

		/* Set Preset Register back to original value */
		val = priv->preset[chan->channel];
		for (i = 0; i < 3; i++)
			outb(val >> (8 * i), base_offset);

		/* Reset Borrow, Carry, Compare, and Sign flags */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_FLAGS, base_offset + 1);
		/* Reset Error flag */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_E, base_offset + 1);

		mutex_unlock(&priv->lock);

		return 0;
	case IIO_CHAN_INFO_ENABLE:
		/* only boolean values accepted */
		if (val < 0 || val > 1)
			return -EINVAL;

		mutex_lock(&priv->lock);

		priv->ab_enable[chan->channel] = val;

		ior_cfg = val | priv->preset_enable[chan->channel] << 1;

		/* Load I/O control configuration */
		outb(QUAD8_CTR_IOR | ior_cfg, base_offset + 1);

		mutex_unlock(&priv->lock);

		return 0;
	case IIO_CHAN_INFO_SCALE:
		mutex_lock(&priv->lock);

		/* Quadrature scaling only available in quadrature mode */
		if (!priv->quadrature_mode[chan->channel] &&
				(val2 || val != 1)) {
			mutex_unlock(&priv->lock);
			return -EINVAL;
		}

		/* Only three gain states (1, 0.5, 0.25) */
		if (val == 1 && !val2)
			priv->quadrature_scale[chan->channel] = 0;
		else if (!val)
			switch (val2) {
			case 500000:
				priv->quadrature_scale[chan->channel] = 1;
				break;
			case 250000:
				priv->quadrature_scale[chan->channel] = 2;
				break;
			default:
				mutex_unlock(&priv->lock);
				return -EINVAL;
			}
		else {
			mutex_unlock(&priv->lock);
			return -EINVAL;
		}

		mutex_unlock(&priv->lock);
		return 0;
	}

	return -EINVAL;
}

static const struct iio_info quad8_info = {
	.read_raw = quad8_read_raw,
	.write_raw = quad8_write_raw
};

static ssize_t quad8_read_preset(struct iio_dev *indio_dev, uintptr_t private,
	const struct iio_chan_spec *chan, char *buf)
{
	const struct quad8_iio *const priv = iio_priv(indio_dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", priv->preset[chan->channel]);
}

static ssize_t quad8_write_preset(struct iio_dev *indio_dev, uintptr_t private,
	const struct iio_chan_spec *chan, const char *buf, size_t len)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel;
	unsigned int preset;
	int ret;
	int i;

	ret = kstrtouint(buf, 0, &preset);
	if (ret)
		return ret;

	/* Only 24-bit values are supported */
	if (preset > 0xFFFFFF)
		return -EINVAL;

	mutex_lock(&priv->lock);

	priv->preset[chan->channel] = preset;

	/* Reset Byte Pointer */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

	/* Set Preset Register */
	for (i = 0; i < 3; i++)
		outb(preset >> (8 * i), base_offset);

	mutex_unlock(&priv->lock);

	return len;
}

static ssize_t quad8_read_set_to_preset_on_index(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, char *buf)
{
	const struct quad8_iio *const priv = iio_priv(indio_dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		!priv->preset_enable[chan->channel]);
}

static ssize_t quad8_write_set_to_preset_on_index(struct iio_dev *indio_dev,
	uintptr_t private, const struct iio_chan_spec *chan, const char *buf,
	size_t len)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel + 1;
	bool preset_enable;
	int ret;
	unsigned int ior_cfg;

	ret = kstrtobool(buf, &preset_enable);
	if (ret)
		return ret;

	/* Preset enable is active low in Input/Output Control register */
	preset_enable = !preset_enable;

	mutex_lock(&priv->lock);

	priv->preset_enable[chan->channel] = preset_enable;

	ior_cfg = priv->ab_enable[chan->channel] |
		(unsigned int)preset_enable << 1;

	/* Load I/O control configuration to Input / Output Control Register */
	outb(QUAD8_CTR_IOR | ior_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return len;
}

static const char *const quad8_noise_error_states[] = {
	"No excessive noise is present at the count inputs",
	"Excessive noise is present at the count inputs"
};

static int quad8_get_noise_error(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel + 1;

	return !!(inb(base_offset) & QUAD8_FLAG_E);
}

static const struct iio_enum quad8_noise_error_enum = {
	.items = quad8_noise_error_states,
	.num_items = ARRAY_SIZE(quad8_noise_error_states),
	.get = quad8_get_noise_error
};

static const char *const quad8_count_direction_states[] = {
	"down",
	"up"
};

static int quad8_get_count_direction(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel + 1;

	return !!(inb(base_offset) & QUAD8_FLAG_UD);
}

static const struct iio_enum quad8_count_direction_enum = {
	.items = quad8_count_direction_states,
	.num_items = ARRAY_SIZE(quad8_count_direction_states),
	.get = quad8_get_count_direction
};

static const char *const quad8_count_modes[] = {
	"normal",
	"range limit",
	"non-recycle",
	"modulo-n"
};

static int quad8_set_count_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int cnt_mode)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	unsigned int mode_cfg = cnt_mode << 1;
	const int base_offset = priv->base + 2 * chan->channel + 1;

	mutex_lock(&priv->lock);

	priv->count_mode[chan->channel] = cnt_mode;

	/* Add quadrature mode configuration */
	if (priv->quadrature_mode[chan->channel])
		mode_cfg |= (priv->quadrature_scale[chan->channel] + 1) << 3;

	/* Load mode configuration to Counter Mode Register */
	outb(QUAD8_CTR_CMR | mode_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static int quad8_get_count_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	const struct quad8_iio *const priv = iio_priv(indio_dev);

	return priv->count_mode[chan->channel];
}

static const struct iio_enum quad8_count_mode_enum = {
	.items = quad8_count_modes,
	.num_items = ARRAY_SIZE(quad8_count_modes),
	.set = quad8_set_count_mode,
	.get = quad8_get_count_mode
};

static const char *const quad8_synchronous_modes[] = {
	"non-synchronous",
	"synchronous"
};

static int quad8_set_synchronous_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int synchronous_mode)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel + 1;
	unsigned int idr_cfg = synchronous_mode;

	mutex_lock(&priv->lock);

	idr_cfg |= priv->index_polarity[chan->channel] << 1;

	/* Index function must be non-synchronous in non-quadrature mode */
	if (synchronous_mode && !priv->quadrature_mode[chan->channel]) {
		mutex_unlock(&priv->lock);
		return -EINVAL;
	}

	priv->synchronous_mode[chan->channel] = synchronous_mode;

	/* Load Index Control configuration to Index Control Register */
	outb(QUAD8_CTR_IDR | idr_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static int quad8_get_synchronous_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	const struct quad8_iio *const priv = iio_priv(indio_dev);

	return priv->synchronous_mode[chan->channel];
}

static const struct iio_enum quad8_synchronous_mode_enum = {
	.items = quad8_synchronous_modes,
	.num_items = ARRAY_SIZE(quad8_synchronous_modes),
	.set = quad8_set_synchronous_mode,
	.get = quad8_get_synchronous_mode
};

static const char *const quad8_quadrature_modes[] = {
	"non-quadrature",
	"quadrature"
};

static int quad8_set_quadrature_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int quadrature_mode)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel + 1;
	unsigned int mode_cfg;

	mutex_lock(&priv->lock);

	mode_cfg = priv->count_mode[chan->channel] << 1;

	if (quadrature_mode)
		mode_cfg |= (priv->quadrature_scale[chan->channel] + 1) << 3;
	else {
		/* Quadrature scaling only available in quadrature mode */
		priv->quadrature_scale[chan->channel] = 0;

		/* Synchronous function not supported in non-quadrature mode */
		if (priv->synchronous_mode[chan->channel])
			quad8_set_synchronous_mode(indio_dev, chan, 0);
	}

	priv->quadrature_mode[chan->channel] = quadrature_mode;

	/* Load mode configuration to Counter Mode Register */
	outb(QUAD8_CTR_CMR | mode_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static int quad8_get_quadrature_mode(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	const struct quad8_iio *const priv = iio_priv(indio_dev);

	return priv->quadrature_mode[chan->channel];
}

static const struct iio_enum quad8_quadrature_mode_enum = {
	.items = quad8_quadrature_modes,
	.num_items = ARRAY_SIZE(quad8_quadrature_modes),
	.set = quad8_set_quadrature_mode,
	.get = quad8_get_quadrature_mode
};

static const char *const quad8_index_polarity_modes[] = {
	"negative",
	"positive"
};

static int quad8_set_index_polarity(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int index_polarity)
{
	struct quad8_iio *const priv = iio_priv(indio_dev);
	const int base_offset = priv->base + 2 * chan->channel + 1;
	unsigned int idr_cfg = index_polarity << 1;

	mutex_lock(&priv->lock);

	idr_cfg |= priv->synchronous_mode[chan->channel];

	priv->index_polarity[chan->channel] = index_polarity;

	/* Load Index Control configuration to Index Control Register */
	outb(QUAD8_CTR_IDR | idr_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static int quad8_get_index_polarity(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan)
{
	const struct quad8_iio *const priv = iio_priv(indio_dev);

	return priv->index_polarity[chan->channel];
}

static const struct iio_enum quad8_index_polarity_enum = {
	.items = quad8_index_polarity_modes,
	.num_items = ARRAY_SIZE(quad8_index_polarity_modes),
	.set = quad8_set_index_polarity,
	.get = quad8_get_index_polarity
};

static const struct iio_chan_spec_ext_info quad8_count_ext_info[] = {
	{
		.name = "preset",
		.shared = IIO_SEPARATE,
		.read = quad8_read_preset,
		.write = quad8_write_preset
	},
	{
		.name = "set_to_preset_on_index",
		.shared = IIO_SEPARATE,
		.read = quad8_read_set_to_preset_on_index,
		.write = quad8_write_set_to_preset_on_index
	},
	IIO_ENUM("noise_error", IIO_SEPARATE, &quad8_noise_error_enum),
	IIO_ENUM_AVAILABLE("noise_error", &quad8_noise_error_enum),
	IIO_ENUM("count_direction", IIO_SEPARATE, &quad8_count_direction_enum),
	IIO_ENUM_AVAILABLE("count_direction", &quad8_count_direction_enum),
	IIO_ENUM("count_mode", IIO_SEPARATE, &quad8_count_mode_enum),
	IIO_ENUM_AVAILABLE("count_mode", &quad8_count_mode_enum),
	IIO_ENUM("quadrature_mode", IIO_SEPARATE, &quad8_quadrature_mode_enum),
	IIO_ENUM_AVAILABLE("quadrature_mode", &quad8_quadrature_mode_enum),
	{}
};

static const struct iio_chan_spec_ext_info quad8_index_ext_info[] = {
	IIO_ENUM("synchronous_mode", IIO_SEPARATE,
		&quad8_synchronous_mode_enum),
	IIO_ENUM_AVAILABLE("synchronous_mode", &quad8_synchronous_mode_enum),
	IIO_ENUM("index_polarity", IIO_SEPARATE, &quad8_index_polarity_enum),
	IIO_ENUM_AVAILABLE("index_polarity", &quad8_index_polarity_enum),
	{}
};

#define QUAD8_COUNT_CHAN(_chan) {					\
	.type = IIO_COUNT,						\
	.channel = (_chan),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
		BIT(IIO_CHAN_INFO_ENABLE) | BIT(IIO_CHAN_INFO_SCALE),	\
	.ext_info = quad8_count_ext_info,				\
	.indexed = 1							\
}

#define QUAD8_INDEX_CHAN(_chan) {			\
	.type = IIO_INDEX,				\
	.channel = (_chan),				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.ext_info = quad8_index_ext_info,		\
	.indexed = 1					\
}

static const struct iio_chan_spec quad8_channels[] = {
	QUAD8_COUNT_CHAN(0), QUAD8_INDEX_CHAN(0),
	QUAD8_COUNT_CHAN(1), QUAD8_INDEX_CHAN(1),
	QUAD8_COUNT_CHAN(2), QUAD8_INDEX_CHAN(2),
	QUAD8_COUNT_CHAN(3), QUAD8_INDEX_CHAN(3),
	QUAD8_COUNT_CHAN(4), QUAD8_INDEX_CHAN(4),
	QUAD8_COUNT_CHAN(5), QUAD8_INDEX_CHAN(5),
	QUAD8_COUNT_CHAN(6), QUAD8_INDEX_CHAN(6),
	QUAD8_COUNT_CHAN(7), QUAD8_INDEX_CHAN(7)
};

static int quad8_signal_read(struct counter_device *counter,
	struct counter_signal *signal, enum counter_signal_value *val)
{
	const struct quad8_iio *const priv = counter->priv;
	unsigned int state;

	/* Only Index signal levels can be read */
	if (signal->id < 16)
		return -EINVAL;

	state = inb(priv->base + QUAD8_REG_INDEX_INPUT_LEVELS)
		& BIT(signal->id - 16);

	*val = (state) ? COUNTER_SIGNAL_HIGH : COUNTER_SIGNAL_LOW;

	return 0;
}

static int quad8_count_read(struct counter_device *counter,
	struct counter_count *count, unsigned long *val)
{
	struct quad8_iio *const priv = counter->priv;
	const int base_offset = priv->base + 2 * count->id;
	unsigned int flags;
	unsigned int borrow;
	unsigned int carry;
	int i;

	flags = inb(base_offset + 1);
	borrow = flags & QUAD8_FLAG_BT;
	carry = !!(flags & QUAD8_FLAG_CT);

	/* Borrow XOR Carry effectively doubles count range */
	*val = (unsigned long)(borrow ^ carry) << 24;

	mutex_lock(&priv->lock);

	/* Reset Byte Pointer; transfer Counter to Output Latch */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP | QUAD8_RLD_CNTR_OUT,
	     base_offset + 1);

	for (i = 0; i < 3; i++)
		*val |= (unsigned long)inb(base_offset) << (8 * i);

	mutex_unlock(&priv->lock);

	return 0;
}

static int quad8_count_write(struct counter_device *counter,
	struct counter_count *count, unsigned long val)
{
	struct quad8_iio *const priv = counter->priv;
	const int base_offset = priv->base + 2 * count->id;
	int i;

	/* Only 24-bit values are supported */
	if (val > 0xFFFFFF)
		return -EINVAL;

	mutex_lock(&priv->lock);

	/* Reset Byte Pointer */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

	/* Counter can only be set via Preset Register */
	for (i = 0; i < 3; i++)
		outb(val >> (8 * i), base_offset);

	/* Transfer Preset Register to Counter */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_PRESET_CNTR, base_offset + 1);

	/* Reset Byte Pointer */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

	/* Set Preset Register back to original value */
	val = priv->preset[count->id];
	for (i = 0; i < 3; i++)
		outb(val >> (8 * i), base_offset);

	/* Reset Borrow, Carry, Compare, and Sign flags */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_FLAGS, base_offset + 1);
	/* Reset Error flag */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_E, base_offset + 1);

	mutex_unlock(&priv->lock);

	return 0;
}

enum quad8_count_function {
	QUAD8_COUNT_FUNCTION_PULSE_DIRECTION = 0,
	QUAD8_COUNT_FUNCTION_QUADRATURE_X1,
	QUAD8_COUNT_FUNCTION_QUADRATURE_X2,
	QUAD8_COUNT_FUNCTION_QUADRATURE_X4
};

static enum counter_count_function quad8_count_functions_list[] = {
	[QUAD8_COUNT_FUNCTION_PULSE_DIRECTION] = COUNTER_COUNT_FUNCTION_PULSE_DIRECTION,
	[QUAD8_COUNT_FUNCTION_QUADRATURE_X1] = COUNTER_COUNT_FUNCTION_QUADRATURE_X1_A,
	[QUAD8_COUNT_FUNCTION_QUADRATURE_X2] = COUNTER_COUNT_FUNCTION_QUADRATURE_X2_A,
	[QUAD8_COUNT_FUNCTION_QUADRATURE_X4] = COUNTER_COUNT_FUNCTION_QUADRATURE_X4
};

static int quad8_function_get(struct counter_device *counter,
	struct counter_count *count, size_t *function)
{
	struct quad8_iio *const priv = counter->priv;
	const int id = count->id;

	mutex_lock(&priv->lock);

	if (priv->quadrature_mode[id])
		switch (priv->quadrature_scale[id]) {
		case 0:
			*function = QUAD8_COUNT_FUNCTION_QUADRATURE_X1;
			break;
		case 1:
			*function = QUAD8_COUNT_FUNCTION_QUADRATURE_X2;
			break;
		case 2:
			*function = QUAD8_COUNT_FUNCTION_QUADRATURE_X4;
			break;
		}
	else
		*function = QUAD8_COUNT_FUNCTION_PULSE_DIRECTION;

	mutex_unlock(&priv->lock);

	return 0;
}

static int quad8_function_set(struct counter_device *counter,
	struct counter_count *count, size_t function)
{
	struct quad8_iio *const priv = counter->priv;
	const int id = count->id;
	unsigned int *const quadrature_mode = priv->quadrature_mode + id;
	unsigned int *const scale = priv->quadrature_scale + id;
	unsigned int *const synchronous_mode = priv->synchronous_mode + id;
	const int base_offset = priv->base + 2 * id + 1;
	unsigned int mode_cfg;
	unsigned int idr_cfg;

	mutex_lock(&priv->lock);

	mode_cfg = priv->count_mode[id] << 1;
	idr_cfg = priv->index_polarity[id] << 1;

	if (function == QUAD8_COUNT_FUNCTION_PULSE_DIRECTION) {
		*quadrature_mode = 0;

		/* Quadrature scaling only available in quadrature mode */
		*scale = 0;

		/* Synchronous function not supported in non-quadrature mode */
		if (*synchronous_mode) {
			*synchronous_mode = 0;
			/* Disable synchronous function mode */
			outb(QUAD8_CTR_IDR | idr_cfg, base_offset);
		}
	} else {
		*quadrature_mode = 1;

		switch (function) {
		case QUAD8_COUNT_FUNCTION_QUADRATURE_X1:
			*scale = 0;
			mode_cfg |= QUAD8_CMR_QUADRATURE_X1;
			break;
		case QUAD8_COUNT_FUNCTION_QUADRATURE_X2:
			*scale = 1;
			mode_cfg |= QUAD8_CMR_QUADRATURE_X2;
			break;
		case QUAD8_COUNT_FUNCTION_QUADRATURE_X4:
			*scale = 2;
			mode_cfg |= QUAD8_CMR_QUADRATURE_X4;
			break;
		}
	}

	/* Load mode configuration to Counter Mode Register */
	outb(QUAD8_CTR_CMR | mode_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static void quad8_direction_get(struct counter_device *counter,
	struct counter_count *count, enum counter_count_direction *direction)
{
	const struct quad8_iio *const priv = counter->priv;
	unsigned int ud_flag;
	const unsigned int flag_addr = priv->base + 2 * count->id + 1;

	/* U/D flag: nonzero = up, zero = down */
	ud_flag = inb(flag_addr) & QUAD8_FLAG_UD;

	*direction = (ud_flag) ? COUNTER_COUNT_DIRECTION_FORWARD :
		COUNTER_COUNT_DIRECTION_BACKWARD;
}

enum quad8_synapse_action {
	QUAD8_SYNAPSE_ACTION_NONE = 0,
	QUAD8_SYNAPSE_ACTION_RISING_EDGE,
	QUAD8_SYNAPSE_ACTION_FALLING_EDGE,
	QUAD8_SYNAPSE_ACTION_BOTH_EDGES
};

static enum counter_synapse_action quad8_index_actions_list[] = {
	[QUAD8_SYNAPSE_ACTION_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[QUAD8_SYNAPSE_ACTION_RISING_EDGE] = COUNTER_SYNAPSE_ACTION_RISING_EDGE
};

static enum counter_synapse_action quad8_synapse_actions_list[] = {
	[QUAD8_SYNAPSE_ACTION_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[QUAD8_SYNAPSE_ACTION_RISING_EDGE] = COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	[QUAD8_SYNAPSE_ACTION_FALLING_EDGE] = COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	[QUAD8_SYNAPSE_ACTION_BOTH_EDGES] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES
};

static int quad8_action_get(struct counter_device *counter,
	struct counter_count *count, struct counter_synapse *synapse,
	size_t *action)
{
	struct quad8_iio *const priv = counter->priv;
	int err;
	size_t function = 0;
	const size_t signal_a_id = count->synapses[0].signal->id;
	enum counter_count_direction direction;

	/* Handle Index signals */
	if (synapse->signal->id >= 16) {
		if (priv->preset_enable[count->id])
			*action = QUAD8_SYNAPSE_ACTION_RISING_EDGE;
		else
			*action = QUAD8_SYNAPSE_ACTION_NONE;

		return 0;
	}

	err = quad8_function_get(counter, count, &function);
	if (err)
		return err;

	/* Default action mode */
	*action = QUAD8_SYNAPSE_ACTION_NONE;

	/* Determine action mode based on current count function mode */
	switch (function) {
	case QUAD8_COUNT_FUNCTION_PULSE_DIRECTION:
		if (synapse->signal->id == signal_a_id)
			*action = QUAD8_SYNAPSE_ACTION_RISING_EDGE;
		break;
	case QUAD8_COUNT_FUNCTION_QUADRATURE_X1:
		if (synapse->signal->id == signal_a_id) {
			quad8_direction_get(counter, count, &direction);

			if (direction == COUNTER_COUNT_DIRECTION_FORWARD)
				*action = QUAD8_SYNAPSE_ACTION_RISING_EDGE;
			else
				*action = QUAD8_SYNAPSE_ACTION_FALLING_EDGE;
		}
		break;
	case QUAD8_COUNT_FUNCTION_QUADRATURE_X2:
		if (synapse->signal->id == signal_a_id)
			*action = QUAD8_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	case QUAD8_COUNT_FUNCTION_QUADRATURE_X4:
		*action = QUAD8_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	}

	return 0;
}

static const struct counter_ops quad8_ops = {
	.signal_read = quad8_signal_read,
	.count_read = quad8_count_read,
	.count_write = quad8_count_write,
	.function_get = quad8_function_get,
	.function_set = quad8_function_set,
	.action_get = quad8_action_get
};

static int quad8_index_polarity_get(struct counter_device *counter,
	struct counter_signal *signal, size_t *index_polarity)
{
	const struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id - 16;

	*index_polarity = priv->index_polarity[channel_id];

	return 0;
}

static int quad8_index_polarity_set(struct counter_device *counter,
	struct counter_signal *signal, size_t index_polarity)
{
	struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id - 16;
	const int base_offset = priv->base + 2 * channel_id + 1;
	unsigned int idr_cfg = index_polarity << 1;

	mutex_lock(&priv->lock);

	idr_cfg |= priv->synchronous_mode[channel_id];

	priv->index_polarity[channel_id] = index_polarity;

	/* Load Index Control configuration to Index Control Register */
	outb(QUAD8_CTR_IDR | idr_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static struct counter_signal_enum_ext quad8_index_pol_enum = {
	.items = quad8_index_polarity_modes,
	.num_items = ARRAY_SIZE(quad8_index_polarity_modes),
	.get = quad8_index_polarity_get,
	.set = quad8_index_polarity_set
};

static int quad8_synchronous_mode_get(struct counter_device *counter,
	struct counter_signal *signal, size_t *synchronous_mode)
{
	const struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id - 16;

	*synchronous_mode = priv->synchronous_mode[channel_id];

	return 0;
}

static int quad8_synchronous_mode_set(struct counter_device *counter,
	struct counter_signal *signal, size_t synchronous_mode)
{
	struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id - 16;
	const int base_offset = priv->base + 2 * channel_id + 1;
	unsigned int idr_cfg = synchronous_mode;

	mutex_lock(&priv->lock);

	idr_cfg |= priv->index_polarity[channel_id] << 1;

	/* Index function must be non-synchronous in non-quadrature mode */
	if (synchronous_mode && !priv->quadrature_mode[channel_id]) {
		mutex_unlock(&priv->lock);
		return -EINVAL;
	}

	priv->synchronous_mode[channel_id] = synchronous_mode;

	/* Load Index Control configuration to Index Control Register */
	outb(QUAD8_CTR_IDR | idr_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static struct counter_signal_enum_ext quad8_syn_mode_enum = {
	.items = quad8_synchronous_modes,
	.num_items = ARRAY_SIZE(quad8_synchronous_modes),
	.get = quad8_synchronous_mode_get,
	.set = quad8_synchronous_mode_set
};

static ssize_t quad8_count_floor_read(struct counter_device *counter,
	struct counter_count *count, void *private, char *buf)
{
	/* Only a floor of 0 is supported */
	return sprintf(buf, "0\n");
}

static int quad8_count_mode_get(struct counter_device *counter,
	struct counter_count *count, size_t *cnt_mode)
{
	const struct quad8_iio *const priv = counter->priv;

	/* Map 104-QUAD-8 count mode to Generic Counter count mode */
	switch (priv->count_mode[count->id]) {
	case 0:
		*cnt_mode = COUNTER_COUNT_MODE_NORMAL;
		break;
	case 1:
		*cnt_mode = COUNTER_COUNT_MODE_RANGE_LIMIT;
		break;
	case 2:
		*cnt_mode = COUNTER_COUNT_MODE_NON_RECYCLE;
		break;
	case 3:
		*cnt_mode = COUNTER_COUNT_MODE_MODULO_N;
		break;
	}

	return 0;
}

static int quad8_count_mode_set(struct counter_device *counter,
	struct counter_count *count, size_t cnt_mode)
{
	struct quad8_iio *const priv = counter->priv;
	unsigned int mode_cfg;
	const int base_offset = priv->base + 2 * count->id + 1;

	/* Map Generic Counter count mode to 104-QUAD-8 count mode */
	switch (cnt_mode) {
	case COUNTER_COUNT_MODE_NORMAL:
		cnt_mode = 0;
		break;
	case COUNTER_COUNT_MODE_RANGE_LIMIT:
		cnt_mode = 1;
		break;
	case COUNTER_COUNT_MODE_NON_RECYCLE:
		cnt_mode = 2;
		break;
	case COUNTER_COUNT_MODE_MODULO_N:
		cnt_mode = 3;
		break;
	}

	mutex_lock(&priv->lock);

	priv->count_mode[count->id] = cnt_mode;

	/* Set count mode configuration value */
	mode_cfg = cnt_mode << 1;

	/* Add quadrature mode configuration */
	if (priv->quadrature_mode[count->id])
		mode_cfg |= (priv->quadrature_scale[count->id] + 1) << 3;

	/* Load mode configuration to Counter Mode Register */
	outb(QUAD8_CTR_CMR | mode_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return 0;
}

static struct counter_count_enum_ext quad8_cnt_mode_enum = {
	.items = counter_count_mode_str,
	.num_items = ARRAY_SIZE(counter_count_mode_str),
	.get = quad8_count_mode_get,
	.set = quad8_count_mode_set
};

static ssize_t quad8_count_direction_read(struct counter_device *counter,
	struct counter_count *count, void *priv, char *buf)
{
	enum counter_count_direction dir;

	quad8_direction_get(counter, count, &dir);

	return sprintf(buf, "%s\n", counter_count_direction_str[dir]);
}

static ssize_t quad8_count_enable_read(struct counter_device *counter,
	struct counter_count *count, void *private, char *buf)
{
	const struct quad8_iio *const priv = counter->priv;

	return sprintf(buf, "%u\n", priv->ab_enable[count->id]);
}

static ssize_t quad8_count_enable_write(struct counter_device *counter,
	struct counter_count *count, void *private, const char *buf, size_t len)
{
	struct quad8_iio *const priv = counter->priv;
	const int base_offset = priv->base + 2 * count->id;
	int err;
	bool ab_enable;
	unsigned int ior_cfg;

	err = kstrtobool(buf, &ab_enable);
	if (err)
		return err;

	mutex_lock(&priv->lock);

	priv->ab_enable[count->id] = ab_enable;

	ior_cfg = ab_enable | priv->preset_enable[count->id] << 1;

	/* Load I/O control configuration */
	outb(QUAD8_CTR_IOR | ior_cfg, base_offset + 1);

	mutex_unlock(&priv->lock);

	return len;
}

static int quad8_error_noise_get(struct counter_device *counter,
	struct counter_count *count, size_t *noise_error)
{
	const struct quad8_iio *const priv = counter->priv;
	const int base_offset = priv->base + 2 * count->id + 1;

	*noise_error = !!(inb(base_offset) & QUAD8_FLAG_E);

	return 0;
}

static struct counter_count_enum_ext quad8_error_noise_enum = {
	.items = quad8_noise_error_states,
	.num_items = ARRAY_SIZE(quad8_noise_error_states),
	.get = quad8_error_noise_get
};

static ssize_t quad8_count_preset_read(struct counter_device *counter,
	struct counter_count *count, void *private, char *buf)
{
	const struct quad8_iio *const priv = counter->priv;

	return sprintf(buf, "%u\n", priv->preset[count->id]);
}

static void quad8_preset_register_set(struct quad8_iio *quad8iio, int id,
		unsigned int preset)
{
	const unsigned int base_offset = quad8iio->base + 2 * id;
	int i;

	quad8iio->preset[id] = preset;

	/* Reset Byte Pointer */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

	/* Set Preset Register */
	for (i = 0; i < 3; i++)
		outb(preset >> (8 * i), base_offset);
}

static ssize_t quad8_count_preset_write(struct counter_device *counter,
	struct counter_count *count, void *private, const char *buf, size_t len)
{
	struct quad8_iio *const priv = counter->priv;
	unsigned int preset;
	int ret;

	ret = kstrtouint(buf, 0, &preset);
	if (ret)
		return ret;

	/* Only 24-bit values are supported */
	if (preset > 0xFFFFFF)
		return -EINVAL;

	mutex_lock(&priv->lock);

	quad8_preset_register_set(priv, count->id, preset);

	mutex_unlock(&priv->lock);

	return len;
}

static ssize_t quad8_count_ceiling_read(struct counter_device *counter,
	struct counter_count *count, void *private, char *buf)
{
	struct quad8_iio *const priv = counter->priv;

	mutex_lock(&priv->lock);

	/* Range Limit and Modulo-N count modes use preset value as ceiling */
	switch (priv->count_mode[count->id]) {
	case 1:
	case 3:
		mutex_unlock(&priv->lock);
		return sprintf(buf, "%u\n", priv->preset[count->id]);
	}

	mutex_unlock(&priv->lock);

	/* By default 0x1FFFFFF (25 bits unsigned) is maximum count */
	return sprintf(buf, "33554431\n");
}

static ssize_t quad8_count_ceiling_write(struct counter_device *counter,
	struct counter_count *count, void *private, const char *buf, size_t len)
{
	struct quad8_iio *const priv = counter->priv;
	unsigned int ceiling;
	int ret;

	ret = kstrtouint(buf, 0, &ceiling);
	if (ret)
		return ret;

	/* Only 24-bit values are supported */
	if (ceiling > 0xFFFFFF)
		return -EINVAL;

	mutex_lock(&priv->lock);

	/* Range Limit and Modulo-N count modes use preset value as ceiling */
	switch (priv->count_mode[count->id]) {
	case 1:
	case 3:
		quad8_preset_register_set(priv, count->id, ceiling);
		mutex_unlock(&priv->lock);
		return len;
	}

	mutex_unlock(&priv->lock);

	return -EINVAL;
}

static ssize_t quad8_count_preset_enable_read(struct counter_device *counter,
	struct counter_count *count, void *private, char *buf)
{
	const struct quad8_iio *const priv = counter->priv;

	return sprintf(buf, "%u\n", !priv->preset_enable[count->id]);
}

static ssize_t quad8_count_preset_enable_write(struct counter_device *counter,
	struct counter_count *count, void *private, const char *buf, size_t len)
{
	struct quad8_iio *const priv = counter->priv;
	const int base_offset = priv->base + 2 * count->id + 1;
	bool preset_enable;
	int ret;
	unsigned int ior_cfg;

	ret = kstrtobool(buf, &preset_enable);
	if (ret)
		return ret;

	/* Preset enable is active low in Input/Output Control register */
	preset_enable = !preset_enable;

	mutex_lock(&priv->lock);

	priv->preset_enable[count->id] = preset_enable;

	ior_cfg = priv->ab_enable[count->id] | (unsigned int)preset_enable << 1;

	/* Load I/O control configuration to Input / Output Control Register */
	outb(QUAD8_CTR_IOR | ior_cfg, base_offset);

	mutex_unlock(&priv->lock);

	return len;
}

static ssize_t quad8_signal_cable_fault_read(struct counter_device *counter,
					     struct counter_signal *signal,
					     void *private, char *buf)
{
	struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id / 2;
	bool disabled;
	unsigned int status;
	unsigned int fault;

	mutex_lock(&priv->lock);

	disabled = !(priv->cable_fault_enable & BIT(channel_id));

	if (disabled) {
		mutex_unlock(&priv->lock);
		return -EINVAL;
	}

	/* Logic 0 = cable fault */
	status = inb(priv->base + QUAD8_DIFF_ENCODER_CABLE_STATUS);

	mutex_unlock(&priv->lock);

	/* Mask respective channel and invert logic */
	fault = !(status & BIT(channel_id));

	return sprintf(buf, "%u\n", fault);
}

static ssize_t quad8_signal_cable_fault_enable_read(
	struct counter_device *counter, struct counter_signal *signal,
	void *private, char *buf)
{
	const struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id / 2;
	const unsigned int enb = !!(priv->cable_fault_enable & BIT(channel_id));

	return sprintf(buf, "%u\n", enb);
}

static ssize_t quad8_signal_cable_fault_enable_write(
	struct counter_device *counter, struct counter_signal *signal,
	void *private, const char *buf, size_t len)
{
	struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id / 2;
	bool enable;
	int ret;
	unsigned int cable_fault_enable;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	mutex_lock(&priv->lock);

	if (enable)
		priv->cable_fault_enable |= BIT(channel_id);
	else
		priv->cable_fault_enable &= ~BIT(channel_id);

	/* Enable is active low in Differential Encoder Cable Status register */
	cable_fault_enable = ~priv->cable_fault_enable;

	outb(cable_fault_enable, priv->base + QUAD8_DIFF_ENCODER_CABLE_STATUS);

	mutex_unlock(&priv->lock);

	return len;
}

static ssize_t quad8_signal_fck_prescaler_read(struct counter_device *counter,
	struct counter_signal *signal, void *private, char *buf)
{
	const struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id / 2;

	return sprintf(buf, "%u\n", priv->fck_prescaler[channel_id]);
}

static ssize_t quad8_signal_fck_prescaler_write(struct counter_device *counter,
	struct counter_signal *signal, void *private, const char *buf,
	size_t len)
{
	struct quad8_iio *const priv = counter->priv;
	const size_t channel_id = signal->id / 2;
	const int base_offset = priv->base + 2 * channel_id;
	u8 prescaler;
	int ret;

	ret = kstrtou8(buf, 0, &prescaler);
	if (ret)
		return ret;

	mutex_lock(&priv->lock);

	priv->fck_prescaler[channel_id] = prescaler;

	/* Reset Byte Pointer */
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);

	/* Set filter clock factor */
	outb(prescaler, base_offset);
	outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP | QUAD8_RLD_PRESET_PSC,
	     base_offset + 1);

	mutex_unlock(&priv->lock);

	return len;
}

static const struct counter_signal_ext quad8_signal_ext[] = {
	{
		.name = "cable_fault",
		.read = quad8_signal_cable_fault_read
	},
	{
		.name = "cable_fault_enable",
		.read = quad8_signal_cable_fault_enable_read,
		.write = quad8_signal_cable_fault_enable_write
	},
	{
		.name = "filter_clock_prescaler",
		.read = quad8_signal_fck_prescaler_read,
		.write = quad8_signal_fck_prescaler_write
	}
};

static const struct counter_signal_ext quad8_index_ext[] = {
	COUNTER_SIGNAL_ENUM("index_polarity", &quad8_index_pol_enum),
	COUNTER_SIGNAL_ENUM_AVAILABLE("index_polarity",	&quad8_index_pol_enum),
	COUNTER_SIGNAL_ENUM("synchronous_mode", &quad8_syn_mode_enum),
	COUNTER_SIGNAL_ENUM_AVAILABLE("synchronous_mode", &quad8_syn_mode_enum)
};

#define QUAD8_QUAD_SIGNAL(_id, _name) {		\
	.id = (_id),				\
	.name = (_name),			\
	.ext = quad8_signal_ext,		\
	.num_ext = ARRAY_SIZE(quad8_signal_ext)	\
}

#define	QUAD8_INDEX_SIGNAL(_id, _name) {	\
	.id = (_id),				\
	.name = (_name),			\
	.ext = quad8_index_ext,			\
	.num_ext = ARRAY_SIZE(quad8_index_ext)	\
}

static struct counter_signal quad8_signals[] = {
	QUAD8_QUAD_SIGNAL(0, "Channel 1 Quadrature A"),
	QUAD8_QUAD_SIGNAL(1, "Channel 1 Quadrature B"),
	QUAD8_QUAD_SIGNAL(2, "Channel 2 Quadrature A"),
	QUAD8_QUAD_SIGNAL(3, "Channel 2 Quadrature B"),
	QUAD8_QUAD_SIGNAL(4, "Channel 3 Quadrature A"),
	QUAD8_QUAD_SIGNAL(5, "Channel 3 Quadrature B"),
	QUAD8_QUAD_SIGNAL(6, "Channel 4 Quadrature A"),
	QUAD8_QUAD_SIGNAL(7, "Channel 4 Quadrature B"),
	QUAD8_QUAD_SIGNAL(8, "Channel 5 Quadrature A"),
	QUAD8_QUAD_SIGNAL(9, "Channel 5 Quadrature B"),
	QUAD8_QUAD_SIGNAL(10, "Channel 6 Quadrature A"),
	QUAD8_QUAD_SIGNAL(11, "Channel 6 Quadrature B"),
	QUAD8_QUAD_SIGNAL(12, "Channel 7 Quadrature A"),
	QUAD8_QUAD_SIGNAL(13, "Channel 7 Quadrature B"),
	QUAD8_QUAD_SIGNAL(14, "Channel 8 Quadrature A"),
	QUAD8_QUAD_SIGNAL(15, "Channel 8 Quadrature B"),
	QUAD8_INDEX_SIGNAL(16, "Channel 1 Index"),
	QUAD8_INDEX_SIGNAL(17, "Channel 2 Index"),
	QUAD8_INDEX_SIGNAL(18, "Channel 3 Index"),
	QUAD8_INDEX_SIGNAL(19, "Channel 4 Index"),
	QUAD8_INDEX_SIGNAL(20, "Channel 5 Index"),
	QUAD8_INDEX_SIGNAL(21, "Channel 6 Index"),
	QUAD8_INDEX_SIGNAL(22, "Channel 7 Index"),
	QUAD8_INDEX_SIGNAL(23, "Channel 8 Index")
};

#define QUAD8_COUNT_SYNAPSES(_id) {					\
	{								\
		.actions_list = quad8_synapse_actions_list,		\
		.num_actions = ARRAY_SIZE(quad8_synapse_actions_list),	\
		.signal = quad8_signals + 2 * (_id)			\
	},								\
	{								\
		.actions_list = quad8_synapse_actions_list,		\
		.num_actions = ARRAY_SIZE(quad8_synapse_actions_list),	\
		.signal = quad8_signals + 2 * (_id) + 1			\
	},								\
	{								\
		.actions_list = quad8_index_actions_list,		\
		.num_actions = ARRAY_SIZE(quad8_index_actions_list),	\
		.signal = quad8_signals + 2 * (_id) + 16		\
	}								\
}

static struct counter_synapse quad8_count_synapses[][3] = {
	QUAD8_COUNT_SYNAPSES(0), QUAD8_COUNT_SYNAPSES(1),
	QUAD8_COUNT_SYNAPSES(2), QUAD8_COUNT_SYNAPSES(3),
	QUAD8_COUNT_SYNAPSES(4), QUAD8_COUNT_SYNAPSES(5),
	QUAD8_COUNT_SYNAPSES(6), QUAD8_COUNT_SYNAPSES(7)
};

static const struct counter_count_ext quad8_count_ext[] = {
	{
		.name = "ceiling",
		.read = quad8_count_ceiling_read,
		.write = quad8_count_ceiling_write
	},
	{
		.name = "floor",
		.read = quad8_count_floor_read
	},
	COUNTER_COUNT_ENUM("count_mode", &quad8_cnt_mode_enum),
	COUNTER_COUNT_ENUM_AVAILABLE("count_mode", &quad8_cnt_mode_enum),
	{
		.name = "direction",
		.read = quad8_count_direction_read
	},
	{
		.name = "enable",
		.read = quad8_count_enable_read,
		.write = quad8_count_enable_write
	},
	COUNTER_COUNT_ENUM("error_noise", &quad8_error_noise_enum),
	COUNTER_COUNT_ENUM_AVAILABLE("error_noise", &quad8_error_noise_enum),
	{
		.name = "preset",
		.read = quad8_count_preset_read,
		.write = quad8_count_preset_write
	},
	{
		.name = "preset_enable",
		.read = quad8_count_preset_enable_read,
		.write = quad8_count_preset_enable_write
	}
};

#define QUAD8_COUNT(_id, _cntname) {					\
	.id = (_id),							\
	.name = (_cntname),						\
	.functions_list = quad8_count_functions_list,			\
	.num_functions = ARRAY_SIZE(quad8_count_functions_list),	\
	.synapses = quad8_count_synapses[(_id)],			\
	.num_synapses =	2,						\
	.ext = quad8_count_ext,						\
	.num_ext = ARRAY_SIZE(quad8_count_ext)				\
}

static struct counter_count quad8_counts[] = {
	QUAD8_COUNT(0, "Channel 1 Count"),
	QUAD8_COUNT(1, "Channel 2 Count"),
	QUAD8_COUNT(2, "Channel 3 Count"),
	QUAD8_COUNT(3, "Channel 4 Count"),
	QUAD8_COUNT(4, "Channel 5 Count"),
	QUAD8_COUNT(5, "Channel 6 Count"),
	QUAD8_COUNT(6, "Channel 7 Count"),
	QUAD8_COUNT(7, "Channel 8 Count")
};

static int quad8_probe(struct device *dev, unsigned int id)
{
	struct iio_dev *indio_dev;
	struct quad8_iio *quad8iio;
	int i, j;
	unsigned int base_offset;
	int err;

	if (!devm_request_region(dev, base[id], QUAD8_EXTENT, dev_name(dev))) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + QUAD8_EXTENT);
		return -EBUSY;
	}

	/* Allocate IIO device; this also allocates driver data structure */
	indio_dev = devm_iio_device_alloc(dev, sizeof(*quad8iio));
	if (!indio_dev)
		return -ENOMEM;

	/* Initialize IIO device */
	indio_dev->info = &quad8_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = ARRAY_SIZE(quad8_channels);
	indio_dev->channels = quad8_channels;
	indio_dev->name = dev_name(dev);

	/* Initialize Counter device and driver data */
	quad8iio = iio_priv(indio_dev);
	quad8iio->counter.name = dev_name(dev);
	quad8iio->counter.parent = dev;
	quad8iio->counter.ops = &quad8_ops;
	quad8iio->counter.counts = quad8_counts;
	quad8iio->counter.num_counts = ARRAY_SIZE(quad8_counts);
	quad8iio->counter.signals = quad8_signals;
	quad8iio->counter.num_signals = ARRAY_SIZE(quad8_signals);
	quad8iio->counter.priv = quad8iio;
	quad8iio->base = base[id];

	/* Initialize mutex */
	mutex_init(&quad8iio->lock);

	/* Reset all counters and disable interrupt function */
	outb(QUAD8_CHAN_OP_RESET_COUNTERS, base[id] + QUAD8_REG_CHAN_OP);
	/* Set initial configuration for all counters */
	for (i = 0; i < QUAD8_NUM_COUNTERS; i++) {
		base_offset = base[id] + 2 * i;
		/* Reset Byte Pointer */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);
		/* Reset filter clock factor */
		outb(0, base_offset);
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP | QUAD8_RLD_PRESET_PSC,
		     base_offset + 1);
		/* Reset Byte Pointer */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_BP, base_offset + 1);
		/* Reset Preset Register */
		for (j = 0; j < 3; j++)
			outb(0x00, base_offset);
		/* Reset Borrow, Carry, Compare, and Sign flags */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_FLAGS, base_offset + 1);
		/* Reset Error flag */
		outb(QUAD8_CTR_RLD | QUAD8_RLD_RESET_E, base_offset + 1);
		/* Binary encoding; Normal count; non-quadrature mode */
		outb(QUAD8_CTR_CMR, base_offset + 1);
		/* Disable A and B inputs; preset on index; FLG1 as Carry */
		outb(QUAD8_CTR_IOR, base_offset + 1);
		/* Disable index function; negative index polarity */
		outb(QUAD8_CTR_IDR, base_offset + 1);
	}
	/* Disable Differential Encoder Cable Status for all channels */
	outb(0xFF, base[id] + QUAD8_DIFF_ENCODER_CABLE_STATUS);
	/* Enable all counters */
	outb(QUAD8_CHAN_OP_ENABLE_COUNTERS, base[id] + QUAD8_REG_CHAN_OP);

	/* Register IIO device */
	err = devm_iio_device_register(dev, indio_dev);
	if (err)
		return err;

	/* Register Counter device */
	return devm_counter_register(dev, &quad8iio->counter);
}

static struct isa_driver quad8_driver = {
	.probe = quad8_probe,
	.driver = {
		.name = "104-quad-8"
	}
};

module_isa_driver(quad8_driver, num_quad8);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-QUAD-8 IIO driver");
MODULE_LICENSE("GPL v2");
