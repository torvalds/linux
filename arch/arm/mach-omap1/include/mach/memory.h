/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/mach-omap1/include/mach/memory.h
 */

#ifndef __ASM_ARCH_MEMORY_H
#define __ASM_ARCH_MEMORY_H

/* REVISIT: omap1 legacy drivers still rely on this */
#include <mach/soc.h>

/*
 * Bus address is physical address, except for OMAP-1510 Local Bus.
 * OMAP-1510 bus address is translated into a Local Bus address if the
 * OMAP bus type is lbus. We do the address translation based on the
 * device overriding the defaults used in the dma-mapping API.
 */

/*
 * OMAP-1510 Local Bus address offset
 */
#define OMAP1510_LB_OFFSET	UL(0x30000000)

#endif
