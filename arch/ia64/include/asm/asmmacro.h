#ifndef _ASM_IA64_ASMMACRO_H
#define _ASM_IA64_ASMMACRO_H

/*
 * Copyright (C) 2000-2001, 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */


#define ENTRY(name)				\
	.align 32;				\
	.proc name;				\
name:

#define ENTRY_MIN_ALIGN(name)			\
	.align 16;				\
	.proc name;				\
name:

#define GLOBAL_ENTRY(name)			\
	.global name;				\
	ENTRY(name)

#define END(name)				\
	.endp name

/*
 * Helper macros to make unwind directives more readable:
 */

/* prologue_gr: */
#define ASM_UNW_PRLG_RP			0x8
#define ASM_UNW_PRLG_PFS		0x4
#define ASM_UNW_PRLG_PSP		0x2
#define ASM_UNW_PRLG_PR			0x1
#define ASM_UNW_PRLG_GRSAVE(ninputs)	(32+(ninputs))

/*
 * Helper macros for accessing user memory.
 *
 * When adding any new .section/.previous entries here, make sure to
 * also add it to the DISCARD section in arch/ia64/kernel/gate.lds.S or
 * unpleasant things will happen.
 */

	.section "__ex_table", "a"		// declare section & section attributes
	.previous

# define EX(y,x...)				\
	.xdata4 "__ex_table", 99f-., y-.;	\
  [99:]	x
# define EXCLR(y,x...)				\
	.xdata4 "__ex_table", 99f-., y-.+4;	\
  [99:]	x

/*
 * Tag MCA recoverable instruction ranges.
 */

	.section "__mca_table", "a"		// declare section & section attributes
	.previous

# define MCA_RECOVER_RANGE(y)			\
	.xdata4 "__mca_table", y-., 99f-.;	\
  [99:]

/*
 * Mark instructions that need a load of a virtual address patched to be
 * a load of a physical address.  We use this either in critical performance
 * path (ivt.S - TLB miss processing) or in places where it might not be
 * safe to use a "tpa" instruction (mca_asm.S - error recovery).
 */
	.section ".data..patch.vtop", "a"	// declare section & section attributes
	.previous

#define	LOAD_PHYSICAL(pr, reg, obj)		\
[1:](pr)movl reg = obj;				\
	.xdata4 ".data..patch.vtop", 1b-.

/*
 * For now, we always put in the McKinley E9 workaround.  On CPUs that don't need it,
 * we'll patch out the work-around bundles with NOPs, so their impact is minimal.
 */
#define DO_MCKINLEY_E9_WORKAROUND

#ifdef DO_MCKINLEY_E9_WORKAROUND
	.section ".data..patch.mckinley_e9", "a"
	.previous
/* workaround for Itanium 2 Errata 9: */
# define FSYS_RETURN					\
	.xdata4 ".data..patch.mckinley_e9", 1f-.;	\
1:{ .mib;						\
	nop.m 0;					\
	mov r16=ar.pfs;					\
	br.call.sptk.many b7=2f;;			\
  };							\
2:{ .mib;						\
	nop.m 0;					\
	mov ar.pfs=r16;					\
	br.ret.sptk.many b6;;				\
  }
#else
# define FSYS_RETURN	br.ret.sptk.many b6
#endif

/*
 * If physical stack register size is different from DEF_NUM_STACK_REG,
 * dynamically patch the kernel for correct size.
 */
	.section ".data..patch.phys_stack_reg", "a"
	.previous
#define LOAD_PHYS_STACK_REG_SIZE(reg)			\
[1:]	adds reg=IA64_NUM_PHYS_STACK_REG*8+8,r0;	\
	.xdata4 ".data..patch.phys_stack_reg", 1b-.

/*
 * Up until early 2004, use of .align within a function caused bad unwind info.
 * TEXT_ALIGN(n) expands into ".align n" if a fixed GAS is available or into nothing
 * otherwise.
 */
#ifdef HAVE_WORKING_TEXT_ALIGN
# define TEXT_ALIGN(n)	.align n
#else
# define TEXT_ALIGN(n)
#endif

#ifdef HAVE_SERIALIZE_DIRECTIVE
# define dv_serialize_data		.serialize.data
# define dv_serialize_instruction	.serialize.instruction
#else
# define dv_serialize_data
# define dv_serialize_instruction
#endif

#endif /* _ASM_IA64_ASMMACRO_H */
