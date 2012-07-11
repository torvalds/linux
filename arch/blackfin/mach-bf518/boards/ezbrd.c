/*
 * Copyright 2004-2009 Analog Devices Inc.
 *                2005 National ICT Australia (NICTA)
 *                      Aidan Williams <aidan@nicta.com.au>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>

#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>
#include <asm/bfin_sdh.h>
#include <linux/spi/ad7877.h>
#include <net/dsa.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF518F-EZBRD";

/*
 *  Driver needs to know address, irq and flag pin.
 */

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition ezbrd_partitions[] = {
	{
		.name       = "bootloader(nor)",
		.size       = 0x40000,
		.offset     = 0,
	}, {
		.name       = "linux kernel(nor)",
		.size       = 0x1C0000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "file system(nor)",
		.size       = MTDPART_SIZ_FULL,
		.offset     = MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data ezbrd_flash_data = {
	.width      = 2,
	.parts      = ezbrd_partitions,
	.nr_parts   = ARRAY_SIZE(ezbrd_partitions),
};

static struct resource ezbrd_flash_resource = {
	.start = 0x20000000,
#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
	.end   = 0x202fffff,
#else
	.end   = 0x203fffff,
#endif
	.flags = IORESOURCE_MEM,
};

static struct platform_device ezbrd_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &ezbrd_flash_data,
	},
	.num_resources = 1,
	.resource      = &ezbrd_flash_resource,
};
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
#include <linux/bfin_mac.h>
static const unsigned short bfin_mac_peripherals[] = {
	P_MII0_ETxD0,
	P_MII0_ETxD1,
	P_MII0_ETxEN,
	P_MII0_ERxD0,
	P_MII0_ERxD1,
	P_MII0_TxCLK,
	P_MII0_PHYINT,
	P_MII0_CRS,
	P_MII0_MDC,
	P_MII0_MDIO,
	0
};

static struct bfin_phydev_platform_data bfin_phydev_data[] = {
	{
#if defined(CONFIG_NET_DSA_KSZ8893M) || defined(CONFIG_NET_DSA_KSZ8893M_MODULE)
		.addr = 3,
#else
		.addr = 1,
#endif
		.irq = IRQ_MAC_PHYINT,
	},
};

static struct bfin_mii_bus_platform_data bfin_mii_bus_data = {
	.phydev_number = 1,
	.phydev_data = bfin_phydev_data,
	.phy_mode = PHY_INTERFACE_MODE_MII,
	.mac_peripherals = bfin_mac_peripherals,
#if defined(CONFIG_NET_DSA_KSZ8893M) || defined(CONFIG_NET_DSA_KSZ8893M_MODULE)
	.phy_mask = 0xfff7, /* Only probe the port phy connect to the on chip MAC */
#endif
	.vlan1_mask = 1,
	.vlan2_mask = 2,
};

static struct platform_device bfin_mii_bus = {
	.name = "bfin_mii_bus",
	.dev = {
		.platform_data = &bfin_mii_bus_data,
	}
};

static struct platform_device bfin_mac_device = {
	.name = "bfin_mac",
	.dev = {
		.platform_data = &bfin_mii_bus,
	}
};

#if defined(CONFIG_NET_DSA_KSZ8893M) || defined(CONFIG_NET_DSA_KSZ8893M_MODULE)
static struct dsa_chip_data ksz8893m_switch_chip_data = {
	.mii_bus = &bfin_mii_bus.dev,
	.port_names = {
		NULL,
		"eth%d",
		"eth%d",
		"cpu",
	},
};
static struct dsa_platform_data ksz8893m_switch_data = {
	.nr_chips = 1,
	.netdev = &bfin_mac_device.dev,
	.chip = &ksz8893m_switch_chip_data,
};

static struct platform_device ksz8893m_switch_device = {
	.name		= "dsa",
	.id		= 0,
	.num_resources	= 0,
	.dev.platform_data = &ksz8893m_switch_data,
};
#endif
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
		.size = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data bfin_spi_flash_data = {
	.name = "m25p80",
	.parts = bfin_spi_flash_partitions,
	.nr_parts = ARRAY_SIZE(bfin_spi_flash_partitions),
	.type = "m25p16",
};

/* SPI flash chip (m25p64) */
static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
};
#endif

#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
static struct bfin5xx_spi_chip mmc_spi_chip_info = {
	.enable_dma = 0,
};
#endif

#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
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

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 2, /* On BF518F-EZBRD it's SPI0_SSEL2 */
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
#if defined(CONFIG_NET_DSA_KSZ8893M) \
	|| defined(CONFIG_NET_DSA_KSZ8893M_MODULE)
	{
		.modalias = "ksz8893m",
		.max_speed_hz = 5000000,
		.bus_num = 0,
		.chip_select = 1,
		.platform_data = NULL,
		.mode = SPI_MODE_3,
	},
#endif
#endif

#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 5,
		.controller_data = &mmc_spi_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
	{
		.modalias		= "ad7877",
		.platform_data		= &bfin_ad7877_ts_info,
		.irq			= IRQ_PF8,
		.max_speed_hz	= 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select  = 2,
	},
#endif
#if defined(CONFIG_SND_SOC_WM8731) || defined(CONFIG_SND_SOC_WM8731_MODULE) \
	 && defined(CONFIG_SND_SOC_WM8731_SPI)
	{
		.modalias	= "wm8731",
		.max_speed_hz	= 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num	= 0,
		.chip_select    = 5,
		.mode = SPI_MODE_0,
	},
#endif
#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
	{
		.modalias = "spidev",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
	},
#endif
#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
	{
		.modalias = "bfin-lq035q1-spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 1,
		.mode = SPI_CPHA | SPI_CPOL,
	},
#endif
};

/* SPI controller data */
#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
/* SPI (0) */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 6,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct resource bfin_spi0_resource[] = {
	[0] = {
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = CH_SPI0,
		.end   = CH_SPI0,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI0,
		.end   = IRQ_SPI0,
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

/* SPI (1) */
static struct bfin5xx_spi_master bfin_spi1_info = {
	.num_chipselect = 6,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI1_SCK, P_SPI1_MISO, P_SPI1_MOSI, 0},
};

static struct resource bfin_spi1_resource[] = {
	[0] = {
		.start = SPI1_REGBASE,
		.end   = SPI1_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
		},
	[1] = {
		.start = CH_SPI1,
		.end   = CH_SPI1,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI1,
		.end   = IRQ_SPI1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_spi1_device = {
	.name = "bfin-spi",
	.id = 1, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi1_resource),
	.resource = bfin_spi1_resource,
	.dev = {
		.platform_data = &bfin_spi1_info, /* Passed to driver */
	},
};
#endif  /* spi master and devices */

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = UART0_THR,
		.end = UART0_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART0_TX,
		.end = IRQ_UART0_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_RX,
		.end = IRQ_UART0_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART0_ERROR,
		.end = IRQ_UART0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_TX,
		.end = CH_UART0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART0_RX,
		.end = CH_UART0_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart0_peripherals[] = {
	P_UART0_TX, P_UART0_RX, 0
};

static struct platform_device bfin_uart0_device = {
	.name = "bfin-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_uart0_resources),
	.resource = bfin_uart0_resources,
	.dev = {
		.platform_data = &bfin_uart0_peripherals, /* Passed to driver */
	},
};
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
static struct resource bfin_uart1_resources[] = {
	{
		.start = UART1_THR,
		.end = UART1_GCTL+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART1_TX,
		.end = IRQ_UART1_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_RX,
		.end = IRQ_UART1_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART1_ERROR,
		.end = IRQ_UART1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_TX,
		.end = CH_UART1_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART1_RX,
		.end = CH_UART1_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart1_peripherals[] = {
	P_UART1_TX, P_UART1_RX, 0
};

static struct platform_device bfin_uart1_device = {
	.name = "bfin-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_uart1_resources),
	.resource = bfin_uart1_resources,
	.dev = {
		.platform_data = &bfin_uart1_peripherals, /* Passed to driver */
	},
};
#endif
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

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	/* TODO: add platform data here */
};
#endif

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
static const u16 bfin_twi0_pins[] = {P_TWI0_SCL, P_TWI0_SDA, 0};

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
	.dev = {
		.platform_data = &bfin_twi0_pins,
	},
};
#endif

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_BFIN_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if defined(CONFIG_INPUT_PCF8574) || defined(CONFIG_INPUT_PCF8574_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = IRQ_PF8,
	},
#endif
#if defined(CONFIG_SND_SOC_SSM2602) || defined(CONFIG_SND_SOC_SSM2602_MODULE)
	{
		I2C_BOARD_INFO("ssm2602", 0x1b),
	},
#endif
};

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
static struct resource bfin_sport0_uart_resources[] = {
	{
		.start = SPORT0_TCR1,
		.end = SPORT0_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT0_RX,
		.end = IRQ_SPORT0_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT0_ERROR,
		.end = IRQ_SPORT0_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport0_peripherals[] = {
	P_SPORT0_TFS, P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_RFS,
	P_SPORT0_DRPRI, P_SPORT0_RSCLK, 0
};

static struct platform_device bfin_sport0_uart_device = {
	.name = "bfin-sport-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_sport0_uart_resources),
	.resource = bfin_sport0_uart_resources,
	.dev = {
		.platform_data = &bfin_sport0_peripherals, /* Passed to driver */
	},
};
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
static struct resource bfin_sport1_uart_resources[] = {
	{
		.start = SPORT1_TCR1,
		.end = SPORT1_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT1_RX,
		.end = IRQ_SPORT1_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT1_ERROR,
		.end = IRQ_SPORT1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport1_peripherals[] = {
	P_SPORT1_TFS, P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_RFS,
	P_SPORT1_DRPRI, P_SPORT1_RSCLK, 0
};

static struct platform_device bfin_sport1_uart_device = {
	.name = "bfin-sport-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_sport1_uart_resources),
	.resource = bfin_sport1_uart_resources,
	.dev = {
		.platform_data = &bfin_sport1_peripherals, /* Passed to driver */
	},
};
#endif
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PG0, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PG13, 1, "gpio-keys: BTN1"},
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

#if defined(CONFIG_SDH_BFIN) || defined(CONFIG_SDH_BFIN_MODULE)

static struct bfin_sd_host bfin_sdh_data = {
	.dma_chan = CH_RSI,
	.irq_int0 = IRQ_RSI_INT0,
	.pin_req = {P_RSI_DATA0, P_RSI_DATA1, P_RSI_DATA2, P_RSI_DATA3, P_RSI_CMD, P_RSI_CLK, 0},
};

static struct platform_device bf51x_sdh_device = {
	.name = "bfin-sdh",
	.id = 0,
	.dev = {
		.platform_data = &bfin_sdh_data,
	},
};
#endif

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_100, 400000000),
	VRPAIR(VLEV_105, 426000000),
	VRPAIR(VLEV_110, 500000000),
	VRPAIR(VLEV_115, 533000000),
	VRPAIR(VLEV_120, 600000000),
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

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
	&bfin_mii_bus,
	&bfin_mac_device,
#if defined(CONFIG_NET_DSA_KSZ8893M) || defined(CONFIG_NET_DSA_KSZ8893M_MODULE)
	&ksz8893m_switch_device,
#endif
#endif

#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
	&bfin_spi0_device,
	&bfin_spi1_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
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

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
	&bfin_i2s,
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_SDH_BFIN) || defined(CONFIG_SDH_BFIN_MODULE)
	&bf51x_sdh_device,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ezbrd_flash_device,
#endif
};

static int __init ezbrd_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
	platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	/* setup BF518-EZBRD GPIO pin PG11 to AMS2, PG15 to AMS3. */
	peripheral_request(P_AMS2, "ParaFlash");
#if !defined(CONFIG_SPI_BFIN5XX) && !defined(CONFIG_SPI_BFIN5XX_MODULE)
	peripheral_request(P_AMS3, "ParaFlash");
#endif
	return 0;
}

arch_initcall(ezbrd_init);

static struct platform_device *ezbrd_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT_CONSOLE)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(ezbrd_early_devices,
		ARRAY_SIZE(ezbrd_early_devices));
}

void native_machine_restart(char *cmd)
{
	/* workaround reboot hang when booting from SPI */
	if ((bfin_read_SYSCR() & 0x7) == 0x3)
		bfin_reset_boot_spi_cs(P_DEFAULT_BOOT_SPI_CS);
}

int bfin_get_ether_addr(char *addr)
{
	/* the MAC is stored in OTP memory page 0xDF */
	u32 ret;
	u64 otp_mac;
	u32 (*otp_read)(u32 page, u32 flags, u64 *page_content) = (void *)0xEF00001A;

	ret = otp_read(0xDF, 0x00, &otp_mac);
	if (!(ret & 0x1)) {
		char *otp_mac_p = (char *)&otp_mac;
		for (ret = 0; ret < 6; ++ret)
			addr[ret] = otp_mac_p[5 - ret];
	}
	return 0;
}
EXPORT_SYMBOL(bfin_get_ether_addr);
