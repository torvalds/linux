/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Andes Technology Corporation */

#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_KASAN

#include <asm/pgtable.h>

#define KASAN_SHADOW_SCALE_SHIFT	3

#define KASAN_SHADOW_SIZE	(UL(1) << (38 - KASAN_SHADOW_SCALE_SHIFT))
#define KASAN_SHADOW_START	0xffffffc000000000 /* 2^64 - 2^38 */
#define KASAN_SHADOW_END	(KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

#define KASAN_SHADOW_OFFSET	(KASAN_SHADOW_END - (1ULL << \
					(64 - KASAN_SHADOW_SCALE_SHIFT)))

void kasan_init(void);
asmlinkage void kasan_early_init(void);

#endif
#endif
#endif /* __ASM_KASAN_H */
