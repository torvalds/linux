/*
 * Support for o32 Linux/MIPS ELF binaries.
 *
 * Copyright (C) 1999, 2001 Ralf Baechle
 * Copyright (C) 1999, 2001 Silicon Graphics, Inc.
 *
 * Heavily inspired by the 32-bit Sparc compat code which is
 * Copyright (C) 1995, 1996, 1997, 1998 David S. Miller (davem@redhat.com)
 * Copyright (C) 1995, 1996, 1997, 1998 Jakub Jelinek	(jj@ultra.linux.cz)
 */

#define ELF_ARCH		EM_MIPS
#define ELF_CLASS		ELFCLASS32
#ifdef __MIPSEB__
#define ELF_DATA		ELFDATA2MSB;
#else /* __MIPSEL__ */
#define ELF_DATA		ELFDATA2LSB;
#endif

/* ELF register definitions */
#define ELF_NGREG	45
#define ELF_NFPREG	33

typedef unsigned int elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(hdr)						\
({									\
	int __res = 1;							\
	struct elfhdr *__h = (hdr);					\
									\
	if (__h->e_machine != EM_MIPS)					\
		__res = 0;						\
	if (__h->e_ident[EI_CLASS] != ELFCLASS32)			\
		__res = 0;						\
	if ((__h->e_flags & EF_MIPS_ABI2) != 0)				\
		__res = 0;						\
	if (((__h->e_flags & EF_MIPS_ABI) != 0) &&			\
	    ((__h->e_flags & EF_MIPS_ABI) != EF_MIPS_ABI_O32))		\
		__res = 0;						\
									\
	__res;								\
})

#define TASK32_SIZE		0x7fff8000UL
#undef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE		(TASK32_SIZE / 3 * 2)

#include <asm/processor.h>

/*
 * When this file is selected, we are definitely running a 64bit kernel.
 * So using the right regs define in asm/reg.h
 */
#define WANT_COMPAT_REG_H

/* These MUST be defined before elf.h gets included */
extern void elf32_core_copy_regs(elf_gregset_t grp, struct pt_regs *regs);
#define ELF_CORE_COPY_REGS(_dest, _regs) elf32_core_copy_regs(_dest, _regs);
#define ELF_CORE_COPY_TASK_REGS(_tsk, _dest)				\
({									\
	int __res = 1;							\
	elf32_core_copy_regs(*(_dest), task_pt_regs(_tsk));		\
	__res;								\
})

#include <linux/module.h>
#include <linux/elfcore.h>
#include <linux/compat.h>
#include <linux/math64.h>

#define elf_prstatus elf_prstatus32
struct elf_prstatus32
{
	struct elf_siginfo pr_info;	/* Info associated with signal */
	short	pr_cursig;		/* Current signal */
	unsigned int pr_sigpend;	/* Set of pending signals */
	unsigned int pr_sighold;	/* Set of held signals */
	pid_t	pr_pid;
	pid_t	pr_ppid;
	pid_t	pr_pgrp;
	pid_t	pr_sid;
	struct compat_timeval pr_utime; /* User time */
	struct compat_timeval pr_stime; /* System time */
	struct compat_timeval pr_cutime;/* Cumulative user time */
	struct compat_timeval pr_cstime;/* Cumulative system time */
	elf_gregset_t pr_reg;	/* GP registers */
	int pr_fpvalid;		/* True if math co-processor being used.  */
};

#define elf_prpsinfo elf_prpsinfo32
struct elf_prpsinfo32
{
	char	pr_state;	/* numeric process state */
	char	pr_sname;	/* char for pr_state */
	char	pr_zomb;	/* zombie */
	char	pr_nice;	/* nice val */
	unsigned int pr_flag;	/* flags */
	__kernel_uid_t	pr_uid;
	__kernel_gid_t	pr_gid;
	pid_t	pr_pid, pr_ppid, pr_pgrp, pr_sid;
	/* Lots missing */
	char	pr_fname[16];	/* filename of executable */
	char	pr_psargs[ELF_PRARGSZ]; /* initial part of arg list */
};

#define elf_caddr_t	u32
#define init_elf_binfmt init_elf32_binfmt

#define jiffies_to_timeval jiffies_to_compat_timeval
static inline void
jiffies_to_compat_timeval(unsigned long jiffies, struct compat_timeval *value)
{
	/*
	 * Convert jiffies to nanoseconds and separate with
	 * one divide.
	 */
	u64 nsec = (u64)jiffies * TICK_NSEC;
	u32 rem;
	value->tv_sec = div_u64_rem(nsec, NSEC_PER_SEC, &rem);
	value->tv_usec = rem / NSEC_PER_USEC;
}

void elf32_core_copy_regs(elf_gregset_t grp, struct pt_regs *regs)
{
	int i;

	for (i = 0; i < EF_R0; i++)
		grp[i] = 0;
	grp[EF_R0] = 0;
	for (i = 1; i <= 31; i++)
		grp[EF_R0 + i] = (elf_greg_t) regs->regs[i];
	grp[EF_R26] = 0;
	grp[EF_R27] = 0;
	grp[EF_LO] = (elf_greg_t) regs->lo;
	grp[EF_HI] = (elf_greg_t) regs->hi;
	grp[EF_CP0_EPC] = (elf_greg_t) regs->cp0_epc;
	grp[EF_CP0_BADVADDR] = (elf_greg_t) regs->cp0_badvaddr;
	grp[EF_CP0_STATUS] = (elf_greg_t) regs->cp0_status;
	grp[EF_CP0_CAUSE] = (elf_greg_t) regs->cp0_cause;
#ifdef EF_UNUSED0
	grp[EF_UNUSED0] = 0;
#endif
}

MODULE_DESCRIPTION("Binary format loader for compatibility with o32 Linux/MIPS binaries");
MODULE_AUTHOR("Ralf Baechle (ralf@linux-mips.org)");

#undef MODULE_DESCRIPTION
#undef MODULE_AUTHOR

#undef TASK_SIZE
#define TASK_SIZE TASK_SIZE32

#include "../../../fs/binfmt_elf.c"
