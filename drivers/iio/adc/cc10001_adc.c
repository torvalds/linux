// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015 Imagination Technologies Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/* Registers */
#define CC10001_ADC_CONFIG		0x00
#define CC10001_ADC_START_CONV		BIT(4)
#define CC10001_ADC_MODE_SINGLE_CONV	BIT(5)

#define CC10001_ADC_DDATA_OUT		0x04
#define CC10001_ADC_EOC			0x08
#define CC10001_ADC_EOC_SET		BIT(0)

#define CC10001_ADC_CHSEL_SAMPLED	0x0c
#define CC10001_ADC_POWER_DOWN		0x10
#define CC10001_ADC_POWER_DOWN_SET	BIT(0)

#define CC10001_ADC_DEBUG		0x14
#define CC10001_ADC_DATA_COUNT		0x20

#define CC10001_ADC_DATA_MASK		GENMASK(9, 0)
#define CC10001_ADC_NUM_CHANNELS	8
#define CC10001_ADC_CH_MASK		GENMASK(2, 0)

#define CC10001_INVALID_SAMPLED		0xffff
#define CC10001_MAX_POLL_COUNT		20

/*
 * As per device specification, wait six clock cycles after power-up to
 * activate START. Since adding two more clock cycles delay does not
 * impact the performance too much, we are adding two additional cycles delay
 * intentionally here.
 */
#define	CC10001_WAIT_CYCLES		8

struct cc10001_adc_device {
	void __iomem *reg_base;
	struct clk *adc_clk;
	struct regulator *reg;
	u16 *buf;

	bool shared;
	struct mutex lock;
	unsigned int start_delay_ns;
	unsigned int eoc_delay_ns;
};

static inline void cc10001_adc_write_reg(struct cc10001_adc_device *adc_dev,
					 u32 reg, u32 val)
{
	writel(val, adc_dev->reg_base + reg);
}

static inline u32 cc10001_adc_read_reg(struct cc10001_adc_device *adc_dev,
				       u32 reg)
{
	return readl(adc_dev->reg_base + reg);
}

static void cc10001_adc_power_up(struct cc10001_adc_device *adc_dev)
{
	cc10001_adc_write_reg(adc_dev, CC10001_ADC_POWER_DOWN, 0);
	ndelay(adc_dev->start_delay_ns);
}

static void cc10001_adc_power_down(struct cc10001_adc_device *adc_dev)
{
	cc10001_adc_write_reg(adc_dev, CC10001_ADC_POWER_DOWN,
			      CC10001_ADC_POWER_DOWN_SET);
}

static void cc10001_adc_start(struct cc10001_adc_device *adc_dev,
			      unsigned int channel)
{
	u32 val;

	/* Channel selection and mode of operation */
	val = (channel & CC10001_ADC_CH_MASK) | CC10001_ADC_MODE_SINGLE_CONV;
	cc10001_adc_write_reg(adc_dev, CC10001_ADC_CONFIG, val);

	udelay(1);
	val = cc10001_adc_read_reg(adc_dev, CC10001_ADC_CONFIG);
	val = val | CC10001_ADC_START_CONV;
	cc10001_adc_write_reg(adc_dev, CC10001_ADC_CONFIG, val);
}

static u16 cc10001_adc_poll_done(struct iio_dev *indio_dev,
				 unsigned int channel,
				 unsigned int delay)
{
	struct cc10001_adc_device *adc_dev = iio_priv(indio_dev);
	unsigned int poll_count = 0;

	while (!(cc10001_adc_read_reg(adc_dev, CC10001_ADC_EOC) &
			CC10001_ADC_EOC_SET)) {

		ndelay(delay);
		if (poll_count++ == CC10001_MAX_POLL_COUNT)
			return CC10001_INVALID_SAMPLED;
	}

	poll_count = 0;
	while ((cc10001_adc_read_reg(adc_dev, CC10001_ADC_CHSEL_SAMPLED) &
			CC10001_ADC_CH_MASK) != channel) {

		ndelay(delay);
		if (poll_count++ == CC10001_MAX_POLL_COUNT)
			return CC10001_INVALID_SAMPLED;
	}

	/* Read the 10 bit output register */
	return cc10001_adc_read_reg(adc_dev, CC10001_ADC_DDATA_OUT) &
			       CC10001_ADC_DATA_MASK;
}

static irqreturn_t cc10001_adc_trigger_h(int irq, void *p)
{
	struct cc10001_adc_device *adc_dev;
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev;
	unsigned int delay_ns;
	unsigned int channel;
	unsigned int scan_idx;
	bool sample_invalid;
	u16 *data;
	int i;

	indio_dev = pf->indio_dev;
	adc_dev = iio_priv(indio_dev);
	data = adc_dev->buf;

	mutex_lock(&adc_dev->lock);

	if (!adc_dev->shared)
		cc10001_adc_power_up(adc_dev);

	/* Calculate delay step for eoc and sampled data */
	delay_ns = adc_dev->eoc_delay_ns / CC10001_MAX_POLL_COUNT;

	i = 0;
	sample_invalid = false;
	iio_for_each_active_channel(indio_dev, scan_idx) {
		channel = indio_dev->channels[scan_idx].channel;
		cc10001_adc_start(adc_dev, channel);

		data[i] = cc10001_adc_poll_done(indio_dev, channel, delay_ns);
		if (data[i] == CC10001_INVALID_SAMPLED) {
			dev_warn(&indio_dev->dev,
				 "invalid sample on channel %d\n", channel);
			sample_invalid = true;
			goto done;
		}
		i++;
	}

done:
	if (!adc_dev->shared)
		cc10001_adc_power_down(adc_dev);

	mutex_unlock(&adc_dev->lock);

	if (!sample_invalid)
		iio_push_to_buffers_with_timestamp(indio_dev, data,
						   iio_get_time_ns(indio_dev));
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static u16 cc10001_adc_read_raw_voltage(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan)
{
	struct cc10001_adc_device *adc_dev = iio_priv(indio_dev);
	unsigned int delay_ns;
	u16 val;

	if (!adc_dev->shared)
		cc10001_adc_power_up(adc_dev);

	/* Calculate delay step for eoc and sampled data */
	delay_ns = adc_dev->eoc_delay_ns / CC10001_MAX_POLL_COUNT;

	cc10001_adc_start(adc_dev, chan->channel);

	val = cc10001_adc_poll_done(indio_dev, chan->channel, delay_ns);

	if (!adc_dev->shared)
		cc10001_adc_power_down(adc_dev);

	return val;
}

static int cc10001_adc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct cc10001_adc_device *adc_dev = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (iio_buffer_enabled(indio_dev))
			return -EBUSY;
		mutex_lock(&adc_dev->lock);
		*val = cc10001_adc_read_raw_voltage(indio_dev, chan);
		mutex_unlock(&adc_dev->lock);

		if (*val == CC10001_INVALID_SAMPLED)
			return -EIO;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(adc_dev->reg);
		if (ret < 0)
			return ret;

		*val = ret / 1000;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static int cc10001_update_scan_mode(struct iio_dev *indio_dev,
				    const unsigned long *scan_mask)
{
	struct cc10001_adc_device *adc_dev = iio_priv(indio_dev);

	kfree(adc_dev->buf);
	adc_dev->buf = kmalloc(indio_dev->scan_bytes, GFP_KERNEL);
	if (!adc_dev->buf)
		return -ENOMEM;

	return 0;
}

static const struct iio_info cc10001_adc_info = {
	.read_raw = &cc10001_adc_read_raw,
	.update_scan_mode = &cc10001_update_scan_mode,
};

static int cc10001_adc_channel_init(struct iio_dev *indio_dev,
				    unsigned long channel_map)
{
	struct iio_chan_spec *chan_array, *timestamp;
	unsigned int bit, idx = 0;

	indio_dev->num_channels = bitmap_weight(&channel_map,
						CC10001_ADC_NUM_CHANNELS) + 1;

	chan_array = devm_kcalloc(&indio_dev->dev, indio_dev->num_channels,
				  sizeof(struct iio_chan_spec),
				  GFP_KERNEL);
	if (!chan_array)
		return -ENOMEM;

	for_each_set_bit(bit, &channel_map, CC10001_ADC_NUM_CHANNELS) {
		struct iio_chan_spec *chan = &chan_array[idx];

		chan->type = IIO_VOLTAGE;
		chan->indexed = 1;
		chan->channel = bit;
		chan->scan_index = idx;
		chan->scan_type.sign = 'u';
		chan->scan_type.realbits = 10;
		chan->scan_type.storagebits = 16;
		chan->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE);
		chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		idx++;
	}

	timestamp = &chan_array[idx];
	timestamp->type = IIO_TIMESTAMP;
	timestamp->channel = -1;
	timestamp->scan_index = idx;
	timestamp->scan_type.sign = 's';
	timestamp->scan_type.realbits = 64;
	timestamp->scan_type.storagebits = 64;

	indio_dev->channels = chan_array;

	return 0;
}

static void cc10001_reg_disable(void *priv)
{
	regulator_disable(priv);
}

static void cc10001_pd_cb(void *priv)
{
	cc10001_adc_power_down(priv);
}

static int cc10001_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct cc10001_adc_device *adc_dev;
	unsigned long adc_clk_rate;
	struct iio_dev *indio_dev;
	unsigned long channel_map;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc_dev));
	if (indio_dev == NULL)
		return -ENOMEM;

	adc_dev = iio_priv(indio_dev);

	channel_map = GENMASK(CC10001_ADC_NUM_CHANNELS - 1, 0);
	if (!of_property_read_u32(node, "adc-reserved-channels", &ret)) {
		adc_dev->shared = true;
		channel_map &= ~ret;
	}

	adc_dev->reg = devm_regulator_get(dev, "vref");
	if (IS_ERR(adc_dev->reg))
		return PTR_ERR(adc_dev->reg);

	ret = regulator_enable(adc_dev->reg);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, cc10001_reg_disable, adc_dev->reg);
	if (ret)
		return ret;

	indio_dev->name = dev_name(dev);
	indio_dev->info = &cc10001_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	adc_dev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc_dev->reg_base))
		return PTR_ERR(adc_dev->reg_base);

	adc_dev->adc_clk = devm_clk_get_enabled(dev, "adc");
	if (IS_ERR(adc_dev->adc_clk)) {
		dev_err(dev, "failed to get/enable the clock\n");
		return PTR_ERR(adc_dev->adc_clk);
	}

	adc_clk_rate = clk_get_rate(adc_dev->adc_clk);
	if (!adc_clk_rate) {
		dev_err(dev, "null clock rate!\n");
		return -EINVAL;
	}

	adc_dev->eoc_delay_ns = NSEC_PER_SEC / adc_clk_rate;
	adc_dev->start_delay_ns = adc_dev->eoc_delay_ns * CC10001_WAIT_CYCLES;

	/*
	 * There is only one register to power-up/power-down the AUX ADC.
	 * If the ADC is shared among multiple CPUs, always power it up here.
	 * If the ADC is used only by the MIPS, power-up/power-down at runtime.
	 */
	if (adc_dev->shared)
		cc10001_adc_power_up(adc_dev);

	ret = devm_add_action_or_reset(dev, cc10001_pd_cb, adc_dev);
	if (ret)
		return ret;
	/* Setup the ADC channels available on the device */
	ret = cc10001_adc_channel_init(indio_dev, channel_map);
	if (ret < 0)
		return ret;

	mutex_init(&adc_dev->lock);

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      &cc10001_adc_trigger_h, NULL);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id cc10001_adc_dt_ids[] = {
	{ .compatible = "cosmic,10001-adc", },
	{ }
};
MODULE_DEVICE_TABLE(of, cc10001_adc_dt_ids);

static struct platform_driver cc10001_adc_driver = {
	.driver = {
		.name   = "cc10001-adc",
		.of_match_table = cc10001_adc_dt_ids,
	},
	.probe	= cc10001_adc_probe,
};
module_platform_driver(cc10001_adc_driver);

MODULE_AUTHOR("Phani Movva <Phani.Movva@imgtec.com>");
MODULE_DESCRIPTION("Cosmic Circuits ADC driver");
MODULE_LICENSE("GPL v2");
