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
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/i2c-tegra.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/setup.h>

#include <mach/io.h>
#include <mach/w1.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>
#include <mach/gpio.h>
#include <mach/clk.h>

#include <linux/usb/android_composite.h>

#include "board.h"
#include "board-stingray.h"
#include "clock.h"
#include "gpio-names.h"
#include "devices.h"
#include "power.h"

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

#define USB_MANUFACTURER_NAME           "Motorola"
#define USB_PRODUCT_NAME                "MZ600"
#define USB_PRODUCT_ID_BLAN             0x70A3
#define USB_PRODUCT_ID_MTP              0x70A8
#define USB_PRODUCT_ID_MTP_ADB          0x70A9
#define USB_VENDOR_ID                   0x22b8

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

static struct plat_serial8250_port hs_uarta_platform_data[] = {
	{
		.mapbase	= TEGRA_UARTA_BASE,
		.membase	= IO_ADDRESS(TEGRA_UARTA_BASE),
		.irq		= INT_UARTA,
	}, {
		.flags		= 0
	}
};

static struct platform_device hs_uarta = {
	.name = "tegra_uart",
	.id = 0,
	.dev = {
		.platform_data = hs_uarta_platform_data,
		.coherent_dma_mask = 0xffffffff,
	},
};

static struct plat_serial8250_port hs_uartc_platform_data[] = {
	{
		.mapbase	= TEGRA_UARTC_BASE,
		.membase	= IO_ADDRESS(TEGRA_UARTC_BASE),
		.irq		= INT_UARTC,
	}, {
		.flags		= 0
	}
};

static struct platform_device hs_uartc = {
	.name = "tegra_uart",
	.id = 2,
	.dev = {
		.platform_data = hs_uartc_platform_data,
		.coherent_dma_mask = 0xffffffff,
	},
};

static struct plat_serial8250_port hs_uartd_platform_data[] = {
	{
		.mapbase	= TEGRA_UARTD_BASE,
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.irq		= INT_UARTD,
	}, {
		.flags		= 0
	}
};

static struct platform_device hs_uartd = {
	.name = "tegra_uart",
	.id = 3,
	.dev = {
		.platform_data = hs_uartd_platform_data,
		.coherent_dma_mask = 0xffffffff,
	},
};

static struct plat_serial8250_port hs_uarte_platform_data[] = {
	{
		.mapbase	= TEGRA_UARTE_BASE,
		.membase	= IO_ADDRESS(TEGRA_UARTE_BASE),
		.irq		= INT_UARTE,
	}, {
		.flags		= 0
	}
};

static struct platform_device hs_uarte = {
	.name = "tegra_uart",
	.id = 4,
	.dev = {
		.platform_data = hs_uarte_platform_data,
		.coherent_dma_mask = 0xffffffff,
	},
};

/* OTG gadget device */
static u64 tegra_gadget_dmamask = DMA_BIT_MASK(32);

static struct resource tegra_gadget_resources[] = {
	[0] = {
		.start	= TEGRA_USB_BASE,
		.end	= TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_USB,
		.end	= INT_USB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct fsl_usb2_platform_data tegra_gadget_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI,
};

static struct platform_device tegra_gadget = {
	.name = "fsl-tegra-udc",
	.id   = -1,
	.dev  = {
		.dma_mask		= &tegra_gadget_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data = &tegra_gadget_pdata,
	},
	.resource = tegra_gadget_resources,
	.num_resources = ARRAY_SIZE(tegra_gadget_resources),
};

/* OTG transceiver */
static struct resource cpcap_otg_resources[] = {
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

static struct platform_device cpcap_otg = {
	.name = "cpcap-otg",
	.id   = -1,
	.resource = cpcap_otg_resources,
	.num_resources = ARRAY_SIZE(cpcap_otg_resources),
};

/* USB host devices */
static u64 tegra_hcd_dmamask = DMA_BIT_MASK(32);

static struct resource tegra_hcd_resources[][2] = {
	[0] = {
		[0] = {
			.flags = IORESOURCE_MEM,
			.start = TEGRA_USB_BASE,
			.end = TEGRA_USB_BASE + TEGRA_USB_SIZE - 1,
		},
		[1] = {
			.flags = IORESOURCE_IRQ,
			.start = INT_USB,
			.end = INT_USB,
		},
	},
	[1] = {
		[0] = {
			.flags = IORESOURCE_MEM,
			.start = TEGRA_USB2_BASE,
			.end = TEGRA_USB2_BASE + TEGRA_USB2_SIZE - 1,
		},
		[1] = {
			.flags = IORESOURCE_IRQ,
			.start = INT_USB2,
			.end = INT_USB2,
		},
	},
	[2] = {
		[0] = {
			.flags = IORESOURCE_MEM,
			.start = TEGRA_USB3_BASE,
			.end = TEGRA_USB3_BASE + TEGRA_USB3_SIZE - 1,
		},
		[1] = {
			.flags = IORESOURCE_IRQ,
			.start = INT_USB3,
			.end = INT_USB3,
		},
	},
};

static struct platform_device tegra_hcd[] = {
	[0] = {
		.name = "tegra-ehci",
		.id = 0,
		.dev = {
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.dma_mask = &tegra_hcd_dmamask,
		},
		.resource = tegra_hcd_resources[0],
		.num_resources = ARRAY_SIZE(tegra_hcd_resources[0]),
	},
	[1] = {
		.name = "tegra-ehci",
		.id = 1,
		.dev = {
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.dma_mask = &tegra_hcd_dmamask,
		},
		.resource = tegra_hcd_resources[1],
		.num_resources = ARRAY_SIZE(tegra_hcd_resources[1]),
	},
	[2] = {
		.name = "tegra-ehci",
		.id = 2,
		.dev = {
			.coherent_dma_mask = DMA_BIT_MASK(32),
			.dma_mask = &tegra_hcd_dmamask,
		},
		.resource = tegra_hcd_resources[2],
		.num_resources = ARRAY_SIZE(tegra_hcd_resources[2]),
	},
};

static char *usb_functions[] = { "mtp" };
static char *usb_functions_adb[] = { "mtp", "adb" };

static struct android_usb_product usb_products[] = {
	{
		.product_id	= USB_PRODUCT_ID_MTP,
		.num_functions	= ARRAY_SIZE(usb_functions),
		.functions	= usb_functions,
	},
	{
		.product_id	= USB_PRODUCT_ID_MTP_ADB,
		.num_functions	= ARRAY_SIZE(usb_functions_adb),
		.functions	= usb_functions_adb,
	},
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id		= USB_VENDOR_ID,
	.product_id		= USB_PRODUCT_ID_MTP_ADB,
	.manufacturer_name	= USB_MANUFACTURER_NAME,
	.product_name		= USB_PRODUCT_NAME,
	.serial_number		= "0000",
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_adb),
	.functions = usb_functions_adb,
};


static struct platform_device androidusb_device = {
	.name	= "android_usb",
	.id	= -1,
	.dev	= {
		.platform_data	= &andusb_plat,
	},
};

static char *factory_usb_functions[] = {
	"usbnet"
};

static struct android_usb_product factory_usb_products[] = {
	{
		.product_id	= USB_PRODUCT_ID_BLAN,
		.num_functions	= ARRAY_SIZE(factory_usb_functions),
		.functions	= factory_usb_functions,
	},
};

/* android USB platform data for factory test mode*/
static struct android_usb_platform_data andusb_plat_factory = {
	.vendor_id		= USB_VENDOR_ID,
	.product_id		= USB_PRODUCT_ID_BLAN,
	.manufacturer_name	= USB_MANUFACTURER_NAME,
	.product_name		= USB_PRODUCT_NAME,
	.serial_number		= "000000000",
	.num_products = ARRAY_SIZE(factory_usb_products),
	.products = factory_usb_products,
	.num_functions = ARRAY_SIZE(factory_usb_functions),
	.functions = factory_usb_functions,
};

static struct platform_device usbnet_device = {
	.name = "usbnet",
};

/* bq24617 charger */
static struct resource bq24617_resources[] = {
	[0] = {
		.name  = "stat1",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
	},
	[1] = {
		.name  = "stat2",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PD1),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PD1),
	},
	[2] = {
		.name  = "detect",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	},
};

static struct resource bq24617_resources_m1_p0[] = {
	[0] = {
		.name  = "stat1",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV5),
	},
	[1] = {
		.name  = "stat2",
		.flags = IORESOURCE_IRQ,
		.start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
		.end   = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	},
};

static struct platform_device bq24617_device = {
	.name		= "bq24617",
	.id		= -1,
	.resource       = bq24617_resources,
	.num_resources  = ARRAY_SIZE(bq24617_resources),
};

static struct resource tegra_gart_resources[] = {
    {
	.name = "mc",
	.flags = IORESOURCE_MEM,
	.start = TEGRA_MC_BASE,
	.end = TEGRA_MC_BASE + TEGRA_MC_SIZE - 1,
    },
    {
	.name = "gart",
	.flags = IORESOURCE_MEM,
	.start = 0x58000000,
	.end = 0x58000000 - 1 + 32 * 1024 * 1024,
    }
};


static struct platform_device tegra_gart_dev = {
    .name = "tegra_gart",
    .id = -1,
    .num_resources = ARRAY_SIZE(tegra_gart_resources),
    .resource = tegra_gart_resources
};

static struct platform_device bcm4329_rfkill = {
	.name = "bcm4329_rfkill",
	.id = -1,
};

static struct tegra_w1_timings tegra_w1_platform_timings = {
	.tsu = 0x1,
	.trelease = 0xf,
	.trdv = 0xf,
	.tlow0 = 0x3c,
	.tlow1 = 0x1,
	.tslot = 0x77,

	.tpdl = 0x78,
	.tpdh = 0x1e,
	.trstl = 0x1df,
	.trsth = 0x1df,

	.rdsclk = 0x7,
	.psclk = 0x50,
};

static struct tegra_w1_platform_data tegra_w1_pdata = {
	.clk_id = NULL,
	.timings = &tegra_w1_platform_timings,
};

static struct resource ram_console_resources[] = {
	{
		.start  = SZ_1G - SZ_256K,
		.end    = SZ_1G - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device ram_console_device = {
	.name           = "ram_console",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(ram_console_resources),
	.resource       = ram_console_resources,
};

static struct platform_device *stingray_devices[] __initdata = {
	&debug_uart,
	&cpcap_otg,
	&tegra_gadget,
	&tegra_hcd[0],
	&tegra_hcd[2],
	&bq24617_device,
	&bcm4329_rfkill,
	&hs_uarta,
	&hs_uartc,
	&hs_uartd,
	&hs_uarte,
	&tegra_spi_device1,
	&tegra_spi_device2,
	&tegra_spi_device3,
	&tegra_spi_device4,
	&tegra_gart_dev,
	&ram_console_device,
};

extern struct tegra_sdhci_platform_data stingray_wifi_data; /* sdhci2 */

static struct tegra_sdhci_platform_data stingray_sdhci_platform_data3 = {
	.clk_id = NULL,
	.force_hs = 0,
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct tegra_sdhci_platform_data stingray_sdhci_platform_data4 = {
	.clk_id = NULL,
	.force_hs = 0,
	.cd_gpio = TEGRA_GPIO_PH2,
	.wp_gpio = TEGRA_GPIO_PH3,
	.power_gpio = TEGRA_GPIO_PI6,
};

static struct tegra_i2c_platform_data stingray_i2c1_platform_data = {
	.bus_clk_rate = 400000,
};

static __initdata struct tegra_clk_init_table stingray_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartb",	"clk_m",	26000000,	true},
	{ "emc",	"pll_p",	0,		true},
	{ "pll_m",	NULL,		600000000,	true},
	{ "emc",	"pll_m",	600000000,	false},
	{ "host1x",	"pll_m",	150000000,	true},
	{ "2d",		"pll_m",	300000000,	true},
	{ "3d",		"pll_m",	300000000,	true},
	{ "epp",	"pll_m",	100000000,	true},
	{ "vi",		"pll_m",	100000000,	true},
	{ NULL,		NULL,		0,		0},
};

static void stingray_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &stingray_i2c1_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}

static void stingray_sdhci_init(void)
{
	/* TODO: setup GPIOs for cd, wd, and power */
	tegra_sdhci_device2.dev.platform_data = &stingray_wifi_data;
	tegra_sdhci_device3.dev.platform_data = &stingray_sdhci_platform_data3;
	tegra_sdhci_device4.dev.platform_data = &stingray_sdhci_platform_data4;

	platform_device_register(&tegra_sdhci_device2);
	platform_device_register(&tegra_sdhci_device3);
	platform_device_register(&tegra_sdhci_device4);
}
#define ATAG_BDADDR 0x43294329	/* stingray bluetooth address tag */
#define ATAG_BDADDR_SIZE 4
#define BDADDR_STR_SIZE 18

static char bdaddr[BDADDR_STR_SIZE];

module_param_string(bdaddr, bdaddr, sizeof(bdaddr), 0400);
MODULE_PARM_DESC(bdaddr, "bluetooth address");

static int __init parse_tag_bdaddr(const struct tag *tag)
{
	unsigned char *b = (unsigned char *)&tag->u;

	if (tag->hdr.size != ATAG_BDADDR_SIZE)
		return -EINVAL;

	snprintf(bdaddr, BDADDR_STR_SIZE, "%02X:%02X:%02X:%02X:%02X:%02X",
			b[0], b[1], b[2], b[3], b[4], b[5]);

	return 0;
}
__tagtable(ATAG_BDADDR, parse_tag_bdaddr);

static void stingray_w1_init(void)
{
	tegra_w1_device.dev.platform_data = &tegra_w1_pdata;
	platform_device_register(&tegra_w1_device);
}

/* powerup reason */
#define ATAG_POWERUP_REASON		 0xf1000401
#define ATAG_POWERUP_REASON_SIZE 3 /* size + tag id + tag data */

static unsigned int powerup_reason = PU_REASON_PWR_KEY_PRESS;

static int __init parse_tag_powerup_reason(const struct tag *tag)
{
	if (tag->hdr.size != ATAG_POWERUP_REASON_SIZE)
		return -EINVAL;
	memcpy(&powerup_reason, &tag->u, sizeof(powerup_reason));
	printk(KERN_INFO "powerup reason=0x%08x\n", powerup_reason);
	return 0;
}
__tagtable(ATAG_POWERUP_REASON, parse_tag_powerup_reason);

#define SERIAL_NUMBER_LENGTH 16
static char usb_serial_num[SERIAL_NUMBER_LENGTH + 1];
static int __init mot_usb_serial_num_setup(char *options)
{
	strncpy(usb_serial_num, options, SERIAL_NUMBER_LENGTH);
	usb_serial_num[SERIAL_NUMBER_LENGTH] = '\0';
	printk(KERN_INFO "usb_serial_num=%s\n", usb_serial_num);
	return 1;
}
__setup("androidboot.serialno=", mot_usb_serial_num_setup);

static void stingray_gadget_init(void)
{
	struct android_usb_platform_data *platform_data;

	if (powerup_reason & PU_REASON_FACTORY_CABLE)
	{
		platform_data = &andusb_plat_factory;
		platform_device_register(&usbnet_device);
	}
	else
		platform_data = &andusb_plat;

	platform_data->serial_number = usb_serial_num;
	androidusb_device.dev.platform_data = platform_data;
	platform_device_register(&androidusb_device);
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
	mi->bank[1].size = SZ_512M - SZ_256K;
}

static void stingray_power_off(void)
{
	printk(KERN_INFO "stingray_pm_power_off...\n");

	local_irq_disable();

	/* signal WDI gpio to shutdown CPCAP, which will
	   cascade to all of the regulators. */
	gpio_direction_output(TEGRA_GPIO_PV7, 0);

	do {} while (1);

	local_irq_enable();
}

static void __init stingray_power_off_init(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PV7);
	if (!gpio_request(TEGRA_GPIO_PV7, "wdi"))
		pm_power_off = stingray_power_off;
}

static unsigned int stingray_board_revision = STINGRAY_REVISION_UNKNOWN;

unsigned int stingray_revision(void)
{
	return stingray_board_revision;
}

static int __init stingray_revision_parse(char *options)
{
	if (!strcmp(options, "m1"))
		stingray_board_revision = STINGRAY_REVISION_M1;
	else if (!strcmp(options, "p0"))
		stingray_board_revision = STINGRAY_REVISION_P0;
	else if (!strcmp(options, "p1"))
		stingray_board_revision = STINGRAY_REVISION_P1;
	else if (!strcmp(options, "p2"))
		stingray_board_revision = STINGRAY_REVISION_P2;
	else
		stingray_board_revision = system_rev;

	printk(KERN_INFO "hw_rev=0x%x\n", stingray_board_revision);

	return 1;
}
__setup("hw_rev=", stingray_revision_parse);

static struct tegra_suspend_platform_data stingray_suspend = {
	.cpu_timer = 5000,
	.cpu_off_timer = 5000,
	.core_timer = 0x7e7e,
	.core_off_timer = 0xf,
	.separate_req = true,
        .corereq_high = true,
	.sysclkreq_high = true,
	.dram_suspend = false,
	.core_off = false,
	.wake_enb = 0,
	.wake_low = 0,
	.wake_high = 0,
	.wake_any = 0,
};

static void __init tegra_stingray_init(void)
{
	struct clk *clk;

	tegra_common_init();
	tegra_init_suspend(&stingray_suspend);

	/* Stingray has a USB switch that disconnects the usb port from the AP20
	   unless a factory cable is used, the factory jumper is set, or the
	   usb_data_en gpio is set.
	 */
	tegra_gpio_enable(TEGRA_GPIO_PV4);
	gpio_request(TEGRA_GPIO_PV4, "usb_data_en");
	gpio_direction_output(TEGRA_GPIO_PV4, 1);

	/* Enable charging */
	tegra_gpio_enable(TEGRA_GPIO_PV5);
	gpio_request(TEGRA_GPIO_PV5, "chg_stat1");
	gpio_direction_input(TEGRA_GPIO_PV5);
	gpio_export(TEGRA_GPIO_PV5, false);
	if (stingray_revision() <= STINGRAY_REVISION_P0) {
		bq24617_device.resource = bq24617_resources_m1_p0;

		tegra_gpio_enable(TEGRA_GPIO_PV6);
		gpio_request(TEGRA_GPIO_PV6, "chg_stat2");
		gpio_direction_input(TEGRA_GPIO_PV6);
		gpio_export(TEGRA_GPIO_PV6, false);

		tegra_gpio_enable(TEGRA_GPIO_PJ0);
		gpio_request(TEGRA_GPIO_PJ0, "chg_disable");
		gpio_direction_output(TEGRA_GPIO_PJ0, 0);
		gpio_export(TEGRA_GPIO_PJ0, false);
	} else {
		tegra_gpio_enable(TEGRA_GPIO_PV6);
		gpio_request(TEGRA_GPIO_PV6, "chg_detect");
		gpio_direction_input(TEGRA_GPIO_PV6);
		gpio_export(TEGRA_GPIO_PV6, false);

		tegra_gpio_enable(TEGRA_GPIO_PD1);
		gpio_request(TEGRA_GPIO_PD1, "chg_stat2");
		gpio_direction_input(TEGRA_GPIO_PD1);
		gpio_export(TEGRA_GPIO_PD1, false);

		tegra_gpio_enable(TEGRA_GPIO_PI4);
		gpio_request(TEGRA_GPIO_PI4, "chg_disable");
		gpio_direction_output(TEGRA_GPIO_PI4, 0);
		gpio_export(TEGRA_GPIO_PI4, false);
	}

	stingray_pinmux_init();

	tegra_clk_init_from_table(stingray_clk_init_table);

	clk = tegra_get_clock_by_name("uartb");
	debug_uart_platform_data[0].uartclk = clk_get_rate(clk);

	clk = clk_get_sys("3d", NULL);
	tegra_periph_reset_assert(clk);
	writel(0x101, IO_ADDRESS(TEGRA_PMC_BASE) + 0x30);
	clk_enable(clk);
	udelay(10);
	writel(1 << 1, IO_ADDRESS(TEGRA_PMC_BASE) + 0x34);
	tegra_periph_reset_deassert(clk);
	clk_put(clk);

	platform_add_devices(stingray_devices, ARRAY_SIZE(stingray_devices));

	stingray_i2c_init();
	stingray_power_off_init();
	stingray_keypad_init();
	stingray_touch_init();
	stingray_power_init();
	stingray_panel_init();
	stingray_sdhci_init();
	stingray_w1_init();
	stingray_sensors_init();
	stingray_wlan_init();
	stingray_gps_init();
	stingray_gadget_init();

}

MACHINE_START(STINGRAY, "stingray")
	.boot_params	= 0x00000100,
	.phys_io	= IO_APB_PHYS,
	.io_pg_offst	= ((IO_APB_VIRT) >> 18) & 0xfffc,
	.fixup		= tegra_stingray_fixup,
	.init_irq	= tegra_init_irq,
	.init_machine	= tegra_stingray_init,
	.map_io		= tegra_map_common_io,
	.timer		= &tegra_timer,
MACHINE_END
