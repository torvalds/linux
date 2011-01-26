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

#ifdef HAVE_JUMP_LABEL

#define J_RANGE_MASK ((1ul << 28) - 1)

void arch_jump_label_transform(struct jump_entry *e,
			       enum jump_label_type type)
{
	union mips_instruction insn;
	union mips_instruction *insn_p =
		(union mips_instruction *)(unsigned long)e->code;

	/* Jump only works within a 256MB aligned region. */
	BUG_ON((e->target & ~J_RANGE_MASK) != (e->code & ~J_RANGE_MASK));

	/* Target must have 4 byte alignment. */
	BUG_ON((e->target & 3) != 0);

	if (type == JUMP_LABEL_ENABLE) {
		insn.j_format.opcode = j_op;
		insn.j_format.target = (e->target & J_RANGE_MASK) >> 2;
	} else {
		insn.word = 0; /* nop */
	}

	get_online_cpus();
	mutex_lock(&text_mutex);
	*insn_p = insn;

	flush_icache_range((unsigned long)insn_p,
			   (unsigned long)insn_p + sizeof(*insn_p));

	mutex_unlock(&text_mutex);
	put_online_cpus();
}

#endif /* HAVE_JUMP_LABEL */
