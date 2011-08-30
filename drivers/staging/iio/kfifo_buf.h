
#include <linux/kfifo.h>
#include "iio.h"
#include "ring_generic.h"

extern const struct iio_ring_access_funcs kfifo_access_funcs;

struct iio_ring_buffer *iio_kfifo_allocate(struct iio_dev *indio_dev);
void iio_kfifo_free(struct iio_ring_buffer *r);

