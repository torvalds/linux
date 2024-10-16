// SPDX-License-Identifier: GPL-2.0-only
/* The industrial I/O callback buffer
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer_impl.h>
#include <linux/iio/consumer.h>

struct iio_cb_buffer {
	struct iio_buffer buffer;
	int (*cb)(const void *data, void *private);
	void *private;
	struct iio_channel *channels;
	struct iio_dev *indio_dev;
};

static struct iio_cb_buffer *buffer_to_cb_buffer(struct iio_buffer *buffer)
{
	return container_of(buffer, struct iio_cb_buffer, buffer);
}

static int iio_buffer_cb_store_to(struct iio_buffer *buffer, const void *data)
{
	struct iio_cb_buffer *cb_buff = buffer_to_cb_buffer(buffer);
	return cb_buff->cb(data, cb_buff->private);
}

static void iio_buffer_cb_release(struct iio_buffer *buffer)
{
	struct iio_cb_buffer *cb_buff = buffer_to_cb_buffer(buffer);

	bitmap_free(cb_buff->buffer.scan_mask);
	kfree(cb_buff);
}

static const struct iio_buffer_access_funcs iio_cb_access = {
	.store_to = &iio_buffer_cb_store_to,
	.release = &iio_buffer_cb_release,

	.modes = INDIO_BUFFER_SOFTWARE | INDIO_BUFFER_TRIGGERED,
};

struct iio_cb_buffer *iio_channel_get_all_cb(struct device *dev,
					     int (*cb)(const void *data,
						       void *private),
					     void *private)
{
	int ret;
	struct iio_cb_buffer *cb_buff;
	struct iio_channel *chan;

	if (!cb) {
		dev_err(dev, "Invalid arguments: A callback must be provided!\n");
		return ERR_PTR(-EINVAL);
	}

	cb_buff = kzalloc(sizeof(*cb_buff), GFP_KERNEL);
	if (cb_buff == NULL)
		return ERR_PTR(-ENOMEM);

	iio_buffer_init(&cb_buff->buffer);

	cb_buff->private = private;
	cb_buff->cb = cb;
	cb_buff->buffer.access = &iio_cb_access;
	INIT_LIST_HEAD(&cb_buff->buffer.demux_list);

	cb_buff->channels = iio_channel_get_all(dev);
	if (IS_ERR(cb_buff->channels)) {
		ret = PTR_ERR(cb_buff->channels);
		goto error_free_cb_buff;
	}

	cb_buff->indio_dev = cb_buff->channels[0].indio_dev;
	cb_buff->buffer.scan_mask = bitmap_zalloc(iio_get_masklength(cb_buff->indio_dev),
						  GFP_KERNEL);
	if (cb_buff->buffer.scan_mask == NULL) {
		ret = -ENOMEM;
		goto error_release_channels;
	}
	chan = &cb_buff->channels[0];
	while (chan->indio_dev) {
		if (chan->indio_dev != cb_buff->indio_dev) {
			ret = -EINVAL;
			goto error_free_scan_mask;
		}
		set_bit(chan->channel->scan_index,
			cb_buff->buffer.scan_mask);
		chan++;
	}

	return cb_buff;

error_free_scan_mask:
	bitmap_free(cb_buff->buffer.scan_mask);
error_release_channels:
	iio_channel_release_all(cb_buff->channels);
error_free_cb_buff:
	kfree(cb_buff);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(iio_channel_get_all_cb);

int iio_channel_cb_set_buffer_watermark(struct iio_cb_buffer *cb_buff,
					size_t watermark)
{
	if (!watermark)
		return -EINVAL;
	cb_buff->buffer.watermark = watermark;

	return 0;
}
EXPORT_SYMBOL_GPL(iio_channel_cb_set_buffer_watermark);

int iio_channel_start_all_cb(struct iio_cb_buffer *cb_buff)
{
	return iio_update_buffers(cb_buff->indio_dev, &cb_buff->buffer,
				  NULL);
}
EXPORT_SYMBOL_GPL(iio_channel_start_all_cb);

void iio_channel_stop_all_cb(struct iio_cb_buffer *cb_buff)
{
	iio_update_buffers(cb_buff->indio_dev, NULL, &cb_buff->buffer);
}
EXPORT_SYMBOL_GPL(iio_channel_stop_all_cb);

void iio_channel_release_all_cb(struct iio_cb_buffer *cb_buff)
{
	iio_channel_release_all(cb_buff->channels);
	iio_buffer_put(&cb_buff->buffer);
}
EXPORT_SYMBOL_GPL(iio_channel_release_all_cb);

struct iio_channel
*iio_channel_cb_get_channels(const struct iio_cb_buffer *cb_buffer)
{
	return cb_buffer->channels;
}
EXPORT_SYMBOL_GPL(iio_channel_cb_get_channels);

struct iio_dev
*iio_channel_cb_get_iio_dev(const struct iio_cb_buffer *cb_buffer)
{
	return cb_buffer->indio_dev;
}
EXPORT_SYMBOL_GPL(iio_channel_cb_get_iio_dev);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Industrial I/O callback buffer");
MODULE_LICENSE("GPL");
