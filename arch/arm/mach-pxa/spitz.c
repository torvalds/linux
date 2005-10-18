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
#include <linux/device.h>
#include <linux/delay.h>
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
#include <asm/arch/pxafb.h>
#include <asm/arch/akita.h>
#include <asm/arch/spitz.h>
#include <asm/arch/sharpsl.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
#include "sharpsl.h"

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
	.io_dir 	= SPITZ_SCP_IO_DIR,
	.io_out		= SPITZ_SCP_IO_OUT,
	.suspend_clr = SPITZ_SCP_SUS_CLR,
	.suspend_set = SPITZ_SCP_SUS_SET,
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
	.io_dir 	= SPITZ_SCP2_IO_DIR,
	.io_out		= SPITZ_SCP2_IO_OUT,
	.suspend_clr = SPITZ_SCP2_SUS_CLR,
	.suspend_set = SPITZ_SCP2_SUS_SET,
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


/*
 * Spitz SSP Device
 *
 * Set the parent as the scoop device because a lot of SSP devices
 * also use scoop functions and this makes the power up/down order
 * work correctly.
 */
struct platform_device spitzssp_device = {
	.name		= "corgi-ssp",
	.dev		= {
 		.parent = &spitzscoop_device.dev,
	},
	.id		= -1,
};

struct corgissp_machinfo spitz_ssp_machinfo = {
	.port		= 2,
	.cs_lcdcon	= SPITZ_GPIO_LCDCON_CS,
	.cs_ads7846	= SPITZ_GPIO_ADS7846_CS,
	.cs_max1111	= SPITZ_GPIO_MAX1111_CS,
	.clk_lcdcon	= 520,
	.clk_ads7846	= 14,
	.clk_max1111	= 56,
};


/*
 * Spitz Backlight Device
 */
static struct corgibl_machinfo spitz_bl_machinfo = {
	.max_intensity = 0x2f,
};

static struct platform_device spitzbl_device = {
	.name		= "corgi-bl",
	.dev		= {
 		.platform_data	= &spitz_bl_machinfo,
	},
	.id		= -1,
};


/*
 * Spitz Keyboard Device
 */
static struct platform_device spitzkbd_device = {
	.name		= "spitz-keyboard",
	.id		= -1,
};


/*
 * Spitz Touch Screen Device
 */
static struct resource spitzts_resources[] = {
	[0] = {
		.start		= SPITZ_IRQ_GPIO_TP_INT,
		.end		= SPITZ_IRQ_GPIO_TP_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct corgits_machinfo  spitz_ts_machinfo = {
	.get_hsync_len   = spitz_get_hsync_len,
	.put_hsync       = spitz_put_hsync,
	.wait_hsync      = spitz_wait_hsync,
};

static struct platform_device spitzts_device = {
	.name		= "corgi-ts",
	.dev		= {
 		.parent = &spitzssp_device.dev,
		.platform_data	= &spitz_ts_machinfo,
	},
	.id		= -1,
	.num_resources	= ARRAY_SIZE(spitzts_resources),
	.resource	= spitzts_resources,
};


/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */

static struct pxamci_platform_data spitz_mci_platform_data;

static int spitz_mci_init(struct device *dev, irqreturn_t (*spitz_detect_int)(int, void *, struct pt_regs *), void *data)
{
	int err;

	/* setup GPIO for PXA27x MMC controller	*/
	pxa_gpio_mode(GPIO32_MMCCLK_MD);
	pxa_gpio_mode(GPIO112_MMCCMD_MD);
	pxa_gpio_mode(GPIO92_MMCDAT0_MD);
	pxa_gpio_mode(GPIO109_MMCDAT1_MD);
	pxa_gpio_mode(GPIO110_MMCDAT2_MD);
	pxa_gpio_mode(GPIO111_MMCDAT3_MD);
	pxa_gpio_mode(SPITZ_GPIO_nSD_DETECT | GPIO_IN);
	pxa_gpio_mode(SPITZ_GPIO_nSD_WP | GPIO_IN);

	spitz_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(SPITZ_IRQ_GPIO_nSD_DETECT, spitz_detect_int, SA_INTERRUPT,
			     "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "spitz_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		return -1;
	}

	set_irq_type(SPITZ_IRQ_GPIO_nSD_DETECT, IRQT_BOTHEDGE);

	return 0;
}

/* Power control is shared with one of the CF slots so we have a mess */
static void spitz_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	unsigned short cpr = read_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR);

	if (( 1 << vdd) & p_d->ocr_mask) {
		/* printk(KERN_DEBUG "%s: on\n", __FUNCTION__); */
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CF_POWER);
		mdelay(2);
		write_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR, cpr | 0x04);
	} else {
		/* printk(KERN_DEBUG "%s: off\n", __FUNCTION__); */
		write_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR, cpr & ~0x04);

		if (!(cpr | 0x02)) {
			mdelay(1);
			reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CF_POWER);
		}
	}
}

static int spitz_mci_get_ro(struct device *dev)
{
	return GPLR(SPITZ_GPIO_nSD_WP) & GPIO_bit(SPITZ_GPIO_nSD_WP);
}

static void spitz_mci_exit(struct device *dev, void *data)
{
	free_irq(SPITZ_IRQ_GPIO_nSD_DETECT, data);
}

static struct pxamci_platform_data spitz_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= spitz_mci_init,
	.get_ro		= spitz_mci_get_ro,
	.setpower 	= spitz_mci_setpower,
	.exit		= spitz_mci_exit,
};


/*
 * Spitz PXA Framebuffer
 */
static struct pxafb_mach_info spitz_pxafb_info __initdata = {
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
        .lccr0          = LCCR0_Color | LCCR0_Sngl | LCCR0_Act | LCCR0_LDDALT | LCCR0_OUC | LCCR0_CMDIM | LCCR0_RDSTM,
        .lccr3          = LCCR3_PixRsEdg | LCCR3_OutEnH,
        .pxafb_lcd_power = spitz_lcd_power,
};


static struct platform_device *devices[] __initdata = {
	&spitzscoop_device,
	&spitzssp_device,
	&spitzkbd_device,
	&spitzts_device,
	&spitzbl_device,
};

static void __init common_init(void)
{
	PMCR = 0x00;

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x0158C000;
	PGSR1 = 0x00FF0080;
	PGSR2 = 0x0001C004;

	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;

	corgi_ssp_set_machinfo(&spitz_ssp_machinfo);

	pxa_gpio_mode(SPITZ_GPIO_HSYNC | GPIO_IN);

	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_mci_info(&spitz_mci_platform_data);
	set_pxa_fb_parent(&spitzssp_device.dev);
	set_pxa_fb_info(&spitz_pxafb_info);
}

static void __init spitz_init(void)
{
	scoop_num = 2;
	scoop_devs = &spitz_pcmcia_scoop[0];
	spitz_bl_machinfo.set_bl_intensity = spitz_bl_set_intensity;

	common_init();

	platform_device_register(&spitzscoop2_device);
}

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
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_spitz,
	.map_io		= pxa_map_io,
	.init_irq	= pxa_init_irq,
	.init_machine	= spitz_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_BORZOI
MACHINE_START(BORZOI, "SHARP Borzoi")
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_spitz,
	.map_io		= pxa_map_io,
	.init_irq	= pxa_init_irq,
	.init_machine	= spitz_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif
