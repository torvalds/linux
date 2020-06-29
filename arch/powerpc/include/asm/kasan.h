/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifdef CONFIG_KASAN
#define _GLOBAL_KASAN(fn)	_GLOBAL(__##fn)
#define _GLOBAL_TOC_KASAN(fn)	_GLOBAL_TOC(__##fn)
#define EXPORT_SYMBOL_KASAN(fn)	EXPORT_SYMBOL(__##fn)
#else
#define _GLOBAL_KASAN(fn)	_GLOBAL(fn)
#define _GLOBAL_TOC_KASAN(fn)	_GLOBAL_TOC(fn)
#define EXPORT_SYMBOL_KASAN(fn)
#endif

#ifndef __ASSEMBLY__

#include <asm/page.h>

#define KASAN_SHADOW_SCALE_SHIFT	3

#define KASAN_SHADOW_START	(KASAN_SHADOW_OFFSET + \
				 (PAGE_OFFSET >> KASAN_SHADOW_SCALE_SHIFT))

#define KASAN_SHADOW_OFFSET	ASM_CONST(CONFIG_KASAN_SHADOW_OFFSET)

#define KASAN_SHADOW_END	(-(-KASAN_SHADOW_START >> KASAN_SHADOW_SCALE_SHIFT))

#ifdef CONFIG_KASAN
void kasan_early_init(void);
void kasan_init(void);
void kasan_late_init(void);
#else
static inline void kasan_init(void) { }
static inline void kasan_late_init(void) { }
#endif

void kasan_update_early_region(unsigned long k_start, unsigned long k_end, pte_t pte);
int kasan_init_shadow_page_tables(unsigned long k_start, unsigned long k_end);
int kasan_init_region(void *start, size_t size);

#endif /* __ASSEMBLY */
#endif
