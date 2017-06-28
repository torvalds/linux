/*
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/kdebug.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>

#include <asm/irq.h>

enum nmi_action {
	NMI_SHOW_STATE	= 1 << 0,
	NMI_SHOW_REGS	= 1 << 1,
	NMI_DIE		= 1 << 2,
	NMI_DEBOUNCE	= 1 << 3,
};

static unsigned long nmi_actions;

static int nmi_debug_notify(struct notifier_block *self,
		unsigned long val, void *data)
{
	struct die_args *args = data;

	if (likely(val != DIE_NMI))
		return NOTIFY_DONE;

	if (nmi_actions & NMI_SHOW_STATE)
		show_state();
	if (nmi_actions & NMI_SHOW_REGS)
		show_regs(args->regs);
	if (nmi_actions & NMI_DEBOUNCE)
		mdelay(10);
	if (nmi_actions & NMI_DIE)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block nmi_debug_nb = {
	.notifier_call = nmi_debug_notify,
};

static int __init nmi_debug_setup(char *str)
{
	char *p, *sep;

	register_die_notifier(&nmi_debug_nb);
	if (nmi_enable()) {
		printk(KERN_WARNING "Unable to enable NMI.\n");
		return 0;
	}

	if (*str != '=')
		return 0;

	for (p = str + 1; *p; p = sep + 1) {
		sep = strchr(p, ',');
		if (sep)
			*sep = 0;
		if (strcmp(p, "state") == 0)
			nmi_actions |= NMI_SHOW_STATE;
		else if (strcmp(p, "regs") == 0)
			nmi_actions |= NMI_SHOW_REGS;
		else if (strcmp(p, "debounce") == 0)
			nmi_actions |= NMI_DEBOUNCE;
		else if (strcmp(p, "die") == 0)
			nmi_actions |= NMI_DIE;
		else
			printk(KERN_WARNING "NMI: Unrecognized action `%s'\n",
				p);
		if (!sep)
			break;
	}

	return 0;
}
__setup("nmi_debug", nmi_debug_setup);
