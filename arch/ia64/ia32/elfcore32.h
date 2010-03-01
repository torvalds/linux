/*
 * IA-32 ELF core dump support.
 *
 * Copyright (C) 2003 Arun Sharma <arun.sharma@intel.com>
 *
 * Derived from the x86_64 version
 */
#ifndef _ELFCORE32_H_
#define _ELFCORE32_H_

#include <asm/intrinsics.h>
#include <asm/uaccess.h>

/* Override elfcore.h */
#define _LINUX_ELFCORE_H 1
typedef unsigned int elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct32) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct ia32_user_i387_struct elf_fpregset_t;
typedef struct ia32_user_fxsr_struct elf_fpxregset_t;

struct elf_siginfo
{
	int	si_signo;			/* signal number */
	int	si_code;			/* extra code */
	int	si_errno;			/* errno */
};

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
/*
 * Hacks are here since types between compat_timeval (= pair of s32) and
 * ia64-native timeval (= pair of s64) are not compatible, at least a file
 * arch/ia64/ia32/../../../fs/binfmt_elf.c will get warnings from compiler on
 * use of cputime_to_timeval(), which usually an alias of jiffies_to_timeval().
 */
#define cputime_to_timeval(a,b) \
	do { (b)->tv_usec = 0; (b)->tv_sec = (a)/NSEC_PER_SEC; } while(0)
#else
#define jiffies_to_timeval(a,b) \
	do { (b)->tv_usec = 0; (b)->tv_sec = (a)/HZ; } while(0)
#endif

struct elf_prstatus
{
	struct elf_siginfo pr_info;	/* Info associated with signal */
	short	pr_cursig;		/* Current signal */
	unsigned int pr_sigpend;	/* Set of pending signals */
	unsigned int pr_sighold;	/* Set of held signals */
	pid_t	pr_pid;
	pid_t	pr_ppid;
	pid_t	pr_pgrp;
	pid_t	pr_sid;
	struct compat_timeval pr_utime;	/* User time */
	struct compat_timeval pr_stime;	/* System time */
	struct compat_timeval pr_cutime;	/* Cumulative user time */
	struct compat_timeval pr_cstime;	/* Cumulative system time */
	elf_gregset_t pr_reg;	/* GP registers */
	int pr_fpvalid;		/* True if math co-processor being used.  */
};

#define ELF_PRARGSZ	(80)	/* Number of chars for args */

struct elf_prpsinfo
{
	char	pr_state;	/* numeric process state */
	char	pr_sname;	/* char for pr_state */
	char	pr_zomb;	/* zombie */
	char	pr_nice;	/* nice val */
	unsigned int pr_flag;	/* flags */
	__u16	pr_uid;
	__u16	pr_gid;
	pid_t	pr_pid, pr_ppid, pr_pgrp, pr_sid;
	/* Lots missing */
	char	pr_fname[16];	/* filename of executable */
	char	pr_psargs[ELF_PRARGSZ];	/* initial part of arg list */
};

#define ELF_CORE_COPY_REGS(pr_reg, regs)       		\
	pr_reg[0] = regs->r11;				\
	pr_reg[1] = regs->r9;				\
	pr_reg[2] = regs->r10;				\
	pr_reg[3] = regs->r14;				\
	pr_reg[4] = regs->r15;				\
	pr_reg[5] = regs->r13;				\
	pr_reg[6] = regs->r8;				\
	pr_reg[7] = regs->r16 & 0xffff;			\
	pr_reg[8] = (regs->r16 >> 16) & 0xffff;		\
	pr_reg[9] = (regs->r16 >> 32) & 0xffff;		\
	pr_reg[10] = (regs->r16 >> 48) & 0xffff;	\
	pr_reg[11] = regs->r1; 				\
	pr_reg[12] = regs->cr_iip;			\
	pr_reg[13] = regs->r17 & 0xffff;		\
	pr_reg[14] = ia64_getreg(_IA64_REG_AR_EFLAG);	\
	pr_reg[15] = regs->r12;				\
	pr_reg[16] = (regs->r17 >> 16) & 0xffff;

static inline void elf_core_copy_regs(elf_gregset_t *elfregs,
				      struct pt_regs *regs)
{
	ELF_CORE_COPY_REGS((*elfregs), regs)
}

static inline int elf_core_copy_task_regs(struct task_struct *t,
					  elf_gregset_t* elfregs)
{
	ELF_CORE_COPY_REGS((*elfregs), task_pt_regs(t));
	return 1;
}

static inline int
elf_core_copy_task_fpregs(struct task_struct *tsk, struct pt_regs *regs, elf_fpregset_t *fpu)
{
	struct ia32_user_i387_struct *fpstate = (void*)fpu;
	mm_segment_t old_fs;

	if (!tsk_used_math(tsk))
		return 0;
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	save_ia32_fpstate(tsk, (struct ia32_user_i387_struct __user *) fpstate);
	set_fs(old_fs);

	return 1;
}

#define ELF_CORE_COPY_XFPREGS 1
#define ELF_CORE_XFPREG_TYPE NT_PRXFPREG
static inline int
elf_core_copy_task_xfpregs(struct task_struct *tsk, elf_fpxregset_t *xfpu)
{
	struct ia32_user_fxsr_struct *fpxstate = (void*) xfpu;
	mm_segment_t old_fs;

	if (!tsk_used_math(tsk))
		return 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	save_ia32_fpxstate(tsk, (struct ia32_user_fxsr_struct __user *) fpxstate);
	set_fs(old_fs);

	return 1;
}

#endif /* _ELFCORE32_H_ */
