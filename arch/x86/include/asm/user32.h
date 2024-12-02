/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_USER32_H
#define _ASM_X86_USER32_H

/* IA32 compatible user structures for ptrace.
 * These should be used for 32bit coredumps too. */

struct user_i387_ia32_struct {
	u32	cwd;
	u32	swd;
	u32	twd;
	u32	fip;
	u32	fcs;
	u32	foo;
	u32	fos;
	u32	st_space[20];   /* 8*10 bytes for each FP-reg = 80 bytes */
};

/* FSAVE frame with extensions */
struct user32_fxsr_struct {
	unsigned short	cwd;
	unsigned short	swd;
	unsigned short	twd;	/* not compatible to 64bit twd */
	unsigned short	fop;
	int	fip;
	int	fcs;
	int	foo;
	int	fos;
	int	mxcsr;
	int	reserved;
	int	st_space[32];	/* 8*16 bytes for each FP-reg = 128 bytes */
	int	xmm_space[32];	/* 8*16 bytes for each XMM-reg = 128 bytes */
	int	padding[56];
};

struct user_regs_struct32 {
	__u32 ebx, ecx, edx, esi, edi, ebp, eax;
	unsigned short ds, __ds, es, __es;
	unsigned short fs, __fs, gs, __gs;
	__u32 orig_eax, eip;
	unsigned short cs, __cs;
	__u32 eflags, esp;
	unsigned short ss, __ss;
};

struct user32 {
  struct user_regs_struct32 regs; /* Where the registers are actually stored */
  int u_fpvalid;		/* True if math co-processor being used. */
				/* for this mess. Not yet used. */
  struct user_i387_ia32_struct i387;	/* Math Co-processor registers. */
/* The rest of this junk is to help gdb figure out what goes where */
  __u32 u_tsize;	/* Text segment size (pages). */
  __u32 u_dsize;	/* Data segment size (pages). */
  __u32 u_ssize;	/* Stack segment size (pages). */
  __u32 start_code;     /* Starting virtual address of text. */
  __u32 start_stack;	/* Starting virtual address of stack area.
				   This is actually the bottom of the stack,
				   the top of the stack is always found in the
				   esp register.  */
  __u32 signal;     		/* Signal that caused the core dump. */
  int reserved;			/* No __u32er used */
  __u32 u_ar0;	/* Used by gdb to help find the values for */
				/* the registers. */
  __u32 u_fpstate;	/* Math Co-processor pointer. */
  __u32 magic;		/* To uniquely identify a core file */
  char u_comm[32];		/* User command that was responsible */
  int u_debugreg[8];
};


#endif /* _ASM_X86_USER32_H */
