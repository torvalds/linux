/*
 * Jump label s390 support
 *
 * Copyright IBM Corp. 2011
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>
#include <linux/jump_label.h>
#include <asm/ipl.h>

#ifdef HAVE_JUMP_LABEL

struct insn {
	u16 opcode;
	s32 offset;
} __packed;

struct insn_args {
	struct jump_entry *entry;
	enum jump_label_type type;
};

static void jump_label_make_nop(struct jump_entry *entry, struct insn *insn)
{
	/* brcl 0,0 */
	insn->opcode = 0xc004;
	insn->offset = 0;
}

static void jump_label_make_branch(struct jump_entry *entry, struct insn *insn)
{
	/* brcl 15,offset */
	insn->opcode = 0xc0f4;
	insn->offset = (entry->target - entry->code) >> 1;
}

static void jump_label_bug(struct jump_entry *entry, struct insn *expected,
			   struct insn *new)
{
	unsigned char *ipc = (unsigned char *)entry->code;
	unsigned char *ipe = (unsigned char *)expected;
	unsigned char *ipn = (unsigned char *)new;

	pr_emerg("Jump label code mismatch at %pS [%p]\n", ipc, ipc);
	pr_emerg("Found:    %02x %02x %02x %02x %02x %02x\n",
		 ipc[0], ipc[1], ipc[2], ipc[3], ipc[4], ipc[5]);
	pr_emerg("Expected: %02x %02x %02x %02x %02x %02x\n",
		 ipe[0], ipe[1], ipe[2], ipe[3], ipe[4], ipe[5]);
	pr_emerg("New:      %02x %02x %02x %02x %02x %02x\n",
		 ipn[0], ipn[1], ipn[2], ipn[3], ipn[4], ipn[5]);
	panic("Corrupted kernel text");
}

static struct insn orignop = {
	.opcode = 0xc004,
	.offset = JUMP_LABEL_NOP_OFFSET >> 1,
};

static void __jump_label_transform(struct jump_entry *entry,
				   enum jump_label_type type,
				   int init)
{
	struct insn old, new;

	if (type == JUMP_LABEL_ENABLE) {
		jump_label_make_nop(entry, &old);
		jump_label_make_branch(entry, &new);
	} else {
		jump_label_make_branch(entry, &old);
		jump_label_make_nop(entry, &new);
	}
	if (init) {
		if (memcmp((void *)entry->code, &orignop, sizeof(orignop)))
			jump_label_bug(entry, &orignop, &new);
	} else {
		if (memcmp((void *)entry->code, &old, sizeof(old)))
			jump_label_bug(entry, &old, &new);
	}
	probe_kernel_write((void *)entry->code, &new, sizeof(new));
}

static int __sm_arch_jump_label_transform(void *data)
{
	struct insn_args *args = data;

	__jump_label_transform(args->entry, args->type, 0);
	return 0;
}

void arch_jump_label_transform(struct jump_entry *entry,
			       enum jump_label_type type)
{
	struct insn_args args;

	args.entry = entry;
	args.type = type;

	stop_machine(__sm_arch_jump_label_transform, &args, NULL);
}

void arch_jump_label_transform_static(struct jump_entry *entry,
				      enum jump_label_type type)
{
	__jump_label_transform(entry, type, 1);
}

#endif
