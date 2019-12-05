// SPDX-License-Identifier: GPL-2.0+
/*
 * Renesas R-Car GyroADC driver
 *
 * Copyright 2016 Marek Vasut <marek.vasut@gmail.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>

#define DRIVER_NAME				"rcar-gyroadc"

/* GyroADC registers. */
#define RCAR_GYROADC_MODE_SELECT		0x00
#define RCAR_GYROADC_MODE_SELECT_1_MB88101A	0x0
#define RCAR_GYROADC_MODE_SELECT_2_ADCS7476	0x1
#define RCAR_GYROADC_MODE_SELECT_3_MAX1162	0x3

#define RCAR_GYROADC_START_STOP			0x04
#define RCAR_GYROADC_START_STOP_START		BIT(0)

#define RCAR_GYROADC_CLOCK_LENGTH		0x08
#define RCAR_GYROADC_1_25MS_LENGTH		0x0c

#define RCAR_GYROADC_REALTIME_DATA(ch)		(0x10 + ((ch) * 4))
#define RCAR_GYROADC_100MS_ADDED_DATA(ch)	(0x30 + ((ch) * 4))
#define RCAR_GYROADC_10MS_AVG_DATA(ch)		(0x50 + ((ch) * 4))

#define RCAR_GYROADC_FIFO_STATUS		0x70
#define RCAR_GYROADC_FIFO_STATUS_EMPTY(ch)	BIT(0 + (4 * (ch)))
#define RCAR_GYROADC_FIFO_STATUS_FULL(ch)	BIT(1 + (4 * (ch)))
#define RCAR_GYROADC_FIFO_STATUS_ERROR(ch)	BIT(2 + (4 * (ch)))

#define RCAR_GYROADC_INTR			0x74
#define RCAR_GYROADC_INTR_INT			BIT(0)

#define RCAR_GYROADC_INTENR			0x78
#define RCAR_GYROADC_INTENR_INTEN		BIT(0)

#define RCAR_GYROADC_SAMPLE_RATE		800	/* Hz */

#define RCAR_GYROADC_RUNTIME_PM_DELAY_MS	2000

enum rcar_gyroadc_model {
	RCAR_GYROADC_MODEL_DEFAULT,
	RCAR_GYROADC_MODEL_R8A7792,
};

struct rcar_gyroadc {
	struct device			*dev;
	void __iomem			*regs;
	struct clk			*clk;
	struct regulator		*vref[8];
	unsigned int			num_channels;
	enum rcar_gyroadc_model		model;
	unsigned int			mode;
	unsigned int			sample_width;
};

static void rcar_gyroadc_hw_init(struct rcar_gyroadc *priv)
{
	const unsigned long clk_mhz = clk_get_rate(priv->clk) / 1000000;
	const unsigned long clk_mul =
		(priv->mode == RCAR_GYROADC_MODE_SELECT_1_MB88101A) ? 10 : 5;
	unsigned long clk_len = clk_mhz * clk_mul;

	/*
	 * According to the R-Car Gen2 datasheet Rev. 1.01, Sept 08 2014,
	 * page 77-7, clock length must be even number. If it's odd number,
	 * add one.
	 */
	if (clk_len & 1)
		clk_len++;

	/* Stop the GyroADC. */
	writel(0, priv->regs + RCAR_GYROADC_START_STOP);

	/* Disable IRQ on V2H. */
	if (priv->model == RCAR_GYROADC_MODEL_R8A7792)
		writel(0, priv->regs + RCAR_GYROADC_INTENR);

	/* Set mode and timing. */
	writel(priv->mode, priv->regs + RCAR_GYROADC_MODE_SELECT);
	writel(clk_len, priv->regs + RCAR_GYROADC_CLOCK_LENGTH);
	writel(clk_mhz * 1250, priv->regs + RCAR_GYROADC_1_25MS_LENGTH);
}

static void rcar_gyroadc_hw_start(struct rcar_gyroadc *priv)
{
	/* Start sampling. */
	writel(RCAR_GYROADC_START_STOP_START,
	       priv->regs + RCAR_GYROADC_START_STOP);

	/*
	 * Wait for the first conversion to complete. This is longer than
	 * the 1.25 mS in the datasheet because 1.25 mS is not enough for
	 * the hardware to deliver the first sample and the hardware does
	 * then return zeroes instead of valid data.
	 */
	mdelay(3);
}

static void rcar_gyroadc_hw_stop(struct rcar_gyroadc *priv)
{
	/* Stop the GyroADC. */
	writel(0, priv->regs + RCAR_GYROADC_START_STOP);
}

#define RCAR_GYROADC_CHAN(_idx) {				\
	.type			= IIO_VOLTAGE,			\
	.indexed		= 1,				\
	.channel		= (_idx),			\
	.info_mask_separate	= BIT(IIO_CHAN_INFO_RAW) |	\
				  BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
}

static const struct iio_chan_spec rcar_gyroadc_iio_channels_1[] = {
	RCAR_GYROADC_CHAN(0),
	RCAR_GYROADC_CHAN(1),
	RCAR_GYROADC_CHAN(2),
	RCAR_GYROADC_CHAN(3),
};

static const struct iio_chan_spec rcar_gyroadc_iio_channels_2[] = {
	RCAR_GYROADC_CHAN(0),
	RCAR_GYROADC_CHAN(1),
	RCAR_GYROADC_CHAN(2),
	RCAR_GYROADC_CHAN(3),
	RCAR_GYROADC_CHAN(4),
	RCAR_GYROADC_CHAN(5),
	RCAR_GYROADC_CHAN(6),
	RCAR_GYROADC_CHAN(7),
};

static const struct iio_chan_spec rcar_gyroadc_iio_channels_3[] = {
	RCAR_GYROADC_CHAN(0),
	RCAR_GYROADC_CHAN(1),
	RCAR_GYROADC_CHAN(2),
	RCAR_GYROADC_CHAN(3),
	RCAR_GYROADC_CHAN(4),
	RCAR_GYROADC_CHAN(5),
	RCAR_GYROADC_CHAN(6),
	RCAR_GYROADC_CHAN(7),
};

static int rcar_gyroadc_set_power(struct rcar_gyroadc *priv, bool on)
{
	struct device *dev = priv->dev;
	int ret;

	if (on) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0)
			pm_runtime_put_noidle(dev);
	} else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	return ret;
}

static int rcar_gyroadc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct rcar_gyroadc *priv = iio_priv(indio_dev);
	struct regulator *consumer;
	unsigned int datareg = RCAR_GYROADC_REALTIME_DATA(chan->channel);
	unsigned int vref;
	int ret;

	/*
	 * MB88101 is special in that it has only single regulator for
	 * all four channels.
	 */
	if (priv->mode == RCAR_GYROADC_MODE_SELECT_1_MB88101A)
		consumer = priv->vref[0];
	else
		consumer = priv->vref[chan->channel];

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_VOLTAGE)
			return -EINVAL;

		/* Channel not connected. */
		if (!consumer)
			return -EINVAL;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = rcar_gyroadc_set_power(priv, true);
		if (ret < 0) {
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}

		*val = readl(priv->regs + datareg);
		*val &= BIT(priv->sample_width) - 1;

		ret = rcar_gyroadc_set_power(priv, false);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/* Channel not connected. */
		if (!consumer)
			return -EINVAL;

		vref = regulator_get_voltage(consumer);
		*val = vref / 1000;
		*val2 = 1 << priv->sample_width;

		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = RCAR_GYROADC_SAMPLE_RATE;

		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int rcar_gyroadc_reg_access(struct iio_dev *indio_dev,
				   unsigned int reg, unsigned int writeval,
				   unsigned int *readval)
{
	struct rcar_gyroadc *priv = iio_priv(indio_dev);
	unsigned int maxreg = RCAR_GYROADC_FIFO_STATUS;

	if (readval == NULL)
		return -EINVAL;

	if (reg % 4)
		return -EINVAL;

	/* Handle the V2H case with extra interrupt block. */
	if (priv->model == RCAR_GYROADC_MODEL_R8A7792)
		maxreg = RCAR_GYROADC_INTENR;

	if (reg > maxreg)
		return -EINVAL;

	*readval = readl(priv->regs + reg);

	return 0;
}

static const struct iio_info rcar_gyroadc_iio_info = {
	.read_raw		= rcar_gyroadc_read_raw,
	.debugfs_reg_access	= rcar_gyroadc_reg_access,
};

static const struct of_device_id rcar_gyroadc_match[] = {
	{
		/* R-Car compatible GyroADC */
		.compatible	= "renesas,rcar-gyroadc",
		.data		= (void *)RCAR_GYROADC_MODEL_DEFAULT,
	}, {
		/* R-Car V2H specialty with interrupt registers. */
		.compatible	= "renesas,r8a7792-gyroadc",
		.data		= (void *)RCAR_GYROADC_MODEL_R8A7792,
	}, {
		/* sentinel */
	}
};

MODULE_DEVICE_TABLE(of, rcar_gyroadc_match);

static const struct of_device_id rcar_gyroadc_child_match[] = {
	/* Mode 1 ADCs */
	{
		.compatible	= "fujitsu,mb88101a",
		.data		= (void *)RCAR_GYROADC_MODE_SELECT_1_MB88101A,
	},
	/* Mode 2 ADCs */
	{
		.compatible	= "ti,adcs7476",
		.data		= (void *)RCAR_GYROADC_MODE_SELECT_2_ADCS7476,
	}, {
		.compatible	= "ti,adc121",
		.data		= (void *)RCAR_GYROADC_MODE_SELECT_2_ADCS7476,
	}, {
		.compatible	= "adi,ad7476",
		.data		= (void *)RCAR_GYROADC_MODE_SELECT_2_ADCS7476,
	},
	/* Mode 3 ADCs */
	{
		.compatible	= "maxim,max1162",
		.data		= (void *)RCAR_GYROADC_MODE_SELECT_3_MAX1162,
	}, {
		.compatible	= "maxim,max11100",
		.data		= (void *)RCAR_GYROADC_MODE_SELECT_3_MAX1162,
	},
	{ /* sentinel */ }
};

static int rcar_gyroadc_parse_subdevs(struct iio_dev *indio_dev)
{
	const struct of_device_id *of_id;
	const struct iio_chan_spec *channels;
	struct rcar_gyroadc *priv = iio_priv(indio_dev);
	struct device *dev = priv->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct regulator *vref;
	unsigned int reg;
	unsigned int adcmode = -1, childmode;
	unsigned int sample_width;
	unsigned int num_channels;
	int ret, first = 1;

	for_each_child_of_node(np, child) {
		of_id = of_match_node(rcar_gyroadc_child_match, child);
		if (!of_id) {
			dev_err(dev, "Ignoring unsupported ADC \"%pOFn\".",
				child);
			continue;
		}

		childmode = (uintptr_t)of_id->data;
		switch (childmode) {
		case RCAR_GYROADC_MODE_SELECT_1_MB88101A:
			sample_width = 12;
			channels = rcar_gyroadc_iio_channels_1;
			num_channels = ARRAY_SIZE(rcar_gyroadc_iio_channels_1);
			break;
		case RCAR_GYROADC_MODE_SELECT_2_ADCS7476:
			sample_width = 15;
			channels = rcar_gyroadc_iio_channels_2;
			num_channels = ARRAY_SIZE(rcar_gyroadc_iio_channels_2);
			break;
		case RCAR_GYROADC_MODE_SELECT_3_MAX1162:
			sample_width = 16;
			channels = rcar_gyroadc_iio_channels_3;
			num_channels = ARRAY_SIZE(rcar_gyroadc_iio_channels_3);
			break;
		default:
			return -EINVAL;
		}

		/*
		 * MB88101 is special in that it's only a single chip taking
		 * up all the CHS lines. Thus, the DT binding is also special
		 * and has no reg property. If we run into such ADC, handle
		 * it here.
		 */
		if (childmode == RCAR_GYROADC_MODE_SELECT_1_MB88101A) {
			reg = 0;
		} else {
			ret = of_property_read_u32(child, "reg", &reg);
			if (ret) {
				dev_err(dev,
					"Failed to get child reg property of ADC \"%pOFn\".\n",
					child);
				return ret;
			}

			/* Channel number is too high. */
			if (reg >= num_channels) {
				dev_err(dev,
					"Only %i channels supported with %pOFn, but reg = <%i>.\n",
					num_channels, child, reg);
				return -EINVAL;
			}
		}

		/* Child node selected different mode than the rest. */
		if (!first && (adcmode != childmode)) {
			dev_err(dev,
				"Channel %i uses different ADC mode than the rest.\n",
				reg);
			return -EINVAL;
		}

		/* Channel is valid, grab the regulator. */
		dev->of_node = child;
		vref = devm_regulator_get(dev, "vref");
		dev->of_node = np;
		if (IS_ERR(vref)) {
			dev_dbg(dev, "Channel %i 'vref' supply not connected.\n",
				reg);
			return PTR_ERR(vref);
		}

		priv->vref[reg] = vref;

		if (!first)
			continue;

		/* First child node which passed sanity tests. */
		adcmode = childmode;
		first = 0;

		priv->num_channels = num_channels;
		priv->mode = childmode;
		priv->sample_width = sample_width;

		indio_dev->channels = channels;
		indio_dev->num_channels = num_channels;

		/*
		 * MB88101 is special and we only have one such device
		 * attached to the GyroADC at a time, so if we found it,
		 * we can stop parsing here.
		 */
		if (childmode == RCAR_GYROADC_MODE_SELECT_1_MB88101A)
			break;
	}

	if (first) {
		dev_err(dev, "No valid ADC channels found, aborting.\n");
		return -EINVAL;
	}

	return 0;
}

static void rcar_gyroadc_deinit_supplies(struct iio_dev *indio_dev)
{
	struct rcar_gyroadc *priv = iio_priv(indio_dev);
	unsigned int i;

	for (i = 0; i < priv->num_channels; i++) {
		if (!priv->vref[i])
			continue;

		regulator_disable(priv->vref[i]);
	}
}

static int rcar_gyroadc_init_supplies(struct iio_dev *indio_dev)
{
	struct rcar_gyroadc *priv = iio_priv(indio_dev);
	struct device *dev = priv->dev;
	unsigned int i;
	int ret;

	for (i = 0; i < priv->num_channels; i++) {
		if (!priv->vref[i])
			continue;

		ret = regulator_enable(priv->vref[i]);
		if (ret) {
			dev_err(dev, "Failed to enable regulator %i (ret=%i)\n",
				i, ret);
			goto err;
		}
	}

	return 0;

err:
	rcar_gyroadc_deinit_supplies(indio_dev);
	return ret;
}

static int rcar_gyroadc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rcar_gyroadc *priv;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->dev = dev;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	priv->clk = devm_clk_get(dev, "fck");
	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get IF clock (ret=%i)\n", ret);
		return ret;
	}

	ret = rcar_gyroadc_parse_subdevs(indio_dev);
	if (ret)
		return ret;

	ret = rcar_gyroadc_init_supplies(indio_dev);
	if (ret)
		return ret;

	priv->model = (enum rcar_gyroadc_model)
		of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = DRIVER_NAME;
	indio_dev->dev.parent = dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &rcar_gyroadc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Could not prepare or enable the IF clock.\n");
		goto err_clk_if_enable;
	}

	pm_runtime_set_autosuspend_delay(dev, RCAR_GYROADC_RUNTIME_PM_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	pm_runtime_get_sync(dev);
	rcar_gyroadc_hw_init(priv);
	rcar_gyroadc_hw_start(priv);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "Couldn't register IIO device.\n");
		goto err_iio_device_register;
	}

	pm_runtime_put_sync(dev);

	return 0;

err_iio_device_register:
	rcar_gyroadc_hw_stop(priv);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	clk_disable_unprepare(priv->clk);
err_clk_if_enable:
	rcar_gyroadc_deinit_supplies(indio_dev);

	return ret;
}

static int rcar_gyroadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct rcar_gyroadc *priv = iio_priv(indio_dev);
	struct device *dev = priv->dev;

	iio_device_unregister(indio_dev);
	pm_runtime_get_sync(dev);
	rcar_gyroadc_hw_stop(priv);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	clk_disable_unprepare(priv->clk);
	rcar_gyroadc_deinit_supplies(indio_dev);

	return 0;
}

#if defined(CONFIG_PM)
static int rcar_gyroadc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rcar_gyroadc *priv = iio_priv(indio_dev);

	rcar_gyroadc_hw_stop(priv);

	return 0;
}

static int rcar_gyroadc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rcar_gyroadc *priv = iio_priv(indio_dev);

	rcar_gyroadc_hw_start(priv);

	return 0;
}
#endif

static const struct dev_pm_ops rcar_gyroadc_pm_ops = {
	SET_RUNTIME_PM_OPS(rcar_gyroadc_suspend, rcar_gyroadc_resume, NULL)
};

static struct platform_driver rcar_gyroadc_driver = {
	.probe          = rcar_gyroadc_probe,
	.remove         = rcar_gyroadc_remove,
	.driver         = {
		.name		= DRIVER_NAME,
		.of_match_table	= rcar_gyroadc_match,
		.pm		= &rcar_gyroadc_pm_ops,
	},
};

module_platform_driver(rcar_gyroadc_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("Renesas R-Car GyroADC driver");
MODULE_LICENSE("GPL");
