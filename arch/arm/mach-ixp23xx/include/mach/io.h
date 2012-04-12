/*
 * arch/arm/mach-ixp23xx/include/mach/io.h
 *
 * Original Author: Naeem M Afzal <naeem.m.afzal@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2003-2005 Intel Corp.
 * Copyright (C) 2005 MontaVista Software, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#define IO_SPACE_LIMIT 0xffffffff

#define __io(p)		((void __iomem*)((p) + IXP23XX_PCI_IO_VIRT))

#endif
