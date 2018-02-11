/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_DMI_H
#define __ASM_DMI_H

#include <linux/io.h>
#include <linux/slab.h>

#define dmi_early_remap(x, l)		memremap(x, l, MEMREMAP_WB)
#define dmi_early_unmap(x, l)		memunmap(x)
#define dmi_remap(x, l)			memremap(x, l, MEMREMAP_WB)
#define dmi_unmap(x)			memunmap(x)
#define dmi_alloc(l)			kzalloc(l, GFP_KERNEL)

#endif
