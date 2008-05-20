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
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/gpio.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-regs.h>
#include <asm/arch/mfp-pxa25x.h>
#include <asm/arch/irda.h>
#include <asm/arch/i2c.h>
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

static unsigned long tosa_pin_config[] = {
	GPIO78_nCS_2, /* Scoop */
	GPIO80_nCS_4, /* tg6393xb */
	GPIO33_nCS_5, /* Scoop */

	// GPIO76 CARD_VCC_ON1

	GPIO19_GPIO, /* Reset out */
	GPIO1_RST | WAKEUP_ON_EDGE_FALL,

	GPIO0_GPIO | WAKEUP_ON_EDGE_FALL, /* WAKE_UP */
	GPIO2_GPIO | WAKEUP_ON_EDGE_BOTH, /* AC_IN */
	GPIO3_GPIO | WAKEUP_ON_EDGE_FALL, /* RECORD */
	GPIO4_GPIO | WAKEUP_ON_EDGE_FALL, /* SYNC */
	GPIO20_GPIO, /* EAR_IN */
	GPIO22_GPIO, /* On */

	GPIO5_GPIO, /* USB_IN */
	GPIO32_GPIO, /* Pen IRQ */

	GPIO7_GPIO, /* Jacket Detect */
	GPIO14_GPIO, /* BAT0_CRG */
	GPIO12_GPIO, /* BAT1_CRG */
	GPIO17_GPIO, /* BAT0_LOW */
	GPIO84_GPIO, /* BAT1_LOW */
	GPIO38_GPIO, /* BAT_LOCK */

	GPIO11_3_6MHz,
	GPIO15_GPIO, /* TC6393XB IRQ */
	GPIO18_RDY,
	GPIO27_GPIO, /* LCD Sync */

	/* MMC */
	GPIO6_MMC_CLK,
	GPIO8_MMC_CS0,
	GPIO9_GPIO, /* Detect */
	// GPIO10 nSD_INT

	/* CF */
	GPIO13_GPIO, /* CD_IRQ */
	GPIO21_GPIO, /* Main Slot IRQ */
	GPIO36_GPIO, /* Jacket Slot IRQ */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* AC97 */
	GPIO31_AC97_SYNC,
	GPIO30_AC97_SDATA_OUT,
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	// GPIO79 nAUD_IRQ

	/* FFUART */
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,
	GPIO37_FFUART_DSR,
	GPIO39_FFUART_TXD,
	GPIO40_FFUART_DTR,
	GPIO41_FFUART_RTS,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* IrDA */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* Keybd */
	GPIO58_GPIO,
	GPIO59_GPIO,
	GPIO60_GPIO,
	GPIO61_GPIO,
	GPIO62_GPIO,
	GPIO63_GPIO,
	GPIO64_GPIO,
	GPIO65_GPIO,
	GPIO66_GPIO,
	GPIO67_GPIO,
	GPIO68_GPIO,
	GPIO69_GPIO,
	GPIO70_GPIO,
	GPIO71_GPIO,
	GPIO72_GPIO,
	GPIO73_GPIO,
	GPIO74_GPIO,
	GPIO75_GPIO,

	/* SPI */
	GPIO81_SSP2_CLK_OUT,
	GPIO82_SSP2_FRM_OUT,
	GPIO83_SSP2_TXD,
};

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
	.gpio_base	= TOSA_SCOOP_GPIO_BASE,
};

static struct platform_device tosascoop_device = {
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
	.gpio_base	= TOSA_SCOOP_JC_GPIO_BASE,
};

static struct platform_device tosascoop_jc_device = {
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

static struct scoop_pcmcia_config tosa_pcmcia_config = {
	.devs         = &tosa_pcmcia_scoop[0],
	.num_devs     = 2,
};

/*
 * USB Device Controller
 */
static struct pxa2xx_udc_mach_info udc_info __initdata = {
	.gpio_pullup		= TOSA_GPIO_USB_PULLUP,
	.gpio_vbus		= TOSA_GPIO_USB_IN,
	.gpio_vbus_inverted	= 1,
};

/*
 * MMC/SD Device
 */
static struct pxamci_platform_data tosa_mci_platform_data;

static int tosa_mci_init(struct device *dev, irq_handler_t tosa_detect_int, void *data)
{
	int err;

	tosa_mci_platform_data.detect_delay = msecs_to_jiffies(250);

	err = request_irq(TOSA_IRQ_GPIO_nSD_DETECT, tosa_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"MMC/SD card detect", data);
	if (err) {
		printk(KERN_ERR "tosa_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		goto err_irq;
	}

	err = gpio_request(TOSA_GPIO_SD_WP, "sd_wp");
	if (err) {
		printk(KERN_ERR "tosa_mci_init: can't request SD_WP gpio\n");
		goto err_gpio_wp;
	}
	err = gpio_direction_input(TOSA_GPIO_SD_WP);
	if (err)
		goto err_gpio_wp_dir;

	err = gpio_request(TOSA_GPIO_PWR_ON, "sd_pwr");
	if (err) {
		printk(KERN_ERR "tosa_mci_init: can't request SD_PWR gpio\n");
		goto err_gpio_pwr;
	}
	err = gpio_direction_output(TOSA_GPIO_PWR_ON, 0);
	if (err)
		goto err_gpio_pwr_dir;

	return 0;

err_gpio_pwr_dir:
	gpio_free(TOSA_GPIO_PWR_ON);
err_gpio_pwr:
err_gpio_wp_dir:
	gpio_free(TOSA_GPIO_SD_WP);
err_gpio_wp:
	free_irq(TOSA_IRQ_GPIO_nSD_DETECT, data);
err_irq:
	return err;
}

static void tosa_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		gpio_set_value(TOSA_GPIO_PWR_ON, 1);
	} else {
		gpio_set_value(TOSA_GPIO_PWR_ON, 0);
	}
}

static int tosa_mci_get_ro(struct device *dev)
{
	return gpio_get_value(TOSA_GPIO_SD_WP);
}

static void tosa_mci_exit(struct device *dev, void *data)
{
	gpio_free(TOSA_GPIO_PWR_ON);
	gpio_free(TOSA_GPIO_SD_WP);
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
static int tosa_irda_startup(struct device *dev)
{
	int ret;

	ret = gpio_request(TOSA_GPIO_IR_POWERDWN, "IrDA powerdown");
	if (ret)
		return ret;

	ret = gpio_direction_output(TOSA_GPIO_IR_POWERDWN, 0);
	if (ret)
		gpio_free(TOSA_GPIO_IR_POWERDWN);

	return ret;
	}

static void tosa_irda_shutdown(struct device *dev)
{
	gpio_free(TOSA_GPIO_IR_POWERDWN);
}

static void tosa_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(TOSA_GPIO_IR_POWERDWN, !(mode & IR_OFF));
}

static struct pxaficp_platform_data tosa_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = tosa_irda_transceiver_mode,
	.startup = tosa_irda_startup,
	.shutdown = tosa_irda_shutdown,
};

/*
 * Tosa Keyboard
 */
static struct platform_device tosakbd_device = {
	.name		= "tosa-keyboard",
	.id		= -1,
};

static struct gpio_keys_button tosa_gpio_keys[] = {
	/*
	 * Two following keys are directly tied to "ON" button of tosa. Why?
	 * The first one can be used as a wakeup source, the second can't;
	 * also the first one is OR of ac_powered and on_button.
	 */
	{
		.type	= EV_PWR,
		.code	= KEY_RESERVED,
		.gpio	= TOSA_GPIO_POWERON,
		.desc	= "Poweron",
		.wakeup	= 1,
		.active_low = 1,
	},
	{
		.type	= EV_PWR,
		.code	= KEY_SUSPEND,
		.gpio	= TOSA_GPIO_ON_KEY,
		.desc	= "On key",
		/*
		 * can't be used as wakeup
		 * .wakeup	= 1,
		 */
		.active_low = 1,
	},
	{
		.type	= EV_KEY,
		.code	= TOSA_KEY_RECORD,
		.gpio	= TOSA_GPIO_RECORD_BTN,
		.desc	= "Record Button",
		.wakeup	= 1,
		.active_low = 1,
	},
	{
		.type	= EV_KEY,
		.code	= TOSA_KEY_SYNC,
		.gpio	= TOSA_GPIO_SYNC,
		.desc	= "Sync Button",
		.wakeup	= 1,
		.active_low = 1,
	},
};

static struct gpio_keys_platform_data tosa_gpio_keys_platform_data = {
	.buttons	= tosa_gpio_keys,
	.nbuttons	= ARRAY_SIZE(tosa_gpio_keys),
};

static struct platform_device tosa_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &tosa_gpio_keys_platform_data,
	},
};

/*
 * Tosa LEDs
 */
static struct gpio_led tosa_gpio_leds[] = {
	{
		.name			= "tosa:amber:charge",
		.default_trigger	= "main-battery-charging",
		.gpio			= TOSA_GPIO_CHRG_ERR_LED,
	},
	{
		.name			= "tosa:green:mail",
		.default_trigger	= "nand-disk",
		.gpio			= TOSA_GPIO_NOTE_LED,
	},
	{
		.name			= "tosa:dual:wlan",
		.default_trigger	= "none",
		.gpio			= TOSA_GPIO_WLAN_LED,
	},
	{
		.name			= "tosa:blue:bluetooth",
		.default_trigger	= "none",
		.gpio			= TOSA_GPIO_BT_LED,
	},
};

static struct gpio_led_platform_data tosa_gpio_leds_platform_data = {
	.leds		= tosa_gpio_leds,
	.num_leds	= ARRAY_SIZE(tosa_gpio_leds),
};

static struct platform_device tosaled_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &tosa_gpio_leds_platform_data,
	},
};

static struct platform_device *devices[] __initdata = {
	&tosascoop_device,
	&tosascoop_jc_device,
	&tosakbd_device,
	&tosa_gpio_keys_device,
	&tosaled_device,
};

static void tosa_poweroff(void)
{
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
	pxa2xx_mfp_config(ARRAY_AND_SIZE(tosa_pin_config));
	gpio_set_wake(MFP_PIN_GPIO1, 1);
	/* We can't pass to gpio-keys since it will drop the Reset altfunc */

	pm_power_off = tosa_poweroff;
	arm_pm_restart = tosa_restart;

	PCFR |= PCFR_OPDE;

	/* enable batt_fault */
	PMCR = 0x01;

	pxa_set_mci_info(&tosa_mci_platform_data);
	pxa_set_udc_info(&udc_info);
	pxa_set_ficp_info(&tosa_ficp_platform_data);
	pxa_set_i2c_info(NULL);
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
