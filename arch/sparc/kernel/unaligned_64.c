/*
 * unaligned.c: Unaligned load/store trap handling with special
 *              cases for the kernel to do them more quickly.
 *
 * Copyright (C) 1996,2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */


#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/asi.h>
#include <asm/ptrace.h>
#include <asm/pstate.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <linux/smp.h>
#include <linux/bitops.h>
#include <linux/perf_event.h>
#include <linux/ratelimit.h>
#include <linux/context_tracking.h>
#include <asm/fpumacro.h>
#include <asm/cacheflush.h>

#include "entry.h"

enum direction {
	load,    /* ld, ldd, ldh, ldsh */
	store,   /* st, std, sth, stsh */
	both,    /* Swap, ldstub, cas, ... */
	fpld,
	fpst,
	invalid,
};

static inline enum direction decode_direction(unsigned int insn)
{
	unsigned long tmp = (insn >> 21) & 1;

	if (!tmp)
		return load;
	else {
		switch ((insn>>19)&0xf) {
		case 15: /* swap* */
			return both;
		default:
			return store;
		}
	}
}

/* 16 = double-word, 8 = extra-word, 4 = word, 2 = half-word */
static inline int decode_access_size(struct pt_regs *regs, unsigned int insn)
{
	unsigned int tmp;

	tmp = ((insn >> 19) & 0xf);
	if (tmp == 11 || tmp == 14) /* ldx/stx */
		return 8;
	tmp &= 3;
	if (!tmp)
		return 4;
	else if (tmp == 3)
		return 16;	/* ldd/std - Although it is actually 8 */
	else if (tmp == 2)
		return 2;
	else {
		printk("Impossible unaligned trap. insn=%08x\n", insn);
		die_if_kernel("Byte sized unaligned access?!?!", regs);

		/* GCC should never warn that control reaches the end
		 * of this function without returning a value because
		 * die_if_kernel() is marked with attribute 'noreturn'.
		 * Alas, some versions do...
		 */

		return 0;
	}
}

static inline int decode_asi(unsigned int insn, struct pt_regs *regs)
{
	if (insn & 0x800000) {
		if (insn & 0x2000)
			return (unsigned char)(regs->tstate >> 24);	/* %asi */
		else
			return (unsigned char)(insn >> 5);		/* imm_asi */
	} else
		return ASI_P;
}

/* 0x400000 = signed, 0 = unsigned */
static inline int decode_signedness(unsigned int insn)
{
	return (insn & 0x400000);
}

static inline void maybe_flush_windows(unsigned int rs1, unsigned int rs2,
				       unsigned int rd, int from_kernel)
{
	if (rs2 >= 16 || rs1 >= 16 || rd >= 16) {
		if (from_kernel != 0)
			__asm__ __volatile__("flushw");
		else
			flushw_user();
	}
}

static inline long sign_extend_imm13(long imm)
{
	return imm << 51 >> 51;
}

static unsigned long fetch_reg(unsigned int reg, struct pt_regs *regs)
{
	unsigned long value, fp;
	
	if (reg < 16)
		return (!reg ? 0 : regs->u_regs[reg]);

	fp = regs->u_regs[UREG_FP];

	if (regs->tstate & TSTATE_PRIV) {
		struct reg_window *win;
		win = (struct reg_window *)(fp + STACK_BIAS);
		value = win->locals[reg - 16];
	} else if (!test_thread_64bit_stack(fp)) {
		struct reg_window32 __user *win32;
		win32 = (struct reg_window32 __user *)((unsigned long)((u32)fp));
		get_user(value, &win32->locals[reg - 16]);
	} else {
		struct reg_window __user *win;
		win = (struct reg_window __user *)(fp + STACK_BIAS);
		get_user(value, &win->locals[reg - 16]);
	}
	return value;
}

static unsigned long *fetch_reg_addr(unsigned int reg, struct pt_regs *regs)
{
	unsigned long fp;

	if (reg < 16)
		return &regs->u_regs[reg];

	fp = regs->u_regs[UREG_FP];

	if (regs->tstate & TSTATE_PRIV) {
		struct reg_window *win;
		win = (struct reg_window *)(fp + STACK_BIAS);
		return &win->locals[reg - 16];
	} else if (!test_thread_64bit_stack(fp)) {
		struct reg_window32 *win32;
		win32 = (struct reg_window32 *)((unsigned long)((u32)fp));
		return (unsigned long *)&win32->locals[reg - 16];
	} else {
		struct reg_window *win;
		win = (struct reg_window *)(fp + STACK_BIAS);
		return &win->locals[reg - 16];
	}
}

unsigned long compute_effective_address(struct pt_regs *regs,
					unsigned int insn, unsigned int rd)
{
	int from_kernel = (regs->tstate & TSTATE_PRIV) != 0;
	unsigned int rs1 = (insn >> 14) & 0x1f;
	unsigned int rs2 = insn & 0x1f;
	unsigned long addr;

	if (insn & 0x2000) {
		maybe_flush_windows(rs1, 0, rd, from_kernel);
		addr = (fetch_reg(rs1, regs) + sign_extend_imm13(insn));
	} else {
		maybe_flush_windows(rs1, rs2, rd, from_kernel);
		addr = (fetch_reg(rs1, regs) + fetch_reg(rs2, regs));
	}

	if (!from_kernel && test_thread_flag(TIF_32BIT))
		addr &= 0xffffffff;

	return addr;
}

/* This is just to make gcc think die_if_kernel does return... */
static void __used unaligned_panic(char *str, struct pt_regs *regs)
{
	die_if_kernel(str, regs);
}

extern int do_int_load(unsigned long *dest_reg, int size,
		       unsigned long *saddr, int is_signed, int asi);
	
extern int __do_int_store(unsigned long *dst_addr, int size,
			  unsigned long src_val, int asi);

static inline int do_int_store(int reg_num, int size, unsigned long *dst_addr,
			       struct pt_regs *regs, int asi, int orig_asi)
{
	unsigned long zero = 0;
	unsigned long *src_val_p = &zero;
	unsigned long src_val;

	if (size == 16) {
		size = 8;
		zero = (((long)(reg_num ?
		        (unsigned)fetch_reg(reg_num, regs) : 0)) << 32) |
			(unsigned)fetch_reg(reg_num + 1, regs);
	} else if (reg_num) {
		src_val_p = fetch_reg_addr(reg_num, regs);
	}
	src_val = *src_val_p;
	if (unlikely(asi != orig_asi)) {
		switch (size) {
		case 2:
			src_val = swab16(src_val);
			break;
		case 4:
			src_val = swab32(src_val);
			break;
		case 8:
			src_val = swab64(src_val);
			break;
		case 16:
		default:
			BUG();
			break;
		}
	}
	return __do_int_store(dst_addr, size, src_val, asi);
}

static inline void advance(struct pt_regs *regs)
{
	regs->tpc   = regs->tnpc;
	regs->tnpc += 4;
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
}

static inline int floating_point_load_or_store_p(unsigned int insn)
{
	return (insn >> 24) & 1;
}

static inline int ok_for_kernel(unsigned int insn)
{
	return !floating_point_load_or_store_p(insn);
}

static void kernel_mna_trap_fault(int fixup_tstate_asi)
{
	struct pt_regs *regs = current_thread_info()->kern_una_regs;
	unsigned int insn = current_thread_info()->kern_una_insn;
	const struct exception_table_entry *entry;

	entry = search_exception_tables(regs->tpc);
	if (!entry) {
		unsigned long address;

		address = compute_effective_address(regs, insn,
						    ((insn >> 25) & 0x1f));
        	if (address < PAGE_SIZE) {
                	printk(KERN_ALERT "Unable to handle kernel NULL "
			       "pointer dereference in mna handler");
        	} else
                	printk(KERN_ALERT "Unable to handle kernel paging "
			       "request in mna handler");
	        printk(KERN_ALERT " at virtual address %016lx\n",address);
		printk(KERN_ALERT "current->{active_,}mm->context = %016lx\n",
			(current->mm ? CTX_HWBITS(current->mm->context) :
			CTX_HWBITS(current->active_mm->context)));
		printk(KERN_ALERT "current->{active_,}mm->pgd = %016lx\n",
			(current->mm ? (unsigned long) current->mm->pgd :
			(unsigned long) current->active_mm->pgd));
	        die_if_kernel("Oops", regs);
		/* Not reached */
	}
	regs->tpc = entry->fixup;
	regs->tnpc = regs->tpc + 4;

	if (fixup_tstate_asi) {
		regs->tstate &= ~TSTATE_ASI;
		regs->tstate |= (ASI_AIUS << 24UL);
	}
}

static void log_unaligned(struct pt_regs *regs)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 5);

	if (__ratelimit(&ratelimit)) {
		printk("Kernel unaligned access at TPC[%lx] %pS\n",
		       regs->tpc, (void *) regs->tpc);
	}
}

asmlinkage void kernel_unaligned_trap(struct pt_regs *regs, unsigned int insn)
{
	enum direction dir = decode_direction(insn);
	int size = decode_access_size(regs, insn);
	int orig_asi, asi;

	current_thread_info()->kern_una_regs = regs;
	current_thread_info()->kern_una_insn = insn;

	orig_asi = asi = decode_asi(insn, regs);

	/* If this is a {get,put}_user() on an unaligned userspace pointer,
	 * just signal a fault and do not log the event.
	 */
	if (asi == ASI_AIUS) {
		kernel_mna_trap_fault(0);
		return;
	}

	log_unaligned(regs);

	if (!ok_for_kernel(insn) || dir == both) {
		printk("Unsupported unaligned load/store trap for kernel "
		       "at <%016lx>.\n", regs->tpc);
		unaligned_panic("Kernel does fpu/atomic "
				"unaligned load/store.", regs);

		kernel_mna_trap_fault(0);
	} else {
		unsigned long addr, *reg_addr;
		int err;

		addr = compute_effective_address(regs, insn,
						 ((insn >> 25) & 0x1f));
		perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, addr);
		switch (asi) {
		case ASI_NL:
		case ASI_AIUPL:
		case ASI_AIUSL:
		case ASI_PL:
		case ASI_SL:
		case ASI_PNFL:
		case ASI_SNFL:
			asi &= ~0x08;
			break;
		}
		switch (dir) {
		case load:
			reg_addr = fetch_reg_addr(((insn>>25)&0x1f), regs);
			err = do_int_load(reg_addr, size,
					  (unsigned long *) addr,
					  decode_signedness(insn), asi);
			if (likely(!err) && unlikely(asi != orig_asi)) {
				unsigned long val_in = *reg_addr;
				switch (size) {
				case 2:
					val_in = swab16(val_in);
					break;
				case 4:
					val_in = swab32(val_in);
					break;
				case 8:
					val_in = swab64(val_in);
					break;
				case 16:
				default:
					BUG();
					break;
				}
				*reg_addr = val_in;
			}
			break;

		case store:
			err = do_int_store(((insn>>25)&0x1f), size,
					   (unsigned long *) addr, regs,
					   asi, orig_asi);
			break;

		default:
			panic("Impossible kernel unaligned trap.");
			/* Not reached... */
		}
		if (unlikely(err))
			kernel_mna_trap_fault(1);
		else
			advance(regs);
	}
}

int handle_popc(u32 insn, struct pt_regs *regs)
{
	int from_kernel = (regs->tstate & TSTATE_PRIV) != 0;
	int ret, rd = ((insn >> 25) & 0x1f);
	u64 value;
	                        
	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);
	if (insn & 0x2000) {
		maybe_flush_windows(0, 0, rd, from_kernel);
		value = sign_extend_imm13(insn);
	} else {
		maybe_flush_windows(0, insn & 0x1f, rd, from_kernel);
		value = fetch_reg(insn & 0x1f, regs);
	}
	ret = hweight64(value);
	if (rd < 16) {
		if (rd)
			regs->u_regs[rd] = ret;
	} else {
		unsigned long fp = regs->u_regs[UREG_FP];

		if (!test_thread_64bit_stack(fp)) {
			struct reg_window32 __user *win32;
			win32 = (struct reg_window32 __user *)((unsigned long)((u32)fp));
			put_user(ret, &win32->locals[rd - 16]);
		} else {
			struct reg_window __user *win;
			win = (struct reg_window __user *)(fp + STACK_BIAS);
			put_user(ret, &win->locals[rd - 16]);
		}
	}
	advance(regs);
	return 1;
}

extern void do_fpother(struct pt_regs *regs);
extern void do_privact(struct pt_regs *regs);
extern void sun4v_data_access_exception(struct pt_regs *regs,
					unsigned long addr,
					unsigned long type_ctx);

int handle_ldf_stq(u32 insn, struct pt_regs *regs)
{
	unsigned long addr = compute_effective_address(regs, insn, 0);
	int freg = ((insn >> 25) & 0x1e) | ((insn >> 20) & 0x20);
	struct fpustate *f = FPUSTATE;
	int asi = decode_asi(insn, regs);
	int flag = (freg < 32) ? FPRS_DL : FPRS_DU;

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);

	save_and_clear_fpu();
	current_thread_info()->xfsr[0] &= ~0x1c000;
	if (freg & 3) {
		current_thread_info()->xfsr[0] |= (6 << 14) /* invalid_fp_register */;
		do_fpother(regs);
		return 0;
	}
	if (insn & 0x200000) {
		/* STQ */
		u64 first = 0, second = 0;
		
		if (current_thread_info()->fpsaved[0] & flag) {
			first = *(u64 *)&f->regs[freg];
			second = *(u64 *)&f->regs[freg+2];
		}
		if (asi < 0x80) {
			do_privact(regs);
			return 1;
		}
		switch (asi) {
		case ASI_P:
		case ASI_S: break;
		case ASI_PL:
		case ASI_SL: 
			{
				/* Need to convert endians */
				u64 tmp = __swab64p(&first);
				
				first = __swab64p(&second);
				second = tmp;
				break;
			}
		default:
			if (tlb_type == hypervisor)
				sun4v_data_access_exception(regs, addr, 0);
			else
				spitfire_data_access_exception(regs, 0, addr);
			return 1;
		}
		if (put_user (first >> 32, (u32 __user *)addr) ||
		    __put_user ((u32)first, (u32 __user *)(addr + 4)) ||
		    __put_user (second >> 32, (u32 __user *)(addr + 8)) ||
		    __put_user ((u32)second, (u32 __user *)(addr + 12))) {
			if (tlb_type == hypervisor)
				sun4v_data_access_exception(regs, addr, 0);
			else
				spitfire_data_access_exception(regs, 0, addr);
		    	return 1;
		}
	} else {
		/* LDF, LDDF, LDQF */
		u32 data[4] __attribute__ ((aligned(8)));
		int size, i;
		int err;

		if (asi < 0x80) {
			do_privact(regs);
			return 1;
		} else if (asi > ASI_SNFL) {
			if (tlb_type == hypervisor)
				sun4v_data_access_exception(regs, addr, 0);
			else
				spitfire_data_access_exception(regs, 0, addr);
			return 1;
		}
		switch (insn & 0x180000) {
		case 0x000000: size = 1; break;
		case 0x100000: size = 4; break;
		default: size = 2; break;
		}
		for (i = 0; i < size; i++)
			data[i] = 0;
		
		err = get_user (data[0], (u32 __user *) addr);
		if (!err) {
			for (i = 1; i < size; i++)
				err |= __get_user (data[i], (u32 __user *)(addr + 4*i));
		}
		if (err && !(asi & 0x2 /* NF */)) {
			if (tlb_type == hypervisor)
				sun4v_data_access_exception(regs, addr, 0);
			else
				spitfire_data_access_exception(regs, 0, addr);
			return 1;
		}
		if (asi & 0x8) /* Little */ {
			u64 tmp;

			switch (size) {
			case 1: data[0] = le32_to_cpup(data + 0); break;
			default:*(u64 *)(data + 0) = le64_to_cpup((u64 *)(data + 0));
				break;
			case 4: tmp = le64_to_cpup((u64 *)(data + 0));
				*(u64 *)(data + 0) = le64_to_cpup((u64 *)(data + 2));
				*(u64 *)(data + 2) = tmp;
				break;
			}
		}
		if (!(current_thread_info()->fpsaved[0] & FPRS_FEF)) {
			current_thread_info()->fpsaved[0] = FPRS_FEF;
			current_thread_info()->gsr[0] = 0;
		}
		if (!(current_thread_info()->fpsaved[0] & flag)) {
			if (freg < 32)
				memset(f->regs, 0, 32*sizeof(u32));
			else
				memset(f->regs+32, 0, 32*sizeof(u32));
		}
		memcpy(f->regs + freg, data, size * 4);
		current_thread_info()->fpsaved[0] |= flag;
	}
	advance(regs);
	return 1;
}

void handle_ld_nf(u32 insn, struct pt_regs *regs)
{
	int rd = ((insn >> 25) & 0x1f);
	int from_kernel = (regs->tstate & TSTATE_PRIV) != 0;
	unsigned long *reg;
	                        
	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, 0);

	maybe_flush_windows(0, 0, rd, from_kernel);
	reg = fetch_reg_addr(rd, regs);
	if (from_kernel || rd < 16) {
		reg[0] = 0;
		if ((insn & 0x780000) == 0x180000)
			reg[1] = 0;
	} else if (!test_thread_64bit_stack(regs->u_regs[UREG_FP])) {
		put_user(0, (int __user *) reg);
		if ((insn & 0x780000) == 0x180000)
			put_user(0, ((int __user *) reg) + 1);
	} else {
		put_user(0, (unsigned long __user *) reg);
		if ((insn & 0x780000) == 0x180000)
			put_user(0, (unsigned long __user *) reg + 1);
	}
	advance(regs);
}

void handle_lddfmna(struct pt_regs *regs, unsigned long sfar, unsigned long sfsr)
{
	enum ctx_state prev_state = exception_enter();
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;
	u32 insn;
	u64 value;
	u8 freg;
	int flag;
	struct fpustate *f = FPUSTATE;

	if (tstate & TSTATE_PRIV)
		die_if_kernel("lddfmna from kernel", regs);
	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, sfar);
	if (test_thread_flag(TIF_32BIT))
		pc = (u32)pc;
	if (get_user(insn, (u32 __user *) pc) != -EFAULT) {
		int asi = decode_asi(insn, regs);
		u32 first, second;
		int err;

		if ((asi > ASI_SNFL) ||
		    (asi < ASI_P))
			goto daex;
		first = second = 0;
		err = get_user(first, (u32 __user *)sfar);
		if (!err)
			err = get_user(second, (u32 __user *)(sfar + 4));
		if (err) {
			if (!(asi & 0x2))
				goto daex;
			first = second = 0;
		}
		save_and_clear_fpu();
		freg = ((insn >> 25) & 0x1e) | ((insn >> 20) & 0x20);
		value = (((u64)first) << 32) | second;
		if (asi & 0x8) /* Little */
			value = __swab64p(&value);
		flag = (freg < 32) ? FPRS_DL : FPRS_DU;
		if (!(current_thread_info()->fpsaved[0] & FPRS_FEF)) {
			current_thread_info()->fpsaved[0] = FPRS_FEF;
			current_thread_info()->gsr[0] = 0;
		}
		if (!(current_thread_info()->fpsaved[0] & flag)) {
			if (freg < 32)
				memset(f->regs, 0, 32*sizeof(u32));
			else
				memset(f->regs+32, 0, 32*sizeof(u32));
		}
		*(u64 *)(f->regs + freg) = value;
		current_thread_info()->fpsaved[0] |= flag;
	} else {
daex:
		if (tlb_type == hypervisor)
			sun4v_data_access_exception(regs, sfar, sfsr);
		else
			spitfire_data_access_exception(regs, sfsr, sfar);
		goto out;
	}
	advance(regs);
out:
	exception_exit(prev_state);
}

void handle_stdfmna(struct pt_regs *regs, unsigned long sfar, unsigned long sfsr)
{
	enum ctx_state prev_state = exception_enter();
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;
	u32 insn;
	u64 value;
	u8 freg;
	int flag;
	struct fpustate *f = FPUSTATE;

	if (tstate & TSTATE_PRIV)
		die_if_kernel("stdfmna from kernel", regs);
	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS, 1, regs, sfar);
	if (test_thread_flag(TIF_32BIT))
		pc = (u32)pc;
	if (get_user(insn, (u32 __user *) pc) != -EFAULT) {
		int asi = decode_asi(insn, regs);
		freg = ((insn >> 25) & 0x1e) | ((insn >> 20) & 0x20);
		value = 0;
		flag = (freg < 32) ? FPRS_DL : FPRS_DU;
		if ((asi > ASI_SNFL) ||
		    (asi < ASI_P))
			goto daex;
		save_and_clear_fpu();
		if (current_thread_info()->fpsaved[0] & flag)
			value = *(u64 *)&f->regs[freg];
		switch (asi) {
		case ASI_P:
		case ASI_S: break;
		case ASI_PL:
		case ASI_SL: 
			value = __swab64p(&value); break;
		default: goto daex;
		}
		if (put_user (value >> 32, (u32 __user *) sfar) ||
		    __put_user ((u32)value, (u32 __user *)(sfar + 4)))
			goto daex;
	} else {
daex:
		if (tlb_type == hypervisor)
			sun4v_data_access_exception(regs, sfar, sfsr);
		else
			spitfire_data_access_exception(regs, sfsr, sfar);
		goto out;
	}
	advance(regs);
out:
	exception_exit(prev_state);
}
