/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <asm/ppc_asm.h>
#include <asm/vdso.h>

	.text
/*
 * Exact prototype of getcpu
 *
 * int __kernel_getcpu(unsigned *cpu, unsigned *node);
 *
 */
V_FUNCTION_BEGIN(__kernel_getcpu)
  .cfi_startproc
	mfspr	r5,SPRN_SPRG_VDSO_READ
	cmpdi	cr0,r3,0
	cmpdi	cr1,r4,0
	clrlwi  r6,r5,16
	rlwinm  r7,r5,16,31-15,31-0
	beq	cr0,1f
	stw	r6,0(r3)
1:	beq	cr1,2f
	stw	r7,0(r4)
2:	crclr	cr0*4+so
	li	r3,0			/* always success */
	blr
  .cfi_endproc
V_FUNCTION_END(__kernel_getcpu)
