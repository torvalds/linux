/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Limited
 * Copyright (C) 2014 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_VDSO_H
#define _ASM_RISCV_VDSO_H

#include <linux/types.h>

#ifndef CONFIG_GENERIC_TIME_VSYSCALL
struct vdso_data {
};
#endif

/*
 * The VDSO symbols are mapped into Linux so we can just use regular symbol
 * addressing to get their offsets in userspace.  The symbols are mapped at an
 * offset of 0, but since the linker must support setting weak undefined
 * symbols to the absolute address 0 it also happens to support other low
 * addresses even when the code model suggests those low addresses would not
 * otherwise be availiable.
 */
#define VDSO_SYMBOL(base, name)							\
({										\
	extern const char __vdso_##name[];					\
	(void __user *)((unsigned long)(base) + __vdso_##name);			\
})

asmlinkage long sys_riscv_flush_icache(uintptr_t, uintptr_t, uintptr_t);

#endif /* _ASM_RISCV_VDSO_H */
