/*
 *  arch/arm/mach-pxa/colibri-pxa300.c
 *
 *  Support for Toradex PXA300 based Colibri module
 *  Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <net/ax88796.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#include <mach/pxa300.h>
#include <mach/colibri.h>
#include <mach/mmc.h>
#include <mach/ohci.h>

#include "generic.h"
#include "devices.h"

/*
 * GPIO configuration
 */
static mfp_cfg_t colibri_pxa300_pin_config[] __initdata = {
	GPIO1_nCS2,			/* AX88796 chip select */
	GPIO26_GPIO | MFP_PULL_HIGH,	/* AX88796 IRQ */

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
	GPIO7_MMC1_CLK,
	GPIO14_MMC1_CMD,
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
#endif

#if defined (CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
        GPIO0_2_USBH_PEN,
        GPIO1_2_USBH_PWR,
#endif
};

#if defined(CONFIG_AX88796)
/*
 * Asix AX88796 Ethernet
 */
static struct ax_plat_data colibri_asix_platdata = {
	.flags		= AXFLG_MAC_FROMDEV,
	.wordlength	= 2
};

static struct resource colibri_asix_resource[] = {
	[0] = {
		.start = PXA3xx_CS2_PHYS,
		.end   = PXA3xx_CS2_PHYS + (0x20 * 2) - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = COLIBRI_PXA300_ETH_IRQ,
		.end   = COLIBRI_PXA300_ETH_IRQ,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device asix_device = {
	.name		= "ax88796",
	.id		= 0,
	.num_resources 	= ARRAY_SIZE(colibri_asix_resource),
	.resource	= colibri_asix_resource,
	.dev		= {
		.platform_data = &colibri_asix_platdata
	}
};
#endif /* CONFIG_AX88796 */

static struct platform_device *colibri_pxa300_devices[] __initdata = {
#if defined(CONFIG_AX88796)
	&asix_device
#endif
};

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
#define MMC_DETECT_PIN   mfp_to_gpio(MFP_PIN_GPIO13)

static int colibri_pxa300_mci_init(struct device *dev,
				   irq_handler_t colibri_mmc_detect_int,
				   void *data)
{
	int ret;

	ret = gpio_request(MMC_DETECT_PIN, "mmc card detect");
	if (ret)
		return ret;

	gpio_direction_input(MMC_DETECT_PIN);
	ret = request_irq(gpio_to_irq(MMC_DETECT_PIN), colibri_mmc_detect_int,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (ret) {
		gpio_free(MMC_DETECT_PIN);
		return ret;
	}

	return 0;
}

static void colibri_pxa300_mci_exit(struct device *dev, void *data)
{
	free_irq(MMC_DETECT_PIN, data);
	gpio_free(gpio_to_irq(MMC_DETECT_PIN));
}

static struct pxamci_platform_data colibri_pxa300_mci_platform_data = {
	.detect_delay	= 20,
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.init		= colibri_pxa300_mci_init,
	.exit		= colibri_pxa300_mci_exit,
};

static void __init colibri_pxa300_init_mmc(void)
{
	pxa_set_mci_info(&colibri_pxa300_mci_platform_data);
}

#else
static inline void colibri_pxa300_init_mmc(void) {}
#endif /* CONFIG_MMC_PXA */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct pxaohci_platform_data colibri_pxa300_ohci_info = {
	.port_mode	= PMM_GLOBAL_MODE,
	.flags		= ENABLE_PORT1 | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

static void __init colibri_pxa300_init_ohci(void)
{
	pxa_set_ohci_info(&colibri_pxa300_ohci_info);
}
#else
static inline void colibri_pxa300_init_ohci(void) {}
#endif /* CONFIG_USB_OHCI_HCD || CONFIG_USB_OHCI_HCD_MODULE */

static void __init colibri_pxa300_init(void)
{
	set_irq_type(COLIBRI_PXA300_ETH_IRQ, IRQ_TYPE_EDGE_FALLING);
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa300_pin_config));
	platform_add_devices(ARRAY_AND_SIZE(colibri_pxa300_devices));
	colibri_pxa300_init_mmc();
	colibri_pxa300_init_ohci();
}

MACHINE_START(COLIBRI300, "Toradex Colibri PXA300")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa300_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

