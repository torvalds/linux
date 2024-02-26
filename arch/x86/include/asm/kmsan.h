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

#include <asm/cpu_entry_area.h>
#include <asm/processor.h>
#include <linux/mmzone.h>

DECLARE_PER_CPU(char[CPU_ENTRY_AREA_SIZE], cpu_entry_area_shadow);
DECLARE_PER_CPU(char[CPU_ENTRY_AREA_SIZE], cpu_entry_area_origin);

/*
 * Functions below are declared in the header to make sure they are inlined.
 * They all are called from kmsan_get_metadata() for every memory access in
 * the kernel, so speed is important here.
 */

/*
 * Compute metadata addresses for the CPU entry area on x86.
 */
static inline void *arch_kmsan_get_meta_or_null(void *addr, bool is_origin)
{
	unsigned long addr64 = (unsigned long)addr;
	char *metadata_array;
	unsigned long off;
	int cpu;

	if ((addr64 < CPU_ENTRY_AREA_BASE) ||
	    (addr64 >= (CPU_ENTRY_AREA_BASE + CPU_ENTRY_AREA_MAP_SIZE)))
		return NULL;
	cpu = (addr64 - CPU_ENTRY_AREA_BASE) / CPU_ENTRY_AREA_SIZE;
	off = addr64 - (unsigned long)get_cpu_entry_area(cpu);
	if ((off < 0) || (off >= CPU_ENTRY_AREA_SIZE))
		return NULL;
	metadata_array = is_origin ? cpu_entry_area_origin :
				     cpu_entry_area_shadow;
	return &per_cpu(metadata_array[off], cpu);
}

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
	bool ret;

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

	/*
	 * pfn_valid() relies on RCU, and may call into the scheduler on exiting
	 * the critical section. However, this would result in recursion with
	 * KMSAN. Therefore, disable preemption here, and re-enable preemption
	 * below while suppressing reschedules to avoid recursion.
	 *
	 * Note, this sacrifices occasionally breaking scheduling guarantees.
	 * Although, a kernel compiled with KMSAN has already given up on any
	 * performance guarantees due to being heavily instrumented.
	 */
	preempt_disable();
	ret = pfn_valid(x >> PAGE_SHIFT);
	preempt_enable_no_resched();

	return ret;
}

#endif /* !MODULE */

#endif /* _ASM_X86_KMSAN_H */
