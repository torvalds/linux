/* SPDX-License-Identifier: GPL-2.0 */
	.globl		getcc
	.type		getcc,#function
getcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	srlx		%o1, 32, %o1
	and		%o1, 0xf, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_G1]
	.size		getcc,.-getcc

	.globl		setcc
	.type		setcc,#function
setcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	ldx		[%o0 + PT_V9_G1], %o2
	or		%g0, %ulo(TSTATE_ICC), %o3
	sllx		%o3, 32, %o3
	andn		%o1, %o3, %o1
	sllx		%o2, 32, %o2
	and		%o2, %o3, %o2
	or		%o1, %o2, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_TSTATE]
	.size		setcc,.-setcc
