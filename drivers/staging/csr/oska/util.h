/*
 * OSKA Linux implementation -- misc. utility functions
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_UTILS_H
#define __OSKA_LINUX_UTILS_H

#include <linux/kernel.h>
#include <linux/bug.h>
#include <asm/byteorder.h>

#define OS_ASSERT(expr) BUG_ON(!(expr))

static inline uint16_t os_le16_to_cpu(uint16_t x)
{
    return le16_to_cpu(x);
}

static inline uint16_t os_cpu_to_le16(uint16_t x)
{
    return cpu_to_le16(x);
}

static inline uint32_t os_le32_to_cpu(uint32_t x)
{
    return le32_to_cpu(x);
}

static inline uint32_t os_cpu_to_le32(uint32_t x)
{
    return cpu_to_le32(x);
}

static inline uint64_t os_le64_to_cpu(uint64_t x)
{
    return le64_to_cpu(x);
}

static inline uint64_t os_cpu_to_le64(uint64_t x)
{
    return cpu_to_le64(x);
}

#endif /* __OSKA_LINUX_UTILS_H */
