/*
 * arch/arm/mach-tegra/headavp.h
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _MACH_TEGRA_HEADAVP_H
#define _MACH_TEGRA_HEADAVP_H

#define AVP_MMU_TLB_BASE		0xF000F000
#define AVP_KERNEL_VIRT_BASE		0x00100000

#ifndef __ASSEMBLY__

struct tegra_avp_boot_stub_data {
	unsigned long	mmu_tlb_base;
	unsigned long	jump_addr;
	unsigned long	map_virt_addr;
	unsigned long	map_phys_addr;
};

extern void _tegra_avp_boot_stub(void);
extern struct tegra_avp_boot_stub_data _tegra_avp_boot_stub_data;

#endif

#endif
