/*
 * Freescale Vybrid vf610 DAC driver
 *
 * Copyright 2016 Toradex AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define VF610_DACx_STATCTRL		0x20

#define VF610_DAC_DACEN			BIT(15)
#define VF610_DAC_DACRFS		BIT(14)
#define VF610_DAC_LPEN			BIT(11)

#define VF610_DAC_DAT0(x)		((x) & 0xFFF)

enum vf610_conversion_mode_sel {
	VF610_DAC_CONV_HIGH_POWER,
	VF610_DAC_CONV_LOW_POWER,
};

struct vf610_dac {
	struct clk *clk;
	struct device *dev;
	enum vf610_conversion_mode_sel conv_mode;
	void __iomem *regs;
};

static void vf610_dac_init(struct vf610_dac *info)
{
	int val;

	info->conv_mode = VF610_DAC_CONV_LOW_POWER;
	val = VF610_DAC_DACEN | VF610_DAC_DACRFS |
		VF610_DAC_LPEN;
	writel(val, info->regs + VF610_DACx_STATCTRL);
}

static void vf610_dac_exit(struct vf610_dac *info)
{
	int val;

	val = readl(info->regs + VF610_DACx_STATCTRL);
	val &= ~VF610_DAC_DACEN;
	writel(val, info->regs + VF610_DACx_STATCTRL);
}

static int vf610_set_conversion_mode(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				unsigned int mode)
{
	struct vf610_dac *info = iio_priv(indio_dev);
	int val;

	mutex_lock(&indio_dev->mlock);
	info->conv_mode = mode;
	val = readl(info->regs + VF610_DACx_STATCTRL);
	if (mode)
		val |= VF610_DAC_LPEN;
	else
		val &= ~VF610_DAC_LPEN;
	writel(val, info->regs + VF610_DACx_STATCTRL);
	mutex_unlock(&indio_dev->mlock);

	return 0;
}

static int vf610_get_conversion_mode(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan)
{
	struct vf610_dac *info = iio_priv(indio_dev);

	return info->conv_mode;
}

static const char * const vf610_conv_modes[] = { "high-power", "low-power" };

static const struct iio_enum vf610_conversion_mode = {
	.items = vf610_conv_modes,
	.num_items = ARRAY_SIZE(vf610_conv_modes),
	.get = vf610_get_conversion_mode,
	.set = vf610_set_conversion_mode,
};

static const struct iio_chan_spec_ext_info vf610_ext_info[] = {
	IIO_ENUM("conversion_mode", IIO_SHARED_BY_DIR,
		&vf610_conversion_mode),
	{},
};

#define VF610_DAC_CHAN(_chan_type) { \
	.type = (_chan_type), \
	.output = 1, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.ext_info = vf610_ext_info, \
}

static const struct iio_chan_spec vf610_dac_iio_channels[] = {
	VF610_DAC_CHAN(IIO_VOLTAGE),
};

static int vf610_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2,
			long mask)
{
	struct vf610_dac *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = VF610_DAC_DAT0(readl(info->regs));
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * DACRFS is always 1 for valid reference and typical
		 * reference voltage as per Vybrid datasheet is 3.3V
		 * from section 9.1.2.1 of Vybrid datasheet
		 */
		*val = 3300 /* mV */;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		return -EINVAL;
	}
}

static int vf610_write_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int val, int val2,
			long mask)
{
	struct vf610_dac *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		writel(VF610_DAC_DAT0(val), info->regs);
		mutex_unlock(&indio_dev->mlock);
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct iio_info vf610_dac_iio_info = {
	.read_raw = &vf610_read_raw,
	.write_raw = &vf610_write_raw,
};

static const struct of_device_id vf610_dac_match[] = {
	{ .compatible = "fsl,vf610-dac", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vf610_dac_match);

static int vf610_dac_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct vf610_dac *info;
	struct resource *mem;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					sizeof(struct vf610_dac));
	if (!indio_dev) {
		dev_err(&pdev->dev, "Failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	info->clk = devm_clk_get(&pdev->dev, "dac");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "Failed getting clock, err = %ld\n",
			PTR_ERR(info->clk));
		return PTR_ERR(info->clk);
	}

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &vf610_dac_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = vf610_dac_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(vf610_dac_iio_channels);

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not prepare or enable the clock\n");
		return ret;
	}

	vf610_dac_init(info);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device\n");
		goto error_iio_device_register;
	}

	return 0;

error_iio_device_register:
	clk_disable_unprepare(info->clk);

	return ret;
}

static int vf610_dac_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct vf610_dac *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	vf610_dac_exit(info);
	clk_disable_unprepare(info->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int vf610_dac_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct vf610_dac *info = iio_priv(indio_dev);

	vf610_dac_exit(info);
	clk_disable_unprepare(info->clk);

	return 0;
}

static int vf610_dac_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct vf610_dac *info = iio_priv(indio_dev);
	int ret;

	ret = clk_prepare_enable(info->clk);
	if (ret)
		return ret;

	vf610_dac_init(info);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(vf610_dac_pm_ops, vf610_dac_suspend, vf610_dac_resume);

static struct platform_driver vf610_dac_driver = {
	.probe          = vf610_dac_probe,
	.remove         = vf610_dac_remove,
	.driver         = {
		.name   = "vf610-dac",
		.of_match_table = vf610_dac_match,
		.pm     = &vf610_dac_pm_ops,
	},
};
module_platform_driver(vf610_dac_driver);

MODULE_AUTHOR("Sanchayan Maity <sanchayan.maity@toradex.com>");
MODULE_DESCRIPTION("Freescale VF610 DAC driver");
MODULE_LICENSE("GPL v2");
