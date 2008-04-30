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
#include <asm/hardware.h>
#include <asm/io.h>
#include "common.h"

/*
 * The Orion has fully programable address map. There's a separate address
 * map for each of the device _master_ interfaces, e.g. CPU, PCI, PCIE, USB,
 * Gigabit Ethernet, DMA/XOR engines, etc. Each interface has its own
 * address decode windows that allow it to access any of the Orion resources.
 *
 * CPU address decoding --
 * Linux assumes that it is the boot loader that already setup the access to
 * DDR and internal registers.
 * Setup access to PCI and PCI-E IO/MEM space is issued by this file.
 * Setup access to various devices located on the device bus interface (e.g.
 * flashes, RTC, etc) should be issued by machine-setup.c according to
 * specific board population (by using orion5x_setup_*_win()).
 *
 * Non-CPU Masters address decoding --
 * Unlike the CPU, we setup the access from Orion's master interfaces to DDR
 * banks only (the typical use case).
 * Setup access for each master to DDR is issued by common.c.
 *
 * Note: although orion_setbits() and orion_clrbits() are not atomic
 * no locking is necessary here since code in this file is only called
 * at boot time when there is no concurrency issues.
 */

/*
 * Generic Address Decode Windows bit settings
 */
#define TARGET_DDR		0
#define TARGET_DEV_BUS		1
#define TARGET_PCI		3
#define TARGET_PCIE		4
#define ATTR_DDR_CS(n)		(((n) ==0) ? 0xe :	\
				((n) == 1) ? 0xd :	\
				((n) == 2) ? 0xb :	\
				((n) == 3) ? 0x7 : 0xf)
#define ATTR_PCIE_MEM		0x59
#define ATTR_PCIE_IO		0x51
#define ATTR_PCIE_WA		0x79
#define ATTR_PCI_MEM		0x59
#define ATTR_PCI_IO		0x51
#define ATTR_DEV_CS0		0x1e
#define ATTR_DEV_CS1		0x1d
#define ATTR_DEV_CS2		0x1b
#define ATTR_DEV_BOOT		0xf
#define WIN_EN			1

/*
 * Helpers to get DDR bank info
 */
#define DDR_BASE_CS(n)		ORION5X_DDR_REG(0x1500 + ((n) * 8))
#define DDR_SIZE_CS(n)		ORION5X_DDR_REG(0x1504 + ((n) * 8))
#define DDR_MAX_CS		4
#define DDR_REG_TO_SIZE(reg)	(((reg) | 0xffffff) + 1)
#define DDR_REG_TO_BASE(reg)	((reg) & 0xff000000)
#define DDR_BANK_EN		1

/*
 * CPU Address Decode Windows registers
 */
#define CPU_WIN_CTRL(n)		ORION5X_BRIDGE_REG(0x000 | ((n) << 4))
#define CPU_WIN_BASE(n)		ORION5X_BRIDGE_REG(0x004 | ((n) << 4))
#define CPU_WIN_REMAP_LO(n)	ORION5X_BRIDGE_REG(0x008 | ((n) << 4))
#define CPU_WIN_REMAP_HI(n)	ORION5X_BRIDGE_REG(0x00c | ((n) << 4))

/*
 * Gigabit Ethernet Address Decode Windows registers
 */
#define ETH_WIN_BASE(win)	ORION5X_ETH_REG(0x200 + ((win) * 8))
#define ETH_WIN_SIZE(win)	ORION5X_ETH_REG(0x204 + ((win) * 8))
#define ETH_WIN_REMAP(win)	ORION5X_ETH_REG(0x280 + ((win) * 4))
#define ETH_WIN_EN		ORION5X_ETH_REG(0x290)
#define ETH_WIN_PROT		ORION5X_ETH_REG(0x294)
#define ETH_MAX_WIN		6
#define ETH_MAX_REMAP_WIN	4


struct mbus_dram_target_info orion5x_mbus_dram_info;

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

static void __init setup_cpu_win(int win, u32 base, u32 size,
				 u8 target, u8 attr, int remap)
{
	orion5x_write(CPU_WIN_BASE(win), base & 0xffff0000);
	orion5x_write(CPU_WIN_CTRL(win),
		((size - 1) & 0xffff0000) | (attr << 8) | (target << 4) | 1);

	if (orion5x_cpu_win_can_remap(win)) {
		if (remap < 0)
			remap = base;

		orion5x_write(CPU_WIN_REMAP_LO(win), remap & 0xffff0000);
		orion5x_write(CPU_WIN_REMAP_HI(win), 0);
	}
}

void __init orion5x_setup_cpu_mbus_bridge(void)
{
	int i;
	int cs;

	/*
	 * First, disable and clear windows.
	 */
	for (i = 0; i < 8; i++) {
		orion5x_write(CPU_WIN_BASE(i), 0);
		orion5x_write(CPU_WIN_CTRL(i), 0);
		if (orion5x_cpu_win_can_remap(i)) {
			orion5x_write(CPU_WIN_REMAP_LO(i), 0);
			orion5x_write(CPU_WIN_REMAP_HI(i), 0);
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
			w->base = base & 0xff000000;
			w->size = (size | 0x00ffffff) + 1;
		}
	}
	orion5x_mbus_dram_info.num_cs = cs;
}

void __init orion5x_setup_dev_boot_win(u32 base, u32 size)
{
	setup_cpu_win(4, base, size, TARGET_DEV_BUS, ATTR_DEV_BOOT, -1);
}

void __init orion5x_setup_dev0_win(u32 base, u32 size)
{
	setup_cpu_win(5, base, size, TARGET_DEV_BUS, ATTR_DEV_CS0, -1);
}

void __init orion5x_setup_dev1_win(u32 base, u32 size)
{
	setup_cpu_win(6, base, size, TARGET_DEV_BUS, ATTR_DEV_CS1, -1);
}

void __init orion5x_setup_dev2_win(u32 base, u32 size)
{
	setup_cpu_win(7, base, size, TARGET_DEV_BUS, ATTR_DEV_CS2, -1);
}

void __init orion5x_setup_pcie_wa_win(u32 base, u32 size)
{
	setup_cpu_win(7, base, size, TARGET_PCIE, ATTR_PCIE_WA, -1);
}

void __init orion5x_setup_eth_wins(void)
{
	int i;

	/*
	 * First, disable and clear windows
	 */
	for (i = 0; i < ETH_MAX_WIN; i++) {
		orion5x_write(ETH_WIN_BASE(i), 0);
		orion5x_write(ETH_WIN_SIZE(i), 0);
		orion5x_setbits(ETH_WIN_EN, 1 << i);
		orion5x_clrbits(ETH_WIN_PROT, 0x3 << (i * 2));
		if (i < ETH_MAX_REMAP_WIN)
			orion5x_write(ETH_WIN_REMAP(i), 0);
	}

	/*
	 * Setup windows for DDR banks.
	 */
	for (i = 0; i < DDR_MAX_CS; i++) {
		u32 base, size;
		size = orion5x_read(DDR_SIZE_CS(i));
		base = orion5x_read(DDR_BASE_CS(i));
		if (size & DDR_BANK_EN) {
			base = DDR_REG_TO_BASE(base);
			size = DDR_REG_TO_SIZE(size);
			orion5x_write(ETH_WIN_SIZE(i), (size-1) & 0xffff0000);
			orion5x_write(ETH_WIN_BASE(i), (base & 0xffff0000) |
					(ATTR_DDR_CS(i) << 8) |
					TARGET_DDR);
			orion5x_clrbits(ETH_WIN_EN, 1 << i);
			orion5x_setbits(ETH_WIN_PROT, 0x3 << (i * 2));
		}
	}
}
