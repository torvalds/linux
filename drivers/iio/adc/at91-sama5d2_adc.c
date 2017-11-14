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
 * plus the timestamp
 */
#define AT91_BUFFER_MAX_BYTES ((AT91_SAMA5D2_SINGLE_CHAN_CNT +		\
				AT91_SAMA5D2_DIFF_CHAN_CNT) * 2 + 8)

#define AT91_BUFFER_MAX_HWORDS (AT91_BUFFER_MAX_BYTES / 2)

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
		struct iio_chan_spec const *chan = indio->channels + bit;

		if (state) {
			at91_adc_writel(st, AT91_SAMA5D2_CHER,
					BIT(chan->channel));
			at91_adc_writel(st, AT91_SAMA5D2_IER,
					BIT(chan->channel));
		} else {
			at91_adc_writel(st, AT91_SAMA5D2_IDR,
					BIT(chan->channel));
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

	enable_irq(st->irq);

	/* Needed to ACK the DRDY interruption */
	at91_adc_readl(st, AT91_SAMA5D2_LCDR);
	return 0;
}

static const struct iio_trigger_ops at91_adc_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &at91_adc_configure_trigger,
	.try_reenable = &at91_adc_reenable_trigger,
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

static irqreturn_t at91_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio = pf->indio_dev;
	struct at91_adc_state *st = iio_priv(indio);
	int i = 0;
	u8 bit;

	for_each_set_bit(bit, indio->active_scan_mask, indio->num_channels) {
		struct iio_chan_spec const *chan = indio->channels + bit;

		st->buffer[i] = at91_adc_readl(st, chan->address);
		i++;
	}

	iio_push_to_buffers_with_timestamp(indio, st->buffer, pf->timestamp);

	iio_trigger_notify_done(indio->trig);

	return IRQ_HANDLED;
}

static int at91_adc_buffer_init(struct iio_dev *indio)
{
	return devm_iio_triggered_buffer_setup(&indio->dev, indio,
			&iio_pollfunc_store_time,
			&at91_adc_trigger_handler, NULL);
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

	if (iio_buffer_enabled(indio)) {
		disable_irq_nosync(irq);
		iio_trigger_poll(indio->trig);
	} else {
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

static const struct iio_info at91_adc_info = {
	.read_raw = &at91_adc_read_raw,
	.write_raw = &at91_adc_write_raw,
	.driver_module = THIS_MODULE,
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
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto per_clk_disable_unprepare;

	if (st->selected_trig->hw_trig)
		dev_info(&pdev->dev, "setting up trigger as %s\n",
			 st->selected_trig->name);

	dev_info(&pdev->dev, "version: %x\n",
		 readl_relaxed(st->base + AT91_SAMA5D2_VERSION));

	return 0;

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
