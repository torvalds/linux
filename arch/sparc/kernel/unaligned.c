/* $Id: unaligned.c,v 1.23 2001/12/21 00:54:31 davem Exp $
 * unaligned.c: Unaligned load/store trap handling with special
 *              cases for the kernel to do them more quickly.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

/* #define DEBUG_MNA */

enum direction {
	load,    /* ld, ldd, ldh, ldsh */
	store,   /* st, std, sth, stsh */
	both,    /* Swap, ldstub, etc. */
	fpload,
	fpstore,
	invalid,
};

#ifdef DEBUG_MNA
static char *dirstrings[] = {
  "load", "store", "both", "fpload", "fpstore", "invalid"
};
#endif

static inline enum direction decode_direction(unsigned int insn)
{
	unsigned long tmp = (insn >> 21) & 1;

	if(!tmp)
		return load;
	else {
		if(((insn>>19)&0x3f) == 15)
			return both;
		else
			return store;
	}
}

/* 8 = double-word, 4 = word, 2 = half-word */
static inline int decode_access_size(unsigned int insn)
{
	insn = (insn >> 19) & 3;

	if(!insn)
		return 4;
	else if(insn == 3)
		return 8;
	else if(insn == 2)
		return 2;
	else {
		printk("Impossible unaligned trap. insn=%08x\n", insn);
		die_if_kernel("Byte sized unaligned access?!?!", current->thread.kregs);
		return 4; /* just to keep gcc happy. */
	}
}

/* 0x400000 = signed, 0 = unsigned */
static inline int decode_signedness(unsigned int insn)
{
	return (insn & 0x400000);
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

static inline int sign_extend_imm13(int imm)
{
	return imm << 19 >> 19;
}

static inline unsigned long fetch_reg(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window *win;

	if(reg < 16)
		return (!reg ? 0 : regs->u_regs[reg]);

	/* Ho hum, the slightly complicated case. */
	win = (struct reg_window *) regs->u_regs[UREG_FP];
	return win->locals[reg - 16]; /* yes, I know what this does... */
}

static inline unsigned long safe_fetch_reg(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window __user *win;
	unsigned long ret;

	if (reg < 16)
		return (!reg ? 0 : regs->u_regs[reg]);

	/* Ho hum, the slightly complicated case. */
	win = (struct reg_window __user *) regs->u_regs[UREG_FP];

	if ((unsigned long)win & 3)
		return -1;

	if (get_user(ret, &win->locals[reg - 16]))
		return -1;

	return ret;
}

static inline unsigned long *fetch_reg_addr(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window *win;

	if(reg < 16)
		return &regs->u_regs[reg];
	win = (struct reg_window *) regs->u_regs[UREG_FP];
	return &win->locals[reg - 16];
}

static unsigned long compute_effective_address(struct pt_regs *regs,
					       unsigned int insn)
{
	unsigned int rs1 = (insn >> 14) & 0x1f;
	unsigned int rs2 = insn & 0x1f;
	unsigned int rd = (insn >> 25) & 0x1f;

	if(insn & 0x2000) {
		maybe_flush_windows(rs1, 0, rd);
		return (fetch_reg(rs1, regs) + sign_extend_imm13(insn));
	} else {
		maybe_flush_windows(rs1, rs2, rd);
		return (fetch_reg(rs1, regs) + fetch_reg(rs2, regs));
	}
}

unsigned long safe_compute_effective_address(struct pt_regs *regs,
					     unsigned int insn)
{
	unsigned int rs1 = (insn >> 14) & 0x1f;
	unsigned int rs2 = insn & 0x1f;
	unsigned int rd = (insn >> 25) & 0x1f;

	if(insn & 0x2000) {
		maybe_flush_windows(rs1, 0, rd);
		return (safe_fetch_reg(rs1, regs) + sign_extend_imm13(insn));
	} else {
		maybe_flush_windows(rs1, rs2, rd);
		return (safe_fetch_reg(rs1, regs) + safe_fetch_reg(rs2, regs));
	}
}

/* This is just to make gcc think panic does return... */
static void unaligned_panic(char *str)
{
	panic(str);
}

#define do_integer_load(dest_reg, size, saddr, is_signed, errh) ({		\
__asm__ __volatile__ (								\
	"cmp	%1, 8\n\t"							\
	"be	9f\n\t"								\
	" cmp	%1, 4\n\t"							\
	"be	6f\n"								\
"4:\t"	" ldub	[%2], %%l1\n"							\
"5:\t"	"ldub	[%2 + 1], %%l2\n\t"						\
	"sll	%%l1, 8, %%l1\n\t"						\
	"tst	%3\n\t"								\
	"be	3f\n\t"								\
	" add	%%l1, %%l2, %%l1\n\t"						\
	"sll	%%l1, 16, %%l1\n\t"						\
	"sra	%%l1, 16, %%l1\n"						\
"3:\t"	"b	0f\n\t"								\
	" st	%%l1, [%0]\n"							\
"6:\t"	"ldub	[%2 + 1], %%l2\n\t"						\
	"sll	%%l1, 24, %%l1\n"						\
"7:\t"	"ldub	[%2 + 2], %%g7\n\t"						\
	"sll	%%l2, 16, %%l2\n"						\
"8:\t"	"ldub	[%2 + 3], %%g1\n\t"						\
	"sll	%%g7, 8, %%g7\n\t"						\
	"or	%%l1, %%l2, %%l1\n\t"						\
	"or	%%g7, %%g1, %%g7\n\t"						\
	"or	%%l1, %%g7, %%l1\n\t"						\
	"b	0f\n\t"								\
	" st	%%l1, [%0]\n"							\
"9:\t"	"ldub	[%2], %%l1\n"							\
"10:\t"	"ldub	[%2 + 1], %%l2\n\t"						\
	"sll	%%l1, 24, %%l1\n"						\
"11:\t"	"ldub	[%2 + 2], %%g7\n\t"						\
	"sll	%%l2, 16, %%l2\n"						\
"12:\t"	"ldub	[%2 + 3], %%g1\n\t"						\
	"sll	%%g7, 8, %%g7\n\t"						\
	"or	%%l1, %%l2, %%l1\n\t"						\
	"or	%%g7, %%g1, %%g7\n\t"						\
	"or	%%l1, %%g7, %%g7\n"						\
"13:\t"	"ldub	[%2 + 4], %%l1\n\t"						\
	"st	%%g7, [%0]\n"							\
"14:\t"	"ldub	[%2 + 5], %%l2\n\t"						\
	"sll	%%l1, 24, %%l1\n"						\
"15:\t"	"ldub	[%2 + 6], %%g7\n\t"						\
	"sll	%%l2, 16, %%l2\n"						\
"16:\t"	"ldub	[%2 + 7], %%g1\n\t"						\
	"sll	%%g7, 8, %%g7\n\t"						\
	"or	%%l1, %%l2, %%l1\n\t"						\
	"or	%%g7, %%g1, %%g7\n\t"						\
	"or	%%l1, %%g7, %%g7\n\t"						\
	"st	%%g7, [%0 + 4]\n"						\
"0:\n\n\t"									\
	".section __ex_table,#alloc\n\t"					\
	".word	4b, " #errh "\n\t"						\
	".word	5b, " #errh "\n\t"						\
	".word	6b, " #errh "\n\t"						\
	".word	7b, " #errh "\n\t"						\
	".word	8b, " #errh "\n\t"						\
	".word	9b, " #errh "\n\t"						\
	".word	10b, " #errh "\n\t"						\
	".word	11b, " #errh "\n\t"						\
	".word	12b, " #errh "\n\t"						\
	".word	13b, " #errh "\n\t"						\
	".word	14b, " #errh "\n\t"						\
	".word	15b, " #errh "\n\t"						\
	".word	16b, " #errh "\n\n\t"						\
	".previous\n\t"								\
	: : "r" (dest_reg), "r" (size), "r" (saddr), "r" (is_signed)		\
	: "l1", "l2", "g7", "g1", "cc");					\
})
	
#define store_common(dst_addr, size, src_val, errh) ({				\
__asm__ __volatile__ (								\
	"ld	[%2], %%l1\n"							\
	"cmp	%1, 2\n\t"							\
	"be	2f\n\t"								\
	" cmp	%1, 4\n\t"							\
	"be	1f\n\t"								\
	" srl	%%l1, 24, %%l2\n\t"						\
	"srl	%%l1, 16, %%g7\n"						\
"4:\t"	"stb	%%l2, [%0]\n\t"							\
	"srl	%%l1, 8, %%l2\n"						\
"5:\t"	"stb	%%g7, [%0 + 1]\n\t"						\
	"ld	[%2 + 4], %%g7\n"						\
"6:\t"	"stb	%%l2, [%0 + 2]\n\t"						\
	"srl	%%g7, 24, %%l2\n"						\
"7:\t"	"stb	%%l1, [%0 + 3]\n\t"						\
	"srl	%%g7, 16, %%l1\n"						\
"8:\t"	"stb	%%l2, [%0 + 4]\n\t"						\
	"srl	%%g7, 8, %%l2\n"						\
"9:\t"	"stb	%%l1, [%0 + 5]\n"						\
"10:\t"	"stb	%%l2, [%0 + 6]\n\t"						\
	"b	0f\n"								\
"11:\t"	" stb	%%g7, [%0 + 7]\n"						\
"1:\t"	"srl	%%l1, 16, %%g7\n"						\
"12:\t"	"stb	%%l2, [%0]\n\t"							\
	"srl	%%l1, 8, %%l2\n"						\
"13:\t"	"stb	%%g7, [%0 + 1]\n"						\
"14:\t"	"stb	%%l2, [%0 + 2]\n\t"						\
	"b	0f\n"								\
"15:\t"	" stb	%%l1, [%0 + 3]\n"						\
"2:\t"	"srl	%%l1, 8, %%l2\n"						\
"16:\t"	"stb	%%l2, [%0]\n"							\
"17:\t"	"stb	%%l1, [%0 + 1]\n"						\
"0:\n\n\t"									\
	".section __ex_table,#alloc\n\t"					\
	".word	4b, " #errh "\n\t"						\
	".word	5b, " #errh "\n\t"						\
	".word	6b, " #errh "\n\t"						\
	".word	7b, " #errh "\n\t"						\
	".word	8b, " #errh "\n\t"						\
	".word	9b, " #errh "\n\t"						\
	".word	10b, " #errh "\n\t"						\
	".word	11b, " #errh "\n\t"						\
	".word	12b, " #errh "\n\t"						\
	".word	13b, " #errh "\n\t"						\
	".word	14b, " #errh "\n\t"						\
	".word	15b, " #errh "\n\t"						\
	".word	16b, " #errh "\n\t"						\
	".word	17b, " #errh "\n\n\t"						\
	".previous\n\t"								\
	: : "r" (dst_addr), "r" (size), "r" (src_val)				\
	: "l1", "l2", "g7", "g1", "cc");					\
})

#define do_integer_store(reg_num, size, dst_addr, regs, errh) ({		\
	unsigned long *src_val;							\
	static unsigned long zero[2] = { 0, };					\
										\
	if (reg_num) src_val = fetch_reg_addr(reg_num, regs);			\
	else {									\
		src_val = &zero[0];						\
		if (size == 8)							\
			zero[1] = fetch_reg(1, regs);				\
	}									\
	store_common(dst_addr, size, src_val, errh);				\
})

extern void smp_capture(void);
extern void smp_release(void);

#define do_atomic(srcdest_reg, mem, errh) ({					\
	unsigned long flags, tmp;						\
										\
	smp_capture();								\
	local_irq_save(flags);							\
	tmp = *srcdest_reg;							\
	do_integer_load(srcdest_reg, 4, mem, 0, errh);				\
	store_common(mem, 4, &tmp, errh);					\
	local_irq_restore(flags);						\
	smp_release();								\
})

static inline void advance(struct pt_regs *regs)
{
	regs->pc   = regs->npc;
	regs->npc += 4;
}

static inline int floating_point_load_or_store_p(unsigned int insn)
{
	return (insn >> 24) & 1;
}

static inline int ok_for_kernel(unsigned int insn)
{
	return !floating_point_load_or_store_p(insn);
}

void kernel_mna_trap_fault(struct pt_regs *regs, unsigned int insn) __asm__ ("kernel_mna_trap_fault");

void kernel_mna_trap_fault(struct pt_regs *regs, unsigned int insn)
{
	unsigned long g2 = regs->u_regs [UREG_G2];
	unsigned long fixup = search_extables_range(regs->pc, &g2);

	if (!fixup) {
		unsigned long address = compute_effective_address(regs, insn);
        	if(address < PAGE_SIZE) {
                	printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference in mna handler");
        	} else
                	printk(KERN_ALERT "Unable to handle kernel paging request in mna handler");
	        printk(KERN_ALERT " at virtual address %08lx\n",address);
		printk(KERN_ALERT "current->{mm,active_mm}->context = %08lx\n",
			(current->mm ? current->mm->context :
			current->active_mm->context));
		printk(KERN_ALERT "current->{mm,active_mm}->pgd = %08lx\n",
			(current->mm ? (unsigned long) current->mm->pgd :
			(unsigned long) current->active_mm->pgd));
	        die_if_kernel("Oops", regs);
		/* Not reached */
	}
	regs->pc = fixup;
	regs->npc = regs->pc + 4;
	regs->u_regs [UREG_G2] = g2;
}

asmlinkage void kernel_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir = decode_direction(insn);
	int size = decode_access_size(insn);

	if(!ok_for_kernel(insn) || dir == both) {
		printk("Unsupported unaligned load/store trap for kernel at <%08lx>.\n",
		       regs->pc);
		unaligned_panic("Wheee. Kernel does fpu/atomic unaligned load/store.");

		__asm__ __volatile__ ("\n"
"kernel_unaligned_trap_fault:\n\t"
		"mov	%0, %%o0\n\t"
		"call	kernel_mna_trap_fault\n\t"
		" mov	%1, %%o1\n\t"
		:
		: "r" (regs), "r" (insn)
		: "o0", "o1", "o2", "o3", "o4", "o5", "o7",
		  "g1", "g2", "g3", "g4", "g5", "g7", "cc");
	} else {
		unsigned long addr = compute_effective_address(regs, insn);

#ifdef DEBUG_MNA
		printk("KMNA: pc=%08lx [dir=%s addr=%08lx size=%d] retpc[%08lx]\n",
		       regs->pc, dirstrings[dir], addr, size, regs->u_regs[UREG_RETPC]);
#endif
		switch(dir) {
		case load:
			do_integer_load(fetch_reg_addr(((insn>>25)&0x1f), regs),
					size, (unsigned long *) addr,
					decode_signedness(insn),
					kernel_unaligned_trap_fault);
			break;

		case store:
			do_integer_store(((insn>>25)&0x1f), size,
					 (unsigned long *) addr, regs,
					 kernel_unaligned_trap_fault);
			break;
#if 0 /* unsupported */
		case both:
			do_atomic(fetch_reg_addr(((insn>>25)&0x1f), regs),
				  (unsigned long *) addr,
				  kernel_unaligned_trap_fault);
			break;
#endif
		default:
			panic("Impossible kernel unaligned trap.");
			/* Not reached... */
		}
		advance(regs);
	}
}

static inline int ok_for_user(struct pt_regs *regs, unsigned int insn,
			      enum direction dir)
{
	unsigned int reg;
	int check = (dir == load) ? VERIFY_READ : VERIFY_WRITE;
	int size = ((insn >> 19) & 3) == 3 ? 8 : 4;

	if ((regs->pc | regs->npc) & 3)
		return 0;

	/* Must access_ok() in all the necessary places. */
#define WINREG_ADDR(regnum) \
	((void __user *)(((unsigned long *)regs->u_regs[UREG_FP])+(regnum)))

	reg = (insn >> 25) & 0x1f;
	if (reg >= 16) {
		if (!access_ok(check, WINREG_ADDR(reg - 16), size))
			return -EFAULT;
	}
	reg = (insn >> 14) & 0x1f;
	if (reg >= 16) {
		if (!access_ok(check, WINREG_ADDR(reg - 16), size))
			return -EFAULT;
	}
	if (!(insn & 0x2000)) {
		reg = (insn & 0x1f);
		if (reg >= 16) {
			if (!access_ok(check, WINREG_ADDR(reg - 16), size))
				return -EFAULT;
		}
	}
#undef WINREG_ADDR
	return 0;
}

void user_mna_trap_fault(struct pt_regs *regs, unsigned int insn) __asm__ ("user_mna_trap_fault");

void user_mna_trap_fault(struct pt_regs *regs, unsigned int insn)
{
	siginfo_t info;

	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void __user *)safe_compute_effective_address(regs, insn);
	info.si_trapno = 0;
	send_sig_info(SIGBUS, &info, current);
}

asmlinkage void user_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir;

	lock_kernel();
	if(!(current->thread.flags & SPARC_FLAG_UNALIGNED) ||
	   (((insn >> 30) & 3) != 3))
		goto kill_user;
	dir = decode_direction(insn);
	if(!ok_for_user(regs, insn, dir)) {
		goto kill_user;
	} else {
		int size = decode_access_size(insn);
		unsigned long addr;

		if(floating_point_load_or_store_p(insn)) {
			printk("User FPU load/store unaligned unsupported.\n");
			goto kill_user;
		}

		addr = compute_effective_address(regs, insn);
		switch(dir) {
		case load:
			do_integer_load(fetch_reg_addr(((insn>>25)&0x1f), regs),
					size, (unsigned long *) addr,
					decode_signedness(insn),
					user_unaligned_trap_fault);
			break;

		case store:
			do_integer_store(((insn>>25)&0x1f), size,
					 (unsigned long *) addr, regs,
					 user_unaligned_trap_fault);
			break;

		case both:
#if 0 /* unsupported */
			do_atomic(fetch_reg_addr(((insn>>25)&0x1f), regs),
				  (unsigned long *) addr,
				  user_unaligned_trap_fault);
#else
			/*
			 * This was supported in 2.4. However, we question
			 * the value of SWAP instruction across word boundaries.
			 */
			printk("Unaligned SWAP unsupported.\n");
			goto kill_user;
#endif
			break;

		default:
			unaligned_panic("Impossible user unaligned trap.");

			__asm__ __volatile__ ("\n"
"user_unaligned_trap_fault:\n\t"
			"mov	%0, %%o0\n\t"
			"call	user_mna_trap_fault\n\t"
			" mov	%1, %%o1\n\t"
			:
			: "r" (regs), "r" (insn)
			: "o0", "o1", "o2", "o3", "o4", "o5", "o7",
			  "g1", "g2", "g3", "g4", "g5", "g7", "cc");
			goto out;
		}
		advance(regs);
		goto out;
	}

kill_user:
	user_mna_trap_fault(regs, insn);
out:
	unlock_kernel();
}
