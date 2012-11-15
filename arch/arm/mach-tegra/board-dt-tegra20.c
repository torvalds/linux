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
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/pda_power.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/i2c-tegra.h>
#include <linux/usb/tegra_usb_phy.h>

#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include "board.h"
#include "clock.h"
#include "common.h"
#include "iomap.h"

struct tegra_ehci_platform_data tegra_ehci1_pdata = {
	.operating_mode = TEGRA_USB_OTG,
	.power_down_on_bus_suspend = 1,
	.vbus_gpio = -1,
};

struct tegra_ulpi_config tegra_ehci2_ulpi_phy_config = {
	.reset_gpio = -1,
	.clk = "cdev2",
};

struct tegra_ehci_platform_data tegra_ehci2_pdata = {
	.phy_config = &tegra_ehci2_ulpi_phy_config,
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
	.vbus_gpio = -1,
};

struct tegra_ehci_platform_data tegra_ehci3_pdata = {
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 1,
	.vbus_gpio = -1,
};

struct of_dev_auxdata tegra20_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC1_BASE, "sdhci-tegra.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC2_BASE, "sdhci-tegra.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC3_BASE, "sdhci-tegra.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sdhci", TEGRA_SDMMC4_BASE, "sdhci-tegra.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C_BASE, "tegra-i2c.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C2_BASE, "tegra-i2c.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c", TEGRA_I2C3_BASE, "tegra-i2c.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2c-dvc", TEGRA_DVC_BASE, "tegra-i2c.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2s", TEGRA_I2S1_BASE, "tegra20-i2s.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-i2s", TEGRA_I2S2_BASE, "tegra20-i2s.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-das", TEGRA_APB_MISC_DAS_BASE, "tegra20-das", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", TEGRA_USB_BASE, "tegra-ehci.0",
		       &tegra_ehci1_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", TEGRA_USB2_BASE, "tegra-ehci.1",
		       &tegra_ehci2_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-ehci", TEGRA_USB3_BASE, "tegra-ehci.2",
		       &tegra_ehci3_pdata),
	OF_DEV_AUXDATA("nvidia,tegra20-apbdma", TEGRA_APB_DMA_BASE, "tegra-apbdma", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-pwm", TEGRA_PWFM_BASE, "tegra-pwm", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-sflash", 0x7000c380, "spi", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-slink", 0x7000D400, "spi_tegra.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-slink", 0x7000D600, "spi_tegra.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-slink", 0x7000D800, "spi_tegra.2", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-slink", 0x7000DA00, "spi_tegra.3", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-host1x", 0x50000000, "host1x", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-dc", 0x54200000, "tegradc.0", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-dc", 0x54240000, "tegradc.1", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-hdmi", 0x54280000, "hdmi", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-dsi", 0x54300000, "dsi", NULL),
	OF_DEV_AUXDATA("nvidia,tegra20-tvo", 0x542c0000, "tvo", NULL),
	{}
};

static __initdata struct tegra_clk_init_table tegra_dt_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uarta",	"pll_p",	216000000,	true },
	{ "uartd",	"pll_p",	216000000,	true },
	{ "usbd",	"clk_m",	12000000,	false },
	{ "usb2",	"clk_m",	12000000,	false },
	{ "usb3",	"clk_m",	12000000,	false },
	{ "pll_a",      "pll_p_out1",   56448000,       true },
	{ "pll_a_out0", "pll_a",        11289600,       true },
	{ "cdev1",      NULL,           0,              true },
	{ "blink",      "clk_32k",      32768,          true },
	{ "i2s1",       "pll_a_out0",   11289600,       false},
	{ "i2s2",       "pll_a_out0",   11289600,       false},
	{ "sdmmc1",	"pll_p",	48000000,	false},
	{ "sdmmc3",	"pll_p",	48000000,	false},
	{ "sdmmc4",	"pll_p",	48000000,	false},
	{ "spi",	"pll_p",	20000000,	false },
	{ "sbc1",	"pll_p",	100000000,	false },
	{ "sbc2",	"pll_p",	100000000,	false },
	{ "sbc3",	"pll_p",	100000000,	false },
	{ "sbc4",	"pll_p",	100000000,	false },
	{ "host1x",	"pll_c",	150000000,	false },
	{ "disp1",	"pll_p",	600000000,	false },
	{ "disp2",	"pll_p",	600000000,	false },
	{ NULL,		NULL,		0,		0},
};

static void __init tegra_dt_init(void)
{
	tegra_clk_init_from_table(tegra_dt_clk_init_table);

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
	.init_early	= tegra20_init_early,
	.init_irq	= tegra_dt_init_irq,
	.handle_irq	= gic_handle_irq,
	.timer		= &tegra_sys_timer,
	.init_machine	= tegra_dt_init,
	.init_late	= tegra_dt_init_late,
	.restart	= tegra_assert_system_reset,
	.dt_compat	= tegra20_dt_board_compat,
MACHINE_END
