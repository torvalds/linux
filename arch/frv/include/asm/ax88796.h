/* ax88796.h: access points to the driver for the AX88796 NE2000 clone
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_AX88796_H
#define _ASM_AX88796_H

#include <asm/mb-regs.h>

#define AX88796_IOADDR		(__region_CS1 + 0x200)
#define AX88796_IRQ		IRQ_CPU_EXTERNAL7
#define AX88796_FULL_DUPLEX	0			/* force full duplex */
#define AX88796_BUS_INFO	"CS1#+0x200"		/* bus info for ethtool */

#endif /* _ASM_AX88796_H */
