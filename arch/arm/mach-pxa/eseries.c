/*
 * Hardware definitions for the Toshiba eseries PDAs
 *
 * Copyright (c) 2003 Ian Molton <spyro@f2s.com>
 *
 * This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 */

#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/gpio/machine.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mfd/tc6387xb.h>
#include <linux/mfd/tc6393xb.h>
#include <linux/mfd/t7l66xb.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/memblock.h>

#include <video/w100fb.h>

#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include "pxa25x.h"
#include <mach/eseries-gpio.h>
#include "eseries-irq.h"
#include <mach/audio.h>
#include <linux/platform_data/video-pxafb.h>
#include "udc.h"
#include <linux/platform_data/irda-pxaficp.h>

#include "devices.h"
#include "generic.h"

/* Only e800 has 128MB RAM */
void __init eseries_fixup(struct tag *tags, char **cmdline)
{
	if (machine_is_e800())
		memblock_add(0xa0000000, SZ_128M);
	else
		memblock_add(0xa0000000, SZ_64M);
}

static struct gpiod_lookup_table e7xx_gpio_vbus_gpiod_table __maybe_unused = {
	.dev_id = "gpio-vbus",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_E7XX_USB_DISC,
			    "vbus", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-pxa", GPIO_E7XX_USB_PULLUP,
			    "pullup", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct platform_device e7xx_gpio_vbus __maybe_unused = {
	.name	= "gpio-vbus",
	.id	= -1,
};

struct pxaficp_platform_data e7xx_ficp_platform_data = {
	.gpio_pwdown		= GPIO_E7XX_IR_OFF,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

int eseries_tmio_enable(struct platform_device *dev)
{
	/* Reset - bring SUSPEND high before PCLR */
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 0);
	gpio_set_value(GPIO_ESERIES_TMIO_PCLR, 0);
	msleep(1);
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 1);
	msleep(1);
	gpio_set_value(GPIO_ESERIES_TMIO_PCLR, 1);
	msleep(1);
	return 0;
}

int eseries_tmio_disable(struct platform_device *dev)
{
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 0);
	gpio_set_value(GPIO_ESERIES_TMIO_PCLR, 0);
	return 0;
}

int eseries_tmio_suspend(struct platform_device *dev)
{
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 0);
	return 0;
}

int eseries_tmio_resume(struct platform_device *dev)
{
	gpio_set_value(GPIO_ESERIES_TMIO_SUSPEND, 1);
	msleep(1);
	return 0;
}

void eseries_get_tmio_gpios(void)
{
	gpio_request(GPIO_ESERIES_TMIO_SUSPEND, NULL);
	gpio_request(GPIO_ESERIES_TMIO_PCLR, NULL);
	gpio_direction_output(GPIO_ESERIES_TMIO_SUSPEND, 0);
	gpio_direction_output(GPIO_ESERIES_TMIO_PCLR, 0);
}

/* TMIO controller uses the same resources on all e-series machines. */
struct resource eseries_tmio_resources[] = {
	[0] = {
		.start  = PXA_CS4_PHYS,
		.end    = PXA_CS4_PHYS + 0x1fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = PXA_GPIO_TO_IRQ(GPIO_ESERIES_TMIO_IRQ),
		.end    = PXA_GPIO_TO_IRQ(GPIO_ESERIES_TMIO_IRQ),
		.flags  = IORESOURCE_IRQ,
	},
};

/* Some e-series hardware cannot control the 32K clock */
static void __init __maybe_unused eseries_register_clks(void)
{
	clk_register_fixed_rate(NULL, "CLK_CK32K", NULL, 0, 32768);
}

#ifdef CONFIG_MACH_E330
/* -------------------- e330 tc6387xb parameters -------------------- */

static struct tc6387xb_platform_data e330_tc6387xb_info = {
	.enable   = &eseries_tmio_enable,
	.disable  = &eseries_tmio_disable,
	.suspend  = &eseries_tmio_suspend,
	.resume   = &eseries_tmio_resume,
};

static struct platform_device e330_tc6387xb_device = {
	.name           = "tc6387xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e330_tc6387xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

/* --------------------------------------------------------------- */

static struct platform_device *e330_devices[] __initdata = {
	&e330_tc6387xb_device,
	&e7xx_gpio_vbus,
};

static void __init e330_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	eseries_register_clks();
	eseries_get_tmio_gpios();
	gpiod_add_lookup_table(&e7xx_gpio_vbus_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(e330_devices));
}

MACHINE_START(E330, "Toshiba e330")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= ESERIES_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e330_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E350
/* -------------------- e350 t7l66xb parameters -------------------- */

static struct t7l66xb_platform_data e350_t7l66xb_info = {
	.irq_base               = IRQ_BOARD_START,
	.enable                 = &eseries_tmio_enable,
	.suspend                = &eseries_tmio_suspend,
	.resume                 = &eseries_tmio_resume,
};

static struct platform_device e350_t7l66xb_device = {
	.name           = "t7l66xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e350_t7l66xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

/* ---------------------------------------------------------- */

static struct platform_device *e350_devices[] __initdata = {
	&e350_t7l66xb_device,
	&e7xx_gpio_vbus,
};

static void __init e350_init(void)
{
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	eseries_register_clks();
	eseries_get_tmio_gpios();
	gpiod_add_lookup_table(&e7xx_gpio_vbus_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(e350_devices));
}

MACHINE_START(E350, "Toshiba e350")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= ESERIES_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e350_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E400
/* ------------------------ E400 LCD definitions ------------------------ */

static struct pxafb_mode_info e400_pxafb_mode_info = {
	.pixclock       = 140703,
	.xres           = 240,
	.yres           = 320,
	.bpp            = 16,
	.hsync_len      = 4,
	.left_margin    = 28,
	.right_margin   = 8,
	.vsync_len      = 3,
	.upper_margin   = 5,
	.lower_margin   = 6,
	.sync           = 0,
};

static struct pxafb_mach_info e400_pxafb_mach_info = {
	.modes          = &e400_pxafb_mode_info,
	.num_modes      = 1,
	.lcd_conn	= LCD_COLOR_TFT_16BPP,
	.lccr3          = 0,
	.pxafb_backlight_power  = NULL,
};

/* ------------------------ E400 MFP config ----------------------------- */

static unsigned long e400_pin_config[] __initdata = {
	/* Chip selects */
	GPIO15_nCS_1,   /* CS1 - Flash */
	GPIO80_nCS_4,   /* CS4 - TMIO */

	/* Clocks */
	GPIO12_32KHz,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,

	/* TMIO controller */
	GPIO19_GPIO, /* t7l66xb #PCLR */
	GPIO45_GPIO, /* t7l66xb #SUSPEND (NOT BTUART!) */

	/* wakeup */
	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,
};

/* ---------------------------------------------------------------------- */

static struct mtd_partition partition_a = {
	.name = "Internal NAND flash",
	.offset =  0,
	.size =  MTDPART_SIZ_FULL,
};

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr e400_t7l66xb_nand_bbt = {
	.options = 0,
	.offs = 4,
	.len = 2,
	.pattern = scan_ff_pattern
};

static struct tmio_nand_data e400_t7l66xb_nand_config = {
	.num_partitions = 1,
	.partition = &partition_a,
	.badblock_pattern = &e400_t7l66xb_nand_bbt,
};

static struct t7l66xb_platform_data e400_t7l66xb_info = {
	.irq_base 		= IRQ_BOARD_START,
	.enable                 = &eseries_tmio_enable,
	.suspend                = &eseries_tmio_suspend,
	.resume                 = &eseries_tmio_resume,

	.nand_data              = &e400_t7l66xb_nand_config,
};

static struct platform_device e400_t7l66xb_device = {
	.name           = "t7l66xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e400_t7l66xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

/* ---------------------------------------------------------- */

static struct platform_device *e400_devices[] __initdata = {
	&e400_t7l66xb_device,
	&e7xx_gpio_vbus,
};

static void __init e400_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e400_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	/* Fixme - e400 may have a switched clock */
	eseries_register_clks();
	eseries_get_tmio_gpios();
	pxa_set_fb_info(NULL, &e400_pxafb_mach_info);
	gpiod_add_lookup_table(&e7xx_gpio_vbus_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(e400_devices));
}

MACHINE_START(E400, "Toshiba e400")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= ESERIES_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e400_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E740
/* ------------------------ e740 video support --------------------------- */

static struct w100_gen_regs e740_lcd_regs = {
	.lcd_format =            0x00008023,
	.lcdd_cntl1 =            0x0f000000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x00ffff03,
	.genlcd_cntl2 =          0x003c0f03,
	.genlcd_cntl3 =          0x000143aa,
};

static struct w100_mode e740_lcd_mode = {
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 20,
	.right_margin    = 28,
	.upper_margin    = 9,
	.lower_margin    = 8,
	.crtc_ss         = 0x80140013,
	.crtc_ls         = 0x81150110,
	.crtc_gs         = 0x80050005,
	.crtc_vpos_gs    = 0x000a0009,
	.crtc_rev        = 0x0040010a,
	.crtc_dclk       = 0xa906000a,
	.crtc_gclk       = 0x80050108,
	.crtc_goe        = 0x80050108,
	.pll_freq        = 57,
	.pixclk_divider         = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_XTAL,
	.sysclk_divider  = 1,
	.sysclk_src     = CLK_SRC_PLL,
	.crtc_ps1_active =       0x41060010,
};

static struct w100_gpio_regs e740_w100_gpio_info = {
	.init_data1 = 0x21002103,
	.gpio_dir1  = 0xffffdeff,
	.gpio_oe1   = 0x03c00643,
	.init_data2 = 0x003f003f,
	.gpio_dir2  = 0xffffffff,
	.gpio_oe2   = 0x000000ff,
};

static struct w100fb_mach_info e740_fb_info = {
	.modelist   = &e740_lcd_mode,
	.num_modes  = 1,
	.regs       = &e740_lcd_regs,
	.gpio       = &e740_w100_gpio_info,
	.xtal_freq = 14318000,
	.xtal_dbl   = 1,
};

static struct resource e740_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device e740_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e740_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e740_fb_resources),
	.resource       = e740_fb_resources,
};

/* --------------------------- MFP Pin config -------------------------- */

static unsigned long e740_pin_config[] __initdata = {
	/* Chip selects */
	GPIO15_nCS_1,   /* CS1 - Flash */
	GPIO79_nCS_3,   /* CS3 - IMAGEON */
	GPIO80_nCS_4,   /* CS4 - TMIO */

	/* Clocks */
	GPIO12_32KHz,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,

	/* TMIO controller */
	GPIO19_GPIO, /* t7l66xb #PCLR */
	GPIO45_GPIO, /* t7l66xb #SUSPEND (NOT BTUART!) */

	/* UDC */
	GPIO13_GPIO,
	GPIO3_GPIO,

	/* IrDA */
	GPIO38_GPIO | MFP_LPM_DRIVE_HIGH,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* Audio power control */
	GPIO16_GPIO,  /* AC97 codec AVDD2 supply (analogue power) */
	GPIO40_GPIO,  /* Mic amp power */
	GPIO41_GPIO,  /* Headphone amp power */

	/* PC Card */
	GPIO8_GPIO,   /* CD0 */
	GPIO44_GPIO,  /* CD1 */
	GPIO11_GPIO,  /* IRQ0 */
	GPIO6_GPIO,   /* IRQ1 */
	GPIO27_GPIO,  /* RST0 */
	GPIO24_GPIO,  /* RST1 */
	GPIO20_GPIO,  /* PWR0 */
	GPIO23_GPIO,  /* PWR1 */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* wakeup */
	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,
};

/* -------------------- e740 t7l66xb parameters -------------------- */

static struct t7l66xb_platform_data e740_t7l66xb_info = {
	.irq_base 		= IRQ_BOARD_START,
	.enable                 = &eseries_tmio_enable,
	.suspend                = &eseries_tmio_suspend,
	.resume                 = &eseries_tmio_resume,
};

static struct platform_device e740_t7l66xb_device = {
	.name           = "t7l66xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e740_t7l66xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

static struct platform_device e740_audio_device = {
	.name		= "e740-audio",
	.id		= -1,
};

/* ----------------------------------------------------------------------- */

static struct platform_device *e740_devices[] __initdata = {
	&e740_fb_device,
	&e740_t7l66xb_device,
	&e7xx_gpio_vbus,
	&e740_audio_device,
};

static void __init e740_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e740_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	eseries_register_clks();
	clk_add_alias("CLK_CK48M", e740_t7l66xb_device.name,
			"UDCCLK", &pxa25x_device_udc.dev),
	eseries_get_tmio_gpios();
	gpiod_add_lookup_table(&e7xx_gpio_vbus_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(e740_devices));
	pxa_set_ac97_info(NULL);
	pxa_set_ficp_info(&e7xx_ficp_platform_data);
}

MACHINE_START(E740, "Toshiba e740")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= ESERIES_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e740_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E750
/* ---------------------- E750 LCD definitions -------------------- */

static struct w100_gen_regs e750_lcd_regs = {
	.lcd_format =            0x00008003,
	.lcdd_cntl1 =            0x00000000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x00fff003,
	.genlcd_cntl2 =          0x003c0f03,
	.genlcd_cntl3 =          0x000143aa,
};

static struct w100_mode e750_lcd_mode = {
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 21,
	.right_margin    = 22,
	.upper_margin    = 5,
	.lower_margin    = 4,
	.crtc_ss         = 0x80150014,
	.crtc_ls         = 0x8014000d,
	.crtc_gs         = 0xc1000005,
	.crtc_vpos_gs    = 0x00020147,
	.crtc_rev        = 0x0040010a,
	.crtc_dclk       = 0xa1700030,
	.crtc_gclk       = 0x80cc0015,
	.crtc_goe        = 0x80cc0015,
	.crtc_ps1_active = 0x61060017,
	.pll_freq        = 57,
	.pixclk_divider         = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_XTAL,
	.sysclk_divider  = 1,
	.sysclk_src     = CLK_SRC_PLL,
};

static struct w100_gpio_regs e750_w100_gpio_info = {
	.init_data1 = 0x01192f1b,
	.gpio_dir1  = 0xd5ffdeff,
	.gpio_oe1   = 0x000020bf,
	.init_data2 = 0x010f010f,
	.gpio_dir2  = 0xffffffff,
	.gpio_oe2   = 0x000001cf,
};

static struct w100fb_mach_info e750_fb_info = {
	.modelist   = &e750_lcd_mode,
	.num_modes  = 1,
	.regs       = &e750_lcd_regs,
	.gpio       = &e750_w100_gpio_info,
	.xtal_freq  = 14318000,
	.xtal_dbl   = 1,
};

static struct resource e750_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device e750_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e750_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e750_fb_resources),
	.resource       = e750_fb_resources,
};

/* -------------------- e750 MFP parameters -------------------- */

static unsigned long e750_pin_config[] __initdata = {
	/* Chip selects */
	GPIO15_nCS_1,   /* CS1 - Flash */
	GPIO79_nCS_3,   /* CS3 - IMAGEON */
	GPIO80_nCS_4,   /* CS4 - TMIO */

	/* Clocks */
	GPIO11_3_6MHz,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,

	/* TMIO controller */
	GPIO19_GPIO, /* t7l66xb #PCLR */
	GPIO45_GPIO, /* t7l66xb #SUSPEND (NOT BTUART!) */

	/* UDC */
	GPIO13_GPIO,
	GPIO3_GPIO,

	/* IrDA */
	GPIO38_GPIO | MFP_LPM_DRIVE_HIGH,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* Audio power control */
	GPIO4_GPIO,  /* Headphone amp power */
	GPIO7_GPIO,  /* Speaker amp power */
	GPIO37_GPIO, /* Headphone detect */

	/* PC Card */
	GPIO8_GPIO,   /* CD0 */
	GPIO44_GPIO,  /* CD1 */
	/* GPIO11_GPIO,  IRQ0 */
	GPIO6_GPIO,   /* IRQ1 */
	GPIO27_GPIO,  /* RST0 */
	GPIO24_GPIO,  /* RST1 */
	GPIO20_GPIO,  /* PWR0 */
	GPIO23_GPIO,  /* PWR1 */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* wakeup */
	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,
};

/* ----------------- e750 tc6393xb parameters ------------------ */

static struct tc6393xb_platform_data e750_tc6393xb_info = {
	.irq_base       = IRQ_BOARD_START,
	.scr_pll2cr     = 0x0cc1,
	.scr_gper       = 0,
	.gpio_base      = -1,
	.suspend        = &eseries_tmio_suspend,
	.resume         = &eseries_tmio_resume,
	.enable         = &eseries_tmio_enable,
	.disable        = &eseries_tmio_disable,
};

static struct platform_device e750_tc6393xb_device = {
	.name           = "tc6393xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e750_tc6393xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

static struct platform_device e750_audio_device = {
	.name		= "e750-audio",
	.id		= -1,
};

/* ------------------------------------------------------------- */

static struct platform_device *e750_devices[] __initdata = {
	&e750_fb_device,
	&e750_tc6393xb_device,
	&e7xx_gpio_vbus,
	&e750_audio_device,
};

static void __init e750_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e750_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	clk_add_alias("CLK_CK3P6MI", e750_tc6393xb_device.name,
			"GPIO11_CLK", NULL),
	eseries_get_tmio_gpios();
	gpiod_add_lookup_table(&e7xx_gpio_vbus_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(e750_devices));
	pxa_set_ac97_info(NULL);
	pxa_set_ficp_info(&e7xx_ficp_platform_data);
}

MACHINE_START(E750, "Toshiba e750")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= ESERIES_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e750_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_E800
/* ------------------------ e800 LCD definitions ------------------------- */

static unsigned long e800_pin_config[] __initdata = {
	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,

	/* tc6393xb */
	GPIO11_3_6MHz,
};

static struct w100_gen_regs e800_lcd_regs = {
	.lcd_format =            0x00008003,
	.lcdd_cntl1 =            0x02a00000,
	.lcdd_cntl2 =            0x0003ffff,
	.genlcd_cntl1 =          0x000ff2a3,
	.genlcd_cntl2 =          0x000002a3,
	.genlcd_cntl3 =          0x000102aa,
};

static struct w100_mode e800_lcd_mode[2] = {
	[0] = {
		.xres            = 480,
		.yres            = 640,
		.left_margin     = 52,
		.right_margin    = 148,
		.upper_margin    = 2,
		.lower_margin    = 6,
		.crtc_ss         = 0x80350034,
		.crtc_ls         = 0x802b0026,
		.crtc_gs         = 0x80160016,
		.crtc_vpos_gs    = 0x00020003,
		.crtc_rev        = 0x0040001d,
		.crtc_dclk       = 0xe0000000,
		.crtc_gclk       = 0x82a50049,
		.crtc_goe        = 0x80ee001c,
		.crtc_ps1_active = 0x00000000,
		.pll_freq        = 128,
		.pixclk_divider         = 4,
		.pixclk_divider_rotated = 6,
		.pixclk_src     = CLK_SRC_PLL,
		.sysclk_divider  = 0,
		.sysclk_src     = CLK_SRC_PLL,
	},
	[1] = {
		.xres            = 240,
		.yres            = 320,
		.left_margin     = 15,
		.right_margin    = 88,
		.upper_margin    = 0,
		.lower_margin    = 7,
		.crtc_ss         = 0xd010000f,
		.crtc_ls         = 0x80070003,
		.crtc_gs         = 0x80000000,
		.crtc_vpos_gs    = 0x01460147,
		.crtc_rev        = 0x00400003,
		.crtc_dclk       = 0xa1700030,
		.crtc_gclk       = 0x814b0008,
		.crtc_goe        = 0x80cc0015,
		.crtc_ps1_active = 0x00000000,
		.pll_freq        = 100,
		.pixclk_divider         = 6, /* Wince uses 14 which gives a */
		.pixclk_divider_rotated = 6, /* 7MHz Pclk. We use a 14MHz one */
		.pixclk_src     = CLK_SRC_PLL,
		.sysclk_divider  = 0,
		.sysclk_src     = CLK_SRC_PLL,
	}
};


static struct w100_gpio_regs e800_w100_gpio_info = {
	.init_data1 = 0xc13fc019,
	.gpio_dir1  = 0x3e40df7f,
	.gpio_oe1   = 0x003c3000,
	.init_data2 = 0x00000000,
	.gpio_dir2  = 0x00000000,
	.gpio_oe2   = 0x00000000,
};

static struct w100_mem_info e800_w100_mem_info = {
	.ext_cntl        = 0x09640011,
	.sdram_mode_reg  = 0x00600021,
	.ext_timing_cntl = 0x10001545,
	.io_cntl         = 0x7ddd7333,
	.size            = 0x1fffff,
};

static void e800_tg_change(struct w100fb_par *par)
{
	unsigned long tmp;

	tmp = w100fb_gpio_read(W100_GPIO_PORT_A);
	if (par->mode->xres == 480)
		tmp |= 0x100;
	else
		tmp &= ~0x100;
	w100fb_gpio_write(W100_GPIO_PORT_A, tmp);
}

static struct w100_tg_info e800_tg_info = {
	.change = e800_tg_change,
};

static struct w100fb_mach_info e800_fb_info = {
	.modelist   = e800_lcd_mode,
	.num_modes  = 2,
	.regs       = &e800_lcd_regs,
	.gpio       = &e800_w100_gpio_info,
	.mem        = &e800_w100_mem_info,
	.tg         = &e800_tg_info,
	.xtal_freq  = 16000000,
};

static struct resource e800_fb_resources[] = {
	[0] = {
		.start          = 0x0c000000,
		.end            = 0x0cffffff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device e800_fb_device = {
	.name           = "w100fb",
	.id             = -1,
	.dev            = {
		.platform_data  = &e800_fb_info,
	},
	.num_resources  = ARRAY_SIZE(e800_fb_resources),
	.resource       = e800_fb_resources,
};

/* --------------------------- UDC definitions --------------------------- */

static struct gpiod_lookup_table e800_gpio_vbus_gpiod_table = {
	.dev_id = "gpio-vbus",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_E800_USB_DISC,
			    "vbus", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-pxa", GPIO_E800_USB_PULLUP,
			    "pullup", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct platform_device e800_gpio_vbus = {
	.name	= "gpio-vbus",
	.id	= -1,
};


/* ----------------- e800 tc6393xb parameters ------------------ */

static struct tc6393xb_platform_data e800_tc6393xb_info = {
	.irq_base       = IRQ_BOARD_START,
	.scr_pll2cr     = 0x0cc1,
	.scr_gper       = 0,
	.gpio_base      = -1,
	.suspend        = &eseries_tmio_suspend,
	.resume         = &eseries_tmio_resume,
	.enable         = &eseries_tmio_enable,
	.disable        = &eseries_tmio_disable,
};

static struct platform_device e800_tc6393xb_device = {
	.name           = "tc6393xb",
	.id             = -1,
	.dev            = {
		.platform_data = &e800_tc6393xb_info,
	},
	.num_resources = 2,
	.resource      = eseries_tmio_resources,
};

static struct platform_device e800_audio_device = {
	.name		= "e800-audio",
	.id		= -1,
};

/* ----------------------------------------------------------------------- */

static struct platform_device *e800_devices[] __initdata = {
	&e800_fb_device,
	&e800_tc6393xb_device,
	&e800_gpio_vbus,
	&e800_audio_device,
};

static void __init e800_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(e800_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);
	clk_add_alias("CLK_CK3P6MI", e800_tc6393xb_device.name,
			"GPIO11_CLK", NULL),
	eseries_get_tmio_gpios();
	gpiod_add_lookup_table(&e800_gpio_vbus_gpiod_table);
	platform_add_devices(ARRAY_AND_SIZE(e800_devices));
	pxa_set_ac97_info(NULL);
}

MACHINE_START(E800, "Toshiba e800")
	/* Maintainer: Ian Molton (spyro@f2s.com) */
	.atag_offset	= 0x100,
	.map_io		= pxa25x_map_io,
	.nr_irqs	= ESERIES_NR_IRQS,
	.init_irq	= pxa25x_init_irq,
	.handle_irq	= pxa25x_handle_irq,
	.fixup		= eseries_fixup,
	.init_machine	= e800_init,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif
