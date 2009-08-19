/*
 * Copyright (C) 2009 Thomas Gleixner <tglx@linutronix.de>
 *
 *  For licencing details see kernel-base/COPYING
 */
#include <linux/init.h>

#include <asm/x86_init.h>

void __cpuinit x86_init_noop(void) { }

/*
 * The platform setup functions are preset with the default functions
 * for standard PC hardware.
 */
struct __initdata x86_init_ops x86_init = {

	.resources = {
		.probe_roms		= x86_init_noop,
	},
};
