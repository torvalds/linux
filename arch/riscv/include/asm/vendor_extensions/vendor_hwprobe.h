/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024 Rivos, Inc
 */

#ifndef _ASM_RISCV_SYS_HWPROBE_H
#define _ASM_RISCV_SYS_HWPROBE_H

#include <asm/cpufeature.h>

#define VENDOR_EXT_KEY(ext)								\
	do {										\
		if (__riscv_isa_extension_available(isainfo->isa, RISCV_ISA_VENDOR_EXT_##ext)) \
			pair->value |= RISCV_HWPROBE_VENDOR_EXT_##ext;			\
		else									\
			missing |= RISCV_HWPROBE_VENDOR_EXT_##ext;			\
	} while (false)

/*
 * Loop through and record extensions that 1) anyone has, and 2) anyone
 * doesn't have.
 *
 * _extension_checks is an arbitrary C block to set the values of pair->value
 * and missing. It should be filled with VENDOR_EXT_KEY expressions.
 */
#define VENDOR_EXTENSION_SUPPORTED(pair, cpus, per_hart_vendor_bitmap, _extension_checks)	\
	do {											\
		int cpu;									\
		u64 missing = 0;								\
		for_each_cpu(cpu, (cpus)) {							\
			struct riscv_isavendorinfo *isainfo = &(per_hart_vendor_bitmap)[cpu];	\
			_extension_checks							\
		}										\
		(pair)->value &= ~missing;							\
	} while (false)										\

#endif /* _ASM_RISCV_SYS_HWPROBE_H */
