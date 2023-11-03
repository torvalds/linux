/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _RISCV_CRASH_CORE_H
#define _RISCV_CRASH_CORE_H

#define CRASH_ALIGN			PMD_SIZE

#define CRASH_ADDR_LOW_MAX		dma32_phys_limit
#define CRASH_ADDR_HIGH_MAX		memblock_end_of_DRAM()

extern phys_addr_t memblock_end_of_DRAM(void);
#endif
