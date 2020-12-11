/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/include/asm/setup.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 *  Structure passed to kernel to tell it about the
 *  hardware it's running on.  See Documentation/arm/setup.rst
 *  for more info.
 */
#ifndef __ASMARM_SETUP_H
#define __ASMARM_SETUP_H

#include <uapi/asm/setup.h>


#define __tag __used __section(".taglist.init")
#define __tagtable(tag, fn) \
static const struct tagtable __tagtable_##fn __tag = { tag, fn }

extern int arm_add_memory(u64 start, u64 size);
extern void early_print(const char *str, ...);
extern void dump_machine_table(void);

#ifdef CONFIG_ATAGS_PROC
extern void save_atags(const struct tag *tags);
#else
static inline void save_atags(const struct tag *tags) { }
#endif

#endif
