/*
 * linux/arch/arm/mach-nuc93x/dev.c
 *
 * Copyright (C) 2009 Nuvoton corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>

#include "cpu.h"

/*Here should be your evb resourse,such as LCD*/

static struct platform_device *nuc93x_public_dev[] __initdata = {
	&nuc93x_serial_device,
};

/* Provide adding specific CPU platform devices API */

void __init nuc93x_board_init(struct platform_device **device, int size)
{
	platform_add_devices(device, size);
	platform_add_devices(nuc93x_public_dev, ARRAY_SIZE(nuc93x_public_dev));
}

