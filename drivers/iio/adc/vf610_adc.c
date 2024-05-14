// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale Vybrid vf610 ADC driver
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

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
#define VF610_ADC_ADSTS_SHORT   0x100
#define VF610_ADC_ADSTS_NORMAL  0x200
#define VF610_ADC_ADSTS_LONG    0x300
#define VF610_ADC_ADSTS_MASK		0x300
#define VF610_ADC_ADLPC_EN		0x80
#define VF610_ADC_ADHSC_EN		0x400
#define VF610_ADC_REFSEL_VALT		0x800
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
#define VF610_ADC_ADCHC(x)		((x) & 0x1F)
#define VF610_ADC_AIEN			(0x1 << 7)
#define VF610_ADC_CONV_DISABLE		0x1F
#define VF610_ADC_HS_COCO0		0x1
#define VF610_ADC_CALF			0x2
#define VF610_ADC_TIMEOUT		msecs_to_jiffies(100)

#define DEFAULT_SAMPLE_TIME		1000

/* V at 25°C of 696 mV */
#define VF610_VTEMP25_3V0		950
/* V at 25°C of 699 mV */
#define VF610_VTEMP25_3V3		867
/* Typical sensor slope coefficient at all temperatures */
#define VF610_TEMP_SLOPE_COEFF		1840

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

enum conversion_mode_sel {
	VF610_ADC_CONV_NORMAL,
	VF610_ADC_CONV_HIGH_SPEED,
	VF610_ADC_CONV_LOW_POWER,
};

enum lst_adder_sel {
	VF610_ADCK_CYCLES_3,
	VF610_ADCK_CYCLES_5,
	VF610_ADCK_CYCLES_7,
	VF610_ADCK_CYCLES_9,
	VF610_ADCK_CYCLES_13,
	VF610_ADCK_CYCLES_17,
	VF610_ADCK_CYCLES_21,
	VF610_ADCK_CYCLES_25,
};

struct vf610_adc_feature {
	enum clk_sel	clk_sel;
	enum vol_ref	vol_ref;
	enum conversion_mode_sel conv_mode;

	int	clk_div;
	int     sample_rate;
	int	res_mode;
	u32 lst_adder_index;
	u32 default_sample_time;

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

	u32 max_adck_rate[3];
	struct vf610_adc_feature adc_feature;

	u32 sample_freq_avail[5];

	struct completion completion;
	/* Ensure the timestamp is naturally aligned */
	struct {
		u16 chan;
		s64 timestamp __aligned(8);
	} scan;
};

static const u32 vf610_hw_avgs[] = { 1, 4, 8, 16, 32 };
static const u32 vf610_lst_adder[] = { 3, 5, 7, 9, 13, 17, 21, 25 };

static inline void vf610_adc_calculate_rates(struct vf610_adc *info)
{
	struct vf610_adc_feature *adc_feature = &info->adc_feature;
	unsigned long adck_rate, ipg_rate = clk_get_rate(info->clk);
	u32 adck_period, lst_addr_min;
	int divisor, i;

	adck_rate = info->max_adck_rate[adc_feature->conv_mode];

	if (adck_rate) {
		/* calculate clk divider which is within specification */
		divisor = ipg_rate / adck_rate;
		adc_feature->clk_div = 1 << fls(divisor + 1);
	} else {
		/* fall-back value using a safe divisor */
		adc_feature->clk_div = 8;
	}

	adck_rate = ipg_rate / adc_feature->clk_div;

	/*
	 * Determine the long sample time adder value to be used based
	 * on the default minimum sample time provided.
	 */
	adck_period = NSEC_PER_SEC / adck_rate;
	lst_addr_min = adc_feature->default_sample_time / adck_period;
	for (i = 0; i < ARRAY_SIZE(vf610_lst_adder); i++) {
		if (vf610_lst_adder[i] > lst_addr_min) {
			adc_feature->lst_adder_index = i;
			break;
		}
	}

	/*
	 * Calculate ADC sample frequencies
	 * Sample time unit is ADCK cycles. ADCK clk source is ipg clock,
	 * which is the same as bus clock.
	 *
	 * ADC conversion time = SFCAdder + AverageNum x (BCT + LSTAdder)
	 * SFCAdder: fixed to 6 ADCK cycles
	 * AverageNum: 1, 4, 8, 16, 32 samples for hardware average.
	 * BCT (Base Conversion Time): fixed to 25 ADCK cycles for 12 bit mode
	 * LSTAdder(Long Sample Time): 3, 5, 7, 9, 13, 17, 21, 25 ADCK cycles
	 */
	for (i = 0; i < ARRAY_SIZE(vf610_hw_avgs); i++)
		info->sample_freq_avail[i] =
			adck_rate / (6 + vf610_hw_avgs[i] *
			 (25 + vf610_lst_adder[adc_feature->lst_adder_index]));
}

static inline void vf610_adc_cfg_init(struct vf610_adc *info)
{
	struct vf610_adc_feature *adc_feature = &info->adc_feature;

	/* set default Configuration for ADC controller */
	adc_feature->clk_sel = VF610_ADCIOC_BUSCLK_SET;
	adc_feature->vol_ref = VF610_ADCIOC_VR_VREF_SET;

	adc_feature->calibration = true;
	adc_feature->ovwren = true;

	adc_feature->res_mode = 12;
	adc_feature->sample_rate = 1;

	adc_feature->conv_mode = VF610_ADC_CONV_LOW_POWER;

	vf610_adc_calculate_rates(info);
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

	if (!info->adc_feature.calibration)
		return;

	/* enable calibration interrupt */
	hc_cfg = VF610_ADC_AIEN | VF610_ADC_CONV_DISABLE;
	writel(hc_cfg, info->regs + VF610_REG_ADC_HC0);

	adc_gc = readl(info->regs + VF610_REG_ADC_GC);
	writel(adc_gc | VF610_ADC_CAL, info->regs + VF610_REG_ADC_GC);

	if (!wait_for_completion_timeout(&info->completion, VF610_ADC_TIMEOUT))
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

	cfg_data &= ~VF610_ADC_ADLPC_EN;
	if (adc_feature->conv_mode == VF610_ADC_CONV_LOW_POWER)
		cfg_data |= VF610_ADC_ADLPC_EN;

	cfg_data &= ~VF610_ADC_ADHSC_EN;
	if (adc_feature->conv_mode == VF610_ADC_CONV_HIGH_SPEED)
		cfg_data |= VF610_ADC_ADHSC_EN;

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

	/*
	 * Set ADLSMP and ADSTS based on the Long Sample Time Adder value
	 * determined.
	 */
	switch (adc_feature->lst_adder_index) {
	case VF610_ADCK_CYCLES_3:
		break;
	case VF610_ADCK_CYCLES_5:
		cfg_data |= VF610_ADC_ADSTS_SHORT;
		break;
	case VF610_ADCK_CYCLES_7:
		cfg_data |= VF610_ADC_ADSTS_NORMAL;
		break;
	case VF610_ADCK_CYCLES_9:
		cfg_data |= VF610_ADC_ADSTS_LONG;
		break;
	case VF610_ADCK_CYCLES_13:
		cfg_data |= VF610_ADC_ADLSMP_LONG;
		break;
	case VF610_ADCK_CYCLES_17:
		cfg_data |= VF610_ADC_ADLSMP_LONG;
		cfg_data |= VF610_ADC_ADSTS_SHORT;
		break;
	case VF610_ADCK_CYCLES_21:
		cfg_data |= VF610_ADC_ADLSMP_LONG;
		cfg_data |= VF610_ADC_ADSTS_NORMAL;
		break;
	case VF610_ADCK_CYCLES_25:
		cfg_data |= VF610_ADC_ADLSMP_LONG;
		cfg_data |= VF610_ADC_ADSTS_NORMAL;
		break;
	default:
		dev_err(info->dev, "error in sample time select\n");
	}

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

static int vf610_set_conversion_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     unsigned int mode)
{
	struct vf610_adc *info = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	info->adc_feature.conv_mode = mode;
	vf610_adc_calculate_rates(info);
	vf610_adc_hw_init(info);
	mutex_unlock(&indio_dev->mlock);

	return 0;
}

static int vf610_get_conversion_mode(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan)
{
	struct vf610_adc *info = iio_priv(indio_dev);

	return info->adc_feature.conv_mode;
}

static const char * const vf610_conv_modes[] = { "normal", "high-speed",
						 "low-power" };

static const struct iio_enum vf610_conversion_mode = {
	.items = vf610_conv_modes,
	.num_items = ARRAY_SIZE(vf610_conv_modes),
	.get = vf610_get_conversion_mode,
	.set = vf610_set_conversion_mode,
};

static const struct iio_chan_spec_ext_info vf610_ext_info[] = {
	IIO_ENUM("conversion_mode", IIO_SHARED_BY_DIR, &vf610_conversion_mode),
	{},
};

#define VF610_ADC_CHAN(_idx, _chan_type) {			\
	.type = (_chan_type),					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.ext_info = vf610_ext_info,				\
	.scan_index = (_idx),			\
	.scan_type = {					\
		.sign = 'u',				\
		.realbits = 12,				\
		.storagebits = 16,			\
	},						\
}

#define VF610_ADC_TEMPERATURE_CHAN(_idx, _chan_type) {	\
	.type = (_chan_type),	\
	.channel = (_idx),		\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),	\
	.scan_index = (_idx),					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 12,					\
		.storagebits = 16,				\
	},							\
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
	VF610_ADC_TEMPERATURE_CHAN(26, IIO_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(32),
	/* sentinel */
};

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
	struct iio_dev *indio_dev = dev_id;
	struct vf610_adc *info = iio_priv(indio_dev);
	int coco;

	coco = readl(info->regs + VF610_REG_ADC_HS);
	if (coco & VF610_ADC_HS_COCO0) {
		info->value = vf610_adc_read_data(info);
		if (iio_buffer_enabled(indio_dev)) {
			info->scan.chan = info->value;
			iio_push_to_buffers_with_timestamp(indio_dev,
					&info->scan,
					iio_get_time_ns(indio_dev));
			iio_trigger_notify_done(indio_dev->trig);
		} else
			complete(&info->completion);
	}

	return IRQ_HANDLED;
}

static ssize_t vf610_show_samp_freq_avail(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct vf610_adc *info = iio_priv(dev_to_iio_dev(dev));
	size_t len = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(info->sample_freq_avail); i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"%u ", info->sample_freq_avail[i]);

	/* replace trailing space by newline */
	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_SAMP_FREQ_AVAIL(vf610_show_samp_freq_avail);

static struct attribute *vf610_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
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
	case IIO_CHAN_INFO_PROCESSED:
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev)) {
			mutex_unlock(&indio_dev->mlock);
			return -EBUSY;
		}

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

		switch (chan->type) {
		case IIO_VOLTAGE:
			*val = info->value;
			break;
		case IIO_TEMP:
			/*
			 * Calculate in degree Celsius times 1000
			 * Using the typical sensor slope of 1.84 mV/°C
			 * and VREFH_ADC at 3.3V, V at 25°C of 699 mV
			 */
			*val = 25000 - ((int)info->value - VF610_VTEMP25_3V3) *
					1000000 / VF610_TEMP_SLOPE_COEFF;

			break;
		default:
			mutex_unlock(&indio_dev->mlock);
			return -EINVAL;
		}

		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_uv / 1000;
		*val2 = info->adc_feature.res_mode;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = info->sample_freq_avail[info->adc_feature.sample_rate];
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
			i < ARRAY_SIZE(info->sample_freq_avail);
			i++)
			if (val == info->sample_freq_avail[i]) {
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

static int vf610_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct vf610_adc *info = iio_priv(indio_dev);
	unsigned int channel;
	int val;

	val = readl(info->regs + VF610_REG_ADC_GC);
	val |= VF610_ADC_ADCON;
	writel(val, info->regs + VF610_REG_ADC_GC);

	channel = find_first_bit(indio_dev->active_scan_mask,
						indio_dev->masklength);

	val = VF610_ADC_ADCHC(channel);
	val |= VF610_ADC_AIEN;

	writel(val, info->regs + VF610_REG_ADC_HC0);

	return 0;
}

static int vf610_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct vf610_adc *info = iio_priv(indio_dev);
	unsigned int hc_cfg = 0;
	int val;

	val = readl(info->regs + VF610_REG_ADC_GC);
	val &= ~VF610_ADC_ADCON;
	writel(val, info->regs + VF610_REG_ADC_GC);

	hc_cfg |= VF610_ADC_CONV_DISABLE;
	hc_cfg &= ~VF610_ADC_AIEN;

	writel(hc_cfg, info->regs + VF610_REG_ADC_HC0);

	return 0;
}

static const struct iio_buffer_setup_ops iio_triggered_buffer_setup_ops = {
	.postenable = &vf610_adc_buffer_postenable,
	.predisable = &vf610_adc_buffer_predisable,
	.validate_scan_mask = &iio_validate_scan_mask_onehot,
};

static int vf610_adc_reg_access(struct iio_dev *indio_dev,
			unsigned reg, unsigned writeval,
			unsigned *readval)
{
	struct vf610_adc *info = iio_priv(indio_dev);

	if ((readval == NULL) ||
		((reg % 4) || (reg > VF610_REG_ADC_PCTL)))
		return -EINVAL;

	*readval = readl(info->regs + reg);

	return 0;
}

static const struct iio_info vf610_adc_iio_info = {
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
	struct device *dev = &pdev->dev;
	struct vf610_adc *info;
	struct iio_dev *indio_dev;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct vf610_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "Failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = &pdev->dev;

	info->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(info->dev, irq,
				vf610_adc_isr, 0,
				dev_name(&pdev->dev), indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
						PTR_ERR(info->clk));
		return PTR_ERR(info->clk);
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref))
		return PTR_ERR(info->vref);

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	info->vref_uv = regulator_get_voltage(info->vref);

	device_property_read_u32_array(dev, "fsl,adck-max-frequency", info->max_adck_rate, 3);

	info->adc_feature.default_sample_time = DEFAULT_SAMPLE_TIME;
	device_property_read_u32(dev, "min-sample-time", &info->adc_feature.default_sample_time);

	platform_set_drvdata(pdev, indio_dev);

	init_completion(&info->completion);

	indio_dev->name = dev_name(&pdev->dev);
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

	ret = iio_triggered_buffer_setup(indio_dev, &iio_pollfunc_store_time,
					NULL, &iio_triggered_buffer_setup_ops);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't initialise the buffer\n");
		goto error_iio_device_register;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto error_adc_buffer_init;
	}

	return 0;

error_adc_buffer_init:
	iio_triggered_buffer_cleanup(indio_dev);
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
	iio_triggered_buffer_cleanup(indio_dev);
	regulator_disable(info->vref);
	clk_disable_unprepare(info->clk);

	return 0;
}

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
		goto disable_reg;

	vf610_adc_hw_init(info);

	return 0;

disable_reg:
	regulator_disable(info->vref);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(vf610_adc_pm_ops, vf610_adc_suspend,
				vf610_adc_resume);

static struct platform_driver vf610_adc_driver = {
	.probe          = vf610_adc_probe,
	.remove         = vf610_adc_remove,
	.driver         = {
		.name   = DRIVER_NAME,
		.of_match_table = vf610_adc_match,
		.pm     = pm_sleep_ptr(&vf610_adc_pm_ops),
	},
};

module_platform_driver(vf610_adc_driver);

MODULE_AUTHOR("Fugang Duan <B38611@freescale.com>");
MODULE_DESCRIPTION("Freescale VF610 ADC driver");
MODULE_LICENSE("GPL v2");
