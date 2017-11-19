/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#include <asm/pgtable.h>

#ifdef CONFIG_KASAN

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_SIZE						       \
	(_AC(1, UL) << (_REGION2_SHIFT - KASAN_SHADOW_SCALE_SHIFT))
#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)
#define KASAN_SHADOW_START	KASAN_SHADOW_OFFSET
#define KASAN_SHADOW_END	(KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

extern void kasan_early_init(void);
extern void kasan_copy_shadow(pgd_t *dst);
#else
static inline void kasan_early_init(void) { }
static inline void kasan_copy_shadow(pgd_t *dst) { }
#endif

#endif
