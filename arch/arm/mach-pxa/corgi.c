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
#include <linux/backlight.h>
#include <video/w100fb.h>

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
#include <mach/pxa2xx-gpio.h>
#include <mach/irda.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/corgi.h>
#include <mach/sharpsl.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
#include "devices.h"
#include "sharpsl.h"


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

static void corgi_pcmcia_init(void)
{
	/* Setup default state of GPIO outputs
	   before we enable them as outputs. */
	GPSR(GPIO48_nPOE) = GPIO_bit(GPIO48_nPOE) |
		GPIO_bit(GPIO49_nPWE) | GPIO_bit(GPIO50_nPIOR) |
		GPIO_bit(GPIO51_nPIOW) | GPIO_bit(GPIO52_nPCE_1) |
		GPIO_bit(GPIO53_nPCE_2);

	pxa_gpio_mode(GPIO48_nPOE_MD);
	pxa_gpio_mode(GPIO49_nPWE_MD);
	pxa_gpio_mode(GPIO50_nPIOR_MD);
	pxa_gpio_mode(GPIO51_nPIOW_MD);
	pxa_gpio_mode(GPIO55_nPREG_MD);
	pxa_gpio_mode(GPIO56_nPWAIT_MD);
	pxa_gpio_mode(GPIO57_nIOIS16_MD);
	pxa_gpio_mode(GPIO52_nPCE_1_MD);
	pxa_gpio_mode(GPIO53_nPCE_2_MD);
	pxa_gpio_mode(GPIO54_pSKTSEL_MD);
}

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
	.pcmcia_init  = corgi_pcmcia_init,
};

EXPORT_SYMBOL(corgiscoop_device);


/*
 * Corgi SSP Device
 *
 * Set the parent as the scoop device because a lot of SSP devices
 * also use scoop functions and this makes the power up/down order
 * work correctly.
 */
struct platform_device corgissp_device = {
	.name		= "corgi-ssp",
	.dev		= {
 		.parent = &corgiscoop_device.dev,
	},
	.id		= -1,
};

struct corgissp_machinfo corgi_ssp_machinfo = {
	.port		= 1,
	.cs_lcdcon	= CORGI_GPIO_LCDCON_CS,
	.cs_ads7846	= CORGI_GPIO_ADS7846_CS,
	.cs_max1111	= CORGI_GPIO_MAX1111_CS,
	.clk_lcdcon	= 76,
	.clk_ads7846	= 2,
	.clk_max1111	= 8,
};


/*
 * LCD/Framebuffer
 */
static void w100_lcdtg_suspend(struct w100fb_par *par)
{
	corgi_lcdtg_suspend();
}

static void w100_lcdtg_init(struct w100fb_par *par)
{
	corgi_lcdtg_hw_init(par->xres);
}


static struct w100_tg_info corgi_lcdtg_info = {
	.change  = w100_lcdtg_init,
	.suspend = w100_lcdtg_suspend,
	.resume  = w100_lcdtg_init,
};

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
	.tg         = &corgi_lcdtg_info,
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
		.parent = &corgissp_device.dev,
	},

};


/*
 * Corgi Backlight Device
 */
static void corgi_bl_kick_battery(void)
{
	void (*kick_batt)(void);

	kick_batt = symbol_get(sharpsl_battery_kick);
	if (kick_batt) {
		kick_batt();
		symbol_put(sharpsl_battery_kick);
	}
}

static void corgi_bl_set_intensity(int intensity)
{
	if (intensity > 0x10)
		intensity += 0x10;

	/* Bits 0-4 are accessed via the SSP interface */
	corgi_ssp_blduty_set(intensity & 0x1f);

	/* Bit 5 is via SCOOP */
	if (intensity & 0x0020)
		set_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_BACKLIGHT_CONT);
	else
		reset_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_BACKLIGHT_CONT);
}

static struct generic_bl_info corgi_bl_machinfo = {
	.name = "corgi-bl",
	.max_intensity = 0x2f,
	.default_intensity = 0x1f,
	.limit_mask = 0x0b,
	.set_bl_intensity = corgi_bl_set_intensity,
	.kick_battery = corgi_bl_kick_battery,
};

static struct platform_device corgibl_device = {
	.name		= "generic-bl",
	.dev		= {
 		.parent = &corgifb_device.dev,
		.platform_data	= &corgi_bl_machinfo,
	},
	.id		= -1,
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
static struct platform_device corgiled_device = {
	.name		= "corgi-led",
	.id		= -1,
};


/*
 * Corgi Touch Screen Device
 */
static unsigned long (*get_hsync_invperiod)(struct device *dev);

static void inline sharpsl_wait_sync(int gpio)
{
	while((GPLR(gpio) & GPIO_bit(gpio)) == 0);
	while((GPLR(gpio) & GPIO_bit(gpio)) != 0);
}

static unsigned long corgi_get_hsync_invperiod(void)
{
	if (!get_hsync_invperiod)
		get_hsync_invperiod = symbol_get(w100fb_get_hsynclen);
	if (!get_hsync_invperiod)
		return 0;

	return get_hsync_invperiod(&corgifb_device.dev);
}

static void corgi_put_hsync(void)
{
	if (get_hsync_invperiod)
		symbol_put(w100fb_get_hsynclen);
	get_hsync_invperiod = NULL;
}

static void corgi_wait_hsync(void)
{
	sharpsl_wait_sync(CORGI_GPIO_HSYNC);
}

static struct resource corgits_resources[] = {
	[0] = {
		.start		= CORGI_IRQ_GPIO_TP_INT,
		.end		= CORGI_IRQ_GPIO_TP_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct corgits_machinfo  corgi_ts_machinfo = {
	.get_hsync_invperiod = corgi_get_hsync_invperiod,
	.put_hsync           = corgi_put_hsync,
	.wait_hsync          = corgi_wait_hsync,
};

static struct platform_device corgits_device = {
	.name		= "corgi-ts",
	.dev		= {
 		.parent = &corgissp_device.dev,
		.platform_data	= &corgi_ts_machinfo,
	},
	.id		= -1,
	.num_resources	= ARRAY_SIZE(corgits_resources),
	.resource	= corgits_resources,
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

	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);
	pxa_gpio_mode(CORGI_GPIO_nSD_DETECT | GPIO_IN);
	pxa_gpio_mode(CORGI_GPIO_SD_PWR | GPIO_OUT);

	corgi_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(CORGI_IRQ_GPIO_nSD_DETECT, corgi_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err)
		printk(KERN_ERR "corgi_mci_init: MMC/SD: can't request MMC card detect IRQ\n");

	return err;
}

static void corgi_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask)
		GPSR1 = GPIO_bit(CORGI_GPIO_SD_PWR);
	else
		GPCR1 = GPIO_bit(CORGI_GPIO_SD_PWR);
}

static int corgi_mci_get_ro(struct device *dev)
{
	return GPLR(CORGI_GPIO_nSD_WP) & GPIO_bit(CORGI_GPIO_nSD_WP);
}

static void corgi_mci_exit(struct device *dev, void *data)
{
	free_irq(CORGI_IRQ_GPIO_nSD_DETECT, data);
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
	if (mode & IR_OFF)
		GPSR(CORGI_GPIO_IR_ON) = GPIO_bit(CORGI_GPIO_IR_ON);
	else
		GPCR(CORGI_GPIO_IR_ON) = GPIO_bit(CORGI_GPIO_IR_ON);
	pxa2xx_transceiver_mode(dev, mode);
}

static struct pxaficp_platform_data corgi_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = corgi_irda_transceiver_mode,
};


/*
 * USB Device Controller
 */
static struct pxa2xx_udc_mach_info udc_info __initdata = {
	/* no connect GPIO; corgi can't tell connection status */
	.gpio_pullup		= CORGI_GPIO_USB_PULLUP,
};


static struct platform_device *devices[] __initdata = {
	&corgiscoop_device,
	&corgissp_device,
	&corgifb_device,
	&corgikbd_device,
	&corgibl_device,
	&corgits_device,
	&corgiled_device,
};

static void corgi_poweroff(void)
{
	if (!machine_is_corgi())
		/* Green LED off tells the bootloader to halt */
		reset_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_LED_GREEN);
	arm_machine_restart('h');
}

static void corgi_restart(char mode)
{
	if (!machine_is_corgi())
		/* Green LED on tells the bootloader to reboot */
		set_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_LED_GREEN);
	arm_machine_restart('h');
}

static void __init corgi_init(void)
{
	pm_power_off = corgi_poweroff;
	arm_pm_restart = corgi_restart;

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x0158C000;
	PGSR1 = 0x00FF0080;
	PGSR2 = 0x0001C004;
	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;

	corgi_ssp_set_machinfo(&corgi_ssp_machinfo);

	pxa_gpio_mode(CORGI_GPIO_IR_ON | GPIO_OUT);
	pxa_gpio_mode(CORGI_GPIO_HSYNC | GPIO_IN);

 	pxa_set_udc_info(&udc_info);
	pxa_set_mci_info(&corgi_mci_platform_data);
	pxa_set_ficp_info(&corgi_ficp_platform_data);

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

