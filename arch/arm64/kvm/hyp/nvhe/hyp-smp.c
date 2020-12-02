// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

unsigned long __hyp_per_cpu_offset(unsigned int cpu)
{
	unsigned long *cpu_base_array;
	unsigned long this_cpu_base;
	unsigned long elf_base;

	if (cpu >= ARRAY_SIZE(kvm_arm_hyp_percpu_base))
		hyp_panic();

	cpu_base_array = (unsigned long *)hyp_symbol_addr(kvm_arm_hyp_percpu_base);
	this_cpu_base = kern_hyp_va(cpu_base_array[cpu]);
	elf_base = (unsigned long)hyp_symbol_addr(__per_cpu_start);
	return this_cpu_base - elf_base;
}
