/*
 * Microwatt FPGA-based SoC platform setup code.
 *
 * Copyright 2020 Paul Mackerras (paulus@ozlabs.org), IBM Corp.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/xics.h>
#include <asm/udbg.h>

#include "microwatt.h"

static void __init microwatt_init_IRQ(void)
{
	xics_init();
}

static int __init microwatt_populate(void)
{
	return of_platform_default_populate(NULL, NULL, NULL);
}
machine_arch_initcall(microwatt, microwatt_populate);

static int __init microwatt_probe(void)
{
	/* Main reason for having this is to start the other CPU(s) */
	if (IS_ENABLED(CONFIG_SMP))
		microwatt_init_smp();
	return 1;
}

static void __init microwatt_setup_arch(void)
{
	microwatt_rng_init();
}

static void microwatt_idle(void)
{
	if (!prep_irq_for_idle_irqsoff())
		return;

	__asm__ __volatile__ ("wait");
}

define_machine(microwatt) {
	.name			= "microwatt",
	.compatible		= "microwatt-soc",
	.probe			= microwatt_probe,
	.init_IRQ		= microwatt_init_IRQ,
	.setup_arch		= microwatt_setup_arch,
	.progress		= udbg_progress,
	.power_save		= microwatt_idle,
};
