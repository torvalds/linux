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
#include <linux/device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mmc/host.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/irq.h>
#include <asm/arch/mmc.h>
#include <asm/arch/udc.h>
#include <asm/arch/corgi.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>
#include <video/w100fb.h>

#include "generic.h"


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


/*
 * Corgi SSP Device
 *
 * Set the parent as the scoop device because a lot of SSP devices
 * also use scoop functions and this makes the power up/down order
 * work correctly.
 */
static struct platform_device corgissp_device = {
	.name		= "corgi-ssp",
	.dev		= {
 		.parent = &corgiscoop_device.dev,
	},
	.id		= -1,
};


/*
 * Corgi w100 Frame Buffer Device
 */
static struct w100fb_mach_info corgi_fb_info = {
	.w100fb_ssp_send 	= corgi_ssp_lcdtg_send,
	.comadj 			= -1,
	.phadadj 			= -1,
};

static struct resource corgi_fb_resources[] = {
	[0] = {
		.start		= 0x08000000,
		.end		= 0x08ffffff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device corgifb_device = {
	.name		= "w100fb",
	.id		= -1,
	.dev		= {
 		.platform_data	= &corgi_fb_info,
 		.parent = &corgissp_device.dev,
	},
	.num_resources	= ARRAY_SIZE(corgi_fb_resources),
	.resource	= corgi_fb_resources,
};


/*
 * Corgi Backlight Device
 */
static struct platform_device corgibl_device = {
	.name		= "corgi-bl",
	.dev		= {
 		.parent = &corgifb_device.dev,
	},
	.id		= -1,
};


/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by HZ/4
 * to give the card a chance to fully insert/eject.
 */
static struct mmc_detect {
	struct timer_list detect_timer;
	void *devid;
} mmc_detect;

static void mmc_detect_callback(unsigned long data)
{
	mmc_detect_change(mmc_detect.devid);
}

static irqreturn_t corgi_mmc_detect_int(int irq, void *devid, struct pt_regs *regs)
{
	mmc_detect.devid=devid;
	mod_timer(&mmc_detect.detect_timer, jiffies + HZ/4);
	return IRQ_HANDLED;
}

static int corgi_mci_init(struct device *dev, irqreturn_t (*unused_detect_int)(int, void *, struct pt_regs *), void *data)
{
	int err;

	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);
	pxa_gpio_mode(CORGI_GPIO_nSD_DETECT | GPIO_IN);
	pxa_gpio_mode(CORGI_GPIO_SD_PWR | GPIO_OUT);

	init_timer(&mmc_detect.detect_timer);
	mmc_detect.detect_timer.function = mmc_detect_callback;
	mmc_detect.detect_timer.data = (unsigned long) &mmc_detect;

	err = request_irq(CORGI_IRQ_GPIO_nSD_DETECT, corgi_mmc_detect_int, SA_INTERRUPT,
			     "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "corgi_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		return -1;
	}

	set_irq_type(CORGI_IRQ_GPIO_nSD_DETECT, IRQT_BOTHEDGE);

	return 0;
}

static void corgi_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		printk(KERN_DEBUG "%s: on\n", __FUNCTION__);
		GPSR1 = GPIO_bit(CORGI_GPIO_SD_PWR);
	} else {
		printk(KERN_DEBUG "%s: off\n", __FUNCTION__);
		GPCR1 = GPIO_bit(CORGI_GPIO_SD_PWR);
	}
}

static void corgi_mci_exit(struct device *dev, void *data)
{
	free_irq(CORGI_IRQ_GPIO_nSD_DETECT, data);
	del_timer(&mmc_detect.detect_timer);
}

static struct pxamci_platform_data corgi_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= corgi_mci_init,
	.setpower 	= corgi_mci_setpower,
	.exit		= corgi_mci_exit,
};


/*
 * USB Device Controller
 */
static void corgi_udc_command(int cmd)
{
	switch(cmd)	{
	case PXA2XX_UDC_CMD_CONNECT:
		GPSR(CORGI_GPIO_USB_PULLUP) = GPIO_bit(CORGI_GPIO_USB_PULLUP);
		break;
	case PXA2XX_UDC_CMD_DISCONNECT:
		GPCR(CORGI_GPIO_USB_PULLUP) = GPIO_bit(CORGI_GPIO_USB_PULLUP);
		break;
	}
}

static struct pxa2xx_udc_mach_info udc_info __initdata = {
	/* no connect GPIO; corgi can't tell connection status */
	.udc_command		= corgi_udc_command,
};


static struct platform_device *devices[] __initdata = {
	&corgiscoop_device,
	&corgissp_device,
	&corgifb_device,
	&corgibl_device,
};

static void __init corgi_init(void)
{
	corgi_fb_info.comadj=sharpsl_param.comadj;
	corgi_fb_info.phadadj=sharpsl_param.phadadj;

	pxa_gpio_mode(CORGI_GPIO_USB_PULLUP | GPIO_OUT);
 	pxa_set_udc_info(&udc_info);
	pxa_set_mci_info(&corgi_mci_platform_data);

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

static void __init corgi_init_irq(void)
{
	pxa_init_irq();
}

static struct map_desc corgi_io_desc[] __initdata = {
/*    virtual     physical    length      */
/*	{ 0xf1000000, 0x08000000, 0x01000000, MT_DEVICE },*/ /* LCDC (readable for Qt driver) */
/*	{ 0xef700000, 0x10800000, 0x00001000, MT_DEVICE },*/  /* SCOOP */
	{ 0xef800000, 0x00000000, 0x00800000, MT_DEVICE }, /* Boot Flash */
};

static void __init corgi_map_io(void)
{
	pxa_map_io();
	iotable_init(corgi_io_desc,ARRAY_SIZE(corgi_io_desc));

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x0158C000;
	PGSR1 = 0x00FF0080;
	PGSR2 = 0x0001C004;
	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;
}

#ifdef CONFIG_MACH_CORGI
MACHINE_START(CORGI, "SHARP Corgi")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	FIXUP(fixup_corgi)
	MAPIO(corgi_map_io)
	INITIRQ(corgi_init_irq)
	.init_machine = corgi_init,
	.timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SHEPHERD
MACHINE_START(SHEPHERD, "SHARP Shepherd")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	FIXUP(fixup_corgi)
	MAPIO(corgi_map_io)
	INITIRQ(corgi_init_irq)
	.init_machine = corgi_init,
	.timer = &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_HUSKY
MACHINE_START(HUSKY, "SHARP Husky")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	FIXUP(fixup_corgi)
	MAPIO(corgi_map_io)
	INITIRQ(corgi_init_irq)
	.init_machine = corgi_init,
	.timer = &pxa_timer,
MACHINE_END
#endif

