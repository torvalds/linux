/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/linkage.h>

#include <asm/fpsimdmacros.h>

	.text
	.pushsection	.hyp.text, "ax"

ENTRY(__fpsimd_save_state)
	fpsimd_save	x0, 1
	ret
ENDPROC(__fpsimd_save_state)

ENTRY(__fpsimd_restore_state)
	fpsimd_restore	x0, 1
	ret
ENDPROC(__fpsimd_restore_state)
