/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_ARM64_RING_BUFFER_H
#define _ASM_ARM64_RING_BUFFER_H

#include <asm/cacheflush.h>

/* Flush D-cache on persistent ring buffer */
#define arch_ring_buffer_flush_range(start, end)	dcache_clean_pop(start, end)

#endif /* _ASM_ARM64_RING_BUFFER_H */
