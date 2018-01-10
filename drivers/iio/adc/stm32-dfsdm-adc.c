// SPDX-License-Identifier: GPL-2.0
/*
 * This file is the ADC part of the STM32 DFSDM driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Arnaud Pouliquen <arnaud.pouliquen@st.com>.
 */

#include <linux/interrupt.h>
#include <linux/iio/buffer.h>
#include <linux/iio/hw-consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "stm32-dfsdm.h"

/* Conversion timeout */
#define DFSDM_TIMEOUT_US 100000
#define DFSDM_TIMEOUT (msecs_to_jiffies(DFSDM_TIMEOUT_US / 1000))

/* Oversampling attribute default */
#define DFSDM_DEFAULT_OVERSAMPLING  100

/* Oversampling max values */
#define DFSDM_MAX_INT_OVERSAMPLING 256
#define DFSDM_MAX_FL_OVERSAMPLING 1024

/* Max sample resolutions */
#define DFSDM_MAX_RES BIT(31)
#define DFSDM_DATA_RES BIT(23)

enum sd_converter_type {
	DFSDM_AUDIO,
	DFSDM_IIO,
};

struct stm32_dfsdm_dev_data {
	int type;
	int (*init)(struct iio_dev *indio_dev);
	unsigned int num_channels;
	const struct regmap_config *regmap_cfg;
};

struct stm32_dfsdm_adc {
	struct stm32_dfsdm *dfsdm;
	const struct stm32_dfsdm_dev_data *dev_data;
	unsigned int fl_id;
	unsigned int ch_id;

	/* ADC specific */
	unsigned int oversamp;
	struct iio_hw_consumer *hwc;
	struct completion completion;
	u32 *buffer;

};

struct stm32_dfsdm_str2field {
	const char	*name;
	unsigned int	val;
};

/* DFSDM channel serial interface type */
static const struct stm32_dfsdm_str2field stm32_dfsdm_chan_type[] = {
	{ "SPI_R", 0 }, /* SPI with data on rising edge */
	{ "SPI_F", 1 }, /* SPI with data on falling edge */
	{ "MANCH_R", 2 }, /* Manchester codec, rising edge = logic 0 */
	{ "MANCH_F", 3 }, /* Manchester codec, falling edge = logic 1 */
	{},
};

/* DFSDM channel clock source */
static const struct stm32_dfsdm_str2field stm32_dfsdm_chan_src[] = {
	/* External SPI clock (CLKIN x) */
	{ "CLKIN", DFSDM_CHANNEL_SPI_CLOCK_EXTERNAL },
	/* Internal SPI clock (CLKOUT) */
	{ "CLKOUT", DFSDM_CHANNEL_SPI_CLOCK_INTERNAL },
	/* Internal SPI clock divided by 2 (falling edge) */
	{ "CLKOUT_F", DFSDM_CHANNEL_SPI_CLOCK_INTERNAL_DIV2_FALLING },
	/* Internal SPI clock divided by 2 (falling edge) */
	{ "CLKOUT_R", DFSDM_CHANNEL_SPI_CLOCK_INTERNAL_DIV2_RISING },
	{},
};

static int stm32_dfsdm_str2val(const char *str,
			       const struct stm32_dfsdm_str2field *list)
{
	const struct stm32_dfsdm_str2field *p = list;

	for (p = list; p && p->name; p++)
		if (!strcmp(p->name, str))
			return p->val;

	return -EINVAL;
}

static int stm32_dfsdm_set_osrs(struct stm32_dfsdm_filter *fl,
				unsigned int fast, unsigned int oversamp)
{
	unsigned int i, d, fosr, iosr;
	u64 res;
	s64 delta;
	unsigned int m = 1;	/* multiplication factor */
	unsigned int p = fl->ford;	/* filter order (ford) */

	pr_debug("%s: Requested oversampling: %d\n",  __func__, oversamp);
	/*
	 * This function tries to compute filter oversampling and integrator
	 * oversampling, base on oversampling ratio requested by user.
	 *
	 * Decimation d depends on the filter order and the oversampling ratios.
	 * ford: filter order
	 * fosr: filter over sampling ratio
	 * iosr: integrator over sampling ratio
	 */
	if (fl->ford == DFSDM_FASTSINC_ORDER) {
		m = 2;
		p = 2;
	}

	/*
	 * Look for filter and integrator oversampling ratios which allows
	 * to reach 24 bits data output resolution.
	 * Leave as soon as if exact resolution if reached.
	 * Otherwise the higher resolution below 32 bits is kept.
	 */
	for (fosr = 1; fosr <= DFSDM_MAX_FL_OVERSAMPLING; fosr++) {
		for (iosr = 1; iosr <= DFSDM_MAX_INT_OVERSAMPLING; iosr++) {
			if (fast)
				d = fosr * iosr;
			else if (fl->ford == DFSDM_FASTSINC_ORDER)
				d = fosr * (iosr + 3) + 2;
			else
				d = fosr * (iosr - 1 + p) + p;

			if (d > oversamp)
				break;
			else if (d != oversamp)
				continue;
			/*
			 * Check resolution (limited to signed 32 bits)
			 *   res <= 2^31
			 * Sincx filters:
			 *   res = m * fosr^p x iosr (with m=1, p=ford)
			 * FastSinc filter
			 *   res = m * fosr^p x iosr (with m=2, p=2)
			 */
			res = fosr;
			for (i = p - 1; i > 0; i--) {
				res = res * (u64)fosr;
				if (res > DFSDM_MAX_RES)
					break;
			}
			if (res > DFSDM_MAX_RES)
				continue;
			res = res * (u64)m * (u64)iosr;
			if (res > DFSDM_MAX_RES)
				continue;

			delta = res - DFSDM_DATA_RES;

			if (res >= fl->res) {
				fl->res = res;
				fl->fosr = fosr;
				fl->iosr = iosr;
				fl->fast = fast;
				pr_debug("%s: fosr = %d, iosr = %d\n",
					 __func__, fl->fosr, fl->iosr);
			}

			if (!delta)
				return 0;
		}
	}

	if (!fl->fosr)
		return -EINVAL;

	return 0;
}

static int stm32_dfsdm_start_channel(struct stm32_dfsdm *dfsdm,
				     unsigned int ch_id)
{
	return regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(ch_id),
				  DFSDM_CHCFGR1_CHEN_MASK,
				  DFSDM_CHCFGR1_CHEN(1));
}

static void stm32_dfsdm_stop_channel(struct stm32_dfsdm *dfsdm,
				     unsigned int ch_id)
{
	regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(ch_id),
			   DFSDM_CHCFGR1_CHEN_MASK, DFSDM_CHCFGR1_CHEN(0));
}

static int stm32_dfsdm_chan_configure(struct stm32_dfsdm *dfsdm,
				      struct stm32_dfsdm_channel *ch)
{
	unsigned int id = ch->id;
	struct regmap *regmap = dfsdm->regmap;
	int ret;

	ret = regmap_update_bits(regmap, DFSDM_CHCFGR1(id),
				 DFSDM_CHCFGR1_SITP_MASK,
				 DFSDM_CHCFGR1_SITP(ch->type));
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(regmap, DFSDM_CHCFGR1(id),
				 DFSDM_CHCFGR1_SPICKSEL_MASK,
				 DFSDM_CHCFGR1_SPICKSEL(ch->src));
	if (ret < 0)
		return ret;
	return regmap_update_bits(regmap, DFSDM_CHCFGR1(id),
				  DFSDM_CHCFGR1_CHINSEL_MASK,
				  DFSDM_CHCFGR1_CHINSEL(ch->alt_si));
}

static int stm32_dfsdm_start_filter(struct stm32_dfsdm *dfsdm,
				    unsigned int fl_id)
{
	int ret;

	/* Enable filter */
	ret = regmap_update_bits(dfsdm->regmap, DFSDM_CR1(fl_id),
				 DFSDM_CR1_DFEN_MASK, DFSDM_CR1_DFEN(1));
	if (ret < 0)
		return ret;

	/* Start conversion */
	return regmap_update_bits(dfsdm->regmap, DFSDM_CR1(fl_id),
				  DFSDM_CR1_RSWSTART_MASK,
				  DFSDM_CR1_RSWSTART(1));
}

void stm32_dfsdm_stop_filter(struct stm32_dfsdm *dfsdm, unsigned int fl_id)
{
	/* Disable conversion */
	regmap_update_bits(dfsdm->regmap, DFSDM_CR1(fl_id),
			   DFSDM_CR1_DFEN_MASK, DFSDM_CR1_DFEN(0));
}

static int stm32_dfsdm_filter_configure(struct stm32_dfsdm *dfsdm,
					unsigned int fl_id, unsigned int ch_id)
{
	struct regmap *regmap = dfsdm->regmap;
	struct stm32_dfsdm_filter *fl = &dfsdm->fl_list[fl_id];
	int ret;

	/* Average integrator oversampling */
	ret = regmap_update_bits(regmap, DFSDM_FCR(fl_id), DFSDM_FCR_IOSR_MASK,
				 DFSDM_FCR_IOSR(fl->iosr - 1));
	if (ret)
		return ret;

	/* Filter order and Oversampling */
	ret = regmap_update_bits(regmap, DFSDM_FCR(fl_id), DFSDM_FCR_FOSR_MASK,
				 DFSDM_FCR_FOSR(fl->fosr - 1));
	if (ret)
		return ret;

	ret = regmap_update_bits(regmap, DFSDM_FCR(fl_id), DFSDM_FCR_FORD_MASK,
				 DFSDM_FCR_FORD(fl->ford));
	if (ret)
		return ret;

	/* No scan mode supported for the moment */
	ret = regmap_update_bits(regmap, DFSDM_CR1(fl_id), DFSDM_CR1_RCH_MASK,
				 DFSDM_CR1_RCH(ch_id));
	if (ret)
		return ret;

	return regmap_update_bits(regmap, DFSDM_CR1(fl_id),
				  DFSDM_CR1_RSYNC_MASK,
				  DFSDM_CR1_RSYNC(fl->sync_mode));
}

int stm32_dfsdm_channel_parse_of(struct stm32_dfsdm *dfsdm,
				 struct iio_dev *indio_dev,
				 struct iio_chan_spec *ch)
{
	struct stm32_dfsdm_channel *df_ch;
	const char *of_str;
	int chan_idx = ch->scan_index;
	int ret, val;

	ret = of_property_read_u32_index(indio_dev->dev.of_node,
					 "st,adc-channels", chan_idx,
					 &ch->channel);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			" Error parsing 'st,adc-channels' for idx %d\n",
			chan_idx);
		return ret;
	}
	if (ch->channel >= dfsdm->num_chs) {
		dev_err(&indio_dev->dev,
			" Error bad channel number %d (max = %d)\n",
			ch->channel, dfsdm->num_chs);
		return -EINVAL;
	}

	ret = of_property_read_string_index(indio_dev->dev.of_node,
					    "st,adc-channel-names", chan_idx,
					    &ch->datasheet_name);
	if (ret < 0) {
		dev_err(&indio_dev->dev,
			" Error parsing 'st,adc-channel-names' for idx %d\n",
			chan_idx);
		return ret;
	}

	df_ch =  &dfsdm->ch_list[ch->channel];
	df_ch->id = ch->channel;

	ret = of_property_read_string_index(indio_dev->dev.of_node,
					    "st,adc-channel-types", chan_idx,
					    &of_str);
	if (!ret) {
		val  = stm32_dfsdm_str2val(of_str, stm32_dfsdm_chan_type);
		if (val < 0)
			return val;
	} else {
		val = 0;
	}
	df_ch->type = val;

	ret = of_property_read_string_index(indio_dev->dev.of_node,
					    "st,adc-channel-clk-src", chan_idx,
					    &of_str);
	if (!ret) {
		val  = stm32_dfsdm_str2val(of_str, stm32_dfsdm_chan_src);
		if (val < 0)
			return val;
	} else {
		val = 0;
	}
	df_ch->src = val;

	ret = of_property_read_u32_index(indio_dev->dev.of_node,
					 "st,adc-alt-channel", chan_idx,
					 &df_ch->alt_si);
	if (ret < 0)
		df_ch->alt_si = 0;

	return 0;
}

static int stm32_dfsdm_start_conv(struct stm32_dfsdm_adc *adc, bool dma)
{
	struct regmap *regmap = adc->dfsdm->regmap;
	int ret;

	ret = stm32_dfsdm_start_channel(adc->dfsdm, adc->ch_id);
	if (ret < 0)
		return ret;

	ret = stm32_dfsdm_filter_configure(adc->dfsdm, adc->fl_id,
					   adc->ch_id);
	if (ret < 0)
		goto stop_channels;

	ret = stm32_dfsdm_start_filter(adc->dfsdm, adc->fl_id);
	if (ret < 0)
		goto stop_channels;

	return 0;

stop_channels:
	regmap_update_bits(regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_RDMAEN_MASK, 0);

	regmap_update_bits(regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_RCONT_MASK, 0);
	stm32_dfsdm_stop_channel(adc->dfsdm, adc->fl_id);

	return ret;
}

static void stm32_dfsdm_stop_conv(struct stm32_dfsdm_adc *adc)
{
	struct regmap *regmap = adc->dfsdm->regmap;

	stm32_dfsdm_stop_filter(adc->dfsdm, adc->fl_id);

	/* Clean conversion options */
	regmap_update_bits(regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_RDMAEN_MASK, 0);

	regmap_update_bits(regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_RCONT_MASK, 0);

	stm32_dfsdm_stop_channel(adc->dfsdm, adc->ch_id);
}

static int stm32_dfsdm_single_conv(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan, int *res)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	long timeout;
	int ret;

	reinit_completion(&adc->completion);

	adc->buffer = res;

	ret = stm32_dfsdm_start_dfsdm(adc->dfsdm);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(adc->dfsdm->regmap, DFSDM_CR2(adc->fl_id),
				 DFSDM_CR2_REOCIE_MASK, DFSDM_CR2_REOCIE(1));
	if (ret < 0)
		goto stop_dfsdm;

	ret = stm32_dfsdm_start_conv(adc, false);
	if (ret < 0) {
		regmap_update_bits(adc->dfsdm->regmap, DFSDM_CR2(adc->fl_id),
				   DFSDM_CR2_REOCIE_MASK, DFSDM_CR2_REOCIE(0));
		goto stop_dfsdm;
	}

	timeout = wait_for_completion_interruptible_timeout(&adc->completion,
							    DFSDM_TIMEOUT);

	/* Mask IRQ for regular conversion achievement*/
	regmap_update_bits(adc->dfsdm->regmap, DFSDM_CR2(adc->fl_id),
			   DFSDM_CR2_REOCIE_MASK, DFSDM_CR2_REOCIE(0));

	if (timeout == 0)
		ret = -ETIMEDOUT;
	else if (timeout < 0)
		ret = timeout;
	else
		ret = IIO_VAL_INT;

	stm32_dfsdm_stop_conv(adc);

stop_dfsdm:
	stm32_dfsdm_stop_dfsdm(adc->dfsdm);

	return ret;
}

static int stm32_dfsdm_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	struct stm32_dfsdm_filter *fl = &adc->dfsdm->fl_list[adc->fl_id];
	int ret = -EINVAL;

	if (mask == IIO_CHAN_INFO_OVERSAMPLING_RATIO) {
		ret = stm32_dfsdm_set_osrs(fl, 0, val);
		if (!ret)
			adc->oversamp = val;
	}

	return ret;
}

static int stm32_dfsdm_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int *val,
				int *val2, long mask)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_hw_consumer_enable(adc->hwc);
		if (ret < 0) {
			dev_err(&indio_dev->dev,
				"%s: IIO enable failed (channel %d)\n",
				__func__, chan->channel);
			return ret;
		}
		ret = stm32_dfsdm_single_conv(indio_dev, chan, val);
		iio_hw_consumer_disable(adc->hwc);
		if (ret < 0) {
			dev_err(&indio_dev->dev,
				"%s: Conversion failed (channel %d)\n",
				__func__, chan->channel);
			return ret;
		}
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*val = adc->oversamp;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info stm32_dfsdm_info_adc = {
	.read_raw = stm32_dfsdm_read_raw,
	.write_raw = stm32_dfsdm_write_raw,
};

static irqreturn_t stm32_dfsdm_irq(int irq, void *arg)
{
	struct stm32_dfsdm_adc *adc = arg;
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct regmap *regmap = adc->dfsdm->regmap;
	unsigned int status, int_en;

	regmap_read(regmap, DFSDM_ISR(adc->fl_id), &status);
	regmap_read(regmap, DFSDM_CR2(adc->fl_id), &int_en);

	if (status & DFSDM_ISR_REOCF_MASK) {
		/* Read the data register clean the IRQ status */
		regmap_read(regmap, DFSDM_RDATAR(adc->fl_id), adc->buffer);
		complete(&adc->completion);
	}

	if (status & DFSDM_ISR_ROVRF_MASK) {
		if (int_en & DFSDM_CR2_ROVRIE_MASK)
			dev_warn(&indio_dev->dev, "Overrun detected\n");
		regmap_update_bits(regmap, DFSDM_ICR(adc->fl_id),
				   DFSDM_ICR_CLRROVRF_MASK,
				   DFSDM_ICR_CLRROVRF_MASK);
	}

	return IRQ_HANDLED;
}

static int stm32_dfsdm_adc_chan_init_one(struct iio_dev *indio_dev,
					 struct iio_chan_spec *ch)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = stm32_dfsdm_channel_parse_of(adc->dfsdm, indio_dev, ch);
	if (ret < 0)
		return ret;

	ch->type = IIO_VOLTAGE;
	ch->indexed = 1;

	/*
	 * IIO_CHAN_INFO_RAW: used to compute regular conversion
	 * IIO_CHAN_INFO_OVERSAMPLING_RATIO: used to set oversampling
	 */
	ch->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
	ch->info_mask_shared_by_all = BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO);

	ch->scan_type.sign = 'u';
	ch->scan_type.realbits = 24;
	ch->scan_type.storagebits = 32;
	adc->ch_id = ch->channel;

	return stm32_dfsdm_chan_configure(adc->dfsdm,
					  &adc->dfsdm->ch_list[ch->channel]);
}

static int stm32_dfsdm_adc_init(struct iio_dev *indio_dev)
{
	struct iio_chan_spec *ch;
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	int num_ch;
	int ret, chan_idx;

	adc->oversamp = DFSDM_DEFAULT_OVERSAMPLING;
	ret = stm32_dfsdm_set_osrs(&adc->dfsdm->fl_list[adc->fl_id], 0,
				   adc->oversamp);
	if (ret < 0)
		return ret;

	num_ch = of_property_count_u32_elems(indio_dev->dev.of_node,
					     "st,adc-channels");
	if (num_ch < 0 || num_ch > adc->dfsdm->num_chs) {
		dev_err(&indio_dev->dev, "Bad st,adc-channels\n");
		return num_ch < 0 ? num_ch : -EINVAL;
	}

	/* Bind to SD modulator IIO device */
	adc->hwc = devm_iio_hw_consumer_alloc(&indio_dev->dev);
	if (IS_ERR(adc->hwc))
		return -EPROBE_DEFER;

	ch = devm_kcalloc(&indio_dev->dev, num_ch, sizeof(*ch),
			  GFP_KERNEL);
	if (!ch)
		return -ENOMEM;

	for (chan_idx = 0; chan_idx < num_ch; chan_idx++) {
		ch->scan_index = chan_idx;
		ret = stm32_dfsdm_adc_chan_init_one(indio_dev, ch);
		if (ret < 0) {
			dev_err(&indio_dev->dev, "Channels init failed\n");
			return ret;
		}
	}

	indio_dev->num_channels = num_ch;
	indio_dev->channels = ch;

	init_completion(&adc->completion);

	return 0;
}

static const struct stm32_dfsdm_dev_data stm32h7_dfsdm_adc_data = {
	.type = DFSDM_IIO,
	.init = stm32_dfsdm_adc_init,
};

static const struct of_device_id stm32_dfsdm_adc_match[] = {
	{
		.compatible = "st,stm32-dfsdm-adc",
		.data = &stm32h7_dfsdm_adc_data,
	},
	{}
};

static int stm32_dfsdm_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_dfsdm_adc *adc;
	struct device_node *np = dev->of_node;
	const struct stm32_dfsdm_dev_data *dev_data;
	struct iio_dev *iio;
	const struct of_device_id *of_id;
	char *name;
	int ret, irq, val;

	of_id = of_match_node(stm32_dfsdm_adc_match, np);
	if (!of_id->data) {
		dev_err(&pdev->dev, "Data associated to device is missing\n");
		return -EINVAL;
	}

	dev_data = (const struct stm32_dfsdm_dev_data *)of_id->data;

	iio = devm_iio_device_alloc(dev, sizeof(*adc));
	if (IS_ERR(iio)) {
		dev_err(dev, "%s: Failed to allocate IIO\n", __func__);
		return PTR_ERR(iio);
	}

	adc = iio_priv(iio);
	if (IS_ERR(adc)) {
		dev_err(dev, "%s: Failed to allocate ADC\n", __func__);
		return PTR_ERR(adc);
	}
	adc->dfsdm = dev_get_drvdata(dev->parent);

	iio->dev.parent = dev;
	iio->dev.of_node = np;
	iio->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;

	platform_set_drvdata(pdev, adc);

	ret = of_property_read_u32(dev->of_node, "reg", &adc->fl_id);
	if (ret != 0) {
		dev_err(dev, "Missing reg property\n");
		return -EINVAL;
	}

	name = devm_kzalloc(dev, sizeof("dfsdm-adc0"), GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	iio->info = &stm32_dfsdm_info_adc;
	snprintf(name, sizeof("dfsdm-adc0"), "dfsdm-adc%d", adc->fl_id);
	iio->name = name;

	/*
	 * In a first step IRQs generated for channels are not treated.
	 * So IRQ associated to filter instance 0 is dedicated to the Filter 0.
	 */
	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, irq, stm32_dfsdm_irq,
			       0, pdev->name, adc);
	if (ret < 0) {
		dev_err(dev, "Failed to request IRQ\n");
		return ret;
	}

	ret = of_property_read_u32(dev->of_node, "st,filter-order", &val);
	if (ret < 0) {
		dev_err(dev, "Failed to set filter order\n");
		return ret;
	}

	adc->dfsdm->fl_list[adc->fl_id].ford = val;

	ret = of_property_read_u32(dev->of_node, "st,filter0-sync", &val);
	if (!ret)
		adc->dfsdm->fl_list[adc->fl_id].sync_mode = val;

	adc->dev_data = dev_data;
	ret = dev_data->init(iio);
	if (ret < 0)
		return ret;

	return iio_device_register(iio);
}

static int stm32_dfsdm_adc_remove(struct platform_device *pdev)
{
	struct stm32_dfsdm_adc *adc = platform_get_drvdata(pdev);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	iio_device_unregister(indio_dev);

	return 0;
}

static struct platform_driver stm32_dfsdm_adc_driver = {
	.driver = {
		.name = "stm32-dfsdm-adc",
		.of_match_table = stm32_dfsdm_adc_match,
	},
	.probe = stm32_dfsdm_adc_probe,
	.remove = stm32_dfsdm_adc_remove,
};
module_platform_driver(stm32_dfsdm_adc_driver);

MODULE_DESCRIPTION("STM32 sigma delta ADC");
MODULE_AUTHOR("Arnaud Pouliquen <arnaud.pouliquen@st.com>");
MODULE_LICENSE("GPL v2");
