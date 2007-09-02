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
#include <linux/mmc/host.h>
#include <linux/pm.h>
#include <linux/backlight.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/system.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/irda.h>
#include <asm/arch/mmc.h>
#include <asm/arch/ohci.h>
#include <asm/arch/udc.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/akita.h>
#include <asm/arch/spitz.h>
#include <asm/arch/sharpsl.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
#include "devices.h"
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

#define SPITZ_PWR_SD 0x01
#define SPITZ_PWR_CF 0x02

/* Power control is shared with between one of the CF slots and SD */
static void spitz_card_pwr_ctrl(int device, unsigned short new_cpr)
{
	unsigned short cpr = read_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR);

	if (new_cpr & 0x0007) {
	        set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CF_POWER);
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
		        reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_CF_POWER);
		} else {
		        write_scoop_reg(&spitzscoop_device.dev, SCOOP_CPR, cpr | new_cpr);
		}
	}
}

static void spitz_pcmcia_init(void)
{
	/* Setup default state of GPIO outputs
	   before we enable them as outputs. */
	GPSR(GPIO48_nPOE) = GPIO_bit(GPIO48_nPOE) |
		GPIO_bit(GPIO49_nPWE) |	GPIO_bit(GPIO50_nPIOR) |
		GPIO_bit(GPIO51_nPIOW) | GPIO_bit(GPIO54_nPCE_2);
	GPSR(GPIO85_nPCE_1) = GPIO_bit(GPIO85_nPCE_1);

	pxa_gpio_mode(GPIO48_nPOE_MD);
	pxa_gpio_mode(GPIO49_nPWE_MD);
	pxa_gpio_mode(GPIO50_nPIOR_MD);
	pxa_gpio_mode(GPIO51_nPIOW_MD);
	pxa_gpio_mode(GPIO55_nPREG_MD);
	pxa_gpio_mode(GPIO56_nPWAIT_MD);
	pxa_gpio_mode(GPIO57_nIOIS16_MD);
	pxa_gpio_mode(GPIO85_nPCE_1_MD);
	pxa_gpio_mode(GPIO54_nPCE_2_MD);
	pxa_gpio_mode(GPIO104_pSKTSEL_MD);
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
	.pcmcia_init  = spitz_pcmcia_init,
	.power_ctrl   = spitz_pcmcia_pwr,
};

EXPORT_SYMBOL(spitzscoop_device);
EXPORT_SYMBOL(spitzscoop2_device);


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
static void spitz_bl_kick_battery(void)
{
	void (*kick_batt)(void);

	kick_batt = symbol_get(sharpsl_battery_kick);
	if (kick_batt) {
		kick_batt();
		symbol_put(sharpsl_battery_kick);
	}
}

static struct generic_bl_info spitz_bl_machinfo = {
	.name = "corgi-bl",
	.default_intensity = 0x1f,
	.limit_mask = 0x0b,
	.max_intensity = 0x2f,
	.kick_battery = spitz_bl_kick_battery,
};

static struct platform_device spitzbl_device = {
	.name		= "generic-bl",
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
 * Spitz LEDs
 */
static struct platform_device spitzled_device = {
	.name		= "spitz-led",
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

static int spitz_mci_init(struct device *dev, irq_handler_t spitz_detect_int, void *data)
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

	err = request_irq(SPITZ_IRQ_GPIO_nSD_DETECT, spitz_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "spitz_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		return -1;
	}

	return 0;
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
 * USB Host (OHCI)
 */
static int spitz_ohci_init(struct device *dev)
{
	/* Only Port 2 is connected */
	pxa_gpio_mode(SPITZ_GPIO_USB_CONNECT | GPIO_IN);
	pxa_gpio_mode(SPITZ_GPIO_USB_HOST | GPIO_OUT);
	pxa_gpio_mode(SPITZ_GPIO_USB_DEVICE | GPIO_IN);

	/* Setup USB Port 2 Output Control Register */
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;

	GPSR(SPITZ_GPIO_USB_HOST) = GPIO_bit(SPITZ_GPIO_USB_HOST);

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
static void spitz_irda_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF)
		set_scoop_gpio(&spitzscoop2_device.dev, SPITZ_SCP2_IR_ON);
	else
		reset_scoop_gpio(&spitzscoop2_device.dev, SPITZ_SCP2_IR_ON);
}

#ifdef CONFIG_MACH_AKITA
static void akita_irda_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF)
		akita_set_ioexp(&akitaioexp_device.dev, AKITA_IOEXP_IR_ON);
	else
		akita_reset_ioexp(&akitaioexp_device.dev, AKITA_IOEXP_IR_ON);
}
#endif

static struct pxaficp_platform_data spitz_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = spitz_irda_transceiver_mode,
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
	&spitzled_device,
};

static void spitz_poweroff(void)
{
	RCSR = RCSR_HWR | RCSR_WDR | RCSR_SMR | RCSR_GPR;

	pxa_gpio_mode(SPITZ_GPIO_ON_RESET | GPIO_OUT);
	GPSR(SPITZ_GPIO_ON_RESET) = GPIO_bit(SPITZ_GPIO_ON_RESET);

	mdelay(1000);
	arm_machine_restart('h');
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
	pm_power_off = spitz_poweroff;
	arm_pm_restart = spitz_restart;

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
	pxa_set_ohci_info(&spitz_ohci_platform_data);
	pxa_set_ficp_info(&spitz_ficp_platform_data);
	set_pxa_fb_parent(&spitzssp_device.dev);
	set_pxa_fb_info(&spitz_pxafb_info);
}

static void __init spitz_init(void)
{
	platform_scoop_config = &spitz_pcmcia_config;

	spitz_bl_machinfo.set_bl_intensity = spitz_bl_set_intensity;

	common_init();

	platform_device_register(&spitzscoop2_device);
}

#ifdef CONFIG_MACH_AKITA
/*
 * Akita IO Expander
 */
struct platform_device akitaioexp_device = {
	.name		= "akita-ioexp",
	.id		= -1,
};

EXPORT_SYMBOL_GPL(akitaioexp_device);

static void __init akita_init(void)
{
	spitz_ficp_platform_data.transceiver_mode = akita_irda_transceiver_mode;

	/* We just pretend the second element of the array doesn't exist */
	spitz_pcmcia_config.num_devs = 1;
	platform_scoop_config = &spitz_pcmcia_config;
	spitz_bl_machinfo.set_bl_intensity = akita_bl_set_intensity;

	platform_device_register(&akitaioexp_device);

	spitzscoop_device.dev.parent = &akitaioexp_device.dev;
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
