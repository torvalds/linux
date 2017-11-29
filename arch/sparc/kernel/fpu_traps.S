/* SPDX-License-Identifier: GPL-2.0 */
	/* This is trivial with the new code... */
	.globl		do_fpdis
	.type		do_fpdis,#function
do_fpdis:
	sethi		%hi(TSTATE_PEF), %g4
	rdpr		%tstate, %g5
	andcc		%g5, %g4, %g0
	be,pt		%xcc, 1f
	 nop
	rd		%fprs, %g5
	andcc		%g5, FPRS_FEF, %g0
	be,pt		%xcc, 1f
	 nop

	/* Legal state when DCR_IFPOE is set in Cheetah %dcr. */
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	add		%g0, %g0, %g0
	ba,a,pt		%xcc, rtrap

1:	TRAP_LOAD_THREAD_REG(%g6, %g1)
	ldub		[%g6 + TI_FPSAVED], %g5
	wr		%g0, FPRS_FEF, %fprs
	andcc		%g5, FPRS_FEF, %g0
	be,a,pt		%icc, 1f
	 clr		%g7
	ldx		[%g6 + TI_GSR], %g7
1:	andcc		%g5, FPRS_DL, %g0
	bne,pn		%icc, 2f
	 fzero		%f0
	andcc		%g5, FPRS_DU, %g0
	bne,pn		%icc, 1f
	 fzero		%f2
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10
	faddd		%f0, %f2, %f12
	fmuld		%f0, %f2, %f14
	faddd		%f0, %f2, %f16
	fmuld		%f0, %f2, %f18
	faddd		%f0, %f2, %f20
	fmuld		%f0, %f2, %f22
	faddd		%f0, %f2, %f24
	fmuld		%f0, %f2, %f26
	faddd		%f0, %f2, %f28
	fmuld		%f0, %f2, %f30
	faddd		%f0, %f2, %f32
	fmuld		%f0, %f2, %f34
	faddd		%f0, %f2, %f36
	fmuld		%f0, %f2, %f38
	faddd		%f0, %f2, %f40
	fmuld		%f0, %f2, %f42
	faddd		%f0, %f2, %f44
	fmuld		%f0, %f2, %f46
	faddd		%f0, %f2, %f48
	fmuld		%f0, %f2, %f50
	faddd		%f0, %f2, %f52
	fmuld		%f0, %f2, %f54
	faddd		%f0, %f2, %f56
	fmuld		%f0, %f2, %f58
	b,pt		%xcc, fpdis_exit2
	 faddd		%f0, %f2, %f60
1:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, TI_FPREGS + 0x80, %g1
	faddd		%f0, %f2, %f4
	fmuld		%f0, %f2, %f6

661:	ldxa		[%g3] ASI_DMMU, %g5
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	ldxa		[%g3] ASI_MMU, %g5
	.previous

	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2

661:	stxa		%g2, [%g3] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g2, [%g3] ASI_MMU
	.previous

	membar		#Sync
	add		%g6, TI_FPREGS + 0xc0, %g2
	faddd		%f0, %f2, %f8
	fmuld		%f0, %f2, %f10
	membar		#Sync
	ldda		[%g1] ASI_BLK_S, %f32
	ldda		[%g2] ASI_BLK_S, %f48
	membar		#Sync
	faddd		%f0, %f2, %f12
	fmuld		%f0, %f2, %f14
	faddd		%f0, %f2, %f16
	fmuld		%f0, %f2, %f18
	faddd		%f0, %f2, %f20
	fmuld		%f0, %f2, %f22
	faddd		%f0, %f2, %f24
	fmuld		%f0, %f2, %f26
	faddd		%f0, %f2, %f28
	fmuld		%f0, %f2, %f30
	ba,a,pt		%xcc, fpdis_exit

2:	andcc		%g5, FPRS_DU, %g0
	bne,pt		%icc, 3f
	 fzero		%f32
	mov		SECONDARY_CONTEXT, %g3
	fzero		%f34

661:	ldxa		[%g3] ASI_DMMU, %g5
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	ldxa		[%g3] ASI_MMU, %g5
	.previous

	add		%g6, TI_FPREGS, %g1
	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2

661:	stxa		%g2, [%g3] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g2, [%g3] ASI_MMU
	.previous

	membar		#Sync
	add		%g6, TI_FPREGS + 0x40, %g2
	faddd		%f32, %f34, %f36
	fmuld		%f32, %f34, %f38
	membar		#Sync
	ldda		[%g1] ASI_BLK_S, %f0
	ldda		[%g2] ASI_BLK_S, %f16
	membar		#Sync
	faddd		%f32, %f34, %f40
	fmuld		%f32, %f34, %f42
	faddd		%f32, %f34, %f44
	fmuld		%f32, %f34, %f46
	faddd		%f32, %f34, %f48
	fmuld		%f32, %f34, %f50
	faddd		%f32, %f34, %f52
	fmuld		%f32, %f34, %f54
	faddd		%f32, %f34, %f56
	fmuld		%f32, %f34, %f58
	faddd		%f32, %f34, %f60
	fmuld		%f32, %f34, %f62
	ba,a,pt		%xcc, fpdis_exit

3:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, TI_FPREGS, %g1

661:	ldxa		[%g3] ASI_DMMU, %g5
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	ldxa		[%g3] ASI_MMU, %g5
	.previous

	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2

661:	stxa		%g2, [%g3] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g2, [%g3] ASI_MMU
	.previous

	membar		#Sync
	mov		0x40, %g2
	membar		#Sync
	ldda		[%g1] ASI_BLK_S, %f0
	ldda		[%g1 + %g2] ASI_BLK_S, %f16
	add		%g1, 0x80, %g1
	ldda		[%g1] ASI_BLK_S, %f32
	ldda		[%g1 + %g2] ASI_BLK_S, %f48
	membar		#Sync
fpdis_exit:

661:	stxa		%g5, [%g3] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g5, [%g3] ASI_MMU
	.previous

	membar		#Sync
fpdis_exit2:
	wr		%g7, 0, %gsr
	ldx		[%g6 + TI_XFSR], %fsr
	rdpr		%tstate, %g3
	or		%g3, %g4, %g3		! anal...
	wrpr		%g3, %tstate
	wr		%g0, FPRS_FEF, %fprs	! clean DU/DL bits
	retry
	.size		do_fpdis,.-do_fpdis

	.align		32
	.type		fp_other_bounce,#function
fp_other_bounce:
	call		do_fpother
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap
	.size		fp_other_bounce,.-fp_other_bounce

	.align		32
	.globl		do_fpother_check_fitos
	.type		do_fpother_check_fitos,#function
do_fpother_check_fitos:
	TRAP_LOAD_THREAD_REG(%g6, %g1)
	sethi		%hi(fp_other_bounce - 4), %g7
	or		%g7, %lo(fp_other_bounce - 4), %g7

	/* NOTE: Need to preserve %g7 until we fully commit
	 *       to the fitos fixup.
	 */
	stx		%fsr, [%g6 + TI_XFSR]
	rdpr		%tstate, %g3
	andcc		%g3, TSTATE_PRIV, %g0
	bne,pn		%xcc, do_fptrap_after_fsr
	 nop
	ldx		[%g6 + TI_XFSR], %g3
	srlx		%g3, 14, %g1
	and		%g1, 7, %g1
	cmp		%g1, 2			! Unfinished FP-OP
	bne,pn		%xcc, do_fptrap_after_fsr
	 sethi		%hi(1 << 23), %g1	! Inexact
	andcc		%g3, %g1, %g0
	bne,pn		%xcc, do_fptrap_after_fsr
	 rdpr		%tpc, %g1
	lduwa		[%g1] ASI_AIUP, %g3	! This cannot ever fail
#define FITOS_MASK	0xc1f83fe0
#define FITOS_COMPARE	0x81a01880
	sethi		%hi(FITOS_MASK), %g1
	or		%g1, %lo(FITOS_MASK), %g1
	and		%g3, %g1, %g1
	sethi		%hi(FITOS_COMPARE), %g2
	or		%g2, %lo(FITOS_COMPARE), %g2
	cmp		%g1, %g2
	bne,pn		%xcc, do_fptrap_after_fsr
	 nop
	std		%f62, [%g6 + TI_FPREGS + (62 * 4)]
	sethi		%hi(fitos_table_1), %g1
	and		%g3, 0x1f, %g2
	or		%g1, %lo(fitos_table_1),  %g1
	sllx		%g2, 2, %g2
	jmpl		%g1 + %g2, %g0
	 ba,pt		%xcc, fitos_emul_continue

fitos_table_1:
	fitod		%f0, %f62
	fitod		%f1, %f62
	fitod		%f2, %f62
	fitod		%f3, %f62
	fitod		%f4, %f62
	fitod		%f5, %f62
	fitod		%f6, %f62
	fitod		%f7, %f62
	fitod		%f8, %f62
	fitod		%f9, %f62
	fitod		%f10, %f62
	fitod		%f11, %f62
	fitod		%f12, %f62
	fitod		%f13, %f62
	fitod		%f14, %f62
	fitod		%f15, %f62
	fitod		%f16, %f62
	fitod		%f17, %f62
	fitod		%f18, %f62
	fitod		%f19, %f62
	fitod		%f20, %f62
	fitod		%f21, %f62
	fitod		%f22, %f62
	fitod		%f23, %f62
	fitod		%f24, %f62
	fitod		%f25, %f62
	fitod		%f26, %f62
	fitod		%f27, %f62
	fitod		%f28, %f62
	fitod		%f29, %f62
	fitod		%f30, %f62
	fitod		%f31, %f62

fitos_emul_continue:
	sethi		%hi(fitos_table_2), %g1
	srl		%g3, 25, %g2
	or		%g1, %lo(fitos_table_2), %g1
	and		%g2, 0x1f, %g2
	sllx		%g2, 2, %g2
	jmpl		%g1 + %g2, %g0
	 ba,pt		%xcc, fitos_emul_fini

fitos_table_2:
	fdtos		%f62, %f0
	fdtos		%f62, %f1
	fdtos		%f62, %f2
	fdtos		%f62, %f3
	fdtos		%f62, %f4
	fdtos		%f62, %f5
	fdtos		%f62, %f6
	fdtos		%f62, %f7
	fdtos		%f62, %f8
	fdtos		%f62, %f9
	fdtos		%f62, %f10
	fdtos		%f62, %f11
	fdtos		%f62, %f12
	fdtos		%f62, %f13
	fdtos		%f62, %f14
	fdtos		%f62, %f15
	fdtos		%f62, %f16
	fdtos		%f62, %f17
	fdtos		%f62, %f18
	fdtos		%f62, %f19
	fdtos		%f62, %f20
	fdtos		%f62, %f21
	fdtos		%f62, %f22
	fdtos		%f62, %f23
	fdtos		%f62, %f24
	fdtos		%f62, %f25
	fdtos		%f62, %f26
	fdtos		%f62, %f27
	fdtos		%f62, %f28
	fdtos		%f62, %f29
	fdtos		%f62, %f30
	fdtos		%f62, %f31

fitos_emul_fini:
	ldd		[%g6 + TI_FPREGS + (62 * 4)], %f62
	done
	.size		do_fpother_check_fitos,.-do_fpother_check_fitos

	.align		32
	.globl		do_fptrap
	.type		do_fptrap,#function
do_fptrap:
	TRAP_LOAD_THREAD_REG(%g6, %g1)
	stx		%fsr, [%g6 + TI_XFSR]
do_fptrap_after_fsr:
	ldub		[%g6 + TI_FPSAVED], %g3
	rd		%fprs, %g1
	or		%g3, %g1, %g3
	stb		%g3, [%g6 + TI_FPSAVED]
	rd		%gsr, %g3
	stx		%g3, [%g6 + TI_GSR]
	mov		SECONDARY_CONTEXT, %g3

661:	ldxa		[%g3] ASI_DMMU, %g5
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	ldxa		[%g3] ASI_MMU, %g5
	.previous

	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2

661:	stxa		%g2, [%g3] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g2, [%g3] ASI_MMU
	.previous

	membar		#Sync
	add		%g6, TI_FPREGS, %g2
	andcc		%g1, FPRS_DL, %g0
	be,pn		%icc, 4f
	 mov		0x40, %g3
	stda		%f0, [%g2] ASI_BLK_S
	stda		%f16, [%g2 + %g3] ASI_BLK_S
	andcc		%g1, FPRS_DU, %g0
	be,pn		%icc, 5f
4:       add		%g2, 128, %g2
	stda		%f32, [%g2] ASI_BLK_S
	stda		%f48, [%g2 + %g3] ASI_BLK_S
5:	mov		SECONDARY_CONTEXT, %g1
	membar		#Sync

661:	stxa		%g5, [%g1] ASI_DMMU
	.section	.sun4v_1insn_patch, "ax"
	.word		661b
	stxa		%g5, [%g1] ASI_MMU
	.previous

	membar		#Sync
	ba,pt		%xcc, etrap
	 wr		%g0, 0, %fprs
	.size		do_fptrap,.-do_fptrap
