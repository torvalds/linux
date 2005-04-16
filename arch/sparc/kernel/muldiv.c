/* $Id: muldiv.c,v 1.5 1997/12/15 20:07:20 ecd Exp $
 * muldiv.c: Hardware multiply/division illegal instruction trap
 *		for sun4c/sun4 (which do not have those instructions)
 *
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 *
 * 2004-12-25	Krzysztof Helt (krzysztof.h1@wp.pl) 
 *		- fixed registers constrains in inline assembly declarations
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/* #define DEBUG_MULDIV */

static inline int has_imm13(int insn)
{
	return (insn & 0x2000);
}

static inline int is_foocc(int insn)
{
	return (insn & 0x800000);
}

static inline int sign_extend_imm13(int imm)
{
	return imm << 19 >> 19;
}

static inline void advance(struct pt_regs *regs)
{
	regs->pc   = regs->npc;
	regs->npc += 4;
}

static inline void maybe_flush_windows(unsigned int rs1, unsigned int rs2,
				       unsigned int rd)
{
	if(rs2 >= 16 || rs1 >= 16 || rd >= 16) {
		/* Wheee... */
		__asm__ __volatile__("save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "save %sp, -0x40, %sp\n\t"
				     "restore; restore; restore; restore;\n\t"
				     "restore; restore; restore;\n\t");
	}
}

#define fetch_reg(reg, regs) ({						\
	struct reg_window __user *win;					\
	register unsigned long ret;					\
									\
	if (!(reg)) ret = 0;						\
	else if ((reg) < 16) {						\
		ret = regs->u_regs[(reg)];				\
	} else {							\
		/* Ho hum, the slightly complicated case. */		\
		win = (struct reg_window __user *)regs->u_regs[UREG_FP];\
		if (get_user (ret, &win->locals[(reg) - 16])) return -1;\
	}								\
	ret;								\
})

static inline int
store_reg(unsigned int result, unsigned int reg, struct pt_regs *regs)
{
	struct reg_window __user *win;

	if (!reg)
		return 0;
	if (reg < 16) {
		regs->u_regs[reg] = result;
		return 0;
	} else {
		/* need to use put_user() in this case: */
		win = (struct reg_window __user *) regs->u_regs[UREG_FP];
		return (put_user(result, &win->locals[reg - 16]));
	}
}
		
extern void handle_hw_divzero (struct pt_regs *regs, unsigned long pc,
			       unsigned long npc, unsigned long psr);

/* Should return 0 if mul/div emulation succeeded and SIGILL should
 * not be issued.
 */
int do_user_muldiv(struct pt_regs *regs, unsigned long pc)
{
	unsigned int insn;
	int inst;
	unsigned int rs1, rs2, rdv;

	if (!pc)
		return -1; /* This happens to often, I think */
	if (get_user (insn, (unsigned int __user *)pc))
		return -1;
	if ((insn & 0xc1400000) != 0x80400000)
		return -1;
	inst = ((insn >> 19) & 0xf);
	if ((inst & 0xe) != 10 && (inst & 0xe) != 14)
		return -1;

	/* Now we know we have to do something with umul, smul, udiv or sdiv */
	rs1 = (insn >> 14) & 0x1f;
	rs2 = insn & 0x1f;
	rdv = (insn >> 25) & 0x1f;
	if (has_imm13(insn)) {
		maybe_flush_windows(rs1, 0, rdv);
		rs2 = sign_extend_imm13(insn);
	} else {
		maybe_flush_windows(rs1, rs2, rdv);
		rs2 = fetch_reg(rs2, regs);
	}
	rs1 = fetch_reg(rs1, regs);
	switch (inst) {
	case 10: /* umul */
#ifdef DEBUG_MULDIV	
		printk ("unsigned muldiv: 0x%x * 0x%x = ", rs1, rs2);
#endif		
		__asm__ __volatile__ ("\n\t"
			"mov	%0, %%o0\n\t"
			"call	.umul\n\t"
			" mov	%1, %%o1\n\t"
			"mov	%%o0, %0\n\t"
			"mov	%%o1, %1\n\t"
			: "=r" (rs1), "=r" (rs2)
		        : "0" (rs1), "1" (rs2)
			: "o0", "o1", "o2", "o3", "o4", "o5", "o7", "cc");
#ifdef DEBUG_MULDIV
		printk ("0x%x%08x\n", rs2, rs1);
#endif
		if (store_reg(rs1, rdv, regs))
			return -1;
		regs->y = rs2;
		break;
	case 11: /* smul */
#ifdef DEBUG_MULDIV
		printk ("signed muldiv: 0x%x * 0x%x = ", rs1, rs2);
#endif
		__asm__ __volatile__ ("\n\t"
			"mov	%0, %%o0\n\t"
			"call	.mul\n\t"
			" mov	%1, %%o1\n\t"
			"mov	%%o0, %0\n\t"
			"mov	%%o1, %1\n\t"
			: "=r" (rs1), "=r" (rs2)
		        : "0" (rs1), "1" (rs2)
			: "o0", "o1", "o2", "o3", "o4", "o5", "o7", "cc");
#ifdef DEBUG_MULDIV
		printk ("0x%x%08x\n", rs2, rs1);
#endif
		if (store_reg(rs1, rdv, regs))
			return -1;
		regs->y = rs2;
		break;
	case 14: /* udiv */
#ifdef DEBUG_MULDIV
		printk ("unsigned muldiv: 0x%x%08x / 0x%x = ", regs->y, rs1, rs2);
#endif
		if (!rs2) {
#ifdef DEBUG_MULDIV
			printk ("DIVISION BY ZERO\n");
#endif
			handle_hw_divzero (regs, pc, regs->npc, regs->psr);
			return 0;
		}
		__asm__ __volatile__ ("\n\t"
			"mov	%2, %%o0\n\t"
			"mov	%0, %%o1\n\t"
			"mov	%%g0, %%o2\n\t"
			"call	__udivdi3\n\t"
			" mov	%1, %%o3\n\t"
			"mov	%%o1, %0\n\t"
			"mov	%%o0, %1\n\t"
			: "=r" (rs1), "=r" (rs2)
			: "r" (regs->y), "0" (rs1), "1" (rs2)
			: "o0", "o1", "o2", "o3", "o4", "o5", "o7",
			  "g1", "g2", "g3", "cc");
#ifdef DEBUG_MULDIV
		printk ("0x%x\n", rs1);
#endif
		if (store_reg(rs1, rdv, regs))
			return -1;
		break;
	case 15: /* sdiv */
#ifdef DEBUG_MULDIV
		printk ("signed muldiv: 0x%x%08x / 0x%x = ", regs->y, rs1, rs2);
#endif
		if (!rs2) {
#ifdef DEBUG_MULDIV
			printk ("DIVISION BY ZERO\n");
#endif
			handle_hw_divzero (regs, pc, regs->npc, regs->psr);
			return 0;
		}
		__asm__ __volatile__ ("\n\t"
			"mov	%2, %%o0\n\t"
			"mov	%0, %%o1\n\t"
			"mov	%%g0, %%o2\n\t"
			"call	__divdi3\n\t"
			" mov	%1, %%o3\n\t"
			"mov	%%o1, %0\n\t"
			"mov	%%o0, %1\n\t"
			: "=r" (rs1), "=r" (rs2)
			: "r" (regs->y), "0" (rs1), "1" (rs2)
			: "o0", "o1", "o2", "o3", "o4", "o5", "o7",
			  "g1", "g2", "g3", "cc");
#ifdef DEBUG_MULDIV
		printk ("0x%x\n", rs1);
#endif
		if (store_reg(rs1, rdv, regs))
			return -1;
		break;
	}
	if (is_foocc (insn)) {
		regs->psr &= ~PSR_ICC;
		if ((inst & 0xe) == 14) {
			/* ?div */
			if (rs2) regs->psr |= PSR_V;
		}
		if (!rs1) regs->psr |= PSR_Z;
		if (((int)rs1) < 0) regs->psr |= PSR_N;
#ifdef DEBUG_MULDIV
		printk ("psr muldiv: %08x\n", regs->psr);
#endif
	}
	advance(regs);
	return 0;
}
