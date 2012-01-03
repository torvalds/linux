
#include <linux/kfifo.h>
#include "iio.h"
#include "buffer.h"

struct iio_buffer *iio_kfifo_allocate(struct iio_dev *indio_dev);
void iio_kfifo_free(struct iio_buffer *r);

