/*
 * arch/arm/mach-ns9xxx/mach-cc9p9360js.c
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include "board-jscc9p9360.h"
#include "generic.h"

static void __init mach_cc9p9360js_init_machine(void)
{
	ns9xxx_init_machine();
	board_jscc9p9360_init_machine();
}

MACHINE_START(CC9P9360JS, "Digi ConnectCore 9P 9360 on an JSCC9P9360 Devboard")
	.map_io = ns9xxx_map_io,
	.init_irq = ns9xxx_init_irq,
	.init_machine = mach_cc9p9360js_init_machine,
	.timer = &ns9xxx_timer,
	.boot_params = 0x100,
MACHINE_END
