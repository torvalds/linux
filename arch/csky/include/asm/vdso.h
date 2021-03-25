/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_VDSO_H
#define __ASM_CSKY_VDSO_H

#include <linux/types.h>

#ifndef GENERIC_TIME_VSYSCALL
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

#endif /* __ASM_CSKY_VDSO_H */
