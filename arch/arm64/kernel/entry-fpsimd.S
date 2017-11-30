/*
 * FP/SIMD state saving and restoring
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
	sve_load 0, x1, x2, 3
	ret
ENDPROC(sve_load_state)

ENTRY(sve_get_vl)
	_sve_rdvl	0, 1
	ret
ENDPROC(sve_get_vl)
#endif /* CONFIG_ARM64_SVE */
