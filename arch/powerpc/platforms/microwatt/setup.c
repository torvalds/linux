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

static void __init microwatt_init_IRQ(void)
{
	xics_init();
}

static int __init microwatt_probe(void)
{
	return of_machine_is_compatible("microwatt-soc");
}

static int __init microwatt_populate(void)
{
	return of_platform_default_populate(NULL, NULL, NULL);
}
machine_arch_initcall(microwatt, microwatt_populate);

define_machine(microwatt) {
	.name			= "microwatt",
	.probe			= microwatt_probe,
	.init_IRQ		= microwatt_init_IRQ,
	.progress		= udbg_progress,
	.calibrate_decr		= generic_calibrate_decr,
};
