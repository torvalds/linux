/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2010 Cavium Networks, Inc.
 */

#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/cpu.h>

#include <asm/cacheflush.h>
#include <asm/inst.h>

/*
 * Define parameters for the standard MIPS and the microMIPS jump
 * instruction encoding respectively:
 *
 * - the ISA bit of the target, either 0 or 1 respectively,
 *
 * - the amount the jump target address is shifted right to fit in the
 *   immediate field of the machine instruction, either 2 or 1,
 *
 * - the mask determining the size of the jump region relative to the
 *   delay-slot instruction, either 256MB or 128MB,
 *
 * - the jump target alignment, either 4 or 2 bytes.
 */
#define J_ISA_BIT	IS_ENABLED(CONFIG_CPU_MICROMIPS)
#define J_RANGE_SHIFT	(2 - J_ISA_BIT)
#define J_RANGE_MASK	((1ul << (26 + J_RANGE_SHIFT)) - 1)
#define J_ALIGN_MASK	((1ul << J_RANGE_SHIFT) - 1)

void arch_jump_label_transform(struct jump_entry *e,
			       enum jump_label_type type)
{
	union mips_instruction *insn_p;
	union mips_instruction insn;
	long offset;

	insn_p = (union mips_instruction *)msk_isa16_mode(e->code);

	/* Target must have the right alignment and ISA must be preserved. */
	BUG_ON((e->target & J_ALIGN_MASK) != J_ISA_BIT);

	if (type == JUMP_LABEL_JMP) {
		if (!IS_ENABLED(CONFIG_CPU_MICROMIPS) && MIPS_ISA_REV >= 6) {
			offset = e->target - ((unsigned long)insn_p + 4);
			offset >>= 2;

			/*
			 * The branch offset must fit in the instruction's 26
			 * bit field.
			 */
			WARN_ON((offset >= (long)BIT(25)) ||
				(offset < -(long)BIT(25)));

			insn.j_format.opcode = bc6_op;
			insn.j_format.target = offset;
		} else {
			/*
			 * Jump only works within an aligned region its delay
			 * slot is in.
			 */
			WARN_ON((e->target & ~J_RANGE_MASK) !=
				((e->code + 4) & ~J_RANGE_MASK));

			insn.j_format.opcode = J_ISA_BIT ? mm_j32_op : j_op;
			insn.j_format.target = e->target >> J_RANGE_SHIFT;
		}
	} else {
		insn.word = 0; /* nop */
	}

	mutex_lock(&text_mutex);
	if (IS_ENABLED(CONFIG_CPU_MICROMIPS)) {
		insn_p->halfword[0] = insn.word >> 16;
		insn_p->halfword[1] = insn.word;
	} else
		*insn_p = insn;

	flush_icache_range((unsigned long)insn_p,
			   (unsigned long)insn_p + sizeof(*insn_p));

	mutex_unlock(&text_mutex);
}

#ifdef CONFIG_MODULES
void jump_label_apply_nops(struct module *mod)
{
	struct jump_entry *iter_start = mod->jump_entries;
	struct jump_entry *iter_stop = iter_start + mod->num_jump_entries;
	struct jump_entry *iter;

	/* if the module doesn't have jump label entries, just return */
	if (iter_start == iter_stop)
		return;

	for (iter = iter_start; iter < iter_stop; iter++) {
		/* Only write NOPs for arch_branch_static(). */
		if (jump_label_init_type(iter) == JUMP_LABEL_NOP)
			arch_jump_label_transform(iter, JUMP_LABEL_NOP);
	}
}
#endif
