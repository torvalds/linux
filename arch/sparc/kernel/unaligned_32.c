/*
 * unaligned.c: Unaligned load/store trap handling with special
 *              cases for the kernel to do them more quickly.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */


#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/perf_event.h>

#include <asm/setup.h>

#include "kernel.h"

enum direction {
	load,    /* ld, ldd, ldh, ldsh */
	store,   /* st, std, sth, stsh */
	both,    /* Swap, ldstub, etc. */
	fpload,
	fpstore,
	invalid,
};

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
	struct reg_window32 *win;

	if(reg < 16)
		return (!reg ? 0 : regs->u_regs[reg]);

	/* Ho hum, the slightly complicated case. */
	win = (struct reg_window32 *) regs->u_regs[UREG_FP];
	return win->locals[reg - 16]; /* yes, I know what this does... */
}

static inline unsigned long safe_fetch_reg(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window32 __user *win;
	unsigned long ret;

	if (reg < 16)
		return (!reg ? 0 : regs->u_regs[reg]);

	/* Ho hum, the slightly complicated case. */
	win = (struct reg_window32 __user *) regs->u_regs[UREG_FP];

	if ((unsigned long)win & 3)
		return -1;

	if (get_user(ret, &win->locals[reg - 16]))
		return -1;

	return ret;
}

static inline unsigned long *fetch_reg_addr(unsigned int reg, struct pt_regs *regs)
{
	struct reg_window32 *win;

	if(reg < 16)
		return &regs->u_regs[reg];
	win = (struct reg_window32 *) regs->u_regs[UREG_FP];
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
	panic("%s", str);
}

/* una_asm.S */
extern int do_int_load(unsigned long *dest_reg, int size,
		       unsigned long *saddr, int is_signed);
extern int __do_int_store(unsigned long *dst_addr, int size,
			  unsigned long *src_val);

static int do_int_store(int reg_num, int size, unsigned long *dst_addr,
			struct pt_regs *regs)
{
	unsigned long zero[2] = { 0, 0 };
	unsigned long *src_val;

	if (reg_num)
		src_val = fetch_reg_addr(reg_num, regs);
	else {
		src_val = &zero[0];
		if (size == 8)
			zero[1] = fetch_reg(1, regs);
	}
	return __do_int_store(dst_addr, size, src_val);
}

extern void smp_capture(void);
extern void smp_release(void);

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

static void kernel_mna_trap_fault(struct pt_regs *regs, unsigned int insn)
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
	} else {
		unsigned long addr = compute_effective_address(regs, insn);
		int err;

		perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, addr);
		switch (dir) {
		case load:
			err = do_int_load(fetch_reg_addr(((insn>>25)&0x1f),
							 regs),
					  size, (unsigned long *) addr,
					  decode_signedness(insn));
			break;

		case store:
			err = do_int_store(((insn>>25)&0x1f), size,
					   (unsigned long *) addr, regs);
			break;
		default:
			panic("Impossible kernel unaligned trap.");
			/* Not reached... */
		}
		if (err)
			kernel_mna_trap_fault(regs, insn);
		else
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

static void user_mna_trap_fault(struct pt_regs *regs, unsigned int insn)
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

	if(!(current->thread.flags & SPARC_FLAG_UNALIGNED) ||
	   (((insn >> 30) & 3) != 3))
		goto kill_user;
	dir = decode_direction(insn);
	if(!ok_for_user(regs, insn, dir)) {
		goto kill_user;
	} else {
		int err, size = decode_access_size(insn);
		unsigned long addr;

		if(floating_point_load_or_store_p(insn)) {
			printk("User FPU load/store unaligned unsupported.\n");
			goto kill_user;
		}

		addr = compute_effective_address(regs, insn);
		perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, addr);
		switch(dir) {
		case load:
			err = do_int_load(fetch_reg_addr(((insn>>25)&0x1f),
							 regs),
					  size, (unsigned long *) addr,
					  decode_signedness(insn));
			break;

		case store:
			err = do_int_store(((insn>>25)&0x1f), size,
					   (unsigned long *) addr, regs);
			break;

		case both:
			/*
			 * This was supported in 2.4. However, we question
			 * the value of SWAP instruction across word boundaries.
			 */
			printk("Unaligned SWAP unsupported.\n");
			err = -EFAULT;
			break;

		default:
			unaligned_panic("Impossible user unaligned trap.");
			goto out;
		}
		if (err)
			goto kill_user;
		else
			advance(regs);
		goto out;
	}

kill_user:
	user_mna_trap_fault(regs, insn);
out:
	;
}
