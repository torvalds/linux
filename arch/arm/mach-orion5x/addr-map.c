/*
 * arch/arm/mach-orion5x/addr-map.c
 *
 * Address map functions for Marvell Orion 5x SoCs
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <mach/hardware.h>
#include "common.h"

/*
 * The Orion has fully programable address map. There's a separate address
 * map for each of the device _master_ interfaces, e.g. CPU, PCI, PCIe, USB,
 * Gigabit Ethernet, DMA/XOR engines, etc. Each interface has its own
 * address decode windows that allow it to access any of the Orion resources.
 *
 * CPU address decoding --
 * Linux assumes that it is the boot loader that already setup the access to
 * DDR and internal registers.
 * Setup access to PCI and PCIe IO/MEM space is issued by this file.
 * Setup access to various devices located on the device bus interface (e.g.
 * flashes, RTC, etc) should be issued by machine-setup.c according to
 * specific board population (by using orion5x_setup_*_win()).
 *
 * Non-CPU Masters address decoding --
 * Unlike the CPU, we setup the access from Orion's master interfaces to DDR
 * banks only (the typical use case).
 * Setup access for each master to DDR is issued by platform device setup.
 */

/*
 * Generic Address Decode Windows bit settings
 */
#define TARGET_DDR		0
#define TARGET_DEV_BUS		1
#define TARGET_PCI		3
#define TARGET_PCIE		4
#define TARGET_SRAM		9
#define ATTR_PCIE_MEM		0x59
#define ATTR_PCIE_IO		0x51
#define ATTR_PCIE_WA		0x79
#define ATTR_PCI_MEM		0x59
#define ATTR_PCI_IO		0x51
#define ATTR_DEV_CS0		0x1e
#define ATTR_DEV_CS1		0x1d
#define ATTR_DEV_CS2		0x1b
#define ATTR_DEV_BOOT		0xf
#define ATTR_SRAM		0x0

/*
 * Helpers to get DDR bank info
 */
#define ORION5X_DDR_REG(x)	(ORION5X_DDR_VIRT_BASE | (x))
#define DDR_BASE_CS(n)		ORION5X_DDR_REG(0x1500 + ((n) << 3))
#define DDR_SIZE_CS(n)		ORION5X_DDR_REG(0x1504 + ((n) << 3))

/*
 * CPU Address Decode Windows registers
 */
#define ORION5X_BRIDGE_REG(x)	(ORION5X_BRIDGE_VIRT_BASE | (x))
#define CPU_WIN_CTRL(n)		ORION5X_BRIDGE_REG(0x000 | ((n) << 4))
#define CPU_WIN_BASE(n)		ORION5X_BRIDGE_REG(0x004 | ((n) << 4))
#define CPU_WIN_REMAP_LO(n)	ORION5X_BRIDGE_REG(0x008 | ((n) << 4))
#define CPU_WIN_REMAP_HI(n)	ORION5X_BRIDGE_REG(0x00c | ((n) << 4))


struct mbus_dram_target_info orion5x_mbus_dram_info;
static int __initdata win_alloc_count;

static int __init orion5x_cpu_win_can_remap(int win)
{
	u32 dev, rev;

	orion5x_pcie_id(&dev, &rev);
	if ((dev == MV88F5281_DEV_ID && win < 4)
	    || (dev == MV88F5182_DEV_ID && win < 2)
	    || (dev == MV88F5181_DEV_ID && win < 2))
		return 1;

	return 0;
}

static int __init setup_cpu_win(int win, u32 base, u32 size,
				 u8 target, u8 attr, int remap)
{
	if (win >= 8) {
		printk(KERN_ERR "setup_cpu_win: trying to allocate "
				"window %d\n", win);
		return -ENOSPC;
	}

	writel(base & 0xffff0000, CPU_WIN_BASE(win));
	writel(((size - 1) & 0xffff0000) | (attr << 8) | (target << 4) | 1,
		CPU_WIN_CTRL(win));

	if (orion5x_cpu_win_can_remap(win)) {
		if (remap < 0)
			remap = base;

		writel(remap & 0xffff0000, CPU_WIN_REMAP_LO(win));
		writel(0, CPU_WIN_REMAP_HI(win));
	}
	return 0;
}

void __init orion5x_setup_cpu_mbus_bridge(void)
{
	int i;
	int cs;

	/*
	 * First, disable and clear windows.
	 */
	for (i = 0; i < 8; i++) {
		writel(0, CPU_WIN_BASE(i));
		writel(0, CPU_WIN_CTRL(i));
		if (orion5x_cpu_win_can_remap(i)) {
			writel(0, CPU_WIN_REMAP_LO(i));
			writel(0, CPU_WIN_REMAP_HI(i));
		}
	}

	/*
	 * Setup windows for PCI+PCIe IO+MEM space.
	 */
	setup_cpu_win(0, ORION5X_PCIE_IO_PHYS_BASE, ORION5X_PCIE_IO_SIZE,
		TARGET_PCIE, ATTR_PCIE_IO, ORION5X_PCIE_IO_BUS_BASE);
	setup_cpu_win(1, ORION5X_PCI_IO_PHYS_BASE, ORION5X_PCI_IO_SIZE,
		TARGET_PCI, ATTR_PCI_IO, ORION5X_PCI_IO_BUS_BASE);
	setup_cpu_win(2, ORION5X_PCIE_MEM_PHYS_BASE, ORION5X_PCIE_MEM_SIZE,
		TARGET_PCIE, ATTR_PCIE_MEM, -1);
	setup_cpu_win(3, ORION5X_PCI_MEM_PHYS_BASE, ORION5X_PCI_MEM_SIZE,
		TARGET_PCI, ATTR_PCI_MEM, -1);
	win_alloc_count = 4;

	/*
	 * Setup MBUS dram target info.
	 */
	orion5x_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 4; i++) {
		u32 base = readl(DDR_BASE_CS(i));
		u32 size = readl(DDR_SIZE_CS(i));

		/*
		 * Chip select enabled?
		 */
		if (size & 1) {
			struct mbus_dram_window *w;

			w = &orion5x_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0xf & ~(1 << i);
			w->base = base & 0xffff0000;
			w->size = (size | 0x0000ffff) + 1;
		}
	}
	orion5x_mbus_dram_info.num_cs = cs;
}

void __init orion5x_setup_dev_boot_win(u32 base, u32 size)
{
	setup_cpu_win(win_alloc_count++, base, size,
		      TARGET_DEV_BUS, ATTR_DEV_BOOT, -1);
}

void __init orion5x_setup_dev0_win(u32 base, u32 size)
{
	setup_cpu_win(win_alloc_count++, base, size,
		      TARGET_DEV_BUS, ATTR_DEV_CS0, -1);
}

void __init orion5x_setup_dev1_win(u32 base, u32 size)
{
	setup_cpu_win(win_alloc_count++, base, size,
		      TARGET_DEV_BUS, ATTR_DEV_CS1, -1);
}

void __init orion5x_setup_dev2_win(u32 base, u32 size)
{
	setup_cpu_win(win_alloc_count++, base, size,
		      TARGET_DEV_BUS, ATTR_DEV_CS2, -1);
}

void __init orion5x_setup_pcie_wa_win(u32 base, u32 size)
{
	setup_cpu_win(win_alloc_count++, base, size,
		      TARGET_PCIE, ATTR_PCIE_WA, -1);
}

int __init orion5x_setup_sram_win(void)
{
	return setup_cpu_win(win_alloc_count++, ORION5X_SRAM_PHYS_BASE,
			ORION5X_SRAM_SIZE, TARGET_SRAM, ATTR_SRAM, -1);
}
