// SPDX-License-Identifier: GPL-2.0
/*
 * jump label x86 support
 *
 * Copyright (C) 2009 Jason Baron <jbaron@redhat.com>
 *
 */
#include <linux/jump_label.h>
#include <linux/memory.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/cpu.h>
#include <asm/kprobes.h>
#include <asm/alternative.h>
#include <asm/text-patching.h>
#include <asm/insn.h>

int arch_jump_entry_size(struct jump_entry *entry)
{
	struct insn insn = {};

	insn_decode_kernel(&insn, (void *)jump_entry_code(entry));
	BUG_ON(insn.length != 2 && insn.length != 5);

	return insn.length;
}

struct jump_label_patch {
	const void *code;
	int size;
};

static struct jump_label_patch
__jump_label_patch(struct jump_entry *entry, enum jump_label_type type)
{
	const void *expect, *code, *nop;
	const void *addr, *dest;
	int size;

	addr = (void *)jump_entry_code(entry);
	dest = (void *)jump_entry_target(entry);

	size = arch_jump_entry_size(entry);
	switch (size) {
	case JMP8_INSN_SIZE:
		code = text_gen_insn(JMP8_INSN_OPCODE, addr, dest);
		nop = x86_nops[size];
		break;

	case JMP32_INSN_SIZE:
		code = text_gen_insn(JMP32_INSN_OPCODE, addr, dest);
		nop = x86_nops[size];
		break;

	default: BUG();
	}

	if (type == JUMP_LABEL_JMP)
		expect = nop;
	else
		expect = code;

	if (memcmp(addr, expect, size)) {
		/*
		 * The location is not an op that we were expecting.
		 * Something went wrong. Crash the box, as something could be
		 * corrupting the kernel.
		 */
		pr_crit("jump_label: Fatal kernel bug, unexpected op at %pS [%p] (%5ph != %5ph)) size:%d type:%d\n",
				addr, addr, addr, expect, size, type);
		BUG();
	}

	if (type == JUMP_LABEL_NOP)
		code = nop;

	return (struct jump_label_patch){.code = code, .size = size};
}

static __always_inline void
__jump_label_transform(struct jump_entry *entry,
		       enum jump_label_type type,
		       int init)
{
	const struct jump_label_patch jlp = __jump_label_patch(entry, type);

	/*
	 * As long as only a single processor is running and the code is still
	 * not marked as RO, text_poke_early() can be used; Checking that
	 * system_state is SYSTEM_BOOTING guarantees it. It will be set to
	 * SYSTEM_SCHEDULING before other cores are awaken and before the
	 * code is write-protected.
	 *
	 * At the time the change is being done, just ignore whether we
	 * are doing nop -> jump or jump -> nop transition, and assume
	 * always nop being the 'currently valid' instruction
	 */
	if (init || system_state == SYSTEM_BOOTING) {
		text_poke_early((void *)jump_entry_code(entry), jlp.code, jlp.size);
		return;
	}

	text_poke_bp((void *)jump_entry_code(entry), jlp.code, jlp.size, NULL);
}

static void __ref jump_label_transform(struct jump_entry *entry,
				       enum jump_label_type type,
				       int init)
{
	mutex_lock(&text_mutex);
	__jump_label_transform(entry, type, init);
	mutex_unlock(&text_mutex);
}

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	jump_label_transform(entry, type, 0);
}

bool arch_jump_label_transform_queue(struct jump_entry *entry,
				     enum jump_label_type type)
{
	struct jump_label_patch jlp;

	if (system_state == SYSTEM_BOOTING) {
		/*
		 * Fallback to the non-batching mode.
		 */
		arch_jump_label_transform(entry, type);
		return true;
	}

	mutex_lock(&text_mutex);
	jlp = __jump_label_patch(entry, type);
	text_poke_queue((void *)jump_entry_code(entry), jlp.code, jlp.size, NULL);
	mutex_unlock(&text_mutex);
	return true;
}

void arch_jump_label_transform_apply(void)
{
	mutex_lock(&text_mutex);
	text_poke_finish();
	mutex_unlock(&text_mutex);
}

static enum {
	JL_STATE_START,
	JL_STATE_NO_UPDATE,
	JL_STATE_UPDATE,
} jlstate __initdata_or_module = JL_STATE_START;

__init_or_module void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	if (jlstate == JL_STATE_UPDATE)
		jump_label_transform(entry, type, 1);
}
