/*
 *  Kernel Probes Jump Optimization (Optprobes)
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
 * Copyright (C) Hitachi Ltd., 2012
 * Copyright (C) Huawei Inc., 2014
 */

#include <linux/kprobes.h>
#include <linux/jump_label.h>
#include <asm/kprobes.h>
#include <asm/cacheflush.h>
/* for arm_gen_branch */
#include <asm/insn.h>
/* for patch_text */
#include <asm/patch.h>

#include "core.h"

/*
 * See register_usage_flags. If the probed instruction doesn't use PC,
 * we can copy it into template and have it executed directly without
 * simulation or emulation.
 */
#define ARM_REG_PC	15
#define can_kprobe_direct_exec(m)	(!test_bit(ARM_REG_PC, &(m)))

/*
 * NOTE: the first sub and add instruction will be modified according
 * to the stack cost of the instruction.
 */
asm (
			".global optprobe_template_entry\n"
			"optprobe_template_entry:\n"
			".global optprobe_template_sub_sp\n"
			"optprobe_template_sub_sp:"
			"	sub	sp, sp, #0xff\n"
			"	stmia	sp, {r0 - r14} \n"
			".global optprobe_template_add_sp\n"
			"optprobe_template_add_sp:"
			"	add	r3, sp, #0xff\n"
			"	str	r3, [sp, #52]\n"
			"	mrs	r4, cpsr\n"
			"	str	r4, [sp, #64]\n"
			"	mov	r1, sp\n"
			"	ldr	r0, 1f\n"
			"	ldr	r2, 2f\n"
			/*
			 * AEABI requires an 8-bytes alignment stack. If
			 * SP % 8 != 0 (SP % 4 == 0 should be ensured),
			 * alloc more bytes here.
			 */
			"	and	r4, sp, #4\n"
			"	sub	sp, sp, r4\n"
#if __LINUX_ARM_ARCH__ >= 5
			"	blx	r2\n"
#else
			"	mov     lr, pc\n"
			"	mov	pc, r2\n"
#endif
			"	add	sp, sp, r4\n"
			"	ldr	r1, [sp, #64]\n"
			"	tst	r1, #"__stringify(PSR_T_BIT)"\n"
			"	ldrne	r2, [sp, #60]\n"
			"	orrne	r2, #1\n"
			"	strne	r2, [sp, #60] @ set bit0 of PC for thumb\n"
			"	msr	cpsr_cxsf, r1\n"
			".global optprobe_template_restore_begin\n"
			"optprobe_template_restore_begin:\n"
			"	ldmia	sp, {r0 - r15}\n"
			".global optprobe_template_restore_orig_insn\n"
			"optprobe_template_restore_orig_insn:\n"
			"	nop\n"
			".global optprobe_template_restore_end\n"
			"optprobe_template_restore_end:\n"
			"	nop\n"
			".global optprobe_template_val\n"
			"optprobe_template_val:\n"
			"1:	.long 0\n"
			".global optprobe_template_call\n"
			"optprobe_template_call:\n"
			"2:	.long 0\n"
			".global optprobe_template_end\n"
			"optprobe_template_end:\n");

#define TMPL_VAL_IDX \
	((unsigned long *)&optprobe_template_val - (unsigned long *)&optprobe_template_entry)
#define TMPL_CALL_IDX \
	((unsigned long *)&optprobe_template_call - (unsigned long *)&optprobe_template_entry)
#define TMPL_END_IDX \
	((unsigned long *)&optprobe_template_end - (unsigned long *)&optprobe_template_entry)
#define TMPL_ADD_SP \
	((unsigned long *)&optprobe_template_add_sp - (unsigned long *)&optprobe_template_entry)
#define TMPL_SUB_SP \
	((unsigned long *)&optprobe_template_sub_sp - (unsigned long *)&optprobe_template_entry)
#define TMPL_RESTORE_BEGIN \
	((unsigned long *)&optprobe_template_restore_begin - (unsigned long *)&optprobe_template_entry)
#define TMPL_RESTORE_ORIGN_INSN \
	((unsigned long *)&optprobe_template_restore_orig_insn - (unsigned long *)&optprobe_template_entry)
#define TMPL_RESTORE_END \
	((unsigned long *)&optprobe_template_restore_end - (unsigned long *)&optprobe_template_entry)

/*
 * ARM can always optimize an instruction when using ARM ISA, except
 * instructions like 'str r0, [sp, r1]' which store to stack and unable
 * to determine stack space consumption statically.
 */
int arch_prepared_optinsn(struct arch_optimized_insn *optinsn)
{
	return optinsn->insn != NULL;
}

/*
 * In ARM ISA, kprobe opt always replace one instruction (4 bytes
 * aligned and 4 bytes long). It is impossible to encounter another
 * kprobe in the address range. So always return 0.
 */
int arch_check_optimized_kprobe(struct optimized_kprobe *op)
{
	return 0;
}

/* Caller must ensure addr & 3 == 0 */
static int can_optimize(struct kprobe *kp)
{
	if (kp->ainsn.stack_space < 0)
		return 0;
	/*
	 * 255 is the biggest imm can be used in 'sub r0, r0, #<imm>'.
	 * Number larger than 255 needs special encoding.
	 */
	if (kp->ainsn.stack_space > 255 - sizeof(struct pt_regs))
		return 0;
	return 1;
}

/* Free optimized instruction slot */
static void
__arch_remove_optimized_kprobe(struct optimized_kprobe *op, int dirty)
{
	if (op->optinsn.insn) {
		free_optinsn_slot(op->optinsn.insn, dirty);
		op->optinsn.insn = NULL;
	}
}

extern void kprobe_handler(struct pt_regs *regs);

static void
optimized_callback(struct optimized_kprobe *op, struct pt_regs *regs)
{
	unsigned long flags;
	struct kprobe *p = &op->kp;
	struct kprobe_ctlblk *kcb;

	/* Save skipped registers */
	regs->ARM_pc = (unsigned long)op->kp.addr;
	regs->ARM_ORIG_r0 = ~0UL;

	local_irq_save(flags);
	kcb = get_kprobe_ctlblk();

	if (kprobe_running()) {
		kprobes_inc_nmissed_count(&op->kp);
	} else {
		__this_cpu_write(current_kprobe, &op->kp);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		opt_pre_handler(&op->kp, regs);
		__this_cpu_write(current_kprobe, NULL);
	}

	/*
	 * We singlestep the replaced instruction only when it can't be
	 * executed directly during restore.
	 */
	if (!p->ainsn.kprobe_direct_exec)
		op->kp.ainsn.insn_singlestep(p->opcode, &p->ainsn, regs);

	local_irq_restore(flags);
}
NOKPROBE_SYMBOL(optimized_callback)

int arch_prepare_optimized_kprobe(struct optimized_kprobe *op, struct kprobe *orig)
{
	kprobe_opcode_t *code;
	unsigned long rel_chk;
	unsigned long val;
	unsigned long stack_protect = sizeof(struct pt_regs);

	if (!can_optimize(orig))
		return -EILSEQ;

	code = get_optinsn_slot();
	if (!code)
		return -ENOMEM;

	/*
	 * Verify if the address gap is in 32MiB range, because this uses
	 * a relative jump.
	 *
	 * kprobe opt use a 'b' instruction to branch to optinsn.insn.
	 * According to ARM manual, branch instruction is:
	 *
	 *   31  28 27           24 23             0
	 *  +------+---+---+---+---+----------------+
	 *  | cond | 1 | 0 | 1 | 0 |      imm24     |
	 *  +------+---+---+---+---+----------------+
	 *
	 * imm24 is a signed 24 bits integer. The real branch offset is computed
	 * by: imm32 = SignExtend(imm24:'00', 32);
	 *
	 * So the maximum forward branch should be:
	 *   (0x007fffff << 2) = 0x01fffffc =  0x1fffffc
	 * The maximum backword branch should be:
	 *   (0xff800000 << 2) = 0xfe000000 = -0x2000000
	 *
	 * We can simply check (rel & 0xfe000003):
	 *  if rel is positive, (rel & 0xfe000000) shoule be 0
	 *  if rel is negitive, (rel & 0xfe000000) should be 0xfe000000
	 *  the last '3' is used for alignment checking.
	 */
	rel_chk = (unsigned long)((long)code -
			(long)orig->addr + 8) & 0xfe000003;

	if ((rel_chk != 0) && (rel_chk != 0xfe000000)) {
		/*
		 * Different from x86, we free code buf directly instead of
		 * calling __arch_remove_optimized_kprobe() because
		 * we have not fill any field in op.
		 */
		free_optinsn_slot(code, 0);
		return -ERANGE;
	}

	/* Copy arch-dep-instance from template. */
	memcpy(code, (unsigned long *)&optprobe_template_entry,
			TMPL_END_IDX * sizeof(kprobe_opcode_t));

	/* Adjust buffer according to instruction. */
	BUG_ON(orig->ainsn.stack_space < 0);

	stack_protect += orig->ainsn.stack_space;

	/* Should have been filtered by can_optimize(). */
	BUG_ON(stack_protect > 255);

	/* Create a 'sub sp, sp, #<stack_protect>' */
	code[TMPL_SUB_SP] = __opcode_to_mem_arm(0xe24dd000 | stack_protect);
	/* Create a 'add r3, sp, #<stack_protect>' */
	code[TMPL_ADD_SP] = __opcode_to_mem_arm(0xe28d3000 | stack_protect);

	/* Set probe information */
	val = (unsigned long)op;
	code[TMPL_VAL_IDX] = val;

	/* Set probe function call */
	val = (unsigned long)optimized_callback;
	code[TMPL_CALL_IDX] = val;

	/* If possible, copy insn and have it executed during restore */
	orig->ainsn.kprobe_direct_exec = false;
	if (can_kprobe_direct_exec(orig->ainsn.register_usage_flags)) {
		kprobe_opcode_t final_branch = arm_gen_branch(
				(unsigned long)(&code[TMPL_RESTORE_END]),
				(unsigned long)(op->kp.addr) + 4);
		if (final_branch != 0) {
			/*
			 * Replace original 'ldmia sp, {r0 - r15}' with
			 * 'ldmia {r0 - r14}', restore all registers except pc.
			 */
			code[TMPL_RESTORE_BEGIN] = __opcode_to_mem_arm(0xe89d7fff);

			/* The original probed instruction */
			code[TMPL_RESTORE_ORIGN_INSN] = __opcode_to_mem_arm(orig->opcode);

			/* Jump back to next instruction */
			code[TMPL_RESTORE_END] = __opcode_to_mem_arm(final_branch);
			orig->ainsn.kprobe_direct_exec = true;
		}
	}

	flush_icache_range((unsigned long)code,
			   (unsigned long)(&code[TMPL_END_IDX]));

	/* Set op->optinsn.insn means prepared. */
	op->optinsn.insn = code;
	return 0;
}

void __kprobes arch_optimize_kprobes(struct list_head *oplist)
{
	struct optimized_kprobe *op, *tmp;

	list_for_each_entry_safe(op, tmp, oplist, list) {
		unsigned long insn;
		WARN_ON(kprobe_disabled(&op->kp));

		/*
		 * Backup instructions which will be replaced
		 * by jump address
		 */
		memcpy(op->optinsn.copied_insn, op->kp.addr,
				RELATIVEJUMP_SIZE);

		insn = arm_gen_branch((unsigned long)op->kp.addr,
				(unsigned long)op->optinsn.insn);
		BUG_ON(insn == 0);

		/*
		 * Make it a conditional branch if replaced insn
		 * is consitional
		 */
		insn = (__mem_to_opcode_arm(
			  op->optinsn.copied_insn[0]) & 0xf0000000) |
			(insn & 0x0fffffff);

		/*
		 * Similar to __arch_disarm_kprobe, operations which
		 * removing breakpoints must be wrapped by stop_machine
		 * to avoid racing.
		 */
		kprobes_remove_breakpoint(op->kp.addr, insn);

		list_del_init(&op->list);
	}
}

void arch_unoptimize_kprobe(struct optimized_kprobe *op)
{
	arch_arm_kprobe(&op->kp);
}

/*
 * Recover original instructions and breakpoints from relative jumps.
 * Caller must call with locking kprobe_mutex.
 */
void arch_unoptimize_kprobes(struct list_head *oplist,
			    struct list_head *done_list)
{
	struct optimized_kprobe *op, *tmp;

	list_for_each_entry_safe(op, tmp, oplist, list) {
		arch_unoptimize_kprobe(op);
		list_move(&op->list, done_list);
	}
}

int arch_within_optimized_kprobe(struct optimized_kprobe *op,
				unsigned long addr)
{
	return ((unsigned long)op->kp.addr <= addr &&
		(unsigned long)op->kp.addr + RELATIVEJUMP_SIZE > addr);
}

void arch_remove_optimized_kprobe(struct optimized_kprobe *op)
{
	__arch_remove_optimized_kprobe(op, 1);
}
