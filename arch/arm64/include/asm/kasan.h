/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_KASAN

#include <linux/linkage.h>
#include <asm/memory.h>
#include <asm/pgtable-types.h>

/*
 * KASAN_SHADOW_START: beginning of the kernel virtual addresses.
 * KASAN_SHADOW_END: KASAN_SHADOW_START + 1/N of kernel virtual addresses,
 * where N = (1 << KASAN_SHADOW_SCALE_SHIFT).
 */
#define KASAN_SHADOW_START      (VA_START)
#define KASAN_SHADOW_END        (KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

/*
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
#define KASAN_SHADOW_OFFSET     (KASAN_SHADOW_END - (1ULL << \
					(64 - KASAN_SHADOW_SCALE_SHIFT)))

void kasan_init(void);
void kasan_copy_shadow(pgd_t *pgdir);
asmlinkage void kasan_early_init(void);

#else
static inline void kasan_init(void) { }
static inline void kasan_copy_shadow(pgd_t *pgdir) { }
#endif

#endif
#endif
