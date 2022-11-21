// SPDX-License-Identifier: GPL-2.0-only
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/buffer_impl.h>
#include <linux/sched.h>
#include <linux/poll.h>

struct iio_kfifo {
	struct iio_buffer buffer;
	struct kfifo kf;
	struct mutex user_lock;
	int update_needed;
};

#define iio_to_kfifo(r) container_of(r, struct iio_kfifo, buffer)

static inline int __iio_allocate_kfifo(struct iio_kfifo *buf,
			size_t bytes_per_datum, unsigned int length)
{
	if ((length == 0) || (bytes_per_datum == 0))
		return -EINVAL;

	/*
	 * Make sure we don't overflow an unsigned int after kfifo rounds up to
	 * the next power of 2.
	 */
	if (roundup_pow_of_two(length) > UINT_MAX / bytes_per_datum)
		return -EINVAL;

	return __kfifo_alloc((struct __kfifo *)&buf->kf, length,
			     bytes_per_datum, GFP_KERNEL);
}

static int iio_request_update_kfifo(struct iio_buffer *r)
{
	int ret = 0;
	struct iio_kfifo *buf = iio_to_kfifo(r);

	mutex_lock(&buf->user_lock);
	if (buf->update_needed) {
		kfifo_free(&buf->kf);
		ret = __iio_allocate_kfifo(buf, buf->buffer.bytes_per_datum,
				   buf->buffer.length);
		if (ret >= 0)
			buf->update_needed = false;
	} else {
		kfifo_reset_out(&buf->kf);
	}
	mutex_unlock(&buf->user_lock);

	return ret;
}

static int iio_mark_update_needed_kfifo(struct iio_buffer *r)
{
	struct iio_kfifo *kf = iio_to_kfifo(r);
	kf->update_needed = true;
	return 0;
}

static int iio_set_bytes_per_datum_kfifo(struct iio_buffer *r, size_t bpd)
{
	if (r->bytes_per_datum != bpd) {
		r->bytes_per_datum = bpd;
		iio_mark_update_needed_kfifo(r);
	}
	return 0;
}

static int iio_set_length_kfifo(struct iio_buffer *r, unsigned int length)
{
	/* Avoid an invalid state */
	if (length < 2)
		length = 2;
	if (r->length != length) {
		r->length = length;
		iio_mark_update_needed_kfifo(r);
	}
	return 0;
}

static int iio_store_to_kfifo(struct iio_buffer *r,
			      const void *data)
{
	int ret;
	struct iio_kfifo *kf = iio_to_kfifo(r);
	ret = kfifo_in(&kf->kf, data, 1);
	if (ret != 1)
		return -EBUSY;
	return 0;
}

static int iio_read_kfifo(struct iio_buffer *r, size_t n, char __user *buf)
{
	int ret, copied;
	struct iio_kfifo *kf = iio_to_kfifo(r);

	if (mutex_lock_interruptible(&kf->user_lock))
		return -ERESTARTSYS;

	if (!kfifo_initialized(&kf->kf) || n < kfifo_esize(&kf->kf))
		ret = -EINVAL;
	else
		ret = kfifo_to_user(&kf->kf, buf, n, &copied);
	mutex_unlock(&kf->user_lock);
	if (ret < 0)
		return ret;

	return copied;
}

static size_t iio_kfifo_buf_data_available(struct iio_buffer *r)
{
	struct iio_kfifo *kf = iio_to_kfifo(r);
	size_t samples;

	mutex_lock(&kf->user_lock);
	samples = kfifo_len(&kf->kf);
	mutex_unlock(&kf->user_lock);

	return samples;
}

static void iio_kfifo_buffer_release(struct iio_buffer *buffer)
{
	struct iio_kfifo *kf = iio_to_kfifo(buffer);

	mutex_destroy(&kf->user_lock);
	kfifo_free(&kf->kf);
	kfree(kf);
}

static size_t iio_kfifo_buf_space_available(struct iio_buffer *r)
{
	struct iio_kfifo *kf = iio_to_kfifo(r);
	size_t avail;

	mutex_lock(&kf->user_lock);
	avail = kfifo_avail(&kf->kf);
	mutex_unlock(&kf->user_lock);

	return avail;
}

static int iio_kfifo_remove_from(struct iio_buffer *r, void *data)
{
	int ret;
	struct iio_kfifo *kf = iio_to_kfifo(r);

	if (kfifo_size(&kf->kf) < 1)
		return -EBUSY;

	ret = kfifo_out(&kf->kf, data, 1);
	if (ret != 1)
		return -EBUSY;

	wake_up_interruptible_poll(&r->pollq, EPOLLOUT | EPOLLWRNORM);

	return 0;
}

static int iio_kfifo_write(struct iio_buffer *r, size_t n,
	const char __user *buf)
{
	struct iio_kfifo *kf = iio_to_kfifo(r);
	int ret, copied;

	mutex_lock(&kf->user_lock);
	if (!kfifo_initialized(&kf->kf) || n < kfifo_esize(&kf->kf))
		ret = -EINVAL;
	else
		ret = kfifo_from_user(&kf->kf, buf, n, &copied);
	mutex_unlock(&kf->user_lock);
	if (ret)
		return ret;

	return copied;
}

static const struct iio_buffer_access_funcs kfifo_access_funcs = {
	.store_to = &iio_store_to_kfifo,
	.read = &iio_read_kfifo,
	.data_available = iio_kfifo_buf_data_available,
	.remove_from = &iio_kfifo_remove_from,
	.write = &iio_kfifo_write,
	.space_available = &iio_kfifo_buf_space_available,
	.request_update = &iio_request_update_kfifo,
	.set_bytes_per_datum = &iio_set_bytes_per_datum_kfifo,
	.set_length = &iio_set_length_kfifo,
	.release = &iio_kfifo_buffer_release,

	.modes = INDIO_BUFFER_SOFTWARE | INDIO_BUFFER_TRIGGERED,
};

struct iio_buffer *iio_kfifo_allocate(void)
{
	struct iio_kfifo *kf;

	kf = kzalloc(sizeof(*kf), GFP_KERNEL);
	if (!kf)
		return NULL;

	kf->update_needed = true;
	iio_buffer_init(&kf->buffer);
	kf->buffer.access = &kfifo_access_funcs;
	kf->buffer.length = 2;
	mutex_init(&kf->user_lock);

	return &kf->buffer;
}
EXPORT_SYMBOL(iio_kfifo_allocate);

void iio_kfifo_free(struct iio_buffer *r)
{
	iio_buffer_put(r);
}
EXPORT_SYMBOL(iio_kfifo_free);

static void devm_iio_kfifo_release(struct device *dev, void *res)
{
	iio_kfifo_free(*(struct iio_buffer **)res);
}

/**
 * devm_iio_kfifo_allocate - Resource-managed iio_kfifo_allocate()
 * @dev:		Device to allocate kfifo buffer for
 *
 * RETURNS:
 * Pointer to allocated iio_buffer on success, NULL on failure.
 */
static struct iio_buffer *devm_iio_kfifo_allocate(struct device *dev)
{
	struct iio_buffer **ptr, *r;

	ptr = devres_alloc(devm_iio_kfifo_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	r = iio_kfifo_allocate();
	if (r) {
		*ptr = r;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return r;
}

/**
 * devm_iio_kfifo_buffer_setup_ext - Allocate a kfifo buffer & attach it to an IIO device
 * @dev: Device object to which to attach the life-time of this kfifo buffer
 * @indio_dev: The device the buffer should be attached to
 * @mode_flags: The mode flags for this buffer (INDIO_BUFFER_SOFTWARE and/or
 *		INDIO_BUFFER_TRIGGERED).
 * @setup_ops: The setup_ops required to configure the HW part of the buffer (optional)
 * @buffer_attrs: Extra sysfs buffer attributes for this IIO buffer
 *
 * This function allocates a kfifo buffer via devm_iio_kfifo_allocate() and
 * attaches it to the IIO device via iio_device_attach_buffer().
 * This is meant to be a bit of a short-hand/helper function as there are a few
 * drivers that seem to do this.
 */
int devm_iio_kfifo_buffer_setup_ext(struct device *dev,
				    struct iio_dev *indio_dev,
				    int mode_flags,
				    const struct iio_buffer_setup_ops *setup_ops,
				    const struct attribute **buffer_attrs)
{
	struct iio_buffer *buffer;

	if (!mode_flags)
		return -EINVAL;

	buffer = devm_iio_kfifo_allocate(dev);
	if (!buffer)
		return -ENOMEM;

	mode_flags &= kfifo_access_funcs.modes;

	indio_dev->modes |= mode_flags;
	indio_dev->setup_ops = setup_ops;

	buffer->attrs = buffer_attrs;

	return iio_device_attach_buffer(indio_dev, buffer);
}
EXPORT_SYMBOL_GPL(devm_iio_kfifo_buffer_setup_ext);

MODULE_LICENSE("GPL");
