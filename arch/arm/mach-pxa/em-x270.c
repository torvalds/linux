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
#include <linux/delay.h>

#include <linux/dm9000.h>
#include <linux/rtc-v3020.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/mfd/da903x.h>
#include <linux/regulator/machine.h>
#include <linux/spi/spi.h>
#include <linux/spi/tdo24m.h>
#include <linux/spi/libertas_spi.h>
#include <linux/power_supply.h>
#include <linux/apm-emulation.h>
#include <linux/i2c.h>
#include <linux/i2c/pca953x.h>
#include <linux/regulator/userspace-consumer.h>

#include <media/soc_camera.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa27x.h>
#include <mach/pxa27x-udc.h>
#include <mach/audio.h>
#include <mach/pxafb.h>
#include <mach/ohci.h>
#include <mach/mmc.h>
#include <mach/pxa27x_keypad.h>
#include <plat/i2c.h>
#include <mach/camera.h>
#include <mach/pxa2xx_spi.h>

#include "generic.h"
#include "devices.h"

/* EM-X270 specific GPIOs */
#define GPIO13_MMC_CD		(13)
#define GPIO95_MMC_WP		(95)
#define GPIO56_NAND_RB		(56)
#define GPIO93_CAM_RESET	(93)
#define GPIO16_USB_HUB_RESET	(16)

/* eXeda specific GPIOs */
#define GPIO114_MMC_CD		(114)
#define GPIO20_NAND_RB		(20)
#define GPIO38_SD_PWEN		(38)
#define GPIO37_WLAN_RST		(37)
#define GPIO95_TOUCHPAD_INT	(95)
#define GPIO130_CAM_RESET	(130)
#define GPIO10_USB_HUB_RESET	(10)

/* common  GPIOs */
#define GPIO11_NAND_CS		(11)
#define GPIO41_ETHIRQ		(41)
#define EM_X270_ETHIRQ		IRQ_GPIO(GPIO41_ETHIRQ)
#define GPIO115_WLAN_PWEN	(115)
#define GPIO19_WLAN_STRAP	(19)
#define GPIO9_USB_VBUS_EN	(9)

static int mmc_cd;
static int nand_rb;
static int dm9000_flags;
static int cam_reset;
static int usb_hub_reset;

static unsigned long common_pin_config[] = {
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
	GPIO19_GPIO,	/* SSP2 clock is used as GPIO for Libertas pin-strap */
	GPIO14_GPIO,
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
	GPIO1_GPIO | WAKEUP_ON_EDGE_BOTH,	/* sleep/resume button */

	/* power controls */
	GPIO20_GPIO	| MFP_LPM_DRIVE_LOW,	/* GPRS_PWEN */
	GPIO115_GPIO	| MFP_LPM_DRIVE_LOW,	/* WLAN_PWEN */

	/* NAND controls */
	GPIO11_GPIO	| MFP_LPM_DRIVE_HIGH,	/* NAND CE# */

	/* interrupts */
	GPIO41_GPIO,	/* DM9000 interrupt */
};

static unsigned long em_x270_pin_config[] = {
	GPIO13_GPIO,				/* MMC card detect */
	GPIO16_GPIO,				/* USB hub reset */
	GPIO56_GPIO,				/* NAND Ready/Busy */
	GPIO93_GPIO	| MFP_LPM_DRIVE_LOW,	/* Camera reset */
	GPIO95_GPIO,				/* MMC Write protect */
};

static unsigned long exeda_pin_config[] = {
	GPIO10_GPIO,				/* USB hub reset */
	GPIO20_GPIO,				/* NAND Ready/Busy */
	GPIO38_GPIO	| MFP_LPM_DRIVE_LOW,	/* SD slot power */
	GPIO95_GPIO,				/* touchpad IRQ */
	GPIO114_GPIO,				/* MMC card detect */
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
	.flags		= DM9000_PLATF_NO_EEPROM,
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
	em_x270_dm9000_platdata.flags |= dm9000_flags;
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

	return gpio_get_value(nand_rb);
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

	err = gpio_request(nand_rb, "NAND R/B");
	if (err) {
		pr_warning("EM-X270: failed to request NAND R/B gpio\n");
		gpio_free(GPIO11_NAND_CS);
		return;
	}

	gpio_direction_input(nand_rb);

	platform_device_register(&em_x270_nand);
}
#else
static inline void em_x270_init_nand(void) {}
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition em_x270_nor_parts[] = {
	{
		.name =		"Bootloader",
		.offset =	0x00000000,
		.size =		0x00050000,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	}, {
		.name =		"Environment",
		.offset =	0x00050000,
		.size =		0x00010000,
	}, {
		.name =		"Reserved",
		.offset =	0x00060000,
		.size =		0x00050000,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	}, {
		.name =		"Splashscreen",
		.offset =	0x000b0000,
		.size =		0x00050000,
	}
};

static struct physmap_flash_data em_x270_nor_data[] = {
	[0] = {
		.width = 2,
		.parts = em_x270_nor_parts,
		.nr_parts = ARRAY_SIZE(em_x270_nor_parts),
	},
};

static struct resource em_x270_nor_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_1M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device em_x270_physmap_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.num_resources	= 1,
	.resource	= &em_x270_nor_flash_resource,
	.dev		= {
		.platform_data	= &em_x270_nor_data,
	},
};

static void __init em_x270_init_nor(void)
{
	platform_device_register(&em_x270_physmap_flash);
}
#else
static inline void em_x270_init_nor(void) {}
#endif

/* PXA27x OHCI controller setup */
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static struct regulator *em_x270_usb_ldo;

static int em_x270_usb_hub_init(void)
{
	int err;

	em_x270_usb_ldo = regulator_get(NULL, "vcc usb");
	if (IS_ERR(em_x270_usb_ldo))
		return PTR_ERR(em_x270_usb_ldo);

	err = gpio_request(GPIO9_USB_VBUS_EN, "vbus en");
	if (err)
		goto err_free_usb_ldo;

	err = gpio_request(usb_hub_reset, "hub rst");
	if (err)
		goto err_free_vbus_gpio;

	/* USB Hub power-on and reset */
	gpio_direction_output(usb_hub_reset, 0);
	regulator_enable(em_x270_usb_ldo);
	gpio_set_value(usb_hub_reset, 1);
	gpio_set_value(usb_hub_reset, 0);
	regulator_disable(em_x270_usb_ldo);
	regulator_enable(em_x270_usb_ldo);
	gpio_set_value(usb_hub_reset, 1);

	/* enable VBUS */
	gpio_direction_output(GPIO9_USB_VBUS_EN, 1);

	return 0;

err_free_vbus_gpio:
	gpio_free(GPIO9_USB_VBUS_EN);
err_free_usb_ldo:
	regulator_put(em_x270_usb_ldo);

	return err;
}

static int em_x270_ohci_init(struct device *dev)
{
	int err;

	/* we don't want to entirely disable USB if the HUB init failed */
	err = em_x270_usb_hub_init();
	if (err)
		pr_err("USB Hub initialization failed: %d\n", err);

	/* enable port 2 transiever */
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE;

	return 0;
}

static void em_x270_ohci_exit(struct device *dev)
{
	gpio_free(usb_hub_reset);
	gpio_free(GPIO9_USB_VBUS_EN);

	if (!IS_ERR(em_x270_usb_ldo)) {
		if (regulator_is_enabled(em_x270_usb_ldo))
			regulator_disable(em_x270_usb_ldo);

		regulator_put(em_x270_usb_ldo);
	}
}

static struct pxaohci_platform_data em_x270_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.flags		= ENABLE_PORT1 | ENABLE_PORT2 | POWER_CONTROL_LOW,
	.init		= em_x270_ohci_init,
	.exit		= em_x270_ohci_exit,
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
static struct regulator *em_x270_sdio_ldo;

static int em_x270_mci_init(struct device *dev,
			    irq_handler_t em_x270_detect_int,
			    void *data)
{
	int err;

	em_x270_sdio_ldo = regulator_get(dev, "vcc sdio");
	if (IS_ERR(em_x270_sdio_ldo)) {
		dev_err(dev, "can't request SDIO power supply: %ld\n",
			PTR_ERR(em_x270_sdio_ldo));
		return PTR_ERR(em_x270_sdio_ldo);
	}

	err = request_irq(gpio_to_irq(mmc_cd), em_x270_detect_int,
			      IRQF_DISABLED | IRQF_TRIGGER_RISING |
			      IRQF_TRIGGER_FALLING,
			      "MMC card detect", data);
	if (err) {
		dev_err(dev, "can't request MMC card detect IRQ: %d\n", err);
		goto err_irq;
	}

	if (machine_is_em_x270()) {
		err = gpio_request(GPIO95_MMC_WP, "MMC WP");
		if (err) {
			dev_err(dev, "can't request MMC write protect: %d\n",
				err);
			goto err_gpio_wp;
		}
		gpio_direction_input(GPIO95_MMC_WP);
	} else {
		err = gpio_request(GPIO38_SD_PWEN, "sdio power");
		if (err) {
			dev_err(dev, "can't request MMC power control : %d\n",
				err);
			goto err_gpio_wp;
		}
		gpio_direction_output(GPIO38_SD_PWEN, 1);
	}

	return 0;

err_gpio_wp:
	free_irq(gpio_to_irq(mmc_cd), data);
err_irq:
	regulator_put(em_x270_sdio_ldo);

	return err;
}

static void em_x270_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if ((1 << vdd) & p_d->ocr_mask) {
		int vdd_uV = (2000 + (vdd - __ffs(MMC_VDD_20_21)) * 100) * 1000;

		regulator_set_voltage(em_x270_sdio_ldo, vdd_uV, vdd_uV);
		regulator_enable(em_x270_sdio_ldo);
	} else {
		regulator_disable(em_x270_sdio_ldo);
	}
}

static void em_x270_mci_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(mmc_cd), data);
	regulator_put(em_x270_sdio_ldo);

	if (machine_is_em_x270())
		gpio_free(GPIO95_MMC_WP);
	else
		gpio_free(GPIO38_SD_PWEN);
}

static int em_x270_mci_get_ro(struct device *dev)
{
	return gpio_get_value(GPIO95_MMC_WP);
}

static struct pxamci_platform_data em_x270_mci_platform_data = {
	.ocr_mask	= MMC_VDD_20_21|MMC_VDD_21_22|MMC_VDD_22_23|
			  MMC_VDD_24_25|MMC_VDD_25_26|MMC_VDD_26_27|
			  MMC_VDD_27_28|MMC_VDD_28_29|MMC_VDD_29_30|
			  MMC_VDD_30_31|MMC_VDD_31_32,
	.init 		= em_x270_mci_init,
	.setpower 	= em_x270_mci_setpower,
	.exit		= em_x270_mci_exit,
};

static void __init em_x270_init_mmc(void)
{
	if (machine_is_em_x270())
		em_x270_mci_platform_data.get_ro = em_x270_mci_get_ro;

	em_x270_mci_platform_data.detect_delay	= msecs_to_jiffies(250);
	pxa_set_mci_info(&em_x270_mci_platform_data);
}
#else
static inline void em_x270_init_mmc(void) {}
#endif

/* LCD */
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info em_x270_lcd_modes[] = {
	[0] = {
		.pixclock	= 38250,
		.bpp		= 16,
		.xres		= 480,
		.yres		= 640,
		.hsync_len	= 8,
		.vsync_len	= 2,
		.left_margin	= 8,
		.upper_margin	= 2,
		.right_margin	= 24,
		.lower_margin	= 4,
		.sync		= 0,
	},
	[1] = {
		.pixclock       = 153800,
		.bpp		= 16,
		.xres		= 240,
		.yres		= 320,
		.hsync_len	= 8,
		.vsync_len	= 2,
		.left_margin	= 8,
		.upper_margin	= 2,
		.right_margin	= 88,
		.lower_margin	= 2,
		.sync		= 0,
	},
};

static struct pxafb_mach_info em_x270_lcd = {
	.modes		= em_x270_lcd_modes,
	.num_modes	= 2,
	.lcd_conn	= LCD_COLOR_TFT_16BPP,
};

static void __init em_x270_init_lcd(void)
{
	set_pxa_fb_info(&em_x270_lcd);
}
#else
static inline void em_x270_init_lcd(void) {}
#endif

#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MODULE)
static struct pxa2xx_spi_master em_x270_spi_info = {
	.num_chipselect	= 1,
};

static struct pxa2xx_spi_chip em_x270_tdo24m_chip = {
	.rx_threshold	= 1,
	.tx_threshold	= 1,
	.gpio_cs	= -1,
};

static struct tdo24m_platform_data em_x270_tdo24m_pdata = {
	.model = TDO35S,
};

static struct pxa2xx_spi_master em_x270_spi_2_info = {
	.num_chipselect	= 1,
	.enable_dma	= 1,
};

static struct pxa2xx_spi_chip em_x270_libertas_chip = {
	.rx_threshold	= 1,
	.tx_threshold	= 1,
	.timeout	= 1000,
	.gpio_cs	= 14,
};

static unsigned long em_x270_libertas_pin_config[] = {
	/* SSP2 */
	GPIO19_SSP2_SCLK,
	GPIO14_GPIO,
	GPIO89_SSP2_TXD,
	GPIO88_SSP2_RXD,
};

static int em_x270_libertas_setup(struct spi_device *spi)
{
	int err = gpio_request(GPIO115_WLAN_PWEN, "WLAN PWEN");
	if (err)
		return err;

	err = gpio_request(GPIO19_WLAN_STRAP, "WLAN STRAP");
	if (err)
		goto err_free_pwen;

	if (machine_is_exeda()) {
		err = gpio_request(GPIO37_WLAN_RST, "WLAN RST");
		if (err)
			goto err_free_strap;

		gpio_direction_output(GPIO37_WLAN_RST, 1);
		msleep(100);
	}

	gpio_direction_output(GPIO19_WLAN_STRAP, 1);
	msleep(100);

	pxa2xx_mfp_config(ARRAY_AND_SIZE(em_x270_libertas_pin_config));

	gpio_direction_output(GPIO115_WLAN_PWEN, 0);
	msleep(100);
	gpio_set_value(GPIO115_WLAN_PWEN, 1);
	msleep(100);

	spi->bits_per_word = 16;
	spi_setup(spi);

	return 0;

err_free_strap:
	gpio_free(GPIO19_WLAN_STRAP);
err_free_pwen:
	gpio_free(GPIO115_WLAN_PWEN);

	return err;
}

static int em_x270_libertas_teardown(struct spi_device *spi)
{
	gpio_set_value(GPIO115_WLAN_PWEN, 0);
	gpio_free(GPIO115_WLAN_PWEN);
	gpio_free(GPIO19_WLAN_STRAP);

	if (machine_is_exeda()) {
		gpio_set_value(GPIO37_WLAN_RST, 0);
		gpio_free(GPIO37_WLAN_RST);
	}

	return 0;
}

struct libertas_spi_platform_data em_x270_libertas_pdata = {
	.use_dummy_writes	= 1,
	.setup			= em_x270_libertas_setup,
	.teardown		= em_x270_libertas_teardown,
};

static struct spi_board_info em_x270_spi_devices[] __initdata = {
	{
		.modalias		= "tdo24m",
		.max_speed_hz		= 1000000,
		.bus_num		= 1,
		.chip_select		= 0,
		.controller_data	= &em_x270_tdo24m_chip,
		.platform_data		= &em_x270_tdo24m_pdata,
	},
	{
		.modalias		= "libertas_spi",
		.max_speed_hz		= 13000000,
		.bus_num		= 2,
		.irq			= IRQ_GPIO(116),
		.chip_select		= 0,
		.controller_data	= &em_x270_libertas_chip,
		.platform_data		= &em_x270_libertas_pdata,
	},
};

static void __init em_x270_init_spi(void)
{
	pxa2xx_set_spi_info(1, &em_x270_spi_info);
	pxa2xx_set_spi_info(2, &em_x270_spi_2_info);
	spi_register_board_info(ARRAY_AND_SIZE(em_x270_spi_devices));
}
#else
static inline void em_x270_init_spi(void) {}
#endif

#if defined(CONFIG_SND_PXA2XX_LIB_AC97)
static pxa2xx_audio_ops_t em_x270_ac97_info = {
	.reset_gpio = 113,
};

static void __init em_x270_init_ac97(void)
{
	pxa_set_ac97_info(&em_x270_ac97_info);
}
#else
static inline void em_x270_init_ac97(void) {}
#endif

#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static unsigned int em_x270_module_matrix_keys[] = {
	KEY(0, 0, KEY_A), KEY(1, 0, KEY_UP), KEY(2, 1, KEY_B),
	KEY(0, 2, KEY_LEFT), KEY(1, 1, KEY_ENTER), KEY(2, 0, KEY_RIGHT),
	KEY(0, 1, KEY_C), KEY(1, 2, KEY_DOWN), KEY(2, 2, KEY_D),
};

struct pxa27x_keypad_platform_data em_x270_module_keypad_info = {
	/* code map for the matrix keys */
	.matrix_key_rows	= 3,
	.matrix_key_cols	= 3,
	.matrix_key_map		= em_x270_module_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(em_x270_module_matrix_keys),
};

static unsigned int em_x270_exeda_matrix_keys[] = {
	KEY(0, 0, KEY_RIGHTSHIFT), KEY(0, 1, KEY_RIGHTCTRL),
	KEY(0, 2, KEY_RIGHTALT), KEY(0, 3, KEY_SPACE),
	KEY(0, 4, KEY_LEFTALT), KEY(0, 5, KEY_LEFTCTRL),
	KEY(0, 6, KEY_ENTER), KEY(0, 7, KEY_SLASH),

	KEY(1, 0, KEY_DOT), KEY(1, 1, KEY_M),
	KEY(1, 2, KEY_N), KEY(1, 3, KEY_B),
	KEY(1, 4, KEY_V), KEY(1, 5, KEY_C),
	KEY(1, 6, KEY_X), KEY(1, 7, KEY_Z),

	KEY(2, 0, KEY_LEFTSHIFT), KEY(2, 1, KEY_SEMICOLON),
	KEY(2, 2, KEY_L), KEY(2, 3, KEY_K),
	KEY(2, 4, KEY_J), KEY(2, 5, KEY_H),
	KEY(2, 6, KEY_G), KEY(2, 7, KEY_F),

	KEY(3, 0, KEY_D), KEY(3, 1, KEY_S),
	KEY(3, 2, KEY_A), KEY(3, 3, KEY_TAB),
	KEY(3, 4, KEY_BACKSPACE), KEY(3, 5, KEY_P),
	KEY(3, 6, KEY_O), KEY(3, 7, KEY_I),

	KEY(4, 0, KEY_U), KEY(4, 1, KEY_Y),
	KEY(4, 2, KEY_T), KEY(4, 3, KEY_R),
	KEY(4, 4, KEY_E), KEY(4, 5, KEY_W),
	KEY(4, 6, KEY_Q), KEY(4, 7, KEY_MINUS),

	KEY(5, 0, KEY_0), KEY(5, 1, KEY_9),
	KEY(5, 2, KEY_8), KEY(5, 3, KEY_7),
	KEY(5, 4, KEY_6), KEY(5, 5, KEY_5),
	KEY(5, 6, KEY_4), KEY(5, 7, KEY_3),

	KEY(6, 0, KEY_2), KEY(6, 1, KEY_1),
	KEY(6, 2, KEY_ENTER), KEY(6, 3, KEY_END),
	KEY(6, 4, KEY_DOWN), KEY(6, 5, KEY_UP),
	KEY(6, 6, KEY_MENU), KEY(6, 7, KEY_F1),

	KEY(7, 0, KEY_LEFT), KEY(7, 1, KEY_RIGHT),
	KEY(7, 2, KEY_BACK), KEY(7, 3, KEY_HOME),
	KEY(7, 4, 0), KEY(7, 5, 0),
	KEY(7, 6, 0), KEY(7, 7, 0),
};

struct pxa27x_keypad_platform_data em_x270_exeda_keypad_info = {
	/* code map for the matrix keys */
	.matrix_key_rows	= 8,
	.matrix_key_cols	= 8,
	.matrix_key_map		= em_x270_exeda_matrix_keys,
	.matrix_key_map_size	= ARRAY_SIZE(em_x270_exeda_matrix_keys),
};

static void __init em_x270_init_keypad(void)
{
	if (machine_is_em_x270())
		pxa_set_keypad_info(&em_x270_module_keypad_info);
	else
		pxa_set_keypad_info(&em_x270_exeda_keypad_info);
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

/* Quick Capture Interface and sensor setup */
#if defined(CONFIG_VIDEO_PXA27x) || defined(CONFIG_VIDEO_PXA27x_MODULE)
static struct regulator *em_x270_camera_ldo;

static int em_x270_sensor_init(struct device *dev)
{
	int ret;

	ret = gpio_request(cam_reset, "camera reset");
	if (ret)
		return ret;

	gpio_direction_output(cam_reset, 0);

	em_x270_camera_ldo = regulator_get(NULL, "vcc cam");
	if (em_x270_camera_ldo == NULL) {
		gpio_free(cam_reset);
		return -ENODEV;
	}

	ret = regulator_enable(em_x270_camera_ldo);
	if (ret) {
		regulator_put(em_x270_camera_ldo);
		gpio_free(cam_reset);
		return ret;
	}

	gpio_set_value(cam_reset, 1);

	return 0;
}

struct pxacamera_platform_data em_x270_camera_platform_data = {
	.init	= em_x270_sensor_init,
	.flags  = PXA_CAMERA_MASTER | PXA_CAMERA_DATAWIDTH_8 |
		PXA_CAMERA_PCLK_EN | PXA_CAMERA_MCLK_EN,
	.mclk_10khz = 2600,
};

static int em_x270_sensor_power(struct device *dev, int on)
{
	int ret;
	int is_on = regulator_is_enabled(em_x270_camera_ldo);

	if (on == is_on)
		return 0;

	gpio_set_value(cam_reset, !on);

	if (on)
		ret = regulator_enable(em_x270_camera_ldo);
	else
		ret = regulator_disable(em_x270_camera_ldo);

	if (ret)
		return ret;

	gpio_set_value(cam_reset, on);

	return 0;
}

static struct soc_camera_link iclink = {
	.bus_id	= 0,
	.power = em_x270_sensor_power,
};

static struct i2c_board_info em_x270_i2c_cam_info[] = {
	{
		I2C_BOARD_INFO("mt9m111", 0x48),
		.platform_data = &iclink,
	},
};

static void  __init em_x270_init_camera(void)
{
	i2c_register_board_info(0, ARRAY_AND_SIZE(em_x270_i2c_cam_info));
	pxa_set_camera_info(&em_x270_camera_platform_data);
}
#else
static inline void em_x270_init_camera(void) {}
#endif

static struct regulator_bulk_data em_x270_gps_consumer_supply = {
	.supply		= "vcc gps",
};

static struct regulator_userspace_consumer_data em_x270_gps_consumer_data = {
	.name		= "vcc gps",
	.num_supplies	= 1,
	.supplies	= &em_x270_gps_consumer_supply,
};

static struct platform_device em_x270_gps_userspace_consumer = {
	.name		= "reg-userspace-consumer",
	.id		= 0,
	.dev		= {
		.platform_data = &em_x270_gps_consumer_data,
	},
};

static struct regulator_bulk_data em_x270_gprs_consumer_supply = {
	.supply		= "vcc gprs",
};

static struct regulator_userspace_consumer_data em_x270_gprs_consumer_data = {
	.name		= "vcc gprs",
	.num_supplies	= 1,
	.supplies	= &em_x270_gprs_consumer_supply
};

static struct platform_device em_x270_gprs_userspace_consumer = {
	.name		= "reg-userspace-consumer",
	.id		= 1,
	.dev		= {
		.platform_data = &em_x270_gprs_consumer_data,
	}
};

static struct platform_device *em_x270_userspace_consumers[] = {
	&em_x270_gps_userspace_consumer,
	&em_x270_gprs_userspace_consumer,
};

static void __init em_x270_userspace_consumers_init(void)
{
	platform_add_devices(ARRAY_AND_SIZE(em_x270_userspace_consumers));
}

/* DA9030 related initializations */
#define REGULATOR_CONSUMER(_name, _dev, _supply)			       \
	static struct regulator_consumer_supply _name##_consumers[] = {	\
		{							\
			.dev = _dev,					\
			.supply = _supply,				\
		},							\
	}

REGULATOR_CONSUMER(ldo3, &em_x270_gps_userspace_consumer.dev, "vcc gps");
REGULATOR_CONSUMER(ldo5, NULL, "vcc cam");
REGULATOR_CONSUMER(ldo10, &pxa_device_mci.dev, "vcc sdio");
REGULATOR_CONSUMER(ldo12, NULL, "vcc usb");
REGULATOR_CONSUMER(ldo19, &em_x270_gprs_userspace_consumer.dev, "vcc gprs");

#define REGULATOR_INIT(_ldo, _min_uV, _max_uV, _ops_mask)		\
	static struct regulator_init_data _ldo##_data = {		\
		.constraints = {					\
			.min_uV = _min_uV,				\
			.max_uV = _max_uV,				\
			.state_mem = {					\
				.enabled = 0,				\
			},						\
			.valid_ops_mask = _ops_mask,			\
			.apply_uV = 1,					\
		},							\
		.num_consumer_supplies = ARRAY_SIZE(_ldo##_consumers),	\
		.consumer_supplies = _ldo##_consumers,			\
	};

REGULATOR_INIT(ldo3, 3200000, 3200000, REGULATOR_CHANGE_STATUS);
REGULATOR_INIT(ldo5, 3000000, 3000000, REGULATOR_CHANGE_STATUS);
REGULATOR_INIT(ldo10, 2000000, 3200000,
	       REGULATOR_CHANGE_STATUS | REGULATOR_CHANGE_VOLTAGE);
REGULATOR_INIT(ldo12, 3000000, 3000000, REGULATOR_CHANGE_STATUS);
REGULATOR_INIT(ldo19, 3200000, 3200000, REGULATOR_CHANGE_STATUS);

struct led_info em_x270_led_info = {
	.name = "em-x270:orange",
	.default_trigger = "battery-charging-or-full",
};

struct power_supply_info em_x270_psy_info = {
	.name = "battery",
	.technology = POWER_SUPPLY_TECHNOLOGY_LIPO,
	.voltage_max_design = 4200000,
	.voltage_min_design = 3000000,
	.use_for_apm = 1,
};

static void em_x270_battery_low(void)
{
	apm_queue_event(APM_LOW_BATTERY);
}

static void em_x270_battery_critical(void)
{
	apm_queue_event(APM_CRITICAL_SUSPEND);
}

struct da9030_battery_info em_x270_batterty_info = {
	.battery_info = &em_x270_psy_info,

	.charge_milliamp = 1000,
	.charge_millivolt = 4200,

	.vbat_low = 3600,
	.vbat_crit = 3400,
	.vbat_charge_start = 4100,
	.vbat_charge_stop = 4200,
	.vbat_charge_restart = 4000,

	.vcharge_min = 3200,
	.vcharge_max = 5500,

	.tbat_low = 197,
	.tbat_high = 78,
	.tbat_restart = 100,

	.batmon_interval = 0,

	.battery_low = em_x270_battery_low,
	.battery_critical = em_x270_battery_critical,
};

#define DA9030_SUBDEV(_name, _id, _pdata)	\
	{					\
		.name = "da903x-" #_name,	\
		.id = DA9030_ID_##_id,		\
		.platform_data = _pdata,	\
	}

#define DA9030_LDO(num)	DA9030_SUBDEV(regulator, LDO##num, &ldo##num##_data)

struct da903x_subdev_info em_x270_da9030_subdevs[] = {
	DA9030_LDO(3),
	DA9030_LDO(5),
	DA9030_LDO(10),
	DA9030_LDO(12),
	DA9030_LDO(19),

	DA9030_SUBDEV(led, LED_PC, &em_x270_led_info),
	DA9030_SUBDEV(backlight, WLED, &em_x270_led_info),
	DA9030_SUBDEV(battery, BAT, &em_x270_batterty_info),
};

static struct da903x_platform_data em_x270_da9030_info = {
	.num_subdevs = ARRAY_SIZE(em_x270_da9030_subdevs),
	.subdevs = em_x270_da9030_subdevs,
};

static struct i2c_board_info em_x270_i2c_pmic_info = {
	I2C_BOARD_INFO("da9030", 0x49),
	.irq = IRQ_GPIO(0),
	.platform_data = &em_x270_da9030_info,
};

static struct i2c_pxa_platform_data em_x270_pwr_i2c_info = {
	.use_pio = 1,
};

static void __init em_x270_init_da9030(void)
{
	pxa27x_set_i2c_power_info(&em_x270_pwr_i2c_info);
	i2c_register_board_info(1, &em_x270_i2c_pmic_info, 1);
}

static struct pca953x_platform_data exeda_gpio_ext_pdata = {
	.gpio_base = 128,
};

static struct i2c_board_info exeda_i2c_info[] = {
	{
		I2C_BOARD_INFO("pca9555", 0x21),
		.platform_data = &exeda_gpio_ext_pdata,
	},
};

static struct i2c_pxa_platform_data em_x270_i2c_info = {
	.fast_mode = 1,
};

static void __init em_x270_init_i2c(void)
{
	pxa_set_i2c_info(&em_x270_i2c_info);

	if (machine_is_exeda())
		i2c_register_board_info(0, ARRAY_AND_SIZE(exeda_i2c_info));
}

static void __init em_x270_module_init(void)
{
	pr_info("%s\n", __func__);
	pxa2xx_mfp_config(ARRAY_AND_SIZE(em_x270_pin_config));

	mmc_cd = GPIO13_MMC_CD;
	nand_rb = GPIO56_NAND_RB;
	dm9000_flags = DM9000_PLATF_32BITONLY;
	cam_reset = GPIO93_CAM_RESET;
	usb_hub_reset = GPIO16_USB_HUB_RESET;
}

static void __init em_x270_exeda_init(void)
{
	pr_info("%s\n", __func__);
	pxa2xx_mfp_config(ARRAY_AND_SIZE(exeda_pin_config));

	mmc_cd = GPIO114_MMC_CD;
	nand_rb = GPIO20_NAND_RB;
	dm9000_flags = DM9000_PLATF_16BITONLY;
	cam_reset = GPIO130_CAM_RESET;
	usb_hub_reset = GPIO10_USB_HUB_RESET;
}

static void __init em_x270_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(common_pin_config));

#ifdef CONFIG_PM
	pxa27x_set_pwrmode(PWRMODE_DEEPSLEEP);
#endif

	if (machine_is_em_x270())
		em_x270_module_init();
	else if (machine_is_exeda())
		em_x270_exeda_init();
	else
		panic("Unsupported machine: %d\n", machine_arch_type);

	em_x270_init_da9030();
	em_x270_init_dm9000();
	em_x270_init_rtc();
	em_x270_init_nand();
	em_x270_init_nor();
	em_x270_init_lcd();
	em_x270_init_mmc();
	em_x270_init_ohci();
	em_x270_init_keypad();
	em_x270_init_gpio_keys();
	em_x270_init_ac97();
	em_x270_init_spi();
	em_x270_init_i2c();
	em_x270_init_camera();
	em_x270_userspace_consumers_init();
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

MACHINE_START(EXEDA, "Compulab eXeda")
	.boot_params	= 0xa0000100,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.map_io		= pxa_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= em_x270_init,
MACHINE_END
