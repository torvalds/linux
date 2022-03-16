/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 ARM Ltd.
 */
#ifndef __ASM_VECTORS_H
#define __ASM_VECTORS_H

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
#endif /* CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY */

	/*
	 * Remap the kernel before branching to the canonical vectors.
	 */
	EL1_VECTOR_KPTI,
};

#endif /* __ASM_VECTORS_H */
