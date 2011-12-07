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
#include <plat/addr-map.h>
#include "common.h"

/*
 * Generic Address Decode Windows bit settings
 */
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
 * CPU Address Decode Windows registers
 */
#define WIN0_OFF(n)		(BRIDGE_VIRT_BASE + 0x0000 + ((n) << 4))
#define WIN8_OFF(n)		(BRIDGE_VIRT_BASE + 0x0900 + (((n) - 8) << 4))

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

/*
 * Description of the windows needed by the platform code
 */
static struct __initdata orion_addr_map_cfg addr_map_cfg = {
	.num_wins = 14,
	.remappable_wins = 8,
	.win_cfg_base = win_cfg_base,
};

void __init mv78xx0_setup_cpu_mbus(void)
{
	/*
	 * Disable, clear and configure windows.
	 */
	orion_config_wins(&addr_map_cfg, NULL);

	/*
	 * Setup MBUS dram target info.
	 */
	if (mv78xx0_core_index() == 0)
		orion_setup_cpu_mbus_target(&addr_map_cfg,
					    DDR_WINDOW_CPU0_BASE);
	else
		orion_setup_cpu_mbus_target(&addr_map_cfg,
					    DDR_WINDOW_CPU1_BASE);
}

void __init mv78xx0_setup_pcie_io_win(int window, u32 base, u32 size,
				      int maj, int min)
{
	orion_setup_cpu_win(&addr_map_cfg, window, base, size,
			    TARGET_PCIE(maj), ATTR_PCIE_IO(min), -1);
}

void __init mv78xx0_setup_pcie_mem_win(int window, u32 base, u32 size,
				       int maj, int min)
{
	orion_setup_cpu_win(&addr_map_cfg, window, base, size,
			    TARGET_PCIE(maj), ATTR_PCIE_MEM(min), -1);
}
