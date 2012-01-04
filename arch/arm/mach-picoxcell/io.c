/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>

#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/picoxcell_soc.h>

#include "common.h"

void __init picoxcell_map_io(void)
{
	struct map_desc io_map = {
		.virtual	= PHYS_TO_IO(PICOXCELL_PERIPH_BASE),
		.pfn		= __phys_to_pfn(PICOXCELL_PERIPH_BASE),
		.length		= PICOXCELL_PERIPH_LENGTH,
		.type		= MT_DEVICE,
	};

	iotable_init(&io_map, 1);
}
