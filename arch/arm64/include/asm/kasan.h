/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <asm/memory.h>
#include <asm/mte-kasan.h>
#include <asm/pgtable-types.h>

#define arch_kasan_set_tag(addr, tag)	__tag_set(addr, tag)
#define arch_kasan_reset_tag(addr)	__tag_reset(addr)
#define arch_kasan_get_tag(addr)	__tag_get(addr)

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

void kasan_init(void);

/*
 * KASAN_SHADOW_START: beginning of the kernel virtual addresses.
 * KASAN_SHADOW_END: KASAN_SHADOW_START + 1/N of kernel virtual addresses,
 * where N = (1 << KASAN_SHADOW_SCALE_SHIFT).
 *
 * KASAN_SHADOW_OFFSET:
 * This value is used to map an address to the corresponding shadow
 * address by the following formula:
 *     shadow_addr = (address >> KASAN_SHADOW_SCALE_SHIFT) + KASAN_SHADOW_OFFSET
 *
 * (1 << (64 - KASAN_SHADOW_SCALE_SHIFT)) shadow addresses that lie in range
 * [KASAN_SHADOW_OFFSET, KASAN_SHADOW_END) cover all 64-bits of virtual
 * addresses. So KASAN_SHADOW_OFFSET should satisfy the following equation:
 *      KASAN_SHADOW_OFFSET = KASAN_SHADOW_END -
 *				(1ULL << (64 - KASAN_SHADOW_SCALE_SHIFT))
 */
#define _KASAN_SHADOW_START(va)	(KASAN_SHADOW_END - (1UL << ((va) - KASAN_SHADOW_SCALE_SHIFT)))
#define KASAN_SHADOW_START      _KASAN_SHADOW_START(vabits_actual)

void kasan_copy_shadow(pgd_t *pgdir);
asmlinkage void kasan_early_init(void);

#else
static inline void kasan_init(void) { }
static inline void kasan_copy_shadow(pgd_t *pgdir) { }
#endif

#endif
#endif
