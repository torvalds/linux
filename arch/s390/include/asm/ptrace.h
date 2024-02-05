/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */
#ifndef _S390_PTRACE_H
#define _S390_PTRACE_H

#include <linux/bits.h>
#include <uapi/asm/ptrace.h>
#include <asm/tpi.h>

#define PIF_SYSCALL			0	/* inside a system call */
#define PIF_EXECVE_PGSTE_RESTART	1	/* restart execve for PGSTE binaries */
#define PIF_SYSCALL_RET_SET		2	/* return value was set via ptrace */
#define PIF_GUEST_FAULT			3	/* indicates program check in sie64a */
#define PIF_FTRACE_FULL_REGS		4	/* all register contents valid (ftrace) */

#define _PIF_SYSCALL			BIT(PIF_SYSCALL)
#define _PIF_EXECVE_PGSTE_RESTART	BIT(PIF_EXECVE_PGSTE_RESTART)
#define _PIF_SYSCALL_RET_SET		BIT(PIF_SYSCALL_RET_SET)
#define _PIF_GUEST_FAULT		BIT(PIF_GUEST_FAULT)
#define _PIF_FTRACE_FULL_REGS		BIT(PIF_FTRACE_FULL_REGS)

#define PSW32_MASK_PER		_AC(0x40000000, UL)
#define PSW32_MASK_DAT		_AC(0x04000000, UL)
#define PSW32_MASK_IO		_AC(0x02000000, UL)
#define PSW32_MASK_EXT		_AC(0x01000000, UL)
#define PSW32_MASK_KEY		_AC(0x00F00000, UL)
#define PSW32_MASK_BASE		_AC(0x00080000, UL)	/* Always one */
#define PSW32_MASK_MCHECK	_AC(0x00040000, UL)
#define PSW32_MASK_WAIT		_AC(0x00020000, UL)
#define PSW32_MASK_PSTATE	_AC(0x00010000, UL)
#define PSW32_MASK_ASC		_AC(0x0000C000, UL)
#define PSW32_MASK_CC		_AC(0x00003000, UL)
#define PSW32_MASK_PM		_AC(0x00000f00, UL)
#define PSW32_MASK_RI		_AC(0x00000080, UL)

#define PSW32_ADDR_AMODE	_AC(0x80000000, UL)
#define PSW32_ADDR_INSN		_AC(0x7FFFFFFF, UL)

#define PSW32_DEFAULT_KEY	((PAGE_DEFAULT_ACC) << 20)

#define PSW32_ASC_PRIMARY	_AC(0x00000000, UL)
#define PSW32_ASC_ACCREG	_AC(0x00004000, UL)
#define PSW32_ASC_SECONDARY	_AC(0x00008000, UL)
#define PSW32_ASC_HOME		_AC(0x0000C000, UL)

#define PSW_DEFAULT_KEY			((PAGE_DEFAULT_ACC) << 52)

#define PSW_KERNEL_BITS	(PSW_DEFAULT_KEY | PSW_MASK_BASE | PSW_ASC_HOME | \
			 PSW_MASK_EA | PSW_MASK_BA | PSW_MASK_DAT)
#define PSW_USER_BITS	(PSW_MASK_DAT | PSW_MASK_IO | PSW_MASK_EXT | \
			 PSW_DEFAULT_KEY | PSW_MASK_BASE | PSW_MASK_MCHECK | \
			 PSW_MASK_PSTATE | PSW_ASC_PRIMARY)

#ifndef __ASSEMBLY__

struct psw_bits {
	unsigned long	     :	1;
	unsigned long per    :	1; /* PER-Mask */
	unsigned long	     :	3;
	unsigned long dat    :	1; /* DAT Mode */
	unsigned long io     :	1; /* Input/Output Mask */
	unsigned long ext    :	1; /* External Mask */
	unsigned long key    :	4; /* PSW Key */
	unsigned long	     :	1;
	unsigned long mcheck :	1; /* Machine-Check Mask */
	unsigned long wait   :	1; /* Wait State */
	unsigned long pstate :	1; /* Problem State */
	unsigned long as     :	2; /* Address Space Control */
	unsigned long cc     :	2; /* Condition Code */
	unsigned long pm     :	4; /* Program Mask */
	unsigned long ri     :	1; /* Runtime Instrumentation */
	unsigned long	     :	6;
	unsigned long eaba   :	2; /* Addressing Mode */
	unsigned long	     : 31;
	unsigned long ia     : 64; /* Instruction Address */
};

enum {
	PSW_BITS_AMODE_24BIT = 0,
	PSW_BITS_AMODE_31BIT = 1,
	PSW_BITS_AMODE_64BIT = 3
};

enum {
	PSW_BITS_AS_PRIMARY	= 0,
	PSW_BITS_AS_ACCREG	= 1,
	PSW_BITS_AS_SECONDARY	= 2,
	PSW_BITS_AS_HOME	= 3
};

#define psw_bits(__psw) (*({			\
	typecheck(psw_t, __psw);		\
	&(*(struct psw_bits *)(&(__psw)));	\
}))

typedef struct {
	unsigned int mask;
	unsigned int addr;
} psw_t32 __aligned(8);

#define PGM_INT_CODE_MASK	0x7f
#define PGM_INT_CODE_PER	0x80

/*
 * The pt_regs struct defines the way the registers are stored on
 * the stack during a system call.
 */
struct pt_regs {
	union {
		user_pt_regs user_regs;
		struct {
			unsigned long args[1];
			psw_t psw;
			unsigned long gprs[NUM_GPRS];
		};
	};
	unsigned long orig_gpr2;
	union {
		struct {
			unsigned int int_code;
			unsigned int int_parm;
			unsigned long int_parm_long;
		};
		struct tpi_info tpi_info;
	};
	unsigned long flags;
	unsigned long cr1;
	unsigned long last_break;
};

/*
 * Program event recording (PER) register set.
 */
struct per_regs {
	unsigned long control;		/* PER control bits */
	unsigned long start;		/* PER starting address */
	unsigned long end;		/* PER ending address */
};

/*
 * PER event contains information about the cause of the last PER exception.
 */
struct per_event {
	unsigned short cause;		/* PER code, ATMID and AI */
	unsigned long address;		/* PER address */
	unsigned char paid;		/* PER access identification */
};

/*
 * Simplified per_info structure used to decode the ptrace user space ABI.
 */
struct per_struct_kernel {
	unsigned long cr9;		/* PER control bits */
	unsigned long cr10;		/* PER starting address */
	unsigned long cr11;		/* PER ending address */
	unsigned long bits;		/* Obsolete software bits */
	unsigned long starting_addr;	/* User specified start address */
	unsigned long ending_addr;	/* User specified end address */
	unsigned short perc_atmid;	/* PER trap ATMID */
	unsigned long address;		/* PER trap instruction address */
	unsigned char access_id;	/* PER trap access identification */
};

#define PER_EVENT_MASK			0xEB000000UL

#define PER_EVENT_BRANCH		0x80000000UL
#define PER_EVENT_IFETCH		0x40000000UL
#define PER_EVENT_STORE			0x20000000UL
#define PER_EVENT_STORE_REAL		0x08000000UL
#define PER_EVENT_TRANSACTION_END	0x02000000UL
#define PER_EVENT_NULLIFICATION		0x01000000UL

#define PER_CONTROL_MASK		0x00e00000UL

#define PER_CONTROL_BRANCH_ADDRESS	0x00800000UL
#define PER_CONTROL_SUSPENSION		0x00400000UL
#define PER_CONTROL_ALTERATION		0x00200000UL

static inline void set_pt_regs_flag(struct pt_regs *regs, int flag)
{
	regs->flags |= (1UL << flag);
}

static inline void clear_pt_regs_flag(struct pt_regs *regs, int flag)
{
	regs->flags &= ~(1UL << flag);
}

static inline int test_pt_regs_flag(struct pt_regs *regs, int flag)
{
	return !!(regs->flags & (1UL << flag));
}

static inline int test_and_clear_pt_regs_flag(struct pt_regs *regs, int flag)
{
	int ret = test_pt_regs_flag(regs, flag);

	clear_pt_regs_flag(regs, flag);
	return ret;
}

struct task_struct;

void update_cr_regs(struct task_struct *task);

/*
 * These are defined as per linux/ptrace.h, which see.
 */
#define arch_has_single_step()	(1)
#define arch_has_block_step()	(1)

#define user_mode(regs) (((regs)->psw.mask & PSW_MASK_PSTATE) != 0)
#define instruction_pointer(regs) ((regs)->psw.addr)
#define user_stack_pointer(regs)((regs)->gprs[15])
#define profile_pc(regs) instruction_pointer(regs)

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->gprs[2];
}

static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	regs->psw.addr = val;
}

int regs_query_register_offset(const char *name);
const char *regs_query_register_name(unsigned int offset);
unsigned long regs_get_register(struct pt_regs *regs, unsigned int offset);
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n);

/**
 * regs_get_kernel_argument() - get Nth function argument in kernel
 * @regs:	pt_regs of that context
 * @n:		function argument number (start from 0)
 *
 * regs_get_kernel_argument() returns @n th argument of the function call.
 */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
						     unsigned int n)
{
	unsigned int argoffset = STACK_FRAME_OVERHEAD / sizeof(long);

#define NR_REG_ARGUMENTS 5
	if (n < NR_REG_ARGUMENTS)
		return regs_get_register(regs, 2 + n);
	n -= NR_REG_ARGUMENTS;
	return regs_get_kernel_stack_nth(regs, argoffset + n);
}

static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->gprs[15];
}

static inline void regs_set_return_value(struct pt_regs *regs, unsigned long rc)
{
	regs->gprs[2] = rc;
}

#endif /* __ASSEMBLY__ */
#endif /* _S390_PTRACE_H */
