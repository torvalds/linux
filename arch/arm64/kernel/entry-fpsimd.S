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
ENTRY(fpsimd_save_state)
	fpsimd_save x0, 8
	ret
ENDPROC(fpsimd_save_state)

/*
 * Load the FP registers.
 *
 * x0 - pointer to struct fpsimd_state
 */
ENTRY(fpsimd_load_state)
	fpsimd_restore x0, 8
	ret
ENDPROC(fpsimd_load_state)

#ifdef CONFIG_ARM64_SVE
ENTRY(sve_save_state)
	sve_save 0, x1, 2
	ret
ENDPROC(sve_save_state)

ENTRY(sve_load_state)
	sve_load 0, x1, x2, 3, x4
	ret
ENDPROC(sve_load_state)

ENTRY(sve_get_vl)
	_sve_rdvl	0, 1
	ret
ENDPROC(sve_get_vl)
#endif /* CONFIG_ARM64_SVE */
