/*
 * linux/arch/arm/mach-s3c24xx/mach-mini2451.c
 *
 * Copyright (c) 2015 FriendlyARM (www.arm9.net)
 *
 * based on mach-smdk2416.c
 *
 * Copyright (c) 2009 Yauhen Kharuzhy <jekhor@gmail.com>,
 *	as part of OpenInkpot project
 * Copyright (c) 2009 Promwad Innovation Company
 *	Yauhen Kharuzhy <yauhen.kharuzhy@promwad.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mtd/partitions.h>
#include <linux/gpio.h>
#include <linux/fb.h>
#include <linux/delay.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <video/samsung_fimd.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/regs-gpio.h>
#include <mach/regs-lcd.h>
#include <mach/regs-s3c2443-clock.h>
#include <mach/gpio-samsung.h>

#include <linux/platform_data/leds-s3c24xx.h>
#include <linux/platform_data/i2c-s3c2410.h>

#include <plat/gpio-cfg.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/pm.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>
#include <linux/mmc/host.h>
#include <plat/sdhci.h>
#include <linux/platform_data/usb-ohci-s3c2410.h>
#include <linux/platform_data/usb-s3c2410_udc.h>
#include <linux/platform_data/s3c-hsudc.h>
#include <linux/platform_data/touchscreen-one-wire.h>
#include <plat/samsung-time.h>

#include <plat/fb.h>
#include <mach/s3cfb.h>
#include <mach/board-wlan.h>

#include "common.h"
#if defined(CONFIG_S3C24XX_SMDK)
#include "common-smdk.h"
#endif

static struct map_desc mini2451_iodesc[] __initdata = {
	/* ISA IO Space map (memory space selected by A24) */

	{
		.virtual	= (u32)S3C24XX_VA_ISA_WORD,
		.pfn		= __phys_to_pfn(S3C2410_CS2),
		.length		= 0x10000,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_WORD + 0x10000,
		.pfn		= __phys_to_pfn(S3C2410_CS2 + (1<<24)),
		.length		= SZ_4M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_BYTE,
		.pfn		= __phys_to_pfn(S3C2410_CS2),
		.length		= 0x10000,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_BYTE + 0x10000,
		.pfn		= __phys_to_pfn(S3C2410_CS2 + (1<<24)),
		.length		= SZ_4M,
		.type		= MT_DEVICE,
	}
};

#define UCON (S3C2410_UCON_DEFAULT	| \
		S3C2440_UCON_PCLK	| \
		S3C2443_UCON_RXERR_IRQEN)

#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE)

#define UFCON (S3C2410_UFCON_RXTRIG8	| \
		S3C2410_UFCON_FIFOMODE	| \
		S3C2440_UFCON_TXTRIG16)

static struct s3c2410_uartcfg mini2451_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
#ifdef CONFIG_IRDA
		.ulcon	     = ULCON | 0x50,
#else
		.ulcon	     = ULCON,
#endif
		.ufcon	     = UFCON,
	},
	[3] = {
		.hwport	     = 3,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	}
};

/* USB 1.1 HOST 1 Port */
static struct s3c2410_hcd_info mini2451_hcd_info __initdata = {
	.num_ports		= 1,
};

static void mini2451_hsudc_gpio_init(void)
{
	s3c2410_modify_misccr(S3C2416_MISCCR_SEL_SUSPND, 0);
}

static void mini2451_hsudc_gpio_uninit(void)
{
	s3c2410_modify_misccr(S3C2416_MISCCR_SEL_SUSPND, 1);
}

static void mini2451_hsudc_phy_init(void)
{
#define HSUDC_RESET		((1 << 2) | (1 << 0))
	u32 cfg;

	cfg = readl(S3C2443_PWRCFG) | S3C2443_PWRCFG_USBPHY;
	writel(cfg, S3C2443_PWRCFG);
	udelay(5);

	writel(0, S3C2443_PHYCTRL);

	cfg = readl(S3C2443_PHYPWR) | (1 << 31);
	writel(cfg, S3C2443_PHYPWR);

	cfg = readl(S3C2443_UCLKCON);
	cfg |= (1 << 31) | (1 << 2);
	writel(cfg, S3C2443_UCLKCON);
	udelay(5);

	cfg = readl(S3C2443_URSTCON);
	cfg |= HSUDC_RESET;
	writel(cfg, S3C2443_URSTCON);
	mdelay(1);

	cfg = readl(S3C2443_URSTCON);
	cfg &= ~HSUDC_RESET;
	writel(cfg, S3C2443_URSTCON);
	udelay(5);
}

/* USB 2.0 Device 1 Port */
static struct s3c24xx_hsudc_platdata mini2451_hsudc_platdata = {
	.epnum		= 9,
	.gpio_init	= mini2451_hsudc_gpio_init,
	.gpio_uninit	= mini2451_hsudc_gpio_uninit,
	.phy_init	= mini2451_hsudc_phy_init,
};

/* Framebuffer */
static struct s3c_fb_pd_win mini2451_fb_win[] = {
	[0] = {
		.default_bpp	= 16,
		.max_bpp	= 32,
		.xres           = 800,
		.yres           = 480,
	},
};

static struct fb_videomode mini2451_lcd_timing = {
	.pixclock	= 41094,
	.left_margin	= 8,
	.right_margin	= 13,
	.upper_margin	= 7,
	.lower_margin	= 5,
	.hsync_len	= 3,
	.vsync_len	= 1,
	.xres           = 800,
	.yres           = 480,
};

static void s3c2416_fb_gpio_setup_24bpp(void)
{
	unsigned int gpio;

	for (gpio = S3C2410_GPC(1); gpio <= S3C2410_GPC(4); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	for (gpio = S3C2410_GPC(8); gpio <= S3C2410_GPC(15); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	for (gpio = S3C2410_GPD(0); gpio <= S3C2410_GPD(15); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}
}

static struct s3c_fb_platdata mini2451_fb_platdata = {
	.win[0]		= &mini2451_fb_win[0],
	.vtiming	= &mini2451_lcd_timing,
	.setup_gpio	= s3c2416_fb_gpio_setup_24bpp,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
};

static void __init mini2451_fb_init_pdata(struct s3c_fb_platdata *pd) {
	struct s3cfb_lcd *lcd;
	struct s3c_fb_pd_win *win = pd->win[0];
	struct fb_videomode *mode = pd->vtiming;
	unsigned long val = 0;
	u64 pixclk = 1000000000000ULL;
	u32 div;

	lcd = mini2451_get_lcd();

	win->default_bpp	= lcd->bpp < 25 ? lcd->bpp : 24;
	win->xres			= lcd->width;
	win->yres			= lcd->height;

	mode->left_margin	= lcd->timing.h_bp;
	mode->right_margin	= lcd->timing.h_fp;
	mode->upper_margin	= lcd->timing.v_bp;
	mode->lower_margin	= lcd->timing.v_fp;
	mode->hsync_len		= lcd->timing.h_sw;
	mode->vsync_len		= lcd->timing.v_sw;
	mode->xres			= lcd->width;
	mode->yres			= lcd->height;

	/* calculates pixel clock */
	div  = mode->left_margin + mode->hsync_len + mode->right_margin +
		mode->xres;
	div *= mode->upper_margin + mode->vsync_len + mode->lower_margin +
		mode->yres;
	div *= lcd->freq ? : 60;

	do_div(pixclk, div);

	mode->pixclock		= pixclk;

	/* initialize signal polarity of RGB interface */
	if (lcd->polarity.rise_vclk)
		val |= VIDCON1_INV_VCLK;
	if (lcd->polarity.inv_hsync)
		val |= VIDCON1_INV_HSYNC;
	if (lcd->polarity.inv_vsync)
		val |= VIDCON1_INV_VSYNC;
	if (lcd->polarity.inv_vden)
		val |= VIDCON1_INV_VDEN;

	pd->vidcon1 = val;
}

#if defined(CONFIG_BCMDHD)
/* Broadcom WI-FI support */
static DEFINE_MUTEX(notify_lock);

#define DEFINE_MMC_CARD_NOTIFIER(num) \
static void (*hsmmc##num##_notify_func)(struct platform_device *, int state); \
static int ext_cd_init_hsmmc##num(void (*notify_func)( \
			struct platform_device *, int state)) \
{ \
	mutex_lock(&notify_lock); \
	WARN_ON(hsmmc##num##_notify_func); \
	hsmmc##num##_notify_func = notify_func; \
	mutex_unlock(&notify_lock); \
	return 0; \
} \
static int ext_cd_cleanup_hsmmc##num(void (*notify_func)( \
			struct platform_device *, int state)) \
{ \
	mutex_lock(&notify_lock); \
	WARN_ON(hsmmc##num##_notify_func != notify_func); \
	hsmmc##num##_notify_func = NULL; \
	mutex_unlock(&notify_lock); \
	return 0; \
}

#ifdef CONFIG_S3C_DEV_HSMMC
	DEFINE_MMC_CARD_NOTIFIER(0)
#endif

/*
 * call this when you need sd stack to recognize insertion or removal of card
 * that can't be told by SDHCI regs
 */
void mmc_force_presence_change_onoff(struct platform_device *pdev, int val)
{
	void (*notify_func)(struct platform_device *, int state) = NULL;
	mutex_lock(&notify_lock);
#ifdef CONFIG_S3C_DEV_HSMMC
	if (pdev == &s3c_device_hsmmc0)
		notify_func = hsmmc0_notify_func;
#endif

	if (notify_func)
		notify_func(pdev, val);
	else
		pr_warn("%s: called for device with no notifier\n", __func__);
	mutex_unlock(&notify_lock);
}
EXPORT_SYMBOL_GPL(mmc_force_presence_change_onoff);
#endif

/* HSMMC */
static struct s3c_sdhci_platdata mini2451_hsmmc0_pdata __initdata = {
	.max_width		= 4,
#if defined(CONFIG_BCMDHD)
	.cd_type        = S3C_SDHCI_CD_EXTERNAL,
	.pm_flags       = MMC_PM_KEEP_POWER | MMC_PM_IGNORE_PM_NOTIFY,
	.ext_cd_init    = ext_cd_init_hsmmc0,
	.ext_cd_cleanup = ext_cd_cleanup_hsmmc0,
#else
	.cd_type		= S3C_SDHCI_CD_NONE,
#endif
};

static int mini2451_hsmmc1_get_ro(void *host) {
	return 0;
}

static struct s3c_sdhci_platdata mini2451_hsmmc1_pdata __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_NONE,
	.get_ro			= mini2451_hsmmc1_get_ro,

	/* FIXME: Malfunction */
#if 0
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= S3C2410_GPG(0),
	.ext_cd_gpio_invert	= 1,
#endif
};

static void __init mini2451_hsmmc_gpio_setup(void)
{
	/* NC pins on NanoPi */
	s3c_gpio_setpull(S3C2410_GPJ(15), S3C_GPIO_PULL_UP);
	s3c_gpio_cfgrange_nopull(S3C2410_GPJ(13), 3, S3C_GPIO_SFN(2));
}

static void __init mini2451_wifi_init(void)
{
#if defined(CONFIG_BRCMFMAC_SDIO)
	WARN_ON(gpio_request(GPIO_WLAN_EN, "WIFI_EN"));

	gpio_direction_output(GPIO_WLAN_EN, 1);
	udelay(50);
	gpio_free(GPIO_WLAN_EN);
#endif
}

#ifdef CONFIG_S3C64XX_DEV_SPI0
#include <linux/spi/spi.h>
#include <linux/platform_data/spi-s3c64xx.h>

static struct s3c64xx_spi_csinfo spi0_csi = {
	.line		= S3C2410_GPL(13),
	.fb_delay	= 0x2,
};

static struct spi_board_info spi0_board_info[] __initdata = {
#ifdef CONFIG_SPI_SPIDEV
	{
		.modalias		= "spidev",
		.max_speed_hz	= 10000000,
		.bus_num		= 0,
		.chip_select	= 0,
		.mode			= SPI_MODE_0,
		.controller_data= &spi0_csi,
	},
#endif
};
#endif

/* LEDs */
static struct s3c24xx_led_platdata mini2451_led1_pdata = {
	.name		= "led1",
	.gpio		= S3C2410_GPB(5),
	.flags		= S3C24XX_LEDF_ACTLOW | S3C24XX_LEDF_TRISTATE,
	.def_trigger	= "heartbeat",
};

static struct platform_device mini2451_led1 = {
	.name		= "s3c24xx_led",
	.id			= 1,
	.dev		= {
		.platform_data  = &mini2451_led1_pdata,
	},
};

static struct ts_onewire_platform_data mini2451_1wire_pdata = {
	.timer_irq	= IRQ_TIMER2,
	.pwm_id		= 2,
	.gpio		= S3C2410_GPG(2),
};

static struct platform_device mini2451_1wire = {
	.name		= "mini2451-1wire",
	.id			= -1,
	.dev		= {
		.platform_data	= &mini2451_1wire_pdata,
	},
};

static struct platform_device *mini2451_devices[] __initdata = {
	&s3c_device_fb,
	&s3c_device_hsmmc1,
	&s3c_device_i2c0,
	&s3c_device_wdt,
	&s3c_device_usb_hsudc,
	&s3c_device_ohci,
	&s3c_device_rtc,
	&s3c_device_hsmmc0,
	&s3c2443_device_dma,
#ifdef CONFIG_S3C64XX_DEV_SPI0
	&s3c64xx_device_spi0,
#endif
	&samsung_device_pwm,
	&mini2451_led1,
	&mini2451_1wire,
};

static void __init mini2451_init_time(void)
{
	s3c2416_init_clocks(12000000);
	samsung_timer_init();
}

static void __init mini2451_map_io(void)
{
	s3c24xx_init_io(mini2451_iodesc, ARRAY_SIZE(mini2451_iodesc));
	s3c24xx_init_uarts(mini2451_uartcfgs, ARRAY_SIZE(mini2451_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
	/* Set prescaler & div for Timer 2/3/4 */
	samsung_set_timer_div(56, 2);
}

static void __init mini2451_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);

	mini2451_fb_init_pdata(&mini2451_fb_platdata);
	s3c_fb_set_platdata(&mini2451_fb_platdata);

	s3c_sdhci0_set_platdata(&mini2451_hsmmc0_pdata);
	s3c_sdhci1_set_platdata(&mini2451_hsmmc1_pdata);

	mini2451_hsmmc_gpio_setup();
	mini2451_wifi_init();

	s3c_ohci_set_platdata(&mini2451_hcd_info);
	s3c24xx_hsudc_set_platdata(&mini2451_hsudc_platdata);

#ifdef CONFIG_S3C64XX_DEV_SPI0
	s3c64xx_spi0_set_platdata(NULL, 0, 1);
	spi_register_board_info(spi0_board_info,
			ARRAY_SIZE(spi0_board_info));
#endif

	platform_add_devices(mini2451_devices, ARRAY_SIZE(mini2451_devices));

#if defined(CONFIG_S3C24XX_SMDK)
	smdk_machine_init();
#else
	s3c_pm_init();
#endif
}

MACHINE_START(MINI2451, "MINI2451")
	/* Maintainer: FriendlyARM (www.arm9.net) */
	.atag_offset	= 0x100,

	.init_irq	= s3c2416_init_irq,
	.map_io		= mini2451_map_io,
	.init_machine	= mini2451_machine_init,
	.init_time	= mini2451_init_time,
MACHINE_END
