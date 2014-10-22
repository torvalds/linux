/*
 * Freescale Vybrid vf610 ADC driver
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
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
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/driver.h>

/* This will be the driver name the kernel reports */
#define DRIVER_NAME "vf610-adc"

/* Vybrid/IMX ADC registers */
#define VF610_REG_ADC_HC0		0x00
#define VF610_REG_ADC_HC1		0x04
#define VF610_REG_ADC_HS		0x08
#define VF610_REG_ADC_R0		0x0c
#define VF610_REG_ADC_R1		0x10
#define VF610_REG_ADC_CFG		0x14
#define VF610_REG_ADC_GC		0x18
#define VF610_REG_ADC_GS		0x1c
#define VF610_REG_ADC_CV		0x20
#define VF610_REG_ADC_OFS		0x24
#define VF610_REG_ADC_CAL		0x28
#define VF610_REG_ADC_PCTL		0x30

/* Configuration register field define */
#define VF610_ADC_MODE_BIT8		0x00
#define VF610_ADC_MODE_BIT10		0x04
#define VF610_ADC_MODE_BIT12		0x08
#define VF610_ADC_MODE_MASK		0x0c
#define VF610_ADC_BUSCLK2_SEL		0x01
#define VF610_ADC_ALTCLK_SEL		0x02
#define VF610_ADC_ADACK_SEL		0x03
#define VF610_ADC_ADCCLK_MASK		0x03
#define VF610_ADC_CLK_DIV2		0x20
#define VF610_ADC_CLK_DIV4		0x40
#define VF610_ADC_CLK_DIV8		0x60
#define VF610_ADC_CLK_MASK		0x60
#define VF610_ADC_ADLSMP_LONG		0x10
#define VF610_ADC_ADSTS_MASK		0x300
#define VF610_ADC_ADLPC_EN		0x80
#define VF610_ADC_ADHSC_EN		0x400
#define VF610_ADC_REFSEL_VALT		0x100
#define VF610_ADC_REFSEL_VBG		0x1000
#define VF610_ADC_ADTRG_HARD		0x2000
#define VF610_ADC_AVGS_8		0x4000
#define VF610_ADC_AVGS_16		0x8000
#define VF610_ADC_AVGS_32		0xC000
#define VF610_ADC_AVGS_MASK		0xC000
#define VF610_ADC_OVWREN		0x10000

/* General control register field define */
#define VF610_ADC_ADACKEN		0x1
#define VF610_ADC_DMAEN			0x2
#define VF610_ADC_ACREN			0x4
#define VF610_ADC_ACFGT			0x8
#define VF610_ADC_ACFE			0x10
#define VF610_ADC_AVGEN			0x20
#define VF610_ADC_ADCON			0x40
#define VF610_ADC_CAL			0x80

/* Other field define */
#define VF610_ADC_ADCHC(x)		((x) & 0xF)
#define VF610_ADC_AIEN			(0x1 << 7)
#define VF610_ADC_CONV_DISABLE		0x1F
#define VF610_ADC_HS_COCO0		0x1
#define VF610_ADC_CALF			0x2
#define VF610_ADC_TIMEOUT		msecs_to_jiffies(100)

enum clk_sel {
	VF610_ADCIOC_BUSCLK_SET,
	VF610_ADCIOC_ALTCLK_SET,
	VF610_ADCIOC_ADACK_SET,
};

enum vol_ref {
	VF610_ADCIOC_VR_VREF_SET,
	VF610_ADCIOC_VR_VALT_SET,
	VF610_ADCIOC_VR_VBG_SET,
};

enum average_sel {
	VF610_ADC_SAMPLE_1,
	VF610_ADC_SAMPLE_4,
	VF610_ADC_SAMPLE_8,
	VF610_ADC_SAMPLE_16,
	VF610_ADC_SAMPLE_32,
};

struct vf610_adc_feature {
	enum clk_sel	clk_sel;
	enum vol_ref	vol_ref;

	int	clk_div;
	int     sample_rate;
	int	res_mode;

	bool	lpm;
	bool	calibration;
	bool	ovwren;
};

struct vf610_adc {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;

	u32 vref_uv;
	u32 value;
	struct regulator *vref;
	struct vf610_adc_feature adc_feature;

	struct completion completion;
};

#define VF610_ADC_CHAN(_idx, _chan_type) {			\
	.type = (_chan_type),					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

static const struct iio_chan_spec vf610_adc_iio_channels[] = {
	VF610_ADC_CHAN(0, IIO_VOLTAGE),
	VF610_ADC_CHAN(1, IIO_VOLTAGE),
	VF610_ADC_CHAN(2, IIO_VOLTAGE),
	VF610_ADC_CHAN(3, IIO_VOLTAGE),
	VF610_ADC_CHAN(4, IIO_VOLTAGE),
	VF610_ADC_CHAN(5, IIO_VOLTAGE),
	VF610_ADC_CHAN(6, IIO_VOLTAGE),
	VF610_ADC_CHAN(7, IIO_VOLTAGE),
	VF610_ADC_CHAN(8, IIO_VOLTAGE),
	VF610_ADC_CHAN(9, IIO_VOLTAGE),
	VF610_ADC_CHAN(10, IIO_VOLTAGE),
	VF610_ADC_CHAN(11, IIO_VOLTAGE),
	VF610_ADC_CHAN(12, IIO_VOLTAGE),
	VF610_ADC_CHAN(13, IIO_VOLTAGE),
	VF610_ADC_CHAN(14, IIO_VOLTAGE),
	VF610_ADC_CHAN(15, IIO_VOLTAGE),
	/* sentinel */
};

/*
 * ADC sample frequency, unit is ADCK cycles.
 * ADC clk source is ipg clock, which is the same as bus clock.
 *
 * ADC conversion time = SFCAdder + AverageNum x (BCT + LSTAdder)
 * SFCAdder: fixed to 6 ADCK cycles
 * AverageNum: 1, 4, 8, 16, 32 samples for hardware average.
 * BCT (Base Conversion Time): fixed to 25 ADCK cycles for 12 bit mode
 * LSTAdder(Long Sample Time): fixed to 3 ADCK cycles
 *
 * By default, enable 12 bit resolution mode, clock source
 * set to ipg clock, So get below frequency group:
 */
static const u32 vf610_sample_freq_avail[5] =
{1941176, 559332, 286957, 145374, 73171};

static inline void vf610_adc_cfg_init(struct vf610_adc *info)
{
	/* set default Configuration for ADC controller */
	info->adc_feature.clk_sel = VF610_ADCIOC_BUSCLK_SET;
	info->adc_feature.vol_ref = VF610_ADCIOC_VR_VREF_SET;

	info->adc_feature.calibration = true;
	info->adc_feature.ovwren = true;

	info->adc_feature.clk_div = 1;
	info->adc_feature.res_mode = 12;
	info->adc_feature.sample_rate = 1;
	info->adc_feature.lpm = true;
}

static void vf610_adc_cfg_post_set(struct vf610_adc *info)
{
	struct vf610_adc_feature *adc_feature = &info->adc_feature;
	int cfg_data = 0;
	int gc_data = 0;

	switch (adc_feature->clk_sel) {
	case VF610_ADCIOC_ALTCLK_SET:
		cfg_data |= VF610_ADC_ALTCLK_SEL;
		break;
	case VF610_ADCIOC_ADACK_SET:
		cfg_data |= VF610_ADC_ADACK_SEL;
		break;
	default:
		break;
	}

	/* low power set for calibration */
	cfg_data |= VF610_ADC_ADLPC_EN;

	/* enable high speed for calibration */
	cfg_data |= VF610_ADC_ADHSC_EN;

	/* voltage reference */
	switch (adc_feature->vol_ref) {
	case VF610_ADCIOC_VR_VREF_SET:
		break;
	case VF610_ADCIOC_VR_VALT_SET:
		cfg_data |= VF610_ADC_REFSEL_VALT;
		break;
	case VF610_ADCIOC_VR_VBG_SET:
		cfg_data |= VF610_ADC_REFSEL_VBG;
		break;
	default:
		dev_err(info->dev, "error voltage reference\n");
	}

	/* data overwrite enable */
	if (adc_feature->ovwren)
		cfg_data |= VF610_ADC_OVWREN;

	writel(cfg_data, info->regs + VF610_REG_ADC_CFG);
	writel(gc_data, info->regs + VF610_REG_ADC_GC);
}

static void vf610_adc_calibration(struct vf610_adc *info)
{
	int adc_gc, hc_cfg;
	int timeout;

	if (!info->adc_feature.calibration)
		return;

	/* enable calibration interrupt */
	hc_cfg = VF610_ADC_AIEN | VF610_ADC_CONV_DISABLE;
	writel(hc_cfg, info->regs + VF610_REG_ADC_HC0);

	adc_gc = readl(info->regs + VF610_REG_ADC_GC);
	writel(adc_gc | VF610_ADC_CAL, info->regs + VF610_REG_ADC_GC);

	timeout = wait_for_completion_timeout
			(&info->completion, VF610_ADC_TIMEOUT);
	if (timeout == 0)
		dev_err(info->dev, "Timeout for adc calibration\n");

	adc_gc = readl(info->regs + VF610_REG_ADC_GS);
	if (adc_gc & VF610_ADC_CALF)
		dev_err(info->dev, "ADC calibration failed\n");

	info->adc_feature.calibration = false;
}

static void vf610_adc_cfg_set(struct vf610_adc *info)
{
	struct vf610_adc_feature *adc_feature = &(info->adc_feature);
	int cfg_data;

	cfg_data = readl(info->regs + VF610_REG_ADC_CFG);

	/* low power configuration */
	cfg_data &= ~VF610_ADC_ADLPC_EN;
	if (adc_feature->lpm)
		cfg_data |= VF610_ADC_ADLPC_EN;

	/* disable high speed */
	cfg_data &= ~VF610_ADC_ADHSC_EN;

	writel(cfg_data, info->regs + VF610_REG_ADC_CFG);
}

static void vf610_adc_sample_set(struct vf610_adc *info)
{
	struct vf610_adc_feature *adc_feature = &(info->adc_feature);
	int cfg_data, gc_data;

	cfg_data = readl(info->regs + VF610_REG_ADC_CFG);
	gc_data = readl(info->regs + VF610_REG_ADC_GC);

	/* resolution mode */
	cfg_data &= ~VF610_ADC_MODE_MASK;
	switch (adc_feature->res_mode) {
	case 8:
		cfg_data |= VF610_ADC_MODE_BIT8;
		break;
	case 10:
		cfg_data |= VF610_ADC_MODE_BIT10;
		break;
	case 12:
		cfg_data |= VF610_ADC_MODE_BIT12;
		break;
	default:
		dev_err(info->dev, "error resolution mode\n");
		break;
	}

	/* clock select and clock divider */
	cfg_data &= ~(VF610_ADC_CLK_MASK | VF610_ADC_ADCCLK_MASK);
	switch (adc_feature->clk_div) {
	case 1:
		break;
	case 2:
		cfg_data |= VF610_ADC_CLK_DIV2;
		break;
	case 4:
		cfg_data |= VF610_ADC_CLK_DIV4;
		break;
	case 8:
		cfg_data |= VF610_ADC_CLK_DIV8;
		break;
	case 16:
		switch (adc_feature->clk_sel) {
		case VF610_ADCIOC_BUSCLK_SET:
			cfg_data |= VF610_ADC_BUSCLK2_SEL | VF610_ADC_CLK_DIV8;
			break;
		default:
			dev_err(info->dev, "error clk divider\n");
			break;
		}
		break;
	}

	/* Use the short sample mode */
	cfg_data &= ~(VF610_ADC_ADLSMP_LONG | VF610_ADC_ADSTS_MASK);

	/* update hardware average selection */
	cfg_data &= ~VF610_ADC_AVGS_MASK;
	gc_data &= ~VF610_ADC_AVGEN;
	switch (adc_feature->sample_rate) {
	case VF610_ADC_SAMPLE_1:
		break;
	case VF610_ADC_SAMPLE_4:
		gc_data |= VF610_ADC_AVGEN;
		break;
	case VF610_ADC_SAMPLE_8:
		gc_data |= VF610_ADC_AVGEN;
		cfg_data |= VF610_ADC_AVGS_8;
		break;
	case VF610_ADC_SAMPLE_16:
		gc_data |= VF610_ADC_AVGEN;
		cfg_data |= VF610_ADC_AVGS_16;
		break;
	case VF610_ADC_SAMPLE_32:
		gc_data |= VF610_ADC_AVGEN;
		cfg_data |= VF610_ADC_AVGS_32;
		break;
	default:
		dev_err(info->dev,
			"error hardware sample average select\n");
	}

	writel(cfg_data, info->regs + VF610_REG_ADC_CFG);
	writel(gc_data, info->regs + VF610_REG_ADC_GC);
}

static void vf610_adc_hw_init(struct vf610_adc *info)
{
	/* CFG: Feature set */
	vf610_adc_cfg_post_set(info);
	vf610_adc_sample_set(info);

	/* adc calibration */
	vf610_adc_calibration(info);

	/* CFG: power and speed set */
	vf610_adc_cfg_set(info);
}

static int vf610_adc_read_data(struct vf610_adc *info)
{
	int result;

	result = readl(info->regs + VF610_REG_ADC_R0);

	switch (info->adc_feature.res_mode) {
	case 8:
		result &= 0xFF;
		break;
	case 10:
		result &= 0x3FF;
		break;
	case 12:
		result &= 0xFFF;
		break;
	default:
		break;
	}

	return result;
}

static irqreturn_t vf610_adc_isr(int irq, void *dev_id)
{
	struct vf610_adc *info = (struct vf610_adc *)dev_id;
	int coco;

	coco = readl(info->regs + VF610_REG_ADC_HS);
	if (coco & VF610_ADC_HS_COCO0) {
		info->value = vf610_adc_read_data(info);
		complete(&info->completion);
	}

	return IRQ_HANDLED;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("1941176, 559332, 286957, 145374, 73171");

static struct attribute *vf610_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group vf610_attribute_group = {
	.attrs = vf610_attributes,
};

static int vf610_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask)
{
	struct vf610_adc *info = iio_priv(indio_dev);
	unsigned int hc_cfg;
	long ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		reinit_completion(&info->completion);

		hc_cfg = VF610_ADC_ADCHC(chan->channel);
		hc_cfg |= VF610_ADC_AIEN;
		writel(hc_cfg, info->regs + VF610_REG_ADC_HC0);
		ret = wait_for_completion_interruptible_timeout
				(&info->completion, VF610_ADC_TIMEOUT);
		if (ret == 0) {
			mutex_unlock(&indio_dev->mlock);
			return -ETIMEDOUT;
		}
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		*val = info->value;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_uv / 1000;
		*val2 = info->adc_feature.res_mode;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = vf610_sample_freq_avail[info->adc_feature.sample_rate];
		*val2 = 0;
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static int vf610_write_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int val,
			int val2,
			long mask)
{
	struct vf610_adc *info = iio_priv(indio_dev);
	int i;

	switch (mask) {
		case IIO_CHAN_INFO_SAMP_FREQ:
			for (i = 0;
				i < ARRAY_SIZE(vf610_sample_freq_avail);
				i++)
				if (val == vf610_sample_freq_avail[i]) {
					info->adc_feature.sample_rate = i;
					vf610_adc_sample_set(info);
					return 0;
				}
			break;

		default:
			break;
	}

	return -EINVAL;
}

static int vf610_adc_reg_access(struct iio_dev *indio_dev,
			unsigned reg, unsigned writeval,
			unsigned *readval)
{
	struct vf610_adc *info = iio_priv(indio_dev);

	if ((readval == NULL) ||
		(!(reg % 4) || (reg > VF610_REG_ADC_PCTL)))
		return -EINVAL;

	*readval = readl(info->regs + reg);

	return 0;
}

static const struct iio_info vf610_adc_iio_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &vf610_read_raw,
	.write_raw = &vf610_write_raw,
	.debugfs_reg_access = &vf610_adc_reg_access,
	.attrs = &vf610_attribute_group,
};

static const struct of_device_id vf610_adc_match[] = {
	{ .compatible = "fsl,vf610-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vf610_adc_match);

static int vf610_adc_probe(struct platform_device *pdev)
{
	struct vf610_adc *info;
	struct iio_dev *indio_dev;
	struct resource *mem;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct vf610_adc));
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

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -EINVAL;
	}

	ret = devm_request_irq(info->dev, irq,
				vf610_adc_isr, 0,
				dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
						PTR_ERR(info->clk));
		ret = PTR_ERR(info->clk);
		return ret;
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref))
		return PTR_ERR(info->vref);

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	info->vref_uv = regulator_get_voltage(info->vref);

	platform_set_drvdata(pdev, indio_dev);

	init_completion(&info->completion);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &vf610_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = vf610_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(vf610_adc_iio_channels);

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not prepare or enable the clock.\n");
		goto error_adc_clk_enable;
	}

	vf610_adc_cfg_init(info);
	vf610_adc_hw_init(info);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto error_iio_device_register;
	}

	return 0;


error_iio_device_register:
	clk_disable_unprepare(info->clk);
error_adc_clk_enable:
	regulator_disable(info->vref);

	return ret;
}

static int vf610_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct vf610_adc *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(info->vref);
	clk_disable_unprepare(info->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int vf610_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct vf610_adc *info = iio_priv(indio_dev);
	int hc_cfg;

	/* ADC controller enters to stop mode */
	hc_cfg = readl(info->regs + VF610_REG_ADC_HC0);
	hc_cfg |= VF610_ADC_CONV_DISABLE;
	writel(hc_cfg, info->regs + VF610_REG_ADC_HC0);

	clk_disable_unprepare(info->clk);
	regulator_disable(info->vref);

	return 0;
}

static int vf610_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct vf610_adc *info = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	ret = clk_prepare_enable(info->clk);
	if (ret)
		return ret;

	vf610_adc_hw_init(info);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(vf610_adc_pm_ops,
			vf610_adc_suspend,
			vf610_adc_resume);

static struct platform_driver vf610_adc_driver = {
	.probe          = vf610_adc_probe,
	.remove         = vf610_adc_remove,
	.driver         = {
		.name   = DRIVER_NAME,
		.of_match_table = vf610_adc_match,
		.pm     = &vf610_adc_pm_ops,
	},
};

module_platform_driver(vf610_adc_driver);

MODULE_AUTHOR("Fugang Duan <B38611@freescale.com>");
MODULE_DESCRIPTION("Freescale VF610 ADC driver");
MODULE_LICENSE("GPL v2");
