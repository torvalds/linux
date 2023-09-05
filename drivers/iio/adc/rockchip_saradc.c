// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip Successive Approximation Register (SAR) A/D Converter
 * Copyright (C) 2014 ROCKCHIP, Inc.
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/mutex.h>
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
#define SARADC2_CONV_CON		0x000
#define SARADC_T_PD_SOC			0x004
#define SARADC_T_DAS_SOC		0x00c
#define SARADC2_END_INT_EN		0x104
#define SARADC2_ST_CON			0x108
#define SARADC2_STATUS			0x10c
#define SARADC2_END_INT_ST		0x110
#define SARADC2_DATA_BASE		0x120

#define SARADC2_EN_END_INT		BIT(0)
#define SARADC2_START			BIT(4)
#define SARADC2_SINGLE_MODE		BIT(5)

#define SARADC2_CONV_CHANNELS GENMASK(15, 0)

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
	/* lock to protect against multiple access to the device */
	struct mutex		lock;
	int			uv_vref;
	struct reset_control	*reset;
	const struct rockchip_saradc_data *data;
	u16			last_val;
	const struct iio_chan_spec *last_chan;
	struct notifier_block nb;
};

static void rockchip_saradc_reset_controller(struct reset_control *reset);

static void rockchip_saradc_start_v1(struct rockchip_saradc *info, int chn)
{
	/* 8 clock periods as delay between power up and start cmd */
	writel_relaxed(8, info->regs + SARADC_DLY_PU_SOC);
	/* Select the channel to be used and trigger conversion */
	writel(SARADC_CTRL_POWER_CTRL | (chn & SARADC_CTRL_CHN_MASK) |
	       SARADC_CTRL_IRQ_ENABLE, info->regs + SARADC_CTRL);
}

static void rockchip_saradc_start_v2(struct rockchip_saradc *info, int chn)
{
	int val;

	if (info->reset)
		rockchip_saradc_reset_controller(info->reset);

	writel_relaxed(0xc, info->regs + SARADC_T_DAS_SOC);
	writel_relaxed(0x20, info->regs + SARADC_T_PD_SOC);
	val = FIELD_PREP(SARADC2_EN_END_INT, 1);
	val |= val << 16;
	writel_relaxed(val, info->regs + SARADC2_END_INT_EN);
	val = FIELD_PREP(SARADC2_START, 1) |
	      FIELD_PREP(SARADC2_SINGLE_MODE, 1) |
	      FIELD_PREP(SARADC2_CONV_CHANNELS, chn);
	val |= val << 16;
	writel(val, info->regs + SARADC2_CONV_CON);
}

static void rockchip_saradc_start(struct rockchip_saradc *info, int chn)
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

	/* Clear irq */
	writel_relaxed(0x1, info->regs + SARADC2_END_INT_ST);

	offset = SARADC2_DATA_BASE + info->last_chan->channel * 0x4;

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

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&info->lock);

		ret = rockchip_saradc_conversion(info, chan);
		if (ret) {
			rockchip_saradc_power_down(info);
			mutex_unlock(&info->lock);
			return ret;
		}

		*val = info->last_val;
		mutex_unlock(&info->lock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
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

	/* Read value */
	info->last_val = rockchip_saradc_read(info);
	info->last_val &= GENMASK(info->last_chan->scan_type.realbits - 1, 0);

	rockchip_saradc_power_down(info);

	complete(&info->completion);

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

	mutex_lock(&info->lock);

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
	mutex_unlock(&info->lock);

	iio_trigger_notify_done(i_dev->trig);

	return IRQ_HANDLED;
}

static int rockchip_saradc_volt_notify(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct rockchip_saradc *info =
			container_of(nb, struct rockchip_saradc, nb);

	if (event & REGULATOR_EVENT_VOLTAGE_CHANGE)
		info->uv_vref = (unsigned long)data;

	return NOTIFY_OK;
}

static void rockchip_saradc_regulator_unreg_notifier(void *data)
{
	struct rockchip_saradc *info = data;

	regulator_unregister_notifier(info->vref, &info->nb);
}

static int rockchip_saradc_probe(struct platform_device *pdev)
{
	const struct rockchip_saradc_data *match_data;
	struct rockchip_saradc *info = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct iio_dev *indio_dev = NULL;
	int ret;
	int irq;

	if (!np)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev)
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed allocating iio device\n");

	info = iio_priv(indio_dev);

	match_data = of_device_get_match_data(&pdev->dev);
	if (!match_data)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "failed to match device\n");

	info->data = match_data;

	/* Sanity check for possible later IP variants with more channels */
	if (info->data->num_channels > SARADC_MAX_CHANNELS)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "max channels exceeded");

	info->regs = devm_platform_ioremap_resource(pdev, 0);
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
			return dev_err_probe(&pdev->dev, ret,
					     "failed to get saradc-apb\n");

		dev_dbg(&pdev->dev, "no reset control found\n");
		info->reset = NULL;
	}

	init_completion(&info->completion);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(&pdev->dev, irq, "failed to get irq\n");

	ret = devm_request_irq(&pdev->dev, irq, rockchip_saradc_isr,
			       0, dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq %d\n", irq);
		return ret;
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref))
		return dev_err_probe(&pdev->dev, PTR_ERR(info->vref),
				     "failed to get regulator\n");

	if (info->reset)
		rockchip_saradc_reset_controller(info->reset);

	/*
	 * Use a default value for the converter clock.
	 * This may become user-configurable in the future.
	 */
	ret = clk_set_rate(info->clk, info->data->clk_rate);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to set adc clk rate\n");

	ret = regulator_enable(info->vref);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to enable vref regulator\n");

	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_regulator_disable, info);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register devm action\n");

	ret = regulator_get_voltage(info->vref);
	if (ret < 0)
		return ret;

	info->uv_vref = ret;

	info->pclk = devm_clk_get_enabled(&pdev->dev, "apb_pclk");
	if (IS_ERR(info->pclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(info->pclk),
				     "failed to get pclk\n");

	info->clk = devm_clk_get_enabled(&pdev->dev, "saradc");
	if (IS_ERR(info->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(info->clk),
				     "failed to get adc clock\n");

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

	info->nb.notifier_call = rockchip_saradc_volt_notify;
	ret = regulator_register_notifier(info->vref, &info->nb);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(&pdev->dev,
				       rockchip_saradc_regulator_unreg_notifier,
				       info);
	if (ret)
		return ret;

	mutex_init(&info->lock);

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static int rockchip_saradc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rockchip_saradc *info = iio_priv(indio_dev);

	clk_disable_unprepare(info->clk);
	clk_disable_unprepare(info->pclk);
	regulator_disable(info->vref);

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

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(rockchip_saradc_pm_ops,
				rockchip_saradc_suspend,
				rockchip_saradc_resume);

static struct platform_driver rockchip_saradc_driver = {
	.probe		= rockchip_saradc_probe,
	.driver		= {
		.name	= "rockchip-saradc",
		.of_match_table = rockchip_saradc_match,
		.pm	= pm_sleep_ptr(&rockchip_saradc_pm_ops),
	},
};

module_platform_driver(rockchip_saradc_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko@sntech.de>");
MODULE_DESCRIPTION("Rockchip SARADC driver");
MODULE_LICENSE("GPL v2");
