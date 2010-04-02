/*
 * arch/arm/mach-ixp4xx/include/mach/hardware.h 
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * Hardware definitions for IXP4xx based systems
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#define __ASM_ARCH_HARDWARE_H__

#define PCIBIOS_MIN_IO		0x00001000
#ifdef CONFIG_IXP4XX_INDIRECT_PCI
#define PCIBIOS_MIN_MEM		0x10000000 /* 1 GB of indirect PCI MMIO space */
#define PCIBIOS_MAX_MEM		0x4FFFFFFF
#else
#define PCIBIOS_MIN_MEM		0x48000000 /* 64 MB of PCI MMIO space */
#define PCIBIOS_MAX_MEM		0x4BFFFFFF
#endif

#define pcibios_assign_all_busses()	1

/* Register locations and bits */
#include "ixp4xx-regs.h"

#ifndef __ASSEMBLER__
#include <mach/cpu.h>
#endif

/* Platform helper functions and definitions */
#include "platform.h"

#endif  /* _ASM_ARCH_HARDWARE_H */
