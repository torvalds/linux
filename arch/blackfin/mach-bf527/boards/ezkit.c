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
#include <linux/usb/musb.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <asm/dma.h>
#include <asm/bfin5xx_spi.h>
#include <asm/reboot.h>
#include <asm/nand.h>
#include <asm/portmux.h>
#include <asm/dpmc.h>
#include <linux/spi/ad7877.h>
#include <asm/bfin_sport.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
#ifdef CONFIG_BFIN527_EZKIT_V2
const char bfin_board_name[] = "ADI BF527-EZKIT V2";
#else
const char bfin_board_name[] = "ADI BF527-EZKIT";
#endif

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
		.flags  = IORESOURCE_IRQ,
	},
};

static struct isp1760_platform_data isp1760_priv = {
	.is_isp1761 = 0,
	.bus_width_16 = 1,
	.port1_otg = 0,
	.analog_oc = 0,
	.dack_polarity_high = 0,
	.dreq_polarity_high = 0,
};

static struct platform_device bfin_isp1760_device = {
	.name           = "isp1760",
	.id             = 0,
	.dev = {
		.platform_data = &isp1760_priv,
	},
	.num_resources  = ARRAY_SIZE(bfin_isp1760_resources),
	.resource       = bfin_isp1760_resources,
};
#endif

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
static struct resource musb_resources[] = {
	[0] = {
		.start	= 0xffc03800,
		.end	= 0xffc03cff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {	/* general IRQ */
		.start	= IRQ_USB_INT0,
		.end	= IRQ_USB_INT0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
		.name	= "mc"
	},
	[2] = {	/* DMA IRQ */
		.start	= IRQ_USB_DMA,
		.end	= IRQ_USB_DMA,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
		.name	= "dma"
	},
};

static struct musb_hdrc_config musb_config = {
	.multipoint	= 0,
	.dyn_fifo	= 0,
	.soft_con	= 1,
	.dma		= 1,
	.num_eps	= 8,
	.dma_channels	= 8,
	.gpio_vrsel	= GPIO_PG13,
	/* Some custom boards need to be active low, just set it to "0"
	 * if it is the case.
	 */
	.gpio_vrsel_active	= 1,
	.clkin          = 24,           /* musb CLKIN in MHZ */
};

static struct musb_hdrc_platform_data musb_plat = {
#if defined(CONFIG_USB_MUSB_HDRC) && defined(CONFIG_USB_GADGET_MUSB_HDRC)
	.mode		= MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC)
	.mode		= MUSB_HOST,
#elif defined(CONFIG_USB_GADGET_MUSB_HDRC)
	.mode		= MUSB_PERIPHERAL,
#endif
	.config		= &musb_config,
};

static u64 musb_dmamask = ~(u32)0;

static struct platform_device musb_device = {
	.name		= "musb-blackfin",
	.id		= 0,
	.dev = {
		.dma_mask		= &musb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &musb_plat,
	},
	.num_resources	= ARRAY_SIZE(musb_resources),
	.resource	= musb_resources,
};
#endif

#if defined(CONFIG_FB_BFIN_T350MCQB) || defined(CONFIG_FB_BFIN_T350MCQB_MODULE)

static struct resource bf52x_t350mcqb_resources[] = {
	{
		.start = IRQ_PPI_ERROR,
		.end = IRQ_PPI_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bf52x_t350mcqb_device = {
	.name		= "bfin-t350mcqb",
	.id		= -1,
	.num_resources 	= ARRAY_SIZE(bf52x_t350mcqb_resources),
	.resource 	= bf52x_t350mcqb_resources,
};
#endif

#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
#include <asm/bfin-lq035q1.h>

static struct bfin_lq035q1fb_disp_info bfin_lq035q1_data = {
	.mode = LQ035_NORM | LQ035_RGB | LQ035_RL | LQ035_TB,
	.ppi_mode = USE_RGB565_8_BIT_PPI,
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
	.num_resources	= ARRAY_SIZE(bfin_lq035q1_resources),
	.resource	= bfin_lq035q1_resources,
	.dev		= {
		.platform_data = &bfin_lq035q1_data,
	},
};
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition ezkit_partitions[] = {
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

static struct physmap_flash_data ezkit_flash_data = {
	.width      = 2,
	.parts      = ezkit_partitions,
	.nr_parts   = ARRAY_SIZE(ezkit_partitions),
};

static struct resource ezkit_flash_resource = {
	.start = 0x20000000,
	.end   = 0x203fffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device ezkit_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &ezkit_flash_data,
	},
	.num_resources = 1,
	.resource      = &ezkit_flash_resource,
};
#endif

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
static struct mtd_partition partition_info[] = {
	{
		.name = "bootloader(nand)",
		.offset = 0,
		.size = 0x40000,
	}, {
		.name = "linux kernel(nand)",
		.offset = MTDPART_OFS_APPEND,
		.size = 4 * 1024 * 1024,
	},
	{
		.name = "file system(nand)",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static struct bf5xx_nand_platform bf5xx_nand_platform = {
	.data_width = NFC_NWIDTH_8,
	.partitions = partition_info,
	.nr_partitions = ARRAY_SIZE(partition_info),
	.rd_dly = 3,
	.wr_dly = 3,
};

static struct resource bf5xx_nand_resources[] = {
	{
		.start = NFC_CTL,
		.end = NFC_DATA_RD + 2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = CH_NFC,
		.end = CH_NFC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bf5xx_nand_device = {
	.name = "bf5xx-nand",
	.id = 0,
	.num_resources = ARRAY_SIZE(bf5xx_nand_resources),
	.resource = bf5xx_nand_resources,
	.dev = {
		.platform_data = &bf5xx_nand_platform,
	},
};
#endif

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

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= 0x203FB800,
		.end	= 0x203FB800 + 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0x203FB800 + 4,
		.end	= 0x203FB800 + 5,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
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

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
#include <linux/bfin_mac.h>
static const unsigned short bfin_mac_peripherals[] = P_RMII0;

static struct bfin_phydev_platform_data bfin_phydev_data[] = {
	{
		.addr = 1,
		.irq = IRQ_MAC_PHYINT,
	},
};

static struct bfin_mii_bus_platform_data bfin_mii_bus_data = {
	.phydev_number = 1,
	.phydev_data = bfin_phydev_data,
	.phy_mode = PHY_INTERFACE_MODE_RMII,
	.mac_peripherals = bfin_mac_peripherals,
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
static struct bfin5xx_spi_chip  mmc_spi_chip_info = {
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
	.gpio_export		= 0,	/* Export GPIO to gpiolib */
};
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)

static const u16 bfin_snd_pin[][7] = {
	{P_SPORT0_DTPRI, P_SPORT0_TSCLK, P_SPORT0_RFS,
		P_SPORT0_DRPRI, P_SPORT0_RSCLK, 0, 0},
	{P_SPORT1_DTPRI, P_SPORT1_TSCLK, P_SPORT1_RFS,
		P_SPORT1_DRPRI, P_SPORT1_RSCLK, P_SPORT1_TFS, 0},
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

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources = ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
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

#if defined(CONFIG_SND_BF5XX_SOC_AD183X) \
	|| defined(CONFIG_SND_BF5XX_SOC_AD183X_MODULE)
	{
		.modalias = "ad183x",
		.max_speed_hz = 3125000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 4,
		.platform_data = "ad1836",
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 20000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 3,
		.controller_data = &mmc_spi_chip_info,
		.mode = SPI_MODE_0,
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
#if defined(CONFIG_TOUCHSCREEN_AD7879_SPI) || defined(CONFIG_TOUCHSCREEN_AD7879_SPI_MODULE)
	{
		.modalias = "ad7879",
		.platform_data = &bfin_ad7879_ts_info,
		.irq = IRQ_PF8,
		.max_speed_hz = 5000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = 3,
		.mode = SPI_CPHA | SPI_CPOL,
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
		.chip_select = 7,
		.mode = SPI_CPHA | SPI_CPOL,
	},
#endif
};

#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
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
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = IRQ_SPI,
		.end   = IRQ_SPI,
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
#ifdef CONFIG_BFIN_UART1_CTSRTS
	{	/* CTS pin */
		.start = GPIO_PF9,
		.end = GPIO_PF9,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin */
		.start = GPIO_PF10,
		.end = GPIO_PF10,
		.flags = IORESOURCE_IO,
	},
#endif
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

#if defined(CONFIG_PMIC_ADP5520) || defined(CONFIG_PMIC_ADP5520_MODULE)
#include <linux/mfd/adp5520.h>

	/*
	 *  ADP5520/5501 LEDs Data
	 */

static struct led_info adp5520_leds[] = {
	{
		.name = "adp5520-led1",
		.default_trigger = "none",
		.flags = FLAG_ID_ADP5520_LED1_ADP5501_LED0 | ADP5520_LED_OFFT_600ms,
	},
};

static struct adp5520_leds_platform_data adp5520_leds_data = {
	.num_leds = ARRAY_SIZE(adp5520_leds),
	.leds = adp5520_leds,
	.fade_in = ADP5520_FADE_T_600ms,
	.fade_out = ADP5520_FADE_T_600ms,
	.led_on_time = ADP5520_LED_ONT_600ms,
};

	/*
	 *  ADP5520 Keypad Data
	 */

static const unsigned short adp5520_keymap[ADP5520_KEYMAPSIZE] = {
	[ADP5520_KEY(3, 3)]	= KEY_1,
	[ADP5520_KEY(2, 3)]	= KEY_2,
	[ADP5520_KEY(1, 3)]	= KEY_3,
	[ADP5520_KEY(0, 3)]	= KEY_UP,
	[ADP5520_KEY(3, 2)]	= KEY_4,
	[ADP5520_KEY(2, 2)]	= KEY_5,
	[ADP5520_KEY(1, 2)]	= KEY_6,
	[ADP5520_KEY(0, 2)]	= KEY_DOWN,
	[ADP5520_KEY(3, 1)]	= KEY_7,
	[ADP5520_KEY(2, 1)]	= KEY_8,
	[ADP5520_KEY(1, 1)]	= KEY_9,
	[ADP5520_KEY(0, 1)]	= KEY_DOT,
	[ADP5520_KEY(3, 0)]	= KEY_BACKSPACE,
	[ADP5520_KEY(2, 0)]	= KEY_0,
	[ADP5520_KEY(1, 0)]	= KEY_HELP,
	[ADP5520_KEY(0, 0)]	= KEY_ENTER,
};

static struct adp5520_keys_platform_data adp5520_keys_data = {
	.rows_en_mask	= ADP5520_ROW_R3 | ADP5520_ROW_R2 | ADP5520_ROW_R1 | ADP5520_ROW_R0,
	.cols_en_mask	= ADP5520_COL_C3 | ADP5520_COL_C2 | ADP5520_COL_C1 | ADP5520_COL_C0,
	.keymap		= adp5520_keymap,
	.keymapsize	= ARRAY_SIZE(adp5520_keymap),
	.repeat		= 0,
};

	/*
	 *  ADP5520/5501 Multifunction Device Init Data
	 */

static struct adp5520_platform_data adp5520_pdev_data = {
	.leds = &adp5520_leds_data,
	.keys = &adp5520_keys_data,
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
#if defined(CONFIG_FB_BFIN_7393) || defined(CONFIG_FB_BFIN_7393_MODULE)
	{
		I2C_BOARD_INFO("bfin-adv7393", 0x2B),
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7879_I2C) || defined(CONFIG_TOUCHSCREEN_AD7879_I2C_MODULE)
	{
		I2C_BOARD_INFO("ad7879", 0x2C),
		.irq = IRQ_PF8,
		.platform_data = (void *)&bfin_ad7879_ts_info,
	},
#endif
#if defined(CONFIG_PMIC_ADP5520) || defined(CONFIG_PMIC_ADP5520_MODULE)
	{
		I2C_BOARD_INFO("pmic-adp5520", 0x32),
		.irq = IRQ_PF9,
		.platform_data = (void *)&adp5520_pdev_data,
	},
#endif
#if defined(CONFIG_SND_SOC_SSM2602) || defined(CONFIG_SND_SOC_SSM2602_MODULE)
	{
		I2C_BOARD_INFO("ssm2602", 0x1b),
	},
#endif
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_BFIN_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("ad5252", 0x2f),
	},
#endif
#if defined(CONFIG_SND_SOC_ADAU1373) || defined(CONFIG_SND_SOC_ADAU1373_MODULE)
	{
		I2C_BOARD_INFO("adau1373", 0x1A),
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

#if defined(CONFIG_INPUT_BFIN_ROTARY) || defined(CONFIG_INPUT_BFIN_ROTARY_MODULE)
#include <asm/bfin_rotary.h>

static struct bfin_rotary_platform_data bfin_rotary_data = {
	/*.rotary_up_key     = KEY_UP,*/
	/*.rotary_down_key   = KEY_DOWN,*/
	.rotary_rel_code   = REL_WHEEL,
	.rotary_button_key = KEY_ENTER,
	.debounce	   = 10,	/* 0..17 */
	.mode		   = ROT_QUAD_ENC | ROT_DEBE,
	.pm_wakeup	   = 1,
};

static struct resource bfin_rotary_resources[] = {
	{
		.start = IRQ_CNT,
		.end = IRQ_CNT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_rotary_device = {
	.name		= "bfin-rotary",
	.id		= -1,
	.num_resources 	= ARRAY_SIZE(bfin_rotary_resources),
	.resource 	= bfin_rotary_resources,
	.dev		= {
		.platform_data = &bfin_rotary_data,
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

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
	&bf5xx_nand_device,
#endif

#if defined(CONFIG_BFIN_CFPCMCIA) || defined(CONFIG_BFIN_CFPCMCIA_MODULE)
	&bfin_pcmcia_cf_device,
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
	&bfin_isp1760_device,
#endif

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
	&musb_device,
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif

#if defined(CONFIG_DM9000) || defined(CONFIG_DM9000_MODULE)
	&dm9000_device,
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
	&bfin_mii_bus,
	&bfin_mac_device,
#endif

#if defined(CONFIG_USB_NET2272) || defined(CONFIG_USB_NET2272_MODULE)
	&net2272_bfin_device,
#endif

#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_FB_BFIN_T350MCQB) || defined(CONFIG_FB_BFIN_T350MCQB_MODULE)
	&bf52x_t350mcqb_device,
#endif

#if defined(CONFIG_FB_BFIN_LQ035Q1) || defined(CONFIG_FB_BFIN_LQ035Q1_MODULE)
	&bfin_lq035q1_device,
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

#if defined(CONFIG_INPUT_BFIN_ROTARY) || defined(CONFIG_INPUT_BFIN_ROTARY_MODULE)
	&bfin_rotary_device,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ezkit_flash_device,
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
	&bfin_i2s_pcm,
#endif

#if defined(CONFIG_SND_BF5XX_AC97) || defined(CONFIG_SND_BF5XX_AC97_MODULE)
	&bfin_ac97_pcm,
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
	&bfin_i2s,
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD1836) || \
	defined(CONFIG_SND_BF5XX_SOC_AD1836_MODULE)
	&bfin_ad1836_machine,
#endif
};

static int __init ezkit_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
	platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(ezkit_init);

static struct platform_device *ezkit_early_devices[] __initdata = {
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
	early_platform_add_devices(ezkit_early_devices,
		ARRAY_SIZE(ezkit_early_devices));
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
