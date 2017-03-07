/*
 *    ptrace cpu depend helper functions
 *
 *  Copyright 2003, 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/linkage.h>
#include <linux/sched/signal.h>
#include <asm/ptrace.h>

#define BREAKINST 0x5730 /* trapa #3 */

/* disable singlestep */
void user_disable_single_step(struct task_struct *child)
{
	if ((long)child->thread.breakinfo.addr != -1L) {
		*(child->thread.breakinfo.addr) = child->thread.breakinfo.inst;
		child->thread.breakinfo.addr = (unsigned short *)-1L;
	}
}

/* calculate next pc */
enum jump_type {none,	 /* normal instruction */
		jabs,	 /* absolute address jump */
		ind,	 /* indirect address jump */
		ret,	 /* return to subrutine */
		reg,	 /* register indexed jump */
		relb,	 /* pc relative jump (byte offset) */
		relw,	 /* pc relative jump (word offset) */
	       };

/* opcode decode table define
   ptn: opcode pattern
   msk: opcode bitmask
   len: instruction length (<0 next table index)
   jmp: jump operation mode */
struct optable {
	unsigned char bitpattern;
	unsigned char bitmask;
	signed char length;
	signed char type;
} __packed __aligned(1);

#define OPTABLE(ptn, msk, len, jmp)	\
	{				\
		.bitpattern = ptn,	\
		.bitmask    = msk,	\
		.length	    = len,	\
		.type	    = jmp,	\
	}

static const struct optable optable_0[] = {
	OPTABLE(0x00, 0xff,  1, none), /* 0x00 */
	OPTABLE(0x01, 0xff, -1, none), /* 0x01 */
	OPTABLE(0x02, 0xfe,  1, none), /* 0x02-0x03 */
	OPTABLE(0x04, 0xee,  1, none), /* 0x04-0x05/0x14-0x15 */
	OPTABLE(0x06, 0xfe,  1, none), /* 0x06-0x07 */
	OPTABLE(0x08, 0xea,  1, none), /* 0x08-0x09/0x0c-0x0d/0x18-0x19/0x1c-0x1d */
	OPTABLE(0x0a, 0xee,  1, none), /* 0x0a-0x0b/0x1a-0x1b */
	OPTABLE(0x0e, 0xee,  1, none), /* 0x0e-0x0f/0x1e-0x1f */
	OPTABLE(0x10, 0xfc,  1, none), /* 0x10-0x13 */
	OPTABLE(0x16, 0xfe,  1, none), /* 0x16-0x17 */
	OPTABLE(0x20, 0xe0,  1, none), /* 0x20-0x3f */
	OPTABLE(0x40, 0xf0,  1, relb), /* 0x40-0x4f */
	OPTABLE(0x50, 0xfc,  1, none), /* 0x50-0x53 */
	OPTABLE(0x54, 0xfd,  1, ret), /* 0x54/0x56 */
	OPTABLE(0x55, 0xff,  1, relb), /* 0x55 */
	OPTABLE(0x57, 0xff,  1, none), /* 0x57 */
	OPTABLE(0x58, 0xfb,  2, relw), /* 0x58/0x5c */
	OPTABLE(0x59, 0xfb,  1, reg), /* 0x59/0x5b */
	OPTABLE(0x5a, 0xfb,  2, jabs), /* 0x5a/0x5e */
	OPTABLE(0x5b, 0xfb,  2, ind), /* 0x5b/0x5f */
	OPTABLE(0x60, 0xe8,  1, none), /* 0x60-0x67/0x70-0x77 */
	OPTABLE(0x68, 0xfa,  1, none), /* 0x68-0x69/0x6c-0x6d */
	OPTABLE(0x6a, 0xfe, -2, none), /* 0x6a-0x6b */
	OPTABLE(0x6e, 0xfe,  2, none), /* 0x6e-0x6f */
	OPTABLE(0x78, 0xff,  4, none), /* 0x78 */
	OPTABLE(0x79, 0xff,  2, none), /* 0x79 */
	OPTABLE(0x7a, 0xff,  3, none), /* 0x7a */
	OPTABLE(0x7b, 0xff,  2, none), /* 0x7b */
	OPTABLE(0x7c, 0xfc,  2, none), /* 0x7c-0x7f */
	OPTABLE(0x80, 0x80,  1, none), /* 0x80-0xff */
};

static const struct optable optable_1[] = {
	OPTABLE(0x00, 0xff, -3, none), /* 0x0100 */
	OPTABLE(0x40, 0xf0, -3, none), /* 0x0140-0x14f */
	OPTABLE(0x80, 0xf0,  1, none), /* 0x0180-0x018f */
	OPTABLE(0xc0, 0xc0,  2, none), /* 0x01c0-0x01ff */
};

static const struct optable optable_2[] = {
	OPTABLE(0x00, 0x20,  2, none), /* 0x6a0?/0x6a8?/0x6b0?/0x6b8? */
	OPTABLE(0x20, 0x20,  3, none), /* 0x6a2?/0x6aa?/0x6b2?/0x6ba? */
};

static const struct optable optable_3[] = {
	OPTABLE(0x69, 0xfb,  2, none), /* 0x010069/0x01006d/014069/0x01406d */
	OPTABLE(0x6b, 0xff, -4, none), /* 0x01006b/0x01406b */
	OPTABLE(0x6f, 0xff,  3, none), /* 0x01006f/0x01406f */
	OPTABLE(0x78, 0xff,  5, none), /* 0x010078/0x014078 */
};

static const struct optable optable_4[] = {
/* 0x0100690?/0x01006d0?/0140690?/0x01406d0?/
   0x0100698?/0x01006d8?/0140698?/0x01406d8? */
	OPTABLE(0x00, 0x78, 3, none),
/* 0x0100692?/0x01006d2?/0140692?/0x01406d2?/
   0x010069a?/0x01006da?/014069a?/0x01406da? */
	OPTABLE(0x20, 0x78, 4, none),
};

static const struct optables_list {
	const struct optable *ptr;
	int size;
} optables[] = {
#define OPTABLES(no)                                                   \
	{                                                              \
		.ptr  = optable_##no,                                  \
		.size = sizeof(optable_##no) / sizeof(struct optable), \
	}
	OPTABLES(0),
	OPTABLES(1),
	OPTABLES(2),
	OPTABLES(3),
	OPTABLES(4),

};

const unsigned char condmask[] = {
	0x00, 0x40, 0x01, 0x04, 0x02, 0x08, 0x10, 0x20
};

static int isbranch(struct task_struct *task, int reson)
{
	unsigned char cond = h8300_get_reg(task, PT_CCR);

	/* encode complex conditions */
	/* B4: N^V
	   B5: Z|(N^V)
	   B6: C|Z */
	__asm__("bld #3,%w0\n\t"
		"bxor #1,%w0\n\t"
		"bst #4,%w0\n\t"
		"bor #2,%w0\n\t"
		"bst #5,%w0\n\t"
		"bld #2,%w0\n\t"
		"bor #0,%w0\n\t"
		"bst #6,%w0\n\t"
		: "=&r"(cond) : "0"(cond) : "cc");
	cond &= condmask[reson >> 1];
	if (!(reson & 1))
		return cond == 0;
	else
		return cond != 0;
}

static unsigned short *decode(struct task_struct *child,
			      const struct optable *op,
			      char *fetch_p, unsigned short *pc,
			      unsigned char inst)
{
	unsigned long addr;
	unsigned long *sp;
	int regno;

	switch (op->type) {
	case none:
		return (unsigned short *)pc + op->length;
	case jabs:
		addr = *(unsigned long *)pc;
		return (unsigned short *)(addr & 0x00ffffff);
	case ind:
		addr = *pc & 0xff;
		return (unsigned short *)(*(unsigned long *)addr);
	case ret:
		sp = (unsigned long *)h8300_get_reg(child, PT_USP);
		/* user stack frames
		   |   er0  | temporary saved
		   +--------+
		   |   exp  | exception stack frames
		   +--------+
		   | ret pc | userspace return address
		*/
		return (unsigned short *)(*(sp+2) & 0x00ffffff);
	case reg:
		regno = (*pc >> 4) & 0x07;
		if (regno == 0)
			addr = h8300_get_reg(child, PT_ER0);
		else
			addr = h8300_get_reg(child, regno-1 + PT_ER1);
		return (unsigned short *)addr;
	case relb:
		if (inst == 0x55 || isbranch(child, inst & 0x0f))
			pc = (unsigned short *)((unsigned long)pc +
						((signed char)(*fetch_p)));
		return pc+1; /* skip myself */
	case relw:
		if (inst == 0x5c || isbranch(child, (*fetch_p & 0xf0) >> 4))
			pc = (unsigned short *)((unsigned long)pc +
						((signed short)(*(pc+1))));
		return pc+2; /* skip myself */
	default:
		return NULL;
	}
}

static unsigned short *nextpc(struct task_struct *child, unsigned short *pc)
{
	const struct optable *op;
	unsigned char *fetch_p;
	int op_len;
	unsigned char inst;

	op = optables[0].ptr;
	op_len = optables[0].size;
	fetch_p = (unsigned char *)pc;
	inst = *fetch_p++;
	do {
		if ((inst & op->bitmask) == op->bitpattern) {
			if (op->length < 0) {
				op = optables[-op->length].ptr;
				op_len = optables[-op->length].size + 1;
				inst = *fetch_p++;
			} else
				return decode(child, op, fetch_p, pc, inst);
		} else
			op++;
	} while (--op_len > 0);
	return NULL;
}

/* Set breakpoint(s) to simulate a single step from the current PC.  */

void user_enable_single_step(struct task_struct *child)
{
	unsigned short *next;

	next = nextpc(child, (unsigned short *)h8300_get_reg(child, PT_PC));
	child->thread.breakinfo.addr = next;
	child->thread.breakinfo.inst = *next;
	*next = BREAKINST;
}

asmlinkage void trace_trap(unsigned long bp)
{
	if ((unsigned long)current->thread.breakinfo.addr == bp) {
		user_disable_single_step(current);
		force_sig(SIGTRAP, current);
	} else
		force_sig(SIGILL, current);
}
