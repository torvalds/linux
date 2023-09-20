// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 *
 * Based on arch/arm64/kernel/jump_label.c
 */
#include <linux/kernel.h>
#include <linux/jump_label.h>
#include <asm/inst.h>

void arch_jump_label_transform(struct jump_entry *entry, enum jump_label_type type)
{
	u32 insn;
	void *addr = (void *)jump_entry_code(entry);

	if (type == JUMP_LABEL_JMP)
		insn = larch_insn_gen_b(jump_entry_code(entry), jump_entry_target(entry));
	else
		insn = larch_insn_gen_nop();

	larch_insn_patch_text(addr, insn);
}
