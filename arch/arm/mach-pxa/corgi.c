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
#include <asm/arch/irda.h>
#include <asm/arch/mmc.h>
#include <asm/arch/udc.h>
#include <asm/arch/corgi.h>
#include <asm/arch/sharpsl.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
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

static struct scoop_pcmcia_dev corgi_pcmcia_scoop[] = {
{
	.dev        = &corgiscoop_device.dev,
	.irq        = CORGI_IRQ_GPIO_CF_IRQ,
	.cd_irq     = CORGI_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},
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
 * Corgi Backlight Device
 */
static struct corgibl_machinfo corgi_bl_machinfo = {
	.max_intensity = 0x2f,
	.set_bl_intensity = corgi_bl_set_intensity,
};

static struct platform_device corgibl_device = {
	.name		= "corgi-bl",
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
 * Corgi Touch Screen Device
 */
static struct resource corgits_resources[] = {
	[0] = {
		.start		= CORGI_IRQ_GPIO_TP_INT,
		.end		= CORGI_IRQ_GPIO_TP_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct corgits_machinfo  corgi_ts_machinfo = {
	.get_hsync_len   = corgi_get_hsync_len,
	.put_hsync       = corgi_put_hsync,
	.wait_hsync      = corgi_wait_hsync,
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

static int corgi_mci_init(struct device *dev, irqreturn_t (*corgi_detect_int)(int, void *, struct pt_regs *), void *data)
{
	int err;

	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);
	pxa_gpio_mode(CORGI_GPIO_nSD_DETECT | GPIO_IN);
	pxa_gpio_mode(CORGI_GPIO_SD_PWR | GPIO_OUT);

	corgi_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(CORGI_IRQ_GPIO_nSD_DETECT, corgi_detect_int, SA_INTERRUPT,
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
}

static struct pxaficp_platform_data corgi_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = corgi_irda_transceiver_mode,
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
	&corgikbd_device,
	&corgibl_device,
	&corgits_device,
};

static void __init corgi_init(void)
{
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
	pxa_gpio_mode(CORGI_GPIO_USB_PULLUP | GPIO_OUT);
	pxa_gpio_mode(CORGI_GPIO_HSYNC | GPIO_IN);

 	pxa_set_udc_info(&udc_info);
	pxa_set_mci_info(&corgi_mci_platform_data);
	pxa_set_ficp_info(&corgi_ficp_platform_data);

	scoop_num = 1;
	scoop_devs = &corgi_pcmcia_scoop[0];

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
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_corgi,
	.map_io		= pxa_map_io,
	.init_irq	= pxa_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SHEPHERD
MACHINE_START(SHEPHERD, "SHARP Shepherd")
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_corgi,
	.map_io		= pxa_map_io,
	.init_irq	= pxa_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_HUSKY
MACHINE_START(HUSKY, "SHARP Husky")
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_corgi,
	.map_io		= pxa_map_io,
	.init_irq	= pxa_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

