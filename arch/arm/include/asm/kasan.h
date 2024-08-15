/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/include/asm/kasan.h
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 */

#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifdef CONFIG_KASAN

#include <asm/kasan_def.h>

#define KASAN_SHADOW_SCALE_SHIFT 3

/*
 * The compiler uses a shadow offset assuming that addresses start
 * from 0. Kernel addresses don't start from 0, so shadow
 * for kernel really starts from 'compiler's shadow offset' +
 * ('kernel address space start' >> KASAN_SHADOW_SCALE_SHIFT)
 */

asmlinkage void kasan_early_init(void);
extern void kasan_init(void);

#else
static inline void kasan_init(void) { }
#endif

#endif
