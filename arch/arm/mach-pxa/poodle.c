/*
 * linux/arch/arm/mach-pxa/poodle.c
 *
 *  Support for the SHARP Poodle Board.
 *
 * Based on:
 *  linux/arch/arm/mach-pxa/lubbock.c Author:	Nicolas Pitre
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 * Change Log
 *  12-Dec-2002 Sharp Corporation for Poodle
 *  John Lenz <lenz@cs.wisc.edu> updates to 2.6
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/pm.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/system.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/mmc.h>
#include <asm/arch/udc.h>
#include <asm/arch/irda.h>
#include <asm/arch/poodle.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/sharpsl.h>
#include <asm/arch/ssp.h>

#include <asm/hardware/scoop.h>
#include <asm/hardware/locomo.h>
#include <asm/mach/sharpsl_param.h>

#include "generic.h"
#include "devices.h"
#include "sharpsl.h"

static struct resource poodle_scoop_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config poodle_scoop_setup = {
	.io_dir		= POODLE_SCOOP_IO_DIR,
	.io_out		= POODLE_SCOOP_IO_OUT,
};

struct platform_device poodle_scoop_device = {
	.name		= "sharp-scoop",
	.id		= -1,
	.dev		= {
		.platform_data	= &poodle_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(poodle_scoop_resources),
	.resource	= poodle_scoop_resources,
};

static void poodle_pcmcia_init(void)
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

static struct scoop_pcmcia_dev poodle_pcmcia_scoop[] = {
{
	.dev        = &poodle_scoop_device.dev,
	.irq        = POODLE_IRQ_GPIO_CF_IRQ,
	.cd_irq     = POODLE_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},
};

static struct scoop_pcmcia_config poodle_pcmcia_config = {
	.devs         = &poodle_pcmcia_scoop[0],
	.num_devs     = 1,
	.pcmcia_init  = poodle_pcmcia_init,
};

EXPORT_SYMBOL(poodle_scoop_device);


/* LoCoMo device */
static struct resource locomo_resources[] = {
	[0] = {
		.start		= 0x10000000,
		.end		= 0x10001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO(10),
		.end		= IRQ_GPIO(10),
		.flags		= IORESOURCE_IRQ,
	},
};

struct platform_device poodle_locomo_device = {
	.name		= "locomo",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(locomo_resources),
	.resource	= locomo_resources,
};

EXPORT_SYMBOL(poodle_locomo_device);

/*
 * Poodle SSP Device
 */

struct platform_device poodle_ssp_device = {
	.name		= "corgi-ssp",
	.id		= -1,
};

struct corgissp_machinfo poodle_ssp_machinfo = {
	.port		= 1,
	.cs_lcdcon	= -1,
	.cs_ads7846	= -1,
	.cs_max1111	= -1,
	.clk_lcdcon	= 2,
	.clk_ads7846	= 36,
	.clk_max1111	= 2,
};


/*
 * Poodle Touch Screen Device
 */
static struct resource poodlets_resources[] = {
	[0] = {
		.start		= POODLE_IRQ_GPIO_TP_INT,
		.end		= POODLE_IRQ_GPIO_TP_INT,
		.flags		= IORESOURCE_IRQ,
	},
};

static unsigned long poodle_get_hsync_len(void)
{
	return 0;
}

static void poodle_null_hsync(void)
{
}

static struct corgits_machinfo  poodle_ts_machinfo = {
	.get_hsync_len   = poodle_get_hsync_len,
	.put_hsync       = poodle_null_hsync,
	.wait_hsync      = poodle_null_hsync,
};

static struct platform_device poodle_ts_device = {
	.name		= "corgi-ts",
	.dev		= {
		.platform_data	= &poodle_ts_machinfo,
	},
	.id		= -1,
	.num_resources	= ARRAY_SIZE(poodlets_resources),
	.resource	= poodlets_resources,
};


/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */
static struct pxamci_platform_data poodle_mci_platform_data;

static int poodle_mci_init(struct device *dev, irq_handler_t poodle_detect_int, void *data)
{
	int err;

	/* setup GPIO for PXA25x MMC controller	*/
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);
	pxa_gpio_mode(POODLE_GPIO_nSD_DETECT | GPIO_IN);
	pxa_gpio_mode(POODLE_GPIO_nSD_WP | GPIO_IN);
	pxa_gpio_mode(POODLE_GPIO_SD_PWR | GPIO_OUT);
	pxa_gpio_mode(POODLE_GPIO_SD_PWR1 | GPIO_OUT);

	poodle_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(POODLE_IRQ_GPIO_nSD_DETECT, poodle_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  "MMC card detect", data);
	if (err)
		printk(KERN_ERR "poodle_mci_init: MMC/SD: can't request MMC card detect IRQ\n");

	return err;
}

static void poodle_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		GPSR(POODLE_GPIO_SD_PWR) = GPIO_bit(POODLE_GPIO_SD_PWR);
		mdelay(2);
		GPSR(POODLE_GPIO_SD_PWR1) = GPIO_bit(POODLE_GPIO_SD_PWR1);
	} else {
		GPCR(POODLE_GPIO_SD_PWR1) = GPIO_bit(POODLE_GPIO_SD_PWR1);
		GPCR(POODLE_GPIO_SD_PWR) = GPIO_bit(POODLE_GPIO_SD_PWR);
	}
}

static int poodle_mci_get_ro(struct device *dev)
{
	return GPLR(POODLE_GPIO_nSD_WP) & GPIO_bit(POODLE_GPIO_nSD_WP);
}


static void poodle_mci_exit(struct device *dev, void *data)
{
	free_irq(POODLE_IRQ_GPIO_nSD_DETECT, data);
}

static struct pxamci_platform_data poodle_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= poodle_mci_init,
	.get_ro		= poodle_mci_get_ro,
	.setpower 	= poodle_mci_setpower,
	.exit		= poodle_mci_exit,
};


/*
 * Irda
 */
static void poodle_irda_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF) {
		GPSR(POODLE_GPIO_IR_ON) = GPIO_bit(POODLE_GPIO_IR_ON);
	} else {
		GPCR(POODLE_GPIO_IR_ON) = GPIO_bit(POODLE_GPIO_IR_ON);
	}
}

static struct pxaficp_platform_data poodle_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = poodle_irda_transceiver_mode,
};


/*
 * USB Device Controller
 */
static void poodle_udc_command(int cmd)
{
	switch(cmd)	{
	case PXA2XX_UDC_CMD_CONNECT:
		GPSR(POODLE_GPIO_USB_PULLUP) = GPIO_bit(POODLE_GPIO_USB_PULLUP);
		break;
	case PXA2XX_UDC_CMD_DISCONNECT:
		GPCR(POODLE_GPIO_USB_PULLUP) = GPIO_bit(POODLE_GPIO_USB_PULLUP);
		break;
	}
}

static struct pxa2xx_udc_mach_info udc_info __initdata = {
	/* no connect GPIO; poodle can't tell connection status */
	.udc_command		= poodle_udc_command,
};


/* PXAFB device */
static struct pxafb_mode_info poodle_fb_mode = {
	.pixclock	= 144700,
	.xres		= 320,
	.yres		= 240,
	.bpp		= 16,
	.hsync_len	= 7,
	.left_margin	= 11,
	.right_margin	= 30,
	.vsync_len	= 2,
	.upper_margin	= 2,
	.lower_margin	= 0,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
};

static struct pxafb_mach_info poodle_fb_info = {
	.modes		= &poodle_fb_mode,
	.num_modes	= 1,
	.lccr0		= LCCR0_Act | LCCR0_Sngl | LCCR0_Color,
	.lccr3		= 0,
};

static struct platform_device *devices[] __initdata = {
	&poodle_locomo_device,
	&poodle_scoop_device,
	&poodle_ssp_device,
	&poodle_ts_device,
};

static void poodle_poweroff(void)
{
	RCSR = RCSR_HWR | RCSR_WDR | RCSR_SMR | RCSR_GPR;
	arm_machine_restart('h');
}

static void poodle_restart(char mode)
{
	RCSR = RCSR_HWR | RCSR_WDR | RCSR_SMR | RCSR_GPR;
	arm_machine_restart('h');
}

static void __init poodle_init(void)
{
	int ret = 0;

	pm_power_off = poodle_poweroff;
	arm_pm_restart = poodle_restart;

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00008000;
	PGSR1 = 0x003F0202;
	PGSR2 = 0x0001C000;
	PCFR |= PCFR_OPDE;

	/* cpu initialize */
	/* Pgsr Register */
  	PGSR0 = 0x0146dd80;
  	PGSR1 = 0x03bf0890;
  	PGSR2 = 0x0001c000;

	/* Alternate Register */
  	GAFR0_L = 0x01001000;
  	GAFR0_U = 0x591a8010;
  	GAFR1_L = 0x900a8451;
  	GAFR1_U = 0xaaa5aaaa;
  	GAFR2_L = 0x8aaaaaaa;
  	GAFR2_U = 0x00000002;

	/* Direction Register */
  	GPDR0 = 0xd3f0904c;
  	GPDR1 = 0xfcffb7d3;
  	GPDR2 = 0x0001ffff;

	/* Output Register */
  	GPCR0 = 0x00000000;
  	GPCR1 = 0x00000000;
  	GPCR2 = 0x00000000;

  	GPSR0 = 0x00400000;
  	GPSR1 = 0x00000000;
        GPSR2 = 0x00000000;

	set_pxa_fb_parent(&poodle_locomo_device.dev);
	set_pxa_fb_info(&poodle_fb_info);
	pxa_gpio_mode(POODLE_GPIO_USB_PULLUP | GPIO_OUT);
	pxa_gpio_mode(POODLE_GPIO_IR_ON | GPIO_OUT);
	pxa_set_udc_info(&udc_info);
	pxa_set_mci_info(&poodle_mci_platform_data);
	pxa_set_ficp_info(&poodle_ficp_platform_data);

	platform_scoop_config = &poodle_pcmcia_config;

	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret) {
		printk(KERN_WARNING "poodle: Unable to register LoCoMo device\n");
	}
	corgi_ssp_set_machinfo(&poodle_ssp_machinfo);
}

static void __init fixup_poodle(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	mi->bank[0].size = (32*1024*1024);
}

MACHINE_START(POODLE, "SHARP Poodle")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_poodle,
	.map_io		= pxa_map_io,
	.init_irq	= pxa25x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= poodle_init,
MACHINE_END
