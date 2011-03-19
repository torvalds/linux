/*
 * arch/arm/mach-kirkwood/include/mach/io.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

#include "kirkwood.h"

#define IO_SPACE_LIMIT		0xffffffff

static inline void __iomem *__io(unsigned long addr)
{
	return (void __iomem *)((addr - KIRKWOOD_PCIE_IO_BUS_BASE)
					+ KIRKWOOD_PCIE_IO_VIRT_BASE);
}

static inline void __iomem *
__arch_ioremap(unsigned long paddr, size_t size, unsigned int mtype)
{
	void __iomem *retval;
	unsigned long offs = paddr - KIRKWOOD_REGS_PHYS_BASE;
	if (mtype == MT_DEVICE && size && offs < KIRKWOOD_REGS_SIZE &&
	    size <= KIRKWOOD_REGS_SIZE && offs + size <= KIRKWOOD_REGS_SIZE) {
		retval = (void __iomem *)KIRKWOOD_REGS_VIRT_BASE + offs;
	} else {
		retval = __arm_ioremap(paddr, size, mtype);
	}

	return retval;
}

static inline void
__arch_iounmap(void __iomem *addr)
{
	if (addr < (void __iomem *)KIRKWOOD_REGS_VIRT_BASE ||
	    addr >= (void __iomem *)(KIRKWOOD_REGS_VIRT_BASE + KIRKWOOD_REGS_SIZE))
		__iounmap(addr);
}

#define __arch_ioremap		__arch_ioremap
#define __arch_iounmap		__arch_iounmap
#define __io(a)			__io(a)
#define __mem_pci(a)		(a)


#endif
