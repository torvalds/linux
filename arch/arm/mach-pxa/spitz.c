/*
 * Support for Sharp SL-Cxx00 Series of PDAs
 * Models: SL-C3000 (Spitz), SL-C1000 (Akita) and SL-C3100 (Borzoi)
 *
 * Copyright (c) 2005 Richard Purdie
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
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/mmc/host.h>
#include <linux/pm.h>
#include <linux/backlight.h>
#include <linux/i2c.h>
#include <linux/i2c/pca953x.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/corgi_lcd.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/pxa-regs.h>
#include <mach/pxa2xx-regs.h>
#include <mach/mfp-pxa27x.h>
#include <mach/pxa27x-udc.h>
#include <mach/reset.h>
#include <mach/i2c.h>
#include <mach/irda.h>
#include <mach/mmc.h>
#include <mach/ohci.h>
#include <mach/udc.h>
#include <mach/pxafb.h>
#include <mach/pxa2xx_spi.h>
#include <mach/spitz.h>
#include <mach/sharpsl.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
#include "devices.h"
#include "sharpsl.h"

static unsigned long spitz_pin_config[] __initdata = {
	/* Chip Selects */
	GPIO78_nCS_2,	/* SCOOP #2 */
	GPIO80_nCS_4,	/* SCOOP #1 */

	/* LCD - 16bpp Active TFT */
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO79_PSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,

	/* GPIOs */
	GPIO9_GPIO,	/* SPITZ_GPIO_nSD_DETECT */
	GPIO81_GPIO,	/* SPITZ_GPIO_nSD_WP */
	GPIO41_GPIO,	/* SPITZ_GPIO_USB_CONNECT */
	GPIO37_GPIO,	/* SPITZ_GPIO_USB_HOST */
	GPIO35_GPIO,	/* SPITZ_GPIO_USB_DEVICE */
	GPIO22_GPIO,	/* SPITZ_GPIO_HSYNC */
	GPIO94_GPIO,	/* SPITZ_GPIO_CF_CD */
	GPIO105_GPIO,	/* SPITZ_GPIO_CF_IRQ */
	GPIO106_GPIO,	/* SPITZ_GPIO_CF2_IRQ */

	GPIO1_GPIO | WAKEUP_ON_EDGE_RISE,
};

/*
 * Spitz SCOOP Device #1
 */
static struct resource spitz_scoop_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config spitz_scoop_setup = {
	.io_dir		= SPITZ_SCP_IO_DIR,
	.io_out		= SPITZ_SCP_IO_OUT,
	.suspend_clr	= SPITZ_SCP_SUS_CLR,
	.suspend_set	= SPITZ_SCP_SUS_SET,
	.gpio_base	= SPITZ_SCP_GPIO_BASE,
};

struct platform_device spitzscoop_device = {
	.name		= "sharp-scoop",
	.id		= 0,
	.dev		= {
 		.platform_data	= &spitz_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(spitz_scoop_resources),
	.resource	= spitz_scoop_resources,
};

/*
 * Spitz SCOOP Device #2
 */
static struct resource spitz_scoop2_resources[] = {
	[0] = {
		.start		= 0x08800040,
		.end		= 0x08800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config spitz_scoop2_setup = {
	.io_dir		= SPITZ_SCP2_IO_DIR,
	.io_out		= SPITZ_SCP2_IO_OUT,
	.suspend_clr	= SPITZ_SCP2_SUS_CLR,
	.suspend_set	= SPITZ_SCP2_SUS_SET,
	.gpio_base	= SPITZ_SCP2_GPIO_BASE,
};

struct platform_device spitzscoop2_device = {
	.name		= "sharp-scoop",
	.id		= 1,
	.dev		= {
 		.platform_data	= &spitz_scoop2_setup,
	},
	.num_resources	= ARRAY_SIZE(spitz_scoop2_resources),
	.resource	= spitz_scoop2_resources,
};

#define SPITZ_PWR_SD 0x01
#define SPITZ_PWR_CF 0x02

/* Power control is shared with between one of the CF slots and SD */
static void spitz_card_pwr_ctrl(int device, unsigned short new_cpr)
{
	unsigned short cpr = read_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR);

	if (new_cpr & 0x0007) {
		gpio_set_value(SPITZ_GPIO_CF_POWER, 1);
		if (!(cpr & 0x0002) && !(cpr & 0x0004))
		        mdelay(5);
		if (device == SPITZ_PWR_CF)
		        cpr |= 0x0002;
		if (device == SPITZ_PWR_SD)
		        cpr |= 0x0004;
	        write_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR, cpr | new_cpr);
	} else {
		if (device == SPITZ_PWR_CF)
		        cpr &= ~0x0002;
		if (device == SPITZ_PWR_SD)
		        cpr &= ~0x0004;
		if (!(cpr & 0x0002) && !(cpr & 0x0004)) {
			write_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR, 0x0000);
		        mdelay(1);
			gpio_set_value(SPITZ_GPIO_CF_POWER, 0);
		} else {
		        write_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR, cpr | new_cpr);
		}
	}
}

static void spitz_pcmcia_pwr(struct device *scoop, unsigned short cpr, int nr)
{
	/* Only need to override behaviour for slot 0 */
	if (nr == 0)
		spitz_card_pwr_ctrl(SPITZ_PWR_CF, cpr);
	else
		write_scoop_reg(scoop, SCOOP_CPR, cpr);
}

static struct scoop_pcmcia_dev spitz_pcmcia_scoop[] = {
{
	.dev        = &spitzscoop_device.dev,
	.irq        = SPITZ_IRQ_GPIO_CF_IRQ,
	.cd_irq     = SPITZ_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},{
	.dev        = &spitzscoop2_device.dev,
	.irq        = SPITZ_IRQ_GPIO_CF2_IRQ,
	.cd_irq     = -1,
},
};

static struct scoop_pcmcia_config spitz_pcmcia_config = {
	.devs         = &spitz_pcmcia_scoop[0],
	.num_devs     = 2,
	.power_ctrl   = spitz_pcmcia_pwr,
};

EXPORT_SYMBOL(spitzscoop_device);
EXPORT_SYMBOL(spitzscoop2_device);

/*
 * Spitz Keyboard Device
 */
static struct platform_device spitzkbd_device = {
	.name		= "spitz-keyboard",
	.id		= -1,
};


/*
 * Spitz LEDs
 */
static struct gpio_led spitz_gpio_leds[] = {
	{
		.name			= "spitz:amber:charge",
		.default_trigger	= "sharpsl-charge",
		.gpio			= SPITZ_GPIO_LED_ORANGE,
	},
	{
		.name			= "spitz:green:hddactivity",
		.default_trigger	= "ide-disk",
		.gpio			= SPITZ_GPIO_LED_GREEN,
	},
};

static struct gpio_led_platform_data spitz_gpio_leds_info = {
	.leds		= spitz_gpio_leds,
	.num_leds	= ARRAY_SIZE(spitz_gpio_leds),
};

static struct platform_device spitzled_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data = &spitz_gpio_leds_info,
	},
};

#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MODULE)
static struct pxa2xx_spi_master spitz_spi_info = {
	.num_chipselect	= 3,
};

static struct ads7846_platform_data spitz_ads7846_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.gpio_pendown		= SPITZ_GPIO_TP_INT,
};

static void spitz_ads7846_cs(u32 command)
{
	gpio_set_value(SPITZ_GPIO_ADS7846_CS, !(command == PXA2XX_CS_ASSERT));
}

static struct pxa2xx_spi_chip spitz_ads7846_chip = {
	.cs_control		= spitz_ads7846_cs,
};

static void spitz_notify_intensity(int intensity)
{
	if (machine_is_spitz() || machine_is_borzoi()) {
		gpio_set_value(SPITZ_GPIO_BACKLIGHT_CONT, !(intensity & 0x20));
		gpio_set_value(SPITZ_GPIO_BACKLIGHT_ON, intensity);
		return;
	}

	if (machine_is_akita()) {
		gpio_set_value(AKITA_GPIO_BACKLIGHT_CONT, !(intensity & 0x20));
		gpio_set_value(AKITA_GPIO_BACKLIGHT_ON, intensity);
		return;
	}
}

static void spitz_bl_kick_battery(void)
{
	void (*kick_batt)(void);

	kick_batt = symbol_get(sharpsl_battery_kick);
	if (kick_batt) {
		kick_batt();
		symbol_put(sharpsl_battery_kick);
	}
}

static struct corgi_lcd_platform_data spitz_lcdcon_info = {
	.init_mode		= CORGI_LCD_MODE_VGA,
	.max_intensity		= 0x2f,
	.default_intensity	= 0x1f,
	.limit_mask		= 0x0b,
	.notify			= spitz_notify_intensity,
	.kick_battery		= spitz_bl_kick_battery,
};

static void spitz_lcdcon_cs(u32 command)
{
	gpio_set_value(SPITZ_GPIO_LCDCON_CS, !(command == PXA2XX_CS_ASSERT));
}

static struct pxa2xx_spi_chip spitz_lcdcon_chip = {
	.cs_control	= spitz_lcdcon_cs,
};

static void spitz_max1111_cs(u32 command)
{
	gpio_set_value(SPITZ_GPIO_MAX1111_CS, !(command == PXA2XX_CS_ASSERT));
}

static struct pxa2xx_spi_chip spitz_max1111_chip = {
	.cs_control	= spitz_max1111_cs,
};

static struct spi_board_info spitz_spi_devices[] = {
	{
		.modalias	= "ads7846",
		.max_speed_hz	= 1200000,
		.bus_num	= 2,
		.chip_select	= 0,
		.platform_data	= &spitz_ads7846_info,
		.controller_data= &spitz_ads7846_chip,
		.irq		= gpio_to_irq(SPITZ_GPIO_TP_INT),
	}, {
		.modalias	= "corgi-lcd",
		.max_speed_hz	= 50000,
		.bus_num	= 2,
		.chip_select	= 1,
		.platform_data	= &spitz_lcdcon_info,
		.controller_data= &spitz_lcdcon_chip,
	}, {
		.modalias	= "max1111",
		.max_speed_hz	= 450000,
		.bus_num	= 2,
		.chip_select	= 2,
		.controller_data= &spitz_max1111_chip,
	},
};

static void __init spitz_init_spi(void)
{
	int err;

	err = gpio_request(SPITZ_GPIO_ADS7846_CS, "ADS7846_CS");
	if (err)
		return;

	err = gpio_request(SPITZ_GPIO_LCDCON_CS, "LCDCON_CS");
	if (err)
		goto err_free_1;

	err = gpio_request(SPITZ_GPIO_MAX1111_CS, "MAX1111_CS");
	if (err)
		goto err_free_2;

	pxa2xx_set_spi_info(2, &spitz_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(spitz_spi_devices));
	return;

err_free_2:
	gpio_free(SPITZ_GPIO_LCDCON_CS);
err_free_1:
	gpio_free(SPITZ_GPIO_ADS7846_CS);
}
#else
static inline void spitz_init_spi(void) {}
#endif

/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */

static struct pxamci_platform_data spitz_mci_platform_data;

static int spitz_mci_init(struct device *dev, irq_handler_t spitz_detect_int, void *data)
{
	int err;

	err = gpio_request(SPITZ_GPIO_nSD_DETECT, "nSD_DETECT");
	if (err)
		goto err_out;

	err = gpio_request(SPITZ_GPIO_nSD_WP, "nSD_WP");
	if (err)
		goto err_free_1;

	gpio_direction_input(SPITZ_GPIO_nSD_DETECT);
	gpio_direction_input(SPITZ_GPIO_nSD_WP);

	spitz_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(SPITZ_IRQ_GPIO_nSD_DETECT, spitz_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING |
			  IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err) {
		pr_err("%s: MMC/SD: can't request MMC card detect IRQ\n",
				__func__);
		goto err_free_2;
	}
	return 0;

err_free_2:
	gpio_free(SPITZ_GPIO_nSD_WP);
err_free_1:
	gpio_free(SPITZ_GPIO_nSD_DETECT);
err_out:
	return err;
}

static void spitz_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask)
		spitz_card_pwr_ctrl(SPITZ_PWR_SD, 0x0004);
	else
		spitz_card_pwr_ctrl(SPITZ_PWR_SD, 0x0000);
}

static int spitz_mci_get_ro(struct device *dev)
{
	return gpio_get_value(SPITZ_GPIO_nSD_WP);
}

static void spitz_mci_exit(struct device *dev, void *data)
{
	free_irq(SPITZ_IRQ_GPIO_nSD_DETECT, data);
	gpio_free(SPITZ_GPIO_nSD_WP);
	gpio_free(SPITZ_GPIO_nSD_DETECT);
}

static struct pxamci_platform_data spitz_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= spitz_mci_init,
	.get_ro		= spitz_mci_get_ro,
	.setpower 	= spitz_mci_setpower,
	.exit		= spitz_mci_exit,
};


/*
 * USB Host (OHCI)
 */
static int spitz_ohci_init(struct device *dev)
{
	int err;

	err = gpio_request(SPITZ_GPIO_USB_HOST, "USB_HOST");
	if (err)
		return err;

	/* Only Port 2 is connected
	 * Setup USB Port 2 Output Control Register
	 */
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;

	gpio_direction_output(SPITZ_GPIO_USB_HOST, 1);

	UHCHR = (UHCHR) &
		~(UHCHR_SSEP1 | UHCHR_SSEP2 | UHCHR_SSEP3 | UHCHR_SSE);

	UHCRHDA |= UHCRHDA_NOCP;

	return 0;
}

static struct pxaohci_platform_data spitz_ohci_platform_data = {
	.port_mode	= PMM_NPS_MODE,
	.init		= spitz_ohci_init,
	.power_budget	= 150,
};


/*
 * Irda
 */
static int spitz_irda_startup(struct device *dev)
{
	int rc;

	rc = gpio_request(SPITZ_GPIO_IR_ON, "IrDA on");
	if (rc)
		goto err;

	rc = gpio_direction_output(SPITZ_GPIO_IR_ON, 1);
	if (rc)
		goto err_dir;

	return 0;

err_dir:
	gpio_free(SPITZ_GPIO_IR_ON);
err:
	return rc;
}

static void spitz_irda_shutdown(struct device *dev)
{
	gpio_free(SPITZ_GPIO_IR_ON);
}

static void spitz_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(SPITZ_GPIO_IR_ON, mode & IR_OFF);
	pxa2xx_transceiver_mode(dev, mode);
}

#ifdef CONFIG_MACH_AKITA
static void akita_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(AKITA_GPIO_IR_ON, mode & IR_OFF);
	pxa2xx_transceiver_mode(dev, mode);
}
#endif

static struct pxaficp_platform_data spitz_ficp_platform_data = {
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
	.transceiver_mode	= spitz_irda_transceiver_mode,
	.startup		= spitz_irda_startup,
	.shutdown		= spitz_irda_shutdown,
};


/*
 * Spitz PXA Framebuffer
 */

static struct pxafb_mode_info spitz_pxafb_modes[] = {
{
	.pixclock       = 19231,
	.xres           = 480,
	.yres           = 640,
	.bpp            = 16,
	.hsync_len      = 40,
	.left_margin    = 46,
	.right_margin   = 125,
	.vsync_len      = 3,
	.upper_margin   = 1,
	.lower_margin   = 0,
	.sync           = 0,
},{
	.pixclock       = 134617,
	.xres           = 240,
	.yres           = 320,
	.bpp            = 16,
	.hsync_len      = 20,
	.left_margin    = 20,
	.right_margin   = 46,
	.vsync_len      = 2,
	.upper_margin   = 1,
	.lower_margin   = 0,
	.sync           = 0,
},
};

static struct pxafb_mach_info spitz_pxafb_info = {
	.modes          = &spitz_pxafb_modes[0],
	.num_modes      = 2,
	.fixed_modes    = 1,
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_ALTERNATE_MAPPING,
};


static struct platform_device *devices[] __initdata = {
	&spitzscoop_device,
	&spitzkbd_device,
	&spitzled_device,
};

static void spitz_poweroff(void)
{
	arm_machine_restart('g');
}

static void spitz_restart(char mode)
{
	/* Bootloader magic for a reboot */
	if((MSC0 & 0xffff0000) == 0x7ff00000)
		MSC0 = (MSC0 & 0xffff) | 0x7ee00000;

	spitz_poweroff();
}

static void __init common_init(void)
{
	init_gpio_reset(SPITZ_GPIO_ON_RESET);
	pm_power_off = spitz_poweroff;
	arm_pm_restart = spitz_restart;

	PMCR = 0x00;

	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(spitz_pin_config));

	spitz_init_spi();

	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_mci_info(&spitz_mci_platform_data);
	pxa_set_ohci_info(&spitz_ohci_platform_data);
	pxa_set_ficp_info(&spitz_ficp_platform_data);
	set_pxa_fb_info(&spitz_pxafb_info);
	pxa_set_i2c_info(NULL);
}

#if defined(CONFIG_MACH_SPITZ) || defined(CONFIG_MACH_BORZOI)
static void __init spitz_init(void)
{
	platform_scoop_config = &spitz_pcmcia_config;

	common_init();

	platform_device_register(&spitzscoop2_device);
}
#endif

#ifdef CONFIG_MACH_AKITA
/*
 * Akita IO Expander
 */
static struct pca953x_platform_data akita_ioexp = {
	.gpio_base		= AKITA_IOEXP_GPIO_BASE,
};

static struct i2c_board_info akita_i2c_board_info[] = {
	{
		.type		= "max7310",
		.addr		= 0x18,
		.platform_data	= &akita_ioexp,
	},
};

static void __init akita_init(void)
{
	spitz_ficp_platform_data.transceiver_mode = akita_irda_transceiver_mode;

	/* We just pretend the second element of the array doesn't exist */
	spitz_pcmcia_config.num_devs = 1;
	platform_scoop_config = &spitz_pcmcia_config;

	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(akita_i2c_board_info));

	common_init();
}
#endif

static void __init fixup_spitz(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
	mi->nr_banks = 1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	mi->bank[0].size = (64*1024*1024);
}

#ifdef CONFIG_MACH_SPITZ
MACHINE_START(SPITZ, "SHARP Spitz")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_spitz,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.init_machine	= spitz_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_BORZOI
MACHINE_START(BORZOI, "SHARP Borzoi")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_spitz,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.init_machine	= spitz_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_AKITA
MACHINE_START(AKITA, "SHARP Akita")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_spitz,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.init_machine	= akita_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif
