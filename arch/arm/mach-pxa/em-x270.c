/*
 * Support for CompuLab EM-X270 platform
 *
 * Copyright (C) 2007, 2008 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irq.h>
#include <linux/platform_device.h>

#include <linux/dm9000.h>
#include <linux/rtc-v3020.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/mfp-pxa27x.h>
#include <mach/pxa-regs.h>
#include <mach/pxa27x-udc.h>
#include <mach/audio.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/mmc.h>
#include <mach/pxa27x_keypad.h>

#include "generic.h"

/* GPIO IRQ usage */
#define GPIO41_ETHIRQ		(41)
#define GPIO13_MMC_CD		(13)
#define EM_X270_ETHIRQ		IRQ_GPIO(GPIO41_ETHIRQ)
#define EM_X270_MMC_CD		IRQ_GPIO(GPIO13_MMC_CD)

/* NAND control GPIOs */
#define GPIO11_NAND_CS	(11)
#define GPIO56_NAND_RB	(56)

static unsigned long em_x270_pin_config[] = {
	/* AC'97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO98_AC97_SYSCLK,
	GPIO113_AC97_nRESET,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* STUART */
	GPIO46_STUART_RXD,
	GPIO47_STUART_TXD,

	/* MCI controller */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,

	/* LCD */
	GPIO58_LCD_LDD_0,
	GPIO59_LCD_LDD_1,
	GPIO60_LCD_LDD_2,
	GPIO61_LCD_LDD_3,
	GPIO62_LCD_LDD_4,
	GPIO63_LCD_LDD_5,
	GPIO64_LCD_LDD_6,
	GPIO65_LCD_LDD_7,
	GPIO66_LCD_LDD_8,
	GPIO67_LCD_LDD_9,
	GPIO68_LCD_LDD_10,
	GPIO69_LCD_LDD_11,
	GPIO70_LCD_LDD_12,
	GPIO71_LCD_LDD_13,
	GPIO72_LCD_LDD_14,
	GPIO73_LCD_LDD_15,
	GPIO74_LCD_FCLK,
	GPIO75_LCD_LCLK,
	GPIO76_LCD_PCLK,
	GPIO77_LCD_BIAS,

	/* QCI */
	GPIO84_CIF_FV,
	GPIO25_CIF_LV,
	GPIO53_CIF_MCLK,
	GPIO54_CIF_PCLK,
	GPIO81_CIF_DD_0,
	GPIO55_CIF_DD_1,
	GPIO51_CIF_DD_2,
	GPIO50_CIF_DD_3,
	GPIO52_CIF_DD_4,
	GPIO48_CIF_DD_5,
	GPIO17_CIF_DD_6,
	GPIO12_CIF_DD_7,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* Keypad */
	GPIO100_KP_MKIN_0	| WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1	| WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2	| WAKEUP_ON_LEVEL_HIGH,
	GPIO34_KP_MKIN_3	| WAKEUP_ON_LEVEL_HIGH,
	GPIO39_KP_MKIN_4	| WAKEUP_ON_LEVEL_HIGH,
	GPIO99_KP_MKIN_5	| WAKEUP_ON_LEVEL_HIGH,
	GPIO91_KP_MKIN_6	| WAKEUP_ON_LEVEL_HIGH,
	GPIO36_KP_MKIN_7	| WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,
	GPIO106_KP_MKOUT_3,
	GPIO107_KP_MKOUT_4,
	GPIO108_KP_MKOUT_5,
	GPIO96_KP_MKOUT_6,
	GPIO22_KP_MKOUT_7,

	/* SSP1 */
	GPIO26_SSP1_RXD,
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO57_SSP1_TXD,

	/* SSP2 */
	GPIO19_SSP2_SCLK,
	GPIO14_SSP2_SFRM,
	GPIO89_SSP2_TXD,
	GPIO88_SSP2_RXD,

	/* SDRAM and local bus */
	GPIO15_nCS_1,
	GPIO78_nCS_2,
	GPIO79_nCS_3,
	GPIO80_nCS_4,
	GPIO49_nPWE,
	GPIO18_RDY,

	/* GPIO */
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,

	/* power controls */
	GPIO20_GPIO	| MFP_LPM_DRIVE_LOW,	/* GPRS_PWEN */
	GPIO115_GPIO	| MFP_LPM_DRIVE_LOW,	/* WLAN_PWEN */

	/* NAND controls */
	GPIO11_GPIO	| MFP_LPM_DRIVE_HIGH,	/* NAND CE# */
	GPIO56_GPIO,				/* NAND Ready/Busy */

	/* interrupts */
	GPIO13_GPIO,	/* MMC card detect */
	GPIO41_GPIO,	/* DM9000 interrupt */
};

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource em_x270_dm9000_resource[] = {
	[0] = {
		.start = PXA_CS2_PHYS,
		.end   = PXA_CS2_PHYS + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PXA_CS2_PHYS + 8,
		.end   = PXA_CS2_PHYS + 8 + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = EM_X270_ETHIRQ,
		.end   = EM_X270_ETHIRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct dm9000_plat_data em_x270_dm9000_platdata = {
	.flags		= DM9000_PLATF_32BITONLY,
};

static struct platform_device em_x270_dm9000 = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(em_x270_dm9000_resource),
	.resource	= em_x270_dm9000_resource,
	.dev		= {
		.platform_data = &em_x270_dm9000_platdata,
	}
};

static void __init em_x270_init_dm9000(void)
{
	platform_device_register(&em_x270_dm9000);
}
#else
static inline void em_x270_init_dm9000(void) {}
#endif

/* V3020 RTC */
#if defined(CONFIG_RTC_DRV_V3020) || defined(CONFIG_RTC_DRV_V3020_MODULE)
static struct resource em_x270_v3020_resource[] = {
	[0] = {
		.start = PXA_CS4_PHYS,
		.end   = PXA_CS4_PHYS + 3,
		.flags = IORESOURCE_MEM,
	},
};

static struct v3020_platform_data em_x270_v3020_platdata = {
	.leftshift = 0,
};

static struct platform_device em_x270_rtc = {
	.name		= "v3020",
	.num_resources	= ARRAY_SIZE(em_x270_v3020_resource),
	.resource	= em_x270_v3020_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &em_x270_v3020_platdata,
	}
};

static void __init em_x270_init_rtc(void)
{
	platform_device_register(&em_x270_rtc);
}
#else
static inline void em_x270_init_rtc(void) {}
#endif

/* NAND flash */
#if defined(CONFIG_MTD_NAND_PLATFORM) || defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
static inline void nand_cs_on(void)
{
	gpio_set_value(GPIO11_NAND_CS, 0);
}

static void nand_cs_off(void)
{
	dsb();

	gpio_set_value(GPIO11_NAND_CS, 1);
}

/* hardware specific access to control-lines */
static void em_x270_nand_cmd_ctl(struct mtd_info *mtd, int dat,
				 unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	unsigned long nandaddr = (unsigned long)this->IO_ADDR_W;

	dsb();

	if (ctrl & NAND_CTRL_CHANGE) {
		if (ctrl & NAND_ALE)
			nandaddr |=  (1 << 3);
		else
			nandaddr &= ~(1 << 3);
		if (ctrl & NAND_CLE)
			nandaddr |=  (1 << 2);
		else
			nandaddr &= ~(1 << 2);
		if (ctrl & NAND_NCE)
			nand_cs_on();
		else
			nand_cs_off();
	}

	dsb();
	this->IO_ADDR_W = (void __iomem *)nandaddr;
	if (dat != NAND_CMD_NONE)
		writel(dat, this->IO_ADDR_W);

	dsb();
}

/* read device ready pin */
static int em_x270_nand_device_ready(struct mtd_info *mtd)
{
	dsb();

	return gpio_get_value(GPIO56_NAND_RB);
}

static struct mtd_partition em_x270_partition_info[] = {
	[0] = {
		.name	= "em_x270-0",
		.offset	= 0,
		.size	= SZ_4M,
	},
	[1] = {
		.name	= "em_x270-1",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL
	},
};

static const char *em_x270_part_probes[] = { "cmdlinepart", NULL };

struct platform_nand_data em_x270_nand_platdata = {
	.chip = {
		.nr_chips = 1,
		.chip_offset = 0,
		.nr_partitions = ARRAY_SIZE(em_x270_partition_info),
		.partitions = em_x270_partition_info,
		.chip_delay = 20,
		.part_probe_types = em_x270_part_probes,
	},
	.ctrl = {
		.hwcontrol = 0,
		.dev_ready = em_x270_nand_device_ready,
		.select_chip = 0,
		.cmd_ctrl = em_x270_nand_cmd_ctl,
	},
};

static struct resource em_x270_nand_resource[] = {
	[0] = {
		.start = PXA_CS1_PHYS,
		.end   = PXA_CS1_PHYS + 12,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device em_x270_nand = {
	.name		= "gen_nand",
	.num_resources	= ARRAY_SIZE(em_x270_nand_resource),
	.resource	= em_x270_nand_resource,
	.id		= -1,
	.dev		= {
		.platform_data = &em_x270_nand_platdata,
	}
};

static void __init em_x270_init_nand(void)
{
	int err;

	err = gpio_request(GPIO11_NAND_CS, "NAND CS");
	if (err) {
		pr_warning("EM-X270: failed to request NAND CS gpio\n");
		return;
	}

	gpio_direction_output(GPIO11_NAND_CS, 1);

	err = gpio_request(GPIO56_NAND_RB, "NAND R/B");
	if (err) {
		pr_warning("EM-X270: failed to request NAND R/B gpio\n");
		gpio_free(GPIO11_NAND_CS);
		return;
	}

	gpio_direction_input(GPIO56_NAND_RB);

	platform_device_register(&em_x270_nand);
}
#else
static inline void em_x270_init_nand(void) {}
#endif

/* PXA27x OHCI controller setup */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int em_x270_ohci_init(struct device *dev)
{
	/* enable port 2 transiever */
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE;

	return 0;
}

static struct pxaohci_platform_data em_x270_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 | POWER_CONTROL_LOW,
	.init		= em_x270_ohci_init,
};

static void __init em_x270_init_ohci(void)
{
	pxa_set_ohci_info(&em_x270_ohci_platform_data);
}
#else
static inline void em_x270_init_ohci(void) {}
#endif

/* MCI controller setup */
#if defined(CONFIG_MMC) || defined(CONFIG_MMC_MODULE)
static int em_x270_mci_init(struct device *dev,
			    irq_handler_t em_x270_detect_int,
			    void *data)
{
	int err = request_irq(EM_X270_MMC_CD, em_x270_detect_int,
			      IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			      "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "%s: can't request MMC card detect IRQ: %d\n",
		       __func__, err);
		return err;
	}

	return 0;
}

static void em_x270_mci_setpower(struct device *dev, unsigned int vdd)
{
	/*
	   FIXME: current hardware implementation does not allow to
	   enable/disable MMC power. This will be fixed in next HW releases,
	   and we'll need to add implmentation here.
	*/
	return;
}

static void em_x270_mci_exit(struct device *dev, void *data)
{
	int irq = gpio_to_irq(GPIO13_MMC_CD);
	free_irq(irq, data);
}

static struct pxamci_platform_data em_x270_mci_platform_data = {
	.ocr_mask	= MMC_VDD_28_29|MMC_VDD_29_30|MMC_VDD_30_31,
	.init 		= em_x270_mci_init,
	.setpower 	= em_x270_mci_setpower,
	.exit		= em_x270_mci_exit,
};

static void __init em_x270_init_mmc(void)
{
	pxa_set_mci_info(&em_x270_mci_platform_data);
}
#else
static inline void em_x270_init_mmc(void) {}
#endif

/* LCD 480x640 */
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info em_x270_lcd_mode = {
	.pixclock	= 50000,
	.bpp		= 16,
	.xres		= 480,
	.yres		= 640,
	.hsync_len	= 8,
	.vsync_len	= 2,
	.left_margin	= 8,
	.upper_margin	= 0,
	.right_margin	= 24,
	.lower_margin	= 4,
	.cmap_greyscale	= 0,
};

static struct pxafb_mach_info em_x270_lcd = {
	.modes		= &em_x270_lcd_mode,
	.num_modes	= 1,
	.lcd_conn	= LCD_COLOR_TFT_16BPP,
};
static void __init em_x270_init_lcd(void)
{
	set_pxa_fb_info(&em_x270_lcd);
}
#else
static inline void em_x270_init_lcd(void) {}
#endif

#if defined(CONFIG_SND_PXA2XX_AC97) || defined(CONFIG_SND_PXA2XX_AC97_MODULE)
static void __init em_x270_init_ac97(void)
{
	pxa_set_ac97_info(NULL);
}
#else
static inline void em_x270_init_ac97(void) {}
#endif

#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int em_x270_matrix_keys[] = {
	KEY(0, 0, KEY_A), KEY(1, 0, KEY_UP), KEY(2, 1, KEY_B),
	KEY(0, 2, KEY_LEFT), KEY(1, 1, KEY_ENTER), KEY(2, 0, KEY_RIGHT),
	KEY(0, 1, KEY_C), KEY(1, 2, KEY_DOWN), KEY(2, 2, KEY_D),
};

struct pxa27x_keypad_platform_data em_x270_keypad_info = {
	/* code map for the matrix keys */
	.matrix_key_rows	= 3,
	.matrix_key_cols	= 3,
	.matrix_key_map		= em_x270_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(em_x270_matrix_keys),
};

static void __init em_x270_init_keypad(void)
{
	pxa_set_keypad_info(&em_x270_keypad_info);
}
#else
static inline void em_x270_init_keypad(void) {}
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button gpio_keys_button[] = {
	[0] = {
		.desc	= "sleep/wakeup",
		.code	= KEY_SUSPEND,
		.type	= EV_PWR,
		.gpio	= 1,
		.wakeup	= 1,
	},
};

static struct gpio_keys_platform_data em_x270_gpio_keys_data = {
	.buttons	= gpio_keys_button,
	.nbuttons	= 1,
};

static struct platform_device em_x270_gpio_keys = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &em_x270_gpio_keys_data,
	},
};

static void __init em_x270_init_gpio_keys(void)
{
	platform_device_register(&em_x270_gpio_keys);
}
#else
static inline void em_x270_init_gpio_keys(void) {}
#endif

static void __init em_x270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(em_x270_pin_config));

	em_x270_init_dm9000();
	em_x270_init_rtc();
	em_x270_init_nand();
	em_x270_init_lcd();
	em_x270_init_mmc();
	em_x270_init_ohci();
	em_x270_init_keypad();
	em_x270_init_gpio_keys();
	em_x270_init_ac97();
}

MACHINE_START(EM_X270, "Compulab EM-X270")
	.boot_params	= 0xa0000100,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= em_x270_init,
MACHINE_END
