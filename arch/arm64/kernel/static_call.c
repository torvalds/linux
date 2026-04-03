// SPDX-License-Identifier: GPL-2.0
#include <linux/static_call.h>
#include <linux/memory.h>
#include <asm/text-patching.h>

void arch_static_call_transform(void *site, void *tramp, void *func, bool tail)
{
	u64 literal;
	int ret;

	if (!func)
		func = __static_call_return0;

	/* decode the instructions to discover the literal address */
	literal = ALIGN_DOWN((u64)tramp + 4, SZ_4K) +
		  aarch64_insn_adrp_get_offset(le32_to_cpup(tramp + 4)) +
		  8 * aarch64_insn_decode_immediate(AARCH64_INSN_IMM_12,
						    le32_to_cpup(tramp + 8));

	ret = aarch64_insn_write_literal_u64((void *)literal, (u64)func);
	WARN_ON_ONCE(ret);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);
