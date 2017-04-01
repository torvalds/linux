/*
 * Code for Kernel probes Jump optimization.
 *
 * Copyright 2017, Anju T, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kprobes.h>
#include <linux/jump_label.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <asm/kprobes.h>
#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <asm/code-patching.h>
#include <asm/sstep.h>
#include <asm/ppc-opcode.h>

#define TMPL_CALL_HDLR_IDX	\
	(optprobe_template_call_handler - optprobe_template_entry)
#define TMPL_EMULATE_IDX	\
	(optprobe_template_call_emulate - optprobe_template_entry)
#define TMPL_RET_IDX		\
	(optprobe_template_ret - optprobe_template_entry)
#define TMPL_OP_IDX		\
	(optprobe_template_op_address - optprobe_template_entry)
#define TMPL_INSN_IDX		\
	(optprobe_template_insn - optprobe_template_entry)
#define TMPL_END_IDX		\
	(optprobe_template_end - optprobe_template_entry)

DEFINE_INSN_CACHE_OPS(ppc_optinsn);

static bool insn_page_in_use;

static void *__ppc_alloc_insn_page(void)
{
	if (insn_page_in_use)
		return NULL;
	insn_page_in_use = true;
	return &optinsn_slot;
}

static void __ppc_free_insn_page(void *page __maybe_unused)
{
	insn_page_in_use = false;
}

struct kprobe_insn_cache kprobe_ppc_optinsn_slots = {
	.mutex = __MUTEX_INITIALIZER(kprobe_ppc_optinsn_slots.mutex),
	.pages = LIST_HEAD_INIT(kprobe_ppc_optinsn_slots.pages),
	/* insn_size initialized later */
	.alloc = __ppc_alloc_insn_page,
	.free = __ppc_free_insn_page,
	.nr_garbage = 0,
};

/*
 * Check if we can optimize this probe. Returns NIP post-emulation if this can
 * be optimized and 0 otherwise.
 */
static unsigned long can_optimize(struct kprobe *p)
{
	struct pt_regs regs;
	struct instruction_op op;
	unsigned long nip = 0;

	/*
	 * kprobe placed for kretprobe during boot time
	 * has a 'nop' instruction, which can be emulated.
	 * So further checks can be skipped.
	 */
	if (p->addr == (kprobe_opcode_t *)&kretprobe_trampoline)
		return (unsigned long)p->addr + sizeof(kprobe_opcode_t);

	/*
	 * We only support optimizing kernel addresses, but not
	 * module addresses.
	 *
	 * FIXME: Optimize kprobes placed in module addresses.
	 */
	if (!is_kernel_addr((unsigned long)p->addr))
		return 0;

	memset(&regs, 0, sizeof(struct pt_regs));
	regs.nip = (unsigned long)p->addr;
	regs.trap = 0x0;
	regs.msr = MSR_KERNEL;

	/*
	 * Kprobe placed in conditional branch instructions are
	 * not optimized, as we can't predict the nip prior with
	 * dummy pt_regs and can not ensure that the return branch
	 * from detour buffer falls in the range of address (i.e 32MB).
	 * A branch back from trampoline is set up in the detour buffer
	 * to the nip returned by the analyse_instr() here.
	 *
	 * Ensure that the instruction is not a conditional branch,
	 * and that can be emulated.
	 */
	if (!is_conditional_branch(*p->ainsn.insn) &&
			analyse_instr(&op, &regs, *p->ainsn.insn))
		nip = regs.nip;

	return nip;
}

static void optimized_callback(struct optimized_kprobe *op,
			       struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long flags;

	/* This is possible if op is under delayed unoptimizing */
	if (kprobe_disabled(&op->kp))
		return;

	local_irq_save(flags);
	hard_irq_disable();

	if (kprobe_running()) {
		kprobes_inc_nmissed_count(&op->kp);
	} else {
		__this_cpu_write(current_kprobe, &op->kp);
		regs->nip = (unsigned long)op->kp.addr;
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		opt_pre_handler(&op->kp, regs);
		__this_cpu_write(current_kprobe, NULL);
	}

	/*
	 * No need for an explicit __hard_irq_enable() here.
	 * local_irq_restore() will re-enable interrupts,
	 * if they were hard disabled.
	 */
	local_irq_restore(flags);
}
NOKPROBE_SYMBOL(optimized_callback);

void arch_remove_optimized_kprobe(struct optimized_kprobe *op)
{
	if (op->optinsn.insn) {
		free_ppc_optinsn_slot(op->optinsn.insn, 1);
		op->optinsn.insn = NULL;
	}
}

/*
 * emulate_step() requires insn to be emulated as
 * second parameter. Load register 'r4' with the
 * instruction.
 */
void patch_imm32_load_insns(unsigned int val, kprobe_opcode_t *addr)
{
	/* addis r4,0,(insn)@h */
	*addr++ = PPC_INST_ADDIS | ___PPC_RT(4) |
		  ((val >> 16) & 0xffff);

	/* ori r4,r4,(insn)@l */
	*addr = PPC_INST_ORI | ___PPC_RA(4) | ___PPC_RS(4) |
		(val & 0xffff);
}

/*
 * Generate instructions to load provided immediate 64-bit value
 * to register 'r3' and patch these instructions at 'addr'.
 */
void patch_imm64_load_insns(unsigned long val, kprobe_opcode_t *addr)
{
	/* lis r3,(op)@highest */
	*addr++ = PPC_INST_ADDIS | ___PPC_RT(3) |
		  ((val >> 48) & 0xffff);

	/* ori r3,r3,(op)@higher */
	*addr++ = PPC_INST_ORI | ___PPC_RA(3) | ___PPC_RS(3) |
		  ((val >> 32) & 0xffff);

	/* rldicr r3,r3,32,31 */
	*addr++ = PPC_INST_RLDICR | ___PPC_RA(3) | ___PPC_RS(3) |
		  __PPC_SH64(32) | __PPC_ME64(31);

	/* oris r3,r3,(op)@h */
	*addr++ = PPC_INST_ORIS | ___PPC_RA(3) | ___PPC_RS(3) |
		  ((val >> 16) & 0xffff);

	/* ori r3,r3,(op)@l */
	*addr = PPC_INST_ORI | ___PPC_RA(3) | ___PPC_RS(3) |
		(val & 0xffff);
}

int arch_prepare_optimized_kprobe(struct optimized_kprobe *op, struct kprobe *p)
{
	kprobe_opcode_t *buff, branch_op_callback, branch_emulate_step;
	kprobe_opcode_t *op_callback_addr, *emulate_step_addr;
	long b_offset;
	unsigned long nip;

	kprobe_ppc_optinsn_slots.insn_size = MAX_OPTINSN_SIZE;

	nip = can_optimize(p);
	if (!nip)
		return -EILSEQ;

	/* Allocate instruction slot for detour buffer */
	buff = get_ppc_optinsn_slot();
	if (!buff)
		return -ENOMEM;

	/*
	 * OPTPROBE uses 'b' instruction to branch to optinsn.insn.
	 *
	 * The target address has to be relatively nearby, to permit use
	 * of branch instruction in powerpc, because the address is specified
	 * in an immediate field in the instruction opcode itself, ie 24 bits
	 * in the opcode specify the address. Therefore the address should
	 * be within 32MB on either side of the current instruction.
	 */
	b_offset = (unsigned long)buff - (unsigned long)p->addr;
	if (!is_offset_in_branch_range(b_offset))
		goto error;

	/* Check if the return address is also within 32MB range */
	b_offset = (unsigned long)(buff + TMPL_RET_IDX) -
			(unsigned long)nip;
	if (!is_offset_in_branch_range(b_offset))
		goto error;

	/* Setup template */
	memcpy(buff, optprobe_template_entry,
			TMPL_END_IDX * sizeof(kprobe_opcode_t));

	/*
	 * Fixup the template with instructions to:
	 * 1. load the address of the actual probepoint
	 */
	patch_imm64_load_insns((unsigned long)op, buff + TMPL_OP_IDX);

	/*
	 * 2. branch to optimized_callback() and emulate_step()
	 */
	kprobe_lookup_name("optimized_callback", op_callback_addr);
	kprobe_lookup_name("emulate_step", emulate_step_addr);
	if (!op_callback_addr || !emulate_step_addr) {
		WARN(1, "kprobe_lookup_name() failed\n");
		goto error;
	}

	branch_op_callback = create_branch((unsigned int *)buff + TMPL_CALL_HDLR_IDX,
				(unsigned long)op_callback_addr,
				BRANCH_SET_LINK);

	branch_emulate_step = create_branch((unsigned int *)buff + TMPL_EMULATE_IDX,
				(unsigned long)emulate_step_addr,
				BRANCH_SET_LINK);

	if (!branch_op_callback || !branch_emulate_step)
		goto error;

	buff[TMPL_CALL_HDLR_IDX] = branch_op_callback;
	buff[TMPL_EMULATE_IDX] = branch_emulate_step;

	/*
	 * 3. load instruction to be emulated into relevant register, and
	 */
	patch_imm32_load_insns(*p->ainsn.insn, buff + TMPL_INSN_IDX);

	/*
	 * 4. branch back from trampoline
	 */
	buff[TMPL_RET_IDX] = create_branch((unsigned int *)buff + TMPL_RET_IDX,
				(unsigned long)nip, 0);

	flush_icache_range((unsigned long)buff,
			   (unsigned long)(&buff[TMPL_END_IDX]));

	op->optinsn.insn = buff;

	return 0;

error:
	free_ppc_optinsn_slot(buff, 0);
	return -ERANGE;

}

int arch_prepared_optinsn(struct arch_optimized_insn *optinsn)
{
	return optinsn->insn != NULL;
}

/*
 * On powerpc, Optprobes always replaces one instruction (4 bytes
 * aligned and 4 bytes long). It is impossible to encounter another
 * kprobe in this address range. So always return 0.
 */
int arch_check_optimized_kprobe(struct optimized_kprobe *op)
{
	return 0;
}

void arch_optimize_kprobes(struct list_head *oplist)
{
	struct optimized_kprobe *op;
	struct optimized_kprobe *tmp;

	list_for_each_entry_safe(op, tmp, oplist, list) {
		/*
		 * Backup instructions which will be replaced
		 * by jump address
		 */
		memcpy(op->optinsn.copied_insn, op->kp.addr,
					       RELATIVEJUMP_SIZE);
		patch_instruction(op->kp.addr,
			create_branch((unsigned int *)op->kp.addr,
				      (unsigned long)op->optinsn.insn, 0));
		list_del_init(&op->list);
	}
}

void arch_unoptimize_kprobe(struct optimized_kprobe *op)
{
	arch_arm_kprobe(&op->kp);
}

void arch_unoptimize_kprobes(struct list_head *oplist,
			     struct list_head *done_list)
{
	struct optimized_kprobe *op;
	struct optimized_kprobe *tmp;

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
