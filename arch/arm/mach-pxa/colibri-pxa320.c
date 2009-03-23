/*
 *  arch/arm/mach-pxa/colibri-pxa320.c
 *
 *  Support for Toradex PXA320/310 based Colibri module
 *
 *  Daniel Mack <daniel@caiaq.de>
 *  Matthias Meier <matthias.j.meier@gmx.net>
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
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa320.h>
#include <mach/colibri.h>
#include <mach/ohci.h>

#include "generic.h"
#include "devices.h"

#if defined(CONFIG_AX88796)
#define COLIBRI_ETH_IRQ_GPIO	mfp_to_gpio(GPIO36_GPIO)

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
		.start = gpio_to_irq(COLIBRI_ETH_IRQ_GPIO),
		.end   = gpio_to_irq(COLIBRI_ETH_IRQ_GPIO),
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

static mfp_cfg_t colibri_pxa320_eth_pin_config[] __initdata = {
	GPIO3_nCS2,			/* AX88796 chip select */
	GPIO36_GPIO | MFP_PULL_HIGH	/* AX88796 IRQ */
};

static void __init colibri_pxa320_init_eth(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_eth_pin_config));
	set_irq_type(gpio_to_irq(COLIBRI_ETH_IRQ_GPIO), IRQ_TYPE_EDGE_FALLING);
	platform_device_register(&asix_device);
}
#else
static inline void __init colibri_pxa320_init_eth(void) {}
#endif /* CONFIG_AX88796 */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static mfp_cfg_t colibri_pxa320_usb_pin_config[] __initdata = {
	GPIO2_2_USBH_PEN,
	GPIO3_2_USBH_PWR,
};

static struct pxaohci_platform_data colibri_pxa320_ohci_info = {
	.port_mode	= PMM_GLOBAL_MODE,
	.flags		= ENABLE_PORT1 | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

void __init colibri_pxa320_init_ohci(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_usb_pin_config));
	pxa_set_ohci_info(&colibri_pxa320_ohci_info);
}
#else
static inline void colibri_pxa320_init_ohci(void) {}
#endif /* CONFIG_USB_OHCI_HCD || CONFIG_USB_OHCI_HCD_MODULE */

static mfp_cfg_t colibri_pxa320_mmc_pin_config[] __initdata = {
	GPIO22_MMC1_CLK,
	GPIO23_MMC1_CMD,
	GPIO18_MMC1_DAT0,
	GPIO19_MMC1_DAT1,
	GPIO20_MMC1_DAT2,
	GPIO21_MMC1_DAT3
};

void __init colibri_pxa320_init(void)
{
	colibri_pxa320_init_eth();
	colibri_pxa320_init_ohci();
	colibri_pxa3xx_init_mmc(ARRAY_AND_SIZE(colibri_pxa320_mmc_pin_config),
				mfp_to_gpio(MFP_PIN_GPIO28));
}

MACHINE_START(COLIBRI320, "Toradex Colibri PXA320")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= COLIBRI_SDRAM_BASE + 0x100,
	.init_machine	= colibri_pxa320_init,
	.map_io		= pxa_map_io,
	.init_irq	= pxa3xx_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

