/*
 * TI ADC MFD driver
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

#include <linux/mfd/ti_am335x_tscadc.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#define DMA_BUFFER_SIZE		SZ_2K

struct tiadc_dma {
	struct dma_slave_config	conf;
	struct dma_chan		*chan;
	dma_addr_t		addr;
	dma_cookie_t		cookie;
	u8			*buf;
	int			current_period;
	int			period_size;
	u8			fifo_thresh;
};

struct tiadc_device {
	struct ti_tscadc_dev *mfd_tscadc;
	struct tiadc_dma dma;
	struct mutex fifo1_lock; /* to protect fifo access */
	int channels;
	int total_ch_enabled;
	u8 channel_line[8];
	u8 channel_step[8];
	int buffer_en_ch_steps;
	u16 data[8];
	u32 open_delay[8], sample_delay[8], step_avg[8];
};

static unsigned int tiadc_readl(struct tiadc_device *adc, unsigned int reg)
{
	return readl(adc->mfd_tscadc->tscadc_base + reg);
}

static void tiadc_writel(struct tiadc_device *adc, unsigned int reg,
					unsigned int val)
{
	writel(val, adc->mfd_tscadc->tscadc_base + reg);
}

static u32 get_adc_step_mask(struct tiadc_device *adc_dev)
{
	u32 step_en;

	step_en = ((1 << adc_dev->channels) - 1);
	step_en <<= TOTAL_STEPS - adc_dev->channels + 1;
	return step_en;
}

static u32 get_adc_chan_step_mask(struct tiadc_device *adc_dev,
		struct iio_chan_spec const *chan)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adc_dev->channel_step); i++) {
		if (chan->channel == adc_dev->channel_line[i]) {
			u32 step;

			step = adc_dev->channel_step[i];
			/* +1 for the charger */
			return 1 << (step + 1);
		}
	}
	WARN_ON(1);
	return 0;
}

static u32 get_adc_step_bit(struct tiadc_device *adc_dev, int chan)
{
	return 1 << adc_dev->channel_step[chan];
}

static void tiadc_step_config(struct iio_dev *indio_dev)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct device *dev = adc_dev->mfd_tscadc->dev;
	unsigned int stepconfig;
	int i, steps = 0;

	/*
	 * There are 16 configurable steps and 8 analog input
	 * lines available which are shared between Touchscreen and ADC.
	 *
	 * Steps forwards i.e. from 0 towards 16 are used by ADC
	 * depending on number of input lines needed.
	 * Channel would represent which analog input
	 * needs to be given to ADC to digitalize data.
	 */


	for (i = 0; i < adc_dev->channels; i++) {
		int chan;

		chan = adc_dev->channel_line[i];

		if (adc_dev->step_avg[i] > STEPCONFIG_AVG_16) {
			dev_warn(dev, "chan %d step_avg truncating to %d\n",
				 chan, STEPCONFIG_AVG_16);
			adc_dev->step_avg[i] = STEPCONFIG_AVG_16;
		}

		if (adc_dev->step_avg[i])
			stepconfig =
			STEPCONFIG_AVG(ffs(adc_dev->step_avg[i]) - 1) |
			STEPCONFIG_FIFO1;
		else
			stepconfig = STEPCONFIG_FIFO1;

		if (iio_buffer_enabled(indio_dev))
			stepconfig |= STEPCONFIG_MODE_SWCNT;

		tiadc_writel(adc_dev, REG_STEPCONFIG(steps),
				stepconfig | STEPCONFIG_INP(chan));

		if (adc_dev->open_delay[i] > STEPDELAY_OPEN_MASK) {
			dev_warn(dev, "chan %d open delay truncating to 0x3FFFF\n",
				 chan);
			adc_dev->open_delay[i] = STEPDELAY_OPEN_MASK;
		}

		if (adc_dev->sample_delay[i] > 0xFF) {
			dev_warn(dev, "chan %d sample delay truncating to 0xFF\n",
				 chan);
			adc_dev->sample_delay[i] = 0xFF;
		}

		tiadc_writel(adc_dev, REG_STEPDELAY(steps),
				STEPDELAY_OPEN(adc_dev->open_delay[i]) |
				STEPDELAY_SAMPLE(adc_dev->sample_delay[i]));

		adc_dev->channel_step[i] = steps;
		steps++;
	}
}

static irqreturn_t tiadc_irq_h(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	unsigned int status, config, adc_fsm;
	unsigned short count = 0;

	status = tiadc_readl(adc_dev, REG_IRQSTATUS);

	/*
	 * ADC and touchscreen share the IRQ line.
	 * FIFO0 interrupts are used by TSC. Handle FIFO1 IRQs here only
	 */
	if (status & IRQENB_FIFO1OVRRUN) {
		/* FIFO Overrun. Clear flag. Disable/Enable ADC to recover */
		config = tiadc_readl(adc_dev, REG_CTRL);
		config &= ~(CNTRLREG_TSCSSENB);
		tiadc_writel(adc_dev, REG_CTRL, config);
		tiadc_writel(adc_dev, REG_IRQSTATUS, IRQENB_FIFO1OVRRUN
				| IRQENB_FIFO1UNDRFLW | IRQENB_FIFO1THRES);

		/* wait for idle state.
		 * ADC needs to finish the current conversion
		 * before disabling the module
		 */
		do {
			adc_fsm = tiadc_readl(adc_dev, REG_ADCFSM);
		} while (adc_fsm != 0x10 && count++ < 100);

		tiadc_writel(adc_dev, REG_CTRL, (config | CNTRLREG_TSCSSENB));
		return IRQ_HANDLED;
	} else if (status & IRQENB_FIFO1THRES) {
		/* Disable irq and wake worker thread */
		tiadc_writel(adc_dev, REG_IRQCLR, IRQENB_FIFO1THRES);
		return IRQ_WAKE_THREAD;
	}

	return IRQ_NONE;
}

static irqreturn_t tiadc_worker_h(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	int i, k, fifo1count, read;
	u16 *data = adc_dev->data;

	fifo1count = tiadc_readl(adc_dev, REG_FIFO1CNT);
	for (k = 0; k < fifo1count; k = k + i) {
		for (i = 0; i < (indio_dev->scan_bytes)/2; i++) {
			read = tiadc_readl(adc_dev, REG_FIFO1);
			data[i] = read & FIFOREAD_DATA_MASK;
		}
		iio_push_to_buffers(indio_dev, (u8 *) data);
	}

	tiadc_writel(adc_dev, REG_IRQSTATUS, IRQENB_FIFO1THRES);
	tiadc_writel(adc_dev, REG_IRQENABLE, IRQENB_FIFO1THRES);

	return IRQ_HANDLED;
}

static void tiadc_dma_rx_complete(void *param)
{
	struct iio_dev *indio_dev = param;
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct tiadc_dma *dma = &adc_dev->dma;
	u8 *data;
	int i;

	data = dma->buf + dma->current_period * dma->period_size;
	dma->current_period = 1 - dma->current_period; /* swap the buffer ID */

	for (i = 0; i < dma->period_size; i += indio_dev->scan_bytes) {
		iio_push_to_buffers(indio_dev, data);
		data += indio_dev->scan_bytes;
	}
}

static int tiadc_start_dma(struct iio_dev *indio_dev)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct tiadc_dma *dma = &adc_dev->dma;
	struct dma_async_tx_descriptor *desc;

	dma->current_period = 0; /* We start to fill period 0 */
	/*
	 * Make the fifo thresh as the multiple of total number of
	 * channels enabled, so make sure that cyclic DMA period
	 * length is also a multiple of total number of channels
	 * enabled. This ensures that no invalid data is reported
	 * to the stack via iio_push_to_buffers().
	 */
	dma->fifo_thresh = rounddown(FIFO1_THRESHOLD + 1,
				     adc_dev->total_ch_enabled) - 1;
	/* Make sure that period length is multiple of fifo thresh level */
	dma->period_size = rounddown(DMA_BUFFER_SIZE / 2,
				    (dma->fifo_thresh + 1) * sizeof(u16));

	dma->conf.src_maxburst = dma->fifo_thresh + 1;
	dmaengine_slave_config(dma->chan, &dma->conf);

	desc = dmaengine_prep_dma_cyclic(dma->chan, dma->addr,
					 dma->period_size * 2,
					 dma->period_size, DMA_DEV_TO_MEM,
					 DMA_PREP_INTERRUPT);
	if (!desc)
		return -EBUSY;

	desc->callback = tiadc_dma_rx_complete;
	desc->callback_param = indio_dev;

	dma->cookie = dmaengine_submit(desc);

	dma_async_issue_pending(dma->chan);

	tiadc_writel(adc_dev, REG_FIFO1THR, dma->fifo_thresh);
	tiadc_writel(adc_dev, REG_DMA1REQ, dma->fifo_thresh);
	tiadc_writel(adc_dev, REG_DMAENABLE_SET, DMA_FIFO1);

	return 0;
}

static int tiadc_buffer_preenable(struct iio_dev *indio_dev)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	int i, fifo1count, read;

	tiadc_writel(adc_dev, REG_IRQCLR, (IRQENB_FIFO1THRES |
				IRQENB_FIFO1OVRRUN |
				IRQENB_FIFO1UNDRFLW));

	/* Flush FIFO. Needed in corner cases in simultaneous tsc/adc use */
	fifo1count = tiadc_readl(adc_dev, REG_FIFO1CNT);
	for (i = 0; i < fifo1count; i++)
		read = tiadc_readl(adc_dev, REG_FIFO1);

	return 0;
}

static int tiadc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct tiadc_dma *dma = &adc_dev->dma;
	unsigned int irq_enable;
	unsigned int enb = 0;
	u8 bit;

	tiadc_step_config(indio_dev);
	for_each_set_bit(bit, indio_dev->active_scan_mask, adc_dev->channels) {
		enb |= (get_adc_step_bit(adc_dev, bit) << 1);
		adc_dev->total_ch_enabled++;
	}
	adc_dev->buffer_en_ch_steps = enb;

	if (dma->chan)
		tiadc_start_dma(indio_dev);

	am335x_tsc_se_set_cache(adc_dev->mfd_tscadc, enb);

	tiadc_writel(adc_dev,  REG_IRQSTATUS, IRQENB_FIFO1THRES
				| IRQENB_FIFO1OVRRUN | IRQENB_FIFO1UNDRFLW);

	irq_enable = IRQENB_FIFO1OVRRUN;
	if (!dma->chan)
		irq_enable |= IRQENB_FIFO1THRES;
	tiadc_writel(adc_dev,  REG_IRQENABLE, irq_enable);

	return 0;
}

static int tiadc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct tiadc_dma *dma = &adc_dev->dma;
	int fifo1count, i, read;

	tiadc_writel(adc_dev, REG_IRQCLR, (IRQENB_FIFO1THRES |
				IRQENB_FIFO1OVRRUN | IRQENB_FIFO1UNDRFLW));
	am335x_tsc_se_clr(adc_dev->mfd_tscadc, adc_dev->buffer_en_ch_steps);
	adc_dev->buffer_en_ch_steps = 0;
	adc_dev->total_ch_enabled = 0;
	if (dma->chan) {
		tiadc_writel(adc_dev, REG_DMAENABLE_CLEAR, 0x2);
		dmaengine_terminate_async(dma->chan);
	}

	/* Flush FIFO of leftover data in the time it takes to disable adc */
	fifo1count = tiadc_readl(adc_dev, REG_FIFO1CNT);
	for (i = 0; i < fifo1count; i++)
		read = tiadc_readl(adc_dev, REG_FIFO1);

	return 0;
}

static int tiadc_buffer_postdisable(struct iio_dev *indio_dev)
{
	tiadc_step_config(indio_dev);

	return 0;
}

static const struct iio_buffer_setup_ops tiadc_buffer_setup_ops = {
	.preenable = &tiadc_buffer_preenable,
	.postenable = &tiadc_buffer_postenable,
	.predisable = &tiadc_buffer_predisable,
	.postdisable = &tiadc_buffer_postdisable,
};

static int tiadc_iio_buffered_hardware_setup(struct iio_dev *indio_dev,
	irqreturn_t (*pollfunc_bh)(int irq, void *p),
	irqreturn_t (*pollfunc_th)(int irq, void *p),
	int irq,
	unsigned long flags,
	const struct iio_buffer_setup_ops *setup_ops)
{
	struct iio_buffer *buffer;
	int ret;

	buffer = iio_kfifo_allocate();
	if (!buffer)
		return -ENOMEM;

	iio_device_attach_buffer(indio_dev, buffer);

	ret = request_threaded_irq(irq,	pollfunc_th, pollfunc_bh,
				flags, indio_dev->name, indio_dev);
	if (ret)
		goto error_kfifo_free;

	indio_dev->setup_ops = setup_ops;
	indio_dev->modes |= INDIO_BUFFER_SOFTWARE;

	return 0;

error_kfifo_free:
	iio_kfifo_free(indio_dev->buffer);
	return ret;
}

static void tiadc_iio_buffered_hardware_remove(struct iio_dev *indio_dev)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);

	free_irq(adc_dev->mfd_tscadc->irq, indio_dev);
	iio_kfifo_free(indio_dev->buffer);
}


static const char * const chan_name_ain[] = {
	"AIN0",
	"AIN1",
	"AIN2",
	"AIN3",
	"AIN4",
	"AIN5",
	"AIN6",
	"AIN7",
};

static int tiadc_channel_init(struct iio_dev *indio_dev, int channels)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct iio_chan_spec *chan_array;
	struct iio_chan_spec *chan;
	int i;

	indio_dev->num_channels = channels;
	chan_array = kcalloc(channels, sizeof(*chan_array), GFP_KERNEL);
	if (chan_array == NULL)
		return -ENOMEM;

	chan = chan_array;
	for (i = 0; i < channels; i++, chan++) {

		chan->type = IIO_VOLTAGE;
		chan->indexed = 1;
		chan->channel = adc_dev->channel_line[i];
		chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		chan->datasheet_name = chan_name_ain[chan->channel];
		chan->scan_index = i;
		chan->scan_type.sign = 'u';
		chan->scan_type.realbits = 12;
		chan->scan_type.storagebits = 16;
	}

	indio_dev->channels = chan_array;

	return 0;
}

static void tiadc_channels_remove(struct iio_dev *indio_dev)
{
	kfree(indio_dev->channels);
}

static int tiadc_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int *val, int *val2, long mask)
{
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	int ret = IIO_VAL_INT;
	int i, map_val;
	unsigned int fifo1count, read, stepid;
	bool found = false;
	u32 step_en;
	unsigned long timeout;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	step_en = get_adc_chan_step_mask(adc_dev, chan);
	if (!step_en)
		return -EINVAL;

	mutex_lock(&adc_dev->fifo1_lock);
	fifo1count = tiadc_readl(adc_dev, REG_FIFO1CNT);
	while (fifo1count--)
		tiadc_readl(adc_dev, REG_FIFO1);

	am335x_tsc_se_set_once(adc_dev->mfd_tscadc, step_en);

	timeout = jiffies + msecs_to_jiffies
				(IDLE_TIMEOUT * adc_dev->channels);
	/* Wait for Fifo threshold interrupt */
	while (1) {
		fifo1count = tiadc_readl(adc_dev, REG_FIFO1CNT);
		if (fifo1count)
			break;

		if (time_after(jiffies, timeout)) {
			am335x_tsc_se_adc_done(adc_dev->mfd_tscadc);
			ret = -EAGAIN;
			goto err_unlock;
		}
	}
	map_val = adc_dev->channel_step[chan->scan_index];

	/*
	 * We check the complete FIFO. We programmed just one entry but in case
	 * something went wrong we left empty handed (-EAGAIN previously) and
	 * then the value apeared somehow in the FIFO we would have two entries.
	 * Therefore we read every item and keep only the latest version of the
	 * requested channel.
	 */
	for (i = 0; i < fifo1count; i++) {
		read = tiadc_readl(adc_dev, REG_FIFO1);
		stepid = read & FIFOREAD_CHNLID_MASK;
		stepid = stepid >> 0x10;

		if (stepid == map_val) {
			read = read & FIFOREAD_DATA_MASK;
			found = true;
			*val = (u16) read;
		}
	}
	am335x_tsc_se_adc_done(adc_dev->mfd_tscadc);

	if (found == false)
		ret =  -EBUSY;

err_unlock:
	mutex_unlock(&adc_dev->fifo1_lock);
	return ret;
}

static const struct iio_info tiadc_info = {
	.read_raw = &tiadc_read_raw,
};

static int tiadc_request_dma(struct platform_device *pdev,
			     struct tiadc_device *adc_dev)
{
	struct tiadc_dma	*dma = &adc_dev->dma;
	dma_cap_mask_t		mask;

	/* Default slave configuration parameters */
	dma->conf.direction = DMA_DEV_TO_MEM;
	dma->conf.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	dma->conf.src_addr = adc_dev->mfd_tscadc->tscadc_phys_base + REG_FIFO1;

	dma_cap_zero(mask);
	dma_cap_set(DMA_CYCLIC, mask);

	/* Get a channel for RX */
	dma->chan = dma_request_chan(adc_dev->mfd_tscadc->dev, "fifo1");
	if (IS_ERR(dma->chan)) {
		int ret = PTR_ERR(dma->chan);

		dma->chan = NULL;
		return ret;
	}

	/* RX buffer */
	dma->buf = dma_alloc_coherent(dma->chan->device->dev, DMA_BUFFER_SIZE,
				      &dma->addr, GFP_KERNEL);
	if (!dma->buf)
		goto err;

	return 0;
err:
	dma_release_channel(dma->chan);
	return -ENOMEM;
}

static int tiadc_parse_dt(struct platform_device *pdev,
			  struct tiadc_device *adc_dev)
{
	struct device_node *node = pdev->dev.of_node;
	struct property *prop;
	const __be32 *cur;
	int channels = 0;
	u32 val;

	of_property_for_each_u32(node, "ti,adc-channels", prop, cur, val) {
		adc_dev->channel_line[channels] = val;

		/* Set Default values for optional DT parameters */
		adc_dev->open_delay[channels] = STEPCONFIG_OPENDLY;
		adc_dev->sample_delay[channels] = STEPCONFIG_SAMPLEDLY;
		adc_dev->step_avg[channels] = 16;

		channels++;
	}

	of_property_read_u32_array(node, "ti,chan-step-avg",
				   adc_dev->step_avg, channels);
	of_property_read_u32_array(node, "ti,chan-step-opendelay",
				   adc_dev->open_delay, channels);
	of_property_read_u32_array(node, "ti,chan-step-sampledelay",
				   adc_dev->sample_delay, channels);

	adc_dev->channels = channels;
	return 0;
}

static int tiadc_probe(struct platform_device *pdev)
{
	struct iio_dev		*indio_dev;
	struct tiadc_device	*adc_dev;
	struct device_node	*node = pdev->dev.of_node;
	int			err;

	if (!node) {
		dev_err(&pdev->dev, "Could not find valid DT data.\n");
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*adc_dev));
	if (indio_dev == NULL) {
		dev_err(&pdev->dev, "failed to allocate iio device\n");
		return -ENOMEM;
	}
	adc_dev = iio_priv(indio_dev);

	adc_dev->mfd_tscadc = ti_tscadc_dev_get(pdev);
	tiadc_parse_dt(pdev, adc_dev);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &tiadc_info;

	tiadc_step_config(indio_dev);
	tiadc_writel(adc_dev, REG_FIFO1THR, FIFO1_THRESHOLD);
	mutex_init(&adc_dev->fifo1_lock);

	err = tiadc_channel_init(indio_dev, adc_dev->channels);
	if (err < 0)
		return err;

	err = tiadc_iio_buffered_hardware_setup(indio_dev,
		&tiadc_worker_h,
		&tiadc_irq_h,
		adc_dev->mfd_tscadc->irq,
		IRQF_SHARED,
		&tiadc_buffer_setup_ops);

	if (err)
		goto err_free_channels;

	err = iio_device_register(indio_dev);
	if (err)
		goto err_buffer_unregister;

	platform_set_drvdata(pdev, indio_dev);

	err = tiadc_request_dma(pdev, adc_dev);
	if (err && err == -EPROBE_DEFER)
		goto err_dma;

	return 0;

err_dma:
	iio_device_unregister(indio_dev);
err_buffer_unregister:
	tiadc_iio_buffered_hardware_remove(indio_dev);
err_free_channels:
	tiadc_channels_remove(indio_dev);
	return err;
}

static int tiadc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct tiadc_dma *dma = &adc_dev->dma;
	u32 step_en;

	if (dma->chan) {
		dma_free_coherent(dma->chan->device->dev, DMA_BUFFER_SIZE,
				  dma->buf, dma->addr);
		dma_release_channel(dma->chan);
	}
	iio_device_unregister(indio_dev);
	tiadc_iio_buffered_hardware_remove(indio_dev);
	tiadc_channels_remove(indio_dev);

	step_en = get_adc_step_mask(adc_dev);
	am335x_tsc_se_clr(adc_dev->mfd_tscadc, step_en);

	return 0;
}

static int __maybe_unused tiadc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	struct ti_tscadc_dev *tscadc_dev;
	unsigned int idle;

	tscadc_dev = ti_tscadc_dev_get(to_platform_device(dev));
	if (!device_may_wakeup(tscadc_dev->dev)) {
		idle = tiadc_readl(adc_dev, REG_CTRL);
		idle &= ~(CNTRLREG_TSCSSENB);
		tiadc_writel(adc_dev, REG_CTRL, (idle |
				CNTRLREG_POWERDOWN));
	}

	return 0;
}

static int __maybe_unused tiadc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct tiadc_device *adc_dev = iio_priv(indio_dev);
	unsigned int restore;

	/* Make sure ADC is powered up */
	restore = tiadc_readl(adc_dev, REG_CTRL);
	restore &= ~(CNTRLREG_POWERDOWN);
	tiadc_writel(adc_dev, REG_CTRL, restore);

	tiadc_step_config(indio_dev);
	am335x_tsc_se_set_cache(adc_dev->mfd_tscadc,
			adc_dev->buffer_en_ch_steps);
	return 0;
}

static SIMPLE_DEV_PM_OPS(tiadc_pm_ops, tiadc_suspend, tiadc_resume);

static const struct of_device_id ti_adc_dt_ids[] = {
	{ .compatible = "ti,am3359-adc", },
	{ }
};
MODULE_DEVICE_TABLE(of, ti_adc_dt_ids);

static struct platform_driver tiadc_driver = {
	.driver = {
		.name   = "TI-am335x-adc",
		.pm	= &tiadc_pm_ops,
		.of_match_table = ti_adc_dt_ids,
	},
	.probe	= tiadc_probe,
	.remove	= tiadc_remove,
};
module_platform_driver(tiadc_driver);

MODULE_DESCRIPTION("TI ADC controller driver");
MODULE_AUTHOR("Rachna Patil <rachna@ti.com>");
MODULE_LICENSE("GPL");
