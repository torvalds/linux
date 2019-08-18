/*
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

#ifndef __MACH_TEGRA_IOMAP_H
#define __MACH_TEGRA_IOMAP_H

#include <asm/pgtable.h>
#include <asm/sizes.h>

#define TEGRA_IRAM_BASE			0x40000000
#define TEGRA_IRAM_SIZE			SZ_256K

#define TEGRA_ARM_PERIF_BASE		0x50040000
#define TEGRA_ARM_PERIF_SIZE		SZ_8K

#define TEGRA_ARM_INT_DIST_BASE		0x50041000
#define TEGRA_ARM_INT_DIST_SIZE		SZ_4K

#define TEGRA_TMR1_BASE			0x60005000
#define TEGRA_TMR1_SIZE			SZ_8

#define TEGRA_TMR2_BASE			0x60005008
#define TEGRA_TMR2_SIZE			SZ_8

#define TEGRA_TMRUS_BASE		0x60005010
#define TEGRA_TMRUS_SIZE		SZ_64

#define TEGRA_TMR3_BASE			0x60005050
#define TEGRA_TMR3_SIZE			SZ_8

#define TEGRA_TMR4_BASE			0x60005058
#define TEGRA_TMR4_SIZE			SZ_8

#define TEGRA_CLK_RESET_BASE		0x60006000
#define TEGRA_CLK_RESET_SIZE		SZ_4K

#define TEGRA_FLOW_CTRL_BASE		0x60007000
#define TEGRA_FLOW_CTRL_SIZE		20

#define TEGRA_SB_BASE			0x6000C200
#define TEGRA_SB_SIZE			256

#define TEGRA_EXCEPTION_VECTORS_BASE    0x6000F000
#define TEGRA_EXCEPTION_VECTORS_SIZE    SZ_4K

#define TEGRA_APB_MISC_BASE		0x70000000
#define TEGRA_APB_MISC_SIZE		SZ_4K

#define TEGRA_UARTA_BASE		0x70006000
#define TEGRA_UARTA_SIZE		SZ_64

#define TEGRA_UARTB_BASE		0x70006040
#define TEGRA_UARTB_SIZE		SZ_64

#define TEGRA_UARTC_BASE		0x70006200
#define TEGRA_UARTC_SIZE		SZ_256

#define TEGRA_UARTD_BASE		0x70006300
#define TEGRA_UARTD_SIZE		SZ_256

#define TEGRA_UARTE_BASE		0x70006400
#define TEGRA_UARTE_SIZE		SZ_256

#define TEGRA_PMC_BASE			0x7000E400
#define TEGRA_PMC_SIZE			SZ_256

#define TEGRA_MC_BASE			0x7000F000
#define TEGRA_MC_SIZE			SZ_1K

#define TEGRA_EMC_BASE			0x7000F400
#define TEGRA_EMC_SIZE			SZ_1K

#define TEGRA114_MC_BASE		0x70019000
#define TEGRA114_MC_SIZE		SZ_4K

#define TEGRA_EMC0_BASE			0x7001A000
#define TEGRA_EMC0_SIZE			SZ_2K

#define TEGRA_EMC1_BASE			0x7001A800
#define TEGRA_EMC1_SIZE			SZ_2K

#define TEGRA124_MC_BASE		0x70019000
#define TEGRA124_MC_SIZE		SZ_4K

#define TEGRA124_EMC_BASE		0x7001B000
#define TEGRA124_EMC_SIZE		SZ_2K

#define TEGRA_CSITE_BASE		0x70040000
#define TEGRA_CSITE_SIZE		SZ_256K

/* On TEGRA, many peripherals are very closely packed in
 * two 256MB io windows (that actually only use about 64KB
 * at the start of each).
 *
 * We will just map the first MMU section of each window (to minimize
 * pt entries needed) and provide a macro to transform physical
 * io addresses to an appropriate void __iomem *.
 */

#define IO_IRAM_PHYS	0x40000000
#define IO_IRAM_VIRT	IOMEM(0xFE400000)
#define IO_IRAM_SIZE	SZ_256K

#define IO_CPU_PHYS	0x50040000
#define IO_CPU_VIRT	IOMEM(0xFE440000)
#define IO_CPU_SIZE	SZ_16K

#define IO_PPSB_PHYS	0x60000000
#define IO_PPSB_VIRT	IOMEM(0xFE200000)
#define IO_PPSB_SIZE	SECTION_SIZE

#define IO_APB_PHYS	0x70000000
#define IO_APB_VIRT	IOMEM(0xFE000000)
#define IO_APB_SIZE	SECTION_SIZE

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
	NULL)

#define IO_ADDRESS(n) (IO_TO_VIRT(n))

#endif
