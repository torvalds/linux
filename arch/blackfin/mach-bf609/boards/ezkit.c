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
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/usb/musb.h>
#include <asm/bfin6xx_spi.h>
#include <asm/dma.h>
#include <asm/gpio.h>
#include <asm/nand.h>
#include <asm/dpmc.h>
#include <asm/portmux.h>
#include <asm/bfin_sdh.h>
#include <linux/input.h>
#include <linux/spi/ad7877.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF609-EZKIT";

/*
 *  Driver needs to know address, irq and flag pin.
 */

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
#include <linux/usb/isp1760.h>
static struct resource bfin_isp1760_resources[] = {
	[0] = {
		.start  = 0x2C0C0000,
		.end    = 0x2C0C0000 + 0xfffff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_PG7,
		.end    = IRQ_PG7,
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
	.num_resources 	= ARRAY_SIZE(bfin_rotary_resources),
	.resource 	= bfin_rotary_resources,
	.dev		= {
		.platform_data = &bfin_rotary_data,
	},
};
#endif

#if defined(CONFIG_STMMAC_ETH) || defined(CONFIG_STMMAC_ETH_MODULE)
#include <linux/stmmac.h>

static unsigned short pins[] = P_RMII0;

static struct stmmac_mdio_bus_data phy_private_data = {
	.bus_id = 0,
	.phy_mask = 1,
};

static struct plat_stmmacenet_data eth_private_data = {
	.bus_id   = 0,
	.enh_desc = 1,
	.phy_addr = 1,
	.mdio_bus_data = &phy_private_data,
};

static struct platform_device bfin_eth_device = {
	.name           = "stmmaceth",
	.id             = 0,
	.num_resources  = 2,
	.resource       = (struct resource[]) {
		{
			.start  = EMAC0_MACCFG,
			.end    = EMAC0_MACCFG + 0x1274,
			.flags  = IORESOURCE_MEM,
		},
		{
			.name   = "macirq",
			.start  = IRQ_EMAC0_STAT,
			.end    = IRQ_EMAC0_STAT,
			.flags  = IORESOURCE_IRQ,
		},
	},
	.dev = {
		.power.can_wakeup = 1,
		.platform_data = &eth_private_data,
	}
};
#endif

#if defined(CONFIG_INPUT_ADXL34X) || defined(CONFIG_INPUT_ADXL34X_MODULE)
#include <linux/input/adxl34x.h>
static const struct adxl34x_platform_data adxl34x_info = {
	.x_axis_offset = 0,
	.y_axis_offset = 0,
	.z_axis_offset = 0,
	.tap_threshold = 0x31,
	.tap_duration = 0x10,
	.tap_latency = 0x60,
	.tap_window = 0xF0,
	.tap_axis_control = ADXL_TAP_X_EN | ADXL_TAP_Y_EN | ADXL_TAP_Z_EN,
	.act_axis_control = 0xFF,
	.activity_threshold = 5,
	.inactivity_threshold = 3,
	.inactivity_time = 4,
	.free_fall_threshold = 0x7,
	.free_fall_time = 0x20,
	.data_rate = 0x8,
	.data_range = ADXL_FULL_RES,

	.ev_type = EV_ABS,
	.ev_code_x = ABS_X,		/* EV_REL */
	.ev_code_y = ABS_Y,		/* EV_REL */
	.ev_code_z = ABS_Z,		/* EV_REL */

	.ev_code_tap = {BTN_TOUCH, BTN_TOUCH, BTN_TOUCH}, /* EV_KEY x,y,z */

/*	.ev_code_ff = KEY_F,*/		/* EV_KEY */
/*	.ev_code_act_inactivity = KEY_A,*/	/* EV_KEY */
	.power_mode = ADXL_AUTO_SLEEP | ADXL_LINK,
	.fifo_mode = ADXL_FIFO_STREAM,
	.orientation_enable = ADXL_EN_ORIENTATION_3D,
	.deadzone_angle = ADXL_DEADZONE_ANGLE_10p8,
	.divisor_length = ADXL_LP_FILTER_DIVISOR_16,
	/* EV_KEY {+Z, +Y, +X, -X, -Y, -Z} */
	.ev_codes_orient_3d = {BTN_Z, BTN_Y, BTN_X, BTN_A, BTN_B, BTN_C},
};
#endif

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
#ifdef CONFIG_SERIAL_BFIN_UART0
static struct resource bfin_uart0_resources[] = {
	{
		.start = UART0_REVID,
		.end = UART0_RXDIV+4,
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
		.start = IRQ_UART0_STAT,
		.end = IRQ_UART0_STAT,
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
#ifdef CONFIG_BFIN_UART0_CTSRTS
	{	/* CTS pin -- 0 means not supported */
		.start = GPIO_PD10,
		.end = GPIO_PD10,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin -- 0 means not supported */
		.start = GPIO_PD9,
		.end = GPIO_PD9,
		.flags = IORESOURCE_IO,
	},
#endif
};

static unsigned short bfin_uart0_peripherals[] = {
	P_UART0_TX, P_UART0_RX,
#ifdef CONFIG_BFIN_UART0_CTSRTS
	P_UART0_RTS, P_UART0_CTS,
#endif
	0
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
		.start = UART1_REVID,
		.end = UART1_RXDIV+4,
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
		.start = IRQ_UART1_STAT,
		.end = IRQ_UART1_STAT,
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
	{	/* CTS pin -- 0 means not supported */
		.start = GPIO_PG13,
		.end = GPIO_PG13,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin -- 0 means not supported */
		.start = GPIO_PG10,
		.end = GPIO_PG10,
		.flags = IORESOURCE_IO,
	},
#endif
};

static unsigned short bfin_uart1_peripherals[] = {
	P_UART1_TX, P_UART1_RX,
#ifdef CONFIG_BFIN_UART1_CTSRTS
	P_UART1_RTS, P_UART1_CTS,
#endif
	0
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
		.start = IRQ_UART0_TX,
		.end = IRQ_UART0_TX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART0_TX,
		.end = CH_UART0_TX+1,
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
		.start = IRQ_UART1_TX,
		.end = IRQ_UART1_TX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART1_TX,
		.end = CH_UART1_TX+1,
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

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
static struct resource musb_resources[] = {
	[0] = {
		.start	= 0xFFCC1000,
		.end	= 0xFFCC1398,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {	/* general IRQ */
		.start	= IRQ_USB_STAT,
		.end	= IRQ_USB_STAT,
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
	.multipoint	= 1,
	.dyn_fifo	= 0,
	.dma		= 1,
	.num_eps	= 16,
	.dma_channels	= 8,
	.clkin          = 48,           /* musb CLKIN in MHZ */
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
#ifdef CONFIG_SERIAL_BFIN_SPORT2_UART
static struct resource bfin_sport2_uart_resources[] = {
	{
		.start = SPORT2_TCR1,
		.end = SPORT2_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT2_RX,
		.end = IRQ_SPORT2_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT2_ERROR,
		.end = IRQ_SPORT2_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport2_peripherals[] = {
	P_SPORT2_TFS, P_SPORT2_DTPRI, P_SPORT2_TSCLK, P_SPORT2_RFS,
	P_SPORT2_DRPRI, P_SPORT2_RSCLK, P_SPORT2_DRSEC, P_SPORT2_DTSEC, 0
};

static struct platform_device bfin_sport2_uart_device = {
	.name = "bfin-sport-uart",
	.id = 2,
	.num_resources = ARRAY_SIZE(bfin_sport2_uart_resources),
	.resource = bfin_sport2_uart_resources,
	.dev = {
		.platform_data = &bfin_sport2_peripherals, /* Passed to driver */
	},
};
#endif
#endif

#if defined(CONFIG_CAN_BFIN) || defined(CONFIG_CAN_BFIN_MODULE)

static unsigned short bfin_can0_peripherals[] = {
	P_CAN0_RX, P_CAN0_TX, 0
};

static struct resource bfin_can0_resources[] = {
	{
		.start = 0xFFC00A00,
		.end = 0xFFC00FFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_CAN0_RX,
		.end = IRQ_CAN0_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_CAN0_TX,
		.end = IRQ_CAN0_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_CAN0_STAT,
		.end = IRQ_CAN0_STAT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_can0_device = {
	.name = "bfin_can",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_can0_resources),
	.resource = bfin_can0_resources,
	.dev = {
		.platform_data = &bfin_can0_peripherals, /* Passed to driver */
	},
};

#endif

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
static struct mtd_partition partition_info[] = {
	{
		.name = "bootloader(nand)",
		.offset = 0,
		.size = 0x80000,
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

static struct bf5xx_nand_platform bfin_nand_platform = {
	.data_width = NFC_NWIDTH_8,
	.partitions = partition_info,
	.nr_partitions = ARRAY_SIZE(partition_info),
	.rd_dly = 3,
	.wr_dly = 3,
};

static struct resource bfin_nand_resources[] = {
	{
		.start = 0xFFC03B00,
		.end = 0xFFC03B4F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = CH_NFC,
		.end = CH_NFC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_nand_device = {
	.name = "bfin-nand",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_nand_resources),
	.resource = bfin_nand_resources,
	.dev = {
		.platform_data = &bfin_nand_platform,
	},
};
#endif

#if defined(CONFIG_SDH_BFIN) || defined(CONFIG_SDH_BFIN_MODULE)

static struct bfin_sd_host bfin_sdh_data = {
	.dma_chan = CH_RSI,
	.irq_int0 = IRQ_RSI_INT0,
	.pin_req = {P_RSI_DATA0, P_RSI_DATA1, P_RSI_DATA2, P_RSI_DATA3, P_RSI_CMD, P_RSI_CLK, 0},
};

static struct platform_device bfin_sdh_device = {
	.name = "bfin-sdh",
	.id = 0,
	.dev = {
		.platform_data = &bfin_sdh_data,
	},
};
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition ezkit_partitions[] = {
	{
		.name       = "bootloader(nor)",
		.size       = 0x80000,
		.offset     = 0,
	}, {
		.name       = "linux kernel(nor)",
		.size       = 0x400000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "file system(nor)",
		.size       = 0x1000000 - 0x80000 - 0x400000,
		.offset     = MTDPART_OFS_APPEND,
	},
};

int bf609_nor_flash_init(struct platform_device *dev)
{
#define CONFIG_SMC_GCTL_VAL     0x00000010
	const unsigned short pins[] = {
		P_A3, P_A4, P_A5, P_A6, P_A7, P_A8, P_A9, P_A10, P_A11, P_A12,
		P_A13, P_A14, P_A15, P_A16, P_A17, P_A18, P_A19, P_A20, P_A21,
		P_A22, P_A23, P_A24, P_A25, P_NORCK, 0,
	};

	peripheral_request_list(pins, "smc0");

	bfin_write32(SMC_GCTL, CONFIG_SMC_GCTL_VAL);
	bfin_write32(SMC_B0CTL, 0x01002011);
	bfin_write32(SMC_B0TIM, 0x08170977);
	bfin_write32(SMC_B0ETIM, 0x00092231);
	return 0;
}

static struct physmap_flash_data ezkit_flash_data = {
	.width      = 2,
	.parts      = ezkit_partitions,
	.init 	    = bf609_nor_flash_init,
	.nr_parts   = ARRAY_SIZE(ezkit_partitions),
#ifdef CONFIG_ROMKERNEL
	.probe_type = "map_rom",
#endif
};

static struct resource ezkit_flash_resource = {
	.start = 0xb0000000,
	.end   = 0xb0ffffff,
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

#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
/* SPI flash chip (w25q32) */
static struct mtd_partition bfin_spi_flash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00080000,
		.offset = 0,
		.mask_flags = MTD_CAP_ROM
	}, {
		.name = "linux kernel(spi)",
		.size = 0x00180000,
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
	.type = "w25q32",
};

static struct bfin6xx_spi_chip spi_flash_chip_info = {
	.enable_dma = true,         /* use dma transfer with this chip*/
};
#endif

#if defined(CONFIG_SPI_SPIDEV) || defined(CONFIG_SPI_SPIDEV_MODULE)
static struct bfin6xx_spi_chip spidev_chip_info = {
	.enable_dma = true,
};
#endif

#if defined(CONFIG_SND_BF6XX_I2S) || defined(CONFIG_SND_BF6XX_I2S_MODULE)
static struct platform_device bfin_i2s_pcm = {
	.name = "bfin-i2s-pcm-audio",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF6XX_SOC_I2S) || \
	defined(CONFIG_SND_BF6XX_SOC_I2S_MODULE)
#include <asm/bfin_sport3.h>
static struct resource bfin_snd_resources[] = {
	{
		.start = SPORT0_CTL_A,
		.end = SPORT0_CTL_A,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = SPORT0_CTL_B,
		.end = SPORT0_CTL_B,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = CH_SPORT0_TX,
		.end = CH_SPORT0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_SPORT0_RX,
		.end = CH_SPORT0_RX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = IRQ_SPORT0_TX_STAT,
		.end = IRQ_SPORT0_TX_STAT,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT0_RX_STAT,
		.end = IRQ_SPORT0_RX_STAT,
		.flags = IORESOURCE_IRQ,
	},
};

static const unsigned short bfin_snd_pin[] = {
	P_SPORT0_ACLK, P_SPORT0_AFS, P_SPORT0_AD0, P_SPORT0_BCLK,
	P_SPORT0_BFS, P_SPORT0_BD0, 0,
};

static struct bfin_snd_platform_data bfin_snd_data = {
	.pin_req = bfin_snd_pin,
};

static struct platform_device bfin_i2s = {
	.name = "bfin-i2s",
	.num_resources = ARRAY_SIZE(bfin_snd_resources),
	.resource = bfin_snd_resources,
	.dev = {
		.platform_data = &bfin_snd_data,
	},
};
#endif

#if defined(CONFIG_SND_SOC_BFIN_EVAL_ADAU1X61) || \
	defined(CONFIG_SND_SOC_BFIN_EVAL_ADAU1X61_MODULE)
static struct platform_device adau1761_device = {
	.name = "bfin-eval-adau1x61",
};
#endif

#if defined(CONFIG_SND_SOC_ADAU1761) || defined(CONFIG_SND_SOC_ADAU1761_MODULE)
#include <sound/adau17x1.h>
static struct adau1761_platform_data adau1761_info = {
	.lineout_mode = ADAU1761_OUTPUT_MODE_LINE,
	.headphone_mode = ADAU1761_OUTPUT_MODE_HEADPHONE_CAPLESS,
};
#endif

#if defined(CONFIG_VIDEO_BLACKFIN_CAPTURE) \
	|| defined(CONFIG_VIDEO_BLACKFIN_CAPTURE_MODULE)
#include <linux/videodev2.h>
#include <media/blackfin/bfin_capture.h>
#include <media/blackfin/ppi.h>

static const unsigned short ppi_req[] = {
	P_PPI0_D0, P_PPI0_D1, P_PPI0_D2, P_PPI0_D3,
	P_PPI0_D4, P_PPI0_D5, P_PPI0_D6, P_PPI0_D7,
	P_PPI0_D8, P_PPI0_D9, P_PPI0_D10, P_PPI0_D11,
	P_PPI0_D12, P_PPI0_D13, P_PPI0_D14, P_PPI0_D15,
	P_PPI0_D16, P_PPI0_D17, P_PPI0_D18, P_PPI0_D19,
	P_PPI0_D20, P_PPI0_D21, P_PPI0_D22, P_PPI0_D23,
	P_PPI0_CLK, P_PPI0_FS1, P_PPI0_FS2,
	0,
};

static const struct ppi_info ppi_info = {
	.type = PPI_TYPE_EPPI3,
	.dma_ch = CH_EPPI0_CH0,
	.irq_err = IRQ_EPPI0_STAT,
	.base = (void __iomem *)EPPI0_STAT,
	.pin_req = ppi_req,
};

#if defined(CONFIG_VIDEO_VS6624) \
	|| defined(CONFIG_VIDEO_VS6624_MODULE)
static struct v4l2_input vs6624_inputs[] = {
	{
		.index = 0,
		.name = "Camera",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_UNKNOWN,
	},
};

static struct bcap_route vs6624_routes[] = {
	{
		.input = 0,
		.output = 0,
	},
};

static const unsigned vs6624_ce_pin = GPIO_PD1;

static struct bfin_capture_config bfin_capture_data = {
	.card_name = "BF609",
	.inputs = vs6624_inputs,
	.num_inputs = ARRAY_SIZE(vs6624_inputs),
	.routes = vs6624_routes,
	.i2c_adapter_id = 0,
	.board_info = {
		.type = "vs6624",
		.addr = 0x10,
		.platform_data = (void *)&vs6624_ce_pin,
	},
	.ppi_info = &ppi_info,
	.ppi_control = (PACK_EN | DLEN_8 | EPPI_CTL_FS1HI_FS2HI
			| EPPI_CTL_POLC3 | EPPI_CTL_SYNC2 | EPPI_CTL_NON656),
	.blank_clocks = 8,
};
#endif

#if defined(CONFIG_VIDEO_ADV7842) \
	|| defined(CONFIG_VIDEO_ADV7842_MODULE)
#include <media/adv7842.h>

static struct v4l2_input adv7842_inputs[] = {
	{
		.index = 0,
		.name = "Composite",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
	},
	{
		.index = 1,
		.name = "S-Video",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
	},
	{
		.index = 2,
		.name = "Component",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
	},
	{
		.index = 3,
		.name = "VGA",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
	},
	{
		.index = 4,
		.name = "HDMI",
		.type = V4L2_INPUT_TYPE_CAMERA,
		.std = V4L2_STD_ALL,
	},
};

static struct bcap_route adv7842_routes[] = {
	{
		.input = 3,
	},
	{
		.input = 4,
	},
	{
		.input = 2,
	},
	{
		.input = 1,
	},
	{
		.input = 0,
	},
};

static struct adv7842_platform_data adv7842_data = {
	.ain_sel = ADV7842_AIN10_11_12_NC_SYNC_4_1,
	.op_ch_sel = ADV7842_OP_CH_SEL_BRG,
	.prim_mode = ADV7842_PRIM_MODE_SDP,
	.vid_std_select = ADV7842_SDP_VID_STD_CVBS_SD_4x1,
	.inp_color_space = ADV7842_INP_COLOR_SPACE_AUTO,
	.op_format_sel = ADV7842_OP_FORMAT_SEL_SDR_ITU656_8,
	.op_656_range = 1,
	.blank_data = 1,
	.insert_av_codes = 1,
	.i2c_sdp_io = 0x30,
	.i2c_sdp = 0x31,
	.i2c_cp = 0x32,
	.i2c_vdp = 0x33,
	.i2c_afe = 0x34,
	.i2c_hdmi = 0x35,
	.i2c_repeater = 0x36,
	.i2c_edid = 0x37,
	.i2c_infoframe = 0x38,
	.i2c_cec = 0x39,
	.i2c_avlink = 0x3a,
};

static struct bfin_capture_config bfin_capture_data = {
	.card_name = "BF609",
	.inputs = adv7842_inputs,
	.num_inputs = ARRAY_SIZE(adv7842_inputs),
	.routes = adv7842_routes,
	.i2c_adapter_id = 0,
	.board_info = {
		.type = "adv7842",
		.addr = 0x20,
		.platform_data = (void *)&adv7842_data,
	},
	.ppi_info = &ppi_info,
	.ppi_control = (PACK_EN | DLEN_8 | EPPI_CTL_FLDSEL
			| EPPI_CTL_ACTIVE656),
};
#endif

static struct platform_device bfin_capture_device = {
	.name = "bfin_capture",
	.dev = {
		.platform_data = &bfin_capture_data,
	},
};
#endif

#if defined(CONFIG_BFIN_CRC)
#define BFIN_CRC_NAME "bfin-crc"

static struct resource bfin_crc0_resources[] = {
	{
		.start = REG_CRC0_CTL,
		.end = REG_CRC0_REVID+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_CRC0_DCNTEXP,
		.end = IRQ_CRC0_DCNTEXP,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_MEM_STREAM0_SRC_CRC0,
		.end = CH_MEM_STREAM0_SRC_CRC0,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_MEM_STREAM0_DEST_CRC0,
		.end = CH_MEM_STREAM0_DEST_CRC0,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_crc0_device = {
	.name = BFIN_CRC_NAME,
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_crc0_resources),
	.resource = bfin_crc0_resources,
};

static struct resource bfin_crc1_resources[] = {
	{
		.start = REG_CRC1_CTL,
		.end = REG_CRC1_REVID+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_CRC1_DCNTEXP,
		.end = IRQ_CRC1_DCNTEXP,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_MEM_STREAM1_SRC_CRC1,
		.end = CH_MEM_STREAM1_SRC_CRC1,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_MEM_STREAM1_DEST_CRC1,
		.end = CH_MEM_STREAM1_DEST_CRC1,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_crc1_device = {
	.name = BFIN_CRC_NAME,
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_crc1_resources),
	.resource = bfin_crc1_resources,
};
#endif

#if defined(CONFIG_CRYPTO_DEV_BFIN_CRC)
#define BFIN_CRYPTO_CRC_NAME		"bfin-hmac-crc"
#define BFIN_CRYPTO_CRC_POLY_DATA	0x5c5c5c5c

static struct resource bfin_crypto_crc_resources[] = {
	{
		.start = REG_CRC0_CTL,
		.end = REG_CRC0_REVID+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_CRC0_DCNTEXP,
		.end = IRQ_CRC0_DCNTEXP,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_MEM_STREAM0_SRC_CRC0,
		.end = CH_MEM_STREAM0_SRC_CRC0,
		.flags = IORESOURCE_DMA,
	},
};

static struct platform_device bfin_crypto_crc_device = {
	.name = BFIN_CRYPTO_CRC_NAME,
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_crypto_crc_resources),
	.resource = bfin_crypto_crc_resources,
	.dev = {
		.platform_data = (void *)BFIN_CRYPTO_CRC_POLY_DATA,
	},
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

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <linux/input.h>
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PB10, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PE1, 1, "gpio-keys: BTN1"},
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

static struct spi_board_info bfin_spi_board_info[] __initdata = {
#if defined(CONFIG_MTD_M25P80) \
	|| defined(CONFIG_MTD_M25P80_MODULE)
	{
		/* the modalias must be the same as spi device driver name */
		.modalias = "m25p80", /* Name of spi_driver for this device */
		.max_speed_hz = 25000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num = 0, /* Framework bus number */
		.chip_select = 1, /* SPI_SSEL1*/
		.platform_data = &bfin_spi_flash_data,
		.controller_data = &spi_flash_chip_info,
		.mode = SPI_MODE_3,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
	{
		.modalias		= "ad7877",
		.platform_data		= &bfin_ad7877_ts_info,
		.irq			= IRQ_PD9,
		.max_speed_hz		= 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num		= 0,
		.chip_select  		= 4,
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
#if defined(CONFIG_INPUT_ADXL34X_SPI) || defined(CONFIG_INPUT_ADXL34X_SPI_MODULE)
	{
		.modalias		= "adxl34x",
		.platform_data		= &adxl34x_info,
		.irq			= IRQ_PC5,
		.max_speed_hz		= 5000000,     /* max spi clock (SCK) speed in HZ */
		.bus_num		= 1,
		.chip_select  		= 2,
		.mode = SPI_MODE_3,
	},
#endif
};
#if defined(CONFIG_SPI_BFIN6XX) || defined(CONFIG_SPI_BFIN6XX_MODULE)
/* SPI (0) */
static struct resource bfin_spi0_resource[] = {
	{
		.start = SPI0_REGBASE,
		.end   = SPI0_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = CH_SPI0_TX,
		.end   = CH_SPI0_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_SPI0_RX,
		.end   = CH_SPI0_RX,
		.flags = IORESOURCE_DMA,
	},
};

/* SPI (1) */
static struct resource bfin_spi1_resource[] = {
	{
		.start = SPI1_REGBASE,
		.end   = SPI1_REGBASE + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = CH_SPI1_TX,
		.end   = CH_SPI1_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_SPI1_RX,
		.end   = CH_SPI1_RX,
		.flags = IORESOURCE_DMA,
	},

};

/* SPI controller data */
static struct bfin6xx_spi_master bf60x_spi_master_info0 = {
	.num_chipselect = MAX_CTRL_CS + MAX_BLACKFIN_GPIOS,
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct platform_device bf60x_spi_master0 = {
	.name = "bfin-spi",
	.id = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bf60x_spi_master_info0, /* Passed to driver */
	},
};

static struct bfin6xx_spi_master bf60x_spi_master_info1 = {
	.num_chipselect = MAX_CTRL_CS + MAX_BLACKFIN_GPIOS,
	.pin_req = {P_SPI1_SCK, P_SPI1_MISO, P_SPI1_MOSI, 0},
};

static struct platform_device bf60x_spi_master1 = {
	.name = "bfin-spi",
	.id = 1, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi1_resource),
	.resource = bfin_spi1_resource,
	.dev = {
		.platform_data = &bf60x_spi_master_info1, /* Passed to driver */
	},
};
#endif  /* spi master and devices */

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
static const u16 bfin_twi0_pins[] = {P_TWI0_SCL, P_TWI0_SDA, 0};

static struct resource bfin_twi0_resource[] = {
	[0] = {
		.start = TWI0_CLKDIV,
		.end   = TWI0_CLKDIV + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TWI0,
		.end   = IRQ_TWI0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_bfin_twi0_device = {
	.name = "i2c-bfin-twi",
	.id = 0,
	.num_resources = ARRAY_SIZE(bfin_twi0_resource),
	.resource = bfin_twi0_resource,
	.dev = {
		.platform_data = &bfin_twi0_pins,
	},
};

static const u16 bfin_twi1_pins[] = {P_TWI1_SCL, P_TWI1_SDA, 0};

static struct resource bfin_twi1_resource[] = {
	[0] = {
		.start = TWI1_CLKDIV,
		.end   = TWI1_CLKDIV + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_TWI1,
		.end   = IRQ_TWI1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c_bfin_twi1_device = {
	.name = "i2c-bfin-twi",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_twi1_resource),
	.resource = bfin_twi1_resource,
	.dev = {
		.platform_data = &bfin_twi1_pins,
	},
};
#endif

static struct i2c_board_info __initdata bfin_i2c_board_info0[] = {
#if defined(CONFIG_INPUT_ADXL34X_I2C) || defined(CONFIG_INPUT_ADXL34X_I2C_MODULE)
	{
		I2C_BOARD_INFO("adxl34x", 0x53),
		.irq = IRQ_PC5,
		.platform_data = (void *)&adxl34x_info,
	},
#endif
#if defined(CONFIG_SND_SOC_ADAU1761) || defined(CONFIG_SND_SOC_ADAU1761_MODULE)
	{
		I2C_BOARD_INFO("adau1761", 0x38),
		.platform_data = (void *)&adau1761_info
	},
#endif
#if defined(CONFIG_SND_SOC_SSM2602) || defined(CONFIG_SND_SOC_SSM2602_MODULE)
	{
		I2C_BOARD_INFO("ssm2602", 0x1b),
	},
#endif
};

static struct i2c_board_info __initdata bfin_i2c_board_info1[] = {
};

static const unsigned int cclk_vlev_datasheet[] =
{
/*
 * Internal VLEV BF54XSBBC1533
 ****temporarily using these values until data sheet is updated
 */
	VRPAIR(VLEV_085, 150000000),
	VRPAIR(VLEV_090, 250000000),
	VRPAIR(VLEV_110, 276000000),
	VRPAIR(VLEV_115, 301000000),
	VRPAIR(VLEV_120, 525000000),
	VRPAIR(VLEV_125, 550000000),
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

static struct platform_device *ezkit_devices[] __initdata = {

	&bfin_dpmc,

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
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

#if defined(CONFIG_STMMAC_ETH) || defined(CONFIG_STMMAC_ETH_MODULE)
	&bfin_eth_device,
#endif

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
	&musb_device,
#endif

#if defined(CONFIG_USB_ISP1760_HCD) || defined(CONFIG_USB_ISP1760_HCD_MODULE)
	&bfin_isp1760_device,
#endif

#if defined(CONFIG_SERIAL_BFIN_SPORT) || defined(CONFIG_SERIAL_BFIN_SPORT_MODULE)
#ifdef CONFIG_SERIAL_BFIN_SPORT0_UART
	&bfin_sport0_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT1_UART
	&bfin_sport1_uart_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_SPORT2_UART
	&bfin_sport2_uart_device,
#endif
#endif

#if defined(CONFIG_CAN_BFIN) || defined(CONFIG_CAN_BFIN_MODULE)
	&bfin_can0_device,
#endif

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
	&bfin_nand_device,
#endif

#if defined(CONFIG_SDH_BFIN) || defined(CONFIG_SDH_BFIN_MODULE)
	&bfin_sdh_device,
#endif

#if defined(CONFIG_SPI_BFIN6XX) || defined(CONFIG_SPI_BFIN6XX_MODULE)
	&bf60x_spi_master0,
	&bf60x_spi_master1,
#endif

#if defined(CONFIG_INPUT_BFIN_ROTARY) || defined(CONFIG_INPUT_BFIN_ROTARY_MODULE)
	&bfin_rotary_device,
#endif

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
	&i2c_bfin_twi0_device,
#if !defined(CONFIG_BF542)
	&i2c_bfin_twi1_device,
#endif
#endif

#if defined(CONFIG_BFIN_CRC)
	&bfin_crc0_device,
	&bfin_crc1_device,
#endif
#if defined(CONFIG_CRYPTO_DEV_BFIN_CRC)
	&bfin_crypto_crc_device,
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ezkit_flash_device,
#endif
#if defined(CONFIG_SND_BF6XX_I2S) || defined(CONFIG_SND_BF6XX_I2S_MODULE)
	&bfin_i2s_pcm,
#endif
#if defined(CONFIG_SND_BF6XX_SOC_I2S) || \
	defined(CONFIG_SND_BF6XX_SOC_I2S_MODULE)
	&bfin_i2s,
#endif
#if defined(CONFIG_SND_SOC_BFIN_EVAL_ADAU1X61) || \
	defined(CONFIG_SND_SOC_BFIN_EVAL_ADAU1X61_MODULE)
	&adau1761_device,
#endif
#if defined(CONFIG_VIDEO_BLACKFIN_CAPTURE) \
	|| defined(CONFIG_VIDEO_BLACKFIN_CAPTURE_MODULE)
	&bfin_capture_device,
#endif
};

static int __init ezkit_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	i2c_register_board_info(0, bfin_i2c_board_info0,
				ARRAY_SIZE(bfin_i2c_board_info0));
	i2c_register_board_info(1, bfin_i2c_board_info1,
				ARRAY_SIZE(bfin_i2c_board_info1));

#if defined(CONFIG_STMMAC_ETH) || defined(CONFIG_STMMAC_ETH_MODULE)
	if (!peripheral_request_list(pins, "emac0"))
		printk(KERN_ERR "%s(): request emac pins failed\n", __func__);
#endif

	platform_add_devices(ezkit_devices, ARRAY_SIZE(ezkit_devices));

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
#ifdef CONFIG_SERIAL_BFIN_SPORT2_UART
	&bfin_sport2_uart_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(ezkit_early_devices,
		ARRAY_SIZE(ezkit_early_devices));
}
