/*
 * arch/arm/mach-orion5x/include/mach/io.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#include <mach/orion5x.h>
#include <asm/sizes.h>

#define IO_SPACE_LIMIT		SZ_2M
static inline void __iomem *__io(unsigned long addr)
{
	return (void __iomem *)(addr + ORION5X_PCIE_IO_VIRT_BASE);
}

#define __io(a)			 __io(a)
#endif
