/*
 * nVidia Tegra device tree board support
 *
 * Copyright (C) 2010 Secret Lab Technologies, Ltd.
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

#include <linux/clocksource.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/pda_power.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/usb/tegra_usb_phy.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "board.h"
#include "common.h"
#include "iomap.h"

static struct tegra_ehci_platform_data tegra_ehci1_pdata = {
	.operating_mode = TEGRA_USB_OTG,
	.power_down_on_bus_suspend = 1,
	.vbus_gpio = -1,
};

static struct tegra_ulpi_config tegra_ehci2_ulpi_phy_config = {
	.reset_gpio = -1,
	.clk = "cdev2",
};

static struct tegra_ehci_platform_data tegra_ehci2_pdata = {
	.phy_config = &tegra_ehci2_ulpi_phy_config,
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
	.vbus_gpio = -1,
};

static struct tegra_ehci_platform_data tegra_ehci3_pdata = {
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
	.vbus_gpio = -1,
};

static struct of_dev_auxdata tegra20_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0xC5000000, "tegra-ehci.0",
		       &tegra_ehci1_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0xC5004000, "tegra-ehci.1",
		       &tegra_ehci2_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", 0xC5008000, "tegra-ehci.2",
		       &tegra_ehci3_pdata),
	{}
};

static void __init tegra_dt_init(void)
{
	/*
	 * Finished with the static registrations now; fill in the missing
	 * devices
	 */
	of_platform_populate(NULL, of_default_bus_match_table,
				tegra20_auxdata_lookup, NULL);
}

static void __init trimslice_init(void)
{
#ifdef CONFIG_TEGRA_PCI
	int ret;

	ret = tegra_pcie_init(true, true);
	if (ret)
		pr_err("tegra_pci_init() failed: %d\n", ret);
#endif
}

static void __init harmony_init(void)
{
#ifdef CONFIG_TEGRA_PCI
	int ret;

	ret = harmony_pcie_init();
	if (ret)
		pr_err("harmony_pcie_init() failed: %d\n", ret);
#endif
}

static void __init paz00_init(void)
{
	tegra_paz00_wifikill_init();
}

static struct {
	char *machine;
	void (*init)(void);
} board_init_funcs[] = {
	{ "compulab,trimslice", trimslice_init },
	{ "nvidia,harmony", harmony_init },
	{ "compal,paz00", paz00_init },
};

static void __init tegra_dt_init_late(void)
{
	int i;

	tegra_init_late();

	for (i = 0; i < ARRAY_SIZE(board_init_funcs); i++) {
		if (of_machine_is_compatible(board_init_funcs[i].machine)) {
			board_init_funcs[i].init();
			break;
		}
	}
}

static const char *tegra20_dt_board_compat[] = {
	"nvidia,tegra20",
	NULL
};

DT_MACHINE_START(TEGRA_DT, "nVidia Tegra20 (Flattened Device Tree)")
	.map_io		= tegra_map_common_io,
	.smp		= smp_ops(tegra_smp_ops),
	.init_early	= tegra_init_early,
	.init_irq	= tegra_dt_init_irq,
	.init_time	= clocksource_of_init,
	.init_machine	= tegra_dt_init,
	.init_late	= tegra_dt_init_late,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= tegra20_dt_board_compat,
MACHINE_END
