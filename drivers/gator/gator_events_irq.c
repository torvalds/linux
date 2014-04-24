/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <trace/events/irq.h>

#define HARDIRQ		0
#define SOFTIRQ		1
#define TOTALIRQ	(SOFTIRQ+1)

static ulong hardirq_enabled;
static ulong softirq_enabled;
static ulong hardirq_key;
static ulong softirq_key;
static DEFINE_PER_CPU(atomic_t[TOTALIRQ], irqCnt);
static DEFINE_PER_CPU(int[TOTALIRQ * 2], irqGet);

GATOR_DEFINE_PROBE(irq_handler_exit,
		   TP_PROTO(int irq, struct irqaction *action, int ret))
{
	atomic_inc(&per_cpu(irqCnt, get_physical_cpu())[HARDIRQ]);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
GATOR_DEFINE_PROBE(softirq_exit, TP_PROTO(struct softirq_action *h, struct softirq_action *vec))
#else
GATOR_DEFINE_PROBE(softirq_exit, TP_PROTO(unsigned int vec_nr))
#endif
{
	atomic_inc(&per_cpu(irqCnt, get_physical_cpu())[SOFTIRQ]);
}

static int gator_events_irq_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	/* irq */
	dir = gatorfs_mkdir(sb, root, "Linux_irq_irq");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &hardirq_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &hardirq_key);

	/* soft irq */
	dir = gatorfs_mkdir(sb, root, "Linux_irq_softirq");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &softirq_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &softirq_key);

	return 0;
}

static int gator_events_irq_online(int **buffer, bool migrate)
{
	int len = 0, cpu = get_physical_cpu();

	// synchronization with the irq_exit functions is not necessary as the values are being reset
	if (hardirq_enabled) {
		atomic_set(&per_cpu(irqCnt, cpu)[HARDIRQ], 0);
		per_cpu(irqGet, cpu)[len++] = hardirq_key;
		per_cpu(irqGet, cpu)[len++] = 0;
	}

	if (softirq_enabled) {
		atomic_set(&per_cpu(irqCnt, cpu)[SOFTIRQ], 0);
		per_cpu(irqGet, cpu)[len++] = softirq_key;
		per_cpu(irqGet, cpu)[len++] = 0;
	}

	if (buffer)
		*buffer = per_cpu(irqGet, cpu);

	return len;
}

static int gator_events_irq_start(void)
{
	// register tracepoints
	if (hardirq_enabled)
		if (GATOR_REGISTER_TRACE(irq_handler_exit))
			goto fail_hardirq_exit;
	if (softirq_enabled)
		if (GATOR_REGISTER_TRACE(softirq_exit))
			goto fail_softirq_exit;
	pr_debug("gator: registered irq tracepoints\n");

	return 0;

	// unregister tracepoints on error
fail_softirq_exit:
	if (hardirq_enabled)
		GATOR_UNREGISTER_TRACE(irq_handler_exit);
fail_hardirq_exit:
	pr_err("gator: irq tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_events_irq_stop(void)
{
	if (hardirq_enabled)
		GATOR_UNREGISTER_TRACE(irq_handler_exit);
	if (softirq_enabled)
		GATOR_UNREGISTER_TRACE(softirq_exit);
	pr_debug("gator: unregistered irq tracepoints\n");

	hardirq_enabled = 0;
	softirq_enabled = 0;
}

static int gator_events_irq_read(int **buffer)
{
	int len, value;
	int cpu = get_physical_cpu();

	len = 0;
	if (hardirq_enabled) {
		value = atomic_read(&per_cpu(irqCnt, cpu)[HARDIRQ]);
		atomic_sub(value, &per_cpu(irqCnt, cpu)[HARDIRQ]);

		per_cpu(irqGet, cpu)[len++] = hardirq_key;
		per_cpu(irqGet, cpu)[len++] = value;
	}

	if (softirq_enabled) {
		value = atomic_read(&per_cpu(irqCnt, cpu)[SOFTIRQ]);
		atomic_sub(value, &per_cpu(irqCnt, cpu)[SOFTIRQ]);

		per_cpu(irqGet, cpu)[len++] = softirq_key;
		per_cpu(irqGet, cpu)[len++] = value;
	}

	if (buffer)
		*buffer = per_cpu(irqGet, cpu);

	return len;
}

static struct gator_interface gator_events_irq_interface = {
	.create_files = gator_events_irq_create_files,
	.online = gator_events_irq_online,
	.start = gator_events_irq_start,
	.stop = gator_events_irq_stop,
	.read = gator_events_irq_read,
};

int gator_events_irq_init(void)
{
	hardirq_key = gator_events_get_key();
	softirq_key = gator_events_get_key();

	hardirq_enabled = 0;
	softirq_enabled = 0;

	return gator_events_install(&gator_events_irq_interface);
}
