#ifndef _PTRACE32_H
#define _PTRACE32_H

#include "compat_linux.h"  /* needed for psw_compat_t */

typedef struct {
	__u32 cr[3];
} per_cr_words32;

typedef struct {
	__u16          perc_atmid;          /* 0x096 */
	__u32          address;             /* 0x098 */
	__u8           access_id;           /* 0x0a1 */
} per_lowcore_words32;

typedef struct {
	union {
		per_cr_words32   words;
	} control_regs;
	/*
	 * Use these flags instead of setting em_instruction_fetch
	 * directly they are used so that single stepping can be
	 * switched on & off while not affecting other tracing
	 */
	unsigned  single_step       : 1;
	unsigned  instruction_fetch : 1;
	unsigned                    : 30;
	/*
	 * These addresses are copied into cr10 & cr11 if single
	 * stepping is switched off
	 */
	__u32     starting_addr;
	__u32     ending_addr;
	union {
		per_lowcore_words32 words;
	} lowcore; 
} per_struct32;

struct user_regs_struct32
{
	psw_compat_t psw;
	u32 gprs[NUM_GPRS];
	u32 acrs[NUM_ACRS];
	u32 orig_gpr2;
	/* nb: there's a 4-byte hole here */
	s390_fp_regs fp_regs;
	/*
	 * These per registers are in here so that gdb can modify them
	 * itself as there is no "official" ptrace interface for hardware
	 * watchpoints. This is the way intel does it.
	 */
	per_struct32 per_info;
	u32  ieee_instruction_pointer; 
	/* Used to give failing instruction back to user for ieee exceptions */
};

struct user32 {
	/* We start with the registers, to mimic the way that "memory"
	   is returned from the ptrace(3,...) function.  */
	struct user_regs_struct32 regs; /* Where the registers are actually stored */
	/* The rest of this junk is to help gdb figure out what goes where */
	u32 u_tsize;		/* Text segment size (pages). */
	u32 u_dsize;	        /* Data segment size (pages). */
	u32 u_ssize;	        /* Stack segment size (pages). */
	u32 start_code;         /* Starting virtual address of text. */
	u32 start_stack;	/* Starting virtual address of stack area.
				   This is actually the bottom of the stack,
				   the top of the stack is always found in the
				   esp register.  */
	s32 signal;     	 /* Signal that caused the core dump. */
	u32 u_ar0;               /* Used by gdb to help find the values for */
	                         /* the registers. */
	u32 magic;		 /* To uniquely identify a core file */
	char u_comm[32];	 /* User command that was responsible */
};

typedef struct
{
	__u32   len;
	__u32   kernel_addr;
	__u32   process_addr;
} ptrace_area_emu31;

#endif /* _PTRACE32_H */
