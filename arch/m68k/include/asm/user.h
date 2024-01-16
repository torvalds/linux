/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_USER_H
#define _M68K_USER_H

/* Core file format: The core file is written in such a way that gdb
   can understand it and provide useful information to the user (under
   linux we use the 'trad-core' bfd).  There are quite a number of
   obstacles to being able to view the contents of the floating point
   registers, and until these are solved you will not be able to view the
   contents of them.  Actually, you can read in the core file and look at
   the contents of the user struct to find out what the floating point
   registers contain.
   The actual file contents are as follows:
   UPAGE: 1 page consisting of a user struct that tells gdb what is present
   in the file.  Directly after this is a copy of the task_struct, which
   is currently not used by gdb, but it may come in useful at some point.
   All of the registers are stored as part of the upage.  The upage should
   always be only one page.
   DATA: The data area is stored.  We use current->end_text to
   current->brk to pick up all of the user variables, plus any memory
   that may have been malloced.  No attempt is made to determine if a page
   is demand-zero or if a page is totally unused, we just cover the entire
   range.  All of the addresses are rounded in such a way that an integral
   number of pages is written.
   STACK: We need the stack information in order to get a meaningful
   backtrace.  We need to write the data from (esp) to
   current->start_stack, so we round each of these off in order to be able
   to write an integer number of pages.
   The minimum core file size is 3 pages, or 12288 bytes.
*/

struct user_m68kfp_struct {
	unsigned long  fpregs[8*3];	/* fp0-fp7 registers */
	unsigned long  fpcntl[3];	/* fp control regs */
};

/* This is the old layout of "struct pt_regs" as of Linux 1.x, and
   is still the layout used by user (the new pt_regs doesn't have
   all registers). */
struct user_regs_struct {
	long d1,d2,d3,d4,d5,d6,d7;
	long a0,a1,a2,a3,a4,a5,a6;
	long d0;
	long usp;
	long orig_d0;
	short stkadj;
	short sr;
	long pc;
	short fmtvec;
	short __fill;
};


/* When the kernel dumps core, it starts by dumping the user struct -
   this will be used by gdb to figure out where the data and stack segments
   are within the file, and what virtual addresses to use. */
struct user{
/* We start with the registers, to mimic the way that "memory" is returned
   from the ptrace(3,...) function.  */
  struct user_regs_struct regs;	/* Where the registers are actually stored */
/* ptrace does not yet supply these.  Someday.... */
  int u_fpvalid;		/* True if math co-processor being used. */
                                /* for this mess. Not yet used. */
  struct user_m68kfp_struct m68kfp; /* Math Co-processor registers. */
/* The rest of this junk is to help gdb figure out what goes where */
  unsigned long int u_tsize;	/* Text segment size (pages). */
  unsigned long int u_dsize;	/* Data segment size (pages). */
  unsigned long int u_ssize;	/* Stack segment size (pages). */
  unsigned long start_code;     /* Starting virtual address of text. */
  unsigned long start_stack;	/* Starting virtual address of stack area.
				   This is actually the bottom of the stack,
				   the top of the stack is always found in the
				   esp register.  */
  long int signal;		/* Signal that caused the core dump. */
  int reserved;			/* No longer used */
  unsigned long u_ar0;		/* Used by gdb to help find the values for */
				/* the registers. */
  struct user_m68kfp_struct* u_fpstate;	/* Math Co-processor pointer. */
  unsigned long magic;		/* To uniquely identify a core file */
  char u_comm[32];		/* User command that was responsible */
};

#endif
