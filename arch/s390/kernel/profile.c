/*
 * arch/s390/kernel/profile.c
 *
 * Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 */
#include <linux/proc_fs.h>
#include <linux/profile.h>

static struct proc_dir_entry * root_irq_dir;

void init_irq_proc(void)
{
	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", 0);

	/* create /proc/irq/prof_cpu_mask */
	create_prof_cpu_mask(root_irq_dir);
}
