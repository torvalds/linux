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

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	union jump_code_union code;

	if (type == JUMP_LABEL_ENABLE) {
		code.jump = 0xe9;
		code.offset = entry->target -
				(entry->code + JUMP_LABEL_NOP_SIZE);
	} else
		memcpy(&code, ideal_nops[NOP_ATOMIC5], JUMP_LABEL_NOP_SIZE);
	get_online_cpus();
	mutex_lock(&text_mutex);
	text_poke_smp((void *)entry->code, &code, JUMP_LABEL_NOP_SIZE);
	mutex_unlock(&text_mutex);
	put_online_cpus();
}

void arch_jump_label_text_poke_early(jump_label_t addr)
{
	text_poke_early((void *)addr, ideal_nops[NOP_ATOMIC5],
			JUMP_LABEL_NOP_SIZE);
}

#endif
