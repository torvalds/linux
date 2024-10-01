/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ARM64_CRASH_RESERVE_H
#define _ARM64_CRASH_RESERVE_H

/* Current arm64 boot protocol requires 2MB alignment */
#define CRASH_ALIGN                     SZ_2M

#define CRASH_ADDR_LOW_MAX              arm64_dma_phys_limit
#define CRASH_ADDR_HIGH_MAX             (PHYS_MASK + 1)
#endif
