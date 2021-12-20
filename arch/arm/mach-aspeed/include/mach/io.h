/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IO definitions for ASPEED PCI IO configuration
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#if defined(CONFIG_PCI) && defined(CONFIG_PCIE_ASPEED)
#include <linux/aspeed_pcie_io.h>

#define outb(v, p)	aspeed_pcie_outb(v, p)
#define inb(p)	aspeed_pcie_inb(p)
#endif

#endif
