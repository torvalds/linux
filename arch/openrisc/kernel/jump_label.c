// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Chen Miao
 *
 * Based on arch/arm/kernel/jump_label.c
 */
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <asm/bug.h>
#include <asm/cacheflush.h>
#include <asm/text-patching.h>

bool arch_jump_label_transform_queue(struct jump_entry *entry,
				     enum jump_label_type type)
{
	void *addr = (void *)jump_entry_code(entry);
	u32 insn;

	if (type == JUMP_LABEL_JMP) {
		long offset;

		offset = jump_entry_target(entry) - jump_entry_code(entry);
		/*
		 * The actual maximum range of the l.j instruction's offset is -134,217,728
		 * ~ 134,217,724 (sign 26-bit imm).
		 * For the original jump range, we need to right-shift N by 2 to obtain the
		 * instruction's offset.
		 */
		WARN_ON_ONCE(offset < -134217728 || offset > 134217724);

		/* 26bit imm mask */
		offset = (offset >> 2) & 0x03ffffff;

		insn = offset;
	} else {
		insn = OPENRISC_INSN_NOP;
	}

	if (early_boot_irqs_disabled)
		copy_to_kernel_nofault(addr, &insn, sizeof(insn));
	else
		patch_insn_write(addr, insn);

	return true;
}

void arch_jump_label_transform_apply(void)
{
	kick_all_cpus_sync();
}
