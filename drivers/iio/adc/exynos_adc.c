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
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

/* S3C/EXYNOS4412/5250 ADC_V1 registers definitions */
#define ADC_V1_CON(x)		((x) + 0x00)
#define ADC_V1_DLY(x)		((x) + 0x08)
#define ADC_V1_DATX(x)		((x) + 0x0C)
#define ADC_V1_INTCLR(x)	((x) + 0x18)
#define ADC_V1_MUX(x)		((x) + 0x1c)

/* S3C2410 ADC registers definitions */
#define ADC_S3C2410_MUX(x)	((x) + 0x18)

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

/* Bit definitions for S3C2410 ADC */
#define ADC_S3C2410_CON_SELMUX(x) (((x) & 7) << 3)
#define ADC_S3C2410_DATX_MASK	0x3FF
#define ADC_S3C2416_CON_RES_SEL	(1u << 3)

/* Bit definitions for ADC_V2 */
#define ADC_V2_CON1_SOFT_RESET	(1u << 2)

#define ADC_V2_CON2_OSEL	(1u << 10)
#define ADC_V2_CON2_ESEL	(1u << 9)
#define ADC_V2_CON2_HIGHF	(1u << 8)
#define ADC_V2_CON2_C_TIME(x)	(((x) & 7) << 4)
#define ADC_V2_CON2_ACH_SEL(x)	(((x) & 0xF) << 0)
#define ADC_V2_CON2_ACH_MASK	0xF

#define MAX_ADC_V2_CHANNELS		10
#define MAX_ADC_V1_CHANNELS		8
#define MAX_EXYNOS3250_ADC_CHANNELS	2

/* Bit definitions common for ADC_V1 and ADC_V2 */
#define ADC_CON_EN_START	(1u << 0)
#define ADC_CON_EN_START_MASK	(0x3 << 0)
#define ADC_DATX_MASK		0xFFF

#define EXYNOS_ADC_TIMEOUT	(msecs_to_jiffies(100))

struct exynos_adc {
	struct exynos_adc_data	*data;
	struct device		*dev;
	void __iomem		*regs;
	void __iomem		*enable_reg;
	struct clk		*clk;
	struct clk		*sclk;
	unsigned int		irq;
	struct regulator	*vdd;

	struct completion	completion;

	u32			value;
	unsigned int            version;
};

struct exynos_adc_data {
	int num_channels;
	bool needs_sclk;
	bool needs_adc_phy;
	u32 mask;

	void (*init_hw)(struct exynos_adc *info);
	void (*exit_hw)(struct exynos_adc *info);
	void (*clear_irq)(struct exynos_adc *info);
	void (*start_conv)(struct exynos_adc *info, unsigned long addr);
};

static void exynos_adc_unprepare_clk(struct exynos_adc *info)
{
	if (info->data->needs_sclk)
		clk_unprepare(info->sclk);
	clk_unprepare(info->clk);
}

static int exynos_adc_prepare_clk(struct exynos_adc *info)
{
	int ret;

	ret = clk_prepare(info->clk);
	if (ret) {
		dev_err(info->dev, "failed preparing adc clock: %d\n", ret);
		return ret;
	}

	if (info->data->needs_sclk) {
		ret = clk_prepare(info->sclk);
		if (ret) {
			clk_unprepare(info->clk);
			dev_err(info->dev,
				"failed preparing sclk_adc clock: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static void exynos_adc_disable_clk(struct exynos_adc *info)
{
	if (info->data->needs_sclk)
		clk_disable(info->sclk);
	clk_disable(info->clk);
}

static int exynos_adc_enable_clk(struct exynos_adc *info)
{
	int ret;

	ret = clk_enable(info->clk);
	if (ret) {
		dev_err(info->dev, "failed enabling adc clock: %d\n", ret);
		return ret;
	}

	if (info->data->needs_sclk) {
		ret = clk_enable(info->sclk);
		if (ret) {
			clk_disable(info->clk);
			dev_err(info->dev,
				"failed enabling sclk_adc clock: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static void exynos_adc_v1_init_hw(struct exynos_adc *info)
{
	u32 con1;

	if (info->data->needs_adc_phy)
		writel(1, info->enable_reg);

	/* set default prescaler values and Enable prescaler */
	con1 =  ADC_V1_CON_PRSCLV(49) | ADC_V1_CON_PRSCEN;

	/* Enable 12-bit ADC resolution */
	con1 |= ADC_V1_CON_RES;
	writel(con1, ADC_V1_CON(info->regs));
}

static void exynos_adc_v1_exit_hw(struct exynos_adc *info)
{
	u32 con;

	if (info->data->needs_adc_phy)
		writel(0, info->enable_reg);

	con = readl(ADC_V1_CON(info->regs));
	con |= ADC_V1_CON_STANDBY;
	writel(con, ADC_V1_CON(info->regs));
}

static void exynos_adc_v1_clear_irq(struct exynos_adc *info)
{
	writel(1, ADC_V1_INTCLR(info->regs));
}

static void exynos_adc_v1_start_conv(struct exynos_adc *info,
				     unsigned long addr)
{
	u32 con1;

	writel(addr, ADC_V1_MUX(info->regs));

	con1 = readl(ADC_V1_CON(info->regs));
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

static const struct exynos_adc_data exynos_adc_v1_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */
	.needs_adc_phy	= true,

	.init_hw	= exynos_adc_v1_init_hw,
	.exit_hw	= exynos_adc_v1_exit_hw,
	.clear_irq	= exynos_adc_v1_clear_irq,
	.start_conv	= exynos_adc_v1_start_conv,
};

static void exynos_adc_s3c2416_start_conv(struct exynos_adc *info,
					  unsigned long addr)
{
	u32 con1;

	/* Enable 12 bit ADC resolution */
	con1 = readl(ADC_V1_CON(info->regs));
	con1 |= ADC_S3C2416_CON_RES_SEL;
	writel(con1, ADC_V1_CON(info->regs));

	/* Select channel for S3C2416 */
	writel(addr, ADC_S3C2410_MUX(info->regs));

	con1 = readl(ADC_V1_CON(info->regs));
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

static struct exynos_adc_data const exynos_adc_s3c2416_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */

	.init_hw	= exynos_adc_v1_init_hw,
	.exit_hw	= exynos_adc_v1_exit_hw,
	.start_conv	= exynos_adc_s3c2416_start_conv,
};

static void exynos_adc_s3c2443_start_conv(struct exynos_adc *info,
					  unsigned long addr)
{
	u32 con1;

	/* Select channel for S3C2433 */
	writel(addr, ADC_S3C2410_MUX(info->regs));

	con1 = readl(ADC_V1_CON(info->regs));
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

static struct exynos_adc_data const exynos_adc_s3c2443_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_S3C2410_DATX_MASK, /* 10 bit ADC resolution */

	.init_hw	= exynos_adc_v1_init_hw,
	.exit_hw	= exynos_adc_v1_exit_hw,
	.start_conv	= exynos_adc_s3c2443_start_conv,
};

static void exynos_adc_s3c64xx_start_conv(struct exynos_adc *info,
					  unsigned long addr)
{
	u32 con1;

	con1 = readl(ADC_V1_CON(info->regs));
	con1 &= ~ADC_S3C2410_CON_SELMUX(0x7);
	con1 |= ADC_S3C2410_CON_SELMUX(addr);
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

static struct exynos_adc_data const exynos_adc_s3c24xx_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_S3C2410_DATX_MASK, /* 10 bit ADC resolution */

	.init_hw	= exynos_adc_v1_init_hw,
	.exit_hw	= exynos_adc_v1_exit_hw,
	.start_conv	= exynos_adc_s3c64xx_start_conv,
};

static struct exynos_adc_data const exynos_adc_s3c64xx_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */

	.init_hw	= exynos_adc_v1_init_hw,
	.exit_hw	= exynos_adc_v1_exit_hw,
	.clear_irq	= exynos_adc_v1_clear_irq,
	.start_conv	= exynos_adc_s3c64xx_start_conv,
};

static void exynos_adc_v2_init_hw(struct exynos_adc *info)
{
	u32 con1, con2;

	if (info->data->needs_adc_phy)
		writel(1, info->enable_reg);

	con1 = ADC_V2_CON1_SOFT_RESET;
	writel(con1, ADC_V2_CON1(info->regs));

	con2 = ADC_V2_CON2_OSEL | ADC_V2_CON2_ESEL |
		ADC_V2_CON2_HIGHF | ADC_V2_CON2_C_TIME(0);
	writel(con2, ADC_V2_CON2(info->regs));

	/* Enable interrupts */
	writel(1, ADC_V2_INT_EN(info->regs));
}

static void exynos_adc_v2_exit_hw(struct exynos_adc *info)
{
	u32 con;

	if (info->data->needs_adc_phy)
		writel(0, info->enable_reg);

	con = readl(ADC_V2_CON1(info->regs));
	con &= ~ADC_CON_EN_START;
	writel(con, ADC_V2_CON1(info->regs));
}

static void exynos_adc_v2_clear_irq(struct exynos_adc *info)
{
	writel(1, ADC_V2_INT_ST(info->regs));
}

static void exynos_adc_v2_start_conv(struct exynos_adc *info,
				     unsigned long addr)
{
	u32 con1, con2;

	con2 = readl(ADC_V2_CON2(info->regs));
	con2 &= ~ADC_V2_CON2_ACH_MASK;
	con2 |= ADC_V2_CON2_ACH_SEL(addr);
	writel(con2, ADC_V2_CON2(info->regs));

	con1 = readl(ADC_V2_CON1(info->regs));
	writel(con1 | ADC_CON_EN_START, ADC_V2_CON1(info->regs));
}

static const struct exynos_adc_data exynos_adc_v2_data = {
	.num_channels	= MAX_ADC_V2_CHANNELS,
	.mask		= ADC_DATX_MASK, /* 12 bit ADC resolution */
	.needs_adc_phy	= true,

	.init_hw	= exynos_adc_v2_init_hw,
	.exit_hw	= exynos_adc_v2_exit_hw,
	.clear_irq	= exynos_adc_v2_clear_irq,
	.start_conv	= exynos_adc_v2_start_conv,
};

static const struct exynos_adc_data exynos3250_adc_data = {
	.num_channels	= MAX_EXYNOS3250_ADC_CHANNELS,
	.mask		= ADC_DATX_MASK, /* 12 bit ADC resolution */
	.needs_sclk	= true,
	.needs_adc_phy	= true,

	.init_hw	= exynos_adc_v2_init_hw,
	.exit_hw	= exynos_adc_v2_exit_hw,
	.clear_irq	= exynos_adc_v2_clear_irq,
	.start_conv	= exynos_adc_v2_start_conv,
};

static const struct of_device_id exynos_adc_match[] = {
	{
		.compatible = "samsung,s3c2410-adc",
		.data = &exynos_adc_s3c24xx_data,
	}, {
		.compatible = "samsung,s3c2416-adc",
		.data = &exynos_adc_s3c2416_data,
	}, {
		.compatible = "samsung,s3c2440-adc",
		.data = &exynos_adc_s3c24xx_data,
	}, {
		.compatible = "samsung,s3c2443-adc",
		.data = &exynos_adc_s3c2443_data,
	}, {
		.compatible = "samsung,s3c6410-adc",
		.data = &exynos_adc_s3c64xx_data,
	}, {
		.compatible = "samsung,exynos-adc-v1",
		.data = &exynos_adc_v1_data,
	}, {
		.compatible = "samsung,exynos-adc-v2",
		.data = &exynos_adc_v2_data,
	}, {
		.compatible = "samsung,exynos3250-adc",
		.data = &exynos3250_adc_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_adc_match);

static struct exynos_adc_data *exynos_adc_get_data(struct platform_device *pdev)
{
	const struct of_device_id *match;

	match = of_match_node(exynos_adc_match, pdev->dev.of_node);
	return (struct exynos_adc_data *)match->data;
}

static int exynos_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct exynos_adc *info = iio_priv(indio_dev);
	unsigned long timeout;
	int ret;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	mutex_lock(&indio_dev->mlock);
	reinit_completion(&info->completion);

	/* Select the channel to be used and Trigger conversion */
	if (info->data->start_conv)
		info->data->start_conv(info, chan->address);

	timeout = wait_for_completion_timeout
			(&info->completion, EXYNOS_ADC_TIMEOUT);
	if (timeout == 0) {
		dev_warn(&indio_dev->dev, "Conversion timed out! Resetting\n");
		if (info->data->init_hw)
			info->data->init_hw(info);
		ret = -ETIMEDOUT;
	} else {
		*val = info->value;
		*val2 = 0;
		ret = IIO_VAL_INT;
	}

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static irqreturn_t exynos_adc_isr(int irq, void *dev_id)
{
	struct exynos_adc *info = (struct exynos_adc *)dev_id;
	u32 mask = info->data->mask;

	/* Read value */
	info->value = readl(ADC_V1_DATX(info->regs)) & mask;

	/* clear irq */
	if (info->data->clear_irq)
		info->data->clear_irq(info);

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

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct exynos_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);

	info->data = exynos_adc_get_data(pdev);
	if (!info->data) {
		dev_err(&pdev->dev, "failed getting exynos_adc_data\n");
		return -EINVAL;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);


	if (info->data->needs_adc_phy) {
		mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		info->enable_reg = devm_ioremap_resource(&pdev->dev, mem);
		if (IS_ERR(info->enable_reg))
			return PTR_ERR(info->enable_reg);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	info->irq = irq;
	info->dev = &pdev->dev;

	init_completion(&info->completion);

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
							PTR_ERR(info->clk));
		return PTR_ERR(info->clk);
	}

	if (info->data->needs_sclk) {
		info->sclk = devm_clk_get(&pdev->dev, "sclk");
		if (IS_ERR(info->sclk)) {
			dev_err(&pdev->dev,
				"failed getting sclk clock, err = %ld\n",
				PTR_ERR(info->sclk));
			return PTR_ERR(info->sclk);
		}
	}

	info->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(info->vdd)) {
		dev_err(&pdev->dev, "failed getting regulator, err = %ld\n",
							PTR_ERR(info->vdd));
		return PTR_ERR(info->vdd);
	}

	ret = regulator_enable(info->vdd);
	if (ret)
		return ret;

	ret = exynos_adc_prepare_clk(info);
	if (ret)
		goto err_disable_reg;

	ret = exynos_adc_enable_clk(info);
	if (ret)
		goto err_unprepare_clk;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &exynos_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = exynos_adc_iio_channels;
	indio_dev->num_channels = info->data->num_channels;

	ret = request_irq(info->irq, exynos_adc_isr,
					0, dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n",
							info->irq);
		goto err_disable_clk;
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto err_irq;

	if (info->data->init_hw)
		info->data->init_hw(info);

	ret = of_platform_populate(np, exynos_adc_match, NULL, &indio_dev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed adding child nodes\n");
		goto err_of_populate;
	}

	return 0;

err_of_populate:
	device_for_each_child(&indio_dev->dev, NULL,
				exynos_adc_remove_devices);
	iio_device_unregister(indio_dev);
err_irq:
	free_irq(info->irq, info);
err_disable_clk:
	if (info->data->exit_hw)
		info->data->exit_hw(info);
	exynos_adc_disable_clk(info);
err_unprepare_clk:
	exynos_adc_unprepare_clk(info);
err_disable_reg:
	regulator_disable(info->vdd);
	return ret;
}

static int exynos_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct exynos_adc *info = iio_priv(indio_dev);

	device_for_each_child(&indio_dev->dev, NULL,
				exynos_adc_remove_devices);
	iio_device_unregister(indio_dev);
	free_irq(info->irq, info);
	if (info->data->exit_hw)
		info->data->exit_hw(info);
	exynos_adc_disable_clk(info);
	exynos_adc_unprepare_clk(info);
	regulator_disable(info->vdd);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct exynos_adc *info = iio_priv(indio_dev);

	if (info->data->exit_hw)
		info->data->exit_hw(info);
	exynos_adc_disable_clk(info);
	regulator_disable(info->vdd);

	return 0;
}

static int exynos_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct exynos_adc *info = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(info->vdd);
	if (ret)
		return ret;

	ret = exynos_adc_enable_clk(info);
	if (ret)
		return ret;

	if (info->data->init_hw)
		info->data->init_hw(info);

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
