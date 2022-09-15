/* SPDX-License-Identifier: GPL-2.0 */
/*
 * x86 KMSAN support.
 *
 * Copyright (C) 2022, Google LLC
 * Author: Alexander Potapenko <glider@google.com>
 */

#ifndef _ASM_X86_KMSAN_H
#define _ASM_X86_KMSAN_H

#ifndef MODULE

#include <asm/processor.h>
#include <linux/mmzone.h>

/*
 * Taken from arch/x86/mm/physaddr.h to avoid using an instrumented version.
 */
static inline bool kmsan_phys_addr_valid(unsigned long addr)
{
	if (IS_ENABLED(CONFIG_PHYS_ADDR_T_64BIT))
		return !(addr >> boot_cpu_data.x86_phys_bits);
	else
		return true;
}

/*
 * Taken from arch/x86/mm/physaddr.c to avoid using an instrumented version.
 */
static inline bool kmsan_virt_addr_valid(void *addr)
{
	unsigned long x = (unsigned long)addr;
	unsigned long y = x - __START_KERNEL_map;

	/* use the carry flag to determine if x was < __START_KERNEL_map */
	if (unlikely(x > y)) {
		x = y + phys_base;

		if (y >= KERNEL_IMAGE_SIZE)
			return false;
	} else {
		x = y + (__START_KERNEL_map - PAGE_OFFSET);

		/* carry flag will be set if starting x was >= PAGE_OFFSET */
		if ((x > y) || !kmsan_phys_addr_valid(x))
			return false;
	}

	return pfn_valid(x >> PAGE_SHIFT);
}

#endif /* !MODULE */

#endif /* _ASM_X86_KMSAN_H */
