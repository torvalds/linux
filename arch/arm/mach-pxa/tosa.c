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
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/mmc/host.h>
#include <linux/mfd/tc6393xb.h>
#include <linux/mfd/tmio.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/pm.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/pda_power.h>
#include <linux/rfkill.h>
#include <linux/spi/spi.h>

#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/pxa25x.h>
#include <mach/reset.h>
#include <mach/irda.h>
#include <mach/i2c.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/tosa_bt.h>
#include <mach/pxa2xx_spi.h>
#include <mach/audio.h>

#include <asm/mach/arch.h>
#include <mach/tosa.h>

#include <asm/hardware/scoop.h>
#include <asm/mach/sharpsl_param.h>

#include "generic.h"
#include "clock.h"
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
	GPIO10_GPIO, /* nSD_INT */

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

	/* Keybd */
	GPIO58_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO59_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO60_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO61_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO62_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO63_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO64_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO65_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO66_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO67_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO68_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO69_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO70_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO71_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO72_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO73_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO74_GPIO | MFP_LPM_DRIVE_LOW,
	GPIO75_GPIO | MFP_LPM_DRIVE_LOW,

	/* SPI */
	GPIO81_SSP2_CLK_OUT,
	GPIO82_SSP2_FRM_OUT,
	GPIO83_SSP2_TXD,

	/* IrDA is managed in other way */
	GPIO46_GPIO,
	GPIO47_GPIO,
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

	err = gpio_request(TOSA_GPIO_nSD_DETECT, "MMC/SD card detect");
	if (err) {
		printk(KERN_ERR "tosa_mci_init: can't request nSD_DETECT gpio\n");
		goto err_gpio_detect;
	}
	err = gpio_direction_input(TOSA_GPIO_nSD_DETECT);
	if (err)
		goto err_gpio_detect_dir;

	err = request_irq(TOSA_IRQ_GPIO_nSD_DETECT, tosa_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"MMC/SD card detect", data);
	if (err) {
		printk(KERN_ERR "tosa_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		goto err_irq;
	}

	err = gpio_request(TOSA_GPIO_SD_WP, "SD Write Protect");
	if (err) {
		printk(KERN_ERR "tosa_mci_init: can't request SD_WP gpio\n");
		goto err_gpio_wp;
	}
	err = gpio_direction_input(TOSA_GPIO_SD_WP);
	if (err)
		goto err_gpio_wp_dir;

	err = gpio_request(TOSA_GPIO_PWR_ON, "SD Power");
	if (err) {
		printk(KERN_ERR "tosa_mci_init: can't request SD_PWR gpio\n");
		goto err_gpio_pwr;
	}
	err = gpio_direction_output(TOSA_GPIO_PWR_ON, 0);
	if (err)
		goto err_gpio_pwr_dir;

	err = gpio_request(TOSA_GPIO_nSD_INT, "SD Int");
	if (err) {
		printk(KERN_ERR "tosa_mci_init: can't request SD_PWR gpio\n");
		goto err_gpio_int;
	}
	err = gpio_direction_input(TOSA_GPIO_nSD_INT);
	if (err)
		goto err_gpio_int_dir;

	return 0;

err_gpio_int_dir:
	gpio_free(TOSA_GPIO_nSD_INT);
err_gpio_int:
err_gpio_pwr_dir:
	gpio_free(TOSA_GPIO_PWR_ON);
err_gpio_pwr:
err_gpio_wp_dir:
	gpio_free(TOSA_GPIO_SD_WP);
err_gpio_wp:
	free_irq(TOSA_IRQ_GPIO_nSD_DETECT, data);
err_irq:
err_gpio_detect_dir:
	gpio_free(TOSA_GPIO_nSD_DETECT);
err_gpio_detect:
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
	gpio_free(TOSA_GPIO_nSD_INT);
	gpio_free(TOSA_GPIO_PWR_ON);
	gpio_free(TOSA_GPIO_SD_WP);
	free_irq(TOSA_IRQ_GPIO_nSD_DETECT, data);
	gpio_free(TOSA_GPIO_nSD_DETECT);
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
		gpio_set_value(TOSA_GPIO_IR_POWERDWN, 0);
		pxa2xx_transceiver_mode(dev, mode);
		gpio_direction_output(TOSA_GPIO_IRDA_TX, 0);
	} else {
		pxa2xx_transceiver_mode(dev, mode);
		gpio_set_value(TOSA_GPIO_IR_POWERDWN, 1);
	}
}

static int tosa_irda_startup(struct device *dev)
{
	int ret;

	ret = gpio_request(TOSA_GPIO_IRDA_TX, "IrDA TX");
	if (ret)
		goto err_tx;
	ret = gpio_direction_output(TOSA_GPIO_IRDA_TX, 0);
	if (ret)
		goto err_tx_dir;

	ret = gpio_request(TOSA_GPIO_IR_POWERDWN, "IrDA powerdown");
	if (ret)
		goto err_pwr;

	ret = gpio_direction_output(TOSA_GPIO_IR_POWERDWN, 0);
	if (ret)
		goto err_pwr_dir;

	tosa_irda_transceiver_mode(dev, IR_SIRMODE | IR_OFF);

	return 0;

err_pwr_dir:
	gpio_free(TOSA_GPIO_IR_POWERDWN);
err_pwr:
err_tx_dir:
	gpio_free(TOSA_GPIO_IRDA_TX);
err_tx:
	return ret;
}

static void tosa_irda_shutdown(struct device *dev)
{
	tosa_irda_transceiver_mode(dev, IR_SIRMODE | IR_OFF);
	gpio_free(TOSA_GPIO_IR_POWERDWN);
	gpio_free(TOSA_GPIO_IRDA_TX);
}

static struct pxaficp_platform_data tosa_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = tosa_irda_transceiver_mode,
	.startup = tosa_irda_startup,
	.shutdown = tosa_irda_shutdown,
};

/*
 * Tosa AC IN
 */
static int tosa_power_init(struct device *dev)
{
	int ret = gpio_request(TOSA_GPIO_AC_IN, "ac in");
	if (ret)
		goto err_gpio_req;

	ret = gpio_direction_input(TOSA_GPIO_AC_IN);
	if (ret)
		goto err_gpio_in;

	return 0;

err_gpio_in:
	gpio_free(TOSA_GPIO_AC_IN);
err_gpio_req:
	return ret;
}

static void tosa_power_exit(struct device *dev)
{
	gpio_free(TOSA_GPIO_AC_IN);
}

static int tosa_power_ac_online(void)
{
	return gpio_get_value(TOSA_GPIO_AC_IN) == 0;
}

static char *tosa_ac_supplied_to[] = {
	"main-battery",
	"backup-battery",
	"jacket-battery",
};

static struct pda_power_pdata tosa_power_data = {
	.init			= tosa_power_init,
	.is_ac_online		= tosa_power_ac_online,
	.exit			= tosa_power_exit,
	.supplied_to		= tosa_ac_supplied_to,
	.num_supplicants	= ARRAY_SIZE(tosa_ac_supplied_to),
};

static struct resource tosa_power_resource[] = {
	{
		.name		= "ac",
		.start		= gpio_to_irq(TOSA_GPIO_AC_IN),
		.end		= gpio_to_irq(TOSA_GPIO_AC_IN),
		.flags		= IORESOURCE_IRQ |
				  IORESOURCE_IRQ_HIGHEDGE |
				  IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device tosa_power_device = {
	.name			= "pda-power",
	.id			= -1,
	.dev.platform_data	= &tosa_power_data,
	.resource		= tosa_power_resource,
	.num_resources		= ARRAY_SIZE(tosa_power_resource),
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
	{
		.type	= EV_SW,
		.code	= SW_HEADPHONE_INSERT,
		.gpio	= TOSA_GPIO_EAR_IN,
		.desc	= "HeadPhone insert",
		.active_low = 1,
		.debounce_interval = 300,
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
		.default_trigger	= "tosa-bt",
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

/*
 * Toshiba Mobile IO Controller
 */
static struct resource tc6393xb_resources[] = {
	[0] = {
		.start	= TOSA_LCDC_PHYS,
		.end	= TOSA_LCDC_PHYS + 0x3ffffff,
		.flags	= IORESOURCE_MEM,
	},

	[1] = {
		.start	= TOSA_IRQ_GPIO_TC6393XB_INT,
		.end	= TOSA_IRQ_GPIO_TC6393XB_INT,
		.flags	= IORESOURCE_IRQ,
	},
};


static int tosa_tc6393xb_enable(struct platform_device *dev)
{
	int rc;

	rc = gpio_request(TOSA_GPIO_TC6393XB_REST_IN, "tc6393xb #pclr");
	if (rc)
		goto err_req_pclr;
	rc = gpio_request(TOSA_GPIO_TC6393XB_SUSPEND, "tc6393xb #suspend");
	if (rc)
		goto err_req_suspend;
	rc = gpio_request(TOSA_GPIO_TC6393XB_L3V_ON, "tc6393xb l3v");
	if (rc)
		goto err_req_l3v;
	rc = gpio_direction_output(TOSA_GPIO_TC6393XB_L3V_ON, 0);
	if (rc)
		goto err_dir_l3v;
	rc = gpio_direction_output(TOSA_GPIO_TC6393XB_SUSPEND, 0);
	if (rc)
		goto err_dir_suspend;
	rc = gpio_direction_output(TOSA_GPIO_TC6393XB_REST_IN, 0);
	if (rc)
		goto err_dir_pclr;

	mdelay(1);

	gpio_set_value(TOSA_GPIO_TC6393XB_SUSPEND, 1);

	mdelay(10);

	gpio_set_value(TOSA_GPIO_TC6393XB_REST_IN, 1);
	gpio_set_value(TOSA_GPIO_TC6393XB_L3V_ON, 1);

	return 0;
err_dir_pclr:
err_dir_suspend:
err_dir_l3v:
	gpio_free(TOSA_GPIO_TC6393XB_L3V_ON);
err_req_l3v:
	gpio_free(TOSA_GPIO_TC6393XB_SUSPEND);
err_req_suspend:
	gpio_free(TOSA_GPIO_TC6393XB_REST_IN);
err_req_pclr:
	return rc;
}

static int tosa_tc6393xb_disable(struct platform_device *dev)
{
	gpio_free(TOSA_GPIO_TC6393XB_L3V_ON);
	gpio_free(TOSA_GPIO_TC6393XB_SUSPEND);
	gpio_free(TOSA_GPIO_TC6393XB_REST_IN);

	return 0;
}

static int tosa_tc6393xb_resume(struct platform_device *dev)
{
	gpio_set_value(TOSA_GPIO_TC6393XB_SUSPEND, 1);
	mdelay(10);
	gpio_set_value(TOSA_GPIO_TC6393XB_L3V_ON, 1);
	mdelay(10);

	return 0;
}

static int tosa_tc6393xb_suspend(struct platform_device *dev)
{
	gpio_set_value(TOSA_GPIO_TC6393XB_L3V_ON, 0);
	gpio_set_value(TOSA_GPIO_TC6393XB_SUSPEND, 0);
	return 0;
}

static struct mtd_partition tosa_nand_partition[] = {
	{
		.name	= "smf",
		.offset	= 0,
		.size	= 7 * 1024 * 1024,
	},
	{
		.name	= "root",
		.offset	= MTDPART_OFS_APPEND,
		.size	= 28 * 1024 * 1024,
	},
	{
		.name	= "home",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr tosa_tc6393xb_nand_bbt = {
	.options	= 0,
	.offs		= 4,
	.len		= 2,
	.pattern	= scan_ff_pattern
};

static struct tmio_nand_data tosa_tc6393xb_nand_config = {
	.num_partitions	= ARRAY_SIZE(tosa_nand_partition),
	.partition	= tosa_nand_partition,
	.badblock_pattern = &tosa_tc6393xb_nand_bbt,
};

static int tosa_tc6393xb_setup(struct platform_device *dev)
{
	int rc;

	rc = gpio_request(TOSA_GPIO_CARD_VCC_ON, "CARD_VCC_ON");
	if (rc)
		goto err_req;

	rc = gpio_direction_output(TOSA_GPIO_CARD_VCC_ON, 1);
	if (rc)
		goto err_dir;

	return rc;

err_dir:
	gpio_free(TOSA_GPIO_CARD_VCC_ON);
err_req:
	return rc;
}

static void tosa_tc6393xb_teardown(struct platform_device *dev)
{
	gpio_free(TOSA_GPIO_CARD_VCC_ON);
}

#ifdef CONFIG_MFD_TC6393XB
static struct fb_videomode tosa_tc6393xb_lcd_mode[] = {
	{
		.xres = 480,
		.yres = 640,
		.pixclock = 0x002cdf00,/* PLL divisor */
		.left_margin = 0x004c,
		.right_margin = 0x005b,
		.upper_margin = 0x0001,
		.lower_margin = 0x000d,
		.hsync_len = 0x0002,
		.vsync_len = 0x0001,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode = FB_VMODE_NONINTERLACED,
	},{
		.xres = 240,
		.yres = 320,
		.pixclock = 0x00e7f203,/* PLL divisor */
		.left_margin = 0x0024,
		.right_margin = 0x002f,
		.upper_margin = 0x0001,
		.lower_margin = 0x000d,
		.hsync_len = 0x0002,
		.vsync_len = 0x0001,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode = FB_VMODE_NONINTERLACED,
	}
};

static struct tmio_fb_data tosa_tc6393xb_fb_config = {
	.lcd_set_power	= tc6393xb_lcd_set_power,
	.lcd_mode	= tc6393xb_lcd_mode,
	.num_modes	= ARRAY_SIZE(tosa_tc6393xb_lcd_mode),
	.modes		= &tosa_tc6393xb_lcd_mode[0],
	.height		= 82,
	.width		= 60,
};
#endif

static struct tc6393xb_platform_data tosa_tc6393xb_data = {
	.scr_pll2cr	= 0x0cc1,
	.scr_gper	= 0x3300,

	.irq_base	= IRQ_BOARD_START,
	.gpio_base	= TOSA_TC6393XB_GPIO_BASE,
	.setup		= tosa_tc6393xb_setup,
	.teardown	= tosa_tc6393xb_teardown,

	.enable		= tosa_tc6393xb_enable,
	.disable	= tosa_tc6393xb_disable,
	.suspend	= tosa_tc6393xb_suspend,
	.resume		= tosa_tc6393xb_resume,

	.nand_data	= &tosa_tc6393xb_nand_config,
#ifdef CONFIG_MFD_TC6393XB
	.fb_data	= &tosa_tc6393xb_fb_config,
#endif

	.resume_restore = 1,
};


static struct platform_device tc6393xb_device = {
	.name	= "tc6393xb",
	.id	= -1,
	.dev	= {
		.platform_data	= &tosa_tc6393xb_data,
	},
	.num_resources	= ARRAY_SIZE(tc6393xb_resources),
	.resource	= tc6393xb_resources,
};

static struct tosa_bt_data tosa_bt_data = {
	.gpio_pwr	= TOSA_GPIO_BT_PWR_EN,
	.gpio_reset	= TOSA_GPIO_BT_RESET,
};

static struct platform_device tosa_bt_device = {
	.name	= "tosa-bt",
	.id	= -1,
	.dev.platform_data = &tosa_bt_data,
};

static struct pxa2xx_spi_master pxa_ssp_master_info = {
	.num_chipselect	= 1,
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias	= "tosa-lcd",
		// .platform_data
		.max_speed_hz	= 28750,
		.bus_num	= 2,
		.chip_select	= 0,
		.mode		= SPI_MODE_0,
	},
};

static struct mtd_partition sharpsl_rom_parts[] = {
	{
		.name	="Boot PROM Filesystem",
		.offset	= 0x00160000,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data sharpsl_rom_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(sharpsl_rom_parts),
	.parts		= sharpsl_rom_parts,
};

static struct resource sharpsl_rom_resources[] = {
	{
		.start	= 0x00000000,
		.end	= 0x007fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device sharpsl_rom_device = {
	.name	= "physmap-flash",
	.id	= -1,
	.resource = sharpsl_rom_resources,
	.num_resources = ARRAY_SIZE(sharpsl_rom_resources),
	.dev.platform_data = &sharpsl_rom_data,
};

static struct platform_device *devices[] __initdata = {
	&tosascoop_device,
	&tosascoop_jc_device,
	&tc6393xb_device,
	&tosa_power_device,
	&tosakbd_device,
	&tosa_gpio_keys_device,
	&tosaled_device,
	&tosa_bt_device,
	&sharpsl_rom_device,
};

static void tosa_poweroff(void)
{
	arm_machine_restart('g', NULL);
}

static void tosa_restart(char mode, const char *cmd)
{
	/* Bootloader magic for a reboot */
	if((MSC0 & 0xffff0000) == 0x7ff00000)
		MSC0 = (MSC0 & 0xffff) | 0x7ee00000;

	tosa_poweroff();
}

static void __init tosa_init(void)
{
	int dummy;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(tosa_pin_config));
	gpio_set_wake(MFP_PIN_GPIO1, 1);
	/* We can't pass to gpio-keys since it will drop the Reset altfunc */

	init_gpio_reset(TOSA_GPIO_ON_RESET, 0);

	pm_power_off = tosa_poweroff;
	arm_pm_restart = tosa_restart;

	PCFR |= PCFR_OPDE;

	/* enable batt_fault */
	PMCR = 0x01;

	dummy = gpiochip_reserve(TOSA_SCOOP_GPIO_BASE, 12);
	dummy = gpiochip_reserve(TOSA_SCOOP_JC_GPIO_BASE, 12);
	dummy = gpiochip_reserve(TOSA_TC6393XB_GPIO_BASE, 16);

	pxa_set_mci_info(&tosa_mci_platform_data);
	pxa_set_udc_info(&udc_info);
	pxa_set_ficp_info(&tosa_ficp_platform_data);
	pxa_set_i2c_info(NULL);
	pxa_set_ac97_info(NULL);
	platform_scoop_config = &tosa_pcmcia_config;

	pxa2xx_set_spi_info(2, &pxa_ssp_master_info);
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	clk_add_alias("CLK_CK3P6MI", tc6393xb_device.name, "GPIO11_CLK", NULL);

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
