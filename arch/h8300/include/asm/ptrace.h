#ifndef _H8300_PTRACE_H
#define _H8300_PTRACE_H

#ifndef __ASSEMBLY__

#define PT_ER1	   0
#define PT_ER2	   1
#define PT_ER3	   2
#define PT_ER4	   3
#define PT_ER5	   4
#define PT_ER6	   5
#define PT_ER0	   6
#define PT_ORIG_ER0	   7
#define PT_CCR	   8
#define PT_PC	   9
#define PT_USP	   10
#define PT_EXR     12

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	long     retpc;
	long     er4;
	long     er5;
	long     er6;
	long     er3;
	long     er2;
	long     er1;
	long     orig_er0;
	unsigned short ccr;
	long     er0;
	long     vector;
#if defined(CONFIG_CPU_H8S)
	unsigned short exr;
#endif
	unsigned long  pc;
} __attribute__((aligned(2),packed));

#define PTRACE_GETREGS            12
#define PTRACE_SETREGS            13

#ifdef __KERNEL__
#ifndef PS_S
#define PS_S  (0x10)
#endif

#if defined(__H8300H__)
#define H8300_REGS_NO 11
#endif
#if defined(__H8300S__)
#define H8300_REGS_NO 12
#endif

/* Find the stack offset for a register, relative to thread.esp0. */
#define PT_REG(reg)	((long)&((struct pt_regs *)0)->reg)

#define arch_has_single_step()	(1)

#define user_mode(regs) (!((regs)->ccr & PS_S))
#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)
extern void show_regs(struct pt_regs *);
#endif /* __KERNEL__ */
#endif /* __ASSEMBLY__ */
#endif /* _H8300_PTRACE_H */
