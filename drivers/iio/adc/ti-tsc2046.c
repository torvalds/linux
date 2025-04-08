// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments TSC2046 SPI ADC driver
 *
 * Copyright (c) 2021 Oleksij Rempel <kernel@pengutronix.de>, Pengutronix
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#include <linux/unaligned.h>

#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger.h>

/*
 * The PENIRQ of TSC2046 controller is implemented as level shifter attached to
 * the X+ line. If voltage of the X+ line reaches a specific level the IRQ will
 * be activated or deactivated.
 * To make this kind of IRQ reusable as trigger following additions were
 * implemented:
 * - rate limiting:
 *   For typical touchscreen use case, we need to trigger about each 10ms.
 * - hrtimer:
 *   Continue triggering at least once after the IRQ was deactivated. Then
 *   deactivate this trigger to stop sampling in order to reduce power
 *   consumption.
 */

#define TI_TSC2046_NAME				"tsc2046"

/* This driver doesn't aim at the peak continuous sample rate */
#define	TI_TSC2046_MAX_SAMPLE_RATE		125000
#define	TI_TSC2046_SAMPLE_BITS \
	BITS_PER_TYPE(struct tsc2046_adc_atom)
#define	TI_TSC2046_MAX_CLK_FREQ \
	(TI_TSC2046_MAX_SAMPLE_RATE * TI_TSC2046_SAMPLE_BITS)

#define TI_TSC2046_SAMPLE_INTERVAL_US		10000

#define TI_TSC2046_START			BIT(7)
#define TI_TSC2046_ADDR				GENMASK(6, 4)
#define TI_TSC2046_ADDR_TEMP1			7
#define TI_TSC2046_ADDR_AUX			6
#define TI_TSC2046_ADDR_X			5
#define TI_TSC2046_ADDR_Z2			4
#define TI_TSC2046_ADDR_Z1			3
#define TI_TSC2046_ADDR_VBAT			2
#define TI_TSC2046_ADDR_Y			1
#define TI_TSC2046_ADDR_TEMP0			0

/*
 * The mode bit sets the resolution of the ADC. With this bit low, the next
 * conversion has 12-bit resolution, whereas with this bit high, the next
 * conversion has 8-bit resolution. This driver is optimized for 12-bit mode.
 * So, for this driver, this bit should stay zero.
 */
#define TI_TSC2046_8BIT_MODE			BIT(3)

/*
 * SER/DFR - The SER/DFR bit controls the reference mode, either single-ended
 * (high) or differential (low).
 */
#define TI_TSC2046_SER				BIT(2)

/*
 * If VREF_ON and ADC_ON are both zero, then the chip operates in
 * auto-wake/suspend mode. In most case this bits should stay zero.
 */
#define TI_TSC2046_PD1_VREF_ON			BIT(1)
#define TI_TSC2046_PD0_ADC_ON			BIT(0)

/*
 * All supported devices can do 8 or 12bit resolution. This driver
 * supports only 12bit mode, here we have a 16bit data transfer, where
 * the MSB and the 3 LSB are 0.
 */
#define TI_TSC2046_DATA_12BIT			GENMASK(14, 3)

#define TI_TSC2046_MAX_CHAN			8
#define TI_TSC2046_MIN_POLL_CNT			3
#define TI_TSC2046_EXT_POLL_CNT			3
#define TI_TSC2046_POLL_CNT \
	(TI_TSC2046_MIN_POLL_CNT + TI_TSC2046_EXT_POLL_CNT)
#define TI_TSC2046_INT_VREF			2500

/* Represents a HW sample */
struct tsc2046_adc_atom {
	/*
	 * Command transmitted to the controller. This field is empty on the RX
	 * buffer.
	 */
	u8 cmd;
	/*
	 * Data received from the controller. This field is empty for the TX
	 * buffer
	 */
	__be16 data;
} __packed;

/* Layout of atomic buffers within big buffer */
struct tsc2046_adc_group_layout {
	/* Group offset within the SPI RX buffer */
	unsigned int offset;
	/*
	 * Amount of tsc2046_adc_atom structs within the same command gathered
	 * within same group.
	 */
	unsigned int count;
	/*
	 * Settling samples (tsc2046_adc_atom structs) which should be skipped
	 * before good samples will start.
	 */
	unsigned int skip;
};

struct tsc2046_adc_dcfg {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

struct tsc2046_adc_ch_cfg {
	unsigned int settling_time_us;
	unsigned int oversampling_ratio;
};

enum tsc2046_state {
	TSC2046_STATE_SHUTDOWN,
	TSC2046_STATE_STANDBY,
	TSC2046_STATE_POLL,
	TSC2046_STATE_POLL_IRQ_DISABLE,
	TSC2046_STATE_ENABLE_IRQ,
};

struct tsc2046_adc_priv {
	struct spi_device *spi;
	const struct tsc2046_adc_dcfg *dcfg;
	bool internal_vref;

	struct iio_trigger *trig;
	struct hrtimer trig_timer;
	enum tsc2046_state state;
	int poll_cnt;
	spinlock_t state_lock;

	struct spi_transfer xfer;
	struct spi_message msg;

	struct {
		/* Scan data for each channel */
		u16 data[TI_TSC2046_MAX_CHAN];
		/* Timestamp */
		aligned_s64 ts;
	} scan_buf;

	/*
	 * Lock to protect the layout and the SPI transfer buffer.
	 * tsc2046_adc_group_layout can be changed within update_scan_mode(),
	 * in this case the l[] and tx/rx buffer will be out of sync to each
	 * other.
	 */
	struct mutex slock;
	struct tsc2046_adc_group_layout l[TI_TSC2046_MAX_CHAN];
	struct tsc2046_adc_atom *rx;
	struct tsc2046_adc_atom *tx;

	unsigned int count;
	unsigned int groups;
	u32 effective_speed_hz;
	u32 scan_interval_us;
	u32 time_per_scan_us;
	u32 time_per_bit_ns;
	unsigned int vref_mv;

	struct tsc2046_adc_ch_cfg ch_cfg[TI_TSC2046_MAX_CHAN];
};

#define TI_TSC2046_V_CHAN(index, bits, name)			\
{								\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = index,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.datasheet_name = "#name",				\
	.scan_index = index,					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = bits,				\
		.storagebits = 16,				\
		.endianness = IIO_CPU,				\
	},							\
}

#define DECLARE_TI_TSC2046_8_CHANNELS(name, bits) \
const struct iio_chan_spec name ## _channels[] = { \
	TI_TSC2046_V_CHAN(0, bits, TEMP0), \
	TI_TSC2046_V_CHAN(1, bits, Y), \
	TI_TSC2046_V_CHAN(2, bits, VBAT), \
	TI_TSC2046_V_CHAN(3, bits, Z1), \
	TI_TSC2046_V_CHAN(4, bits, Z2), \
	TI_TSC2046_V_CHAN(5, bits, X), \
	TI_TSC2046_V_CHAN(6, bits, AUX), \
	TI_TSC2046_V_CHAN(7, bits, TEMP1), \
	IIO_CHAN_SOFT_TIMESTAMP(8), \
}

static DECLARE_TI_TSC2046_8_CHANNELS(tsc2046_adc, 12);

static const struct tsc2046_adc_dcfg tsc2046_adc_dcfg_tsc2046e = {
	.channels = tsc2046_adc_channels,
	.num_channels = ARRAY_SIZE(tsc2046_adc_channels),
};

/*
 * Convert time to a number of samples which can be transferred within this
 * time.
 */
static unsigned int tsc2046_adc_time_to_count(struct tsc2046_adc_priv *priv,
					      unsigned long time)
{
	unsigned int bit_count, sample_count;

	bit_count = DIV_ROUND_UP(time * NSEC_PER_USEC, priv->time_per_bit_ns);
	sample_count = DIV_ROUND_UP(bit_count, TI_TSC2046_SAMPLE_BITS);

	dev_dbg(&priv->spi->dev, "Effective speed %u, time per bit: %u, count bits: %u, count samples: %u\n",
		priv->effective_speed_hz, priv->time_per_bit_ns,
		bit_count, sample_count);

	return sample_count;
}

static u8 tsc2046_adc_get_cmd(struct tsc2046_adc_priv *priv, int ch_idx,
			      bool keep_power)
{
	u32 pd;

	/*
	 * if PD bits are 0, controller will automatically disable ADC, VREF and
	 * enable IRQ.
	 */
	if (keep_power)
		pd = TI_TSC2046_PD0_ADC_ON;
	else
		pd = 0;

	switch (ch_idx) {
	case TI_TSC2046_ADDR_TEMP1:
	case TI_TSC2046_ADDR_AUX:
	case TI_TSC2046_ADDR_VBAT:
	case TI_TSC2046_ADDR_TEMP0:
		pd |= TI_TSC2046_SER;
		if (priv->internal_vref)
			pd |= TI_TSC2046_PD1_VREF_ON;
	}

	return TI_TSC2046_START | FIELD_PREP(TI_TSC2046_ADDR, ch_idx) | pd;
}

static u16 tsc2046_adc_get_value(struct tsc2046_adc_atom *buf)
{
	return FIELD_GET(TI_TSC2046_DATA_12BIT, get_unaligned_be16(&buf->data));
}

static int tsc2046_adc_read_one(struct tsc2046_adc_priv *priv, int ch_idx,
				u32 *effective_speed_hz)
{
	struct tsc2046_adc_ch_cfg *ch = &priv->ch_cfg[ch_idx];
	unsigned int val, val_normalized = 0;
	int ret, i, count_skip = 0, max_count;
	struct spi_transfer xfer;
	struct spi_message msg;
	u8 cmd;

	if (!effective_speed_hz) {
		count_skip = tsc2046_adc_time_to_count(priv, ch->settling_time_us);
		max_count = count_skip + ch->oversampling_ratio;
	} else {
		max_count = 1;
	}

	if (sizeof(struct tsc2046_adc_atom) * max_count > PAGE_SIZE)
		return -ENOSPC;

	struct tsc2046_adc_atom *tx_buf __free(kfree) = kcalloc(max_count,
								sizeof(*tx_buf),
								GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	struct tsc2046_adc_atom *rx_buf __free(kfree) = kcalloc(max_count,
								sizeof(*rx_buf),
								GFP_KERNEL);
	if (!rx_buf)
		return -ENOMEM;

	/*
	 * Do not enable automatic power down on working samples. Otherwise the
	 * plates will never be completely charged.
	 */
	cmd = tsc2046_adc_get_cmd(priv, ch_idx, true);

	for (i = 0; i < max_count - 1; i++)
		tx_buf[i].cmd = cmd;

	/* automatically power down on last sample */
	tx_buf[i].cmd = tsc2046_adc_get_cmd(priv, ch_idx, false);

	memset(&xfer, 0, sizeof(xfer));
	xfer.tx_buf = tx_buf;
	xfer.rx_buf = rx_buf;
	xfer.len = sizeof(*tx_buf) * max_count;
	spi_message_init_with_transfers(&msg, &xfer, 1);

	/*
	 * We aren't using spi_write_then_read() because we need to be able
	 * to get hold of the effective_speed_hz from the xfer
	 */
	ret = spi_sync(priv->spi, &msg);
	if (ret) {
		dev_err_ratelimited(&priv->spi->dev, "SPI transfer failed %pe\n",
				    ERR_PTR(ret));
		return ret;
	}

	if (effective_speed_hz)
		*effective_speed_hz = xfer.effective_speed_hz;

	for (i = 0; i < max_count - count_skip; i++) {
		val = tsc2046_adc_get_value(&rx_buf[count_skip + i]);
		val_normalized += val;
	}

	return DIV_ROUND_UP(val_normalized, max_count - count_skip);
}

static size_t tsc2046_adc_group_set_layout(struct tsc2046_adc_priv *priv,
					   unsigned int group,
					   unsigned int ch_idx)
{
	struct tsc2046_adc_ch_cfg *ch = &priv->ch_cfg[ch_idx];
	struct tsc2046_adc_group_layout *cur;
	unsigned int max_count, count_skip;
	unsigned int offset = 0;

	if (group)
		offset = priv->l[group - 1].offset + priv->l[group - 1].count;

	count_skip = tsc2046_adc_time_to_count(priv, ch->settling_time_us);
	max_count = count_skip + ch->oversampling_ratio;

	cur = &priv->l[group];
	cur->offset = offset;
	cur->count = max_count;
	cur->skip = count_skip;

	return sizeof(*priv->tx) * max_count;
}

static void tsc2046_adc_group_set_cmd(struct tsc2046_adc_priv *priv,
				      unsigned int group, int ch_idx)
{
	struct tsc2046_adc_group_layout *l = &priv->l[group];
	unsigned int i;
	u8 cmd;

	/*
	 * Do not enable automatic power down on working samples. Otherwise the
	 * plates will never be completely charged.
	 */
	cmd = tsc2046_adc_get_cmd(priv, ch_idx, true);

	for (i = 0; i < l->count - 1; i++)
		priv->tx[l->offset + i].cmd = cmd;

	/* automatically power down on last sample */
	priv->tx[l->offset + i].cmd = tsc2046_adc_get_cmd(priv, ch_idx, false);
}

static u16 tsc2046_adc_get_val(struct tsc2046_adc_priv *priv, int group)
{
	struct tsc2046_adc_group_layout *l;
	unsigned int val, val_normalized = 0;
	int valid_count, i;

	l = &priv->l[group];
	valid_count = l->count - l->skip;

	for (i = 0; i < valid_count; i++) {
		val = tsc2046_adc_get_value(&priv->rx[l->offset + l->skip + i]);
		val_normalized += val;
	}

	return DIV_ROUND_UP(val_normalized, valid_count);
}

static int tsc2046_adc_scan(struct iio_dev *indio_dev)
{
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);
	struct device *dev = &priv->spi->dev;
	int group;
	int ret;

	ret = spi_sync(priv->spi, &priv->msg);
	if (ret < 0) {
		dev_err_ratelimited(dev, "SPI transfer failed: %pe\n", ERR_PTR(ret));
		return ret;
	}

	for (group = 0; group < priv->groups; group++)
		priv->scan_buf.data[group] = tsc2046_adc_get_val(priv, group);

	ret = iio_push_to_buffers_with_timestamp(indio_dev, &priv->scan_buf,
						 iio_get_time_ns(indio_dev));
	/* If the consumer is kfifo, we may get a EBUSY here - ignore it. */
	if (ret < 0 && ret != -EBUSY) {
		dev_err_ratelimited(dev, "Failed to push scan buffer %pe\n",
				    ERR_PTR(ret));

		return ret;
	}

	return 0;
}

static irqreturn_t tsc2046_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);

	mutex_lock(&priv->slock);
	tsc2046_adc_scan(indio_dev);
	mutex_unlock(&priv->slock);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int tsc2046_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long m)
{
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		ret = tsc2046_adc_read_one(priv, chan->channel, NULL);
		if (ret < 0)
			return ret;

		*val = ret;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		/*
		 * Note: the TSC2046 has internal voltage divider on the VBAT
		 * line. This divider can be influenced by external divider.
		 * So, it is better to use external voltage-divider driver
		 * instead, which is calculating complete chain.
		 */
		*val = priv->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int tsc2046_adc_update_scan_mode(struct iio_dev *indio_dev,
					const unsigned long *active_scan_mask)
{
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);
	unsigned int ch_idx, group = 0;
	size_t size;

	mutex_lock(&priv->slock);

	size = 0;
	for_each_set_bit(ch_idx, active_scan_mask, ARRAY_SIZE(priv->l)) {
		size += tsc2046_adc_group_set_layout(priv, group, ch_idx);
		tsc2046_adc_group_set_cmd(priv, group, ch_idx);
		group++;
	}

	priv->groups = group;
	priv->xfer.len = size;
	priv->time_per_scan_us = size * 8 * priv->time_per_bit_ns / NSEC_PER_USEC;

	if (priv->scan_interval_us < priv->time_per_scan_us)
		dev_warn(&priv->spi->dev, "The scan interval (%d) is less then calculated scan time (%d)\n",
			 priv->scan_interval_us, priv->time_per_scan_us);

	mutex_unlock(&priv->slock);

	return 0;
}

static const struct iio_info tsc2046_adc_info = {
	.read_raw	  = tsc2046_adc_read_raw,
	.update_scan_mode = tsc2046_adc_update_scan_mode,
};

static enum hrtimer_restart tsc2046_adc_timer(struct hrtimer *hrtimer)
{
	struct tsc2046_adc_priv *priv = container_of(hrtimer,
						     struct tsc2046_adc_priv,
						     trig_timer);
	unsigned long flags;

	/*
	 * This state machine should address following challenges :
	 * - the interrupt source is based on level shifter attached to the X
	 *   channel of ADC. It will change the state every time we switch
	 *   between channels. So, we need to disable IRQ if we do
	 *   iio_trigger_poll().
	 * - we should do iio_trigger_poll() at some reduced sample rate
	 * - we should still trigger for some amount of time after last
	 *   interrupt with enabled IRQ was processed.
	 */

	spin_lock_irqsave(&priv->state_lock, flags);
	switch (priv->state) {
	case TSC2046_STATE_ENABLE_IRQ:
		if (priv->poll_cnt < TI_TSC2046_POLL_CNT) {
			priv->poll_cnt++;
			hrtimer_start(&priv->trig_timer,
				      ns_to_ktime(priv->scan_interval_us *
						  NSEC_PER_USEC),
				      HRTIMER_MODE_REL_SOFT);

			if (priv->poll_cnt >= TI_TSC2046_MIN_POLL_CNT) {
				priv->state = TSC2046_STATE_POLL_IRQ_DISABLE;
				enable_irq(priv->spi->irq);
			} else {
				priv->state = TSC2046_STATE_POLL;
			}
		} else {
			priv->state = TSC2046_STATE_STANDBY;
			enable_irq(priv->spi->irq);
		}
		break;
	case TSC2046_STATE_POLL_IRQ_DISABLE:
		disable_irq_nosync(priv->spi->irq);
		fallthrough;
	case TSC2046_STATE_POLL:
		priv->state = TSC2046_STATE_ENABLE_IRQ;
		/* iio_trigger_poll() starts hrtimer */
		iio_trigger_poll(priv->trig);
		break;
	case TSC2046_STATE_SHUTDOWN:
		break;
	case TSC2046_STATE_STANDBY:
		fallthrough;
	default:
		dev_warn(&priv->spi->dev, "Got unexpected state: %i\n",
			 priv->state);
		break;
	}
	spin_unlock_irqrestore(&priv->state_lock, flags);

	return HRTIMER_NORESTART;
}

static irqreturn_t tsc2046_adc_irq(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);
	unsigned long flags;

	hrtimer_try_to_cancel(&priv->trig_timer);

	spin_lock_irqsave(&priv->state_lock, flags);
	if (priv->state != TSC2046_STATE_SHUTDOWN) {
		priv->state = TSC2046_STATE_ENABLE_IRQ;
		priv->poll_cnt = 0;

		/* iio_trigger_poll() starts hrtimer */
		disable_irq_nosync(priv->spi->irq);
		iio_trigger_poll(priv->trig);
	}
	spin_unlock_irqrestore(&priv->state_lock, flags);

	return IRQ_HANDLED;
}

static void tsc2046_adc_reenable_trigger(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);
	ktime_t tim;

	/*
	 * We can sample it as fast as we can, but usually we do not need so
	 * many samples. Reduce the sample rate for default (touchscreen) use
	 * case.
	 */
	tim = ns_to_ktime((priv->scan_interval_us - priv->time_per_scan_us) *
			  NSEC_PER_USEC);
	hrtimer_start(&priv->trig_timer, tim, HRTIMER_MODE_REL_SOFT);
}

static int tsc2046_adc_set_trigger_state(struct iio_trigger *trig, bool enable)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct tsc2046_adc_priv *priv = iio_priv(indio_dev);
	unsigned long flags;

	if (enable) {
		spin_lock_irqsave(&priv->state_lock, flags);
		if (priv->state == TSC2046_STATE_SHUTDOWN) {
			priv->state = TSC2046_STATE_STANDBY;
			enable_irq(priv->spi->irq);
		}
		spin_unlock_irqrestore(&priv->state_lock, flags);
	} else {
		spin_lock_irqsave(&priv->state_lock, flags);

		if (priv->state == TSC2046_STATE_STANDBY ||
		    priv->state == TSC2046_STATE_POLL_IRQ_DISABLE)
			disable_irq_nosync(priv->spi->irq);

		priv->state = TSC2046_STATE_SHUTDOWN;
		spin_unlock_irqrestore(&priv->state_lock, flags);

		hrtimer_cancel(&priv->trig_timer);
	}

	return 0;
}

static const struct iio_trigger_ops tsc2046_adc_trigger_ops = {
	.set_trigger_state = tsc2046_adc_set_trigger_state,
	.reenable = tsc2046_adc_reenable_trigger,
};

static int tsc2046_adc_setup_spi_msg(struct tsc2046_adc_priv *priv)
{
	unsigned int ch_idx;
	size_t size;
	int ret;

	/*
	 * Make dummy read to set initial power state and get real SPI clock
	 * freq. It seems to be not important which channel is used for this
	 * case.
	 */
	ret = tsc2046_adc_read_one(priv, TI_TSC2046_ADDR_TEMP0,
				   &priv->effective_speed_hz);
	if (ret < 0)
		return ret;

	/*
	 * In case SPI controller do not report effective_speed_hz, use
	 * configure value and hope it will match.
	 */
	if (!priv->effective_speed_hz)
		priv->effective_speed_hz = priv->spi->max_speed_hz;


	priv->scan_interval_us = TI_TSC2046_SAMPLE_INTERVAL_US;
	priv->time_per_bit_ns = DIV_ROUND_UP(NSEC_PER_SEC,
					     priv->effective_speed_hz);

	/*
	 * Calculate and allocate maximal size buffer if all channels are
	 * enabled.
	 */
	size = 0;
	for (ch_idx = 0; ch_idx < ARRAY_SIZE(priv->l); ch_idx++)
		size += tsc2046_adc_group_set_layout(priv, ch_idx, ch_idx);

	if (size > PAGE_SIZE) {
		dev_err(&priv->spi->dev,
			"Calculated scan buffer is too big. Try to reduce spi-max-frequency, settling-time-us or oversampling-ratio\n");
		return -ENOSPC;
	}

	priv->tx = devm_kzalloc(&priv->spi->dev, size, GFP_KERNEL);
	if (!priv->tx)
		return -ENOMEM;

	priv->rx = devm_kzalloc(&priv->spi->dev, size, GFP_KERNEL);
	if (!priv->rx)
		return -ENOMEM;

	priv->xfer.tx_buf = priv->tx;
	priv->xfer.rx_buf = priv->rx;
	priv->xfer.len = size;
	spi_message_init_with_transfers(&priv->msg, &priv->xfer, 1);

	return 0;
}

static void tsc2046_adc_parse_fwnode(struct tsc2046_adc_priv *priv)
{
	struct fwnode_handle *child;
	struct device *dev = &priv->spi->dev;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(priv->ch_cfg); i++) {
		priv->ch_cfg[i].settling_time_us = 1;
		priv->ch_cfg[i].oversampling_ratio = 1;
	}

	device_for_each_child_node(dev, child) {
		u32 stl, overs, reg;
		int ret;

		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret) {
			dev_err(dev, "invalid reg on %pfw, err: %pe\n", child,
				ERR_PTR(ret));
			continue;
		}

		if (reg >= ARRAY_SIZE(priv->ch_cfg)) {
			dev_err(dev, "%pfw: Unsupported reg value: %i, max supported is: %zu.\n",
				child, reg, ARRAY_SIZE(priv->ch_cfg));
			continue;
		}

		ret = fwnode_property_read_u32(child, "settling-time-us", &stl);
		if (!ret)
			priv->ch_cfg[reg].settling_time_us = stl;

		ret = fwnode_property_read_u32(child, "oversampling-ratio",
					       &overs);
		if (!ret)
			priv->ch_cfg[reg].oversampling_ratio = overs;
	}
}

static int tsc2046_adc_probe(struct spi_device *spi)
{
	const struct tsc2046_adc_dcfg *dcfg;
	struct device *dev = &spi->dev;
	struct tsc2046_adc_priv *priv;
	struct iio_dev *indio_dev;
	struct iio_trigger *trig;
	int ret;

	if (spi->max_speed_hz > TI_TSC2046_MAX_CLK_FREQ) {
		dev_err(dev, "SPI max_speed_hz is too high: %d Hz. Max supported freq is %zu Hz\n",
			spi->max_speed_hz, TI_TSC2046_MAX_CLK_FREQ);
		return -EINVAL;
	}

	dcfg = spi_get_device_match_data(spi);
	if (!dcfg)
		return -EINVAL;

	spi->bits_per_word = 8;
	spi->mode &= ~SPI_MODE_X_MASK;
	spi->mode |= SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Error in SPI setup\n");

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->dcfg = dcfg;

	priv->spi = spi;

	indio_dev->name = TI_TSC2046_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = dcfg->channels;
	indio_dev->num_channels = dcfg->num_channels;
	indio_dev->info = &tsc2046_adc_info;

	ret = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (ret < 0 && ret != -ENODEV)
		return ret;

	priv->internal_vref = ret == -ENODEV;
	priv->vref_mv = priv->internal_vref ? TI_TSC2046_INT_VREF : ret / MILLI;

	tsc2046_adc_parse_fwnode(priv);

	ret = tsc2046_adc_setup_spi_msg(priv);
	if (ret)
		return ret;

	mutex_init(&priv->slock);

	ret = devm_request_irq(dev, spi->irq, &tsc2046_adc_irq,
			       IRQF_NO_AUTOEN, indio_dev->name, indio_dev);
	if (ret)
		return ret;

	trig = devm_iio_trigger_alloc(dev, "touchscreen-%s", indio_dev->name);
	if (!trig)
		return -ENOMEM;

	priv->trig = trig;
	iio_trigger_set_drvdata(trig, indio_dev);
	trig->ops = &tsc2046_adc_trigger_ops;

	spin_lock_init(&priv->state_lock);
	priv->state = TSC2046_STATE_SHUTDOWN;
	hrtimer_setup(&priv->trig_timer, tsc2046_adc_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);

	ret = devm_iio_trigger_register(dev, trig);
	if (ret) {
		dev_err(dev, "failed to register trigger\n");
		return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      &tsc2046_adc_trigger_handler, NULL);
	if (ret) {
		dev_err(dev, "Failed to setup triggered buffer\n");
		return ret;
	}

	/* set default trigger */
	indio_dev->trig = iio_trigger_get(priv->trig);

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ads7950_of_table[] = {
	{ .compatible = "ti,tsc2046e-adc", .data = &tsc2046_adc_dcfg_tsc2046e },
	{ }
};
MODULE_DEVICE_TABLE(of, ads7950_of_table);

static const struct spi_device_id tsc2046_adc_spi_ids[] = {
	{ "tsc2046e-adc", (unsigned long)&tsc2046_adc_dcfg_tsc2046e },
	{ }
};
MODULE_DEVICE_TABLE(spi, tsc2046_adc_spi_ids);

static struct spi_driver tsc2046_adc_driver = {
	.driver = {
		.name = "tsc2046",
		.of_match_table = ads7950_of_table,
	},
	.id_table = tsc2046_adc_spi_ids,
	.probe = tsc2046_adc_probe,
};
module_spi_driver(tsc2046_adc_driver);

MODULE_AUTHOR("Oleksij Rempel <kernel@pengutronix.de>");
MODULE_DESCRIPTION("TI TSC2046 ADC");
MODULE_LICENSE("GPL v2");
