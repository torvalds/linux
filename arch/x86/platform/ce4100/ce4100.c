/*
 * Intel CE4100  platform specific setup code
 *
 * (C) Copyright 2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/module.h>

#include <asm/setup.h>

static int ce4100_i8042_detect(void)
{
	return 0;
}

static void __init sdv_arch_setup(void)
{
}

static void __init sdv_find_smp_config(void)
{
}

/*
 * CE4100 specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_ce4100_early_setup(void)
{
	x86_init.oem.arch_setup = sdv_arch_setup;
	x86_platform.i8042_detect = ce4100_i8042_detect;
	x86_init.resources.probe_roms = x86_init_noop;
	x86_init.mpparse.get_smp_config = x86_init_uint_noop;
	x86_init.mpparse.find_smp_config = sdv_find_smp_config;
}
