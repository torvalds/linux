/*
 * OSKA Linux implementation -- memory allocation
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef __OSKA_LINUX_ALLOC_H
#define __OSKA_LINUX_ALLOC_H

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

static inline void *os_alloc(size_t size)
{
    return kzalloc(size, GFP_ATOMIC);
}

static inline void *os_alloc_nonzeroed(size_t size)
{
    return kmalloc(size, GFP_KERNEL);
}

static inline void os_free(void *ptr)
{
    kfree(ptr);
}

static inline void *os_alloc_big(size_t size)
{
    return vmalloc(size);
}

static inline void os_free_big(void *ptr)
{
    vfree(ptr);
}

#endif /* #ifndef __OSKA_LINUX_ALLOC_H */
