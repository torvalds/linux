#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/iio/buffer.h>
#include <linux/iio/consumer.h>

struct iio_cb_buffer {
	struct iio_buffer buffer;
	int (*cb)(u8 *data, void *private);
	void *private;
	struct iio_channel *channels;
};

static int iio_buffer_cb_store_to(struct iio_buffer *buffer, u8 *data)
{
	struct iio_cb_buffer *cb_buff = container_of(buffer,
						     struct iio_cb_buffer,
						     buffer);

	return cb_buff->cb(data, cb_buff->private);
}

static const struct iio_buffer_access_funcs iio_cb_access = {
	.store_to = &iio_buffer_cb_store_to,
};

struct iio_cb_buffer *iio_channel_get_all_cb(struct device *dev,
					     int (*cb)(u8 *data,
						       void *private),
					     void *private)
{
	int ret;
	struct iio_cb_buffer *cb_buff;
	struct iio_dev *indio_dev;
	struct iio_channel *chan;

	cb_buff = kzalloc(sizeof(*cb_buff), GFP_KERNEL);
	if (cb_buff == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	cb_buff->private = private;
	cb_buff->cb = cb;
	cb_buff->buffer.access = &iio_cb_access;
	INIT_LIST_HEAD(&cb_buff->buffer.demux_list);

	cb_buff->channels = iio_channel_get_all(dev);
	if (IS_ERR(cb_buff->channels)) {
		ret = PTR_ERR(cb_buff->channels);
		goto error_free_cb_buff;
	}

	indio_dev = cb_buff->channels[0].indio_dev;
	cb_buff->buffer.scan_mask
		= kcalloc(BITS_TO_LONGS(indio_dev->masklength), sizeof(long),
			  GFP_KERNEL);
	if (cb_buff->buffer.scan_mask == NULL) {
		ret = -ENOMEM;
		goto error_release_channels;
	}
	chan = &cb_buff->channels[0];
	while (chan->indio_dev) {
		if (chan->indio_dev != indio_dev) {
			ret = -EINVAL;
			goto error_free_scan_mask;
		}
		set_bit(chan->channel->scan_index,
			cb_buff->buffer.scan_mask);
		chan++;
	}

	return cb_buff;

error_free_scan_mask:
	kfree(cb_buff->buffer.scan_mask);
error_release_channels:
	iio_channel_release_all(cb_buff->channels);
error_free_cb_buff:
	kfree(cb_buff);
error_ret:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(iio_channel_get_all_cb);

int iio_channel_start_all_cb(struct iio_cb_buffer *cb_buff)
{
	return iio_update_buffers(cb_buff->channels[0].indio_dev,
				  &cb_buff->buffer,
				  NULL);
}
EXPORT_SYMBOL_GPL(iio_channel_start_all_cb);

void iio_channel_stop_all_cb(struct iio_cb_buffer *cb_buff)
{
	iio_update_buffers(cb_buff->channels[0].indio_dev,
			   NULL,
			   &cb_buff->buffer);
}
EXPORT_SYMBOL_GPL(iio_channel_stop_all_cb);

void iio_channel_release_all_cb(struct iio_cb_buffer *cb_buff)
{
	kfree(cb_buff->buffer.scan_mask);
	iio_channel_release_all(cb_buff->channels);
	kfree(cb_buff);
}
EXPORT_SYMBOL_GPL(iio_channel_release_all_cb);

struct iio_channel
*iio_channel_cb_get_channels(const struct iio_cb_buffer *cb_buffer)
{
	return cb_buffer->channels;
}
EXPORT_SYMBOL_GPL(iio_channel_cb_get_channels);
