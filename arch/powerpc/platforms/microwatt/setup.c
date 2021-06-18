/*
 * Microwatt FPGA-based SoC platform setup code.
 *
 * Copyright 2020 Paul Mackerras (paulus@ozlabs.org), IBM Corp.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <asm/machdep.h>
#include <asm/time.h>

static int __init microwatt_probe(void)
{
	return of_machine_is_compatible("microwatt-soc");
}

define_machine(microwatt) {
	.name			= "microwatt",
	.probe			= microwatt_probe,
	.calibrate_decr		= generic_calibrate_decr,
};
