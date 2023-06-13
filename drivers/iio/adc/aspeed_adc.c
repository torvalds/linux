// SPDX-License-Identifier: GPL-2.0-only
/*
 * Aspeed AST2400/2500/2600 ADC
 *
 * Copyright (C) 2017 Google, Inc.
 * Copyright (C) 2021 Aspeed Technology Inc.
 *
 * ADC clock formula:
 * Ast2400/Ast2500:
 * clock period = period of PCLK * 2 * (ADC0C[31:17] + 1) * (ADC0C[9:0] + 1)
 * Ast2600:
 * clock period = period of PCLK * 2 * (ADC0C[15:0] + 1)
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iopoll.h>

#define ASPEED_RESOLUTION_BITS		10
#define ASPEED_CLOCKS_PER_SAMPLE	12

#define ASPEED_REG_ENGINE_CONTROL	0x00
#define ASPEED_REG_INTERRUPT_CONTROL	0x04
#define ASPEED_REG_VGA_DETECT_CONTROL	0x08
#define ASPEED_REG_CLOCK_CONTROL	0x0C
#define ASPEED_REG_COMPENSATION_TRIM	0xC4
/*
 * The register offset between 0xC8~0xCC can be read and won't affect the
 * hardware logic in each version of ADC.
 */
#define ASPEED_REG_MAX			0xD0

#define ASPEED_ADC_ENGINE_ENABLE		BIT(0)
#define ASPEED_ADC_OP_MODE			GENMASK(3, 1)
#define ASPEED_ADC_OP_MODE_PWR_DOWN		0
#define ASPEED_ADC_OP_MODE_STANDBY		1
#define ASPEED_ADC_OP_MODE_NORMAL		7
#define ASPEED_ADC_CTRL_COMPENSATION		BIT(4)
#define ASPEED_ADC_AUTO_COMPENSATION		BIT(5)
/*
 * Bit 6 determines not only the reference voltage range but also the dividing
 * circuit for battery sensing.
 */
#define ASPEED_ADC_REF_VOLTAGE			GENMASK(7, 6)
#define ASPEED_ADC_REF_VOLTAGE_2500mV		0
#define ASPEED_ADC_REF_VOLTAGE_1200mV		1
#define ASPEED_ADC_REF_VOLTAGE_EXT_HIGH		2
#define ASPEED_ADC_REF_VOLTAGE_EXT_LOW		3
#define ASPEED_ADC_BAT_SENSING_DIV		BIT(6)
#define ASPEED_ADC_BAT_SENSING_DIV_2_3		0
#define ASPEED_ADC_BAT_SENSING_DIV_1_3		1
#define ASPEED_ADC_CTRL_INIT_RDY		BIT(8)
#define ASPEED_ADC_CH7_MODE			BIT(12)
#define ASPEED_ADC_CH7_NORMAL			0
#define ASPEED_ADC_CH7_BAT			1
#define ASPEED_ADC_BAT_SENSING_ENABLE		BIT(13)
#define ASPEED_ADC_CTRL_CHANNEL			GENMASK(31, 16)
#define ASPEED_ADC_CTRL_CHANNEL_ENABLE(ch)	FIELD_PREP(ASPEED_ADC_CTRL_CHANNEL, BIT(ch))

#define ASPEED_ADC_INIT_POLLING_TIME	500
#define ASPEED_ADC_INIT_TIMEOUT		500000
/*
 * When the sampling rate is too high, the ADC may not have enough charging
 * time, resulting in a low voltage value. Thus, the default uses a slow
 * sampling rate for most use cases.
 */
#define ASPEED_ADC_DEF_SAMPLING_RATE	65000

struct aspeed_adc_trim_locate {
	const unsigned int offset;
	const unsigned int field;
};

struct aspeed_adc_model_data {
	const char *model_name;
	unsigned int min_sampling_rate;	// Hz
	unsigned int max_sampling_rate;	// Hz
	unsigned int vref_fixed_mv;
	bool wait_init_sequence;
	bool need_prescaler;
	bool bat_sense_sup;
	bool require_extra_eoc;
	u8 scaler_bit_width;
	unsigned int num_channels;
	const struct aspeed_adc_trim_locate *trim_locate;
};

struct adc_gain {
	u8 mult;
	u8 div;
};

struct aspeed_adc_data {
	struct device		*dev;
	const struct aspeed_adc_model_data *model_data;
	struct regulator	*regulator;
	void __iomem		*base;
	spinlock_t		clk_lock;
	struct clk_hw		*fixed_div_clk;
	struct clk_hw		*clk_prescaler;
	struct clk_hw		*clk_scaler;
	struct reset_control	*rst;
	int			vref_mv;
	u32			sample_period_ns;
	int			cv;
	bool			battery_sensing;
	struct adc_gain		battery_mode_gain;
	unsigned int		required_eoc_num;
	u16			*upper_bound;
	u16			*lower_bound;
	bool			*upper_en;
	bool			*lower_en;
};

static const struct iio_event_spec aspeed_adc_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate =
			BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate =
			BIT(IIO_EV_INFO_VALUE) | BIT(IIO_EV_INFO_ENABLE),
	},
};

#define ASPEED_CHAN(_idx, _data_reg_addr) {			\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.address = (_data_reg_addr),				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ) |	\
				BIT(IIO_CHAN_INFO_OFFSET),	\
	.event_spec = aspeed_adc_events,			\
	.num_event_specs = ARRAY_SIZE(aspeed_adc_events),	\
}

static const struct iio_chan_spec aspeed_adc_iio_channels[] = {
	ASPEED_CHAN(0, 0x10),
	ASPEED_CHAN(1, 0x12),
	ASPEED_CHAN(2, 0x14),
	ASPEED_CHAN(3, 0x16),
	ASPEED_CHAN(4, 0x18),
	ASPEED_CHAN(5, 0x1A),
	ASPEED_CHAN(6, 0x1C),
	ASPEED_CHAN(7, 0x1E),
	ASPEED_CHAN(8, 0x20),
	ASPEED_CHAN(9, 0x22),
	ASPEED_CHAN(10, 0x24),
	ASPEED_CHAN(11, 0x26),
	ASPEED_CHAN(12, 0x28),
	ASPEED_CHAN(13, 0x2A),
	ASPEED_CHAN(14, 0x2C),
	ASPEED_CHAN(15, 0x2E),
};

#define ASPEED_BAT_CHAN(_idx, _data_reg_addr) {					\
		.type = IIO_VOLTAGE,						\
		.indexed = 1,							\
		.channel = (_idx),						\
		.address = (_data_reg_addr),					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
				      BIT(IIO_CHAN_INFO_OFFSET),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
					    BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}
static const struct iio_chan_spec aspeed_adc_iio_bat_channels[] = {
	ASPEED_CHAN(0, 0x10),
	ASPEED_CHAN(1, 0x12),
	ASPEED_CHAN(2, 0x14),
	ASPEED_CHAN(3, 0x16),
	ASPEED_CHAN(4, 0x18),
	ASPEED_CHAN(5, 0x1A),
	ASPEED_CHAN(6, 0x1C),
	ASPEED_BAT_CHAN(7, 0x1E),
};

static int aspeed_adc_set_trim_data(struct iio_dev *indio_dev)
{
	struct regmap *scu;
	u32 scu_otp, trimming_val;
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	scu = syscon_regmap_lookup_by_phandle(data->dev->of_node, "aspeed,scu");
	if (IS_ERR(scu)) {
		dev_warn(data->dev, "Failed to get syscon regmap\n");
		return -EOPNOTSUPP;
	}
	if (data->model_data->trim_locate) {
		if (regmap_read(scu, data->model_data->trim_locate->offset,
				&scu_otp)) {
			dev_warn(data->dev,
				 "Failed to get adc trimming data\n");
			trimming_val = 0x8;
		} else {
			trimming_val =
				((scu_otp) &
				 (data->model_data->trim_locate->field)) >>
				__ffs(data->model_data->trim_locate->field);
			if (!trimming_val)
				trimming_val = 0x8;
		}
		dev_dbg(data->dev,
			"trimming val = %d, offset = %08x, fields = %08x\n",
			trimming_val, data->model_data->trim_locate->offset,
			data->model_data->trim_locate->field);
		writel(trimming_val, data->base + ASPEED_REG_COMPENSATION_TRIM);
	}
	return 0;
}

static int aspeed_adc_compensation(struct iio_dev *indio_dev)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);
	u32 index, adc_raw = 0;
	u32 adc_engine_control_reg_val;

	adc_engine_control_reg_val =
		readl(data->base + ASPEED_REG_ENGINE_CONTROL);
	adc_engine_control_reg_val &= ~ASPEED_ADC_OP_MODE;
	adc_engine_control_reg_val |=
		(FIELD_PREP(ASPEED_ADC_OP_MODE, ASPEED_ADC_OP_MODE_NORMAL) |
		 ASPEED_ADC_ENGINE_ENABLE);
	/*
	 * Enable compensating sensing:
	 * After that, the input voltage of ADC will force to half of the reference
	 * voltage. So the expected reading raw data will become half of the max
	 * value. We can get compensating value = 0x200 - ADC read raw value.
	 * It is recommended to average at least 10 samples to get a final CV.
	 */
	writel(adc_engine_control_reg_val | ASPEED_ADC_CTRL_COMPENSATION |
		       ASPEED_ADC_CTRL_CHANNEL_ENABLE(0),
	       data->base + ASPEED_REG_ENGINE_CONTROL);
	/*
	 * After enable compensating sensing mode need to wait some time for ADC stable
	 * Experiment result is 1ms.
	 */
	mdelay(1);

	for (index = 0; index < 16; index++) {
		/*
		 * Waiting for the sampling period ensures that the value acquired
		 * is fresh each time.
		 */
		ndelay(data->sample_period_ns);
		adc_raw += readw(data->base + aspeed_adc_iio_channels[0].address);
	}
	adc_raw >>= 4;
	data->cv = BIT(ASPEED_RESOLUTION_BITS - 1) - adc_raw;
	writel(adc_engine_control_reg_val,
	       data->base + ASPEED_REG_ENGINE_CONTROL);
	dev_dbg(data->dev, "Compensating value = %d\n", data->cv);

	return 0;
}

static int aspeed_adc_set_sampling_rate(struct iio_dev *indio_dev, u32 rate)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	if (rate < data->model_data->min_sampling_rate ||
	    rate > data->model_data->max_sampling_rate)
		return -EINVAL;
	/* Each sampling needs 12 clocks to convert.*/
	clk_set_rate(data->clk_scaler->clk, rate * ASPEED_CLOCKS_PER_SAMPLE);
	rate = clk_get_rate(data->clk_scaler->clk);
	data->sample_period_ns = DIV_ROUND_UP_ULL(
		(u64)NSEC_PER_SEC * ASPEED_CLOCKS_PER_SAMPLE, rate);
	dev_dbg(data->dev, "Adc clock = %d sample period = %d ns", rate,
		data->sample_period_ns);

	return 0;
}

static int aspeed_adc_get_voltage_raw(struct aspeed_adc_data *data, struct iio_chan_spec const *chan)
{
	int val;

	val = readw(data->base + chan->address);
	dev_dbg(data->dev,
		"%d upper_bound: %d %x, lower_bound: %d %x, delay: %d * %d ns",
		chan->channel, data->upper_en[chan->channel],
		data->upper_bound[chan->channel], data->lower_en[chan->channel],
		data->lower_bound[chan->channel], data->sample_period_ns,
		data->required_eoc_num);
	if (data->upper_en[chan->channel]) {
		if (val >= data->upper_bound[chan->channel]) {
			ndelay(data->sample_period_ns *
			       data->required_eoc_num);
			val = readw(data->base + chan->address);
		}
	}
	if (data->lower_en[chan->channel]) {
		if (val <= data->lower_bound[chan->channel]) {
			ndelay(data->sample_period_ns *
			       data->required_eoc_num);
			val = readw(data->base + chan->address);
		}
	}
	return val;
}

static int aspeed_adc_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);
	u32 adc_engine_control_reg_val;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (data->battery_sensing && chan->channel == 7) {
			adc_engine_control_reg_val =
				readl(data->base + ASPEED_REG_ENGINE_CONTROL);
			writel(adc_engine_control_reg_val |
				       FIELD_PREP(ASPEED_ADC_CH7_MODE,
						  ASPEED_ADC_CH7_BAT) |
				       ASPEED_ADC_BAT_SENSING_ENABLE,
			       data->base + ASPEED_REG_ENGINE_CONTROL);
			/*
			 * After enable battery sensing mode need to wait some time for adc stable
			 * Experiment result is 1ms.
			 */
			mdelay(1);
			*val = aspeed_adc_get_voltage_raw(data, chan);
			*val = (*val * data->battery_mode_gain.mult) /
			       data->battery_mode_gain.div;
			/* Restore control register value */
			writel(adc_engine_control_reg_val,
			       data->base + ASPEED_REG_ENGINE_CONTROL);
		} else {
			*val = aspeed_adc_get_voltage_raw(data, chan);
		}
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_OFFSET:
		if (data->battery_sensing && chan->channel == 7)
			*val = (data->cv * data->battery_mode_gain.mult) /
			       data->battery_mode_gain.div;
		else
			*val = data->cv;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = data->vref_mv;
		*val2 = ASPEED_RESOLUTION_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = clk_get_rate(data->clk_scaler->clk) /
				ASPEED_CLOCKS_PER_SAMPLE;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int aspeed_adc_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int val, int val2, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return aspeed_adc_set_sampling_rate(indio_dev, val);

	case IIO_CHAN_INFO_SCALE:
	case IIO_CHAN_INFO_RAW:
		/*
		 * Technically, these could be written but the only reasons
		 * for doing so seem better handled in userspace.  EPERM is
		 * returned to signal this is a policy choice rather than a
		 * hardware limitation.
		 */
		return -EPERM;

	default:
		return -EINVAL;
	}
}

static int aspeed_adc_reg_access(struct iio_dev *indio_dev,
				 unsigned int reg, unsigned int writeval,
				 unsigned int *readval)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	if (!readval || reg % 4 || reg > ASPEED_REG_MAX)
		return -EINVAL;

	*readval = readl(data->base + reg);

	return 0;
}

static int aspeed_adc_read_event_config(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		return data->upper_en[chan->channel];
	case IIO_EV_DIR_FALLING:
		return data->lower_en[chan->channel];
	default:
		return -EINVAL;
	}
}

static int aspeed_adc_write_event_config(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir,
					 int state)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	switch (dir) {
	case IIO_EV_DIR_RISING:
		data->upper_en[chan->channel] = state ? 1 : 0;
		break;
	case IIO_EV_DIR_FALLING:
		data->lower_en[chan->channel] = state ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int aspeed_adc_write_event_value(struct iio_dev *indio_dev,
					const struct iio_chan_spec *chan,
					enum iio_event_type type,
					enum iio_event_direction dir,
					enum iio_event_info info, int val,
					int val2)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		if (val >= BIT(ASPEED_RESOLUTION_BITS))
			return -EINVAL;
		data->upper_bound[chan->channel] = val;
		break;
	case IIO_EV_DIR_FALLING:
		data->lower_bound[chan->channel] = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int aspeed_adc_read_event_value(struct iio_dev *indio_dev,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       enum iio_event_info info, int *val,
				       int *val2)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);

	if (info != IIO_EV_INFO_VALUE)
		return -EINVAL;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		*val = data->upper_bound[chan->channel];
		break;
	case IIO_EV_DIR_FALLING:
		*val = data->lower_bound[chan->channel];
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static const struct iio_info aspeed_adc_iio_info = {
	.read_raw = aspeed_adc_read_raw,
	.write_raw = aspeed_adc_write_raw,
	.read_event_config = &aspeed_adc_read_event_config,
	.write_event_config = &aspeed_adc_write_event_config,
	.read_event_value = &aspeed_adc_read_event_value,
	.write_event_value = &aspeed_adc_write_event_value,
	.debugfs_reg_access = aspeed_adc_reg_access,
};

static void aspeed_adc_unregister_fixed_divider(void *data)
{
	struct clk_hw *clk = data;

	clk_hw_unregister_fixed_factor(clk);
}

static void aspeed_adc_reset_assert(void *data)
{
	struct reset_control *rst = data;

	reset_control_assert(rst);
}

static void aspeed_adc_clk_disable_unprepare(void *data)
{
	struct clk *clk = data;

	clk_disable_unprepare(clk);
}

static void aspeed_adc_power_down(void *data)
{
	struct aspeed_adc_data *priv_data = data;

	writel(FIELD_PREP(ASPEED_ADC_OP_MODE, ASPEED_ADC_OP_MODE_PWR_DOWN),
	       priv_data->base + ASPEED_REG_ENGINE_CONTROL);
}

static void aspeed_adc_reg_disable(void *data)
{
	struct regulator *reg = data;

	regulator_disable(reg);
}

static int aspeed_adc_vref_config(struct iio_dev *indio_dev)
{
	struct aspeed_adc_data *data = iio_priv(indio_dev);
	int ret;
	u32 adc_engine_control_reg_val;

	if (data->model_data->vref_fixed_mv) {
		data->vref_mv = data->model_data->vref_fixed_mv;
		return 0;
	}
	adc_engine_control_reg_val =
		readl(data->base + ASPEED_REG_ENGINE_CONTROL);
	data->regulator = devm_regulator_get_optional(data->dev, "vref");
	if (!IS_ERR(data->regulator)) {
		ret = regulator_enable(data->regulator);
		if (ret)
			return ret;
		ret = devm_add_action_or_reset(
			data->dev, aspeed_adc_reg_disable, data->regulator);
		if (ret)
			return ret;
		data->vref_mv = regulator_get_voltage(data->regulator);
		/* Conversion from uV to mV */
		data->vref_mv /= 1000;
		if ((data->vref_mv >= 1550) && (data->vref_mv <= 2700))
			writel(adc_engine_control_reg_val |
				FIELD_PREP(
					ASPEED_ADC_REF_VOLTAGE,
					ASPEED_ADC_REF_VOLTAGE_EXT_HIGH),
			data->base + ASPEED_REG_ENGINE_CONTROL);
		else if ((data->vref_mv >= 900) && (data->vref_mv <= 1650))
			writel(adc_engine_control_reg_val |
				FIELD_PREP(
					ASPEED_ADC_REF_VOLTAGE,
					ASPEED_ADC_REF_VOLTAGE_EXT_LOW),
			data->base + ASPEED_REG_ENGINE_CONTROL);
		else {
			dev_err(data->dev, "Regulator voltage %d not support",
				data->vref_mv);
			return -EOPNOTSUPP;
		}
	} else {
		if (PTR_ERR(data->regulator) != -ENODEV)
			return PTR_ERR(data->regulator);
		data->vref_mv = 2500000;
		of_property_read_u32(data->dev->of_node,
				     "aspeed,int-vref-microvolt",
				     &data->vref_mv);
		/* Conversion from uV to mV */
		data->vref_mv /= 1000;
		if (data->vref_mv == 2500)
			writel(adc_engine_control_reg_val |
				FIELD_PREP(ASPEED_ADC_REF_VOLTAGE,
						ASPEED_ADC_REF_VOLTAGE_2500mV),
			data->base + ASPEED_REG_ENGINE_CONTROL);
		else if (data->vref_mv == 1200)
			writel(adc_engine_control_reg_val |
				FIELD_PREP(ASPEED_ADC_REF_VOLTAGE,
						ASPEED_ADC_REF_VOLTAGE_1200mV),
			data->base + ASPEED_REG_ENGINE_CONTROL);
		else {
			dev_err(data->dev, "Voltage %d not support", data->vref_mv);
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int aspeed_adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct aspeed_adc_data *data;
	int ret;
	u32 adc_engine_control_reg_val;
	unsigned long scaler_flags = 0;
	char clk_name[32], clk_parent_name[32];

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = &pdev->dev;
	data->model_data = of_device_get_match_data(&pdev->dev);
	platform_set_drvdata(pdev, indio_dev);

	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->upper_bound = devm_kzalloc(&pdev->dev,
					 sizeof(data->upper_bound) *
						 data->model_data->num_channels,
					 GFP_KERNEL);
	if (!data->upper_bound)
		return -ENOMEM;
	data->upper_en = devm_kzalloc(&pdev->dev,
				      sizeof(data->upper_en) *
					      data->model_data->num_channels,
				      GFP_KERNEL);
	if (!data->upper_en)
		return -ENOMEM;
	data->lower_bound = devm_kzalloc(&pdev->dev,
					 sizeof(data->lower_bound) *
						 data->model_data->num_channels,
					 GFP_KERNEL);
	if (!data->lower_bound)
		return -ENOMEM;
	data->lower_en = devm_kzalloc(&pdev->dev,
				      sizeof(data->lower_en) *
					      data->model_data->num_channels,
				      GFP_KERNEL);
	if (!data->lower_en)
		return -ENOMEM;
	/* Register ADC clock prescaler with source specified by device tree. */
	spin_lock_init(&data->clk_lock);
	snprintf(clk_parent_name, ARRAY_SIZE(clk_parent_name), "%s",
		 of_clk_get_parent_name(pdev->dev.of_node, 0));
	snprintf(clk_name, ARRAY_SIZE(clk_name), "%s-fixed-div",
		 data->model_data->model_name);
	data->fixed_div_clk = clk_hw_register_fixed_factor(
		&pdev->dev, clk_name, clk_parent_name, 0, 1, 2);
	if (IS_ERR(data->fixed_div_clk))
		return PTR_ERR(data->fixed_div_clk);

	ret = devm_add_action_or_reset(data->dev,
				       aspeed_adc_unregister_fixed_divider,
				       data->fixed_div_clk);
	if (ret)
		return ret;
	snprintf(clk_parent_name, ARRAY_SIZE(clk_parent_name), clk_name);

	if (data->model_data->need_prescaler) {
		snprintf(clk_name, ARRAY_SIZE(clk_name), "%s-prescaler",
			 data->model_data->model_name);
		data->clk_prescaler = devm_clk_hw_register_divider(
			&pdev->dev, clk_name, clk_parent_name, 0,
			data->base + ASPEED_REG_CLOCK_CONTROL, 17, 15, 0,
			&data->clk_lock);
		if (IS_ERR(data->clk_prescaler))
			return PTR_ERR(data->clk_prescaler);
		snprintf(clk_parent_name, ARRAY_SIZE(clk_parent_name),
			 clk_name);
		scaler_flags = CLK_SET_RATE_PARENT;
	}
	/*
	 * Register ADC clock scaler downstream from the prescaler. Allow rate
	 * setting to adjust the prescaler as well.
	 */
	snprintf(clk_name, ARRAY_SIZE(clk_name), "%s-scaler",
		 data->model_data->model_name);
	data->clk_scaler = devm_clk_hw_register_divider(
		&pdev->dev, clk_name, clk_parent_name, scaler_flags,
		data->base + ASPEED_REG_CLOCK_CONTROL, 0,
		data->model_data->scaler_bit_width,
		data->model_data->need_prescaler ? CLK_DIVIDER_ONE_BASED : 0,
		&data->clk_lock);
	if (IS_ERR(data->clk_scaler))
		return PTR_ERR(data->clk_scaler);

	data->rst = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(data->rst)) {
		dev_err(&pdev->dev,
			"invalid or missing reset controller device tree entry");
		return PTR_ERR(data->rst);
	}
	reset_control_deassert(data->rst);

	ret = devm_add_action_or_reset(data->dev, aspeed_adc_reset_assert,
				       data->rst);
	if (ret)
		return ret;

	ret = aspeed_adc_vref_config(indio_dev);
	if (ret)
		return ret;

	ret = aspeed_adc_set_trim_data(indio_dev);
	if (ret)
		return ret;

	if (of_find_property(data->dev->of_node, "aspeed,battery-sensing",
			     NULL)) {
		if (data->model_data->bat_sense_sup) {
			data->battery_sensing = 1;
			if (readl(data->base + ASPEED_REG_ENGINE_CONTROL) &
			    ASPEED_ADC_BAT_SENSING_DIV) {
				data->battery_mode_gain.mult = 3;
				data->battery_mode_gain.div = 1;
			} else {
				data->battery_mode_gain.mult = 3;
				data->battery_mode_gain.div = 2;
			}
		} else
			dev_warn(&pdev->dev,
				 "Failed to enable battery-sensing mode\n");
	}

	ret = clk_prepare_enable(data->clk_scaler->clk);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(data->dev,
				       aspeed_adc_clk_disable_unprepare,
				       data->clk_scaler->clk);
	if (ret)
		return ret;
	ret = aspeed_adc_set_sampling_rate(indio_dev,
					   ASPEED_ADC_DEF_SAMPLING_RATE);
	if (ret)
		return ret;

	adc_engine_control_reg_val =
		readl(data->base + ASPEED_REG_ENGINE_CONTROL);
	adc_engine_control_reg_val |=
		FIELD_PREP(ASPEED_ADC_OP_MODE, ASPEED_ADC_OP_MODE_NORMAL) |
		ASPEED_ADC_ENGINE_ENABLE;
	/* Enable engine in normal mode. */
	writel(adc_engine_control_reg_val,
	       data->base + ASPEED_REG_ENGINE_CONTROL);

	ret = devm_add_action_or_reset(data->dev, aspeed_adc_power_down,
					data);
	if (ret)
		return ret;

	if (data->model_data->wait_init_sequence) {
		/* Wait for initial sequence complete. */
		ret = readl_poll_timeout(data->base + ASPEED_REG_ENGINE_CONTROL,
					 adc_engine_control_reg_val,
					 adc_engine_control_reg_val &
					 ASPEED_ADC_CTRL_INIT_RDY,
					 ASPEED_ADC_INIT_POLLING_TIME,
					 ASPEED_ADC_INIT_TIMEOUT);
		if (ret)
			return ret;
	}

	aspeed_adc_compensation(indio_dev);
	/* Start all channels in normal mode. */
	adc_engine_control_reg_val =
		readl(data->base + ASPEED_REG_ENGINE_CONTROL);
	adc_engine_control_reg_val |= ASPEED_ADC_CTRL_CHANNEL;
	writel(adc_engine_control_reg_val,
	       data->base + ASPEED_REG_ENGINE_CONTROL);
	adc_engine_control_reg_val =
		FIELD_GET(ASPEED_ADC_CTRL_CHANNEL,
			  readl(data->base + ASPEED_REG_ENGINE_CONTROL));
	data->required_eoc_num = hweight_long(adc_engine_control_reg_val);
	if (data->model_data->require_extra_eoc &&
	    (adc_engine_control_reg_val &
	     BIT(data->model_data->num_channels - 1)))
		data->required_eoc_num += 12;
	indio_dev->name = data->model_data->model_name;
	indio_dev->info = &aspeed_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = data->battery_sensing ?
					    aspeed_adc_iio_bat_channels :
					    aspeed_adc_iio_channels;
	indio_dev->num_channels = data->model_data->num_channels;

	ret = devm_iio_device_register(data->dev, indio_dev);
	return ret;
}

static const struct aspeed_adc_trim_locate ast2500_adc_trim = {
	.offset = 0x154,
	.field = GENMASK(31, 28),
};

static const struct aspeed_adc_trim_locate ast2600_adc0_trim = {
	.offset = 0x5d0,
	.field = GENMASK(3, 0),
};

static const struct aspeed_adc_trim_locate ast2600_adc1_trim = {
	.offset = 0x5d0,
	.field = GENMASK(7, 4),
};

static const struct aspeed_adc_trim_locate ast2700_adc0_trim = {
	.offset = 0x828,
	.field = GENMASK(3, 0),
};

static const struct aspeed_adc_trim_locate ast2700_adc1_trim = {
	.offset = 0x828,
	.field = GENMASK(7, 4),
};

static const struct aspeed_adc_model_data ast2400_model_data = {
	.model_name = "ast2400-adc",
	.vref_fixed_mv = 2500,
	.min_sampling_rate = 10000,
	.max_sampling_rate = 500000,
	.need_prescaler = true,
	.scaler_bit_width = 10,
	.num_channels = 16,
	.require_extra_eoc = 0,
};

static const struct aspeed_adc_model_data ast2500_model_data = {
	.model_name = "ast2500-adc",
	.vref_fixed_mv = 1800,
	.min_sampling_rate = 1,
	.max_sampling_rate = 1000000,
	.wait_init_sequence = true,
	.need_prescaler = true,
	.scaler_bit_width = 10,
	.num_channels = 16,
	.trim_locate = &ast2500_adc_trim,
	.require_extra_eoc = 0,
};

static const struct aspeed_adc_model_data ast2600_adc0_model_data = {
	.model_name = "ast2600-adc0",
	.min_sampling_rate = 10000,
	.max_sampling_rate = 500000,
	.wait_init_sequence = true,
	.bat_sense_sup = true,
	.scaler_bit_width = 16,
	.num_channels = 8,
	.trim_locate = &ast2600_adc0_trim,
	.require_extra_eoc = 1,
};

static const struct aspeed_adc_model_data ast2600_adc1_model_data = {
	.model_name = "ast2600-adc1",
	.min_sampling_rate = 10000,
	.max_sampling_rate = 500000,
	.wait_init_sequence = true,
	.bat_sense_sup = true,
	.scaler_bit_width = 16,
	.num_channels = 8,
	.trim_locate = &ast2600_adc1_trim,
	.require_extra_eoc = 1,
};

static const struct aspeed_adc_model_data ast2700_adc0_model_data = {
	.model_name = "ast2700-adc0",
	.min_sampling_rate = 10000,
	.max_sampling_rate = 500000,
	.wait_init_sequence = true,
	.bat_sense_sup = true,
	.scaler_bit_width = 16,
	.num_channels = 8,
	.trim_locate = &ast2700_adc0_trim,
};

static const struct aspeed_adc_model_data ast2700_adc1_model_data = {
	.model_name = "ast2700-adc1",
	.min_sampling_rate = 10000,
	.max_sampling_rate = 500000,
	.wait_init_sequence = true,
	.bat_sense_sup = true,
	.scaler_bit_width = 16,
	.num_channels = 8,
	.trim_locate = &ast2700_adc1_trim,
};

static const struct of_device_id aspeed_adc_matches[] = {
	{ .compatible = "aspeed,ast2400-adc", .data = &ast2400_model_data },
	{ .compatible = "aspeed,ast2500-adc", .data = &ast2500_model_data },
	{ .compatible = "aspeed,ast2600-adc0", .data = &ast2600_adc0_model_data },
	{ .compatible = "aspeed,ast2600-adc1", .data = &ast2600_adc1_model_data },
	{ .compatible = "aspeed,ast2700-adc0", .data = &ast2700_adc0_model_data },
	{ .compatible = "aspeed,ast2700-adc1", .data = &ast2700_adc1_model_data },
	{},
};
MODULE_DEVICE_TABLE(of, aspeed_adc_matches);

static struct platform_driver aspeed_adc_driver = {
	.probe = aspeed_adc_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_adc_matches,
	}
};

module_platform_driver(aspeed_adc_driver);

MODULE_AUTHOR("Rick Altherr <raltherr@google.com>");
MODULE_DESCRIPTION("Aspeed AST2400/2500/2600 ADC Driver");
MODULE_LICENSE("GPL");
