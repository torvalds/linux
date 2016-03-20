/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */
#ifndef __UM_ELF_X86_H
#define __UM_ELF_X86_H

#include <asm/user.h>
#include <skas.h>

#ifdef CONFIG_X86_32

#define R_386_NONE	0
#define R_386_32	1
#define R_386_PC32	2
#define R_386_GOT32	3
#define R_386_PLT32	4
#define R_386_COPY	5
#define R_386_GLOB_DAT	6
#define R_386_JMP_SLOT	7
#define R_386_RELATIVE	8
#define R_386_GOTOFF	9
#define R_386_GOTPC	10
#define R_386_NUM	11

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

#define ELF_CLASS	ELFCLASS32
#define ELF_DATA        ELFDATA2LSB
#define ELF_ARCH        EM_386

#define ELF_PLAT_INIT(regs, load_addr) do { \
	PT_REGS_BX(regs) = 0; \
	PT_REGS_CX(regs) = 0; \
	PT_REGS_DX(regs) = 0; \
	PT_REGS_SI(regs) = 0; \
	PT_REGS_DI(regs) = 0; \
	PT_REGS_BP(regs) = 0; \
	PT_REGS_AX(regs) = 0; \
} while (0)

/* Shamelessly stolen from include/asm-i386/elf.h */

#define ELF_CORE_COPY_REGS(pr_reg, regs) do {	\
	pr_reg[0] = PT_REGS_BX(regs);		\
	pr_reg[1] = PT_REGS_CX(regs);		\
	pr_reg[2] = PT_REGS_DX(regs);		\
	pr_reg[3] = PT_REGS_SI(regs);		\
	pr_reg[4] = PT_REGS_DI(regs);		\
	pr_reg[5] = PT_REGS_BP(regs);		\
	pr_reg[6] = PT_REGS_AX(regs);		\
	pr_reg[7] = PT_REGS_DS(regs);		\
	pr_reg[8] = PT_REGS_ES(regs);		\
	/* fake once used fs and gs selectors? */	\
	pr_reg[9] = PT_REGS_DS(regs);		\
	pr_reg[10] = PT_REGS_DS(regs);		\
	pr_reg[11] = PT_REGS_SYSCALL_NR(regs);	\
	pr_reg[12] = PT_REGS_IP(regs);		\
	pr_reg[13] = PT_REGS_CS(regs);		\
	pr_reg[14] = PT_REGS_EFLAGS(regs);	\
	pr_reg[15] = PT_REGS_SP(regs);		\
	pr_reg[16] = PT_REGS_SS(regs);		\
} while (0);

extern char * elf_aux_platform;
#define ELF_PLATFORM (elf_aux_platform)

extern unsigned long vsyscall_ehdr;
extern unsigned long vsyscall_end;
extern unsigned long __kernel_vsyscall;

/*
 * This is the range that is readable by user mode, and things
 * acting like user mode such as get_user_pages.
 */
#define FIXADDR_USER_START      vsyscall_ehdr
#define FIXADDR_USER_END        vsyscall_end


/*
 * Architecture-neutral AT_ values in 0-17, leave some room
 * for more of them, start the x86-specific ones at 32.
 */
#define AT_SYSINFO		32
#define AT_SYSINFO_EHDR		33

#define ARCH_DLINFO						\
do {								\
	if ( vsyscall_ehdr ) {					\
		NEW_AUX_ENT(AT_SYSINFO,	__kernel_vsyscall);	\
		NEW_AUX_ENT(AT_SYSINFO_EHDR, vsyscall_ehdr);	\
	}							\
} while (0)

#else

/* x86-64 relocation types, taken from asm-x86_64/elf.h */
#define R_X86_64_NONE		0	/* No reloc */
#define R_X86_64_64		1	/* Direct 64 bit  */
#define R_X86_64_PC32		2	/* PC relative 32 bit signed */
#define R_X86_64_GOT32		3	/* 32 bit GOT entry */
#define R_X86_64_PLT32		4	/* 32 bit PLT address */
#define R_X86_64_COPY		5	/* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT	6	/* Create GOT entry */
#define R_X86_64_JUMP_SLOT	7	/* Create PLT entry */
#define R_X86_64_RELATIVE	8	/* Adjust by program base */
#define R_X86_64_GOTPCREL	9	/* 32 bit signed pc relative
					   offset to GOT */
#define R_X86_64_32		10	/* Direct 32 bit zero extended */
#define R_X86_64_32S		11	/* Direct 32 bit sign extended */
#define R_X86_64_16		12	/* Direct 16 bit zero extended */
#define R_X86_64_PC16		13	/* 16 bit sign extended pc relative */
#define R_X86_64_8		14	/* Direct 8 bit sign extended  */
#define R_X86_64_PC8		15	/* 8 bit sign extended pc relative */

#define R_X86_64_NUM		16

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	((x)->e_machine == EM_X86_64)

#define ELF_CLASS	ELFCLASS64
#define ELF_DATA        ELFDATA2LSB
#define ELF_ARCH        EM_X86_64

#define ELF_PLAT_INIT(regs, load_addr)    do { \
	PT_REGS_BX(regs) = 0; \
	PT_REGS_CX(regs) = 0; \
	PT_REGS_DX(regs) = 0; \
	PT_REGS_SI(regs) = 0; \
	PT_REGS_DI(regs) = 0; \
	PT_REGS_BP(regs) = 0; \
	PT_REGS_AX(regs) = 0; \
	PT_REGS_R8(regs) = 0; \
	PT_REGS_R9(regs) = 0; \
	PT_REGS_R10(regs) = 0; \
	PT_REGS_R11(regs) = 0; \
	PT_REGS_R12(regs) = 0; \
	PT_REGS_R13(regs) = 0; \
	PT_REGS_R14(regs) = 0; \
	PT_REGS_R15(regs) = 0; \
} while (0)

#define ELF_CORE_COPY_REGS(pr_reg, _regs)		\
	(pr_reg)[0] = (_regs)->regs.gp[0];			\
	(pr_reg)[1] = (_regs)->regs.gp[1];			\
	(pr_reg)[2] = (_regs)->regs.gp[2];			\
	(pr_reg)[3] = (_regs)->regs.gp[3];			\
	(pr_reg)[4] = (_regs)->regs.gp[4];			\
	(pr_reg)[5] = (_regs)->regs.gp[5];			\
	(pr_reg)[6] = (_regs)->regs.gp[6];			\
	(pr_reg)[7] = (_regs)->regs.gp[7];			\
	(pr_reg)[8] = (_regs)->regs.gp[8];			\
	(pr_reg)[9] = (_regs)->regs.gp[9];			\
	(pr_reg)[10] = (_regs)->regs.gp[10];			\
	(pr_reg)[11] = (_regs)->regs.gp[11];			\
	(pr_reg)[12] = (_regs)->regs.gp[12];			\
	(pr_reg)[13] = (_regs)->regs.gp[13];			\
	(pr_reg)[14] = (_regs)->regs.gp[14];			\
	(pr_reg)[15] = (_regs)->regs.gp[15];			\
	(pr_reg)[16] = (_regs)->regs.gp[16];			\
	(pr_reg)[17] = (_regs)->regs.gp[17];			\
	(pr_reg)[18] = (_regs)->regs.gp[18];			\
	(pr_reg)[19] = (_regs)->regs.gp[19];			\
	(pr_reg)[20] = (_regs)->regs.gp[20];			\
	(pr_reg)[21] = current->thread.arch.fs;			\
	(pr_reg)[22] = 0;					\
	(pr_reg)[23] = 0;					\
	(pr_reg)[24] = 0;					\
	(pr_reg)[25] = 0;					\
	(pr_reg)[26] = 0;

#define ELF_PLATFORM "x86_64"

/* No user-accessible fixmap addresses, i.e. vsyscall */
#define FIXADDR_USER_START      0
#define FIXADDR_USER_END        0

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp);

extern unsigned long um_vdso_addr;
#define AT_SYSINFO_EHDR 33
#define ARCH_DLINFO	NEW_AUX_ENT(AT_SYSINFO_EHDR, um_vdso_addr)

#endif

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_i387_struct elf_fpregset_t;

struct task_struct;

extern int elf_core_copy_fpregs(struct task_struct *t, elf_fpregset_t *fpu);

#define ELF_CORE_COPY_FPREGS(t, fpu) elf_core_copy_fpregs(t, fpu)

#define ELF_EXEC_PAGESIZE 4096

#define ELF_ET_DYN_BASE (TASK_SIZE / 3 * 2)

extern long elf_aux_hwcap;
#define ELF_HWCAP (elf_aux_hwcap)

#define SET_PERSONALITY(ex) do ; while(0)

#endif
