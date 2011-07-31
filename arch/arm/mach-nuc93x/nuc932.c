/*
 * linux/arch/arm/mach-nuc93x/nuc932.c
 *
 * Copyright (c) 2009 Nuvoton corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * NUC932 cpu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/mach/map.h>
#include <mach/hardware.h>

#include "cpu.h"
#include "clock.h"

/* define specific CPU platform device */

static struct platform_device *nuc932_dev[] __initdata = {
};

/* define specific CPU platform io map */

static struct map_desc nuc932evb_iodesc[] __initdata = {
};

/*Init NUC932 evb io*/

void __init nuc932_map_io(void)
{
	nuc93x_map_io(nuc932evb_iodesc, ARRAY_SIZE(nuc932evb_iodesc));
}

/*Init NUC932 clock*/

void __init nuc932_init_clocks(void)
{
	nuc93x_init_clocks();
}

/*enable NUC932 uart clock*/

void __init nuc932_init_uartclk(void)
{
	struct clk *ck_uart = clk_get(NULL, "uart");
	BUG_ON(IS_ERR(ck_uart));

	clk_enable(ck_uart);
}

/*Init NUC932 board info*/

void __init nuc932_board_init(void)
{
	nuc93x_board_init(nuc932_dev, ARRAY_SIZE(nuc932_dev));
}
