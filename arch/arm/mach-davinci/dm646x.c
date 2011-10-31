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
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/platform_device.h>

#include <asm/mach/map.h>

#include <mach/dm646x.h>
#include <mach/cputype.h>
#include <mach/edma.h>
#include <mach/irqs.h>
#include <mach/psc.h>
#include <mach/mux.h>
#include <mach/time.h>
#include <mach/serial.h>
#include <mach/common.h>
#include <mach/asp.h>
#include <mach/gpio-davinci.h>

#include "clock.h"
#include "mux.h"

#define DAVINCI_VPIF_BASE       (0x01C12000)
#define VDD3P3V_PWDN_OFFSET	(0x48)
#define VSCLKDIS_OFFSET		(0x6C)

#define VDD3P3V_VID_MASK	(BIT_MASK(3) | BIT_MASK(2) | BIT_MASK(1) |\
					BIT_MASK(0))
#define VSCLKDIS_MASK		(BIT_MASK(11) | BIT_MASK(10) | BIT_MASK(9) |\
					BIT_MASK(8))

/*
 * Device specific clocks
 */
#define DM646X_REF_FREQ		27000000
#define DM646X_AUX_FREQ		24000000

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
	.rate = DM646X_REF_FREQ,
	.set_rate = davinci_simple_set_rate,
};

static struct clk aux_clkin = {
	.name = "aux_clkin",
	.rate = DM646X_AUX_FREQ,
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

static struct clk pll1_sysclk4 = {
	.name = "pll1_sysclk4",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV4,
};

static struct clk pll1_sysclk5 = {
	.name = "pll1_sysclk5",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV5,
};

static struct clk pll1_sysclk6 = {
	.name = "pll1_sysclk6",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV6,
};

static struct clk pll1_sysclk8 = {
	.name = "pll1_sysclk8",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV8,
};

static struct clk pll1_sysclk9 = {
	.name = "pll1_sysclk9",
	.parent = &pll1_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV9,
};

static struct clk pll1_sysclkbp = {
	.name = "pll1_sysclkbp",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
	.div_reg = BPDIV,
};

static struct clk pll1_aux_clk = {
	.name = "pll1_aux_clk",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
};

static struct clk pll2_clk = {
	.name = "pll2_clk",
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

static struct clk dsp_clk = {
	.name = "dsp",
	.parent = &pll1_sysclk1,
	.lpsc = DM646X_LPSC_C64X_CPU,
	.flags = PSC_DSP,
	.usecount = 1,			/* REVISIT how to disable? */
};

static struct clk arm_clk = {
	.name = "arm",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_ARM,
	.flags = ALWAYS_ENABLED,
};

static struct clk edma_cc_clk = {
	.name = "edma_cc",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_TPCC,
	.flags = ALWAYS_ENABLED,
};

static struct clk edma_tc0_clk = {
	.name = "edma_tc0",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_TPTC0,
	.flags = ALWAYS_ENABLED,
};

static struct clk edma_tc1_clk = {
	.name = "edma_tc1",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_TPTC1,
	.flags = ALWAYS_ENABLED,
};

static struct clk edma_tc2_clk = {
	.name = "edma_tc2",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_TPTC2,
	.flags = ALWAYS_ENABLED,
};

static struct clk edma_tc3_clk = {
	.name = "edma_tc3",
	.parent = &pll1_sysclk2,
	.lpsc = DM646X_LPSC_TPTC3,
	.flags = ALWAYS_ENABLED,
};

static struct clk uart0_clk = {
	.name = "uart0",
	.parent = &aux_clkin,
	.lpsc = DM646X_LPSC_UART0,
};

static struct clk uart1_clk = {
	.name = "uart1",
	.parent = &aux_clkin,
	.lpsc = DM646X_LPSC_UART1,
};

static struct clk uart2_clk = {
	.name = "uart2",
	.parent = &aux_clkin,
	.lpsc = DM646X_LPSC_UART2,
};

static struct clk i2c_clk = {
	.name = "I2CCLK",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_I2C,
};

static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_GPIO,
};

static struct clk mcasp0_clk = {
	.name = "mcasp0",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_McASP0,
};

static struct clk mcasp1_clk = {
	.name = "mcasp1",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_McASP1,
};

static struct clk aemif_clk = {
	.name = "aemif",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_AEMIF,
	.flags = ALWAYS_ENABLED,
};

static struct clk emac_clk = {
	.name = "emac",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_EMAC,
};

static struct clk pwm0_clk = {
	.name = "pwm0",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_PWM0,
	.usecount = 1,            /* REVIST: disabling hangs system */
};

static struct clk pwm1_clk = {
	.name = "pwm1",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_PWM1,
	.usecount = 1,            /* REVIST: disabling hangs system */
};

static struct clk timer0_clk = {
	.name = "timer0",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_TIMER0,
};

static struct clk timer1_clk = {
	.name = "timer1",
	.parent = &pll1_sysclk3,
	.lpsc = DM646X_LPSC_TIMER1,
};

static struct clk timer2_clk = {
	.name = "timer2",
	.parent = &pll1_sysclk3,
	.flags = ALWAYS_ENABLED, /* no LPSC, always enabled; c.f. spruep9a */
};


static struct clk ide_clk = {
	.name = "ide",
	.parent = &pll1_sysclk4,
	.lpsc = DAVINCI_LPSC_ATA,
};

static struct clk vpif0_clk = {
	.name = "vpif0",
	.parent = &ref_clk,
	.lpsc = DM646X_LPSC_VPSSMSTR,
	.flags = ALWAYS_ENABLED,
};

static struct clk vpif1_clk = {
	.name = "vpif1",
	.parent = &ref_clk,
	.lpsc = DM646X_LPSC_VPSSSLV,
	.flags = ALWAYS_ENABLED,
};

static struct clk_lookup dm646x_clks[] = {
	CLK(NULL, "ref", &ref_clk),
	CLK(NULL, "aux", &aux_clkin),
	CLK(NULL, "pll1", &pll1_clk),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk1),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk2),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk3),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk4),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk5),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk6),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk8),
	CLK(NULL, "pll1_sysclk", &pll1_sysclk9),
	CLK(NULL, "pll1_sysclk", &pll1_sysclkbp),
	CLK(NULL, "pll1_aux", &pll1_aux_clk),
	CLK(NULL, "pll2", &pll2_clk),
	CLK(NULL, "pll2_sysclk1", &pll2_sysclk1),
	CLK(NULL, "dsp", &dsp_clk),
	CLK(NULL, "arm", &arm_clk),
	CLK(NULL, "edma_cc", &edma_cc_clk),
	CLK(NULL, "edma_tc0", &edma_tc0_clk),
	CLK(NULL, "edma_tc1", &edma_tc1_clk),
	CLK(NULL, "edma_tc2", &edma_tc2_clk),
	CLK(NULL, "edma_tc3", &edma_tc3_clk),
	CLK(NULL, "uart0", &uart0_clk),
	CLK(NULL, "uart1", &uart1_clk),
	CLK(NULL, "uart2", &uart2_clk),
	CLK("i2c_davinci.1", NULL, &i2c_clk),
	CLK(NULL, "gpio", &gpio_clk),
	CLK("davinci-mcasp.0", NULL, &mcasp0_clk),
	CLK("davinci-mcasp.1", NULL, &mcasp1_clk),
	CLK(NULL, "aemif", &aemif_clk),
	CLK("davinci_emac.1", NULL, &emac_clk),
	CLK(NULL, "pwm0", &pwm0_clk),
	CLK(NULL, "pwm1", &pwm1_clk),
	CLK(NULL, "timer0", &timer0_clk),
	CLK(NULL, "timer1", &timer1_clk),
	CLK("watchdog", NULL, &timer2_clk),
	CLK("palm_bk3710", NULL, &ide_clk),
	CLK(NULL, "vpif0", &vpif0_clk),
	CLK(NULL, "vpif1", &vpif1_clk),
	CLK(NULL, NULL, NULL),
};

static struct emac_platform_data dm646x_emac_pdata = {
	.ctrl_reg_offset	= DM646X_EMAC_CNTRL_OFFSET,
	.ctrl_mod_reg_offset	= DM646X_EMAC_CNTRL_MOD_OFFSET,
	.ctrl_ram_offset	= DM646X_EMAC_CNTRL_RAM_OFFSET,
	.ctrl_ram_size		= DM646X_EMAC_CNTRL_RAM_SIZE,
	.version		= EMAC_VERSION_2,
};

static struct resource dm646x_emac_resources[] = {
	{
		.start	= DM646X_EMAC_BASE,
		.end	= DM646X_EMAC_BASE + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_DM646X_EMACRXTHINT,
		.end	= IRQ_DM646X_EMACRXTHINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_EMACRXINT,
		.end	= IRQ_DM646X_EMACRXINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_EMACTXINT,
		.end	= IRQ_DM646X_EMACTXINT,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_DM646X_EMACMISCINT,
		.end	= IRQ_DM646X_EMACMISCINT,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dm646x_emac_device = {
	.name		= "davinci_emac",
	.id		= 1,
	.dev = {
		.platform_data	= &dm646x_emac_pdata,
	},
	.num_resources	= ARRAY_SIZE(dm646x_emac_resources),
	.resource	= dm646x_emac_resources,
};

static struct resource dm646x_mdio_resources[] = {
	{
		.start	= DM646X_EMAC_MDIO_BASE,
		.end	= DM646X_EMAC_MDIO_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device dm646x_mdio_device = {
	.name		= "davinci_mdio",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm646x_mdio_resources),
	.resource	= dm646x_mdio_resources,
};

/*
 * Device specific mux setup
 *
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config dm646x_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
MUX_CFG(DM646X, ATAEN,		0,   0,     5,	  1,	 true)

MUX_CFG(DM646X, AUDCK1,		0,   29,    1,	  0,	 false)

MUX_CFG(DM646X, AUDCK0,		0,   28,    1,	  0,	 false)

MUX_CFG(DM646X, CRGMUX,			0,   24,    7,    5,	 true)

MUX_CFG(DM646X, STSOMUX_DISABLE,	0,   22,    3,    0,	 true)

MUX_CFG(DM646X, STSIMUX_DISABLE,	0,   20,    3,    0,	 true)

MUX_CFG(DM646X, PTSOMUX_DISABLE,	0,   18,    3,    0,	 true)

MUX_CFG(DM646X, PTSIMUX_DISABLE,	0,   16,    3,    0,	 true)

MUX_CFG(DM646X, STSOMUX,		0,   22,    3,    2,	 true)

MUX_CFG(DM646X, STSIMUX,		0,   20,    3,    2,	 true)

MUX_CFG(DM646X, PTSOMUX_PARALLEL,	0,   18,    3,    2,	 true)

MUX_CFG(DM646X, PTSIMUX_PARALLEL,	0,   16,    3,    2,	 true)

MUX_CFG(DM646X, PTSOMUX_SERIAL,		0,   18,    3,    3,	 true)

MUX_CFG(DM646X, PTSIMUX_SERIAL,		0,   16,    3,    3,	 true)
#endif
};

static u8 dm646x_default_priorities[DAVINCI_N_AINTC_IRQ] = {
	[IRQ_DM646X_VP_VERTINT0]        = 7,
	[IRQ_DM646X_VP_VERTINT1]        = 7,
	[IRQ_DM646X_VP_VERTINT2]        = 7,
	[IRQ_DM646X_VP_VERTINT3]        = 7,
	[IRQ_DM646X_VP_ERRINT]          = 7,
	[IRQ_DM646X_RESERVED_1]         = 7,
	[IRQ_DM646X_RESERVED_2]         = 7,
	[IRQ_DM646X_WDINT]              = 7,
	[IRQ_DM646X_CRGENINT0]          = 7,
	[IRQ_DM646X_CRGENINT1]          = 7,
	[IRQ_DM646X_TSIFINT0]           = 7,
	[IRQ_DM646X_TSIFINT1]           = 7,
	[IRQ_DM646X_VDCEINT]            = 7,
	[IRQ_DM646X_USBINT]             = 7,
	[IRQ_DM646X_USBDMAINT]          = 7,
	[IRQ_DM646X_PCIINT]             = 7,
	[IRQ_CCINT0]                    = 7,    /* dma */
	[IRQ_CCERRINT]                  = 7,    /* dma */
	[IRQ_TCERRINT0]                 = 7,    /* dma */
	[IRQ_TCERRINT]                  = 7,    /* dma */
	[IRQ_DM646X_TCERRINT2]          = 7,
	[IRQ_DM646X_TCERRINT3]          = 7,
	[IRQ_DM646X_IDE]                = 7,
	[IRQ_DM646X_HPIINT]             = 7,
	[IRQ_DM646X_EMACRXTHINT]        = 7,
	[IRQ_DM646X_EMACRXINT]          = 7,
	[IRQ_DM646X_EMACTXINT]          = 7,
	[IRQ_DM646X_EMACMISCINT]        = 7,
	[IRQ_DM646X_MCASP0TXINT]        = 7,
	[IRQ_DM646X_MCASP0RXINT]        = 7,
	[IRQ_AEMIFINT]                  = 7,
	[IRQ_DM646X_RESERVED_3]         = 7,
	[IRQ_DM646X_MCASP1TXINT]        = 7,    /* clockevent */
	[IRQ_TINT0_TINT34]              = 7,    /* clocksource */
	[IRQ_TINT1_TINT12]              = 7,    /* DSP timer */
	[IRQ_TINT1_TINT34]              = 7,    /* system tick */
	[IRQ_PWMINT0]                   = 7,
	[IRQ_PWMINT1]                   = 7,
	[IRQ_DM646X_VLQINT]             = 7,
	[IRQ_I2C]                       = 7,
	[IRQ_UARTINT0]                  = 7,
	[IRQ_UARTINT1]                  = 7,
	[IRQ_DM646X_UARTINT2]           = 7,
	[IRQ_DM646X_SPINT0]             = 7,
	[IRQ_DM646X_SPINT1]             = 7,
	[IRQ_DM646X_DSP2ARMINT]         = 7,
	[IRQ_DM646X_RESERVED_4]         = 7,
	[IRQ_DM646X_PSCINT]             = 7,
	[IRQ_DM646X_GPIO0]              = 7,
	[IRQ_DM646X_GPIO1]              = 7,
	[IRQ_DM646X_GPIO2]              = 7,
	[IRQ_DM646X_GPIO3]              = 7,
	[IRQ_DM646X_GPIO4]              = 7,
	[IRQ_DM646X_GPIO5]              = 7,
	[IRQ_DM646X_GPIO6]              = 7,
	[IRQ_DM646X_GPIO7]              = 7,
	[IRQ_DM646X_GPIOBNK0]           = 7,
	[IRQ_DM646X_GPIOBNK1]           = 7,
	[IRQ_DM646X_GPIOBNK2]           = 7,
	[IRQ_DM646X_DDRINT]             = 7,
	[IRQ_DM646X_AEMIFINT]           = 7,
	[IRQ_COMMTX]                    = 7,
	[IRQ_COMMRX]                    = 7,
	[IRQ_EMUINT]                    = 7,
};

/*----------------------------------------------------------------------*/

/* Four Transfer Controllers on DM646x */
static const s8
dm646x_queue_tc_mapping[][2] = {
	/* {event queue no, TC no} */
	{0, 0},
	{1, 1},
	{2, 2},
	{3, 3},
	{-1, -1},
};

static const s8
dm646x_queue_priority_mapping[][2] = {
	/* {event queue no, Priority} */
	{0, 4},
	{1, 0},
	{2, 5},
	{3, 1},
	{-1, -1},
};

static struct edma_soc_info edma_cc0_info = {
	.n_channel		= 64,
	.n_region		= 6,	/* 0-1, 4-7 */
	.n_slot			= 512,
	.n_tc			= 4,
	.n_cc			= 1,
	.queue_tc_mapping	= dm646x_queue_tc_mapping,
	.queue_priority_mapping	= dm646x_queue_priority_mapping,
	.default_queue		= EVENTQ_1,
};

static struct edma_soc_info *dm646x_edma_info[EDMA_MAX_CC] = {
	&edma_cc0_info,
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
		.name	= "edma_tc2",
		.start	= 0x01c10800,
		.end	= 0x01c10800 + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "edma_tc3",
		.start	= 0x01c10c00,
		.end	= 0x01c10c00 + SZ_1K - 1,
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

static struct platform_device dm646x_edma_device = {
	.name			= "edma",
	.id			= 0,
	.dev.platform_data	= dm646x_edma_info,
	.num_resources		= ARRAY_SIZE(edma_resources),
	.resource		= edma_resources,
};

static struct resource dm646x_mcasp0_resources[] = {
	{
		.name	= "mcasp0",
		.start 	= DAVINCI_DM646X_MCASP0_REG_BASE,
		.end 	= DAVINCI_DM646X_MCASP0_REG_BASE + (SZ_1K << 1) - 1,
		.flags 	= IORESOURCE_MEM,
	},
	/* first TX, then RX */
	{
		.start	= DAVINCI_DM646X_DMA_MCASP0_AXEVT0,
		.end	= DAVINCI_DM646X_DMA_MCASP0_AXEVT0,
		.flags	= IORESOURCE_DMA,
	},
	{
		.start	= DAVINCI_DM646X_DMA_MCASP0_AREVT0,
		.end	= DAVINCI_DM646X_DMA_MCASP0_AREVT0,
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource dm646x_mcasp1_resources[] = {
	{
		.name	= "mcasp1",
		.start	= DAVINCI_DM646X_MCASP1_REG_BASE,
		.end	= DAVINCI_DM646X_MCASP1_REG_BASE + (SZ_1K << 1) - 1,
		.flags	= IORESOURCE_MEM,
	},
	/* DIT mode, only TX event */
	{
		.start	= DAVINCI_DM646X_DMA_MCASP1_AXEVT1,
		.end	= DAVINCI_DM646X_DMA_MCASP1_AXEVT1,
		.flags	= IORESOURCE_DMA,
	},
	/* DIT mode, dummy entry */
	{
		.start	= -1,
		.end	= -1,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device dm646x_mcasp0_device = {
	.name		= "davinci-mcasp",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm646x_mcasp0_resources),
	.resource	= dm646x_mcasp0_resources,
};

static struct platform_device dm646x_mcasp1_device = {
	.name		= "davinci-mcasp",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(dm646x_mcasp1_resources),
	.resource	= dm646x_mcasp1_resources,
};

static struct platform_device dm646x_dit_device = {
	.name	= "spdif-dit",
	.id	= -1,
};

static u64 vpif_dma_mask = DMA_BIT_MASK(32);

static struct resource vpif_resource[] = {
	{
		.start	= DAVINCI_VPIF_BASE,
		.end	= DAVINCI_VPIF_BASE + 0x03ff,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device vpif_dev = {
	.name		= "vpif",
	.id		= -1,
	.dev		= {
			.dma_mask 		= &vpif_dma_mask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= vpif_resource,
	.num_resources	= ARRAY_SIZE(vpif_resource),
};

static struct resource vpif_display_resource[] = {
	{
		.start = IRQ_DM646X_VP_VERTINT2,
		.end   = IRQ_DM646X_VP_VERTINT2,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_DM646X_VP_VERTINT3,
		.end   = IRQ_DM646X_VP_VERTINT3,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device vpif_display_dev = {
	.name		= "vpif_display",
	.id		= -1,
	.dev		= {
			.dma_mask 		= &vpif_dma_mask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= vpif_display_resource,
	.num_resources	= ARRAY_SIZE(vpif_display_resource),
};

static struct resource vpif_capture_resource[] = {
	{
		.start = IRQ_DM646X_VP_VERTINT0,
		.end   = IRQ_DM646X_VP_VERTINT0,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = IRQ_DM646X_VP_VERTINT1,
		.end   = IRQ_DM646X_VP_VERTINT1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device vpif_capture_dev = {
	.name		= "vpif_capture",
	.id		= -1,
	.dev		= {
			.dma_mask 		= &vpif_dma_mask,
			.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= vpif_capture_resource,
	.num_resources	= ARRAY_SIZE(vpif_capture_resource),
};

/*----------------------------------------------------------------------*/

static struct map_desc dm646x_io_desc[] = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= SRAM_VIRT,
		.pfn		= __phys_to_pfn(0x00010000),
		.length		= SZ_32K,
		.type		= MT_MEMORY_NONCACHED,
	},
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id dm646x_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb770,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DM6467,
		.name		= "dm6467_rev1.x",
	},
	{
		.variant	= 0x1,
		.part_no	= 0xb770,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DM6467,
		.name		= "dm6467_rev3.x",
	},
};

static u32 dm646x_psc_bases[] = { DAVINCI_PWR_SLEEP_CNTRL_BASE };

/*
 * T0_BOT: Timer 0, bottom:  clockevent source for hrtimers
 * T0_TOP: Timer 0, top   :  clocksource for generic timekeeping
 * T1_BOT: Timer 1, bottom:  (used by DSP in TI DSPLink code)
 * T1_TOP: Timer 1, top   :  <unused>
 */
static struct davinci_timer_info dm646x_timer_info = {
	.timers		= davinci_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

static struct plat_serial8250_port dm646x_serial_platform_data[] = {
	{
		.mapbase	= DAVINCI_UART0_BASE,
		.irq		= IRQ_UARTINT0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	},
	{
		.mapbase	= DAVINCI_UART1_BASE,
		.irq		= IRQ_UARTINT1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	},
	{
		.mapbase	= DAVINCI_UART2_BASE,
		.irq		= IRQ_DM646X_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
	},
	{
		.flags		= 0
	},
};

static struct platform_device dm646x_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= dm646x_serial_platform_data,
	},
};

static struct davinci_soc_info davinci_soc_info_dm646x = {
	.io_desc		= dm646x_io_desc,
	.io_desc_num		= ARRAY_SIZE(dm646x_io_desc),
	.jtag_id_reg		= 0x01c40028,
	.ids			= dm646x_ids,
	.ids_num		= ARRAY_SIZE(dm646x_ids),
	.cpu_clks		= dm646x_clks,
	.psc_bases		= dm646x_psc_bases,
	.psc_bases_num		= ARRAY_SIZE(dm646x_psc_bases),
	.pinmux_base		= DAVINCI_SYSTEM_MODULE_BASE,
	.pinmux_pins		= dm646x_pins,
	.pinmux_pins_num	= ARRAY_SIZE(dm646x_pins),
	.intc_base		= DAVINCI_ARM_INTC_BASE,
	.intc_type		= DAVINCI_INTC_TYPE_AINTC,
	.intc_irq_prios		= dm646x_default_priorities,
	.intc_irq_num		= DAVINCI_N_AINTC_IRQ,
	.timer_info		= &dm646x_timer_info,
	.gpio_type		= GPIO_TYPE_DAVINCI,
	.gpio_base		= DAVINCI_GPIO_BASE,
	.gpio_num		= 43, /* Only 33 usable */
	.gpio_irq		= IRQ_DM646X_GPIOBNK0,
	.serial_dev		= &dm646x_serial_device,
	.emac_pdata		= &dm646x_emac_pdata,
	.sram_dma		= 0x10010000,
	.sram_len		= SZ_32K,
	.reset_device		= &davinci_wdt_device,
};

void __init dm646x_init_mcasp0(struct snd_platform_data *pdata)
{
	dm646x_mcasp0_device.dev.platform_data = pdata;
	platform_device_register(&dm646x_mcasp0_device);
}

void __init dm646x_init_mcasp1(struct snd_platform_data *pdata)
{
	dm646x_mcasp1_device.dev.platform_data = pdata;
	platform_device_register(&dm646x_mcasp1_device);
	platform_device_register(&dm646x_dit_device);
}

void dm646x_setup_vpif(struct vpif_display_config *display_config,
		       struct vpif_capture_config *capture_config)
{
	unsigned int value;
	void __iomem *base = IO_ADDRESS(DAVINCI_SYSTEM_MODULE_BASE);

	value = __raw_readl(base + VSCLKDIS_OFFSET);
	value &= ~VSCLKDIS_MASK;
	__raw_writel(value, base + VSCLKDIS_OFFSET);

	value = __raw_readl(base + VDD3P3V_PWDN_OFFSET);
	value &= ~VDD3P3V_VID_MASK;
	__raw_writel(value, base + VDD3P3V_PWDN_OFFSET);

	davinci_cfg_reg(DM646X_STSOMUX_DISABLE);
	davinci_cfg_reg(DM646X_STSIMUX_DISABLE);
	davinci_cfg_reg(DM646X_PTSOMUX_DISABLE);
	davinci_cfg_reg(DM646X_PTSIMUX_DISABLE);

	vpif_display_dev.dev.platform_data = display_config;
	vpif_capture_dev.dev.platform_data = capture_config;
	platform_device_register(&vpif_dev);
	platform_device_register(&vpif_display_dev);
	platform_device_register(&vpif_capture_dev);
}

int __init dm646x_init_edma(struct edma_rsv_info *rsv)
{
	edma_cc0_info.rsv = rsv;

	return platform_device_register(&dm646x_edma_device);
}

void __init dm646x_init(void)
{
	davinci_common_init(&davinci_soc_info_dm646x);
}

static int __init dm646x_init_devices(void)
{
	if (!cpu_is_davinci_dm646x())
		return 0;

	platform_device_register(&dm646x_mdio_device);
	platform_device_register(&dm646x_emac_device);
	clk_add_alias(NULL, dev_name(&dm646x_mdio_device.dev),
		      NULL, &dm646x_emac_device.dev);

	return 0;
}
postcore_initcall(dm646x_init_devices);
