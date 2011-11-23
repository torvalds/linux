/*
 * AD7792/AD7793 SPI ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "../iio.h"
#include "../sysfs.h"
#include "../buffer_generic.h"
#include "../ring_sw.h"
#include "../trigger.h"
#include "../trigger_consumer.h"

#include "ad7793.h"

/* NOTE:
 * The AD7792/AD7793 features a dual use data out ready DOUT/RDY output.
 * In order to avoid contentions on the SPI bus, it's therefore necessary
 * to use spi bus locking.
 *
 * The DOUT/RDY output must also be wired to an interrupt capable GPIO.
 */

struct ad7793_chip_info {
	struct iio_chan_spec		channel[7];
};

struct ad7793_state {
	struct spi_device		*spi;
	struct iio_trigger		*trig;
	const struct ad7793_chip_info	*chip_info;
	struct regulator		*reg;
	struct ad7793_platform_data	*pdata;
	wait_queue_head_t		wq_data_avail;
	bool				done;
	bool				irq_dis;
	u16				int_vref_mv;
	u16				mode;
	u16				conf;
	u32				scale_avail[8][2];
	/* Note this uses fact that 8 the mask always fits in a long */
	unsigned long			available_scan_masks[7];
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u8				data[4] ____cacheline_aligned;
};

enum ad7793_supported_device_ids {
	ID_AD7792,
	ID_AD7793,
};

static int __ad7793_write_reg(struct ad7793_state *st, bool locked,
			      bool cs_change, unsigned char reg,
			      unsigned size, unsigned val)
{
	u8 *data = st->data;
	struct spi_transfer t = {
		.tx_buf		= data,
		.len		= size + 1,
		.cs_change	= cs_change,
	};
	struct spi_message m;

	data[0] = AD7793_COMM_WRITE | AD7793_COMM_ADDR(reg);

	switch (size) {
	case 3:
		data[1] = val >> 16;
		data[2] = val >> 8;
		data[3] = val;
		break;
	case 2:
		data[1] = val >> 8;
		data[2] = val;
		break;
	case 1:
		data[1] = val;
		break;
	default:
		return -EINVAL;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);

	if (locked)
		return spi_sync_locked(st->spi, &m);
	else
		return spi_sync(st->spi, &m);
}

static int ad7793_write_reg(struct ad7793_state *st,
			    unsigned reg, unsigned size, unsigned val)
{
	return __ad7793_write_reg(st, false, false, reg, size, val);
}

static int __ad7793_read_reg(struct ad7793_state *st, bool locked,
			     bool cs_change, unsigned char reg,
			     int *val, unsigned size)
{
	u8 *data = st->data;
	int ret;
	struct spi_transfer t[] = {
		{
			.tx_buf = data,
			.len = 1,
		}, {
			.rx_buf = data,
			.len = size,
			.cs_change = cs_change,
		},
	};
	struct spi_message m;

	data[0] = AD7793_COMM_READ | AD7793_COMM_ADDR(reg);

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	if (locked)
		ret = spi_sync_locked(st->spi, &m);
	else
		ret = spi_sync(st->spi, &m);

	if (ret < 0)
		return ret;

	switch (size) {
	case 3:
		*val = data[0] << 16 | data[1] << 8 | data[2];
		break;
	case 2:
		*val = data[0] << 8 | data[1];
		break;
	case 1:
		*val = data[0];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ad7793_read_reg(struct ad7793_state *st,
			   unsigned reg, int *val, unsigned size)
{
	return __ad7793_read_reg(st, 0, 0, reg, val, size);
}

static int ad7793_read(struct ad7793_state *st, unsigned ch,
		       unsigned len, int *val)
{
	int ret;
	st->conf = (st->conf & ~AD7793_CONF_CHAN(-1)) | AD7793_CONF_CHAN(ch);
	st->mode = (st->mode & ~AD7793_MODE_SEL(-1)) |
		AD7793_MODE_SEL(AD7793_MODE_SINGLE);

	ad7793_write_reg(st, AD7793_REG_CONF, sizeof(st->conf), st->conf);

	spi_bus_lock(st->spi->master);
	st->done = false;

	ret = __ad7793_write_reg(st, 1, 1, AD7793_REG_MODE,
				 sizeof(st->mode), st->mode);
	if (ret < 0)
		goto out;

	st->irq_dis = false;
	enable_irq(st->spi->irq);
	wait_event_interruptible(st->wq_data_avail, st->done);

	ret = __ad7793_read_reg(st, 1, 0, AD7793_REG_DATA, val, len);
out:
	spi_bus_unlock(st->spi->master);

	return ret;
}

static int ad7793_calibrate(struct ad7793_state *st, unsigned mode, unsigned ch)
{
	int ret;

	st->conf = (st->conf & ~AD7793_CONF_CHAN(-1)) | AD7793_CONF_CHAN(ch);
	st->mode = (st->mode & ~AD7793_MODE_SEL(-1)) | AD7793_MODE_SEL(mode);

	ad7793_write_reg(st, AD7793_REG_CONF, sizeof(st->conf), st->conf);

	spi_bus_lock(st->spi->master);
	st->done = false;

	ret = __ad7793_write_reg(st, 1, 1, AD7793_REG_MODE,
				 sizeof(st->mode), st->mode);
	if (ret < 0)
		goto out;

	st->irq_dis = false;
	enable_irq(st->spi->irq);
	wait_event_interruptible(st->wq_data_avail, st->done);

	st->mode = (st->mode & ~AD7793_MODE_SEL(-1)) |
		AD7793_MODE_SEL(AD7793_MODE_IDLE);

	ret = __ad7793_write_reg(st, 1, 0, AD7793_REG_MODE,
				 sizeof(st->mode), st->mode);
out:
	spi_bus_unlock(st->spi->master);

	return ret;
}

static const u8 ad7793_calib_arr[6][2] = {
	{AD7793_MODE_CAL_INT_ZERO, AD7793_CH_AIN1P_AIN1M},
	{AD7793_MODE_CAL_INT_FULL, AD7793_CH_AIN1P_AIN1M},
	{AD7793_MODE_CAL_INT_ZERO, AD7793_CH_AIN2P_AIN2M},
	{AD7793_MODE_CAL_INT_FULL, AD7793_CH_AIN2P_AIN2M},
	{AD7793_MODE_CAL_INT_ZERO, AD7793_CH_AIN3P_AIN3M},
	{AD7793_MODE_CAL_INT_FULL, AD7793_CH_AIN3P_AIN3M}
};

static int ad7793_calibrate_all(struct ad7793_state *st)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ad7793_calib_arr); i++) {
		ret = ad7793_calibrate(st, ad7793_calib_arr[i][0],
				       ad7793_calib_arr[i][1]);
		if (ret)
			goto out;
	}

	return 0;
out:
	dev_err(&st->spi->dev, "Calibration failed\n");
	return ret;
}

static int ad7793_setup(struct ad7793_state *st)
{
	int i, ret = -1;
	unsigned long long scale_uv;
	u32 id;

	/* reset the serial interface */
	ret = spi_write(st->spi, (u8 *)&ret, sizeof(ret));
	if (ret < 0)
		goto out;
	msleep(1); /* Wait for at least 500us */

	/* write/read test for device presence */
	ret = ad7793_read_reg(st, AD7793_REG_ID, &id, 1);
	if (ret)
		goto out;

	id &= AD7793_ID_MASK;

	if (!((id == AD7792_ID) || (id == AD7793_ID))) {
		dev_err(&st->spi->dev, "device ID query failed\n");
		goto out;
	}

	st->mode  = (st->pdata->mode & ~AD7793_MODE_SEL(-1)) |
			AD7793_MODE_SEL(AD7793_MODE_IDLE);
	st->conf  = st->pdata->conf & ~AD7793_CONF_CHAN(-1);

	ret = ad7793_write_reg(st, AD7793_REG_MODE, sizeof(st->mode), st->mode);
	if (ret)
		goto out;

	ret = ad7793_write_reg(st, AD7793_REG_CONF, sizeof(st->conf), st->conf);
	if (ret)
		goto out;

	ret = ad7793_write_reg(st, AD7793_REG_IO,
			       sizeof(st->pdata->io), st->pdata->io);
	if (ret)
		goto out;

	ret = ad7793_calibrate_all(st);
	if (ret)
		goto out;

	/* Populate available ADC input ranges */
	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++) {
		scale_uv = ((u64)st->int_vref_mv * 100000000)
			>> (st->chip_info->channel[0].scan_type.realbits -
			(!!(st->conf & AD7793_CONF_UNIPOLAR) ? 0 : 1));
		scale_uv >>= i;

		st->scale_avail[i][1] = do_div(scale_uv, 100000000) * 10;
		st->scale_avail[i][0] = scale_uv;
	}

	return 0;
out:
	dev_err(&st->spi->dev, "setup failed\n");
	return ret;
}

static int ad7793_scan_from_ring(struct ad7793_state *st, unsigned ch, int *val)
{
	struct iio_buffer *ring = iio_priv_to_dev(st)->buffer;
	int ret;
	s64 dat64[2];
	u32 *dat32 = (u32 *)dat64;

	if (!(test_bit(ch, ring->scan_mask)))
		return  -EBUSY;

	ret = ring->access->read_last(ring, (u8 *) &dat64);
	if (ret)
		return ret;

	*val = *dat32;

	return 0;
}

static int ad7793_ring_preenable(struct iio_dev *indio_dev)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	struct iio_buffer *ring = indio_dev->buffer;
	size_t d_size;
	unsigned channel;

	if (!ring->scan_count)
		return -EINVAL;

	channel = find_first_bit(ring->scan_mask,
				 indio_dev->masklength);

	d_size = ring->scan_count *
		 indio_dev->channels[0].scan_type.storagebits / 8;

	if (ring->scan_timestamp) {
		d_size += sizeof(s64);

		if (d_size % sizeof(s64))
			d_size += sizeof(s64) - (d_size % sizeof(s64));
	}

	if (indio_dev->buffer->access->set_bytes_per_datum)
		indio_dev->buffer->access->
			set_bytes_per_datum(indio_dev->buffer, d_size);

	st->mode  = (st->mode & ~AD7793_MODE_SEL(-1)) |
		    AD7793_MODE_SEL(AD7793_MODE_CONT);
	st->conf  = (st->conf & ~AD7793_CONF_CHAN(-1)) |
		    AD7793_CONF_CHAN(indio_dev->channels[channel].address);

	ad7793_write_reg(st, AD7793_REG_CONF, sizeof(st->conf), st->conf);

	spi_bus_lock(st->spi->master);
	__ad7793_write_reg(st, 1, 1, AD7793_REG_MODE,
			   sizeof(st->mode), st->mode);

	st->irq_dis = false;
	enable_irq(st->spi->irq);

	return 0;
}

static int ad7793_ring_postdisable(struct iio_dev *indio_dev)
{
	struct ad7793_state *st = iio_priv(indio_dev);

	st->mode  = (st->mode & ~AD7793_MODE_SEL(-1)) |
		    AD7793_MODE_SEL(AD7793_MODE_IDLE);

	st->done = false;
	wait_event_interruptible(st->wq_data_avail, st->done);

	if (!st->irq_dis)
		disable_irq_nosync(st->spi->irq);

	__ad7793_write_reg(st, 1, 0, AD7793_REG_MODE,
			   sizeof(st->mode), st->mode);

	return spi_bus_unlock(st->spi->master);
}

/**
 * ad7793_trigger_handler() bh of trigger launched polling to ring buffer
 **/

static irqreturn_t ad7793_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct iio_buffer *ring = indio_dev->buffer;
	struct ad7793_state *st = iio_priv(indio_dev);
	s64 dat64[2];
	s32 *dat32 = (s32 *)dat64;

	if (ring->scan_count)
		__ad7793_read_reg(st, 1, 1, AD7793_REG_DATA,
				  dat32,
				  indio_dev->channels[0].scan_type.realbits/8);

	/* Guaranteed to be aligned with 8 byte boundary */
	if (ring->scan_timestamp)
		dat64[1] = pf->timestamp;

	ring->access->store_to(ring, (u8 *)dat64, pf->timestamp);

	iio_trigger_notify_done(indio_dev->trig);
	st->irq_dis = false;
	enable_irq(st->spi->irq);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops ad7793_ring_setup_ops = {
	.preenable = &ad7793_ring_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &ad7793_ring_postdisable,
};

static int ad7793_register_ring_funcs_and_init(struct iio_dev *indio_dev)
{
	int ret;

	indio_dev->buffer = iio_sw_rb_allocate(indio_dev);
	if (!indio_dev->buffer) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* Effectively select the ring buffer implementation */
	indio_dev->buffer->access = &ring_sw_access_funcs;
	indio_dev->pollfunc = iio_alloc_pollfunc(&iio_pollfunc_store_time,
						 &ad7793_trigger_handler,
						 IRQF_ONESHOT,
						 indio_dev,
						 "ad7793_consumer%d",
						 indio_dev->id);
	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_deallocate_sw_rb;
	}

	/* Ring buffer functions - here trigger setup related */
	indio_dev->buffer->setup_ops = &ad7793_ring_setup_ops;

	/* Flag that polled ring buffering is possible */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;

error_deallocate_sw_rb:
	iio_sw_rb_free(indio_dev->buffer);
error_ret:
	return ret;
}

static void ad7793_ring_cleanup(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_sw_rb_free(indio_dev->buffer);
}

/**
 * ad7793_data_rdy_trig_poll() the event handler for the data rdy trig
 **/
static irqreturn_t ad7793_data_rdy_trig_poll(int irq, void *private)
{
	struct ad7793_state *st = iio_priv(private);

	st->done = true;
	wake_up_interruptible(&st->wq_data_avail);
	disable_irq_nosync(irq);
	st->irq_dis = true;
	iio_trigger_poll(st->trig, iio_get_time_ns());

	return IRQ_HANDLED;
}

static int ad7793_probe_trigger(struct iio_dev *indio_dev)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	int ret;

	st->trig = iio_allocate_trigger("%s-dev%d",
					spi_get_device_id(st->spi)->name,
					indio_dev->id);
	if (st->trig == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	ret = request_irq(st->spi->irq,
			  ad7793_data_rdy_trig_poll,
			  IRQF_TRIGGER_LOW,
			  spi_get_device_id(st->spi)->name,
			  indio_dev);
	if (ret)
		goto error_free_trig;

	disable_irq_nosync(st->spi->irq);
	st->irq_dis = true;
	st->trig->dev.parent = &st->spi->dev;
	st->trig->owner = THIS_MODULE;
	st->trig->private_data = indio_dev;

	ret = iio_trigger_register(st->trig);

	/* select default trigger */
	indio_dev->trig = st->trig;
	if (ret)
		goto error_free_irq;

	return 0;

error_free_irq:
	free_irq(st->spi->irq, indio_dev);
error_free_trig:
	iio_free_trigger(st->trig);
error_ret:
	return ret;
}

static void ad7793_remove_trigger(struct iio_dev *indio_dev)
{
	struct ad7793_state *st = iio_priv(indio_dev);

	iio_trigger_unregister(st->trig);
	free_irq(st->spi->irq, indio_dev);
	iio_free_trigger(st->trig);
}

static const u16 sample_freq_avail[16] = {0, 470, 242, 123, 62, 50, 39, 33, 19,
					  17, 16, 12, 10, 8, 6, 4};

static ssize_t ad7793_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7793_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n",
		       sample_freq_avail[AD7793_MODE_RATE(st->mode)]);
}

static ssize_t ad7793_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7793_state *st = iio_priv(indio_dev);
	long lval;
	int i, ret;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		mutex_unlock(&indio_dev->mlock);
		return -EBUSY;
	}
	mutex_unlock(&indio_dev->mlock);

	ret = strict_strtol(buf, 10, &lval);
	if (ret)
		return ret;

	ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(sample_freq_avail); i++)
		if (lval == sample_freq_avail[i]) {
			mutex_lock(&indio_dev->mlock);
			st->mode &= ~AD7793_MODE_RATE(-1);
			st->mode |= AD7793_MODE_RATE(i);
			ad7793_write_reg(st, AD7793_REG_MODE,
					 sizeof(st->mode), st->mode);
			mutex_unlock(&indio_dev->mlock);
			ret = 0;
		}

	return ret ? ret : len;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		ad7793_read_frequency,
		ad7793_write_frequency);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
	"470 242 123 62 50 39 33 19 17 16 12 10 8 6 4");

static ssize_t ad7793_show_scale_available(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7793_state *st = iio_priv(indio_dev);
	int i, len = 0;

	for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
		len += sprintf(buf + len, "%d.%09u ", st->scale_avail[i][0],
			       st->scale_avail[i][1]);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEVICE_ATTR_NAMED(in_m_in_scale_available, in-in_scale_available,
			     S_IRUGO, ad7793_show_scale_available, NULL, 0);

static struct attribute *ad7793_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_m_in_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ad7793_attribute_group = {
	.attrs = ad7793_attributes,
};

static int ad7793_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	int ret, smpl = 0;
	unsigned long long scale_uv;
	bool unipolar = !!(st->conf & AD7793_CONF_UNIPOLAR);

	switch (m) {
	case 0:
		mutex_lock(&indio_dev->mlock);
		if (iio_buffer_enabled(indio_dev))
			ret = ad7793_scan_from_ring(st,
					chan->scan_index, &smpl);
		else
			ret = ad7793_read(st, chan->address,
					chan->scan_type.realbits / 8, &smpl);
		mutex_unlock(&indio_dev->mlock);

		if (ret < 0)
			return ret;

		*val = (smpl >> chan->scan_type.shift) &
			((1 << (chan->scan_type.realbits)) - 1);

		if (!unipolar)
			*val -= (1 << (chan->scan_type.realbits - 1));

		return IIO_VAL_INT;

	case (1 << IIO_CHAN_INFO_SCALE_SHARED):
		*val = st->scale_avail[(st->conf >> 8) & 0x7][0];
		*val2 = st->scale_avail[(st->conf >> 8) & 0x7][1];

		return IIO_VAL_INT_PLUS_NANO;

	case (1 << IIO_CHAN_INFO_SCALE_SEPARATE):
		switch (chan->type) {
		case IIO_VOLTAGE:
			/* 1170mV / 2^23 * 6 */
			scale_uv = (1170ULL * 100000000ULL * 6ULL)
				>> (chan->scan_type.realbits -
				(unipolar ? 0 : 1));
			break;
		case IIO_TEMP:
			/* Always uses unity gain and internal ref */
			scale_uv = (2500ULL * 100000000ULL)
				>> (chan->scan_type.realbits -
				(unipolar ? 0 : 1));
			break;
		default:
			return -EINVAL;
		}

		*val2 = do_div(scale_uv, 100000000) * 10;
		*val =  scale_uv;

		return IIO_VAL_INT_PLUS_NANO;
	}
	return -EINVAL;
}

static int ad7793_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct ad7793_state *st = iio_priv(indio_dev);
	int ret, i;
	unsigned int tmp;

	mutex_lock(&indio_dev->mlock);
	if (iio_buffer_enabled(indio_dev)) {
		mutex_unlock(&indio_dev->mlock);
		return -EBUSY;
	}

	switch (mask) {
	case (1 << IIO_CHAN_INFO_SCALE_SHARED):
		ret = -EINVAL;
		for (i = 0; i < ARRAY_SIZE(st->scale_avail); i++)
			if (val2 == st->scale_avail[i][1]) {
				tmp = st->conf;
				st->conf &= ~AD7793_CONF_GAIN(-1);
				st->conf |= AD7793_CONF_GAIN(i);

				if (tmp != st->conf) {
					ad7793_write_reg(st, AD7793_REG_CONF,
							 sizeof(st->conf),
							 st->conf);
					ad7793_calibrate_all(st);
				}
				ret = 0;
			}

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&indio_dev->mlock);
	return ret;
}

static int ad7793_validate_trigger(struct iio_dev *indio_dev,
				   struct iio_trigger *trig)
{
	if (indio_dev->trig != trig)
		return -EINVAL;

	return 0;
}

static int ad7793_write_raw_get_fmt(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       long mask)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static const struct iio_info ad7793_info = {
	.read_raw = &ad7793_read_raw,
	.write_raw = &ad7793_write_raw,
	.write_raw_get_fmt = &ad7793_write_raw_get_fmt,
	.attrs = &ad7793_attribute_group,
	.validate_trigger = ad7793_validate_trigger,
	.driver_module = THIS_MODULE,
};

static const struct ad7793_chip_info ad7793_chip_info_tbl[] = {
	[ID_AD7793] = {
		.channel[0] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.indexed = 1,
			.channel = 0,
			.channel2 = 0,
			.address = AD7793_CH_AIN1P_AIN1M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 0,
			.scan_type = IIO_ST('s', 24, 32, 0)
		},
		.channel[1] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.indexed = 1,
			.channel = 1,
			.channel2 = 1,
			.address = AD7793_CH_AIN2P_AIN2M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 1,
			.scan_type = IIO_ST('s', 24, 32, 0)
		},
		.channel[2] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.indexed = 1,
			.channel = 2,
			.channel2 = 2,
			.address = AD7793_CH_AIN3P_AIN3M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 2,
			.scan_type = IIO_ST('s', 24, 32, 0)
		},
		.channel[3] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.extend_name = "shorted",
			.indexed = 1,
			.channel = 2,
			.channel2 = 2,
			.address = AD7793_CH_AIN1M_AIN1M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 2,
			.scan_type = IIO_ST('s', 24, 32, 0)
		},
		.channel[4] = {
			.type = IIO_TEMP,
			.indexed = 1,
			.channel = 0,
			.address = AD7793_CH_TEMP,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
			.scan_index = 4,
			.scan_type = IIO_ST('s', 24, 32, 0),
		},
		.channel[5] = {
			.type = IIO_VOLTAGE,
			.extend_name = "supply",
			.indexed = 1,
			.channel = 4,
			.address = AD7793_CH_AVDD_MONITOR,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
			.scan_index = 5,
			.scan_type = IIO_ST('s', 24, 32, 0),
		},
		.channel[6] = IIO_CHAN_SOFT_TIMESTAMP(6),
	},
	[ID_AD7792] = {
		.channel[0] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.indexed = 1,
			.channel = 0,
			.channel2 = 0,
			.address = AD7793_CH_AIN1P_AIN1M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 0,
			.scan_type = IIO_ST('s', 16, 32, 0)
		},
		.channel[1] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.indexed = 1,
			.channel = 1,
			.channel2 = 1,
			.address = AD7793_CH_AIN2P_AIN2M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 1,
			.scan_type = IIO_ST('s', 16, 32, 0)
		},
		.channel[2] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.indexed = 1,
			.channel = 2,
			.channel2 = 2,
			.address = AD7793_CH_AIN3P_AIN3M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 2,
			.scan_type = IIO_ST('s', 16, 32, 0)
		},
		.channel[3] = {
			.type = IIO_VOLTAGE,
			.differential = 1,
			.extend_name = "shorted",
			.indexed = 1,
			.channel = 2,
			.channel2 = 2,
			.address = AD7793_CH_AIN1M_AIN1M,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SHARED),
			.scan_index = 2,
			.scan_type = IIO_ST('s', 16, 32, 0)
		},
		.channel[4] = {
			.type = IIO_TEMP,
			.indexed = 1,
			.channel = 0,
			.address = AD7793_CH_TEMP,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
			.scan_index = 4,
			.scan_type = IIO_ST('s', 16, 32, 0),
		},
		.channel[5] = {
			.type = IIO_VOLTAGE,
			.extend_name = "supply",
			.indexed = 1,
			.channel = 4,
			.address = AD7793_CH_AVDD_MONITOR,
			.info_mask = (1 << IIO_CHAN_INFO_SCALE_SEPARATE),
			.scan_index = 5,
			.scan_type = IIO_ST('s', 16, 32, 0),
		},
		.channel[6] = IIO_CHAN_SOFT_TIMESTAMP(6),
	},
};

static int __devinit ad7793_probe(struct spi_device *spi)
{
	struct ad7793_platform_data *pdata = spi->dev.platform_data;
	struct ad7793_state *st;
	struct iio_dev *indio_dev;
	int ret, i, voltage_uv = 0;

	if (!pdata) {
		dev_err(&spi->dev, "no platform data?\n");
		return -ENODEV;
	}

	if (!spi->irq) {
		dev_err(&spi->dev, "no IRQ?\n");
		return -ENODEV;
	}

	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);

	st->reg = regulator_get(&spi->dev, "vcc");
	if (!IS_ERR(st->reg)) {
		ret = regulator_enable(st->reg);
		if (ret)
			goto error_put_reg;

		voltage_uv = regulator_get_voltage(st->reg);
	}

	st->chip_info =
		&ad7793_chip_info_tbl[spi_get_device_id(spi)->driver_data];

	st->pdata = pdata;

	if (pdata && pdata->vref_mv)
		st->int_vref_mv = pdata->vref_mv;
	else if (voltage_uv)
		st->int_vref_mv = voltage_uv / 1000;
	else
		st->int_vref_mv = 2500; /* Build-in ref */

	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channel;
	indio_dev->available_scan_masks = st->available_scan_masks;
	indio_dev->num_channels = 7;
	indio_dev->info = &ad7793_info;

	for (i = 0; i < indio_dev->num_channels; i++) {
		set_bit(i, &st->available_scan_masks[i]);
		set_bit(indio_dev->
			channels[indio_dev->num_channels - 1].scan_index,
			&st->available_scan_masks[i]);
	}

	init_waitqueue_head(&st->wq_data_avail);

	ret = ad7793_register_ring_funcs_and_init(indio_dev);
	if (ret)
		goto error_disable_reg;

	ret = ad7793_probe_trigger(indio_dev);
	if (ret)
		goto error_unreg_ring;

	ret = iio_buffer_register(indio_dev,
				  indio_dev->channels,
				  indio_dev->num_channels);
	if (ret)
		goto error_remove_trigger;

	ret = ad7793_setup(st);
	if (ret)
		goto error_uninitialize_ring;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_uninitialize_ring;

	return 0;

error_uninitialize_ring:
	iio_buffer_unregister(indio_dev);
error_remove_trigger:
	ad7793_remove_trigger(indio_dev);
error_unreg_ring:
	ad7793_ring_cleanup(indio_dev);
error_disable_reg:
	if (!IS_ERR(st->reg))
		regulator_disable(st->reg);
error_put_reg:
	if (!IS_ERR(st->reg))
		regulator_put(st->reg);

	iio_free_device(indio_dev);

	return ret;
}

static int ad7793_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad7793_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	iio_buffer_unregister(indio_dev);
	ad7793_remove_trigger(indio_dev);
	ad7793_ring_cleanup(indio_dev);

	if (!IS_ERR(st->reg)) {
		regulator_disable(st->reg);
		regulator_put(st->reg);
	}

	iio_free_device(indio_dev);

	return 0;
}

static const struct spi_device_id ad7793_id[] = {
	{"ad7792", ID_AD7792},
	{"ad7793", ID_AD7793},
	{}
};

static struct spi_driver ad7793_driver = {
	.driver = {
		.name	= "ad7793",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= ad7793_probe,
	.remove		= __devexit_p(ad7793_remove),
	.id_table	= ad7793_id,
};

static int __init ad7793_init(void)
{
	return spi_register_driver(&ad7793_driver);
}
module_init(ad7793_init);

static void __exit ad7793_exit(void)
{
	spi_unregister_driver(&ad7793_driver);
}
module_exit(ad7793_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("Analog Devices AD7792/3 ADC");
MODULE_LICENSE("GPL v2");
