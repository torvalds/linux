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
#include <linux/interrupt.h>

#include <asm/mach-types.h>
#include <asm/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa320.h>
#include <mach/colibri.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/audio.h>

#include "generic.h"
#include "devices.h"

#if defined(CONFIG_AX88796)
#define COLIBRI_ETH_IRQ_GPIO	mfp_to_gpio(GPIO36_GPIO)

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

static mfp_cfg_t colibri_pxa320_eth_pin_config[] __initdata = {
	GPIO3_nCS2,			/* AX88796 chip select */
	GPIO36_GPIO | MFP_PULL_HIGH	/* AX88796 IRQ */
};

static void __init colibri_pxa320_init_eth(void)
{
	colibri_pxa3xx_init_eth(&colibri_asix_platdata);
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_eth_pin_config));
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

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static mfp_cfg_t colibri_pxa320_lcd_pin_config[] __initdata = {
	GPIO6_2_LCD_LDD_0,
	GPIO7_2_LCD_LDD_1,
	GPIO8_2_LCD_LDD_2,
	GPIO9_2_LCD_LDD_3,
	GPIO10_2_LCD_LDD_4,
	GPIO11_2_LCD_LDD_5,
	GPIO12_2_LCD_LDD_6,
	GPIO13_2_LCD_LDD_7,
	GPIO63_LCD_LDD_8,
	GPIO64_LCD_LDD_9,
	GPIO65_LCD_LDD_10,
	GPIO66_LCD_LDD_11,
	GPIO67_LCD_LDD_12,
	GPIO68_LCD_LDD_13,
	GPIO69_LCD_LDD_14,
	GPIO70_LCD_LDD_15,
	GPIO71_LCD_LDD_16,
	GPIO72_LCD_LDD_17,
	GPIO73_LCD_CS_N,
	GPIO74_LCD_VSYNC,
	GPIO14_2_LCD_FCLK,
	GPIO15_2_LCD_LCLK,
	GPIO16_2_LCD_PCLK,
	GPIO17_2_LCD_BIAS,
};

static void __init colibri_pxa320_init_lcd(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_lcd_pin_config));
}
#else
static inline void colibri_pxa320_init_lcd(void) {}
#endif

#if	defined(CONFIG_SND_AC97_CODEC) || \
	defined(CONFIG_SND_AC97_CODEC_MODULE)
static mfp_cfg_t colibri_pxa320_ac97_pin_config[] __initdata = {
	GPIO34_AC97_SYSCLK,
	GPIO35_AC97_SDATA_IN_0,
	GPIO37_AC97_SDATA_OUT,
	GPIO38_AC97_SYNC,
	GPIO39_AC97_BITCLK,
	GPIO40_AC97_nACRESET
};

static inline void __init colibri_pxa320_init_ac97(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_ac97_pin_config));
	pxa_set_ac97_info(NULL);
}
#else
static inline void colibri_pxa320_init_ac97(void) {}
#endif

/*
 * The following configuration is verified to work with the Toradex Orchid
 * carrier board
 */
static mfp_cfg_t colibri_pxa320_uart_pin_config[] __initdata = {
	/* UART 1 configuration (may be set by bootloader) */
	GPIO99_UART1_CTS,
	GPIO104_UART1_RTS,
	GPIO97_UART1_RXD,
	GPIO98_UART1_TXD,
	GPIO101_UART1_DTR,
	GPIO103_UART1_DSR,
	GPIO100_UART1_DCD,
	GPIO102_UART1_RI,

	/* UART 2 configuration */
	GPIO109_UART2_CTS,
	GPIO112_UART2_RTS,
	GPIO110_UART2_RXD,
	GPIO111_UART2_TXD,

	/* UART 3 configuration */
	GPIO30_UART3_RXD,
	GPIO31_UART3_TXD,
};

static void __init colibri_pxa320_init_uart(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_uart_pin_config));
}

void __init colibri_pxa320_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	colibri_pxa320_init_eth();
	colibri_pxa320_init_ohci();
	colibri_pxa3xx_init_nand();
	colibri_pxa320_init_lcd();
	colibri_pxa3xx_init_lcd(mfp_to_gpio(GPIO49_GPIO));
	colibri_pxa320_init_ac97();
	colibri_pxa3xx_init_mmc(ARRAY_AND_SIZE(colibri_pxa320_mmc_pin_config),
				mfp_to_gpio(MFP_PIN_GPIO28));
	colibri_pxa320_init_uart();
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

