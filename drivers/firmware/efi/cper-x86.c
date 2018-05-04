// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018, Advanced Micro Devices, Inc.

#include <linux/cper.h>

/*
 * We don't need a "CPER_IA" prefix since these are all locally defined.
 * This will save us a lot of line space.
 */
#define VALID_LAPIC_ID			BIT_ULL(0)
#define VALID_CPUID_INFO		BIT_ULL(1)

void cper_print_proc_ia(const char *pfx, const struct cper_sec_proc_ia *proc)
{
	if (proc->validation_bits & VALID_LAPIC_ID)
		printk("%sLocal APIC_ID: 0x%llx\n", pfx, proc->lapic_id);

	if (proc->validation_bits & VALID_CPUID_INFO) {
		printk("%sCPUID Info:\n", pfx);
		print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4, proc->cpuid,
			       sizeof(proc->cpuid), 0);
	}
}
