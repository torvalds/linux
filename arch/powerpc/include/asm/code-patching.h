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
#include <linux/string.h>
#include <linux/kallsyms.h>

/* Flags for create_branch:
 * "b"   == create_branch(addr, target, 0);
 * "ba"  == create_branch(addr, target, BRANCH_ABSOLUTE);
 * "bl"  == create_branch(addr, target, BRANCH_SET_LINK);
 * "bla" == create_branch(addr, target, BRANCH_ABSOLUTE | BRANCH_SET_LINK);
 */
#define BRANCH_SET_LINK	0x1
#define BRANCH_ABSOLUTE	0x2

bool is_offset_in_branch_range(long offset);
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
extern bool is_conditional_branch(unsigned int instr);
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
#ifdef PPC64_ELF_ABI_v2
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
#elif defined(PPC64_ELF_ABI_v1)
	/*
	 * On PPC64 ABIv1 the function pointer actually points to the
	 * function's descriptor. The first entry in the descriptor is the
	 * address of the function text.
	 */
	return ((func_descr_t *)func)->entry;
#else
	return (unsigned long)func;
#endif
}

static inline unsigned long ppc_global_function_entry(void *func)
{
#ifdef PPC64_ELF_ABI_v2
	/* PPC64 ABIv2 the global entry point is at the address */
	return (unsigned long)func;
#else
	/* All other cases there is no change vs ppc_function_entry() */
	return ppc_function_entry(func);
#endif
}

/*
 * Wrapper around kallsyms_lookup() to return function entry address:
 * - For ABIv1, we lookup the dot variant.
 * - For ABIv2, we return the local entry point.
 */
static inline unsigned long ppc_kallsyms_lookup_name(const char *name)
{
	unsigned long addr;
#ifdef PPC64_ELF_ABI_v1
	/* check for dot variant */
	char dot_name[1 + KSYM_NAME_LEN];
	bool dot_appended = false;

	if (strnlen(name, KSYM_NAME_LEN) >= KSYM_NAME_LEN)
		return 0;

	if (name[0] != '.') {
		dot_name[0] = '.';
		dot_name[1] = '\0';
		strlcat(dot_name, name, sizeof(dot_name));
		dot_appended = true;
	} else {
		dot_name[0] = '\0';
		strlcat(dot_name, name, sizeof(dot_name));
	}
	addr = kallsyms_lookup_name(dot_name);
	if (!addr && dot_appended)
		/* Let's try the original non-dot symbol lookup	*/
		addr = kallsyms_lookup_name(name);
#elif defined(PPC64_ELF_ABI_v2)
	addr = kallsyms_lookup_name(name);
	if (addr)
		addr = ppc_function_entry((void *)addr);
#else
	addr = kallsyms_lookup_name(name);
#endif
	return addr;
}

#ifdef CONFIG_PPC64
/*
 * Some instruction encodings commonly used in dynamic ftracing
 * and function live patching.
 */

/* This must match the definition of STK_GOT in <asm/ppc_asm.h> */
#ifdef PPC64_ELF_ABI_v2
#define R2_STACK_OFFSET         24
#else
#define R2_STACK_OFFSET         40
#endif

#define PPC_INST_LD_TOC		(PPC_INST_LD  | ___PPC_RT(__REG_R2) | \
				 ___PPC_RA(__REG_R1) | R2_STACK_OFFSET)

/* usually preceded by a mflr r0 */
#define PPC_INST_STD_LR		(PPC_INST_STD | ___PPC_RS(__REG_R0) | \
				 ___PPC_RA(__REG_R1) | PPC_LR_STKOFF)
#endif /* CONFIG_PPC64 */

#endif /* _ASM_POWERPC_CODE_PATCHING_H */
