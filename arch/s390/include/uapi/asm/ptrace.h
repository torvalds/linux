/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#ifndef _UAPI_S390_PTRACE_H
#define _UAPI_S390_PTRACE_H

#include <linux/const.h>

/*
 * Offsets in the user_regs_struct. They are used for the ptrace
 * system call and in entry.S
 */
#ifndef __s390x__

#define PT_PSWMASK  0x00
#define PT_PSWADDR  0x04
#define PT_GPR0     0x08
#define PT_GPR1     0x0C
#define PT_GPR2     0x10
#define PT_GPR3     0x14
#define PT_GPR4     0x18
#define PT_GPR5     0x1C
#define PT_GPR6     0x20
#define PT_GPR7     0x24
#define PT_GPR8     0x28
#define PT_GPR9     0x2C
#define PT_GPR10    0x30
#define PT_GPR11    0x34
#define PT_GPR12    0x38
#define PT_GPR13    0x3C
#define PT_GPR14    0x40
#define PT_GPR15    0x44
#define PT_ACR0     0x48
#define PT_ACR1     0x4C
#define PT_ACR2     0x50
#define PT_ACR3     0x54
#define PT_ACR4	    0x58
#define PT_ACR5	    0x5C
#define PT_ACR6	    0x60
#define PT_ACR7	    0x64
#define PT_ACR8	    0x68
#define PT_ACR9	    0x6C
#define PT_ACR10    0x70
#define PT_ACR11    0x74
#define PT_ACR12    0x78
#define PT_ACR13    0x7C
#define PT_ACR14    0x80
#define PT_ACR15    0x84
#define PT_ORIGGPR2 0x88
#define PT_FPC	    0x90
/*
 * A nasty fact of life that the ptrace api
 * only supports passing of longs.
 */
#define PT_FPR0_HI  0x98
#define PT_FPR0_LO  0x9C
#define PT_FPR1_HI  0xA0
#define PT_FPR1_LO  0xA4
#define PT_FPR2_HI  0xA8
#define PT_FPR2_LO  0xAC
#define PT_FPR3_HI  0xB0
#define PT_FPR3_LO  0xB4
#define PT_FPR4_HI  0xB8
#define PT_FPR4_LO  0xBC
#define PT_FPR5_HI  0xC0
#define PT_FPR5_LO  0xC4
#define PT_FPR6_HI  0xC8
#define PT_FPR6_LO  0xCC
#define PT_FPR7_HI  0xD0
#define PT_FPR7_LO  0xD4
#define PT_FPR8_HI  0xD8
#define PT_FPR8_LO  0XDC
#define PT_FPR9_HI  0xE0
#define PT_FPR9_LO  0xE4
#define PT_FPR10_HI 0xE8
#define PT_FPR10_LO 0xEC
#define PT_FPR11_HI 0xF0
#define PT_FPR11_LO 0xF4
#define PT_FPR12_HI 0xF8
#define PT_FPR12_LO 0xFC
#define PT_FPR13_HI 0x100
#define PT_FPR13_LO 0x104
#define PT_FPR14_HI 0x108
#define PT_FPR14_LO 0x10C
#define PT_FPR15_HI 0x110
#define PT_FPR15_LO 0x114
#define PT_CR_9	    0x118
#define PT_CR_10    0x11C
#define PT_CR_11    0x120
#define PT_IEEE_IP  0x13C
#define PT_LASTOFF  PT_IEEE_IP
#define PT_ENDREGS  0x140-1

#define GPR_SIZE	4
#define CR_SIZE		4

#define STACK_FRAME_OVERHEAD	96	/* size of minimum stack frame */

#else /* __s390x__ */

#define PT_PSWMASK  0x00
#define PT_PSWADDR  0x08
#define PT_GPR0     0x10
#define PT_GPR1     0x18
#define PT_GPR2     0x20
#define PT_GPR3     0x28
#define PT_GPR4     0x30
#define PT_GPR5     0x38
#define PT_GPR6     0x40
#define PT_GPR7     0x48
#define PT_GPR8     0x50
#define PT_GPR9     0x58
#define PT_GPR10    0x60
#define PT_GPR11    0x68
#define PT_GPR12    0x70
#define PT_GPR13    0x78
#define PT_GPR14    0x80
#define PT_GPR15    0x88
#define PT_ACR0     0x90
#define PT_ACR1     0x94
#define PT_ACR2     0x98
#define PT_ACR3     0x9C
#define PT_ACR4	    0xA0
#define PT_ACR5	    0xA4
#define PT_ACR6	    0xA8
#define PT_ACR7	    0xAC
#define PT_ACR8	    0xB0
#define PT_ACR9	    0xB4
#define PT_ACR10    0xB8
#define PT_ACR11    0xBC
#define PT_ACR12    0xC0
#define PT_ACR13    0xC4
#define PT_ACR14    0xC8
#define PT_ACR15    0xCC
#define PT_ORIGGPR2 0xD0
#define PT_FPC	    0xD8
#define PT_FPR0     0xE0
#define PT_FPR1     0xE8
#define PT_FPR2     0xF0
#define PT_FPR3     0xF8
#define PT_FPR4     0x100
#define PT_FPR5     0x108
#define PT_FPR6     0x110
#define PT_FPR7     0x118
#define PT_FPR8     0x120
#define PT_FPR9     0x128
#define PT_FPR10    0x130
#define PT_FPR11    0x138
#define PT_FPR12    0x140
#define PT_FPR13    0x148
#define PT_FPR14    0x150
#define PT_FPR15    0x158
#define PT_CR_9     0x160
#define PT_CR_10    0x168
#define PT_CR_11    0x170
#define PT_IEEE_IP  0x1A8
#define PT_LASTOFF  PT_IEEE_IP
#define PT_ENDREGS  0x1B0-1

#define GPR_SIZE	8
#define CR_SIZE		8

#define STACK_FRAME_OVERHEAD	160	 /* size of minimum stack frame */

#endif /* __s390x__ */

#ifndef __s390x__

#define PSW_MASK_PER		_AC(0x40000000, UL)
#define PSW_MASK_DAT		_AC(0x04000000, UL)
#define PSW_MASK_IO		_AC(0x02000000, UL)
#define PSW_MASK_EXT		_AC(0x01000000, UL)
#define PSW_MASK_KEY		_AC(0x00F00000, UL)
#define PSW_MASK_BASE		_AC(0x00080000, UL)	/* always one */
#define PSW_MASK_MCHECK		_AC(0x00040000, UL)
#define PSW_MASK_WAIT		_AC(0x00020000, UL)
#define PSW_MASK_PSTATE		_AC(0x00010000, UL)
#define PSW_MASK_ASC		_AC(0x0000C000, UL)
#define PSW_MASK_CC		_AC(0x00003000, UL)
#define PSW_MASK_PM		_AC(0x00000F00, UL)
#define PSW_MASK_RI		_AC(0x00000000, UL)
#define PSW_MASK_EA		_AC(0x00000000, UL)
#define PSW_MASK_BA		_AC(0x00000000, UL)

#define PSW_MASK_USER		_AC(0x0000FF00, UL)

#define PSW_ADDR_AMODE		_AC(0x80000000, UL)
#define PSW_ADDR_INSN		_AC(0x7FFFFFFF, UL)

#define PSW_ASC_PRIMARY		_AC(0x00000000, UL)
#define PSW_ASC_ACCREG		_AC(0x00004000, UL)
#define PSW_ASC_SECONDARY	_AC(0x00008000, UL)
#define PSW_ASC_HOME		_AC(0x0000C000, UL)

#else /* __s390x__ */

#define PSW_MASK_PER		_AC(0x4000000000000000, UL)
#define PSW_MASK_DAT		_AC(0x0400000000000000, UL)
#define PSW_MASK_IO		_AC(0x0200000000000000, UL)
#define PSW_MASK_EXT		_AC(0x0100000000000000, UL)
#define PSW_MASK_BASE		_AC(0x0000000000000000, UL)
#define PSW_MASK_KEY		_AC(0x00F0000000000000, UL)
#define PSW_MASK_MCHECK		_AC(0x0004000000000000, UL)
#define PSW_MASK_WAIT		_AC(0x0002000000000000, UL)
#define PSW_MASK_PSTATE		_AC(0x0001000000000000, UL)
#define PSW_MASK_ASC		_AC(0x0000C00000000000, UL)
#define PSW_MASK_CC		_AC(0x0000300000000000, UL)
#define PSW_MASK_PM		_AC(0x00000F0000000000, UL)
#define PSW_MASK_RI		_AC(0x0000008000000000, UL)
#define PSW_MASK_EA		_AC(0x0000000100000000, UL)
#define PSW_MASK_BA		_AC(0x0000000080000000, UL)

#define PSW_MASK_USER		_AC(0x0000FF0180000000, UL)

#define PSW_ADDR_AMODE		_AC(0x0000000000000000, UL)
#define PSW_ADDR_INSN		_AC(0xFFFFFFFFFFFFFFFF, UL)

#define PSW_ASC_PRIMARY		_AC(0x0000000000000000, UL)
#define PSW_ASC_ACCREG		_AC(0x0000400000000000, UL)
#define PSW_ASC_SECONDARY	_AC(0x0000800000000000, UL)
#define PSW_ASC_HOME		_AC(0x0000C00000000000, UL)

#endif /* __s390x__ */

#define NUM_GPRS	16
#define NUM_FPRS	16
#define NUM_CRS		16
#define NUM_ACRS	16

#define NUM_CR_WORDS	3

#define FPR_SIZE	8
#define FPC_SIZE	4
#define FPC_PAD_SIZE	4 /* gcc insists on aligning the fpregs */
#define ACR_SIZE	4


#define PTRACE_OLDSETOPTIONS		21
#define PTRACE_SYSEMU			31
#define PTRACE_SYSEMU_SINGLESTEP	32

#ifndef __ASSEMBLER__
#include <linux/stddef.h>
#include <linux/types.h>

typedef union {
	float	f;
	double	d;
	__u64	ui;
	struct
	{
		__u32 hi;
		__u32 lo;
	} fp;
} freg_t;

typedef struct {
	__u32	fpc;
	__u32	pad;
	freg_t	fprs[NUM_FPRS];
} s390_fp_regs;

#define FPC_EXCEPTION_MASK	0xF8000000
#define FPC_FLAGS_MASK		0x00F80000
#define FPC_DXC_MASK		0x0000FF00
#define FPC_RM_MASK		0x00000003

/* this typedef defines how a Program Status Word looks like */
typedef struct {
	unsigned long mask;
	unsigned long addr;
} __attribute__ ((aligned(8))) psw_t;

/*
 * The s390_regs structure is used to define the elf_gregset_t.
 */
typedef struct {
	psw_t psw;
	unsigned long gprs[NUM_GPRS];
	unsigned int  acrs[NUM_ACRS];
	unsigned long orig_gpr2;
} s390_regs;

/*
 * The user_pt_regs structure exports the beginning of
 * the in-kernel pt_regs structure to user space.
 */
typedef struct {
	unsigned long args[1];
	psw_t psw;
	unsigned long gprs[NUM_GPRS];
} user_pt_regs;

/*
 * Now for the user space program event recording (trace) definitions.
 * The following structures are used only for the ptrace interface, don't
 * touch or even look at it if you don't want to modify the user-space
 * ptrace interface. In particular stay away from it for in-kernel PER.
 */
typedef struct {
	unsigned long cr[NUM_CR_WORDS];
} per_cr_words;

#define PER_EM_MASK 0xE8000000UL

typedef struct {
#ifdef __s390x__
	unsigned		       : 32;
#endif /* __s390x__ */
	unsigned em_branching	       : 1;
	unsigned em_instruction_fetch  : 1;
	/*
	 * Switching on storage alteration automatically fixes
	 * the storage alteration event bit in the users std.
	 */
	unsigned em_storage_alteration : 1;
	unsigned em_gpr_alt_unused     : 1;
	unsigned em_store_real_address : 1;
	unsigned		       : 3;
	unsigned branch_addr_ctl       : 1;
	unsigned		       : 1;
	unsigned storage_alt_space_ctl : 1;
	unsigned		       : 21;
	unsigned long starting_addr;
	unsigned long ending_addr;
} per_cr_bits;

typedef struct {
	unsigned short perc_atmid;
	unsigned long address;
	unsigned char access_id;
} per_lowcore_words;

typedef struct {
	unsigned perc_branching		 : 1;
	unsigned perc_instruction_fetch  : 1;
	unsigned perc_storage_alteration : 1;
	unsigned perc_gpr_alt_unused	 : 1;
	unsigned perc_store_real_address : 1;
	unsigned			 : 3;
	unsigned atmid_psw_bit_31	 : 1;
	unsigned atmid_validity_bit	 : 1;
	unsigned atmid_psw_bit_32	 : 1;
	unsigned atmid_psw_bit_5	 : 1;
	unsigned atmid_psw_bit_16	 : 1;
	unsigned atmid_psw_bit_17	 : 1;
	unsigned si			 : 2;
	unsigned long address;
	unsigned			 : 4;
	unsigned access_id		 : 4;
} per_lowcore_bits;

typedef struct {
	union {
		per_cr_words   words;
		per_cr_bits    bits;
	} control_regs;
	/*
	 * The single_step and instruction_fetch bits are obsolete,
	 * the kernel always sets them to zero. To enable single
	 * stepping use ptrace(PTRACE_SINGLESTEP) instead.
	 */
	unsigned  single_step	    : 1;
	unsigned  instruction_fetch : 1;
	unsigned		    : 30;
	/*
	 * These addresses are copied into cr10 & cr11 if single
	 * stepping is switched off
	 */
	unsigned long starting_addr;
	unsigned long ending_addr;
	union {
		per_lowcore_words words;
		per_lowcore_bits  bits;
	} lowcore;
} per_struct;

typedef struct {
	unsigned int  len;
	unsigned long kernel_addr;
	unsigned long process_addr;
} ptrace_area;

/*
 * S/390 specific non posix ptrace requests. I chose unusual values so
 * they are unlikely to clash with future ptrace definitions.
 */
#define PTRACE_PEEKUSR_AREA	      0x5000
#define PTRACE_POKEUSR_AREA	      0x5001
#define PTRACE_PEEKTEXT_AREA	      0x5002
#define PTRACE_PEEKDATA_AREA	      0x5003
#define PTRACE_POKETEXT_AREA	      0x5004
#define PTRACE_POKEDATA_AREA	      0x5005
#define PTRACE_GET_LAST_BREAK	      0x5006
#define PTRACE_PEEK_SYSTEM_CALL       0x5007
#define PTRACE_POKE_SYSTEM_CALL	      0x5008
#define PTRACE_ENABLE_TE	      0x5009
#define PTRACE_DISABLE_TE	      0x5010
#define PTRACE_TE_ABORT_RAND	      0x5011

/*
 * The numbers chosen here are somewhat arbitrary but absolutely MUST
 * not overlap with any of the number assigned in <linux/ptrace.h>.
 */
#define PTRACE_SINGLEBLOCK	12	/* resume execution until next branch */

/*
 * PT_PROT definition is loosely based on hppa bsd definition in
 * gdb/hppab-nat.c
 */
#define PTRACE_PROT			  21

typedef enum {
	ptprot_set_access_watchpoint,
	ptprot_set_write_watchpoint,
	ptprot_disable_watchpoint
} ptprot_flags;

typedef struct {
	unsigned long lowaddr;
	unsigned long hiaddr;
	ptprot_flags prot;
} ptprot_area;

/* Sequence of bytes for breakpoint illegal instruction.  */
#define S390_BREAKPOINT     {0x0,0x1}
#define S390_BREAKPOINT_U16 ((__u16)0x0001)
#define S390_SYSCALL_OPCODE ((__u16)0x0a00)
#define S390_SYSCALL_SIZE   2

/*
 * The user_regs_struct defines the way the user registers are
 * store on the stack for signal handling.
 */
struct user_regs_struct {
	psw_t psw;
	unsigned long gprs[NUM_GPRS];
	unsigned int  acrs[NUM_ACRS];
	unsigned long orig_gpr2;
	s390_fp_regs fp_regs;
	/*
	 * These per registers are in here so that gdb can modify them
	 * itself as there is no "official" ptrace interface for hardware
	 * watchpoints. This is the way intel does it.
	 */
	per_struct per_info;
	unsigned long ieee_instruction_pointer;	/* obsolete, always 0 */
};

#endif /* __ASSEMBLER__ */

#endif /* _UAPI_S390_PTRACE_H */
