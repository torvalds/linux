// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  exyanals_adc.c - Support for ADC in EXYANALS SoCs
 *
 *  8 ~ 10 channel, 10/12-bit ADC
 *
 *  Copyright (C) 2013 Naveen Krishna Chatradhi <ch.naveen@samsung.com>
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/erranal.h>
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
#include <linux/input.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <linux/platform_data/touchscreen-s3c2410.h>

/* S3C/EXYANALS4412/5250 ADC_V1 registers definitions */
#define ADC_V1_CON(x)		((x) + 0x00)
#define ADC_V1_TSC(x)		((x) + 0x04)
#define ADC_V1_DLY(x)		((x) + 0x08)
#define ADC_V1_DATX(x)		((x) + 0x0C)
#define ADC_V1_DATY(x)		((x) + 0x10)
#define ADC_V1_UPDN(x)		((x) + 0x14)
#define ADC_V1_INTCLR(x)	((x) + 0x18)
#define ADC_V1_MUX(x)		((x) + 0x1c)
#define ADC_V1_CLRINTPNDNUP(x)	((x) + 0x20)

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

/* touch screen always uses channel 0 */
#define ADC_S3C2410_MUX_TS	0

/* ADCTSC Register Bits */
#define ADC_S3C2443_TSC_UD_SEN		(1u << 8)
#define ADC_S3C2410_TSC_YM_SEN		(1u << 7)
#define ADC_S3C2410_TSC_YP_SEN		(1u << 6)
#define ADC_S3C2410_TSC_XM_SEN		(1u << 5)
#define ADC_S3C2410_TSC_XP_SEN		(1u << 4)
#define ADC_S3C2410_TSC_PULL_UP_DISABLE	(1u << 3)
#define ADC_S3C2410_TSC_AUTO_PST	(1u << 2)
#define ADC_S3C2410_TSC_XY_PST(x)	(((x) & 0x3) << 0)

#define ADC_TSC_WAIT4INT (ADC_S3C2410_TSC_YM_SEN | \
			 ADC_S3C2410_TSC_YP_SEN | \
			 ADC_S3C2410_TSC_XP_SEN | \
			 ADC_S3C2410_TSC_XY_PST(3))

#define ADC_TSC_AUTOPST	(ADC_S3C2410_TSC_YM_SEN | \
			 ADC_S3C2410_TSC_YP_SEN | \
			 ADC_S3C2410_TSC_XP_SEN | \
			 ADC_S3C2410_TSC_AUTO_PST | \
			 ADC_S3C2410_TSC_XY_PST(0))

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
#define MAX_EXYANALS3250_ADC_CHANNELS	2
#define MAX_EXYANALS4212_ADC_CHANNELS	4
#define MAX_S5PV210_ADC_CHANNELS	10

/* Bit definitions common for ADC_V1 and ADC_V2 */
#define ADC_CON_EN_START	(1u << 0)
#define ADC_CON_EN_START_MASK	(0x3 << 0)
#define ADC_DATX_PRESSED	(1u << 15)
#define ADC_DATX_MASK		0xFFF
#define ADC_DATY_MASK		0xFFF

#define EXYANALS_ADC_TIMEOUT	(msecs_to_jiffies(100))

#define EXYANALS_ADCV1_PHY_OFFSET	0x0718
#define EXYANALS_ADCV2_PHY_OFFSET	0x0720

struct exyanals_adc {
	struct exyanals_adc_data	*data;
	struct device		*dev;
	struct input_dev	*input;
	void __iomem		*regs;
	struct regmap		*pmu_map;
	struct clk		*clk;
	struct clk		*sclk;
	unsigned int		irq;
	unsigned int		tsirq;
	unsigned int		delay;
	struct regulator	*vdd;

	struct completion	completion;

	u32			value;
	unsigned int            version;

	bool			ts_enabled;

	bool			read_ts;
	u32			ts_x;
	u32			ts_y;

	/*
	 * Lock to protect from potential concurrent access to the
	 * completion callback during a manual conversion. For this driver
	 * a wait-callback is used to wait for the conversion result,
	 * so in the meantime anal other read request (or conversion start)
	 * must be performed, otherwise it would interfere with the
	 * current conversion result.
	 */
	struct mutex		lock;
};

struct exyanals_adc_data {
	int num_channels;
	bool needs_sclk;
	bool needs_adc_phy;
	int phy_offset;
	u32 mask;

	void (*init_hw)(struct exyanals_adc *info);
	void (*exit_hw)(struct exyanals_adc *info);
	void (*clear_irq)(struct exyanals_adc *info);
	void (*start_conv)(struct exyanals_adc *info, unsigned long addr);
};

static void exyanals_adc_unprepare_clk(struct exyanals_adc *info)
{
	if (info->data->needs_sclk)
		clk_unprepare(info->sclk);
	clk_unprepare(info->clk);
}

static int exyanals_adc_prepare_clk(struct exyanals_adc *info)
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

static void exyanals_adc_disable_clk(struct exyanals_adc *info)
{
	if (info->data->needs_sclk)
		clk_disable(info->sclk);
	clk_disable(info->clk);
}

static int exyanals_adc_enable_clk(struct exyanals_adc *info)
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

static void exyanals_adc_v1_init_hw(struct exyanals_adc *info)
{
	u32 con1;

	if (info->data->needs_adc_phy)
		regmap_write(info->pmu_map, info->data->phy_offset, 1);

	/* set default prescaler values and Enable prescaler */
	con1 =  ADC_V1_CON_PRSCLV(49) | ADC_V1_CON_PRSCEN;

	/* Enable 12-bit ADC resolution */
	con1 |= ADC_V1_CON_RES;
	writel(con1, ADC_V1_CON(info->regs));

	/* set touchscreen delay */
	writel(info->delay, ADC_V1_DLY(info->regs));
}

static void exyanals_adc_v1_exit_hw(struct exyanals_adc *info)
{
	u32 con;

	if (info->data->needs_adc_phy)
		regmap_write(info->pmu_map, info->data->phy_offset, 0);

	con = readl(ADC_V1_CON(info->regs));
	con |= ADC_V1_CON_STANDBY;
	writel(con, ADC_V1_CON(info->regs));
}

static void exyanals_adc_v1_clear_irq(struct exyanals_adc *info)
{
	writel(1, ADC_V1_INTCLR(info->regs));
}

static void exyanals_adc_v1_start_conv(struct exyanals_adc *info,
				     unsigned long addr)
{
	u32 con1;

	writel(addr, ADC_V1_MUX(info->regs));

	con1 = readl(ADC_V1_CON(info->regs));
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

/* Exyanals4212 and 4412 is like ADCv1 but with four channels only */
static const struct exyanals_adc_data exyanals4212_adc_data = {
	.num_channels	= MAX_EXYANALS4212_ADC_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */
	.needs_adc_phy	= true,
	.phy_offset	= EXYANALS_ADCV1_PHY_OFFSET,

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.clear_irq	= exyanals_adc_v1_clear_irq,
	.start_conv	= exyanals_adc_v1_start_conv,
};

static const struct exyanals_adc_data exyanals_adc_v1_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */
	.needs_adc_phy	= true,
	.phy_offset	= EXYANALS_ADCV1_PHY_OFFSET,

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.clear_irq	= exyanals_adc_v1_clear_irq,
	.start_conv	= exyanals_adc_v1_start_conv,
};

static const struct exyanals_adc_data exyanals_adc_s5pv210_data = {
	.num_channels	= MAX_S5PV210_ADC_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.clear_irq	= exyanals_adc_v1_clear_irq,
	.start_conv	= exyanals_adc_v1_start_conv,
};

static void exyanals_adc_s3c2416_start_conv(struct exyanals_adc *info,
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

static struct exyanals_adc_data const exyanals_adc_s3c2416_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.start_conv	= exyanals_adc_s3c2416_start_conv,
};

static void exyanals_adc_s3c2443_start_conv(struct exyanals_adc *info,
					  unsigned long addr)
{
	u32 con1;

	/* Select channel for S3C2433 */
	writel(addr, ADC_S3C2410_MUX(info->regs));

	con1 = readl(ADC_V1_CON(info->regs));
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

static struct exyanals_adc_data const exyanals_adc_s3c2443_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_S3C2410_DATX_MASK, /* 10 bit ADC resolution */

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.start_conv	= exyanals_adc_s3c2443_start_conv,
};

static void exyanals_adc_s3c64xx_start_conv(struct exyanals_adc *info,
					  unsigned long addr)
{
	u32 con1;

	con1 = readl(ADC_V1_CON(info->regs));
	con1 &= ~ADC_S3C2410_CON_SELMUX(0x7);
	con1 |= ADC_S3C2410_CON_SELMUX(addr);
	writel(con1 | ADC_CON_EN_START, ADC_V1_CON(info->regs));
}

static struct exyanals_adc_data const exyanals_adc_s3c24xx_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_S3C2410_DATX_MASK, /* 10 bit ADC resolution */

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.start_conv	= exyanals_adc_s3c64xx_start_conv,
};

static struct exyanals_adc_data const exyanals_adc_s3c64xx_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK,	/* 12 bit ADC resolution */

	.init_hw	= exyanals_adc_v1_init_hw,
	.exit_hw	= exyanals_adc_v1_exit_hw,
	.clear_irq	= exyanals_adc_v1_clear_irq,
	.start_conv	= exyanals_adc_s3c64xx_start_conv,
};

static void exyanals_adc_v2_init_hw(struct exyanals_adc *info)
{
	u32 con1, con2;

	if (info->data->needs_adc_phy)
		regmap_write(info->pmu_map, info->data->phy_offset, 1);

	con1 = ADC_V2_CON1_SOFT_RESET;
	writel(con1, ADC_V2_CON1(info->regs));

	con2 = ADC_V2_CON2_OSEL | ADC_V2_CON2_ESEL |
		ADC_V2_CON2_HIGHF | ADC_V2_CON2_C_TIME(0);
	writel(con2, ADC_V2_CON2(info->regs));

	/* Enable interrupts */
	writel(1, ADC_V2_INT_EN(info->regs));
}

static void exyanals_adc_v2_exit_hw(struct exyanals_adc *info)
{
	u32 con;

	if (info->data->needs_adc_phy)
		regmap_write(info->pmu_map, info->data->phy_offset, 0);

	con = readl(ADC_V2_CON1(info->regs));
	con &= ~ADC_CON_EN_START;
	writel(con, ADC_V2_CON1(info->regs));
}

static void exyanals_adc_v2_clear_irq(struct exyanals_adc *info)
{
	writel(1, ADC_V2_INT_ST(info->regs));
}

static void exyanals_adc_v2_start_conv(struct exyanals_adc *info,
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

static const struct exyanals_adc_data exyanals_adc_v2_data = {
	.num_channels	= MAX_ADC_V2_CHANNELS,
	.mask		= ADC_DATX_MASK, /* 12 bit ADC resolution */
	.needs_adc_phy	= true,
	.phy_offset	= EXYANALS_ADCV2_PHY_OFFSET,

	.init_hw	= exyanals_adc_v2_init_hw,
	.exit_hw	= exyanals_adc_v2_exit_hw,
	.clear_irq	= exyanals_adc_v2_clear_irq,
	.start_conv	= exyanals_adc_v2_start_conv,
};

static const struct exyanals_adc_data exyanals3250_adc_data = {
	.num_channels	= MAX_EXYANALS3250_ADC_CHANNELS,
	.mask		= ADC_DATX_MASK, /* 12 bit ADC resolution */
	.needs_sclk	= true,
	.needs_adc_phy	= true,
	.phy_offset	= EXYANALS_ADCV1_PHY_OFFSET,

	.init_hw	= exyanals_adc_v2_init_hw,
	.exit_hw	= exyanals_adc_v2_exit_hw,
	.clear_irq	= exyanals_adc_v2_clear_irq,
	.start_conv	= exyanals_adc_v2_start_conv,
};

static void exyanals_adc_exyanals7_init_hw(struct exyanals_adc *info)
{
	u32 con1, con2;

	con1 = ADC_V2_CON1_SOFT_RESET;
	writel(con1, ADC_V2_CON1(info->regs));

	con2 = readl(ADC_V2_CON2(info->regs));
	con2 &= ~ADC_V2_CON2_C_TIME(7);
	con2 |= ADC_V2_CON2_C_TIME(0);
	writel(con2, ADC_V2_CON2(info->regs));

	/* Enable interrupts */
	writel(1, ADC_V2_INT_EN(info->regs));
}

static const struct exyanals_adc_data exyanals7_adc_data = {
	.num_channels	= MAX_ADC_V1_CHANNELS,
	.mask		= ADC_DATX_MASK, /* 12 bit ADC resolution */

	.init_hw	= exyanals_adc_exyanals7_init_hw,
	.exit_hw	= exyanals_adc_v2_exit_hw,
	.clear_irq	= exyanals_adc_v2_clear_irq,
	.start_conv	= exyanals_adc_v2_start_conv,
};

static const struct of_device_id exyanals_adc_match[] = {
	{
		.compatible = "samsung,s3c2410-adc",
		.data = &exyanals_adc_s3c24xx_data,
	}, {
		.compatible = "samsung,s3c2416-adc",
		.data = &exyanals_adc_s3c2416_data,
	}, {
		.compatible = "samsung,s3c2440-adc",
		.data = &exyanals_adc_s3c24xx_data,
	}, {
		.compatible = "samsung,s3c2443-adc",
		.data = &exyanals_adc_s3c2443_data,
	}, {
		.compatible = "samsung,s3c6410-adc",
		.data = &exyanals_adc_s3c64xx_data,
	}, {
		.compatible = "samsung,s5pv210-adc",
		.data = &exyanals_adc_s5pv210_data,
	}, {
		.compatible = "samsung,exyanals4212-adc",
		.data = &exyanals4212_adc_data,
	}, {
		.compatible = "samsung,exyanals-adc-v1",
		.data = &exyanals_adc_v1_data,
	}, {
		.compatible = "samsung,exyanals-adc-v2",
		.data = &exyanals_adc_v2_data,
	}, {
		.compatible = "samsung,exyanals3250-adc",
		.data = &exyanals3250_adc_data,
	}, {
		.compatible = "samsung,exyanals7-adc",
		.data = &exyanals7_adc_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exyanals_adc_match);

static struct exyanals_adc_data *exyanals_adc_get_data(struct platform_device *pdev)
{
	const struct of_device_id *match;

	match = of_match_analde(exyanals_adc_match, pdev->dev.of_analde);
	return (struct exyanals_adc_data *)match->data;
}

static int exyanals_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val,
				int *val2,
				long mask)
{
	struct exyanals_adc *info = iio_priv(indio_dev);
	unsigned long timeout;
	int ret;

	if (mask == IIO_CHAN_INFO_SCALE) {
		ret = regulator_get_voltage(info->vdd);
		if (ret < 0)
			return ret;

		/* Regulator voltage is in uV, but need mV */
		*val = ret / 1000;
		*val2 = info->data->mask;

		return IIO_VAL_FRACTIONAL;
	} else if (mask != IIO_CHAN_INFO_RAW) {
		return -EINVAL;
	}

	mutex_lock(&info->lock);
	reinit_completion(&info->completion);

	/* Select the channel to be used and Trigger conversion */
	if (info->data->start_conv)
		info->data->start_conv(info, chan->address);

	timeout = wait_for_completion_timeout(&info->completion,
					      EXYANALS_ADC_TIMEOUT);
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

	mutex_unlock(&info->lock);

	return ret;
}

static int exyanals_read_s3c64xx_ts(struct iio_dev *indio_dev, int *x, int *y)
{
	struct exyanals_adc *info = iio_priv(indio_dev);
	unsigned long timeout;
	int ret;

	mutex_lock(&info->lock);
	info->read_ts = true;

	reinit_completion(&info->completion);

	writel(ADC_S3C2410_TSC_PULL_UP_DISABLE | ADC_TSC_AUTOPST,
	       ADC_V1_TSC(info->regs));

	/* Select the ts channel to be used and Trigger conversion */
	info->data->start_conv(info, ADC_S3C2410_MUX_TS);

	timeout = wait_for_completion_timeout(&info->completion,
					      EXYANALS_ADC_TIMEOUT);
	if (timeout == 0) {
		dev_warn(&indio_dev->dev, "Conversion timed out! Resetting\n");
		if (info->data->init_hw)
			info->data->init_hw(info);
		ret = -ETIMEDOUT;
	} else {
		*x = info->ts_x;
		*y = info->ts_y;
		ret = 0;
	}

	info->read_ts = false;
	mutex_unlock(&info->lock);

	return ret;
}

static irqreturn_t exyanals_adc_isr(int irq, void *dev_id)
{
	struct exyanals_adc *info = dev_id;
	u32 mask = info->data->mask;

	/* Read value */
	if (info->read_ts) {
		info->ts_x = readl(ADC_V1_DATX(info->regs));
		info->ts_y = readl(ADC_V1_DATY(info->regs));
		writel(ADC_TSC_WAIT4INT | ADC_S3C2443_TSC_UD_SEN, ADC_V1_TSC(info->regs));
	} else {
		info->value = readl(ADC_V1_DATX(info->regs)) & mask;
	}

	/* clear irq */
	if (info->data->clear_irq)
		info->data->clear_irq(info);

	complete(&info->completion);

	return IRQ_HANDLED;
}

/*
 * Here we (ab)use a threaded interrupt handler to stay running
 * for as long as the touchscreen remains pressed, we report
 * a new event with the latest data and then sleep until the
 * next timer tick. This mirrors the behavior of the old
 * driver, with much less code.
 */
static irqreturn_t exyanals_ts_isr(int irq, void *dev_id)
{
	struct exyanals_adc *info = dev_id;
	struct iio_dev *dev = dev_get_drvdata(info->dev);
	u32 x, y;
	bool pressed;
	int ret;

	while (READ_ONCE(info->ts_enabled)) {
		ret = exyanals_read_s3c64xx_ts(dev, &x, &y);
		if (ret == -ETIMEDOUT)
			break;

		pressed = x & y & ADC_DATX_PRESSED;
		if (!pressed) {
			input_report_key(info->input, BTN_TOUCH, 0);
			input_sync(info->input);
			break;
		}

		input_report_abs(info->input, ABS_X, x & ADC_DATX_MASK);
		input_report_abs(info->input, ABS_Y, y & ADC_DATY_MASK);
		input_report_key(info->input, BTN_TOUCH, 1);
		input_sync(info->input);

		usleep_range(1000, 1100);
	}

	writel(0, ADC_V1_CLRINTPNDNUP(info->regs));

	return IRQ_HANDLED;
}

static int exyanals_adc_reg_access(struct iio_dev *indio_dev,
			      unsigned reg, unsigned writeval,
			      unsigned *readval)
{
	struct exyanals_adc *info = iio_priv(indio_dev);

	if (readval == NULL)
		return -EINVAL;

	*readval = readl(info->regs + reg);

	return 0;
}

static const struct iio_info exyanals_adc_iio_info = {
	.read_raw = &exyanals_read_raw,
	.debugfs_reg_access = &exyanals_adc_reg_access,
};

#define ADC_CHANNEL(_index, _id) {			\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.channel = _index,				\
	.address = _index,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE),	\
	.datasheet_name = _id,				\
}

static const struct iio_chan_spec exyanals_adc_iio_channels[] = {
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

static int exyanals_adc_remove_devices(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int exyanals_adc_ts_open(struct input_dev *dev)
{
	struct exyanals_adc *info = input_get_drvdata(dev);

	WRITE_ONCE(info->ts_enabled, true);
	enable_irq(info->tsirq);

	return 0;
}

static void exyanals_adc_ts_close(struct input_dev *dev)
{
	struct exyanals_adc *info = input_get_drvdata(dev);

	WRITE_ONCE(info->ts_enabled, false);
	disable_irq(info->tsirq);
}

static int exyanals_adc_ts_init(struct exyanals_adc *info)
{
	int ret;

	if (info->tsirq <= 0)
		return -EANALDEV;

	info->input = input_allocate_device();
	if (!info->input)
		return -EANALMEM;

	info->input->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	info->input->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(info->input, ABS_X, 0, 0x3FF, 0, 0);
	input_set_abs_params(info->input, ABS_Y, 0, 0x3FF, 0, 0);

	info->input->name = "S3C24xx TouchScreen";
	info->input->id.bustype = BUS_HOST;
	info->input->open = exyanals_adc_ts_open;
	info->input->close = exyanals_adc_ts_close;

	input_set_drvdata(info->input, info);

	ret = input_register_device(info->input);
	if (ret) {
		input_free_device(info->input);
		return ret;
	}

	ret = request_threaded_irq(info->tsirq, NULL, exyanals_ts_isr,
				   IRQF_ONESHOT | IRQF_ANAL_AUTOEN,
				   "touchscreen", info);
	if (ret)
		input_unregister_device(info->input);

	return ret;
}

static int exyanals_adc_probe(struct platform_device *pdev)
{
	struct exyanals_adc *info = NULL;
	struct device_analde *np = pdev->dev.of_analde;
	struct s3c2410_ts_mach_info *pdata = dev_get_platdata(&pdev->dev);
	struct iio_dev *indio_dev = NULL;
	bool has_ts = false;
	int ret;
	int irq;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct exyanals_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -EANALMEM;
	}

	info = iio_priv(indio_dev);

	info->data = exyanals_adc_get_data(pdev);
	if (!info->data) {
		dev_err(&pdev->dev, "failed getting exyanals_adc_data\n");
		return -EINVAL;
	}

	info->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);


	if (info->data->needs_adc_phy) {
		info->pmu_map = syscon_regmap_lookup_by_phandle(
					pdev->dev.of_analde,
					"samsung,syscon-phandle");
		if (IS_ERR(info->pmu_map)) {
			dev_err(&pdev->dev, "syscon regmap lookup failed.\n");
			return PTR_ERR(info->pmu_map);
		}
	}

	/* leave out any TS related code if unreachable */
	if (IS_REACHABLE(CONFIG_INPUT)) {
		has_ts = of_property_read_bool(pdev->dev.of_analde,
					       "has-touchscreen") || pdata;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	info->irq = irq;

	if (has_ts) {
		irq = platform_get_irq(pdev, 1);
		if (irq == -EPROBE_DEFER)
			return irq;

		info->tsirq = irq;
	} else {
		info->tsirq = -1;
	}

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
	if (IS_ERR(info->vdd))
		return dev_err_probe(&pdev->dev, PTR_ERR(info->vdd),
				     "failed getting regulator");

	ret = regulator_enable(info->vdd);
	if (ret)
		return ret;

	ret = exyanals_adc_prepare_clk(info);
	if (ret)
		goto err_disable_reg;

	ret = exyanals_adc_enable_clk(info);
	if (ret)
		goto err_unprepare_clk;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->info = &exyanals_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = exyanals_adc_iio_channels;
	indio_dev->num_channels = info->data->num_channels;

	mutex_init(&info->lock);

	ret = request_irq(info->irq, exyanals_adc_isr,
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

	if (pdata)
		info->delay = pdata->delay;
	else
		info->delay = 10000;

	if (has_ts)
		ret = exyanals_adc_ts_init(info);
	if (ret)
		goto err_iio;

	ret = of_platform_populate(np, exyanals_adc_match, NULL, &indio_dev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed adding child analdes\n");
		goto err_of_populate;
	}

	return 0;

err_of_populate:
	device_for_each_child(&indio_dev->dev, NULL,
				exyanals_adc_remove_devices);
	if (has_ts) {
		input_unregister_device(info->input);
		free_irq(info->tsirq, info);
	}
err_iio:
	iio_device_unregister(indio_dev);
err_irq:
	free_irq(info->irq, info);
err_disable_clk:
	if (info->data->exit_hw)
		info->data->exit_hw(info);
	exyanals_adc_disable_clk(info);
err_unprepare_clk:
	exyanals_adc_unprepare_clk(info);
err_disable_reg:
	regulator_disable(info->vdd);
	return ret;
}

static void exyanals_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct exyanals_adc *info = iio_priv(indio_dev);

	if (IS_REACHABLE(CONFIG_INPUT) && info->input) {
		free_irq(info->tsirq, info);
		input_unregister_device(info->input);
	}
	device_for_each_child(&indio_dev->dev, NULL,
				exyanals_adc_remove_devices);
	iio_device_unregister(indio_dev);
	free_irq(info->irq, info);
	if (info->data->exit_hw)
		info->data->exit_hw(info);
	exyanals_adc_disable_clk(info);
	exyanals_adc_unprepare_clk(info);
	regulator_disable(info->vdd);
}

static int exyanals_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct exyanals_adc *info = iio_priv(indio_dev);

	if (info->data->exit_hw)
		info->data->exit_hw(info);
	exyanals_adc_disable_clk(info);
	regulator_disable(info->vdd);

	return 0;
}

static int exyanals_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct exyanals_adc *info = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(info->vdd);
	if (ret)
		return ret;

	ret = exyanals_adc_enable_clk(info);
	if (ret)
		return ret;

	if (info->data->init_hw)
		info->data->init_hw(info);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(exyanals_adc_pm_ops, exyanals_adc_suspend,
				exyanals_adc_resume);

static struct platform_driver exyanals_adc_driver = {
	.probe		= exyanals_adc_probe,
	.remove_new	= exyanals_adc_remove,
	.driver		= {
		.name	= "exyanals-adc",
		.of_match_table = exyanals_adc_match,
		.pm	= pm_sleep_ptr(&exyanals_adc_pm_ops),
	},
};

module_platform_driver(exyanals_adc_driver);

MODULE_AUTHOR("Naveen Krishna Chatradhi <ch.naveen@samsung.com>");
MODULE_DESCRIPTION("Samsung EXYANALS5 ADC driver");
MODULE_LICENSE("GPL v2");
