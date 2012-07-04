/* The industrial I/O simple minimally locked ring buffer.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This code is deliberately kept separate from the main industrialio I/O core
 * as it is intended that in the future a number of different software ring
 * buffer implementations will exist with different characteristics to suit
 * different applications.
 *
 * This particular one was designed for a data capture application where it was
 * particularly important that no userspace reads would interrupt the capture
 * process. To this end the ring is not locked during a read.
 *
 * Comments on this buffer design welcomed. It's far from efficient and some of
 * my understanding of the effects of scheduling on this are somewhat limited.
 * Frankly, to my mind, this is the current weak point in the industrial I/O
 * patch set.
 */

#ifndef _IIO_RING_SW_H_
#define _IIO_RING_SW_H_
#include <linux/iio/buffer.h>

struct iio_buffer *iio_sw_rb_allocate(struct iio_dev *indio_dev);
void iio_sw_rb_free(struct iio_buffer *ring);
#endif /* _IIO_RING_SW_H_ */
