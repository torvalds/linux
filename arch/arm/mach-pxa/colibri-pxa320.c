// SPDX-License-Identifier: GPL-2.0-only
/*
 *  arch/arm/mach-pxa/colibri-pxa320.c
 *
 *  Support for Toradex PXA320/310 based Colibri module
 *
 *  Daniel Mack <daniel@caiaq.de>
 *  Matthias Meier <matthias.j.meier@gmx.net>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio/machine.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include <asm/mach-types.h>
#include <linux/sizes.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#include "pxa320.h"
#include "colibri.h"
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <linux/platform_data/asoc-pxa.h>
#include "pxa27x-udc.h"
#include "udc.h"

#include "generic.h"
#include "devices.h"

#ifdef	CONFIG_MACH_COLIBRI_EVALBOARD
static mfp_cfg_t colibri_pxa320_evalboard_pin_config[] __initdata = {
	/* MMC */
	GPIO22_MMC1_CLK,
	GPIO23_MMC1_CMD,
	GPIO18_MMC1_DAT0,
	GPIO19_MMC1_DAT1,
	GPIO20_MMC1_DAT2,
	GPIO21_MMC1_DAT3,
	GPIO28_GPIO,	/* SD detect */

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

	/* UHC */
	GPIO2_2_USBH_PEN,
	GPIO3_2_USBH_PWR,

	/* I2C */
	GPIO32_I2C_SCL,
	GPIO33_I2C_SDA,

	/* PCMCIA */
	MFP_CFG(GPIO59, AF7),	/* PRST ; AF7 to tristate */
	MFP_CFG(GPIO61, AF7),	/* PCE1 ; AF7 to tristate */
	MFP_CFG(GPIO60, AF7),	/* PCE2 ; AF7 to tristate */
	MFP_CFG(GPIO62, AF7),	/* PCD ; AF7 to tristate */
	MFP_CFG(GPIO56, AF7),	/* PSKTSEL ; AF7 to tristate */
	GPIO27_GPIO,		/* RDnWR ; input/tristate */
	GPIO50_GPIO,		/* PREG ; input/tristate */
	GPIO2_RDY,
	GPIO5_NPIOR,
	GPIO6_NPIOW,
	GPIO7_NPIOS16,
	GPIO8_NPWAIT,
	GPIO29_GPIO,		/* PRDY (READY GPIO) */
	GPIO57_GPIO,		/* PPEN (POWER GPIO) */
	GPIO81_GPIO,		/* PCD (DETECT GPIO) */
	GPIO77_GPIO,		/* PRST (RESET GPIO) */
	GPIO53_GPIO,		/* PBVD1 */
	GPIO79_GPIO,		/* PBVD2 */
	GPIO54_GPIO,		/* POE */
};
#else
static mfp_cfg_t colibri_pxa320_evalboard_pin_config[] __initdata = {};
#endif

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
		.start = PXA_GPIO_TO_IRQ(COLIBRI_ETH_IRQ_GPIO),
		.end   = PXA_GPIO_TO_IRQ(COLIBRI_ETH_IRQ_GPIO),
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

#if defined(CONFIG_USB_PXA27X)||defined(CONFIG_USB_PXA27X_MODULE)
static struct gpiod_lookup_table gpio_vbus_gpiod_table = {
	.dev_id = "gpio-vbus",
	.table = {
		GPIO_LOOKUP("gpio-pxa", MFP_PIN_GPIO96,
			    "vbus", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct platform_device colibri_pxa320_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
};

static void colibri_pxa320_udc_command(int cmd)
{
	if (cmd == PXA2XX_UDC_CMD_CONNECT)
		UP2OCR = UP2OCR_HXOE | UP2OCR_DPPUE;
	else if (cmd == PXA2XX_UDC_CMD_DISCONNECT)
		UP2OCR = UP2OCR_HXOE;
}

static struct pxa2xx_udc_mach_info colibri_pxa320_udc_info __initdata = {
	.udc_command		= colibri_pxa320_udc_command,
	.gpio_pullup		= -1,
};

static void __init colibri_pxa320_init_udc(void)
{
	pxa_set_udc_info(&colibri_pxa320_udc_info);
	gpiod_add_lookup_table(&gpio_vbus_gpiod_table);
	platform_device_register(&colibri_pxa320_gpio_vbus);
}
#else
static inline void colibri_pxa320_init_udc(void) {}
#endif

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

void __init colibri_pxa320_init(void)
{
	colibri_pxa320_init_eth();
	colibri_pxa3xx_init_nand();
	colibri_pxa320_init_lcd();
	colibri_pxa3xx_init_lcd(mfp_to_gpio(GPIO49_GPIO));
	colibri_pxa320_init_ac97();
	colibri_pxa320_init_udc();

	/* Evalboard init */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(colibri_pxa320_evalboard_pin_config));
	colibri_evalboard_init();
}

MACHINE_START(COLIBRI320, "Toradex Colibri PXA320")
	.atag_offset	= 0x100,
	.init_machine	= colibri_pxa320_init,
	.map_io		= pxa3xx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa3xx_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END

