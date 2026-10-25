// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/iio/adc-helpers.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

#define RZT2H_ADCSR_REG			0x00
#define RZT2H_ADCSR_ADIE_MASK		BIT(12)
#define RZT2H_ADCSR_ADCS_MASK		GENMASK(14, 13)
#define RZT2H_ADCSR_ADCS_SINGLE		0b00
#define RZT2H_ADCSR_ADCS_CONTINUOUS	0b10
#define RZT2H_ADCSR_ADST_MASK		BIT(15)

#define RZT2H_ADANSA0_REG		0x04
#define RZT2H_ADANSA0_CH_MASK(x)	BIT(x)

#define RZT2H_ADDR_REG(x)		(0x20 + 0x2 * (x))

#define RZT2H_ADSSTRn(n)		(0xe0 + 0x1 * (n))

/*
 * Each A/D channel conversion takes a fixed base of 13 ADCLK cycles plus the
 * sample time programmed in ADSSTRn, giving a conversion rate of
 * ADCLK / (13 + ADSSTRn).
 */
#define RZT2H_ADC_CONV_CYCLES_BASE	13
#define RZT2H_ADC_SST_MIN		0x7
#define RZT2H_ADC_SST_MAX		0xff
#define RZT2H_ADC_SST_DEFAULT		0xb

#define RZT2H_ADCALCTL_REG		0x1f0
#define RZT2H_ADCALCTL_CAL_MASK		BIT(0)
#define RZT2H_ADCALCTL_CAL_RDY_MASK	BIT(1)
#define RZT2H_ADCALCTL_CAL_ERR_MASK	BIT(2)

#define RZT2H_ADC_MAX_CHANNELS		16
#define RZT2H_ADC_CHANNEL_BYTES		sizeof(u16)
#define RZT2H_ADC_DMA_PERIOD_SAMPLES	128
#define RZT2H_ADC_DMA_PERIODS		64
#define RZT2H_ADC_DMA_BUFFER_SAMPLES	(RZT2H_ADC_DMA_PERIODS * \
					 RZT2H_ADC_DMA_PERIOD_SAMPLES)
#define RZT2H_ADC_DMA_BUFFER_SIZE	(RZT2H_ADC_DMA_BUFFER_SAMPLES * \
					 RZT2H_ADC_MAX_CHANNELS * \
					 RZT2H_ADC_CHANNEL_BYTES)

struct rzt2h_adc_dma {
	struct dma_chan *chan;
	u16 *buf;
	dma_addr_t addr;

	unsigned int period_index;
	unsigned int period_bytes;
	unsigned int first_chan;
	unsigned int sample_chans;

	u8 gather[RZT2H_ADC_MAX_CHANNELS];
	unsigned int gather_len;

	atomic_t pending_periods;

	wait_queue_head_t wq;
	struct task_struct *thread;
};

struct rzt2h_adc {
	void __iomem *base;
	struct device *dev;

	phys_addr_t phys_base;
	struct rzt2h_adc_dma dma;
	struct completion completion;
	/* lock to protect against multiple access to the device */
	struct mutex lock;

	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	u16 buf[RZT2H_ADC_MAX_CHANNELS];

	unsigned long adclk_rate;
	int samp_freq_avail[3];
	u8 sst[RZT2H_ADC_MAX_CHANNELS];

	int irq;
};

static void rzt2h_adc_start(struct rzt2h_adc *adc, unsigned int conversion_type)
{
	u16 reg;

	reg = readw(adc->base + RZT2H_ADCSR_REG);

	/* Set conversion type */
	FIELD_MODIFY(RZT2H_ADCSR_ADCS_MASK, &reg, conversion_type);

	/* Set end of conversion interrupt and start bit. */
	reg |= RZT2H_ADCSR_ADIE_MASK | RZT2H_ADCSR_ADST_MASK;

	writew(reg, adc->base + RZT2H_ADCSR_REG);
}

static void rzt2h_adc_stop(struct rzt2h_adc *adc)
{
	u16 reg;

	reg = readw(adc->base + RZT2H_ADCSR_REG);

	/* Clear end of conversion interrupt and start bit. */
	reg &= ~(RZT2H_ADCSR_ADIE_MASK | RZT2H_ADCSR_ADST_MASK);

	writew(reg, adc->base + RZT2H_ADCSR_REG);
}

static void rzt2h_adc_set_sst(struct rzt2h_adc *adc, unsigned int ch, u8 sst)
{
	writeb(sst, adc->base + RZT2H_ADSSTRn(ch));
}

static int rzt2h_adc_read_single(struct rzt2h_adc *adc, unsigned int ch, int *val)
{
	int ret;

	ret = pm_runtime_resume_and_get(adc->dev);
	if (ret)
		return ret;

	mutex_lock(&adc->lock);

	reinit_completion(&adc->completion);

	/* Enable a single channel */
	writew(RZT2H_ADANSA0_CH_MASK(ch), adc->base + RZT2H_ADANSA0_REG);

	rzt2h_adc_set_sst(adc, ch, adc->sst[ch]);

	rzt2h_adc_start(adc, RZT2H_ADCSR_ADCS_SINGLE);

	/*
	 * Conversion can take up to ~4.3us at the maximum configurable ADSSTRn
	 * (ADSSTRn + 13 cycles at 62.5 MHz), which rounds up to 1 jiffy. A bare
	 * 1-jiffy timeout can expire almost immediately if it's armed right
	 * before a tick, so add one more jiffy to guarantee the conversion time
	 * actually elapses.
	 */
	ret = wait_for_completion_timeout(&adc->completion, usecs_to_jiffies(5) + 1);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto disable;
	}

	*val = readw(adc->base + RZT2H_ADDR_REG(ch));
	ret = IIO_VAL_INT;

disable:
	rzt2h_adc_stop(adc);

	mutex_unlock(&adc->lock);

	pm_runtime_put_autosuspend(adc->dev);

	return ret;
}

static void rzt2h_adc_set_cal(struct rzt2h_adc *adc, bool cal)
{
	u16 val;

	val = readw(adc->base + RZT2H_ADCALCTL_REG);
	if (cal)
		val |= RZT2H_ADCALCTL_CAL_MASK;
	else
		val &= ~RZT2H_ADCALCTL_CAL_MASK;

	writew(val, adc->base + RZT2H_ADCALCTL_REG);
}

static int rzt2h_adc_calibrate(struct rzt2h_adc *adc)
{
	u16 val;
	int ret;

	rzt2h_adc_set_cal(adc, true);

	ret = read_poll_timeout(readw, val, val & RZT2H_ADCALCTL_CAL_RDY_MASK,
				200, 1000, true, adc->base + RZT2H_ADCALCTL_REG);
	if (ret) {
		dev_err(adc->dev, "Calibration timed out: %d\n", ret);
		return ret;
	}

	rzt2h_adc_set_cal(adc, false);

	if (val & RZT2H_ADCALCTL_CAL_ERR_MASK) {
		dev_err(adc->dev, "Calibration failed\n");
		return -EINVAL;
	}

	return 0;
}

static void rzt2h_adc_push_period(struct iio_dev *indio_dev, u16 *period,
				  dma_addr_t addr)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	u16 *dst = adc->buf;
	u16 *src = period;

	dma_sync_single_for_cpu(adc->dev, addr, adc->dma.period_bytes,
				DMA_FROM_DEVICE);

	for (unsigned int sample = 0; sample < RZT2H_ADC_DMA_PERIOD_SAMPLES; sample++) {
		for (unsigned int i = 0; i < adc->dma.gather_len; i++)
			dst[i] = src[adc->dma.gather[i]];

		src += adc->dma.sample_chans;

		iio_push_to_buffers(indio_dev, adc->buf);
	}
}

static void rzt2h_adc_advance_period_index(struct rzt2h_adc *adc, unsigned int i)
{
	adc->dma.period_index += i;
	adc->dma.period_index %= RZT2H_ADC_DMA_PERIODS;
}

static void rzt2h_adc_dma_thread_loop(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	int pending, drop;
	dma_addr_t addr;
	u16 *period;

	pending = atomic_xchg(&adc->dma.pending_periods, 0);

	if (pending >= RZT2H_ADC_DMA_PERIODS) {
		/*
		 * The consumer fell a full buffer behind and the oldest periods
		 * have already been overwritten by the cyclic DMA transfer.
		 * Drop them and jump straight to the oldest period that has
		 * not been overwritten.
		 */
		drop = pending - RZT2H_ADC_DMA_PERIODS + 1;

		rzt2h_adc_advance_period_index(adc, drop);
		pending -= drop;
	}

	for (unsigned int i = 0; i < pending; i++) {
		unsigned int backlog = atomic_read(&adc->dma.pending_periods) +
				       pending - i;

		/*
		 * Bail if enough new periods have completed since reading the
		 * pending_periods that the next period about to be read is at
		 * risk of being overwritten.
		 */
		if (backlog >= RZT2H_ADC_DMA_PERIODS) {
			rzt2h_adc_advance_period_index(adc, pending - i);
			break;
		}

		period = adc->dma.buf + adc->dma.period_index *
			 RZT2H_ADC_DMA_PERIOD_SAMPLES * adc->dma.sample_chans;
		addr = adc->dma.addr + adc->dma.period_index *
		       adc->dma.period_bytes;

		rzt2h_adc_push_period(indio_dev, period, addr);
		rzt2h_adc_advance_period_index(adc, 1);
	}
}

static int rzt2h_adc_dma_thread(void *data)
{
	struct iio_dev *indio_dev = data;
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	while (!kthread_should_stop()) {
		wait_event_interruptible(adc->dma.wq,
					 atomic_read(&adc->dma.pending_periods) ||
					 kthread_should_stop());

		if (kthread_should_stop())
			break;

		rzt2h_adc_dma_thread_loop(indio_dev);
	}

	return 0;
}

static void rzt2h_adc_dma_callback(void *data)
{
	struct iio_dev *indio_dev = data;
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	atomic_inc(&adc->dma.pending_periods);
	wake_up(&adc->dma.wq);
}

static void rzt2h_adc_dma_calc_layout(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	unsigned int hi = 0, lo = RZT2H_ADC_MAX_CHANNELS - 1;
	const struct iio_chan_spec *chan;
	unsigned int sample_chans;
	unsigned int first_chan;
	unsigned int scan_index;
	unsigned int swap;
	unsigned int idx;

	/* Find the lowest and highest enabled channel. */
	iio_for_each_active_channel(indio_dev, scan_index) {
		chan = &indio_dev->channels[scan_index];

		lo = min_t(unsigned int, lo, chan->channel);
		hi = max_t(unsigned int, hi, chan->channel);
	}

	/*
	 * The DMA has no scatter-gather and transfers must have a power-of-two
	 * width, so pick the smallest power-of-two-aligned block of channels
	 * that covers all enabled channels.
	 */
	for (sample_chans = 1; sample_chans < RZT2H_ADC_MAX_CHANNELS; sample_chans <<= 1) {
		first_chan = round_down(lo, sample_chans);

		if (first_chan + sample_chans > hi)
			break;
	}

	/*
	 * Build a table to map each enabled channel to its position in the
	 * transferred block, it will be used later to extract only the enabled
	 * channels out of it.
	 * The DMA moves data in 32-bit words, which swaps each pair of adjacent
	 * 16-bit channels. Undo it.
	 */
	adc->dma.gather_len = 0;
	swap = sample_chans > 1;
	iio_for_each_active_channel(indio_dev, scan_index) {
		chan = &indio_dev->channels[scan_index];
		idx = chan->channel - first_chan;

		adc->dma.gather[adc->dma.gather_len++] = idx ^ swap;
	}

	adc->dma.first_chan = first_chan;
	adc->dma.sample_chans = sample_chans;
	adc->dma.period_bytes = RZT2H_ADC_DMA_PERIOD_SAMPLES * sample_chans *
				RZT2H_ADC_CHANNEL_BYTES;
}

static int rzt2h_adc_start_dma(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	struct dma_async_tx_descriptor *desc;
	struct dma_slave_config config;
	unsigned int buffer_bytes;
	dma_cookie_t cookie;
	int ret;

	rzt2h_adc_dma_calc_layout(indio_dev);

	config = (struct dma_slave_config) {
		.src_addr = adc->phys_base + RZT2H_ADDR_REG(adc->dma.first_chan),
		.src_addr_width = adc->dma.sample_chans * RZT2H_ADC_CHANNEL_BYTES,
	};

	buffer_bytes = RZT2H_ADC_DMA_PERIODS * adc->dma.period_bytes;

	ret = dmaengine_slave_config(adc->dma.chan, &config);
	if (ret)
		return ret;

	desc = dmaengine_prep_dma_cyclic(adc->dma.chan, adc->dma.addr,
					 buffer_bytes, adc->dma.period_bytes,
					 DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT);
	if (!desc)
		return -EBUSY;

	desc->callback = rzt2h_adc_dma_callback;
	desc->callback_param = indio_dev;

	cookie = dmaengine_submit(desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dmaengine_terminate_sync(adc->dma.chan);
		return ret;
	}

	adc->dma.thread = kthread_run(rzt2h_adc_dma_thread, indio_dev,
				      "rzt2h-adc-dma");
	if (IS_ERR(adc->dma.thread)) {
		dmaengine_terminate_sync(adc->dma.chan);
		return PTR_ERR(adc->dma.thread);
	}

	disable_irq(adc->irq);

	dma_async_issue_pending(adc->dma.chan);

	return 0;
}

static int rzt2h_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	const struct iio_chan_spec *chan;
	struct device *dev = adc->dev;
	unsigned int scan_index;
	u16 val = 0;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	iio_for_each_active_channel(indio_dev, scan_index) {
		chan = &indio_dev->channels[scan_index];
		val |= RZT2H_ADANSA0_CH_MASK(chan->channel);
		rzt2h_adc_set_sst(adc, chan->channel, adc->sst[chan->channel]);
	}

	writew(val, adc->base + RZT2H_ADANSA0_REG);

	adc->dma.period_index = 0;
	atomic_set(&adc->dma.pending_periods, 0);

	ret = rzt2h_adc_start_dma(indio_dev);
	if (ret) {
		pm_runtime_put_autosuspend(dev);
		return ret;
	}

	rzt2h_adc_start(adc, RZT2H_ADCSR_ADCS_CONTINUOUS);

	return 0;
}

static int rzt2h_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;

	rzt2h_adc_stop(adc);

	dmaengine_terminate_sync(adc->dma.chan);

	kthread_stop(adc->dma.thread);

	enable_irq(adc->irq);

	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int rzt2h_adc_sst_to_freq(struct rzt2h_adc *adc, u8 sst)
{
	return adc->adclk_rate / (RZT2H_ADC_CONV_CYCLES_BASE + sst);
}

static u8 rzt2h_adc_freq_to_sst(struct rzt2h_adc *adc, int freq)
{
	unsigned int cycles;

	cycles = DIV_ROUND_CLOSEST(adc->adclk_rate, freq);
	cycles = clamp_val(cycles,
			   RZT2H_ADC_CONV_CYCLES_BASE + RZT2H_ADC_SST_MIN,
			   RZT2H_ADC_CONV_CYCLES_BASE + RZT2H_ADC_SST_MAX);

	return cycles - RZT2H_ADC_CONV_CYCLES_BASE;
}

static int rzt2h_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		return rzt2h_adc_read_single(adc, chan->channel, val);
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 1800;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = rzt2h_adc_sst_to_freq(adc, adc->sst[chan->channel]);
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int rzt2h_adc_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val, int val2, long mask)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		if (val <= 0)
			return -EINVAL;

		IIO_DEV_ACQUIRE_DIRECT_MODE(indio_dev, claim);
		if (IIO_DEV_ACQUIRE_FAILED(claim))
			return -EBUSY;

		adc->sst[chan->channel] = rzt2h_adc_freq_to_sst(adc, val);

		return 0;
	}
	default:
		return -EINVAL;
	}
}

static int rzt2h_adc_read_avail(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				const int **vals, int *type, int *length,
				long mask)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = adc->samp_freq_avail;
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(adc->samp_freq_avail);
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static const struct iio_buffer_setup_ops rzt2h_adc_buffer_setup_ops = {
	.postenable = rzt2h_adc_buffer_postenable,
	.predisable = rzt2h_adc_buffer_predisable,
};

static const struct iio_info rzt2h_adc_iio_info = {
	.read_raw = rzt2h_adc_read_raw,
	.write_raw = rzt2h_adc_write_raw,
	.read_avail = rzt2h_adc_read_avail,
};

static irqreturn_t rzt2h_adc_isr(int irq, void *private)
{
	struct rzt2h_adc *adc = private;

	complete(&adc->completion);

	return IRQ_HANDLED;
}

static const struct iio_chan_spec rzt2h_adc_chan_template = {
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			      BIT(IIO_CHAN_INFO_SCALE) |
			      BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_SAMP_FREQ),
	.type = IIO_VOLTAGE,
	.scan_type = {
		.sign = 'u',
		.realbits = 12,
		.storagebits = 16,
		.endianness = IIO_CPU,
	},
};

static int rzt2h_adc_parse_properties(struct rzt2h_adc *adc)
{
	struct iio_chan_spec *chan_array;
	int ret;

	ret = devm_iio_adc_device_alloc_chaninfo_se(adc->dev,
						    &rzt2h_adc_chan_template,
						    RZT2H_ADC_MAX_CHANNELS - 1,
						    &chan_array);
	if (ret < 0)
		return dev_err_probe(adc->dev, ret, "Failed to read channel info");

	adc->num_channels = ret;
	adc->channels = chan_array;

	for (unsigned int i = 0; i < adc->num_channels; i++) {
		adc->sst[chan_array[i].channel] = RZT2H_ADC_SST_DEFAULT;

		chan_array[i].scan_index = i;
	}

	return 0;
}

static void rzt2h_adc_free_dma_buf(void *p)
{
	struct rzt2h_adc *adc = p;

	dma_free_noncoherent(adc->dev, RZT2H_ADC_DMA_BUFFER_SIZE,
			     adc->dma.buf, adc->dma.addr, DMA_FROM_DEVICE);
}

static int rzt2h_adc_setup_dma(struct iio_dev *indio_dev)
{
	struct rzt2h_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;
	int ret;

	adc->dma.chan = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(adc->dma.chan)) {
		ret = PTR_ERR(adc->dma.chan);
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret, "DMA channel request failed\n");

		adc->dma.chan = NULL;
		return 0;
	}

	adc->dma.buf = dma_alloc_noncoherent(dev, RZT2H_ADC_DMA_BUFFER_SIZE,
					     &adc->dma.addr, DMA_FROM_DEVICE,
					     GFP_KERNEL);
	if (!adc->dma.buf)
		return -ENOMEM;

	dma_sync_single_for_device(dev, adc->dma.addr, RZT2H_ADC_DMA_BUFFER_SIZE,
				   DMA_FROM_DEVICE);

	ret = devm_add_action_or_reset(dev, rzt2h_adc_free_dma_buf, adc);
	if (ret)
		return ret;

	return devm_iio_kfifo_buffer_setup_ext(dev, indio_dev,
					       &rzt2h_adc_buffer_setup_ops, NULL);
}

static int rzt2h_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct rzt2h_adc *adc;
	struct resource *res;
	struct clk *adclk;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->dev = dev;
	init_completion(&adc->completion);
	init_waitqueue_head(&adc->dma.wq);

	ret = devm_mutex_init(dev, &adc->lock);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, adc);

	ret = rzt2h_adc_parse_properties(adc);
	if (ret)
		return ret;

	adc->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	adc->phys_base = res->start;

	adclk = devm_clk_get(dev, "adclk");
	if (IS_ERR(adclk))
		return dev_err_probe(dev, PTR_ERR(adclk), "failed to get adclk\n");

	adc->adclk_rate = clk_get_rate(adclk);
	if (!adc->adclk_rate)
		return dev_err_probe(dev, -EINVAL, "invalid adclk rate\n");

	adc->samp_freq_avail[0] = rzt2h_adc_sst_to_freq(adc, RZT2H_ADC_SST_MAX);
	adc->samp_freq_avail[1] = 1;
	adc->samp_freq_avail[2] = rzt2h_adc_sst_to_freq(adc, RZT2H_ADC_SST_MIN);

	pm_runtime_set_autosuspend_delay(dev, 300);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	adc->irq = platform_get_irq_byname(pdev, "adi");
	if (adc->irq < 0)
		return adc->irq;

	ret = devm_request_irq(dev, adc->irq, rzt2h_adc_isr, 0, dev_name(dev), adc);
	if (ret)
		return ret;

	indio_dev->name = "rzt2h-adc";
	indio_dev->info = &rzt2h_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->channels;
	indio_dev->num_channels = adc->num_channels;

	ret = rzt2h_adc_setup_dma(indio_dev);
	if (ret)
		return ret;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id rzt2h_adc_match[] = {
	{ .compatible = "renesas,r9a09g077-adc" },
	{ }
};
MODULE_DEVICE_TABLE(of, rzt2h_adc_match);

static int rzt2h_adc_pm_runtime_resume(struct device *dev)
{
	struct rzt2h_adc *adc = dev_get_drvdata(dev);

	/*
	 * Datasheet Page 2810, Section 41.5.6:
	 * After release from the module-stop state, wait for at least
	 * 0.5 µs before starting A/D conversion.
	 */
	fsleep(1);

	return rzt2h_adc_calibrate(adc);
}

static const struct dev_pm_ops rzt2h_adc_pm_ops = {
	RUNTIME_PM_OPS(NULL, rzt2h_adc_pm_runtime_resume, NULL)
};

static struct platform_driver rzt2h_adc_driver = {
	.probe		= rzt2h_adc_probe,
	.driver		= {
		.name		= "rzt2h-adc",
		.of_match_table = rzt2h_adc_match,
		.pm		= pm_ptr(&rzt2h_adc_pm_ops),
	},
};

module_platform_driver(rzt2h_adc_driver);

MODULE_AUTHOR("Cosmin Tanislav <cosmin-gabriel.tanislav.xa@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/T2H / RZ/N2H ADC driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DRIVER");
