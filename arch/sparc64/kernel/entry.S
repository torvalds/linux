/* $Id: entry.S,v 1.144 2002/02/09 19:49:30 davem Exp $
 * arch/sparc64/kernel/entry.S:  Sparc64 trap low-level entry points.
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost        (ecd@skynet.be)
 * Copyright (C) 1996 Miguel de Icaza      (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996,98,99 Jakub Jelinek  (jj@sunsite.mff.cuni.cz)
 */

#include <linux/config.h>
#include <linux/errno.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/visasm.h>
#include <asm/estate.h>
#include <asm/auxio.h>
#include <asm/sfafsr.h>

#define curptr      g6

#define NR_SYSCALLS 300      /* Each OS is different... */

	.text
	.align		32

	/* This is trivial with the new code... */
	.globl		do_fpdis
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
	ba,a,pt		%xcc, rtrap_clr_l6

1:	ldub		[%g6 + TI_FPSAVED], %g5
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
	ldxa		[%g3] ASI_DMMU, %g5
	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2
	stxa		%g2, [%g3] ASI_DMMU
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
	b,pt		%xcc, fpdis_exit
	 nop
2:	andcc		%g5, FPRS_DU, %g0
	bne,pt		%icc, 3f
	 fzero		%f32
	mov		SECONDARY_CONTEXT, %g3
	fzero		%f34
	ldxa		[%g3] ASI_DMMU, %g5
	add		%g6, TI_FPREGS, %g1
	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2
	stxa		%g2, [%g3] ASI_DMMU
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
	ba,pt		%xcc, fpdis_exit
	 nop
3:	mov		SECONDARY_CONTEXT, %g3
	add		%g6, TI_FPREGS, %g1
	ldxa		[%g3] ASI_DMMU, %g5
	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2
	stxa		%g2, [%g3] ASI_DMMU
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
	stxa		%g5, [%g3] ASI_DMMU
	membar		#Sync
fpdis_exit2:
	wr		%g7, 0, %gsr
	ldx		[%g6 + TI_XFSR], %fsr
	rdpr		%tstate, %g3
	or		%g3, %g4, %g3		! anal...
	wrpr		%g3, %tstate
	wr		%g0, FPRS_FEF, %fprs	! clean DU/DL bits
	retry

	.align		32
fp_other_bounce:
	call		do_fpother
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_fpother_check_fitos
	.align		32
do_fpother_check_fitos:
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

	.globl		do_fptrap
	.align		32
do_fptrap:
	stx		%fsr, [%g6 + TI_XFSR]
do_fptrap_after_fsr:
	ldub		[%g6 + TI_FPSAVED], %g3
	rd		%fprs, %g1
	or		%g3, %g1, %g3
	stb		%g3, [%g6 + TI_FPSAVED]
	rd		%gsr, %g3
	stx		%g3, [%g6 + TI_GSR]
	mov		SECONDARY_CONTEXT, %g3
	ldxa		[%g3] ASI_DMMU, %g5
	sethi		%hi(sparc64_kern_sec_context), %g2
	ldx		[%g2 + %lo(sparc64_kern_sec_context)], %g2
	stxa		%g2, [%g3] ASI_DMMU
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
	stxa		%g5, [%g1] ASI_DMMU
	membar		#Sync
	ba,pt		%xcc, etrap
	 wr		%g0, 0, %fprs

	/* The registers for cross calls will be:
	 *
	 * DATA 0: [low 32-bits]  Address of function to call, jmp to this
	 *         [high 32-bits] MMU Context Argument 0, place in %g5
	 * DATA 1: Address Argument 1, place in %g1
	 * DATA 2: Address Argument 2, place in %g7
	 *
	 * With this method we can do most of the cross-call tlb/cache
	 * flushing very quickly.
	 *
	 * Current CPU's IRQ worklist table is locked into %g6, don't touch.
	 */
	.text
	.align		32
	.globl		do_ivec
do_ivec:
	mov		0x40, %g3
	ldxa		[%g3 + %g0] ASI_INTR_R, %g3
	sethi		%hi(KERNBASE), %g4
	cmp		%g3, %g4
	bgeu,pn		%xcc, do_ivec_xcall
	 srlx		%g3, 32, %g5
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync

	sethi		%hi(ivector_table), %g2
	sllx		%g3, 5, %g3
	or		%g2, %lo(ivector_table), %g2
	add		%g2, %g3, %g3
	ldub		[%g3 + 0x04], %g4	/* pil */
	mov		1, %g2
	sllx		%g2, %g4, %g2
	sllx		%g4, 2, %g4

	lduw		[%g6 + %g4], %g5	/* g5 = irq_work(cpu, pil) */
	stw		%g5, [%g3 + 0x00]	/* bucket->irq_chain = g5 */
	stw		%g3, [%g6 + %g4]	/* irq_work(cpu, pil) = bucket */
	wr		%g2, 0x0, %set_softint
	retry
do_ivec_xcall:
	mov		0x50, %g1
	ldxa		[%g1 + %g0] ASI_INTR_R, %g1
	srl		%g3, 0, %g3

	mov		0x60, %g7
	ldxa		[%g7 + %g0] ASI_INTR_R, %g7
	stxa		%g0, [%g0] ASI_INTR_RECEIVE
	membar		#Sync
	ba,pt		%xcc, 1f
	 nop

	.align		32
1:	jmpl		%g3, %g0
	 nop

	.globl		save_alternate_globals
save_alternate_globals: /* %o0 = save_area */
	rdpr		%pstate, %o5
	andn		%o5, PSTATE_IE, %o1
	wrpr		%o1, PSTATE_AG, %pstate
	stx		%g0, [%o0 + 0x00]
	stx		%g1, [%o0 + 0x08]
	stx		%g2, [%o0 + 0x10]
	stx		%g3, [%o0 + 0x18]
	stx		%g4, [%o0 + 0x20]
	stx		%g5, [%o0 + 0x28]
	stx		%g6, [%o0 + 0x30]
	stx		%g7, [%o0 + 0x38]
	wrpr		%o1, PSTATE_IG, %pstate
	stx		%g0, [%o0 + 0x40]
	stx		%g1, [%o0 + 0x48]
	stx		%g2, [%o0 + 0x50]
	stx		%g3, [%o0 + 0x58]
	stx		%g4, [%o0 + 0x60]
	stx		%g5, [%o0 + 0x68]
	stx		%g6, [%o0 + 0x70]
	stx		%g7, [%o0 + 0x78]
	wrpr		%o1, PSTATE_MG, %pstate
	stx		%g0, [%o0 + 0x80]
	stx		%g1, [%o0 + 0x88]
	stx		%g2, [%o0 + 0x90]
	stx		%g3, [%o0 + 0x98]
	stx		%g4, [%o0 + 0xa0]
	stx		%g5, [%o0 + 0xa8]
	stx		%g6, [%o0 + 0xb0]
	stx		%g7, [%o0 + 0xb8]
	wrpr		%o5, 0x0, %pstate
	retl
	 nop

	.globl		restore_alternate_globals
restore_alternate_globals: /* %o0 = save_area */
	rdpr		%pstate, %o5
	andn		%o5, PSTATE_IE, %o1
	wrpr		%o1, PSTATE_AG, %pstate
	ldx		[%o0 + 0x00], %g0
	ldx		[%o0 + 0x08], %g1
	ldx		[%o0 + 0x10], %g2
	ldx		[%o0 + 0x18], %g3
	ldx		[%o0 + 0x20], %g4
	ldx		[%o0 + 0x28], %g5
	ldx		[%o0 + 0x30], %g6
	ldx		[%o0 + 0x38], %g7
	wrpr		%o1, PSTATE_IG, %pstate
	ldx		[%o0 + 0x40], %g0
	ldx		[%o0 + 0x48], %g1
	ldx		[%o0 + 0x50], %g2
	ldx		[%o0 + 0x58], %g3
	ldx		[%o0 + 0x60], %g4
	ldx		[%o0 + 0x68], %g5
	ldx		[%o0 + 0x70], %g6
	ldx		[%o0 + 0x78], %g7
	wrpr		%o1, PSTATE_MG, %pstate
	ldx		[%o0 + 0x80], %g0
	ldx		[%o0 + 0x88], %g1
	ldx		[%o0 + 0x90], %g2
	ldx		[%o0 + 0x98], %g3
	ldx		[%o0 + 0xa0], %g4
	ldx		[%o0 + 0xa8], %g5
	ldx		[%o0 + 0xb0], %g6
	ldx		[%o0 + 0xb8], %g7
	wrpr		%o5, 0x0, %pstate
	retl
	 nop

	.globl		getcc, setcc
getcc:
	ldx		[%o0 + PT_V9_TSTATE], %o1
	srlx		%o1, 32, %o1
	and		%o1, 0xf, %o1
	retl
	 stx		%o1, [%o0 + PT_V9_G1]
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

	.globl		utrap, utrap_ill
utrap:	brz,pn		%g1, etrap
	 nop
	save		%sp, -128, %sp
	rdpr		%tstate, %l6
	rdpr		%cwp, %l7
	andn		%l6, TSTATE_CWP, %l6
	wrpr		%l6, %l7, %tstate
	rdpr		%tpc, %l6
	rdpr		%tnpc, %l7
	wrpr		%g1, 0, %tnpc
	done
utrap_ill:
        call		bad_trap
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	/* XXX Here is stuff we still need to write... -DaveM XXX */
	.globl		netbsd_syscall
netbsd_syscall:
	retl
	 nop

	/* We need to carefully read the error status, ACK
	 * the errors, prevent recursive traps, and pass the
	 * information on to C code for logging.
	 *
	 * We pass the AFAR in as-is, and we encode the status
	 * information as described in asm-sparc64/sfafsr.h
	 */
	.globl		__spitfire_access_error
__spitfire_access_error:
	/* Disable ESTATE error reporting so that we do not
	 * take recursive traps and RED state the processor.
	 */
	stxa		%g0, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	mov		UDBE_UE, %g1
	ldxa		[%g0] ASI_AFSR, %g4	! Get AFSR

	/* __spitfire_cee_trap branches here with AFSR in %g4 and
	 * UDBE_CE in %g1.  It only clears ESTATE_ERR_CE in the
	 * ESTATE Error Enable register.
	 */
__spitfire_cee_trap_continue:
	ldxa		[%g0] ASI_AFAR, %g5	! Get AFAR

	rdpr		%tt, %g3
	and		%g3, 0x1ff, %g3		! Paranoia
	sllx		%g3, SFSTAT_TRAP_TYPE_SHIFT, %g3
	or		%g4, %g3, %g4
	rdpr		%tl, %g3
	cmp		%g3, 1
	mov		1, %g3
	bleu		%xcc, 1f
	 sllx		%g3, SFSTAT_TL_GT_ONE_SHIFT, %g3

	or		%g4, %g3, %g4

	/* Read in the UDB error register state, clearing the
	 * sticky error bits as-needed.  We only clear them if
	 * the UE bit is set.  Likewise, __spitfire_cee_trap
	 * below will only do so if the CE bit is set.
	 *
	 * NOTE: UltraSparc-I/II have high and low UDB error
	 *       registers, corresponding to the two UDB units
	 *       present on those chips.  UltraSparc-IIi only
	 *       has a single UDB, called "SDB" in the manual.
	 *       For IIi the upper UDB register always reads
	 *       as zero so for our purposes things will just
	 *       work with the checks below.
	 */
1:	ldxa		[%g0] ASI_UDBH_ERROR_R, %g3
	and		%g3, 0x3ff, %g7		! Paranoia
	sllx		%g7, SFSTAT_UDBH_SHIFT, %g7
	or		%g4, %g7, %g4
	andcc		%g3, %g1, %g3		! UDBE_UE or UDBE_CE
	be,pn		%xcc, 1f
	 nop
	stxa		%g3, [%g0] ASI_UDB_ERROR_W
	membar		#Sync

1:	mov		0x18, %g3
	ldxa		[%g3] ASI_UDBL_ERROR_R, %g3
	and		%g3, 0x3ff, %g7		! Paranoia
	sllx		%g7, SFSTAT_UDBL_SHIFT, %g7
	or		%g4, %g7, %g4
	andcc		%g3, %g1, %g3		! UDBE_UE or UDBE_CE
	be,pn		%xcc, 1f
	 nop
	mov		0x18, %g7
	stxa		%g3, [%g7] ASI_UDB_ERROR_W
	membar		#Sync

1:	/* Ok, now that we've latched the error state,
	 * clear the sticky bits in the AFSR.
	 */
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	rdpr		%tl, %g2
	cmp		%g2, 1
	rdpr		%pil, %g2
	bleu,pt		%xcc, 1f
	 wrpr		%g0, 15, %pil

	ba,pt		%xcc, etraptl1
	 rd		%pc, %g7

	ba,pt		%xcc, 2f
	 nop

1:	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7

2:	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_access_error
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	/* This is the trap handler entry point for ECC correctable
	 * errors.  They are corrected, but we listen for the trap
	 * so that the event can be logged.
	 *
	 * Disrupting errors are either:
	 * 1) single-bit ECC errors during UDB reads to system
	 *    memory
	 * 2) data parity errors during write-back events
	 *
	 * As far as I can make out from the manual, the CEE trap
	 * is only for correctable errors during memory read
	 * accesses by the front-end of the processor.
	 *
	 * The code below is only for trap level 1 CEE events,
	 * as it is the only situation where we can safely record
	 * and log.  For trap level >1 we just clear the CE bit
	 * in the AFSR and return.
	 *
	 * This is just like __spiftire_access_error above, but it
	 * specifically handles correctable errors.  If an
	 * uncorrectable error is indicated in the AFSR we
	 * will branch directly above to __spitfire_access_error
	 * to handle it instead.  Uncorrectable therefore takes
	 * priority over correctable, and the error logging
	 * C code will notice this case by inspecting the
	 * trap type.
	 */
	.globl		__spitfire_cee_trap
__spitfire_cee_trap:
	ldxa		[%g0] ASI_AFSR, %g4	! Get AFSR
	mov		1, %g3
	sllx		%g3, SFAFSR_UE_SHIFT, %g3
	andcc		%g4, %g3, %g0		! Check for UE
	bne,pn		%xcc, __spitfire_access_error
	 nop

	/* Ok, in this case we only have a correctable error.
	 * Indicate we only wish to capture that state in register
	 * %g1, and we only disable CE error reporting unlike UE
	 * handling which disables all errors.
	 */
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g3
	andn		%g3, ESTATE_ERR_CE, %g3
	stxa		%g3, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Preserve AFSR in %g4, indicate UDB state to capture in %g1 */
	ba,pt		%xcc, __spitfire_cee_trap_continue
	 mov		UDBE_CE, %g1

	.globl		__spitfire_data_access_exception
	.globl		__spitfire_data_access_exception_tl1
__spitfire_data_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	rdpr		%tt, %g3
	cmp		%g3, 0x80		! first win spill/fill trap
	blu,pn		%xcc, 1f
	 cmp		%g3, 0xff		! last win spill/fill trap
	bgu,pn		%xcc, 1f
	 nop
	ba,pt		%xcc, winfix_dax
	 rdpr		%tpc, %g3
1:	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_data_access_exception_tl1
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

__spitfire_data_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g5
	ldxa		[%g3] ASI_DMMU, %g4	! Get SFSR
	ldxa		[%g5] ASI_DMMU, %g5	! Get SFAR
	stxa		%g0, [%g3] ASI_DMMU	! Clear SFSR.FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_data_access_exception
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		__spitfire_insn_access_exception
	.globl		__spitfire_insn_access_exception_tl1
__spitfire_insn_access_exception_tl1:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	ldxa		[%g3] ASI_IMMU, %g4	! Get SFSR
	rdpr		%tpc, %g5		! IMMU has no SFAR, use TPC
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etraptl1
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_insn_access_exception_tl1
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

__spitfire_insn_access_exception:
	rdpr		%pstate, %g4
	wrpr		%g4, PSTATE_MG|PSTATE_AG, %pstate
	mov		TLB_SFSR, %g3
	ldxa		[%g3] ASI_IMMU, %g4	! Get SFSR
	rdpr		%tpc, %g5		! IMMU has no SFAR, use TPC
	stxa		%g0, [%g3] ASI_IMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		spitfire_insn_access_exception
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	/* These get patched into the trap table at boot time
	 * once we know we have a cheetah processor.
	 */
	.globl		cheetah_fecc_trap_vector, cheetah_fecc_trap_vector_tl1
cheetah_fecc_trap_vector:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_DC | DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_fast_ecc), %g2
	jmpl		%g2 + %lo(cheetah_fast_ecc), %g0
	 mov		0, %g1
cheetah_fecc_trap_vector_tl1:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_DC | DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_fast_ecc), %g2
	jmpl		%g2 + %lo(cheetah_fast_ecc), %g0
	 mov		1, %g1
	.globl	cheetah_cee_trap_vector, cheetah_cee_trap_vector_tl1
cheetah_cee_trap_vector:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_cee), %g2
	jmpl		%g2 + %lo(cheetah_cee), %g0
	 mov		0, %g1
cheetah_cee_trap_vector_tl1:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	andn		%g1, DCU_IC, %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	sethi		%hi(cheetah_cee), %g2
	jmpl		%g2 + %lo(cheetah_cee), %g0
	 mov		1, %g1
	.globl	cheetah_deferred_trap_vector, cheetah_deferred_trap_vector_tl1
cheetah_deferred_trap_vector:
	membar		#Sync
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1;
	andn		%g1, DCU_DC | DCU_IC, %g1;
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG;
	membar		#Sync;
	sethi		%hi(cheetah_deferred_trap), %g2
	jmpl		%g2 + %lo(cheetah_deferred_trap), %g0
	 mov		0, %g1
cheetah_deferred_trap_vector_tl1:
	membar		#Sync;
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1;
	andn		%g1, DCU_DC | DCU_IC, %g1;
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG;
	membar		#Sync;
	sethi		%hi(cheetah_deferred_trap), %g2
	jmpl		%g2 + %lo(cheetah_deferred_trap), %g0
	 mov		1, %g1

	/* Cheetah+ specific traps. These are for the new I/D cache parity
	 * error traps.  The first argument to cheetah_plus_parity_handler
	 * is encoded as follows:
	 *
	 * Bit0:	0=dcache,1=icache
	 * Bit1:	0=recoverable,1=unrecoverable
	 */
	.globl		cheetah_plus_dcpe_trap_vector, cheetah_plus_dcpe_trap_vector_tl1
cheetah_plus_dcpe_trap_vector:
	membar		#Sync
	sethi		%hi(do_cheetah_plus_data_parity), %g7
	jmpl		%g7 + %lo(do_cheetah_plus_data_parity), %g0
	 nop
	nop
	nop
	nop
	nop

do_cheetah_plus_data_parity:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
	mov		0x0, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,a,pt		%xcc, rtrap_irq

cheetah_plus_dcpe_trap_vector_tl1:
	membar		#Sync
	wrpr		PSTATE_IG | PSTATE_PEF | PSTATE_PRIV, %pstate
	sethi		%hi(do_dcpe_tl1), %g3
	jmpl		%g3 + %lo(do_dcpe_tl1), %g0
	 nop
	nop
	nop
	nop

	.globl		cheetah_plus_icpe_trap_vector, cheetah_plus_icpe_trap_vector_tl1
cheetah_plus_icpe_trap_vector:
	membar		#Sync
	sethi		%hi(do_cheetah_plus_insn_parity), %g7
	jmpl		%g7 + %lo(do_cheetah_plus_insn_parity), %g0
	 nop
	nop
	nop
	nop
	nop

do_cheetah_plus_insn_parity:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
	mov		0x1, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,a,pt		%xcc, rtrap_irq

cheetah_plus_icpe_trap_vector_tl1:
	membar		#Sync
	wrpr		PSTATE_IG | PSTATE_PEF | PSTATE_PRIV, %pstate
	sethi		%hi(do_icpe_tl1), %g3
	jmpl		%g3 + %lo(do_icpe_tl1), %g0
	 nop
	nop
	nop
	nop

	/* If we take one of these traps when tl >= 1, then we
	 * jump to interrupt globals.  If some trap level above us
	 * was also using interrupt globals, we cannot recover.
	 * We may use all interrupt global registers except %g6.
	 */
	.globl		do_dcpe_tl1, do_icpe_tl1
do_dcpe_tl1:
	rdpr		%tl, %g1		! Save original trap level
	mov		1, %g2			! Setup TSTATE checking loop
	sethi		%hi(TSTATE_IG), %g3	! TSTATE mask bit
1:	wrpr		%g2, %tl		! Set trap level to check
	rdpr		%tstate, %g4		! Read TSTATE for this level
	andcc		%g4, %g3, %g0		! Interrupt globals in use?
	bne,a,pn	%xcc, do_dcpe_tl1_fatal	! Yep, irrecoverable
	 wrpr		%g1, %tl		! Restore original trap level
	add		%g2, 1, %g2		! Next trap level
	cmp		%g2, %g1		! Hit them all yet?
	ble,pt		%icc, 1b		! Not yet
	 nop
	wrpr		%g1, %tl		! Restore original trap level
do_dcpe_tl1_nonfatal:	/* Ok we may use interrupt globals safely. */
	sethi		%hi(dcache_parity_tl1_occurred), %g2
	lduw		[%g2 + %lo(dcache_parity_tl1_occurred)], %g1
	add		%g1, 1, %g1
	stw		%g1, [%g2 + %lo(dcache_parity_tl1_occurred)]
	/* Reset D-cache parity */
	sethi		%hi(1 << 16), %g1	! D-cache size
	mov		(1 << 5), %g2		! D-cache line size
	sub		%g1, %g2, %g1		! Move down 1 cacheline
1:	srl		%g1, 14, %g3		! Compute UTAG
	membar		#Sync
	stxa		%g3, [%g1] ASI_DCACHE_UTAG
	membar		#Sync
	sub		%g2, 8, %g3		! 64-bit data word within line
2:	membar		#Sync
	stxa		%g0, [%g1 + %g3] ASI_DCACHE_DATA
	membar		#Sync
	subcc		%g3, 8, %g3		! Next 64-bit data word
	bge,pt		%icc, 2b
	 nop
	subcc		%g1, %g2, %g1		! Next cacheline
	bge,pt		%icc, 1b
	 nop
	ba,pt		%xcc, dcpe_icpe_tl1_common
	 nop

do_dcpe_tl1_fatal:
	sethi		%hi(1f), %g7
	ba,pt		%xcc, etraptl1
1:	or		%g7, %lo(1b), %g7
	mov		0x2, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,pt		%xcc, rtrap
	 clr		%l6

do_icpe_tl1:
	rdpr		%tl, %g1		! Save original trap level
	mov		1, %g2			! Setup TSTATE checking loop
	sethi		%hi(TSTATE_IG), %g3	! TSTATE mask bit
1:	wrpr		%g2, %tl		! Set trap level to check
	rdpr		%tstate, %g4		! Read TSTATE for this level
	andcc		%g4, %g3, %g0		! Interrupt globals in use?
	bne,a,pn	%xcc, do_icpe_tl1_fatal	! Yep, irrecoverable
	 wrpr		%g1, %tl		! Restore original trap level
	add		%g2, 1, %g2		! Next trap level
	cmp		%g2, %g1		! Hit them all yet?
	ble,pt		%icc, 1b		! Not yet
	 nop
	wrpr		%g1, %tl		! Restore original trap level
do_icpe_tl1_nonfatal:	/* Ok we may use interrupt globals safely. */
	sethi		%hi(icache_parity_tl1_occurred), %g2
	lduw		[%g2 + %lo(icache_parity_tl1_occurred)], %g1
	add		%g1, 1, %g1
	stw		%g1, [%g2 + %lo(icache_parity_tl1_occurred)]
	/* Flush I-cache */
	sethi		%hi(1 << 15), %g1	! I-cache size
	mov		(1 << 5), %g2		! I-cache line size
	sub		%g1, %g2, %g1
1:	or		%g1, (2 << 3), %g3
	stxa		%g0, [%g3] ASI_IC_TAG
	membar		#Sync
	subcc		%g1, %g2, %g1
	bge,pt		%icc, 1b
	 nop
	ba,pt		%xcc, dcpe_icpe_tl1_common
	 nop

do_icpe_tl1_fatal:
	sethi		%hi(1f), %g7
	ba,pt		%xcc, etraptl1
1:	or		%g7, %lo(1b), %g7
	mov		0x3, %o0
	call		cheetah_plus_parity_error
	 add		%sp, PTREGS_OFF, %o1
	ba,pt		%xcc, rtrap
	 clr		%l6
	
dcpe_icpe_tl1_common:
	/* Flush D-cache, re-enable D/I caches in DCU and finally
	 * retry the trapping instruction.
	 */
	sethi		%hi(1 << 16), %g1	! D-cache size
	mov		(1 << 5), %g2		! D-cache line size
	sub		%g1, %g2, %g1
1:	stxa		%g0, [%g1] ASI_DCACHE_TAG
	membar		#Sync
	subcc		%g1, %g2, %g1
	bge,pt		%icc, 1b
	 nop
	ldxa		[%g0] ASI_DCU_CONTROL_REG, %g1
	or		%g1, (DCU_DC | DCU_IC), %g1
	stxa		%g1, [%g0] ASI_DCU_CONTROL_REG
	membar		#Sync
	retry

	/* Capture I/D/E-cache state into per-cpu error scoreboard.
	 *
	 * %g1:		(TL>=0) ? 1 : 0
	 * %g2:		scratch
	 * %g3:		scratch
	 * %g4:		AFSR
	 * %g5:		AFAR
	 * %g6:		current thread ptr
	 * %g7:		scratch
	 */
__cheetah_log_error:
	/* Put "TL1" software bit into AFSR. */
	and		%g1, 0x1, %g1
	sllx		%g1, 63, %g2
	or		%g4, %g2, %g4

	/* Get log entry pointer for this cpu at this trap level. */
	BRANCH_IF_JALAPENO(g2,g3,50f)
	ldxa		[%g0] ASI_SAFARI_CONFIG, %g2
	srlx		%g2, 17, %g2
	ba,pt		%xcc, 60f
	 and		%g2, 0x3ff, %g2

50:	ldxa		[%g0] ASI_JBUS_CONFIG, %g2
	srlx		%g2, 17, %g2
	and		%g2, 0x1f, %g2

60:	sllx		%g2, 9, %g2
	sethi		%hi(cheetah_error_log), %g3
	ldx		[%g3 + %lo(cheetah_error_log)], %g3
	brz,pn		%g3, 80f
	 nop

	add		%g3, %g2, %g3
	sllx		%g1, 8, %g1
	add		%g3, %g1, %g1

	/* %g1 holds pointer to the top of the logging scoreboard */
	ldx		[%g1 + 0x0], %g7
	cmp		%g7, -1
	bne,pn		%xcc, 80f
	 nop

	stx		%g4, [%g1 + 0x0]
	stx		%g5, [%g1 + 0x8]
	add		%g1, 0x10, %g1

	/* %g1 now points to D-cache logging area */
	set		0x3ff8, %g2	/* DC_addr mask		*/
	and		%g5, %g2, %g2	/* DC_addr bits of AFAR	*/
	srlx		%g5, 12, %g3
	or		%g3, 1, %g3	/* PHYS tag + valid	*/

10:	ldxa		[%g2] ASI_DCACHE_TAG, %g7
	cmp		%g3, %g7	/* TAG match?		*/
	bne,pt		%xcc, 13f
	 nop

	/* Yep, what we want, capture state. */
	stx		%g2, [%g1 + 0x20]
	stx		%g7, [%g1 + 0x28]

	/* A membar Sync is required before and after utag access. */
	membar		#Sync
	ldxa		[%g2] ASI_DCACHE_UTAG, %g7
	membar		#Sync
	stx		%g7, [%g1 + 0x30]
	ldxa		[%g2] ASI_DCACHE_SNOOP_TAG, %g7
	stx		%g7, [%g1 + 0x38]
	clr		%g3

12:	ldxa		[%g2 + %g3] ASI_DCACHE_DATA, %g7
	stx		%g7, [%g1]
	add		%g3, (1 << 5), %g3
	cmp		%g3, (4 << 5)
	bl,pt		%xcc, 12b
	 add		%g1, 0x8, %g1

	ba,pt		%xcc, 20f
	 add		%g1, 0x20, %g1

13:	sethi		%hi(1 << 14), %g7
	add		%g2, %g7, %g2
	srlx		%g2, 14, %g7
	cmp		%g7, 4
	bl,pt		%xcc, 10b
	 nop

	add		%g1, 0x40, %g1

	/* %g1 now points to I-cache logging area */
20:	set		0x1fe0, %g2	/* IC_addr mask		*/
	and		%g5, %g2, %g2	/* IC_addr bits of AFAR	*/
	sllx		%g2, 1, %g2	/* IC_addr[13:6]==VA[12:5] */
	srlx		%g5, (13 - 8), %g3 /* Make PTAG */
	andn		%g3, 0xff, %g3	/* Mask off undefined bits */

21:	ldxa		[%g2] ASI_IC_TAG, %g7
	andn		%g7, 0xff, %g7
	cmp		%g3, %g7
	bne,pt		%xcc, 23f
	 nop

	/* Yep, what we want, capture state. */
	stx		%g2, [%g1 + 0x40]
	stx		%g7, [%g1 + 0x48]
	add		%g2, (1 << 3), %g2
	ldxa		[%g2] ASI_IC_TAG, %g7
	add		%g2, (1 << 3), %g2
	stx		%g7, [%g1 + 0x50]
	ldxa		[%g2] ASI_IC_TAG, %g7
	add		%g2, (1 << 3), %g2
	stx		%g7, [%g1 + 0x60]
	ldxa		[%g2] ASI_IC_TAG, %g7
	stx		%g7, [%g1 + 0x68]
	sub		%g2, (3 << 3), %g2
	ldxa		[%g2] ASI_IC_STAG, %g7
	stx		%g7, [%g1 + 0x58]
	clr		%g3
	srlx		%g2, 2, %g2

22:	ldxa		[%g2 + %g3] ASI_IC_INSTR, %g7
	stx		%g7, [%g1]
	add		%g3, (1 << 3), %g3
	cmp		%g3, (8 << 3)
	bl,pt		%xcc, 22b
	 add		%g1, 0x8, %g1

	ba,pt		%xcc, 30f
	 add		%g1, 0x30, %g1

23:	sethi		%hi(1 << 14), %g7
	add		%g2, %g7, %g2
	srlx		%g2, 14, %g7
	cmp		%g7, 4
	bl,pt		%xcc, 21b
	 nop

	add		%g1, 0x70, %g1

	/* %g1 now points to E-cache logging area */
30:	andn		%g5, (32 - 1), %g2
	stx		%g2, [%g1 + 0x20]
	ldxa		[%g2] ASI_EC_TAG_DATA, %g7
	stx		%g7, [%g1 + 0x28]
	ldxa		[%g2] ASI_EC_R, %g0
	clr		%g3

31:	ldxa		[%g3] ASI_EC_DATA, %g7
	stx		%g7, [%g1 + %g3]
	add		%g3, 0x8, %g3
	cmp		%g3, 0x20

	bl,pt		%xcc, 31b
	 nop
80:
	rdpr		%tt, %g2
	cmp		%g2, 0x70
	be		c_fast_ecc
	 cmp		%g2, 0x63
	be		c_cee
	 nop
	ba,pt		%xcc, c_deferred

	/* Cheetah FECC trap handling, we get here from tl{0,1}_fecc
	 * in the trap table.  That code has done a memory barrier
	 * and has disabled both the I-cache and D-cache in the DCU
	 * control register.  The I-cache is disabled so that we may
	 * capture the corrupted cache line, and the D-cache is disabled
	 * because corrupt data may have been placed there and we don't
	 * want to reference it.
	 *
	 * %g1 is one if this trap occurred at %tl >= 1.
	 *
	 * Next, we turn off error reporting so that we don't recurse.
	 */
	.globl		cheetah_fast_ecc
cheetah_fast_ecc:
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g2
	andn		%g2, ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN, %g2
	stxa		%g2, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Fetch and clear AFSR/AFAR */
	ldxa		[%g0] ASI_AFSR, %g4
	ldxa		[%g0] ASI_AFAR, %g5
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	ba,pt		%xcc, __cheetah_log_error
	 nop

c_fast_ecc:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		cheetah_fecc_handler
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_irq

	/* Our caller has disabled I-cache and performed membar Sync. */
	.globl		cheetah_cee
cheetah_cee:
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g2
	andn		%g2, ESTATE_ERROR_CEEN, %g2
	stxa		%g2, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Fetch and clear AFSR/AFAR */
	ldxa		[%g0] ASI_AFSR, %g4
	ldxa		[%g0] ASI_AFAR, %g5
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	ba,pt		%xcc, __cheetah_log_error
	 nop

c_cee:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		cheetah_cee_handler
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_irq

	/* Our caller has disabled I-cache+D-cache and performed membar Sync. */
	.globl		cheetah_deferred_trap
cheetah_deferred_trap:
	ldxa		[%g0] ASI_ESTATE_ERROR_EN, %g2
	andn		%g2, ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN, %g2
	stxa		%g2, [%g0] ASI_ESTATE_ERROR_EN
	membar		#Sync

	/* Fetch and clear AFSR/AFAR */
	ldxa		[%g0] ASI_AFSR, %g4
	ldxa		[%g0] ASI_AFAR, %g5
	stxa		%g4, [%g0] ASI_AFSR
	membar		#Sync

	ba,pt		%xcc, __cheetah_log_error
	 nop

c_deferred:
	rdpr		%pil, %g2
	wrpr		%g0, 15, %pil
	ba,pt		%xcc, etrap_irq
	 rd		%pc, %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		cheetah_deferred_handler
	 add		%sp, PTREGS_OFF, %o0
	ba,a,pt		%xcc, rtrap_irq

	.globl		__do_privact
__do_privact:
	mov		TLB_SFSR, %g3
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	or		%g7, %lo(109b), %g7
	call		do_privact
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_mna
do_mna:
	rdpr		%tl, %g3
	cmp		%g3, 1

	/* Setup %g4/%g5 now as they are used in the
	 * winfixup code.
	 */
	mov		TLB_SFSR, %g3
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ldxa		[%g3] ASI_DMMU, %g5
	stxa		%g0, [%g3] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	bgu,pn		%icc, winfix_mna
	 rdpr		%tpc, %g3

1:	sethi		%hi(109f), %g7
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		mem_address_unaligned
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_lddfmna
do_lddfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_lddfmna
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl		do_stdfmna
do_stdfmna:
	sethi		%hi(109f), %g7
	mov		TLB_SFSR, %g4
	ldxa		[%g4] ASI_DMMU, %g5
	stxa		%g0, [%g4] ASI_DMMU	! Clear FaultValid bit
	membar		#Sync
	mov		DMMU_SFAR, %g4
	ldxa		[%g4] ASI_DMMU, %g4
	ba,pt		%xcc, etrap
109:	 or		%g7, %lo(109b), %g7
	mov		%l4, %o1
	mov		%l5, %o2
	call		handle_stdfmna
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 clr		%l6

	.globl	breakpoint_trap
breakpoint_trap:
	call		sparc_breakpoint
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop

#if defined(CONFIG_SUNOS_EMUL) || defined(CONFIG_SOLARIS_EMUL) || \
    defined(CONFIG_SOLARIS_EMUL_MODULE)
	/* SunOS uses syscall zero as the 'indirect syscall' it looks
	 * like indir_syscall(scall_num, arg0, arg1, arg2...);  etc.
	 * This is complete brain damage.
	 */
	.globl	sunos_indir
sunos_indir:
	srl		%o0, 0, %o0
	mov		%o7, %l4
	cmp		%o0, NR_SYSCALLS
	blu,a,pt	%icc, 1f
	 sll		%o0, 0x2, %o0
	sethi		%hi(sunos_nosys), %l6
	b,pt		%xcc, 2f
	 or		%l6, %lo(sunos_nosys), %l6
1:	sethi		%hi(sunos_sys_table), %l7
	or		%l7, %lo(sunos_sys_table), %l7
	lduw		[%l7 + %o0], %l6
2:	mov		%o1, %o0
	mov		%o2, %o1
	mov		%o3, %o2
	mov		%o4, %o3
	mov		%o5, %o4
	call		%l6
	 mov		%l4, %o7

	.globl	sunos_getpid
sunos_getpid:
	call	sys_getppid
	 nop
	call	sys_getpid
	 stx	%o0, [%sp + PTREGS_OFF + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + PTREGS_OFF + PT_V9_I0]

	/* SunOS getuid() returns uid in %o0 and euid in %o1 */
	.globl	sunos_getuid
sunos_getuid:
	call	sys32_geteuid16
	 nop
	call	sys32_getuid16
	 stx	%o0, [%sp + PTREGS_OFF + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + PTREGS_OFF + PT_V9_I0]

	/* SunOS getgid() returns gid in %o0 and egid in %o1 */
	.globl	sunos_getgid
sunos_getgid:
	call	sys32_getegid16
	 nop
	call	sys32_getgid16
	 stx	%o0, [%sp + PTREGS_OFF + PT_V9_I1]
	b,pt	%xcc, ret_sys_call
	 stx	%o0, [%sp + PTREGS_OFF + PT_V9_I0]
#endif

	/* SunOS's execv() call only specifies the argv argument, the
	 * environment settings are the same as the calling processes.
	 */
	.globl	sunos_execv
sys_execve:
	sethi		%hi(sparc_execve), %g1
	ba,pt		%xcc, execve_merge
	 or		%g1, %lo(sparc_execve), %g1
#ifdef CONFIG_COMPAT
	.globl	sys_execve
sunos_execv:
	stx		%g0, [%sp + PTREGS_OFF + PT_V9_I2]
	.globl	sys32_execve
sys32_execve:
	sethi		%hi(sparc32_execve), %g1
	or		%g1, %lo(sparc32_execve), %g1
#endif
execve_merge:
	flushw
	jmpl		%g1, %g0
	 add		%sp, PTREGS_OFF, %o0

	.globl	sys_pipe, sys_sigpause, sys_nis_syscall
	.globl	sys_rt_sigreturn
	.globl	sys_ptrace
	.globl	sys_sigaltstack
	.align	32
sys_pipe:	ba,pt		%xcc, sparc_pipe
		 add		%sp, PTREGS_OFF, %o0
sys_nis_syscall:ba,pt		%xcc, c_sys_nis_syscall
		 add		%sp, PTREGS_OFF, %o0
sys_memory_ordering:
		ba,pt		%xcc, sparc_memory_ordering
		 add		%sp, PTREGS_OFF, %o1
sys_sigaltstack:ba,pt		%xcc, do_sigaltstack
		 add		%i6, STACK_BIAS, %o2
#ifdef CONFIG_COMPAT
	.globl	sys32_sigstack
sys32_sigstack:	ba,pt		%xcc, do_sys32_sigstack
		 mov		%i6, %o2
	.globl	sys32_sigaltstack
sys32_sigaltstack:
		ba,pt		%xcc, do_sys32_sigaltstack
		 mov		%i6, %o2
#endif
		.align		32
#ifdef CONFIG_COMPAT
	.globl	sys32_sigreturn
sys32_sigreturn:
		add		%sp, PTREGS_OFF, %o0
		call		do_sigreturn32
		 add		%o7, 1f-.-4, %o7
		nop
#endif
sys_rt_sigreturn:
		add		%sp, PTREGS_OFF, %o0
		call		do_rt_sigreturn
		 add		%o7, 1f-.-4, %o7
		nop
#ifdef CONFIG_COMPAT
	.globl	sys32_rt_sigreturn
sys32_rt_sigreturn:
		add		%sp, PTREGS_OFF, %o0
		call		do_rt_sigreturn32
		 add		%o7, 1f-.-4, %o7
		nop
#endif
sys_ptrace:	add		%sp, PTREGS_OFF, %o0
		call		do_ptrace
		 add		%o7, 1f-.-4, %o7
		nop
		.align		32
1:		ldx		[%curptr + TI_FLAGS], %l5
		andcc		%l5, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %g0
		be,pt		%icc, rtrap
		 clr		%l6
		add		%sp, PTREGS_OFF, %o0
		call		syscall_trace
		 mov		1, %o1

		ba,pt		%xcc, rtrap
		 clr		%l6

	/* This is how fork() was meant to be done, 8 instruction entry.
	 *
	 * I questioned the following code briefly, let me clear things
	 * up so you must not reason on it like I did.
	 *
	 * Know the fork_kpsr etc. we use in the sparc32 port?  We don't
	 * need it here because the only piece of window state we copy to
	 * the child is the CWP register.  Even if the parent sleeps,
	 * we are safe because we stuck it into pt_regs of the parent
	 * so it will not change.
	 *
	 * XXX This raises the question, whether we can do the same on
	 * XXX sparc32 to get rid of fork_kpsr _and_ fork_kwim.  The
	 * XXX answer is yes.  We stick fork_kpsr in UREG_G0 and
	 * XXX fork_kwim in UREG_G1 (global registers are considered
	 * XXX volatile across a system call in the sparc ABI I think
	 * XXX if it isn't we can use regs->y instead, anyone who depends
	 * XXX upon the Y register being preserved across a fork deserves
	 * XXX to lose).
	 *
	 * In fact we should take advantage of that fact for other things
	 * during system calls...
	 */
	.globl	sys_fork, sys_vfork, sys_clone, sparc_exit
	.globl	ret_from_syscall
	.align	32
sys_vfork:	/* Under Linux, vfork and fork are just special cases of clone. */
		sethi		%hi(0x4000 | 0x0100 | SIGCHLD), %o0
		or		%o0, %lo(0x4000 | 0x0100 | SIGCHLD), %o0
		ba,pt		%xcc, sys_clone
sys_fork:	 clr		%o1
		mov		SIGCHLD, %o0
sys_clone:	flushw
		movrz		%o1, %fp, %o1
		mov		0, %o3
		ba,pt		%xcc, sparc_do_fork
		 add		%sp, PTREGS_OFF, %o2
ret_from_syscall:
		/* Clear current_thread_info()->new_child, and
		 * check performance counter stuff too.
		 */
		stb		%g0, [%g6 + TI_NEW_CHILD]
		ldx		[%g6 + TI_FLAGS], %l0
		call		schedule_tail
		 mov		%g7, %o0
		andcc		%l0, _TIF_PERFCTR, %g0
		be,pt		%icc, 1f
		 nop
		ldx		[%g6 + TI_PCR], %o7
		wr		%g0, %o7, %pcr

		/* Blackbird errata workaround.  See commentary in
		 * smp.c:smp_percpu_timer_interrupt() for more
		 * information.
		 */
		ba,pt		%xcc, 99f
		 nop
		.align		64
99:		wr		%g0, %g0, %pic
		rd		%pic, %g0

1:		b,pt		%xcc, ret_sys_call
		 ldx		[%sp + PTREGS_OFF + PT_V9_I0], %o0
sparc_exit:	wrpr		%g0, (PSTATE_RMO | PSTATE_PEF | PSTATE_PRIV), %pstate
		rdpr		%otherwin, %g1
		rdpr		%cansave, %g3
		add		%g3, %g1, %g3
		wrpr		%g3, 0x0, %cansave
		wrpr		%g0, 0x0, %otherwin
		wrpr		%g0, (PSTATE_RMO | PSTATE_PEF | PSTATE_PRIV | PSTATE_IE), %pstate
		ba,pt		%xcc, sys_exit
		 stb		%g0, [%g6 + TI_WSAVED]

linux_sparc_ni_syscall:
	sethi		%hi(sys_ni_syscall), %l7
	b,pt		%xcc, 4f
	 or		%l7, %lo(sys_ni_syscall), %l7

linux_syscall_trace32:
	add		%sp, PTREGS_OFF, %o0
	call		syscall_trace
	 clr		%o1
	srl		%i0, 0, %o0
	srl		%i4, 0, %o4
	srl		%i1, 0, %o1
	srl		%i2, 0, %o2
	b,pt		%xcc, 2f
	 srl		%i3, 0, %o3

linux_syscall_trace:
	add		%sp, PTREGS_OFF, %o0
	call		syscall_trace
	 clr		%o1
	mov		%i0, %o0
	mov		%i1, %o1
	mov		%i2, %o2
	mov		%i3, %o3
	b,pt		%xcc, 2f
	 mov		%i4, %o4


	/* Linux 32-bit and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall32
linux_sparc_syscall32:
	/* Direct access to user regs, much faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 srl		%i0, 0, %o0				! IEU0
	sll		%g1, 2, %l4				! IEU0	Group
	srl		%i4, 0, %o4				! IEU1
	lduw		[%l7 + %l4], %l7			! Load
	srl		%i1, 0, %o1				! IEU0	Group
	ldx		[%curptr + TI_FLAGS], %l0		! Load

	srl		%i5, 0, %o5				! IEU1
	srl		%i2, 0, %o2				! IEU0	Group
	andcc		%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %g0
	bne,pn		%icc, linux_syscall_trace32		! CTI
	 mov		%i0, %l5				! IEU1
	call		%l7					! CTI	Group brk forced
	 srl		%i3, 0, %o3				! IEU0
	ba,a,pt		%xcc, 3f

	/* Linux native and SunOS system calls enter here... */
	.align	32
	.globl	linux_sparc_syscall, ret_sys_call
linux_sparc_syscall:
	/* Direct access to user regs, much faster. */
	cmp		%g1, NR_SYSCALLS			! IEU1	Group
	bgeu,pn		%xcc, linux_sparc_ni_syscall		! CTI
	 mov		%i0, %o0				! IEU0
	sll		%g1, 2, %l4				! IEU0	Group
	mov		%i1, %o1				! IEU1
	lduw		[%l7 + %l4], %l7			! Load
4:	mov		%i2, %o2				! IEU0	Group
	ldx		[%curptr + TI_FLAGS], %l0		! Load

	mov		%i3, %o3				! IEU1
	mov		%i4, %o4				! IEU0	Group
	andcc		%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %g0
	bne,pn		%icc, linux_syscall_trace		! CTI	Group
	 mov		%i0, %l5				! IEU0
2:	call		%l7					! CTI	Group brk forced
	 mov		%i5, %o5				! IEU0
	nop

3:	stx		%o0, [%sp + PTREGS_OFF + PT_V9_I0]
ret_sys_call:
	ldx		[%sp + PTREGS_OFF + PT_V9_TSTATE], %g3
	ldx		[%sp + PTREGS_OFF + PT_V9_TNPC], %l1 ! pc = npc
	sra		%o0, 0, %o0
	mov		%ulo(TSTATE_XCARRY | TSTATE_ICARRY), %g2
	sllx		%g2, 32, %g2

	/* Check if force_successful_syscall_return()
	 * was invoked.
	 */
	ldub            [%curptr + TI_SYS_NOERROR], %l2
	brnz,a,pn       %l2, 80f
	 stb		%g0, [%curptr + TI_SYS_NOERROR]

	cmp		%o0, -ERESTART_RESTARTBLOCK
	bgeu,pn		%xcc, 1f
	 andcc		%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %l6
80:
	/* System call success, clear Carry condition code. */
	andn		%g3, %g2, %g3
	stx		%g3, [%sp + PTREGS_OFF + PT_V9_TSTATE]	
	bne,pn		%icc, linux_syscall_trace2
	 add		%l1, 0x4, %l2			! npc = npc+4
	stx		%l1, [%sp + PTREGS_OFF + PT_V9_TPC]
	ba,pt		%xcc, rtrap_clr_l6
	 stx		%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]

1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	andcc		%l0, (_TIF_SYSCALL_TRACE|_TIF_SECCOMP|_TIF_SYSCALL_AUDIT), %l6	
	sub		%g0, %o0, %o0
	or		%g3, %g2, %g3
	stx		%o0, [%sp + PTREGS_OFF + PT_V9_I0]
	mov		1, %l6
	stx		%g3, [%sp + PTREGS_OFF + PT_V9_TSTATE]
	bne,pn		%icc, linux_syscall_trace2
	 add		%l1, 0x4, %l2			! npc = npc+4
	stx		%l1, [%sp + PTREGS_OFF + PT_V9_TPC]

	b,pt		%xcc, rtrap
	 stx		%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]
linux_syscall_trace2:
	add		%sp, PTREGS_OFF, %o0
	call		syscall_trace
	 mov		1, %o1
	stx		%l1, [%sp + PTREGS_OFF + PT_V9_TPC]
	ba,pt		%xcc, rtrap
	 stx		%l2, [%sp + PTREGS_OFF + PT_V9_TNPC]

	.align		32
	.globl		__flushw_user
__flushw_user:
	rdpr		%otherwin, %g1
	brz,pn		%g1, 2f
	 clr		%g2
1:	save		%sp, -128, %sp
	rdpr		%otherwin, %g1
	brnz,pt		%g1, 1b
	 add		%g2, 1, %g2
1:	sub		%g2, 1, %g2
	brnz,pt		%g2, 1b
	 restore	%g0, %g0, %g0
2:	retl
	 nop
