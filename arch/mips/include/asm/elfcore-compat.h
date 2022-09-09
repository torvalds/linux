#ifndef _ASM_MIPS_ELFCORE_COMPAT_H
#define _ASM_MIPS_ELFCORE_COMPAT_H

/*
 * On mips we have two 32bit ABIs - o32 and n32.  The latter
 * has bigger registers, so we use it for compat_elf_regset_t.
 * The former uses o32_elf_prstatus and PRSTATUS_SIZE/SET_PR_FPVALID
 * are used to choose the size and location of ->pr_fpvalid of
 * the layout actually used.
 */
typedef elf_gregset_t compat_elf_gregset_t;

struct o32_elf_prstatus
{
	struct compat_elf_prstatus_common	common;
	unsigned int 			pr_reg[ELF_NGREG];
	compat_int_t			pr_fpvalid;
};

#define PRSTATUS_SIZE \
	(!test_thread_flag(TIF_32BIT_REGS) \
		? sizeof(struct compat_elf_prstatus) \
		: sizeof(struct o32_elf_prstatus))
#define SET_PR_FPVALID(S) \
	(*(!test_thread_flag(TIF_32BIT_REGS) \
		? &(S)->pr_fpvalid 	\
		: &((struct o32_elf_prstatus *)(S))->pr_fpvalid) = 1)

#endif
