/*
 * TI DaVinci DM355 chip specific setup
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/clk-provider.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/init.h>
#include <linux/platform_data/edma.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/spi-davinci.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/spi/spi.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/cputype.h>
#include <mach/irqs.h>
#include <mach/mux.h>
#include <mach/serial.h>
#include <mach/time.h>

#include "asp.h"
#include "davinci.h"
#include "mux.h"

#define DM355_UART2_BASE	(IO_PHYS + 0x206000)
#define DM355_OSD_BASE		(IO_PHYS + 0x70200)
#define DM355_VENC_BASE		(IO_PHYS + 0x70400)

/*
 * Device specific clocks
 */
#define DM355_REF_FREQ		24000000	/* 24 or 36 MHz */

static u64 dm355_spi0_dma_mask = DMA_BIT_MASK(32);

static struct resource dm355_spi0_resources[] = {
	{
		.start = 0x01c66000,
		.end   = 0x01c667ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_DM355_SPINT0_0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct davinci_spi_platform_data dm355_spi0_pdata = {
	.version 	= SPI_VERSION_1,
	.num_chipselect = 2,
	.cshold_bug	= true,
	.dma_event_q	= EVENTQ_1,
	.prescaler_limit = 1,
};
static struct platform_device dm355_spi0_device = {
	.name = "spi_davinci",
	.id = 0,
	.dev = {
		.dma_mask = &dm355_spi0_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &dm355_spi0_pdata,
	},
	.num_resources = ARRAY_SIZE(dm355_spi0_resources),
	.resource = dm355_spi0_resources,
};

void __init dm355_init_spi0(unsigned chipselect_mask,
		const struct spi_board_info *info, unsigned len)
{
	/* for now, assume we need MISO */
	davinci_cfg_reg(DM355_SPI0_SDI);

	/* not all slaves will be wired up */
	if (chipselect_mask & BIT(0))
		davinci_cfg_reg(DM355_SPI0_SDENA0);
	if (chipselect_mask & BIT(1))
		davinci_cfg_reg(DM355_SPI0_SDENA1);

	spi_register_board_info(info, len);

	platform_device_register(&dm355_spi0_device);
}

/*----------------------------------------------------------------------*/

#define INTMUX		0x18
#define EVTMUX		0x1c

/*
 * Device specific mux setup
 *
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config dm355_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
MUX_CFG(DM355,	MMCSD0,		4,   2,     1,	  0,	 false)

MUX_CFG(DM355,	SD1_CLK,	3,   6,     1,	  1,	 false)
MUX_CFG(DM355,	SD1_CMD,	3,   7,     1,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA3,	3,   8,     3,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA2,	3,   10,    3,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA1,	3,   12,    3,	  1,	 false)
MUX_CFG(DM355,	SD1_DATA0,	3,   14,    3,	  1,	 false)

MUX_CFG(DM355,	I2C_SDA,	3,   19,    1,	  1,	 false)
MUX_CFG(DM355,	I2C_SCL,	3,   20,    1,	  1,	 false)

MUX_CFG(DM355,	MCBSP0_BDX,	3,   0,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_X,	3,   1,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_BFSX,	3,   2,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_BDR,	3,   3,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_R,	3,   4,     1,	  1,	 false)
MUX_CFG(DM355,	MCBSP0_BFSR,	3,   5,     1,	  1,	 false)

MUX_CFG(DM355,	SPI0_SDI,	4,   1,     1,    0,	 false)
MUX_CFG(DM355,	SPI0_SDENA0,	4,   0,     1,    0,	 false)
MUX_CFG(DM355,	SPI0_SDENA1,	3,   28,    1,    1,	 false)

INT_CFG(DM355,  INT_EDMA_CC,	      2,    1,    1,     false)
INT_CFG(DM355,  INT_EDMA_TC0_ERR,     3,    1,    1,     false)
INT_CFG(DM355,  INT_EDMA_TC1_ERR,     4,    1,    1,     false)

EVT_CFG(DM355,  EVT8_ASP1_TX,	      0,    1,    0,     false)
EVT_CFG(DM355,  EVT9_ASP1_RX,	      1,    1,    0,     false)
EVT_CFG(DM355,  EVT26_MMC0_RX,	      2,    1,    0,     false)

MUX_CFG(DM355,	VOUT_FIELD,	1,   18,    3,	  1,	 false)
MUX_CFG(DM355,	VOUT_FIELD_G70,	1,   18,    3,	  0,	 false)
MUX_CFG(DM355,	VOUT_HVSYNC,	1,   16,    1,	  0,	 false)
MUX_CFG(DM355,	VOUT_COUTL_EN,	1,   0,     0xff, 0x55,  false)
MUX_CFG(DM355,	VOUT_COUTH_EN,	1,   8,     0xff, 0x55,  false)

MUX_CFG(DM355,	VIN_PCLK,	0,   14,    1,    1,	 false)
MUX_CFG(DM355,	VIN_CAM_WEN,	0,   13,    1,    1,	 false)
MUX_CFG(DM355,	VIN_CAM_VD,	0,   12,    1,    1,	 false)
MUX_CFG(DM355,	VIN_CAM_HD,	0,   11,    1,    1,	 false)
MUX_CFG(DM355,	VIN_YIN_EN,	0,   10,    1,    1,	 false)
MUX_CFG(DM355,	VIN_CINL_EN,	0,   0,   0xff, 0x55,	 false)
MUX_CFG(DM355,	VIN_CINH_EN,	0,   8,     3,    3,	 false)
#endif
};

static u8 dm355_default_priorities[DAVINCI_N_AINTC_IRQ] = {
	[IRQ_DM355_CCDC_VDINT0]		= 2,
	[IRQ_DM355_CCDC_VDINT1]		= 6,
	[IRQ_DM355_CCDC_VDINT2]		= 6,
	[IRQ_DM355_IPIPE_HST]		= 6,
	[IRQ_DM355_H3AINT]		= 6,
	[IRQ_DM355_IPIPE_SDR]		= 6,
	[IRQ_DM355_IPIPEIFINT]		= 6,
	[IRQ_DM355_OSDINT]		= 7,
	[IRQ_DM355_VENCINT]		= 6,
	[IRQ_ASQINT]			= 6,
	[IRQ_IMXINT]			= 6,
	[IRQ_USBINT]			= 4,
	[IRQ_DM355_RTOINT]		= 4,
	[IRQ_DM355_UARTINT2]		= 7,
	[IRQ_DM355_TINT6]		= 7,
	[IRQ_CCINT0]			= 5,	/* dma */
	[IRQ_CCERRINT]			= 5,	/* dma */
	[IRQ_TCERRINT0]			= 5,	/* dma */
	[IRQ_TCERRINT]			= 5,	/* dma */
	[IRQ_DM355_SPINT2_1]		= 7,
	[IRQ_DM355_TINT7]		= 4,
	[IRQ_DM355_SDIOINT0]		= 7,
	[IRQ_MBXINT]			= 7,
	[IRQ_MBRINT]			= 7,
	[IRQ_MMCINT]			= 7,
	[IRQ_DM355_MMCINT1]		= 7,
	[IRQ_DM355_PWMINT3]		= 7,
	[IRQ_DDRINT]			= 7,
	[IRQ_AEMIFINT]			= 7,
	[IRQ_DM355_SDIOINT1]		= 4,
	[IRQ_TINT0_TINT12]		= 2,	/* clockevent */
	[IRQ_TINT0_TINT34]		= 2,	/* clocksource */
	[IRQ_TINT1_TINT12]		= 7,	/* DSP timer */
	[IRQ_TINT1_TINT34]		= 7,	/* system tick */
	[IRQ_PWMINT0]			= 7,
	[IRQ_PWMINT1]			= 7,
	[IRQ_PWMINT2]			= 7,
	[IRQ_I2C]			= 3,
	[IRQ_UARTINT0]			= 3,
	[IRQ_UARTINT1]			= 3,
	[IRQ_DM355_SPINT0_0]		= 3,
	[IRQ_DM355_SPINT0_1]		= 3,
	[IRQ_DM355_GPIO0]		= 3,
	[IRQ_DM355_GPIO1]		= 7,
	[IRQ_DM355_GPIO2]		= 4,
	[IRQ_DM355_GPIO3]		= 4,
	[IRQ_DM355_GPIO4]		= 7,
	[IRQ_DM355_GPIO5]		= 7,
	[IRQ_DM355_GPIO6]		= 7,
	[IRQ_DM355_GPIO7]		= 7,
	[IRQ_DM355_GPIO8]		= 7,
	[IRQ_DM355_GPIO9]		= 7,
	[IRQ_DM355_GPIOBNK0]		= 7,
	[IRQ_DM355_GPIOBNK1]		= 7,
	[IRQ_DM355_GPIOBNK2]		= 7,
	[IRQ_DM355_GPIOBNK3]		= 7,
	[IRQ_DM355_GPIOBNK4]		= 7,
	[IRQ_DM355_GPIOBNK5]		= 7,
	[IRQ_DM355_GPIOBNK6]		= 7,
	[IRQ_COMMTX]			= 7,
	[IRQ_COMMRX]			= 7,
	[IRQ_EMUINT]			= 7,
};

/*----------------------------------------------------------------------*/

static s8 queue_priority_mapping[][2] = {
	/* {event queue no, Priority} */
	{0, 3},
	{1, 7},
	{-1, -1},
};

static const struct dma_slave_map dm355_edma_map[] = {
	{ "davinci-mcbsp.0", "tx", EDMA_FILTER_PARAM(0, 2) },
	{ "davinci-mcbsp.0", "rx", EDMA_FILTER_PARAM(0, 3) },
	{ "davinci-mcbsp.1", "tx", EDMA_FILTER_PARAM(0, 8) },
	{ "davinci-mcbsp.1", "rx", EDMA_FILTER_PARAM(0, 9) },
	{ "spi_davinci.2", "tx", EDMA_FILTER_PARAM(0, 10) },
	{ "spi_davinci.2", "rx", EDMA_FILTER_PARAM(0, 11) },
	{ "spi_davinci.1", "tx", EDMA_FILTER_PARAM(0, 14) },
	{ "spi_davinci.1", "rx", EDMA_FILTER_PARAM(0, 15) },
	{ "spi_davinci.0", "tx", EDMA_FILTER_PARAM(0, 16) },
	{ "spi_davinci.0", "rx", EDMA_FILTER_PARAM(0, 17) },
	{ "dm6441-mmc.0", "rx", EDMA_FILTER_PARAM(0, 26) },
	{ "dm6441-mmc.0", "tx", EDMA_FILTER_PARAM(0, 27) },
	{ "dm6441-mmc.1", "rx", EDMA_FILTER_PARAM(0, 30) },
	{ "dm6441-mmc.1", "tx", EDMA_FILTER_PARAM(0, 31) },
};

static struct edma_soc_info dm355_edma_pdata = {
	.queue_priority_mapping	= queue_priority_mapping,
	.default_queue		= EVENTQ_1,
	.slave_map		= dm355_edma_map,
	.slavecnt		= ARRAY_SIZE(dm355_edma_map),
};

static struct resource edma_resources[] = {
	{
		.name	= "edma3_cc",
		.start	= 0x01c00000,
		.end	= 0x01c00000 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_tc0",
		.start	= 0x01c10000,
		.end	= 0x01c10000 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_tc1",
		.start	= 0x01c10400,
		.end	= 0x01c10400 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma3_ccint",
		.start	= IRQ_CCINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma3_ccerrint",
		.start	= IRQ_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
	/* not using (or muxing) TC*_ERR */
};

static const struct platform_device_info dm355_edma_device __initconst = {
	.name		= "edma",
	.id		= 0,
	.dma_mask	= DMA_BIT_MASK(32),
	.res		= edma_resources,
	.num_res	= ARRAY_SIZE(edma_resources),
	.data		= &dm355_edma_pdata,
	.size_data	= sizeof(dm355_edma_pdata),
};

static struct resource dm355_asp1_resources[] = {
	{
		.name	= "mpu",
		.start	= DAVINCI_ASP1_BASE,
		.end	= DAVINCI_ASP1_BASE + SZ_8K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= DAVINCI_DMA_ASP1_TX,
		.end	= DAVINCI_DMA_ASP1_TX,
		.flags	= IORESOURCE_DMA,
	},
	{
		.start	= DAVINCI_DMA_ASP1_RX,
		.end	= DAVINCI_DMA_ASP1_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device dm355_asp1_device = {
	.name		= "davinci-mcbsp",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(dm355_asp1_resources),
	.resource	= dm355_asp1_resources,
};

static void dm355_ccdc_setup_pinmux(void)
{
	davinci_cfg_reg(DM355_VIN_PCLK);
	davinci_cfg_reg(DM355_VIN_CAM_WEN);
	davinci_cfg_reg(DM355_VIN_CAM_VD);
	davinci_cfg_reg(DM355_VIN_CAM_HD);
	davinci_cfg_reg(DM355_VIN_YIN_EN);
	davinci_cfg_reg(DM355_VIN_CINL_EN);
	davinci_cfg_reg(DM355_VIN_CINH_EN);
}

static struct resource dm355_vpss_resources[] = {
	{
		/* VPSS BL Base address */
		.name		= "vpss",
		.start          = 0x01c70800,
		.end            = 0x01c70800 + 0xff,
		.flags          = IORESOURCE_MEM,
	},
	{
		/* VPSS CLK Base address */
		.name		= "vpss",
		.start          = 0x01c70000,
		.end            = 0x01c70000 + 0xf,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device dm355_vpss_device = {
	.name			= "vpss",
	.id			= -1,
	.dev.platform_data	= "dm355_vpss",
	.num_resources		= ARRAY_SIZE(dm355_vpss_resources),
	.resource		= dm355_vpss_resources,
};

static struct resource vpfe_resources[] = {
	{
		.start          = IRQ_VDINT0,
		.end            = IRQ_VDINT0,
		.flags          = IORESOURCE_IRQ,
	},
	{
		.start          = IRQ_VDINT1,
		.end            = IRQ_VDINT1,
		.flags          = IORESOURCE_IRQ,
	},
};

static u64 vpfe_capture_dma_mask = DMA_BIT_MASK(32);
static struct resource dm355_ccdc_resource[] = {
	/* CCDC Base address */
	{
		.flags          = IORESOURCE_MEM,
		.start          = 0x01c70600,
		.end            = 0x01c70600 + 0x1ff,
	},
};
static struct platform_device dm355_ccdc_dev = {
	.name           = "dm355_ccdc",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(dm355_ccdc_resource),
	.resource       = dm355_ccdc_resource,
	.dev = {
		.dma_mask               = &vpfe_capture_dma_mask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
		.platform_data		= dm355_ccdc_setup_pinmux,
	},
};

static struct platform_device vpfe_capture_dev = {
	.name		= CAPTURE_DRV_NAME,
	.id		= -1,
	.num_resources	= ARRAY_SIZE(vpfe_resources),
	.resource	= vpfe_resources,
	.dev = {
		.dma_mask		= &vpfe_capture_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource dm355_osd_resources[] = {
	{
		.start	= DM355_OSD_BASE,
		.end	= DM355_OSD_BASE + 0x17f,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dm355_osd_dev = {
	.name		= DM355_VPBE_OSD_SUBDEV_NAME,
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm355_osd_resources),
	.resource	= dm355_osd_resources,
	.dev		= {
		.dma_mask		= &vpfe_capture_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource dm355_venc_resources[] = {
	{
		.start	= IRQ_VENCINT,
		.end	= IRQ_VENCINT,
		.flags	= IORESOURCE_IRQ,
	},
	/* venc registers io space */
	{
		.start	= DM355_VENC_BASE,
		.end	= DM355_VENC_BASE + 0x17f,
		.flags	= IORESOURCE_MEM,
	},
	/* VDAC config register io space */
	{
		.start	= DAVINCI_SYSTEM_MODULE_BASE + SYSMOD_VDAC_CONFIG,
		.end	= DAVINCI_SYSTEM_MODULE_BASE + SYSMOD_VDAC_CONFIG + 3,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource dm355_v4l2_disp_resources[] = {
	{
		.start	= IRQ_VENCINT,
		.end	= IRQ_VENCINT,
		.flags	= IORESOURCE_IRQ,
	},
	/* venc registers io space */
	{
		.start	= DM355_VENC_BASE,
		.end	= DM355_VENC_BASE + 0x17f,
		.flags	= IORESOURCE_MEM,
	},
};

static int dm355_vpbe_setup_pinmux(u32 if_type, int field)
{
	switch (if_type) {
	case MEDIA_BUS_FMT_SGRBG8_1X8:
		davinci_cfg_reg(DM355_VOUT_FIELD_G70);
		break;
	case MEDIA_BUS_FMT_YUYV10_1X20:
		if (field)
			davinci_cfg_reg(DM355_VOUT_FIELD);
		else
			davinci_cfg_reg(DM355_VOUT_FIELD_G70);
		break;
	default:
		return -EINVAL;
	}

	davinci_cfg_reg(DM355_VOUT_COUTL_EN);
	davinci_cfg_reg(DM355_VOUT_COUTH_EN);

	return 0;
}

static int dm355_venc_setup_clock(enum vpbe_enc_timings_type type,
				   unsigned int pclock)
{
	void __iomem *vpss_clk_ctrl_reg;

	vpss_clk_ctrl_reg = DAVINCI_SYSMOD_VIRT(SYSMOD_VPSS_CLKCTL);

	switch (type) {
	case VPBE_ENC_STD:
		writel(VPSS_DACCLKEN_ENABLE | VPSS_VENCCLKEN_ENABLE,
		       vpss_clk_ctrl_reg);
		break;
	case VPBE_ENC_DV_TIMINGS:
		if (pclock > 27000000)
			/*
			 * For HD, use external clock source since we cannot
			 * support HD mode with internal clocks.
			 */
			writel(VPSS_MUXSEL_EXTCLK_ENABLE, vpss_clk_ctrl_reg);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct platform_device dm355_vpbe_display = {
	.name		= "vpbe-v4l2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm355_v4l2_disp_resources),
	.resource	= dm355_v4l2_disp_resources,
	.dev		= {
		.dma_mask		= &vpfe_capture_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct venc_platform_data dm355_venc_pdata = {
	.setup_pinmux	= dm355_vpbe_setup_pinmux,
	.setup_clock	= dm355_venc_setup_clock,
};

static struct platform_device dm355_venc_dev = {
	.name		= DM355_VPBE_VENC_SUBDEV_NAME,
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm355_venc_resources),
	.resource	= dm355_venc_resources,
	.dev		= {
		.dma_mask		= &vpfe_capture_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= (void *)&dm355_venc_pdata,
	},
};

static struct platform_device dm355_vpbe_dev = {
	.name		= "vpbe_controller",
	.id		= -1,
	.dev		= {
		.dma_mask		= &vpfe_capture_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

static struct resource dm355_gpio_resources[] = {
	{	/* registers */
		.start	= DAVINCI_GPIO_BASE,
		.end	= DAVINCI_GPIO_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{	/* interrupt */
		.start	= IRQ_DM355_GPIOBNK0,
		.end	= IRQ_DM355_GPIOBNK6,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct davinci_gpio_platform_data dm355_gpio_platform_data = {
	.ngpio		= 104,
};

int __init dm355_gpio_register(void)
{
	return davinci_gpio_register(dm355_gpio_resources,
				     ARRAY_SIZE(dm355_gpio_resources),
				     &dm355_gpio_platform_data);
}
/*----------------------------------------------------------------------*/

static struct map_desc dm355_io_desc[] = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id dm355_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb73b,
		.manufacturer	= 0x00f,
		.cpu_id		= DAVINCI_CPU_ID_DM355,
		.name		= "dm355",
	},
};

/*
 * T0_BOT: Timer 0, bottom:  clockevent source for hrtimers
 * T0_TOP: Timer 0, top   :  clocksource for generic timekeeping
 * T1_BOT: Timer 1, bottom:  (used by DSP in TI DSPLink code)
 * T1_TOP: Timer 1, top   :  <unused>
 */
static struct davinci_timer_info dm355_timer_info = {
	.timers		= davinci_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

static struct plat_serial8250_port dm355_serial0_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART0_BASE,
		.irq		= IRQ_UARTINT0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};
static struct plat_serial8250_port dm355_serial1_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART1_BASE,
		.irq		= IRQ_UARTINT1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};
static struct plat_serial8250_port dm355_serial2_platform_data[] = {
	{
		.mapbase	= DM355_UART2_BASE,
		.irq		= IRQ_DM355_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags	= 0,
	}
};

struct platform_device dm355_serial_device[] = {
	{
		.name			= "serial8250",
		.id			= PLAT8250_DEV_PLATFORM,
		.dev			= {
			.platform_data	= dm355_serial0_platform_data,
		}
	},
	{
		.name			= "serial8250",
		.id			= PLAT8250_DEV_PLATFORM1,
		.dev			= {
			.platform_data	= dm355_serial1_platform_data,
		}
	},
	{
		.name			= "serial8250",
		.id			= PLAT8250_DEV_PLATFORM2,
		.dev			= {
			.platform_data	= dm355_serial2_platform_data,
		}
	},
	{
	}
};

static const struct davinci_soc_info davinci_soc_info_dm355 = {
	.io_desc		= dm355_io_desc,
	.io_desc_num		= ARRAY_SIZE(dm355_io_desc),
	.jtag_id_reg		= 0x01c40028,
	.ids			= dm355_ids,
	.ids_num		= ARRAY_SIZE(dm355_ids),
	.pinmux_base		= DAVINCI_SYSTEM_MODULE_BASE,
	.pinmux_pins		= dm355_pins,
	.pinmux_pins_num	= ARRAY_SIZE(dm355_pins),
	.intc_base		= DAVINCI_ARM_INTC_BASE,
	.intc_type		= DAVINCI_INTC_TYPE_AINTC,
	.intc_irq_prios		= dm355_default_priorities,
	.intc_irq_num		= DAVINCI_N_AINTC_IRQ,
	.timer_info		= &dm355_timer_info,
	.sram_dma		= 0x00010000,
	.sram_len		= SZ_32K,
};

void __init dm355_init_asp1(u32 evt_enable)
{
	/* we don't use ASP1 IRQs, or we'd need to mux them ... */
	if (evt_enable & ASP1_TX_EVT_EN)
		davinci_cfg_reg(DM355_EVT8_ASP1_TX);

	if (evt_enable & ASP1_RX_EVT_EN)
		davinci_cfg_reg(DM355_EVT9_ASP1_RX);

	platform_device_register(&dm355_asp1_device);
}

void __init dm355_init(void)
{
	davinci_common_init(&davinci_soc_info_dm355);
	davinci_map_sysmod();
}

void __init dm355_init_time(void)
{
	void __iomem *pll1, *psc;
	struct clk *clk;

	clk_register_fixed_rate(NULL, "ref_clk", NULL, 0, DM355_REF_FREQ);

	pll1 = ioremap(DAVINCI_PLL1_BASE, SZ_1K);
	dm355_pll1_init(NULL, pll1, NULL);

	psc = ioremap(DAVINCI_PWR_SLEEP_CNTRL_BASE, SZ_4K);
	dm355_psc_init(NULL, psc);

	clk = clk_get(NULL, "timer0");

	davinci_timer_init(clk);
}

static struct resource dm355_pll2_resources[] = {
	{
		.start	= DAVINCI_PLL2_BASE,
		.end	= DAVINCI_PLL2_BASE + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dm355_pll2_device = {
	.name		= "dm355-pll2",
	.id		= -1,
	.resource	= dm355_pll2_resources,
	.num_resources	= ARRAY_SIZE(dm355_pll2_resources),
};

void __init dm355_register_clocks(void)
{
	/* PLL1 and PSC are registered in dm355_init_time() */
	platform_device_register(&dm355_pll2_device);
}

int __init dm355_init_video(struct vpfe_config *vpfe_cfg,
				struct vpbe_config *vpbe_cfg)
{
	if (vpfe_cfg || vpbe_cfg)
		platform_device_register(&dm355_vpss_device);

	if (vpfe_cfg) {
		vpfe_capture_dev.dev.platform_data = vpfe_cfg;
		platform_device_register(&dm355_ccdc_dev);
		platform_device_register(&vpfe_capture_dev);
	}

	if (vpbe_cfg) {
		dm355_vpbe_dev.dev.platform_data = vpbe_cfg;
		platform_device_register(&dm355_osd_dev);
		platform_device_register(&dm355_venc_dev);
		platform_device_register(&dm355_vpbe_dev);
		platform_device_register(&dm355_vpbe_display);
	}

	return 0;
}

static int __init dm355_init_devices(void)
{
	struct platform_device *edma_pdev;
	int ret = 0;

	if (!cpu_is_davinci_dm355())
		return 0;

	davinci_cfg_reg(DM355_INT_EDMA_CC);
	edma_pdev = platform_device_register_full(&dm355_edma_device);
	if (IS_ERR(edma_pdev)) {
		pr_warn("%s: Failed to register eDMA\n", __func__);
		return PTR_ERR(edma_pdev);
	}

	ret = davinci_init_wdt();
	if (ret)
		pr_warn("%s: watchdog init failed: %d\n", __func__, ret);

	return ret;
}
postcore_initcall(dm355_init_devices);
