// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Based on arch/arm/kernel/jump_label.c
 */
#include <linux/kernel.h>
#include <linux/jump_label.h>
#include <asm/insn.h>

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	void *addr = (void *)jump_entry_code(entry);
	u32 insn;

	if (type == JUMP_LABEL_JMP) {
		insn = aarch64_insn_gen_branch_imm(jump_entry_code(entry),
						   jump_entry_target(entry),
						   AARCH64_INSN_BRANCH_NOLINK);
	} else {
		insn = aarch64_insn_gen_nop();
	}

	aarch64_insn_patch_text_nosync(addr, insn);
}

void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	/*
	 * We use the architected A64 NOP in arch_static_branch, so there's no
	 * need to patch an identical A64 NOP over the top of it here. The core
	 * will call arch_jump_label_transform from a module notifier if the
	 * NOP needs to be replaced by a branch.
	 */
}
