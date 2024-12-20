// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Helge Deller <deller@gmx.de>
 *
 * Based on arch/arm64/kernel/jump_label.c
 */
#include <linux/kernel.h>
#include <linux/jump_label.h>
#include <linux/bug.h>
#include <asm/alternative.h>
#include <asm/text-patching.h>

static inline int reassemble_17(int as17)
{
	return (((as17 & 0x10000) >> 16) |
		((as17 & 0x0f800) << 5) |
		((as17 & 0x00400) >> 8) |
		((as17 & 0x003ff) << 3));
}

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	void *addr = (void *)jump_entry_code(entry);
	u32 insn;

	if (type == JUMP_LABEL_JMP) {
		void *target = (void *)jump_entry_target(entry);
		int distance = target - addr;
		/*
		 * Encode the PA1.1 "b,n" instruction with a 17-bit
		 * displacement.  In case we hit the BUG(), we could use
		 * another branch instruction with a 22-bit displacement on
		 * 64-bit CPUs instead. But this seems sufficient for now.
		 */
		distance -= 8;
		BUG_ON(distance > 262143 || distance < -262144);
		insn = 0xe8000002 | reassemble_17(distance >> 2);
	} else {
		insn = INSN_NOP;
	}

	patch_text(addr, insn);
}
