#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_KASAN

#include <linux/linkage.h>
#include <asm/memory.h>

/*
 * KASAN_SHADOW_START: beginning of the kernel virtual addresses.
 * KASAN_SHADOW_END: KASAN_SHADOW_START + 1/8 of kernel virtual addresses.
 */
#define KASAN_SHADOW_START      (VA_START)
#define KASAN_SHADOW_END        (KASAN_SHADOW_START + (1UL << (VA_BITS - 3)))

/*
 * This value is used to map an address to the corresponding shadow
 * address by the following formula:
 *     shadow_addr = (address >> 3) + KASAN_SHADOW_OFFSET;
 *
 * (1 << 61) shadow addresses - [KASAN_SHADOW_OFFSET,KASAN_SHADOW_END]
 * cover all 64-bits of virtual addresses. So KASAN_SHADOW_OFFSET
 * should satisfy the following equation:
 *      KASAN_SHADOW_OFFSET = KASAN_SHADOW_END - (1ULL << 61)
 */
#define KASAN_SHADOW_OFFSET     (KASAN_SHADOW_END - (1ULL << (64 - 3)))

void kasan_init(void);
asmlinkage void kasan_early_init(void);

#else
static inline void kasan_init(void) { }
#endif

#endif
#endif
