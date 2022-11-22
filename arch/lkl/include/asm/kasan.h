/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_KASAN_H
#define _ASM_LKL_KASAN_H

#ifdef CONFIG_KASAN
#include <linux/const.h>
#include <linux/pgtable.h>

#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)
#define KASAN_SHADOW_SIZE	_AC(CONFIG_KASAN_SHADOW_SIZE, UL)

#define KASAN_SHADOW_SCALE_SHIFT 3

#define KASAN_SHADOW_START	KASAN_SHADOW_OFFSET
#define KASAN_SHADOW_END	(KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

extern int kasan_init(void);
extern int kasan_cleanup(void);
extern void kasan_unpoison_stack(void);
#else
static inline int kasan_init(void)
{
	return 0;
}

static inline int kasan_cleanup(void)
{
	return 0;
}

static inline void kasan_unpoison_stack(void)
{
}
#endif

#endif
