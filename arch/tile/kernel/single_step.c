/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * A code-rewriter that enables instruction single-stepping.
 */

#include <linux/smp.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/uaccess.h>
#include <linux/mman.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/prctl.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <arch/abi.h>
#include <arch/spr_def.h>
#include <arch/opcode.h>


#ifndef __tilegx__   /* Hardware support for single step unavailable. */

#define signExtend17(val) sign_extend((val), 17)
#define TILE_X1_MASK (0xffffffffULL << 31)

enum mem_op {
	MEMOP_NONE,
	MEMOP_LOAD,
	MEMOP_STORE,
	MEMOP_LOAD_POSTINCR,
	MEMOP_STORE_POSTINCR
};

static inline tilepro_bundle_bits set_BrOff_X1(tilepro_bundle_bits n,
	s32 offset)
{
	tilepro_bundle_bits result;

	/* mask out the old offset */
	tilepro_bundle_bits mask = create_BrOff_X1(-1);
	result = n & (~mask);

	/* or in the new offset */
	result |= create_BrOff_X1(offset);

	return result;
}

static inline tilepro_bundle_bits move_X1(tilepro_bundle_bits n, int dest,
	int src)
{
	tilepro_bundle_bits result;
	tilepro_bundle_bits op;

	result = n & (~TILE_X1_MASK);

	op = create_Opcode_X1(SPECIAL_0_OPCODE_X1) |
		create_RRROpcodeExtension_X1(OR_SPECIAL_0_OPCODE_X1) |
		create_Dest_X1(dest) |
		create_SrcB_X1(TREG_ZERO) |
		create_SrcA_X1(src) ;

	result |= op;
	return result;
}

static inline tilepro_bundle_bits nop_X1(tilepro_bundle_bits n)
{
	return move_X1(n, TREG_ZERO, TREG_ZERO);
}

static inline tilepro_bundle_bits addi_X1(
	tilepro_bundle_bits n, int dest, int src, int imm)
{
	n &= ~TILE_X1_MASK;

	n |=  (create_SrcA_X1(src) |
	       create_Dest_X1(dest) |
	       create_Imm8_X1(imm) |
	       create_S_X1(0) |
	       create_Opcode_X1(IMM_0_OPCODE_X1) |
	       create_ImmOpcodeExtension_X1(ADDI_IMM_0_OPCODE_X1));

	return n;
}

static tilepro_bundle_bits rewrite_load_store_unaligned(
	struct single_step_state *state,
	tilepro_bundle_bits bundle,
	struct pt_regs *regs,
	enum mem_op mem_op,
	int size, int sign_ext)
{
	unsigned char __user *addr;
	int val_reg, addr_reg, err, val;
	int align_ctl;

	align_ctl = unaligned_fixup;
	switch (task_thread_info(current)->align_ctl) {
	case PR_UNALIGN_NOPRINT:
		align_ctl = 1;
		break;
	case PR_UNALIGN_SIGBUS:
		align_ctl = 0;
		break;
	}

	/* Get address and value registers */
	if (bundle & TILEPRO_BUNDLE_Y_ENCODING_MASK) {
		addr_reg = get_SrcA_Y2(bundle);
		val_reg = get_SrcBDest_Y2(bundle);
	} else if (mem_op == MEMOP_LOAD || mem_op == MEMOP_LOAD_POSTINCR) {
		addr_reg = get_SrcA_X1(bundle);
		val_reg  = get_Dest_X1(bundle);
	} else {
		addr_reg = get_SrcA_X1(bundle);
		val_reg  = get_SrcB_X1(bundle);
	}

	/*
	 * If registers are not GPRs, don't try to handle it.
	 *
	 * FIXME: we could handle non-GPR loads by getting the real value
	 * from memory, writing it to the single step buffer, using a
	 * temp_reg to hold a pointer to that memory, then executing that
	 * instruction and resetting temp_reg.  For non-GPR stores, it's a
	 * little trickier; we could use the single step buffer for that
	 * too, but we'd have to add some more state bits so that we could
	 * call back in here to copy that value to the real target.  For
	 * now, we just handle the simple case.
	 */
	if ((val_reg >= PTREGS_NR_GPRS &&
	     (val_reg != TREG_ZERO ||
	      mem_op == MEMOP_LOAD ||
	      mem_op == MEMOP_LOAD_POSTINCR)) ||
	    addr_reg >= PTREGS_NR_GPRS)
		return bundle;

	/* If it's aligned, don't handle it specially */
	addr = (void __user *)regs->regs[addr_reg];
	if (((unsigned long)addr % size) == 0)
		return bundle;

	/*
	 * Return SIGBUS with the unaligned address, if requested.
	 * Note that we return SIGBUS even for completely invalid addresses
	 * as long as they are in fact unaligned; this matches what the
	 * tilepro hardware would be doing, if it could provide us with the
	 * actual bad address in an SPR, which it doesn't.
	 */
	if (align_ctl == 0) {
		siginfo_t info = {
			.si_signo = SIGBUS,
			.si_code = BUS_ADRALN,
			.si_addr = addr
		};
		trace_unhandled_signal("unaligned trap", regs,
				       (unsigned long)addr, SIGBUS);
		force_sig_info(info.si_signo, &info, current);
		return (tilepro_bundle_bits) 0;
	}

	/* Handle unaligned load/store */
	if (mem_op == MEMOP_LOAD || mem_op == MEMOP_LOAD_POSTINCR) {
		unsigned short val_16;
		switch (size) {
		case 2:
			err = copy_from_user(&val_16, addr, sizeof(val_16));
			val = sign_ext ? ((short)val_16) : val_16;
			break;
		case 4:
			err = copy_from_user(&val, addr, sizeof(val));
			break;
		default:
			BUG();
		}
		if (err == 0) {
			state->update_reg = val_reg;
			state->update_value = val;
			state->update = 1;
		}
	} else {
		unsigned short val_16;
		val = (val_reg == TREG_ZERO) ? 0 : regs->regs[val_reg];
		switch (size) {
		case 2:
			val_16 = val;
			err = copy_to_user(addr, &val_16, sizeof(val_16));
			break;
		case 4:
			err = copy_to_user(addr, &val, sizeof(val));
			break;
		default:
			BUG();
		}
	}

	if (err) {
		siginfo_t info = {
			.si_signo = SIGBUS,
			.si_code = BUS_ADRALN,
			.si_addr = addr
		};
		trace_unhandled_signal("bad address for unaligned fixup", regs,
				       (unsigned long)addr, SIGBUS);
		force_sig_info(info.si_signo, &info, current);
		return (tilepro_bundle_bits) 0;
	}

	if (unaligned_printk || unaligned_fixup_count == 0) {
		pr_info("Process %d/%s: PC %#lx: Fixup of unaligned %s at %#lx\n",
			current->pid, current->comm, regs->pc,
			mem_op == MEMOP_LOAD || mem_op == MEMOP_LOAD_POSTINCR ?
			"load" : "store",
			(unsigned long)addr);
		if (!unaligned_printk) {
#define P pr_info
P("\n");
P("Unaligned fixups in the kernel will slow your application considerably.\n");
P("To find them, write a \"1\" to /proc/sys/tile/unaligned_fixup/printk,\n");
P("which requests the kernel show all unaligned fixups, or write a \"0\"\n");
P("to /proc/sys/tile/unaligned_fixup/enabled, in which case each unaligned\n");
P("access will become a SIGBUS you can debug. No further warnings will be\n");
P("shown so as to avoid additional slowdown, but you can track the number\n");
P("of fixups performed via /proc/sys/tile/unaligned_fixup/count.\n");
P("Use the tile-addr2line command (see \"info addr2line\") to decode PCs.\n");
P("\n");
#undef P
		}
	}
	++unaligned_fixup_count;

	if (bundle & TILEPRO_BUNDLE_Y_ENCODING_MASK) {
		/* Convert the Y2 instruction to a prefetch. */
		bundle &= ~(create_SrcBDest_Y2(-1) |
			    create_Opcode_Y2(-1));
		bundle |= (create_SrcBDest_Y2(TREG_ZERO) |
			   create_Opcode_Y2(LW_OPCODE_Y2));
	/* Replace the load postincr with an addi */
	} else if (mem_op == MEMOP_LOAD_POSTINCR) {
		bundle = addi_X1(bundle, addr_reg, addr_reg,
				 get_Imm8_X1(bundle));
	/* Replace the store postincr with an addi */
	} else if (mem_op == MEMOP_STORE_POSTINCR) {
		bundle = addi_X1(bundle, addr_reg, addr_reg,
				 get_Dest_Imm8_X1(bundle));
	} else {
		/* Convert the X1 instruction to a nop. */
		bundle &= ~(create_Opcode_X1(-1) |
			    create_UnShOpcodeExtension_X1(-1) |
			    create_UnOpcodeExtension_X1(-1));
		bundle |= (create_Opcode_X1(SHUN_0_OPCODE_X1) |
			   create_UnShOpcodeExtension_X1(
				   UN_0_SHUN_0_OPCODE_X1) |
			   create_UnOpcodeExtension_X1(
				   NOP_UN_0_SHUN_0_OPCODE_X1));
	}

	return bundle;
}

/*
 * Called after execve() has started the new image.  This allows us
 * to reset the info state.  Note that the the mmap'ed memory, if there
 * was any, has already been unmapped by the exec.
 */
void single_step_execve(void)
{
	struct thread_info *ti = current_thread_info();
	kfree(ti->step_state);
	ti->step_state = NULL;
}

/*
 * single_step_once() - entry point when single stepping has been triggered.
 * @regs: The machine register state
 *
 *  When we arrive at this routine via a trampoline, the single step
 *  engine copies the executing bundle to the single step buffer.
 *  If the instruction is a condition branch, then the target is
 *  reset to one past the next instruction. If the instruction
 *  sets the lr, then that is noted. If the instruction is a jump
 *  or call, then the new target pc is preserved and the current
 *  bundle instruction set to null.
 *
 *  The necessary post-single-step rewriting information is stored in
 *  single_step_state->  We use data segment values because the
 *  stack will be rewound when we run the rewritten single-stepped
 *  instruction.
 */
void single_step_once(struct pt_regs *regs)
{
	extern tilepro_bundle_bits __single_step_ill_insn;
	extern tilepro_bundle_bits __single_step_j_insn;
	extern tilepro_bundle_bits __single_step_addli_insn;
	extern tilepro_bundle_bits __single_step_auli_insn;
	struct thread_info *info = (void *)current_thread_info();
	struct single_step_state *state = info->step_state;
	int is_single_step = test_ti_thread_flag(info, TIF_SINGLESTEP);
	tilepro_bundle_bits __user *buffer, *pc;
	tilepro_bundle_bits bundle;
	int temp_reg;
	int target_reg = TREG_LR;
	int err;
	enum mem_op mem_op = MEMOP_NONE;
	int size = 0, sign_ext = 0;  /* happy compiler */
	int align_ctl;

	align_ctl = unaligned_fixup;
	switch (task_thread_info(current)->align_ctl) {
	case PR_UNALIGN_NOPRINT:
		align_ctl = 1;
		break;
	case PR_UNALIGN_SIGBUS:
		align_ctl = 0;
		break;
	}

	asm(
"    .pushsection .rodata.single_step\n"
"    .align 8\n"
"    .globl    __single_step_ill_insn\n"
"__single_step_ill_insn:\n"
"    ill\n"
"    .globl    __single_step_addli_insn\n"
"__single_step_addli_insn:\n"
"    { nop; addli r0, zero, 0 }\n"
"    .globl    __single_step_auli_insn\n"
"__single_step_auli_insn:\n"
"    { nop; auli r0, r0, 0 }\n"
"    .globl    __single_step_j_insn\n"
"__single_step_j_insn:\n"
"    j .\n"
"    .popsection\n"
	);

	/*
	 * Enable interrupts here to allow touching userspace and the like.
	 * The callers expect this: do_trap() already has interrupts
	 * enabled, and do_work_pending() handles functions that enable
	 * interrupts internally.
	 */
	local_irq_enable();

	if (state == NULL) {
		/* allocate a page of writable, executable memory */
		state = kmalloc(sizeof(struct single_step_state), GFP_KERNEL);
		if (state == NULL) {
			pr_err("Out of kernel memory trying to single-step\n");
			return;
		}

		/* allocate a cache line of writable, executable memory */
		buffer = (void __user *) vm_mmap(NULL, 0, 64,
					  PROT_EXEC | PROT_READ | PROT_WRITE,
					  MAP_PRIVATE | MAP_ANONYMOUS,
					  0);

		if (IS_ERR((void __force *)buffer)) {
			kfree(state);
			pr_err("Out of kernel pages trying to single-step\n");
			return;
		}

		state->buffer = buffer;
		state->is_enabled = 0;

		info->step_state = state;

		/* Validate our stored instruction patterns */
		BUG_ON(get_Opcode_X1(__single_step_addli_insn) !=
		       ADDLI_OPCODE_X1);
		BUG_ON(get_Opcode_X1(__single_step_auli_insn) !=
		       AULI_OPCODE_X1);
		BUG_ON(get_SrcA_X1(__single_step_addli_insn) != TREG_ZERO);
		BUG_ON(get_Dest_X1(__single_step_addli_insn) != 0);
		BUG_ON(get_JOffLong_X1(__single_step_j_insn) != 0);
	}

	/*
	 * If we are returning from a syscall, we still haven't hit the
	 * "ill" for the swint1 instruction.  So back the PC up to be
	 * pointing at the swint1, but we'll actually return directly
	 * back to the "ill" so we come back in via SIGILL as if we
	 * had "executed" the swint1 without ever being in kernel space.
	 */
	if (regs->faultnum == INT_SWINT_1)
		regs->pc -= 8;

	pc = (tilepro_bundle_bits __user *)(regs->pc);
	if (get_user(bundle, pc) != 0) {
		pr_err("Couldn't read instruction at %p trying to step\n", pc);
		return;
	}

	/* We'll follow the instruction with 2 ill op bundles */
	state->orig_pc = (unsigned long)pc;
	state->next_pc = (unsigned long)(pc + 1);
	state->branch_next_pc = 0;
	state->update = 0;

	if (!(bundle & TILEPRO_BUNDLE_Y_ENCODING_MASK)) {
		/* two wide, check for control flow */
		int opcode = get_Opcode_X1(bundle);

		switch (opcode) {
		/* branches */
		case BRANCH_OPCODE_X1:
		{
			s32 offset = signExtend17(get_BrOff_X1(bundle));

			/*
			 * For branches, we use a rewriting trick to let the
			 * hardware evaluate whether the branch is taken or
			 * untaken.  We record the target offset and then
			 * rewrite the branch instruction to target 1 insn
			 * ahead if the branch is taken.  We then follow the
			 * rewritten branch with two bundles, each containing
			 * an "ill" instruction. The supervisor examines the
			 * pc after the single step code is executed, and if
			 * the pc is the first ill instruction, then the
			 * branch (if any) was not taken.  If the pc is the
			 * second ill instruction, then the branch was
			 * taken. The new pc is computed for these cases, and
			 * inserted into the registers for the thread.  If
			 * the pc is the start of the single step code, then
			 * an exception or interrupt was taken before the
			 * code started processing, and the same "original"
			 * pc is restored.  This change, different from the
			 * original implementation, has the advantage of
			 * executing a single user instruction.
			 */
			state->branch_next_pc = (unsigned long)(pc + offset);

			/* rewrite branch offset to go forward one bundle */
			bundle = set_BrOff_X1(bundle, 2);
		}
		break;

		/* jumps */
		case JALB_OPCODE_X1:
		case JALF_OPCODE_X1:
			state->update = 1;
			state->next_pc =
				(unsigned long) (pc + get_JOffLong_X1(bundle));
			break;

		case JB_OPCODE_X1:
		case JF_OPCODE_X1:
			state->next_pc =
				(unsigned long) (pc + get_JOffLong_X1(bundle));
			bundle = nop_X1(bundle);
			break;

		case SPECIAL_0_OPCODE_X1:
			switch (get_RRROpcodeExtension_X1(bundle)) {
			/* jump-register */
			case JALRP_SPECIAL_0_OPCODE_X1:
			case JALR_SPECIAL_0_OPCODE_X1:
				state->update = 1;
				state->next_pc =
					regs->regs[get_SrcA_X1(bundle)];
				break;

			case JRP_SPECIAL_0_OPCODE_X1:
			case JR_SPECIAL_0_OPCODE_X1:
				state->next_pc =
					regs->regs[get_SrcA_X1(bundle)];
				bundle = nop_X1(bundle);
				break;

			case LNK_SPECIAL_0_OPCODE_X1:
				state->update = 1;
				target_reg = get_Dest_X1(bundle);
				break;

			/* stores */
			case SH_SPECIAL_0_OPCODE_X1:
				mem_op = MEMOP_STORE;
				size = 2;
				break;

			case SW_SPECIAL_0_OPCODE_X1:
				mem_op = MEMOP_STORE;
				size = 4;
				break;
			}
			break;

		/* loads and iret */
		case SHUN_0_OPCODE_X1:
			if (get_UnShOpcodeExtension_X1(bundle) ==
			    UN_0_SHUN_0_OPCODE_X1) {
				switch (get_UnOpcodeExtension_X1(bundle)) {
				case LH_UN_0_SHUN_0_OPCODE_X1:
					mem_op = MEMOP_LOAD;
					size = 2;
					sign_ext = 1;
					break;

				case LH_U_UN_0_SHUN_0_OPCODE_X1:
					mem_op = MEMOP_LOAD;
					size = 2;
					sign_ext = 0;
					break;

				case LW_UN_0_SHUN_0_OPCODE_X1:
					mem_op = MEMOP_LOAD;
					size = 4;
					break;

				case IRET_UN_0_SHUN_0_OPCODE_X1:
				{
					unsigned long ex0_0 = __insn_mfspr(
						SPR_EX_CONTEXT_0_0);
					unsigned long ex0_1 = __insn_mfspr(
						SPR_EX_CONTEXT_0_1);
					/*
					 * Special-case it if we're iret'ing
					 * to PL0 again.  Otherwise just let
					 * it run and it will generate SIGILL.
					 */
					if (EX1_PL(ex0_1) == USER_PL) {
						state->next_pc = ex0_0;
						regs->ex1 = ex0_1;
						bundle = nop_X1(bundle);
					}
				}
				}
			}
			break;

		/* postincrement operations */
		case IMM_0_OPCODE_X1:
			switch (get_ImmOpcodeExtension_X1(bundle)) {
			case LWADD_IMM_0_OPCODE_X1:
				mem_op = MEMOP_LOAD_POSTINCR;
				size = 4;
				break;

			case LHADD_IMM_0_OPCODE_X1:
				mem_op = MEMOP_LOAD_POSTINCR;
				size = 2;
				sign_ext = 1;
				break;

			case LHADD_U_IMM_0_OPCODE_X1:
				mem_op = MEMOP_LOAD_POSTINCR;
				size = 2;
				sign_ext = 0;
				break;

			case SWADD_IMM_0_OPCODE_X1:
				mem_op = MEMOP_STORE_POSTINCR;
				size = 4;
				break;

			case SHADD_IMM_0_OPCODE_X1:
				mem_op = MEMOP_STORE_POSTINCR;
				size = 2;
				break;

			default:
				break;
			}
			break;
		}

		if (state->update) {
			/*
			 * Get an available register.  We start with a
			 * bitmask with 1's for available registers.
			 * We truncate to the low 32 registers since
			 * we are guaranteed to have set bits in the
			 * low 32 bits, then use ctz to pick the first.
			 */
			u32 mask = (u32) ~((1ULL << get_Dest_X0(bundle)) |
					   (1ULL << get_SrcA_X0(bundle)) |
					   (1ULL << get_SrcB_X0(bundle)) |
					   (1ULL << target_reg));
			temp_reg = __builtin_ctz(mask);
			state->update_reg = temp_reg;
			state->update_value = regs->regs[temp_reg];
			regs->regs[temp_reg] = (unsigned long) (pc+1);
			regs->flags |= PT_FLAGS_RESTORE_REGS;
			bundle = move_X1(bundle, target_reg, temp_reg);
		}
	} else {
		int opcode = get_Opcode_Y2(bundle);

		switch (opcode) {
		/* loads */
		case LH_OPCODE_Y2:
			mem_op = MEMOP_LOAD;
			size = 2;
			sign_ext = 1;
			break;

		case LH_U_OPCODE_Y2:
			mem_op = MEMOP_LOAD;
			size = 2;
			sign_ext = 0;
			break;

		case LW_OPCODE_Y2:
			mem_op = MEMOP_LOAD;
			size = 4;
			break;

		/* stores */
		case SH_OPCODE_Y2:
			mem_op = MEMOP_STORE;
			size = 2;
			break;

		case SW_OPCODE_Y2:
			mem_op = MEMOP_STORE;
			size = 4;
			break;
		}
	}

	/*
	 * Check if we need to rewrite an unaligned load/store.
	 * Returning zero is a special value meaning we generated a signal.
	 */
	if (mem_op != MEMOP_NONE && align_ctl >= 0) {
		bundle = rewrite_load_store_unaligned(state, bundle, regs,
						      mem_op, size, sign_ext);
		if (bundle == 0)
			return;
	}

	/* write the bundle to our execution area */
	buffer = state->buffer;
	err = __put_user(bundle, buffer++);

	/*
	 * If we're really single-stepping, we take an INT_ILL after.
	 * If we're just handling an unaligned access, we can just
	 * jump directly back to where we were in user code.
	 */
	if (is_single_step) {
		err |= __put_user(__single_step_ill_insn, buffer++);
		err |= __put_user(__single_step_ill_insn, buffer++);
	} else {
		long delta;

		if (state->update) {
			/* We have some state to update; do it inline */
			int ha16;
			bundle = __single_step_addli_insn;
			bundle |= create_Dest_X1(state->update_reg);
			bundle |= create_Imm16_X1(state->update_value);
			err |= __put_user(bundle, buffer++);
			bundle = __single_step_auli_insn;
			bundle |= create_Dest_X1(state->update_reg);
			bundle |= create_SrcA_X1(state->update_reg);
			ha16 = (state->update_value + 0x8000) >> 16;
			bundle |= create_Imm16_X1(ha16);
			err |= __put_user(bundle, buffer++);
			state->update = 0;
		}

		/* End with a jump back to the next instruction */
		delta = ((regs->pc + TILEPRO_BUNDLE_SIZE_IN_BYTES) -
			(unsigned long)buffer) >>
			TILEPRO_LOG2_BUNDLE_ALIGNMENT_IN_BYTES;
		bundle = __single_step_j_insn;
		bundle |= create_JOffLong_X1(delta);
		err |= __put_user(bundle, buffer++);
	}

	if (err) {
		pr_err("Fault when writing to single-step buffer\n");
		return;
	}

	/*
	 * Flush the buffer.
	 * We do a local flush only, since this is a thread-specific buffer.
	 */
	__flush_icache_range((unsigned long)state->buffer,
			     (unsigned long)buffer);

	/* Indicate enabled */
	state->is_enabled = is_single_step;
	regs->pc = (unsigned long)state->buffer;

	/* Fault immediately if we are coming back from a syscall. */
	if (regs->faultnum == INT_SWINT_1)
		regs->pc += 8;
}

#else

static DEFINE_PER_CPU(unsigned long, ss_saved_pc);


/*
 * Called directly on the occasion of an interrupt.
 *
 * If the process doesn't have single step set, then we use this as an
 * opportunity to turn single step off.
 *
 * It has been mentioned that we could conditionally turn off single stepping
 * on each entry into the kernel and rely on single_step_once to turn it
 * on for the processes that matter (as we already do), but this
 * implementation is somewhat more efficient in that we muck with registers
 * once on a bum interrupt rather than on every entry into the kernel.
 *
 * If SINGLE_STEP_CONTROL_K has CANCELED set, then an interrupt occurred,
 * so we have to run through this process again before we can say that an
 * instruction has executed.
 *
 * swint will set CANCELED, but it's a legitimate instruction.  Fortunately
 * it changes the PC.  If it hasn't changed, then we know that the interrupt
 * wasn't generated by swint and we'll need to run this process again before
 * we can say an instruction has executed.
 *
 * If either CANCELED == 0 or the PC's changed, we send out SIGTRAPs and get
 * on with our lives.
 */

void gx_singlestep_handle(struct pt_regs *regs, int fault_num)
{
	unsigned long *ss_pc = this_cpu_ptr(&ss_saved_pc);
	struct thread_info *info = (void *)current_thread_info();
	int is_single_step = test_ti_thread_flag(info, TIF_SINGLESTEP);
	unsigned long control = __insn_mfspr(SPR_SINGLE_STEP_CONTROL_K);

	if (is_single_step == 0) {
		__insn_mtspr(SPR_SINGLE_STEP_EN_K_K, 0);

	} else if ((*ss_pc != regs->pc) ||
		   (!(control & SPR_SINGLE_STEP_CONTROL_1__CANCELED_MASK))) {

		control |= SPR_SINGLE_STEP_CONTROL_1__CANCELED_MASK;
		control |= SPR_SINGLE_STEP_CONTROL_1__INHIBIT_MASK;
		__insn_mtspr(SPR_SINGLE_STEP_CONTROL_K, control);
		send_sigtrap(current, regs);
	}
}


/*
 * Called from need_singlestep.  Set up the control registers and the enable
 * register, then return back.
 */

void single_step_once(struct pt_regs *regs)
{
	unsigned long *ss_pc = this_cpu_ptr(&ss_saved_pc);
	unsigned long control = __insn_mfspr(SPR_SINGLE_STEP_CONTROL_K);

	*ss_pc = regs->pc;
	control |= SPR_SINGLE_STEP_CONTROL_1__CANCELED_MASK;
	control |= SPR_SINGLE_STEP_CONTROL_1__INHIBIT_MASK;
	__insn_mtspr(SPR_SINGLE_STEP_CONTROL_K, control);
	__insn_mtspr(SPR_SINGLE_STEP_EN_K_K, 1 << USER_PL);
}

void single_step_execve(void)
{
	/* Nothing */
}

#endif /* !__tilegx__ */
