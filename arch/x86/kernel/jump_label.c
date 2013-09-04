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

#ifdef HAVE_JUMP_LABEL

union jump_code_union {
	char code[JUMP_LABEL_NOP_SIZE];
	struct {
		char jump;
		int offset;
	} __attribute__((packed));
};

static void __jump_label_transform(struct jump_entry *entry,
				   enum jump_label_type type,
				   void *(*poker)(void *, const void *, size_t))
{
	union jump_code_union code;

	if (type == JUMP_LABEL_ENABLE) {
		code.jump = 0xe9;
		code.offset = entry->target -
				(entry->code + JUMP_LABEL_NOP_SIZE);
	} else
		memcpy(&code, ideal_nops[NOP_ATOMIC5], JUMP_LABEL_NOP_SIZE);

	/*
	 * Make text_poke_bp() a default fallback poker.
	 *
	 * At the time the change is being done, just ignore whether we
	 * are doing nop -> jump or jump -> nop transition, and assume
	 * always nop being the 'currently valid' instruction
	 *
	 */
	if (poker)
		(*poker)((void *)entry->code, &code, JUMP_LABEL_NOP_SIZE);
	else
		text_poke_bp((void *)entry->code, &code, JUMP_LABEL_NOP_SIZE,
			     (void *)entry->code + JUMP_LABEL_NOP_SIZE);
}

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	get_online_cpus();
	mutex_lock(&text_mutex);
	__jump_label_transform(entry, type, NULL);
	mutex_unlock(&text_mutex);
	put_online_cpus();
}

__init_or_module void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	__jump_label_transform(entry, type, text_poke_early);
}

#endif
