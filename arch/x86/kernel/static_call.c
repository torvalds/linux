// SPDX-License-Identifier: GPL-2.0
#include <linux/static_call.h>
#include <linux/memory.h>
#include <linux/bug.h>
#include <asm/text-patching.h>

enum insn_type {
	CALL = 0, /* site call */
	NOP = 1,  /* site cond-call */
	JMP = 2,  /* tramp / site tail-call */
	RET = 3,  /* tramp / site cond-tail-call */
};

static void __static_call_transform(void *insn, enum insn_type type, void *func)
{
	int size = CALL_INSN_SIZE;
	const void *code;

	switch (type) {
	case CALL:
		code = text_gen_insn(CALL_INSN_OPCODE, insn, func);
		break;

	case NOP:
		code = ideal_nops[NOP_ATOMIC5];
		break;

	case JMP:
		code = text_gen_insn(JMP32_INSN_OPCODE, insn, func);
		break;

	case RET:
		code = text_gen_insn(RET_INSN_OPCODE, insn, func);
		size = RET_INSN_SIZE;
		break;
	}

	if (memcmp(insn, code, size) == 0)
		return;

	text_poke_bp(insn, code, size, NULL);
}

void arch_static_call_transform(void *site, void *tramp, void *func)
{
	mutex_lock(&text_mutex);

	if (tramp)
		__static_call_transform(tramp, func ? JMP : RET, func);

	if (IS_ENABLED(CONFIG_HAVE_STATIC_CALL_INLINE) && site)
		__static_call_transform(site, func ? CALL : NOP, func);

	mutex_unlock(&text_mutex);
}
EXPORT_SYMBOL_GPL(arch_static_call_transform);
