/*
 * pseries CPU Hotplug infrastructure.
 *
 * Split out from arch/powerpc/kernel/rtas.c
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 *
 * Copyright (C) 2006 Michael Ellerman, IBM Corporation
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <asm/system.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/firmware.h>
#include <asm/machdep.h>
#include <asm/vdso_datapage.h>
#include <asm/pSeries_reconfig.h>
#include "xics.h"

/* This version can't take the spinlock, because it never returns */
static struct rtas_args rtas_stop_self_args = {
	.token = RTAS_UNKNOWN_SERVICE,
	.nargs = 0,
	.nret = 1,
	.rets = &rtas_stop_self_args.args[0],
};

void rtas_stop_self(void)
{
	struct rtas_args *args = &rtas_stop_self_args;

	local_irq_disable();

	BUG_ON(args->token == RTAS_UNKNOWN_SERVICE);

	printk("cpu %u (hwid %u) Ready to die...\n",
	       smp_processor_id(), hard_smp_processor_id());
	enter_rtas(__pa(args));

	panic("Alas, I survived.\n");
}

static int __init pseries_cpu_hotplug_init(void)
{
	rtas_stop_self_args.token = rtas_token("stop-self");

	return 0;
}
arch_initcall(pseries_cpu_hotplug_init);
