/*
 * Copyright(c) 2004 - 2006 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#ifndef IOATDMA_IO_H
#define IOATDMA_IO_H

#include <asm/io.h>

/*
 * device and per-channel MMIO register read and write functions
 * this is a lot of anoying inline functions, but it's typesafe
 */

static inline u8 ioatdma_read8(struct ioat_device *device,
                               unsigned int offset)
{
	return readb(device->reg_base + offset);
}

static inline u16 ioatdma_read16(struct ioat_device *device,
                                 unsigned int offset)
{
	return readw(device->reg_base + offset);
}

static inline u32 ioatdma_read32(struct ioat_device *device,
                                 unsigned int offset)
{
	return readl(device->reg_base + offset);
}

static inline void ioatdma_write8(struct ioat_device *device,
                                  unsigned int offset, u8 value)
{
	writeb(value, device->reg_base + offset);
}

static inline void ioatdma_write16(struct ioat_device *device,
                                   unsigned int offset, u16 value)
{
	writew(value, device->reg_base + offset);
}

static inline void ioatdma_write32(struct ioat_device *device,
                                   unsigned int offset, u32 value)
{
	writel(value, device->reg_base + offset);
}

static inline u8 ioatdma_chan_read8(struct ioat_dma_chan *chan,
                                    unsigned int offset)
{
	return readb(chan->reg_base + offset);
}

static inline u16 ioatdma_chan_read16(struct ioat_dma_chan *chan,
                                      unsigned int offset)
{
	return readw(chan->reg_base + offset);
}

static inline u32 ioatdma_chan_read32(struct ioat_dma_chan *chan,
                                      unsigned int offset)
{
	return readl(chan->reg_base + offset);
}

static inline void ioatdma_chan_write8(struct ioat_dma_chan *chan,
                                       unsigned int offset, u8 value)
{
	writeb(value, chan->reg_base + offset);
}

static inline void ioatdma_chan_write16(struct ioat_dma_chan *chan,
                                        unsigned int offset, u16 value)
{
	writew(value, chan->reg_base + offset);
}

static inline void ioatdma_chan_write32(struct ioat_dma_chan *chan,
                                        unsigned int offset, u32 value)
{
	writel(value, chan->reg_base + offset);
}

#if (BITS_PER_LONG == 64)
static inline u64 ioatdma_chan_read64(struct ioat_dma_chan *chan,
                                      unsigned int offset)
{
	return readq(chan->reg_base + offset);
}

static inline void ioatdma_chan_write64(struct ioat_dma_chan *chan,
                                        unsigned int offset, u64 value)
{
	writeq(value, chan->reg_base + offset);
}
#endif

#endif /* IOATDMA_IO_H */

