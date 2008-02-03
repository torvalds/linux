/*
 * RapidIO PPC32 support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/rio.h>

#include <asm/rio.h>

/**
 * platform_rio_init - Do platform specific RIO init
 *
 * Any platform specific initialization of RapdIO
 * hardware is done here as well as registration
 * of any active master ports in the system.
 */
void __attribute__ ((weak))
    platform_rio_init(void)
{
	printk(KERN_WARNING "RIO: No platform_rio_init() present\n");
}

/**
 * ppc_rio_init - Do PPC32 RIO init
 *
 * Calls platform-specific RIO init code and then calls
 * rio_init_mports() to initialize any master ports that
 * have been registered with the RIO subsystem.
 */
static int __init ppc_rio_init(void)
{
	printk(KERN_INFO "RIO: RapidIO init\n");

	/* Platform specific initialization */
	platform_rio_init();

	/* Enumerate all registered ports */
	rio_init_mports();

	return 0;
}

subsys_initcall(ppc_rio_init);
