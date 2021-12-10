/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 ARM Ltd.
 */
#ifndef __ASM_VECTORS_H
#define __ASM_VECTORS_H

#include <linux/bug.h>
#include <linux/percpu.h>

#include <asm/fixmap.h>

extern char vectors[];
extern char tramp_vectors[];
extern char __bp_harden_el1_vectors[];

/*
 * Note: the order of this enum corresponds to two arrays in entry.S:
 * tramp_vecs and __bp_harden_el1_vectors. By default the canonical
 * 'full fat' vectors are used directly.
 */
enum arm64_bp_harden_el1_vectors {
#ifdef CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY
	/*
	 * Perform the BHB loop mitigation, before branching to the canonical
	 * vectors.
	 */
	EL1_VECTOR_BHB_LOOP,

	/*
	 * Make the SMC call for firmware mitigation, before branching to the
	 * canonical vectors.
	 */
	EL1_VECTOR_BHB_FW,

	/*
	 * Use the ClearBHB instruction, before branching to the canonical
	 * vectors.
	 */
	EL1_VECTOR_BHB_CLEAR_INSN,
#endif /* CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY */

	/*
	 * Remap the kernel before branching to the canonical vectors.
	 */
	EL1_VECTOR_KPTI,
};

#ifndef CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY
#define EL1_VECTOR_BHB_LOOP		-1
#define EL1_VECTOR_BHB_FW		-1
#define EL1_VECTOR_BHB_CLEAR_INSN	-1
#endif /* !CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY */

/* The vectors to use on return from EL0. e.g. to remap the kernel */
DECLARE_PER_CPU_READ_MOSTLY(const char *, this_cpu_vector);

#ifndef CONFIG_UNMAP_KERNEL_AT_EL0
#define TRAMP_VALIAS	0
#endif

static inline const char *
arm64_get_bp_hardening_vector(enum arm64_bp_harden_el1_vectors slot)
{
	if (arm64_kernel_unmapped_at_el0())
		return (char *)TRAMP_VALIAS + SZ_2K * slot;

	WARN_ON_ONCE(slot == EL1_VECTOR_KPTI);

	return __bp_harden_el1_vectors + SZ_2K * slot;
}

#endif /* __ASM_VECTORS_H */
