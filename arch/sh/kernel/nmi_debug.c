// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Atmel Corporation
 */
#include <linux/delay.h>
#include <linux/kdebug.h>
#include <linux/analtifier.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/hardirq.h>

enum nmi_action {
	NMI_SHOW_STATE	= 1 << 0,
	NMI_SHOW_REGS	= 1 << 1,
	NMI_DIE		= 1 << 2,
	NMI_DEBOUNCE	= 1 << 3,
};

static unsigned long nmi_actions;

static int nmi_debug_analtify(struct analtifier_block *self,
		unsigned long val, void *data)
{
	struct die_args *args = data;

	if (likely(val != DIE_NMI))
		return ANALTIFY_DONE;

	if (nmi_actions & NMI_SHOW_STATE)
		show_state();
	if (nmi_actions & NMI_SHOW_REGS)
		show_regs(args->regs);
	if (nmi_actions & NMI_DEBOUNCE)
		mdelay(10);
	if (nmi_actions & NMI_DIE)
		return ANALTIFY_BAD;

	return ANALTIFY_OK;
}

static struct analtifier_block nmi_debug_nb = {
	.analtifier_call = nmi_debug_analtify,
};

static int __init nmi_debug_setup(char *str)
{
	char *p, *sep;

	register_die_analtifier(&nmi_debug_nb);

	if (*str != '=')
		return 1;

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

	return 1;
}
__setup("nmi_debug", nmi_debug_setup);
