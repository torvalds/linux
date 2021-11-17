// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2016
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 *
 */

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/timer/stm32-timer-trigger.h>
#include <linux/iio/trigger.h>
#include <linux/mfd/stm32-timers.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#define MAX_TRIGGERS 7
#define MAX_VALIDS 5

/* List the triggers created by each timer */
static const void *triggers_table[][MAX_TRIGGERS] = {
	{ TIM1_TRGO, TIM1_TRGO2, TIM1_CH1, TIM1_CH2, TIM1_CH3, TIM1_CH4,},
	{ TIM2_TRGO, TIM2_CH1, TIM2_CH2, TIM2_CH3, TIM2_CH4,},
	{ TIM3_TRGO, TIM3_CH1, TIM3_CH2, TIM3_CH3, TIM3_CH4,},
	{ TIM4_TRGO, TIM4_CH1, TIM4_CH2, TIM4_CH3, TIM4_CH4,},
	{ TIM5_TRGO, TIM5_CH1, TIM5_CH2, TIM5_CH3, TIM5_CH4,},
	{ TIM6_TRGO,},
	{ TIM7_TRGO,},
	{ TIM8_TRGO, TIM8_TRGO2, TIM8_CH1, TIM8_CH2, TIM8_CH3, TIM8_CH4,},
	{ TIM9_TRGO, TIM9_CH1, TIM9_CH2,},
	{ TIM10_OC1,},
	{ TIM11_OC1,},
	{ TIM12_TRGO, TIM12_CH1, TIM12_CH2,},
	{ TIM13_OC1,},
	{ TIM14_OC1,},
	{ TIM15_TRGO,},
	{ TIM16_OC1,},
	{ TIM17_OC1,},
};

/* List the triggers accepted by each timer */
static const void *valids_table[][MAX_VALIDS] = {
	{ TIM5_TRGO, TIM2_TRGO, TIM3_TRGO, TIM4_TRGO,},
	{ TIM1_TRGO, TIM8_TRGO, TIM3_TRGO, TIM4_TRGO,},
	{ TIM1_TRGO, TIM2_TRGO, TIM5_TRGO, TIM4_TRGO,},
	{ TIM1_TRGO, TIM2_TRGO, TIM3_TRGO, TIM8_TRGO,},
	{ TIM2_TRGO, TIM3_TRGO, TIM4_TRGO, TIM8_TRGO,},
	{ }, /* timer 6 */
	{ }, /* timer 7 */
	{ TIM1_TRGO, TIM2_TRGO, TIM4_TRGO, TIM5_TRGO,},
	{ TIM2_TRGO, TIM3_TRGO, TIM10_OC1, TIM11_OC1,},
	{ }, /* timer 10 */
	{ }, /* timer 11 */
	{ TIM4_TRGO, TIM5_TRGO, TIM13_OC1, TIM14_OC1,},
};

static const void *stm32h7_valids_table[][MAX_VALIDS] = {
	{ TIM15_TRGO, TIM2_TRGO, TIM3_TRGO, TIM4_TRGO,},
	{ TIM1_TRGO, TIM8_TRGO, TIM3_TRGO, TIM4_TRGO,},
	{ TIM1_TRGO, TIM2_TRGO, TIM15_TRGO, TIM4_TRGO,},
	{ TIM1_TRGO, TIM2_TRGO, TIM3_TRGO, TIM8_TRGO,},
	{ TIM1_TRGO, TIM8_TRGO, TIM3_TRGO, TIM4_TRGO,},
	{ }, /* timer 6 */
	{ }, /* timer 7 */
	{ TIM1_TRGO, TIM2_TRGO, TIM4_TRGO, TIM5_TRGO,},
	{ }, /* timer 9 */
	{ }, /* timer 10 */
	{ }, /* timer 11 */
	{ TIM4_TRGO, TIM5_TRGO, TIM13_OC1, TIM14_OC1,},
	{ }, /* timer 13 */
	{ }, /* timer 14 */
	{ TIM1_TRGO, TIM3_TRGO, TIM16_OC1, TIM17_OC1,},
	{ }, /* timer 16 */
	{ }, /* timer 17 */
};

struct stm32_timer_trigger_regs {
	u32 cr1;
	u32 cr2;
	u32 psc;
	u32 arr;
	u32 cnt;
	u32 smcr;
};

struct stm32_timer_trigger {
	struct device *dev;
	struct regmap *regmap;
	struct clk *clk;
	bool enabled;
	u32 max_arr;
	const void *triggers;
	const void *valids;
	bool has_trgo2;
	struct mutex lock; /* concurrent sysfs configuration */
	struct list_head tr_list;
	struct stm32_timer_trigger_regs bak;
};

struct stm32_timer_trigger_cfg {
	const void *(*valids_table)[MAX_VALIDS];
	const unsigned int num_valids_table;
};

static bool stm32_timer_is_trgo2_name(const char *name)
{
	return !!strstr(name, "trgo2");
}

static bool stm32_timer_is_trgo_name(const char *name)
{
	return (!!strstr(name, "trgo") && !strstr(name, "trgo2"));
}

static int stm32_timer_start(struct stm32_timer_trigger *priv,
			     struct iio_trigger *trig,
			     unsigned int frequency)
{
	unsigned long long prd, div;
	int prescaler = 0;
	u32 ccer;

	/* Period and prescaler values depends of clock rate */
	div = (unsigned long long)clk_get_rate(priv->clk);

	do_div(div, frequency);

	prd = div;

	/*
	 * Increase prescaler value until we get a result that fit
	 * with auto reload register maximum value.
	 */
	while (div > priv->max_arr) {
		prescaler++;
		div = prd;
		do_div(div, (prescaler + 1));
	}
	prd = div;

	if (prescaler > MAX_TIM_PSC) {
		dev_err(priv->dev, "prescaler exceeds the maximum value\n");
		return -EINVAL;
	}

	/* Check if nobody else use the timer */
	regmap_read(priv->regmap, TIM_CCER, &ccer);
	if (ccer & TIM_CCER_CCXE)
		return -EBUSY;

	mutex_lock(&priv->lock);
	if (!priv->enabled) {
		priv->enabled = true;
		clk_enable(priv->clk);
	}

	regmap_write(priv->regmap, TIM_PSC, prescaler);
	regmap_write(priv->regmap, TIM_ARR, prd - 1);
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, TIM_CR1_ARPE);

	/* Force master mode to update mode */
	if (stm32_timer_is_trgo2_name(trig->name))
		regmap_update_bits(priv->regmap, TIM_CR2, TIM_CR2_MMS2,
				   0x2 << TIM_CR2_MMS2_SHIFT);
	else
		regmap_update_bits(priv->regmap, TIM_CR2, TIM_CR2_MMS,
				   0x2 << TIM_CR2_MMS_SHIFT);

	/* Make sure that registers are updated */
	regmap_update_bits(priv->regmap, TIM_EGR, TIM_EGR_UG, TIM_EGR_UG);

	/* Enable controller */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, TIM_CR1_CEN);
	mutex_unlock(&priv->lock);

	return 0;
}

static void stm32_timer_stop(struct stm32_timer_trigger *priv,
			     struct iio_trigger *trig)
{
	u32 ccer;

	regmap_read(priv->regmap, TIM_CCER, &ccer);
	if (ccer & TIM_CCER_CCXE)
		return;

	mutex_lock(&priv->lock);
	/* Stop timer */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, 0);
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);
	regmap_write(priv->regmap, TIM_PSC, 0);
	regmap_write(priv->regmap, TIM_ARR, 0);

	/* Force disable master mode */
	if (stm32_timer_is_trgo2_name(trig->name))
		regmap_update_bits(priv->regmap, TIM_CR2, TIM_CR2_MMS2, 0);
	else
		regmap_update_bits(priv->regmap, TIM_CR2, TIM_CR2_MMS, 0);

	/* Make sure that registers are updated */
	regmap_update_bits(priv->regmap, TIM_EGR, TIM_EGR_UG, TIM_EGR_UG);

	if (priv->enabled) {
		priv->enabled = false;
		clk_disable(priv->clk);
	}
	mutex_unlock(&priv->lock);
}

static ssize_t stm32_tt_store_frequency(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct iio_trigger *trig = to_iio_trigger(dev);
	struct stm32_timer_trigger *priv = iio_trigger_get_drvdata(trig);
	unsigned int freq;
	int ret;

	ret = kstrtouint(buf, 10, &freq);
	if (ret)
		return ret;

	if (freq == 0) {
		stm32_timer_stop(priv, trig);
	} else {
		ret = stm32_timer_start(priv, trig, freq);
		if (ret)
			return ret;
	}

	return len;
}

static ssize_t stm32_tt_read_frequency(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct iio_trigger *trig = to_iio_trigger(dev);
	struct stm32_timer_trigger *priv = iio_trigger_get_drvdata(trig);
	u32 psc, arr, cr1;
	unsigned long long freq = 0;

	regmap_read(priv->regmap, TIM_CR1, &cr1);
	regmap_read(priv->regmap, TIM_PSC, &psc);
	regmap_read(priv->regmap, TIM_ARR, &arr);

	if (cr1 & TIM_CR1_CEN) {
		freq = (unsigned long long)clk_get_rate(priv->clk);
		do_div(freq, psc + 1);
		do_div(freq, arr + 1);
	}

	return sprintf(buf, "%d\n", (unsigned int)freq);
}

static IIO_DEV_ATTR_SAMP_FREQ(0660,
			      stm32_tt_read_frequency,
			      stm32_tt_store_frequency);

#define MASTER_MODE_MAX		7
#define MASTER_MODE2_MAX	15

static char *master_mode_table[] = {
	"reset",
	"enable",
	"update",
	"compare_pulse",
	"OC1REF",
	"OC2REF",
	"OC3REF",
	"OC4REF",
	/* Master mode selection 2 only */
	"OC5REF",
	"OC6REF",
	"compare_pulse_OC4REF",
	"compare_pulse_OC6REF",
	"compare_pulse_OC4REF_r_or_OC6REF_r",
	"compare_pulse_OC4REF_r_or_OC6REF_f",
	"compare_pulse_OC5REF_r_or_OC6REF_r",
	"compare_pulse_OC5REF_r_or_OC6REF_f",
};

static ssize_t stm32_tt_show_master_mode(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct stm32_timer_trigger *priv = dev_get_drvdata(dev);
	struct iio_trigger *trig = to_iio_trigger(dev);
	u32 cr2;

	regmap_read(priv->regmap, TIM_CR2, &cr2);

	if (stm32_timer_is_trgo2_name(trig->name))
		cr2 = (cr2 & TIM_CR2_MMS2) >> TIM_CR2_MMS2_SHIFT;
	else
		cr2 = (cr2 & TIM_CR2_MMS) >> TIM_CR2_MMS_SHIFT;

	return sysfs_emit(buf, "%s\n", master_mode_table[cr2]);
}

static ssize_t stm32_tt_store_master_mode(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	struct stm32_timer_trigger *priv = dev_get_drvdata(dev);
	struct iio_trigger *trig = to_iio_trigger(dev);
	u32 mask, shift, master_mode_max;
	int i;

	if (stm32_timer_is_trgo2_name(trig->name)) {
		mask = TIM_CR2_MMS2;
		shift = TIM_CR2_MMS2_SHIFT;
		master_mode_max = MASTER_MODE2_MAX;
	} else {
		mask = TIM_CR2_MMS;
		shift = TIM_CR2_MMS_SHIFT;
		master_mode_max = MASTER_MODE_MAX;
	}

	for (i = 0; i <= master_mode_max; i++) {
		if (!strncmp(master_mode_table[i], buf,
			     strlen(master_mode_table[i]))) {
			mutex_lock(&priv->lock);
			if (!priv->enabled) {
				/* Clock should be enabled first */
				priv->enabled = true;
				clk_enable(priv->clk);
			}
			regmap_update_bits(priv->regmap, TIM_CR2, mask,
					   i << shift);
			mutex_unlock(&priv->lock);
			return len;
		}
	}

	return -EINVAL;
}

static ssize_t stm32_tt_show_master_mode_avail(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct iio_trigger *trig = to_iio_trigger(dev);
	unsigned int i, master_mode_max;
	size_t len = 0;

	if (stm32_timer_is_trgo2_name(trig->name))
		master_mode_max = MASTER_MODE2_MAX;
	else
		master_mode_max = MASTER_MODE_MAX;

	for (i = 0; i <= master_mode_max; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"%s ", master_mode_table[i]);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEVICE_ATTR(master_mode_available, 0444,
		       stm32_tt_show_master_mode_avail, NULL, 0);

static IIO_DEVICE_ATTR(master_mode, 0660,
		       stm32_tt_show_master_mode,
		       stm32_tt_store_master_mode,
		       0);

static struct attribute *stm32_trigger_attrs[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_master_mode.dev_attr.attr,
	&iio_dev_attr_master_mode_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group stm32_trigger_attr_group = {
	.attrs = stm32_trigger_attrs,
};

static const struct attribute_group *stm32_trigger_attr_groups[] = {
	&stm32_trigger_attr_group,
	NULL,
};

static const struct iio_trigger_ops timer_trigger_ops = {
};

static void stm32_unregister_iio_triggers(struct stm32_timer_trigger *priv)
{
	struct iio_trigger *tr;

	list_for_each_entry(tr, &priv->tr_list, alloc_list)
		iio_trigger_unregister(tr);
}

static int stm32_register_iio_triggers(struct stm32_timer_trigger *priv)
{
	int ret;
	const char * const *cur = priv->triggers;

	INIT_LIST_HEAD(&priv->tr_list);

	while (cur && *cur) {
		struct iio_trigger *trig;
		bool cur_is_trgo = stm32_timer_is_trgo_name(*cur);
		bool cur_is_trgo2 = stm32_timer_is_trgo2_name(*cur);

		if (cur_is_trgo2 && !priv->has_trgo2) {
			cur++;
			continue;
		}

		trig = devm_iio_trigger_alloc(priv->dev, "%s", *cur);
		if  (!trig)
			return -ENOMEM;

		trig->dev.parent = priv->dev->parent;
		trig->ops = &timer_trigger_ops;

		/*
		 * sampling frequency and master mode attributes
		 * should only be available on trgo/trgo2 triggers
		 */
		if (cur_is_trgo || cur_is_trgo2)
			trig->dev.groups = stm32_trigger_attr_groups;

		iio_trigger_set_drvdata(trig, priv);

		ret = iio_trigger_register(trig);
		if (ret) {
			stm32_unregister_iio_triggers(priv);
			return ret;
		}

		list_add_tail(&trig->alloc_list, &priv->tr_list);
		cur++;
	}

	return 0;
}

static int stm32_counter_read_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int *val, int *val2, long mask)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	u32 dat;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		regmap_read(priv->regmap, TIM_CNT, &dat);
		*val = dat;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_ENABLE:
		regmap_read(priv->regmap, TIM_CR1, &dat);
		*val = (dat & TIM_CR1_CEN) ? 1 : 0;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		regmap_read(priv->regmap, TIM_SMCR, &dat);
		dat &= TIM_SMCR_SMS;

		*val = 1;
		*val2 = 0;

		/* in quadrature case scale = 0.25 */
		if (dat == 3)
			*val2 = 2;

		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int stm32_counter_write_raw(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   int val, int val2, long mask)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return regmap_write(priv->regmap, TIM_CNT, val);

	case IIO_CHAN_INFO_SCALE:
		/* fixed scale */
		return -EINVAL;

	case IIO_CHAN_INFO_ENABLE:
		mutex_lock(&priv->lock);
		if (val) {
			if (!priv->enabled) {
				priv->enabled = true;
				clk_enable(priv->clk);
			}
			regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN,
					   TIM_CR1_CEN);
		} else {
			regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN,
					   0);
			if (priv->enabled) {
				priv->enabled = false;
				clk_disable(priv->clk);
			}
		}
		mutex_unlock(&priv->lock);
		return 0;
	}

	return -EINVAL;
}

static int stm32_counter_validate_trigger(struct iio_dev *indio_dev,
					  struct iio_trigger *trig)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	const char * const *cur = priv->valids;
	unsigned int i = 0;

	if (!is_stm32_timer_trigger(trig))
		return -EINVAL;

	while (cur && *cur) {
		if (!strncmp(trig->name, *cur, strlen(trig->name))) {
			regmap_update_bits(priv->regmap,
					   TIM_SMCR, TIM_SMCR_TS,
					   i << TIM_SMCR_TS_SHIFT);
			return 0;
		}
		cur++;
		i++;
	}

	return -EINVAL;
}

static const struct iio_info stm32_trigger_info = {
	.validate_trigger = stm32_counter_validate_trigger,
	.read_raw = stm32_counter_read_raw,
	.write_raw = stm32_counter_write_raw
};

static const char *const stm32_trigger_modes[] = {
	"trigger",
};

static int stm32_set_trigger_mode(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  unsigned int mode)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);

	regmap_update_bits(priv->regmap, TIM_SMCR, TIM_SMCR_SMS, TIM_SMCR_SMS);

	return 0;
}

static int stm32_get_trigger_mode(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	u32 smcr;

	regmap_read(priv->regmap, TIM_SMCR, &smcr);

	return (smcr & TIM_SMCR_SMS) == TIM_SMCR_SMS ? 0 : -EINVAL;
}

static const struct iio_enum stm32_trigger_mode_enum = {
	.items = stm32_trigger_modes,
	.num_items = ARRAY_SIZE(stm32_trigger_modes),
	.set = stm32_set_trigger_mode,
	.get = stm32_get_trigger_mode
};

static const char *const stm32_enable_modes[] = {
	"always",
	"gated",
	"triggered",
};

static int stm32_enable_mode2sms(int mode)
{
	switch (mode) {
	case 0:
		return 0;
	case 1:
		return 5;
	case 2:
		return 6;
	}

	return -EINVAL;
}

static int stm32_set_enable_mode(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 unsigned int mode)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	int sms = stm32_enable_mode2sms(mode);

	if (sms < 0)
		return sms;
	/*
	 * Triggered mode sets CEN bit automatically by hardware. So, first
	 * enable counter clock, so it can use it. Keeps it in sync with CEN.
	 */
	mutex_lock(&priv->lock);
	if (sms == 6 && !priv->enabled) {
		clk_enable(priv->clk);
		priv->enabled = true;
	}
	mutex_unlock(&priv->lock);

	regmap_update_bits(priv->regmap, TIM_SMCR, TIM_SMCR_SMS, sms);

	return 0;
}

static int stm32_sms2enable_mode(int mode)
{
	switch (mode) {
	case 0:
		return 0;
	case 5:
		return 1;
	case 6:
		return 2;
	}

	return -EINVAL;
}

static int stm32_get_enable_mode(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	u32 smcr;

	regmap_read(priv->regmap, TIM_SMCR, &smcr);
	smcr &= TIM_SMCR_SMS;

	return stm32_sms2enable_mode(smcr);
}

static const struct iio_enum stm32_enable_mode_enum = {
	.items = stm32_enable_modes,
	.num_items = ARRAY_SIZE(stm32_enable_modes),
	.set = stm32_set_enable_mode,
	.get = stm32_get_enable_mode
};

static ssize_t stm32_count_get_preset(struct iio_dev *indio_dev,
				      uintptr_t private,
				      const struct iio_chan_spec *chan,
				      char *buf)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	u32 arr;

	regmap_read(priv->regmap, TIM_ARR, &arr);

	return snprintf(buf, PAGE_SIZE, "%u\n", arr);
}

static ssize_t stm32_count_set_preset(struct iio_dev *indio_dev,
				      uintptr_t private,
				      const struct iio_chan_spec *chan,
				      const char *buf, size_t len)
{
	struct stm32_timer_trigger *priv = iio_priv(indio_dev);
	unsigned int preset;
	int ret;

	ret = kstrtouint(buf, 0, &preset);
	if (ret)
		return ret;

	/* TIMx_ARR register shouldn't be buffered (ARPE=0) */
	regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_ARPE, 0);
	regmap_write(priv->regmap, TIM_ARR, preset);

	return len;
}

static const struct iio_chan_spec_ext_info stm32_trigger_count_info[] = {
	{
		.name = "preset",
		.shared = IIO_SEPARATE,
		.read = stm32_count_get_preset,
		.write = stm32_count_set_preset
	},
	IIO_ENUM("enable_mode", IIO_SEPARATE, &stm32_enable_mode_enum),
	IIO_ENUM_AVAILABLE("enable_mode", &stm32_enable_mode_enum),
	IIO_ENUM("trigger_mode", IIO_SEPARATE, &stm32_trigger_mode_enum),
	IIO_ENUM_AVAILABLE("trigger_mode", &stm32_trigger_mode_enum),
	{}
};

static const struct iio_chan_spec stm32_trigger_channel = {
	.type = IIO_COUNT,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_ENABLE) |
			      BIT(IIO_CHAN_INFO_SCALE),
	.ext_info = stm32_trigger_count_info,
	.indexed = 1
};

static struct stm32_timer_trigger *stm32_setup_counter_device(struct device *dev)
{
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev,
					  sizeof(struct stm32_timer_trigger));
	if (!indio_dev)
		return NULL;

	indio_dev->name = dev_name(dev);
	indio_dev->info = &stm32_trigger_info;
	indio_dev->modes = INDIO_HARDWARE_TRIGGERED;
	indio_dev->num_channels = 1;
	indio_dev->channels = &stm32_trigger_channel;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return NULL;

	return iio_priv(indio_dev);
}

/**
 * is_stm32_timer_trigger
 * @trig: trigger to be checked
 *
 * return true if the trigger is a valid stm32 iio timer trigger
 * either return false
 */
bool is_stm32_timer_trigger(struct iio_trigger *trig)
{
	return (trig->ops == &timer_trigger_ops);
}
EXPORT_SYMBOL(is_stm32_timer_trigger);

static void stm32_timer_detect_trgo2(struct stm32_timer_trigger *priv)
{
	u32 val;

	/*
	 * Master mode selection 2 bits can only be written and read back when
	 * timer supports it.
	 */
	regmap_update_bits(priv->regmap, TIM_CR2, TIM_CR2_MMS2, TIM_CR2_MMS2);
	regmap_read(priv->regmap, TIM_CR2, &val);
	regmap_update_bits(priv->regmap, TIM_CR2, TIM_CR2_MMS2, 0);
	priv->has_trgo2 = !!val;
}

static int stm32_timer_trigger_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_timer_trigger *priv;
	struct stm32_timers *ddata = dev_get_drvdata(pdev->dev.parent);
	const struct stm32_timer_trigger_cfg *cfg;
	unsigned int index;
	int ret;

	if (of_property_read_u32(dev->of_node, "reg", &index))
		return -EINVAL;

	cfg = (const struct stm32_timer_trigger_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;

	if (index >= ARRAY_SIZE(triggers_table) ||
	    index >= cfg->num_valids_table)
		return -EINVAL;

	/* Create an IIO device only if we have triggers to be validated */
	if (*cfg->valids_table[index])
		priv = stm32_setup_counter_device(dev);
	else
		priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regmap = ddata->regmap;
	priv->clk = ddata->clk;
	priv->max_arr = ddata->max_arr;
	priv->triggers = triggers_table[index];
	priv->valids = cfg->valids_table[index];
	stm32_timer_detect_trgo2(priv);
	mutex_init(&priv->lock);

	ret = stm32_register_iio_triggers(priv);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int stm32_timer_trigger_remove(struct platform_device *pdev)
{
	struct stm32_timer_trigger *priv = platform_get_drvdata(pdev);
	u32 val;

	/* Unregister triggers before everything can be safely turned off */
	stm32_unregister_iio_triggers(priv);

	/* Check if nobody else use the timer, then disable it */
	regmap_read(priv->regmap, TIM_CCER, &val);
	if (!(val & TIM_CCER_CCXE))
		regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);

	if (priv->enabled)
		clk_disable(priv->clk);

	return 0;
}

static int __maybe_unused stm32_timer_trigger_suspend(struct device *dev)
{
	struct stm32_timer_trigger *priv = dev_get_drvdata(dev);

	/* Only take care of enabled timer: don't disturb other MFD child */
	if (priv->enabled) {
		/* Backup registers that may get lost in low power mode */
		regmap_read(priv->regmap, TIM_CR1, &priv->bak.cr1);
		regmap_read(priv->regmap, TIM_CR2, &priv->bak.cr2);
		regmap_read(priv->regmap, TIM_PSC, &priv->bak.psc);
		regmap_read(priv->regmap, TIM_ARR, &priv->bak.arr);
		regmap_read(priv->regmap, TIM_CNT, &priv->bak.cnt);
		regmap_read(priv->regmap, TIM_SMCR, &priv->bak.smcr);

		/* Disable the timer */
		regmap_update_bits(priv->regmap, TIM_CR1, TIM_CR1_CEN, 0);
		clk_disable(priv->clk);
	}

	return 0;
}

static int __maybe_unused stm32_timer_trigger_resume(struct device *dev)
{
	struct stm32_timer_trigger *priv = dev_get_drvdata(dev);
	int ret;

	if (priv->enabled) {
		ret = clk_enable(priv->clk);
		if (ret)
			return ret;

		/* restore master/slave modes */
		regmap_write(priv->regmap, TIM_SMCR, priv->bak.smcr);
		regmap_write(priv->regmap, TIM_CR2, priv->bak.cr2);

		/* restore sampling_frequency (trgo / trgo2 triggers) */
		regmap_write(priv->regmap, TIM_PSC, priv->bak.psc);
		regmap_write(priv->regmap, TIM_ARR, priv->bak.arr);
		regmap_write(priv->regmap, TIM_CNT, priv->bak.cnt);

		/* Also re-enables the timer */
		regmap_write(priv->regmap, TIM_CR1, priv->bak.cr1);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(stm32_timer_trigger_pm_ops,
			 stm32_timer_trigger_suspend,
			 stm32_timer_trigger_resume);

static const struct stm32_timer_trigger_cfg stm32_timer_trg_cfg = {
	.valids_table = valids_table,
	.num_valids_table = ARRAY_SIZE(valids_table),
};

static const struct stm32_timer_trigger_cfg stm32h7_timer_trg_cfg = {
	.valids_table = stm32h7_valids_table,
	.num_valids_table = ARRAY_SIZE(stm32h7_valids_table),
};

static const struct of_device_id stm32_trig_of_match[] = {
	{
		.compatible = "st,stm32-timer-trigger",
		.data = (void *)&stm32_timer_trg_cfg,
	}, {
		.compatible = "st,stm32h7-timer-trigger",
		.data = (void *)&stm32h7_timer_trg_cfg,
	},
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_trig_of_match);

static struct platform_driver stm32_timer_trigger_driver = {
	.probe = stm32_timer_trigger_probe,
	.remove = stm32_timer_trigger_remove,
	.driver = {
		.name = "stm32-timer-trigger",
		.of_match_table = stm32_trig_of_match,
		.pm = &stm32_timer_trigger_pm_ops,
	},
};
module_platform_driver(stm32_timer_trigger_driver);

MODULE_ALIAS("platform: stm32-timer-trigger");
MODULE_DESCRIPTION("STMicroelectronics STM32 Timer Trigger driver");
MODULE_LICENSE("GPL v2");
