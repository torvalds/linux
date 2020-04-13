/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/assembler.h, arch/arm/mm/proc-macros.S
 *
 * Copyright (C) 1996-2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

#ifndef __ASM_ASSEMBLER_H
#define __ASM_ASSEMBLER_H

#include <asm-generic/export.h>

#include <asm/asm-offsets.h>
#include <asm/cpufeature.h>
#include <asm/cputype.h>
#include <asm/debug-monitors.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>

	.macro save_and_disable_daif, flags
	mrs	\flags, daif
	msr	daifset, #0xf
	.endm

	.macro disable_daif
	msr	daifset, #0xf
	.endm

	.macro enable_daif
	msr	daifclr, #0xf
	.endm

	.macro	restore_daif, flags:req
	msr	daif, \flags
	.endm

	/* IRQ is the lowest priority flag, unconditionally unmask the rest. */
	.macro enable_da_f
	msr	daifclr, #(8 | 4 | 1)
	.endm

/*
 * Save/restore interrupts.
 */
	.macro	save_and_disable_irq, flags
	mrs	\flags, daif
	msr	daifset, #2
	.endm

	.macro	restore_irq, flags
	msr	daif, \flags
	.endm

	.macro	enable_dbg
	msr	daifclr, #8
	.endm

	.macro	disable_step_tsk, flgs, tmp
	tbz	\flgs, #TIF_SINGLESTEP, 9990f
	mrs	\tmp, mdscr_el1
	bic	\tmp, \tmp, #DBG_MDSCR_SS
	msr	mdscr_el1, \tmp
	isb	// Synchronise with enable_dbg
9990:
	.endm

	/* call with daif masked */
	.macro	enable_step_tsk, flgs, tmp
	tbz	\flgs, #TIF_SINGLESTEP, 9990f
	mrs	\tmp, mdscr_el1
	orr	\tmp, \tmp, #DBG_MDSCR_SS
	msr	mdscr_el1, \tmp
9990:
	.endm

/*
 * RAS Error Synchronization barrier
 */
	.macro  esb
#ifdef CONFIG_ARM64_RAS_EXTN
	hint    #16
#else
	nop
#endif
	.endm

/*
 * Value prediction barrier
 */
	.macro	csdb
	hint	#20
	.endm

/*
 * Speculation barrier
 */
	.macro	sb
alternative_if_not ARM64_HAS_SB
	dsb	nsh
	isb
alternative_else
	SB_BARRIER_INSN
	nop
alternative_endif
	.endm

/*
 * NOP sequence
 */
	.macro	nops, num
	.rept	\num
	nop
	.endr
	.endm

/*
 * Emit an entry into the exception table
 */
	.macro		_asm_extable, from, to
	.pushsection	__ex_table, "a"
	.align		3
	.long		(\from - .), (\to - .)
	.popsection
	.endm

#define USER(l, x...)				\
9999:	x;					\
	_asm_extable	9999b, l

/*
 * Register aliases.
 */
lr	.req	x30		// link register

/*
 * Vector entry
 */
	 .macro	ventry	label
	.align	7
	b	\label
	.endm

/*
 * Select code when configured for BE.
 */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define CPU_BE(code...) code
#else
#define CPU_BE(code...)
#endif

/*
 * Select code when configured for LE.
 */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define CPU_LE(code...)
#else
#define CPU_LE(code...) code
#endif

/*
 * Define a macro that constructs a 64-bit value by concatenating two
 * 32-bit registers. Note that on big endian systems the order of the
 * registers is swapped.
 */
#ifndef CONFIG_CPU_BIG_ENDIAN
	.macro	regs_to_64, rd, lbits, hbits
#else
	.macro	regs_to_64, rd, hbits, lbits
#endif
	orr	\rd, \lbits, \hbits, lsl #32
	.endm

/*
 * Pseudo-ops for PC-relative adr/ldr/str <reg>, <symbol> where
 * <symbol> is within the range +/- 4 GB of the PC.
 */
	/*
	 * @dst: destination register (64 bit wide)
	 * @sym: name of the symbol
	 */
	.macro	adr_l, dst, sym
	adrp	\dst, \sym
	add	\dst, \dst, :lo12:\sym
	.endm

	/*
	 * @dst: destination register (32 or 64 bit wide)
	 * @sym: name of the symbol
	 * @tmp: optional 64-bit scratch register to be used if <dst> is a
	 *       32-bit wide register, in which case it cannot be used to hold
	 *       the address
	 */
	.macro	ldr_l, dst, sym, tmp=
	.ifb	\tmp
	adrp	\dst, \sym
	ldr	\dst, [\dst, :lo12:\sym]
	.else
	adrp	\tmp, \sym
	ldr	\dst, [\tmp, :lo12:\sym]
	.endif
	.endm

	/*
	 * @src: source register (32 or 64 bit wide)
	 * @sym: name of the symbol
	 * @tmp: mandatory 64-bit scratch register to calculate the address
	 *       while <src> needs to be preserved.
	 */
	.macro	str_l, src, sym, tmp
	adrp	\tmp, \sym
	str	\src, [\tmp, :lo12:\sym]
	.endm

	/*
	 * @dst: Result of per_cpu(sym, smp_processor_id()) (can be SP)
	 * @sym: The name of the per-cpu variable
	 * @tmp: scratch register
	 */
	.macro adr_this_cpu, dst, sym, tmp
	adrp	\tmp, \sym
	add	\dst, \tmp, #:lo12:\sym
alternative_if_not ARM64_HAS_VIRT_HOST_EXTN
	mrs	\tmp, tpidr_el1
alternative_else
	mrs	\tmp, tpidr_el2
alternative_endif
	add	\dst, \dst, \tmp
	.endm

	/*
	 * @dst: Result of READ_ONCE(per_cpu(sym, smp_processor_id()))
	 * @sym: The name of the per-cpu variable
	 * @tmp: scratch register
	 */
	.macro ldr_this_cpu dst, sym, tmp
	adr_l	\dst, \sym
alternative_if_not ARM64_HAS_VIRT_HOST_EXTN
	mrs	\tmp, tpidr_el1
alternative_else
	mrs	\tmp, tpidr_el2
alternative_endif
	ldr	\dst, [\dst, \tmp]
	.endm

/*
 * vma_vm_mm - get mm pointer from vma pointer (vma->vm_mm)
 */
	.macro	vma_vm_mm, rd, rn
	ldr	\rd, [\rn, #VMA_VM_MM]
	.endm

/*
 * read_ctr - read CTR_EL0. If the system has mismatched register fields,
 * provide the system wide safe value from arm64_ftr_reg_ctrel0.sys_val
 */
	.macro	read_ctr, reg
alternative_if_not ARM64_MISMATCHED_CACHE_TYPE
	mrs	\reg, ctr_el0			// read CTR
	nop
alternative_else
	ldr_l	\reg, arm64_ftr_reg_ctrel0 + ARM64_FTR_SYSVAL
alternative_endif
	.endm


/*
 * raw_dcache_line_size - get the minimum D-cache line size on this CPU
 * from the CTR register.
 */
	.macro	raw_dcache_line_size, reg, tmp
	mrs	\tmp, ctr_el0			// read CTR
	ubfm	\tmp, \tmp, #16, #19		// cache line size encoding
	mov	\reg, #4			// bytes per word
	lsl	\reg, \reg, \tmp		// actual cache line size
	.endm

/*
 * dcache_line_size - get the safe D-cache line size across all CPUs
 */
	.macro	dcache_line_size, reg, tmp
	read_ctr	\tmp
	ubfm		\tmp, \tmp, #16, #19	// cache line size encoding
	mov		\reg, #4		// bytes per word
	lsl		\reg, \reg, \tmp	// actual cache line size
	.endm

/*
 * raw_icache_line_size - get the minimum I-cache line size on this CPU
 * from the CTR register.
 */
	.macro	raw_icache_line_size, reg, tmp
	mrs	\tmp, ctr_el0			// read CTR
	and	\tmp, \tmp, #0xf		// cache line size encoding
	mov	\reg, #4			// bytes per word
	lsl	\reg, \reg, \tmp		// actual cache line size
	.endm

/*
 * icache_line_size - get the safe I-cache line size across all CPUs
 */
	.macro	icache_line_size, reg, tmp
	read_ctr	\tmp
	and		\tmp, \tmp, #0xf	// cache line size encoding
	mov		\reg, #4		// bytes per word
	lsl		\reg, \reg, \tmp	// actual cache line size
	.endm

/*
 * tcr_set_t0sz - update TCR.T0SZ so that we can load the ID map
 */
	.macro	tcr_set_t0sz, valreg, t0sz
	bfi	\valreg, \t0sz, #TCR_T0SZ_OFFSET, #TCR_TxSZ_WIDTH
	.endm

/*
 * tcr_set_t1sz - update TCR.T1SZ
 */
	.macro	tcr_set_t1sz, valreg, t1sz
	bfi	\valreg, \t1sz, #TCR_T1SZ_OFFSET, #TCR_TxSZ_WIDTH
	.endm

/*
 * tcr_compute_pa_size - set TCR.(I)PS to the highest supported
 * ID_AA64MMFR0_EL1.PARange value
 *
 *	tcr:		register with the TCR_ELx value to be updated
 *	pos:		IPS or PS bitfield position
 *	tmp{0,1}:	temporary registers
 */
	.macro	tcr_compute_pa_size, tcr, pos, tmp0, tmp1
	mrs	\tmp0, ID_AA64MMFR0_EL1
	// Narrow PARange to fit the PS field in TCR_ELx
	ubfx	\tmp0, \tmp0, #ID_AA64MMFR0_PARANGE_SHIFT, #3
	mov	\tmp1, #ID_AA64MMFR0_PARANGE_MAX
	cmp	\tmp0, \tmp1
	csel	\tmp0, \tmp1, \tmp0, hi
	bfi	\tcr, \tmp0, \pos, #3
	.endm

/*
 * Macro to perform a data cache maintenance for the interval
 * [kaddr, kaddr + size)
 *
 * 	op:		operation passed to dc instruction
 * 	domain:		domain used in dsb instruciton
 * 	kaddr:		starting virtual address of the region
 * 	size:		size of the region
 * 	Corrupts:	kaddr, size, tmp1, tmp2
 */
	.macro __dcache_op_workaround_clean_cache, op, kaddr
alternative_if_not ARM64_WORKAROUND_CLEAN_CACHE
	dc	\op, \kaddr
alternative_else
	dc	civac, \kaddr
alternative_endif
	.endm

	.macro dcache_by_line_op op, domain, kaddr, size, tmp1, tmp2
	dcache_line_size \tmp1, \tmp2
	add	\size, \kaddr, \size
	sub	\tmp2, \tmp1, #1
	bic	\kaddr, \kaddr, \tmp2
9998:
	.ifc	\op, cvau
	__dcache_op_workaround_clean_cache \op, \kaddr
	.else
	.ifc	\op, cvac
	__dcache_op_workaround_clean_cache \op, \kaddr
	.else
	.ifc	\op, cvap
	sys	3, c7, c12, 1, \kaddr	// dc cvap
	.else
	.ifc	\op, cvadp
	sys	3, c7, c13, 1, \kaddr	// dc cvadp
	.else
	dc	\op, \kaddr
	.endif
	.endif
	.endif
	.endif
	add	\kaddr, \kaddr, \tmp1
	cmp	\kaddr, \size
	b.lo	9998b
	dsb	\domain
	.endm

/*
 * Macro to perform an instruction cache maintenance for the interval
 * [start, end)
 *
 * 	start, end:	virtual addresses describing the region
 *	label:		A label to branch to on user fault.
 * 	Corrupts:	tmp1, tmp2
 */
	.macro invalidate_icache_by_line start, end, tmp1, tmp2, label
	icache_line_size \tmp1, \tmp2
	sub	\tmp2, \tmp1, #1
	bic	\tmp2, \start, \tmp2
9997:
USER(\label, ic	ivau, \tmp2)			// invalidate I line PoU
	add	\tmp2, \tmp2, \tmp1
	cmp	\tmp2, \end
	b.lo	9997b
	dsb	ish
	isb
	.endm

/*
 * reset_pmuserenr_el0 - reset PMUSERENR_EL0 if PMUv3 present
 */
	.macro	reset_pmuserenr_el0, tmpreg
	mrs	\tmpreg, id_aa64dfr0_el1
	sbfx	\tmpreg, \tmpreg, #ID_AA64DFR0_PMUVER_SHIFT, #4
	cmp	\tmpreg, #1			// Skip if no PMU present
	b.lt	9000f
	msr	pmuserenr_el0, xzr		// Disable PMU access from EL0
9000:
	.endm

/*
 * reset_amuserenr_el0 - reset AMUSERENR_EL0 if AMUv1 present
 */
	.macro	reset_amuserenr_el0, tmpreg
	mrs	\tmpreg, id_aa64pfr0_el1	// Check ID_AA64PFR0_EL1
	ubfx	\tmpreg, \tmpreg, #ID_AA64PFR0_AMU_SHIFT, #4
	cbz	\tmpreg, .Lskip_\@		// Skip if no AMU present
	msr_s	SYS_AMUSERENR_EL0, xzr		// Disable AMU access from EL0
.Lskip_\@:
	.endm
/*
 * copy_page - copy src to dest using temp registers t1-t8
 */
	.macro copy_page dest:req src:req t1:req t2:req t3:req t4:req t5:req t6:req t7:req t8:req
9998:	ldp	\t1, \t2, [\src]
	ldp	\t3, \t4, [\src, #16]
	ldp	\t5, \t6, [\src, #32]
	ldp	\t7, \t8, [\src, #48]
	add	\src, \src, #64
	stnp	\t1, \t2, [\dest]
	stnp	\t3, \t4, [\dest, #16]
	stnp	\t5, \t6, [\dest, #32]
	stnp	\t7, \t8, [\dest, #48]
	add	\dest, \dest, #64
	tst	\src, #(PAGE_SIZE - 1)
	b.ne	9998b
	.endm

/*
 * Annotate a function as being unsuitable for kprobes.
 */
#ifdef CONFIG_KPROBES
#define NOKPROBE(x)				\
	.pushsection "_kprobe_blacklist", "aw";	\
	.quad	x;				\
	.popsection;
#else
#define NOKPROBE(x)
#endif

#ifdef CONFIG_KASAN
#define EXPORT_SYMBOL_NOKASAN(name)
#else
#define EXPORT_SYMBOL_NOKASAN(name)	EXPORT_SYMBOL(name)
#endif

	/*
	 * Emit a 64-bit absolute little endian symbol reference in a way that
	 * ensures that it will be resolved at build time, even when building a
	 * PIE binary. This requires cooperation from the linker script, which
	 * must emit the lo32/hi32 halves individually.
	 */
	.macro	le64sym, sym
	.long	\sym\()_lo32
	.long	\sym\()_hi32
	.endm

	/*
	 * mov_q - move an immediate constant into a 64-bit register using
	 *         between 2 and 4 movz/movk instructions (depending on the
	 *         magnitude and sign of the operand)
	 */
	.macro	mov_q, reg, val
	.if (((\val) >> 31) == 0 || ((\val) >> 31) == 0x1ffffffff)
	movz	\reg, :abs_g1_s:\val
	.else
	.if (((\val) >> 47) == 0 || ((\val) >> 47) == 0x1ffff)
	movz	\reg, :abs_g2_s:\val
	.else
	movz	\reg, :abs_g3:\val
	movk	\reg, :abs_g2_nc:\val
	.endif
	movk	\reg, :abs_g1_nc:\val
	.endif
	movk	\reg, :abs_g0_nc:\val
	.endm

/*
 * Return the current task_struct.
 */
	.macro	get_current_task, rd
	mrs	\rd, sp_el0
	.endm

/*
 * Offset ttbr1 to allow for 48-bit kernel VAs set with 52-bit PTRS_PER_PGD.
 * orr is used as it can cover the immediate value (and is idempotent).
 * In future this may be nop'ed out when dealing with 52-bit kernel VAs.
 * 	ttbr: Value of ttbr to set, modified.
 */
	.macro	offset_ttbr1, ttbr, tmp
#ifdef CONFIG_ARM64_VA_BITS_52
	mrs_s	\tmp, SYS_ID_AA64MMFR2_EL1
	and	\tmp, \tmp, #(0xf << ID_AA64MMFR2_LVA_SHIFT)
	cbnz	\tmp, .Lskipoffs_\@
	orr	\ttbr, \ttbr, #TTBR1_BADDR_4852_OFFSET
.Lskipoffs_\@ :
#endif
	.endm

/*
 * Perform the reverse of offset_ttbr1.
 * bic is used as it can cover the immediate value and, in future, won't need
 * to be nop'ed out when dealing with 52-bit kernel VAs.
 */
	.macro	restore_ttbr1, ttbr
#ifdef CONFIG_ARM64_VA_BITS_52
	bic	\ttbr, \ttbr, #TTBR1_BADDR_4852_OFFSET
#endif
	.endm

/*
 * Arrange a physical address in a TTBR register, taking care of 52-bit
 * addresses.
 *
 * 	phys:	physical address, preserved
 * 	ttbr:	returns the TTBR value
 */
	.macro	phys_to_ttbr, ttbr, phys
#ifdef CONFIG_ARM64_PA_BITS_52
	orr	\ttbr, \phys, \phys, lsr #46
	and	\ttbr, \ttbr, #TTBR_BADDR_MASK_52
#else
	mov	\ttbr, \phys
#endif
	.endm

	.macro	phys_to_pte, pte, phys
#ifdef CONFIG_ARM64_PA_BITS_52
	/*
	 * We assume \phys is 64K aligned and this is guaranteed by only
	 * supporting this configuration with 64K pages.
	 */
	orr	\pte, \phys, \phys, lsr #36
	and	\pte, \pte, #PTE_ADDR_MASK
#else
	mov	\pte, \phys
#endif
	.endm

	.macro	pte_to_phys, phys, pte
#ifdef CONFIG_ARM64_PA_BITS_52
	ubfiz	\phys, \pte, #(48 - 16 - 12), #16
	bfxil	\phys, \pte, #16, #32
	lsl	\phys, \phys, #16
#else
	and	\phys, \pte, #PTE_ADDR_MASK
#endif
	.endm

/*
 * tcr_clear_errata_bits - Clear TCR bits that trigger an errata on this CPU.
 */
	.macro	tcr_clear_errata_bits, tcr, tmp1, tmp2
#ifdef CONFIG_FUJITSU_ERRATUM_010001
	mrs	\tmp1, midr_el1

	mov_q	\tmp2, MIDR_FUJITSU_ERRATUM_010001_MASK
	and	\tmp1, \tmp1, \tmp2
	mov_q	\tmp2, MIDR_FUJITSU_ERRATUM_010001
	cmp	\tmp1, \tmp2
	b.ne	10f

	mov_q	\tmp2, TCR_CLEAR_FUJITSU_ERRATUM_010001
	bic	\tcr, \tcr, \tmp2
10:
#endif /* CONFIG_FUJITSU_ERRATUM_010001 */
	.endm

/**
 * Errata workaround prior to disable MMU. Insert an ISB immediately prior
 * to executing the MSR that will change SCTLR_ELn[M] from a value of 1 to 0.
 */
	.macro pre_disable_mmu_workaround
#ifdef CONFIG_QCOM_FALKOR_ERRATUM_E1041
	isb
#endif
	.endm

	/*
	 * frame_push - Push @regcount callee saved registers to the stack,
	 *              starting at x19, as well as x29/x30, and set x29 to
	 *              the new value of sp. Add @extra bytes of stack space
	 *              for locals.
	 */
	.macro		frame_push, regcount:req, extra
	__frame		st, \regcount, \extra
	.endm

	/*
	 * frame_pop  - Pop the callee saved registers from the stack that were
	 *              pushed in the most recent call to frame_push, as well
	 *              as x29/x30 and any extra stack space that may have been
	 *              allocated.
	 */
	.macro		frame_pop
	__frame		ld
	.endm

	.macro		__frame_regs, reg1, reg2, op, num
	.if		.Lframe_regcount == \num
	\op\()r		\reg1, [sp, #(\num + 1) * 8]
	.elseif		.Lframe_regcount > \num
	\op\()p		\reg1, \reg2, [sp, #(\num + 1) * 8]
	.endif
	.endm

	.macro		__frame, op, regcount, extra=0
	.ifc		\op, st
	.if		(\regcount) < 0 || (\regcount) > 10
	.error		"regcount should be in the range [0 ... 10]"
	.endif
	.if		((\extra) % 16) != 0
	.error		"extra should be a multiple of 16 bytes"
	.endif
	.ifdef		.Lframe_regcount
	.if		.Lframe_regcount != -1
	.error		"frame_push/frame_pop may not be nested"
	.endif
	.endif
	.set		.Lframe_regcount, \regcount
	.set		.Lframe_extra, \extra
	.set		.Lframe_local_offset, ((\regcount + 3) / 2) * 16
	stp		x29, x30, [sp, #-.Lframe_local_offset - .Lframe_extra]!
	mov		x29, sp
	.endif

	__frame_regs	x19, x20, \op, 1
	__frame_regs	x21, x22, \op, 3
	__frame_regs	x23, x24, \op, 5
	__frame_regs	x25, x26, \op, 7
	__frame_regs	x27, x28, \op, 9

	.ifc		\op, ld
	.if		.Lframe_regcount == -1
	.error		"frame_push/frame_pop may not be nested"
	.endif
	ldp		x29, x30, [sp], #.Lframe_local_offset + .Lframe_extra
	.set		.Lframe_regcount, -1
	.endif
	.endm

/*
 * Check whether to yield to another runnable task from kernel mode NEON code
 * (which runs with preemption disabled).
 *
 * if_will_cond_yield_neon
 *        // pre-yield patchup code
 * do_cond_yield_neon
 *        // post-yield patchup code
 * endif_yield_neon    <label>
 *
 * where <label> is optional, and marks the point where execution will resume
 * after a yield has been performed. If omitted, execution resumes right after
 * the endif_yield_neon invocation. Note that the entire sequence, including
 * the provided patchup code, will be omitted from the image if
 * CONFIG_PREEMPTION is not defined.
 *
 * As a convenience, in the case where no patchup code is required, the above
 * sequence may be abbreviated to
 *
 * cond_yield_neon <label>
 *
 * Note that the patchup code does not support assembler directives that change
 * the output section, any use of such directives is undefined.
 *
 * The yield itself consists of the following:
 * - Check whether the preempt count is exactly 1 and a reschedule is also
 *   needed. If so, calling of preempt_enable() in kernel_neon_end() will
 *   trigger a reschedule. If it is not the case, yielding is pointless.
 * - Disable and re-enable kernel mode NEON, and branch to the yield fixup
 *   code.
 *
 * This macro sequence may clobber all CPU state that is not guaranteed by the
 * AAPCS to be preserved across an ordinary function call.
 */

	.macro		cond_yield_neon, lbl
	if_will_cond_yield_neon
	do_cond_yield_neon
	endif_yield_neon	\lbl
	.endm

	.macro		if_will_cond_yield_neon
#ifdef CONFIG_PREEMPTION
	get_current_task	x0
	ldr		x0, [x0, #TSK_TI_PREEMPT]
	sub		x0, x0, #PREEMPT_DISABLE_OFFSET
	cbz		x0, .Lyield_\@
	/* fall through to endif_yield_neon */
	.subsection	1
.Lyield_\@ :
#else
	.section	".discard.cond_yield_neon", "ax"
#endif
	.endm

	.macro		do_cond_yield_neon
	bl		kernel_neon_end
	bl		kernel_neon_begin
	.endm

	.macro		endif_yield_neon, lbl
	.ifnb		\lbl
	b		\lbl
	.else
	b		.Lyield_out_\@
	.endif
	.previous
.Lyield_out_\@ :
	.endm

#endif	/* __ASM_ASSEMBLER_H */
