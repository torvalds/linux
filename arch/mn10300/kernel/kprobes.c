/* MN10300 Kernel probes implementation
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by Mark Salter (msalter@redhat.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public Licence as published by
 * the Free Software Foundation; either version 2 of the Licence, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public Licence
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <linux/kdebug.h>
#include <asm/cacheflush.h>

struct kretprobe_blackpoint kretprobe_blacklist[] = { { NULL, NULL } };
const int kretprobe_blacklist_size = ARRAY_SIZE(kretprobe_blacklist);

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned long current_kprobe_orig_pc;
static unsigned long current_kprobe_next_pc;
static int current_kprobe_ss_flags;
static unsigned long kprobe_status;
static kprobe_opcode_t current_kprobe_ss_buf[MAX_INSN_SIZE + 2];
static unsigned long current_kprobe_bp_addr;

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;


/* singlestep flag bits */
#define SINGLESTEP_BRANCH 1
#define SINGLESTEP_PCREL  2

#define READ_BYTE(p, valp) \
	do { *(u8 *)(valp) = *(u8 *)(p); } while (0)

#define READ_WORD16(p, valp)					\
	do {							\
		READ_BYTE((p), (valp));				\
		READ_BYTE((u8 *)(p) + 1, (u8 *)(valp) + 1);	\
	} while (0)

#define READ_WORD32(p, valp)					\
	do {							\
		READ_BYTE((p), (valp));				\
		READ_BYTE((u8 *)(p) + 1, (u8 *)(valp) + 1);	\
		READ_BYTE((u8 *)(p) + 2, (u8 *)(valp) + 2);	\
		READ_BYTE((u8 *)(p) + 3, (u8 *)(valp) + 3);	\
	} while (0)


static const u8 mn10300_insn_sizes[256] =
{
	/* 1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
	1, 3, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3, 1, 3, 3, 3,	/* 0 */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 1 */
	2, 2, 2, 2, 3, 3, 3, 3, 2, 2, 2, 2, 3, 3, 3, 3, /* 2 */
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1, 1, 1, /* 3 */
	1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, /* 4 */
	1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, /* 5 */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 6 */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 7 */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* 8 */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* 9 */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* a */
	2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, /* b */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 2, /* c */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* d */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* e */
	0, 2, 2, 2, 2, 2, 2, 4, 0, 3, 0, 4, 0, 6, 7, 1  /* f */
};

#define LT (1 << 0)
#define GT (1 << 1)
#define GE (1 << 2)
#define LE (1 << 3)
#define CS (1 << 4)
#define HI (1 << 5)
#define CC (1 << 6)
#define LS (1 << 7)
#define EQ (1 << 8)
#define NE (1 << 9)
#define RA (1 << 10)
#define VC (1 << 11)
#define VS (1 << 12)
#define NC (1 << 13)
#define NS (1 << 14)

static const u16 cond_table[] = {
	/*  V  C  N  Z  */
	/*  0  0  0  0  */ (NE | NC | CC | VC | GE | GT | HI),
	/*  0  0  0  1  */ (EQ | NC | CC | VC | GE | LE | LS),
	/*  0  0  1  0  */ (NE | NS | CC | VC | LT | LE | HI),
	/*  0  0  1  1  */ (EQ | NS | CC | VC | LT | LE | LS),
	/*  0  1  0  0  */ (NE | NC | CS | VC | GE | GT | LS),
	/*  0  1  0  1  */ (EQ | NC | CS | VC | GE | LE | LS),
	/*  0  1  1  0  */ (NE | NS | CS | VC | LT | LE | LS),
	/*  0  1  1  1  */ (EQ | NS | CS | VC | LT | LE | LS),
	/*  1  0  0  0  */ (NE | NC | CC | VS | LT | LE | HI),
	/*  1  0  0  1  */ (EQ | NC | CC | VS | LT | LE | LS),
	/*  1  0  1  0  */ (NE | NS | CC | VS | GE | GT | HI),
	/*  1  0  1  1  */ (EQ | NS | CC | VS | GE | LE | LS),
	/*  1  1  0  0  */ (NE | NC | CS | VS | LT | LE | LS),
	/*  1  1  0  1  */ (EQ | NC | CS | VS | LT | LE | LS),
	/*  1  1  1  0  */ (NE | NS | CS | VS | GE | GT | LS),
	/*  1  1  1  1  */ (EQ | NS | CS | VS | GE | LE | LS),
};

/*
 * Calculate what the PC will be after executing next instruction
 */
static unsigned find_nextpc(struct pt_regs *regs, int *flags)
{
	unsigned size;
	s8  x8;
	s16 x16;
	s32 x32;
	u8 opc, *pc, *sp, *next;

	next = 0;
	*flags = SINGLESTEP_PCREL;

	pc = (u8 *) regs->pc;
	sp = (u8 *) (regs + 1);
	opc = *pc;

	size = mn10300_insn_sizes[opc];
	if (size > 0) {
		next = pc + size;
	} else {
		switch (opc) {
			/* Bxx (d8,PC) */
		case 0xc0 ... 0xca:
			x8 = 2;
			if (cond_table[regs->epsw & 0xf] & (1 << (opc & 0xf)))
				x8 = (s8)pc[1];
			next = pc + x8;
			*flags |= SINGLESTEP_BRANCH;
			break;

			/* JMP (d16,PC) or CALL (d16,PC) */
		case 0xcc:
		case 0xcd:
			READ_WORD16(pc + 1, &x16);
			next = pc + x16;
			*flags |= SINGLESTEP_BRANCH;
			break;

			/* JMP (d32,PC) or CALL (d32,PC) */
		case 0xdc:
		case 0xdd:
			READ_WORD32(pc + 1, &x32);
			next = pc + x32;
			*flags |= SINGLESTEP_BRANCH;
			break;

			/* RETF */
		case 0xde:
			next = (u8 *)regs->mdr;
			*flags &= ~SINGLESTEP_PCREL;
			*flags |= SINGLESTEP_BRANCH;
			break;

			/* RET */
		case 0xdf:
			sp += pc[2];
			READ_WORD32(sp, &x32);
			next = (u8 *)x32;
			*flags &= ~SINGLESTEP_PCREL;
			*flags |= SINGLESTEP_BRANCH;
			break;

		case 0xf0:
			next = pc + 2;
			opc = pc[1];
			if (opc >= 0xf0 && opc <= 0xf7) {
				/* JMP (An) / CALLS (An) */
				switch (opc & 3) {
				case 0:
					next = (u8 *)regs->a0;
					break;
				case 1:
					next = (u8 *)regs->a1;
					break;
				case 2:
					next = (u8 *)regs->a2;
					break;
				case 3:
					next = (u8 *)regs->a3;
					break;
				}
				*flags &= ~SINGLESTEP_PCREL;
				*flags |= SINGLESTEP_BRANCH;
			} else if (opc == 0xfc) {
				/* RETS */
				READ_WORD32(sp, &x32);
				next = (u8 *)x32;
				*flags &= ~SINGLESTEP_PCREL;
				*flags |= SINGLESTEP_BRANCH;
			} else if (opc == 0xfd) {
				/* RTI */
				READ_WORD32(sp + 4, &x32);
				next = (u8 *)x32;
				*flags &= ~SINGLESTEP_PCREL;
				*flags |= SINGLESTEP_BRANCH;
			}
			break;

			/* potential 3-byte conditional branches */
		case 0xf8:
			next = pc + 3;
			opc = pc[1];
			if (opc >= 0xe8 && opc <= 0xeb &&
			    (cond_table[regs->epsw & 0xf] &
			     (1 << ((opc & 0xf) + 3)))
			    ) {
				READ_BYTE(pc+2, &x8);
				next = pc + x8;
				*flags |= SINGLESTEP_BRANCH;
			}
			break;

		case 0xfa:
			if (pc[1] == 0xff) {
				/* CALLS (d16,PC) */
				READ_WORD16(pc + 2, &x16);
				next = pc + x16;
			} else
				next = pc + 4;
			*flags |= SINGLESTEP_BRANCH;
			break;

		case 0xfc:
			x32 = 6;
			if (pc[1] == 0xff) {
				/* CALLS (d32,PC) */
				READ_WORD32(pc + 2, &x32);
			}
			next = pc + x32;
			*flags |= SINGLESTEP_BRANCH;
			break;
			/* LXX (d8,PC) */
			/* SETLB - loads the next four bytes into the LIR reg */
		case 0xd0 ... 0xda:
		case 0xdb:
			panic("Can't singlestep Lxx/SETLB\n");
			break;
		}
	}
	return (unsigned)next;

}

/*
 * set up out of place singlestep of some branching instructions
 */
static unsigned __kprobes singlestep_branch_setup(struct pt_regs *regs)
{
	u8 opc, *pc, *sp, *next;

	next = NULL;
	pc = (u8 *) regs->pc;
	sp = (u8 *) (regs + 1);

	switch (pc[0]) {
	case 0xc0 ... 0xca:	/* Bxx (d8,PC) */
	case 0xcc:		/* JMP (d16,PC) */
	case 0xdc:		/* JMP (d32,PC) */
	case 0xf8:              /* Bxx (d8,PC)  3-byte version */
		/* don't really need to do anything except cause trap  */
		next = pc;
		break;

	case 0xcd:		/* CALL (d16,PC) */
		pc[1] = 5;
		pc[2] = 0;
		next = pc + 5;
		break;

	case 0xdd:		/* CALL (d32,PC) */
		pc[1] = 7;
		pc[2] = 0;
		pc[3] = 0;
		pc[4] = 0;
		next = pc + 7;
		break;

	case 0xde:		/* RETF */
		next = pc + 3;
		regs->mdr = (unsigned) next;
		break;

	case 0xdf:		/* RET */
		sp += pc[2];
		next = pc + 3;
		*(unsigned *)sp = (unsigned) next;
		break;

	case 0xf0:
		next = pc + 2;
		opc = pc[1];
		if (opc >= 0xf0 && opc <= 0xf3) {
			/* CALLS (An) */
			/* use CALLS (d16,PC) to avoid mucking with An */
			pc[0] = 0xfa;
			pc[1] = 0xff;
			pc[2] = 4;
			pc[3] = 0;
			next = pc + 4;
		} else if (opc >= 0xf4 && opc <= 0xf7) {
			/* JMP (An) */
			next = pc;
		} else if (opc == 0xfc) {
			/* RETS */
			next = pc + 2;
			*(unsigned *) sp = (unsigned) next;
		} else if (opc == 0xfd) {
			/* RTI */
			next = pc + 2;
			*(unsigned *)(sp + 4) = (unsigned) next;
		}
		break;

	case 0xfa:	/* CALLS (d16,PC) */
		pc[2] = 4;
		pc[3] = 0;
		next = pc + 4;
		break;

	case 0xfc:	/* CALLS (d32,PC) */
		pc[2] = 6;
		pc[3] = 0;
		pc[4] = 0;
		pc[5] = 0;
		next = pc + 6;
		break;

	case 0xd0 ... 0xda:	/* LXX (d8,PC) */
	case 0xdb:		/* SETLB */
		panic("Can't singlestep Lxx/SETLB\n");
	}

	return (unsigned) next;
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	return 0;
}

void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE);
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	*p->addr = BREAKPOINT_INSTRUCTION;
	flush_icache_range((unsigned long) p->addr,
			   (unsigned long) p->addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	mn10300_dcache_flush();
	mn10300_icache_inv();
}

void arch_remove_kprobe(struct kprobe *p)
{
}

static inline
void __kprobes disarm_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	*p->addr = p->opcode;
	regs->pc = (unsigned long) p->addr;
	mn10300_dcache_flush();
	mn10300_icache_inv();
}

static inline
void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long nextpc;

	current_kprobe_orig_pc = regs->pc;
	memcpy(current_kprobe_ss_buf, &p->ainsn.insn[0], MAX_INSN_SIZE);
	regs->pc = (unsigned long) current_kprobe_ss_buf;

	nextpc = find_nextpc(regs, &current_kprobe_ss_flags);
	if (current_kprobe_ss_flags & SINGLESTEP_PCREL)
		current_kprobe_next_pc =
			current_kprobe_orig_pc + (nextpc - regs->pc);
	else
		current_kprobe_next_pc = nextpc;

	/* branching instructions need special handling */
	if (current_kprobe_ss_flags & SINGLESTEP_BRANCH)
		nextpc = singlestep_branch_setup(regs);

	current_kprobe_bp_addr = nextpc;

	*(u8 *) nextpc = BREAKPOINT_INSTRUCTION;
	mn10300_dcache_flush_range2((unsigned) current_kprobe_ss_buf,
				    sizeof(current_kprobe_ss_buf));
	mn10300_icache_inv();
}

static inline int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	unsigned int *addr = (unsigned int *) regs->pc;

	/* We're in an interrupt, but this is clear and BUG()-safe. */
	preempt_disable();

	/* Check we're not actually recursing */
	if (kprobe_running()) {
		/* We *are* holding lock here, so this is safe.
		   Disarm the probe we just hit, and ignore it. */
		p = get_kprobe(addr);
		if (p) {
			disarm_kprobe(p, regs);
			ret = 1;
		} else {
			p = current_kprobe;
			if (p->break_handler && p->break_handler(p, regs))
				goto ss_probe;
		}
		/* If it's not ours, can't be delete race, (we hold lock). */
		goto no_kprobe;
	}

	p = get_kprobe(addr);
	if (!p) {
		if (*addr != BREAKPOINT_INSTRUCTION) {
			/* The breakpoint instruction was removed right after
			 * we hit it.  Another cpu has removed either a
			 * probepoint or a debugger breakpoint at this address.
			 * In either case, no further handling of this
			 * interrupt is appropriate.
			 */
			ret = 1;
		}
		/* Not one of ours: let kernel handle it */
		goto no_kprobe;
	}

	kprobe_status = KPROBE_HIT_ACTIVE;
	current_kprobe = p;
	if (p->pre_handler(p, regs)) {
		/* handler has already set things up, so skip ss setup */
		return 1;
	}

ss_probe:
	prepare_singlestep(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "breakpoint"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 */
static void __kprobes resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	/* we may need to fixup regs/stack after singlestepping a call insn */
	if (current_kprobe_ss_flags & SINGLESTEP_BRANCH) {
		regs->pc = current_kprobe_orig_pc;
		switch (p->ainsn.insn[0]) {
		case 0xcd:	/* CALL (d16,PC) */
			*(unsigned *) regs->sp = regs->mdr = regs->pc + 5;
			break;
		case 0xdd:	/* CALL (d32,PC) */
			/* fixup mdr and return address on stack */
			*(unsigned *) regs->sp = regs->mdr = regs->pc + 7;
			break;
		case 0xf0:
			if (p->ainsn.insn[1] >= 0xf0 &&
			    p->ainsn.insn[1] <= 0xf3) {
				/* CALLS (An) */
				/* fixup MDR and return address on stack */
				regs->mdr = regs->pc + 2;
				*(unsigned *) regs->sp = regs->mdr;
			}
			break;

		case 0xfa:	/* CALLS (d16,PC) */
			/* fixup MDR and return address on stack */
			*(unsigned *) regs->sp = regs->mdr = regs->pc + 4;
			break;

		case 0xfc:	/* CALLS (d32,PC) */
			/* fixup MDR and return address on stack */
			*(unsigned *) regs->sp = regs->mdr = regs->pc + 6;
			break;
		}
	}

	regs->pc = current_kprobe_next_pc;
	current_kprobe_bp_addr = 0;
}

static inline int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);

	resume_execution(current_kprobe, regs);
	reset_current_kprobe();
	preempt_enable_no_resched();
	return 1;
}

/* Interrupts disabled, kprobe_lock held. */
static inline
int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	if (current_kprobe->fault_handler &&
	    current_kprobe->fault_handler(current_kprobe, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		resume_execution(current_kprobe, regs);
		reset_current_kprobe();
		preempt_enable_no_resched();
	}
	return 0;
}

/*
 * Wrapper routine to for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = data;

	switch (val) {
	case DIE_BREAKPOINT:
		if (current_kprobe_bp_addr != args->regs->pc) {
			if (kprobe_handler(args->regs))
				return NOTIFY_STOP;
		} else {
			if (post_kprobe_handler(args->regs))
				return NOTIFY_STOP;
		}
		break;
	case DIE_GPF:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_STOP;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

/* Jprobes support.  */
static struct pt_regs jprobe_saved_regs;
static struct pt_regs *jprobe_saved_regs_location;
static kprobe_opcode_t jprobe_saved_stack[MAX_STACK_SIZE];

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	jprobe_saved_regs_location = regs;
	memcpy(&jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/* Save a whole stack frame, this gets arguments
	 * pushed onto the stack after using up all the
	 * arg registers.
	 */
	memcpy(&jprobe_saved_stack, regs + 1, sizeof(jprobe_saved_stack));

	/* setup return addr to the jprobe handler routine */
	regs->pc = (unsigned long) jp->entry;
	return 1;
}

void __kprobes jprobe_return(void)
{
	void *orig_sp = jprobe_saved_regs_location + 1;

	preempt_enable_no_resched();
	asm volatile("		mov	%0,sp\n"
		     ".globl	jprobe_return_bp_addr\n"
		     "jprobe_return_bp_addr:\n\t"
		     "		.byte	0xff\n"
		     : : "d" (orig_sp));
}

extern void jprobe_return_bp_addr(void);

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	u8 *addr = (u8 *) regs->pc;

	if (addr == (u8 *) jprobe_return_bp_addr) {
		if (jprobe_saved_regs_location != regs) {
			printk(KERN_ERR"JPROBE:"
			       " Current regs (%p) does not match saved regs"
			       " (%p).\n",
			       regs, jprobe_saved_regs_location);
			BUG();
		}

		/* Restore old register state.
		 */
		memcpy(regs, &jprobe_saved_regs, sizeof(struct pt_regs));

		memcpy(regs + 1, &jprobe_saved_stack,
		       sizeof(jprobe_saved_stack));
		return 1;
	}
	return 0;
}

int __init arch_init_kprobes(void)
{
	return 0;
}
