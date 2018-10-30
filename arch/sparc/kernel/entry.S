/* SPDX-License-Identifier: GPL-2.0 */
/* arch/sparc/kernel/entry.S:  Sparc trap low-level entry points.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 * Copyright (C) 1996-1999 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Anton Blanchard (anton@progsoc.uts.edu.au)
 */

#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/head.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/contregs.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/winmacro.h>
#include <asm/signal.h>
#include <asm/obio.h>
#include <asm/mxcc.h>
#include <asm/thread_info.h>
#include <asm/param.h>
#include <asm/unistd.h>

#include <asm/asmmacro.h>
#include <asm/export.h>

#define curptr      g6

/* These are just handy. */
#define _SV	save	%sp, -STACKFRAME_SZ, %sp
#define _RS     restore 

#define FLUSH_ALL_KERNEL_WINDOWS \
	_SV; _SV; _SV; _SV; _SV; _SV; _SV; \
	_RS; _RS; _RS; _RS; _RS; _RS; _RS;

	.text

#ifdef CONFIG_KGDB
	.align	4
	.globl		arch_kgdb_breakpoint
	.type		arch_kgdb_breakpoint,#function
arch_kgdb_breakpoint:
	ta		0x7d
	retl
	 nop
	.size		arch_kgdb_breakpoint,.-arch_kgdb_breakpoint
#endif

#if defined(CONFIG_BLK_DEV_FD) || defined(CONFIG_BLK_DEV_FD_MODULE)
	.align	4
	.globl	floppy_hardint
floppy_hardint:
	/*
	 * This code cannot touch registers %l0 %l1 and %l2
	 * because SAVE_ALL depends on their values. It depends
	 * on %l3 also, but we regenerate it before a call.
	 * Other registers are:
	 * %l3 -- base address of fdc registers
	 * %l4 -- pdma_vaddr
	 * %l5 -- scratch for ld/st address
	 * %l6 -- pdma_size
	 * %l7 -- scratch [floppy byte, ld/st address, aux. data]
	 */

	/* Do we have work to do? */
	sethi	%hi(doing_pdma), %l7
	ld	[%l7 + %lo(doing_pdma)], %l7
	cmp	%l7, 0
	be	floppy_dosoftint
	 nop

	/* Load fdc register base */
	sethi	%hi(fdc_status), %l3
	ld	[%l3 + %lo(fdc_status)], %l3

	/* Setup register addresses */
	sethi	%hi(pdma_vaddr), %l5	! transfer buffer
	ld	[%l5 + %lo(pdma_vaddr)], %l4
	sethi	%hi(pdma_size), %l5	! bytes to go
	ld	[%l5 + %lo(pdma_size)], %l6
next_byte:
  	ldub	[%l3], %l7

	andcc	%l7, 0x80, %g0		! Does fifo still have data
	bz	floppy_fifo_emptied	! fifo has been emptied...
	 andcc	%l7, 0x20, %g0		! in non-dma mode still?
	bz	floppy_overrun		! nope, overrun
	 andcc	%l7, 0x40, %g0		! 0=write 1=read
	bz	floppy_write
	 sub	%l6, 0x1, %l6

	/* Ok, actually read this byte */
	ldub	[%l3 + 1], %l7
	orcc	%g0, %l6, %g0
	stb	%l7, [%l4]
	bne	next_byte
	 add	%l4, 0x1, %l4

	b	floppy_tdone
	 nop

floppy_write:
	/* Ok, actually write this byte */
	ldub	[%l4], %l7
	orcc	%g0, %l6, %g0
	stb	%l7, [%l3 + 1]
	bne	next_byte
	 add	%l4, 0x1, %l4

	/* fall through... */
floppy_tdone:
	sethi	%hi(pdma_vaddr), %l5
	st	%l4, [%l5 + %lo(pdma_vaddr)]
	sethi	%hi(pdma_size), %l5
	st	%l6, [%l5 + %lo(pdma_size)]
	/* Flip terminal count pin */
	set	auxio_register, %l7
	ld	[%l7], %l7

	ldub	[%l7], %l5

	or	%l5, 0xc2, %l5
	stb	%l5, [%l7]
	andn    %l5, 0x02, %l5

2:
	/* Kill some time so the bits set */
	WRITE_PAUSE
	WRITE_PAUSE

	stb     %l5, [%l7]

	/* Prevent recursion */
	sethi	%hi(doing_pdma), %l7
	b	floppy_dosoftint
	 st	%g0, [%l7 + %lo(doing_pdma)]

	/* We emptied the FIFO, but we haven't read everything
	 * as of yet.  Store the current transfer address and
	 * bytes left to read so we can continue when the next
	 * fast IRQ comes in.
	 */
floppy_fifo_emptied:
	sethi	%hi(pdma_vaddr), %l5
	st	%l4, [%l5 + %lo(pdma_vaddr)]
	sethi	%hi(pdma_size), %l7
	st	%l6, [%l7 + %lo(pdma_size)]

	/* Restore condition codes */
	wr	%l0, 0x0, %psr
	WRITE_PAUSE

	jmp	%l1
	rett	%l2

floppy_overrun:
	sethi	%hi(pdma_vaddr), %l5
	st	%l4, [%l5 + %lo(pdma_vaddr)]
	sethi	%hi(pdma_size), %l5
	st	%l6, [%l5 + %lo(pdma_size)]
	/* Prevent recursion */
	sethi	%hi(doing_pdma), %l7
	st	%g0, [%l7 + %lo(doing_pdma)]

	/* fall through... */
floppy_dosoftint:
	rd	%wim, %l3
	SAVE_ALL

	/* Set all IRQs off. */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	mov	11, %o0			! floppy irq level (unused anyway)
	mov	%g0, %o1		! devid is not used in fast interrupts
	call	sparc_floppy_irq
	 add	%sp, STACKFRAME_SZ, %o2	! struct pt_regs *regs

	RESTORE_ALL
	
#endif /* (CONFIG_BLK_DEV_FD) */

	/* Bad trap handler */
	.globl	bad_trap_handler
bad_trap_handler:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0	! pt_regs
	call	do_hw_interrupt
	 mov	%l7, %o1		! trap number

	RESTORE_ALL
	
/* For now all IRQ's not registered get sent here. handler_irq() will
 * see if a routine is registered to handle this interrupt and if not
 * it will say so on the console.
 */

	.align	4
	.globl	real_irq_entry, patch_handler_irq
real_irq_entry:
	SAVE_ALL

#ifdef CONFIG_SMP
	.globl	patchme_maybe_smp_msg

	cmp	%l7, 11
patchme_maybe_smp_msg:
	bgu	maybe_smp4m_msg
	 nop
#endif

real_irq_continue:
	or	%l0, PSR_PIL, %g2
	wr	%g2, 0x0, %psr
	WRITE_PAUSE
	wr	%g2, PSR_ET, %psr
	WRITE_PAUSE
	mov	%l7, %o0		! irq level
patch_handler_irq:
	call	handler_irq
	 add	%sp, STACKFRAME_SZ, %o1	! pt_regs ptr
	or	%l0, PSR_PIL, %g2	! restore PIL after handler_irq
	wr	%g2, PSR_ET, %psr	! keep ET up
	WRITE_PAUSE

	RESTORE_ALL

#ifdef CONFIG_SMP
	/* SMP per-cpu ticker interrupts are handled specially. */
smp4m_ticker:
	bne	real_irq_continue+4
	 or	%l0, PSR_PIL, %g2
	wr	%g2, 0x0, %psr
	WRITE_PAUSE
	wr	%g2, PSR_ET, %psr
	WRITE_PAUSE
	call	smp4m_percpu_timer_interrupt
	 add	%sp, STACKFRAME_SZ, %o0
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE
	RESTORE_ALL

#define GET_PROCESSOR4M_ID(reg)	\
	rd	%tbr, %reg;	\
	srl	%reg, 12, %reg;	\
	and	%reg, 3, %reg;

	/* Here is where we check for possible SMP IPI passed to us
	 * on some level other than 15 which is the NMI and only used
	 * for cross calls.  That has a separate entry point below.
	 *
	 * IPIs are sent on Level 12, 13 and 14. See IRQ_IPI_*.
	 */
maybe_smp4m_msg:
	GET_PROCESSOR4M_ID(o3)
	sethi	%hi(sun4m_irq_percpu), %l5
	sll	%o3, 2, %o3
	or	%l5, %lo(sun4m_irq_percpu), %o5
	sethi	%hi(0x70000000), %o2	! Check all soft-IRQs
	ld	[%o5 + %o3], %o1
	ld	[%o1 + 0x00], %o3	! sun4m_irq_percpu[cpu]->pending
	andcc	%o3, %o2, %g0
	be,a	smp4m_ticker
	 cmp	%l7, 14
	/* Soft-IRQ IPI */
	st	%o2, [%o1 + 0x04]	! sun4m_irq_percpu[cpu]->clear=0x70000000
	WRITE_PAUSE
	ld	[%o1 + 0x00], %g0	! sun4m_irq_percpu[cpu]->pending
	WRITE_PAUSE
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE
	srl	%o3, 28, %o2		! shift for simpler checks below
maybe_smp4m_msg_check_single:
	andcc	%o2, 0x1, %g0
	beq,a	maybe_smp4m_msg_check_mask
	 andcc	%o2, 0x2, %g0
	call	smp_call_function_single_interrupt
	 nop
	andcc	%o2, 0x2, %g0
maybe_smp4m_msg_check_mask:
	beq,a	maybe_smp4m_msg_check_resched
	 andcc	%o2, 0x4, %g0
	call	smp_call_function_interrupt
	 nop
	andcc	%o2, 0x4, %g0
maybe_smp4m_msg_check_resched:
	/* rescheduling is done in RESTORE_ALL regardless, but incr stats */
	beq,a	maybe_smp4m_msg_out
	 nop
	call	smp_resched_interrupt
	 nop
maybe_smp4m_msg_out:
	RESTORE_ALL

	.align	4
	.globl	linux_trap_ipi15_sun4m
linux_trap_ipi15_sun4m:
	SAVE_ALL
	sethi	%hi(0x80000000), %o2
	GET_PROCESSOR4M_ID(o0)
	sethi	%hi(sun4m_irq_percpu), %l5
	or	%l5, %lo(sun4m_irq_percpu), %o5
	sll	%o0, 2, %o0
	ld	[%o5 + %o0], %o5
	ld	[%o5 + 0x00], %o3	! sun4m_irq_percpu[cpu]->pending
	andcc	%o3, %o2, %g0
	be	sun4m_nmi_error		! Must be an NMI async memory error
	 st	%o2, [%o5 + 0x04]	! sun4m_irq_percpu[cpu]->clear=0x80000000
	WRITE_PAUSE
	ld	[%o5 + 0x00], %g0	! sun4m_irq_percpu[cpu]->pending
	WRITE_PAUSE
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE
	call	smp4m_cross_call_irq
	 nop
	b	ret_trap_lockless_ipi
	 clr	%l6

	.globl	smp4d_ticker
	/* SMP per-cpu ticker interrupts are handled specially. */
smp4d_ticker:
	SAVE_ALL
	or	%l0, PSR_PIL, %g2
	sethi	%hi(CC_ICLR), %o0
	sethi	%hi(1 << 14), %o1
	or	%o0, %lo(CC_ICLR), %o0
	stha	%o1, [%o0] ASI_M_MXCC	/* Clear PIL 14 in MXCC's ICLR */
	wr	%g2, 0x0, %psr
	WRITE_PAUSE
	wr	%g2, PSR_ET, %psr
	WRITE_PAUSE
	call	smp4d_percpu_timer_interrupt
	 add	%sp, STACKFRAME_SZ, %o0
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE
	RESTORE_ALL

	.align	4
	.globl	linux_trap_ipi15_sun4d
linux_trap_ipi15_sun4d:
	SAVE_ALL
	sethi	%hi(CC_BASE), %o4
	sethi	%hi(MXCC_ERR_ME|MXCC_ERR_PEW|MXCC_ERR_ASE|MXCC_ERR_PEE), %o2
	or	%o4, (CC_EREG - CC_BASE), %o0
	ldda	[%o0] ASI_M_MXCC, %o0
	andcc	%o0, %o2, %g0
	bne	1f
	 sethi	%hi(BB_STAT2), %o2
	lduba	[%o2] ASI_M_CTL, %o2
	andcc	%o2, BB_STAT2_MASK, %g0
	bne	2f
	 or	%o4, (CC_ICLR - CC_BASE), %o0
	sethi	%hi(1 << 15), %o1
	stha	%o1, [%o0] ASI_M_MXCC	/* Clear PIL 15 in MXCC's ICLR */
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE
	call	smp4d_cross_call_irq
	 nop
	b	ret_trap_lockless_ipi
	 clr	%l6

1:	/* MXCC error */
2:	/* BB error */
	/* Disable PIL 15 */
	set	CC_IMSK, %l4
	lduha	[%l4] ASI_M_MXCC, %l5
	sethi	%hi(1 << 15), %l7
	or	%l5, %l7, %l5
	stha	%l5, [%l4] ASI_M_MXCC
	/* FIXME */
1:	b,a	1b

	.globl	smpleon_ipi
	.extern leon_ipi_interrupt
	/* SMP per-cpu IPI interrupts are handled specially. */
smpleon_ipi:
        SAVE_ALL
	or	%l0, PSR_PIL, %g2
	wr	%g2, 0x0, %psr
	WRITE_PAUSE
	wr	%g2, PSR_ET, %psr
	WRITE_PAUSE
	call	leonsmp_ipi_interrupt
	 add	%sp, STACKFRAME_SZ, %o1 ! pt_regs
	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE
	RESTORE_ALL

	.align	4
	.globl	linux_trap_ipi15_leon
linux_trap_ipi15_leon:
	SAVE_ALL
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE
	call	leon_cross_call_irq
	 nop
	b	ret_trap_lockless_ipi
	 clr	%l6

#endif /* CONFIG_SMP */

	/* This routine handles illegal instructions and privileged
	 * instruction attempts from user code.
	 */
	.align	4
	.globl	bad_instruction
bad_instruction:
	sethi	%hi(0xc1f80000), %l4
	ld	[%l1], %l5
	sethi	%hi(0x81d80000), %l7
	and	%l5, %l4, %l5
	cmp	%l5, %l7
	be	1f
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	do_illegal_instruction
	 mov	%l0, %o3

	RESTORE_ALL

1:	/* unimplemented flush - just skip */
	jmpl	%l2, %g0
	 rett	%l2 + 4

	.align	4
	.globl	priv_instruction
priv_instruction:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	do_priv_instruction
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles unaligned data accesses. */
	.align	4
	.globl	mna_handler
mna_handler:
	andcc	%l0, PSR_PS, %g0
	be	mna_fromuser
	 nop

	SAVE_ALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	ld	[%l1], %o1
	call	kernel_unaligned_trap
	 add	%sp, STACKFRAME_SZ, %o0

	RESTORE_ALL

mna_fromuser:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	ld	[%l1], %o1
	call	user_unaligned_trap
	 add	%sp, STACKFRAME_SZ, %o0

	RESTORE_ALL

	/* This routine handles floating point disabled traps. */
	.align	4
	.globl	fpd_trap_handler
fpd_trap_handler:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	do_fpd_trap
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Floating Point Exceptions. */
	.align	4
	.globl	fpe_trap_handler
fpe_trap_handler:
	set	fpsave_magic, %l5
	cmp	%l1, %l5
	be	1f
	 sethi	%hi(fpsave), %l5
	or	%l5, %lo(fpsave), %l5
	cmp	%l1, %l5
	bne	2f
	 sethi	%hi(fpsave_catch2), %l5
	or	%l5, %lo(fpsave_catch2), %l5
	wr	%l0, 0x0, %psr
	WRITE_PAUSE
	jmp	%l5
	 rett	%l5 + 4
1:	
	sethi	%hi(fpsave_catch), %l5
	or	%l5, %lo(fpsave_catch), %l5
	wr	%l0, 0x0, %psr
	WRITE_PAUSE
	jmp	%l5
	 rett	%l5 + 4

2:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	do_fpe_trap
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Tag Overflow Exceptions. */
	.align	4
	.globl	do_tag_overflow
do_tag_overflow:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	handle_tag_overflow
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Watchpoint Exceptions. */
	.align	4
	.globl	do_watchpoint
do_watchpoint:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	handle_watchpoint
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Register Access Exceptions. */
	.align	4
	.globl	do_reg_access
do_reg_access:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	handle_reg_access
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Co-Processor Disabled Exceptions. */
	.align	4
	.globl	do_cp_disabled
do_cp_disabled:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	handle_cp_disabled
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Co-Processor Exceptions. */
	.align	4
	.globl	do_cp_exception
do_cp_exception:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	handle_cp_exception
	 mov	%l0, %o3

	RESTORE_ALL

	/* This routine handles Hardware Divide By Zero Exceptions. */
	.align	4
	.globl	do_hw_divzero
do_hw_divzero:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr		! re-enable traps
	WRITE_PAUSE

	add	%sp, STACKFRAME_SZ, %o0
	mov	%l1, %o1
	mov	%l2, %o2
	call	handle_hw_divzero
	 mov	%l0, %o3

	RESTORE_ALL

	.align	4
	.globl	do_flush_windows
do_flush_windows:
	SAVE_ALL

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	andcc	%l0, PSR_PS, %g0
	bne	dfw_kernel
	 nop

	call	flush_user_windows
	 nop

	/* Advance over the trap instruction. */
	ld	[%sp + STACKFRAME_SZ + PT_NPC], %l1
	add	%l1, 0x4, %l2
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]

	RESTORE_ALL

	.globl	flush_patch_one

	/* We get these for debugging routines using __builtin_return_address() */
dfw_kernel:
flush_patch_one:
	FLUSH_ALL_KERNEL_WINDOWS

	/* Advance over the trap instruction. */
	ld	[%sp + STACKFRAME_SZ + PT_NPC], %l1
	add	%l1, 0x4, %l2
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]

	RESTORE_ALL

	/* The getcc software trap.  The user wants the condition codes from
	 * the %psr in register %g1.
	 */

	.align	4
	.globl	getcc_trap_handler
getcc_trap_handler:
	srl	%l0, 20, %g1	! give user
	and	%g1, 0xf, %g1	! only ICC bits in %psr
	jmp	%l2		! advance over trap instruction
	rett	%l2 + 0x4	! like this...

	/* The setcc software trap.  The user has condition codes in %g1
	 * that it would like placed in the %psr.  Be careful not to flip
	 * any unintentional bits!
	 */

	.align	4
	.globl	setcc_trap_handler
setcc_trap_handler:
	sll	%g1, 0x14, %l4
	set	PSR_ICC, %l5
	andn	%l0, %l5, %l0	! clear ICC bits in %psr
	and	%l4, %l5, %l4	! clear non-ICC bits in user value
	or	%l4, %l0, %l4	! or them in... mix mix mix

	wr	%l4, 0x0, %psr	! set new %psr
	WRITE_PAUSE		! TI scumbags...

	jmp	%l2		! advance over trap instruction
	rett	%l2 + 0x4	! like this...

sun4m_nmi_error:
	/* NMI async memory error handling. */
	sethi	%hi(0x80000000), %l4
	sethi	%hi(sun4m_irq_global), %o5
	ld	[%o5 + %lo(sun4m_irq_global)], %l5
	st	%l4, [%l5 + 0x0c]	! sun4m_irq_global->mask_set=0x80000000
	WRITE_PAUSE
	ld	[%l5 + 0x00], %g0	! sun4m_irq_global->pending
	WRITE_PAUSE
	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE
	call	sun4m_nmi
	 nop
	st	%l4, [%l5 + 0x08]	! sun4m_irq_global->mask_clear=0x80000000
	WRITE_PAUSE
	ld	[%l5 + 0x00], %g0	! sun4m_irq_global->pending
	WRITE_PAUSE
	RESTORE_ALL

#ifndef CONFIG_SMP
	.align	4
	.globl	linux_trap_ipi15_sun4m
linux_trap_ipi15_sun4m:
	SAVE_ALL

	ba	sun4m_nmi_error
	 nop
#endif /* CONFIG_SMP */

	.align	4
	.globl	srmmu_fault
srmmu_fault:
	mov	0x400, %l5
	mov	0x300, %l4

LEON_PI(lda	[%l5] ASI_LEON_MMUREGS, %l6)	! read sfar first
SUN_PI_(lda	[%l5] ASI_M_MMUREGS, %l6)	! read sfar first

LEON_PI(lda	[%l4] ASI_LEON_MMUREGS, %l5)	! read sfsr last
SUN_PI_(lda	[%l4] ASI_M_MMUREGS, %l5)	! read sfsr last

	andn	%l6, 0xfff, %l6
	srl	%l5, 6, %l5			! and encode all info into l7

	and	%l5, 2, %l5
	or	%l5, %l6, %l6

	or	%l6, %l7, %l7			! l7 = [addr,write,txtfault]

	SAVE_ALL

	mov	%l7, %o1
	mov	%l7, %o2
	and	%o1, 1, %o1		! arg2 = text_faultp
	mov	%l7, %o3
	and	%o2, 2, %o2		! arg3 = writep
	andn	%o3, 0xfff, %o3		! arg4 = faulting address

	wr	%l0, PSR_ET, %psr
	WRITE_PAUSE

	call	do_sparc_fault
	 add	%sp, STACKFRAME_SZ, %o0	! arg1 = pt_regs ptr

	RESTORE_ALL

	.align	4
sunos_execv:
	.globl	sunos_execv
	b	sys_execve
	 clr	%i2

	.align	4
	.globl	sys_sigstack
sys_sigstack:
	mov	%o7, %l5
	mov	%fp, %o2
	call	do_sys_sigstack
	 mov	%l5, %o7

	.align	4
	.globl	sys_sigreturn
sys_sigreturn:
	call	do_sigreturn
	 add	%sp, STACKFRAME_SZ, %o0

	ld	[%curptr + TI_FLAGS], %l5
	andcc	%l5, _TIF_SYSCALL_TRACE, %g0
	be	1f
	 nop

	call	syscall_trace
	 mov	1, %o1

1:
	/* We don't want to muck with user registers like a
	 * normal syscall, just return.
	 */
	RESTORE_ALL

	.align	4
	.globl	sys_rt_sigreturn
sys_rt_sigreturn:
	call	do_rt_sigreturn
	 add	%sp, STACKFRAME_SZ, %o0

	ld	[%curptr + TI_FLAGS], %l5
	andcc	%l5, _TIF_SYSCALL_TRACE, %g0
	be	1f
	 nop

	add	%sp, STACKFRAME_SZ, %o0
	call	syscall_trace
	 mov	1, %o1

1:
	/* We are returning to a signal handler. */
	RESTORE_ALL

	/* Now that we have a real sys_clone, sys_fork() is
	 * implemented in terms of it.  Our _real_ implementation
	 * of SunOS vfork() will use sys_vfork().
	 *
	 * XXX These three should be consolidated into mostly shared
	 * XXX code just like on sparc64... -DaveM
	 */
	.align	4
	.globl	sys_fork, flush_patch_two
sys_fork:
	mov	%o7, %l5
flush_patch_two:
	FLUSH_ALL_KERNEL_WINDOWS;
	ld	[%curptr + TI_TASK], %o4
	rd	%psr, %g4
	WRITE_PAUSE
	mov	SIGCHLD, %o0			! arg0:	clone flags
	rd	%wim, %g5
	WRITE_PAUSE
	mov	%fp, %o1			! arg1:	usp
	std	%g4, [%o4 + AOFF_task_thread + AOFF_thread_fork_kpsr]
	add	%sp, STACKFRAME_SZ, %o2		! arg2:	pt_regs ptr
	mov	0, %o3
	call	sparc_do_fork
	 mov	%l5, %o7

	/* Whee, kernel threads! */
	.globl	sys_clone, flush_patch_three
sys_clone:
	mov	%o7, %l5
flush_patch_three:
	FLUSH_ALL_KERNEL_WINDOWS;
	ld	[%curptr + TI_TASK], %o4
	rd	%psr, %g4
	WRITE_PAUSE

	/* arg0,1: flags,usp  -- loaded already */
	cmp	%o1, 0x0			! Is new_usp NULL?
	rd	%wim, %g5
	WRITE_PAUSE
	be,a	1f
	 mov	%fp, %o1			! yes, use callers usp
	andn	%o1, 7, %o1			! no, align to 8 bytes
1:
	std	%g4, [%o4 + AOFF_task_thread + AOFF_thread_fork_kpsr]
	add	%sp, STACKFRAME_SZ, %o2		! arg2:	pt_regs ptr
	mov	0, %o3
	call	sparc_do_fork
	 mov	%l5, %o7

	/* Whee, real vfork! */
	.globl	sys_vfork, flush_patch_four
sys_vfork:
flush_patch_four:
	FLUSH_ALL_KERNEL_WINDOWS;
	ld	[%curptr + TI_TASK], %o4
	rd	%psr, %g4
	WRITE_PAUSE
	rd	%wim, %g5
	WRITE_PAUSE
	std	%g4, [%o4 + AOFF_task_thread + AOFF_thread_fork_kpsr]
	sethi	%hi(0x4000 | 0x0100 | SIGCHLD), %o0
	mov	%fp, %o1
	or	%o0, %lo(0x4000 | 0x0100 | SIGCHLD), %o0
	sethi	%hi(sparc_do_fork), %l1
	mov	0, %o3
	jmpl	%l1 + %lo(sparc_do_fork), %g0
	 add	%sp, STACKFRAME_SZ, %o2

        .align  4
linux_sparc_ni_syscall:
	sethi   %hi(sys_ni_syscall), %l7
	b       do_syscall
	 or     %l7, %lo(sys_ni_syscall), %l7

linux_syscall_trace:
	add	%sp, STACKFRAME_SZ, %o0
	call	syscall_trace
	 mov	0, %o1
	cmp	%o0, 0
	bne	3f
	 mov	-ENOSYS, %o0

	/* Syscall tracing can modify the registers.  */
	ld	[%sp + STACKFRAME_SZ + PT_G1], %g1
	sethi	%hi(sys_call_table), %l7
	ld	[%sp + STACKFRAME_SZ + PT_I0], %i0
	or	%l7, %lo(sys_call_table), %l7
	ld	[%sp + STACKFRAME_SZ + PT_I1], %i1
	ld	[%sp + STACKFRAME_SZ + PT_I2], %i2
	ld	[%sp + STACKFRAME_SZ + PT_I3], %i3
	ld	[%sp + STACKFRAME_SZ + PT_I4], %i4
	ld	[%sp + STACKFRAME_SZ + PT_I5], %i5
	cmp	%g1, NR_syscalls
	bgeu	3f
	 mov	-ENOSYS, %o0

	sll	%g1, 2, %l4
	mov	%i0, %o0
	ld	[%l7 + %l4], %l7
	mov	%i1, %o1
	mov	%i2, %o2
	mov	%i3, %o3
	b	2f
	 mov	%i4, %o4

	.globl	ret_from_fork
ret_from_fork:
	call	schedule_tail
	 ld	[%g3 + TI_TASK], %o0
	b	ret_sys_call
	 ld	[%sp + STACKFRAME_SZ + PT_I0], %o0

	.globl	ret_from_kernel_thread
ret_from_kernel_thread:
	call	schedule_tail
	 ld	[%g3 + TI_TASK], %o0
	ld	[%sp + STACKFRAME_SZ + PT_G1], %l0
	call	%l0
	 ld	[%sp + STACKFRAME_SZ + PT_G2], %o0
	rd	%psr, %l1
	ld	[%sp + STACKFRAME_SZ + PT_PSR], %l0
	andn	%l0, PSR_CWP, %l0
	nop
	and	%l1, PSR_CWP, %l1
	or	%l0, %l1, %l0
	st	%l0, [%sp + STACKFRAME_SZ + PT_PSR]
	b	ret_sys_call
	 mov	0, %o0

	/* Linux native system calls enter here... */
	.align	4
	.globl	linux_sparc_syscall
linux_sparc_syscall:
	sethi	%hi(PSR_SYSCALL), %l4
	or	%l0, %l4, %l0
	/* Direct access to user regs, must faster. */
	cmp	%g1, NR_syscalls
	bgeu	linux_sparc_ni_syscall
	 sll	%g1, 2, %l4
	ld	[%l7 + %l4], %l7

do_syscall:
	SAVE_ALL_HEAD
	 rd	%wim, %l3

	wr	%l0, PSR_ET, %psr
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2

	ld	[%curptr + TI_FLAGS], %l5
	mov	%i3, %o3
	andcc	%l5, _TIF_SYSCALL_TRACE, %g0
	mov	%i4, %o4
	bne	linux_syscall_trace
	 mov	%i0, %l5
2:
	call	%l7
	 mov	%i5, %o5

3:
	st	%o0, [%sp + STACKFRAME_SZ + PT_I0]

ret_sys_call:
	ld	[%curptr + TI_FLAGS], %l6
	cmp	%o0, -ERESTART_RESTARTBLOCK
	ld	[%sp + STACKFRAME_SZ + PT_PSR], %g3
	set	PSR_C, %g2
	bgeu	1f
	 andcc	%l6, _TIF_SYSCALL_TRACE, %g0

	/* System call success, clear Carry condition code. */
	andn	%g3, %g2, %g3
	clr	%l6
	st	%g3, [%sp + STACKFRAME_SZ + PT_PSR]	
	bne	linux_syscall_trace2
	 ld	[%sp + STACKFRAME_SZ + PT_NPC], %l1 /* pc = npc */
	add	%l1, 0x4, %l2			/* npc = npc+4 */
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	b	ret_trap_entry
	 st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]
1:
	/* System call failure, set Carry condition code.
	 * Also, get abs(errno) to return to the process.
	 */
	sub	%g0, %o0, %o0
	or	%g3, %g2, %g3
	st	%o0, [%sp + STACKFRAME_SZ + PT_I0]
	mov	1, %l6
	st	%g3, [%sp + STACKFRAME_SZ + PT_PSR]
	bne	linux_syscall_trace2
	 ld	[%sp + STACKFRAME_SZ + PT_NPC], %l1 /* pc = npc */
	add	%l1, 0x4, %l2			/* npc = npc+4 */
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	b	ret_trap_entry
	 st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]

linux_syscall_trace2:
	add	%sp, STACKFRAME_SZ, %o0
	mov	1, %o1
	call	syscall_trace
	 add	%l1, 0x4, %l2			/* npc = npc+4 */
	st	%l1, [%sp + STACKFRAME_SZ + PT_PC]
	b	ret_trap_entry
	 st	%l2, [%sp + STACKFRAME_SZ + PT_NPC]


/* Saving and restoring the FPU state is best done from lowlevel code.
 *
 * void fpsave(unsigned long *fpregs, unsigned long *fsr,
 *             void *fpqueue, unsigned long *fpqdepth)
 */

	.globl	fpsave
fpsave:
	st	%fsr, [%o1]	! this can trap on us if fpu is in bogon state
	ld	[%o1], %g1
	set	0x2000, %g4
	andcc	%g1, %g4, %g0
	be	2f
	 mov	0, %g2

	/* We have an fpqueue to save. */
1:
	std	%fq, [%o2]
fpsave_magic:
	st	%fsr, [%o1]
	ld	[%o1], %g3
	andcc	%g3, %g4, %g0
	add	%g2, 1, %g2
	bne	1b
	 add	%o2, 8, %o2

2:
	st	%g2, [%o3]

	std	%f0, [%o0 + 0x00]
	std	%f2, [%o0 + 0x08]
	std	%f4, [%o0 + 0x10]
	std	%f6, [%o0 + 0x18]
	std	%f8, [%o0 + 0x20]
	std	%f10, [%o0 + 0x28]
	std	%f12, [%o0 + 0x30]
	std	%f14, [%o0 + 0x38]
	std	%f16, [%o0 + 0x40]
	std	%f18, [%o0 + 0x48]
	std	%f20, [%o0 + 0x50]
	std	%f22, [%o0 + 0x58]
	std	%f24, [%o0 + 0x60]
	std	%f26, [%o0 + 0x68]
	std	%f28, [%o0 + 0x70]
	retl
	 std	%f30, [%o0 + 0x78]

	/* Thanks for Theo Deraadt and the authors of the Sprite/netbsd/openbsd
	 * code for pointing out this possible deadlock, while we save state
	 * above we could trap on the fsr store so our low level fpu trap
	 * code has to know how to deal with this.
	 */
fpsave_catch:
	b	fpsave_magic + 4
	 st	%fsr, [%o1]

fpsave_catch2:
	b	fpsave + 4
	 st	%fsr, [%o1]

	/* void fpload(unsigned long *fpregs, unsigned long *fsr); */

	.globl	fpload
fpload:
	ldd	[%o0 + 0x00], %f0
	ldd	[%o0 + 0x08], %f2
	ldd	[%o0 + 0x10], %f4
	ldd	[%o0 + 0x18], %f6
	ldd	[%o0 + 0x20], %f8
	ldd	[%o0 + 0x28], %f10
	ldd	[%o0 + 0x30], %f12
	ldd	[%o0 + 0x38], %f14
	ldd	[%o0 + 0x40], %f16
	ldd	[%o0 + 0x48], %f18
	ldd	[%o0 + 0x50], %f20
	ldd	[%o0 + 0x58], %f22
	ldd	[%o0 + 0x60], %f24
	ldd	[%o0 + 0x68], %f26
	ldd	[%o0 + 0x70], %f28
	ldd	[%o0 + 0x78], %f30
	ld	[%o1], %fsr
	retl
	 nop

	/* __ndelay and __udelay take two arguments:
	 * 0 - nsecs or usecs to delay
	 * 1 - per_cpu udelay_val (loops per jiffy)
	 *
	 * Note that ndelay gives HZ times higher resolution but has a 10ms
	 * limit.  udelay can handle up to 1s.
	 */
	.globl	__ndelay
__ndelay:
	save	%sp, -STACKFRAME_SZ, %sp
	mov	%i0, %o0		! round multiplier up so large ns ok
	mov	0x1ae, %o1		! 2**32 / (1 000 000 000 / HZ)
	umul	%o0, %o1, %o0
	rd	%y, %o1
	mov	%i1, %o1		! udelay_val
	umul	%o0, %o1, %o0
	rd	%y, %o1
	ba	delay_continue
	 mov	%o1, %o0		! >>32 later for better resolution

	.globl	__udelay
__udelay:
	save	%sp, -STACKFRAME_SZ, %sp
	mov	%i0, %o0
	sethi	%hi(0x10c7), %o1	! round multiplier up so large us ok
	or	%o1, %lo(0x10c7), %o1	! 2**32 / 1 000 000
	umul	%o0, %o1, %o0
	rd	%y, %o1
	mov	%i1, %o1		! udelay_val
	umul	%o0, %o1, %o0
	rd	%y, %o1
	sethi	%hi(0x028f4b62), %l0	! Add in rounding constant * 2**32,
	or	%g0, %lo(0x028f4b62), %l0
	addcc	%o0, %l0, %o0		! 2**32 * 0.009 999
	bcs,a	3f
	 add	%o1, 0x01, %o1
3:
	mov	HZ, %o0			! >>32 earlier for wider range
	umul	%o0, %o1, %o0
	rd	%y, %o1

delay_continue:
	cmp	%o0, 0x0
1:
	bne	1b
	 subcc	%o0, 1, %o0
	
	ret
	restore
EXPORT_SYMBOL(__udelay)
EXPORT_SYMBOL(__ndelay)

	/* Handle a software breakpoint */
	/* We have to inform parent that child has stopped */
	.align 4
	.globl breakpoint_trap
breakpoint_trap:
	rd	%wim,%l3
	SAVE_ALL
	wr 	%l0, PSR_ET, %psr
	WRITE_PAUSE

	st	%i0, [%sp + STACKFRAME_SZ + PT_G0] ! for restarting syscalls
	call	sparc_breakpoint
	 add	%sp, STACKFRAME_SZ, %o0

	RESTORE_ALL

#ifdef CONFIG_KGDB
	ENTRY(kgdb_trap_low)
	rd	%wim,%l3
	SAVE_ALL
	wr 	%l0, PSR_ET, %psr
	WRITE_PAUSE

	mov	%l7, %o0		! trap_level
	call	kgdb_trap
	 add	%sp, STACKFRAME_SZ, %o1	! struct pt_regs *regs

	RESTORE_ALL
	ENDPROC(kgdb_trap_low)
#endif

	.align	4
	.globl	flush_patch_exception
flush_patch_exception:
	FLUSH_ALL_KERNEL_WINDOWS;
	ldd	[%o0], %o6
	jmpl	%o7 + 0xc, %g0			! see asm-sparc/processor.h
	 mov	1, %g1				! signal EFAULT condition

	.align	4
	.globl	kill_user_windows, kuw_patch1_7win
	.globl	kuw_patch1
kuw_patch1_7win:	sll	%o3, 6, %o3

	/* No matter how much overhead this routine has in the worst
	 * case scenario, it is several times better than taking the
	 * traps with the old method of just doing flush_user_windows().
	 */
kill_user_windows:
	ld	[%g6 + TI_UWINMASK], %o0	! get current umask
	orcc	%g0, %o0, %g0			! if no bits set, we are done
	be	3f				! nothing to do
	 rd	%psr, %o5			! must clear interrupts
	or	%o5, PSR_PIL, %o4		! or else that could change
	wr	%o4, 0x0, %psr			! the uwinmask state
	WRITE_PAUSE				! burn them cycles
1:
	ld	[%g6 + TI_UWINMASK], %o0	! get consistent state
	orcc	%g0, %o0, %g0			! did an interrupt come in?
	be	4f				! yep, we are done
	 rd	%wim, %o3			! get current wim
	srl	%o3, 1, %o4			! simulate a save
kuw_patch1:
	sll	%o3, 7, %o3			! compute next wim
	or	%o4, %o3, %o3			! result
	andncc	%o0, %o3, %o0			! clean this bit in umask
	bne	kuw_patch1			! not done yet
	 srl	%o3, 1, %o4			! begin another save simulation
	wr	%o3, 0x0, %wim			! set the new wim
	st	%g0, [%g6 + TI_UWINMASK]	! clear uwinmask
4:
	wr	%o5, 0x0, %psr			! re-enable interrupts
	WRITE_PAUSE				! burn baby burn
3:
	retl					! return
	 st	%g0, [%g6 + TI_W_SAVED]		! no windows saved

	.align	4
	.globl	restore_current
restore_current:
	LOAD_CURRENT(g6, o0)
	retl
	 nop

#ifdef CONFIG_PCIC_PCI
#include <asm/pcic.h>

	.align	4
	.globl	linux_trap_ipi15_pcic
linux_trap_ipi15_pcic:
	rd	%wim, %l3
	SAVE_ALL

	/*
	 * First deactivate NMI
	 * or we cannot drop ET, cannot get window spill traps.
	 * The busy loop is necessary because the PIO error
	 * sometimes does not go away quickly and we trap again.
	 */
	sethi	%hi(pcic_regs), %o1
	ld	[%o1 + %lo(pcic_regs)], %o2

	! Get pending status for printouts later.
	ld	[%o2 + PCI_SYS_INT_PENDING], %o0

	mov	PCI_SYS_INT_PENDING_CLEAR_ALL, %o1
	stb	%o1, [%o2 + PCI_SYS_INT_PENDING_CLEAR]
1:
	ld	[%o2 + PCI_SYS_INT_PENDING], %o1
	andcc	%o1, ((PCI_SYS_INT_PENDING_PIO|PCI_SYS_INT_PENDING_PCI)>>24), %g0
	bne	1b
	 nop

	or	%l0, PSR_PIL, %l4
	wr	%l4, 0x0, %psr
	WRITE_PAUSE
	wr	%l4, PSR_ET, %psr
	WRITE_PAUSE

	call	pcic_nmi
	 add	%sp, STACKFRAME_SZ, %o1	! struct pt_regs *regs
	RESTORE_ALL

	.globl	pcic_nmi_trap_patch
pcic_nmi_trap_patch:
	sethi	%hi(linux_trap_ipi15_pcic), %l3
	jmpl	%l3 + %lo(linux_trap_ipi15_pcic), %g0
	 rd	%psr, %l0
	.word	0

#endif /* CONFIG_PCIC_PCI */

	.globl	flushw_all
flushw_all:
	save	%sp, -0x40, %sp
	save	%sp, -0x40, %sp
	save	%sp, -0x40, %sp
	save	%sp, -0x40, %sp
	save	%sp, -0x40, %sp
	save	%sp, -0x40, %sp
	save	%sp, -0x40, %sp
	restore
	restore
	restore
	restore
	restore
	restore
	ret
	 restore

#ifdef CONFIG_SMP
ENTRY(hard_smp_processor_id)
661:	rd		%tbr, %g1
	srl		%g1, 12, %o0
	and		%o0, 3, %o0
	.section	.cpuid_patch, "ax"
	/* Instruction location. */
	.word		661b
	/* SUN4D implementation. */
	lda		[%g0] ASI_M_VIKING_TMP1, %o0
	nop
	nop
	/* LEON implementation. */
	rd		%asr17, %o0
	srl		%o0, 0x1c, %o0
	nop
	.previous
	retl
	 nop
ENDPROC(hard_smp_processor_id)
#endif

/* End of entry.S */
