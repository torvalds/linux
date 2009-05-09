/*
 * File:         arch/blackfin/mach-bf537/boards/stamp.c
 * Based on:     arch/blackfin/mach-bf533/boards/ezkit.c
 * Author:       Aidan Williams <aidan@nicta.com.au>
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2005 National ICT Australia (NICTA)
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/plat-ram.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
#include <linux/usb/isp1362.h>
#endif
#include <linux/ata_platform.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/usb/sl811.h>
#include <linux/spi/mmc_spi.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF537-STAMP";

/*
 *  Driver needs to know address, irq and flag pin.
 */

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
#include <linux/usb/isp1760.h>
static struct resource bfin_isp1760_resources[] = {
	[0] = {
		.start  = 0x203C0000,
		.end    = 0x203C0000 + 0x000fffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_PF7,
		.end    = IRQ_PF7,
		.flags  = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct isp1760_platform_data isp1760_priv = {
	.is_isp1761 = 0,
	.port1_disable = 0,
	.bus_width_16 = 1,
	.port1_otg = 0,
	.analog_oc = 0,
	.dack_polarity_high = 0,
	.dreq_polarity_high = 0,
};

static struct platform_device bfin_isp1760_device = {
	.name           = "isp1760-hcd",
	.id             = 0,
	.dev = {
		.platform_data = &isp1760_priv,
	},
	.num_resources  = ARRAY_SIZE(bfin_isp1760_resources),
	.resource       = bfin_isp1760_resources,
};
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PF2, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PF3, 1, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PF4, 1, "gpio-keys: BTN2"},
	{BTN_3, GPIO_PF5, 1, "gpio-keys: BTN3"},
};

static struct gpio_keys_platform_data bfin_gpio_keys_data = {
	.buttons        = bfin_gpio_keys_table,
	.nbuttons       = ARRAY_SIZE(bfin_gpio_keys_table),
};

static struct platform_device bfin_device_gpiokeys = {
	.name      = "gpio-keys",
	.dev = {
		.platform_data = &bfin_gpio_keys_data,
	},
};
#endif

static struct resource bfin_gpios_resources = {
	.start = 0,
	.end   = MAX_BLACKFIN_GPIOS - 1,
	.flags = IORESOURCE_IRQ,
};

static struct platform_device bfin_gpios_device = {
	.name = "simple-gpio",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_gpios_resources,
};

#if defined(CONFIG_BFIN_CFPCMCIA) || defined(CONFIG_BFIN_CFPCMCIA_MODULE)
static struct resource bfin_pcmcia_cf_resources[] = {
	{
		.start = 0x20310000, /* IO PORT */
		.end = 0x20312000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20311000, /* Attribute Memory */
		.end = 0x20311FFF,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF4,
		.end = IRQ_PF4,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	}, {
		.start = 6, /* Card Detect PF6 */
		.end = 6,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_pcmcia_cf_device = {
	.name = "bfin_cf_pcmcia",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_pcmcia_cf_resources),
	.resource = bfin_pcmcia_cf_resources,
};
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
static struct resource smc91x_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x20300300,
		.end = 0x20300300 + 16,
		.flags = IORESOURCE_MEM,
	}, {

		.start = IRQ_PF7,
		.end = IRQ_PF7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};
static struct platform_device smc91x_device = {
	.name = "smc91x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smc91x_resources),
	.resource = smc91x_resources,
};
#endif

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= 0x203FB800,
		.end	= 0x203FB800 + 8,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PF9,
		.end	= IRQ_PF9,
		.flags	= (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE),
	},
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
};
#endif

#if defined(CONFIG_AX88180) || defined(CONFIG_AX88180_MODULE)
static struct resource ax88180_resources[] = {
	[0] = {
		.start	= 0x20300000,
		.end	= 0x20300000 + 0x8000,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PF7,
		.end	= IRQ_PF7,
		.flags	= (IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL),
	},
};

static struct platform_device ax88180_device = {
	.name		= "ax88180",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ax88180_resources),
	.resource	= ax88180_resources,
};
#endif

#if defined(CONFIG_USB_SL811_HCD) || defined(CONFIG_USB_SL811_HCD_MODULE)
static struct resource sl811_hcd_resources[] = {
	{
		.start = 0x20340000,
		.end = 0x20340000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20340004,
		.end = 0x20340004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = CONFIG_USB_SL811_BFIN_IRQ,
		.end = CONFIG_USB_SL811_BFIN_IRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

#if defined(CONFIG_USB_SL811_BFIN_USE_VBUS)
void sl811_port_power(struct device *dev, int is_on)
{
	gpio_request(CONFIG_USB_SL811_BFIN_GPIO_VBUS, "usb:SL811_VBUS");
	gpio_direction_output(CONFIG_USB_SL811_BFIN_GPIO_VBUS, is_on);
}
#endif

static struct sl811_platform_data sl811_priv = {
	.potpg = 10,
	.power = 250,       /* == 500mA */
#if defined(CONFIG_USB_SL811_BFIN_USE_VBUS)
	.port_power = &sl811_port_power,
#endif
};

static struct platform_device sl811_hcd_device = {
	.name = "sl811-hcd",
	.id = 0,
	.dev = {
		.platform_data = &sl811_priv,
	},
	.num_resources = ARRAY_SIZE(sl811_hcd_resources),
	.resource = sl811_hcd_resources,
};
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
static struct resource isp1362_hcd_resources[] = {
	{
		.start = 0x20360000,
		.end = 0x20360000,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x20360004,
		.end = 0x20360004,
		.flags = IORESOURCE_MEM,
	}, {
		.start = CONFIG_USB_ISP1362_BFIN_GPIO_IRQ,
		.end = CONFIG_USB_ISP1362_BFIN_GPIO_IRQ,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct isp1362_platform_data isp1362_priv = {
	.sel15Kres = 1,
	.clknotstop = 0,
	.oc_enable = 0,
	.int_act_high = 0,
	.int_edge_triggered = 0,
	.remote_wakeup_connected = 0,
	.no_power_switching = 1,
	.power_switching_mode = 0,
};

static struct platform_device isp1362_hcd_device = {
	.name = "isp1362-hcd",
	.id = 0,
	.dev = {
		.platform_data = &isp1362_priv,
	},
	.num_resources = ARRAY_SIZE(isp1362_hcd_resources),
	.resource = isp1362_hcd_resources,
};
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
static struct platform_device bfin_mii_bus = {
	.name = "bfin_mii_bus",
};

static struct platform_device bfin_mac_device = {
	.name = "bfin_mac",
	.dev.platform_data = &bfin_mii_bus,
};
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x20300000,
		.end = 0x20300000 + 0x100,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IRQ_PF7,
		.end = IRQ_PF7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device net2272_bfin_device = {
	.name = "net2272",
	.id = -1,
	.num_resources = ARRAY_SIZE(net2272_bfin_resources),
	.resource = net2272_bfin_resources,
};
#endif

#if defined(CONFIG_MTD_NAND_PLATFORM) || defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", "RedBoot", NULL };

static struct mtd_partition bfin_plat_nand_partitions[] = {
	{
		.name   = "linux kernel(nand)",
		.size   = 0x400000,
		.offset = 0,
	}, {
		.name   = "file system(nand)",
		.size   = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	},
};
#endif

#define BFIN_NAND_PLAT_CLE 2
#define BFIN_NAND_PLAT_ALE 1
static void bfin_plat_nand_cmd_ctrl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, this->IO_ADDR_W + (1 << BFIN_NAND_PLAT_CLE));
	else
		writeb(cmd, this->IO_ADDR_W + (1 << BFIN_NAND_PLAT_ALE));
}

#define BFIN_NAND_PLAT_READY GPIO_PF3
static int bfin_plat_nand_dev_ready(struct mtd_info *mtd)
{
	return gpio_get_value(BFIN_NAND_PLAT_READY);
}

static struct platform_nand_data bfin_plat_nand_data = {
	.chip = {
		.chip_delay = 30,
#ifdef CONFIG_MTD_PARTITIONS
		.part_probe_types = part_probes,
		.partitions = bfin_plat_nand_partitions,
		.nr_partitions = ARRAY_SIZE(bfin_plat_nand_partitions),
#endif
	},
	.ctrl = {
		.cmd_ctrl  = bfin_plat_nand_cmd_ctrl,
		.dev_ready = bfin_plat_nand_dev_ready,
	},
};

#define MAX(x, y) (x > y ? x : y)
static struct resource bfin_plat_nand_resources = {
	.start = 0x20212000,
	.end   = 0x20212000 + (1 << MAX(BFIN_NAND_PLAT_CLE, BFIN_NAND_PLAT_ALE)),
	.flags = IORESOURCE_IO,
};

static struct platform_device bfin_async_nand_device = {
	.name = "gen_nand",
	.id = -1,
	.num_resources = 1,
	.resource = &bfin_plat_nand_resources,
	.dev = {
		.platform_data = &bfin_plat_nand_data,
	},
};

static void bfin_plat_nand_init(void)
{
	gpio_request(BFIN_NAND_PLAT_READY, "bfin_nand_plat");
}
#else
static void bfin_plat_nand_init(void) {}
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition stamp_partitions[] = {
	{
		.name       = "bootloader(nor)",
		.size       = 0x40000,
		.offset     = 0,
	}, {
		.name       = "linux kernel(nor)",
		.size       = 0x180000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "file system(nor)",
		.size       = 0x400000 - 0x40000 - 0x180000 - 0x10000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "MAC Address(nor)",
		.size       = MTDPART_SIZ_FULL,
		.offset     = 0x3F0000,
		.mask_flags = MTD_WRITEABLE,
	}
};

static struct physmap_flash_data stamp_flash_data = {
	.width      = 2,
	.parts      = stamp_partitions,
	.nr_parts   = ARRAY_SIZE(stamp_partitions),
};

static struct resource stamp_flash_resource = {
	.start = 0x20000000,
	.end   = 0x203fffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device stamp_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &stamp_flash_data,
	},
	.num_resources = 1,
	.resource      = &stamp_flash_resource,
};
#endif

#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
static struct mtd_partition bfin_spi_flash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name = "file system(spi)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_flash_data = {
	.name = "m25p80",
	.parts = bfin_spi_flash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_flash_partitions),
	/* .type = "m25p64", */
};

/* SPI flash chip (m25p64) */
static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_SPI_ADC_BF533) \
	|| defined(CONFIG_SPI_ADC_BF533_MODULE)
/* SPI ADC chip */
static struct bfin5xx_spi_chip spi_adc_chip_info = {
	.enable_dma = 1,         /* use dma transfer with this chip*/
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
static struct bfin5xx_spi_chip ad1836_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_AD9960) || defined(CONFIG_AD9960_MODULE)
static struct bfin5xx_spi_chip ad9960_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
#define MMC_SPI_CARD_DETECT_INT IRQ_PF5

static int bfin_mmc_spi_init(struct device *dev,
	irqreturn_t (*detect_int)(int, void *), void *data)
{
	return request_irq(MMC_SPI_CARD_DETECT_INT, detect_int,
		IRQF_TRIGGER_FALLING, "mmc-spi-detect", data);
}

static void bfin_mmc_spi_exit(struct device *dev, void *data)
{
	free_irq(MMC_SPI_CARD_DETECT_INT, data);
}

static struct mmc_spi_platform_data bfin_mmc_spi_pdata = {
	.init = bfin_mmc_spi_init,
	.exit = bfin_mmc_spi_exit,
	.detect_delay = 100, /* msecs */
};

static struct bfin5xx_spi_chip  mmc_spi_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_PBX)
static struct bfin5xx_spi_chip spi_si3xxx_chip_info = {
	.ctl_reg	= 0x4, /* send zero */
	.enable_dma	= 0,
	.bits_per_word	= 8,
	.cs_change_per_word = 1,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
#include <linux/spi/ad7877.h>
static struct bfin5xx_spi_chip spi_ad7877_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};

static const struct ad7877_platform_data bfin_ad7877_ts_info = {
	.model			= 7877,
	.vref_delay_usecs	= 50,	/* internal, no capacitor */
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.pressure_max		= 1000,
	.pressure_min		= 0,
	.stopacq_polarity 	= 1,
	.first_conversion_delay = 3,
	.acquisition_time 	= 1,
	.averaging 		= 1,
	.pen_down_acc_interval 	= 1,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_AD7879) || defined(CONFIG_TOUCHSCREEN_AD7879_MODULE)
#include <linux/spi/ad7879.h>
static const struct ad7879_platform_data bfin_ad7879_ts_info = {
	.model			= 7879,	/* Model = AD7879 */
	.x_plate_ohms		= 620,	/* 620 Ohm from the touch datasheet */
	.pressure_max		= 10000,
	.pressure_min		= 0,
	.first_conversion_delay = 3,	/* wait 512us before do a first conversion */
	.acquisition_time 	= 1,	/* 4us acquisition time per sample */
	.median			= 2,	/* do 8 measurements */
	.averaging 		= 1,	/* take the average of 4 middle samples */
	.pen_down_acc_interval 	= 255,	/* 9.4 ms */
	.gpio_output		= 1,	/* configure AUX/VBAT/GPIO as GPIO output */
	.gpio_default 		= 1,	/* During initialization set GPIO = HIGH */
};
#endif

#if defined(CONFIG_TOUCHSCREEN_AD7879_SPI) || defined(CONFIG_TOUCHSCREEN_AD7879_SPI_MODULE)
static struct bfin5xx_spi_chip spi_ad7879_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 16,
};
#endif

#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
static struct bfin5xx_spi_chip spidev_chip_info = {
	.enable_dma = 0,
	.bits_per_word = 8,
};
#endif

#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
static struct bfin5xx_spi_chip lq035q1_spi_chip_info = {
	.enable_dma	= 0,
	.bits_per_word	= 8,
};
#endif

#if defined(CONFIG_ENC28J60) || defined(CONFIG_ENC28J60_MODULE)
static struct bfin5xx_spi_chip enc28j60_spi_chip_info = {
	.enable_dma	= 1,
	.bits_per_word	= 8,
	.cs_gpio = GPIO_PF10,
};
#endif

#if defined(CONFIG_MTD_DATAFLASH) \
	|| defined(CONFIG_MTD_DATAFLASH_MODULE)

static struct mtd_partition bfin_spi_dataflash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00040000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name = "file system(spi)",
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_dataflash_data = {
	.name = "SPI Dataflash",
	.parts = bfin_spi_dataflash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_dataflash_partitions),
};

/* DataFlash chip */
static struct bfin5xx_spi_chip data_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
	.bits_per_word = 8,
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. On STAMP537 it is SPISSEL1*/
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_MTD_DATAFLASH) \
	|| defined(CONFIG_MTD_DATAFLASH_MODULE)
	{	/* DataFlash chip */
		.modalias = "mtd_dataflash",
		.max_speed_hz = 33250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. On STAMP537 it is SPISSEL1*/
		.platform_data = &bfin_spi_dataflash_data,
		.controller_data = &data_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_SPI_ADC_BF533) \
	|| defined(CONFIG_SPI_ADC_BF533_MODULE)
	{
		.modalias = "bfin_spi_adc", /* Name of spi_driver for this device */
		.max_speed_hz = 6250000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* Framework chip select. */
		.platform_data = NULL, /* No spi_driver specific config */
		.controller_data = &spi_adc_chip_info,
	},
#endif

#if defined(CONFIG_SND_BLACKFIN_AD1836) \
	|| defined(CONFIG_SND_BLACKFIN_AD1836_MODULE)
	{
		.modalias = "ad1836-spi",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = CONFIG_SND_BLACKFIN_SPI_PFBIT,
		.controller_data = &ad1836_spi_chip_info,
	},
#endif
#if defined(CONFIG_AD9960) || defined(CONFIG_AD9960_MODULE)
	{
		.modalias = "ad9960-spi",
		.max_speed_hz = 10000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = &ad9960_spi_chip_info,
	},
#endif
#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
		.platform_data = &bfin_mmc_spi_pdata,
		.controller_data = &mmc_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_PBX)
	{
		.modalias = "fxs-spi",
		.max_speed_hz = 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 8 - CONFIG_J11_JUMPER,
		.controller_data = &spi_si3xxx_chip_info,
		.mode = SPI_MODE_3,
	},
	{
		.modalias = "fxo-spi",
		.max_speed_hz = 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 8 - CONFIG_J19_JUMPER,
		.controller_data = &spi_si3xxx_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
	{
		.modalias		= "ad7877",
		.platform_data		= &bfin_ad7877_ts_info,
		.irq			= IRQ_PF6,
		.max_speed_hz	= 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select  = 1,
		.controller_data = &spi_ad7877_chip_info,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7879_SPI) || defined(CONFIG_TOUCHSCREEN_AD7879_SPI_MODULE)
	{
		.modalias = "ad7879",
		.platform_data = &bfin_ad7879_ts_info,
		.irq = IRQ_PF7,
		.max_speed_hz = 5000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = &spi_ad7879_chip_info,
		.mode = SPI_CPHA | SPI_CPOL,
	},
#endif
#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
	{
		.modalias = "spidev",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.controller_data = &spidev_chip_info,
	},
#endif
#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
	{
		.modalias = "bfin-lq035q1-spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 2,
		.controller_data = &lq035q1_spi_chip_info,
		.mode = SPI_CPHA | SPI_CPOL,
	},
#endif
#if defined(CONFIG_ENC28J60) || defined(CONFIG_ENC28J60_MODULE)
	{
		.modalias = "enc28j60",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.irq = IRQ_PF6,
		.bus_num = 0,
		.chip_select = 0,	/* GPIO controlled SSEL */
		.controller_data = &enc28j60_spi_chip_info,
		.mode = SPI_MODE_0,
	},
#endif
};

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	[0] = {
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = CH_SPI,
		.end   = CH_SPI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_spi0_device = {
	.name = "bfin-spi",
	.id = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bfin_spi0_info, /* Passed to driver */
	},
};
#endif  /* spi master and devices */

#if defined(CONFIG_SPI_BFIN_SPORT) || defined(CONFIG_SPI_BFIN_SPORT_MODULE)

/* SPORT SPI controller data */
static struct bfin5xx_spi_master bfin_sport_spi0_info = {
	.num_chipselect = 1, /* master only supports one device */
	.enable_dma = 0,  /* master don't support DMA */
	.pin_req = {P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_DRPRI,
		P_SPORT0_RSCLK, P_SPORT0_TFS, P_SPORT0_RFS, 0},
};

static struct resource bfin_sport_spi0_resource[] = {
	[0] = {
		.start = SPORT0_TCR1,
		.end   = SPORT0_TCR1 + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = IRQ_SPORT0_ERROR,
		.end   = IRQ_SPORT0_ERROR,
		.flags = IORESOURCE_IRQ,
		},
};

static struct platform_device bfin_sport_spi0_device = {
	.name = "bfin-sport-spi",
	.id = 1, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_sport_spi0_resource),
	.resource = bfin_sport_spi0_resource,
	.dev = {
		.platform_data = &bfin_sport_spi0_info, /* Passed to driver */
	},
};

static struct bfin5xx_spi_master bfin_sport_spi1_info = {
	.num_chipselect = 1, /* master only supports one device */
	.enable_dma = 0,  /* master don't support DMA */
	.pin_req = {P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_DRPRI,
		P_SPORT1_RSCLK, P_SPORT1_TFS, P_SPORT1_RFS, 0},
};

static struct resource bfin_sport_spi1_resource[] = {
	[0] = {
		.start = SPORT1_TCR1,
		.end   = SPORT1_TCR1 + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = IRQ_SPORT1_ERROR,
		.end   = IRQ_SPORT1_ERROR,
		.flags = IORESOURCE_IRQ,
		},
};

static struct platform_device bfin_sport_spi1_device = {
	.name = "bfin-sport-spi",
	.id = 2, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_sport_spi1_resource),
	.resource = bfin_sport_spi1_resource,
	.dev = {
		.platform_data = &bfin_sport_spi1_info, /* Passed to driver */
	},
};

#endif  /* sport spi master and devices */

#if defined(CONFIG_FB_BF537_LQ035) || defined(CONFIG_FB_BF537_LQ035_MODULE)
static struct platform_device bfin_fb_device = {
	.name = "bf537-lq035",
};
#endif

#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
static struct platform_device bfin_fb_adv7393_device = {
	.name = "bfin-adv7393",
};
#endif

#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
#include <asm/bfin-lq035q1.h>

static struct bfin_lq035q1fb_disp_info bfin_lq035q1_data = {
	.mode = 	LQ035_NORM | LQ035_RGB | LQ035_RL | LQ035_TB,
	.use_bl = 	0,	/* let something else control the LCD Blacklight */
	.gpio_bl =	GPIO_PF7,
};

static struct resource bfin_lq035q1_resources[] = {
	{
		.start = IRQ_PPI_ERROR,
		.end = IRQ_PPI_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_lq035q1_device = {
	.name		= "bfin-lq035q1",
	.id		= -1,
	.num_resources 	= ARRAY_SIZE(bfin_lq035q1_resources),
	.resource 	= bfin_lq035q1_resources,
	.dev		= {
		.platform_data = &bfin_lq035q1_data,
	},
};
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
static struct resource bfin_uart_resources[] = {
#ifdef CONFIG_SERIAL_BFIN_UART0
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	{
		.start = 0xFFC02000,
		.end = 0xFFC020FF,
		.flags = IORESOURCE_MEM,
	},
#endif
};

static struct platform_device bfin_uart_device = {
	.name = "bfin-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_uart_resources),
	.resource = bfin_uart_resources,
};
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
static struct resource bfin_sir0_resources[] = {
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir0_device = {
	.name = "bfin_sir",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sir0_resources),
	.resource = bfin_sir0_resources,
};
#endif
#ifdef CONFIG_BFIN_SIR1
static struct resource bfin_sir1_resources[] = {
	{
		.start = 0xFFC02000,
		.end = 0xFFC020FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART1_RX,
		.end = IRQ_UART1_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_RX,
		.end = CH_UART1_RX+1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_sir1_device = {
	.name = "bfin_sir",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_sir1_resources),
	.resource = bfin_sir1_resources,
};
#endif
#endif

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
static struct resource bfin_twi0_resource[] = {
	[0] = {
		.start = TWI0_REGBASE,
		.end   = TWI0_REGBASE,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TWI,
		.end   = IRQ_TWI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_bfin_twi_device = {
	.name = "i2c-bfin-twi",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_twi0_resource),
	.resource = bfin_twi0_resource,
};
#endif

#if defined(CONFIG_KEYBOARD_ADP5588) || defined(CONFIG_KEYBOARD_ADP5588_MODULE)
#include <linux/input.h>
#include <linux/i2c/adp5588_keys.h>
static const unsigned short adp5588_keymap[ADP5588_KEYMAPSIZE] = {
	[0]	 = KEY_GRAVE,
	[1]	 = KEY_1,
	[2]	 = KEY_2,
	[3]	 = KEY_3,
	[4]	 = KEY_4,
	[5]	 = KEY_5,
	[6]	 = KEY_6,
	[7]	 = KEY_7,
	[8]	 = KEY_8,
	[9]	 = KEY_9,
	[10]	 = KEY_0,
	[11]	 = KEY_MINUS,
	[12]	 = KEY_EQUAL,
	[13]	 = KEY_BACKSLASH,
	[15]	 = KEY_KP0,
	[16]	 = KEY_Q,
	[17]	 = KEY_W,
	[18]	 = KEY_E,
	[19]	 = KEY_R,
	[20]	 = KEY_T,
	[21]	 = KEY_Y,
	[22]	 = KEY_U,
	[23]	 = KEY_I,
	[24]	 = KEY_O,
	[25]	 = KEY_P,
	[26]	 = KEY_LEFTBRACE,
	[27]	 = KEY_RIGHTBRACE,
	[29]	 = KEY_KP1,
	[30]	 = KEY_KP2,
	[31]	 = KEY_KP3,
	[32]	 = KEY_A,
	[33]	 = KEY_S,
	[34]	 = KEY_D,
	[35]	 = KEY_F,
	[36]	 = KEY_G,
	[37]	 = KEY_H,
	[38]	 = KEY_J,
	[39]	 = KEY_K,
	[40]	 = KEY_L,
	[41]	 = KEY_SEMICOLON,
	[42]	 = KEY_APOSTROPHE,
	[43]	 = KEY_BACKSLASH,
	[45]	 = KEY_KP4,
	[46]	 = KEY_KP5,
	[47]	 = KEY_KP6,
	[48]	 = KEY_102ND,
	[49]	 = KEY_Z,
	[50]	 = KEY_X,
	[51]	 = KEY_C,
	[52]	 = KEY_V,
	[53]	 = KEY_B,
	[54]	 = KEY_N,
	[55]	 = KEY_M,
	[56]	 = KEY_COMMA,
	[57]	 = KEY_DOT,
	[58]	 = KEY_SLASH,
	[60]	 = KEY_KPDOT,
	[61]	 = KEY_KP7,
	[62]	 = KEY_KP8,
	[63]	 = KEY_KP9,
	[64]	 = KEY_SPACE,
	[65]	 = KEY_BACKSPACE,
	[66]	 = KEY_TAB,
	[67]	 = KEY_KPENTER,
	[68]	 = KEY_ENTER,
	[69]	 = KEY_ESC,
	[70]	 = KEY_DELETE,
	[74]	 = KEY_KPMINUS,
	[76]	 = KEY_UP,
	[77]	 = KEY_DOWN,
	[78]	 = KEY_RIGHT,
	[79]	 = KEY_LEFT,
};

static struct adp5588_kpad_platform_data adp5588_kpad_data = {
	.rows		= 8,
	.cols		= 10,
	.keymap		= adp5588_keymap,
	.keymapsize	= ARRAY_SIZE(adp5588_keymap),
	.repeat		= 0,
};
#endif

#if defined(CONFIG_PMIC_ADP5520) || defined(CONFIG_PMIC_ADP5520_MODULE)
#include <linux/mfd/adp5520.h>

	/*
	 *  ADP5520/5501 Backlight Data
	 */

static struct adp5520_backlight_platfrom_data adp5520_backlight_data = {
	.fade_in 		= FADE_T_1200ms,
	.fade_out 		= FADE_T_1200ms,
	.fade_led_law 		= BL_LAW_LINEAR,
	.en_ambl_sens 		= 1,
	.abml_filt 		= BL_AMBL_FILT_640ms,
	.l1_daylight_max 	= BL_CUR_mA(15),
	.l1_daylight_dim 	= BL_CUR_mA(0),
	.l2_office_max 		= BL_CUR_mA(7),
	.l2_office_dim 		= BL_CUR_mA(0),
	.l3_dark_max 		= BL_CUR_mA(3),
	.l3_dark_dim 		= BL_CUR_mA(0),
	.l2_trip 		= L2_COMP_CURR_uA(700),
	.l2_hyst 		= L2_COMP_CURR_uA(50),
	.l3_trip 		= L3_COMP_CURR_uA(80),
	.l3_hyst 		= L3_COMP_CURR_uA(20),
};

	/*
	 *  ADP5520/5501 LEDs Data
	 */

#include <linux/leds.h>

static struct led_info adp5520_leds[] = {
	{
		.name = "adp5520-led1",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED1_ADP5501_LED0 | LED_OFFT_600ms,
	},
#ifdef ADP5520_EN_ALL_LEDS
	{
		.name = "adp5520-led2",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED2_ADP5501_LED1,
	},
	{
		.name = "adp5520-led3",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED3_ADP5501_LED2,
	},
#endif
};

static struct adp5520_leds_platfrom_data adp5520_leds_data = {
	.num_leds = ARRAY_SIZE(adp5520_leds),
	.leds = adp5520_leds,
	.fade_in = FADE_T_600ms,
	.fade_out = FADE_T_600ms,
	.led_on_time = LED_ONT_600ms,
};

	/*
	 *  ADP5520 GPIO Data
	 */

static struct adp5520_gpio_platfrom_data adp5520_gpio_data = {
	.gpio_start = 50,
	.gpio_en_mask = GPIO_C1 | GPIO_C2 | GPIO_R2,
	.gpio_pullup_mask = GPIO_C1 | GPIO_C2 | GPIO_R2,
};

	/*
	 *  ADP5520 Keypad Data
	 */

#include <linux/input.h>
static const unsigned short adp5520_keymap[ADP5520_KEYMAPSIZE] = {
	[KEY(0, 0)]	= KEY_GRAVE,
	[KEY(0, 1)]	= KEY_1,
	[KEY(0, 2)]	= KEY_2,
	[KEY(0, 3)]	= KEY_3,
	[KEY(1, 0)]	= KEY_4,
	[KEY(1, 1)]	= KEY_5,
	[KEY(1, 2)]	= KEY_6,
	[KEY(1, 3)]	= KEY_7,
	[KEY(2, 0)]	= KEY_8,
	[KEY(2, 1)]	= KEY_9,
	[KEY(2, 2)]	= KEY_0,
	[KEY(2, 3)]	= KEY_MINUS,
	[KEY(3, 0)]	= KEY_EQUAL,
	[KEY(3, 1)]	= KEY_BACKSLASH,
	[KEY(3, 2)]	= KEY_BACKSPACE,
	[KEY(3, 3)]	= KEY_ENTER,
};

static struct adp5520_keys_platfrom_data adp5520_keys_data = {
	.rows_en_mask	= ROW_R3 | ROW_R2 | ROW_R1 | ROW_R0,
	.cols_en_mask	= COL_C3 | COL_C2 | COL_C1 | COL_C0,
	.keymap		= adp5520_keymap,
	.keymapsize	= ARRAY_SIZE(adp5520_keymap),
	.repeat		= 0,
};

	/*
	 *  ADP5520/5501 Multifuction Device Init Data
	 */

static struct adp5520_subdev_info adp5520_subdevs[] = {
	{
		.name = "adp5520-backlight",
		.id = ID_ADP5520,
		.platform_data = &adp5520_backlight_data,
	},
	{
		.name = "adp5520-led",
		.id = ID_ADP5520,
		.platform_data = &adp5520_leds_data,
	},
	{
		.name = "adp5520-gpio",
		.id = ID_ADP5520,
		.platform_data = &adp5520_gpio_data,
	},
	{
		.name = "adp5520-keys",
		.id = ID_ADP5520,
		.platform_data = &adp5520_keys_data,
	},
};

static struct adp5520_platform_data adp5520_pdev_data = {
	.num_subdevs = ARRAY_SIZE(adp5520_subdevs),
	.subdevs = adp5520_subdevs,
};

#endif

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if defined(CONFIG_JOYSTICK_AD7142) || defined(CONFIG_JOYSTICK_AD7142_MODULE)
	{
		I2C_BOARD_INFO("ad7142_joystick", 0x2C),
		.irq = IRQ_PF5,
	},
#endif
#if defined(CONFIG_TWI_LCD) || defined(CONFIG_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if defined(CONFIG_TWI_KEYPAD) || defined(CONFIG_TWI_KEYPAD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = IRQ_PG6,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7879_I2C) || defined(CONFIG_TOUCHSCREEN_AD7879_I2C_MODULE)
	{
		I2C_BOARD_INFO("ad7879", 0x2F),
		.irq = IRQ_PG5,
		.platform_data = (void *)&bfin_ad7879_ts_info,
	},
#endif
#if defined(CONFIG_KEYBOARD_ADP5588) || defined(CONFIG_KEYBOARD_ADP5588_MODULE)
	{
		I2C_BOARD_INFO("adp5588-keys", 0x34),
		.irq = IRQ_PG0,
		.platform_data = (void *)&adp5588_kpad_data,
	},
#endif
#if defined(CONFIG_PMIC_ADP5520) || defined(CONFIG_PMIC_ADP5520_MODULE)
	{
		I2C_BOARD_INFO("pmic-adp5520", 0x32),
		.irq = IRQ_PF7,
		.platform_data = (void *)&adp5520_pdev_data,
	},
#endif
};

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
static struct platform_device bfin_sport0_uart_device = {
	.name = "bfin-sport-uart",
	.id = 0,
};

static struct platform_device bfin_sport1_uart_device = {
	.name = "bfin-sport-uart",
	.id = 1,
};
#endif

#if defined(CONFIG_PATA_PLATFORM) || defined(CONFIG_PATA_PLATFORM_MODULE)
#define CF_IDE_NAND_CARD_USE_HDD_INTERFACE
/* #define CF_IDE_NAND_CARD_USE_CF_IN_COMMON_MEMORY_MODE */

#ifdef CF_IDE_NAND_CARD_USE_HDD_INTERFACE
#define PATA_INT	IRQ_PF5
static struct pata_platform_info bfin_pata_platform_data = {
	.ioport_shift = 1,
	.irq_flags = IRQF_TRIGGER_HIGH | IRQF_DISABLED,
};

static struct resource bfin_pata_resources[] = {
	{
		.start = 0x20314020,
		.end = 0x2031403F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0x2031401C,
		.end = 0x2031401F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = PATA_INT,
		.end = PATA_INT,
		.flags = IORESOURCE_IRQ,
	},
};
#elif defined(CF_IDE_NAND_CARD_USE_CF_IN_COMMON_MEMORY_MODE)
static struct pata_platform_info bfin_pata_platform_data = {
	.ioport_shift = 0,
};

static struct resource bfin_pata_resources[] = {
	{
		.start = 0x20211820,
		.end = 0x2021183F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = 0x2021181C,
		.end = 0x2021181F,
		.flags = IORESOURCE_MEM,
	},
};
#endif

static struct platform_device bfin_pata_device = {
	.name = "pata_platform",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_pata_resources),
	.resource = bfin_pata_resources,
	.dev = {
		.platform_data = &bfin_pata_platform_data,
	}
};
#endif

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_085, 250000000),
	VRPAIR(VLEV_090, 376000000),
	VRPAIR(VLEV_095, 426000000),
	VRPAIR(VLEV_100, 426000000),
	VRPAIR(VLEV_105, 476000000),
	VRPAIR(VLEV_110, 476000000),
	VRPAIR(VLEV_115, 476000000),
	VRPAIR(VLEV_120, 500000000),
	VRPAIR(VLEV_125, 533000000),
	VRPAIR(VLEV_130, 600000000),
};

static struct bfin_dpmc_platform_data bfin_dmpc_vreg_data = {
	.tuple_tab = cclk_vlev_datasheet,
	.tabsize = ARRAY_SIZE(cclk_vlev_datasheet),
	.vr_settling_time = 25 /* us */,
};

static struct platform_device bfin_dpmc = {
	.name = "bfin dpmc",
	.dev = {
		.platform_data = &bfin_dmpc_vreg_data,
	},
};

static struct platform_device *stamp_devices[] __initdata = {

	&bfin_dpmc,

#if defined(CONFIG_BFIN_CFPCMCIA) || defined(CONFIG_BFIN_CFPCMCIA_MODULE)
	&bfin_pcmcia_cf_device,
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_USB_SL811_HCD) || defined(CONFIG_USB_SL811_HCD_MODULE)
	&sl811_hcd_device,
#endif

#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
	&isp1362_hcd_device,
#endif

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
	&bfin_isp1760_device,
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
	&dm9000_device,
#endif

#if defined(CONFIG_AX88180) || defined(CONFIG_AX88180_MODULE)
	&ax88180_device,
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
	&bfin_mii_bus,
	&bfin_mac_device,
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	&net2272_bfin_device,
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_SPI_BFIN_SPORT) || defined(CONFIG_SPI_BFIN_SPORT_MODULE)
	&bfin_sport_spi0_device,
	&bfin_sport_spi1_device,
#endif

#if defined(CONFIG_FB_BF537_LQ035) || defined(CONFIG_FB_BF537_LQ035_MODULE)
	&bfin_fb_device,
#endif

#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
	&bfin_lq035q1_device,
#endif

#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
	&bfin_fb_adv7393_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
	&bfin_uart_device,
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#ifdef CONFIG_BFIN_SIR1
	&bfin_sir1_device,
#endif
#endif

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
	&i2c_bfin_twi_device,
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
	&bfin_sport0_uart_device,
	&bfin_sport1_uart_device,
#endif

#if defined(CONFIG_PATA_PLATFORM) || defined(CONFIG_PATA_PLATFORM_MODULE)
	&bfin_pata_device,
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

	&bfin_gpios_device,

#if defined(CONFIG_MTD_NAND_PLATFORM) || defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
	&bfin_async_nand_device,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&stamp_flash_device,
#endif
};

static int __init stamp_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
	bfin_plat_nand_init();
	platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));

#if defined(CONFIG_PATA_PLATFORM) || defined(CONFIG_PATA_PLATFORM_MODULE)
	irq_desc[PATA_INT].status |= IRQ_NOAUTOEN;
#endif

	return 0;
}

arch_initcall(stamp_init);

void native_machine_restart(char *cmd)
{
	/* workaround reboot hang when booting from SPI */
	if ((bfin_read_SYSCR() & 0x7) == 0x3)
		bfin_reset_boot_spi_cs(P_DEFAULT_BOOT_SPI_CS);
}

/*
 * Currently the MAC address is saved in Flash by U-Boot
 */
#define FLASH_MAC	0x203f0000
void bfin_get_ether_addr(char *addr)
{
	*(u32 *)(&(addr[0])) = bfin_read32(FLASH_MAC);
	*(u16 *)(&(addr[4])) = bfin_read16(FLASH_MAC + 4);
}
EXPORT_SYMBOL(bfin_get_ether_addr);
