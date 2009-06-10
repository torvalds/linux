/*
 *  arch/arm/mach-pxa/colibri-pxa300.c
 *
 *  Support for Toradex PXA300/310 based Colibri module
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
#include <linux/interrupt.h>

#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#include <mach/pxa300.h>
#include <mach/colibri.h>
#include <mach/ohci.h>
#include <mach/pxafb.h>

#include "generic.h"
#include "devices.h"

#if defined(CONFIG_AX88796)
#define COLIBRI_ETH_IRQ_GPIO	mfp_to_gpio(GPIO26_GPIO)

/*
 * Asix AX88796 Ethernet
 */
static struct ax_plat_data colibri_asix_platdata = {
	.flags		= 0, /* defined later */
	.wordlength	= 2,
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
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_FALLING,
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

static mfp_cfg_t colibri_pxa300_eth_pin_config[] __initdata = {
	GPIO1_nCS2,			/* AX88796 chip select */
	GPIO26_GPIO | MFP_PULL_HIGH	/* AX88796 IRQ */
};

static void __init colibri_pxa300_init_eth(void)
{
	colibri_pxa3xx_init_eth(&colibri_asix_platdata);
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa300_eth_pin_config));
	platform_device_register(&asix_device);
}
#else
static inline void __init colibri_pxa300_init_eth(void) {}
#endif /* CONFIG_AX88796 */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static mfp_cfg_t colibri_pxa300_usb_pin_config[] __initdata = {
	GPIO0_2_USBH_PEN,
	GPIO1_2_USBH_PWR,
};

static struct pxaohci_platform_data colibri_pxa300_ohci_info = {
	.port_mode	= PMM_GLOBAL_MODE,
	.flags		= ENABLE_PORT1 | POWER_CONTROL_LOW | POWER_SENSE_LOW,
};

void __init colibri_pxa300_init_ohci(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa300_usb_pin_config));
	pxa_set_ohci_info(&colibri_pxa300_ohci_info);
}
#else
static inline void colibri_pxa300_init_ohci(void) {}
#endif /* CONFIG_USB_OHCI_HCD || CONFIG_USB_OHCI_HCD_MODULE */

static mfp_cfg_t colibri_pxa300_mmc_pin_config[] __initdata = {
	GPIO7_MMC1_CLK,
	GPIO14_MMC1_CMD,
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
};

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static mfp_cfg_t colibri_pxa300_lcd_pin_config[] __initdata = {
	GPIO54_LCD_LDD_0,
	GPIO55_LCD_LDD_1,
	GPIO56_LCD_LDD_2,
	GPIO57_LCD_LDD_3,
	GPIO58_LCD_LDD_4,
	GPIO59_LCD_LDD_5,
	GPIO60_LCD_LDD_6,
	GPIO61_LCD_LDD_7,
	GPIO62_LCD_LDD_8,
	GPIO63_LCD_LDD_9,
	GPIO64_LCD_LDD_10,
	GPIO65_LCD_LDD_11,
	GPIO66_LCD_LDD_12,
	GPIO67_LCD_LDD_13,
	GPIO68_LCD_LDD_14,
	GPIO69_LCD_LDD_15,
	GPIO70_LCD_LDD_16,
	GPIO71_LCD_LDD_17,
	GPIO62_LCD_CS_N,
	GPIO72_LCD_FCLK,
	GPIO73_LCD_LCLK,
	GPIO74_LCD_PCLK,
	GPIO75_LCD_BIAS,
	GPIO76_LCD_VSYNC,
};

static void __init colibri_pxa300_init_lcd(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa300_lcd_pin_config));
}

#else
static inline void colibri_pxa300_init_lcd(void) {}
#endif /* CONFIG_FB_PXA || CONFIG_FB_PXA_MODULE */

#if defined(SND_AC97_CODEC) || defined(SND_AC97_CODEC_MODULE)
static mfp_cfg_t colibri_pxa310_ac97_pin_config[] __initdata = {
	GPIO24_AC97_SYSCLK,
	GPIO23_AC97_nACRESET,
	GPIO25_AC97_SDATA_IN_0,
	GPIO27_AC97_SDATA_OUT,
	GPIO28_AC97_SYNC,
	GPIO29_AC97_BITCLK
};

static inline void __init colibri_pxa310_init_ac97(void)
{
	/* no AC97 codec on Colibri PXA300 */
	if (!cpu_is_pxa310())
		return;

	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa310_ac97_pin_config));
	pxa_set_ac97_info(NULL);
}
#else
static inline void colibri_pxa310_init_ac97(void) {}
#endif

void __init colibri_pxa300_init(void)
{
	colibri_pxa300_init_eth();
	colibri_pxa300_init_ohci();
	colibri_pxa300_init_lcd();
	colibri_pxa3xx_init_lcd(mfp_to_gpio(GPIO39_GPIO));
	colibri_pxa310_init_ac97();
	colibri_pxa3xx_init_mmc(ARRAY_AND_SIZE(colibri_pxa300_mmc_pin_config),
				mfp_to_gpio(MFP_PIN_GPIO13));
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

