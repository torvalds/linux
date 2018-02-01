/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 *
 * All rights reserved.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#ifndef _CPQPHP_NVRAM_H
#define _CPQPHP_NVRAM_H

#ifndef CONFIG_HOTPLUG_PCI_COMPAQ_NVRAM

static inline void compaq_nvram_init(void __iomem *rom_start)
{
	return;
}

static inline int compaq_nvram_load(void __iomem *rom_start, struct controller *ctrl)
{
	return 0;
}

static inline int compaq_nvram_store(void __iomem *rom_start)
{
	return 0;
}

#else

void compaq_nvram_init(void __iomem *rom_start);
int compaq_nvram_load(void __iomem *rom_start, struct controller *ctrl);
int compaq_nvram_store(void __iomem *rom_start);

#endif

#endif

