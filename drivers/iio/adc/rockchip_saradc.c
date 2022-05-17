// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip Successive Approximation Register (SAR) A/D Converter
 * Copyright (C) 2014 ROCKCHIP, Inc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define SARADC_DATA			0x00

#define SARADC_STAS			0x04
#define SARADC_STAS_BUSY		BIT(0)

#define SARADC_CTRL			0x08
#define SARADC_CTRL_IRQ_STATUS		BIT(6)
#define SARADC_CTRL_IRQ_ENABLE		BIT(5)
#define SARADC_CTRL_POWER_CTRL		BIT(3)
#define SARADC_CTRL_CHN_MASK		0x7

#define SARADC_DLY_PU_SOC		0x0c
#define SARADC_DLY_PU_SOC_MASK		0x3f

#define SARADC_TIMEOUT			msecs_to_jiffies(100)
#define SARADC_MAX_CHANNELS		8

/* v2 registers */
#define SARADC2_CONV_CON		0x0
#define SARADC_T_PD_SOC			0x4
#define SARADC_T_DAS_SOC		0xc
#define SARADC2_END_INT_EN		0x104
#define SARADC2_ST_CON			0x108
#define SARADC2_STATUS			0x10c
#define SARADC2_END_INT_ST		0x110
#define SARADC2_DATA_BASE		0x120

#define SARADC2_EN_END_INT		BIT(0)
#define SARADC2_START			BIT(4)
#define SARADC2_SINGLE_MODE		BIT(5)

struct rockchip_saradc;

struct rockchip_saradc_data {
	const struct iio_chan_spec	*channels;
	int				num_channels;
	unsigned long			clk_rate;
	void (*start)(struct rockchip_saradc *info, int chn);
	int (*read)(struct rockchip_saradc *info);
	void (*power_down)(struct rockchip_saradc *info);
};

struct rockchip_saradc {
	void __iomem		*regs;
	struct clk		*pclk;
	struct clk		*clk;
	struct completion	completion;
	struct regulator	*vref;
	int			uv_vref;
	struct reset_control	*reset;
	const struct rockchip_saradc_data *data;
	u16			last_val;
	const struct iio_chan_spec *last_chan;
	bool			suspended;
#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	bool			test;
	u32			chn;
	spinlock_t		lock;
	struct workqueue_struct *wq;
	struct delayed_work	work;
#endif
};

static void rockchip_saradc_reset_controller(struct reset_control *reset);

static void rockchip_saradc_start_v1(struct rockchip_saradc *info,
					int chn)
{
	/* 8 clock periods as delay between power up and start cmd */
	writel_relaxed(8, info->regs + SARADC_DLY_PU_SOC);
	/* Select the channel to be used and trigger conversion */
	writel(SARADC_CTRL_POWER_CTRL | (chn & SARADC_CTRL_CHN_MASK) |
	       SARADC_CTRL_IRQ_ENABLE, info->regs + SARADC_CTRL);
}

static void rockchip_saradc_start_v2(struct rockchip_saradc *info,
					int chn)
{
	int val;

	/* If read other chn at anytime, then chn1 will error, assert
	 * controller as a workaround.
	 */
	if (info->reset)
		rockchip_saradc_reset_controller(info->reset);

	writel_relaxed(0xc, info->regs + SARADC_T_DAS_SOC);
	writel_relaxed(0x20, info->regs + SARADC_T_PD_SOC);
	val = SARADC2_EN_END_INT << 16 | SARADC2_EN_END_INT;
	writel_relaxed(val, info->regs + SARADC2_END_INT_EN);
	val = SARADC2_START | SARADC2_SINGLE_MODE | chn;
	writel(val << 16 | val, info->regs + SARADC2_CONV_CON);
}

static void rockchip_saradc_start(struct rockchip_saradc *info,
					int chn)
{
	info->data->start(info, chn);
}

static int rockchip_saradc_read_v1(struct rockchip_saradc *info)
{
	return readl_relaxed(info->regs + SARADC_DATA);
}

static int rockchip_saradc_read_v2(struct rockchip_saradc *info)
{
	int offset;
	int channel;

	/* Clear irq */
	writel_relaxed(0x1, info->regs + SARADC2_END_INT_ST);

#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	channel = info->chn;
#else
	channel = info->last_chan->channel;
#endif

	offset = SARADC2_DATA_BASE + channel * 0x4;

	return readl_relaxed(info->regs + offset);
}

static int rockchip_saradc_read(struct rockchip_saradc *info)
{
	return info->data->read(info);
}

static void rockchip_saradc_power_down_v1(struct rockchip_saradc *info)
{
	writel_relaxed(0, info->regs + SARADC_CTRL);
}

static void rockchip_saradc_power_down(struct rockchip_saradc *info)
{
	if (info->data->power_down)
		info->data->power_down(info);
}

static int rockchip_saradc_conversion(struct rockchip_saradc *info,
				   struct iio_chan_spec const *chan)
{
	reinit_completion(&info->completion);

	/* prevent isr get NULL last_chan */
	info->last_chan = chan;
	rockchip_saradc_start(info, chan->channel);

	if (!wait_for_completion_timeout(&info->completion, SARADC_TIMEOUT))
		return -ETIMEDOUT;

	return 0;
}

static int rockchip_saradc_read_raw(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    int *val, int *val2, long mask)
{
	struct rockchip_saradc *info = iio_priv(indio_dev);
	int ret;

#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	if (info->test)
		return 0;
#endif
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);

		if (info->suspended) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

		ret = rockchip_saradc_conversion(info, chan);
		if (ret) {
			rockchip_saradc_power_down(info);
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		*val = info->last_val;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* It is a dummy regulator */
		if (info->uv_vref < 0)
			return info->uv_vref;

		*val = info->uv_vref / 1000;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static irqreturn_t rockchip_saradc_isr(int irq, void *dev_id)
{
	struct rockchip_saradc *info = dev_id;
#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	unsigned long flags;
#endif

	/* Read value */
	info->last_val = rockchip_saradc_read(info);
#ifndef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	info->last_val &= GENMASK(info->last_chan->scan_type.realbits - 1, 0);
#endif

	rockchip_saradc_power_down(info);

	complete(&info->completion);
#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	spin_lock_irqsave(&info->lock, flags);
	if (info->test) {
		pr_info("chn[%d] val = %d\n", info->chn, info->last_val);
		mod_delayed_work(info->wq, &info->work, msecs_to_jiffies(100));
	}
	spin_unlock_irqrestore(&info->lock, flags);
#endif
	return IRQ_HANDLED;
}

static const struct iio_info rockchip_saradc_iio_info = {
	.read_raw = rockchip_saradc_read_raw,
};

#define SARADC_CHANNEL(_index, _id, _res) {			\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = _index,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.datasheet_name = _id,					\
	.scan_index = _index,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = _res,				\
		.storagebits = 16,				\
		.endianness = IIO_CPU,				\
	},							\
}

static const struct iio_chan_spec rockchip_saradc_iio_channels[] = {
	SARADC_CHANNEL(0, "adc0", 10),
	SARADC_CHANNEL(1, "adc1", 10),
	SARADC_CHANNEL(2, "adc2", 10),
};

static const struct rockchip_saradc_data saradc_data = {
	.channels = rockchip_saradc_iio_channels,
	.num_channels = ARRAY_SIZE(rockchip_saradc_iio_channels),
	.clk_rate = 1000000,
	.start = rockchip_saradc_start_v1,
	.read = rockchip_saradc_read_v1,
	.power_down = rockchip_saradc_power_down_v1,
};

static const struct iio_chan_spec rockchip_rk3066_tsadc_iio_channels[] = {
	SARADC_CHANNEL(0, "adc0", 12),
	SARADC_CHANNEL(1, "adc1", 12),
};

static const struct rockchip_saradc_data rk3066_tsadc_data = {
	.channels = rockchip_rk3066_tsadc_iio_channels,
	.num_channels = ARRAY_SIZE(rockchip_rk3066_tsadc_iio_channels),
	.clk_rate = 50000,
	.start = rockchip_saradc_start_v1,
	.read = rockchip_saradc_read_v1,
	.power_down = rockchip_saradc_power_down_v1,
};

static const struct iio_chan_spec rockchip_rk3399_saradc_iio_channels[] = {
	SARADC_CHANNEL(0, "adc0", 10),
	SARADC_CHANNEL(1, "adc1", 10),
	SARADC_CHANNEL(2, "adc2", 10),
	SARADC_CHANNEL(3, "adc3", 10),
	SARADC_CHANNEL(4, "adc4", 10),
	SARADC_CHANNEL(5, "adc5", 10),
};

static const struct rockchip_saradc_data rk3399_saradc_data = {
	.channels = rockchip_rk3399_saradc_iio_channels,
	.num_channels = ARRAY_SIZE(rockchip_rk3399_saradc_iio_channels),
	.clk_rate = 1000000,
	.start = rockchip_saradc_start_v1,
	.read = rockchip_saradc_read_v1,
	.power_down = rockchip_saradc_power_down_v1,
};

static const struct iio_chan_spec rockchip_rk3568_saradc_iio_channels[] = {
	SARADC_CHANNEL(0, "adc0", 10),
	SARADC_CHANNEL(1, "adc1", 10),
	SARADC_CHANNEL(2, "adc2", 10),
	SARADC_CHANNEL(3, "adc3", 10),
	SARADC_CHANNEL(4, "adc4", 10),
	SARADC_CHANNEL(5, "adc5", 10),
	SARADC_CHANNEL(6, "adc6", 10),
	SARADC_CHANNEL(7, "adc7", 10),
};

static const struct rockchip_saradc_data rk3568_saradc_data = {
	.channels = rockchip_rk3568_saradc_iio_channels,
	.num_channels = ARRAY_SIZE(rockchip_rk3568_saradc_iio_channels),
	.clk_rate = 1000000,
	.start = rockchip_saradc_start_v1,
	.read = rockchip_saradc_read_v1,
	.power_down = rockchip_saradc_power_down_v1,
};

static const struct iio_chan_spec rockchip_rk3588_saradc_iio_channels[] = {
	SARADC_CHANNEL(0, "adc0", 12),
	SARADC_CHANNEL(1, "adc1", 12),
	SARADC_CHANNEL(2, "adc2", 12),
	SARADC_CHANNEL(3, "adc3", 12),
	SARADC_CHANNEL(4, "adc4", 12),
	SARADC_CHANNEL(5, "adc5", 12),
	SARADC_CHANNEL(6, "adc6", 12),
	SARADC_CHANNEL(7, "adc7", 12),
};

static const struct rockchip_saradc_data rk3588_saradc_data = {
	.channels = rockchip_rk3588_saradc_iio_channels,
	.num_channels = ARRAY_SIZE(rockchip_rk3588_saradc_iio_channels),
	.clk_rate = 1000000,
	.start = rockchip_saradc_start_v2,
	.read = rockchip_saradc_read_v2,
};

static const struct iio_chan_spec rockchip_rv1106_saradc_iio_channels[] = {
	SARADC_CHANNEL(0, "adc0", 10),
	SARADC_CHANNEL(1, "adc1", 10),
};

static const struct rockchip_saradc_data rv1106_saradc_data = {
	.channels = rockchip_rv1106_saradc_iio_channels,
	.num_channels = ARRAY_SIZE(rockchip_rv1106_saradc_iio_channels),
	.clk_rate = 1000000,
	.start = rockchip_saradc_start_v2,
	.read = rockchip_saradc_read_v2,
};

static const struct of_device_id rockchip_saradc_match[] = {
	{
		.compatible = "rockchip,saradc",
		.data = &saradc_data,
	}, {
		.compatible = "rockchip,rk3066-tsadc",
		.data = &rk3066_tsadc_data,
	}, {
		.compatible = "rockchip,rk3399-saradc",
		.data = &rk3399_saradc_data,
	}, {
		.compatible = "rockchip,rk3568-saradc",
		.data = &rk3568_saradc_data,
	}, {
		.compatible = "rockchip,rk3588-saradc",
		.data = &rk3588_saradc_data,
	}, {
		.compatible = "rockchip,rv1106-saradc",
		.data = &rv1106_saradc_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_saradc_match);

/*
 * Reset SARADC Controller.
 */
static void rockchip_saradc_reset_controller(struct reset_control *reset)
{
	reset_control_assert(reset);
	usleep_range(10, 20);
	reset_control_deassert(reset);
}

static void rockchip_saradc_clk_disable(void *data)
{
	struct rockchip_saradc *info = data;

	clk_disable_unprepare(info->clk);
}

static void rockchip_saradc_pclk_disable(void *data)
{
	struct rockchip_saradc *info = data;

	clk_disable_unprepare(info->pclk);
}

static void rockchip_saradc_regulator_disable(void *data)
{
	struct rockchip_saradc *info = data;

	regulator_disable(info->vref);
}

static irqreturn_t rockchip_saradc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *i_dev = pf->indio_dev;
	struct rockchip_saradc *info = iio_priv(i_dev);
	/*
	 * @values: each channel takes an u16 value
	 * @timestamp: will be 8-byte aligned automatically
	 */
	struct {
		u16 values[SARADC_MAX_CHANNELS];
		int64_t timestamp;
	} data;
	int ret;
	int i, j = 0;

	mutex_lock(&i_dev->mlock);

	for_each_set_bit(i, i_dev->active_scan_mask, i_dev->masklength) {
		const struct iio_chan_spec *chan = &i_dev->channels[i];

		ret = rockchip_saradc_conversion(info, chan);
		if (ret) {
			rockchip_saradc_power_down(info);
			goto out;
		}

		data.values[j] = info->last_val;
		j++;
	}

	iio_push_to_buffers_with_timestamp(i_dev, &data, iio_get_time_ns(i_dev));
out:
	mutex_unlock(&i_dev->mlock);

	iio_trigger_notify_done(i_dev->trig);

	return IRQ_HANDLED;
}

#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
static ssize_t saradc_test_chn_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	u32 val = 0;
	int err;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rockchip_saradc *info = iio_priv(indio_dev);
	unsigned long flags;

	err = kstrtou32(buf, 10, &val);
	if (err)
		return err;

	spin_lock_irqsave(&info->lock, flags);

	if (val > SARADC_CTRL_CHN_MASK && info->test) {
		info->test = false;
		spin_unlock_irqrestore(&info->lock, flags);
		cancel_delayed_work_sync(&info->work);
		return size;
	}

	if (!info->test && val < SARADC_CTRL_CHN_MASK) {
		info->test = true;
		info->chn = val;
		mod_delayed_work(info->wq, &info->work, msecs_to_jiffies(100));
	}

	spin_unlock_irqrestore(&info->lock, flags);

	return size;
}

static DEVICE_ATTR_WO(saradc_test_chn);

static struct attribute *saradc_attrs[] = {
	&dev_attr_saradc_test_chn.attr,
	NULL
};

static const struct attribute_group rockchip_saradc_attr_group = {
	.attrs = saradc_attrs,
};

static void rockchip_saradc_remove_sysgroup(void *data)
{
	struct platform_device *pdev = data;

	sysfs_remove_group(&pdev->dev.kobj, &rockchip_saradc_attr_group);
}

static void rockchip_saradc_destroy_wq(void *data)
{
	struct rockchip_saradc *info = data;

	destroy_workqueue(info->wq);
}

static void rockchip_saradc_test_work(struct work_struct *work)
{
	struct rockchip_saradc *info = container_of(work,
					struct rockchip_saradc, work.work);

	rockchip_saradc_start(info, info->chn);
}
#endif

static int rockchip_saradc_probe(struct platform_device *pdev)
{
	struct rockchip_saradc *info = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct iio_dev *indio_dev = NULL;
	struct resource	*mem;
	const struct of_device_id *match;
	int ret;
	int irq;

	if (!np)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}
	info = iio_priv(indio_dev);

	match = of_match_device(rockchip_saradc_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "failed to match device\n");
		return -ENODEV;
	}

	info->data = match->data;

	/* Sanity check for possible later IP variants with more channels */
	if (info->data->num_channels > SARADC_MAX_CHANNELS) {
		dev_err(&pdev->dev, "max channels exceeded");
		return -EINVAL;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	/*
	 * The reset should be an optional property, as it should work
	 * with old devicetrees as well
	 */
	info->reset = devm_reset_control_get_exclusive(&pdev->dev,
						       "saradc-apb");
	if (IS_ERR(info->reset)) {
		ret = PTR_ERR(info->reset);
		if (ret != -ENOENT)
			return ret;

		dev_dbg(&pdev->dev, "no reset control found\n");
		info->reset = NULL;
	}

	init_completion(&info->completion);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, rockchip_saradc_isr,
			       0, dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq %d\n", irq);
		return ret;
	}

	info->pclk = devm_clk_get(&pdev->dev, "apb_pclk");
	if (IS_ERR(info->pclk)) {
		dev_err(&pdev->dev, "failed to get pclk\n");
		return PTR_ERR(info->pclk);
	}

	info->clk = devm_clk_get(&pdev->dev, "saradc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed to get adc clock\n");
		return PTR_ERR(info->clk);
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref)) {
		dev_err(&pdev->dev, "failed to get regulator, %ld\n",
			PTR_ERR(info->vref));
		return PTR_ERR(info->vref);
	}

	if (info->reset)
		rockchip_saradc_reset_controller(info->reset);

	/*
	 * Use a default value for the converter clock.
	 * This may become user-configurable in the future.
	 */
	ret = clk_set_rate(info->clk, info->data->clk_rate);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set adc clk rate, %d\n", ret);
		return ret;
	}

	ret = regulator_enable(info->vref);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable vref regulator\n");
		return ret;
	}
	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_regulator_disable, info);
	if (ret) {
		dev_err(&pdev->dev, "failed to register devm action, %d\n",
			ret);
		return ret;
	}

	info->uv_vref = regulator_get_voltage(info->vref);
	if (info->uv_vref < 0) {
		dev_err(&pdev->dev, "failed to get voltage\n");
		ret = info->uv_vref;
		return ret;
	}

	ret = clk_prepare_enable(info->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable pclk\n");
		return ret;
	}
	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_pclk_disable, info);
	if (ret) {
		dev_err(&pdev->dev, "failed to register devm action, %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(info->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable converter clock\n");
		return ret;
	}
	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_clk_disable, info);
	if (ret) {
		dev_err(&pdev->dev, "failed to register devm action, %d\n",
			ret);
		return ret;
	}

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->info = &rockchip_saradc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = info->data->channels;
	indio_dev->num_channels = info->data->num_channels;
	ret = devm_iio_triggered_buffer_setup(&indio_dev->dev, indio_dev, NULL,
					      rockchip_saradc_trigger_handler,
					      NULL);
	if (ret)
		return ret;

#ifdef CONFIG_ROCKCHIP_SARADC_TEST_CHN
	info->wq = create_singlethread_workqueue("adc_wq");
	INIT_DELAYED_WORK(&info->work, rockchip_saradc_test_work);
	spin_lock_init(&info->lock);
	ret = sysfs_create_group(&pdev->dev.kobj, &rockchip_saradc_attr_group);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_remove_sysgroup, pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register devm action, %d\n",
			ret);
		return ret;
	}

	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_destroy_wq, info);
	if (ret) {
		dev_err(&pdev->dev, "failed to register destroy_wq, %d\n",
			ret);
		return ret;
	}
#endif
	return devm_iio_device_register(&pdev->dev, indio_dev);
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_saradc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rockchip_saradc *info = iio_priv(indio_dev);

	/* Avoid reading saradc when suspending */
	mutex_lock(&indio_dev->mlock);

	clk_disable_unprepare(info->clk);
	clk_disable_unprepare(info->pclk);
	regulator_disable(info->vref);

	info->suspended = true;
	mutex_unlock(&indio_dev->mlock);

	return 0;
}

static int rockchip_saradc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rockchip_saradc *info = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	ret = clk_prepare_enable(info->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(info->clk);
	if (ret)
		clk_disable_unprepare(info->pclk);

	info->suspended = false;

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(rockchip_saradc_pm_ops,
			 rockchip_saradc_suspend, rockchip_saradc_resume);

static struct platform_driver rockchip_saradc_driver = {
	.probe		= rockchip_saradc_probe,
	.driver		= {
		.name	= "rockchip-saradc",
		.of_match_table = rockchip_saradc_match,
		.pm	= &rockchip_saradc_pm_ops,
	},
};

module_platform_driver(rockchip_saradc_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Rockchip SARADC driver");
MODULE_LICENSE("GPL v2");
