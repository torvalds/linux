// SPDX-License-Identifier: GPL-2.0
/*
 * This file is the ADC part of the STM32 DFSDM driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Arnaud Pouliquen <arnaud.pouliquen@st.com>.
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/iio/adc/stm32-dfsdm-adc.h>
#include <linux/iio/buffer.h>
#include <linux/iio/hw-consumer.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "stm32-dfsdm.h"

#define DFSDM_DMA_BUFFER_SIZE (4 * PAGE_SIZE)

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

/* Filter configuration */
#define DFSDM_CR1_CFG_MASK (DFSDM_CR1_RCH_MASK | DFSDM_CR1_RCONT_MASK | \
			    DFSDM_CR1_RSYNC_MASK | DFSDM_CR1_JSYNC_MASK | \
			    DFSDM_CR1_JSCAN_MASK)

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
	unsigned int nconv;
	unsigned long smask;

	/* ADC specific */
	unsigned int oversamp;
	struct iio_hw_consumer *hwc;
	struct completion completion;
	u32 *buffer;

	/* Audio specific */
	unsigned int spi_freq;  /* SPI bus clock frequency */
	unsigned int sample_freq; /* Sample frequency after filter decimation */
	int (*cb)(const void *data, size_t size, void *cb_priv);
	void *cb_priv;

	/* DMA */
	u8 *rx_buf;
	unsigned int bufi; /* Buffer current position */
	unsigned int buf_sz; /* Buffer size */
	struct dma_chan	*dma_chan;
	dma_addr_t dma_buf;
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
	fl->res = 0;
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

	if (!fl->res)
		return -EINVAL;

	return 0;
}

static int stm32_dfsdm_start_channel(struct stm32_dfsdm_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct regmap *regmap = adc->dfsdm->regmap;
	const struct iio_chan_spec *chan;
	unsigned int bit;
	int ret;

	for_each_set_bit(bit, &adc->smask, sizeof(adc->smask) * BITS_PER_BYTE) {
		chan = indio_dev->channels + bit;
		ret = regmap_update_bits(regmap, DFSDM_CHCFGR1(chan->channel),
					 DFSDM_CHCFGR1_CHEN_MASK,
					 DFSDM_CHCFGR1_CHEN(1));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void stm32_dfsdm_stop_channel(struct stm32_dfsdm_adc *adc)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct regmap *regmap = adc->dfsdm->regmap;
	const struct iio_chan_spec *chan;
	unsigned int bit;

	for_each_set_bit(bit, &adc->smask, sizeof(adc->smask) * BITS_PER_BYTE) {
		chan = indio_dev->channels + bit;
		regmap_update_bits(regmap, DFSDM_CHCFGR1(chan->channel),
				   DFSDM_CHCFGR1_CHEN_MASK,
				   DFSDM_CHCFGR1_CHEN(0));
	}
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

static int stm32_dfsdm_start_filter(struct stm32_dfsdm_adc *adc,
				    unsigned int fl_id)
{
	struct stm32_dfsdm *dfsdm = adc->dfsdm;
	int ret;

	/* Enable filter */
	ret = regmap_update_bits(dfsdm->regmap, DFSDM_CR1(fl_id),
				 DFSDM_CR1_DFEN_MASK, DFSDM_CR1_DFEN(1));
	if (ret < 0)
		return ret;

	/* Nothing more to do for injected (scan mode/triggered) conversions */
	if (adc->nconv > 1)
		return 0;

	/* Software start (single or continuous) regular conversion */
	return regmap_update_bits(dfsdm->regmap, DFSDM_CR1(fl_id),
				  DFSDM_CR1_RSWSTART_MASK,
				  DFSDM_CR1_RSWSTART(1));
}

static void stm32_dfsdm_stop_filter(struct stm32_dfsdm *dfsdm,
				    unsigned int fl_id)
{
	/* Disable conversion */
	regmap_update_bits(dfsdm->regmap, DFSDM_CR1(fl_id),
			   DFSDM_CR1_DFEN_MASK, DFSDM_CR1_DFEN(0));
}

static int stm32_dfsdm_filter_configure(struct stm32_dfsdm_adc *adc,
					unsigned int fl_id)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);
	struct regmap *regmap = adc->dfsdm->regmap;
	struct stm32_dfsdm_filter *fl = &adc->dfsdm->fl_list[fl_id];
	u32 cr1;
	const struct iio_chan_spec *chan;
	unsigned int bit, jchg = 0;
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

	/*
	 * DFSDM modes configuration W.R.T audio/iio type modes
	 * ----------------------------------------------------------------
	 * Modes         | regular |  regular     | injected | injected   |
	 *               |         |  continuous  |          | + scan     |
	 * --------------|---------|--------------|----------|------------|
	 * single conv   |    x    |              |          |            |
	 * (1 chan)      |         |              |          |            |
	 * --------------|---------|--------------|----------|------------|
	 * 1 Audio chan	 |         | sample freq  |          |            |
	 *               |         | or sync_mode |          |            |
	 * --------------|---------|--------------|----------|------------|
	 * 1 IIO chan	 |         | sample freq  | trigger  |            |
	 *               |         | or sync_mode |          |            |
	 * --------------|---------|--------------|----------|------------|
	 * 2+ IIO chans  |         |              |          | trigger or |
	 *               |         |              |          | sync_mode  |
	 * ----------------------------------------------------------------
	 */
	if (adc->nconv == 1) {
		bit = __ffs(adc->smask);
		chan = indio_dev->channels + bit;

		/* Use regular conversion for single channel without trigger */
		cr1 = DFSDM_CR1_RCH(chan->channel);

		/* Continuous conversions triggered by SPI clk in buffer mode */
		if (indio_dev->currentmode & INDIO_BUFFER_SOFTWARE)
			cr1 |= DFSDM_CR1_RCONT(1);

		cr1 |= DFSDM_CR1_RSYNC(fl->sync_mode);
	} else {
		/* Use injected conversion for multiple channels */
		for_each_set_bit(bit, &adc->smask,
				 sizeof(adc->smask) * BITS_PER_BYTE) {
			chan = indio_dev->channels + bit;
			jchg |= BIT(chan->channel);
		}
		ret = regmap_write(regmap, DFSDM_JCHGR(fl_id), jchg);
		if (ret < 0)
			return ret;

		/* Use scan mode for multiple channels */
		cr1 = DFSDM_CR1_JSCAN(1);

		/*
		 * Continuous conversions not supported in injected mode:
		 * - use conversions in sync with filter 0
		 */
		if (!fl->sync_mode)
			return -EINVAL;
		cr1 |= DFSDM_CR1_JSYNC(fl->sync_mode);
	}

	return regmap_update_bits(regmap, DFSDM_CR1(fl_id), DFSDM_CR1_CFG_MASK,
				  cr1);
}

static int stm32_dfsdm_channel_parse_of(struct stm32_dfsdm *dfsdm,
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
		val = stm32_dfsdm_str2val(of_str, stm32_dfsdm_chan_type);
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
		val = stm32_dfsdm_str2val(of_str, stm32_dfsdm_chan_src);
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

static ssize_t dfsdm_adc_audio_get_spiclk(struct iio_dev *indio_dev,
					  uintptr_t priv,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", adc->spi_freq);
}

static ssize_t dfsdm_adc_audio_set_spiclk(struct iio_dev *indio_dev,
					  uintptr_t priv,
					  const struct iio_chan_spec *chan,
					  const char *buf, size_t len)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	struct stm32_dfsdm_filter *fl = &adc->dfsdm->fl_list[adc->fl_id];
	struct stm32_dfsdm_channel *ch = &adc->dfsdm->ch_list[chan->channel];
	unsigned int sample_freq = adc->sample_freq;
	unsigned int spi_freq;
	int ret;

	dev_err(&indio_dev->dev, "enter %s\n", __func__);
	/* If DFSDM is master on SPI, SPI freq can not be updated */
	if (ch->src != DFSDM_CHANNEL_SPI_CLOCK_EXTERNAL)
		return -EPERM;

	ret = kstrtoint(buf, 0, &spi_freq);
	if (ret)
		return ret;

	if (!spi_freq)
		return -EINVAL;

	if (sample_freq) {
		if (spi_freq % sample_freq)
			dev_warn(&indio_dev->dev,
				 "Sampling rate not accurate (%d)\n",
				 spi_freq / (spi_freq / sample_freq));

		ret = stm32_dfsdm_set_osrs(fl, 0, (spi_freq / sample_freq));
		if (ret < 0) {
			dev_err(&indio_dev->dev,
				"No filter parameters that match!\n");
			return ret;
		}
	}
	adc->spi_freq = spi_freq;

	return len;
}

static int stm32_dfsdm_start_conv(struct stm32_dfsdm_adc *adc)
{
	struct regmap *regmap = adc->dfsdm->regmap;
	int ret;

	ret = stm32_dfsdm_start_channel(adc);
	if (ret < 0)
		return ret;

	ret = stm32_dfsdm_filter_configure(adc, adc->fl_id);
	if (ret < 0)
		goto stop_channels;

	ret = stm32_dfsdm_start_filter(adc, adc->fl_id);
	if (ret < 0)
		goto filter_unconfigure;

	return 0;

filter_unconfigure:
	regmap_update_bits(regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_CFG_MASK, 0);
stop_channels:
	stm32_dfsdm_stop_channel(adc);

	return ret;
}

static void stm32_dfsdm_stop_conv(struct stm32_dfsdm_adc *adc)
{
	struct regmap *regmap = adc->dfsdm->regmap;

	stm32_dfsdm_stop_filter(adc->dfsdm, adc->fl_id);

	regmap_update_bits(regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_CFG_MASK, 0);

	stm32_dfsdm_stop_channel(adc);
}

static int stm32_dfsdm_set_watermark(struct iio_dev *indio_dev,
				     unsigned int val)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	unsigned int watermark = DFSDM_DMA_BUFFER_SIZE / 2;

	/*
	 * DMA cyclic transfers are used, buffer is split into two periods.
	 * There should be :
	 * - always one buffer (period) DMA is working on
	 * - one buffer (period) driver pushed to ASoC side.
	 */
	watermark = min(watermark, val * (unsigned int)(sizeof(u32)));
	adc->buf_sz = watermark * 2;

	return 0;
}

static unsigned int stm32_dfsdm_adc_dma_residue(struct stm32_dfsdm_adc *adc)
{
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(adc->dma_chan,
				     adc->dma_chan->cookie,
				     &state);
	if (status == DMA_IN_PROGRESS) {
		/* Residue is size in bytes from end of buffer */
		unsigned int i = adc->buf_sz - state.residue;
		unsigned int size;

		/* Return available bytes */
		if (i >= adc->bufi)
			size = i - adc->bufi;
		else
			size = adc->buf_sz + i - adc->bufi;

		return size;
	}

	return 0;
}

static void stm32_dfsdm_audio_dma_buffer_done(void *data)
{
	struct iio_dev *indio_dev = data;
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	int available = stm32_dfsdm_adc_dma_residue(adc);
	size_t old_pos;

	/*
	 * FIXME: In Kernel interface does not support cyclic DMA buffer,and
	 * offers only an interface to push data samples per samples.
	 * For this reason IIO buffer interface is not used and interface is
	 * bypassed using a private callback registered by ASoC.
	 * This should be a temporary solution waiting a cyclic DMA engine
	 * support in IIO.
	 */

	dev_dbg(&indio_dev->dev, "%s: pos = %d, available = %d\n", __func__,
		adc->bufi, available);
	old_pos = adc->bufi;

	while (available >= indio_dev->scan_bytes) {
		u32 *buffer = (u32 *)&adc->rx_buf[adc->bufi];

		/* Mask 8 LSB that contains the channel ID */
		*buffer = (*buffer & 0xFFFFFF00) << 8;
		available -= indio_dev->scan_bytes;
		adc->bufi += indio_dev->scan_bytes;
		if (adc->bufi >= adc->buf_sz) {
			if (adc->cb)
				adc->cb(&adc->rx_buf[old_pos],
					 adc->buf_sz - old_pos, adc->cb_priv);
			adc->bufi = 0;
			old_pos = 0;
		}
	}
	if (adc->cb)
		adc->cb(&adc->rx_buf[old_pos], adc->bufi - old_pos,
			adc->cb_priv);
}

static int stm32_dfsdm_adc_dma_start(struct iio_dev *indio_dev)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	struct dma_slave_config config = {
		.src_addr = (dma_addr_t)adc->dfsdm->phys_base,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
	};
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	int ret;

	if (!adc->dma_chan)
		return -EINVAL;

	dev_dbg(&indio_dev->dev, "%s size=%d watermark=%d\n", __func__,
		adc->buf_sz, adc->buf_sz / 2);

	if (adc->nconv == 1)
		config.src_addr += DFSDM_RDATAR(adc->fl_id);
	else
		config.src_addr += DFSDM_JDATAR(adc->fl_id);
	ret = dmaengine_slave_config(adc->dma_chan, &config);
	if (ret)
		return ret;

	/* Prepare a DMA cyclic transaction */
	desc = dmaengine_prep_dma_cyclic(adc->dma_chan,
					 adc->dma_buf,
					 adc->buf_sz, adc->buf_sz / 2,
					 DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT);
	if (!desc)
		return -EBUSY;

	desc->callback = stm32_dfsdm_audio_dma_buffer_done;
	desc->callback_param = indio_dev;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret)
		goto err_stop_dma;

	/* Issue pending DMA requests */
	dma_async_issue_pending(adc->dma_chan);

	if (adc->nconv == 1) {
		/* Enable regular DMA transfer*/
		ret = regmap_update_bits(adc->dfsdm->regmap,
					 DFSDM_CR1(adc->fl_id),
					 DFSDM_CR1_RDMAEN_MASK,
					 DFSDM_CR1_RDMAEN_MASK);
	} else {
		/* Enable injected DMA transfer*/
		ret = regmap_update_bits(adc->dfsdm->regmap,
					 DFSDM_CR1(adc->fl_id),
					 DFSDM_CR1_JDMAEN_MASK,
					 DFSDM_CR1_JDMAEN_MASK);
	}

	if (ret < 0)
		goto err_stop_dma;

	return 0;

err_stop_dma:
	dmaengine_terminate_all(adc->dma_chan);

	return ret;
}

static void stm32_dfsdm_adc_dma_stop(struct iio_dev *indio_dev)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);

	if (!adc->dma_chan)
		return;

	regmap_update_bits(adc->dfsdm->regmap, DFSDM_CR1(adc->fl_id),
			   DFSDM_CR1_RDMAEN_MASK | DFSDM_CR1_JDMAEN_MASK, 0);
	dmaengine_terminate_all(adc->dma_chan);
}

static int stm32_dfsdm_update_scan_mode(struct iio_dev *indio_dev,
					const unsigned long *scan_mask)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);

	adc->nconv = bitmap_weight(scan_mask, indio_dev->masklength);
	adc->smask = *scan_mask;

	dev_dbg(&indio_dev->dev, "nconv=%d mask=%lx\n", adc->nconv, *scan_mask);

	return 0;
}

static int stm32_dfsdm_postenable(struct iio_dev *indio_dev)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	int ret;

	/* Reset adc buffer index */
	adc->bufi = 0;

	if (adc->hwc) {
		ret = iio_hw_consumer_enable(adc->hwc);
		if (ret < 0)
			return ret;
	}

	ret = stm32_dfsdm_start_dfsdm(adc->dfsdm);
	if (ret < 0)
		goto err_stop_hwc;

	ret = stm32_dfsdm_adc_dma_start(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't start DMA\n");
		goto stop_dfsdm;
	}

	ret = stm32_dfsdm_start_conv(adc);
	if (ret) {
		dev_err(&indio_dev->dev, "Can't start conversion\n");
		goto err_stop_dma;
	}

	return 0;

err_stop_dma:
	stm32_dfsdm_adc_dma_stop(indio_dev);
stop_dfsdm:
	stm32_dfsdm_stop_dfsdm(adc->dfsdm);
err_stop_hwc:
	if (adc->hwc)
		iio_hw_consumer_disable(adc->hwc);

	return ret;
}

static int stm32_dfsdm_predisable(struct iio_dev *indio_dev)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);

	stm32_dfsdm_stop_conv(adc);

	stm32_dfsdm_adc_dma_stop(indio_dev);

	stm32_dfsdm_stop_dfsdm(adc->dfsdm);

	if (adc->hwc)
		iio_hw_consumer_disable(adc->hwc);

	return 0;
}

static const struct iio_buffer_setup_ops stm32_dfsdm_buffer_setup_ops = {
	.postenable = &stm32_dfsdm_postenable,
	.predisable = &stm32_dfsdm_predisable,
};

/**
 * stm32_dfsdm_get_buff_cb() - register a callback that will be called when
 *                             DMA transfer period is achieved.
 *
 * @iio_dev: Handle to IIO device.
 * @cb: Pointer to callback function:
 *      - data: pointer to data buffer
 *      - size: size in byte of the data buffer
 *      - private: pointer to consumer private structure.
 * @private: Pointer to consumer private structure.
 */
int stm32_dfsdm_get_buff_cb(struct iio_dev *iio_dev,
			    int (*cb)(const void *data, size_t size,
				      void *private),
			    void *private)
{
	struct stm32_dfsdm_adc *adc;

	if (!iio_dev)
		return -EINVAL;
	adc = iio_priv(iio_dev);

	adc->cb = cb;
	adc->cb_priv = private;

	return 0;
}
EXPORT_SYMBOL_GPL(stm32_dfsdm_get_buff_cb);

/**
 * stm32_dfsdm_release_buff_cb - unregister buffer callback
 *
 * @iio_dev: Handle to IIO device.
 */
int stm32_dfsdm_release_buff_cb(struct iio_dev *iio_dev)
{
	struct stm32_dfsdm_adc *adc;

	if (!iio_dev)
		return -EINVAL;
	adc = iio_priv(iio_dev);

	adc->cb = NULL;
	adc->cb_priv = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(stm32_dfsdm_release_buff_cb);

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

	adc->nconv = 1;
	adc->smask = BIT(chan->scan_index);
	ret = stm32_dfsdm_start_conv(adc);
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
	struct stm32_dfsdm_channel *ch = &adc->dfsdm->ch_list[chan->channel];
	unsigned int spi_freq;
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		ret = stm32_dfsdm_set_osrs(fl, 0, val);
		if (!ret)
			adc->oversamp = val;

		return ret;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (!val)
			return -EINVAL;

		switch (ch->src) {
		case DFSDM_CHANNEL_SPI_CLOCK_INTERNAL:
			spi_freq = adc->dfsdm->spi_master_freq;
			break;
		case DFSDM_CHANNEL_SPI_CLOCK_INTERNAL_DIV2_FALLING:
		case DFSDM_CHANNEL_SPI_CLOCK_INTERNAL_DIV2_RISING:
			spi_freq = adc->dfsdm->spi_master_freq / 2;
			break;
		default:
			spi_freq = adc->spi_freq;
		}

		if (spi_freq % val)
			dev_warn(&indio_dev->dev,
				 "Sampling rate not accurate (%d)\n",
				 spi_freq / (spi_freq / val));

		ret = stm32_dfsdm_set_osrs(fl, 0, (spi_freq / val));
		if (ret < 0) {
			dev_err(&indio_dev->dev,
				"Not able to find parameter that match!\n");
			return ret;
		}
		adc->sample_freq = val;

		return 0;
	}

	return -EINVAL;
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

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = adc->sample_freq;

		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_info stm32_dfsdm_info_audio = {
	.hwfifo_set_watermark = stm32_dfsdm_set_watermark,
	.read_raw = stm32_dfsdm_read_raw,
	.write_raw = stm32_dfsdm_write_raw,
	.update_scan_mode = stm32_dfsdm_update_scan_mode,
};

static const struct iio_info stm32_dfsdm_info_adc = {
	.read_raw = stm32_dfsdm_read_raw,
	.write_raw = stm32_dfsdm_write_raw,
	.update_scan_mode = stm32_dfsdm_update_scan_mode,
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

/*
 * Define external info for SPI Frequency and audio sampling rate that can be
 * configured by ASoC driver through consumer.h API
 */
static const struct iio_chan_spec_ext_info dfsdm_adc_audio_ext_info[] = {
	/* spi_clk_freq : clock freq on SPI/manchester bus used by channel */
	{
		.name = "spi_clk_freq",
		.shared = IIO_SHARED_BY_TYPE,
		.read = dfsdm_adc_audio_get_spiclk,
		.write = dfsdm_adc_audio_set_spiclk,
	},
	{},
};

static void stm32_dfsdm_dma_release(struct iio_dev *indio_dev)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);

	if (adc->dma_chan) {
		dma_free_coherent(adc->dma_chan->device->dev,
				  DFSDM_DMA_BUFFER_SIZE,
				  adc->rx_buf, adc->dma_buf);
		dma_release_channel(adc->dma_chan);
	}
}

static int stm32_dfsdm_dma_request(struct iio_dev *indio_dev)
{
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);

	adc->dma_chan = dma_request_slave_channel(&indio_dev->dev, "rx");
	if (!adc->dma_chan)
		return -EINVAL;

	adc->rx_buf = dma_alloc_coherent(adc->dma_chan->device->dev,
					 DFSDM_DMA_BUFFER_SIZE,
					 &adc->dma_buf, GFP_KERNEL);
	if (!adc->rx_buf) {
		dma_release_channel(adc->dma_chan);
		return -ENOMEM;
	}

	return 0;
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

	if (adc->dev_data->type == DFSDM_AUDIO) {
		ch->scan_type.sign = 's';
		ch->ext_info = dfsdm_adc_audio_ext_info;
	} else {
		ch->scan_type.sign = 'u';
	}
	ch->scan_type.realbits = 24;
	ch->scan_type.storagebits = 32;

	return stm32_dfsdm_chan_configure(adc->dfsdm,
					  &adc->dfsdm->ch_list[ch->channel]);
}

static int stm32_dfsdm_audio_init(struct iio_dev *indio_dev)
{
	struct iio_chan_spec *ch;
	struct stm32_dfsdm_adc *adc = iio_priv(indio_dev);
	struct stm32_dfsdm_channel *d_ch;
	int ret;

	indio_dev->modes |= INDIO_BUFFER_SOFTWARE;
	indio_dev->setup_ops = &stm32_dfsdm_buffer_setup_ops;

	ch = devm_kzalloc(&indio_dev->dev, sizeof(*ch), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;

	ch->scan_index = 0;

	ret = stm32_dfsdm_adc_chan_init_one(indio_dev, ch);
	if (ret < 0) {
		dev_err(&indio_dev->dev, "Channels init failed\n");
		return ret;
	}
	ch->info_mask_separate = BIT(IIO_CHAN_INFO_SAMP_FREQ);

	d_ch = &adc->dfsdm->ch_list[ch->channel];
	if (d_ch->src != DFSDM_CHANNEL_SPI_CLOCK_EXTERNAL)
		adc->spi_freq = adc->dfsdm->spi_master_freq;

	indio_dev->num_channels = 1;
	indio_dev->channels = ch;

	return stm32_dfsdm_dma_request(indio_dev);
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
		ch[chan_idx].scan_index = chan_idx;
		ret = stm32_dfsdm_adc_chan_init_one(indio_dev, &ch[chan_idx]);
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

static const struct stm32_dfsdm_dev_data stm32h7_dfsdm_audio_data = {
	.type = DFSDM_AUDIO,
	.init = stm32_dfsdm_audio_init,
};

static const struct of_device_id stm32_dfsdm_adc_match[] = {
	{
		.compatible = "st,stm32-dfsdm-adc",
		.data = &stm32h7_dfsdm_adc_data,
	},
	{
		.compatible = "st,stm32-dfsdm-dmic",
		.data = &stm32h7_dfsdm_audio_data,
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
	char *name;
	int ret, irq, val;

	dev_data = of_device_get_match_data(dev);
	iio = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!iio) {
		dev_err(dev, "%s: Failed to allocate IIO\n", __func__);
		return -ENOMEM;
	}

	adc = iio_priv(iio);
	adc->dfsdm = dev_get_drvdata(dev->parent);

	iio->dev.parent = dev;
	iio->dev.of_node = np;
	iio->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;

	platform_set_drvdata(pdev, adc);

	ret = of_property_read_u32(dev->of_node, "reg", &adc->fl_id);
	if (ret != 0 || adc->fl_id >= adc->dfsdm->num_fls) {
		dev_err(dev, "Missing or bad reg property\n");
		return -EINVAL;
	}

	name = devm_kzalloc(dev, sizeof("dfsdm-adc0"), GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	if (dev_data->type == DFSDM_AUDIO) {
		iio->info = &stm32_dfsdm_info_audio;
		snprintf(name, sizeof("dfsdm-pdm0"), "dfsdm-pdm%d", adc->fl_id);
	} else {
		iio->info = &stm32_dfsdm_info_adc;
		snprintf(name, sizeof("dfsdm-adc0"), "dfsdm-adc%d", adc->fl_id);
	}
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

	ret = iio_device_register(iio);
	if (ret < 0)
		goto err_cleanup;

	if (dev_data->type == DFSDM_AUDIO) {
		ret = of_platform_populate(np, NULL, NULL, dev);
		if (ret < 0) {
			dev_err(dev, "Failed to find an audio DAI\n");
			goto err_unregister;
		}
	}

	return 0;

err_unregister:
	iio_device_unregister(iio);
err_cleanup:
	stm32_dfsdm_dma_release(iio);

	return ret;
}

static int stm32_dfsdm_adc_remove(struct platform_device *pdev)
{
	struct stm32_dfsdm_adc *adc = platform_get_drvdata(pdev);
	struct iio_dev *indio_dev = iio_priv_to_dev(adc);

	if (adc->dev_data->type == DFSDM_AUDIO)
		of_platform_depopulate(&pdev->dev);
	iio_device_unregister(indio_dev);
	stm32_dfsdm_dma_release(indio_dev);

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
