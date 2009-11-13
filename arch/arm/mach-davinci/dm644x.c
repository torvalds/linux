/*
 * TI DaVinci DM644x chip specific setup
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * 2007 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <asm/mach/map.h>

#include <mach/dm644x.h>
#include <mach/cputype.h>
#include <mach/edma.h>
#include <mach/irqs.h>
#include <mach/psc.h>
#include <mach/mux.h>
#include <mach/time.h>
#include <mach/serial.h>
#include <mach/common.h>
#include <mach/asp.h>

#include "clock.h"
#include "mux.h"

/*
 * Device specific clocks
 */
#define DM644X_REF_FREQ		27000000

static struct pll_data pll1_data = {
	.num       = 1,
	.phys_base = DAVINCI_PLL1_BASE,
};

static struct pll_data pll2_data = {
	.num       = 2,
	.phys_base = DAVINCI_PLL2_BASE,
};

static struct clk ref_clk = {
	.name = "ref_clk",
	.rate = DM644X_REF_FREQ,
};

static struct clk pll1_clk = {
	.name = "pll1",
	.parent = &ref_clk,
	.pll_data = &pll1_data,
	.flags = CLK_PLL,
};

static struct clk pll1_sysclk1 = {
	.name = "pll1_sysclk1",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV1,
};

static struct clk pll1_sysclk2 = {
	.name = "pll1_sysclk2",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV2,
};

static struct clk pll1_sysclk3 = {
	.name = "pll1_sysclk3",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV3,
};

static struct clk pll1_sysclk5 = {
	.name = "pll1_sysclk5",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV5,
};

static struct clk pll1_aux_clk = {
	.name = "pll1_aux_clk",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
};

static struct clk pll1_sysclkbp = {
	.name = "pll1_sysclkbp",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
	.div_reg = BPDIV
};

static struct clk pll2_clk = {
	.name = "pll2",
	.parent = &ref_clk,
	.pll_data = &pll2_data,
	.flags = CLK_PLL,
};

static struct clk pll2_sysclk1 = {
	.name = "pll2_sysclk1",
	.parent = &pll2_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV1,
};

static struct clk pll2_sysclk2 = {
	.name = "pll2_sysclk2",
	.parent = &pll2_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV2,
};

static struct clk pll2_sysclkbp = {
	.name = "pll2_sysclkbp",
	.parent = &pll2_clk,
	.flags = CLK_PLL | PRE_PLL,
	.div_reg = BPDIV
};

static struct clk dsp_clk = {
	.name = "dsp",
	.parent = &pll1_sysclk1,
	.lpsc = DAVINCI_LPSC_GEM,
	.flags = PSC_DSP,
	.usecount = 1,			/* REVISIT how to disable? */
};

static struct clk arm_clk = {
	.name = "arm",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_ARM,
	.flags = ALWAYS_ENABLED,
};

static struct clk vicp_clk = {
	.name = "vicp",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_IMCOP,
	.flags = PSC_DSP,
	.usecount = 1,			/* REVISIT how to disable? */
};

static struct clk vpss_master_clk = {
	.name = "vpss_master",
	.parent = &pll1_sysclk3,
	.lpsc = DAVINCI_LPSC_VPSSMSTR,
	.flags = CLK_PSC,
};

static struct clk vpss_slave_clk = {
	.name = "vpss_slave",
	.parent = &pll1_sysclk3,
	.lpsc = DAVINCI_LPSC_VPSSSLV,
};

static struct clk uart0_clk = {
	.name = "uart0",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_UART0,
};

static struct clk uart1_clk = {
	.name = "uart1",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_UART1,
};

static struct clk uart2_clk = {
	.name = "uart2",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_UART2,
};

static struct clk emac_clk = {
	.name = "emac",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_EMAC_WRAPPER,
};

static struct clk i2c_clk = {
	.name = "i2c",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_I2C,
};

static struct clk ide_clk = {
	.name = "ide",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_ATA,
};

static struct clk asp_clk = {
	.name = "asp0",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_McBSP,
};

static struct clk mmcsd_clk = {
	.name = "mmcsd",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_MMC_SD,
};

static struct clk spi_clk = {
	.name = "spi",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_SPI,
};

static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_GPIO,
};

static struct clk usb_clk = {
	.name = "usb",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_USB,
};

static struct clk vlynq_clk = {
	.name = "vlynq",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_VLYNQ,
};

static struct clk aemif_clk = {
	.name = "aemif",
	.parent = &pll1_sysclk5,
	.lpsc = DAVINCI_LPSC_AEMIF,
};

static struct clk pwm0_clk = {
	.name = "pwm0",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_PWM0,
};

static struct clk pwm1_clk = {
	.name = "pwm1",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_PWM1,
};

static struct clk pwm2_clk = {
	.name = "pwm2",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_PWM2,
};

static struct clk timer0_clk = {
	.name = "timer0",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_TIMER0,
};

static struct clk timer1_clk = {
	.name = "timer1",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_TIMER1,
};

static struct clk timer2_clk = {
	.name = "timer2",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_TIMER2,
	.usecount = 1,              /* REVISIT: why cant' this be disabled? */
};

struct davinci_clk dm644x_clks[] = {
	CLK(NULL, "ref", &ref_clk),
	CLK(NULL, "pll1", &pll1_clk),
	CLK(NULL, "pll1_sysclk1", &pll1_sysclk1),
	CLK(NULL, "pll1_sysclk2", &pll1_sysclk2),
	CLK(NULL, "pll1_sysclk3", &pll1_sysclk3),
	CLK(NULL, "pll1_sysclk5", &pll1_sysclk5),
	CLK(NULL, "pll1_aux", &pll1_aux_clk),
	CLK(NULL, "pll1_sysclkbp", &pll1_sysclkbp),
	CLK(NULL, "pll2", &pll2_clk),
	CLK(NULL, "pll2_sysclk1", &pll2_sysclk1),
	CLK(NULL, "pll2_sysclk2", &pll2_sysclk2),
	CLK(NULL, "pll2_sysclkbp", &pll2_sysclkbp),
	CLK(NULL, "dsp", &dsp_clk),
	CLK(NULL, "arm", &arm_clk),
	CLK(NULL, "vicp", &vicp_clk),
	CLK(NULL, "vpss_master", &vpss_master_clk),
	CLK(NULL, "vpss_slave", &vpss_slave_clk),
	CLK(NULL, "arm", &arm_clk),
	CLK(NULL, "uart0", &uart0_clk),
	CLK(NULL, "uart1", &uart1_clk),
	CLK(NULL, "uart2", &uart2_clk),
	CLK("davinci_emac.1", NULL, &emac_clk),
	CLK("i2c_davinci.1", NULL, &i2c_clk),
	CLK("palm_bk3710", NULL, &ide_clk),
	CLK("davinci-asp", NULL, &asp_clk),
	CLK("davinci_mmc.0", NULL, &mmcsd_clk),
	CLK(NULL, "spi", &spi_clk),
	CLK(NULL, "gpio", &gpio_clk),
	CLK(NULL, "usb", &usb_clk),
	CLK(NULL, "vlynq", &vlynq_clk),
	CLK(NULL, "aemif", &aemif_clk),
	CLK(NULL, "pwm0", &pwm0_clk),
	CLK(NULL, "pwm1", &pwm1_clk),
	CLK(NULL, "pwm2", &pwm2_clk),
	CLK(NULL, "timer0", &timer0_clk),
	CLK(NULL, "timer1", &timer1_clk),
	CLK("watchdog", NULL, &timer2_clk),
	CLK(NULL, NULL, NULL),
};

static struct emac_platform_data dm644x_emac_pdata = {
	.ctrl_reg_offset	= DM644X_EMAC_CNTRL_OFFSET,
	.ctrl_mod_reg_offset	= DM644X_EMAC_CNTRL_MOD_OFFSET,
	.ctrl_ram_offset	= DM644X_EMAC_CNTRL_RAM_OFFSET,
	.mdio_reg_offset	= DM644X_EMAC_MDIO_OFFSET,
	.ctrl_ram_size		= DM644X_EMAC_CNTRL_RAM_SIZE,
	.version		= EMAC_VERSION_1,
};

static struct resource dm644x_emac_resources[] = {
	{
		.start	= DM644X_EMAC_BASE,
		.end	= DM644X_EMAC_BASE + 0x47ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start = IRQ_EMACINT,
		.end   = IRQ_EMACINT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device dm644x_emac_device = {
       .name		= "davinci_emac",
       .id		= 1,
       .dev = {
	       .platform_data	= &dm644x_emac_pdata,
       },
       .num_resources	= ARRAY_SIZE(dm644x_emac_resources),
       .resource	= dm644x_emac_resources,
};

#define PINMUX0		0x00
#define PINMUX1		0x04

/*
 * Device specific mux setup
 *
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config dm644x_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
MUX_CFG(DM644X, HDIREN,		0,   16,    1,	  1,	 true)
MUX_CFG(DM644X, ATAEN,		0,   17,    1,	  1,	 true)
MUX_CFG(DM644X, ATAEN_DISABLE,	0,   17,    1,	  0,	 true)

MUX_CFG(DM644X, HPIEN_DISABLE,	0,   29,    1,	  0,	 true)

MUX_CFG(DM644X, AEAW,		0,   0,     31,	  31,	 true)
MUX_CFG(DM644X, AEAW0,		0,   0,     1,	  0,	 true)
MUX_CFG(DM644X, AEAW1,		0,   1,     1,	  0,	 true)
MUX_CFG(DM644X, AEAW2,		0,   2,     1,	  0,	 true)
MUX_CFG(DM644X, AEAW3,		0,   3,     1,	  0,	 true)
MUX_CFG(DM644X, AEAW4,		0,   4,     1,	  0,	 true)

MUX_CFG(DM644X, MSTK,		1,   9,     1,	  0,	 false)

MUX_CFG(DM644X, I2C,		1,   7,     1,	  1,	 false)

MUX_CFG(DM644X, MCBSP,		1,   10,    1,	  1,	 false)

MUX_CFG(DM644X, UART1,		1,   1,     1,	  1,	 true)
MUX_CFG(DM644X, UART2,		1,   2,     1,	  1,	 true)

MUX_CFG(DM644X, PWM0,		1,   4,     1,	  1,	 false)

MUX_CFG(DM644X, PWM1,		1,   5,     1,	  1,	 false)

MUX_CFG(DM644X, PWM2,		1,   6,     1,	  1,	 false)

MUX_CFG(DM644X, VLYNQEN,	0,   15,    1,	  1,	 false)
MUX_CFG(DM644X, VLSCREN,	0,   14,    1,	  1,	 false)
MUX_CFG(DM644X, VLYNQWD,	0,   12,    3,	  3,	 false)

MUX_CFG(DM644X, EMACEN,		0,   31,    1,	  1,	 true)

MUX_CFG(DM644X, GPIO3V,		0,   31,    1,	  0,	 true)

MUX_CFG(DM644X, GPIO0,		0,   24,    1,	  0,	 true)
MUX_CFG(DM644X, GPIO3,		0,   25,    1,	  0,	 false)
MUX_CFG(DM644X, GPIO43_44,	1,   7,     1,	  0,	 false)
MUX_CFG(DM644X, GPIO46_47,	0,   22,    1,	  0,	 true)

MUX_CFG(DM644X, RGB666,		0,   22,    1,	  1,	 true)

MUX_CFG(DM644X, LOEEN,		0,   24,    1,	  1,	 true)
MUX_CFG(DM644X, LFLDEN,		0,   25,    1,	  1,	 false)
#endif
};

/* FIQ are pri 0-1; otherwise 2-7, with 7 lowest priority */
static u8 dm644x_default_priorities[DAVINCI_N_AINTC_IRQ] = {
	[IRQ_VDINT0]		= 2,
	[IRQ_VDINT1]		= 6,
	[IRQ_VDINT2]		= 6,
	[IRQ_HISTINT]		= 6,
	[IRQ_H3AINT]		= 6,
	[IRQ_PRVUINT]		= 6,
	[IRQ_RSZINT]		= 6,
	[7]			= 7,
	[IRQ_VENCINT]		= 6,
	[IRQ_ASQINT]		= 6,
	[IRQ_IMXINT]		= 6,
	[IRQ_VLCDINT]		= 6,
	[IRQ_USBINT]		= 4,
	[IRQ_EMACINT]		= 4,
	[14]			= 7,
	[15]			= 7,
	[IRQ_CCINT0]		= 5,	/* dma */
	[IRQ_CCERRINT]		= 5,	/* dma */
	[IRQ_TCERRINT0]		= 5,	/* dma */
	[IRQ_TCERRINT]		= 5,	/* dma */
	[IRQ_PSCIN]		= 7,
	[21]			= 7,
	[IRQ_IDE]		= 4,
	[23]			= 7,
	[IRQ_MBXINT]		= 7,
	[IRQ_MBRINT]		= 7,
	[IRQ_MMCINT]		= 7,
	[IRQ_SDIOINT]		= 7,
	[28]			= 7,
	[IRQ_DDRINT]		= 7,
	[IRQ_AEMIFINT]		= 7,
	[IRQ_VLQINT]		= 4,
	[IRQ_TINT0_TINT12]	= 2,	/* clockevent */
	[IRQ_TINT0_TINT34]	= 2,	/* clocksource */
	[IRQ_TINT1_TINT12]	= 7,	/* DSP timer */
	[IRQ_TINT1_TINT34]	= 7,	/* system tick */
	[IRQ_PWMINT0]		= 7,
	[IRQ_PWMINT1]		= 7,
	[IRQ_PWMINT2]		= 7,
	[IRQ_I2C]		= 3,
	[IRQ_UARTINT0]		= 3,
	[IRQ_UARTINT1]		= 3,
	[IRQ_UARTINT2]		= 3,
	[IRQ_SPINT0]		= 3,
	[IRQ_SPINT1]		= 3,
	[45]			= 7,
	[IRQ_DSP2ARM0]		= 4,
	[IRQ_DSP2ARM1]		= 4,
	[IRQ_GPIO0]		= 7,
	[IRQ_GPIO1]		= 7,
	[IRQ_GPIO2]		= 7,
	[IRQ_GPIO3]		= 7,
	[IRQ_GPIO4]		= 7,
	[IRQ_GPIO5]		= 7,
	[IRQ_GPIO6]		= 7,
	[IRQ_GPIO7]		= 7,
	[IRQ_GPIOBNK0]		= 7,
	[IRQ_GPIOBNK1]		= 7,
	[IRQ_GPIOBNK2]		= 7,
	[IRQ_GPIOBNK3]		= 7,
	[IRQ_GPIOBNK4]		= 7,
	[IRQ_COMMTX]		= 7,
	[IRQ_COMMRX]		= 7,
	[IRQ_EMUINT]		= 7,
};

/*----------------------------------------------------------------------*/

static const s8 dma_chan_dm644x_no_event[] = {
	 0,  1, 12, 13, 14,
	15, 25, 30, 31, 45,
	46, 47, 55, 56, 57,
	58, 59, 60, 61, 62,
	63,
	-1
};

static const s8
queue_tc_mapping[][2] = {
	/* {event queue no, TC no} */
	{0, 0},
	{1, 1},
	{-1, -1},
};

static const s8
queue_priority_mapping[][2] = {
	/* {event queue no, Priority} */
	{0, 3},
	{1, 7},
	{-1, -1},
};

static struct edma_soc_info dm644x_edma_info[] = {
	{
		.n_channel		= 64,
		.n_region		= 4,
		.n_slot			= 128,
		.n_tc			= 2,
		.n_cc			= 1,
		.noevent		= dma_chan_dm644x_no_event,
		.queue_tc_mapping	= queue_tc_mapping,
		.queue_priority_mapping	= queue_priority_mapping,
	},
};

static struct resource edma_resources[] = {
	{
		.name	= "edma_cc0",
		.start	= 0x01c00000,
		.end	= 0x01c00000 + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc0",
		.start	= 0x01c10000,
		.end	= 0x01c10000 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc1",
		.start	= 0x01c10400,
		.end	= 0x01c10400 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma0",
		.start	= IRQ_CCINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "edma0_err",
		.start	= IRQ_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
	/* not using TC*_ERR */
};

static struct platform_device dm644x_edma_device = {
	.name			= "edma",
	.id			= 0,
	.dev.platform_data	= dm644x_edma_info,
	.num_resources		= ARRAY_SIZE(edma_resources),
	.resource		= edma_resources,
};

/* DM6446 EVM uses ASP0; line-out is a pair of RCA jacks */
static struct resource dm644x_asp_resources[] = {
	{
		.start	= DAVINCI_ASP0_BASE,
		.end	= DAVINCI_ASP0_BASE + SZ_8K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= DAVINCI_DMA_ASP0_TX,
		.end	= DAVINCI_DMA_ASP0_TX,
		.flags	= IORESOURCE_DMA,
	},
	{
		.start	= DAVINCI_DMA_ASP0_RX,
		.end	= DAVINCI_DMA_ASP0_RX,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device dm644x_asp_device = {
	.name		= "davinci-asp",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm644x_asp_resources),
	.resource	= dm644x_asp_resources,
};

static struct resource dm644x_vpss_resources[] = {
	{
		/* VPSS Base address */
		.name		= "vpss",
		.start          = 0x01c73400,
		.end            = 0x01c73400 + 0xff,
		.flags          = IORESOURCE_MEM,
	},
};

static struct platform_device dm644x_vpss_device = {
	.name			= "vpss",
	.id			= -1,
	.dev.platform_data	= "dm644x_vpss",
	.num_resources		= ARRAY_SIZE(dm644x_vpss_resources),
	.resource		= dm644x_vpss_resources,
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
	{
		.start          = 0x01c70400,
		.end            = 0x01c70400 + 0xff,
		.flags          = IORESOURCE_MEM,
	},
};

static u64 vpfe_capture_dma_mask = DMA_BIT_MASK(32);
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

void dm644x_set_vpfe_config(struct vpfe_config *cfg)
{
	vpfe_capture_dev.dev.platform_data = cfg;
}

/*----------------------------------------------------------------------*/

static struct map_desc dm644x_io_desc[] = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= SRAM_VIRT,
		.pfn		= __phys_to_pfn(0x00008000),
		.length		= SZ_16K,
		/* MT_MEMORY_NONCACHED requires supersection alignment */
		.type		= MT_DEVICE,
	},
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id dm644x_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb700,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DM6446,
		.name		= "dm6446",
	},
	{
		.variant	= 0x1,
		.part_no	= 0xb700,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DM6446,
		.name		= "dm6446a",
	},
};

static void __iomem *dm644x_psc_bases[] = {
	IO_ADDRESS(DAVINCI_PWR_SLEEP_CNTRL_BASE),
};

/*
 * T0_BOT: Timer 0, bottom:  clockevent source for hrtimers
 * T0_TOP: Timer 0, top   :  clocksource for generic timekeeping
 * T1_BOT: Timer 1, bottom:  (used by DSP in TI DSPLink code)
 * T1_TOP: Timer 1, top   :  <unused>
 */
struct davinci_timer_info dm644x_timer_info = {
	.timers		= davinci_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

static struct plat_serial8250_port dm644x_serial_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART0_BASE,
		.irq		= IRQ_UARTINT0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.mapbase	= DAVINCI_UART1_BASE,
		.irq		= IRQ_UARTINT1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.mapbase	= DAVINCI_UART2_BASE,
		.irq		= IRQ_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags		= 0
	},
};

static struct platform_device dm644x_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= dm644x_serial_platform_data,
	},
};

static struct davinci_soc_info davinci_soc_info_dm644x = {
	.io_desc		= dm644x_io_desc,
	.io_desc_num		= ARRAY_SIZE(dm644x_io_desc),
	.jtag_id_base		= IO_ADDRESS(0x01c40028),
	.ids			= dm644x_ids,
	.ids_num		= ARRAY_SIZE(dm644x_ids),
	.cpu_clks		= dm644x_clks,
	.psc_bases		= dm644x_psc_bases,
	.psc_bases_num		= ARRAY_SIZE(dm644x_psc_bases),
	.pinmux_base		= IO_ADDRESS(DAVINCI_SYSTEM_MODULE_BASE),
	.pinmux_pins		= dm644x_pins,
	.pinmux_pins_num	= ARRAY_SIZE(dm644x_pins),
	.intc_base		= IO_ADDRESS(DAVINCI_ARM_INTC_BASE),
	.intc_type		= DAVINCI_INTC_TYPE_AINTC,
	.intc_irq_prios 	= dm644x_default_priorities,
	.intc_irq_num		= DAVINCI_N_AINTC_IRQ,
	.timer_info		= &dm644x_timer_info,
	.gpio_base		= IO_ADDRESS(DAVINCI_GPIO_BASE),
	.gpio_num		= 71,
	.gpio_irq		= IRQ_GPIOBNK0,
	.serial_dev		= &dm644x_serial_device,
	.emac_pdata		= &dm644x_emac_pdata,
	.sram_dma		= 0x00008000,
	.sram_len		= SZ_16K,
};

void __init dm644x_init_asp(struct snd_platform_data *pdata)
{
	davinci_cfg_reg(DM644X_MCBSP);
	dm644x_asp_device.dev.platform_data = pdata;
	platform_device_register(&dm644x_asp_device);
}

void __init dm644x_init(void)
{
	davinci_common_init(&davinci_soc_info_dm644x);
}

static int __init dm644x_init_devices(void)
{
	if (!cpu_is_davinci_dm644x())
		return 0;

	platform_device_register(&dm644x_edma_device);
	platform_device_register(&dm644x_emac_device);
	platform_device_register(&dm644x_vpss_device);
	platform_device_register(&vpfe_capture_dev);

	return 0;
}
postcore_initcall(dm644x_init_devices);
