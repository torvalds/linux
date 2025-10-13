/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_INTERNALS_H
#define I3C_INTERNALS_H

#include <linux/i3c/master.h>
#include <linux/io.h>

void i3c_bus_normaluse_lock(struct i3c_bus *bus);
void i3c_bus_normaluse_unlock(struct i3c_bus *bus);

int i3c_dev_setdasa_locked(struct i3c_dev_desc *dev);
int i3c_dev_do_priv_xfers_locked(struct i3c_dev_desc *dev,
				 struct i3c_priv_xfer *xfers,
				 int nxfers);
int i3c_dev_disable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_enable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_request_ibi_locked(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req);
void i3c_dev_free_ibi_locked(struct i3c_dev_desc *dev);

/**
 * i3c_writel_fifo - Write data buffer to 32bit FIFO
 * @addr: FIFO Address to write to
 * @buf: Pointer to the data bytes to write
 * @nbytes: Number of bytes to write
 */
static inline void i3c_writel_fifo(void __iomem *addr, const void *buf,
				   int nbytes)
{
	writesl(addr, buf, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp = 0;

		memcpy(&tmp, buf + (nbytes & ~3), nbytes & 3);
		/*
		 * writesl() instead of writel() to keep FIFO
		 * byteorder on big-endian targets
		 */
		writesl(addr, &tmp, 1);
	}
}

/**
 * i3c_readl_fifo - Read data buffer from 32bit FIFO
 * @addr: FIFO Address to read from
 * @buf: Pointer to the buffer to store read bytes
 * @nbytes: Number of bytes to read
 */
static inline void i3c_readl_fifo(const void __iomem *addr, void *buf,
				  int nbytes)
{
	readsl(addr, buf, nbytes / 4);
	if (nbytes & 3) {
		u32 tmp;

		/*
		 * readsl() instead of readl() to keep FIFO
		 * byteorder on big-endian targets
		 */
		readsl(addr, &tmp, 1);
		memcpy(buf + (nbytes & ~3), &tmp, nbytes & 3);
	}
}

#endif /* I3C_INTERNAL_H */
