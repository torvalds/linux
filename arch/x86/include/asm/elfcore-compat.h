#ifndef _ASM_X86_ELFCORE_COMPAT_H
#define _ASM_X86_ELFCORE_COMPAT_H

#include <asm/user32.h>

/*
 * On amd64 we have two 32bit ABIs - i386 and x32.  The latter
 * has bigger registers, so we use it for compat_elf_regset_t.
 * The former uses i386_elf_prstatus and PRSTATUS_SIZE/SET_PR_FPVALID
 * are used to choose the size and location of ->pr_fpvalid of
 * the layout actually used.
 */
typedef struct user_regs_struct compat_elf_gregset_t;

struct i386_elf_prstatus
{
	struct compat_elf_prstatus_common	common;
	struct user_regs_struct32		pr_reg;
	compat_int_t			pr_fpvalid;
};

#define PRSTATUS_SIZE \
	(user_64bit_mode(task_pt_regs(current)) \
		? sizeof(struct compat_elf_prstatus) \
		: sizeof(struct i386_elf_prstatus))
#define SET_PR_FPVALID(S) \
	(*(user_64bit_mode(task_pt_regs(current)) \
		? &(S)->pr_fpvalid 	\
		: &((struct i386_elf_prstatus *)(S))->pr_fpvalid) = 1)

#endif
