/*
 * OSKA Linux implementation -- memory mapped I/O.
 *
 * Copyright (C) 2009 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_IO_H
#define __OSKA_LINUX_IO_H

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/kernel-compat.h>

typedef void __iomem *os_io_mem_t;

static inline uint8_t os_io_read8(os_io_mem_t base, unsigned offset)
{
    return readb(base + offset);
}

static inline uint16_t os_io_read16(os_io_mem_t base, unsigned offset)
{
    return readw(base + offset);
}

static inline uint32_t os_io_read32(os_io_mem_t base, unsigned offset)
{
    return readl(base + offset);
}

static inline uint64_t os_io_read64(os_io_mem_t base, unsigned offset)
{
    return readq(base + offset);
}

static inline void os_io_write8(os_io_mem_t base, unsigned offset, uint8_t val)
{
    writeb(val, base + offset);
}

static inline void os_io_write16(os_io_mem_t base, unsigned offset, uint16_t val)
{
    writew(val, base + offset);
}

static inline void os_io_write32(os_io_mem_t base, unsigned offset, uint32_t val)
{
    writel(val, base + offset);
}

static inline void os_io_write64(os_io_mem_t base, unsigned offset, uint64_t val)
{
    writeq(val, base + offset);
}

static inline void os_io_memory_barrier(void)
{
    mb();
}

#endif /* #ifndef __OSKA_LINUX_IO_H */
