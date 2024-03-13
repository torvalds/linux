// SPDX-License-Identifier: GPL-2.0-only

#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

#define NOP32_HI	0xc400
#define NOP32_LO	0x4820
#define BSR_LINK	0xe000

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	unsigned long addr = jump_entry_code(entry);
	u16 insn[2];
	int ret = 0;

	if (type == JUMP_LABEL_JMP) {
		long offset = jump_entry_target(entry) - jump_entry_code(entry);

		if (WARN_ON(offset & 1 || offset < -67108864 || offset >= 67108864))
			return;

		offset = offset >> 1;

		insn[0] = BSR_LINK |
			((uint16_t)((unsigned long) offset >> 16) & 0x3ff);
		insn[1] = (uint16_t)((unsigned long) offset & 0xffff);
	} else {
		insn[0] = NOP32_HI;
		insn[1] = NOP32_LO;
	}

	ret = copy_to_kernel_nofault((void *)addr, insn, 4);
	WARN_ON(ret);

	flush_icache_range(addr, addr + 4);
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
	arch_jump_label_transform(entry, type);
}
