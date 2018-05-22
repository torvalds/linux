/*
 *  arch/arm/mach-pxa/pcm990-baseboard.c
 *  Support for the Phytec phyCORE-PXA270 Development Platform (PCM-990).
 *
 *  Refer
 *   http://www.phytec.com/products/rdk/ARM-XScale/phyCORE-XScale-PXA270.html
 *  for additional hardware info
 *
 *  Author:	Juergen Kilb
 *  Created:	April 05, 2005
 *  Copyright:	Phytec Messtechnik GmbH
 *  e-Mail:	armlinux@phytec.de
 *
 *  based on Intel Mainstone Board
 *
 *  Copyright 2007 Juergen Beisert @ Pengutronix (j.beisert@pengutronix.de)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/platform_data/i2c-pxa.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>

#include <media/i2c/mt9v022.h>
#include <media/soc_camera.h>

#include <linux/platform_data/media/camera-pxa.h>
#include <asm/mach/map.h>
#include "pxa27x.h"
#include <mach/audio.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include "pcm990_baseboard.h"
#include <linux/platform_data/video-pxafb.h>

#include "devices.h"
#include "generic.h"

static unsigned long pcm990_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	/* USB */
	GPIO88_USBH1_PWR,
	GPIO89_USBH1_PEN,
	/* PWM0 */
	GPIO16_PWM0_OUT,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
};

static void __iomem *pcm990_cpld_base;

static u8 pcm990_cpld_readb(unsigned int reg)
{
	return readb(pcm990_cpld_base + reg);
}

static void pcm990_cpld_writeb(u8 value, unsigned int reg)
{
	writeb(value, pcm990_cpld_base + reg);
}

/*
 * pcm990_lcd_power - control power supply to the LCD
 * @on: 0 = switch off, 1 = switch on
 *
 * Called by the pxafb driver
 */
#ifndef CONFIG_PCM990_DISPLAY_NONE
static void pcm990_lcd_power(int on, struct fb_var_screeninfo *var)
{
	if (on) {
		/* enable LCD-Latches
		 * power on LCD
		 */
		pcm990_cpld_writeb(PCM990_CTRL_LCDPWR + PCM990_CTRL_LCDON,
				PCM990_CTRL_REG3);
	} else {
		/* disable LCD-Latches
		 * power off LCD
		 */
		pcm990_cpld_writeb(0, PCM990_CTRL_REG3);
	}
}
#endif

#if defined(CONFIG_PCM990_DISPLAY_SHARP)
static struct pxafb_mode_info fb_info_sharp_lq084v1dg21 = {
	.pixclock		= 28000,
	.xres			= 640,
	.yres			= 480,
	.bpp			= 16,
	.hsync_len		= 20,
	.left_margin		= 103,
	.right_margin		= 47,
	.vsync_len		= 6,
	.upper_margin		= 28,
	.lower_margin		= 5,
	.sync			= 0,
	.cmap_greyscale		= 0,
};

static struct pxafb_mach_info pcm990_fbinfo __initdata = {
	.modes			= &fb_info_sharp_lq084v1dg21,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_lcd_power	= pcm990_lcd_power,
};
#elif defined(CONFIG_PCM990_DISPLAY_NEC)
struct pxafb_mode_info fb_info_nec_nl6448bc20_18d = {
	.pixclock		= 39720,
	.xres			= 640,
	.yres			= 480,
	.bpp			= 16,
	.hsync_len		= 32,
	.left_margin		= 16,
	.right_margin		= 48,
	.vsync_len		= 2,
	.upper_margin		= 12,
	.lower_margin		= 17,
	.sync			= 0,
	.cmap_greyscale		= 0,
};

static struct pxafb_mach_info pcm990_fbinfo __initdata = {
	.modes			= &fb_info_nec_nl6448bc20_18d,
	.num_modes		= 1,
	.lcd_conn		= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_lcd_power	= pcm990_lcd_power,
};
#endif

static struct pwm_lookup pcm990_pwm_lookup[] = {
	PWM_LOOKUP("pxa27x-pwm.0", 0, "pwm-backlight.0", NULL, 78770,
		   PWM_POLARITY_NORMAL),
};

static struct platform_pwm_backlight_data pcm990_backlight_data = {
	.max_brightness	= 1023,
	.dft_brightness	= 1023,
	.enable_gpio	= -1,
};

static struct platform_device pcm990_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent = &pxa27x_device_pwm0.dev,
		.platform_data = &pcm990_backlight_data,
	},
};

/*
 * The PCM-990 development baseboard uses PCM-027's hardware in the
 * following way:
 *
 * - LCD support is in use
 *  - GPIO16 is output for back light on/off with PWM
 *  - GPIO58 ... GPIO73 are outputs for display data
 *  - GPIO74 is output output for LCDFCLK
 *  - GPIO75 is output for LCDLCLK
 *  - GPIO76 is output for LCDPCLK
 *  - GPIO77 is output for LCDBIAS
 * - MMC support is in use
 *  - GPIO32 is output for MMCCLK
 *  - GPIO92 is MMDAT0
 *  - GPIO109 is MMDAT1
 *  - GPIO110 is MMCS0
 *  - GPIO111 is MMCS1
 *  - GPIO112 is MMCMD
 * - IDE/CF card is in use
 *  - GPIO48 is output /POE
 *  - GPIO49 is output /PWE
 *  - GPIO50 is output /PIOR
 *  - GPIO51 is output /PIOW
 *  - GPIO54 is output /PCE2
 *  - GPIO55 is output /PREG
 *  - GPIO56 is input /PWAIT
 *  - GPIO57 is output /PIOS16
 *  - GPIO79 is output PSKTSEL
 *  - GPIO85 is output /PCE1
 * - FFUART is in use
 *  - GPIO34 is input FFRXD
 *  - GPIO35 is input FFCTS
 *  - GPIO36 is input FFDCD
 *  - GPIO37 is input FFDSR
 *  - GPIO38 is input FFRI
 *  - GPIO39 is output FFTXD
 *  - GPIO40 is output FFDTR
 *  - GPIO41 is output FFRTS
 * - BTUART is in use
 *  - GPIO42 is input BTRXD
 *  - GPIO43 is output BTTXD
 *  - GPIO44 is input BTCTS
 *  - GPIO45 is output BTRTS
 * - IRUART is in use
 *  - GPIO46 is input STDRXD
 *  - GPIO47 is output STDTXD
 * - AC97 is in use*)
 *  - GPIO28 is input AC97CLK
 *  - GPIO29 is input AC97DatIn
 *  - GPIO30 is output AC97DatO
 *  - GPIO31 is output AC97SYNC
 *  - GPIO113 is output AC97_RESET
 * - SSP is in use
 *  - GPIO23 is output SSPSCLK
 *  - GPIO24 is output chip select to Max7301
 *  - GPIO25 is output SSPTXD
 *  - GPIO26 is input SSPRXD
 *  - GPIO27 is input for Max7301 IRQ
 *  - GPIO53 is input SSPSYSCLK
 * - SSP3 is in use
 *  - GPIO81 is output SSPTXD3
 *  - GPIO82 is input SSPRXD3
 *  - GPIO83 is output SSPSFRM
 *  - GPIO84 is output SSPCLK3
 *
 * Otherwise claimed GPIOs:
 * GPIO1 -> IRQ from user switch
 * GPIO9 -> IRQ from power management
 * GPIO10 -> IRQ from WML9712 AC97 controller
 * GPIO11 -> IRQ from IDE controller
 * GPIO12 -> IRQ from CF controller
 * GPIO13 -> IRQ from CF controller
 * GPIO14 -> GPIO free
 * GPIO15 -> /CS1 selects baseboard's Control CPLD (U7, 16 bit wide data path)
 * GPIO19 -> GPIO free
 * GPIO20 -> /SDCS2
 * GPIO21 -> /CS3 PC card socket select
 * GPIO33 -> /CS5  network controller select
 * GPIO78 -> /CS2  (16 bit wide data path)
 * GPIO80 -> /CS4  (16 bit wide data path)
 * GPIO86 -> GPIO free
 * GPIO87 -> GPIO free
 * GPIO90 -> LED0 on CPU module
 * GPIO91 -> LED1 on CPI module
 * GPIO117 -> SCL
 * GPIO118 -> SDA
 */

static unsigned long pcm990_irq_enabled;

static void pcm990_mask_ack_irq(struct irq_data *d)
{
	int pcm990_irq = (d->irq - PCM027_IRQ(0));

	pcm990_irq_enabled &= ~(1 << pcm990_irq);

	pcm990_cpld_writeb(pcm990_irq_enabled, PCM990_CTRL_INTMSKENA);
}

static void pcm990_unmask_irq(struct irq_data *d)
{
	int pcm990_irq = (d->irq - PCM027_IRQ(0));
	u8 val;

	/* the irq can be acknowledged only if deasserted, so it's done here */

	pcm990_irq_enabled |= (1 << pcm990_irq);

	val = pcm990_cpld_readb(PCM990_CTRL_INTSETCLR);
	val |= 1 << pcm990_irq;
	pcm990_cpld_writeb(val, PCM990_CTRL_INTSETCLR);

	pcm990_cpld_writeb(pcm990_irq_enabled, PCM990_CTRL_INTMSKENA);
}

static struct irq_chip pcm990_irq_chip = {
	.irq_mask_ack	= pcm990_mask_ack_irq,
	.irq_unmask	= pcm990_unmask_irq,
};

static void pcm990_irq_handler(struct irq_desc *desc)
{
	unsigned int irq;
	unsigned long pending;

	pending = ~pcm990_cpld_readb(PCM990_CTRL_INTSETCLR);
	pending &= pcm990_irq_enabled;

	do {
		/* clear our parent IRQ */
		desc->irq_data.chip->irq_ack(&desc->irq_data);
		if (likely(pending)) {
			irq = PCM027_IRQ(0) + __ffs(pending);
			generic_handle_irq(irq);
		}
		pending = ~pcm990_cpld_readb(PCM990_CTRL_INTSETCLR);
		pending &= pcm990_irq_enabled;
	} while (pending);
}

static void __init pcm990_init_irq(void)
{
	int irq;

	/* setup extra PCM990 irqs */
	for (irq = PCM027_IRQ(0); irq <= PCM027_IRQ(3); irq++) {
		irq_set_chip_and_handler(irq, &pcm990_irq_chip,
					 handle_level_irq);
		irq_clear_status_flags(irq, IRQ_NOREQUEST | IRQ_NOPROBE);
	}

	/* disable all Interrupts */
	pcm990_cpld_writeb(0x0, PCM990_CTRL_INTMSKENA);
	pcm990_cpld_writeb(0xff, PCM990_CTRL_INTSETCLR);

	irq_set_chained_handler(PCM990_CTRL_INT_IRQ, pcm990_irq_handler);
	irq_set_irq_type(PCM990_CTRL_INT_IRQ, PCM990_CTRL_INT_IRQ_EDGE);
}

static int pcm990_mci_init(struct device *dev, irq_handler_t mci_detect_int,
			void *data)
{
	int err;

	err = request_irq(PCM027_MMCDET_IRQ, mci_detect_int, 0,
			     "MMC card detect", data);
	if (err)
		printk(KERN_ERR "pcm990_mci_init: MMC/SD: can't request MMC "
				"card detect IRQ\n");

	return err;
}

static int pcm990_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *p_d = dev->platform_data;
	u8 val;

	val = pcm990_cpld_readb(PCM990_CTRL_REG5);

	if ((1 << vdd) & p_d->ocr_mask)
		val |= PCM990_CTRL_MMC2PWR;
	else
		val &= ~PCM990_CTRL_MMC2PWR;

	pcm990_cpld_writeb(PCM990_CTRL_MMC2PWR, PCM990_CTRL_REG5);
	return 0;
}

static void pcm990_mci_exit(struct device *dev, void *data)
{
	free_irq(PCM027_MMCDET_IRQ, data);
}

#define MSECS_PER_JIFFY (1000/HZ)

static struct pxamci_platform_data pcm990_mci_platform_data = {
	.detect_delay_ms	= 250,
	.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34,
	.init 			= pcm990_mci_init,
	.setpower 		= pcm990_mci_setpower,
	.exit			= pcm990_mci_exit,
	.gpio_card_detect	= -1,
	.gpio_card_ro		= -1,
	.gpio_power		= -1,
};

static struct pxaohci_platform_data pcm990_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | POWER_CONTROL_LOW | POWER_SENSE_LOW,
	.power_on_delay	= 10,
};

/*
 * PXA27x Camera specific stuff
 */
#if defined(CONFIG_VIDEO_PXA27x) || defined(CONFIG_VIDEO_PXA27x_MODULE)
static unsigned long pcm990_camera_pin_config[] = {
	/* CIF */
	GPIO98_CIF_DD_0,
	GPIO105_CIF_DD_1,
	GPIO104_CIF_DD_2,
	GPIO103_CIF_DD_3,
	GPIO95_CIF_DD_4,
	GPIO94_CIF_DD_5,
	GPIO93_CIF_DD_6,
	GPIO108_CIF_DD_7,
	GPIO107_CIF_DD_8,
	GPIO106_CIF_DD_9,
	GPIO42_CIF_MCLK,
	GPIO45_CIF_PCLK,
	GPIO43_CIF_FV,
	GPIO44_CIF_LV,
};

/*
 * CICR4: PCLK_EN:	Pixel clock is supplied by the sensor
 *	MCLK_EN:	Master clock is generated by PXA
 *	PCP:		Data sampled on the falling edge of pixel clock
 */
struct pxacamera_platform_data pcm990_pxacamera_platform_data = {
	.flags  = PXA_CAMERA_MASTER | PXA_CAMERA_DATAWIDTH_8 | PXA_CAMERA_DATAWIDTH_10 |
		PXA_CAMERA_PCLK_EN | PXA_CAMERA_MCLK_EN/* | PXA_CAMERA_PCP*/,
	.mclk_10khz = 1000,
};

#include <linux/platform_data/pca953x.h>

static struct pca953x_platform_data pca9536_data = {
	.gpio_base	= PXA_NR_BUILTIN_GPIO,
};

static int gpio_bus_switch = -EINVAL;

static int pcm990_camera_set_bus_param(struct soc_camera_link *link,
				       unsigned long flags)
{
	if (gpio_bus_switch < 0) {
		if (flags == SOCAM_DATAWIDTH_10)
			return 0;
		else
			return -EINVAL;
	}

	if (flags & SOCAM_DATAWIDTH_8)
		gpio_set_value_cansleep(gpio_bus_switch, 1);
	else
		gpio_set_value_cansleep(gpio_bus_switch, 0);

	return 0;
}

static unsigned long pcm990_camera_query_bus_param(struct soc_camera_link *link)
{
	int ret;

	if (gpio_bus_switch < 0) {
		ret = gpio_request(PXA_NR_BUILTIN_GPIO, "camera");
		if (!ret) {
			gpio_bus_switch = PXA_NR_BUILTIN_GPIO;
			gpio_direction_output(gpio_bus_switch, 0);
		}
	}

	if (gpio_bus_switch >= 0)
		return SOCAM_DATAWIDTH_8 | SOCAM_DATAWIDTH_10;
	else
		return SOCAM_DATAWIDTH_10;
}

static void pcm990_camera_free_bus(struct soc_camera_link *link)
{
	if (gpio_bus_switch < 0)
		return;

	gpio_free(gpio_bus_switch);
	gpio_bus_switch = -EINVAL;
}

/* Board I2C devices. */
static struct i2c_board_info __initdata pcm990_i2c_devices[] = {
	{
		/* Must initialize before the camera(s) */
		I2C_BOARD_INFO("pca9536", 0x41),
		.platform_data = &pca9536_data,
	},
};

static struct mt9v022_platform_data mt9v022_pdata = {
	.y_skip_top = 1,
};

static struct i2c_board_info pcm990_camera_i2c[] = {
	{
		I2C_BOARD_INFO("mt9v022", 0x48),
	}, {
		I2C_BOARD_INFO("mt9m001", 0x5d),
	},
};

static struct soc_camera_link iclink[] = {
	{
		.bus_id			= 0, /* Must match with the camera ID */
		.board_info		= &pcm990_camera_i2c[0],
		.priv			= &mt9v022_pdata,
		.i2c_adapter_id		= 0,
		.query_bus_param	= pcm990_camera_query_bus_param,
		.set_bus_param		= pcm990_camera_set_bus_param,
		.free_bus		= pcm990_camera_free_bus,
	}, {
		.bus_id			= 0, /* Must match with the camera ID */
		.board_info		= &pcm990_camera_i2c[1],
		.i2c_adapter_id		= 0,
		.query_bus_param	= pcm990_camera_query_bus_param,
		.set_bus_param		= pcm990_camera_set_bus_param,
		.free_bus		= pcm990_camera_free_bus,
	},
};

static struct platform_device pcm990_camera[] = {
	{
		.name	= "soc-camera-pdrv",
		.id	= 0,
		.dev	= {
			.platform_data = &iclink[0],
		},
	}, {
		.name	= "soc-camera-pdrv",
		.id	= 1,
		.dev	= {
			.platform_data = &iclink[1],
		},
	},
};
#endif /* CONFIG_VIDEO_PXA27x ||CONFIG_VIDEO_PXA27x_MODULE */

/*
 * system init for baseboard usage. Will be called by pcm027 init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init pcm990_baseboard_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(pcm990_pin_config));

	pcm990_cpld_base = ioremap(PCM990_CTRL_PHYS, PCM990_CTRL_SIZE);
	if (!pcm990_cpld_base) {
		pr_err("pcm990: failed to ioremap cpld\n");
		return;
	}

	/* register CPLD's IRQ controller */
	pcm990_init_irq();

#ifndef CONFIG_PCM990_DISPLAY_NONE
	pxa_set_fb_info(NULL, &pcm990_fbinfo);
#endif
	pwm_add_table(pcm990_pwm_lookup, ARRAY_SIZE(pcm990_pwm_lookup));
	platform_device_register(&pcm990_backlight_device);

	/* MMC */
	pxa_set_mci_info(&pcm990_mci_platform_data);

	/* USB host */
	pxa_set_ohci_info(&pcm990_ohci_platform_data);

	pxa_set_i2c_info(NULL);
	pxa_set_ac97_info(NULL);

#if defined(CONFIG_VIDEO_PXA27x) || defined(CONFIG_VIDEO_PXA27x_MODULE)
	pxa2xx_mfp_config(ARRAY_AND_SIZE(pcm990_camera_pin_config));
	pxa_set_camera_info(&pcm990_pxacamera_platform_data);

	i2c_register_board_info(0, ARRAY_AND_SIZE(pcm990_i2c_devices));

	platform_device_register(&pcm990_camera[0]);
	platform_device_register(&pcm990_camera[1]);
#endif

	printk(KERN_INFO "PCM-990 Evaluation baseboard initialized\n");
}
