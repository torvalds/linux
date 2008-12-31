/*
 * arch/arm/plat-iop/setup.c
 *
 * Author: Nicolas Pitre <nico@cam.org>
 * Copyright (C) 2001 MontaVista Software, Inc.
 * Copyright (C) 2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <asm/mach/map.h>
#include <asm/hardware/iop3xx.h>

/*
 * Standard IO mapping for all IOP3xx based systems.  Note that
 * the IOP3xx OCCDR must be mapped uncached and unbuffered.
 */
static struct map_desc iop3xx_std_desc[] __initdata = {
	 {	/* mem mapped registers */
		.virtual	= IOP3XX_PERIPHERAL_VIRT_BASE,
		.pfn		= __phys_to_pfn(IOP3XX_PERIPHERAL_PHYS_BASE),
		.length		= IOP3XX_PERIPHERAL_SIZE,
		.type		= MT_UNCACHED,
	 }, {	/* PCI IO space */
		.virtual	= IOP3XX_PCI_LOWER_IO_VA,
		.pfn		= __phys_to_pfn(IOP3XX_PCI_LOWER_IO_PA),
		.length		= IOP3XX_PCI_IO_WINDOW_SIZE,
		.type		= MT_DEVICE,
	 },
};

void __init iop3xx_map_io(void)
{
	iotable_init(iop3xx_std_desc, ARRAY_SIZE(iop3xx_std_desc));
}
