/*
 * Support for Sharp SL-C7xx PDAs
 * Models: SL-C700 (Corgi), SL-C750 (Shepherd), SL-C760 (Husky)
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * Based on Sharp's 2.4 kernel patches/lubbock.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mmc/host.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/backlight.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/corgi_lcd.h>
#include <video/w100fb.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h>
#include <mach/mfp-pxa25x.h>
#include <mach/i2c.h>
#include <mach/irda.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/pxa2xx_spi.h>
#include <mach/corgi.h>
#include <mach/sharpsl.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
#include "devices.h"
#include "sharpsl.h"

static unsigned long corgi_pin_config[] __initdata = {
	/* Static Memory I/O */
	GPIO78_nCS_2,	/* w100fb */
	GPIO80_nCS_4,	/* scoop */

	/* SSP1 */
	GPIO23_SSP1_SCLK,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,
	GPIO24_GPIO,	/* CORGI_GPIO_ADS7846_CS - SFRM as chip select */

	/* I2S */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,
	GPIO32_I2S_SYSCLK,

	/* Infra-Red */
	GPIO47_FICP_TXD,
	GPIO46_FICP_RXD,

	/* FFUART */
	GPIO40_FFUART_DTR,
	GPIO41_FFUART_RTS,
	GPIO39_FFUART_TXD,
	GPIO37_FFUART_DSR,
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,

	/* PC Card */
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

	/* MMC */
	GPIO6_MMC_CLK,
	GPIO8_MMC_CS0,

	/* GPIO */
	GPIO9_GPIO,	/* CORGI_GPIO_nSD_DETECT */
	GPIO7_GPIO,	/* CORGI_GPIO_nSD_WP */
	GPIO33_GPIO,	/* CORGI_GPIO_SD_PWR */
	GPIO22_GPIO,	/* CORGI_GPIO_IR_ON */
	GPIO44_GPIO,	/* CORGI_GPIO_HSYNC */

	GPIO1_GPIO | WAKEUP_ON_EDGE_RISE,
};

/*
 * Corgi SCOOP Device
 */
static struct resource corgi_scoop_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config corgi_scoop_setup = {
	.io_dir 	= CORGI_SCOOP_IO_DIR,
	.io_out		= CORGI_SCOOP_IO_OUT,
	.gpio_base	= CORGI_SCOOP_GPIO_BASE,
};

struct platform_device corgiscoop_device = {
	.name		= "sharp-scoop",
	.id		= -1,
	.dev		= {
 		.platform_data	= &corgi_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(corgi_scoop_resources),
	.resource	= corgi_scoop_resources,
};

static struct scoop_pcmcia_dev corgi_pcmcia_scoop[] = {
{
	.dev        = &corgiscoop_device.dev,
	.irq        = CORGI_IRQ_GPIO_CF_IRQ,
	.cd_irq     = CORGI_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},
};

static struct scoop_pcmcia_config corgi_pcmcia_config = {
	.devs         = &corgi_pcmcia_scoop[0],
	.num_devs     = 1,
};

EXPORT_SYMBOL(corgiscoop_device);

static struct w100_mem_info corgi_fb_mem = {
	.ext_cntl          = 0x00040003,
	.sdram_mode_reg    = 0x00650021,
	.ext_timing_cntl   = 0x10002a4a,
	.io_cntl           = 0x7ff87012,
	.size              = 0x1fffff,
};

static struct w100_gen_regs corgi_fb_regs = {
	.lcd_format    = 0x00000003,
	.lcdd_cntl1    = 0x01CC0000,
	.lcdd_cntl2    = 0x0003FFFF,
	.genlcd_cntl1  = 0x00FFFF0D,
	.genlcd_cntl2  = 0x003F3003,
	.genlcd_cntl3  = 0x000102aa,
};

static struct w100_gpio_regs corgi_fb_gpio = {
	.init_data1   = 0x000000bf,
	.init_data2   = 0x00000000,
	.gpio_dir1    = 0x00000000,
	.gpio_oe1     = 0x03c0feff,
	.gpio_dir2    = 0x00000000,
	.gpio_oe2     = 0x00000000,
};

static struct w100_mode corgi_fb_modes[] = {
{
	.xres            = 480,
	.yres            = 640,
	.left_margin     = 0x56,
	.right_margin    = 0x55,
	.upper_margin    = 0x03,
	.lower_margin    = 0x00,
	.crtc_ss         = 0x82360056,
	.crtc_ls         = 0xA0280000,
	.crtc_gs         = 0x80280028,
	.crtc_vpos_gs    = 0x02830002,
	.crtc_rev        = 0x00400008,
	.crtc_dclk       = 0xA0000000,
	.crtc_gclk       = 0x8015010F,
	.crtc_goe        = 0x80100110,
	.crtc_ps1_active = 0x41060010,
	.pll_freq        = 75,
	.fast_pll_freq   = 100,
	.sysclk_src      = CLK_SRC_PLL,
	.sysclk_divider  = 0,
	.pixclk_src      = CLK_SRC_PLL,
	.pixclk_divider  = 2,
	.pixclk_divider_rotated = 6,
},{
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 0x27,
	.right_margin    = 0x2e,
	.upper_margin    = 0x01,
	.lower_margin    = 0x00,
	.crtc_ss         = 0x81170027,
	.crtc_ls         = 0xA0140000,
	.crtc_gs         = 0xC0140014,
	.crtc_vpos_gs    = 0x00010141,
	.crtc_rev        = 0x00400008,
	.crtc_dclk       = 0xA0000000,
	.crtc_gclk       = 0x8015010F,
	.crtc_goe        = 0x80100110,
	.crtc_ps1_active = 0x41060010,
	.pll_freq        = 0,
	.fast_pll_freq   = 0,
	.sysclk_src      = CLK_SRC_XTAL,
	.sysclk_divider  = 0,
	.pixclk_src      = CLK_SRC_XTAL,
	.pixclk_divider  = 1,
	.pixclk_divider_rotated = 1,
},

};

static struct w100fb_mach_info corgi_fb_info = {
	.init_mode  = INIT_MODE_ROTATED,
	.mem        = &corgi_fb_mem,
	.regs       = &corgi_fb_regs,
	.modelist   = &corgi_fb_modes[0],
	.num_modes  = 2,
	.gpio       = &corgi_fb_gpio,
	.xtal_freq  = 12500000,
	.xtal_dbl   = 0,
};

static struct resource corgi_fb_resources[] = {
	[0] = {
		.start   = 0x08000000,
		.end     = 0x08ffffff,
		.flags   = IORESOURCE_MEM,
	},
};

static struct platform_device corgifb_device = {
	.name           = "w100fb",
	.id             = -1,
	.num_resources	= ARRAY_SIZE(corgi_fb_resources),
	.resource	= corgi_fb_resources,
	.dev            = {
		.platform_data = &corgi_fb_info,
	},

};

/*
 * Corgi Keyboard Device
 */
static struct platform_device corgikbd_device = {
	.name		= "corgi-keyboard",
	.id		= -1,
};

/*
 * Corgi LEDs
 */
static struct gpio_led corgi_gpio_leds[] = {
	{
		.name			= "corgi:amber:charge",
		.default_trigger	= "sharpsl-charge",
		.gpio			= CORGI_GPIO_LED_ORANGE,
	},
	{
		.name			= "corgi:green:mail",
		.default_trigger	= "nand-disk",
		.gpio			= CORGI_GPIO_LED_GREEN,
	},
};

static struct gpio_led_platform_data corgi_gpio_leds_info = {
	.leds		= corgi_gpio_leds,
	.num_leds	= ARRAY_SIZE(corgi_gpio_leds),
};

static struct platform_device corgiled_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data = &corgi_gpio_leds_info,
	},
};

/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */
static struct pxamci_platform_data corgi_mci_platform_data;

static int corgi_mci_init(struct device *dev, irq_handler_t corgi_detect_int, void *data)
{
	int err;

	err = gpio_request(CORGI_GPIO_nSD_DETECT, "nSD_DETECT");
	if (err)
		goto err_out;

	err = gpio_request(CORGI_GPIO_nSD_WP, "nSD_WP");
	if (err)
		goto err_free_1;

	err = gpio_request(CORGI_GPIO_SD_PWR, "SD_PWR");
	if (err)
		goto err_free_2;

	gpio_direction_input(CORGI_GPIO_nSD_DETECT);
	gpio_direction_input(CORGI_GPIO_nSD_WP);
	gpio_direction_output(CORGI_GPIO_SD_PWR, 0);

	corgi_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(CORGI_IRQ_GPIO_nSD_DETECT, corgi_detect_int,
				IRQF_DISABLED | IRQF_TRIGGER_RISING |
				IRQF_TRIGGER_FALLING,
				"MMC card detect", data);
	if (err) {
		pr_err("%s: MMC/SD: can't request MMC card detect IRQ\n",
				__func__);
		goto err_free_3;
	}
	return 0;

err_free_3:
	gpio_free(CORGI_GPIO_SD_PWR);
err_free_2:
	gpio_free(CORGI_GPIO_nSD_WP);
err_free_1:
	gpio_free(CORGI_GPIO_nSD_DETECT);
err_out:
	return err;
}

static void corgi_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	gpio_set_value(CORGI_GPIO_SD_PWR, ((1 << vdd) & p_d->ocr_mask));
}

static int corgi_mci_get_ro(struct device *dev)
{
	return gpio_get_value(CORGI_GPIO_nSD_WP);
}

static void corgi_mci_exit(struct device *dev, void *data)
{
	free_irq(CORGI_IRQ_GPIO_nSD_DETECT, data);
	gpio_free(CORGI_GPIO_SD_PWR);
	gpio_free(CORGI_GPIO_nSD_WP);
	gpio_free(CORGI_GPIO_nSD_DETECT);
}

static struct pxamci_platform_data corgi_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= corgi_mci_init,
	.get_ro		= corgi_mci_get_ro,
	.setpower 	= corgi_mci_setpower,
	.exit		= corgi_mci_exit,
};


/*
 * Irda
 */
static void corgi_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(CORGI_GPIO_IR_ON, mode & IR_OFF);
	pxa2xx_transceiver_mode(dev, mode);
}

static int corgi_irda_startup(struct device *dev)
{
	int err;

	err = gpio_request(CORGI_GPIO_IR_ON, "IR_ON");
	if (err)
		return err;

	gpio_direction_output(CORGI_GPIO_IR_ON, 1);
	return 0;
}

static void corgi_irda_shutdown(struct device *dev)
{
	gpio_free(CORGI_GPIO_IR_ON);
}

static struct pxaficp_platform_data corgi_ficp_platform_data = {
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
	.transceiver_mode	= corgi_irda_transceiver_mode,
	.startup		= corgi_irda_startup,
	.shutdown		= corgi_irda_shutdown,
};


/*
 * USB Device Controller
 */
static struct pxa2xx_udc_mach_info udc_info __initdata = {
	/* no connect GPIO; corgi can't tell connection status */
	.gpio_pullup		= CORGI_GPIO_USB_PULLUP,
};

#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MASTER)
static struct pxa2xx_spi_master corgi_spi_info = {
	.num_chipselect	= 3,
};

static struct ads7846_platform_data corgi_ads7846_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.gpio_pendown		= CORGI_GPIO_TP_INT,
};

static void corgi_ads7846_cs(u32 command)
{
	gpio_set_value(CORGI_GPIO_ADS7846_CS, !(command == PXA2XX_CS_ASSERT));
}

static struct pxa2xx_spi_chip corgi_ads7846_chip = {
	.cs_control	= corgi_ads7846_cs,
};

static void corgi_bl_kick_battery(void)
{
	void (*kick_batt)(void);

	kick_batt = symbol_get(sharpsl_battery_kick);
	if (kick_batt) {
		kick_batt();
		symbol_put(sharpsl_battery_kick);
	}
}

static struct corgi_lcd_platform_data corgi_lcdcon_info = {
	.init_mode		= CORGI_LCD_MODE_VGA,
	.max_intensity		= 0x2f,
	.default_intensity	= 0x1f,
	.limit_mask		= 0x0b,
	.gpio_backlight_cont	= CORGI_GPIO_BACKLIGHT_CONT,
	.gpio_backlight_on	= -1,
	.kick_battery		= corgi_bl_kick_battery,
};

static void corgi_lcdcon_cs(u32 command)
{
	gpio_set_value(CORGI_GPIO_LCDCON_CS, !(command == PXA2XX_CS_ASSERT));
}

static struct pxa2xx_spi_chip corgi_lcdcon_chip = {
	.cs_control	= corgi_lcdcon_cs,
};

static void corgi_max1111_cs(u32 command)
{
	gpio_set_value(CORGI_GPIO_MAX1111_CS, !(command == PXA2XX_CS_ASSERT));
}

static struct pxa2xx_spi_chip corgi_max1111_chip = {
	.cs_control	= corgi_max1111_cs,
};

static struct spi_board_info corgi_spi_devices[] = {
	{
		.modalias	= "ads7846",
		.max_speed_hz	= 1200000,
		.bus_num	= 1,
		.chip_select	= 0,
		.platform_data	= &corgi_ads7846_info,
		.controller_data= &corgi_ads7846_chip,
		.irq		= gpio_to_irq(CORGI_GPIO_TP_INT),
	}, {
		.modalias	= "corgi-lcd",
		.max_speed_hz	= 50000,
		.bus_num	= 1,
		.chip_select	= 1,
		.platform_data	= &corgi_lcdcon_info,
		.controller_data= &corgi_lcdcon_chip,
	}, {
		.modalias	= "max1111",
		.max_speed_hz	= 450000,
		.bus_num	= 1,
		.chip_select	= 2,
		.controller_data= &corgi_max1111_chip,
	},
};

static void __init corgi_init_spi(void)
{
	int err;

	err = gpio_request(CORGI_GPIO_ADS7846_CS, "ADS7846_CS");
	if (err)
		return;

	err = gpio_request(CORGI_GPIO_LCDCON_CS, "LCDCON_CS");
	if (err)
		goto err_free_1;

	err = gpio_request(CORGI_GPIO_MAX1111_CS, "MAX1111_CS");
	if (err)
		goto err_free_2;

	gpio_direction_output(CORGI_GPIO_ADS7846_CS, 1);
	gpio_direction_output(CORGI_GPIO_LCDCON_CS, 1);
	gpio_direction_output(CORGI_GPIO_MAX1111_CS, 1);

	pxa2xx_set_spi_info(1, &corgi_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(corgi_spi_devices));
	return;

err_free_2:
	gpio_free(CORGI_GPIO_LCDCON_CS);
err_free_1:
	gpio_free(CORGI_GPIO_ADS7846_CS);
}
#else
static inline void corgi_init_spi(void) {}
#endif

static struct platform_device *devices[] __initdata = {
	&corgiscoop_device,
	&corgifb_device,
	&corgikbd_device,
	&corgiled_device,
};

static void corgi_poweroff(void)
{
	if (!machine_is_corgi())
		/* Green LED off tells the bootloader to halt */
		gpio_set_value(CORGI_GPIO_LED_GREEN, 0);

	arm_machine_restart('h');
}

static void corgi_restart(char mode)
{
	if (!machine_is_corgi())
		/* Green LED on tells the bootloader to reboot */
		gpio_set_value(CORGI_GPIO_LED_GREEN, 1);

	arm_machine_restart('h');
}

static void __init corgi_init(void)
{
	pm_power_off = corgi_poweroff;
	arm_pm_restart = corgi_restart;

	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(corgi_pin_config));

	corgi_init_spi();

 	pxa_set_udc_info(&udc_info);
	pxa_set_mci_info(&corgi_mci_platform_data);
	pxa_set_ficp_info(&corgi_ficp_platform_data);
	pxa_set_i2c_info(NULL);

	platform_scoop_config = &corgi_pcmcia_config;

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init fixup_corgi(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	if (machine_is_corgi())
		mi->bank[0].size = (32*1024*1024);
	else
		mi->bank[0].size = (64*1024*1024);
}

#ifdef CONFIG_MACH_CORGI
MACHINE_START(CORGI, "SHARP Corgi")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_corgi,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SHEPHERD
MACHINE_START(SHEPHERD, "SHARP Shepherd")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_corgi,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_HUSKY
MACHINE_START(HUSKY, "SHARP Husky")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_corgi,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

