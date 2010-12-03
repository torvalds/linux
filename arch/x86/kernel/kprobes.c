/*
 *  Kernel Probes (KProbes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes contributions from
 *		Rusty Russell).
 * 2004-July	Suparna Bhattacharya <suparna@in.ibm.com> added jumper probes
 *		interface to access function arguments.
 * 2004-Oct	Jim Keniston <jkenisto@us.ibm.com> and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> adapted for x86_64 from i386.
 * 2005-Mar	Roland McGrath <roland@redhat.com>
 *		Fixed to handle %rip-relative addressing mode correctly.
 * 2005-May	Hien Nguyen <hien@us.ibm.com>, Jim Keniston
 *		<jkenisto@us.ibm.com> and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> added function-return probes.
 * 2005-May	Rusty Lynch <rusty.lynch@intel.com>
 * 		Added function return probes functionality
 * 2006-Feb	Masami Hiramatsu <hiramatu@sdl.hitachi.co.jp> added
 * 		kprobe-booster and kretprobe-booster for i386.
 * 2007-Dec	Masami Hiramatsu <mhiramat@redhat.com> added kprobe-booster
 * 		and kretprobe-booster for x86-64
 * 2007-Dec	Masami Hiramatsu <mhiramat@redhat.com>, Arjan van de Ven
 * 		<arjan@infradead.org> and Jim Keniston <jkenisto@us.ibm.com>
 * 		unified x86 kprobes code.
 */

#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/hardirq.h>
#include <linux/preempt.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/kallsyms.h>
#include <linux/ftrace.h>

#include <asm/cacheflush.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/alternative.h>
#include <asm/insn.h>
#include <asm/debugreg.h>

void jprobe_return_end(void);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

#define stack_addr(regs) ((unsigned long *)kernel_stack_pointer(regs))

#define W(row, b0, b1, b2, b3, b4, b5, b6, b7, b8, b9, ba, bb, bc, bd, be, bf)\
	(((b0##UL << 0x0)|(b1##UL << 0x1)|(b2##UL << 0x2)|(b3##UL << 0x3) |   \
	  (b4##UL << 0x4)|(b5##UL << 0x5)|(b6##UL << 0x6)|(b7##UL << 0x7) |   \
	  (b8##UL << 0x8)|(b9##UL << 0x9)|(ba##UL << 0xa)|(bb##UL << 0xb) |   \
	  (bc##UL << 0xc)|(bd##UL << 0xd)|(be##UL << 0xe)|(bf##UL << 0xf))    \
	 << (row % 32))
	/*
	 * Undefined/reserved opcodes, conditional jump, Opcode Extension
	 * Groups, and some special opcodes can not boost.
	 */
static const u32 twobyte_is_boostable[256 / 32] = {
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f          */
	/*      ----------------------------------------------          */
	W(0x00, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0) | /* 00 */
	W(0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 10 */
	W(0x20, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* 20 */
	W(0x30, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 30 */
	W(0x40, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 40 */
	W(0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 50 */
	W(0x60, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1) | /* 60 */
	W(0x70, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1) , /* 70 */
	W(0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* 80 */
	W(0x90, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 90 */
	W(0xa0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1) | /* a0 */
	W(0xb0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1) , /* b0 */
	W(0xc0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1) | /* c0 */
	W(0xd0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1) , /* d0 */
	W(0xe0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 0, 1) | /* e0 */
	W(0xf0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0)   /* f0 */
	/*      -----------------------------------------------         */
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f          */
};
#undef W

struct kretprobe_blackpoint kretprobe_blacklist[] = {
	{"__switch_to", }, /* This function switches only current task, but
			      doesn't switch kernel stack.*/
	{NULL, NULL}	/* Terminator */
};
const int kretprobe_blacklist_size = ARRAY_SIZE(kretprobe_blacklist);

static void __kprobes __synthesize_relative_insn(void *from, void *to, u8 op)
{
	struct __arch_relative_insn {
		u8 op;
		s32 raddr;
	} __attribute__((packed)) *insn;

	insn = (struct __arch_relative_insn *)from;
	insn->raddr = (s32)((long)(to) - ((long)(from) + 5));
	insn->op = op;
}

/* Insert a jump instruction at address 'from', which jumps to address 'to'.*/
static void __kprobes synthesize_reljump(void *from, void *to)
{
	__synthesize_relative_insn(from, to, RELATIVEJUMP_OPCODE);
}

/*
 * Skip the prefixes of the instruction.
 */
static kprobe_opcode_t *__kprobes skip_prefixes(kprobe_opcode_t *insn)
{
	insn_attr_t attr;

	attr = inat_get_opcode_attribute((insn_byte_t)*insn);
	while (inat_is_legacy_prefix(attr)) {
		insn++;
		attr = inat_get_opcode_attribute((insn_byte_t)*insn);
	}
#ifdef CONFIG_X86_64
	if (inat_is_rex_prefix(attr))
		insn++;
#endif
	return insn;
}

/*
 * Returns non-zero if opcode is boostable.
 * RIP relative instructions are adjusted at copying time in 64 bits mode
 */
static int __kprobes can_boost(kprobe_opcode_t *opcodes)
{
	kprobe_opcode_t opcode;
	kprobe_opcode_t *orig_opcodes = opcodes;

	if (search_exception_tables((unsigned long)opcodes))
		return 0;	/* Page fault may occur on this address. */

retry:
	if (opcodes - orig_opcodes > MAX_INSN_SIZE - 1)
		return 0;
	opcode = *(opcodes++);

	/* 2nd-byte opcode */
	if (opcode == 0x0f) {
		if (opcodes - orig_opcodes > MAX_INSN_SIZE - 1)
			return 0;
		return test_bit(*opcodes,
				(unsigned long *)twobyte_is_boostable);
	}

	switch (opcode & 0xf0) {
#ifdef CONFIG_X86_64
	case 0x40:
		goto retry; /* REX prefix is boostable */
#endif
	case 0x60:
		if (0x63 < opcode && opcode < 0x67)
			goto retry; /* prefixes */
		/* can't boost Address-size override and bound */
		return (opcode != 0x62 && opcode != 0x67);
	case 0x70:
		return 0; /* can't boost conditional jump */
	case 0xc0:
		/* can't boost software-interruptions */
		return (0xc1 < opcode && opcode < 0xcc) || opcode == 0xcf;
	case 0xd0:
		/* can boost AA* and XLAT */
		return (opcode == 0xd4 || opcode == 0xd5 || opcode == 0xd7);
	case 0xe0:
		/* can boost in/out and absolute jmps */
		return ((opcode & 0x04) || opcode == 0xea);
	case 0xf0:
		if ((opcode & 0x0c) == 0 && opcode != 0xf1)
			goto retry; /* lock/rep(ne) prefix */
		/* clear and set flags are boostable */
		return (opcode == 0xf5 || (0xf7 < opcode && opcode < 0xfe));
	default:
		/* segment override prefixes are boostable */
		if (opcode == 0x26 || opcode == 0x36 || opcode == 0x3e)
			goto retry; /* prefixes */
		/* CS override prefix and call are not boostable */
		return (opcode != 0x2e && opcode != 0x9a);
	}
}

/* Recover the probed instruction at addr for further analysis. */
static int recover_probed_instruction(kprobe_opcode_t *buf, unsigned long addr)
{
	struct kprobe *kp;
	kp = get_kprobe((void *)addr);
	if (!kp)
		return -EINVAL;

	/*
	 *  Basically, kp->ainsn.insn has an original instruction.
	 *  However, RIP-relative instruction can not do single-stepping
	 *  at different place, __copy_instruction() tweaks the displacement of
	 *  that instruction. In that case, we can't recover the instruction
	 *  from the kp->ainsn.insn.
	 *
	 *  On the other hand, kp->opcode has a copy of the first byte of
	 *  the probed instruction, which is overwritten by int3. And
	 *  the instruction at kp->addr is not modified by kprobes except
	 *  for the first byte, we can recover the original instruction
	 *  from it and kp->opcode.
	 */
	memcpy(buf, kp->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
	buf[0] = kp->opcode;
	return 0;
}

/* Check if paddr is at an instruction boundary */
static int __kprobes can_probe(unsigned long paddr)
{
	int ret;
	unsigned long addr, offset = 0;
	struct insn insn;
	kprobe_opcode_t buf[MAX_INSN_SIZE];

	if (!kallsyms_lookup_size_offset(paddr, NULL, &offset))
		return 0;

	/* Decode instructions */
	addr = paddr - offset;
	while (addr < paddr) {
		kernel_insn_init(&insn, (void *)addr);
		insn_get_opcode(&insn);

		/*
		 * Check if the instruction has been modified by another
		 * kprobe, in which case we replace the breakpoint by the
		 * original instruction in our buffer.
		 */
		if (insn.opcode.bytes[0] == BREAKPOINT_INSTRUCTION) {
			ret = recover_probed_instruction(buf, addr);
			if (ret)
				/*
				 * Another debugging subsystem might insert
				 * this breakpoint. In that case, we can't
				 * recover it.
				 */
				return 0;
			kernel_insn_init(&insn, buf);
		}
		insn_get_length(&insn);
		addr += insn.length;
	}

	return (addr == paddr);
}

/*
 * Returns non-zero if opcode modifies the interrupt flag.
 */
static int __kprobes is_IF_modifier(kprobe_opcode_t *insn)
{
	/* Skip prefixes */
	insn = skip_prefixes(insn);

	switch (*insn) {
	case 0xfa:		/* cli */
	case 0xfb:		/* sti */
	case 0xcf:		/* iret/iretd */
	case 0x9d:		/* popf/popfd */
		return 1;
	}

	return 0;
}

/*
 * Copy an instruction and adjust the displacement if the instruction
 * uses the %rip-relative addressing mode.
 * If it does, Return the address of the 32-bit displacement word.
 * If not, return null.
 * Only applicable to 64-bit x86.
 */
static int __kprobes __copy_instruction(u8 *dest, u8 *src, int recover)
{
	struct insn insn;
	int ret;
	kprobe_opcode_t buf[MAX_INSN_SIZE];

	kernel_insn_init(&insn, src);
	if (recover) {
		insn_get_opcode(&insn);
		if (insn.opcode.bytes[0] == BREAKPOINT_INSTRUCTION) {
			ret = recover_probed_instruction(buf,
							 (unsigned long)src);
			if (ret)
				return 0;
			kernel_insn_init(&insn, buf);
		}
	}
	insn_get_length(&insn);
	memcpy(dest, insn.kaddr, insn.length);

#ifdef CONFIG_X86_64
	if (insn_rip_relative(&insn)) {
		s64 newdisp;
		u8 *disp;
		kernel_insn_init(&insn, dest);
		insn_get_displacement(&insn);
		/*
		 * The copied instruction uses the %rip-relative addressing
		 * mode.  Adjust the displacement for the difference between
		 * the original location of this instruction and the location
		 * of the copy that will actually be run.  The tricky bit here
		 * is making sure that the sign extension happens correctly in
		 * this calculation, since we need a signed 32-bit result to
		 * be sign-extended to 64 bits when it's added to the %rip
		 * value and yield the same 64-bit result that the sign-
		 * extension of the original signed 32-bit displacement would
		 * have given.
		 */
		newdisp = (u8 *) src + (s64) insn.displacement.value -
			  (u8 *) dest;
		BUG_ON((s64) (s32) newdisp != newdisp); /* Sanity check.  */
		disp = (u8 *) dest + insn_offset_displacement(&insn);
		*(s32 *) disp = (s32) newdisp;
	}
#endif
	return insn.length;
}

static void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	/*
	 * Copy an instruction without recovering int3, because it will be
	 * put by another subsystem.
	 */
	__copy_instruction(p->ainsn.insn, p->addr, 0);

	if (can_boost(p->addr))
		p->ainsn.boostable = 0;
	else
		p->ainsn.boostable = -1;

	p->opcode = *p->addr;
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	if (alternatives_text_reserved(p->addr, p->addr))
		return -EINVAL;

	if (!can_probe((unsigned long)p->addr))
		return -EILSEQ;
	/* insn: must be on special executable page on x86. */
	p->ainsn.insn = get_insn_slot();
	if (!p->ainsn.insn)
		return -ENOMEM;
	arch_copy_kprobe(p);
	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	text_poke(p->addr, ((unsigned char []){BREAKPOINT_INSTRUCTION}), 1);
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	text_poke(p->addr, &p->opcode, 1);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, (p->ainsn.boostable == 1));
		p->ainsn.insn = NULL;
	}
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.old_flags = kcb->kprobe_old_flags;
	kcb->prev_kprobe.saved_flags = kcb->kprobe_saved_flags;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_old_flags = kcb->prev_kprobe.old_flags;
	kcb->kprobe_saved_flags = kcb->prev_kprobe.saved_flags;
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
				struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = p;
	kcb->kprobe_saved_flags = kcb->kprobe_old_flags
		= (regs->flags & (X86_EFLAGS_TF | X86_EFLAGS_IF));
	if (is_IF_modifier(p->ainsn.insn))
		kcb->kprobe_saved_flags &= ~X86_EFLAGS_IF;
}

static void __kprobes clear_btf(void)
{
	if (test_thread_flag(TIF_BLOCKSTEP)) {
		unsigned long debugctl = get_debugctlmsr();

		debugctl &= ~DEBUGCTLMSR_BTF;
		update_debugctlmsr(debugctl);
	}
}

static void __kprobes restore_btf(void)
{
	if (test_thread_flag(TIF_BLOCKSTEP)) {
		unsigned long debugctl = get_debugctlmsr();

		debugctl |= DEBUGCTLMSR_BTF;
		update_debugctlmsr(debugctl);
	}
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	unsigned long *sara = stack_addr(regs);

	ri->ret_addr = (kprobe_opcode_t *) *sara;

	/* Replace the return addr with trampoline addr */
	*sara = (unsigned long) &kretprobe_trampoline;
}

#ifdef CONFIG_OPTPROBES
static int  __kprobes setup_detour_execution(struct kprobe *p,
					     struct pt_regs *regs,
					     int reenter);
#else
#define setup_detour_execution(p, regs, reenter) (0)
#endif

static void __kprobes setup_singlestep(struct kprobe *p, struct pt_regs *regs,
				       struct kprobe_ctlblk *kcb, int reenter)
{
	if (setup_detour_execution(p, regs, reenter))
		return;

#if !defined(CONFIG_PREEMPT)
	if (p->ainsn.boostable == 1 && !p->post_handler) {
		/* Boost up -- we can execute copied instructions directly */
		if (!reenter)
			reset_current_kprobe();
		/*
		 * Reentering boosted probe doesn't reset current_kprobe,
		 * nor set current_kprobe, because it doesn't use single
		 * stepping.
		 */
		regs->ip = (unsigned long)p->ainsn.insn;
		preempt_enable_no_resched();
		return;
	}
#endif
	if (reenter) {
		save_previous_kprobe(kcb);
		set_current_kprobe(p, regs, kcb);
		kcb->kprobe_status = KPROBE_REENTER;
	} else
		kcb->kprobe_status = KPROBE_HIT_SS;
	/* Prepare real single stepping */
	clear_btf();
	regs->flags |= X86_EFLAGS_TF;
	regs->flags &= ~X86_EFLAGS_IF;
	/* single step inline if the instruction is an int3 */
	if (p->opcode == BREAKPOINT_INSTRUCTION)
		regs->ip = (unsigned long)p->addr;
	else
		regs->ip = (unsigned long)p->ainsn.insn;
}

/*
 * We have reentered the kprobe_handler(), since another probe was hit while
 * within the handler. We save the original kprobes variables and just single
 * step on the instruction of the new probe without calling any user handlers.
 */
static int __kprobes reenter_kprobe(struct kprobe *p, struct pt_regs *regs,
				    struct kprobe_ctlblk *kcb)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		kprobes_inc_nmissed_count(p);
		setup_singlestep(p, regs, kcb, 1);
		break;
	case KPROBE_HIT_SS:
		/* A probe has been hit in the codepath leading up to, or just
		 * after, single-stepping of a probed instruction. This entire
		 * codepath should strictly reside in .kprobes.text section.
		 * Raise a BUG or we'll continue in an endless reentering loop
		 * and eventually a stack overflow.
		 */
		printk(KERN_WARNING "Unrecoverable kprobe detected at %p.\n",
		       p->addr);
		dump_kprobe(p);
		BUG();
	default:
		/* impossible cases */
		WARN_ON(1);
		return 0;
	}

	return 1;
}

/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate and they
 * remain disabled throughout this function.
 */
static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	kprobe_opcode_t *addr;
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;

	addr = (kprobe_opcode_t *)(regs->ip - sizeof(kprobe_opcode_t));
	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing. We conditionally
	 * re-enable preemption at the end of this function,
	 * and also in reenter_kprobe() and setup_singlestep().
	 */
	preempt_disable();

	kcb = get_kprobe_ctlblk();
	p = get_kprobe(addr);

	if (p) {
		if (kprobe_running()) {
			if (reenter_kprobe(p, regs, kcb))
				return 1;
		} else {
			set_current_kprobe(p, regs, kcb);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;

			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with normal processing.  If we have a
			 * pre-handler and it returned non-zero, it prepped
			 * for calling the break_handler below on re-entry
			 * for jprobe processing, so get out doing nothing
			 * more here.
			 */
			if (!p->pre_handler || !p->pre_handler(p, regs))
				setup_singlestep(p, regs, kcb, 0);
			return 1;
		}
	} else if (*addr != BREAKPOINT_INSTRUCTION) {
		/*
		 * The breakpoint instruction was removed right
		 * after we hit it.  Another cpu has removed
		 * either a probepoint or a debugger breakpoint
		 * at this address.  In either case, no further
		 * handling of this interrupt is appropriate.
		 * Back up over the (now missing) int3 and run
		 * the original instruction.
		 */
		regs->ip = (unsigned long)addr;
		preempt_enable_no_resched();
		return 1;
	} else if (kprobe_running()) {
		p = __get_cpu_var(current_kprobe);
		if (p->break_handler && p->break_handler(p, regs)) {
			setup_singlestep(p, regs, kcb, 0);
			return 1;
		}
	} /* else: not a kprobe fault; let the kernel handle it */

	preempt_enable_no_resched();
	return 0;
}

#ifdef CONFIG_X86_64
#define SAVE_REGS_STRING		\
	/* Skip cs, ip, orig_ax. */	\
	"	subq $24, %rsp\n"	\
	"	pushq %rdi\n"		\
	"	pushq %rsi\n"		\
	"	pushq %rdx\n"		\
	"	pushq %rcx\n"		\
	"	pushq %rax\n"		\
	"	pushq %r8\n"		\
	"	pushq %r9\n"		\
	"	pushq %r10\n"		\
	"	pushq %r11\n"		\
	"	pushq %rbx\n"		\
	"	pushq %rbp\n"		\
	"	pushq %r12\n"		\
	"	pushq %r13\n"		\
	"	pushq %r14\n"		\
	"	pushq %r15\n"
#define RESTORE_REGS_STRING		\
	"	popq %r15\n"		\
	"	popq %r14\n"		\
	"	popq %r13\n"		\
	"	popq %r12\n"		\
	"	popq %rbp\n"		\
	"	popq %rbx\n"		\
	"	popq %r11\n"		\
	"	popq %r10\n"		\
	"	popq %r9\n"		\
	"	popq %r8\n"		\
	"	popq %rax\n"		\
	"	popq %rcx\n"		\
	"	popq %rdx\n"		\
	"	popq %rsi\n"		\
	"	popq %rdi\n"		\
	/* Skip orig_ax, ip, cs */	\
	"	addq $24, %rsp\n"
#else
#define SAVE_REGS_STRING		\
	/* Skip cs, ip, orig_ax and gs. */	\
	"	subl $16, %esp\n"	\
	"	pushl %fs\n"		\
	"	pushl %es\n"		\
	"	pushl %ds\n"		\
	"	pushl %eax\n"		\
	"	pushl %ebp\n"		\
	"	pushl %edi\n"		\
	"	pushl %esi\n"		\
	"	pushl %edx\n"		\
	"	pushl %ecx\n"		\
	"	pushl %ebx\n"
#define RESTORE_REGS_STRING		\
	"	popl %ebx\n"		\
	"	popl %ecx\n"		\
	"	popl %edx\n"		\
	"	popl %esi\n"		\
	"	popl %edi\n"		\
	"	popl %ebp\n"		\
	"	popl %eax\n"		\
	/* Skip ds, es, fs, gs, orig_ax, and ip. Note: don't pop cs here*/\
	"	addl $24, %esp\n"
#endif

/*
 * When a retprobed function returns, this code saves registers and
 * calls trampoline_handler() runs, which calls the kretprobe's handler.
 */
static void __used __kprobes kretprobe_trampoline_holder(void)
{
	asm volatile (
			".global kretprobe_trampoline\n"
			"kretprobe_trampoline: \n"
#ifdef CONFIG_X86_64
			/* We don't bother saving the ss register */
			"	pushq %rsp\n"
			"	pushfq\n"
			SAVE_REGS_STRING
			"	movq %rsp, %rdi\n"
			"	call trampoline_handler\n"
			/* Replace saved sp with true return address. */
			"	movq %rax, 152(%rsp)\n"
			RESTORE_REGS_STRING
			"	popfq\n"
#else
			"	pushf\n"
			SAVE_REGS_STRING
			"	movl %esp, %eax\n"
			"	call trampoline_handler\n"
			/* Move flags to cs */
			"	movl 56(%esp), %edx\n"
			"	movl %edx, 52(%esp)\n"
			/* Replace saved flags with true return address. */
			"	movl %eax, 56(%esp)\n"
			RESTORE_REGS_STRING
			"	popf\n"
#endif
			"	ret\n");
}

/*
 * Called from kretprobe_trampoline
 */
static __used __kprobes void *trampoline_handler(struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address = (unsigned long)&kretprobe_trampoline;
	kprobe_opcode_t *correct_ret_addr = NULL;

	INIT_HLIST_HEAD(&empty_rp);
	kretprobe_hash_lock(current, &head, &flags);
	/* fixup registers */
#ifdef CONFIG_X86_64
	regs->cs = __KERNEL_CS;
#else
	regs->cs = __KERNEL_CS | get_kernel_rpl();
	regs->gs = 0;
#endif
	regs->ip = trampoline_address;
	regs->orig_ax = ~0UL;

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because multiple functions in the call path have
	 * return probes installed on them, and/or more than one
	 * return probe was registered for a target function.
	 *
	 * We can handle this because:
	 *     - instances are always pushed into the head of the list
	 *     - when multiple return probes are registered for the same
	 *	 function, the (chronologically) first instance's ret_addr
	 *	 will be the real return address, and all the rest will
	 *	 point to kretprobe_trampoline.
	 */
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		orig_ret_address = (unsigned long)ri->ret_addr;

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_assert(ri, orig_ret_address, trampoline_address);

	correct_ret_addr = ri->ret_addr;
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		orig_ret_address = (unsigned long)ri->ret_addr;
		if (ri->rp && ri->rp->handler) {
			__get_cpu_var(current_kprobe) = &ri->rp->kp;
			get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
			ri->ret_addr = correct_ret_addr;
			ri->rp->handler(ri, regs);
			__get_cpu_var(current_kprobe) = NULL;
		}

		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_hash_unlock(current, &flags);

	hlist_for_each_entry_safe(ri, node, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
	return (void *)orig_ret_address;
}

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "int 3"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 *
 * This function prepares to return from the post-single-step
 * interrupt.  We have to fix up the stack as follows:
 *
 * 0) Except in the case of absolute or indirect jump or call instructions,
 * the new ip is relative to the copied instruction.  We need to make
 * it relative to the original instruction.
 *
 * 1) If the single-stepped instruction was pushfl, then the TF and IF
 * flags are set in the just-pushed flags, and may need to be cleared.
 *
 * 2) If the single-stepped instruction was a call, the return address
 * that is atop the stack is the address following the copied instruction.
 * We need to make it the address following the original instruction.
 *
 * If this is the first time we've single-stepped the instruction at
 * this probepoint, and the instruction is boostable, boost it: add a
 * jump instruction after the copied instruction, that jumps to the next
 * instruction after the probepoint.
 */
static void __kprobes resume_execution(struct kprobe *p,
		struct pt_regs *regs, struct kprobe_ctlblk *kcb)
{
	unsigned long *tos = stack_addr(regs);
	unsigned long copy_ip = (unsigned long)p->ainsn.insn;
	unsigned long orig_ip = (unsigned long)p->addr;
	kprobe_opcode_t *insn = p->ainsn.insn;

	/* Skip prefixes */
	insn = skip_prefixes(insn);

	regs->flags &= ~X86_EFLAGS_TF;
	switch (*insn) {
	case 0x9c:	/* pushfl */
		*tos &= ~(X86_EFLAGS_TF | X86_EFLAGS_IF);
		*tos |= kcb->kprobe_old_flags;
		break;
	case 0xc2:	/* iret/ret/lret */
	case 0xc3:
	case 0xca:
	case 0xcb:
	case 0xcf:
	case 0xea:	/* jmp absolute -- ip is correct */
		/* ip is already adjusted, no more changes required */
		p->ainsn.boostable = 1;
		goto no_change;
	case 0xe8:	/* call relative - Fix return addr */
		*tos = orig_ip + (*tos - copy_ip);
		break;
#ifdef CONFIG_X86_32
	case 0x9a:	/* call absolute -- same as call absolute, indirect */
		*tos = orig_ip + (*tos - copy_ip);
		goto no_change;
#endif
	case 0xff:
		if ((insn[1] & 0x30) == 0x10) {
			/*
			 * call absolute, indirect
			 * Fix return addr; ip is correct.
			 * But this is not boostable
			 */
			*tos = orig_ip + (*tos - copy_ip);
			goto no_change;
		} else if (((insn[1] & 0x31) == 0x20) ||
			   ((insn[1] & 0x31) == 0x21)) {
			/*
			 * jmp near and far, absolute indirect
			 * ip is correct. And this is boostable
			 */
			p->ainsn.boostable = 1;
			goto no_change;
		}
	default:
		break;
	}

	if (p->ainsn.boostable == 0) {
		if ((regs->ip > copy_ip) &&
		    (regs->ip - copy_ip) + 5 < MAX_INSN_SIZE) {
			/*
			 * These instructions can be executed directly if it
			 * jumps back to correct address.
			 */
			synthesize_reljump((void *)regs->ip,
				(void *)orig_ip + (regs->ip - copy_ip));
			p->ainsn.boostable = 1;
		} else {
			p->ainsn.boostable = -1;
		}
	}

	regs->ip += orig_ip - copy_ip;

no_change:
	restore_btf();
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate and they
 * remain disabled throughout this function.
 */
static int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (!cur)
		return 0;

	resume_execution(cur, regs, kcb);
	regs->flags |= kcb->kprobe_saved_flags;

	if ((kcb->kprobe_status != KPROBE_REENTER) && cur->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	/* Restore back the original saved kprobes variables and continue. */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		goto out;
	}
	reset_current_kprobe();
out:
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, flags
	 * will have TF set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (regs->flags & X86_EFLAGS_TF)
		return 0;

	return 1;
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the ip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->ip = (unsigned long)cur->addr;
		regs->flags |= kcb->kprobe_old_flags;
		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();
		preempt_enable_no_resched();
		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * We increment the nmissed count for accounting,
		 * we can also use npre/npostfault count for accounting
		 * these specific fault cases.
		 */
		kprobes_inc_nmissed_count(cur);

		/*
		 * We come here because instructions in the pre/post
		 * handler caused the page_fault, this could happen
		 * if handler tries to access user space by
		 * copy_from_user(), get_user() etc. Let the
		 * user-specified handler try to fix it first.
		 */
		if (cur->fault_handler && cur->fault_handler(cur, regs, trapnr))
			return 1;

		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if (fixup_exception(regs))
			return 1;

		/*
		 * fixup routine could not handle it,
		 * Let do_page_fault() fix it.
		 */
		break;
	default:
		break;
	}
	return 0;
}

/*
 * Wrapper routine for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = data;
	int ret = NOTIFY_DONE;

	if (args->regs && user_mode_vm(args->regs))
		return ret;

	switch (val) {
	case DIE_INT3:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_DEBUG:
		if (post_kprobe_handler(args->regs)) {
			/*
			 * Reset the BS bit in dr6 (pointed by args->err) to
			 * denote completion of processing
			 */
			(*(unsigned long *)ERR_PTR(args->err)) &= ~DR_STEP;
			ret = NOTIFY_STOP;
		}
		break;
	case DIE_GPF:
		/*
		 * To be potentially processing a kprobe fault and to
		 * trust the result from kprobe_running(), we have
		 * be non-preemptible.
		 */
		if (!preemptible() && kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	return ret;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	unsigned long addr;
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	kcb->jprobe_saved_regs = *regs;
	kcb->jprobe_saved_sp = stack_addr(regs);
	addr = (unsigned long)(kcb->jprobe_saved_sp);

	/*
	 * As Linus pointed out, gcc assumes that the callee
	 * owns the argument space and could overwrite it, e.g.
	 * tailcall optimization. So, to be absolutely safe
	 * we also save and restore enough stack bytes to cover
	 * the argument area.
	 */
	memcpy(kcb->jprobes_stack, (kprobe_opcode_t *)addr,
	       MIN_STACK_SIZE(addr));
	regs->flags &= ~X86_EFLAGS_IF;
	trace_hardirqs_off();
	regs->ip = (unsigned long)(jp->entry);
	return 1;
}

void __kprobes jprobe_return(void)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	asm volatile (
#ifdef CONFIG_X86_64
			"       xchg   %%rbx,%%rsp	\n"
#else
			"       xchgl   %%ebx,%%esp	\n"
#endif
			"       int3			\n"
			"       .globl jprobe_return_end\n"
			"       jprobe_return_end:	\n"
			"       nop			\n"::"b"
			(kcb->jprobe_saved_sp):"memory");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	u8 *addr = (u8 *) (regs->ip - 1);
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if ((addr > (u8 *) jprobe_return) &&
	    (addr < (u8 *) jprobe_return_end)) {
		if (stack_addr(regs) != kcb->jprobe_saved_sp) {
			struct pt_regs *saved_regs = &kcb->jprobe_saved_regs;
			printk(KERN_ERR
			       "current sp %p does not match saved sp %p\n",
			       stack_addr(regs), kcb->jprobe_saved_sp);
			printk(KERN_ERR "Saved registers for jprobe %p\n", jp);
			show_registers(saved_regs);
			printk(KERN_ERR "Current registers\n");
			show_registers(regs);
			BUG();
		}
		*regs = kcb->jprobe_saved_regs;
		memcpy((kprobe_opcode_t *)(kcb->jprobe_saved_sp),
		       kcb->jprobes_stack,
		       MIN_STACK_SIZE(kcb->jprobe_saved_sp));
		preempt_enable_no_resched();
		return 1;
	}
	return 0;
}


#ifdef CONFIG_OPTPROBES

/* Insert a call instruction at address 'from', which calls address 'to'.*/
static void __kprobes synthesize_relcall(void *from, void *to)
{
	__synthesize_relative_insn(from, to, RELATIVECALL_OPCODE);
}

/* Insert a move instruction which sets a pointer to eax/rdi (1st arg). */
static void __kprobes synthesize_set_arg1(kprobe_opcode_t *addr,
					  unsigned long val)
{
#ifdef CONFIG_X86_64
	*addr++ = 0x48;
	*addr++ = 0xbf;
#else
	*addr++ = 0xb8;
#endif
	*(unsigned long *)addr = val;
}

static void __used __kprobes kprobes_optinsn_template_holder(void)
{
	asm volatile (
			".global optprobe_template_entry\n"
			"optprobe_template_entry: \n"
#ifdef CONFIG_X86_64
			/* We don't bother saving the ss register */
			"	pushq %rsp\n"
			"	pushfq\n"
			SAVE_REGS_STRING
			"	movq %rsp, %rsi\n"
			".global optprobe_template_val\n"
			"optprobe_template_val: \n"
			ASM_NOP5
			ASM_NOP5
			".global optprobe_template_call\n"
			"optprobe_template_call: \n"
			ASM_NOP5
			/* Move flags to rsp */
			"	movq 144(%rsp), %rdx\n"
			"	movq %rdx, 152(%rsp)\n"
			RESTORE_REGS_STRING
			/* Skip flags entry */
			"	addq $8, %rsp\n"
			"	popfq\n"
#else /* CONFIG_X86_32 */
			"	pushf\n"
			SAVE_REGS_STRING
			"	movl %esp, %edx\n"
			".global optprobe_template_val\n"
			"optprobe_template_val: \n"
			ASM_NOP5
			".global optprobe_template_call\n"
			"optprobe_template_call: \n"
			ASM_NOP5
			RESTORE_REGS_STRING
			"	addl $4, %esp\n"	/* skip cs */
			"	popf\n"
#endif
			".global optprobe_template_end\n"
			"optprobe_template_end: \n");
}

#define TMPL_MOVE_IDX \
	((long)&optprobe_template_val - (long)&optprobe_template_entry)
#define TMPL_CALL_IDX \
	((long)&optprobe_template_call - (long)&optprobe_template_entry)
#define TMPL_END_IDX \
	((long)&optprobe_template_end - (long)&optprobe_template_entry)

#define INT3_SIZE sizeof(kprobe_opcode_t)

/* Optimized kprobe call back function: called from optinsn */
static void __kprobes optimized_callback(struct optimized_kprobe *op,
					 struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	/* This is possible if op is under delayed unoptimizing */
	if (kprobe_disabled(&op->kp))
		return;

	preempt_disable();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(&op->kp);
	} else {
		/* Save skipped registers */
#ifdef CONFIG_X86_64
		regs->cs = __KERNEL_CS;
#else
		regs->cs = __KERNEL_CS | get_kernel_rpl();
		regs->gs = 0;
#endif
		regs->ip = (unsigned long)op->kp.addr + INT3_SIZE;
		regs->orig_ax = ~0UL;

		__get_cpu_var(current_kprobe) = &op->kp;
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		opt_pre_handler(&op->kp, regs);
		__get_cpu_var(current_kprobe) = NULL;
	}
	preempt_enable_no_resched();
}

static int __kprobes copy_optimized_instructions(u8 *dest, u8 *src)
{
	int len = 0, ret;

	while (len < RELATIVEJUMP_SIZE) {
		ret = __copy_instruction(dest + len, src + len, 1);
		if (!ret || !can_boost(dest + len))
			return -EINVAL;
		len += ret;
	}
	/* Check whether the address range is reserved */
	if (ftrace_text_reserved(src, src + len - 1) ||
	    alternatives_text_reserved(src, src + len - 1) ||
	    jump_label_text_reserved(src, src + len - 1))
		return -EBUSY;

	return len;
}

/* Check whether insn is indirect jump */
static int __kprobes insn_is_indirect_jump(struct insn *insn)
{
	return ((insn->opcode.bytes[0] == 0xff &&
		(X86_MODRM_REG(insn->modrm.value) & 6) == 4) || /* Jump */
		insn->opcode.bytes[0] == 0xea);	/* Segment based jump */
}

/* Check whether insn jumps into specified address range */
static int insn_jump_into_range(struct insn *insn, unsigned long start, int len)
{
	unsigned long target = 0;

	switch (insn->opcode.bytes[0]) {
	case 0xe0:	/* loopne */
	case 0xe1:	/* loope */
	case 0xe2:	/* loop */
	case 0xe3:	/* jcxz */
	case 0xe9:	/* near relative jump */
	case 0xeb:	/* short relative jump */
		break;
	case 0x0f:
		if ((insn->opcode.bytes[1] & 0xf0) == 0x80) /* jcc near */
			break;
		return 0;
	default:
		if ((insn->opcode.bytes[0] & 0xf0) == 0x70) /* jcc short */
			break;
		return 0;
	}
	target = (unsigned long)insn->next_byte + insn->immediate.value;

	return (start <= target && target <= start + len);
}

/* Decode whole function to ensure any instructions don't jump into target */
static int __kprobes can_optimize(unsigned long paddr)
{
	int ret;
	unsigned long addr, size = 0, offset = 0;
	struct insn insn;
	kprobe_opcode_t buf[MAX_INSN_SIZE];

	/* Lookup symbol including addr */
	if (!kallsyms_lookup_size_offset(paddr, &size, &offset))
		return 0;

	/* Check there is enough space for a relative jump. */
	if (size - offset < RELATIVEJUMP_SIZE)
		return 0;

	/* Decode instructions */
	addr = paddr - offset;
	while (addr < paddr - offset + size) { /* Decode until function end */
		if (search_exception_tables(addr))
			/*
			 * Since some fixup code will jumps into this function,
			 * we can't optimize kprobe in this function.
			 */
			return 0;
		kernel_insn_init(&insn, (void *)addr);
		insn_get_opcode(&insn);
		if (insn.opcode.bytes[0] == BREAKPOINT_INSTRUCTION) {
			ret = recover_probed_instruction(buf, addr);
			if (ret)
				return 0;
			kernel_insn_init(&insn, buf);
		}
		insn_get_length(&insn);
		/* Recover address */
		insn.kaddr = (void *)addr;
		insn.next_byte = (void *)(addr + insn.length);
		/* Check any instructions don't jump into target */
		if (insn_is_indirect_jump(&insn) ||
		    insn_jump_into_range(&insn, paddr + INT3_SIZE,
					 RELATIVE_ADDR_SIZE))
			return 0;
		addr += insn.length;
	}

	return 1;
}

/* Check optimized_kprobe can actually be optimized. */
int __kprobes arch_check_optimized_kprobe(struct optimized_kprobe *op)
{
	int i;
	struct kprobe *p;

	for (i = 1; i < op->optinsn.size; i++) {
		p = get_kprobe(op->kp.addr + i);
		if (p && !kprobe_disabled(p))
			return -EEXIST;
	}

	return 0;
}

/* Check the addr is within the optimized instructions. */
int __kprobes arch_within_optimized_kprobe(struct optimized_kprobe *op,
					   unsigned long addr)
{
	return ((unsigned long)op->kp.addr <= addr &&
		(unsigned long)op->kp.addr + op->optinsn.size > addr);
}

/* Free optimized instruction slot */
static __kprobes
void __arch_remove_optimized_kprobe(struct optimized_kprobe *op, int dirty)
{
	if (op->optinsn.insn) {
		free_optinsn_slot(op->optinsn.insn, dirty);
		op->optinsn.insn = NULL;
		op->optinsn.size = 0;
	}
}

void __kprobes arch_remove_optimized_kprobe(struct optimized_kprobe *op)
{
	__arch_remove_optimized_kprobe(op, 1);
}

/*
 * Copy replacing target instructions
 * Target instructions MUST be relocatable (checked inside)
 */
int __kprobes arch_prepare_optimized_kprobe(struct optimized_kprobe *op)
{
	u8 *buf;
	int ret;
	long rel;

	if (!can_optimize((unsigned long)op->kp.addr))
		return -EILSEQ;

	op->optinsn.insn = get_optinsn_slot();
	if (!op->optinsn.insn)
		return -ENOMEM;

	/*
	 * Verify if the address gap is in 2GB range, because this uses
	 * a relative jump.
	 */
	rel = (long)op->optinsn.insn - (long)op->kp.addr + RELATIVEJUMP_SIZE;
	if (abs(rel) > 0x7fffffff)
		return -ERANGE;

	buf = (u8 *)op->optinsn.insn;

	/* Copy instructions into the out-of-line buffer */
	ret = copy_optimized_instructions(buf + TMPL_END_IDX, op->kp.addr);
	if (ret < 0) {
		__arch_remove_optimized_kprobe(op, 0);
		return ret;
	}
	op->optinsn.size = ret;

	/* Copy arch-dep-instance from template */
	memcpy(buf, &optprobe_template_entry, TMPL_END_IDX);

	/* Set probe information */
	synthesize_set_arg1(buf + TMPL_MOVE_IDX, (unsigned long)op);

	/* Set probe function call */
	synthesize_relcall(buf + TMPL_CALL_IDX, optimized_callback);

	/* Set returning jmp instruction at the tail of out-of-line buffer */
	synthesize_reljump(buf + TMPL_END_IDX + op->optinsn.size,
			   (u8 *)op->kp.addr + op->optinsn.size);

	flush_icache_range((unsigned long) buf,
			   (unsigned long) buf + TMPL_END_IDX +
			   op->optinsn.size + RELATIVEJUMP_SIZE);
	return 0;
}

#define MAX_OPTIMIZE_PROBES 256
static struct text_poke_param *jump_poke_params;
static struct jump_poke_buffer {
	u8 buf[RELATIVEJUMP_SIZE];
} *jump_poke_bufs;

static void __kprobes setup_optimize_kprobe(struct text_poke_param *tprm,
					    u8 *insn_buf,
					    struct optimized_kprobe *op)
{
	s32 rel = (s32)((long)op->optinsn.insn -
			((long)op->kp.addr + RELATIVEJUMP_SIZE));

	/* Backup instructions which will be replaced by jump address */
	memcpy(op->optinsn.copied_insn, op->kp.addr + INT3_SIZE,
	       RELATIVE_ADDR_SIZE);

	insn_buf[0] = RELATIVEJUMP_OPCODE;
	*(s32 *)(&insn_buf[1]) = rel;

	tprm->addr = op->kp.addr;
	tprm->opcode = insn_buf;
	tprm->len = RELATIVEJUMP_SIZE;
}

/*
 * Replace breakpoints (int3) with relative jumps.
 * Caller must call with locking kprobe_mutex and text_mutex.
 */
void __kprobes arch_optimize_kprobes(struct list_head *oplist)
{
	struct optimized_kprobe *op, *tmp;
	int c = 0;

	list_for_each_entry_safe(op, tmp, oplist, list) {
		WARN_ON(kprobe_disabled(&op->kp));
		/* Setup param */
		setup_optimize_kprobe(&jump_poke_params[c],
				      jump_poke_bufs[c].buf, op);
		list_del_init(&op->list);
		if (++c >= MAX_OPTIMIZE_PROBES)
			break;
	}

	/*
	 * text_poke_smp doesn't support NMI/MCE code modifying.
	 * However, since kprobes itself also doesn't support NMI/MCE
	 * code probing, it's not a problem.
	 */
	text_poke_smp_batch(jump_poke_params, c);
}

/* Replace a relative jump with a breakpoint (int3).  */
void __kprobes arch_unoptimize_kprobe(struct optimized_kprobe *op)
{
	u8 buf[RELATIVEJUMP_SIZE];

	/* Set int3 to first byte for kprobes */
	buf[0] = BREAKPOINT_INSTRUCTION;
	memcpy(buf + 1, op->optinsn.copied_insn, RELATIVE_ADDR_SIZE);
	text_poke_smp(op->kp.addr, buf, RELATIVEJUMP_SIZE);
}

static int  __kprobes setup_detour_execution(struct kprobe *p,
					     struct pt_regs *regs,
					     int reenter)
{
	struct optimized_kprobe *op;

	if (p->flags & KPROBE_FLAG_OPTIMIZED) {
		/* This kprobe is really able to run optimized path. */
		op = container_of(p, struct optimized_kprobe, kp);
		/* Detour through copied instructions */
		regs->ip = (unsigned long)op->optinsn.insn + TMPL_END_IDX;
		if (!reenter)
			reset_current_kprobe();
		preempt_enable_no_resched();
		return 1;
	}
	return 0;
}

static int __kprobes init_poke_params(void)
{
	/* Allocate code buffer and parameter array */
	jump_poke_bufs = kmalloc(sizeof(struct jump_poke_buffer) *
				 MAX_OPTIMIZE_PROBES, GFP_KERNEL);
	if (!jump_poke_bufs)
		return -ENOMEM;

	jump_poke_params = kmalloc(sizeof(struct text_poke_param) *
				   MAX_OPTIMIZE_PROBES, GFP_KERNEL);
	if (!jump_poke_params) {
		kfree(jump_poke_bufs);
		jump_poke_bufs = NULL;
		return -ENOMEM;
	}

	return 0;
}
#else	/* !CONFIG_OPTPROBES */
static int __kprobes init_poke_params(void)
{
	return 0;
}
#endif

int __init arch_init_kprobes(void)
{
	return init_poke_params();
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}
