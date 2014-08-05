/*
 * arch/arm/kernel/unwind.c
 *
 * Copyright (C) 2008 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *
 * Stack unwinding support for ARM
 *
 * An ARM EABI version of gcc is required to generate the unwind
 * tables. For information about the structure of the unwind tables,
 * see "Exception Handling ABI for the ARM Architecture" at:
 *
 * http://infocenter.arm.com/help/topic/com.arm.doc.subset.swdev.abi/index.html
 */

#ifndef __CHECKER__
#if !defined (__ARM_EABI__)
#warning Your compiler does not have EABI support.
#warning    ARM unwind is known to compile only with EABI compilers.
#warning    Change compiler or disable ARM_UNWIND option.
#elif (__GNUC__ == 4 && __GNUC_MINOR__ <= 2) && !defined(__clang__)
#warning Your compiler is too buggy; it is known to not compile ARM unwind support.
#warning    Change compiler or disable ARM_UNWIND option.
#endif
#endif /* __CHECKER__ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>

#include <asm/stacktrace.h>
#include <asm/traps.h>
#include <asm/unwind.h>

/* Dummy functions to avoid linker complaints */
void __aeabi_unwind_cpp_pr0(void)
{
};
EXPORT_SYMBOL(__aeabi_unwind_cpp_pr0);

void __aeabi_unwind_cpp_pr1(void)
{
};
EXPORT_SYMBOL(__aeabi_unwind_cpp_pr1);

void __aeabi_unwind_cpp_pr2(void)
{
};
EXPORT_SYMBOL(__aeabi_unwind_cpp_pr2);

struct unwind_ctrl_block {
	unsigned long vrs[16];		/* virtual register set */
	const unsigned long *insn;	/* pointer to the current instructions word */
	unsigned long sp_high;		/* highest value of sp allowed */
	/*
	 * 1 : check for stack overflow for each register pop.
	 * 0 : save overhead if there is plenty of stack remaining.
	 */
	int check_each_pop;
	int entries;			/* number of entries left to interpret */
	int byte;			/* current byte number in the instructions word */
};

enum regs {
#ifdef CONFIG_THUMB2_KERNEL
	FP = 7,
#else
	FP = 11,
#endif
	SP = 13,
	LR = 14,
	PC = 15
};

extern const struct unwind_idx __start_unwind_idx[];
static const struct unwind_idx *__origin_unwind_idx;
extern const struct unwind_idx __stop_unwind_idx[];

static DEFINE_SPINLOCK(unwind_lock);
static LIST_HEAD(unwind_tables);

/* Convert a prel31 symbol to an absolute address */
#define prel31_to_addr(ptr)				\
({							\
	/* sign-extend to 32 bits */			\
	long offset = (((long)*(ptr)) << 1) >> 1;	\
	(unsigned long)(ptr) + offset;			\
})

/*
 * Binary search in the unwind index. The entries are
 * guaranteed to be sorted in ascending order by the linker.
 *
 * start = first entry
 * origin = first entry with positive offset (or stop if there is no such entry)
 * stop - 1 = last entry
 */
static const struct unwind_idx *search_index(unsigned long addr,
				       const struct unwind_idx *start,
				       const struct unwind_idx *origin,
				       const struct unwind_idx *stop)
{
	unsigned long addr_prel31;

	pr_debug("%s(%08lx, %p, %p, %p)\n",
			__func__, addr, start, origin, stop);

	/*
	 * only search in the section with the matching sign. This way the
	 * prel31 numbers can be compared as unsigned longs.
	 */
	if (addr < (unsigned long)start)
		/* negative offsets: [start; origin) */
		stop = origin;
	else
		/* positive offsets: [origin; stop) */
		start = origin;

	/* prel31 for address relavive to start */
	addr_prel31 = (addr - (unsigned long)start) & 0x7fffffff;

	while (start < stop - 1) {
		const struct unwind_idx *mid = start + ((stop - start) >> 1);

		/*
		 * As addr_prel31 is relative to start an offset is needed to
		 * make it relative to mid.
		 */
		if (addr_prel31 - ((unsigned long)mid - (unsigned long)start) <
				mid->addr_offset)
			stop = mid;
		else {
			/* keep addr_prel31 relative to start */
			addr_prel31 -= ((unsigned long)mid -
					(unsigned long)start);
			start = mid;
		}
	}

	if (likely(start->addr_offset <= addr_prel31))
		return start;
	else {
		pr_warning("unwind: Unknown symbol address %08lx\n", addr);
		return NULL;
	}
}

static const struct unwind_idx *unwind_find_origin(
		const struct unwind_idx *start, const struct unwind_idx *stop)
{
	pr_debug("%s(%p, %p)\n", __func__, start, stop);
	while (start < stop) {
		const struct unwind_idx *mid = start + ((stop - start) >> 1);

		if (mid->addr_offset >= 0x40000000)
			/* negative offset */
			start = mid + 1;
		else
			/* positive offset */
			stop = mid;
	}
	pr_debug("%s -> %p\n", __func__, stop);
	return stop;
}

static const struct unwind_idx *unwind_find_idx(unsigned long addr)
{
	const struct unwind_idx *idx = NULL;
	unsigned long flags;

	pr_debug("%s(%08lx)\n", __func__, addr);

	if (core_kernel_text(addr)) {
		if (unlikely(!__origin_unwind_idx))
			__origin_unwind_idx =
				unwind_find_origin(__start_unwind_idx,
						__stop_unwind_idx);

		/* main unwind table */
		idx = search_index(addr, __start_unwind_idx,
				   __origin_unwind_idx,
				   __stop_unwind_idx);
	} else {
		/* module unwind tables */
		struct unwind_table *table;

		spin_lock_irqsave(&unwind_lock, flags);
		list_for_each_entry(table, &unwind_tables, list) {
			if (addr >= table->begin_addr &&
			    addr < table->end_addr) {
				idx = search_index(addr, table->start,
						   table->origin,
						   table->stop);
				/* Move-to-front to exploit common traces */
				list_move(&table->list, &unwind_tables);
				break;
			}
		}
		spin_unlock_irqrestore(&unwind_lock, flags);
	}

	pr_debug("%s: idx = %p\n", __func__, idx);
	return idx;
}

static unsigned long unwind_get_byte(struct unwind_ctrl_block *ctrl)
{
	unsigned long ret;

	if (ctrl->entries <= 0) {
		pr_warning("unwind: Corrupt unwind table\n");
		return 0;
	}

	ret = (*ctrl->insn >> (ctrl->byte * 8)) & 0xff;

	if (ctrl->byte == 0) {
		ctrl->insn++;
		ctrl->entries--;
		ctrl->byte = 3;
	} else
		ctrl->byte--;

	return ret;
}

/* Before poping a register check whether it is feasible or not */
static int unwind_pop_register(struct unwind_ctrl_block *ctrl,
				unsigned long **vsp, unsigned int reg)
{
	if (unlikely(ctrl->check_each_pop))
		if (*vsp >= (unsigned long *)ctrl->sp_high)
			return -URC_FAILURE;

	ctrl->vrs[reg] = *(*vsp)++;
	return URC_OK;
}

/* Helper functions to execute the instructions */
static int unwind_exec_pop_subset_r4_to_r13(struct unwind_ctrl_block *ctrl,
						unsigned long mask)
{
	unsigned long *vsp = (unsigned long *)ctrl->vrs[SP];
	int load_sp, reg = 4;

	load_sp = mask & (1 << (13 - 4));
	while (mask) {
		if (mask & 1)
			if (unwind_pop_register(ctrl, &vsp, reg))
				return -URC_FAILURE;
		mask >>= 1;
		reg++;
	}
	if (!load_sp)
		ctrl->vrs[SP] = (unsigned long)vsp;

	return URC_OK;
}

static int unwind_exec_pop_r4_to_rN(struct unwind_ctrl_block *ctrl,
					unsigned long insn)
{
	unsigned long *vsp = (unsigned long *)ctrl->vrs[SP];
	int reg;

	/* pop R4-R[4+bbb] */
	for (reg = 4; reg <= 4 + (insn & 7); reg++)
		if (unwind_pop_register(ctrl, &vsp, reg))
				return -URC_FAILURE;

	if (insn & 0x8)
		if (unwind_pop_register(ctrl, &vsp, 14))
				return -URC_FAILURE;

	ctrl->vrs[SP] = (unsigned long)vsp;

	return URC_OK;
}

static int unwind_exec_pop_subset_r0_to_r3(struct unwind_ctrl_block *ctrl,
						unsigned long mask)
{
	unsigned long *vsp = (unsigned long *)ctrl->vrs[SP];
	int reg = 0;

	/* pop R0-R3 according to mask */
	while (mask) {
		if (mask & 1)
			if (unwind_pop_register(ctrl, &vsp, reg))
				return -URC_FAILURE;
		mask >>= 1;
		reg++;
	}
	ctrl->vrs[SP] = (unsigned long)vsp;

	return URC_OK;
}

/*
 * Execute the current unwind instruction.
 */
static int unwind_exec_insn(struct unwind_ctrl_block *ctrl)
{
	unsigned long insn = unwind_get_byte(ctrl);
	int ret = URC_OK;

	pr_debug("%s: insn = %08lx\n", __func__, insn);

	if ((insn & 0xc0) == 0x00)
		ctrl->vrs[SP] += ((insn & 0x3f) << 2) + 4;
	else if ((insn & 0xc0) == 0x40)
		ctrl->vrs[SP] -= ((insn & 0x3f) << 2) + 4;
	else if ((insn & 0xf0) == 0x80) {
		unsigned long mask;

		insn = (insn << 8) | unwind_get_byte(ctrl);
		mask = insn & 0x0fff;
		if (mask == 0) {
			pr_warning("unwind: 'Refuse to unwind' instruction %04lx\n",
				   insn);
			return -URC_FAILURE;
		}

		ret = unwind_exec_pop_subset_r4_to_r13(ctrl, mask);
		if (ret)
			goto error;
	} else if ((insn & 0xf0) == 0x90 &&
		   (insn & 0x0d) != 0x0d)
		ctrl->vrs[SP] = ctrl->vrs[insn & 0x0f];
	else if ((insn & 0xf0) == 0xa0) {
		ret = unwind_exec_pop_r4_to_rN(ctrl, insn);
		if (ret)
			goto error;
	} else if (insn == 0xb0) {
		if (ctrl->vrs[PC] == 0)
			ctrl->vrs[PC] = ctrl->vrs[LR];
		/* no further processing */
		ctrl->entries = 0;
	} else if (insn == 0xb1) {
		unsigned long mask = unwind_get_byte(ctrl);

		if (mask == 0 || mask & 0xf0) {
			pr_warning("unwind: Spare encoding %04lx\n",
			       (insn << 8) | mask);
			return -URC_FAILURE;
		}

		ret = unwind_exec_pop_subset_r0_to_r3(ctrl, mask);
		if (ret)
			goto error;
	} else if (insn == 0xb2) {
		unsigned long uleb128 = unwind_get_byte(ctrl);

		ctrl->vrs[SP] += 0x204 + (uleb128 << 2);
	} else {
		pr_warning("unwind: Unhandled instruction %02lx\n", insn);
		return -URC_FAILURE;
	}

	pr_debug("%s: fp = %08lx sp = %08lx lr = %08lx pc = %08lx\n", __func__,
		 ctrl->vrs[FP], ctrl->vrs[SP], ctrl->vrs[LR], ctrl->vrs[PC]);

error:
	return ret;
}

/*
 * Unwind a single frame starting with *sp for the symbol at *pc. It
 * updates the *pc and *sp with the new values.
 */
int unwind_frame(struct stackframe *frame)
{
	unsigned long low;
	const struct unwind_idx *idx;
	struct unwind_ctrl_block ctrl;

	/* store the highest address on the stack to avoid crossing it*/
	low = frame->sp;
	ctrl.sp_high = ALIGN(low, THREAD_SIZE);

	pr_debug("%s(pc = %08lx lr = %08lx sp = %08lx)\n", __func__,
		 frame->pc, frame->lr, frame->sp);

	if (!kernel_text_address(frame->pc))
		return -URC_FAILURE;

	idx = unwind_find_idx(frame->pc);
	if (!idx) {
		pr_warning("unwind: Index not found %08lx\n", frame->pc);
		return -URC_FAILURE;
	}

	ctrl.vrs[FP] = frame->fp;
	ctrl.vrs[SP] = frame->sp;
	ctrl.vrs[LR] = frame->lr;
	ctrl.vrs[PC] = 0;

	if (idx->insn == 1)
		/* can't unwind */
		return -URC_FAILURE;
	else if ((idx->insn & 0x80000000) == 0)
		/* prel31 to the unwind table */
		ctrl.insn = (unsigned long *)prel31_to_addr(&idx->insn);
	else if ((idx->insn & 0xff000000) == 0x80000000)
		/* only personality routine 0 supported in the index */
		ctrl.insn = &idx->insn;
	else {
		pr_warning("unwind: Unsupported personality routine %08lx in the index at %p\n",
			   idx->insn, idx);
		return -URC_FAILURE;
	}

	/* check the personality routine */
	if ((*ctrl.insn & 0xff000000) == 0x80000000) {
		ctrl.byte = 2;
		ctrl.entries = 1;
	} else if ((*ctrl.insn & 0xff000000) == 0x81000000) {
		ctrl.byte = 1;
		ctrl.entries = 1 + ((*ctrl.insn & 0x00ff0000) >> 16);
	} else {
		pr_warning("unwind: Unsupported personality routine %08lx at %p\n",
			   *ctrl.insn, ctrl.insn);
		return -URC_FAILURE;
	}

	ctrl.check_each_pop = 0;

	while (ctrl.entries > 0) {
		int urc;
		if ((ctrl.sp_high - ctrl.vrs[SP]) < sizeof(ctrl.vrs))
			ctrl.check_each_pop = 1;
		urc = unwind_exec_insn(&ctrl);
		if (urc < 0)
			return urc;
		if (ctrl.vrs[SP] < low || ctrl.vrs[SP] >= ctrl.sp_high)
			return -URC_FAILURE;
	}

	if (ctrl.vrs[PC] == 0)
		ctrl.vrs[PC] = ctrl.vrs[LR];

	/* check for infinite loop */
	if (frame->pc == ctrl.vrs[PC])
		return -URC_FAILURE;

	frame->fp = ctrl.vrs[FP];
	frame->sp = ctrl.vrs[SP];
	frame->lr = ctrl.vrs[LR];
	frame->pc = ctrl.vrs[PC];

	return URC_OK;
}

void unwind_backtrace(struct pt_regs *regs, struct task_struct *tsk)
{
	struct stackframe frame;
	register unsigned long current_sp asm ("sp");

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (!tsk)
		tsk = current;

	if (regs) {
		arm_get_current_stackframe(regs, &frame);
		/* PC might be corrupted, use LR in that case. */
		if (!kernel_text_address(regs->ARM_pc))
			frame.pc = regs->ARM_lr;
	} else if (tsk == current) {
		frame.fp = (unsigned long)__builtin_frame_address(0);
		frame.sp = current_sp;
		frame.lr = (unsigned long)__builtin_return_address(0);
		frame.pc = (unsigned long)unwind_backtrace;
	} else {
		/* task blocked in __switch_to */
		frame.fp = thread_saved_fp(tsk);
		frame.sp = thread_saved_sp(tsk);
		/*
		 * The function calling __switch_to cannot be a leaf function
		 * so LR is recovered from the stack.
		 */
		frame.lr = 0;
		frame.pc = thread_saved_pc(tsk);
	}

	while (1) {
		int urc;
		unsigned long where = frame.pc;

		urc = unwind_frame(&frame);
		if (urc < 0)
			break;
		dump_backtrace_entry(where, frame.pc, frame.sp - 4);
	}
}

struct unwind_table *unwind_table_add(unsigned long start, unsigned long size,
				      unsigned long text_addr,
				      unsigned long text_size)
{
	unsigned long flags;
	struct unwind_table *tab = kmalloc(sizeof(*tab), GFP_KERNEL);

	pr_debug("%s(%08lx, %08lx, %08lx, %08lx)\n", __func__, start, size,
		 text_addr, text_size);

	if (!tab)
		return tab;

	tab->start = (const struct unwind_idx *)start;
	tab->stop = (const struct unwind_idx *)(start + size);
	tab->origin = unwind_find_origin(tab->start, tab->stop);
	tab->begin_addr = text_addr;
	tab->end_addr = text_addr + text_size;

	spin_lock_irqsave(&unwind_lock, flags);
	list_add_tail(&tab->list, &unwind_tables);
	spin_unlock_irqrestore(&unwind_lock, flags);

	return tab;
}

void unwind_table_del(struct unwind_table *tab)
{
	unsigned long flags;

	if (!tab)
		return;

	spin_lock_irqsave(&unwind_lock, flags);
	list_del(&tab->list);
	spin_unlock_irqrestore(&unwind_lock, flags);

	kfree(tab);
}
