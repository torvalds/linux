// SPDX-License-Identifier: GPL-2.0
/*
 * Support for o32 Linux/MIPS ELF binaries.
 * Author: Ralf Baechle (ralf@linux-mips.org)
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
#define elf_check_arch elfo32_check_arch

#ifdef CONFIG_KVM_GUEST
#define TASK32_SIZE		0x3fff8000UL
#else
#define TASK32_SIZE		0x7fff8000UL
#endif
#undef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE		(TASK32_SIZE / 3 * 2)

#include <asm/processor.h>

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
	struct old_timeval32 pr_utime; /* User time */
	struct old_timeval32 pr_stime; /* System time */
	struct old_timeval32 pr_cutime;/* Cumulative user time */
	struct old_timeval32 pr_cstime;/* Cumulative system time */
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

#define jiffies_to_timeval jiffies_to_old_timeval32
static inline void
jiffies_to_old_timeval32(unsigned long jiffies, struct old_timeval32 *value)
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

#undef TASK_SIZE
#define TASK_SIZE TASK_SIZE32

#undef ns_to_kernel_old_timeval
#define ns_to_kernel_old_timeval ns_to_old_timeval32

/*
 * Some data types as stored in coredump.
 */
#define user_long_t             compat_long_t
#define user_siginfo_t          compat_siginfo_t
#define copy_siginfo_to_external        copy_siginfo_to_external32

#include "../../../fs/binfmt_elf.c"
