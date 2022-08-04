/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_ELF_H
#define _ASM_ELF_H

#include <linux/auxvec.h>
#include <linux/fs.h>
#include <uapi/linux/elf.h>

#include <asm/current.h>
#include <asm/vdso.h>

/* The ABI of a file. */
#define EF_LOONGARCH_ABI_LP64_SOFT_FLOAT	0x1
#define EF_LOONGARCH_ABI_LP64_SINGLE_FLOAT	0x2
#define EF_LOONGARCH_ABI_LP64_DOUBLE_FLOAT	0x3

#define EF_LOONGARCH_ABI_ILP32_SOFT_FLOAT	0x5
#define EF_LOONGARCH_ABI_ILP32_SINGLE_FLOAT	0x6
#define EF_LOONGARCH_ABI_ILP32_DOUBLE_FLOAT	0x7

/* LoongArch relocation types used by the dynamic linker */
#define R_LARCH_NONE				0
#define R_LARCH_32				1
#define R_LARCH_64				2
#define R_LARCH_RELATIVE			3
#define R_LARCH_COPY				4
#define R_LARCH_JUMP_SLOT			5
#define R_LARCH_TLS_DTPMOD32			6
#define R_LARCH_TLS_DTPMOD64			7
#define R_LARCH_TLS_DTPREL32			8
#define R_LARCH_TLS_DTPREL64			9
#define R_LARCH_TLS_TPREL32			10
#define R_LARCH_TLS_TPREL64			11
#define R_LARCH_IRELATIVE			12
#define R_LARCH_MARK_LA				20
#define R_LARCH_MARK_PCREL			21
#define R_LARCH_SOP_PUSH_PCREL			22
#define R_LARCH_SOP_PUSH_ABSOLUTE		23
#define R_LARCH_SOP_PUSH_DUP			24
#define R_LARCH_SOP_PUSH_GPREL			25
#define R_LARCH_SOP_PUSH_TLS_TPREL		26
#define R_LARCH_SOP_PUSH_TLS_GOT		27
#define R_LARCH_SOP_PUSH_TLS_GD			28
#define R_LARCH_SOP_PUSH_PLT_PCREL		29
#define R_LARCH_SOP_ASSERT			30
#define R_LARCH_SOP_NOT				31
#define R_LARCH_SOP_SUB				32
#define R_LARCH_SOP_SL				33
#define R_LARCH_SOP_SR				34
#define R_LARCH_SOP_ADD				35
#define R_LARCH_SOP_AND				36
#define R_LARCH_SOP_IF_ELSE			37
#define R_LARCH_SOP_POP_32_S_10_5		38
#define R_LARCH_SOP_POP_32_U_10_12		39
#define R_LARCH_SOP_POP_32_S_10_12		40
#define R_LARCH_SOP_POP_32_S_10_16		41
#define R_LARCH_SOP_POP_32_S_10_16_S2		42
#define R_LARCH_SOP_POP_32_S_5_20		43
#define R_LARCH_SOP_POP_32_S_0_5_10_16_S2	44
#define R_LARCH_SOP_POP_32_S_0_10_10_16_S2	45
#define R_LARCH_SOP_POP_32_U			46
#define R_LARCH_ADD8				47
#define R_LARCH_ADD16				48
#define R_LARCH_ADD24				49
#define R_LARCH_ADD32				50
#define R_LARCH_ADD64				51
#define R_LARCH_SUB8				52
#define R_LARCH_SUB16				53
#define R_LARCH_SUB24				54
#define R_LARCH_SUB32				55
#define R_LARCH_SUB64				56
#define R_LARCH_GNU_VTINHERIT			57
#define R_LARCH_GNU_VTENTRY			58

#ifndef ELF_ARCH

/* ELF register definitions */

/*
 * General purpose have the following registers:
 *	Register	Number
 *	GPRs		32
 *	ORIG_A0		1
 *	ERA		1
 *	BADVADDR	1
 *	CRMD		1
 *	PRMD		1
 *	EUEN		1
 *	ECFG		1
 *	ESTAT		1
 *	Reserved	5
 */
#define ELF_NGREG	45

/*
 * Floating point have the following registers:
 *	Register	Number
 *	FPR		32
 *	FCC		1
 *	FCSR		1
 */
#define ELF_NFPREG	34

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

void loongarch_dump_regs64(u64 *uregs, const struct pt_regs *regs);

#ifdef CONFIG_32BIT
/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch elf32_check_arch

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32

#define ELF_CORE_COPY_REGS(dest, regs) \
	loongarch_dump_regs32((u32 *)&(dest), (regs));

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT
/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch elf64_check_arch

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64

#define ELF_CORE_COPY_REGS(dest, regs) \
	loongarch_dump_regs64((u64 *)&(dest), (regs));

#endif /* CONFIG_64BIT */

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_LOONGARCH

#endif /* !defined(ELF_ARCH) */

#define loongarch_elf_check_machine(x) ((x)->e_machine == EM_LOONGARCH)

#define vmcore_elf32_check_arch loongarch_elf_check_machine
#define vmcore_elf64_check_arch loongarch_elf_check_machine

/*
 * Return non-zero if HDR identifies an 32bit ELF binary.
 */
#define elf32_check_arch(hdr)						\
({									\
	int __res = 1;							\
	struct elfhdr *__h = (hdr);					\
									\
	if (!loongarch_elf_check_machine(__h))				\
		__res = 0;						\
	if (__h->e_ident[EI_CLASS] != ELFCLASS32)			\
		__res = 0;						\
									\
	__res;								\
})

/*
 * Return non-zero if HDR identifies an 64bit ELF binary.
 */
#define elf64_check_arch(hdr)						\
({									\
	int __res = 1;							\
	struct elfhdr *__h = (hdr);					\
									\
	if (!loongarch_elf_check_machine(__h))				\
		__res = 0;						\
	if (__h->e_ident[EI_CLASS] != ELFCLASS64)			\
		__res = 0;						\
									\
	__res;								\
})

#ifdef CONFIG_32BIT

#define SET_PERSONALITY2(ex, state)					\
do {									\
	current->thread.vdso = &vdso_info;				\
									\
	loongarch_set_personality_fcsr(state);				\
									\
	if (personality(current->personality) != PER_LINUX)		\
		set_personality(PER_LINUX);				\
} while (0)

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

#define SET_PERSONALITY2(ex, state)					\
do {									\
	unsigned int p;							\
									\
	clear_thread_flag(TIF_32BIT_REGS);				\
	clear_thread_flag(TIF_32BIT_ADDR);				\
									\
	current->thread.vdso = &vdso_info;				\
	loongarch_set_personality_fcsr(state);				\
									\
	p = personality(current->personality);				\
	if (p != PER_LINUX32 && p != PER_LINUX)				\
		set_personality(PER_LINUX);				\
} while (0)

#endif /* CONFIG_64BIT */

#define CORE_DUMP_USE_REGSET
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports. This could be done in userspace,
 * but it's not easy, and we've already done it here.
 */

#define ELF_HWCAP	(elf_hwcap)
extern unsigned int elf_hwcap;
#include <asm/hwcap.h>

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.	 This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 */

#define ELF_PLATFORM  __elf_platform
extern const char *__elf_platform;

#define ELF_PLAT_INIT(_r, load_addr)	do { \
	_r->regs[1] = _r->regs[2] = _r->regs[3] = _r->regs[4] = 0;	\
	_r->regs[5] = _r->regs[6] = _r->regs[7] = _r->regs[8] = 0;	\
	_r->regs[9] = _r->regs[10] = _r->regs[11] = _r->regs[12] = 0;	\
	_r->regs[13] = _r->regs[14] = _r->regs[15] = _r->regs[16] = 0;	\
	_r->regs[17] = _r->regs[18] = _r->regs[19] = _r->regs[20] = 0;	\
	_r->regs[21] = _r->regs[22] = _r->regs[23] = _r->regs[24] = 0;	\
	_r->regs[25] = _r->regs[26] = _r->regs[27] = _r->regs[28] = 0;	\
	_r->regs[29] = _r->regs[30] = _r->regs[31] = 0;			\
} while (0)

/*
 * This is the location that an ET_DYN program is loaded if exec'ed. Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader. We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */

#define ELF_ET_DYN_BASE		(TASK_SIZE / 3 * 2)

/* update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT entries changes */
#define ARCH_DLINFO							\
do {									\
	NEW_AUX_ENT(AT_SYSINFO_EHDR,					\
		    (unsigned long)current->mm->context.vdso);		\
} while (0)

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp);

struct arch_elf_state {
	int fp_abi;
	int interp_fp_abi;
};

#define LOONGARCH_ABI_FP_ANY	(0)

#define INIT_ARCH_ELF_STATE {			\
	.fp_abi = LOONGARCH_ABI_FP_ANY,		\
	.interp_fp_abi = LOONGARCH_ABI_FP_ANY,	\
}

#define elf_read_implies_exec(ex, exec_stk) (exec_stk == EXSTACK_DEFAULT)

extern int arch_elf_pt_proc(void *ehdr, void *phdr, struct file *elf,
			    bool is_interp, struct arch_elf_state *state);

extern int arch_check_elf(void *ehdr, bool has_interpreter, void *interp_ehdr,
			  struct arch_elf_state *state);

extern void loongarch_set_personality_fcsr(struct arch_elf_state *state);

#endif /* _ASM_ELF_H */
