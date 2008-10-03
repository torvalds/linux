/*
 * arch/arm/mach-ns9xxx/include/mach/io.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#define IO_SPACE_LIMIT  0xffffffff /* XXX */

#define __io(a)         ((void __iomem *)(a))
#define __mem_pci(a)    (a)
#define __mem_isa(a)    (IO_BASE + (a))

#endif /* ifndef __ASM_ARCH_IO_H */
