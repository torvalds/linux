/*
 *  S390 version
 *    Copyright IBM Corp. 1999, 2000
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */
#ifndef _S390_PTRACE_H
#define _S390_PTRACE_H

#include <uapi/asm/ptrace.h>

#define PIF_SYSCALL		0	/* inside a system call */
#define PIF_PER_TRAP		1	/* deliver sigtrap on return to user */

#define _PIF_SYSCALL		(1<<PIF_SYSCALL)
#define _PIF_PER_TRAP		(1<<PIF_PER_TRAP)

#ifndef __ASSEMBLY__

#define PSW_KERNEL_BITS	(PSW_DEFAULT_KEY | PSW_MASK_BASE | PSW_ASC_HOME | \
			 PSW_MASK_EA | PSW_MASK_BA)
#define PSW_USER_BITS	(PSW_MASK_DAT | PSW_MASK_IO | PSW_MASK_EXT | \
			 PSW_DEFAULT_KEY | PSW_MASK_BASE | PSW_MASK_MCHECK | \
			 PSW_MASK_PSTATE | PSW_ASC_PRIMARY)

struct psw_bits {
	unsigned long long	: 1;
	unsigned long long r	: 1; /* PER-Mask */
	unsigned long long	: 3;
	unsigned long long t	: 1; /* DAT Mode */
	unsigned long long i	: 1; /* Input/Output Mask */
	unsigned long long e	: 1; /* External Mask */
	unsigned long long key	: 4; /* PSW Key */
	unsigned long long	: 1;
	unsigned long long m	: 1; /* Machine-Check Mask */
	unsigned long long w	: 1; /* Wait State */
	unsigned long long p	: 1; /* Problem State */
	unsigned long long as	: 2; /* Address Space Control */
	unsigned long long cc	: 2; /* Condition Code */
	unsigned long long pm	: 4; /* Program Mask */
	unsigned long long ri	: 1; /* Runtime Instrumentation */
	unsigned long long	: 6;
	unsigned long long eaba : 2; /* Addressing Mode */
	unsigned long long	: 31;
	unsigned long long ia	: 64;/* Instruction Address */
};

enum {
	PSW_AMODE_24BIT = 0,
	PSW_AMODE_31BIT = 1,
	PSW_AMODE_64BIT = 3
};

enum {
	PSW_AS_PRIMARY	 = 0,
	PSW_AS_ACCREG	 = 1,
	PSW_AS_SECONDARY = 2,
	PSW_AS_HOME	 = 3
};

#define psw_bits(__psw) (*({			\
	typecheck(psw_t, __psw);		\
	&(*(struct psw_bits *)(&(__psw)));	\
}))

/*
 * The pt_regs struct defines the way the registers are stored on
 * the stack during a system call.
 */
struct pt_regs 
{
	unsigned long args[1];
	psw_t psw;
	unsigned long gprs[NUM_GPRS];
	unsigned long orig_gpr2;
	unsigned int int_code;
	unsigned int int_parm;
	unsigned long int_parm_long;
	unsigned long flags;
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
	regs->flags |= (1U << flag);
}

static inline void clear_pt_regs_flag(struct pt_regs *regs, int flag)
{
	regs->flags &= ~(1U << flag);
}

static inline int test_pt_regs_flag(struct pt_regs *regs, int flag)
{
	return !!(regs->flags & (1U << flag));
}

/*
 * These are defined as per linux/ptrace.h, which see.
 */
#define arch_has_single_step()	(1)
#define arch_has_block_step()	(1)

#define user_mode(regs) (((regs)->psw.mask & PSW_MASK_PSTATE) != 0)
#define instruction_pointer(regs) ((regs)->psw.addr & PSW_ADDR_INSN)
#define user_stack_pointer(regs)((regs)->gprs[15])
#define profile_pc(regs) instruction_pointer(regs)

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->gprs[2];
}

static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	regs->psw.addr = val | PSW_ADDR_AMODE;
}

int regs_query_register_offset(const char *name);
const char *regs_query_register_name(unsigned int offset);
unsigned long regs_get_register(struct pt_regs *regs, unsigned int offset);
unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n);

static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->gprs[15] & PSW_ADDR_INSN;
}

#endif /* __ASSEMBLY__ */
#endif /* _S390_PTRACE_H */
