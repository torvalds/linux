// SPDX-License-Identifier: GPL-2.0
/*
 * AD8460 Waveform generator DAC Driver
 *
 * Copyright (C) 2024 Analog Devices, Inc.
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <linux/iio/buffer.h>
#include <linux/iio/buffer-dma.h>
#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/consumer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>

#define AD8460_CTRL_REG(x)			(x)
#define AD8460_HVDAC_DATA_WORD(x)		(0x60 + (2 * (x)))

#define AD8460_HV_RESET_MSK			BIT(7)
#define AD8460_HV_SLEEP_MSK			BIT(4)
#define AD8460_WAVE_GEN_MODE_MSK		BIT(0)

#define AD8460_HVDAC_SLEEP_MSK			BIT(3)

#define AD8460_FAULT_ARM_MSK			BIT(7)
#define AD8460_FAULT_LIMIT_MSK			GENMASK(6, 0)

#define AD8460_APG_MODE_ENABLE_MSK		BIT(5)
#define AD8460_PATTERN_DEPTH_MSK		GENMASK(3, 0)

#define AD8460_QUIESCENT_CURRENT_MSK		GENMASK(7, 0)

#define AD8460_SHUTDOWN_FLAG_MSK		BIT(7)

#define AD8460_DATA_BYTE_LOW_MSK		GENMASK(7, 0)
#define AD8460_DATA_BYTE_HIGH_MSK		GENMASK(5, 0)
#define AD8460_DATA_BYTE_FULL_MSK		GENMASK(13, 0)

#define AD8460_DEFAULT_FAULT_PROTECT		0x00
#define AD8460_DATA_BYTE_WORD_LENGTH		2
#define AD8460_NUM_DATA_WORDS			16
#define AD8460_NOMINAL_VOLTAGE_SPAN		80
#define AD8460_MIN_EXT_RESISTOR_OHMS		2000
#define AD8460_MAX_EXT_RESISTOR_OHMS		20000
#define AD8460_MIN_VREFIO_UV			120000
#define AD8460_MAX_VREFIO_UV			1200000
#define AD8460_ABS_MAX_OVERVOLTAGE_UV		55000000
#define AD8460_ABS_MAX_OVERCURRENT_UA		1000000
#define AD8460_MAX_OVERTEMPERATURE_MC		150000
#define AD8460_MIN_OVERTEMPERATURE_MC		20000
#define AD8460_CURRENT_LIMIT_CONV(x)		((x) / 15625)
#define AD8460_VOLTAGE_LIMIT_CONV(x)		((x) / 1953000)
#define AD8460_TEMP_LIMIT_CONV(x)		(((x) + 266640) / 6510)

enum ad8460_fault_type {
	AD8460_OVERCURRENT_SRC,
	AD8460_OVERCURRENT_SNK,
	AD8460_OVERVOLTAGE_POS,
	AD8460_OVERVOLTAGE_NEG,
	AD8460_OVERTEMPERATURE,
};

struct ad8460_state {
	struct spi_device *spi;
	struct regmap *regmap;
	struct iio_channel *tmp_adc_channel;
	struct clk *sync_clk;
	/* lock to protect against multiple access to the device and shared data */
	struct mutex lock;
	int refio_1p2v_mv;
	u32 ext_resistor_ohms;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__le16 spi_tx_buf __aligned(IIO_DMA_MINALIGN);
};

static int ad8460_hv_reset(struct ad8460_state *state)
{
	int ret;

	ret = regmap_set_bits(state->regmap, AD8460_CTRL_REG(0x00),
			      AD8460_HV_RESET_MSK);
	if (ret)
		return ret;

	fsleep(20);

	return regmap_clear_bits(state->regmap, AD8460_CTRL_REG(0x00),
				 AD8460_HV_RESET_MSK);
}

static int ad8460_reset(const struct ad8460_state *state)
{
	struct device *dev = &state->spi->dev;
	struct gpio_desc *reset;

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset),
				     "Failed to get reset gpio");
	if (reset) {
		/* minimum duration of 10ns */
		ndelay(10);
		gpiod_set_value_cansleep(reset, 1);
		return 0;
	}

	/* bring all registers to their default state */
	return regmap_write(state->regmap, AD8460_CTRL_REG(0x03), 1);
}

static int ad8460_enable_apg_mode(struct ad8460_state *state, int val)
{
	int ret;

	ret = regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x02),
				 AD8460_APG_MODE_ENABLE_MSK,
				 FIELD_PREP(AD8460_APG_MODE_ENABLE_MSK, val));
	if (ret)
		return ret;

	return regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x00),
				  AD8460_WAVE_GEN_MODE_MSK,
				  FIELD_PREP(AD8460_WAVE_GEN_MODE_MSK, val));
}

static int ad8460_read_shutdown_flag(struct ad8460_state *state, u64 *flag)
{
	int ret, val;

	ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x0E), &val);
	if (ret)
		return ret;

	*flag = FIELD_GET(AD8460_SHUTDOWN_FLAG_MSK, val);
	return 0;
}

static int ad8460_get_hvdac_word(struct ad8460_state *state, int index, int *val)
{
	int ret;

	ret = regmap_bulk_read(state->regmap, AD8460_HVDAC_DATA_WORD(index),
			       &state->spi_tx_buf, AD8460_DATA_BYTE_WORD_LENGTH);
	if (ret)
		return ret;

	*val = le16_to_cpu(state->spi_tx_buf);

	return ret;
}

static int ad8460_set_hvdac_word(struct ad8460_state *state, int index, int val)
{
	state->spi_tx_buf = cpu_to_le16(FIELD_PREP(AD8460_DATA_BYTE_FULL_MSK, val));

	return regmap_bulk_write(state->regmap, AD8460_HVDAC_DATA_WORD(index),
				 &state->spi_tx_buf, AD8460_DATA_BYTE_WORD_LENGTH);
}

static ssize_t ad8460_dac_input_read(struct iio_dev *indio_dev, uintptr_t private,
				     const struct iio_chan_spec *chan, char *buf)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	ret = ad8460_get_hvdac_word(state, private, &reg);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", reg);
}

static ssize_t ad8460_dac_input_write(struct iio_dev *indio_dev, uintptr_t private,
				      const struct iio_chan_spec *chan,
				      const char *buf, size_t len)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	ret = kstrtou32(buf, 10, &reg);
	if (ret)
		return ret;

	guard(mutex)(&state->lock);

	return ad8460_set_hvdac_word(state, private, reg);
}

static ssize_t ad8460_read_symbol(struct iio_dev *indio_dev, uintptr_t private,
				  const struct iio_chan_spec *chan, char *buf)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x02), &reg);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%lu\n", FIELD_GET(AD8460_PATTERN_DEPTH_MSK, reg));
}

static ssize_t ad8460_write_symbol(struct iio_dev *indio_dev, uintptr_t private,
				   const struct iio_chan_spec *chan,
				   const char *buf, size_t len)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	uint16_t sym;
	int ret;

	ret = kstrtou16(buf, 10, &sym);
	if (ret)
		return ret;

	guard(mutex)(&state->lock);

	return regmap_update_bits(state->regmap,
				  AD8460_CTRL_REG(0x02),
				  AD8460_PATTERN_DEPTH_MSK,
				  FIELD_PREP(AD8460_PATTERN_DEPTH_MSK, sym));
}

static ssize_t ad8460_read_toggle_en(struct iio_dev *indio_dev, uintptr_t private,
				     const struct iio_chan_spec *chan, char *buf)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x02), &reg);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%ld\n", FIELD_GET(AD8460_APG_MODE_ENABLE_MSK, reg));
}

static ssize_t ad8460_write_toggle_en(struct iio_dev *indio_dev, uintptr_t private,
				      const struct iio_chan_spec *chan,
				      const char *buf, size_t len)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	bool toggle_en;
	int ret;

	ret = kstrtobool(buf, &toggle_en);
	if (ret)
		return ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = ad8460_enable_apg_mode(state, toggle_en);
	iio_device_release_direct(indio_dev);
	return ret;
}

static ssize_t ad8460_read_powerdown(struct iio_dev *indio_dev, uintptr_t private,
				     const struct iio_chan_spec *chan, char *buf)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	unsigned int reg;
	int ret;

	ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x01), &reg);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%ld\n", FIELD_GET(AD8460_HVDAC_SLEEP_MSK, reg));
}

static ssize_t ad8460_write_powerdown(struct iio_dev *indio_dev, uintptr_t private,
				      const struct iio_chan_spec *chan,
				      const char *buf, size_t len)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	bool pwr_down;
	u64 sdn_flag;
	int ret;

	ret = kstrtobool(buf, &pwr_down);
	if (ret)
		return ret;

	guard(mutex)(&state->lock);

	/*
	 * If powerdown is set, HVDAC is enabled and the HV driver is
	 * enabled via HV_RESET in case it is in shutdown mode,
	 * If powerdown is cleared, HVDAC is set to shutdown state
	 * as well as the HV driver. Quiescent current decreases and ouput is
	 * floating (high impedance).
	 */

	ret = regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x01),
				 AD8460_HVDAC_SLEEP_MSK,
				 FIELD_PREP(AD8460_HVDAC_SLEEP_MSK, pwr_down));
	if (ret)
		return ret;

	if (!pwr_down) {
		ret = ad8460_read_shutdown_flag(state, &sdn_flag);
		if (ret)
			return ret;

		if (sdn_flag) {
			ret = ad8460_hv_reset(state);
			if (ret)
				return ret;
		}
	}

	ret = regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x00),
				 AD8460_HV_SLEEP_MSK,
				 FIELD_PREP(AD8460_HV_SLEEP_MSK, !pwr_down));
	if (ret)
		return ret;

	return len;
}

static const char * const ad8460_powerdown_modes[] = {
	"three_state",
};

static int ad8460_get_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan)
{
	return 0;
}

static int ad8460_set_powerdown_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     unsigned int type)
{
	return 0;
}

static int ad8460_set_sample(struct ad8460_state *state, int val)
{
	int ret;

	ret = ad8460_enable_apg_mode(state, 1);
	if (ret)
		return ret;

	guard(mutex)(&state->lock);
	ret = ad8460_set_hvdac_word(state, 0, val);
	if (ret)
		return ret;

	return regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x02),
				  AD8460_PATTERN_DEPTH_MSK,
				  FIELD_PREP(AD8460_PATTERN_DEPTH_MSK, 0));
}

static int ad8460_set_fault_threshold(struct ad8460_state *state,
				      enum ad8460_fault_type fault,
				      unsigned int threshold)
{
	return regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x08 + fault),
				  AD8460_FAULT_LIMIT_MSK,
				  FIELD_PREP(AD8460_FAULT_LIMIT_MSK, threshold));
}

static int ad8460_get_fault_threshold(struct ad8460_state *state,
				      enum ad8460_fault_type fault,
				      unsigned int *threshold)
{
	unsigned int val;
	int ret;

	ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x08 + fault), &val);
	if (ret)
		return ret;

	*threshold = FIELD_GET(AD8460_FAULT_LIMIT_MSK, val);

	return ret;
}

static int ad8460_set_fault_threshold_en(struct ad8460_state *state,
					 enum ad8460_fault_type fault, bool en)
{
	return regmap_update_bits(state->regmap, AD8460_CTRL_REG(0x08 + fault),
				  AD8460_FAULT_ARM_MSK,
				  FIELD_PREP(AD8460_FAULT_ARM_MSK, en));
}

static int ad8460_get_fault_threshold_en(struct ad8460_state *state,
					 enum ad8460_fault_type fault, bool *en)
{
	unsigned int val;
	int ret;

	ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x08 + fault), &val);
	if (ret)
		return ret;

	*en = FIELD_GET(AD8460_FAULT_ARM_MSK, val);

	return 0;
}

static int ad8460_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VOLTAGE:
			if (!iio_device_claim_direct(indio_dev))
				return -EBUSY;
			ret = ad8460_set_sample(state, val);
			iio_device_release_direct(indio_dev);
			return ret;
		case IIO_CURRENT:
			return regmap_write(state->regmap, AD8460_CTRL_REG(0x04),
					    FIELD_PREP(AD8460_QUIESCENT_CURRENT_MSK, val));
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad8460_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	int data, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_VOLTAGE:
			scoped_guard(mutex, &state->lock) {
				ret = ad8460_get_hvdac_word(state, 0, &data);
				if (ret)
					return ret;
			}
			*val = data;
			return IIO_VAL_INT;
		case IIO_CURRENT:
			ret = regmap_read(state->regmap, AD8460_CTRL_REG(0x04),
					  &data);
			if (ret)
				return ret;
			*val = data;
			return IIO_VAL_INT;
		case IIO_TEMP:
			ret = iio_read_channel_raw(state->tmp_adc_channel, &data);
			if (ret)
				return ret;
			*val = data;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = clk_get_rate(state->sync_clk);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * vCONV = vNOMINAL_SPAN * (DAC_CODE / 2**14) - 40V
		 * vMAX  = vNOMINAL_SPAN * (2**14 / 2**14) - 40V
		 * vMIN  = vNOMINAL_SPAN * (0 / 2**14) - 40V
		 * vADJ  = vCONV * (2000 / rSET) * (vREF / 1.2)
		 * vSPAN = vADJ_MAX - vADJ_MIN
		 * See datasheet page 49, section FULL-SCALE REDUCTION
		 */
		*val = AD8460_NOMINAL_VOLTAGE_SPAN * 2000 * state->refio_1p2v_mv;
		*val2 = state->ext_resistor_ohms * 1200;
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static int ad8460_select_fault_type(int chan_type, enum iio_event_direction dir)
{
	switch (chan_type) {
	case IIO_VOLTAGE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return AD8460_OVERVOLTAGE_POS;
		case IIO_EV_DIR_FALLING:
			return AD8460_OVERVOLTAGE_NEG;
		default:
			return -EINVAL;
		}
	case IIO_CURRENT:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return AD8460_OVERCURRENT_SRC;
		case IIO_EV_DIR_FALLING:
			return AD8460_OVERCURRENT_SNK;
		default:
			return -EINVAL;
		}
	case IIO_TEMP:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return AD8460_OVERTEMPERATURE;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad8460_write_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info, int val, int val2)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	int fault;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	fault = ad8460_select_fault_type(chan->type, dir);
	if (fault < 0)
		return fault;

	return ad8460_set_fault_threshold(state, fault, val);
}

static int ad8460_read_event_value(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info, int *val, int *val2)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	int fault;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	fault = ad8460_select_fault_type(chan->type, dir);
	if (fault < 0)
		return fault;

	return ad8460_get_fault_threshold(state, fault, val);
}

static int ad8460_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, bool val)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	int fault;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	fault = ad8460_select_fault_type(chan->type, dir);
	if (fault < 0)
		return fault;

	return ad8460_set_fault_threshold_en(state, fault, val);
}

static int ad8460_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct ad8460_state *state = iio_priv(indio_dev);
	int fault, ret;
	bool en;

	if (type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	fault = ad8460_select_fault_type(chan->type, dir);
	if (fault < 0)
		return fault;

	ret = ad8460_get_fault_threshold_en(state, fault, &en);
	if (ret)
		return ret;

	return en;
}

static int ad8460_reg_access(struct iio_dev *indio_dev, unsigned int reg,
			     unsigned int writeval, unsigned int *readval)
{
	struct ad8460_state *state = iio_priv(indio_dev);

	if (readval)
		return regmap_read(state->regmap, reg, readval);

	return regmap_write(state->regmap, reg, writeval);
}

static int ad8460_buffer_preenable(struct iio_dev *indio_dev)
{
	struct ad8460_state *state = iio_priv(indio_dev);

	return ad8460_enable_apg_mode(state, 0);
}

static int ad8460_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct ad8460_state *state = iio_priv(indio_dev);

	return ad8460_enable_apg_mode(state, 1);
}

static const struct iio_buffer_setup_ops ad8460_buffer_setup_ops = {
	.preenable = &ad8460_buffer_preenable,
	.postdisable = &ad8460_buffer_postdisable,
};

static const struct iio_info ad8460_info = {
	.read_raw = &ad8460_read_raw,
	.write_raw = &ad8460_write_raw,
	.write_event_value = &ad8460_write_event_value,
	.read_event_value = &ad8460_read_event_value,
	.write_event_config = &ad8460_write_event_config,
	.read_event_config = &ad8460_read_event_config,
	.debugfs_reg_access = &ad8460_reg_access,
};

static const struct iio_enum ad8460_powerdown_mode_enum = {
	.items = ad8460_powerdown_modes,
	.num_items = ARRAY_SIZE(ad8460_powerdown_modes),
	.get = ad8460_get_powerdown_mode,
	.set = ad8460_set_powerdown_mode,
};

#define AD8460_CHAN_EXT_INFO(_name, _what, _read, _write) {		\
	.name = (_name),						\
	.read = (_read),						\
	.write = (_write),						\
	.private = (_what),						\
	.shared = IIO_SEPARATE,						\
}

static const struct iio_chan_spec_ext_info ad8460_ext_info[] = {
	AD8460_CHAN_EXT_INFO("raw0", 0, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw1", 1, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw2", 2, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw3", 3, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw4", 4, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw5", 5, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw6", 6, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw7", 7, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw8", 8, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw9", 9, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw10", 10, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw11", 11, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw12", 12, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw13", 13, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw14", 14, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("raw15", 15, ad8460_dac_input_read,
			     ad8460_dac_input_write),
	AD8460_CHAN_EXT_INFO("toggle_en", 0, ad8460_read_toggle_en,
			     ad8460_write_toggle_en),
	AD8460_CHAN_EXT_INFO("symbol", 0, ad8460_read_symbol,
			     ad8460_write_symbol),
	AD8460_CHAN_EXT_INFO("powerdown", 0, ad8460_read_powerdown,
			     ad8460_write_powerdown),
	IIO_ENUM("powerdown_mode", IIO_SEPARATE, &ad8460_powerdown_mode_enum),
	IIO_ENUM_AVAILABLE("powerdown_mode", IIO_SHARED_BY_TYPE,
			   &ad8460_powerdown_mode_enum),
	{ }
};

static const struct iio_event_spec ad8460_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
};

#define AD8460_VOLTAGE_CHAN {					\
	.type = IIO_VOLTAGE,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_SAMP_FREQ) |	\
			      BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.output = 1,						\
	.indexed = 1,						\
	.channel = 0,						\
	.scan_index = 0,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 14,					\
		.storagebits = 16,				\
		.endianness = IIO_CPU,				\
	},                                                      \
	.ext_info = ad8460_ext_info,                            \
	.event_spec = ad8460_events,				\
	.num_event_specs = ARRAY_SIZE(ad8460_events),		\
}

#define AD8460_CURRENT_CHAN {					\
	.type = IIO_CURRENT,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.output = 1,						\
	.indexed = 1,						\
	.channel = 0,						\
	.scan_index = -1,					\
	.event_spec = ad8460_events,				\
	.num_event_specs = ARRAY_SIZE(ad8460_events),		\
}

#define AD8460_TEMP_CHAN {					\
	.type = IIO_TEMP,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.indexed = 1,						\
	.channel = 0,						\
	.scan_index = -1,					\
	.event_spec = ad8460_events,				\
	.num_event_specs = 1,					\
}

static const struct iio_chan_spec ad8460_channels[] = {
	AD8460_VOLTAGE_CHAN,
	AD8460_CURRENT_CHAN,
};

static const struct iio_chan_spec ad8460_channels_with_tmp_adc[] = {
	AD8460_VOLTAGE_CHAN,
	AD8460_CURRENT_CHAN,
	AD8460_TEMP_CHAN,
};

static const struct regmap_config ad8460_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x7F,
};

static const char * const ad8460_supplies[] = {
	"avdd_3p3v", "dvdd_3p3v", "vcc_5v", "hvcc", "hvee", "vref_5v"
};

static int ad8460_probe(struct spi_device *spi)
{
	struct device *dev  = &spi->dev;
	struct ad8460_state *state;
	struct iio_dev *indio_dev;
	u32 tmp[2], temp;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*state));
	if (!indio_dev)
		return -ENOMEM;

	state = iio_priv(indio_dev);

	indio_dev->name = "ad8460";
	indio_dev->info = &ad8460_info;

	state->spi = spi;

	state->regmap = devm_regmap_init_spi(spi, &ad8460_regmap_config);
	if (IS_ERR(state->regmap))
		return dev_err_probe(dev, PTR_ERR(state->regmap),
				     "Failed to initialize regmap");

	ret = devm_mutex_init(dev, &state->lock);
	if (ret)
		return ret;

	state->sync_clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(state->sync_clk))
		return dev_err_probe(dev, PTR_ERR(state->sync_clk),
				     "Failed to get sync clk\n");

	state->tmp_adc_channel = devm_iio_channel_get(dev, "ad8460-tmp");
	if (IS_ERR(state->tmp_adc_channel)) {
		if (PTR_ERR(state->tmp_adc_channel) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		indio_dev->channels = ad8460_channels;
		indio_dev->num_channels = ARRAY_SIZE(ad8460_channels);
	} else {
		indio_dev->channels = ad8460_channels_with_tmp_adc;
		indio_dev->num_channels = ARRAY_SIZE(ad8460_channels_with_tmp_adc);
	}

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(ad8460_supplies),
					     ad8460_supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable power supplies\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "refio_1p2v");
	if (ret < 0 && ret != -ENODEV)
		return dev_err_probe(dev, ret, "Failed to get reference voltage\n");

	state->refio_1p2v_mv = ret == -ENODEV ? 1200 : ret / 1000;

	if (!in_range(state->refio_1p2v_mv, AD8460_MIN_VREFIO_UV / 1000,
		      AD8460_MAX_VREFIO_UV / 1000))
		return dev_err_probe(dev, -EINVAL,
				     "Invalid ref voltage range(%u mV) [%u mV, %u mV]\n",
				     state->refio_1p2v_mv,
				     AD8460_MIN_VREFIO_UV / 1000,
				     AD8460_MAX_VREFIO_UV / 1000);

	ret = device_property_read_u32(dev, "adi,external-resistor-ohms",
				       &state->ext_resistor_ohms);
	if (ret)
		state->ext_resistor_ohms = 2000;
	else if (!in_range(state->ext_resistor_ohms, AD8460_MIN_EXT_RESISTOR_OHMS,
			   AD8460_MAX_EXT_RESISTOR_OHMS))
		return dev_err_probe(dev, -EINVAL,
				     "Invalid resistor set range(%u) [%u, %u]\n",
				     state->ext_resistor_ohms,
				     AD8460_MIN_EXT_RESISTOR_OHMS,
				     AD8460_MAX_EXT_RESISTOR_OHMS);

	ret = device_property_read_u32_array(dev, "adi,range-microamp",
					     tmp, ARRAY_SIZE(tmp));
	if (!ret) {
		if (in_range(tmp[1], 0, AD8460_ABS_MAX_OVERCURRENT_UA))
			regmap_write(state->regmap, AD8460_CTRL_REG(0x08),
				     FIELD_PREP(AD8460_FAULT_ARM_MSK, 1) |
				     AD8460_CURRENT_LIMIT_CONV(tmp[1]));

		if (in_range(tmp[0], -AD8460_ABS_MAX_OVERCURRENT_UA, 0))
			regmap_write(state->regmap, AD8460_CTRL_REG(0x09),
				     FIELD_PREP(AD8460_FAULT_ARM_MSK, 1) |
				     AD8460_CURRENT_LIMIT_CONV(abs(tmp[0])));
	}

	ret = device_property_read_u32_array(dev, "adi,range-microvolt",
					     tmp, ARRAY_SIZE(tmp));
	if (!ret) {
		if (in_range(tmp[1], 0, AD8460_ABS_MAX_OVERVOLTAGE_UV))
			regmap_write(state->regmap, AD8460_CTRL_REG(0x0A),
				     FIELD_PREP(AD8460_FAULT_ARM_MSK, 1) |
				     AD8460_VOLTAGE_LIMIT_CONV(tmp[1]));

		if (in_range(tmp[0], -AD8460_ABS_MAX_OVERVOLTAGE_UV, 0))
			regmap_write(state->regmap, AD8460_CTRL_REG(0x0B),
				     FIELD_PREP(AD8460_FAULT_ARM_MSK, 1) |
				     AD8460_VOLTAGE_LIMIT_CONV(abs(tmp[0])));
	}

	ret = device_property_read_u32(dev, "adi,max-millicelsius", &temp);
	if (!ret) {
		if (in_range(temp, AD8460_MIN_OVERTEMPERATURE_MC,
			     AD8460_MAX_OVERTEMPERATURE_MC))
			regmap_write(state->regmap, AD8460_CTRL_REG(0x0C),
				     FIELD_PREP(AD8460_FAULT_ARM_MSK, 1) |
				     AD8460_TEMP_LIMIT_CONV(abs(temp)));
	}

	ret = ad8460_reset(state);
	if (ret)
		return ret;

	/* Enables DAC by default */
	ret = regmap_clear_bits(state->regmap, AD8460_CTRL_REG(0x01),
				AD8460_HVDAC_SLEEP_MSK);
	if (ret)
		return ret;

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->setup_ops = &ad8460_buffer_setup_ops;

	ret = devm_iio_dmaengine_buffer_setup_ext(dev, indio_dev, "tx",
						  IIO_BUFFER_DIRECTION_OUT);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get DMA buffer\n");

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ad8460_of_match[] = {
	{ .compatible = "adi,ad8460" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad8460_of_match);

static const struct spi_device_id ad8460_spi_match[] = {
	{ .name = "ad8460" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad8460_spi_match);

static struct spi_driver ad8460_driver = {
	.driver = {
		.name = "ad8460",
		.of_match_table = ad8460_of_match,
	},
	.probe = ad8460_probe,
	.id_table = ad8460_spi_match,
};
module_spi_driver(ad8460_driver);

MODULE_AUTHOR("Mariel Tinaco <mariel.tinaco@analog.com");
MODULE_DESCRIPTION("AD8460 DAC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
