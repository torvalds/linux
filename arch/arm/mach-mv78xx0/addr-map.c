/*
 * arch/arm/mach-mv78xx0/addr-map.c
 *
 * Address map functions for Marvell MV78xx0 SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include "common.h"

/*
 * Generic Address Decode Windows bit settings
 */
#define TARGET_DDR		0
#define TARGET_DEV_BUS		1
#define TARGET_PCIE0		4
#define TARGET_PCIE1		8
#define TARGET_PCIE(i)		((i) ? TARGET_PCIE1 : TARGET_PCIE0)
#define ATTR_DEV_SPI_ROM	0x1f
#define ATTR_DEV_BOOT		0x2f
#define ATTR_DEV_CS3		0x37
#define ATTR_DEV_CS2		0x3b
#define ATTR_DEV_CS1		0x3d
#define ATTR_DEV_CS0		0x3e
#define ATTR_PCIE_IO(l)		(0xf0 & ~(0x10 << (l)))
#define ATTR_PCIE_MEM(l)	(0xf8 & ~(0x10 << (l)))

/*
 * Helpers to get DDR bank info
 */
#define DDR_BASE_CS_OFF(n)	(0x0000 + ((n) << 3))
#define DDR_SIZE_CS_OFF(n)	(0x0004 + ((n) << 3))

/*
 * CPU Address Decode Windows registers
 */
#define WIN0_OFF(n)		(BRIDGE_VIRT_BASE + 0x0000 + ((n) << 4))
#define WIN8_OFF(n)		(BRIDGE_VIRT_BASE + 0x0900 + (((n) - 8) << 4))
#define WIN_CTRL_OFF		0x0000
#define WIN_BASE_OFF		0x0004
#define WIN_REMAP_LO_OFF	0x0008
#define WIN_REMAP_HI_OFF	0x000c


struct mbus_dram_target_info mv78xx0_mbus_dram_info;

static void __init __iomem *win_cfg_base(int win)
{
	/*
	 * Find the control register base address for this window.
	 *
	 * BRIDGE_VIRT_BASE points to the right (CPU0's or CPU1's)
	 * MBUS bridge depending on which CPU core we're running on,
	 * so we don't need to take that into account here.
	 */

	return (void __iomem *)((win < 8) ? WIN0_OFF(win) : WIN8_OFF(win));
}

static int __init cpu_win_can_remap(int win)
{
	if (win < 8)
		return 1;

	return 0;
}

static void __init setup_cpu_win(int win, u32 base, u32 size,
				 u8 target, u8 attr, int remap)
{
	void __iomem *addr = win_cfg_base(win);
	u32 ctrl;

	base &= 0xffff0000;
	ctrl = ((size - 1) & 0xffff0000) | (attr << 8) | (target << 4) | 1;

	writel(base, addr + WIN_BASE_OFF);
	writel(ctrl, addr + WIN_CTRL_OFF);
	if (cpu_win_can_remap(win)) {
		if (remap < 0)
			remap = base;

		writel(remap & 0xffff0000, addr + WIN_REMAP_LO_OFF);
		writel(0, addr + WIN_REMAP_HI_OFF);
	}
}

void __init mv78xx0_setup_cpu_mbus(void)
{
	void __iomem *addr;
	int i;
	int cs;

	/*
	 * First, disable and clear windows.
	 */
	for (i = 0; i < 14; i++) {
		addr = win_cfg_base(i);

		writel(0, addr + WIN_BASE_OFF);
		writel(0, addr + WIN_CTRL_OFF);
		if (cpu_win_can_remap(i)) {
			writel(0, addr + WIN_REMAP_LO_OFF);
			writel(0, addr + WIN_REMAP_HI_OFF);
		}
	}

	/*
	 * Setup MBUS dram target info.
	 */
	mv78xx0_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	if (mv78xx0_core_index() == 0)
		addr = (void __iomem *)DDR_WINDOW_CPU0_BASE;
	else
		addr = (void __iomem *)DDR_WINDOW_CPU1_BASE;

	for (i = 0, cs = 0; i < 4; i++) {
		u32 base = readl(addr + DDR_BASE_CS_OFF(i));
		u32 size = readl(addr + DDR_SIZE_CS_OFF(i));

		/*
		 * Chip select enabled?
		 */
		if (size & 1) {
			struct mbus_dram_window *w;

			w = &mv78xx0_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0xf & ~(1 << i);
			w->base = base & 0xffff0000;
			w->size = (size | 0x0000ffff) + 1;
		}
	}
	mv78xx0_mbus_dram_info.num_cs = cs;
}

void __init mv78xx0_setup_pcie_io_win(int window, u32 base, u32 size,
				      int maj, int min)
{
	setup_cpu_win(window, base, size, TARGET_PCIE(maj),
		      ATTR_PCIE_IO(min), -1);
}

void __init mv78xx0_setup_pcie_mem_win(int window, u32 base, u32 size,
				       int maj, int min)
{
	setup_cpu_win(window, base, size, TARGET_PCIE(maj),
		      ATTR_PCIE_MEM(min), -1);
}
