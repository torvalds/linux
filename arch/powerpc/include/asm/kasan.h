/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#if defined(CONFIG_KASAN) && !defined(CONFIG_CC_HAS_KASAN_MEMINTRINSIC_PREFIX)
#define _GLOBAL_KASAN(fn)	_GLOBAL(__##fn)
#define _GLOBAL_TOC_KASAN(fn)	_GLOBAL_TOC(__##fn)
#define EXPORT_SYMBOL_KASAN(fn)	EXPORT_SYMBOL(__##fn)
#else
#define _GLOBAL_KASAN(fn)	_GLOBAL(fn)
#define _GLOBAL_TOC_KASAN(fn)	_GLOBAL_TOC(fn)
#define EXPORT_SYMBOL_KASAN(fn)
#endif

#ifndef __ASSEMBLER__

#include <asm/page.h>
#include <linux/sizes.h>

#define KASAN_SHADOW_SCALE_SHIFT	3

#if defined(CONFIG_EXECMEM) && defined(CONFIG_PPC32)
#define KASAN_KERN_START	ALIGN_DOWN(PAGE_OFFSET - SZ_256M, SZ_256M)
#else
#define KASAN_KERN_START	PAGE_OFFSET
#endif

#define KASAN_SHADOW_START	(KASAN_SHADOW_OFFSET + \
				 (KASAN_KERN_START >> KASAN_SHADOW_SCALE_SHIFT))

#define KASAN_SHADOW_OFFSET	ASM_CONST(CONFIG_KASAN_SHADOW_OFFSET)

#ifdef CONFIG_PPC32
#define KASAN_SHADOW_END	(-(-KASAN_SHADOW_START >> KASAN_SHADOW_SCALE_SHIFT))
#elif defined(CONFIG_PPC_BOOK3S_64)
/*
 * The shadow ends before the highest accessible address
 * because we don't need a shadow for the shadow. Instead:
 * c00e000000000000 << 3 + a80e000000000000 = c00fc00000000000
 */
#define KASAN_SHADOW_END 0xc00fc00000000000UL

#else

/*
 * The shadow ends before the highest accessible address
 * because we don't need a shadow for the shadow.
 * But it doesn't hurt to have a shadow for the shadow,
 * keep shadow end aligned eases things.
 */
#define KASAN_SHADOW_END 0xc000200000000000UL

#endif

#ifdef CONFIG_KASAN

void kasan_early_init(void);
void kasan_mmu_init(void);
void kasan_init(void);
void kasan_late_init(void);
#else
static inline void kasan_init(void) { }
static inline void kasan_mmu_init(void) { }
static inline void kasan_late_init(void) { }
#endif

void kasan_update_early_region(unsigned long k_start, unsigned long k_end, pte_t pte);
int kasan_init_shadow_page_tables(unsigned long k_start, unsigned long k_end);
int kasan_init_region(void *start, size_t size);

#endif /* __ASSEMBLER__ */
#endif
