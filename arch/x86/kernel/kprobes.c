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

#include <asm/cacheflush.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/alternative.h>

void jprobe_return_end(void);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

#ifdef CONFIG_X86_64
#define stack_addr(regs) ((unsigned long *)regs->sp)
#else
/*
 * "&regs->sp" looks wrong, but it's correct for x86_32.  x86_32 CPUs
 * don't save the ss and esp registers if the CPU is already in kernel
 * mode when it traps.  So for kprobes, regs->sp and regs->ss are not
 * the [nonexistent] saved stack pointer and ss register, but rather
 * the top 8 bytes of the pre-int3 stack.  So &regs->sp happens to
 * point to the top of the pre-int3 stack.
 */
#define stack_addr(regs) ((unsigned long *)&regs->sp)
#endif

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
static const u32 onebyte_has_modrm[256 / 32] = {
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f          */
	/*      -----------------------------------------------         */
	W(0x00, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) | /* 00 */
	W(0x10, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) , /* 10 */
	W(0x20, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) | /* 20 */
	W(0x30, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0) , /* 30 */
	W(0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* 40 */
	W(0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 50 */
	W(0x60, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0) | /* 60 */
	W(0x70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 70 */
	W(0x80, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 80 */
	W(0x90, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 90 */
	W(0xa0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* a0 */
	W(0xb0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* b0 */
	W(0xc0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0) | /* c0 */
	W(0xd0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1) , /* d0 */
	W(0xe0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* e0 */
	W(0xf0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1)   /* f0 */
	/*      -----------------------------------------------         */
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f          */
};
static const u32 twobyte_has_modrm[256 / 32] = {
	/*      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f          */
	/*      -----------------------------------------------         */
	W(0x00, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1) | /* 0f */
	W(0x10, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0) , /* 1f */
	W(0x20, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1) | /* 2f */
	W(0x30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) , /* 3f */
	W(0x40, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 4f */
	W(0x50, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 5f */
	W(0x60, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* 6f */
	W(0x70, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1) , /* 7f */
	W(0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) | /* 8f */
	W(0x90, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* 9f */
	W(0xa0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1) | /* af */
	W(0xb0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1) , /* bf */
	W(0xc0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0) | /* cf */
	W(0xd0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) , /* df */
	W(0xe0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1) | /* ef */
	W(0xf0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)   /* ff */
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

/* Insert a jump instruction at address 'from', which jumps to address 'to'.*/
static void __kprobes set_jmp_op(void *from, void *to)
{
	struct __arch_jmp_op {
		char op;
		s32 raddr;
	} __attribute__((packed)) * jop;
	jop = (struct __arch_jmp_op *)from;
	jop->raddr = (s32)((long)(to) - ((long)(from) + 5));
	jop->op = RELATIVEJUMP_INSTRUCTION;
}

/*
 * Check for the REX prefix which can only exist on X86_64
 * X86_32 always returns 0
 */
static int __kprobes is_REX_prefix(kprobe_opcode_t *insn)
{
#ifdef CONFIG_X86_64
	if ((*insn & 0xf0) == 0x40)
		return 1;
#endif
	return 0;
}

/*
 * Returns non-zero if opcode is boostable.
 * RIP relative instructions are adjusted at copying time in 64 bits mode
 */
static int __kprobes can_boost(kprobe_opcode_t *opcodes)
{
	kprobe_opcode_t opcode;
	kprobe_opcode_t *orig_opcodes = opcodes;

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

/*
 * Returns non-zero if opcode modifies the interrupt flag.
 */
static int __kprobes is_IF_modifier(kprobe_opcode_t *insn)
{
	switch (*insn) {
	case 0xfa:		/* cli */
	case 0xfb:		/* sti */
	case 0xcf:		/* iret/iretd */
	case 0x9d:		/* popf/popfd */
		return 1;
	}

	/*
	 * on X86_64, 0x40-0x4f are REX prefixes so we need to look
	 * at the next byte instead.. but of course not recurse infinitely
	 */
	if (is_REX_prefix(insn))
		return is_IF_modifier(++insn);

	return 0;
}

/*
 * Adjust the displacement if the instruction uses the %rip-relative
 * addressing mode.
 * If it does, Return the address of the 32-bit displacement word.
 * If not, return null.
 * Only applicable to 64-bit x86.
 */
static void __kprobes fix_riprel(struct kprobe *p)
{
#ifdef CONFIG_X86_64
	u8 *insn = p->ainsn.insn;
	s64 disp;
	int need_modrm;

	/* Skip legacy instruction prefixes.  */
	while (1) {
		switch (*insn) {
		case 0x66:
		case 0x67:
		case 0x2e:
		case 0x3e:
		case 0x26:
		case 0x64:
		case 0x65:
		case 0x36:
		case 0xf0:
		case 0xf3:
		case 0xf2:
			++insn;
			continue;
		}
		break;
	}

	/* Skip REX instruction prefix.  */
	if (is_REX_prefix(insn))
		++insn;

	if (*insn == 0x0f) {
		/* Two-byte opcode.  */
		++insn;
		need_modrm = test_bit(*insn,
				      (unsigned long *)twobyte_has_modrm);
	} else
		/* One-byte opcode.  */
		need_modrm = test_bit(*insn,
				      (unsigned long *)onebyte_has_modrm);

	if (need_modrm) {
		u8 modrm = *++insn;
		if ((modrm & 0xc7) == 0x05) {
			/* %rip+disp32 addressing mode */
			/* Displacement follows ModRM byte.  */
			++insn;
			/*
			 * The copied instruction uses the %rip-relative
			 * addressing mode.  Adjust the displacement for the
			 * difference between the original location of this
			 * instruction and the location of the copy that will
			 * actually be run.  The tricky bit here is making sure
			 * that the sign extension happens correctly in this
			 * calculation, since we need a signed 32-bit result to
			 * be sign-extended to 64 bits when it's added to the
			 * %rip value and yield the same 64-bit result that the
			 * sign-extension of the original signed 32-bit
			 * displacement would have given.
			 */
			disp = (u8 *) p->addr + *((s32 *) insn) -
			       (u8 *) p->ainsn.insn;
			BUG_ON((s64) (s32) disp != disp); /* Sanity check.  */
			*(s32 *)insn = (s32) disp;
		}
	}
#endif
}

static void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));

	fix_riprel(p);

	if (can_boost(p->addr))
		p->ainsn.boostable = 0;
	else
		p->ainsn.boostable = -1;

	p->opcode = *p->addr;
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
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
	mutex_lock(&kprobe_mutex);
	free_insn_slot(p->ainsn.insn, (p->ainsn.boostable == 1));
	mutex_unlock(&kprobe_mutex);
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
	if (test_thread_flag(TIF_DEBUGCTLMSR))
		wrmsrl(MSR_IA32_DEBUGCTLMSR, 0);
}

static void __kprobes restore_btf(void)
{
	if (test_thread_flag(TIF_DEBUGCTLMSR))
		wrmsrl(MSR_IA32_DEBUGCTLMSR, current->thread.debugctlmsr);
}

static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	clear_btf();
	regs->flags |= X86_EFLAGS_TF;
	regs->flags &= ~X86_EFLAGS_IF;
	/* single step inline if the instruction is an int3 */
	if (p->opcode == BREAKPOINT_INSTRUCTION)
		regs->ip = (unsigned long)p->addr;
	else
		regs->ip = (unsigned long)p->ainsn.insn;
}

/* Called with kretprobe_lock held */
void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	unsigned long *sara = stack_addr(regs);

	ri->ret_addr = (kprobe_opcode_t *) *sara;

	/* Replace the return addr with trampoline addr */
	*sara = (unsigned long) &kretprobe_trampoline;
}

static void __kprobes setup_singlestep(struct kprobe *p, struct pt_regs *regs,
				       struct kprobe_ctlblk *kcb)
{
#if !defined(CONFIG_PREEMPT) || defined(CONFIG_PM)
	if (p->ainsn.boostable == 1 && !p->post_handler) {
		/* Boost up -- we can execute copied instructions directly */
		reset_current_kprobe();
		regs->ip = (unsigned long)p->ainsn.insn;
		preempt_enable_no_resched();
		return;
	}
#endif
	prepare_singlestep(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
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
#ifdef CONFIG_X86_64
		/* TODO: Provide re-entrancy from post_kprobes_handler() and
		 * avoid exception stack corruption while single-stepping on
		 * the instruction of the new probe.
		 */
		arch_disarm_kprobe(p);
		regs->ip = (unsigned long)p->addr;
		reset_current_kprobe();
		preempt_enable_no_resched();
		break;
#endif
	case KPROBE_HIT_ACTIVE:
		save_previous_kprobe(kcb);
		set_current_kprobe(p, regs, kcb);
		kprobes_inc_nmissed_count(p);
		prepare_singlestep(p, regs);
		kcb->kprobe_status = KPROBE_REENTER;
		break;
	case KPROBE_HIT_SS:
		if (p == kprobe_running()) {
			regs->flags &= ~TF_MASK;
			regs->flags |= kcb->kprobe_saved_flags;
			return 0;
		} else {
			/* A probe has been hit in the codepath leading up
			 * to, or just after, single-stepping of a probed
			 * instruction. This entire codepath should strictly
			 * reside in .kprobes.text section. Raise a warning
			 * to highlight this peculiar case.
			 */
		}
	default:
		/* impossible cases */
		WARN_ON(1);
		return 0;
	}

	return 1;
}

/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate and they
 * remain disabled thorough out this function.
 */
static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	kprobe_opcode_t *addr;
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;

	addr = (kprobe_opcode_t *)(regs->ip - sizeof(kprobe_opcode_t));
	if (*addr != BREAKPOINT_INSTRUCTION) {
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
		return 1;
	}

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
				setup_singlestep(p, regs, kcb);
			return 1;
		}
	} else if (kprobe_running()) {
		p = __get_cpu_var(current_kprobe);
		if (p->break_handler && p->break_handler(p, regs)) {
			setup_singlestep(p, regs, kcb);
			return 1;
		}
	} /* else: not a kprobe fault; let the kernel handle it */

	preempt_enable_no_resched();
	return 0;
}

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
			/*
			 * Skip cs, ip, orig_ax.
			 * trampoline_handler() will plug in these values
			 */
			"	subq $24, %rsp\n"
			"	pushq %rdi\n"
			"	pushq %rsi\n"
			"	pushq %rdx\n"
			"	pushq %rcx\n"
			"	pushq %rax\n"
			"	pushq %r8\n"
			"	pushq %r9\n"
			"	pushq %r10\n"
			"	pushq %r11\n"
			"	pushq %rbx\n"
			"	pushq %rbp\n"
			"	pushq %r12\n"
			"	pushq %r13\n"
			"	pushq %r14\n"
			"	pushq %r15\n"
			"	movq %rsp, %rdi\n"
			"	call trampoline_handler\n"
			/* Replace saved sp with true return address. */
			"	movq %rax, 152(%rsp)\n"
			"	popq %r15\n"
			"	popq %r14\n"
			"	popq %r13\n"
			"	popq %r12\n"
			"	popq %rbp\n"
			"	popq %rbx\n"
			"	popq %r11\n"
			"	popq %r10\n"
			"	popq %r9\n"
			"	popq %r8\n"
			"	popq %rax\n"
			"	popq %rcx\n"
			"	popq %rdx\n"
			"	popq %rsi\n"
			"	popq %rdi\n"
			/* Skip orig_ax, ip, cs */
			"	addq $24, %rsp\n"
			"	popfq\n"
#else
			"	pushf\n"
			/*
			 * Skip cs, ip, orig_ax.
			 * trampoline_handler() will plug in these values
			 */
			"	subl $12, %esp\n"
			"	pushl %fs\n"
			"	pushl %ds\n"
			"	pushl %es\n"
			"	pushl %eax\n"
			"	pushl %ebp\n"
			"	pushl %edi\n"
			"	pushl %esi\n"
			"	pushl %edx\n"
			"	pushl %ecx\n"
			"	pushl %ebx\n"
			"	movl %esp, %eax\n"
			"	call trampoline_handler\n"
			/* Move flags to cs */
			"	movl 52(%esp), %edx\n"
			"	movl %edx, 48(%esp)\n"
			/* Replace saved flags with true return address. */
			"	movl %eax, 52(%esp)\n"
			"	popl %ebx\n"
			"	popl %ecx\n"
			"	popl %edx\n"
			"	popl %esi\n"
			"	popl %edi\n"
			"	popl %ebp\n"
			"	popl %eax\n"
			/* Skip ip, orig_ax, es, ds, fs */
			"	addl $20, %esp\n"
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

	INIT_HLIST_HEAD(&empty_rp);
	spin_lock_irqsave(&kretprobe_lock, flags);
	head = kretprobe_inst_table_head(current);
	/* fixup registers */
#ifdef CONFIG_X86_64
	regs->cs = __KERNEL_CS;
#else
	regs->cs = __KERNEL_CS | get_kernel_rpl();
#endif
	regs->ip = trampoline_address;
	regs->orig_ax = ~0UL;

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because multiple functions in the call path have
	 * return probes installed on them, and/or more then one
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

		if (ri->rp && ri->rp->handler) {
			__get_cpu_var(current_kprobe) = &ri->rp->kp;
			get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
			ri->rp->handler(ri, regs);
			__get_cpu_var(current_kprobe) = NULL;
		}

		orig_ret_address = (unsigned long)ri->ret_addr;
		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_assert(ri, orig_ret_address, trampoline_address);

	spin_unlock_irqrestore(&kretprobe_lock, flags);

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

	/*skip the REX prefix*/
	if (is_REX_prefix(insn))
		insn++;

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
			set_jmp_op((void *)regs->ip,
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
 * remain disabled thoroughout this function.
 */
static int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (!cur)
		return 0;

	if ((kcb->kprobe_status != KPROBE_REENTER) && cur->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	resume_execution(cur, regs, kcb);
	regs->flags |= kcb->kprobe_saved_flags;
	trace_hardirqs_fixup_flags(regs->flags);

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
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
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

int __init arch_init_kprobes(void)
{
	return 0;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}
