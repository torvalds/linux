/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FP/SIMD state saving and restoring
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/fpsimdmacros.h>

/*
 * Save the FP registers.
 *
 * x0 - pointer to struct fpsimd_state
 */
SYM_FUNC_START(fpsimd_save_state)
	fpsimd_save x0, 8
	ret
SYM_FUNC_END(fpsimd_save_state)

/*
 * Load the FP registers.
 *
 * x0 - pointer to struct fpsimd_state
 */
SYM_FUNC_START(fpsimd_load_state)
	fpsimd_restore x0, 8
	ret
SYM_FUNC_END(fpsimd_load_state)

#ifdef CONFIG_ARM64_SVE

/*
 * Save the SVE state
 *
 * x0 - pointer to buffer for state
 * x1 - pointer to storage for FPSR
 * x2 - Save FFR if non-zero
 */
SYM_FUNC_START(sve_save_state)
	sve_save 0, x1, x2, 3
	ret
SYM_FUNC_END(sve_save_state)

/*
 * Load the SVE state
 *
 * x0 - pointer to buffer for state
 * x1 - pointer to storage for FPSR
 * x2 - Restore FFR if non-zero
 */
SYM_FUNC_START(sve_load_state)
	sve_load 0, x1, x2, 4
	ret
SYM_FUNC_END(sve_load_state)

SYM_FUNC_START(sve_get_vl)
	_sve_rdvl	0, 1
	ret
SYM_FUNC_END(sve_get_vl)

SYM_FUNC_START(sve_set_vq)
	sve_load_vq x0, x1, x2
	ret
SYM_FUNC_END(sve_set_vq)

/*
 * Zero all SVE registers but the first 128-bits of each vector
 *
 * VQ must already be configured by caller, any further updates of VQ
 * will need to ensure that the register state remains valid.
 *
 * x0 = include FFR?
 * x1 = VQ - 1
 */
SYM_FUNC_START(sve_flush_live)
	cbz		x1, 1f	// A VQ-1 of 0 is 128 bits so no extra Z state
	sve_flush_z
1:	sve_flush_p
	tbz		x0, #0, 2f
	sve_flush_ffr
2:	ret
SYM_FUNC_END(sve_flush_live)

#endif /* CONFIG_ARM64_SVE */
