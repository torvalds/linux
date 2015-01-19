/*
 *
 * arch/arm/plat-meson/include/plat/platform.h
 *
 *  Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic platform init and mapping functions.
 */

#ifndef __PLAT_MESON_PLATFORM_H
#define __PLAT_MESON_PLATFORM_H

#include <linux/types.h>
#include <mach/pinmux.h>


void meson_init_irq(void);
void meson_init_devices(void);

#ifdef CONFIG_MESON_PLATFORM_API
#include "resource.h"

//~ static inline int32_t mesonplat_resource_get_num(const char * name)
//~ {
	//~ ///temp , we should implement a real function later .
	//~ return 4;
//~ }

typedef void * platform_data_t ;

static inline int32_t mesonplat_pad_enable(platform_data_t platform_data)
{
	return pinmux_set(platform_data);
}

static inline int32_t mesonplat_pad_disable(platform_data_t platform_data)
{
	return pinmux_clr(platform_data);
}
#endif // CONFIG_MESON_PLATFORM_API

#endif
