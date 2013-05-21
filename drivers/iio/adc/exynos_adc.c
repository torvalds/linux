/*
 *  exynos_adc.c - Support for ADC in EXYNOS SoCs
 *
 *  8 ~ 10 channel, 10/12-bit ADC
 *
 *  Copyright (C) 2013 Naveen Krishna Chatradhi <ch.naveen@samsung.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

enum adc_version {
	ADC_V1,
	ADC_V2
};

/* EXYNOS4412/5250 ADC_V1 registers definitions */
#define ADC_V1_CON(x)		((x) + 0x00)
#define ADC_V1_DLY(x)		((x) + 0x08)
#define ADC_V1_DATX(x)		((x) + 0x0C)
#define ADC_V1_INTCLR(x)	((x) + 0x18)
#define ADC_V1_MUX(x)		((x) + 0x1c)

/* Future ADC_V2 registers definitions */
#define ADC_V2_CON1(x)		((x) + 0x00)
#define ADC_V2_CON2(x)		((x) + 0x04)
#define ADC_V2_STAT(x)		((x) + 0x08)
#define ADC_V2_INT_EN(x)	((x) + 0x10)
#define ADC_V2_INT_ST(x)	((x) + 0x14)
#define ADC_V2_VER(x)		((x) + 0x20)

/* Bit definitions for ADC_V1 */
#define ADC_V1_CON_RES		(1u << 16)
#define ADC_V1_CON_PRSCEN	(1u << 14)
#define ADC_V1_CON_PRSCLV(x)	(((x) & 0xFF) << 6)
#define ADC_V1_CON_STANDBY	(1u << 2)

/* Bit definitions for ADC_V2 */
#define ADC_V2_CON1_SOFT_RESET	(1u << 2)

#define ADC_V2_CON2_OSEL	(1u << 10)
#define ADC_V2_CON2_ESEL	(1u << 9)
#define ADC_V2_CON2_HIGHF	(1u << 8)
#define ADC_V2_CON2_C_TIME(x)	(((x) & 7) << 4)
#define ADC_V2_CON2_ACH_SEL(x)	(((x) & 0xF) << 0)
#define ADC_V2_CON2_ACH_MASK	0xF

#define MAX_ADC_V2_CHANNELS	10
#define MAX_ADC_V1_CHANNELS	8

/* Bit definitions common for ADC_V1 and ADC_V2 */
#define ADC_CON_EN_START	(1u << 0)
#define ADC_DATX_MASK		0xFFF

#define EXYNOS_ADC_TIMEOUT	(msecs_to_jiffies(1000))

struct exynos_adc {
	void __iomem		*regs;
	void __iomem		*enable_reg;
	struct clk		*clk;
	unsigned int		irq;
	struct regulator	*vdd;

	struct completion	completion;

	u32			value;
	unsigned int            version;
};

static const struct of_device_id exynos_adc_match[] = {
	{ .compatible = "samsung,exynos-adc-v1", .data = (void *)ADC_V1 },
	{ .compatible = "samsung,exynos-adc-v2", .data = (void *)ADC_V2 },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_adc_match);

static inline unsigned int exynos_adc_get_version(struct platform_device *pdev)
{
	const struct of_device_id *match;

	match = of_match_node(exynos_adc_match, pdev->dev.of_node);
	return (unsigned int)match->data;
}

static int exynos_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct exynos_adc *info = iio_priv(indio_dev);
	unsigned long timeout;
	u32 con1, con2;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);

	/* Select the channel to be used and Trigger conversion */
	if (info->version == ADC_V2) {
		con2 = readl(ADC_V2_CON2(info->regs));
		con2 &= ~ADC_V2_CON2_ACH_MASK;
		con2 |= ADC_V2_CON2_ACH_SEL(chan->address);
		writel(con2, ADC_V2_CON2(info->regs));

		con1 = readl(ADC_V2_CON1(info->regs));
		writel(con1 | ADC_CON_EN_START,
				ADC_V2_CON1(info->regs));
	} else {
		writel(chan->address, ADC_V1_MUX(info->regs));

		con1 = readl(ADC_V1_CON(info->regs));
		writel(con1 | ADC_CON_EN_START,
				ADC_V1_CON(info->regs));
	}

	timeout = wait_for_completion_interruptible_timeout
			(&info->completion, EXYNOS_ADC_TIMEOUT);
	*val = info->value;

	mutex_unlock(&indio_dev->mlock);

	if (timeout == 0)
		return -ETIMEDOUT;

	return IIO_VAL_INT;
}

static irqreturn_t exynos_adc_isr(int irq, void *dev_id)
{
	struct exynos_adc *info = (struct exynos_adc *)dev_id;

	/* Read value */
	info->value = readl(ADC_V1_DATX(info->regs)) &
						ADC_DATX_MASK;
	/* clear irq */
	if (info->version == ADC_V2)
		writel(1, ADC_V2_INT_ST(info->regs));
	else
		writel(1, ADC_V1_INTCLR(info->regs));

	complete(&info->completion);

	return IRQ_HANDLED;
}

static int exynos_adc_reg_access(struct iio_dev *indio_dev,
			      unsigned reg, unsigned writeval,
			      unsigned *readval)
{
	struct exynos_adc *info = iio_priv(indio_dev);

	if (readval == NULL)
		return -EINVAL;

	*readval = readl(info->regs + reg);

	return 0;
}

static const struct iio_info exynos_adc_iio_info = {
	.read_raw = &exynos_read_raw,
	.debugfs_reg_access = &exynos_adc_reg_access,
	.driver_module = THIS_MODULE,
};

#define ADC_CHANNEL(_index, _id) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.address = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.datasheet_name = _id,				\
}

static const struct iio_chan_spec exynos_adc_iio_channels[] = {
	ADC_CHANNEL(0, "adc0"),
	ADC_CHANNEL(1, "adc1"),
	ADC_CHANNEL(2, "adc2"),
	ADC_CHANNEL(3, "adc3"),
	ADC_CHANNEL(4, "adc4"),
	ADC_CHANNEL(5, "adc5"),
	ADC_CHANNEL(6, "adc6"),
	ADC_CHANNEL(7, "adc7"),
	ADC_CHANNEL(8, "adc8"),
	ADC_CHANNEL(9, "adc9"),
};

static int exynos_adc_remove_devices(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static void exynos_adc_hw_init(struct exynos_adc *info)
{
	u32 con1, con2;

	if (info->version == ADC_V2) {
		con1 = ADC_V2_CON1_SOFT_RESET;
		writel(con1, ADC_V2_CON1(info->regs));

		con2 = ADC_V2_CON2_OSEL | ADC_V2_CON2_ESEL |
			ADC_V2_CON2_HIGHF | ADC_V2_CON2_C_TIME(0);
		writel(con2, ADC_V2_CON2(info->regs));

		/* Enable interrupts */
		writel(1, ADC_V2_INT_EN(info->regs));
	} else {
		/* set default prescaler values and Enable prescaler */
		con1 =  ADC_V1_CON_PRSCLV(49) | ADC_V1_CON_PRSCEN;

		/* Enable 12-bit ADC resolution */
		con1 |= ADC_V1_CON_RES;
		writel(con1, ADC_V1_CON(info->regs));
	}
}

static int exynos_adc_probe(struct platform_device *pdev)
{
	struct exynos_adc *info = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct iio_dev *indio_dev = NULL;
	struct resource	*mem;
	int ret = -ENODEV;
	int irq;

	if (!np)
		return ret;

	indio_dev = iio_device_alloc(sizeof(struct exynos_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_request_and_ioremap(&pdev->dev, mem);
	if (!info->regs) {
		ret = -ENOMEM;
		goto err_iio;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	info->enable_reg = devm_request_and_ioremap(&pdev->dev, mem);
	if (!info->enable_reg) {
		ret = -ENOMEM;
		goto err_iio;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		ret = irq;
		goto err_iio;
	}

	info->irq = irq;

	init_completion(&info->completion);

	ret = request_irq(info->irq, exynos_adc_isr,
					0, dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n",
							info->irq);
		goto err_iio;
	}

	writel(1, info->enable_reg);

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
							PTR_ERR(info->clk));
		ret = PTR_ERR(info->clk);
		goto err_irq;
	}

	info->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(info->vdd)) {
		dev_err(&pdev->dev, "failed getting regulator, err = %ld\n",
							PTR_ERR(info->vdd));
		ret = PTR_ERR(info->vdd);
		goto err_irq;
	}

	info->version = exynos_adc_get_version(pdev);

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &exynos_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = exynos_adc_iio_channels;

	if (info->version == ADC_V1)
		indio_dev->num_channels = MAX_ADC_V1_CHANNELS;
	else
		indio_dev->num_channels = MAX_ADC_V2_CHANNELS;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_irq;

	ret = regulator_enable(info->vdd);
	if (ret)
		goto err_iio_dev;

	clk_prepare_enable(info->clk);

	exynos_adc_hw_init(info);

	ret = of_platform_populate(np, exynos_adc_match, NULL, &pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed adding child nodes\n");
		goto err_of_populate;
	}

	return 0;

err_of_populate:
	device_for_each_child(&pdev->dev, NULL,
				exynos_adc_remove_devices);
	regulator_disable(info->vdd);
	clk_disable_unprepare(info->clk);
err_iio_dev:
	iio_device_unregister(indio_dev);
err_irq:
	free_irq(info->irq, info);
err_iio:
	iio_device_free(indio_dev);
	return ret;
}

static int exynos_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct exynos_adc *info = iio_priv(indio_dev);

	device_for_each_child(&pdev->dev, NULL,
				exynos_adc_remove_devices);
	regulator_disable(info->vdd);
	clk_disable_unprepare(info->clk);
	writel(0, info->enable_reg);
	iio_device_unregister(indio_dev);
	free_irq(info->irq, info);
	iio_device_free(indio_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_adc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_adc *info = platform_get_drvdata(pdev);
	u32 con;

	if (info->version == ADC_V2) {
		con = readl(ADC_V2_CON1(info->regs));
		con &= ~ADC_CON_EN_START;
		writel(con, ADC_V2_CON1(info->regs));
	} else {
		con = readl(ADC_V1_CON(info->regs));
		con |= ADC_V1_CON_STANDBY;
		writel(con, ADC_V1_CON(info->regs));
	}

	clk_disable_unprepare(info->clk);
	writel(0, info->enable_reg);
	regulator_disable(info->vdd);

	return 0;
}

static int exynos_adc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_adc *info = platform_get_drvdata(pdev);
	int ret;

	ret = regulator_enable(info->vdd);
	if (ret)
		return ret;

	writel(1, info->enable_reg);
	clk_prepare_enable(info->clk);

	exynos_adc_hw_init(info);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(exynos_adc_pm_ops,
			exynos_adc_suspend,
			exynos_adc_resume);

static struct platform_driver exynos_adc_driver = {
	.probe		= exynos_adc_probe,
	.remove		= exynos_adc_remove,
	.driver		= {
		.name	= "exynos-adc",
		.owner	= THIS_MODULE,
		.of_match_table = exynos_adc_match,
		.pm	= &exynos_adc_pm_ops,
	},
};

module_platform_driver(exynos_adc_driver);

MODULE_AUTHOR("Naveen Krishna Chatradhi <ch.naveen@samsung.com>");
MODULE_DESCRIPTION("Samsung EXYNOS5 ADC driver");
MODULE_LICENSE("GPL v2");
