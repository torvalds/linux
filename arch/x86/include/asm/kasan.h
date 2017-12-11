/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KASAN_H
#define _ASM_X86_KASAN_H

#include <linux/const.h>
#define KASAN_SHADOW_OFFSET _AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

/*
 * Compiler uses shadow offset assuming that addresses start
 * from 0. Kernel addresses don't start from 0, so shadow
 * for kernel really starts from compiler's shadow offset +
 * 'kernel address space start' >> KASAN_SHADOW_SCALE_SHIFT
 */
#define KASAN_SHADOW_START      (KASAN_SHADOW_OFFSET + \
					((-1UL << __VIRTUAL_MASK_SHIFT) >> 3))
/*
 * 47 bits for kernel address -> (47 - 3) bits for shadow
 * 56 bits for kernel address -> (56 - 3) bits for shadow
 */
#define KASAN_SHADOW_END        (KASAN_SHADOW_START + (1ULL << (__VIRTUAL_MASK_SHIFT - 3)))

#ifndef __ASSEMBLY__

#ifdef CONFIG_KASAN
void __init kasan_early_init(void);
void __init kasan_init(void);
#else
static inline void kasan_early_init(void) { }
static inline void kasan_init(void) { }
#endif

#endif

#endif
