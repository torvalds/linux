/*
 * Based on arch/arm/include/asm/assembler.h, arch/arm/mm/proc-macros.S
 *
 * Copyright (C) 1996-2000 Russell King
 * Copyright (C) 2012 ARM Ltd.
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
#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

#ifndef __ASM_ASSEMBLER_H
#define __ASM_ASSEMBLER_H

#include <asm/asm-offsets.h>
#include <asm/cpufeature.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>

/*
 * Enable and disable interrupts.
 */
	.macro	disable_irq
	msr	daifset, #2
	.endm

	.macro	enable_irq
	msr	daifclr, #2
	.endm

	.macro	save_and_disable_irq, flags
	mrs	\flags, daif
	msr	daifset, #2
	.endm

	.macro	restore_irq, flags
	msr	daif, \flags
	.endm

/*
 * Enable and disable debug exceptions.
 */
	.macro	disable_dbg
	msr	daifset, #8
	.endm

	.macro	enable_dbg
	msr	daifclr, #8
	.endm

	.macro	disable_step_tsk, flgs, tmp
	tbz	\flgs, #TIF_SINGLESTEP, 9990f
	mrs	\tmp, mdscr_el1
	bic	\tmp, \tmp, #1
	msr	mdscr_el1, \tmp
	isb	// Synchronise with enable_dbg
9990:
	.endm

	.macro	enable_step_tsk, flgs, tmp
	tbz	\flgs, #TIF_SINGLESTEP, 9990f
	disable_dbg
	mrs	\tmp, mdscr_el1
	orr	\tmp, \tmp, #1
	msr	mdscr_el1, \tmp
9990:
	.endm

/*
 * Enable both debug exceptions and interrupts. This is likely to be
 * faster than two daifclr operations, since writes to this register
 * are self-synchronising.
 */
	.macro	enable_dbg_and_irq
	msr	daifclr, #(8 | 2)
	.endm

/*
 * SMP data memory barrier
 */
	.macro	smp_dmb, opt
	dmb	\opt
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
 * <symbol> is within the range +/- 4 GB of the PC when running
 * in core kernel context. In module context, a movz/movk sequence
 * is used, since modules may be loaded far away from the kernel
 * when KASLR is in effect.
 */
	/*
	 * @dst: destination register (64 bit wide)
	 * @sym: name of the symbol
	 */
	.macro	adr_l, dst, sym
#ifndef MODULE
	adrp	\dst, \sym
	add	\dst, \dst, :lo12:\sym
#else
	movz	\dst, #:abs_g3:\sym
	movk	\dst, #:abs_g2_nc:\sym
	movk	\dst, #:abs_g1_nc:\sym
	movk	\dst, #:abs_g0_nc:\sym
#endif
	.endm

	/*
	 * @dst: destination register (32 or 64 bit wide)
	 * @sym: name of the symbol
	 * @tmp: optional 64-bit scratch register to be used if <dst> is a
	 *       32-bit wide register, in which case it cannot be used to hold
	 *       the address
	 */
	.macro	ldr_l, dst, sym, tmp=
#ifndef MODULE
	.ifb	\tmp
	adrp	\dst, \sym
	ldr	\dst, [\dst, :lo12:\sym]
	.else
	adrp	\tmp, \sym
	ldr	\dst, [\tmp, :lo12:\sym]
	.endif
#else
	.ifb	\tmp
	adr_l	\dst, \sym
	ldr	\dst, [\dst]
	.else
	adr_l	\tmp, \sym
	ldr	\dst, [\tmp]
	.endif
#endif
	.endm

	/*
	 * @src: source register (32 or 64 bit wide)
	 * @sym: name of the symbol
	 * @tmp: mandatory 64-bit scratch register to calculate the address
	 *       while <src> needs to be preserved.
	 */
	.macro	str_l, src, sym, tmp
#ifndef MODULE
	adrp	\tmp, \sym
	str	\src, [\tmp, :lo12:\sym]
#else
	adr_l	\tmp, \sym
	str	\src, [\tmp]
#endif
	.endm

	/*
	 * @dst: Result of per_cpu(sym, smp_processor_id())
	 * @sym: The name of the per-cpu variable
	 * @tmp: scratch register
	 */
	.macro adr_this_cpu, dst, sym, tmp
	adr_l	\dst, \sym
	mrs	\tmp, tpidr_el1
	add	\dst, \dst, \tmp
	.endm

	/*
	 * @dst: Result of READ_ONCE(per_cpu(sym, smp_processor_id()))
	 * @sym: The name of the per-cpu variable
	 * @tmp: scratch register
	 */
	.macro ldr_this_cpu dst, sym, tmp
	adr_l	\dst, \sym
	mrs	\tmp, tpidr_el1
	ldr	\dst, [\dst, \tmp]
	.endm

/*
 * vma_vm_mm - get mm pointer from vma pointer (vma->vm_mm)
 */
	.macro	vma_vm_mm, rd, rn
	ldr	\rd, [\rn, #VMA_VM_MM]
	.endm

/*
 * mmid - get context id from mm pointer (mm->context.id)
 */
	.macro	mmid, rd, rn
	ldr	\rd, [\rn, #MM_CONTEXT_ID]
	.endm
/*
 * read_ctr - read CTR_EL0. If the system has mismatched
 * cache line sizes, provide the system wide safe value
 * from arm64_ftr_reg_ctrel0.sys_val
 */
	.macro	read_ctr, reg
alternative_if_not ARM64_MISMATCHED_CACHE_LINE_SIZE
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
 * tcr_set_idmap_t0sz - update TCR.T0SZ so that we can load the ID map
 */
	.macro	tcr_set_idmap_t0sz, valreg, tmpreg
#ifndef CONFIG_ARM64_VA_BITS_48
	ldr_l	\tmpreg, idmap_t0sz
	bfi	\valreg, \tmpreg, #TCR_T0SZ_OFFSET, #TCR_TxSZ_WIDTH
#endif
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
	.macro dcache_by_line_op op, domain, kaddr, size, tmp1, tmp2
	dcache_line_size \tmp1, \tmp2
	add	\size, \kaddr, \size
	sub	\tmp2, \tmp1, #1
	bic	\kaddr, \kaddr, \tmp2
9998:
	.if	(\op == cvau || \op == cvac)
alternative_if_not ARM64_WORKAROUND_CLEAN_CACHE
	dc	\op, \kaddr
alternative_else
	dc	civac, \kaddr
alternative_endif
	.else
	dc	\op, \kaddr
	.endif
	add	\kaddr, \kaddr, \tmp1
	cmp	\kaddr, \size
	b.lo	9998b
	dsb	\domain
	.endm

/*
 * reset_pmuserenr_el0 - reset PMUSERENR_EL0 if PMUv3 present
 */
	.macro	reset_pmuserenr_el0, tmpreg
	mrs	\tmpreg, id_aa64dfr0_el1	// Check ID_AA64DFR0_EL1 PMUVer
	sbfx	\tmpreg, \tmpreg, #8, #4
	cmp	\tmpreg, #1			// Skip if no PMU present
	b.lt	9000f
	msr	pmuserenr_el0, xzr		// Disable PMU access from EL0
9000:
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
 * Annotate a function as position independent, i.e., safe to be called before
 * the kernel virtual mapping is activated.
 */
#define ENDPIPROC(x)			\
	.globl	__pi_##x;		\
	.type 	__pi_##x, %function;	\
	.set	__pi_##x, x;		\
	.size	__pi_##x, . - x;	\
	ENDPROC(x)

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
 * Return the current thread_info.
 */
	.macro	get_thread_info, rd
	mrs	\rd, sp_el0
	.endm

/*
 * Errata workaround prior to TTBR0_EL1 update
 *
 * 	val:	TTBR value with new BADDR, preserved
 * 	tmp0:	temporary register, clobbered
 * 	tmp1:	other temporary register, clobbered
 */
	.macro	pre_ttbr0_update_workaround, val, tmp0, tmp1
#ifdef CONFIG_QCOM_FALKOR_ERRATUM_1003
alternative_if ARM64_WORKAROUND_QCOM_FALKOR_E1003
	mrs	\tmp0, ttbr0_el1
	mov	\tmp1, #FALKOR_RESERVED_ASID
	bfi	\tmp0, \tmp1, #48, #16		// reserved ASID + old BADDR
	msr	ttbr0_el1, \tmp0
	isb
	bfi	\tmp0, \val, #0, #48		// reserved ASID + new BADDR
	msr	ttbr0_el1, \tmp0
	isb
alternative_else_nop_endif
#endif
	.endm

/*
 * Errata workaround post TTBR0_EL1 update.
 */
	.macro	post_ttbr0_update_workaround
#ifdef CONFIG_CAVIUM_ERRATUM_27456
alternative_if ARM64_WORKAROUND_CAVIUM_27456
	ic	iallu
	dsb	nsh
	isb
alternative_else_nop_endif
#endif
	.endm

#endif	/* __ASM_ASSEMBLER_H */
