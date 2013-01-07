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
#include <mach/hardware.h>
#include <plat/addr-map.h>
#include "common.h"

/*
 * The Orion has fully programmable address map. There's a separate address
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

static int __initdata win_alloc_count;

static int __init cpu_win_can_remap(const struct orion_addr_map_cfg *cfg,
		  const int win)
{
	u32 dev, rev;

	orion5x_pcie_id(&dev, &rev);
	if ((dev == MV88F5281_DEV_ID && win < 4)
	    || (dev == MV88F5182_DEV_ID && win < 2)
	    || (dev == MV88F5181_DEV_ID && win < 2)
	    || (dev == MV88F6183_DEV_ID && win < 4))
		return 1;

	return 0;
}

/*
 * Description of the windows needed by the platform code
 */
static struct orion_addr_map_cfg addr_map_cfg __initdata = {
	.num_wins = 8,
	.cpu_win_can_remap = cpu_win_can_remap,
	.bridge_virt_base = ORION5X_BRIDGE_VIRT_BASE,
};

static const struct __initdata orion_addr_map_info addr_map_info[] = {
	/*
	 * Setup windows for PCI+PCIe IO+MEM space.
	 */
	{ 0, ORION5X_PCIE_IO_PHYS_BASE, ORION5X_PCIE_IO_SIZE,
	  TARGET_PCIE, ATTR_PCIE_IO, ORION5X_PCIE_IO_BUS_BASE
	},
	{ 1, ORION5X_PCI_IO_PHYS_BASE, ORION5X_PCI_IO_SIZE,
	  TARGET_PCI, ATTR_PCI_IO, ORION5X_PCI_IO_BUS_BASE
	},
	{ 2, ORION5X_PCIE_MEM_PHYS_BASE, ORION5X_PCIE_MEM_SIZE,
	  TARGET_PCIE, ATTR_PCIE_MEM, -1
	},
	{ 3, ORION5X_PCI_MEM_PHYS_BASE, ORION5X_PCI_MEM_SIZE,
	  TARGET_PCI, ATTR_PCI_MEM, -1
	},
	/* End marker */
	{ -1, 0, 0, 0, 0, 0 }
};

void __init orion5x_setup_cpu_mbus_bridge(void)
{
	/*
	 * Disable, clear and configure windows.
	 */
	orion_config_wins(&addr_map_cfg, addr_map_info);
	win_alloc_count = 4;

	/*
	 * Setup MBUS dram target info.
	 */
	orion_setup_cpu_mbus_target(&addr_map_cfg,
				    (void __iomem *) ORION5X_DDR_WINDOW_CPU_BASE);
}

void __init orion5x_setup_dev_boot_win(u32 base, u32 size)
{
	orion_setup_cpu_win(&addr_map_cfg, win_alloc_count++, base, size,
			    TARGET_DEV_BUS, ATTR_DEV_BOOT, -1);
}

void __init orion5x_setup_dev0_win(u32 base, u32 size)
{
	orion_setup_cpu_win(&addr_map_cfg, win_alloc_count++, base, size,
			    TARGET_DEV_BUS, ATTR_DEV_CS0, -1);
}

void __init orion5x_setup_dev1_win(u32 base, u32 size)
{
	orion_setup_cpu_win(&addr_map_cfg, win_alloc_count++, base, size,
			    TARGET_DEV_BUS, ATTR_DEV_CS1, -1);
}

void __init orion5x_setup_dev2_win(u32 base, u32 size)
{
	orion_setup_cpu_win(&addr_map_cfg, win_alloc_count++, base, size,
			    TARGET_DEV_BUS, ATTR_DEV_CS2, -1);
}

void __init orion5x_setup_pcie_wa_win(u32 base, u32 size)
{
	orion_setup_cpu_win(&addr_map_cfg, win_alloc_count++, base, size,
			    TARGET_PCIE, ATTR_PCIE_WA, -1);
}

void __init orion5x_setup_sram_win(void)
{
	orion_setup_cpu_win(&addr_map_cfg, win_alloc_count++,
			    ORION5X_SRAM_PHYS_BASE, ORION5X_SRAM_SIZE,
			    TARGET_SRAM, ATTR_SRAM, -1);
}
