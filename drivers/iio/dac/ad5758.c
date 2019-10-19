// SPDX-License-Identifier: GPL-2.0
/*
 * AD5758 Digital to analog converters driver
 *
 * Copyright 2018 Analog Devices Inc.
 *
 * TODO: Currently CRC is not supported in this driver
 */
#include <linux/bsearch.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <linux/gpio/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* AD5758 registers definition */
#define AD5758_NOP				0x00
#define AD5758_DAC_INPUT			0x01
#define AD5758_DAC_OUTPUT			0x02
#define AD5758_CLEAR_CODE			0x03
#define AD5758_USER_GAIN			0x04
#define AD5758_USER_OFFSET			0x05
#define AD5758_DAC_CONFIG			0x06
#define AD5758_SW_LDAC				0x07
#define AD5758_KEY				0x08
#define AD5758_GP_CONFIG1			0x09
#define AD5758_GP_CONFIG2			0x0A
#define AD5758_DCDC_CONFIG1			0x0B
#define AD5758_DCDC_CONFIG2			0x0C
#define AD5758_WDT_CONFIG			0x0F
#define AD5758_DIGITAL_DIAG_CONFIG		0x10
#define AD5758_ADC_CONFIG			0x11
#define AD5758_FAULT_PIN_CONFIG			0x12
#define AD5758_TWO_STAGE_READBACK_SELECT	0x13
#define AD5758_DIGITAL_DIAG_RESULTS		0x14
#define AD5758_ANALOG_DIAG_RESULTS		0x15
#define AD5758_STATUS				0x16
#define AD5758_CHIP_ID				0x17
#define AD5758_FREQ_MONITOR			0x18
#define AD5758_DEVICE_ID_0			0x19
#define AD5758_DEVICE_ID_1			0x1A
#define AD5758_DEVICE_ID_2			0x1B
#define AD5758_DEVICE_ID_3			0x1C

/* AD5758_DAC_CONFIG */
#define AD5758_DAC_CONFIG_RANGE_MSK		GENMASK(3, 0)
#define AD5758_DAC_CONFIG_RANGE_MODE(x)		(((x) & 0xF) << 0)
#define AD5758_DAC_CONFIG_INT_EN_MSK		BIT(5)
#define AD5758_DAC_CONFIG_INT_EN_MODE(x)	(((x) & 0x1) << 5)
#define AD5758_DAC_CONFIG_OUT_EN_MSK		BIT(6)
#define AD5758_DAC_CONFIG_OUT_EN_MODE(x)	(((x) & 0x1) << 6)
#define AD5758_DAC_CONFIG_SR_EN_MSK		BIT(8)
#define AD5758_DAC_CONFIG_SR_EN_MODE(x)		(((x) & 0x1) << 8)
#define AD5758_DAC_CONFIG_SR_CLOCK_MSK		GENMASK(12, 9)
#define AD5758_DAC_CONFIG_SR_CLOCK_MODE(x)	(((x) & 0xF) << 9)
#define AD5758_DAC_CONFIG_SR_STEP_MSK		GENMASK(15, 13)
#define AD5758_DAC_CONFIG_SR_STEP_MODE(x)	(((x) & 0x7) << 13)

/* AD5758_KEY */
#define AD5758_KEY_CODE_RESET_1			0x15FA
#define AD5758_KEY_CODE_RESET_2			0xAF51
#define AD5758_KEY_CODE_SINGLE_ADC_CONV		0x1ADC
#define AD5758_KEY_CODE_RESET_WDT		0x0D06
#define AD5758_KEY_CODE_CALIB_MEM_REFRESH	0xFCBA

/* AD5758_DCDC_CONFIG1 */
#define AD5758_DCDC_CONFIG1_DCDC_VPROG_MSK	GENMASK(4, 0)
#define AD5758_DCDC_CONFIG1_DCDC_VPROG_MODE(x)	(((x) & 0x1F) << 0)
#define AD5758_DCDC_CONFIG1_DCDC_MODE_MSK	GENMASK(6, 5)
#define AD5758_DCDC_CONFIG1_DCDC_MODE_MODE(x)	(((x) & 0x3) << 5)

/* AD5758_DCDC_CONFIG2 */
#define AD5758_DCDC_CONFIG2_ILIMIT_MSK		GENMASK(3, 1)
#define AD5758_DCDC_CONFIG2_ILIMIT_MODE(x)	(((x) & 0x7) << 1)
#define AD5758_DCDC_CONFIG2_INTR_SAT_3WI_MSK	BIT(11)
#define AD5758_DCDC_CONFIG2_BUSY_3WI_MSK	BIT(12)

/* AD5758_DIGITAL_DIAG_RESULTS */
#define AD5758_CAL_MEM_UNREFRESHED_MSK		BIT(15)

/* AD5758_ADC_CONFIG */
#define AD5758_ADC_CONFIG_PPC_BUF_EN(x)		(((x) & 0x1) << 11)
#define AD5758_ADC_CONFIG_PPC_BUF_MSK		BIT(11)

#define AD5758_WR_FLAG_MSK(x)		(0x80 | ((x) & 0x1F))

#define AD5758_FULL_SCALE_MICRO	65535000000ULL

/**
 * struct ad5758_state - driver instance specific data
 * @spi:	spi_device
 * @lock:	mutex lock
 * @out_range:	struct which stores the output range
 * @dc_dc_mode:	variable which stores the mode of operation
 * @dc_dc_ilim:	variable which stores the dc-to-dc converter current limit
 * @slew_time:	variable which stores the target slew time
 * @pwr_down:	variable which contains whether a channel is powered down or not
 * @data:	spi transfer buffers
 */

struct ad5758_range {
	int reg;
	int min;
	int max;
};

struct ad5758_state {
	struct spi_device *spi;
	struct mutex lock;
	struct gpio_desc *gpio_reset;
	struct ad5758_range out_range;
	unsigned int dc_dc_mode;
	unsigned int dc_dc_ilim;
	unsigned int slew_time;
	bool pwr_down;
	__be32 d32[3];
};

/**
 * Output ranges corresponding to bits [3:0] from DAC_CONFIG register
 * 0000: 0 V to 5 V voltage range
 * 0001: 0 V to 10 V voltage range
 * 0010: ±5 V voltage range
 * 0011: ±10 V voltage range
 * 1000: 0 mA to 20 mA current range
 * 1001: 0 mA to 24 mA current range
 * 1010: 4 mA to 20 mA current range
 * 1011: ±20 mA current range
 * 1100: ±24 mA current range
 * 1101: -1 mA to +22 mA current range
 */
enum ad5758_output_range {
	AD5758_RANGE_0V_5V,
	AD5758_RANGE_0V_10V,
	AD5758_RANGE_PLUSMINUS_5V,
	AD5758_RANGE_PLUSMINUS_10V,
	AD5758_RANGE_0mA_20mA = 8,
	AD5758_RANGE_0mA_24mA,
	AD5758_RANGE_4mA_24mA,
	AD5758_RANGE_PLUSMINUS_20mA,
	AD5758_RANGE_PLUSMINUS_24mA,
	AD5758_RANGE_MINUS_1mA_PLUS_22mA,
};

enum ad5758_dc_dc_mode {
	AD5758_DCDC_MODE_POWER_OFF,
	AD5758_DCDC_MODE_DPC_CURRENT,
	AD5758_DCDC_MODE_DPC_VOLTAGE,
	AD5758_DCDC_MODE_PPC_CURRENT,
};

static const struct ad5758_range ad5758_voltage_range[] = {
	{ AD5758_RANGE_0V_5V, 0, 5000000 },
	{ AD5758_RANGE_0V_10V, 0, 10000000 },
	{ AD5758_RANGE_PLUSMINUS_5V, -5000000, 5000000 },
	{ AD5758_RANGE_PLUSMINUS_10V, -10000000, 10000000 }
};

static const struct ad5758_range ad5758_current_range[] = {
	{ AD5758_RANGE_0mA_20mA, 0, 20000},
	{ AD5758_RANGE_0mA_24mA, 0, 24000 },
	{ AD5758_RANGE_4mA_24mA, 4, 24000 },
	{ AD5758_RANGE_PLUSMINUS_20mA, -20000, 20000 },
	{ AD5758_RANGE_PLUSMINUS_24mA, -24000, 24000 },
	{ AD5758_RANGE_MINUS_1mA_PLUS_22mA, -1000, 22000 },
};

static const int ad5758_sr_clk[16] = {
	240000, 200000, 150000, 128000, 64000, 32000, 16000, 8000, 4000, 2000,
	1000, 512, 256, 128, 64, 16
};

static const int ad5758_sr_step[8] = {
	4, 12, 64, 120, 256, 500, 1820, 2048
};

static const int ad5758_dc_dc_ilim[6] = {
	150000, 200000, 250000, 300000, 350000, 400000
};

static int ad5758_spi_reg_read(struct ad5758_state *st, unsigned int addr)
{
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->d32[0],
			.len = 4,
			.cs_change = 1,
		}, {
			.tx_buf = &st->d32[1],
			.rx_buf = &st->d32[2],
			.len = 4,
		},
	};
	int ret;

	st->d32[0] = cpu_to_be32(
		(AD5758_WR_FLAG_MSK(AD5758_TWO_STAGE_READBACK_SELECT) << 24) |
		(addr << 8));
	st->d32[1] = cpu_to_be32(AD5758_WR_FLAG_MSK(AD5758_NOP) << 24);

	ret = spi_sync_transfer(st->spi, t, ARRAY_SIZE(t));
	if (ret < 0)
		return ret;

	return (be32_to_cpu(st->d32[2]) >> 8) & 0xFFFF;
}

static int ad5758_spi_reg_write(struct ad5758_state *st,
				unsigned int addr,
				unsigned int val)
{
	st->d32[0] = cpu_to_be32((AD5758_WR_FLAG_MSK(addr) << 24) |
				 ((val & 0xFFFF) << 8));

	return spi_write(st->spi, &st->d32[0], sizeof(st->d32[0]));
}

static int ad5758_spi_write_mask(struct ad5758_state *st,
				 unsigned int addr,
				 unsigned long int mask,
				 unsigned int val)
{
	int regval;

	regval = ad5758_spi_reg_read(st, addr);
	if (regval < 0)
		return regval;

	regval &= ~mask;
	regval |= val;

	return ad5758_spi_reg_write(st, addr, regval);
}

static int cmpfunc(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int ad5758_find_closest_match(const int *array,
				     unsigned int size, int val)
{
	int i;

	for (i = 0; i < size; i++) {
		if (val <= array[i])
			return i;
	}

	return size - 1;
}

static int ad5758_wait_for_task_complete(struct ad5758_state *st,
					 unsigned int reg,
					 unsigned int mask)
{
	unsigned int timeout;
	int ret;

	timeout = 10;
	do {
		ret = ad5758_spi_reg_read(st, reg);
		if (ret < 0)
			return ret;

		if (!(ret & mask))
			return 0;

		usleep_range(100, 1000);
	} while (--timeout);

	dev_err(&st->spi->dev,
		"Error reading bit 0x%x in 0x%x register\n", mask, reg);

	return -EIO;
}

static int ad5758_calib_mem_refresh(struct ad5758_state *st)
{
	int ret;

	ret = ad5758_spi_reg_write(st, AD5758_KEY,
				   AD5758_KEY_CODE_CALIB_MEM_REFRESH);
	if (ret < 0) {
		dev_err(&st->spi->dev,
			"Failed to initiate a calibration memory refresh\n");
		return ret;
	}

	/* Wait to allow time for the internal calibrations to complete */
	return ad5758_wait_for_task_complete(st, AD5758_DIGITAL_DIAG_RESULTS,
					     AD5758_CAL_MEM_UNREFRESHED_MSK);
}

static int ad5758_soft_reset(struct ad5758_state *st)
{
	int ret;

	ret = ad5758_spi_reg_write(st, AD5758_KEY, AD5758_KEY_CODE_RESET_1);
	if (ret < 0)
		return ret;

	ret = ad5758_spi_reg_write(st, AD5758_KEY, AD5758_KEY_CODE_RESET_2);

	/* Perform a software reset and wait at least 100us */
	usleep_range(100, 1000);

	return ret;
}

static int ad5758_set_dc_dc_conv_mode(struct ad5758_state *st,
				      enum ad5758_dc_dc_mode mode)
{
	int ret;

	/*
	 * The ENABLE_PPC_BUFFERS bit must be set prior to enabling PPC current
	 * mode.
	 */
	if (mode == AD5758_DCDC_MODE_PPC_CURRENT) {
		ret  = ad5758_spi_write_mask(st, AD5758_ADC_CONFIG,
				    AD5758_ADC_CONFIG_PPC_BUF_MSK,
				    AD5758_ADC_CONFIG_PPC_BUF_EN(1));
		if (ret < 0)
			return ret;
	}

	ret = ad5758_spi_write_mask(st, AD5758_DCDC_CONFIG1,
				    AD5758_DCDC_CONFIG1_DCDC_MODE_MSK,
				    AD5758_DCDC_CONFIG1_DCDC_MODE_MODE(mode));
	if (ret < 0)
		return ret;

	/*
	 * Poll the BUSY_3WI bit in the DCDC_CONFIG2 register until it is 0.
	 * This allows the 3-wire interface communication to complete.
	 */
	ret = ad5758_wait_for_task_complete(st, AD5758_DCDC_CONFIG2,
					    AD5758_DCDC_CONFIG2_BUSY_3WI_MSK);
	if (ret < 0)
		return ret;

	st->dc_dc_mode = mode;

	return ret;
}

static int ad5758_set_dc_dc_ilim(struct ad5758_state *st, unsigned int ilim)
{
	int ret;

	ret = ad5758_spi_write_mask(st, AD5758_DCDC_CONFIG2,
				    AD5758_DCDC_CONFIG2_ILIMIT_MSK,
				    AD5758_DCDC_CONFIG2_ILIMIT_MODE(ilim));
	if (ret < 0)
		return ret;
	/*
	 * Poll the BUSY_3WI bit in the DCDC_CONFIG2 register until it is 0.
	 * This allows the 3-wire interface communication to complete.
	 */
	return ad5758_wait_for_task_complete(st, AD5758_DCDC_CONFIG2,
					     AD5758_DCDC_CONFIG2_BUSY_3WI_MSK);
}

static int ad5758_slew_rate_set(struct ad5758_state *st,
				unsigned int sr_clk_idx,
				unsigned int sr_step_idx)
{
	unsigned int mode;
	unsigned long int mask;
	int ret;

	mask = AD5758_DAC_CONFIG_SR_EN_MSK |
	       AD5758_DAC_CONFIG_SR_CLOCK_MSK |
	       AD5758_DAC_CONFIG_SR_STEP_MSK;
	mode = AD5758_DAC_CONFIG_SR_EN_MODE(1) |
	       AD5758_DAC_CONFIG_SR_STEP_MODE(sr_step_idx) |
	       AD5758_DAC_CONFIG_SR_CLOCK_MODE(sr_clk_idx);

	ret = ad5758_spi_write_mask(st, AD5758_DAC_CONFIG, mask, mode);
	if (ret < 0)
		return ret;

	/* Wait to allow time for the internal calibrations to complete */
	return ad5758_wait_for_task_complete(st, AD5758_DIGITAL_DIAG_RESULTS,
					     AD5758_CAL_MEM_UNREFRESHED_MSK);
}

static int ad5758_slew_rate_config(struct ad5758_state *st)
{
	unsigned int sr_clk_idx, sr_step_idx;
	int i, res;
	s64 diff_new, diff_old;
	u64 sr_step, calc_slew_time;

	sr_clk_idx = 0;
	sr_step_idx = 0;
	diff_old = S64_MAX;
	/*
	 * The slew time can be determined by using the formula:
	 * Slew Time = (Full Scale Out / (Step Size x Update Clk Freq))
	 * where Slew time is expressed in microseconds
	 * Given the desired slew time, the following algorithm determines the
	 * best match for the step size and the update clock frequency.
	 */
	for (i = 0; i < ARRAY_SIZE(ad5758_sr_clk); i++) {
		/*
		 * Go through each valid update clock freq and determine a raw
		 * value for the step size by using the formula:
		 * Step Size = Full Scale Out / (Update Clk Freq * Slew Time)
		 */
		sr_step = AD5758_FULL_SCALE_MICRO;
		do_div(sr_step, ad5758_sr_clk[i]);
		do_div(sr_step, st->slew_time);
		/*
		 * After a raw value for step size was determined, find the
		 * closest valid match
		 */
		res = ad5758_find_closest_match(ad5758_sr_step,
						ARRAY_SIZE(ad5758_sr_step),
						sr_step);
		/* Calculate the slew time */
		calc_slew_time = AD5758_FULL_SCALE_MICRO;
		do_div(calc_slew_time, ad5758_sr_step[res]);
		do_div(calc_slew_time, ad5758_sr_clk[i]);
		/*
		 * Determine with how many microseconds the calculated slew time
		 * is different from the desired slew time and store the diff
		 * for the next iteration
		 */
		diff_new = abs(st->slew_time - calc_slew_time);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			sr_clk_idx = i;
			sr_step_idx = res;
		}
	}

	return ad5758_slew_rate_set(st, sr_clk_idx, sr_step_idx);
}

static int ad5758_set_out_range(struct ad5758_state *st, int range)
{
	int ret;

	ret = ad5758_spi_write_mask(st, AD5758_DAC_CONFIG,
				    AD5758_DAC_CONFIG_RANGE_MSK,
				    AD5758_DAC_CONFIG_RANGE_MODE(range));
	if (ret < 0)
		return ret;

	/* Wait to allow time for the internal calibrations to complete */
	return ad5758_wait_for_task_complete(st, AD5758_DIGITAL_DIAG_RESULTS,
					     AD5758_CAL_MEM_UNREFRESHED_MSK);
}

static int ad5758_internal_buffers_en(struct ad5758_state *st, bool enable)
{
	int ret;

	ret = ad5758_spi_write_mask(st, AD5758_DAC_CONFIG,
				    AD5758_DAC_CONFIG_INT_EN_MSK,
				    AD5758_DAC_CONFIG_INT_EN_MODE(enable));
	if (ret < 0)
		return ret;

	/* Wait to allow time for the internal calibrations to complete */
	return ad5758_wait_for_task_complete(st, AD5758_DIGITAL_DIAG_RESULTS,
					     AD5758_CAL_MEM_UNREFRESHED_MSK);
}

static int ad5758_reset(struct ad5758_state *st)
{
	if (st->gpio_reset) {
		gpiod_set_value(st->gpio_reset, 0);
		usleep_range(100, 1000);
		gpiod_set_value(st->gpio_reset, 1);
		usleep_range(100, 1000);

		return 0;
	} else {
		/* Perform a software reset */
		return ad5758_soft_reset(st);
	}
}

static int ad5758_reg_access(struct iio_dev *indio_dev,
			     unsigned int reg,
			     unsigned int writeval,
			     unsigned int *readval)
{
	struct ad5758_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);
	if (readval) {
		ret = ad5758_spi_reg_read(st, reg);
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}

		*readval = ret;
		ret = 0;
	} else {
		ret = ad5758_spi_reg_write(st, reg, writeval);
	}
	mutex_unlock(&st->lock);

	return ret;
}

static int ad5758_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long info)
{
	struct ad5758_state *st = iio_priv(indio_dev);
	int max, min, ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = ad5758_spi_reg_read(st, AD5758_DAC_INPUT);
		mutex_unlock(&st->lock);
		if (ret < 0)
			return ret;

		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		min = st->out_range.min;
		max = st->out_range.max;
		*val = (max - min) / 1000;
		*val2 = 16;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_OFFSET:
		min = st->out_range.min;
		max = st->out_range.max;
		*val = ((min * (1 << 16)) / (max - min)) / 1000;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad5758_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long info)
{
	struct ad5758_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = ad5758_spi_reg_write(st, AD5758_DAC_INPUT, val);
		mutex_unlock(&st->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static ssize_t ad5758_read_powerdown(struct iio_dev *indio_dev,
				     uintptr_t priv,
				     const struct iio_chan_spec *chan,
				     char *buf)
{
	struct ad5758_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->pwr_down);
}

static ssize_t ad5758_write_powerdown(struct iio_dev *indio_dev,
				      uintptr_t priv,
				      struct iio_chan_spec const *chan,
				      const char *buf, size_t len)
{
	struct ad5758_state *st = iio_priv(indio_dev);
	bool pwr_down;
	unsigned int dac_config_mode, val;
	unsigned long int dac_config_msk;
	int ret;

	ret = kstrtobool(buf, &pwr_down);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	if (pwr_down)
		val = 0;
	else
		val = 1;

	dac_config_mode = AD5758_DAC_CONFIG_OUT_EN_MODE(val) |
			  AD5758_DAC_CONFIG_INT_EN_MODE(val);
	dac_config_msk = AD5758_DAC_CONFIG_OUT_EN_MSK |
			 AD5758_DAC_CONFIG_INT_EN_MSK;

	ret = ad5758_spi_write_mask(st, AD5758_DAC_CONFIG,
				    dac_config_msk,
				    dac_config_mode);
	if (ret < 0)
		goto err_unlock;

	st->pwr_down = pwr_down;

err_unlock:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static const struct iio_info ad5758_info = {
	.read_raw = ad5758_read_raw,
	.write_raw = ad5758_write_raw,
	.debugfs_reg_access = &ad5758_reg_access,
};

static const struct iio_chan_spec_ext_info ad5758_ext_info[] = {
	{
		.name = "powerdown",
		.read = ad5758_read_powerdown,
		.write = ad5758_write_powerdown,
		.shared = IIO_SHARED_BY_TYPE,
	},
	{ }
};

#define AD5758_DAC_CHAN(_chan_type) {				\
	.type = (_chan_type),					\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_RAW) |	\
		BIT(IIO_CHAN_INFO_SCALE) |			\
		BIT(IIO_CHAN_INFO_OFFSET),			\
	.indexed = 1,						\
	.output = 1,						\
	.ext_info = ad5758_ext_info,				\
}

static const struct iio_chan_spec ad5758_voltage_ch[] = {
	AD5758_DAC_CHAN(IIO_VOLTAGE)
};

static const struct iio_chan_spec ad5758_current_ch[] = {
	AD5758_DAC_CHAN(IIO_CURRENT)
};

static bool ad5758_is_valid_mode(enum ad5758_dc_dc_mode mode)
{
	switch (mode) {
	case AD5758_DCDC_MODE_DPC_CURRENT:
	case AD5758_DCDC_MODE_DPC_VOLTAGE:
	case AD5758_DCDC_MODE_PPC_CURRENT:
		return true;
	default:
		return false;
	}
}

static int ad5758_crc_disable(struct ad5758_state *st)
{
	unsigned int mask;

	mask = (AD5758_WR_FLAG_MSK(AD5758_DIGITAL_DIAG_CONFIG) << 24) | 0x5C3A;
	st->d32[0] = cpu_to_be32(mask);

	return spi_write(st->spi, &st->d32[0], 4);
}

static int ad5758_find_out_range(struct ad5758_state *st,
				 const struct ad5758_range *range,
				 unsigned int size,
				 int min, int max)
{
	int i;

	for (i = 0; i < size; i++) {
		if ((min == range[i].min) && (max == range[i].max)) {
			st->out_range.reg = range[i].reg;
			st->out_range.min = range[i].min;
			st->out_range.max = range[i].max;

			return 0;
		}
	}

	return -EINVAL;
}

static int ad5758_parse_dt(struct ad5758_state *st)
{
	unsigned int tmp, tmparray[2], size;
	const struct ad5758_range *range;
	int *index, ret;

	st->dc_dc_ilim = 0;
	ret = device_property_read_u32(&st->spi->dev,
				       "adi,dc-dc-ilim-microamp", &tmp);
	if (ret) {
		dev_dbg(&st->spi->dev,
			"Missing \"dc-dc-ilim-microamp\" property\n");
	} else {
		index = bsearch(&tmp, ad5758_dc_dc_ilim,
				ARRAY_SIZE(ad5758_dc_dc_ilim),
				sizeof(int), cmpfunc);
		if (!index)
			dev_dbg(&st->spi->dev, "dc-dc-ilim out of range\n");
		else
			st->dc_dc_ilim = index - ad5758_dc_dc_ilim;
	}

	ret = device_property_read_u32(&st->spi->dev, "adi,dc-dc-mode",
				       &st->dc_dc_mode);
	if (ret) {
		dev_err(&st->spi->dev, "Missing \"dc-dc-mode\" property\n");
		return ret;
	}

	if (!ad5758_is_valid_mode(st->dc_dc_mode))
		return -EINVAL;

	if (st->dc_dc_mode == AD5758_DCDC_MODE_DPC_VOLTAGE) {
		ret = device_property_read_u32_array(&st->spi->dev,
						     "adi,range-microvolt",
						     tmparray, 2);
		if (ret) {
			dev_err(&st->spi->dev,
				"Missing \"range-microvolt\" property\n");
			return ret;
		}
		range = ad5758_voltage_range;
		size = ARRAY_SIZE(ad5758_voltage_range);
	} else {
		ret = device_property_read_u32_array(&st->spi->dev,
						     "adi,range-microamp",
						     tmparray, 2);
		if (ret) {
			dev_err(&st->spi->dev,
				"Missing \"range-microamp\" property\n");
			return ret;
		}
		range = ad5758_current_range;
		size = ARRAY_SIZE(ad5758_current_range);
	}

	ret = ad5758_find_out_range(st, range, size, tmparray[0], tmparray[1]);
	if (ret) {
		dev_err(&st->spi->dev, "range invalid\n");
		return ret;
	}

	ret = device_property_read_u32(&st->spi->dev, "adi,slew-time-us", &tmp);
	if (ret) {
		dev_dbg(&st->spi->dev, "Missing \"slew-time-us\" property\n");
		st->slew_time = 0;
	} else {
		st->slew_time = tmp;
	}

	return 0;
}

static int ad5758_init(struct ad5758_state *st)
{
	int regval, ret;

	st->gpio_reset = devm_gpiod_get_optional(&st->spi->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(st->gpio_reset))
		return PTR_ERR(st->gpio_reset);

	/* Disable CRC checks */
	ret = ad5758_crc_disable(st);
	if (ret < 0)
		return ret;

	/* Perform a reset */
	ret = ad5758_reset(st);
	if (ret < 0)
		return ret;

	/* Disable CRC checks */
	ret = ad5758_crc_disable(st);
	if (ret < 0)
		return ret;

	/* Perform a calibration memory refresh */
	ret = ad5758_calib_mem_refresh(st);
	if (ret < 0)
		return ret;

	regval = ad5758_spi_reg_read(st, AD5758_DIGITAL_DIAG_RESULTS);
	if (regval < 0)
		return regval;

	/* Clear all the error flags */
	ret = ad5758_spi_reg_write(st, AD5758_DIGITAL_DIAG_RESULTS, regval);
	if (ret < 0)
		return ret;

	/* Set the dc-to-dc current limit */
	ret = ad5758_set_dc_dc_ilim(st, st->dc_dc_ilim);
	if (ret < 0)
		return ret;

	/* Configure the dc-to-dc controller mode */
	ret = ad5758_set_dc_dc_conv_mode(st, st->dc_dc_mode);
	if (ret < 0)
		return ret;

	/* Configure the output range */
	ret = ad5758_set_out_range(st, st->out_range.reg);
	if (ret < 0)
		return ret;

	/* Enable Slew Rate Control, set the slew rate clock and step */
	if (st->slew_time) {
		ret = ad5758_slew_rate_config(st);
		if (ret < 0)
			return ret;
	}

	/* Power up the DAC and internal (INT) amplifiers */
	ret = ad5758_internal_buffers_en(st, 1);
	if (ret < 0)
		return ret;

	/* Enable VIOUT */
	return ad5758_spi_write_mask(st, AD5758_DAC_CONFIG,
				     AD5758_DAC_CONFIG_OUT_EN_MSK,
				     AD5758_DAC_CONFIG_OUT_EN_MODE(1));
}

static int ad5758_probe(struct spi_device *spi)
{
	struct ad5758_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->spi = spi;

	mutex_init(&st->lock);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->info = &ad5758_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = 1;

	ret = ad5758_parse_dt(st);
	if (ret < 0)
		return ret;

	if (st->dc_dc_mode == AD5758_DCDC_MODE_DPC_VOLTAGE)
		indio_dev->channels = ad5758_voltage_ch;
	else
		indio_dev->channels = ad5758_current_ch;

	ret = ad5758_init(st);
	if (ret < 0) {
		dev_err(&spi->dev, "AD5758 init failed\n");
		return ret;
	}

	return devm_iio_device_register(&st->spi->dev, indio_dev);
}

static const struct spi_device_id ad5758_id[] = {
	{ "ad5758", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad5758_id);

static const struct of_device_id ad5758_of_match[] = {
        { .compatible = "adi,ad5758" },
        { },
};
MODULE_DEVICE_TABLE(of, ad5758_of_match);

static struct spi_driver ad5758_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = ad5758_of_match,
	},
	.probe = ad5758_probe,
	.id_table = ad5758_id,
};

module_spi_driver(ad5758_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5758 DAC");
MODULE_LICENSE("GPL v2");
