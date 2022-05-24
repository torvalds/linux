// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Cadence Design Systems Inc.

#include <linux/cpu.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/types.h>

#include <asm/cacheflush.h>

#define J_OFFSET_MASK 0x0003ffff
#define J_SIGN_MASK (~(J_OFFSET_MASK >> 1))

#if defined(__XTENSA_EL__)
#define J_INSN 0x6
#define NOP_INSN 0x0020f0
#elif defined(__XTENSA_EB__)
#define J_INSN 0x60000000
#define NOP_INSN 0x0f020000
#else
#error Unsupported endianness.
#endif

struct patch {
	atomic_t cpu_count;
	unsigned long addr;
	size_t sz;
	const void *data;
};

static void local_patch_text(unsigned long addr, const void *data, size_t sz)
{
	memcpy((void *)addr, data, sz);
	local_flush_icache_range(addr, addr + sz);
}

static int patch_text_stop_machine(void *data)
{
	struct patch *patch = data;

	if (atomic_inc_return(&patch->cpu_count) == 1) {
		local_patch_text(patch->addr, patch->data, patch->sz);
		atomic_inc(&patch->cpu_count);
	} else {
		while (atomic_read(&patch->cpu_count) <= num_online_cpus())
			cpu_relax();
		__invalidate_icache_range(patch->addr, patch->sz);
	}
	return 0;
}

static void patch_text(unsigned long addr, const void *data, size_t sz)
{
	if (IS_ENABLED(CONFIG_SMP)) {
		struct patch patch = {
			.cpu_count = ATOMIC_INIT(0),
			.addr = addr,
			.sz = sz,
			.data = data,
		};
		stop_machine_cpuslocked(patch_text_stop_machine,
					&patch, cpu_online_mask);
	} else {
		unsigned long flags;

		local_irq_save(flags);
		local_patch_text(addr, data, sz);
		local_irq_restore(flags);
	}
}

void arch_jump_label_transform(struct jump_entry *e,
			       enum jump_label_type type)
{
	u32 d = (jump_entry_target(e) - (jump_entry_code(e) + 4));
	u32 insn;

	/* Jump only works within 128K of the J instruction. */
	BUG_ON(!((d & J_SIGN_MASK) == 0 ||
		 (d & J_SIGN_MASK) == J_SIGN_MASK));

	if (type == JUMP_LABEL_JMP) {
#if defined(__XTENSA_EL__)
		insn = ((d & J_OFFSET_MASK) << 6) | J_INSN;
#elif defined(__XTENSA_EB__)
		insn = ((d & J_OFFSET_MASK) << 8) | J_INSN;
#endif
	} else {
		insn = NOP_INSN;
	}

	patch_text(jump_entry_code(e), &insn, JUMP_LABEL_NOP_SIZE);
}
