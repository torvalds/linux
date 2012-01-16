/*
 * arch/arm/mach-tegra/board-harmony.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2011 NVIDIA, Inc.
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
#include <linux/pda_power.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <sound/wm8903.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/setup.h>

#include <mach/tegra_wm8903_pdata.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/sdhci.h>

#include "board.h"
#include "board-harmony.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.mapbase	= TEGRA_UARTD_BASE,
		.irq		= INT_UARTD,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type		= PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static struct tegra_wm8903_platform_data harmony_audio_pdata = {
	.gpio_spkr_en		= TEGRA_GPIO_SPKR_EN,
	.gpio_hp_det		= TEGRA_GPIO_HP_DET,
	.gpio_hp_mute		= -1,
	.gpio_int_mic_en	= TEGRA_GPIO_INT_MIC_EN,
	.gpio_ext_mic_en	= TEGRA_GPIO_EXT_MIC_EN,
};

static struct platform_device harmony_audio_device = {
	.name	= "tegra-snd-wm8903",
	.id	= 0,
	.dev	= {
		.platform_data  = &harmony_audio_pdata,
	},
};

static struct wm8903_platform_data harmony_wm8903_pdata = {
	.irq_active_low = 0,
	.micdet_cfg = 0,
	.micdet_delay = 100,
	.gpio_base = HARMONY_GPIO_WM8903(0),
	.gpio_cfg = {
		0,
		0,
		WM8903_GPIO_CONFIG_ZERO,
		0,
		0,
	},
};

static struct i2c_board_info __initdata wm8903_board_info = {
	I2C_BOARD_INFO("wm8903", 0x1a),
	.platform_data = &harmony_wm8903_pdata,
	.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_CDC_IRQ),
};

static void __init harmony_i2c_init(void)
{
	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);

	i2c_register_board_info(0, &wm8903_board_info, 1);
}

static struct platform_device *harmony_devices[] __initdata = {
	&debug_uart,
	&tegra_sdhci_device1,
	&tegra_sdhci_device2,
	&tegra_sdhci_device4,
	&tegra_ehci3_device,
	&tegra_i2s_device1,
	&tegra_das_device,
	&tegra_pcm_device,
	&harmony_audio_device,
};

static void __init tegra_harmony_fixup(struct tag *tags, char **cmdline,
	struct meminfo *mi)
{
	mi->nr_banks = 2;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = 448 * SZ_1M;
	mi->bank[1].start = SZ_512M;
	mi->bank[1].size = SZ_512M;
}

static __initdata struct tegra_clk_init_table harmony_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true },
	{ "pll_a",	"pll_p_out1",	56448000,	true },
	{ "pll_a_out0",	"pll_a",	11289600,	true },
	{ "cdev1",	NULL,		0,		true },
	{ "i2s1",	"pll_a_out0",	11289600,	false},
	{ "usb3",	"clk_m",	12000000,	true },
	{ NULL,		NULL,		0,		0},
};


static struct tegra_sdhci_platform_data sdhci_pdata1 = {
	.cd_gpio	= -1,
	.wp_gpio	= -1,
	.power_gpio	= -1,
};

static struct tegra_sdhci_platform_data sdhci_pdata2 = {
	.cd_gpio	= TEGRA_GPIO_SD2_CD,
	.wp_gpio	= TEGRA_GPIO_SD2_WP,
	.power_gpio	= TEGRA_GPIO_SD2_POWER,
};

static struct tegra_sdhci_platform_data sdhci_pdata4 = {
	.cd_gpio	= TEGRA_GPIO_SD4_CD,
	.wp_gpio	= TEGRA_GPIO_SD4_WP,
	.power_gpio	= TEGRA_GPIO_SD4_POWER,
	.is_8bit	= 1,
};

static void __init tegra_harmony_init(void)
{
	tegra_clk_init_from_table(harmony_clk_init_table);

	harmony_pinmux_init();

	tegra_sdhci_device1.dev.platform_data = &sdhci_pdata1;
	tegra_sdhci_device2.dev.platform_data = &sdhci_pdata2;
	tegra_sdhci_device4.dev.platform_data = &sdhci_pdata4;

	platform_add_devices(harmony_devices, ARRAY_SIZE(harmony_devices));
	harmony_i2c_init();
	harmony_regulator_init();
}

MACHINE_START(HARMONY, "harmony")
	.atag_offset	= 0x100,
	.fixup		= tegra_harmony_fixup,
	.map_io         = tegra_map_common_io,
	.init_early	= tegra20_init_early,
	.init_irq       = tegra_init_irq,
	.handle_irq	= gic_handle_irq,
	.timer          = &tegra_timer,
	.init_machine   = tegra_harmony_init,
	.restart	= tegra_assert_system_reset,
MACHINE_END
