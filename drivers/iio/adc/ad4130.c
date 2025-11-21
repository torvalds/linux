// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Analog Devices, Inc.
 * Author: Cosmin Tanislav <cosmin.tanislav@analog.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#include <asm/div64.h>
#include <linux/unaligned.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>

#define AD4130_NAME				"ad4130"

#define AD4130_COMMS_READ_MASK			BIT(6)

#define AD4130_STATUS_REG			0x00

#define AD4130_ADC_CONTROL_REG			0x01
#define AD4130_ADC_CONTROL_BIPOLAR_MASK		BIT(14)
#define AD4130_ADC_CONTROL_INT_REF_VAL_MASK	BIT(13)
#define AD4130_INT_REF_2_5V			2500000
#define AD4130_INT_REF_1_25V			1250000
#define AD4130_ADC_CONTROL_CSB_EN_MASK		BIT(9)
#define AD4130_ADC_CONTROL_INT_REF_EN_MASK	BIT(8)
#define AD4130_ADC_CONTROL_MODE_MASK		GENMASK(5, 2)
#define AD4130_ADC_CONTROL_MCLK_SEL_MASK	GENMASK(1, 0)
#define AD4130_MCLK_FREQ_76_8KHZ		76800
#define AD4130_MCLK_FREQ_153_6KHZ		153600

#define AD4130_DATA_REG				0x02

#define AD4130_IO_CONTROL_REG			0x03
#define AD4130_IO_CONTROL_INT_PIN_SEL_MASK	GENMASK(9, 8)
#define AD4130_IO_CONTROL_GPIO_DATA_MASK	GENMASK(7, 4)
#define AD4130_IO_CONTROL_GPIO_CTRL_MASK	GENMASK(3, 0)

#define AD4130_VBIAS_REG			0x04

#define AD4130_ID_REG				0x05

#define AD4130_ERROR_REG			0x06

#define AD4130_ERROR_EN_REG			0x07

#define AD4130_MCLK_COUNT_REG			0x08

#define AD4130_CHANNEL_X_REG(x)			(0x09 + (x))
#define AD4130_CHANNEL_EN_MASK			BIT(23)
#define AD4130_CHANNEL_SETUP_MASK		GENMASK(22, 20)
#define AD4130_CHANNEL_AINP_MASK		GENMASK(17, 13)
#define AD4130_CHANNEL_AINM_MASK		GENMASK(12, 8)
#define AD4130_CHANNEL_IOUT1_MASK		GENMASK(7, 4)
#define AD4130_CHANNEL_IOUT2_MASK		GENMASK(3, 0)

#define AD4130_CONFIG_X_REG(x)			(0x19 + (x))
#define AD4130_CONFIG_IOUT1_VAL_MASK		GENMASK(15, 13)
#define AD4130_CONFIG_IOUT2_VAL_MASK		GENMASK(12, 10)
#define AD4130_CONFIG_BURNOUT_MASK		GENMASK(9, 8)
#define AD4130_CONFIG_REF_BUFP_MASK		BIT(7)
#define AD4130_CONFIG_REF_BUFM_MASK		BIT(6)
#define AD4130_CONFIG_REF_SEL_MASK		GENMASK(5, 4)
#define AD4130_CONFIG_PGA_MASK			GENMASK(3, 1)

#define AD4130_FILTER_X_REG(x)			(0x21 + (x))
#define AD4130_FILTER_MODE_MASK			GENMASK(15, 12)
#define AD4130_FILTER_SELECT_MASK		GENMASK(10, 0)
#define AD4130_FILTER_SELECT_MIN		1

#define AD4130_OFFSET_X_REG(x)			(0x29 + (x))

#define AD4130_GAIN_X_REG(x)			(0x31 + (x))

#define AD4130_MISC_REG				0x39

#define AD4130_FIFO_CONTROL_REG			0x3a
#define AD4130_FIFO_CONTROL_HEADER_MASK		BIT(18)
#define AD4130_FIFO_CONTROL_MODE_MASK		GENMASK(17, 16)
#define AD4130_FIFO_CONTROL_WM_INT_EN_MASK	BIT(9)
#define AD4130_FIFO_CONTROL_WM_MASK		GENMASK(7, 0)
#define AD4130_WATERMARK_256			0

#define AD4130_FIFO_STATUS_REG			0x3b

#define AD4130_FIFO_THRESHOLD_REG		0x3c

#define AD4130_FIFO_DATA_REG			0x3d
#define AD4130_FIFO_SIZE			256
#define AD4130_FIFO_MAX_SAMPLE_SIZE		3

#define AD4130_MAX_ANALOG_PINS			16
#define AD4130_MAX_CHANNELS			16
#define AD4130_MAX_DIFF_INPUTS			30
#define AD4130_MAX_GPIOS			4
#define AD4130_MAX_ODR				2400
#define AD4130_MAX_PGA				8
#define AD4130_MAX_SETUPS			8

#define AD4130_AIN2_P1				0x2
#define AD4130_AIN3_P2				0x3

#define AD4130_RESET_BUF_SIZE			8
#define AD4130_RESET_SLEEP_US			(160 * MICRO / AD4130_MCLK_FREQ_76_8KHZ)

#define AD4130_INVALID_SLOT			-1

static const unsigned int ad4130_reg_size[] = {
	[AD4130_STATUS_REG] = 1,
	[AD4130_ADC_CONTROL_REG] = 2,
	[AD4130_DATA_REG] = 3,
	[AD4130_IO_CONTROL_REG] = 2,
	[AD4130_VBIAS_REG] = 2,
	[AD4130_ID_REG] = 1,
	[AD4130_ERROR_REG] = 2,
	[AD4130_ERROR_EN_REG] = 2,
	[AD4130_MCLK_COUNT_REG] = 1,
	[AD4130_CHANNEL_X_REG(0) ... AD4130_CHANNEL_X_REG(AD4130_MAX_CHANNELS - 1)] = 3,
	[AD4130_CONFIG_X_REG(0) ... AD4130_CONFIG_X_REG(AD4130_MAX_SETUPS - 1)] = 2,
	[AD4130_FILTER_X_REG(0) ... AD4130_FILTER_X_REG(AD4130_MAX_SETUPS - 1)] = 3,
	[AD4130_OFFSET_X_REG(0) ... AD4130_OFFSET_X_REG(AD4130_MAX_SETUPS - 1)] = 3,
	[AD4130_GAIN_X_REG(0) ... AD4130_GAIN_X_REG(AD4130_MAX_SETUPS - 1)] = 3,
	[AD4130_MISC_REG] = 2,
	[AD4130_FIFO_CONTROL_REG] = 3,
	[AD4130_FIFO_STATUS_REG] = 1,
	[AD4130_FIFO_THRESHOLD_REG] = 3,
	[AD4130_FIFO_DATA_REG] = 3,
};

enum ad4130_int_ref_val {
	AD4130_INT_REF_VAL_2_5V,
	AD4130_INT_REF_VAL_1_25V,
};

enum ad4130_mclk_sel {
	AD4130_MCLK_76_8KHZ,
	AD4130_MCLK_76_8KHZ_OUT,
	AD4130_MCLK_76_8KHZ_EXT,
	AD4130_MCLK_153_6KHZ_EXT,
};

enum ad4130_int_pin_sel {
	AD4130_INT_PIN_INT,
	AD4130_INT_PIN_CLK,
	AD4130_INT_PIN_P2,
	AD4130_INT_PIN_DOUT,
};

enum ad4130_iout {
	AD4130_IOUT_OFF,
	AD4130_IOUT_10000NA,
	AD4130_IOUT_20000NA,
	AD4130_IOUT_50000NA,
	AD4130_IOUT_100000NA,
	AD4130_IOUT_150000NA,
	AD4130_IOUT_200000NA,
	AD4130_IOUT_100NA,
	AD4130_IOUT_MAX
};

enum ad4130_burnout {
	AD4130_BURNOUT_OFF,
	AD4130_BURNOUT_500NA,
	AD4130_BURNOUT_2000NA,
	AD4130_BURNOUT_4000NA,
	AD4130_BURNOUT_MAX
};

enum ad4130_ref_sel {
	AD4130_REF_REFIN1,
	AD4130_REF_REFIN2,
	AD4130_REF_REFOUT_AVSS,
	AD4130_REF_AVDD_AVSS,
	AD4130_REF_SEL_MAX
};

enum ad4130_fifo_mode {
	AD4130_FIFO_MODE_DISABLED = 0b00,
	AD4130_FIFO_MODE_WM = 0b01,
};

enum ad4130_mode {
	AD4130_MODE_CONTINUOUS = 0b0000,
	AD4130_MODE_IDLE = 0b0100,
};

enum ad4130_filter_type {
	AD4130_FILTER_SINC4,
	AD4130_FILTER_SINC4_SINC1,
	AD4130_FILTER_SINC3,
	AD4130_FILTER_SINC3_REJ60,
	AD4130_FILTER_SINC3_SINC1,
	AD4130_FILTER_SINC3_PF1,
	AD4130_FILTER_SINC3_PF2,
	AD4130_FILTER_SINC3_PF3,
	AD4130_FILTER_SINC3_PF4,
};

enum ad4130_pin_function {
	AD4130_PIN_FN_NONE,
	AD4130_PIN_FN_SPECIAL = BIT(0),
	AD4130_PIN_FN_DIFF = BIT(1),
	AD4130_PIN_FN_EXCITATION = BIT(2),
	AD4130_PIN_FN_VBIAS = BIT(3),
};

/*
 * If you make adaptations in this struct, you most likely also have to adapt
 * ad4130_setup_info_eq(), too.
 */
struct ad4130_setup_info {
	unsigned int			iout0_val;
	unsigned int			iout1_val;
	unsigned int			burnout;
	unsigned int			pga;
	unsigned int			fs;
	u32				ref_sel;
	enum ad4130_filter_type		filter_type;
	bool				ref_bufp;
	bool				ref_bufm;
};

struct ad4130_slot_info {
	struct ad4130_setup_info	setup;
	unsigned int			enabled_channels;
	unsigned int			channels;
};

struct ad4130_chan_info {
	struct ad4130_setup_info	setup;
	u32				iout0;
	u32				iout1;
	int				slot;
	bool				enabled;
	bool				initialized;
};

struct ad4130_filter_config {
	enum ad4130_filter_type		filter_type;
	unsigned int			odr_div;
	unsigned int			fs_max;
	enum iio_available_type		samp_freq_avail_type;
	int				samp_freq_avail_len;
	int				samp_freq_avail[3][2];
};

struct ad4130_state {
	struct regmap			*regmap;
	struct spi_device		*spi;
	struct clk			*mclk;
	struct regulator_bulk_data	regulators[4];
	u32				irq_trigger;
	u32				inv_irq_trigger;

	/*
	 * Synchronize access to members the of driver state, and ensure
	 * atomicity of consecutive regmap operations.
	 */
	struct mutex			lock;
	struct completion		completion;

	struct iio_chan_spec		chans[AD4130_MAX_CHANNELS];
	struct ad4130_chan_info		chans_info[AD4130_MAX_CHANNELS];
	struct ad4130_slot_info		slots_info[AD4130_MAX_SETUPS];
	enum ad4130_pin_function	pins_fn[AD4130_MAX_ANALOG_PINS];
	u32				vbias_pins[AD4130_MAX_ANALOG_PINS];
	u32				num_vbias_pins;
	int				scale_tbls[AD4130_REF_SEL_MAX][AD4130_MAX_PGA][2];
	struct gpio_chip		gc;
	struct clk_hw			int_clk_hw;

	u32			int_pin_sel;
	u32			int_ref_uv;
	u32			mclk_sel;
	bool			int_ref_en;
	bool			bipolar;

	unsigned int		num_enabled_channels;
	unsigned int		effective_watermark;
	unsigned int		watermark;

	struct spi_message	fifo_msg;
	struct spi_transfer	fifo_xfer[2];

	/*
	 * DMA (thus cache coherency maintenance) requires any transfer
	 * buffers to live in their own cache lines. As the use of these
	 * buffers is synchronous, all of the buffers used for DMA in this
	 * driver may share a cache line.
	 */
	u8			reset_buf[AD4130_RESET_BUF_SIZE] __aligned(IIO_DMA_MINALIGN);
	u8			reg_write_tx_buf[4];
	u8			reg_read_tx_buf[1];
	u8			reg_read_rx_buf[3];
	u8			fifo_tx_buf[2];
	u8			fifo_rx_buf[AD4130_FIFO_SIZE *
					    AD4130_FIFO_MAX_SAMPLE_SIZE];
};

static const char * const ad4130_int_pin_names[] = {
	[AD4130_INT_PIN_INT] = "int",
	[AD4130_INT_PIN_CLK] = "clk",
	[AD4130_INT_PIN_P2] = "p2",
	[AD4130_INT_PIN_DOUT] = "dout",
};

static const unsigned int ad4130_iout_current_na_tbl[AD4130_IOUT_MAX] = {
	[AD4130_IOUT_OFF] = 0,
	[AD4130_IOUT_100NA] = 100,
	[AD4130_IOUT_10000NA] = 10000,
	[AD4130_IOUT_20000NA] = 20000,
	[AD4130_IOUT_50000NA] = 50000,
	[AD4130_IOUT_100000NA] = 100000,
	[AD4130_IOUT_150000NA] = 150000,
	[AD4130_IOUT_200000NA] = 200000,
};

static const unsigned int ad4130_burnout_current_na_tbl[AD4130_BURNOUT_MAX] = {
	[AD4130_BURNOUT_OFF] = 0,
	[AD4130_BURNOUT_500NA] = 500,
	[AD4130_BURNOUT_2000NA] = 2000,
	[AD4130_BURNOUT_4000NA] = 4000,
};

#define AD4130_VARIABLE_ODR_CONFIG(_filter_type, _odr_div, _fs_max)	\
{									\
		.filter_type = (_filter_type),				\
		.odr_div = (_odr_div),					\
		.fs_max = (_fs_max),					\
		.samp_freq_avail_type = IIO_AVAIL_RANGE,		\
		.samp_freq_avail = {					\
			{ AD4130_MAX_ODR, (_odr_div) * (_fs_max) },	\
			{ AD4130_MAX_ODR, (_odr_div) * (_fs_max) },	\
			{ AD4130_MAX_ODR, (_odr_div) },			\
		},							\
}

#define AD4130_FIXED_ODR_CONFIG(_filter_type, _odr_div)			\
{									\
		.filter_type = (_filter_type),				\
		.odr_div = (_odr_div),					\
		.fs_max = AD4130_FILTER_SELECT_MIN,			\
		.samp_freq_avail_type = IIO_AVAIL_LIST,			\
		.samp_freq_avail_len = 1,				\
		.samp_freq_avail = {					\
			{ AD4130_MAX_ODR, (_odr_div) },			\
		},							\
}

static const struct ad4130_filter_config ad4130_filter_configs[] = {
	AD4130_VARIABLE_ODR_CONFIG(AD4130_FILTER_SINC4,       1,  10),
	AD4130_VARIABLE_ODR_CONFIG(AD4130_FILTER_SINC4_SINC1, 11, 10),
	AD4130_VARIABLE_ODR_CONFIG(AD4130_FILTER_SINC3,       1,  2047),
	AD4130_VARIABLE_ODR_CONFIG(AD4130_FILTER_SINC3_REJ60, 1,  2047),
	AD4130_VARIABLE_ODR_CONFIG(AD4130_FILTER_SINC3_SINC1, 10, 2047),
	AD4130_FIXED_ODR_CONFIG(AD4130_FILTER_SINC3_PF1,      92),
	AD4130_FIXED_ODR_CONFIG(AD4130_FILTER_SINC3_PF2,      100),
	AD4130_FIXED_ODR_CONFIG(AD4130_FILTER_SINC3_PF3,      124),
	AD4130_FIXED_ODR_CONFIG(AD4130_FILTER_SINC3_PF4,      148),
};

static const char * const ad4130_filter_types_str[] = {
	[AD4130_FILTER_SINC4] = "sinc4",
	[AD4130_FILTER_SINC4_SINC1] = "sinc4+sinc1",
	[AD4130_FILTER_SINC3] = "sinc3",
	[AD4130_FILTER_SINC3_REJ60] = "sinc3+rej60",
	[AD4130_FILTER_SINC3_SINC1] = "sinc3+sinc1",
	[AD4130_FILTER_SINC3_PF1] = "sinc3+pf1",
	[AD4130_FILTER_SINC3_PF2] = "sinc3+pf2",
	[AD4130_FILTER_SINC3_PF3] = "sinc3+pf3",
	[AD4130_FILTER_SINC3_PF4] = "sinc3+pf4",
};

static int ad4130_get_reg_size(struct ad4130_state *st, unsigned int reg,
			       unsigned int *size)
{
	if (reg >= ARRAY_SIZE(ad4130_reg_size))
		return -EINVAL;

	*size = ad4130_reg_size[reg];

	return 0;
}

static unsigned int ad4130_data_reg_size(struct ad4130_state *st)
{
	unsigned int data_reg_size;
	int ret;

	ret = ad4130_get_reg_size(st, AD4130_DATA_REG, &data_reg_size);
	if (ret)
		return 0;

	return data_reg_size;
}

static unsigned int ad4130_resolution(struct ad4130_state *st)
{
	return ad4130_data_reg_size(st) * BITS_PER_BYTE;
}

static int ad4130_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct ad4130_state *st = context;
	unsigned int size;
	int ret;

	ret = ad4130_get_reg_size(st, reg, &size);
	if (ret)
		return ret;

	st->reg_write_tx_buf[0] = reg;

	switch (size) {
	case 3:
		put_unaligned_be24(val, &st->reg_write_tx_buf[1]);
		break;
	case 2:
		put_unaligned_be16(val, &st->reg_write_tx_buf[1]);
		break;
	case 1:
		st->reg_write_tx_buf[1] = val;
		break;
	default:
		return -EINVAL;
	}

	return spi_write(st->spi, st->reg_write_tx_buf, size + 1);
}

static int ad4130_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct ad4130_state *st = context;
	struct spi_transfer t[] = {
		{
			.tx_buf = st->reg_read_tx_buf,
			.len = sizeof(st->reg_read_tx_buf),
		},
		{
			.rx_buf = st->reg_read_rx_buf,
		},
	};
	unsigned int size;
	int ret;

	ret = ad4130_get_reg_size(st, reg, &size);
	if (ret)
		return ret;

	st->reg_read_tx_buf[0] = AD4130_COMMS_READ_MASK | reg;
	t[1].len = size;

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	if (ret)
		return ret;

	switch (size) {
	case 3:
		*val = get_unaligned_be24(st->reg_read_rx_buf);
		break;
	case 2:
		*val = get_unaligned_be16(st->reg_read_rx_buf);
		break;
	case 1:
		*val = st->reg_read_rx_buf[0];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct regmap_config ad4130_regmap_config = {
	.reg_read = ad4130_reg_read,
	.reg_write = ad4130_reg_write,
};

static int ad4130_gpio_init_valid_mask(struct gpio_chip *gc,
				       unsigned long *valid_mask,
				       unsigned int ngpios)
{
	struct ad4130_state *st = gpiochip_get_data(gc);
	unsigned int i;

	/*
	 * Output-only GPIO functionality is available on pins AIN2 through
	 * AIN5. If these pins are used for anything else, do not expose them.
	 */
	for (i = 0; i < ngpios; i++) {
		unsigned int pin = i + AD4130_AIN2_P1;
		bool valid = st->pins_fn[pin] == AD4130_PIN_FN_NONE;

		__assign_bit(i, valid_mask, valid);
	}

	return 0;
}

static int ad4130_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int ad4130_gpio_set(struct gpio_chip *gc, unsigned int offset,
			   int value)
{
	struct ad4130_state *st = gpiochip_get_data(gc);
	unsigned int mask = FIELD_PREP(AD4130_IO_CONTROL_GPIO_DATA_MASK,
				       BIT(offset));

	return regmap_update_bits(st->regmap, AD4130_IO_CONTROL_REG, mask,
				  value ? mask : 0);
}

static int ad4130_set_mode(struct ad4130_state *st, enum ad4130_mode mode)
{
	return regmap_update_bits(st->regmap, AD4130_ADC_CONTROL_REG,
				  AD4130_ADC_CONTROL_MODE_MASK,
				  FIELD_PREP(AD4130_ADC_CONTROL_MODE_MASK, mode));
}

static int ad4130_set_watermark_interrupt_en(struct ad4130_state *st, bool en)
{
	return regmap_update_bits(st->regmap, AD4130_FIFO_CONTROL_REG,
				  AD4130_FIFO_CONTROL_WM_INT_EN_MASK,
				  FIELD_PREP(AD4130_FIFO_CONTROL_WM_INT_EN_MASK, en));
}

static unsigned int ad4130_watermark_reg_val(unsigned int val)
{
	if (val == AD4130_FIFO_SIZE)
		val = AD4130_WATERMARK_256;

	return val;
}

static int ad4130_set_fifo_mode(struct ad4130_state *st,
				enum ad4130_fifo_mode mode)
{
	return regmap_update_bits(st->regmap, AD4130_FIFO_CONTROL_REG,
				  AD4130_FIFO_CONTROL_MODE_MASK,
				  FIELD_PREP(AD4130_FIFO_CONTROL_MODE_MASK, mode));
}

static void ad4130_push_fifo_data(struct iio_dev *indio_dev)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int data_reg_size = ad4130_data_reg_size(st);
	unsigned int transfer_len = st->effective_watermark * data_reg_size;
	unsigned int set_size = st->num_enabled_channels * data_reg_size;
	unsigned int i;
	int ret;

	st->fifo_tx_buf[1] = ad4130_watermark_reg_val(st->effective_watermark);
	st->fifo_xfer[1].len = transfer_len;

	ret = spi_sync(st->spi, &st->fifo_msg);
	if (ret)
		return;

	for (i = 0; i < transfer_len; i += set_size)
		iio_push_to_buffers(indio_dev, &st->fifo_rx_buf[i]);
}

static irqreturn_t ad4130_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ad4130_state *st = iio_priv(indio_dev);

	if (iio_buffer_enabled(indio_dev))
		ad4130_push_fifo_data(indio_dev);
	else
		complete(&st->completion);

	return IRQ_HANDLED;
}

static bool ad4130_setup_info_eq(struct ad4130_setup_info *a,
				 struct ad4130_setup_info *b)
{
	/*
	 * This is just to make sure that the comparison is adapted after
	 * struct ad4130_setup_info was changed.
	 */
	static_assert(sizeof(*a) ==
		      sizeof(struct {
				     unsigned int iout0_val;
				     unsigned int iout1_val;
				     unsigned int burnout;
				     unsigned int pga;
				     unsigned int fs;
				     u32 ref_sel;
				     enum ad4130_filter_type filter_type;
				     bool ref_bufp;
				     bool ref_bufm;
			     }));

	if (a->iout0_val != b->iout0_val ||
	    a->iout1_val != b->iout1_val ||
	    a->burnout != b->burnout ||
	    a->pga != b->pga ||
	    a->fs != b->fs ||
	    a->ref_sel != b->ref_sel ||
	    a->filter_type != b->filter_type ||
	    a->ref_bufp != b->ref_bufp ||
	    a->ref_bufm != b->ref_bufm)
		return false;

	return true;
}

static int ad4130_find_slot(struct ad4130_state *st,
			    struct ad4130_setup_info *target_setup_info,
			    unsigned int *slot, bool *overwrite)
{
	unsigned int i;

	*slot = AD4130_INVALID_SLOT;
	*overwrite = false;

	for (i = 0; i < AD4130_MAX_SETUPS; i++) {
		struct ad4130_slot_info *slot_info = &st->slots_info[i];

		/* Immediately accept a matching setup info. */
		if (ad4130_setup_info_eq(target_setup_info, &slot_info->setup)) {
			*slot = i;
			return 0;
		}

		/* Ignore all setups which are used by enabled channels. */
		if (slot_info->enabled_channels)
			continue;

		/* Find the least used slot. */
		if (*slot == AD4130_INVALID_SLOT ||
		    slot_info->channels < st->slots_info[*slot].channels)
			*slot = i;
	}

	if (*slot == AD4130_INVALID_SLOT)
		return -EINVAL;

	*overwrite = true;

	return 0;
}

static void ad4130_unlink_channel(struct ad4130_state *st, unsigned int channel)
{
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	struct ad4130_slot_info *slot_info = &st->slots_info[chan_info->slot];

	chan_info->slot = AD4130_INVALID_SLOT;
	slot_info->channels--;
}

static int ad4130_unlink_slot(struct ad4130_state *st, unsigned int slot)
{
	unsigned int i;

	for (i = 0; i < AD4130_MAX_CHANNELS; i++) {
		struct ad4130_chan_info *chan_info = &st->chans_info[i];

		if (!chan_info->initialized || chan_info->slot != slot)
			continue;

		ad4130_unlink_channel(st, i);
	}

	return 0;
}

static int ad4130_link_channel_slot(struct ad4130_state *st,
				    unsigned int channel, unsigned int slot)
{
	struct ad4130_slot_info *slot_info = &st->slots_info[slot];
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	int ret;

	ret = regmap_update_bits(st->regmap, AD4130_CHANNEL_X_REG(channel),
				 AD4130_CHANNEL_SETUP_MASK,
				 FIELD_PREP(AD4130_CHANNEL_SETUP_MASK, slot));
	if (ret)
		return ret;

	chan_info->slot = slot;
	slot_info->channels++;

	return 0;
}

static int ad4130_write_slot_setup(struct ad4130_state *st,
				   unsigned int slot,
				   struct ad4130_setup_info *setup_info)
{
	unsigned int val;
	int ret;

	val = FIELD_PREP(AD4130_CONFIG_IOUT1_VAL_MASK, setup_info->iout0_val) |
	      FIELD_PREP(AD4130_CONFIG_IOUT1_VAL_MASK, setup_info->iout1_val) |
	      FIELD_PREP(AD4130_CONFIG_BURNOUT_MASK, setup_info->burnout) |
	      FIELD_PREP(AD4130_CONFIG_REF_BUFP_MASK, setup_info->ref_bufp) |
	      FIELD_PREP(AD4130_CONFIG_REF_BUFM_MASK, setup_info->ref_bufm) |
	      FIELD_PREP(AD4130_CONFIG_REF_SEL_MASK, setup_info->ref_sel) |
	      FIELD_PREP(AD4130_CONFIG_PGA_MASK, setup_info->pga);

	ret = regmap_write(st->regmap, AD4130_CONFIG_X_REG(slot), val);
	if (ret)
		return ret;

	val = FIELD_PREP(AD4130_FILTER_MODE_MASK, setup_info->filter_type) |
	      FIELD_PREP(AD4130_FILTER_SELECT_MASK, setup_info->fs);

	ret = regmap_write(st->regmap, AD4130_FILTER_X_REG(slot), val);
	if (ret)
		return ret;

	memcpy(&st->slots_info[slot].setup, setup_info, sizeof(*setup_info));

	return 0;
}

static int ad4130_write_channel_setup(struct ad4130_state *st,
				      unsigned int channel, bool on_enable)
{
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	struct ad4130_setup_info *setup_info = &chan_info->setup;
	bool overwrite;
	int slot;
	int ret;

	/*
	 * The following cases need to be handled.
	 *
	 * 1. Enabled and linked channel with setup changes:
	 *    - Find a slot. If not possible, return error.
	 *    - Unlink channel from current slot.
	 *    - If the slot has channels linked to it, unlink all channels, and
	 *      write the new setup to it.
	 *    - Link channel to new slot.
	 *
	 * 2. Soon to be enabled and unlinked channel:
	 *    - Find a slot. If not possible, return error.
	 *    - If the slot has channels linked to it, unlink all channels, and
	 *      write the new setup to it.
	 *    - Link channel to the slot.
	 *
	 * 3. Disabled and linked channel with setup changes:
	 *    - Unlink channel from current slot.
	 *
	 * 4. Soon to be enabled and linked channel:
	 * 5. Disabled and unlinked channel with setup changes:
	 *    - Do nothing.
	 */

	/* Case 4 */
	if (on_enable && chan_info->slot != AD4130_INVALID_SLOT)
		return 0;

	if (!on_enable && !chan_info->enabled) {
		if (chan_info->slot != AD4130_INVALID_SLOT)
			/* Case 3 */
			ad4130_unlink_channel(st, channel);

		/* Cases 3 & 5 */
		return 0;
	}

	/* Cases 1 & 2 */
	ret = ad4130_find_slot(st, setup_info, &slot, &overwrite);
	if (ret)
		return ret;

	if (chan_info->slot != AD4130_INVALID_SLOT)
		/* Case 1 */
		ad4130_unlink_channel(st, channel);

	if (overwrite) {
		ret = ad4130_unlink_slot(st, slot);
		if (ret)
			return ret;

		ret = ad4130_write_slot_setup(st, slot, setup_info);
		if (ret)
			return ret;
	}

	return ad4130_link_channel_slot(st, channel, slot);
}

static int ad4130_set_channel_enable(struct ad4130_state *st,
				     unsigned int channel, bool status)
{
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	struct ad4130_slot_info *slot_info;
	int ret;

	if (chan_info->enabled == status)
		return 0;

	if (status) {
		ret = ad4130_write_channel_setup(st, channel, true);
		if (ret)
			return ret;
	}

	slot_info = &st->slots_info[chan_info->slot];

	ret = regmap_update_bits(st->regmap, AD4130_CHANNEL_X_REG(channel),
				 AD4130_CHANNEL_EN_MASK,
				 FIELD_PREP(AD4130_CHANNEL_EN_MASK, status));
	if (ret)
		return ret;

	slot_info->enabled_channels += status ? 1 : -1;
	chan_info->enabled = status;

	return 0;
}

/*
 * Table 58. FILTER_MODE_n bits and Filter Types of the datasheet describes
 * the relation between filter mode, ODR and FS.
 *
 * Notice that the max ODR of each filter mode is not necessarily the
 * absolute max ODR supported by the chip.
 *
 * The ODR divider is not explicitly specified, but it can be deduced based
 * on the ODR range of each filter mode.
 *
 * For example, for Sinc4+Sinc1, max ODR is 218.18. That means that the
 * absolute max ODR is divided by 11 to achieve the max ODR of this filter
 * mode.
 *
 * The formulas for converting between ODR and FS for a specific filter
 * mode can be deduced from the same table.
 *
 * Notice that FS = 1 actually means max ODR, and that ODR decreases by
 * (maximum ODR / maximum FS) for each increment of FS.
 *
 * odr = MAX_ODR / odr_div * (1 - (fs - 1) / fs_max) <=>
 * odr = MAX_ODR * (1 - (fs - 1) / fs_max) / odr_div <=>
 * odr = MAX_ODR * (1 - (fs - 1) / fs_max) / odr_div <=>
 * odr = MAX_ODR * (fs_max - fs + 1) / (fs_max * odr_div)
 * (used in ad4130_fs_to_freq)
 *
 * For the opposite formula, FS can be extracted from the last one.
 *
 * MAX_ODR * (fs_max - fs + 1) = fs_max * odr_div * odr <=>
 * fs_max - fs + 1 = fs_max * odr_div * odr / MAX_ODR <=>
 * fs = 1 + fs_max - fs_max * odr_div * odr / MAX_ODR
 * (used in ad4130_fs_to_freq)
 */

static void ad4130_freq_to_fs(enum ad4130_filter_type filter_type,
			      int val, int val2, unsigned int *fs)
{
	const struct ad4130_filter_config *filter_config =
		&ad4130_filter_configs[filter_type];
	u64 dividend, divisor;
	int temp;

	dividend = filter_config->fs_max * filter_config->odr_div *
		   ((u64)val * NANO + val2);
	divisor = (u64)AD4130_MAX_ODR * NANO;

	temp = AD4130_FILTER_SELECT_MIN + filter_config->fs_max -
	       DIV64_U64_ROUND_CLOSEST(dividend, divisor);

	if (temp < AD4130_FILTER_SELECT_MIN)
		temp = AD4130_FILTER_SELECT_MIN;
	else if (temp > filter_config->fs_max)
		temp = filter_config->fs_max;

	*fs = temp;
}

static void ad4130_fs_to_freq(enum ad4130_filter_type filter_type,
			      unsigned int fs, int *val, int *val2)
{
	const struct ad4130_filter_config *filter_config =
		&ad4130_filter_configs[filter_type];
	unsigned int dividend, divisor;
	u64 temp;

	dividend = (filter_config->fs_max - fs + AD4130_FILTER_SELECT_MIN) *
		   AD4130_MAX_ODR;
	divisor = filter_config->fs_max * filter_config->odr_div;

	temp = div_u64((u64)dividend * NANO, divisor);
	*val = div_u64_rem(temp, NANO, val2);
}

static int ad4130_set_filter_type(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  unsigned int val)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int channel = chan->scan_index;
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	struct ad4130_setup_info *setup_info = &chan_info->setup;
	enum ad4130_filter_type old_filter_type;
	int freq_val, freq_val2;
	unsigned int old_fs;
	int ret = 0;

	guard(mutex)(&st->lock);
	if (setup_info->filter_type == val)
		return 0;

	old_fs = setup_info->fs;
	old_filter_type = setup_info->filter_type;

	/*
	 * When switching between filter modes, try to match the ODR as
	 * close as possible. To do this, convert the current FS into ODR
	 * using the old filter mode, then convert it back into FS using
	 * the new filter mode.
	 */
	ad4130_fs_to_freq(setup_info->filter_type, setup_info->fs,
			  &freq_val, &freq_val2);

	ad4130_freq_to_fs(val, freq_val, freq_val2, &setup_info->fs);

	setup_info->filter_type = val;

	ret = ad4130_write_channel_setup(st, channel, false);
	if (ret) {
		setup_info->fs = old_fs;
		setup_info->filter_type = old_filter_type;
		return ret;
	}

	return 0;
}

static int ad4130_get_filter_type(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int channel = chan->scan_index;
	struct ad4130_setup_info *setup_info = &st->chans_info[channel].setup;
	enum ad4130_filter_type filter_type;

	guard(mutex)(&st->lock);
	filter_type = setup_info->filter_type;

	return filter_type;
}

static const struct iio_enum ad4130_filter_type_enum = {
	.items = ad4130_filter_types_str,
	.num_items = ARRAY_SIZE(ad4130_filter_types_str),
	.set = ad4130_set_filter_type,
	.get = ad4130_get_filter_type,
};

static const struct iio_chan_spec_ext_info ad4130_ext_info[] = {
	/*
	 * `filter_type` is the standardized IIO ABI for digital filtering.
	 * `filter_mode` is just kept for backwards compatibility.
	 */
	IIO_ENUM("filter_mode", IIO_SEPARATE, &ad4130_filter_type_enum),
	IIO_ENUM_AVAILABLE("filter_mode", IIO_SHARED_BY_TYPE,
			   &ad4130_filter_type_enum),
	IIO_ENUM("filter_type", IIO_SEPARATE, &ad4130_filter_type_enum),
	IIO_ENUM_AVAILABLE("filter_type", IIO_SHARED_BY_TYPE,
			   &ad4130_filter_type_enum),
	{ }
};

static const struct iio_chan_spec ad4130_channel_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.differential = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_OFFSET) |
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_SCALE) |
					BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.ext_info = ad4130_ext_info,
	.scan_type = {
		.sign = 'u',
		.endianness = IIO_BE,
	},
};

static int ad4130_set_channel_pga(struct ad4130_state *st, unsigned int channel,
				  int val, int val2)
{
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	struct ad4130_setup_info *setup_info = &chan_info->setup;
	unsigned int pga, old_pga;
	int ret;

	for (pga = 0; pga < AD4130_MAX_PGA; pga++)
		if (val == st->scale_tbls[setup_info->ref_sel][pga][0] &&
		    val2 == st->scale_tbls[setup_info->ref_sel][pga][1])
			break;

	if (pga == AD4130_MAX_PGA)
		return -EINVAL;

	guard(mutex)(&st->lock);
	if (pga == setup_info->pga)
		return 0;

	old_pga = setup_info->pga;
	setup_info->pga = pga;

	ret = ad4130_write_channel_setup(st, channel, false);
	if (ret) {
		setup_info->pga = old_pga;
		return ret;
	}

	return 0;
}

static int ad4130_set_channel_freq(struct ad4130_state *st,
				   unsigned int channel, int val, int val2)
{
	struct ad4130_chan_info *chan_info = &st->chans_info[channel];
	struct ad4130_setup_info *setup_info = &chan_info->setup;
	unsigned int fs, old_fs;
	int ret;

	guard(mutex)(&st->lock);
	old_fs = setup_info->fs;

	ad4130_freq_to_fs(setup_info->filter_type, val, val2, &fs);

	if (fs == setup_info->fs)
		return 0;

	setup_info->fs = fs;

	ret = ad4130_write_channel_setup(st, channel, false);
	if (ret) {
		setup_info->fs = old_fs;
		return ret;
	}

	return 0;
}

static int _ad4130_read_sample(struct iio_dev *indio_dev, unsigned int channel,
			       int *val)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	int ret;

	ret = ad4130_set_channel_enable(st, channel, true);
	if (ret)
		return ret;

	reinit_completion(&st->completion);

	ret = ad4130_set_mode(st, AD4130_MODE_CONTINUOUS);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&st->completion,
					  msecs_to_jiffies(1000));
	if (!ret)
		return -ETIMEDOUT;

	ret = ad4130_set_mode(st, AD4130_MODE_IDLE);
	if (ret)
		return ret;

	ret = regmap_read(st->regmap, AD4130_DATA_REG, val);
	if (ret)
		return ret;

	ret = ad4130_set_channel_enable(st, channel, false);
	if (ret)
		return ret;

	return IIO_VAL_INT;
}

static int ad4130_read_sample(struct iio_dev *indio_dev, unsigned int channel,
			      int *val)
{
	struct ad4130_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	return _ad4130_read_sample(indio_dev, channel, val);
}

static int ad4130_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int channel = chan->scan_index;
	struct ad4130_setup_info *setup_info = &st->chans_info[channel].setup;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ad4130_read_sample(indio_dev, channel, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SCALE: {
		guard(mutex)(&st->lock);
		*val = st->scale_tbls[setup_info->ref_sel][setup_info->pga][0];
		*val2 = st->scale_tbls[setup_info->ref_sel][setup_info->pga][1];

		return IIO_VAL_INT_PLUS_NANO;
	}
	case IIO_CHAN_INFO_OFFSET:
		*val = st->bipolar ? -BIT(chan->scan_type.realbits - 1) : 0;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ: {
		guard(mutex)(&st->lock);
		ad4130_fs_to_freq(setup_info->filter_type, setup_info->fs,
				  val, val2);

		return IIO_VAL_INT_PLUS_NANO;
	}
	default:
		return -EINVAL;
	}
}

static int ad4130_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long info)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int channel = chan->scan_index;
	struct ad4130_setup_info *setup_info = &st->chans_info[channel].setup;
	const struct ad4130_filter_config *filter_config;

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		*vals = (int *)st->scale_tbls[setup_info->ref_sel];
		*length = ARRAY_SIZE(st->scale_tbls[setup_info->ref_sel]) * 2;

		*type = IIO_VAL_INT_PLUS_NANO;

		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		scoped_guard(mutex, &st->lock) {
			filter_config = &ad4130_filter_configs[setup_info->filter_type];
		}

		*vals = (int *)filter_config->samp_freq_avail;
		*length = filter_config->samp_freq_avail_len * 2;
		*type = IIO_VAL_FRACTIONAL;

		return filter_config->samp_freq_avail_type;
	default:
		return -EINVAL;
	}
}

static int ad4130_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int ad4130_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int channel = chan->scan_index;

	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		return ad4130_set_channel_pga(st, channel, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return ad4130_set_channel_freq(st, channel, val, val2);
	default:
		return -EINVAL;
	}
}

static int ad4130_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct ad4130_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static int ad4130_update_scan_mode(struct iio_dev *indio_dev,
				   const unsigned long *scan_mask)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int channel;
	unsigned int val = 0;
	int ret;

	guard(mutex)(&st->lock);

	for_each_set_bit(channel, scan_mask, indio_dev->num_channels) {
		ret = ad4130_set_channel_enable(st, channel, true);
		if (ret)
			return ret;

		val++;
	}

	st->num_enabled_channels = val;

	return 0;
}

static int ad4130_set_fifo_watermark(struct iio_dev *indio_dev, unsigned int val)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int eff;
	int ret;

	if (val > AD4130_FIFO_SIZE)
		return -EINVAL;

	eff = val * st->num_enabled_channels;
	if (eff > AD4130_FIFO_SIZE)
		/*
		 * Always set watermark to a multiple of the number of
		 * enabled channels to avoid making the FIFO unaligned.
		 */
		eff = rounddown(AD4130_FIFO_SIZE, st->num_enabled_channels);

	guard(mutex)(&st->lock);

	ret = regmap_update_bits(st->regmap, AD4130_FIFO_CONTROL_REG,
				 AD4130_FIFO_CONTROL_WM_MASK,
				 FIELD_PREP(AD4130_FIFO_CONTROL_WM_MASK,
					    ad4130_watermark_reg_val(eff)));
	if (ret)
		return ret;

	st->effective_watermark = eff;
	st->watermark = val;

	return 0;
}

static const struct iio_info ad4130_info = {
	.read_raw = ad4130_read_raw,
	.read_avail = ad4130_read_avail,
	.write_raw_get_fmt = ad4130_write_raw_get_fmt,
	.write_raw = ad4130_write_raw,
	.update_scan_mode = ad4130_update_scan_mode,
	.hwfifo_set_watermark = ad4130_set_fifo_watermark,
	.debugfs_reg_access = ad4130_reg_access,
};

static int ad4130_buffer_postenable(struct iio_dev *indio_dev)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	int ret;

	guard(mutex)(&st->lock);

	ret = ad4130_set_watermark_interrupt_en(st, true);
	if (ret)
		return ret;

	ret = irq_set_irq_type(st->spi->irq, st->inv_irq_trigger);
	if (ret)
		return ret;

	ret = ad4130_set_fifo_mode(st, AD4130_FIFO_MODE_WM);
	if (ret)
		return ret;

	return ad4130_set_mode(st, AD4130_MODE_CONTINUOUS);
}

static int ad4130_buffer_predisable(struct iio_dev *indio_dev)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int i;
	int ret;

	guard(mutex)(&st->lock);

	ret = ad4130_set_mode(st, AD4130_MODE_IDLE);
	if (ret)
		return ret;

	ret = irq_set_irq_type(st->spi->irq, st->irq_trigger);
	if (ret)
		return ret;

	ret = ad4130_set_fifo_mode(st, AD4130_FIFO_MODE_DISABLED);
	if (ret)
		return ret;

	ret = ad4130_set_watermark_interrupt_en(st, false);
	if (ret)
		return ret;

	/*
	 * update_scan_mode() is not called in the disable path, disable all
	 * channels here.
	 */
	for (i = 0; i < indio_dev->num_channels; i++) {
		ret = ad4130_set_channel_enable(st, i, false);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct iio_buffer_setup_ops ad4130_buffer_ops = {
	.postenable = ad4130_buffer_postenable,
	.predisable = ad4130_buffer_predisable,
};

static ssize_t hwfifo_watermark_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct ad4130_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned int val;

	guard(mutex)(&st->lock);
	val = st->watermark;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t hwfifo_enabled_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct ad4130_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, AD4130_FIFO_CONTROL_REG, &val);
	if (ret)
		return ret;

	val = FIELD_GET(AD4130_FIFO_CONTROL_MODE_MASK, val);

	return sysfs_emit(buf, "%d\n", val != AD4130_FIFO_MODE_DISABLED);
}

static ssize_t hwfifo_watermark_min_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "%s\n", "1");
}

static ssize_t hwfifo_watermark_max_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "%s\n", __stringify(AD4130_FIFO_SIZE));
}

static IIO_DEVICE_ATTR_RO(hwfifo_watermark_min, 0);
static IIO_DEVICE_ATTR_RO(hwfifo_watermark_max, 0);
static IIO_DEVICE_ATTR_RO(hwfifo_watermark, 0);
static IIO_DEVICE_ATTR_RO(hwfifo_enabled, 0);

static const struct iio_dev_attr *ad4130_fifo_attributes[] = {
	&iio_dev_attr_hwfifo_watermark_min,
	&iio_dev_attr_hwfifo_watermark_max,
	&iio_dev_attr_hwfifo_watermark,
	&iio_dev_attr_hwfifo_enabled,
	NULL
};

static int _ad4130_find_table_index(const unsigned int *tbl, size_t len,
				    unsigned int val)
{
	unsigned int i;

	for (i = 0; i < len; i++)
		if (tbl[i] == val)
			return i;

	return -EINVAL;
}

#define ad4130_find_table_index(table, val) \
	_ad4130_find_table_index(table, ARRAY_SIZE(table), val)

static int ad4130_get_ref_voltage(struct ad4130_state *st,
				  enum ad4130_ref_sel ref_sel)
{
	switch (ref_sel) {
	case AD4130_REF_REFIN1:
		return regulator_get_voltage(st->regulators[2].consumer);
	case AD4130_REF_REFIN2:
		return regulator_get_voltage(st->regulators[3].consumer);
	case AD4130_REF_AVDD_AVSS:
		return regulator_get_voltage(st->regulators[0].consumer);
	case AD4130_REF_REFOUT_AVSS:
		return st->int_ref_uv;
	default:
		return -EINVAL;
	}
}

static int ad4130_parse_fw_setup(struct ad4130_state *st,
				 struct fwnode_handle *child,
				 struct ad4130_setup_info *setup_info)
{
	struct device *dev = &st->spi->dev;
	u32 tmp;
	int ret;

	tmp = 0;
	fwnode_property_read_u32(child, "adi,excitation-current-0-nanoamp", &tmp);
	ret = ad4130_find_table_index(ad4130_iout_current_na_tbl, tmp);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Invalid excitation current %unA\n", tmp);
	setup_info->iout0_val = ret;

	tmp = 0;
	fwnode_property_read_u32(child, "adi,excitation-current-1-nanoamp", &tmp);
	ret = ad4130_find_table_index(ad4130_iout_current_na_tbl, tmp);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Invalid excitation current %unA\n", tmp);
	setup_info->iout1_val = ret;

	tmp = 0;
	fwnode_property_read_u32(child, "adi,burnout-current-nanoamp", &tmp);
	ret = ad4130_find_table_index(ad4130_burnout_current_na_tbl, tmp);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Invalid burnout current %unA\n", tmp);
	setup_info->burnout = ret;

	setup_info->ref_bufp = fwnode_property_read_bool(child, "adi,buffered-positive");
	setup_info->ref_bufm = fwnode_property_read_bool(child, "adi,buffered-negative");

	setup_info->ref_sel = AD4130_REF_REFIN1;
	fwnode_property_read_u32(child, "adi,reference-select",
				 &setup_info->ref_sel);
	if (setup_info->ref_sel >= AD4130_REF_SEL_MAX)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid reference selected %u\n",
				     setup_info->ref_sel);

	if (setup_info->ref_sel == AD4130_REF_REFOUT_AVSS)
		st->int_ref_en = true;

	ret = ad4130_get_ref_voltage(st, setup_info->ref_sel);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot use reference %u\n",
				     setup_info->ref_sel);

	return 0;
}

static int ad4130_validate_diff_channel(struct ad4130_state *st, u32 pin)
{
	struct device *dev = &st->spi->dev;

	if (pin >= AD4130_MAX_DIFF_INPUTS)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid differential channel %u\n", pin);

	if (pin >= AD4130_MAX_ANALOG_PINS)
		return 0;

	if (st->pins_fn[pin] == AD4130_PIN_FN_SPECIAL)
		return dev_err_probe(dev, -EINVAL,
				     "Pin %u already used with fn %u\n", pin,
				     st->pins_fn[pin]);

	st->pins_fn[pin] |= AD4130_PIN_FN_DIFF;

	return 0;
}

static int ad4130_validate_diff_channels(struct ad4130_state *st,
					 u32 *pins, unsigned int len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = ad4130_validate_diff_channel(st, pins[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4130_validate_excitation_pin(struct ad4130_state *st, u32 pin)
{
	struct device *dev = &st->spi->dev;

	if (pin >= AD4130_MAX_ANALOG_PINS)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid excitation pin %u\n", pin);

	if (st->pins_fn[pin] == AD4130_PIN_FN_SPECIAL)
		return dev_err_probe(dev, -EINVAL,
				     "Pin %u already used with fn %u\n", pin,
				     st->pins_fn[pin]);

	st->pins_fn[pin] |= AD4130_PIN_FN_EXCITATION;

	return 0;
}

static int ad4130_validate_vbias_pin(struct ad4130_state *st, u32 pin)
{
	struct device *dev = &st->spi->dev;

	if (pin >= AD4130_MAX_ANALOG_PINS)
		return dev_err_probe(dev, -EINVAL, "Invalid vbias pin %u\n",
				     pin);

	if (st->pins_fn[pin] == AD4130_PIN_FN_SPECIAL)
		return dev_err_probe(dev, -EINVAL,
				     "Pin %u already used with fn %u\n", pin,
				     st->pins_fn[pin]);

	st->pins_fn[pin] |= AD4130_PIN_FN_VBIAS;

	return 0;
}

static int ad4130_validate_vbias_pins(struct ad4130_state *st,
				      u32 *pins, unsigned int len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < st->num_vbias_pins; i++) {
		ret = ad4130_validate_vbias_pin(st, pins[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4130_parse_fw_channel(struct iio_dev *indio_dev,
				   struct fwnode_handle *child)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	unsigned int resolution = ad4130_resolution(st);
	unsigned int index = indio_dev->num_channels++;
	struct device *dev = &st->spi->dev;
	struct ad4130_chan_info *chan_info;
	struct iio_chan_spec *chan;
	u32 pins[2];
	int ret;

	if (index >= AD4130_MAX_CHANNELS)
		return dev_err_probe(dev, -EINVAL, "Too many channels\n");

	chan = &st->chans[index];
	chan_info = &st->chans_info[index];

	*chan = ad4130_channel_template;
	chan->scan_type.realbits = resolution;
	chan->scan_type.storagebits = resolution;
	chan->scan_index = index;

	chan_info->slot = AD4130_INVALID_SLOT;
	chan_info->setup.fs = AD4130_FILTER_SELECT_MIN;
	chan_info->initialized = true;

	ret = fwnode_property_read_u32_array(child, "diff-channels", pins,
					     ARRAY_SIZE(pins));
	if (ret)
		return ret;

	ret = ad4130_validate_diff_channels(st, pins, ARRAY_SIZE(pins));
	if (ret)
		return ret;

	chan->channel = pins[0];
	chan->channel2 = pins[1];

	ret = ad4130_parse_fw_setup(st, child, &chan_info->setup);
	if (ret)
		return ret;

	fwnode_property_read_u32(child, "adi,excitation-pin-0",
				 &chan_info->iout0);
	if (chan_info->setup.iout0_val != AD4130_IOUT_OFF) {
		ret = ad4130_validate_excitation_pin(st, chan_info->iout0);
		if (ret)
			return ret;
	}

	fwnode_property_read_u32(child, "adi,excitation-pin-1",
				 &chan_info->iout1);
	if (chan_info->setup.iout1_val != AD4130_IOUT_OFF) {
		ret = ad4130_validate_excitation_pin(st, chan_info->iout1);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4130_parse_fw_children(struct iio_dev *indio_dev)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	int ret;

	indio_dev->channels = st->chans;

	device_for_each_child_node_scoped(dev, child) {
		ret = ad4130_parse_fw_channel(indio_dev, child);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4310_parse_fw(struct iio_dev *indio_dev)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	u32 ext_clk_freq = AD4130_MCLK_FREQ_76_8KHZ;
	unsigned int i;
	int avdd_uv;
	int irq;
	int ret;

	st->mclk = devm_clk_get_optional(dev, "mclk");
	if (IS_ERR(st->mclk))
		return dev_err_probe(dev, PTR_ERR(st->mclk),
				     "Failed to get mclk\n");

	st->int_pin_sel = AD4130_INT_PIN_INT;

	for (i = 0; i < ARRAY_SIZE(ad4130_int_pin_names); i++) {
		irq = fwnode_irq_get_byname(dev_fwnode(dev),
					    ad4130_int_pin_names[i]);
		if (irq > 0) {
			st->int_pin_sel = i;
			break;
		}
	}

	if (st->int_pin_sel == AD4130_INT_PIN_DOUT)
		return dev_err_probe(dev, -EINVAL,
				     "Cannot use DOUT as interrupt pin\n");

	if (st->int_pin_sel == AD4130_INT_PIN_P2)
		st->pins_fn[AD4130_AIN3_P2] = AD4130_PIN_FN_SPECIAL;

	device_property_read_u32(dev, "adi,ext-clk-freq-hz", &ext_clk_freq);
	if (ext_clk_freq != AD4130_MCLK_FREQ_153_6KHZ &&
	    ext_clk_freq != AD4130_MCLK_FREQ_76_8KHZ)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid external clock frequency %u\n",
				     ext_clk_freq);

	if (st->mclk && ext_clk_freq == AD4130_MCLK_FREQ_153_6KHZ)
		st->mclk_sel = AD4130_MCLK_153_6KHZ_EXT;
	else if (st->mclk)
		st->mclk_sel = AD4130_MCLK_76_8KHZ_EXT;
	else
		st->mclk_sel = AD4130_MCLK_76_8KHZ;

	if (st->int_pin_sel == AD4130_INT_PIN_CLK &&
	    st->mclk_sel != AD4130_MCLK_76_8KHZ)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid clock %u for interrupt pin %u\n",
				     st->mclk_sel, st->int_pin_sel);

	st->int_ref_uv = AD4130_INT_REF_2_5V;

	/*
	 * When the AVDD supply is set to below 2.5V the internal reference of
	 * 1.25V should be selected.
	 * See datasheet page 37, section ADC REFERENCE.
	 */
	avdd_uv = regulator_get_voltage(st->regulators[0].consumer);
	if (avdd_uv > 0 && avdd_uv < AD4130_INT_REF_2_5V)
		st->int_ref_uv = AD4130_INT_REF_1_25V;

	st->bipolar = device_property_read_bool(dev, "adi,bipolar");

	ret = device_property_count_u32(dev, "adi,vbias-pins");
	if (ret > 0) {
		if (ret > AD4130_MAX_ANALOG_PINS)
			return dev_err_probe(dev, -EINVAL,
					     "Too many vbias pins %u\n", ret);

		st->num_vbias_pins = ret;

		ret = device_property_read_u32_array(dev, "adi,vbias-pins",
						     st->vbias_pins,
						     st->num_vbias_pins);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Failed to read vbias pins\n");

		ret = ad4130_validate_vbias_pins(st, st->vbias_pins,
						 st->num_vbias_pins);
		if (ret)
			return ret;
	}

	ret = ad4130_parse_fw_children(indio_dev);
	if (ret)
		return ret;

	return 0;
}

static void ad4130_fill_scale_tbls(struct ad4130_state *st)
{
	unsigned int pow = ad4130_resolution(st) - st->bipolar;
	unsigned int i, j;

	for (i = 0; i < AD4130_REF_SEL_MAX; i++) {
		int ret;
		u64 nv;

		ret = ad4130_get_ref_voltage(st, i);
		if (ret < 0)
			continue;

		nv = (u64)ret * NANO;

		for (j = 0; j < AD4130_MAX_PGA; j++)
			st->scale_tbls[i][j][1] = div_u64(nv >> (pow + j), MILLI);
	}
}

static void ad4130_clk_disable_unprepare(void *clk)
{
	clk_disable_unprepare(clk);
}

static int ad4130_set_mclk_sel(struct ad4130_state *st,
			       enum ad4130_mclk_sel mclk_sel)
{
	return regmap_update_bits(st->regmap, AD4130_ADC_CONTROL_REG,
				 AD4130_ADC_CONTROL_MCLK_SEL_MASK,
				 FIELD_PREP(AD4130_ADC_CONTROL_MCLK_SEL_MASK,
					    mclk_sel));
}

static unsigned long ad4130_int_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	return AD4130_MCLK_FREQ_76_8KHZ;
}

static int ad4130_int_clk_is_enabled(struct clk_hw *hw)
{
	struct ad4130_state *st = container_of(hw, struct ad4130_state, int_clk_hw);

	return st->mclk_sel == AD4130_MCLK_76_8KHZ_OUT;
}

static int ad4130_int_clk_prepare(struct clk_hw *hw)
{
	struct ad4130_state *st = container_of(hw, struct ad4130_state, int_clk_hw);
	int ret;

	ret = ad4130_set_mclk_sel(st, AD4130_MCLK_76_8KHZ_OUT);
	if (ret)
		return ret;

	st->mclk_sel = AD4130_MCLK_76_8KHZ_OUT;

	return 0;
}

static void ad4130_int_clk_unprepare(struct clk_hw *hw)
{
	struct ad4130_state *st = container_of(hw, struct ad4130_state, int_clk_hw);
	int ret;

	ret = ad4130_set_mclk_sel(st, AD4130_MCLK_76_8KHZ);
	if (ret)
		return;

	st->mclk_sel = AD4130_MCLK_76_8KHZ;
}

static const struct clk_ops ad4130_int_clk_ops = {
	.recalc_rate = ad4130_int_clk_recalc_rate,
	.is_enabled = ad4130_int_clk_is_enabled,
	.prepare = ad4130_int_clk_prepare,
	.unprepare = ad4130_int_clk_unprepare,
};

static int ad4130_setup_int_clk(struct ad4130_state *st)
{
	struct device *dev = &st->spi->dev;
	struct device_node *of_node = dev_of_node(dev);
	struct clk_init_data init = {};
	const char *clk_name;
	int ret;

	if (st->int_pin_sel == AD4130_INT_PIN_CLK ||
	    st->mclk_sel != AD4130_MCLK_76_8KHZ)
		return 0;

	if (!of_node)
		return 0;

	clk_name = of_node->name;
	of_property_read_string(of_node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = &ad4130_int_clk_ops;

	st->int_clk_hw.init = &init;
	ret = devm_clk_hw_register(dev, &st->int_clk_hw);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &st->int_clk_hw);
}

static int ad4130_setup(struct iio_dev *indio_dev)
{
	struct ad4130_state *st = iio_priv(indio_dev);
	struct device *dev = &st->spi->dev;
	unsigned int int_ref_val;
	unsigned long rate = AD4130_MCLK_FREQ_76_8KHZ;
	unsigned int val;
	unsigned int i;
	int ret;

	if (st->mclk_sel == AD4130_MCLK_153_6KHZ_EXT)
		rate = AD4130_MCLK_FREQ_153_6KHZ;

	ret = clk_set_rate(st->mclk, rate);
	if (ret)
		return ret;

	ret = clk_prepare_enable(st->mclk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, ad4130_clk_disable_unprepare,
				       st->mclk);
	if (ret)
		return ret;

	if (st->int_ref_uv == AD4130_INT_REF_2_5V)
		int_ref_val = AD4130_INT_REF_VAL_2_5V;
	else
		int_ref_val = AD4130_INT_REF_VAL_1_25V;

	/* Switch to SPI 4-wire mode. */
	val =  FIELD_PREP(AD4130_ADC_CONTROL_CSB_EN_MASK, 1);
	val |= FIELD_PREP(AD4130_ADC_CONTROL_BIPOLAR_MASK, st->bipolar);
	val |= FIELD_PREP(AD4130_ADC_CONTROL_INT_REF_EN_MASK, st->int_ref_en);
	val |= FIELD_PREP(AD4130_ADC_CONTROL_MODE_MASK, AD4130_MODE_IDLE);
	val |= FIELD_PREP(AD4130_ADC_CONTROL_MCLK_SEL_MASK, st->mclk_sel);
	val |= FIELD_PREP(AD4130_ADC_CONTROL_INT_REF_VAL_MASK, int_ref_val);

	ret = regmap_write(st->regmap, AD4130_ADC_CONTROL_REG, val);
	if (ret)
		return ret;

	/*
	 * Configure unused GPIOs for output. If configured, the interrupt
	 * function of P2 takes priority over the GPIO out function.
	 */
	val = 0;
	for (i = 0; i < AD4130_MAX_GPIOS; i++)
		if (st->pins_fn[i + AD4130_AIN2_P1] == AD4130_PIN_FN_NONE)
			val |= FIELD_PREP(AD4130_IO_CONTROL_GPIO_CTRL_MASK, BIT(i));

	val |= FIELD_PREP(AD4130_IO_CONTROL_INT_PIN_SEL_MASK, st->int_pin_sel);

	ret = regmap_write(st->regmap, AD4130_IO_CONTROL_REG, val);
	if (ret)
		return ret;

	val = 0;
	for (i = 0; i < st->num_vbias_pins; i++)
		val |= BIT(st->vbias_pins[i]);

	ret = regmap_write(st->regmap, AD4130_VBIAS_REG, val);
	if (ret)
		return ret;

	ret = regmap_clear_bits(st->regmap, AD4130_FIFO_CONTROL_REG,
				AD4130_FIFO_CONTROL_HEADER_MASK);
	if (ret)
		return ret;

	/* FIFO watermark interrupt starts out as enabled, disable it. */
	ret = ad4130_set_watermark_interrupt_en(st, false);
	if (ret)
		return ret;

	/* Setup channels. */
	for (i = 0; i < indio_dev->num_channels; i++) {
		struct ad4130_chan_info *chan_info = &st->chans_info[i];
		struct iio_chan_spec *chan = &st->chans[i];
		unsigned int val;

		val = FIELD_PREP(AD4130_CHANNEL_AINP_MASK, chan->channel) |
		      FIELD_PREP(AD4130_CHANNEL_AINM_MASK, chan->channel2) |
		      FIELD_PREP(AD4130_CHANNEL_IOUT1_MASK, chan_info->iout0) |
		      FIELD_PREP(AD4130_CHANNEL_IOUT2_MASK, chan_info->iout1);

		ret = regmap_write(st->regmap, AD4130_CHANNEL_X_REG(i), val);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad4130_soft_reset(struct ad4130_state *st)
{
	int ret;

	ret = spi_write(st->spi, st->reset_buf, sizeof(st->reset_buf));
	if (ret)
		return ret;

	fsleep(AD4130_RESET_SLEEP_US);

	return 0;
}

static void ad4130_disable_regulators(void *data)
{
	struct ad4130_state *st = data;

	regulator_bulk_disable(ARRAY_SIZE(st->regulators), st->regulators);
}

static int ad4130_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct iio_dev *indio_dev;
	struct ad4130_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	memset(st->reset_buf, 0xff, sizeof(st->reset_buf));
	init_completion(&st->completion);
	mutex_init(&st->lock);
	st->spi = spi;

	/*
	 * Xfer:   [ XFR1 ] [         XFR2         ]
	 * Master:  0x7D N   ......................
	 * Slave:   ......   DATA1 DATA2 ... DATAN
	 */
	st->fifo_tx_buf[0] = AD4130_COMMS_READ_MASK | AD4130_FIFO_DATA_REG;
	st->fifo_xfer[0].tx_buf = st->fifo_tx_buf;
	st->fifo_xfer[0].len = sizeof(st->fifo_tx_buf);
	st->fifo_xfer[1].rx_buf = st->fifo_rx_buf;
	spi_message_init_with_transfers(&st->fifo_msg, st->fifo_xfer,
					ARRAY_SIZE(st->fifo_xfer));

	indio_dev->name = AD4130_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &ad4130_info;

	st->regmap = devm_regmap_init(dev, NULL, st, &ad4130_regmap_config);
	if (IS_ERR(st->regmap))
		return PTR_ERR(st->regmap);

	st->regulators[0].supply = "avdd";
	st->regulators[1].supply = "iovdd";
	st->regulators[2].supply = "refin1";
	st->regulators[3].supply = "refin2";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(st->regulators),
				      st->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(st->regulators), st->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	ret = devm_add_action_or_reset(dev, ad4130_disable_regulators, st);
	if (ret)
		return ret;

	ret = ad4130_soft_reset(st);
	if (ret)
		return ret;

	ret = ad4310_parse_fw(indio_dev);
	if (ret)
		return ret;

	ret = ad4130_setup(indio_dev);
	if (ret)
		return ret;

	ret = ad4130_setup_int_clk(st);
	if (ret)
		return ret;

	ad4130_fill_scale_tbls(st);

	st->gc.owner = THIS_MODULE;
	st->gc.label = AD4130_NAME;
	st->gc.base = -1;
	st->gc.ngpio = AD4130_MAX_GPIOS;
	st->gc.parent = dev;
	st->gc.can_sleep = true;
	st->gc.init_valid_mask = ad4130_gpio_init_valid_mask;
	st->gc.get_direction = ad4130_gpio_get_direction;
	st->gc.set = ad4130_gpio_set;

	ret = devm_gpiochip_add_data(dev, &st->gc, st);
	if (ret)
		return ret;

	ret = devm_iio_kfifo_buffer_setup_ext(dev, indio_dev,
					      &ad4130_buffer_ops,
					      ad4130_fifo_attributes);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, spi->irq, NULL,
					ad4130_irq_handler, IRQF_ONESHOT,
					indio_dev->name, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request irq\n");

	/*
	 * When the chip enters FIFO mode, IRQ polarity is inverted.
	 * When the chip exits FIFO mode, IRQ polarity returns to normal.
	 * See datasheet pages: 65, FIFO Watermark Interrupt section,
	 * and 71, Bit Descriptions for STATUS Register, RDYB.
	 * Cache the normal and inverted IRQ triggers to set them when
	 * entering and exiting FIFO mode.
	 */
	st->irq_trigger = irq_get_trigger_type(spi->irq);
	if (st->irq_trigger & IRQF_TRIGGER_RISING)
		st->inv_irq_trigger = IRQF_TRIGGER_FALLING;
	else if (st->irq_trigger & IRQF_TRIGGER_FALLING)
		st->inv_irq_trigger = IRQF_TRIGGER_RISING;
	else
		return dev_err_probe(dev, -EINVAL, "Invalid irq flags: %u\n",
				     st->irq_trigger);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad4130_of_match[] = {
	{
		.compatible = "adi,ad4130",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ad4130_of_match);

static struct spi_driver ad4130_driver = {
	.driver = {
		.name = AD4130_NAME,
		.of_match_table = ad4130_of_match,
	},
	.probe = ad4130_probe,
};
module_spi_driver(ad4130_driver);

MODULE_AUTHOR("Cosmin Tanislav <cosmin.tanislav@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD4130 SPI driver");
MODULE_LICENSE("GPL");
