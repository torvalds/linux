/*
 *  Setup code for SAMv7x
 *
 *  Copyright (C) 2013 Atmel,
 *                2016 Andras Szemzo <szemzo.andras@gmail.com>
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>
#include "generic.h"

#ifdef CONFIG_PM
/* This function has to be defined for various drivers that are using it */
int at91_suspend_entering_slow_clock(void)
{
	return 0;
}
EXPORT_SYMBOL(at91_suspend_entering_slow_clock);
#endif

static const char *const samv7_dt_board_compat[] __initconst = {
	"atmel,samv7",
	NULL
};

DT_MACHINE_START(samv7_dt, "Atmel SAMV7")
	.dt_compat	= samv7_dt_board_compat,
MACHINE_END
