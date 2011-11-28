/*
 * Copyright 2004-20010 Analog Devices Inc.
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


/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF527-AD7160EVAL";

/*
 *  Driver needs to know address, irq and flag pin.
 */

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
	},
	[2] = {	/* DMA IRQ */
		.start	= IRQ_USB_DMA,
		.end	= IRQ_USB_DMA,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
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
#if defined(CONFIG_USB_MUSB_OTG)
	.mode		= MUSB_OTG,
#elif defined(CONFIG_USB_MUSB_HDRC_HCD)
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

#if defined(CONFIG_FB_BFIN_RA158Z) || defined(CONFIG_FB_BFIN_RA158Z_MODULE)
static struct resource bf52x_ra158z_resources[] = {
	{
		.start = IRQ_PPI_ERROR,
		.end = IRQ_PPI_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bf52x_ra158z_device = {
	.name		= "bfin-ra158z",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(bf52x_ra158z_resources),
	.resource	= bf52x_ra158z_resources,
};
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition ad7160eval_partitions[] = {
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

static struct physmap_flash_data ad7160eval_flash_data = {
	.width      = 2,
	.parts      = ad7160eval_partitions,
	.nr_parts   = ARRAY_SIZE(ad7160eval_partitions),
};

static struct resource ad7160eval_flash_resource = {
	.start = 0x20000000,
	.end   = 0x203fffff,
	.flags = IORESOURCE_MEM,
};

static struct platform_device ad7160eval_flash_device = {
	.name          = "physmap-flash",
	.id            = 0,
	.dev = {
		.platform_data = &ad7160eval_flash_data,
	},
	.num_resources = 1,
	.resource      = &ad7160eval_flash_resource,
};
#endif

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
static struct mtd_partition partition_info[] = {
	{
		.name = "linux kernel(nand)",
		.offset = 0,
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

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
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

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	/* TODO: add platform data here */
};
#endif

#if defined(CONFIG_SND_BF5XX_TDM) || defined(CONFIG_SND_BF5XX_TDM_MODULE)
static struct platform_device bfin_tdm = {
	.name = "bfin-tdm",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	/* TODO: add platform data here */
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
	},
#endif
#if defined(CONFIG_MMC_SPI) || defined(CONFIG_MMC_SPI_MODULE)
	{
		.modalias = "mmc_spi",
		.max_speed_hz = 30000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0,
		.chip_select = GPIO_PH3 + MAX_CTRL_CS,
		.controller_data = &mmc_spi_chip_info,
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
};

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
/* SPI controller data */
static struct bfin5xx_spi_master bfin_spi0_info = {
	.num_chipselect = MAX_CTRL_CS + MAX_BLACKFIN_GPIOS,
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

#if defined(CONFIG_TOUCHSCREEN_AD7160) || defined(CONFIG_TOUCHSCREEN_AD7160_MODULE)
#include <linux/input/ad7160.h>
static const struct ad7160_platform_data bfin_ad7160_ts_info = {
	.sensor_x_res = 854,
	.sensor_y_res = 480,
	.pressure = 100,
	.filter_coef = 3,
	.coord_pref = AD7160_ORIG_TOP_LEFT,
	.first_touch_window = 5,
	.move_window = 3,
	.event_cabs = AD7160_EMIT_ABS_MT_TRACKING_ID |
			AD7160_EMIT_ABS_MT_PRESSURE |
			AD7160_TRACKING_ID_ASCENDING,
	.finger_act_ctrl = 0x64,
	.haptic_effect1_ctrl = AD7160_HAPTIC_SLOT_A(60) |
				AD7160_HAPTIC_SLOT_A_LVL_HIGH |
				AD7160_HAPTIC_SLOT_B(60) |
				AD7160_HAPTIC_SLOT_B_LVL_LOW,

	.haptic_effect2_ctrl = AD7160_HAPTIC_SLOT_A(20) |
				AD7160_HAPTIC_SLOT_A_LVL_HIGH |
				AD7160_HAPTIC_SLOT_B(80) |
				AD7160_HAPTIC_SLOT_B_LVL_LOW |
				AD7160_HAPTIC_SLOT_C(120) |
				AD7160_HAPTIC_SLOT_C_LVL_HIGH |
				AD7160_HAPTIC_SLOT_D(30) |
				AD7160_HAPTIC_SLOT_D_LVL_LOW,
};
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

static struct i2c_board_info __initdata bfin_i2c_board_info[] = {
#if defined(CONFIG_TOUCHSCREEN_AD7160) || defined(CONFIG_TOUCHSCREEN_AD7160_MODULE)
	{
		I2C_BOARD_INFO("ad7160", 0x33),
		.irq = IRQ_PH1,
		.platform_data = (void *)&bfin_ad7160_ts_info,
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

#if defined(CONFIG_INPUT_BFIN_ROTARY) || defined(CONFIG_INPUT_BFIN_ROTARY_MODULE)
#include <asm/bfin_rotary.h>

static struct bfin_rotary_platform_data bfin_rotary_data = {
	/*.rotary_up_key     = KEY_UP,*/
	/*.rotary_down_key   = KEY_DOWN,*/
	.rotary_rel_code   = REL_WHEEL,
	.rotary_button_key = KEY_ENTER,
	.debounce	   = 10,	/* 0..17 */
	.mode		   = ROT_QUAD_ENC | ROT_DEBE,
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
	.num_resources	= ARRAY_SIZE(bfin_rotary_resources),
	.resource	= bfin_rotary_resources,
	.dev		= {
		.platform_data = &bfin_rotary_data,
	},
};
#endif

static const unsigned int cclk_vlev_datasheet[] = {
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

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
	&musb_device,
#endif

#if defined(CONFIG_BFIN_MAC) || defined(CONFIG_BFIN_MAC_MODULE)
	&bfin_mii_bus,
	&bfin_mac_device,
#endif

#if defined(CONFIG_SPI_BFIN) || defined(CONFIG_SPI_BFIN_MODULE)
	&bfin_spi0_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
	&bfin_uart0_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	&bfin_uart1_device,
#endif
#endif

#if defined(CONFIG_FB_BFIN_RA158Z) || defined(CONFIG_FB_BFIN_RA158Z_MODULE)
	&bf52x_ra158z_device,
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

#if defined(CONFIG_INPUT_BFIN_ROTARY) || defined(CONFIG_INPUT_BFIN_ROTARY_MODULE)
	&bfin_rotary_device,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ad7160eval_flash_device,
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
	&bfin_i2s,
#endif

#if defined(CONFIG_SND_BF5XX_TDM) || defined(CONFIG_SND_BF5XX_TDM_MODULE)
	&bfin_tdm,
#endif
};

static int __init ad7160eval_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);
	i2c_register_board_info(0, bfin_i2c_board_info,
				ARRAY_SIZE(bfin_i2c_board_info));
	platform_add_devices(stamp_devices, ARRAY_SIZE(stamp_devices));
	spi_register_board_info(bfin_spi_board_info, ARRAY_SIZE(bfin_spi_board_info));
	return 0;
}

arch_initcall(ad7160eval_init);

static struct platform_device *ad7160eval_early_devices[] __initdata = {
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
	early_platform_add_devices(ad7160eval_early_devices,
		ARRAY_SIZE(ad7160eval_early_devices));
}

void native_machine_restart(char *cmd)
{
	/* workaround reboot hang when booting from SPI */
	if ((bfin_read_SYSCR() & 0x7) == 0x3)
		bfin_reset_boot_spi_cs(P_DEFAULT_BOOT_SPI_CS);
}

void bfin_get_ether_addr(char *addr)
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
}
EXPORT_SYMBOL(bfin_get_ether_addr);
