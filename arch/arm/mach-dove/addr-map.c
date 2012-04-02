/*
 * arch/arm/mach-dove/addr-map.c
 *
 * Address map functions for Marvell Dove 88AP510 SoC
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>
#include <mach/dove.h>
#include <plat/addr-map.h>
#include "common.h"

/*
 * Generic Address Decode Windows bit settings
 */
#define TARGET_DDR		0x0
#define TARGET_BOOTROM		0x1
#define TARGET_CESA		0x3
#define TARGET_PCIE0		0x4
#define TARGET_PCIE1		0x8
#define TARGET_SCRATCHPAD	0xd

#define ATTR_CESA		0x01
#define ATTR_BOOTROM		0xfd
#define ATTR_DEV_SPI0_ROM	0xfe
#define ATTR_DEV_SPI1_ROM	0xfb
#define ATTR_PCIE_IO		0xe0
#define ATTR_PCIE_MEM		0xe8
#define ATTR_SCRATCHPAD		0x0

static inline void __iomem *ddr_map_sc(int i)
{
	return (void __iomem *)(DOVE_MC_VIRT_BASE + 0x100 + ((i) << 4));
}

/*
 * Description of the windows needed by the platform code
 */
static struct __initdata orion_addr_map_cfg addr_map_cfg = {
	.num_wins = 8,
	.remappable_wins = 4,
	.bridge_virt_base = BRIDGE_VIRT_BASE,
};

static const struct __initdata orion_addr_map_info addr_map_info[] = {
	/*
	 * Windows for PCIe IO+MEM space.
	 */
	{ 0, DOVE_PCIE0_IO_PHYS_BASE, DOVE_PCIE0_IO_SIZE,
	  TARGET_PCIE0, ATTR_PCIE_IO, DOVE_PCIE0_IO_BUS_BASE
	},
	{ 1, DOVE_PCIE1_IO_PHYS_BASE, DOVE_PCIE1_IO_SIZE,
	  TARGET_PCIE1, ATTR_PCIE_IO, DOVE_PCIE1_IO_BUS_BASE
	},
	{ 2, DOVE_PCIE0_MEM_PHYS_BASE, DOVE_PCIE0_MEM_SIZE,
	  TARGET_PCIE0, ATTR_PCIE_MEM, -1
	},
	{ 3, DOVE_PCIE1_MEM_PHYS_BASE, DOVE_PCIE1_MEM_SIZE,
	  TARGET_PCIE1, ATTR_PCIE_MEM, -1
	},
	/*
	 * Window for CESA engine.
	 */
	{ 4, DOVE_CESA_PHYS_BASE, DOVE_CESA_SIZE,
	  TARGET_CESA, ATTR_CESA, -1
	},
	/*
	 * Window to the BootROM for Standby and Sleep Resume
	 */
	{ 5, DOVE_BOOTROM_PHYS_BASE, DOVE_BOOTROM_SIZE,
	  TARGET_BOOTROM, ATTR_BOOTROM, -1
	},
	/*
	 * Window to the PMU Scratch Pad space
	 */
	{ 6, DOVE_SCRATCHPAD_PHYS_BASE, DOVE_SCRATCHPAD_SIZE,
	  TARGET_SCRATCHPAD, ATTR_SCRATCHPAD, -1
	},
	/* End marker */
	{ -1, 0, 0, 0, 0, 0 }
};

void __init dove_setup_cpu_mbus(void)
{
	int i;
	int cs;

	/*
	 * Disable, clear and configure windows.
	 */
	orion_config_wins(&addr_map_cfg, addr_map_info);

	/*
	 * Setup MBUS dram target info.
	 */
	orion_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 2; i++) {
		u32 map = readl(ddr_map_sc(i));

		/*
		 * Chip select enabled?
		 */
		if (map & 1) {
			struct mbus_dram_window *w;

			w = &orion_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0; /* CS address decoding done inside */
					  /* the DDR controller, no need to  */
					  /* provide attributes */
			w->base = map & 0xff800000;
			w->size = 0x100000 << (((map & 0x000f0000) >> 16) - 4);
		}
	}
	orion_mbus_dram_info.num_cs = cs;
}
