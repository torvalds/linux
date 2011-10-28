/*
 * arch/powerpc/platforms/embedded6xx/gamecube.c
 *
 * Nintendo GameCube board-specific support
 * Copyright (C) 2004-2009 The GameCube Linux Team
 * Copyright (C) 2007,2008,2009 Albert Herranz
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kexec.h>
#include <linux/seq_file.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/udbg.h>

#include "flipper-pic.h"
#include "usbgecko_udbg.h"


static void gamecube_spin(void)
{
	/* spin until power button pressed */
	for (;;)
		cpu_relax();
}

static void gamecube_restart(char *cmd)
{
	local_irq_disable();
	flipper_platform_reset();
	gamecube_spin();
}

static void gamecube_power_off(void)
{
	local_irq_disable();
	gamecube_spin();
}

static void gamecube_halt(void)
{
	gamecube_restart(NULL);
}

static void __init gamecube_init_early(void)
{
	ug_udbg_init();
}

static int __init gamecube_probe(void)
{
	unsigned long dt_root;

	dt_root = of_get_flat_dt_root();
	if (!of_flat_dt_is_compatible(dt_root, "nintendo,gamecube"))
		return 0;

	return 1;
}

static void gamecube_shutdown(void)
{
	flipper_quiesce();
}

define_machine(gamecube) {
	.name			= "gamecube",
	.probe			= gamecube_probe,
	.init_early		= gamecube_init_early,
	.restart		= gamecube_restart,
	.power_off		= gamecube_power_off,
	.halt			= gamecube_halt,
	.init_IRQ		= flipper_pic_probe,
	.get_irq		= flipper_pic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
	.machine_shutdown	= gamecube_shutdown,
};


static struct of_device_id gamecube_of_bus[] = {
	{ .compatible = "nintendo,flipper", },
	{ },
};

static int __init gamecube_device_probe(void)
{
	if (!machine_is(gamecube))
		return 0;

	of_platform_bus_probe(NULL, gamecube_of_bus, NULL);
	return 0;
}
device_initcall(gamecube_device_probe);

