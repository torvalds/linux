/*
 * Copyright (C) 2012 ARM Limited
 * Copyright (C) 2014 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ASM_RISCV_VDSO_H
#define _ASM_RISCV_VDSO_H

#include <linux/types.h>

struct vdso_data {
};

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
