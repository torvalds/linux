/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LOONGARCH_CRASH_RESERVE_H
#define _LOONGARCH_CRASH_RESERVE_H

#define CRASH_ALIGN			SZ_2M

#define CRASH_ADDR_LOW_MAX		SZ_4G
#define CRASH_ADDR_HIGH_MAX		memblock_end_of_DRAM()

extern phys_addr_t memblock_end_of_DRAM(void);

#endif
