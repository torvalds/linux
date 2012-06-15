/*
 * Copyright (C) 2010 NVIDIA Corporation.
 * Copyright (C) 2010 Google, Inc.
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

#ifndef __MACH_TEGRA_APBIO_H
#define __MACH_TEGRA_APBIO_H

#ifdef CONFIG_TEGRA_SYSTEM_DMA

u32 tegra_apb_readl(unsigned long offset);
void tegra_apb_writel(u32 value, unsigned long offset);

#else
#include <asm/io.h>
#include <mach/io.h>

static inline u32 tegra_apb_readl(unsigned long offset)
{
        return readl(IO_TO_VIRT(offset));
}

static inline void tegra_apb_writel(u32 value, unsigned long offset)
{
        writel(value, IO_TO_VIRT(offset));
}
#endif

#endif
