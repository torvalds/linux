// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Nicera D3-323-AA PIR sensor.
 *
 * Copyright (C) 2025 Axis Communications AB
 */

#include <linux/bitmap.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

/*
 * Register bitmap.
 * For some reason the first bit is denoted as F37 in the datasheet, the second
 * as F38 and so on. Note the gap between F60 and F64.
 */
#define D3323AA_REG_BIT_SLAVEA1		0	/* F37. */
#define D3323AA_REG_BIT_SLAVEA2		1	/* F38. */
#define D3323AA_REG_BIT_SLAVEA3		2	/* F39. */
#define D3323AA_REG_BIT_SLAVEA4		3	/* F40. */
#define D3323AA_REG_BIT_SLAVEA5		4	/* F41. */
#define D3323AA_REG_BIT_SLAVEA6		5	/* F42. */
#define D3323AA_REG_BIT_SLAVEA7		6	/* F43. */
#define D3323AA_REG_BIT_SLAVEA8		7	/* F44. */
#define D3323AA_REG_BIT_SLAVEA9		8	/* F45. */
#define D3323AA_REG_BIT_SLAVEA10	9	/* F46. */
#define D3323AA_REG_BIT_DETLVLABS0	10	/* F47. */
#define D3323AA_REG_BIT_DETLVLABS1	11	/* F48. */
#define D3323AA_REG_BIT_DETLVLABS2	12	/* F49. */
#define D3323AA_REG_BIT_DETLVLABS3	13	/* F50. */
#define D3323AA_REG_BIT_DETLVLABS4	14	/* F51. */
#define D3323AA_REG_BIT_DETLVLABS5	15	/* F52. */
#define D3323AA_REG_BIT_DETLVLABS6	16	/* F53. */
#define D3323AA_REG_BIT_DETLVLABS7	17	/* F54. */
#define D3323AA_REG_BIT_DSLP		18	/* F55. */
#define D3323AA_REG_BIT_FSTEP0		19	/* F56. */
#define D3323AA_REG_BIT_FSTEP1		20	/* F57. */
#define D3323AA_REG_BIT_FILSEL0		21	/* F58. */
#define D3323AA_REG_BIT_FILSEL1		22	/* F59. */
#define D3323AA_REG_BIT_FILSEL2		23	/* F60. */
#define D3323AA_REG_BIT_FDSET		24	/* F64. */
#define D3323AA_REG_BIT_F65		25
#define D3323AA_REG_BIT_F87		(D3323AA_REG_BIT_F65 + (87 - 65))

#define D3323AA_REG_NR_BITS (D3323AA_REG_BIT_F87 - D3323AA_REG_BIT_SLAVEA1 + 1)
#define D3323AA_THRESH_REG_NR_BITS                                             \
	(D3323AA_REG_BIT_DETLVLABS7 - D3323AA_REG_BIT_DETLVLABS0 + 1)
#define D3323AA_FILTER_TYPE_NR_BITS                                            \
	(D3323AA_REG_BIT_FILSEL2 - D3323AA_REG_BIT_FILSEL0 + 1)
#define D3323AA_FILTER_GAIN_REG_NR_BITS                                        \
	(D3323AA_REG_BIT_FSTEP1 - D3323AA_REG_BIT_FSTEP0 + 1)

#define D3323AA_THRESH_DEFAULT_VAL 56
#define D3323AA_FILTER_GAIN_DEFAULT_IDX 1
#define D3323AA_LP_FILTER_FREQ_DEFAULT_IDX 1

/*
 * The pattern is 0b01101, but store it reversed (0b10110) due to writing from
 * LSB on the wire (c.f. d3323aa_write_settings()).
 */
#define D3323AA_SETTING_END_PATTERN 0x16
#define D3323AA_SETTING_END_PATTERN_NR_BITS 5

/*
 * Device should be ready for configuration after this many milliseconds.
 * Datasheet mentions "approx. 1.2 s". Measurements show around 1.23 s,
 * therefore add 100 ms of slack.
 */
#define D3323AA_RESET_TIMEOUT (1200 + 100)

/*
 * The configuration of the device (write and read) should be done within this
 * many milliseconds.
 */
#define D3323AA_CONFIG_TIMEOUT 1400

/* Number of IRQs needed for configuration stage after reset. */
#define D3323AA_IRQ_RESET_COUNT 2

/*
 * High-pass filter cutoff frequency for the band-pass filter. There is a
 * corresponding low-pass cutoff frequency for each of the filter types
 * (denoted A, B, C and D in the datasheet). The index in this array matches
 * that corresponding value in d3323aa_lp_filter_freq.
 * Note that this represents a fractional value (e.g. the first value
 * corresponds to 40 / 100 = 0.4 Hz).
 */
static const int d3323aa_hp_filter_freq[][2] = {
	{ 40, 100 },
	{ 30, 100 },
	{ 30, 100 },
	{ 1, 100 },
};

/*
 * Low-pass filter cutoff frequency for the band-pass filter. There is a
 * corresponding high-pass cutoff frequency for each of the filter types
 * (denoted A, B, C and D in the datasheet). The index in this array matches
 * that corresponding value in d3323aa_hp_filter_freq.
 * Note that this represents a fractional value (e.g. the first value
 * corresponds to 27 / 10 = 2.7 Hz).
 */
static const int d3323aa_lp_filter_freq[][2] = {
	{ 27, 10 },
	{ 15, 10 },
	{ 5, 1 },
	{ 100, 1 },
};

/*
 * Register bitmap values for filter types (denoted A, B, C and D in the
 * datasheet). The index in this array matches the corresponding value in
 * d3323aa_lp_filter_freq (which in turn matches d3323aa_hp_filter_freq). For
 * example, the first value 7 corresponds to 2.7 Hz low-pass and 0.4 Hz
 * high-pass cutoff frequency.
 */
static const int d3323aa_lp_filter_regval[] = {
	7,
	0,
	1,
	2,
};

/*
 * This is denoted as "step" in datasheet and corresponds to the gain at peak
 * for the band-pass filter. The index in this array is the corresponding index
 * in d3323aa_filter_gain_regval for the register bitmap value.
 */
static const int d3323aa_filter_gain[] = { 1, 2, 3 };

/*
 * Register bitmap values for the filter gain. The index in this array is the
 * corresponding index in d3323aa_filter_gain for the gain value.
 */
static const u8 d3323aa_filter_gain_regval[] = { 1, 3, 0 };

struct d3323aa_data {
	struct completion reset_completion;
	/*
	 *  Since the setup process always requires a complete write of _all_
	 *  the state variables, we need to synchronize them with a lock.
	 */
	struct mutex statevar_lock;

	struct device *dev;

	/* Supply voltage. */
	struct regulator *regulator_vdd;
	/* Input clock or output detection signal (Vout). */
	struct gpio_desc *gpiod_clkin_detectout;
	/* Input (setting) or output data. */
	struct gpio_desc *gpiod_data;

	/*
	 * We only need the low-pass cutoff frequency to unambiguously choose
	 * the type of band-pass filter. For example, both filter type B and C
	 * have 0.3 Hz as high-pass cutoff frequency (see
	 * d3323aa_hp_filter_freq).
	 */
	size_t lp_filter_freq_idx;
	size_t filter_gain_idx;
	u8 detect_thresh;
	u8 irq_reset_count;

	/* Indicator for operational mode (configuring or detecting). */
	bool detecting;
};

static int d3323aa_read_settings(struct iio_dev *indio_dev,
				 unsigned long *regbitmap)
{
	struct d3323aa_data *data = iio_priv(indio_dev);
	size_t i;
	int ret;

	/* Bit bang the clock and data pins. */
	ret = gpiod_direction_output(data->gpiod_clkin_detectout, 0);
	if (ret)
		return ret;

	ret = gpiod_direction_input(data->gpiod_data);
	if (ret)
		return ret;

	dev_dbg(data->dev, "Reading settings...\n");

	for (i = 0; i < D3323AA_REG_NR_BITS; ++i) {
		/* Clock frequency needs to be 1 kHz. */
		gpiod_set_value(data->gpiod_clkin_detectout, 1);
		udelay(500);

		/* The data seems to change when clock signal is high. */
		if (gpiod_get_value(data->gpiod_data))
			set_bit(i, regbitmap);

		gpiod_set_value(data->gpiod_clkin_detectout, 0);
		udelay(500);
	}

	/* The first bit (F37) is just dummy data. Discard it. */
	clear_bit(0, regbitmap);

	/* Datasheet says to wait 30 ms after reading the settings. */
	msleep(30);

	return 0;
}

static int d3323aa_write_settings(struct iio_dev *indio_dev,
				  unsigned long *written_regbitmap)
{
#define REGBITMAP_LEN \
	(D3323AA_REG_NR_BITS + D3323AA_SETTING_END_PATTERN_NR_BITS)
	DECLARE_BITMAP(regbitmap, REGBITMAP_LEN);
	struct d3323aa_data *data = iio_priv(indio_dev);
	size_t i;
	int ret;

	/* Build the register bitmap. */
	bitmap_zero(regbitmap, REGBITMAP_LEN);
	bitmap_write(regbitmap, data->detect_thresh, D3323AA_REG_BIT_DETLVLABS0,
		     D3323AA_REG_BIT_DETLVLABS7 - D3323AA_REG_BIT_DETLVLABS0 +
			     1);
	bitmap_write(regbitmap,
		     d3323aa_filter_gain_regval[data->filter_gain_idx],
		     D3323AA_REG_BIT_FSTEP0,
		     D3323AA_REG_BIT_FSTEP1 - D3323AA_REG_BIT_FSTEP0 + 1);
	bitmap_write(regbitmap,
		     d3323aa_lp_filter_regval[data->lp_filter_freq_idx],
		     D3323AA_REG_BIT_FILSEL0,
		     D3323AA_REG_BIT_FILSEL2 - D3323AA_REG_BIT_FILSEL0 + 1);
	/* Compulsory end pattern. */
	bitmap_write(regbitmap, D3323AA_SETTING_END_PATTERN,
		     D3323AA_REG_NR_BITS, D3323AA_SETTING_END_PATTERN_NR_BITS);

	/* Bit bang the clock and data pins. */
	ret = gpiod_direction_output(data->gpiod_clkin_detectout, 0);
	if (ret)
		return ret;

	ret = gpiod_direction_output(data->gpiod_data, 0);
	if (ret)
		return ret;

	dev_dbg(data->dev, "Writing settings...\n");

	/* First bit (F37) is not used when writing the register bitmap. */
	for (i = 1; i < REGBITMAP_LEN; ++i) {
		gpiod_set_value(data->gpiod_data, test_bit(i, regbitmap));

		/* Clock frequency needs to be 1 kHz. */
		gpiod_set_value(data->gpiod_clkin_detectout, 1);
		udelay(500);
		gpiod_set_value(data->gpiod_clkin_detectout, 0);
		udelay(500);
	}

	/* Datasheet says to wait 30 ms after writing the settings. */
	msleep(30);

	bitmap_copy(written_regbitmap, regbitmap, D3323AA_REG_NR_BITS);

	return 0;
}

static irqreturn_t d3323aa_irq_handler(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct d3323aa_data *data = iio_priv(indio_dev);
	enum iio_event_direction dir;
	int val;

	val = gpiod_get_value(data->gpiod_clkin_detectout);
	if (val < 0) {
		dev_err_ratelimited(data->dev,
				    "Could not read from GPIO vout-clk (%d)\n",
				    val);
		return IRQ_HANDLED;
	}

	if (!data->detecting) {
		/* Reset interrupt counting falling edges. */
		if (!val && ++data->irq_reset_count == D3323AA_IRQ_RESET_COUNT)
			complete(&data->reset_completion);

		return IRQ_HANDLED;
	}

	/* Detection interrupt. */
	dir = val ? IIO_EV_DIR_RISING : IIO_EV_DIR_FALLING;
	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
					    IIO_EV_TYPE_THRESH, dir),
		       iio_get_time_ns(indio_dev));

	return IRQ_HANDLED;
}

static int d3323aa_reset(struct iio_dev *indio_dev)
{
	struct d3323aa_data *data = iio_priv(indio_dev);
	long time;
	int ret;

	/* During probe() the regulator may already be disabled. */
	if (regulator_is_enabled(data->regulator_vdd)) {
		ret = regulator_disable(data->regulator_vdd);
		if (ret)
			return ret;
	}

	/*
	 * Datasheet says VDD needs to be low at least for 30 ms. Let's add a
	 * couple more to allow VDD to completely discharge as well.
	 */
	fsleep((30 + 5) * USEC_PER_MSEC);

	/*
	 * When later enabling VDD, the device will signal with
	 * D3323AA_IRQ_RESET_COUNT falling edges on Vout/CLK that it is now
	 * ready for configuration. Datasheet says that this should happen
	 * within D3323AA_RESET_TIMEOUT ms. Count these two edges within that
	 * timeout.
	 */
	data->irq_reset_count = 0;
	reinit_completion(&data->reset_completion);
	data->detecting = false;

	ret = gpiod_direction_input(data->gpiod_clkin_detectout);
	if (ret)
		return ret;

	dev_dbg(data->dev, "Resetting...\n");

	ret = regulator_enable(data->regulator_vdd);
	if (ret)
		return ret;

	/*
	 * Wait for VDD to completely charge up. Measurements have shown that
	 * Vout/CLK signal slowly ramps up during this period. Thus, the digital
	 * signal will have bogus values. It is therefore necessary to wait
	 * before we can count the "real" falling edges.
	 */
	fsleep(2000);

	time = wait_for_completion_killable_timeout(
		&data->reset_completion,
		msecs_to_jiffies(D3323AA_RESET_TIMEOUT));
	if (time == 0) {
		return -ETIMEDOUT;
	} else if (time < 0) {
		/* Got interrupted. */
		return time;
	}

	dev_dbg(data->dev, "Reset completed\n");

	return 0;
}

static int d3323aa_setup(struct iio_dev *indio_dev, size_t lp_filter_freq_idx,
			 size_t filter_gain_idx, u8 detect_thresh)
{
	DECLARE_BITMAP(write_regbitmap, D3323AA_REG_NR_BITS);
	DECLARE_BITMAP(read_regbitmap, D3323AA_REG_NR_BITS);
	struct d3323aa_data *data = iio_priv(indio_dev);
	unsigned long start_time;
	int ret;

	ret = d3323aa_reset(indio_dev);
	if (ret) {
		if (ret != -ERESTARTSYS)
			dev_err(data->dev, "Could not reset device (%d)\n",
				ret);

		return ret;
	}

	/*
	 * Datasheet says to wait 10 us before setting the configuration.
	 * Moreover, the total configuration should be done within
	 * D3323AA_CONFIG_TIMEOUT ms. Clock it.
	 */
	fsleep(10);
	start_time = jiffies;

	ret = d3323aa_write_settings(indio_dev, write_regbitmap);
	if (ret) {
		dev_err(data->dev, "Could not write settings (%d)\n", ret);
		return ret;
	}

	ret = d3323aa_read_settings(indio_dev, read_regbitmap);
	if (ret) {
		dev_err(data->dev, "Could not read settings (%d)\n", ret);
		return ret;
	}

	if (time_is_before_jiffies(start_time +
				   msecs_to_jiffies(D3323AA_CONFIG_TIMEOUT))) {
		dev_err(data->dev, "Could not set up configuration in time\n");
		return -EAGAIN;
	}

	/* Check if settings were set successfully. */
	if (!bitmap_equal(write_regbitmap, read_regbitmap,
			  D3323AA_REG_NR_BITS)) {
		dev_err(data->dev, "Settings data mismatch\n");
		return -EIO;
	}

	/* Now in operational mode. */
	ret = gpiod_direction_input(data->gpiod_clkin_detectout);
	if (ret) {
		dev_err(data->dev,
			"Could not set GPIO vout-clk as input (%d)\n", ret);
		return ret;
	}

	ret = gpiod_direction_input(data->gpiod_data);
	if (ret) {
		dev_err(data->dev, "Could not set GPIO data as input (%d)\n",
			ret);
		return ret;
	}

	data->lp_filter_freq_idx = lp_filter_freq_idx;
	data->filter_gain_idx = filter_gain_idx;
	data->detect_thresh = detect_thresh;
	data->detecting = true;

	dev_dbg(data->dev, "Setup done\n");

	return 0;
}

static int d3323aa_set_lp_filter_freq(struct iio_dev *indio_dev, const int val,
				      int val2)
{
	struct d3323aa_data *data = iio_priv(indio_dev);
	size_t idx;

	/* Truncate fractional part to one digit. */
	val2 /= 100000;

	for (idx = 0; idx < ARRAY_SIZE(d3323aa_lp_filter_freq); ++idx) {
		int integer = d3323aa_lp_filter_freq[idx][0] /
			      d3323aa_lp_filter_freq[idx][1];
		int fract = d3323aa_lp_filter_freq[idx][0] %
			    d3323aa_lp_filter_freq[idx][1];

		if (val == integer && val2 == fract)
			break;
	}

	if (idx == ARRAY_SIZE(d3323aa_lp_filter_freq))
		return -EINVAL;

	return d3323aa_setup(indio_dev, idx, data->filter_gain_idx,
			     data->detect_thresh);
}

static int d3323aa_set_hp_filter_freq(struct iio_dev *indio_dev, const int val,
				      int val2)
{
	struct d3323aa_data *data = iio_priv(indio_dev);
	size_t idx;

	/* Truncate fractional part to two digits. */
	val2 /= 10000;

	for (idx = 0; idx < ARRAY_SIZE(d3323aa_hp_filter_freq); ++idx) {
		int integer = d3323aa_hp_filter_freq[idx][0] /
			      d3323aa_hp_filter_freq[idx][1];
		int fract = d3323aa_hp_filter_freq[idx][0] %
			    d3323aa_hp_filter_freq[idx][1];

		if (val == integer && val2 == fract)
			break;
	}

	if (idx == ARRAY_SIZE(d3323aa_hp_filter_freq))
		return -EINVAL;

	if (idx == data->lp_filter_freq_idx) {
		/* Corresponding filter frequency already set. */
		return 0;
	}

	if (idx == 1 && data->lp_filter_freq_idx == 2) {
		/*
		 * The low-pass cutoff frequency is the only way to
		 * unambiguously choose the type of band-pass filter. For
		 * example, both filter type B (index 1) and C (index 2) have
		 * 0.3 Hz as high-pass cutoff frequency (see
		 * d3323aa_hp_filter_freq). Therefore, if one of these are
		 * requested _and_ the corresponding low-pass filter frequency
		 * is already set, we can't know which filter type is the wanted
		 * one. The low-pass filter frequency is the decider (i.e. in
		 * this case index 2).
		 */
		return 0;
	}

	return d3323aa_setup(indio_dev, idx, data->filter_gain_idx,
			     data->detect_thresh);
}

static int d3323aa_set_filter_gain(struct iio_dev *indio_dev, const int val)
{
	struct d3323aa_data *data = iio_priv(indio_dev);
	size_t idx;

	for (idx = 0; idx < ARRAY_SIZE(d3323aa_filter_gain); ++idx) {
		if (d3323aa_filter_gain[idx] == val)
			break;
	}

	if (idx == ARRAY_SIZE(d3323aa_filter_gain))
		return -EINVAL;

	return d3323aa_setup(indio_dev, data->lp_filter_freq_idx, idx,
			     data->detect_thresh);
}

static int d3323aa_set_threshold(struct iio_dev *indio_dev, const int val)
{
	struct d3323aa_data *data = iio_priv(indio_dev);

	if (val > ((1 << D3323AA_THRESH_REG_NR_BITS) - 1))
		return -EINVAL;

	return d3323aa_setup(indio_dev, data->lp_filter_freq_idx,
			     data->filter_gain_idx, val);
}

static int d3323aa_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*vals = (int *)d3323aa_hp_filter_freq;
		*type = IIO_VAL_FRACTIONAL;
		*length = 2 * ARRAY_SIZE(d3323aa_hp_filter_freq);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*vals = (int *)d3323aa_lp_filter_freq;
		*type = IIO_VAL_FRACTIONAL;
		*length = 2 * ARRAY_SIZE(d3323aa_lp_filter_freq);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		*vals = (int *)d3323aa_filter_gain;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(d3323aa_filter_gain);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int d3323aa_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct d3323aa_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->statevar_lock);

	switch (mask) {
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*val = d3323aa_hp_filter_freq[data->lp_filter_freq_idx][0];
		*val2 = d3323aa_hp_filter_freq[data->lp_filter_freq_idx][1];
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*val = d3323aa_lp_filter_freq[data->lp_filter_freq_idx][0];
		*val2 = d3323aa_lp_filter_freq[data->lp_filter_freq_idx][1];
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		*val = d3323aa_filter_gain[data->filter_gain_idx];
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int d3323aa_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int val,
			     int val2, long mask)
{
	struct d3323aa_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->statevar_lock);

	switch (mask) {
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		return d3323aa_set_hp_filter_freq(indio_dev, val, val2);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return d3323aa_set_lp_filter_freq(indio_dev, val, val2);
	case IIO_CHAN_INFO_HARDWAREGAIN:
		return d3323aa_set_filter_gain(indio_dev, val);
	default:
		return -EINVAL;
	}
}

static int d3323aa_read_event(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir,
			      enum iio_event_info info, int *val, int *val2)
{
	struct d3323aa_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->statevar_lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = data->detect_thresh;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int d3323aa_write_event(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info, int val, int val2)
{
	struct d3323aa_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->statevar_lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return d3323aa_set_threshold(indio_dev, val);
	default:
		return -EINVAL;
	}
}

static const struct iio_info d3323aa_info = {
	.read_avail = d3323aa_read_avail,
	.read_raw = d3323aa_read_raw,
	.write_raw = d3323aa_write_raw,
	.read_event_value = d3323aa_read_event,
	.write_event_value = d3323aa_write_event,
};

static const struct iio_event_spec d3323aa_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_chan_spec d3323aa_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY) |
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |
			BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.info_mask_separate_available =
			BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY) |
			BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |
			BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.event_spec = d3323aa_event_spec,
		.num_event_specs = ARRAY_SIZE(d3323aa_event_spec),
	},
};

static void d3323aa_disable_regulator(void *indata)
{
	struct d3323aa_data *data = indata;
	int ret;

	/*
	 * During probe() the regulator may be disabled. It is enabled during
	 * device setup (in d3323aa_reset(), where it is also briefly disabled).
	 * The check is therefore needed in order to have balanced
	 * regulator_enable/disable() calls.
	 */
	if (!regulator_is_enabled(data->regulator_vdd))
		return;

	ret = regulator_disable(data->regulator_vdd);
	if (ret)
		dev_err(data->dev, "Could not disable regulator (%d)\n", ret);
}

static int d3323aa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct d3323aa_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = dev;

	init_completion(&data->reset_completion);

	ret = devm_mutex_init(dev, &data->statevar_lock);
	if (ret)
		return dev_err_probe(dev, ret, "Could not initialize mutex\n");

	data->regulator_vdd = devm_regulator_get_exclusive(dev, "vdd");
	if (IS_ERR(data->regulator_vdd))
		return dev_err_probe(dev, PTR_ERR(data->regulator_vdd),
				     "Could not get regulator\n");

	/*
	 * The regulator will be enabled for the first time during the
	 * device setup below (in d3323aa_reset()). However parameter changes
	 * from userspace can require a temporary disable of the regulator.
	 * To avoid complex handling of state, use a callback that will disable
	 * the regulator if it happens to be enabled at time of devm unwind.
	 */
	ret = devm_add_action_or_reset(dev, d3323aa_disable_regulator, data);
	if (ret)
		return ret;

	data->gpiod_clkin_detectout =
		devm_gpiod_get(dev, "vout-clk", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_clkin_detectout))
		return dev_err_probe(dev, PTR_ERR(data->gpiod_clkin_detectout),
				     "Could not get GPIO vout-clk\n");

	data->gpiod_data = devm_gpiod_get(dev, "data", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_data))
		return dev_err_probe(dev, PTR_ERR(data->gpiod_data),
				     "Could not get GPIO data\n");

	ret = gpiod_to_irq(data->gpiod_clkin_detectout);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Could not get IRQ\n");

	/*
	 * Device signals with a rising or falling detection signal when the
	 * proximity data is above or below the threshold, respectively.
	 */
	ret = devm_request_irq(dev, ret, d3323aa_irq_handler,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			       dev_name(dev), indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Could not request IRQ\n");

	ret = d3323aa_setup(indio_dev, D3323AA_LP_FILTER_FREQ_DEFAULT_IDX,
			    D3323AA_FILTER_GAIN_DEFAULT_IDX,
			    D3323AA_THRESH_DEFAULT_VAL);
	if (ret)
		return ret;

	indio_dev->info = &d3323aa_info;
	indio_dev->name = "d3323aa";
	indio_dev->channels = d3323aa_channels;
	indio_dev->num_channels = ARRAY_SIZE(d3323aa_channels);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Could not register iio device\n");

	return 0;
}

static const struct of_device_id d3323aa_of_match[] = {
	{
		.compatible = "nicera,d3323aa",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, d3323aa_of_match);

static struct platform_driver d3323aa_driver = {
	.probe = d3323aa_probe,
	.driver = {
		.name = "d3323aa",
		.of_match_table = d3323aa_of_match,
	},
};
module_platform_driver(d3323aa_driver);

MODULE_AUTHOR("Waqar Hameed <waqar.hameed@axis.com>");
MODULE_DESCRIPTION("Nicera D3-323-AA PIR sensor driver");
MODULE_LICENSE("GPL");
