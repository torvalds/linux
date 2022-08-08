/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Andes Technology Corporation */

#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_KASAN

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

#define KASAN_SHADOW_SIZE	(UL(1) << ((CONFIG_VA_BITS - 1) - KASAN_SHADOW_SCALE_SHIFT))
#define KASAN_SHADOW_START	KERN_VIRT_START
#define KASAN_SHADOW_END	(KASAN_SHADOW_START + KASAN_SHADOW_SIZE)
#define KASAN_SHADOW_OFFSET	(KASAN_SHADOW_END - (1ULL << \
					(64 - KASAN_SHADOW_SCALE_SHIFT)))

void kasan_init(void);
asmlinkage void kasan_early_init(void);

#endif
#endif
#endif /* __ASM_KASAN_H */
