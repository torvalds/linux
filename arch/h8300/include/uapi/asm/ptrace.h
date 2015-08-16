#ifndef _UAPI_H8300_PTRACE_H
#define _UAPI_H8300_PTRACE_H

#ifndef __ASSEMBLY__

#define PT_ER1	   0
#define PT_ER2	   1
#define PT_ER3	   2
#define PT_ER4	   3
#define PT_ER5	   4
#define PT_ER6	   5
#define PT_ER0	   6
#define PT_USP	   7
#define PT_ORIG_ER0	   8
#define PT_CCR	   9
#define PT_PC	   10
#define PT_EXR     11

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
	long	 sp;
	unsigned short	 ccr;
	long     er0;
	long     vector;
#if defined(__H8300S__)
	unsigned short	 exr;
#endif
	unsigned long  pc;
} __attribute__((aligned(2), packed));

#endif /* __ASSEMBLY__ */
#endif /* _UAPI_H8300_PTRACE_H */
