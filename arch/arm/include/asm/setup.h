/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/include/asm/setup.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 *  Structure passed to kernel to tell it about the
 *  hardware it's running on.  See Documentation/arch/arm/setup.rst
 *  for more info.
 */
#ifndef __ASMARM_SETUP_H
#define __ASMARM_SETUP_H

#include <uapi/asm/setup.h>


#define __tag __used __section(".taglist.init")
#define __tagtable(tag, fn) \
static const struct tagtable __tagtable_##fn __tag = { tag, fn }

extern int arm_add_memory(u64 start, u64 size);
extern __printf(1, 2) void early_print(const char *str, ...);
extern void dump_machine_table(void);

#ifdef CONFIG_ATAGS_PROC
extern void save_atags(const struct tag *tags);
#else
static inline void save_atags(const struct tag *tags) { }
#endif

struct machine_desc;
void init_default_cache_policy(unsigned long);
void paging_init(const struct machine_desc *desc);
void early_mm_init(const struct machine_desc *);
void adjust_lowmem_bounds(void);
void setup_dma_zone(const struct machine_desc *desc);

#endif
