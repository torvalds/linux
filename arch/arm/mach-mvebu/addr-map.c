/*
 * Address map functions for Marvell 370 / XP SoCs
 *
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <plat/addr-map.h>

/*
 * Generic Address Decode Windows bit settings
 */
#define ARMADA_XP_TARGET_DEV_BUS	1
#define   ARMADA_XP_ATTR_DEV_BOOTROM    0x1D
#define ARMADA_XP_TARGET_ETH1		3
#define ARMADA_XP_TARGET_PCIE_0_2	4
#define ARMADA_XP_TARGET_ETH0		7
#define ARMADA_XP_TARGET_PCIE_1_3	8

#define ARMADA_370_TARGET_DEV_BUS       1
#define   ARMADA_370_ATTR_DEV_BOOTROM   0x1D
#define ARMADA_370_TARGET_PCIE_0        4
#define ARMADA_370_TARGET_PCIE_1        8

#define ARMADA_WINDOW_8_PLUS_OFFSET       0x90
#define ARMADA_SDRAM_ADDR_DECODING_OFFSET 0x180

static const struct __initdata orion_addr_map_info
armada_xp_addr_map_info[] = {
	/*
	 * Window for the BootROM, needed for SMP on Armada XP
	 */
	{ 0, 0xfff00000, SZ_1M, ARMADA_XP_TARGET_DEV_BUS,
	  ARMADA_XP_ATTR_DEV_BOOTROM, -1 },
	/* End marker */
	{ -1, 0, 0, 0, 0, 0 },
};

static const struct __initdata orion_addr_map_info
armada_370_addr_map_info[] = {
	/* End marker */
	{ -1, 0, 0, 0, 0, 0 },
};

static struct of_device_id of_addr_decoding_controller_table[] = {
	{ .compatible = "marvell,armada-addr-decoding-controller" },
	{ /* end of list */ },
};

static void __iomem *
armada_cfg_base(const struct orion_addr_map_cfg *cfg, int win)
{
	unsigned int offset;

	/* The register layout is a bit annoying and the below code
	 * tries to cope with it.
	 * - At offset 0x0, there are the registers for the first 8
	 *   windows, with 4 registers of 32 bits per window (ctrl,
	 *   base, remap low, remap high)
	 * - Then at offset 0x80, there is a hole of 0x10 bytes for
	 *   the internal registers base address and internal units
	 *   sync barrier register.
	 * - Then at offset 0x90, there the registers for 12
	 *   windows, with only 2 registers of 32 bits per window
	 *   (ctrl, base).
	 */
	if (win < 8)
		offset = (win << 4);
	else
		offset = ARMADA_WINDOW_8_PLUS_OFFSET + ((win - 8) << 3);

	return cfg->bridge_virt_base + offset;
}

static struct __initdata orion_addr_map_cfg addr_map_cfg = {
	.num_wins = 20,
	.remappable_wins = 8,
	.win_cfg_base = armada_cfg_base,
};

static int __init armada_setup_cpu_mbus(void)
{
	struct device_node *np;
	void __iomem *mbus_unit_addr_decoding_base;
	void __iomem *sdram_addr_decoding_base;

	np = of_find_matching_node(NULL, of_addr_decoding_controller_table);
	if (!np)
		return -ENODEV;

	mbus_unit_addr_decoding_base = of_iomap(np, 0);
	BUG_ON(!mbus_unit_addr_decoding_base);

	sdram_addr_decoding_base =
		mbus_unit_addr_decoding_base +
		ARMADA_SDRAM_ADDR_DECODING_OFFSET;

	addr_map_cfg.bridge_virt_base = mbus_unit_addr_decoding_base;

	if (of_find_compatible_node(NULL, NULL, "marvell,coherency-fabric"))
		addr_map_cfg.hw_io_coherency = 1;

	/*
	 * Disable, clear and configure windows.
	 */
	if (of_machine_is_compatible("marvell,armadaxp"))
		orion_config_wins(&addr_map_cfg, armada_xp_addr_map_info);
	else if (of_machine_is_compatible("marvell,armada370"))
		orion_config_wins(&addr_map_cfg, armada_370_addr_map_info);
	else {
		pr_err("Unsupported SoC\n");
		return -EINVAL;
	}

	/*
	 * Setup MBUS dram target info.
	 */
	orion_setup_cpu_mbus_target(&addr_map_cfg,
				    sdram_addr_decoding_base);
	return 0;
}

/* Using a early_initcall is needed so that this initialization gets
 * done before the SMP initialization, which requires the BootROM to
 * be remapped. */
early_initcall(armada_setup_cpu_mbus);
