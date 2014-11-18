/*
 *  Copyright (C) 2014 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/traps.h>

/*
 * The runtime support for deprecated instruction support can be in one of
 * following three states -
 *
 * 0 = undef
 * 1 = emulate (software emulation)
 * 2 = hw (supported in hardware)
 */
enum insn_emulation_mode {
	INSN_UNDEF,
	INSN_EMULATE,
	INSN_HW,
};

enum legacy_insn_status {
	INSN_DEPRECATED,
	INSN_OBSOLETE,
};

struct insn_emulation_ops {
	const char		*name;
	enum legacy_insn_status	status;
	struct undef_hook	*hooks;
	int			(*set_hw_mode)(bool enable);
};

struct insn_emulation {
	struct list_head node;
	struct insn_emulation_ops *ops;
	int current_mode;
	int min;
	int max;
};

static LIST_HEAD(insn_emulation);
static int nr_insn_emulated;
static DEFINE_RAW_SPINLOCK(insn_emulation_lock);

static void register_emulation_hooks(struct insn_emulation_ops *ops)
{
	struct undef_hook *hook;

	BUG_ON(!ops->hooks);

	for (hook = ops->hooks; hook->instr_mask; hook++)
		register_undef_hook(hook);

	pr_notice("Registered %s emulation handler\n", ops->name);
}

static void remove_emulation_hooks(struct insn_emulation_ops *ops)
{
	struct undef_hook *hook;

	BUG_ON(!ops->hooks);

	for (hook = ops->hooks; hook->instr_mask; hook++)
		unregister_undef_hook(hook);

	pr_notice("Removed %s emulation handler\n", ops->name);
}

static int update_insn_emulation_mode(struct insn_emulation *insn,
				       enum insn_emulation_mode prev)
{
	int ret = 0;

	switch (prev) {
	case INSN_UNDEF: /* Nothing to be done */
		break;
	case INSN_EMULATE:
		remove_emulation_hooks(insn->ops);
		break;
	case INSN_HW:
		if (insn->ops->set_hw_mode) {
			insn->ops->set_hw_mode(false);
			pr_notice("Disabled %s support\n", insn->ops->name);
		}
		break;
	}

	switch (insn->current_mode) {
	case INSN_UNDEF:
		break;
	case INSN_EMULATE:
		register_emulation_hooks(insn->ops);
		break;
	case INSN_HW:
		if (insn->ops->set_hw_mode && insn->ops->set_hw_mode(true))
			pr_notice("Enabled %s support\n", insn->ops->name);
		else
			ret = -EINVAL;
		break;
	}

	return ret;
}

static void register_insn_emulation(struct insn_emulation_ops *ops)
{
	unsigned long flags;
	struct insn_emulation *insn;

	insn = kzalloc(sizeof(*insn), GFP_KERNEL);
	insn->ops = ops;
	insn->min = INSN_UNDEF;

	switch (ops->status) {
	case INSN_DEPRECATED:
		insn->current_mode = INSN_EMULATE;
		insn->max = INSN_HW;
		break;
	case INSN_OBSOLETE:
		insn->current_mode = INSN_UNDEF;
		insn->max = INSN_EMULATE;
		break;
	}

	raw_spin_lock_irqsave(&insn_emulation_lock, flags);
	list_add(&insn->node, &insn_emulation);
	nr_insn_emulated++;
	raw_spin_unlock_irqrestore(&insn_emulation_lock, flags);

	/* Register any handlers if required */
	update_insn_emulation_mode(insn, INSN_UNDEF);
}

static int emulation_proc_handler(struct ctl_table *table, int write,
				  void __user *buffer, size_t *lenp,
				  loff_t *ppos)
{
	int ret = 0;
	struct insn_emulation *insn = (struct insn_emulation *) table->data;
	enum insn_emulation_mode prev_mode = insn->current_mode;

	table->data = &insn->current_mode;
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (ret || !write || prev_mode == insn->current_mode)
		goto ret;

	ret = update_insn_emulation_mode(insn, prev_mode);
	if (!ret) {
		/* Mode change failed, revert to previous mode. */
		insn->current_mode = prev_mode;
		update_insn_emulation_mode(insn, INSN_UNDEF);
	}
ret:
	table->data = insn;
	return ret;
}

static struct ctl_table ctl_abi[] = {
	{
		.procname = "abi",
		.mode = 0555,
	},
	{ }
};

static void register_insn_emulation_sysctl(struct ctl_table *table)
{
	unsigned long flags;
	int i = 0;
	struct insn_emulation *insn;
	struct ctl_table *insns_sysctl, *sysctl;

	insns_sysctl = kzalloc(sizeof(*sysctl) * (nr_insn_emulated + 1),
			      GFP_KERNEL);

	raw_spin_lock_irqsave(&insn_emulation_lock, flags);
	list_for_each_entry(insn, &insn_emulation, node) {
		sysctl = &insns_sysctl[i];

		sysctl->mode = 0644;
		sysctl->maxlen = sizeof(int);

		sysctl->procname = insn->ops->name;
		sysctl->data = insn;
		sysctl->extra1 = &insn->min;
		sysctl->extra2 = &insn->max;
		sysctl->proc_handler = emulation_proc_handler;
		i++;
	}
	raw_spin_unlock_irqrestore(&insn_emulation_lock, flags);

	table->child = insns_sysctl;
	register_sysctl_table(table);
}

/*
 * Invoked as late_initcall, since not needed before init spawned.
 */
static int __init armv8_deprecated_init(void)
{
	register_insn_emulation_sysctl(ctl_abi);

	return 0;
}

late_initcall(armv8_deprecated_init);
