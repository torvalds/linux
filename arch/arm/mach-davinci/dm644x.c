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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <mach/dm644x.h>
#include <mach/clock.h>
#include <mach/cputype.h>
#include <mach/edma.h>
#include <mach/irqs.h>
#include <mach/psc.h>
#include <mach/mux.h>

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
	CLK("soc-audio.0", NULL, &asp_clk),
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

#if defined(CONFIG_TI_DAVINCI_EMAC) || defined(CONFIG_TI_DAVINCI_EMAC_MODULE)

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
       .num_resources	= ARRAY_SIZE(dm644x_emac_resources),
       .resource	= dm644x_emac_resources,
};

#endif

/*
 * Device specific mux setup
 *
 *	soc	description	mux  mode   mode  mux	 dbg
 *				reg  offset mask  mode
 */
static const struct mux_config dm644x_pins[] = {
MUX_CFG(DM644X, HDIREN,		0,   16,    1,	  1,	 true)
MUX_CFG(DM644X, ATAEN,		0,   17,    1,	  1,	 true)
MUX_CFG(DM644X, ATAEN_DISABLE,	0,   17,    1,	  0,	 true)

MUX_CFG(DM644X, HPIEN_DISABLE,	0,   29,    1,	  0,	 true)

MUX_CFG(DM644X, AEAW,		0,   0,     31,	  31,	 true)

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

static struct edma_soc_info dm644x_edma_info = {
	.n_channel	= 64,
	.n_region	= 4,
	.n_slot		= 128,
	.n_tc		= 2,
	.noevent	= dma_chan_dm644x_no_event,
};

static struct resource edma_resources[] = {
	{
		.name	= "edma_cc",
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
		.start	= IRQ_CCINT0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= IRQ_CCERRINT,
		.flags	= IORESOURCE_IRQ,
	},
	/* not using TC*_ERR */
};

static struct platform_device dm644x_edma_device = {
	.name			= "edma",
	.id			= -1,
	.dev.platform_data	= &dm644x_edma_info,
	.num_resources		= ARRAY_SIZE(edma_resources),
	.resource		= edma_resources,
};

/*----------------------------------------------------------------------*/
void __init dm644x_init(void)
{
	davinci_clk_init(dm644x_clks);
	davinci_mux_register(dm644x_pins, ARRAY_SIZE(dm644x_pins));
}

static int __init dm644x_init_devices(void)
{
	if (!cpu_is_davinci_dm644x())
		return 0;

	platform_device_register(&dm644x_edma_device);
	return 0;
}
postcore_initcall(dm644x_init_devices);
