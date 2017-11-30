/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
	Module: rt2x00mmio
	Abstract: Data structures for the rt2x00mmio module.
 */

#ifndef RT2X00MMIO_H
#define RT2X00MMIO_H

#include <linux/io.h>

/*
 * Register access.
 */
static inline u32 rt2x00mmio_register_read(struct rt2x00_dev *rt2x00dev,
					   const unsigned int offset)
{
	return readl(rt2x00dev->csr.base + offset);
}

static inline void rt2x00mmio_register_multiread(struct rt2x00_dev *rt2x00dev,
						 const unsigned int offset,
						 void *value, const u32 length)
{
	memcpy_fromio(value, rt2x00dev->csr.base + offset, length);
}

static inline void rt2x00mmio_register_write(struct rt2x00_dev *rt2x00dev,
					     const unsigned int offset,
					     u32 value)
{
	writel(value, rt2x00dev->csr.base + offset);
}

static inline void rt2x00mmio_register_multiwrite(struct rt2x00_dev *rt2x00dev,
						  const unsigned int offset,
						  const void *value,
						  const u32 length)
{
	__iowrite32_copy(rt2x00dev->csr.base + offset, value, length >> 2);
}

/**
 * rt2x00mmio_regbusy_read - Read from register with busy check
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 * @offset: Register offset
 * @field: Field to check if register is busy
 * @reg: Pointer to where register contents should be stored
 *
 * This function will read the given register, and checks if the
 * register is busy. If it is, it will sleep for a couple of
 * microseconds before reading the register again. If the register
 * is not read after a certain timeout, this function will return
 * FALSE.
 */
int rt2x00mmio_regbusy_read(struct rt2x00_dev *rt2x00dev,
			    const unsigned int offset,
			    const struct rt2x00_field32 field,
			    u32 *reg);

/**
 * struct queue_entry_priv_mmio: Per entry PCI specific information
 *
 * @desc: Pointer to device descriptor
 * @desc_dma: DMA pointer to &desc.
 * @data: Pointer to device's entry memory.
 * @data_dma: DMA pointer to &data.
 */
struct queue_entry_priv_mmio {
	__le32 *desc;
	dma_addr_t desc_dma;
};

/**
 * rt2x00mmio_rxdone - Handle RX done events
 * @rt2x00dev: Device pointer, see &struct rt2x00_dev.
 *
 * Returns true if there are still rx frames pending and false if all
 * pending rx frames were processed.
 */
bool rt2x00mmio_rxdone(struct rt2x00_dev *rt2x00dev);

/**
 * rt2x00mmio_flush_queue - Flush data queue
 * @queue: Data queue to stop
 * @drop: True to drop all pending frames.
 *
 * This will wait for a maximum of 100ms, waiting for the queues
 * to become empty.
 */
void rt2x00mmio_flush_queue(struct data_queue *queue, bool drop);

/*
 * Device initialization handlers.
 */
int rt2x00mmio_initialize(struct rt2x00_dev *rt2x00dev);
void rt2x00mmio_uninitialize(struct rt2x00_dev *rt2x00dev);

#endif /* RT2X00MMIO_H */
