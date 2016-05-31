/* written by Philipp Rumpf, Copyright (C) 1999 SuSE GmbH Nuernberg
** Copyright (C) 2000 Grant Grundler, Hewlett-Packard
*/
#ifndef _UAPI_PARISC_PTRACE_H
#define _UAPI_PARISC_PTRACE_H


#include <linux/types.h>

/* This struct defines the way the registers are stored on the 
 * stack during a system call.
 *
 * N.B. gdb/strace care about the size and offsets within this
 * structure. If you change things, you may break object compatibility
 * for those applications.
 *
 * Please do NOT use this structure for future programs, but use
 * user_regs_struct (see below) instead.
 *
 * It can be accessed through PTRACE_PEEKUSR/PTRACE_POKEUSR only.
 */

struct pt_regs {
	unsigned long gr[32];	/* PSW is in gr[0] */
	__u64 fr[32];
	unsigned long sr[ 8];
	unsigned long iasq[2];
	unsigned long iaoq[2];
	unsigned long cr27;
	unsigned long pad0;     /* available for other uses */
	unsigned long orig_r28;
	unsigned long ksp;
	unsigned long kpc;
	unsigned long sar;	/* CR11 */
	unsigned long iir;	/* CR19 */
	unsigned long isr;	/* CR20 */
	unsigned long ior;	/* CR21 */
	unsigned long ipsw;	/* CR22 */
};

/**
 * struct user_regs_struct - User general purpose registers
 *
 * This is the user-visible general purpose register state structure
 * which is used to define the elf_gregset_t.
 *
 * It can be accessed through PTRACE_GETREGSET with NT_PRSTATUS
 * and through PTRACE_GETREGS.
 */
struct user_regs_struct {
	unsigned long gr[32];	/* PSW is in gr[0] */
	unsigned long sr[8];
	unsigned long iaoq[2];
	unsigned long iasq[2];
	unsigned long sar;	/* CR11 */
	unsigned long iir;	/* CR19 */
	unsigned long isr;	/* CR20 */
	unsigned long ior;	/* CR21 */
	unsigned long ipsw;	/* CR22 */
	unsigned long cr0;
	unsigned long cr24, cr25, cr26, cr27, cr28, cr29, cr30, cr31;
	unsigned long cr8, cr9, cr12, cr13, cr10, cr15;
	unsigned long _pad[80-64];	/* pad to ELF_NGREG (80) */
};

/**
 * struct user_fp_struct - User floating point registers
 *
 * This is the user-visible floating point register state structure.
 * It uses the same layout and size as elf_fpregset_t.
 *
 * It can be accessed through PTRACE_GETREGSET with NT_PRFPREG
 * and through PTRACE_GETFPREGS.
 */
struct user_fp_struct {
	__u64 fr[32];
};


/*
 * The numbers chosen here are somewhat arbitrary but absolutely MUST
 * not overlap with any of the number assigned in <linux/ptrace.h>.
 *
 * These ones are taken from IA-64 on the assumption that theirs are
 * the most correct (and we also want to support PTRACE_SINGLEBLOCK
 * since we have taken branch traps too)
 */
#define PTRACE_SINGLEBLOCK	12	/* resume execution until next branch */

#define PTRACE_GETREGS		18
#define PTRACE_SETREGS		19
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15

#endif /* _UAPI_PARISC_PTRACE_H */
