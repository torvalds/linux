// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mach-pxa/gumstix.c
 *
 *  Support for the Gumstix motherboards.
 *
 *  Original Author:	Craig Hughes
 *  Created:	Feb 14, 2008
 *  Copyright:	Craig Hughes
 *
 *  Implemented based on lubbock.c by Nicolas Pitre and code from Craig
 *  Hughes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/gpio/machine.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <linux/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include "pxa25x.h"
#include <linux/platform_data/mmc-pxamci.h>
#include "udc.h"
#include "gumstix.h"

#include "generic.h"

static struct resource flash_resource = {
	.start	= 0x00000000,
	.end	= SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct mtd_partition gumstix_partitions[] = {
	{
		.name =		"Bootloader",
		.size =		0x00040000,
		.offset =	0,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	} , {
		.name =		"rootfs",
		.size =		MTDPART_SIZ_FULL,
		.offset =	MTDPART_OFS_APPEND
	}
};

static struct flash_platform_data gumstix_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= gumstix_partitions,
	.nr_parts	= ARRAY_SIZE(gumstix_partitions),
	.width		= 2,
};

static struct platform_device gumstix_flash_device = {
	.name		= "pxa2xx-flash",
	.id		= 0,
	.dev = {
		.platform_data = &gumstix_flash_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

static struct platform_device *devices[] __initdata = {
	&gumstix_flash_device,
};

#ifdef CONFIG_MMC_PXA
static struct pxamci_platform_data gumstix_mci_platform_data = {
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
};

static void __init gumstix_mmc_init(void)
{
	pxa_set_mci_info(&gumstix_mci_platform_data);
}
#else
static void __init gumstix_mmc_init(void)
{
	pr_debug("Gumstix mmc disabled\n");
}
#endif

#ifdef CONFIG_USB_PXA25X
static struct gpiod_lookup_table gumstix_gpio_vbus_gpiod_table = {
	.dev_id = "gpio-vbus",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_GUMSTIX_USB_GPIOn,
			    "vbus", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-pxa", GPIO_GUMSTIX_USB_GPIOx,
			    "pullup", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct platform_device gumstix_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
};

static void __init gumstix_udc_init(void)
{
	gpiod_add_lookup_table(&gumstix_gpio_vbus_gpiod_table);
	platform_device_register(&gumstix_gpio_vbus);
}
#else
static void gumstix_udc_init(void)
{
	pr_debug("Gumstix udc is disabled\n");
}
#endif

#ifdef CONFIG_BT
/* Normally, the bootloader would have enabled this 32kHz clock but many
** boards still have u-boot 1.1.4 so we check if it has been turned on and
** if not, we turn it on with a warning message. */
static void gumstix_setup_bt_clock(void)
{
	int timeout = 500;

	if (!(readl(OSCC) & OSCC_OOK))
		pr_warn("32kHz clock was not on. Bootloader may need to be updated\n");
	else
		return;

	writel(readl(OSCC) | OSCC_OON, OSCC);
	do {
		if (readl(OSCC) & OSCC_OOK)
			break;
		udelay(1);
	} while (--timeout);
	if (!timeout)
		pr_err("Failed to start 32kHz clock\n");
}

static void __init gumstix_bluetooth_init(void)
{
	int err;

	gumstix_setup_bt_clock();

	err = gpio_request(GPIO_GUMSTIX_BTRESET, "BTRST");
	if (err) {
		pr_err("gumstix: failed request gpio for bluetooth reset\n");
		return;
	}

	err = gpio_direction_output(GPIO_GUMSTIX_BTRESET, 1);
	if (err) {
		pr_err("gumstix: can't reset bluetooth\n");
		return;
	}
	gpio_set_value(GPIO_GUMSTIX_BTRESET, 0);
	udelay(100);
	gpio_set_value(GPIO_GUMSTIX_BTRESET, 1);
}
#else
static void gumstix_bluetooth_init(void)
{
	pr_debug("Gumstix Bluetooth is disabled\n");
}
#endif

static unsigned long gumstix_pin_config[] __initdata = {
	GPIO12_32KHz,
	/* BTUART */
	GPIO42_HWUART_RXD,
	GPIO43_HWUART_TXD,
	GPIO44_HWUART_CTS,
	GPIO45_HWUART_RTS,
	/* MMC */
	GPIO6_MMC_CLK,
	GPIO53_MMC_CLK,
	GPIO8_MMC_CS0,
};

int __attribute__((weak)) am200_init(void)
{
	return 0;
}

int __attribute__((weak)) am300_init(void)
{
	return 0;
}

static void __init carrier_board_init(void)
{
	/*
	 * put carrier/expansion board init here if
	 * they cannot be detected programatically
	 */
	am200_init();
	am300_init();
}

static void __init gumstix_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(gumstix_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	pxa_set_hwuart_info(NULL);

	gumstix_bluetooth_init();
	gumstix_udc_init();
	gumstix_mmc_init();
	(void) platform_add_devices(devices, ARRAY_SIZE(devices));
	carrier_board_init();
}

MACHINE_START(GUMSTIX, "Gumstix")
	.atag_offset	= 0x100, /* match u-boot bi_boot_params */
	.map_io		= pxa25x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= gumstix_init,
	.restart	= pxa_restart,
MACHINE_END
