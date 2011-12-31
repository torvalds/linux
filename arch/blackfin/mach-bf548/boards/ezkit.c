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
#include <asm/bfin5xx_spi.h>
#include <asm/dma.h>
#include <asm/gpio.h>
#include <asm/nand.h>
#include <asm/dpmc.h>
#include <asm/bfin_sport.h>
#include <asm/portmux.h>
#include <asm/bfin_sdh.h>
#include <mach/bf54x_keys.h>
#include <linux/input.h>
#include <linux/spi/ad7877.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
const char bfin_board_name[] = "ADI BF548-EZKIT";

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

#if defined(CONFIG_FB_BF54X_LQ043) || defined(CONFIG_FB_BF54X_LQ043_MODULE)

#include <mach/bf54x-lq043.h>

static struct bfin_bf54xfb_mach_info bf54x_lq043_data = {
	.width =	95,
	.height =	54,
	.xres =		{480, 480, 480},
	.yres =		{272, 272, 272},
	.bpp =		{24, 24, 24},
	.disp =		GPIO_PE3,
};

static struct resource bf54x_lq043_resources[] = {
	{
		.start = IRQ_EPPI0_ERR,
		.end = IRQ_EPPI0_ERR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bf54x_lq043_device = {
	.name		= "bf54x-lq043",
	.id		= -1,
	.num_resources 	= ARRAY_SIZE(bf54x_lq043_resources),
	.resource 	= bf54x_lq043_resources,
	.dev		= {
		.platform_data = &bf54x_lq043_data,
	},
};
#endif

#if defined(CONFIG_KEYBOARD_BFIN) || defined(CONFIG_KEYBOARD_BFIN_MODULE)
static const unsigned int bf548_keymap[] = {
	KEYVAL(0, 0, KEY_ENTER),
	KEYVAL(0, 1, KEY_HELP),
	KEYVAL(0, 2, KEY_0),
	KEYVAL(0, 3, KEY_BACKSPACE),
	KEYVAL(1, 0, KEY_TAB),
	KEYVAL(1, 1, KEY_9),
	KEYVAL(1, 2, KEY_8),
	KEYVAL(1, 3, KEY_7),
	KEYVAL(2, 0, KEY_DOWN),
	KEYVAL(2, 1, KEY_6),
	KEYVAL(2, 2, KEY_5),
	KEYVAL(2, 3, KEY_4),
	KEYVAL(3, 0, KEY_UP),
	KEYVAL(3, 1, KEY_3),
	KEYVAL(3, 2, KEY_2),
	KEYVAL(3, 3, KEY_1),
};

static struct bfin_kpad_platform_data bf54x_kpad_data = {
	.rows			= 4,
	.cols			= 4,
	.keymap			= bf548_keymap,
	.keymapsize		= ARRAY_SIZE(bf548_keymap),
	.repeat			= 0,
	.debounce_time		= 5000,	/* ns (5ms) */
	.coldrive_time		= 1000, /* ns (1ms) */
	.keyup_test_interval	= 50, /* ms (50ms) */
};

static struct resource bf54x_kpad_resources[] = {
	{
		.start = IRQ_KEY,
		.end = IRQ_KEY,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bf54x_kpad_device = {
	.name		= "bf54x-keys",
	.id		= -1,
	.num_resources 	= ARRAY_SIZE(bf54x_kpad_resources),
	.resource 	= bf54x_kpad_resources,
	.dev		= {
		.platform_data = &bf54x_kpad_data,
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
		.start = UART0_DLL,
		.end = UART0_RBR+2,
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
		.start = UART1_DLL,
		.end = UART1_RBR+2,
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
	{	/* CTS pin -- 0 means not supported */
		.start = GPIO_PE10,
		.end = GPIO_PE10,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin -- 0 means not supported */
		.start = GPIO_PE9,
		.end = GPIO_PE9,
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
#ifdef CONFIG_SERIAL_BFIN_UART2
static struct resource bfin_uart2_resources[] = {
	{
		.start = UART2_DLL,
		.end = UART2_RBR+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART2_TX,
		.end = IRQ_UART2_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART2_RX,
		.end = IRQ_UART2_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART2_ERROR,
		.end = IRQ_UART2_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART2_TX,
		.end = CH_UART2_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART2_RX,
		.end = CH_UART2_RX,
		.flags = IORESOURCE_DMA,
	},
};

static unsigned short bfin_uart2_peripherals[] = {
	P_UART2_TX, P_UART2_RX, 0
};

static struct platform_device bfin_uart2_device = {
	.name = "bfin-uart",
	.id = 2,
	.num_resources = ARRAY_SIZE(bfin_uart2_resources),
	.resource = bfin_uart2_resources,
	.dev = {
		.platform_data = &bfin_uart2_peripherals, /* Passed to driver */
	},
};
#endif
#ifdef CONFIG_SERIAL_BFIN_UART3
static struct resource bfin_uart3_resources[] = {
	{
		.start = UART3_DLL,
		.end = UART3_RBR+2,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART3_TX,
		.end = IRQ_UART3_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART3_RX,
		.end = IRQ_UART3_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_UART3_ERROR,
		.end = IRQ_UART3_ERROR,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART3_TX,
		.end = CH_UART3_TX,
		.flags = IORESOURCE_DMA,
	},
	{
		.start = CH_UART3_RX,
		.end = CH_UART3_RX,
		.flags = IORESOURCE_DMA,
	},
#ifdef CONFIG_BFIN_UART3_CTSRTS
	{	/* CTS pin -- 0 means not supported */
		.start = GPIO_PB3,
		.end = GPIO_PB3,
		.flags = IORESOURCE_IO,
	},
	{	/* RTS pin -- 0 means not supported */
		.start = GPIO_PB2,
		.end = GPIO_PB2,
		.flags = IORESOURCE_IO,
	},
#endif
};

static unsigned short bfin_uart3_peripherals[] = {
	P_UART3_TX, P_UART3_RX,
#ifdef CONFIG_BFIN_UART3_CTSRTS
	P_UART3_RTS, P_UART3_CTS,
#endif
	0
};

static struct platform_device bfin_uart3_device = {
	.name = "bfin-uart",
	.id = 3,
	.num_resources = ARRAY_SIZE(bfin_uart3_resources),
	.resource = bfin_uart3_resources,
	.dev = {
		.platform_data = &bfin_uart3_peripherals, /* Passed to driver */
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
#ifdef CONFIG_BFIN_SIR2
static struct resource bfin_sir2_resources[] = {
	{
		.start = 0xFFC02100,
		.end = 0xFFC021FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART2_RX,
		.end = IRQ_UART2_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART2_RX,
		.end = CH_UART2_RX+1,
		.flags = IORESOURCE_DMA,
	},
};
static struct platform_device bfin_sir2_device = {
	.name = "bfin_sir",
	.id = 2,
	.num_resources = ARRAY_SIZE(bfin_sir2_resources),
	.resource = bfin_sir2_resources,
};
#endif
#ifdef CONFIG_BFIN_SIR3
static struct resource bfin_sir3_resources[] = {
	{
		.start = 0xFFC03100,
		.end = 0xFFC031FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_UART3_RX,
		.end = IRQ_UART3_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = CH_UART3_RX,
		.end = CH_UART3_RX+1,
		.flags = IORESOURCE_DMA,
	},
};
static struct platform_device bfin_sir3_device = {
	.name = "bfin_sir",
	.id = 3,
	.num_resources = ARRAY_SIZE(bfin_sir3_resources),
	.resource = bfin_sir3_resources,
};
#endif
#endif

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
#include <linux/smsc911x.h>

static struct resource smsc911x_resources[] = {
	{
		.name = "smsc911x-memory",
		.start = 0x24000000,
		.end = 0x24000000 + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_PE8,
		.end = IRQ_PE8,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_config = {
	.flags = SMSC911X_USE_32BIT,
	.irq_polarity = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type = SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.phy_interface = PHY_INTERFACE_MODE_MII,
};

static struct platform_device smsc911x_device = {
	.name = "smsc911x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smsc911x_resources),
	.resource = smsc911x_resources,
	.dev = {
		.platform_data = &smsc911x_config,
	},
};
#endif

#if defined(CONFIG_USB_MUSB_HDRC) || defined(CONFIG_USB_MUSB_HDRC_MODULE)
static struct resource musb_resources[] = {
	[0] = {
		.start	= 0xFFC03C00,
		.end	= 0xFFC040FF,
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
	.gpio_vrsel	= GPIO_PE7,
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
#ifdef CONFIG_SERIAL_BFIN_SPORT3_UART
static struct resource bfin_sport3_uart_resources[] = {
	{
		.start = SPORT3_TCR1,
		.end = SPORT3_MRCS3+4,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_SPORT3_RX,
		.end = IRQ_SPORT3_RX+1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_SPORT3_ERROR,
		.end = IRQ_SPORT3_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static unsigned short bfin_sport3_peripherals[] = {
	P_SPORT3_TFS, P_SPORT3_DTPRI, P_SPORT3_TSCLK, P_SPORT3_RFS,
	P_SPORT3_DRPRI, P_SPORT3_RSCLK, P_SPORT3_DRSEC, P_SPORT3_DTSEC, 0
};

static struct platform_device bfin_sport3_uart_device = {
	.name = "bfin-sport-uart",
	.id = 3,
	.num_resources = ARRAY_SIZE(bfin_sport3_uart_resources),
	.resource = bfin_sport3_uart_resources,
	.dev = {
		.platform_data = &bfin_sport3_peripherals, /* Passed to driver */
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
		.start = 0xFFC02A00,
		.end = 0xFFC02FFF,
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
		.start = IRQ_CAN0_ERROR,
		.end = IRQ_CAN0_ERROR,
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

static unsigned short bfin_can1_peripherals[] = {
	P_CAN1_RX, P_CAN1_TX, 0
};

static struct resource bfin_can1_resources[] = {
	{
		.start = 0xFFC03200,
		.end = 0xFFC037FF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_CAN1_RX,
		.end = IRQ_CAN1_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_CAN1_TX,
		.end = IRQ_CAN1_TX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_CAN1_ERROR,
		.end = IRQ_CAN1_ERROR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_can1_device = {
	.name = "bfin_can",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_can1_resources),
	.resource = bfin_can1_resources,
	.dev = {
		.platform_data = &bfin_can1_peripherals, /* Passed to driver */
	},
};

#endif

#if defined(CONFIG_PATA_BF54X) || defined(CONFIG_PATA_BF54X_MODULE)
static struct resource bfin_atapi_resources[] = {
	{
		.start = 0xFFC03800,
		.end = 0xFFC0386F,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_ATAPI_ERR,
		.end = IRQ_ATAPI_ERR,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device bfin_atapi_device = {
	.name = "pata-bf54x",
	.id = -1,
	.num_resources = ARRAY_SIZE(bfin_atapi_resources),
	.resource = bfin_atapi_resources,
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

static struct bf5xx_nand_platform bf5xx_nand_platform = {
	.data_width = NFC_NWIDTH_8,
	.partitions = partition_info,
	.nr_partitions = ARRAY_SIZE(partition_info),
	.rd_dly = 3,
	.wr_dly = 3,
};

static struct resource bf5xx_nand_resources[] = {
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

#if defined(CONFIG_SDH_BFIN) || defined(CONFIG_SDH_BFIN_MODULE)

static struct bfin_sd_host bfin_sdh_data = {
	.dma_chan = CH_SDH,
	.irq_int0 = IRQ_SDH_MASK0,
	.pin_req = {P_SD_D0, P_SD_D1, P_SD_D2, P_SD_D3, P_SD_CLK, P_SD_CMD, 0},
};

static struct platform_device bf54x_sdh_device = {
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
		.size       = 0x1000000 - 0x80000 - 0x400000 - 0x8000 * 4,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "config(nor)",
		.size       = 0x8000 * 3,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "u-boot env(nor)",
		.size       = 0x8000,
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
	.end   = 0x21ffffff,
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
/* SPI flash chip (m25p16) */
static struct mtd_partition bfin_spi_flash_partitions[] = {
	{
		.name = "bootloader(spi)",
		.size = 0x00080000,
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

static struct bfin5xx_spi_chip spi_flash_chip_info = {
	.enable_dma = 0,         /* use dma transfer with this chip*/
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
		.chip_select = 1, /* SPI_SSEL1*/
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
		.bus_num = 1,
		.chip_select = 4,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_AD7877) || defined(CONFIG_TOUCHSCREEN_AD7877_MODULE)
	{
		.modalias		= "ad7877",
		.platform_data		= &bfin_ad7877_ts_info,
		.irq			= IRQ_PB4,	/* old boards (<=Rev 1.3) use IRQ_PJ11 */
		.max_speed_hz		= 12500000,     /* max spi clock (SCK) speed in HZ */
		.bus_num		= 0,
		.chip_select  		= 2,
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
#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
/* SPI (0) */
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
	}
};

/* SPI (1) */
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
	}
};

/* SPI controller data */
static struct bfin5xx_spi_master bf54x_spi_master_info0 = {
	.num_chipselect = 4,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI0_SCK, P_SPI0_MISO, P_SPI0_MOSI, 0},
};

static struct platform_device bf54x_spi_master0 = {
	.name = "bfin-spi",
	.id = 0, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi0_resource),
	.resource = bfin_spi0_resource,
	.dev = {
		.platform_data = &bf54x_spi_master_info0, /* Passed to driver */
		},
};

static struct bfin5xx_spi_master bf54x_spi_master_info1 = {
	.num_chipselect = 4,
	.enable_dma = 1,  /* master has the ability to do dma transfer */
	.pin_req = {P_SPI1_SCK, P_SPI1_MISO, P_SPI1_MOSI, 0},
};

static struct platform_device bf54x_spi_master1 = {
	.name = "bfin-spi",
	.id = 1, /* Bus number */
	.num_resources = ARRAY_SIZE(bfin_spi1_resource),
	.resource = bfin_spi1_resource,
	.dev = {
		.platform_data = &bf54x_spi_master_info1, /* Passed to driver */
		},
};
#endif  /* spi master and devices */

#if defined(CONFIG_VIDEO_BLACKFIN_CAPTURE) \
	|| defined(CONFIG_VIDEO_BLACKFIN_CAPTURE_MODULE)
#include <linux/videodev2.h>
#include <media/blackfin/bfin_capture.h>
#include <media/blackfin/ppi.h>

static const unsigned short ppi_req[] = {
	P_PPI1_D0, P_PPI1_D1, P_PPI1_D2, P_PPI1_D3,
	P_PPI1_D4, P_PPI1_D5, P_PPI1_D6, P_PPI1_D7,
	P_PPI1_CLK, P_PPI1_FS1, P_PPI1_FS2,
	0,
};

static const struct ppi_info ppi_info = {
	.type = PPI_TYPE_EPPI,
	.dma_ch = CH_EPPI1,
	.irq_err = IRQ_EPPI1_ERROR,
	.base = (void __iomem *)EPPI1_STATUS,
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

static const unsigned vs6624_ce_pin = GPIO_PG6;

static struct bfin_capture_config bfin_capture_data = {
	.card_name = "BF548",
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
	.ppi_control = (POLC | PACKEN | DLEN_8 | XFR_TYPE | 0x20),
};
#endif

static struct platform_device bfin_capture_device = {
	.name = "bfin_capture",
	.dev = {
		.platform_data = &bfin_capture_data,
	},
};
#endif

#if defined(CONFIG_I2C_BLACKFIN_TWI) || defined(CONFIG_I2C_BLACKFIN_TWI_MODULE)
static struct resource bfin_twi0_resource[] = {
	[0] = {
		.start = TWI0_REGBASE,
		.end   = TWI0_REGBASE + 0xFF,
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
};

#if !defined(CONFIG_BF542)	/* The BF542 only has 1 TWI */
static struct resource bfin_twi1_resource[] = {
	[0] = {
		.start = TWI1_REGBASE,
		.end   = TWI1_REGBASE + 0xFF,
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
};
#endif
#endif

static struct i2c_board_info __initdata bfin_i2c_board_info0[] = {
};

#if !defined(CONFIG_BF542)	/* The BF542 only has 1 TWI */
static struct i2c_board_info __initdata bfin_i2c_board_info1[] = {
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_BFIN_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_lcd", 0x22),
	},
#endif
#if defined(CONFIG_INPUT_PCF8574) || defined(CONFIG_INPUT_PCF8574_MODULE)
	{
		I2C_BOARD_INFO("pcf8574_keypad", 0x27),
		.irq = 212,
	},
#endif
#if defined(CONFIG_INPUT_ADXL34X_I2C) || defined(CONFIG_INPUT_ADXL34X_I2C_MODULE)
	{
		I2C_BOARD_INFO("adxl34x", 0x53),
		.irq = IRQ_PC5,
		.platform_data = (void *)&adxl34x_info,
	},
#endif
#if defined(CONFIG_BFIN_TWI_LCD) || defined(CONFIG_BFIN_TWI_LCD_MODULE)
	{
		I2C_BOARD_INFO("ad5252", 0x2f),
	},
#endif
};
#endif

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
#include <linux/gpio_keys.h>

static struct gpio_keys_button bfin_gpio_keys_table[] = {
	{BTN_0, GPIO_PB8, 1, "gpio-keys: BTN0"},
	{BTN_1, GPIO_PB9, 1, "gpio-keys: BTN1"},
	{BTN_2, GPIO_PB10, 1, "gpio-keys: BTN2"},
	{BTN_3, GPIO_PB11, 1, "gpio-keys: BTN3"},
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

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE) || \
	defined(CONFIG_SND_BF5XX_TDM) || defined(CONFIG_SND_BF5XX_TDM_MODULE) || \
	defined(CONFIG_SND_BF5XX_AC97) || defined(CONFIG_SND_BF5XX_AC97_MODULE)

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

static struct platform_device bfin_pcm = {
	.name = "bfin-pcm-audio",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD73311) || defined(CONFIG_SND_BF5XX_SOC_AD73311_MODULE)
static struct platform_device bfin_ad73311_codec_device = {
	.name = "ad73311",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD1980) || defined(CONFIG_SND_BF5XX_SOC_AD1980_MODULE)
static struct platform_device bfin_ad1980_codec_device = {
	.name = "ad1980",
	.id = -1,
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_I2S) || defined(CONFIG_SND_BF5XX_SOC_I2S_MODULE)
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

#if defined(CONFIG_SND_BF5XX_SOC_TDM) || defined(CONFIG_SND_BF5XX_SOC_TDM_MODULE)
static struct platform_device bfin_tdm = {
	.name = "bfin-tdm",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources = ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
};
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AC97) || defined(CONFIG_SND_BF5XX_SOC_AC97_MODULE)
static struct platform_device bfin_ac97 = {
	.name = "bfin-ac97",
	.id = CONFIG_SND_BF5XX_SPORT_NUM,
	.num_resources = ARRAY_SIZE(bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM]),
	.resource = bfin_snd_resources[CONFIG_SND_BF5XX_SPORT_NUM],
	.dev = {
		.platform_data = &bfin_snd_data[CONFIG_SND_BF5XX_SPORT_NUM],
	},
};
#endif

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
#ifdef CONFIG_SERIAL_BFIN_UART2
	&bfin_uart2_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART3
	&bfin_uart3_device,
#endif
#endif

#if defined(CONFIG_BFIN_SIR) || defined(CONFIG_BFIN_SIR_MODULE)
#ifdef CONFIG_BFIN_SIR0
	&bfin_sir0_device,
#endif
#ifdef CONFIG_BFIN_SIR1
	&bfin_sir1_device,
#endif
#ifdef CONFIG_BFIN_SIR2
	&bfin_sir2_device,
#endif
#ifdef CONFIG_BFIN_SIR3
	&bfin_sir3_device,
#endif
#endif

#if defined(CONFIG_FB_BF54X_LQ043) || defined(CONFIG_FB_BF54X_LQ043_MODULE)
	&bf54x_lq043_device,
#endif

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
	&smsc911x_device,
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
#ifdef CONFIG_SERIAL_BFIN_SPORT3_UART
	&bfin_sport3_uart_device,
#endif
#endif

#if defined(CONFIG_CAN_BFIN) || defined(CONFIG_CAN_BFIN_MODULE)
	&bfin_can0_device,
	&bfin_can1_device,
#endif

#if defined(CONFIG_PATA_BF54X) || defined(CONFIG_PATA_BF54X_MODULE)
	&bfin_atapi_device,
#endif

#if defined(CONFIG_MTD_NAND_BF5XX) || defined(CONFIG_MTD_NAND_BF5XX_MODULE)
	&bf5xx_nand_device,
#endif

#if defined(CONFIG_SDH_BFIN) || defined(CONFIG_SDH_BFIN_MODULE)
	&bf54x_sdh_device,
#endif

#if defined(CONFIG_SPI_BFIN5XX) || defined(CONFIG_SPI_BFIN5XX_MODULE)
	&bf54x_spi_master0,
	&bf54x_spi_master1,
#endif
#if defined(CONFIG_VIDEO_BLACKFIN_CAPTURE) \
	|| defined(CONFIG_VIDEO_BLACKFIN_CAPTURE_MODULE)
	&bfin_capture_device,
#endif

#if defined(CONFIG_KEYBOARD_BFIN) || defined(CONFIG_KEYBOARD_BFIN_MODULE)
	&bf54x_kpad_device,
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

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
	&bfin_device_gpiokeys,
#endif

#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
	&ezkit_flash_device,
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE) || \
	defined(CONFIG_SND_BF5XX_TDM) || defined(CONFIG_SND_BF5XX_TDM_MODULE) || \
	defined(CONFIG_SND_BF5XX_AC97) || defined(CONFIG_SND_BF5XX_AC97_MODULE)
	&bfin_pcm,
#endif

#if defined(CONFIG_SND_BF5XX_SOC_AD1980) || defined(CONFIG_SND_BF5XX_SOC_AD1980_MODULE)
	&bfin_ad1980_codec_device,
#endif

#if defined(CONFIG_SND_BF5XX_I2S) || defined(CONFIG_SND_BF5XX_I2S_MODULE)
	&bfin_i2s,
#endif

#if defined(CONFIG_SND_BF5XX_TDM) || defined(CONFIG_SND_BF5XX_TDM_MODULE)
	&bfin_tdm,
#endif

#if defined(CONFIG_SND_BF5XX_AC97) || defined(CONFIG_SND_BF5XX_AC97_MODULE)
	&bfin_ac97,
#endif
};

static int __init ezkit_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __func__);

	i2c_register_board_info(0, bfin_i2c_board_info0,
				ARRAY_SIZE(bfin_i2c_board_info0));
#if !defined(CONFIG_BF542)	/* The BF542 only has 1 TWI */
	i2c_register_board_info(1, bfin_i2c_board_info1,
				ARRAY_SIZE(bfin_i2c_board_info1));
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
#ifdef CONFIG_SERIAL_BFIN_UART2
	&bfin_uart2_device,
#endif
#ifdef CONFIG_SERIAL_BFIN_UART3
	&bfin_uart3_device,
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
#ifdef CONFIG_SERIAL_BFIN_SPORT3_UART
	&bfin_sport3_uart_device,
#endif
#endif
};

void __init native_machine_early_platform_add_devices(void)
{
	printk(KERN_INFO "register early platform devices\n");
	early_platform_add_devices(ezkit_early_devices,
		ARRAY_SIZE(ezkit_early_devices));
}
