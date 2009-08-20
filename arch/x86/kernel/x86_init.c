/*
 * Copyright (C) 2009 Thomas Gleixner <tglx@linutronix.de>
 *
 *  For licencing details see kernel-base/COPYING
 */
#include <linux/init.h>

#include <asm/bios_ebda.h>
#include <asm/setup.h>
#include <asm/e820.h>

void __cpuinit x86_init_noop(void) { }
void __init x86_init_uint_noop(unsigned int unused) { }

/*
 * The platform setup functions are preset with the default functions
 * for standard PC hardware.
 */
struct __initdata x86_init_ops x86_init = {

	.resources = {
		.probe_roms		= x86_init_noop,
		.reserve_resources	= reserve_standard_io_resources,
		.reserve_ebda_region	= reserve_ebda_region,
		.memory_setup		= default_machine_specific_memory_setup,
	},

	.mpparse = {
		.mpc_record		= x86_init_uint_noop,
		.setup_ioapic_ids	= x86_init_noop,
	},
};
