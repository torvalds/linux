// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Based on arch/arm/kernel/jump_label.c
 */
#include <linux/kernel.h>
#include <linux/jump_label.h>
#include <linux/smp.h>
#include <asm/insn.h>
#include <asm/text-patching.h>

bool arch_jump_label_transform_queue(struct jump_entry *entry,
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
	return true;
}

void arch_jump_label_transform_apply(void)
{
	kick_all_cpus_sync();
}
