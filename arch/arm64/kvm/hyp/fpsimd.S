/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/linkage.h>

#include <asm/fpsimdmacros.h>

	.text
	.pushsection	.hyp.text, "ax"

SYM_FUNC_START(__fpsimd_save_state)
	fpsimd_save	x0, 1
	ret
SYM_FUNC_END(__fpsimd_save_state)

SYM_FUNC_START(__fpsimd_restore_state)
	fpsimd_restore	x0, 1
	ret
SYM_FUNC_END(__fpsimd_restore_state)
