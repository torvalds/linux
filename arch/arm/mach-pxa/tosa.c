/*
 *  Support for Sharp SL-C6000x PDAs
 *  Model: (Tosa)
 *
 *  Copyright (c) 2005 Dirk Opfer
 *
 *	Based on code written by Sharp/Lineo for 2.4 kernels
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
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
#include <linux/delay.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/irda.h>
#include <asm/arch/mmc.h>
#include <asm/arch/udc.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/arch/tosa.h>

#include <asm/hardware/scoop.h>
#include <asm/mach/sharpsl_param.h>

#include "generic.h"
#include "devices.h"

/*
 * SCOOP Device
 */
static struct resource tosa_scoop_resources[] = {
	[0] = {
		.start	= TOSA_CF_PHYS,
		.end	= TOSA_CF_PHYS + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct scoop_config tosa_scoop_setup = {
	.io_dir 	= TOSA_SCOOP_IO_DIR,
	.io_out		= TOSA_SCOOP_IO_OUT,

};

struct platform_device tosascoop_device = {
	.name		= "sharp-scoop",
	.id		= 0,
	.dev		= {
 		.platform_data	= &tosa_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(tosa_scoop_resources),
	.resource	= tosa_scoop_resources,
};


/*
 * SCOOP Device Jacket
 */
static struct resource tosa_scoop_jc_resources[] = {
	[0] = {
		.start		= TOSA_SCOOP_PHYS + 0x40,
		.end		= TOSA_SCOOP_PHYS + 0xfff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config tosa_scoop_jc_setup = {
	.io_dir 	= TOSA_SCOOP_JC_IO_DIR,
	.io_out		= TOSA_SCOOP_JC_IO_OUT,
};

struct platform_device tosascoop_jc_device = {
	.name		= "sharp-scoop",
	.id		= 1,
	.dev		= {
 		.platform_data	= &tosa_scoop_jc_setup,
		.parent 	= &tosascoop_device.dev,
	},
	.num_resources	= ARRAY_SIZE(tosa_scoop_jc_resources),
	.resource	= tosa_scoop_jc_resources,
};

/*
 * PCMCIA
 */
static struct scoop_pcmcia_dev tosa_pcmcia_scoop[] = {
{
	.dev        = &tosascoop_device.dev,
	.irq        = TOSA_IRQ_GPIO_CF_IRQ,
	.cd_irq     = TOSA_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},{
	.dev        = &tosascoop_jc_device.dev,
	.irq        = TOSA_IRQ_GPIO_JC_CF_IRQ,
	.cd_irq     = -1,
},
};

static void tosa_pcmcia_init(void)
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

static struct scoop_pcmcia_config tosa_pcmcia_config = {
	.devs         = &tosa_pcmcia_scoop[0],
	.num_devs     = 2,
	.pcmcia_init  = tosa_pcmcia_init,
};

/*
 * USB Device Controller
 */
static void tosa_udc_command(int cmd)
{
	switch(cmd)	{
		case PXA2XX_UDC_CMD_CONNECT:
			set_scoop_gpio(&tosascoop_jc_device.dev,TOSA_SCOOP_JC_USB_PULLUP);
			break;
		case PXA2XX_UDC_CMD_DISCONNECT:
			reset_scoop_gpio(&tosascoop_jc_device.dev,TOSA_SCOOP_JC_USB_PULLUP);
			break;
	}
}

static int tosa_udc_is_connected(void)
{
	return ((GPLR(TOSA_GPIO_USB_IN) & GPIO_bit(TOSA_GPIO_USB_IN)) == 0);
}


static struct pxa2xx_udc_mach_info udc_info __initdata = {
	.udc_command		= tosa_udc_command,
	.udc_is_connected	= tosa_udc_is_connected,
};

/*
 * MMC/SD Device
 */
static struct pxamci_platform_data tosa_mci_platform_data;

static int tosa_mci_init(struct device *dev, irq_handler_t tosa_detect_int, void *data)
{
	int err;

	/* setup GPIO for PXA25x MMC controller */
	pxa_gpio_mode(GPIO6_MMCCLK_MD);
	pxa_gpio_mode(GPIO8_MMCCS0_MD);
	pxa_gpio_mode(TOSA_GPIO_nSD_DETECT | GPIO_IN);

	tosa_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(TOSA_IRQ_GPIO_nSD_DETECT, tosa_detect_int, IRQF_DISABLED,
				"MMC/SD card detect", data);
	if (err) {
		printk(KERN_ERR "tosa_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		return -1;
	}

	set_irq_type(TOSA_IRQ_GPIO_nSD_DETECT, IRQT_BOTHEDGE);

	return 0;
}

static void tosa_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		set_scoop_gpio(&tosascoop_device.dev,TOSA_SCOOP_PWR_ON);
	} else {
		reset_scoop_gpio(&tosascoop_device.dev,TOSA_SCOOP_PWR_ON);
	}
}

static int tosa_mci_get_ro(struct device *dev)
{
	return (read_scoop_reg(&tosascoop_device.dev, SCOOP_GPWR)&TOSA_SCOOP_SD_WP);
}

static void tosa_mci_exit(struct device *dev, void *data)
{
	free_irq(TOSA_IRQ_GPIO_nSD_DETECT, data);
}

static struct pxamci_platform_data tosa_mci_platform_data = {
	.ocr_mask       = MMC_VDD_32_33|MMC_VDD_33_34,
	.init           = tosa_mci_init,
	.get_ro		= tosa_mci_get_ro,
	.setpower       = tosa_mci_setpower,
	.exit           = tosa_mci_exit,
};

/*
 * Irda
 */
static void tosa_irda_transceiver_mode(struct device *dev, int mode)
{
	if (mode & IR_OFF) {
		reset_scoop_gpio(&tosascoop_device.dev,TOSA_SCOOP_IR_POWERDWN);
		pxa_gpio_mode(GPIO47_STTXD|GPIO_DFLT_LOW);
		pxa_gpio_mode(GPIO47_STTXD|GPIO_OUT);
	} else {
		pxa_gpio_mode(GPIO47_STTXD_MD);
		set_scoop_gpio(&tosascoop_device.dev,TOSA_SCOOP_IR_POWERDWN);
	}
}

static struct pxaficp_platform_data tosa_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = tosa_irda_transceiver_mode,
};

/*
 * Tosa Keyboard
 */
static struct platform_device tosakbd_device = {
	.name		= "tosa-keyboard",
	.id		= -1,
};

/*
 * Tosa LEDs
 */
static struct platform_device tosaled_device = {
    .name   = "tosa-led",
    .id     = -1,
};

static struct platform_device *devices[] __initdata = {
	&tosascoop_device,
	&tosascoop_jc_device,
	&tosakbd_device,
	&tosaled_device,
};

static void tosa_poweroff(void)
{
	RCSR = RCSR_HWR | RCSR_WDR | RCSR_SMR | RCSR_GPR;

	pxa_gpio_mode(TOSA_GPIO_ON_RESET | GPIO_OUT);
	GPSR(TOSA_GPIO_ON_RESET) = GPIO_bit(TOSA_GPIO_ON_RESET);

	mdelay(1000);
	arm_machine_restart('h');
}

static void tosa_restart(char mode)
{
	/* Bootloader magic for a reboot */
	if((MSC0 & 0xffff0000) == 0x7ff00000)
		MSC0 = (MSC0 & 0xffff) | 0x7ee00000;

	tosa_poweroff();
}

static void __init tosa_init(void)
{
	pm_power_off = tosa_poweroff;
	arm_pm_restart = tosa_restart;

	pxa_gpio_mode(TOSA_GPIO_ON_RESET | GPIO_IN);
	pxa_gpio_mode(TOSA_GPIO_TC6393_INT | GPIO_IN);
	pxa_gpio_mode(TOSA_GPIO_USB_IN | GPIO_IN);

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00000000;
	PGSR1 = 0x00FF0002;
	PGSR2 = 0x00014000;
	PCFR |= PCFR_OPDE;

	/* enable batt_fault */
	PMCR = 0x01;

	pxa_set_mci_info(&tosa_mci_platform_data);
	pxa_set_udc_info(&udc_info);
	pxa_set_ficp_info(&tosa_ficp_platform_data);
	platform_scoop_config = &tosa_pcmcia_config;

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init fixup_tosa(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	mi->bank[0].node = 0;
	mi->bank[0].size = (64*1024*1024);
}

MACHINE_START(TOSA, "SHARP Tosa")
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup          = fixup_tosa,
	.map_io         = pxa_map_io,
	.init_irq       = pxa25x_init_irq,
	.init_machine   = tosa_init,
	.timer          = &pxa_timer,
MACHINE_END
