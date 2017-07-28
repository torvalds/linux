/*
 * Copyright (C) 2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
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
