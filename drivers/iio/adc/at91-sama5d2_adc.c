/*
 * Atmel ADC driver for SAMA5D2 devices and compatible.
 *
 * Copyright (C) 2015 Atmel,
 *               2015 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>

/* Control Register */
#define AT91_SAMA5D2_CR		0x00
/* Software Reset */
#define	AT91_SAMA5D2_CR_SWRST		BIT(0)
/* Start Conversion */
#define	AT91_SAMA5D2_CR_START		BIT(1)
/* Touchscreen Calibration */
#define	AT91_SAMA5D2_CR_TSCALIB		BIT(2)
/* Comparison Restart */
#define	AT91_SAMA5D2_CR_CMPRST		BIT(4)

/* Mode Register */
#define AT91_SAMA5D2_MR		0x04
/* Trigger Selection */
#define	AT91_SAMA5D2_MR_TRGSEL(v)	((v) << 1)
/* ADTRG */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG0	0
/* TIOA0 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG1	1
/* TIOA1 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG2	2
/* TIOA2 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG3	3
/* PWM event line 0 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG4	4
/* PWM event line 1 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG5	5
/* TIOA3 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG6	6
/* RTCOUT0 */
#define	AT91_SAMA5D2_MR_TRGSEL_TRIG7	7
/* Sleep Mode */
#define	AT91_SAMA5D2_MR_SLEEP		BIT(5)
/* Fast Wake Up */
#define	AT91_SAMA5D2_MR_FWUP		BIT(6)
/* Prescaler Rate Selection */
#define	AT91_SAMA5D2_MR_PRESCAL(v)	((v) << AT91_SAMA5D2_MR_PRESCAL_OFFSET)
#define	AT91_SAMA5D2_MR_PRESCAL_OFFSET	8
#define	AT91_SAMA5D2_MR_PRESCAL_MAX	0xff
#define AT91_SAMA5D2_MR_PRESCAL_MASK	GENMASK(15, 8)
/* Startup Time */
#define	AT91_SAMA5D2_MR_STARTUP(v)	((v) << 16)
#define AT91_SAMA5D2_MR_STARTUP_MASK	GENMASK(19, 16)
/* Analog Change */
#define	AT91_SAMA5D2_MR_ANACH		BIT(23)
/* Tracking Time */
#define	AT91_SAMA5D2_MR_TRACKTIM(v)	((v) << 24)
#define	AT91_SAMA5D2_MR_TRACKTIM_MAX	0xff
/* Transfer Time */
#define	AT91_SAMA5D2_MR_TRANSFER(v)	((v) << 28)
#define	AT91_SAMA5D2_MR_TRANSFER_MAX	0x3
/* Use Sequence Enable */
#define	AT91_SAMA5D2_MR_USEQ		BIT(31)

/* Channel Sequence Register 1 */
#define AT91_SAMA5D2_SEQR1	0x08
/* Channel Sequence Register 2 */
#define AT91_SAMA5D2_SEQR2	0x0c
/* Channel Enable Register */
#define AT91_SAMA5D2_CHER	0x10
/* Channel Disable Register */
#define AT91_SAMA5D2_CHDR	0x14
/* Channel Status Register */
#define AT91_SAMA5D2_CHSR	0x18
/* Last Converted Data Register */
#define AT91_SAMA5D2_LCDR	0x20
/* Interrupt Enable Register */
#define AT91_SAMA5D2_IER	0x24
/* Interrupt Enable Register - general overrun error */
#define AT91_SAMA5D2_IER_GOVRE BIT(25)
/* Interrupt Disable Register */
#define AT91_SAMA5D2_IDR	0x28
/* Interrupt Mask Register */
#define AT91_SAMA5D2_IMR	0x2c
/* Interrupt Status Register */
#define AT91_SAMA5D2_ISR	0x30
/* Last Channel Trigger Mode Register */
#define AT91_SAMA5D2_LCTMR	0x34
/* Last Channel Compare Window Register */
#define AT91_SAMA5D2_LCCWR	0x38
/* Overrun Status Register */
#define AT91_SAMA5D2_OVER	0x3c
/* Extended Mode Register */
#define AT91_SAMA5D2_EMR	0x40
/* Compare Window Register */
#define AT91_SAMA5D2_CWR	0x44
/* Channel Gain Register */
#define AT91_SAMA5D2_CGR	0x48

/* Channel Offset Register */
#define AT91_SAMA5D2_COR	0x4c
#define AT91_SAMA5D2_COR_DIFF_OFFSET	16

/* Channel Data Register 0 */
#define AT91_SAMA5D2_CDR0	0x50
/* Analog Control Register */
#define AT91_SAMA5D2_ACR	0x94
/* Touchscreen Mode Register */
#define AT91_SAMA5D2_TSMR	0xb0
/* Touchscreen X Position Register */
#define AT91_SAMA5D2_XPOSR	0xb4
/* Touchscreen Y Position Register */
#define AT91_SAMA5D2_YPOSR	0xb8
/* Touchscreen Pressure Register */
#define AT91_SAMA5D2_PRESSR	0xbc
/* Trigger Register */
#define AT91_SAMA5D2_TRGR	0xc0
/* Mask for TRGMOD field of TRGR register */
#define AT91_SAMA5D2_TRGR_TRGMOD_MASK GENMASK(2, 0)
/* No trigger, only software trigger can start conversions */
#define AT91_SAMA5D2_TRGR_TRGMOD_NO_TRIGGER 0
/* Trigger Mode external trigger rising edge */
#define AT91_SAMA5D2_TRGR_TRGMOD_EXT_TRIG_RISE 1
/* Trigger Mode external trigger falling edge */
#define AT91_SAMA5D2_TRGR_TRGMOD_EXT_TRIG_FALL 2
/* Trigger Mode external trigger any edge */
#define AT91_SAMA5D2_TRGR_TRGMOD_EXT_TRIG_ANY 3

/* Correction Select Register */
#define AT91_SAMA5D2_COSR	0xd0
/* Correction Value Register */
#define AT91_SAMA5D2_CVR	0xd4
/* Channel Error Correction Register */
#define AT91_SAMA5D2_CECR	0xd8
/* Write Protection Mode Register */
#define AT91_SAMA5D2_WPMR	0xe4
/* Write Protection Status Register */
#define AT91_SAMA5D2_WPSR	0xe8
/* Version Register */
#define AT91_SAMA5D2_VERSION	0xfc

#define AT91_SAMA5D2_HW_TRIG_CNT 3
#define AT91_SAMA5D2_SINGLE_CHAN_CNT 12
#define AT91_SAMA5D2_DIFF_CHAN_CNT 6

/*
 * Maximum number of bytes to hold conversion from all channels
 * without the timestamp.
 */
#define AT91_BUFFER_MAX_CONVERSION_BYTES ((AT91_SAMA5D2_SINGLE_CHAN_CNT + \
					 AT91_SAMA5D2_DIFF_CHAN_CNT) * 2)

/* This total must also include the timestamp */
#define AT91_BUFFER_MAX_BYTES (AT91_BUFFER_MAX_CONVERSION_BYTES + 8)

#define AT91_BUFFER_MAX_HWORDS (AT91_BUFFER_MAX_BYTES / 2)

#define AT91_HWFIFO_MAX_SIZE_STR	"128"
#define AT91_HWFIFO_MAX_SIZE		128

#define AT91_SAMA5D2_CHAN_SINGLE(num, addr)				\
	{								\
		.type = IIO_VOLTAGE,					\
		.channel = num,						\
		.address = addr,					\
		.scan_index = num,					\
		.scan_type = {						\
			.sign = 'u',					\
			.realbits = 12,					\
			.storagebits = 16,				\
		},							\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.datasheet_name = "CH"#num,				\
		.indexed = 1,						\
	}

#define AT91_SAMA5D2_CHAN_DIFF(num, num2, addr)				\
	{								\
		.type = IIO_VOLTAGE,					\
		.differential = 1,					\
		.channel = num,						\
		.channel2 = num2,					\
		.address = addr,					\
		.scan_index = num + AT91_SAMA5D2_SINGLE_CHAN_CNT,	\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 12,					\
			.storagebits = 16,				\
		},							\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.datasheet_name = "CH"#num"-CH"#num2,			\
		.indexed = 1,						\
	}

#define at91_adc_readl(st, reg)		readl_relaxed(st->base + reg)
#define at91_adc_writel(st, reg, val)	writel_relaxed(val, st->base + reg)

struct at91_adc_soc_info {
	unsigned			startup_time;
	unsigned			min_sample_rate;
	unsigned			max_sample_rate;
};

struct at91_adc_trigger {
	char				*name;
	unsigned int			trgmod_value;
	unsigned int			edge_type;
	bool				hw_trig;
};

/**
 * at91_adc_dma - at91-sama5d2 dma information struct
 * @dma_chan:		the dma channel acquired
 * @rx_buf:		dma coherent allocated area
 * @rx_dma_buf:		dma handler for the buffer
 * @phys_addr:		physical address of the ADC base register
 * @buf_idx:		index inside the dma buffer where reading was last done
 * @rx_buf_sz:		size of buffer used by DMA operation
 * @watermark:		number of conversions to copy before DMA triggers irq
 * @dma_ts:		hold the start timestamp of dma operation
 */
struct at91_adc_dma {
	struct dma_chan			*dma_chan;
	u8				*rx_buf;
	dma_addr_t			rx_dma_buf;
	phys_addr_t			phys_addr;
	int				buf_idx;
	int				rx_buf_sz;
	int				watermark;
	s64				dma_ts;
};

struct at91_adc_state {
	void __iomem			*base;
	int				irq;
	struct clk			*per_clk;
	struct regulator		*reg;
	struct regulator		*vref;
	int				vref_uv;
	struct iio_trigger		*trig;
	const struct at91_adc_trigger	*selected_trig;
	const struct iio_chan_spec	*chan;
	bool				conversion_done;
	u32				conversion_value;
	struct at91_adc_soc_info	soc_info;
	wait_queue_head_t		wq_data_available;
	struct at91_adc_dma		dma_st;
	u16				buffer[AT91_BUFFER_MAX_HWORDS];
	/*
	 * lock to prevent concurrent 'single conversion' requests through
	 * sysfs.
	 */
	struct mutex			lock;
};

static const struct at91_adc_trigger at91_adc_trigger_list[] = {
	{
		.name = "external_rising",
		.trgmod_value = AT91_SAMA5D2_TRGR_TRGMOD_EXT_TRIG_RISE,
		.edge_type = IRQ_TYPE_EDGE_RISING,
		.hw_trig = true,
	},
	{
		.name = "external_falling",
		.trgmod_value = AT91_SAMA5D2_TRGR_TRGMOD_EXT_TRIG_FALL,
		.edge_type = IRQ_TYPE_EDGE_FALLING,
		.hw_trig = true,
	},
	{
		.name = "external_any",
		.trgmod_value = AT91_SAMA5D2_TRGR_TRGMOD_EXT_TRIG_ANY,
		.edge_type = IRQ_TYPE_EDGE_BOTH,
		.hw_trig = true,
	},
	{
		.name = "software",
		.trgmod_value = AT91_SAMA5D2_TRGR_TRGMOD_NO_TRIGGER,
		.edge_type = IRQ_TYPE_NONE,
		.hw_trig = false,
	},
};

static const struct iio_chan_spec at91_adc_channels[] = {
	AT91_SAMA5D2_CHAN_SINGLE(0, 0x50),
	AT91_SAMA5D2_CHAN_SINGLE(1, 0x54),
	AT91_SAMA5D2_CHAN_SINGLE(2, 0x58),
	AT91_SAMA5D2_CHAN_SINGLE(3, 0x5c),
	AT91_SAMA5D2_CHAN_SINGLE(4, 0x60),
	AT91_SAMA5D2_CHAN_SINGLE(5, 0x64),
	AT91_SAMA5D2_CHAN_SINGLE(6, 0x68),
	AT91_SAMA5D2_CHAN_SINGLE(7, 0x6c),
	AT91_SAMA5D2_CHAN_SINGLE(8, 0x70),
	AT91_SAMA5D2_CHAN_SINGLE(9, 0x74),
	AT91_SAMA5D2_CHAN_SINGLE(10, 0x78),
	AT91_SAMA5D2_CHAN_SINGLE(11, 0x7c),
	AT91_SAMA5D2_CHAN_DIFF(0, 1, 0x50),
	AT91_SAMA5D2_CHAN_DIFF(2, 3, 0x58),
	AT91_SAMA5D2_CHAN_DIFF(4, 5, 0x60),
	AT91_SAMA5D2_CHAN_DIFF(6, 7, 0x68),
	AT91_SAMA5D2_CHAN_DIFF(8, 9, 0x70),
	AT91_SAMA5D2_CHAN_DIFF(10, 11, 0x78),
	IIO_CHAN_SOFT_TIMESTAMP(AT91_SAMA5D2_SINGLE_CHAN_CNT
				+ AT91_SAMA5D2_DIFF_CHAN_CNT + 1),
};

static int at91_adc_chan_xlate(struct iio_dev *indio_dev, int chan)
{
	int i;

	for (i = 0; i < indio_dev->num_channels; i++) {
		if (indio_dev->channels[i].scan_index == chan)
			return i;
	}
	return -EINVAL;
}

static inline struct iio_chan_spec const *
at91_adc_chan_get(struct iio_dev *indio_dev, int chan)
{
	int index = at91_adc_chan_xlate(indio_dev, chan);

	if (index < 0)
		return NULL;
	return indio_dev->channels + index;
}

static int at91_adc_configure_trigger(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio = iio_trigger_get_drvdata(trig);
	struct at91_adc_state *st = iio_priv(indio);
	u32 status = at91_adc_readl(st, AT91_SAMA5D2_TRGR);
	u8 bit;

	/* clear TRGMOD */
	status &= ~AT91_SAMA5D2_TRGR_TRGMOD_MASK;

	if (state)
		status |= st->selected_trig->trgmod_value;

	/* set/unset hw trigger */
	at91_adc_writel(st, AT91_SAMA5D2_TRGR, status);

	for_each_set_bit(bit, indio->active_scan_mask, indio->num_channels) {
		struct iio_chan_spec const *chan = at91_adc_chan_get(indio, bit);

		if (!chan)
			continue;
		if (state) {
			at91_adc_writel(st, AT91_SAMA5D2_CHER,
					BIT(chan->channel));
			/* enable irq only if not using DMA */
			if (!st->dma_st.dma_chan) {
				at91_adc_writel(st, AT91_SAMA5D2_IER,
						BIT(chan->channel));
			}
		} else {
			/* disable irq only if not using DMA */
			if (!st->dma_st.dma_chan) {
				at91_adc_writel(st, AT91_SAMA5D2_IDR,
						BIT(chan->channel));
			}
			at91_adc_writel(st, AT91_SAMA5D2_CHDR,
					BIT(chan->channel));
		}
	}

	return 0;
}

static int at91_adc_reenable_trigger(struct iio_trigger *trig)
{
	struct iio_dev *indio = iio_trigger_get_drvdata(trig);
	struct at91_adc_state *st = iio_priv(indio);

	/* if we are using DMA, we must not reenable irq after each trigger */
	if (st->dma_st.dma_chan)
		return 0;

	enable_irq(st->irq);

	/* Needed to ACK the DRDY interruption */
	at91_adc_readl(st, AT91_SAMA5D2_LCDR);
	return 0;
}

static const struct iio_trigger_ops at91_adc_trigger_ops = {
	.set_trigger_state = &at91_adc_configure_trigger,
	.try_reenable = &at91_adc_reenable_trigger,
	.validate_device = iio_trigger_validate_own_device,
};

static int at91_adc_dma_size_done(struct at91_adc_state *st)
{
	struct dma_tx_state state;
	enum dma_status status;
	int i, size;

	status = dmaengine_tx_status(st->dma_st.dma_chan,
				     st->dma_st.dma_chan->cookie,
				     &state);
	if (status != DMA_IN_PROGRESS)
		return 0;

	/* Transferred length is size in bytes from end of buffer */
	i = st->dma_st.rx_buf_sz - state.residue;

	/* Return available bytes */
	if (i >= st->dma_st.buf_idx)
		size = i - st->dma_st.buf_idx;
	else
		size = st->dma_st.rx_buf_sz + i - st->dma_st.buf_idx;
	return size;
}

static void at91_dma_buffer_done(void *data)
{
	struct iio_dev *indio_dev = data;

	iio_trigger_poll_chained(indio_dev->trig);
}

static int at91_adc_dma_start(struct iio_dev *indio_dev)
{
	struct at91_adc_state *st = iio_priv(indio_dev);
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	int ret;
	u8 bit;

	if (!st->dma_st.dma_chan)
		return 0;

	/* we start a new DMA, so set buffer index to start */
	st->dma_st.buf_idx = 0;

	/*
	 * compute buffer size w.r.t. watermark and enabled channels.
	 * scan_bytes is aligned so we need an exact size for DMA
	 */
	st->dma_st.rx_buf_sz = 0;

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->num_channels) {
		struct iio_chan_spec const *chan =
					 at91_adc_chan_get(indio_dev, bit);

		if (!chan)
			continue;

		st->dma_st.rx_buf_sz += chan->scan_type.storagebits / 8;
	}
	st->dma_st.rx_buf_sz *= st->dma_st.watermark;

	/* Prepare a DMA cyclic transaction */
	desc = dmaengine_prep_dma_cyclic(st->dma_st.dma_chan,
					 st->dma_st.rx_dma_buf,
					 st->dma_st.rx_buf_sz,
					 st->dma_st.rx_buf_sz / 2,
					 DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);

	if (!desc) {
		dev_err(&indio_dev->dev, "cannot prepare DMA cyclic\n");
		return -EBUSY;
	}

	desc->callback = at91_dma_buffer_done;
	desc->callback_param = indio_dev;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(&indio_dev->dev, "cannot submit DMA cyclic\n");
		dmaengine_terminate_async(st->dma_st.dma_chan);
		return ret;
	}

	/* enable general overrun error signaling */
	at91_adc_writel(st, AT91_SAMA5D2_IER, AT91_SAMA5D2_IER_GOVRE);
	/* Issue pending DMA requests */
	dma_async_issue_pending(st->dma_st.dma_chan);

	/* consider current time as DMA start time for timestamps */
	st->dma_st.dma_ts = iio_get_time_ns(indio_dev);

	dev_dbg(&indio_dev->dev, "DMA cyclic started\n");

	return 0;
}

static int at91_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	int ret;

	ret = at91_adc_dma_start(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "buffer postenable failed\n");
		return ret;
	}

	return iio_triggered_buffer_postenable(indio_dev);
}

static int at91_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct at91_adc_state *st = iio_priv(indio_dev);
	int ret;
	u8 bit;

	ret = iio_triggered_buffer_predisable(indio_dev);
	if (ret < 0)
		dev_err(&indio_dev->dev, "buffer predisable failed\n");

	if (!st->dma_st.dma_chan)
		return ret;

	/* if we are using DMA we must clear registers and end DMA */
	dmaengine_terminate_sync(st->dma_st.dma_chan);

	/*
	 * For each enabled channel we must read the last converted value
	 * to clear EOC status and not get a possible interrupt later.
	 * This value is being read by DMA from LCDR anyway
	 */
	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->num_channels) {
		struct iio_chan_spec const *chan =
					at91_adc_chan_get(indio_dev, bit);

		if (!chan)
			continue;
		if (st->dma_st.dma_chan)
			at91_adc_readl(st, chan->address);
	}

	/* read overflow register to clear possible overflow status */
	at91_adc_readl(st, AT91_SAMA5D2_OVER);
	return ret;
}

static const struct iio_buffer_setup_ops at91_buffer_setup_ops = {
	.postenable = &at91_adc_buffer_postenable,
	.predisable = &at91_adc_buffer_predisable,
};

static struct iio_trigger *at91_adc_allocate_trigger(struct iio_dev *indio,
						     char *trigger_name)
{
	struct iio_trigger *trig;
	int ret;

	trig = devm_iio_trigger_alloc(&indio->dev, "%s-dev%d-%s", indio->name,
				      indio->id, trigger_name);
	if (!trig)
		return NULL;

	trig->dev.parent = indio->dev.parent;
	iio_trigger_set_drvdata(trig, indio);
	trig->ops = &at91_adc_trigger_ops;

	ret = devm_iio_trigger_register(&indio->dev, trig);
	if (ret)
		return ERR_PTR(ret);

	return trig;
}

static int at91_adc_trigger_init(struct iio_dev *indio)
{
	struct at91_adc_state *st = iio_priv(indio);

	st->trig = at91_adc_allocate_trigger(indio, st->selected_trig->name);
	if (IS_ERR(st->trig)) {
		dev_err(&indio->dev,
			"could not allocate trigger\n");
		return PTR_ERR(st->trig);
	}

	return 0;
}

static void at91_adc_trigger_handler_nodma(struct iio_dev *indio_dev,
					   struct iio_poll_func *pf)
{
	struct at91_adc_state *st = iio_priv(indio_dev);
	int i = 0;
	u8 bit;

	for_each_set_bit(bit, indio_dev->active_scan_mask,
			 indio_dev->num_channels) {
		struct iio_chan_spec const *chan =
					at91_adc_chan_get(indio_dev, bit);

		if (!chan)
			continue;
		st->buffer[i] = at91_adc_readl(st, chan->address);
		i++;
	}
	iio_push_to_buffers_with_timestamp(indio_dev, st->buffer,
					   pf->timestamp);
}

static void at91_adc_trigger_handler_dma(struct iio_dev *indio_dev)
{
	struct at91_adc_state *st = iio_priv(indio_dev);
	int transferred_len = at91_adc_dma_size_done(st);
	s64 ns = iio_get_time_ns(indio_dev);
	s64 interval;
	int sample_index = 0, sample_count, sample_size;

	u32 status = at91_adc_readl(st, AT91_SAMA5D2_ISR);
	/* if we reached this point, we cannot sample faster */
	if (status & AT91_SAMA5D2_IER_GOVRE)
		pr_info_ratelimited("%s: conversion overrun detected\n",
				    indio_dev->name);

	sample_size = div_s64(st->dma_st.rx_buf_sz, st->dma_st.watermark);

	sample_count = div_s64(transferred_len, sample_size);

	/*
	 * interval between samples is total time since last transfer handling
	 * divided by the number of samples (total size divided by sample size)
	 */
	interval = div_s64((ns - st->dma_st.dma_ts), sample_count);

	while (transferred_len >= sample_size) {
		iio_push_to_buffers_with_timestamp(indio_dev,
				(st->dma_st.rx_buf + st->dma_st.buf_idx),
				(st->dma_st.dma_ts + interval * sample_index));
		/* adjust remaining length */
		transferred_len -= sample_size;
		/* adjust buffer index */
		st->dma_st.buf_idx += sample_size;
		/* in case of reaching end of buffer, reset index */
		if (st->dma_st.buf_idx >= st->dma_st.rx_buf_sz)
			st->dma_st.buf_idx = 0;
		sample_index++;
	}
	/* adjust saved time for next transfer handling */
	st->dma_st.dma_ts = iio_get_time_ns(indio_dev);
}

static irqreturn_t at91_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct at91_adc_state *st = iio_priv(indio_dev);

	if (st->dma_st.dma_chan)
		at91_adc_trigger_handler_dma(indio_dev);
	else
		at91_adc_trigger_handler_nodma(indio_dev, pf);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int at91_adc_buffer_init(struct iio_dev *indio)
{
	return devm_iio_triggered_buffer_setup(&indio->dev, indio,
			&iio_pollfunc_store_time,
			&at91_adc_trigger_handler, &at91_buffer_setup_ops);
}

static unsigned at91_adc_startup_time(unsigned startup_time_min,
				      unsigned adc_clk_khz)
{
	static const unsigned int startup_lookup[] = {
		  0,   8,  16,  24,
		 64,  80,  96, 112,
		512, 576, 640, 704,
		768, 832, 896, 960
		};
	unsigned ticks_min, i;

	/*
	 * Since the adc frequency is checked before, there is no reason
	 * to not meet the startup time constraint.
	 */

	ticks_min = startup_time_min * adc_clk_khz / 1000;
	for (i = 0; i < ARRAY_SIZE(startup_lookup); i++)
		if (startup_lookup[i] > ticks_min)
			break;

	return i;
}

static void at91_adc_setup_samp_freq(struct at91_adc_state *st, unsigned freq)
{
	struct iio_dev *indio_dev = iio_priv_to_dev(st);
	unsigned f_per, prescal, startup, mr;

	f_per = clk_get_rate(st->per_clk);
	prescal = (f_per / (2 * freq)) - 1;

	startup = at91_adc_startup_time(st->soc_info.startup_time,
					freq / 1000);

	mr = at91_adc_readl(st, AT91_SAMA5D2_MR);
	mr &= ~(AT91_SAMA5D2_MR_STARTUP_MASK | AT91_SAMA5D2_MR_PRESCAL_MASK);
	mr |= AT91_SAMA5D2_MR_STARTUP(startup);
	mr |= AT91_SAMA5D2_MR_PRESCAL(prescal);
	at91_adc_writel(st, AT91_SAMA5D2_MR, mr);

	dev_dbg(&indio_dev->dev, "freq: %u, startup: %u, prescal: %u\n",
		freq, startup, prescal);
}

static unsigned at91_adc_get_sample_freq(struct at91_adc_state *st)
{
	unsigned f_adc, f_per = clk_get_rate(st->per_clk);
	unsigned mr, prescal;

	mr = at91_adc_readl(st, AT91_SAMA5D2_MR);
	prescal = (mr >> AT91_SAMA5D2_MR_PRESCAL_OFFSET)
		  & AT91_SAMA5D2_MR_PRESCAL_MAX;
	f_adc = f_per / (2 * (prescal + 1));

	return f_adc;
}

static irqreturn_t at91_adc_interrupt(int irq, void *private)
{
	struct iio_dev *indio = private;
	struct at91_adc_state *st = iio_priv(indio);
	u32 status = at91_adc_readl(st, AT91_SAMA5D2_ISR);
	u32 imr = at91_adc_readl(st, AT91_SAMA5D2_IMR);

	if (!(status & imr))
		return IRQ_NONE;

	if (iio_buffer_enabled(indio) && !st->dma_st.dma_chan) {
		disable_irq_nosync(irq);
		iio_trigger_poll(indio->trig);
	} else if (iio_buffer_enabled(indio) && st->dma_st.dma_chan) {
		disable_irq_nosync(irq);
		WARN(true, "Unexpected irq occurred\n");
	} else if (!iio_buffer_enabled(indio)) {
		st->conversion_value = at91_adc_readl(st, st->chan->address);
		st->conversion_done = true;
		wake_up_interruptible(&st->wq_data_available);
	}
	return IRQ_HANDLED;
}

static int at91_adc_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val, int *val2, long mask)
{
	struct at91_adc_state *st = iio_priv(indio_dev);
	u32 cor = 0;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/* we cannot use software trigger if hw trigger enabled */
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		mutex_lock(&st->lock);

		st->chan = chan;

		if (chan->differential)
			cor = (BIT(chan->channel) | BIT(chan->channel2)) <<
			      AT91_SAMA5D2_COR_DIFF_OFFSET;

		at91_adc_writel(st, AT91_SAMA5D2_COR, cor);
		at91_adc_writel(st, AT91_SAMA5D2_CHER, BIT(chan->channel));
		at91_adc_writel(st, AT91_SAMA5D2_IER, BIT(chan->channel));
		at91_adc_writel(st, AT91_SAMA5D2_CR, AT91_SAMA5D2_CR_START);

		ret = wait_event_interruptible_timeout(st->wq_data_available,
						       st->conversion_done,
						       msecs_to_jiffies(1000));
		if (ret == 0)
			ret = -ETIMEDOUT;

		if (ret > 0) {
			*val = st->conversion_value;
			if (chan->scan_type.sign == 's')
				*val = sign_extend32(*val, 11);
			ret = IIO_VAL_INT;
			st->conversion_done = false;
		}

		at91_adc_writel(st, AT91_SAMA5D2_IDR, BIT(chan->channel));
		at91_adc_writel(st, AT91_SAMA5D2_CHDR, BIT(chan->channel));

		/* Needed to ACK the DRDY interruption */
		at91_adc_readl(st, AT91_SAMA5D2_LCDR);

		mutex_unlock(&st->lock);

		iio_device_release_direct_mode(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_uv / 1000;
		if (chan->differential)
			*val *= 2;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = at91_adc_get_sample_freq(st);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int at91_adc_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct at91_adc_state *st = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	if (val < st->soc_info.min_sample_rate ||
	    val > st->soc_info.max_sample_rate)
		return -EINVAL;

	at91_adc_setup_samp_freq(st, val);

	return 0;
}

static void at91_adc_dma_init(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct at91_adc_state *st = iio_priv(indio_dev);
	struct dma_slave_config config = {0};
	/*
	 * We make the buffer double the size of the fifo,
	 * such that DMA uses one half of the buffer (full fifo size)
	 * and the software uses the other half to read/write.
	 */
	unsigned int pages = DIV_ROUND_UP(AT91_HWFIFO_MAX_SIZE *
					  AT91_BUFFER_MAX_CONVERSION_BYTES * 2,
					  PAGE_SIZE);

	if (st->dma_st.dma_chan)
		return;

	st->dma_st.dma_chan = dma_request_slave_channel(&pdev->dev, "rx");

	if (!st->dma_st.dma_chan)  {
		dev_info(&pdev->dev, "can't get DMA channel\n");
		goto dma_exit;
	}

	st->dma_st.rx_buf = dma_alloc_coherent(st->dma_st.dma_chan->device->dev,
					       pages * PAGE_SIZE,
					       &st->dma_st.rx_dma_buf,
					       GFP_KERNEL);
	if (!st->dma_st.rx_buf) {
		dev_info(&pdev->dev, "can't allocate coherent DMA area\n");
		goto dma_chan_disable;
	}

	/* Configure DMA channel to read data register */
	config.direction = DMA_DEV_TO_MEM;
	config.src_addr = (phys_addr_t)(st->dma_st.phys_addr
			  + AT91_SAMA5D2_LCDR);
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	config.src_maxburst = 1;
	config.dst_maxburst = 1;

	if (dmaengine_slave_config(st->dma_st.dma_chan, &config)) {
		dev_info(&pdev->dev, "can't configure DMA slave\n");
		goto dma_free_area;
	}

	dev_info(&pdev->dev, "using %s for rx DMA transfers\n",
		 dma_chan_name(st->dma_st.dma_chan));

	return;

dma_free_area:
	dma_free_coherent(st->dma_st.dma_chan->device->dev, pages * PAGE_SIZE,
			  st->dma_st.rx_buf, st->dma_st.rx_dma_buf);
dma_chan_disable:
	dma_release_channel(st->dma_st.dma_chan);
	st->dma_st.dma_chan = 0;
dma_exit:
	dev_info(&pdev->dev, "continuing without DMA support\n");
}

static void at91_adc_dma_disable(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct at91_adc_state *st = iio_priv(indio_dev);
	unsigned int pages = DIV_ROUND_UP(AT91_HWFIFO_MAX_SIZE *
					  AT91_BUFFER_MAX_CONVERSION_BYTES * 2,
					  PAGE_SIZE);

	/* if we are not using DMA, just return */
	if (!st->dma_st.dma_chan)
		return;

	/* wait for all transactions to be terminated first*/
	dmaengine_terminate_sync(st->dma_st.dma_chan);

	dma_free_coherent(st->dma_st.dma_chan->device->dev, pages * PAGE_SIZE,
			  st->dma_st.rx_buf, st->dma_st.rx_dma_buf);
	dma_release_channel(st->dma_st.dma_chan);
	st->dma_st.dma_chan = 0;

	dev_info(&pdev->dev, "continuing without DMA support\n");
}

static int at91_adc_set_watermark(struct iio_dev *indio_dev, unsigned int val)
{
	struct at91_adc_state *st = iio_priv(indio_dev);

	if (val > AT91_HWFIFO_MAX_SIZE)
		return -EINVAL;

	if (!st->selected_trig->hw_trig) {
		dev_dbg(&indio_dev->dev, "we need hw trigger for DMA\n");
		return 0;
	}

	dev_dbg(&indio_dev->dev, "new watermark is %u\n", val);
	st->dma_st.watermark = val;

	/*
	 * The logic here is: if we have watermark 1, it means we do
	 * each conversion with it's own IRQ, thus we don't need DMA.
	 * If the watermark is higher, we do DMA to do all the transfers in bulk
	 */

	if (val == 1)
		at91_adc_dma_disable(to_platform_device(&indio_dev->dev));
	else if (val > 1)
		at91_adc_dma_init(to_platform_device(&indio_dev->dev));

	return 0;
}

static const struct iio_info at91_adc_info = {
	.read_raw = &at91_adc_read_raw,
	.write_raw = &at91_adc_write_raw,
	.hwfifo_set_watermark = &at91_adc_set_watermark,
};

static void at91_adc_hw_init(struct at91_adc_state *st)
{
	at91_adc_writel(st, AT91_SAMA5D2_CR, AT91_SAMA5D2_CR_SWRST);
	at91_adc_writel(st, AT91_SAMA5D2_IDR, 0xffffffff);
	/*
	 * Transfer field must be set to 2 according to the datasheet and
	 * allows different analog settings for each channel.
	 */
	at91_adc_writel(st, AT91_SAMA5D2_MR,
			AT91_SAMA5D2_MR_TRANSFER(2) | AT91_SAMA5D2_MR_ANACH);

	at91_adc_setup_samp_freq(st, st->soc_info.min_sample_rate);
}

static ssize_t at91_adc_get_fifo_state(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev =
			platform_get_drvdata(to_platform_device(dev));
	struct at91_adc_state *st = iio_priv(indio_dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", !!st->dma_st.dma_chan);
}

static ssize_t at91_adc_get_watermark(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev =
			platform_get_drvdata(to_platform_device(dev));
	struct at91_adc_state *st = iio_priv(indio_dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", st->dma_st.watermark);
}

static IIO_DEVICE_ATTR(hwfifo_enabled, 0444,
		       at91_adc_get_fifo_state, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_watermark, 0444,
		       at91_adc_get_watermark, NULL, 0);

static IIO_CONST_ATTR(hwfifo_watermark_min, "2");
static IIO_CONST_ATTR(hwfifo_watermark_max, AT91_HWFIFO_MAX_SIZE_STR);

static const struct attribute *at91_adc_fifo_attributes[] = {
	&iio_const_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_const_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	NULL,
};

static int at91_adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct at91_adc_state *st;
	struct resource	*res;
	int ret, i;
	u32 edge_type = IRQ_TYPE_NONE;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &at91_adc_info;
	indio_dev->channels = at91_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(at91_adc_channels);

	st = iio_priv(indio_dev);

	ret = of_property_read_u32(pdev->dev.of_node,
				   "atmel,min-sample-rate-hz",
				   &st->soc_info.min_sample_rate);
	if (ret) {
		dev_err(&pdev->dev,
			"invalid or missing value for atmel,min-sample-rate-hz\n");
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				   "atmel,max-sample-rate-hz",
				   &st->soc_info.max_sample_rate);
	if (ret) {
		dev_err(&pdev->dev,
			"invalid or missing value for atmel,max-sample-rate-hz\n");
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "atmel,startup-time-ms",
				   &st->soc_info.startup_time);
	if (ret) {
		dev_err(&pdev->dev,
			"invalid or missing value for atmel,startup-time-ms\n");
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node,
				   "atmel,trigger-edge-type", &edge_type);
	if (ret) {
		dev_dbg(&pdev->dev,
			"atmel,trigger-edge-type not specified, only software trigger available\n");
	}

	st->selected_trig = NULL;

	/* find the right trigger, or no trigger at all */
	for (i = 0; i < AT91_SAMA5D2_HW_TRIG_CNT + 1; i++)
		if (at91_adc_trigger_list[i].edge_type == edge_type) {
			st->selected_trig = &at91_adc_trigger_list[i];
			break;
		}

	if (!st->selected_trig) {
		dev_err(&pdev->dev, "invalid external trigger edge value\n");
		return -EINVAL;
	}

	init_waitqueue_head(&st->wq_data_available);
	mutex_init(&st->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	/* if we plan to use DMA, we need the physical address of the regs */
	st->dma_st.phys_addr = res->start;

	st->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(st->base))
		return PTR_ERR(st->base);

	st->irq = platform_get_irq(pdev, 0);
	if (st->irq <= 0) {
		if (!st->irq)
			st->irq = -ENXIO;

		return st->irq;
	}

	st->per_clk = devm_clk_get(&pdev->dev, "adc_clk");
	if (IS_ERR(st->per_clk))
		return PTR_ERR(st->per_clk);

	st->reg = devm_regulator_get(&pdev->dev, "vddana");
	if (IS_ERR(st->reg))
		return PTR_ERR(st->reg);

	st->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(st->vref))
		return PTR_ERR(st->vref);

	ret = devm_request_irq(&pdev->dev, st->irq, at91_adc_interrupt, 0,
			       pdev->dev.driver->name, indio_dev);
	if (ret)
		return ret;

	ret = regulator_enable(st->reg);
	if (ret)
		return ret;

	ret = regulator_enable(st->vref);
	if (ret)
		goto reg_disable;

	st->vref_uv = regulator_get_voltage(st->vref);
	if (st->vref_uv <= 0) {
		ret = -EINVAL;
		goto vref_disable;
	}

	at91_adc_hw_init(st);

	ret = clk_prepare_enable(st->per_clk);
	if (ret)
		goto vref_disable;

	platform_set_drvdata(pdev, indio_dev);

	if (st->selected_trig->hw_trig) {
		ret = at91_adc_buffer_init(indio_dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "couldn't initialize the buffer.\n");
			goto per_clk_disable_unprepare;
		}

		ret = at91_adc_trigger_init(indio_dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "couldn't setup the triggers.\n");
			goto per_clk_disable_unprepare;
		}
		/*
		 * Initially the iio buffer has a length of 2 and
		 * a watermark of 1
		 */
		st->dma_st.watermark = 1;

		iio_buffer_set_attrs(indio_dev->buffer,
				     at91_adc_fifo_attributes);
	}

	if (dma_coerce_mask_and_coherent(&indio_dev->dev, DMA_BIT_MASK(32)))
		dev_info(&pdev->dev, "cannot set DMA mask to 32-bit\n");

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto dma_disable;

	if (st->selected_trig->hw_trig)
		dev_info(&pdev->dev, "setting up trigger as %s\n",
			 st->selected_trig->name);

	dev_info(&pdev->dev, "version: %x\n",
		 readl_relaxed(st->base + AT91_SAMA5D2_VERSION));

	return 0;

dma_disable:
	at91_adc_dma_disable(pdev);
per_clk_disable_unprepare:
	clk_disable_unprepare(st->per_clk);
vref_disable:
	regulator_disable(st->vref);
reg_disable:
	regulator_disable(st->reg);
	return ret;
}

static int at91_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct at91_adc_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	at91_adc_dma_disable(pdev);

	clk_disable_unprepare(st->per_clk);

	regulator_disable(st->vref);
	regulator_disable(st->reg);

	return 0;
}

static __maybe_unused int at91_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev =
			platform_get_drvdata(to_platform_device(dev));
	struct at91_adc_state *st = iio_priv(indio_dev);

	/*
	 * Do a sofware reset of the ADC before we go to suspend.
	 * this will ensure that all pins are free from being muxed by the ADC
	 * and can be used by for other devices.
	 * Otherwise, ADC will hog them and we can't go to suspend mode.
	 */
	at91_adc_writel(st, AT91_SAMA5D2_CR, AT91_SAMA5D2_CR_SWRST);

	clk_disable_unprepare(st->per_clk);
	regulator_disable(st->vref);
	regulator_disable(st->reg);

	return pinctrl_pm_select_sleep_state(dev);
}

static __maybe_unused int at91_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev =
			platform_get_drvdata(to_platform_device(dev));
	struct at91_adc_state *st = iio_priv(indio_dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret)
		goto resume_failed;

	ret = regulator_enable(st->reg);
	if (ret)
		goto resume_failed;

	ret = regulator_enable(st->vref);
	if (ret)
		goto reg_disable_resume;

	ret = clk_prepare_enable(st->per_clk);
	if (ret)
		goto vref_disable_resume;

	at91_adc_hw_init(st);

	/* reconfiguring trigger hardware state */
	if (iio_buffer_enabled(indio_dev))
		at91_adc_configure_trigger(st->trig, true);

	return 0;

vref_disable_resume:
	regulator_disable(st->vref);
reg_disable_resume:
	regulator_disable(st->reg);
resume_failed:
	dev_err(&indio_dev->dev, "failed to resume\n");
	return ret;
}

static SIMPLE_DEV_PM_OPS(at91_adc_pm_ops, at91_adc_suspend, at91_adc_resume);

static const struct of_device_id at91_adc_dt_match[] = {
	{
		.compatible = "atmel,sama5d2-adc",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, at91_adc_dt_match);

static struct platform_driver at91_adc_driver = {
	.probe = at91_adc_probe,
	.remove = at91_adc_remove,
	.driver = {
		.name = "at91-sama5d2_adc",
		.of_match_table = at91_adc_dt_match,
		.pm = &at91_adc_pm_ops,
	},
};
module_platform_driver(at91_adc_driver)

MODULE_AUTHOR("Ludovic Desroches <ludovic.desroches@atmel.com>");
MODULE_DESCRIPTION("Atmel AT91 SAMA5D2 ADC");
MODULE_LICENSE("GPL v2");
