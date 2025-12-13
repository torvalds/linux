/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Andes Technology Corporation */

#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLER__

/*
 * The following comment was copied from arm64:
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
 *                              (1ULL << (64 - KASAN_SHADOW_SCALE_SHIFT))
 */
#define KASAN_SHADOW_SCALE_SHIFT	3

#define KASAN_SHADOW_SIZE	(UL(1) << ((VA_BITS - 1) - KASAN_SHADOW_SCALE_SHIFT))
/*
 * Depending on the size of the virtual address space, the region may not be
 * aligned on PGDIR_SIZE, so force its alignment to ease its population.
 */
#define KASAN_SHADOW_START	((KASAN_SHADOW_END - KASAN_SHADOW_SIZE) & PGDIR_MASK)
#define KASAN_SHADOW_END	MODULES_LOWEST_VADDR

#ifdef CONFIG_KASAN
#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

void kasan_init(void);
asmlinkage void kasan_early_init(void);
void kasan_swapper_init(void);

#endif
#endif
#endif /* __ASM_KASAN_H */
