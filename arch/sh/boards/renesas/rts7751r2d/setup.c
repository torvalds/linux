/*
 * linux/arch/sh/kernel/setup_rts7751r2d.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Modified for RTS7751R2D by
 * Atom Create Engineering Co., Ltd. 2002.
 */

#include <linux/init.h>
#include <linux/pm.h>
#include <asm/io.h>
#include <asm/rts7751r2d/rts7751r2d.h>

unsigned int debug_counter;

const char *get_system_type(void)
{
	return "RTS7751R2D";
}

static void rts7751r2d_power_off(void)
{
	ctrl_outw(0x0001, PA_POWOFF);
}

/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	printk(KERN_INFO "Renesas Technology Sales RTS7751R2D support.\n");
	ctrl_outw(0x0000, PA_OUTPORT);
	pm_power_off = rts7751r2d_power_off;
	debug_counter = 0;
}
