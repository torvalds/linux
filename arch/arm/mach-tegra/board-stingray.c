/*
 * arch/arm/mach-tegra/board-stingray.c
 *
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
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/dma-mapping.h>
#include <linux/fsl_devices.h>
#include <linux/pda_power.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/io.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>
#include <mach/gpio.h>

#include <linux/usb/android_composite.h>

#include "board.h"
#include "board-stingray.h"
#include "clock.h"
#include "gpio-names.h"
#include "devices.h"

/* NVidia bootloader tags */
#define ATAG_NVIDIA		0x41000801

#define ATAG_NVIDIA_RM			0x1
#define ATAG_NVIDIA_DISPLAY		0x2
#define ATAG_NVIDIA_FRAMEBUFFER		0x3
#define ATAG_NVIDIA_CHIPSHMOO		0x4
#define ATAG_NVIDIA_CHIPSHMOOPHYS	0x5
#define ATAG_NVIDIA_PRESERVED_MEM_0	0x10000
#define ATAG_NVIDIA_PRESERVED_MEM_N	2
#define ATAG_NVIDIA_FORCE_32		0x7fffffff

struct tag_tegra {
	__u32 bootarg_key;
	__u32 bootarg_len;
	char bootarg[1];
};

static int __init parse_tag_nvidia(const struct tag *tag)
{

	return 0;
}
__tagtable(ATAG_NVIDIA, parse_tag_nvidia);

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTB_BASE),
		.mapbase	= TEGRA_UARTB_BASE,
		.irq		= INT_UARTB,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 0, /* filled in by tegra_stingray_init */
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

static struct plat_serial8250_port hsuart_platform_data[] = {
	{
		.mapbase	= TEGRA_UARTD_BASE,
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.irq		= INT_UARTD,
	}, {
		.flags		= 0
	}
};

static struct platform_device hsuart = {
	.name = "tegra_uart",
	.id = 3,
	.dev = {
		.platform_data = hsuart_platform_data,
		.coherent_dma_mask = 0xffffffff,
	},
};

/* OTG gadget device */
static u64 tegra_otg_dmamask = DMA_BIT_MASK(32);


static struct resource tegra_otg_resources[] = {
	[0] = {
		.start  = TEGRA_USB_BASE,
		.end    = TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = INT_USB,
		.end    = INT_USB,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct fsl_usb2_platform_data tegra_otg_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI,
};

struct platform_device tegra_otg = {
	.name = "fsl-tegra-udc",
	.id   = -1,
	.dev  = {
		.dma_mask		= &tegra_otg_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data = &tegra_otg_pdata,
	},
	.resource = tegra_otg_resources,
	.num_resources = ARRAY_SIZE(tegra_otg_resources),
};

static char *usb_functions[] = { "usb_mass_storage" };
static char *usb_functions_adb[] = { "usb_mass_storage", "adb" };

static struct android_usb_product usb_products[] = {
	{
		.product_id     = 0xDEAD,
		.num_functions  = ARRAY_SIZE(usb_functions),
		.functions      = usb_functions,
	},
	{
		.product_id     = 0xBEEF,
		.num_functions  = ARRAY_SIZE(usb_functions_adb),
		.functions      = usb_functions_adb,
	},
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id                      = 0x18d1,
	.product_id                     = 0x0002,
	.manufacturer_name      = "Google",
	.product_name           = "Stingray!",
	.serial_number          = "0000",
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_adb),
	.functions = usb_functions_adb,
};


static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};

/* PDA power */
static struct pda_power_pdata pda_power_pdata = {
};

static struct platform_device pda_power_device = {
	.name   = "pda_power",
	.id     = -1,
	.dev    = {
		.platform_data  = &pda_power_pdata,
	},
};

static struct platform_device *stingray_devices[] __initdata = {
	&debug_uart,
	&tegra_otg,
	&androidusb_device,
	&pda_power_device,
	&hsuart,
};

extern struct tegra_sdhci_platform_data stingray_wifi_data; /* sdhci1 */

static struct tegra_sdhci_platform_data stingray_sdhci_platform_data3 = {
	.clk_id = NULL,
	.force_hs = 0,
};

static struct tegra_sdhci_platform_data stingray_sdhci_platform_data4 = {
	.clk_id = NULL,
	.force_hs = 0,
	.cd_gpio = TEGRA_GPIO_PH2,
	.wp_gpio = TEGRA_GPIO_PH3,
	.power_gpio = TEGRA_GPIO_PI6,
};

static __initdata struct tegra_clk_init_table stingray_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartb",	"clk_m",	26000000,	true},
	{ NULL,		NULL,		0,		0},
};


static void stingray_sdhci_init(void)
{
	/* TODO: setup GPIOs for cd, wd, and power */
	tegra_sdhci_device1.dev.platform_data = &stingray_wifi_data;
	tegra_sdhci_device3.dev.platform_data = &stingray_sdhci_platform_data3;
	tegra_sdhci_device4.dev.platform_data = &stingray_sdhci_platform_data4;

	platform_device_register(&tegra_sdhci_device1);
	platform_device_register(&tegra_sdhci_device3);
	platform_device_register(&tegra_sdhci_device4);
}


static void __init tegra_stingray_fixup(struct machine_desc *desc, struct tag *tags,
				 char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 2;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].node = PHYS_TO_NID(PHYS_OFFSET);
	mi->bank[0].size = 448 * SZ_1M;
	mi->bank[1].start = SZ_512M;
	mi->bank[1].node = PHYS_TO_NID(SZ_512M);
	mi->bank[1].size = SZ_512M;
}

static void __init tegra_stingray_init(void)
{
	struct clk *clk;

	tegra_common_init();

	/* Stingray has a USB switch that disconnects the usb port from the AP20
	   unless a factory cable is used, the factory jumper is set, or the
	   usb_data_en gpio is set.
	 */
	tegra_gpio_enable(TEGRA_GPIO_PV6);
	gpio_request(TEGRA_GPIO_PV6, "usb_data_en");
	gpio_direction_output(TEGRA_GPIO_PV6, 1);

	stingray_pinmux_init();

	stingray_sdhci_init();

	tegra_clk_init_from_table(stingray_clk_init_table);

	clk = tegra_get_clock_by_name("uartb");
	debug_uart_platform_data[0].uartclk = clk_get_rate(clk);

	platform_add_devices(stingray_devices, ARRAY_SIZE(stingray_devices));
}

MACHINE_START(STINGRAY, "stingray")
	.boot_params  = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.fixup		= tegra_stingray_fixup,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_stingray_init,
	.map_io         = tegra_map_common_io,
	.timer          = &tegra_timer,
MACHINE_END
