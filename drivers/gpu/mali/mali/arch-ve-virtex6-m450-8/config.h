/*
 * Copyright (C) 2010, 2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARCH_CONFIG_H__
#define __ARCH_CONFIG_H__

/* Configuration for the Versatile Express platform */

#define MALI_BASE_ADDRESS 0xFC040000

static _mali_osk_resource_t arch_configuration [] =
{
	/* GP cluster */
	{
		.type = MALI400L2,
		.base = MALI_BASE_ADDRESS + 0x10000,
		.description = "Mali-450 L2 cache for GP"
	},
	{
		.type = MALI400GP,
		.description = "Mali-450 GP",
		.base = MALI_BASE_ADDRESS,
		.irq = -1,
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x3000,
		.irq = 70,
		.description = "Mali-450 MMU for GP",
	},

	/* PP0-3 cluster */
	{
		.type = MALI400L2,
		.base = MALI_BASE_ADDRESS + 0x1000,
		.description = "Mali-450 L2 cache for PP0-3"
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0x8000,
		.irq = 70,
		.description = "Mali-450 PP0",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x4000,
		.irq = 70,
		.description = "Mali-450 MMU for PP0",
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0xA000,
		.irq = 70,
		.description = "Mali-450 PP1",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x5000,
		.irq = 70,
		.description = "Mali-450 MMU for PP1",
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0xC000,
		.irq = 70,
		.description = "Mali-450 PP2",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x6000,
		.irq = 70,
		.description = "Mali-450 MMU for PP2",
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0xE000,
		.irq = 70,
		.description = "Mali-450 PP3",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x7000,
		.irq = 70,
		.description = "Mali-450 MMU for PP3",
	},

	/* PP4-7 cluster */
	{
		.type = MALI400L2,
		.base = MALI_BASE_ADDRESS + 0x11000,
		.description = "Mali-450 L2 cache for PP4-7"
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0x28000,
		.irq = 70,
		.description = "Mali-450 PP4",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x1C000,
		.irq = 70,
		.description = "Mali-450 MMU for PP4",
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0x2A000,
		.irq = 70,
		.description = "Mali-450 PP5",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x1D000,
		.irq = 70,
		.description = "Mali-450 MMU for PP5",
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0x2C000,
		.irq = 70,
		.description = "Mali-450 PP6",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x1E000,
		.irq = 70,
		.description = "Mali-450 MMU for PP6",
	},
	{
		.type = MALI400PP,
		.base = MALI_BASE_ADDRESS + 0x2E000,
		.irq = 70,
		.description = "Mali-450 PP7",
	},
	{
		.type = MMU,
		.base = MALI_BASE_ADDRESS + 0x1F000,
		.irq = 70,
		.description = "Mali-450 MMU for PP7",
	},

	/* Memory */
	{
		.type = OS_MEMORY,
		.description = "Mali OS memory",
		.cpu_usage_adjust = 0,
		.alloc_order = 0, /* Highest preference for this memory */
		.base = 0x0,
		.size = 256 * 1024 * 1024,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_READABLE | _MALI_PP_WRITEABLE |_MALI_GP_READABLE | _MALI_GP_WRITEABLE
	},
	{
		.type = MEM_VALIDATION,
		.description = "Framebuffer",
		.base = 0xe0000000,
		.size = 0x01000000,
		.flags = _MALI_CPU_WRITEABLE | _MALI_CPU_READABLE | _MALI_PP_WRITEABLE | _MALI_PP_READABLE
	},
};

#endif /* __ARCH_CONFIG_H__ */
