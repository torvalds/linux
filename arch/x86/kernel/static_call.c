// SPDX-License-Identifier: GPL-2.0
#include <linux/static_call.h>
#include <linux/memory.h>
#include <linux/bug.h>
#include <asm/text-patching.h>

static void __static_call_transform(void *insn, u8 opcode, void *func)
{
	const void *code = text_gen_insn(opcode, insn, func);

	if (WARN_ONCE(*(u8 *)insn != opcode,
		      "unexpected static call insn opcode 0x%x at %pS\n",
		      opcode, insn))
		return;

	if (memcmp(insn, code, CALL_INSN_SIZE) == 0)
		return;

	text_poke_bp(insn, code, CALL_INSN_SIZE, NULL);
}

void arch_static_call_transform(void *site, void *tramp, void *func)
{
	mutex_lock(&text_mutex);

	if (tramp)
		__static_call_transform(tramp, JMP32_INSN_OPCODE, func);

	if (IS_ENABLED(CONFIG_HAVE_STATIC_CALL_INLINE) && site)
		__static_call_transform(site, CALL_INSN_OPCODE, func);

	mutex_unlock(&text_mutex);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);
