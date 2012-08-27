#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/iio/kfifo_buf.h>

struct iio_kfifo {
	struct iio_buffer buffer;
	struct kfifo kf;
	int update_needed;
};

#define iio_to_kfifo(r) container_of(r, struct iio_kfifo, buffer)

static inline int __iio_allocate_kfifo(struct iio_kfifo *buf,
				int bytes_per_datum, int length)
{
	if ((length == 0) || (bytes_per_datum == 0))
		return -EINVAL;

	__iio_update_buffer(&buf->buffer, bytes_per_datum, length);
	return __kfifo_alloc((struct __kfifo *)&buf->kf, length,
			     bytes_per_datum, GFP_KERNEL);
}

static int iio_request_update_kfifo(struct iio_buffer *r)
{
	int ret = 0;
	struct iio_kfifo *buf = iio_to_kfifo(r);

	if (!buf->update_needed)
		goto error_ret;
	kfifo_free(&buf->kf);
	ret = __iio_allocate_kfifo(buf, buf->buffer.bytes_per_datum,
				   buf->buffer.length);
error_ret:
	return ret;
}

static int iio_get_length_kfifo(struct iio_buffer *r)
{
	return r->length;
}

static IIO_BUFFER_ENABLE_ATTR;
static IIO_BUFFER_LENGTH_ATTR;

static struct attribute *iio_kfifo_attributes[] = {
	&dev_attr_length.attr,
	&dev_attr_enable.attr,
	NULL,
};

static struct attribute_group iio_kfifo_attribute_group = {
	.attrs = iio_kfifo_attributes,
	.name = "buffer",
};

static int iio_get_bytes_per_datum_kfifo(struct iio_buffer *r)
{
	return r->bytes_per_datum;
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
	if (r->length != length) {
		r->length = length;
		iio_mark_update_needed_kfifo(r);
	}
	return 0;
}

static int iio_store_to_kfifo(struct iio_buffer *r,
			      u8 *data,
			      s64 timestamp)
{
	int ret;
	struct iio_kfifo *kf = iio_to_kfifo(r);
	ret = kfifo_in(&kf->kf, data, 1);
	if (ret != 1)
		return -EBUSY;

	return 0;
}

static int iio_read_first_n_kfifo(struct iio_buffer *r,
			   size_t n, char __user *buf)
{
	int ret, copied;
	struct iio_kfifo *kf = iio_to_kfifo(r);

	if (n < r->bytes_per_datum || r->bytes_per_datum == 0)
		return -EINVAL;

	ret = kfifo_to_user(&kf->kf, buf, n, &copied);
	if (ret < 0)
		return ret;

	return copied;
}

static const struct iio_buffer_access_funcs kfifo_access_funcs = {
	.store_to = &iio_store_to_kfifo,
	.read_first_n = &iio_read_first_n_kfifo,
	.request_update = &iio_request_update_kfifo,
	.get_bytes_per_datum = &iio_get_bytes_per_datum_kfifo,
	.set_bytes_per_datum = &iio_set_bytes_per_datum_kfifo,
	.get_length = &iio_get_length_kfifo,
	.set_length = &iio_set_length_kfifo,
};

struct iio_buffer *iio_kfifo_allocate(struct iio_dev *indio_dev)
{
	struct iio_kfifo *kf;

	kf = kzalloc(sizeof *kf, GFP_KERNEL);
	if (!kf)
		return NULL;
	kf->update_needed = true;
	iio_buffer_init(&kf->buffer);
	kf->buffer.attrs = &iio_kfifo_attribute_group;
	kf->buffer.access = &kfifo_access_funcs;

	return &kf->buffer;
}
EXPORT_SYMBOL(iio_kfifo_allocate);

void iio_kfifo_free(struct iio_buffer *r)
{
	kfree(iio_to_kfifo(r));
}
EXPORT_SYMBOL(iio_kfifo_free);

MODULE_LICENSE("GPL");
