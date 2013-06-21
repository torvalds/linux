/*
 * Copyright 2004-2009 Analog Devices Inc.
 *                2005 National ICT Australia (NICTA)
 *                      Aidan Williams <aidan@nicta.com.au>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/spi/mmc_spi.h>
#if defined(CONFIG_USB_ISP1362_HCD) || defined(CONFIG_USB_ISP1362_HCD_MODULE)
#include <linux/usb/isp1362.h>
#endif
#include <linux/irq.h>
#include <linux/i2c.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF533-STAMP";

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

/*
 *  Driver needs to know address, irq and flag pin.
 */
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
#include <linux/smc91x.h>

static struct smc91x_platdata smc91x_info = {
	.flags = SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda = RPC_LED_100_10,
	.ledb = RPC_LED_TX_RX,
};

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
	.dev	= {
		.platform_data	= &smc91x_info,
	},
};
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
static struct resource net2272_bfin_resources[] = {
	{
		.start = 0x20300000,
		.end = 0x20300000 + 0x100,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 1,
		.flags = IORESOURCE_BUS,
	}, {
		.start = IRQ_PF10,
		.end = IRQ_PF10,
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

#if defined(CONFIG_MTD_BFIN_ASYNC) || defined(CONFIG_MTD_BFIN_ASYNC_MODULE)
static struct mtd_partition stamp_partitions[] = {
	{
		.name   = "bootloader(nor)",
		.size   = 0x40000,
		.offset = 0,
	}, {
		.name   = "linux kernel(nor)",
		.size   = 0x180000,
		.offset = MTDPART_OFS_APPEND,
	}, {
		.name   = "file system(nor)",
		.size   = MTDPART_SIZ_FULL,
		.offset = MTDPART_OFS_APPEND,
	}
};

static struct physmap_flash_data stamp_flash_data = {
	.width    = 2,
	.parts    = stamp_partitions,
	.nr_parts = ARRAY_SIZE(stamp_partitions),
};

static struct resource stamp_flash_resource[] = {
	{
		.name  = "cfi_probe",
		.start = 0x20000000,
		.end   = 0x203fffff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 0x7BB07BB0,	/* AMBCTL0 setting when accessing flash */
		.end   = 0x7BB07BB0,	/* AMBCTL1 setting when accessing flash */
		.flags = IORESOURCE_MEM,
	}, {
		.start = GPIO_PF0,
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device stamp_flash_device = {
	.name          = "bfin-async-flash",
	.id            = 0,
	.dev = {
		.platform_data = &stamp_flash_data,
	},
	.num_resources = ARRAY_SIZE(stamp_flash_resource),
	.resource      = stamp_flash_resource,
};
#endif

#if defined(CONFIG_MTD_M25P80) || defined(CONFIG_MTD_M25P80_MODULE)
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
	.type = "m25p64",
};

/* SPI flash chip (m25p64) */
static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
};
#endif

#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
#define MMC_SPI_CARD_DETECT_INT IRQ_PF5
static int bfin_mmc_spi_init(struct device *dev,
	irqreturn_t (*detect_int)(int, void *), void *data)
{
	return request_irq(MMC_SPI_CARD_DETECT_INT, detect_int,
		IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
		"mmc-spi-detect", data);
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
	.pio_interrupt = 0,
};
#endif

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) || defined(CONFIG_MTD_M25P80_MODULE)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 2, /* Framework chip select. On STAMP537 it is SPISSEL2*/
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD1836) || \
	defined(CONFIG_SND_BF5XX_SOC_AD1836_MODULE)
	{
		.modalias = "ad1836",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
		.platform_data = "ad1836", /* only includes chip name for the moment */
		.mode = SPI_MODE_3,
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
};

#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
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
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI,
		.end   = IRQ_SPI,
		.flags = IORESOURCE_IRQ,
	}
};

/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = 8,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
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

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = BFIN_UART_THR,
		.end = BFIN_UART_GCTL+2,
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
#endif

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
	{BTN_0, GPIO_PF5, 0, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PF6, 0, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PF8, 0, "gpio-keys: BTN2"},
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

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
#include <linux/i2c-gpio.h>

static struct i2c_gpio_platform_data i2c_gpio_data = {
	.sda_pin		= GPIO_PF2,
	.scl_pin		= GPIO_PF3,
	.sda_is_open_drain	= 0,
	.scl_is_open_drain	= 0,
	.udelay			= 10,
};

static struct platform_device i2c_gpio_device = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev		= {
		.platform_data	= &i2c_gpio_data,
	},
};
#endif

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if defined(CONFIG_JOYSTICK_AD7142) || defined(CONFIG_JOYSTICK_AD7142_MODULE)
	{
		I2C_BOARD_INFO("ad7142_joystick", 0x2C),
		.irq = 39,
	},
#endif
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_BFIN_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if defined(CONFIG_INPUT_PCF8574) || defined(CONFIG_INPUT_PCF8574_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = 39,
	},
#endif
#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
	{
		I2C_BOARD_INFO("bfin-adv7393", 0x2B),
	},
#endif
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_BFIN_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("ad5252", 0x2f),
	},
#endif
};

static const unsigned int cclk_vlev_datasheet[] =
{
	VRPAIR(VLEV_085, 250000000),
	VRPAIR(VLEV_090, 376000000),
	VRPAIR(VLEV_095, 426000000),
	VRPAIR(VLEV_100, 426000000),
	VRPAIR(VLEV_105, 476000000),
	VRPAIR(VLEV_110, 476000000),
	VRPAIR(VLEV_115, 476000000),
	VRPAIR(VLEV_120, 600000000),
	VRPAIR(VLEV_125, 600000000),
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

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE) || \
	defined(CONFIG_SND_BF5XX_AC97) || \
	defined(CONFIG_SND_BF5XX_AC97_MODULE)

#include <asm/bfin_sport.h>

#define SPORT_REQ(x) \
	[x] = {P_SPORT##x##_TFS, P_SPORT##x##_DTPRI, P_SPORT##x##_TSCLK, \
		P_SPORT##x##_RFS, P_SPORT##x##_DRPRI, P_SPORT##x##_RSCLK, 0}

static const u16 bfin_snd_pin[][7] = {
	SPORT_REQ(0),
	SPORT_REQ(1),
};

static struct bfin_snd_platform_data bfin_snd_data[] = {
	{
		.pin_req = &bfin_snd_pin[0][0],
	},
	{
		.pin_req = &bfin_snd_pin[1][0],
	},
};

#define BFIN_SND_RES(x) \
	[x] = { \
		{ \
			.start = SPORT##x##_TCR1, \
			.end = SPORT##x##_TCR1, \
			.flags = IORESOURCE_MEM \
		}, \
		{ \
			.start = CH_SPORT##x##_RX, \
			.end = CH_SPORT##x##_RX, \
			.flags = IORESOURCE_DMA, \
		}, \
		{ \
			.start = CH_SPORT##x##_TX, \
			.end = CH_SPORT##x##_TX, \
			.flags = IORESOURCE_DMA, \
		}, \
		{ \
			.start = IRQ_SPORT##x##_ERROR, \
			.end = IRQ_SPORT##x##_ERROR, \
			.flags = IORESOURCE_IRQ, \
		} \
	}

static struct resource bfin_snd_resources[][4] = {
	BFIN_SND_RES(0),
	BFIN_SND_RES(1),
};
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
static struct platform_device bfin_i2s_pcm = {
	.name = "bfin-i2s-pcm-audio",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF5XX_AC97) || defined(CONFIG_SND_BF5XX_AC97_MODULE)
static struct platform_device bfin_ac97_pcm = {
	.name = "bfin-ac97-pcm-audio",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD1836) \
	        || defined(CONFIG_SND_BF5XX_SOC_AD1836_MODULE)
static const char * const ad1836_link[] = {
	"bfin-i2s.0",
	"spi0.4",
};
static struct platform_device bfin_ad1836_machine = {
	.name = "bfin-snd-ad1836",
	.id = -1,
	.dev = {
		.platform_data = (void *)ad1836_link,
	},
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD73311) || \
	defined(CONFIG_SND_BF5XX_SOC_AD73311_MODULE)
static const unsigned ad73311_gpio[] = {
	GPIO_PF4,
};

static struct platform_device bfin_ad73311_machine = {
	.name = "bfin-snd-ad73311",
	.id = 1,
	.dev = {
		.platform_data = (void *)ad73311_gpio,
	},
};
#endif

#if defined(CONFIG_SND_SOC_AD73311) || defined(CONFIG_SND_SOC_AD73311_MODULE)
static struct platform_device bfin_ad73311_codec_device = {
	.name = "ad73311",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_SOC_AD74111) || defined(CONFIG_SND_SOC_AD74111_MODULE)
static struct platform_device bfin_ad74111_codec_device = {
	.name = "ad74111",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_I2S) || \
	defined(CONFIG_SND_BF5XX_SOC_I2S_MODULE)
static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources =
		ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AC97) || \
	defined(CONFIG_SND_BF5XX_SOC_AC97_MODULE)
static struct platform_device bfin_ac97 = {
	.name = "bfin-ac97",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources =
		ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
};
#endif

static struct platform_device *stamp_devices[] __initdata = {

	&bfin_dpmc,

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	&net2272_bfin_device,
#endif

#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || \
	defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
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

#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
	&i2c_gpio_device,
#endif

#if defined(CONFIG_MTD_BFIN_ASYNC) || defined(CONFIG_MTD_BFIN_ASYNC_MODULE)
	&stamp_flash_device,
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
	&bfin_i2s_pcm,
#endif

#if defined(CONFIG_SND_BF5XX_AC97) || defined(CONFIG_SND_BF5XX_AC97_MODULE)
	&bfin_ac97_pcm,
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD1836) || \
	defined(CONFIG_SND_BF5XX_SOC_AD1836_MODULE)
	&bfin_ad1836_machine,
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD73311) || \
	defined(CONFIG_SND_BF5XX_SOC_AD73311_MODULE)
	&bfin_ad73311_machine,
#endif

#if defined(CONFIG_SND_SOC_AD73311) || defined(CONFIG_SND_SOC_AD73311_MODULE)
	&bfin_ad73311_codec_device,
#endif

#if defined(CONFIG_SND_SOC_AD74111) || defined(CONFIG_SND_SOC_AD74111_MODULE)
	&bfin_ad74111_codec_device,
#endif

#if defined(CONFIG_SND_BF5XX_SOC_I2S) || \
	defined(CONFIG_SND_BF5XX_SOC_I2S_MODULE)
	&bfin_i2s,
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AC97) || \
	defined(CONFIG_SND_BF5XX_SOC_AC97_MODULE)
	&bfin_ac97,
#endif
};

static int __init net2272_init(void)
{
#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	int ret;

	/* Set PF0 to 0, PF1 to 1 make /AMS3 work properly */
	ret = gpio_request(GPIO_PF0, "net2272");
	if (ret)
		return ret;

	ret = gpio_request(GPIO_PF1, "net2272");
	if (ret) {
		gpio_free(GPIO_PF0);
		return ret;
	}

	ret = gpio_request(GPIO_PF11, "net2272");
	if (ret) {
		gpio_free(GPIO_PF0);
		gpio_free(GPIO_PF1);
		return ret;
	}

	gpio_direction_output(GPIO_PF0, 0);
	gpio_direction_output(GPIO_PF1, 1);

	/* Reset the USB chip */
	gpio_direction_output(GPIO_PF11, 0);
	mdelay(2);
	gpio_set_value(GPIO_PF11, 1);
#endif

	return 0;
}

static int __init stamp_init(void)
{
	int ret;

	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));

	ret = platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	if (ret < 0)
		return ret;

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	/*
	 * setup BF533_STAMP CPLD to route AMS3 to Ethernet MAC.
	 * the bfin-async-map driver takes care of flipping between
	 * flash and ethernet when necessary.
	 */
	ret = gpio_request(GPIO_PF0, "enet_cpld");
	if (!ret) {
		gpio_direction_output(GPIO_PF0, 1);
		gpio_free(GPIO_PF0);
	}
#endif

	if (net2272_init())
		pr_warning("unable to configure net2272; it probably won't work\n");

	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(stamp_init);

static struct platform_device *stamp_early_devices[] __initdata = {
#if defined(CONFIG_SERIAL_BFIN_CONSOLE) || defined(CONFIG_EARLY_PRINTK)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
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
	early_platform_add_devices(stamp_early_devices,
		ARRAY_SIZE(stamp_early_devices));
}

void native_machine_restart(char *cmd)
{
	/* workaround pull up on cpld / flash pin not being strong enough */
	gpio_request(GPIO_PF0, "flash_cpld");
	gpio_direction_output(GPIO_PF0, 0);
}
