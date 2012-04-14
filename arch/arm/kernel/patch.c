#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/stop_machine.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/opcodes.h>

#include "patch.h"

struct patch {
	void *addr;
	unsigned int insn;
};

void __kprobes __patch_text(void *addr, unsigned int insn)
{
	bool thumb2 = IS_ENABLED(CONFIG_THUMB2_KERNEL);
	int size;

	if (thumb2 && __opcode_is_thumb16(insn)) {
		*(u16 *)addr = __opcode_to_mem_thumb16(insn);
		size = sizeof(u16);
	} else if (thumb2 && ((uintptr_t)addr & 2)) {
		u16 first = __opcode_thumb32_first(insn);
		u16 second = __opcode_thumb32_second(insn);
		u16 *addrh = addr;

		addrh[0] = __opcode_to_mem_thumb16(first);
		addrh[1] = __opcode_to_mem_thumb16(second);

		size = sizeof(u32);
	} else {
		if (thumb2)
			insn = __opcode_to_mem_thumb32(insn);
		else
			insn = __opcode_to_mem_arm(insn);

		*(u32 *)addr = insn;
		size = sizeof(u32);
	}

	flush_icache_range((uintptr_t)(addr),
			   (uintptr_t)(addr) + size);
}

static int __kprobes patch_text_stop_machine(void *data)
{
	struct patch *patch = data;

	__patch_text(patch->addr, patch->insn);

	return 0;
}

void __kprobes patch_text(void *addr, unsigned int insn)
{
	struct patch patch = {
		.addr = addr,
		.insn = insn,
	};

	if (cache_ops_need_broadcast()) {
		stop_machine(patch_text_stop_machine, &patch, cpu_online_mask);
	} else {
		bool straddles_word = IS_ENABLED(CONFIG_THUMB2_KERNEL)
				      && __opcode_is_thumb32(insn)
				      && ((uintptr_t)addr & 2);

		if (straddles_word)
			stop_machine(patch_text_stop_machine, &patch, NULL);
		else
			__patch_text(addr, insn);
	}
}
