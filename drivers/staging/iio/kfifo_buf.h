
#include <linux/kfifo.h>
#include "iio.h"
#include "buffer.h"

extern const struct iio_buffer_access_funcs kfifo_access_funcs;

struct iio_buffer *iio_kfifo_allocate(struct iio_dev *indio_dev);
void iio_kfifo_free(struct iio_buffer *r);

