// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Emil Renner Berthing
 *
 * Based on arch/arm64/kernel/jump_label.c
 */
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <asm/bug.h>
#include <asm/patch.h>

#define RISCV_INSN_NOP 0x00000013U
#define RISCV_INSN_JAL 0x0000006fU

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	void *addr = (void *)jump_entry_code(entry);
	u32 insn;

	if (type == JUMP_LABEL_JMP) {
		long offset = jump_entry_target(entry) - jump_entry_code(entry);

		if (WARN_ON(offset & 1 || offset < -524288 || offset >= 524288))
			return;

		insn = RISCV_INSN_JAL |
			(((u32)offset & GENMASK(19, 12)) << (12 - 12)) |
			(((u32)offset & GENMASK(11, 11)) << (20 - 11)) |
			(((u32)offset & GENMASK(10,  1)) << (21 -  1)) |
			(((u32)offset & GENMASK(20, 20)) << (31 - 20));
	} else {
		insn = RISCV_INSN_NOP;
	}

	mutex_lock(&text_mutex);
	patch_text_nosync(addr, &insn, sizeof(insn));
	mutex_unlock(&text_mutex);
}

void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	/*
	 * We use the same instructions in the arch_static_branch and
	 * arch_static_branch_jump inline functions, so there's no
	 * need to patch them up here.
	 * The core will call arch_jump_label_transform  when those
	 * instructions need to be replaced.
	 */
}
