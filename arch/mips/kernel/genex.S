/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000, 2001, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2002 Maciej W. Rozycki
 */
#include <linux/init.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/cacheops.h>
#include <asm/irqflags.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>
#include <asm/war.h>
#include <asm/page.h>

#define PANIC_PIC(msg)					\
		.set push;				\
		.set	reorder;			\
		PTR_LA	a0,8f;				\
		.set	noat;				\
		PTR_LA	AT, panic;			\
		jr	AT;				\
9:		b	9b;				\
		.set	pop;				\
		TEXT(msg)

	__INIT

NESTED(except_vec0_generic, 0, sp)
	PANIC_PIC("Exception vector 0 called")
	END(except_vec0_generic)

NESTED(except_vec1_generic, 0, sp)
	PANIC_PIC("Exception vector 1 called")
	END(except_vec1_generic)

/*
 * General exception vector for all other CPUs.
 *
 * Be careful when changing this, it has to be at most 128 bytes
 * to fit into space reserved for the exception handler.
 */
NESTED(except_vec3_generic, 0, sp)
	.set	push
	.set	noat
#if R5432_CP0_INTERRUPT_WAR
	mfc0	k0, CP0_INDEX
#endif
	mfc0	k1, CP0_CAUSE
	andi	k1, k1, 0x7c
#ifdef CONFIG_64BIT
	dsll	k1, k1, 1
#endif
	PTR_L	k0, exception_handlers(k1)
	jr	k0
	.set	pop
	END(except_vec3_generic)

/*
 * General exception handler for CPUs with virtual coherency exception.
 *
 * Be careful when changing this, it has to be at most 256 (as a special
 * exception) bytes to fit into space reserved for the exception handler.
 */
NESTED(except_vec3_r4000, 0, sp)
	.set	push
	.set	mips3
	.set	noat
	mfc0	k1, CP0_CAUSE
	li	k0, 31<<2
	andi	k1, k1, 0x7c
	.set	push
	.set	noreorder
	.set	nomacro
	beq	k1, k0, handle_vced
	 li	k0, 14<<2
	beq	k1, k0, handle_vcei
#ifdef CONFIG_64BIT
	 dsll	k1, k1, 1
#endif
	.set	pop
	PTR_L	k0, exception_handlers(k1)
	jr	k0

	/*
	 * Big shit, we now may have two dirty primary cache lines for the same
	 * physical address.  We can safely invalidate the line pointed to by
	 * c0_badvaddr because after return from this exception handler the
	 * load / store will be re-executed.
	 */
handle_vced:
	MFC0	k0, CP0_BADVADDR
	li	k1, -4					# Is this ...
	and	k0, k1					# ... really needed?
	mtc0	zero, CP0_TAGLO
	cache	Index_Store_Tag_D, (k0)
	cache	Hit_Writeback_Inv_SD, (k0)
#ifdef CONFIG_PROC_FS
	PTR_LA	k0, vced_count
	lw	k1, (k0)
	addiu	k1, 1
	sw	k1, (k0)
#endif
	eret

handle_vcei:
	MFC0	k0, CP0_BADVADDR
	cache	Hit_Writeback_Inv_SD, (k0)		# also cleans pi
#ifdef CONFIG_PROC_FS
	PTR_LA	k0, vcei_count
	lw	k1, (k0)
	addiu	k1, 1
	sw	k1, (k0)
#endif
	eret
	.set	pop
	END(except_vec3_r4000)

	__FINIT

	.align  5
NESTED(handle_int, PT_SIZE, sp)
	SAVE_ALL
	CLI
	TRACE_IRQS_OFF

	LONG_L	s0, TI_REGS($28)
	LONG_S	sp, TI_REGS($28)
	PTR_LA	ra, ret_from_irq
	j	plat_irq_dispatch
	END(handle_int)

	__INIT

/*
 * Special interrupt vector for MIPS64 ISA & embedded MIPS processors.
 * This is a dedicated interrupt exception vector which reduces the
 * interrupt processing overhead.  The jump instruction will be replaced
 * at the initialization time.
 *
 * Be careful when changing this, it has to be at most 128 bytes
 * to fit into space reserved for the exception handler.
 */
NESTED(except_vec4, 0, sp)
1:	j	1b			/* Dummy, will be replaced */
	END(except_vec4)

/*
 * EJTAG debug exception handler.
 * The EJTAG debug exception entry point is 0xbfc00480, which
 * normally is in the boot PROM, so the boot PROM must do a
 * unconditional jump to this vector.
 */
NESTED(except_vec_ejtag_debug, 0, sp)
	j	ejtag_debug_handler
	END(except_vec_ejtag_debug)

	__FINIT

/*
 * Vectored interrupt handler.
 * This prototype is copied to ebase + n*IntCtl.VS and patched
 * to invoke the handler
 */
NESTED(except_vec_vi, 0, sp)
	SAVE_SOME
	SAVE_AT
	.set	push
	.set	noreorder
#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * To keep from blindly blocking *all* interrupts
	 * during service by SMTC kernel, we also want to
	 * pass the IM value to be cleared.
	 */
FEXPORT(except_vec_vi_mori)
	ori	a0, $0, 0
#endif /* CONFIG_MIPS_MT_SMTC */
FEXPORT(except_vec_vi_lui)
	lui	v0, 0		/* Patched */
	j	except_vec_vi_handler
FEXPORT(except_vec_vi_ori)
	 ori	v0, 0		/* Patched */
	.set	pop
	END(except_vec_vi)
EXPORT(except_vec_vi_end)

/*
 * Common Vectored Interrupt code
 * Complete the register saves and invoke the handler which is passed in $v0
 */
NESTED(except_vec_vi_handler, 0, sp)
	SAVE_TEMP
	SAVE_STATIC
#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * SMTC has an interesting problem that interrupts are level-triggered,
	 * and the CLI macro will clear EXL, potentially causing a duplicate
	 * interrupt service invocation. So we need to clear the associated
	 * IM bit of Status prior to doing CLI, and restore it after the
	 * service routine has been invoked - we must assume that the
	 * service routine will have cleared the state, and any active
	 * level represents a new or otherwised unserviced event...
	 */
	mfc0	t1, CP0_STATUS
	and	t0, a0, t1
	mfc0	t2, CP0_TCCONTEXT
	or	t0, t0, t2
	mtc0	t0, CP0_TCCONTEXT
	xor	t1, t1, t0
	mtc0	t1, CP0_STATUS
	_ehb
#endif /* CONFIG_MIPS_MT_SMTC */
	CLI
#ifdef CONFIG_TRACE_IRQFLAGS
	move	s0, v0
#ifdef CONFIG_MIPS_MT_SMTC
	move	s1, a0
#endif
	TRACE_IRQS_OFF
#ifdef CONFIG_MIPS_MT_SMTC
	move	a0, s1
#endif
	move	v0, s0
#endif

	LONG_L	s0, TI_REGS($28)
	LONG_S	sp, TI_REGS($28)
	PTR_LA	ra, ret_from_irq
	jr	v0
	END(except_vec_vi_handler)

/*
 * EJTAG debug exception handler.
 */
NESTED(ejtag_debug_handler, PT_SIZE, sp)
	.set	push
	.set	noat
	MTC0	k0, CP0_DESAVE
	mfc0	k0, CP0_DEBUG

	sll	k0, k0, 30	# Check for SDBBP.
	bgez	k0, ejtag_return

	PTR_LA	k0, ejtag_debug_buffer
	LONG_S	k1, 0(k0)
	SAVE_ALL
	move	a0, sp
	jal	ejtag_exception_handler
	RESTORE_ALL
	PTR_LA	k0, ejtag_debug_buffer
	LONG_L	k1, 0(k0)

ejtag_return:
	MFC0	k0, CP0_DESAVE
	.set	mips32
	deret
	.set pop
	END(ejtag_debug_handler)

/*
 * This buffer is reserved for the use of the EJTAG debug
 * handler.
 */
	.data
EXPORT(ejtag_debug_buffer)
	.fill	LONGSIZE
	.previous

	__INIT

/*
 * NMI debug exception handler for MIPS reference boards.
 * The NMI debug exception entry point is 0xbfc00000, which
 * normally is in the boot PROM, so the boot PROM must do a
 * unconditional jump to this vector.
 */
NESTED(except_vec_nmi, 0, sp)
	j	nmi_handler
	END(except_vec_nmi)

	__FINIT

NESTED(nmi_handler, PT_SIZE, sp)
	.set	push
	.set	noat
	SAVE_ALL
 	move	a0, sp
	jal	nmi_exception_handler
	RESTORE_ALL
	.set	mips3
	eret
	.set	pop
	END(nmi_handler)

	.macro	__build_clear_none
	.endm

	.macro	__build_clear_sti
	TRACE_IRQS_ON
	STI
	.endm

	.macro	__build_clear_cli
	CLI
	TRACE_IRQS_OFF
	.endm

	.macro	__build_clear_fpe
	cfc1	a1, fcr31
	li	a2, ~(0x3f << 12)
	and	a2, a1
	ctc1	a2, fcr31
	TRACE_IRQS_ON
	STI
	.endm

	.macro	__build_clear_ade
	MFC0	t0, CP0_BADVADDR
	PTR_S	t0, PT_BVADDR(sp)
	KMODE
	.endm

	.macro	__BUILD_silent exception
	.endm

	/* Gas tries to parse the PRINT argument as a string containing
	   string escapes and emits bogus warnings if it believes to
	   recognize an unknown escape code.  So make the arguments
	   start with an n and gas will believe \n is ok ...  */
	.macro	__BUILD_verbose	nexception
	LONG_L	a1, PT_EPC(sp)
#ifdef CONFIG_32BIT
	PRINT("Got \nexception at %08lx\012")
#endif
#ifdef CONFIG_64BIT
	PRINT("Got \nexception at %016lx\012")
#endif
	.endm

	.macro	__BUILD_count exception
	LONG_L	t0,exception_count_\exception
	LONG_ADDIU t0, 1
	LONG_S	t0,exception_count_\exception
	.comm	exception_count\exception, 8, 8
	.endm

	.macro	__BUILD_HANDLER exception handler clear verbose ext
	.align	5
	NESTED(handle_\exception, PT_SIZE, sp)
	.set	noat
	SAVE_ALL
	FEXPORT(handle_\exception\ext)
	__BUILD_clear_\clear
	.set	at
	__BUILD_\verbose \exception
	move	a0, sp
	PTR_LA	ra, ret_from_exception
	j	do_\handler
	END(handle_\exception)
	.endm

	.macro	BUILD_HANDLER exception handler clear verbose
	__BUILD_HANDLER	\exception \handler \clear \verbose _int
	.endm

	BUILD_HANDLER adel ade ade silent		/* #4  */
	BUILD_HANDLER ades ade ade silent		/* #5  */
	BUILD_HANDLER ibe be cli silent			/* #6  */
	BUILD_HANDLER dbe be cli silent			/* #7  */
	BUILD_HANDLER bp bp sti silent			/* #9  */
	BUILD_HANDLER ri ri sti silent			/* #10 */
	BUILD_HANDLER cpu cpu sti silent		/* #11 */
	BUILD_HANDLER ov ov sti silent			/* #12 */
	BUILD_HANDLER tr tr sti silent			/* #13 */
	BUILD_HANDLER fpe fpe fpe silent		/* #15 */
	BUILD_HANDLER mdmx mdmx sti silent		/* #22 */
	BUILD_HANDLER watch watch sti verbose		/* #23 */
	BUILD_HANDLER mcheck mcheck cli verbose		/* #24 */
	BUILD_HANDLER mt mt sti silent			/* #25 */
	BUILD_HANDLER dsp dsp sti silent		/* #26 */
	BUILD_HANDLER reserved reserved sti verbose	/* others */

	.align	5
	LEAF(handle_ri_rdhwr_vivt)
#ifdef CONFIG_MIPS_MT_SMTC
	PANIC_PIC("handle_ri_rdhwr_vivt called")
#else
	.set	push
	.set	noat
	.set	noreorder
	/* check if TLB contains a entry for EPC */
	MFC0	k1, CP0_ENTRYHI
	andi	k1, 0xff	/* ASID_MASK */
	MFC0	k0, CP0_EPC
	PTR_SRL	k0, PAGE_SHIFT + 1
	PTR_SLL	k0, PAGE_SHIFT + 1
	or	k1, k0
	MTC0	k1, CP0_ENTRYHI
	mtc0_tlbw_hazard
	tlbp
	tlb_probe_hazard
	mfc0	k1, CP0_INDEX
	.set	pop
	bltz	k1, handle_ri	/* slow path */
	/* fall thru */
#endif
	END(handle_ri_rdhwr_vivt)

	LEAF(handle_ri_rdhwr)
	.set	push
	.set	noat
	.set	noreorder
	/* 0x7c03e83b: rdhwr v1,$29 */
	MFC0	k1, CP0_EPC
	lui	k0, 0x7c03
	lw	k1, (k1)
	ori	k0, 0xe83b
	.set	reorder
	bne	k0, k1, handle_ri	/* if not ours */
	/* The insn is rdhwr.  No need to check CAUSE.BD here. */
	get_saved_sp	/* k1 := current_thread_info */
	.set	noreorder
	MFC0	k0, CP0_EPC
#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)
	ori	k1, _THREAD_MASK
	xori	k1, _THREAD_MASK
	LONG_L	v1, TI_TP_VALUE(k1)
	LONG_ADDIU	k0, 4
	jr	k0
	 rfe
#else
	LONG_ADDIU	k0, 4		/* stall on $k0 */
	MTC0	k0, CP0_EPC
	/* I hope three instructions between MTC0 and ERET are enough... */
	ori	k1, _THREAD_MASK
	xori	k1, _THREAD_MASK
	LONG_L	v1, TI_TP_VALUE(k1)
	.set	mips3
	eret
	.set	mips0
#endif
	.set	pop
	END(handle_ri_rdhwr)

#ifdef CONFIG_64BIT
/* A temporary overflow handler used by check_daddi(). */

	__INIT

	BUILD_HANDLER  daddi_ov daddi_ov none silent	/* #12 */
#endif
