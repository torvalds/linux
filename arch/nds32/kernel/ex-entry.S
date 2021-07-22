// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <asm/memory.h>
#include <asm/nds32.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/fpu.h>

#ifdef CONFIG_HWZOL
	.macro push_zol
	mfusr	$r14, $LB
	mfusr	$r15, $LE
	mfusr	$r16, $LC
	.endm
#endif
	.macro  skip_save_fucop_ctl
#if defined(CONFIG_FPU)
skip_fucop_ctl:
	smw.adm $p0, [$sp], $p0, #0x1
	j fucop_ctl_done
#endif
	.endm

	.macro	save_user_regs
#if defined(CONFIG_FPU)
	sethi   $p0, hi20(has_fpu)
	lbsi 	$p0, [$p0+lo12(has_fpu)]
	beqz	$p0, skip_fucop_ctl
	mfsr    $p0, $FUCOP_CTL
	smw.adm $p0, [$sp], $p0, #0x1
	bclr    $p0, $p0, #FUCOP_CTL_offCP0EN
	mtsr    $p0, $FUCOP_CTL
fucop_ctl_done:
	/* move $SP to the bottom of pt_regs */
	addi    $sp, $sp, -FUCOP_CTL_OFFSET
#else
	smw.adm $sp, [$sp], $sp, #0x1
	/* move $SP to the bottom of pt_regs */
	addi    $sp, $sp, -OSP_OFFSET
#endif

	/* push $r0 ~ $r25 */
	smw.bim $r0, [$sp], $r25
	/* push $fp, $gp, $lp */
	smw.bim $sp, [$sp], $sp, #0xe

	mfsr	$r12, $SP_USR
	mfsr	$r13, $IPC
#ifdef CONFIG_HWZOL
	push_zol
#endif
	movi	$r17, -1
	move	$r18, $r0
	mfsr	$r19, $PSW
	mfsr	$r20, $IPSW
	mfsr	$r21, $P_IPSW
	mfsr	$r22, $P_IPC
	mfsr	$r23, $P_P0
	mfsr	$r24, $P_P1
	smw.bim $r12, [$sp], $r24, #0
	addi	$sp, $sp, -FUCOP_CTL_OFFSET

	/* Initialize kernel space $fp */
	andi    $p0, $r20, #PSW_mskPOM
	movi    $p1, #0x0
	cmovz   $fp, $p1, $p0

	andi	$r16, $r19, #PSW_mskINTL
	slti	$r17, $r16, #4
	bnez	$r17, 1f
	addi	$r17, $r19, #-2
	mtsr	$r17, $PSW
	isb
1:
	/* If it was superuser mode, we don't need to update $r25 */
	bnez	$p0, 2f
	la	$p0, __entry_task
	lw	$r25, [$p0]
2:
	.endm

	.text

/*
 * Exception Vector
 */
exception_handlers:
	.long	unhandled_exceptions	!Reset/NMI
	.long	unhandled_exceptions	!TLB fill
	.long	do_page_fault		!PTE not present
	.long	do_dispatch_tlb_misc	!TLB misc
	.long	unhandled_exceptions	!TLB VLPT
	.long	unhandled_exceptions	!Machine Error
	.long	do_debug_trap		!Debug related
	.long	do_dispatch_general	!General exception
	.long	eh_syscall		!Syscall
	.long	asm_do_IRQ		!IRQ

	skip_save_fucop_ctl
common_exception_handler:
	save_user_regs
	mfsr	$p0, $ITYPE
	andi	$p0, $p0, #ITYPE_mskVECTOR
	srli	$p0, $p0, #ITYPE_offVECTOR
	andi	$p1, $p0, #NDS32_VECTOR_mskNONEXCEPTION
	bnez	$p1, 1f
	sethi	$lp, hi20(ret_from_exception)
	ori	$lp, $lp, lo12(ret_from_exception)
	sethi	$p1, hi20(exception_handlers)
	ori	$p1, $p1, lo12(exception_handlers)
	lw	$p1, [$p1+$p0<<2]
	move	$r0, $p0
	mfsr	$r1, $EVA
	mfsr	$r2, $ITYPE
	move	$r3, $sp
	mfsr    $r4, $OIPC
	/* enable gie if it is enabled in IPSW. */
	mfsr	$r21, $PSW
	andi	$r20, $r20, #PSW_mskGIE	/* r20 is $IPSW*/
	or	$r21, $r21, $r20
	mtsr	$r21, $PSW
	dsb
	jr	$p1
	/* syscall */
1:
	addi	$p1, $p0, #-NDS32_VECTOR_offEXCEPTION
	bnez	$p1, 2f
	sethi	$lp, hi20(ret_from_exception)
	ori	$lp, $lp, lo12(ret_from_exception)
	sethi	$p1, hi20(exception_handlers)
	ori	$p1, $p1, lo12(exception_handlers)
	lwi	$p1, [$p1+#NDS32_VECTOR_offEXCEPTION<<2]
	jr	$p1

	/* interrupt */
2:
#ifdef CONFIG_TRACE_IRQFLAGS
	jal     __trace_hardirqs_off
#endif
	move	$r0, $sp
	sethi	$lp, hi20(ret_from_intr)
	ori	$lp, $lp, lo12(ret_from_intr)
	sethi	$p0, hi20(exception_handlers)
	ori	$p0, $p0, lo12(exception_handlers)
	lwi	$p0, [$p0+#NDS32_VECTOR_offINTERRUPT<<2]
	jr	$p0

	.macro	EXCEPTION_VECTOR_DEBUG
	.align 4
	mfsr     $p0, $EDM_CTL
	andi     $p0, $p0, EDM_CTL_mskV3_EDM_MODE
	tnez     $p0, SWID_RAISE_INTERRUPT_LEVEL
	.endm

	.macro	EXCEPTION_VECTOR
	.align 4
	sethi	 $p0, hi20(common_exception_handler)
	ori	 $p0, $p0, lo12(common_exception_handler)
	jral.ton $p0, $p0
	.endm

	.section	".text.init", #alloc, #execinstr
	.global	exception_vector
exception_vector:
.rept 6
	EXCEPTION_VECTOR
.endr
	EXCEPTION_VECTOR_DEBUG
.rept 121
	EXCEPTION_VECTOR
.endr
	.align 4
	.global	exception_vector_end
exception_vector_end:
