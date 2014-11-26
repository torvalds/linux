#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/iio/kfifo_buf.h>
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
				int bytes_per_datum, int length)
{
	if ((length == 0) || (bytes_per_datum == 0))
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
		buf->update_needed = false;
	} else {
		kfifo_reset_out(&buf->kf);
	}
	mutex_unlock(&buf->user_lock);

	return ret;
}

static int iio_get_length_kfifo(struct iio_buffer *r)
{
	return r->length;
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

static int iio_set_length_kfifo(struct iio_buffer *r, int length)
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

	wake_up_interruptible_poll(&r->pollq, POLLIN | POLLRDNORM);

	return 0;
}

static int iio_read_first_n_kfifo(struct iio_buffer *r,
			   size_t n, char __user *buf)
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

static bool iio_kfifo_buf_data_available(struct iio_buffer *r)
{
	struct iio_kfifo *kf = iio_to_kfifo(r);
	bool empty;

	mutex_lock(&kf->user_lock);
	empty = kfifo_is_empty(&kf->kf);
	mutex_unlock(&kf->user_lock);

	return !empty;
}

static void iio_kfifo_buffer_release(struct iio_buffer *buffer)
{
	struct iio_kfifo *kf = iio_to_kfifo(buffer);

	mutex_destroy(&kf->user_lock);
	kfifo_free(&kf->kf);
	kfree(kf);
}

static const struct iio_buffer_access_funcs kfifo_access_funcs = {
	.store_to = &iio_store_to_kfifo,
	.read_first_n = &iio_read_first_n_kfifo,
	.data_available = iio_kfifo_buf_data_available,
	.request_update = &iio_request_update_kfifo,
	.set_bytes_per_datum = &iio_set_bytes_per_datum_kfifo,
	.get_length = &iio_get_length_kfifo,
	.set_length = &iio_set_length_kfifo,
	.release = &iio_kfifo_buffer_release,
};

struct iio_buffer *iio_kfifo_allocate(struct iio_dev *indio_dev)
{
	struct iio_kfifo *kf;

	kf = kzalloc(sizeof *kf, GFP_KERNEL);
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

MODULE_LICENSE("GPL");
