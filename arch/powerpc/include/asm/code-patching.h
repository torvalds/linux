#ifndef _ASM_POWERPC_CODE_PATCHING_H
#define _ASM_POWERPC_CODE_PATCHING_H

/*
 * Copyright 2008, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>
#include <asm/ppc-opcode.h>

/* Flags for create_branch:
 * "b"   == create_branch(addr, target, 0);
 * "ba"  == create_branch(addr, target, BRANCH_ABSOLUTE);
 * "bl"  == create_branch(addr, target, BRANCH_SET_LINK);
 * "bla" == create_branch(addr, target, BRANCH_ABSOLUTE | BRANCH_SET_LINK);
 */
#define BRANCH_SET_LINK	0x1
#define BRANCH_ABSOLUTE	0x2

unsigned int create_branch(const unsigned int *addr,
			   unsigned long target, int flags);
unsigned int create_cond_branch(const unsigned int *addr,
				unsigned long target, int flags);
int patch_branch(unsigned int *addr, unsigned long target, int flags);
int patch_instruction(unsigned int *addr, unsigned int instr);

int instr_is_relative_branch(unsigned int instr);
int instr_is_branch_to_addr(const unsigned int *instr, unsigned long addr);
unsigned long branch_target(const unsigned int *instr);
unsigned int translate_branch(const unsigned int *dest,
			      const unsigned int *src);
#ifdef CONFIG_PPC_BOOK3E_64
void __patch_exception(int exc, unsigned long addr);
#define patch_exception(exc, name) do { \
	extern unsigned int name; \
	__patch_exception((exc), (unsigned long)&name); \
} while (0)
#endif

#define OP_RT_RA_MASK	0xffff0000UL
#define LIS_R2		0x3c020000UL
#define ADDIS_R2_R12	0x3c4c0000UL
#define ADDI_R2_R2	0x38420000UL

static inline unsigned long ppc_function_entry(void *func)
{
#if defined(CONFIG_PPC64)
#if defined(_CALL_ELF) && _CALL_ELF == 2
	u32 *insn = func;

	/*
	 * A PPC64 ABIv2 function may have a local and a global entry
	 * point. We need to use the local entry point when patching
	 * functions, so identify and step over the global entry point
	 * sequence.
	 *
	 * The global entry point sequence is always of the form:
	 *
	 * addis r2,r12,XXXX
	 * addi  r2,r2,XXXX
	 *
	 * A linker optimisation may convert the addis to lis:
	 *
	 * lis   r2,XXXX
	 * addi  r2,r2,XXXX
	 */
	if ((((*insn & OP_RT_RA_MASK) == ADDIS_R2_R12) ||
	     ((*insn & OP_RT_RA_MASK) == LIS_R2)) &&
	    ((*(insn+1) & OP_RT_RA_MASK) == ADDI_R2_R2))
		return (unsigned long)(insn + 2);
	else
		return (unsigned long)func;
#else
	/*
	 * On PPC64 ABIv1 the function pointer actually points to the
	 * function's descriptor. The first entry in the descriptor is the
	 * address of the function text.
	 */
	return ((func_descr_t *)func)->entry;
#endif
#else
	return (unsigned long)func;
#endif
}

static inline unsigned long ppc_global_function_entry(void *func)
{
#if defined(CONFIG_PPC64) && defined(_CALL_ELF) && _CALL_ELF == 2
	/* PPC64 ABIv2 the global entry point is at the address */
	return (unsigned long)func;
#else
	/* All other cases there is no change vs ppc_function_entry() */
	return ppc_function_entry(func);
#endif
}

#endif /* _ASM_POWERPC_CODE_PATCHING_H */
