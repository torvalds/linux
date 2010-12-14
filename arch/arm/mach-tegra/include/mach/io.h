/*
 * arch/arm/mach-tegra/include/mach/io.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_IO_H
#define __MACH_TEGRA_IO_H

#define IO_SPACE_LIMIT 0xffff

/* On TEGRA, many peripherals are very closely packed in
 * two 256MB io windows (that actually only use about 64KB
 * at the start of each).
 *
 * We will just map the first 1MB of each window (to minimize
 * pt entries needed) and provide a macro to transform physical
 * io addresses to an appropriate void __iomem *.
 *
 */

#define IO_IRAM_PHYS	0x40000000
#define IO_IRAM_VIRT	0xFE400000
#define IO_IRAM_SIZE	SZ_256K

#define IO_CPU_PHYS     0x50040000
#define IO_CPU_VIRT     0xFE000000
#define IO_CPU_SIZE	SZ_16K

#define IO_PPSB_PHYS	0x60000000
#define IO_PPSB_VIRT	0xFE200000
#define IO_PPSB_SIZE	SZ_1M

#define IO_APB_PHYS	0x70000000
#define IO_APB_VIRT	0xFE300000
#define IO_APB_SIZE	SZ_1M

#define IO_TO_VIRT_BETWEEN(p, st, sz)	((p) >= (st) && (p) < ((st) + (sz)))
#define IO_TO_VIRT_XLATE(p, pst, vst)	(((p) - (pst) + (vst)))

#define IO_TO_VIRT(n) ( \
	IO_TO_VIRT_BETWEEN((n), IO_PPSB_PHYS, IO_PPSB_SIZE) ?		\
		IO_TO_VIRT_XLATE((n), IO_PPSB_PHYS, IO_PPSB_VIRT) :	\
	IO_TO_VIRT_BETWEEN((n), IO_APB_PHYS, IO_APB_SIZE) ?		\
		IO_TO_VIRT_XLATE((n), IO_APB_PHYS, IO_APB_VIRT) :	\
	IO_TO_VIRT_BETWEEN((n), IO_CPU_PHYS, IO_CPU_SIZE) ?		\
		IO_TO_VIRT_XLATE((n), IO_CPU_PHYS, IO_CPU_VIRT) :	\
	IO_TO_VIRT_BETWEEN((n), IO_IRAM_PHYS, IO_IRAM_SIZE) ?		\
		IO_TO_VIRT_XLATE((n), IO_IRAM_PHYS, IO_IRAM_VIRT) :	\
	0)

#ifndef __ASSEMBLER__

#define __arch_ioremap(p, s, t)	tegra_ioremap(p, s, t)
#define __arch_iounmap(v)	tegra_iounmap(v)

void __iomem *tegra_ioremap(unsigned long phys, size_t size, unsigned int type);
void tegra_iounmap(volatile void __iomem *addr);

#define IO_ADDRESS(n) ((void __iomem *) IO_TO_VIRT(n))

#ifdef CONFIG_TEGRA_PCI
extern void __iomem *tegra_pcie_io_base;

static inline void __iomem *__io(unsigned long addr)
{
	return tegra_pcie_io_base + (addr & IO_SPACE_LIMIT);
}
#else
static inline void __iomem *__io(unsigned long addr)
{
	return (void __iomem *)addr;
}
#endif

#define __io(a)         __io(a)
#define __mem_pci(a)    (a)

#endif

#endif
