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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>

#include <linux/spi/spi.h>

#include <asm/mach/map.h>

#include <mach/dm355.h>
#include <mach/clock.h>
#include <mach/cputype.h>
#include <mach/edma.h>
#include <mach/psc.h>
#include <mach/mux.h>
#include <mach/irqs.h>
#include <mach/time.h>
#include <mach/serial.h>
#include <mach/common.h>

#include "clock.h"
#include "mux.h"

#define DM355_UART2_BASE	(IO_PHYS + 0x206000)

/*
 * Device specific clocks
 */
#define DM355_REF_FREQ		24000000	/* 24 or 36 MHz */

static struct pll_data pll1_data = {
	.num       = 1,
	.phys_base = DAVINCI_PLL1_BASE,
	.flags     = PLL_HAS_PREDIV | PLL_HAS_POSTDIV,
};

static struct pll_data pll2_data = {
	.num       = 2,
	.phys_base = DAVINCI_PLL2_BASE,
	.flags     = PLL_HAS_PREDIV,
};

static struct clk ref_clk = {
	.name = "ref_clk",
	/* FIXME -- crystal rate is board-specific */
	.rate = DM355_REF_FREQ,
};

static struct clk pll1_clk = {
	.name = "pll1",
	.parent = &ref_clk,
	.flags = CLK_PLL,
	.pll_data = &pll1_data,
};

static struct clk pll1_aux_clk = {
	.name = "pll1_aux_clk",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
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

static struct clk pll1_sysclkbp = {
	.name = "pll1_sysclkbp",
	.parent = &pll1_clk,
	.flags = CLK_PLL | PRE_PLL,
	.div_reg = BPDIV
};

static struct clk vpss_dac_clk = {
	.name = "vpss_dac",
	.parent = &pll1_sysclk3,
	.lpsc = DM355_LPSC_VPSS_DAC,
};

static struct clk vpss_master_clk = {
	.name = "vpss_master",
	.parent = &pll1_sysclk4,
	.lpsc = DAVINCI_LPSC_VPSSMSTR,
	.flags = CLK_PSC,
};

static struct clk vpss_slave_clk = {
	.name = "vpss_slave",
	.parent = &pll1_sysclk4,
	.lpsc = DAVINCI_LPSC_VPSSSLV,
};


static struct clk clkout1_clk = {
	.name = "clkout1",
	.parent = &pll1_aux_clk,
	/* NOTE:  clkout1 can be externally gated by muxing GPIO-18 */
};

static struct clk clkout2_clk = {
	.name = "clkout2",
	.parent = &pll1_sysclkbp,
};

static struct clk pll2_clk = {
	.name = "pll2",
	.parent = &ref_clk,
	.flags = CLK_PLL,
	.pll_data = &pll2_data,
};

static struct clk pll2_sysclk1 = {
	.name = "pll2_sysclk1",
	.parent = &pll2_clk,
	.flags = CLK_PLL,
	.div_reg = PLLDIV1,
};

static struct clk pll2_sysclkbp = {
	.name = "pll2_sysclkbp",
	.parent = &pll2_clk,
	.flags = CLK_PLL | PRE_PLL,
	.div_reg = BPDIV
};

static struct clk clkout3_clk = {
	.name = "clkout3",
	.parent = &pll2_sysclkbp,
	/* NOTE:  clkout3 can be externally gated by muxing GPIO-16 */
};

static struct clk arm_clk = {
	.name = "arm_clk",
	.parent = &pll1_sysclk1,
	.lpsc = DAVINCI_LPSC_ARM,
	.flags = ALWAYS_ENABLED,
};

/*
 * NOT LISTED below, and not touched by Linux
 *   - in SyncReset state by default
 *	.lpsc = DAVINCI_LPSC_TPCC,
 *	.lpsc = DAVINCI_LPSC_TPTC0,
 *	.lpsc = DAVINCI_LPSC_TPTC1,
 *	.lpsc = DAVINCI_LPSC_DDR_EMIF, .parent = &sysclk2_clk,
 *	.lpsc = DAVINCI_LPSC_MEMSTICK,
 *   - in Enabled state by default
 *	.lpsc = DAVINCI_LPSC_SYSTEM_SUBSYS,
 *	.lpsc = DAVINCI_LPSC_SCR2,	// "bus"
 *	.lpsc = DAVINCI_LPSC_SCR3,	// "bus"
 *	.lpsc = DAVINCI_LPSC_SCR4,	// "bus"
 *	.lpsc = DAVINCI_LPSC_CROSSBAR,	// "emulation"
 *	.lpsc = DAVINCI_LPSC_CFG27,	// "test"
 *	.lpsc = DAVINCI_LPSC_CFG3,	// "test"
 *	.lpsc = DAVINCI_LPSC_CFG5,	// "test"
 */

static struct clk mjcp_clk = {
	.name = "mjcp",
	.parent = &pll1_sysclk1,
	.lpsc = DAVINCI_LPSC_IMCOP,
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
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_UART2,
};

static struct clk i2c_clk = {
	.name = "i2c",
	.parent = &pll1_aux_clk,
	.lpsc = DAVINCI_LPSC_I2C,
};

static struct clk asp0_clk = {
	.name = "asp0",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_McBSP,
};

static struct clk asp1_clk = {
	.name = "asp1",
	.parent = &pll1_sysclk2,
	.lpsc = DM355_LPSC_McBSP1,
};

static struct clk mmcsd0_clk = {
	.name = "mmcsd0",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_MMC_SD,
};

static struct clk mmcsd1_clk = {
	.name = "mmcsd1",
	.parent = &pll1_sysclk2,
	.lpsc = DM355_LPSC_MMC_SD1,
};

static struct clk spi0_clk = {
	.name = "spi0",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_SPI,
};

static struct clk spi1_clk = {
	.name = "spi1",
	.parent = &pll1_sysclk2,
	.lpsc = DM355_LPSC_SPI1,
};

static struct clk spi2_clk = {
	.name = "spi2",
	.parent = &pll1_sysclk2,
	.lpsc = DM355_LPSC_SPI2,
};

static struct clk gpio_clk = {
	.name = "gpio",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_GPIO,
};

static struct clk aemif_clk = {
	.name = "aemif",
	.parent = &pll1_sysclk2,
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

static struct clk pwm3_clk = {
	.name = "pwm3",
	.parent = &pll1_aux_clk,
	.lpsc = DM355_LPSC_PWM3,
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

static struct clk timer3_clk = {
	.name = "timer3",
	.parent = &pll1_aux_clk,
	.lpsc = DM355_LPSC_TIMER3,
};

static struct clk rto_clk = {
	.name = "rto",
	.parent = &pll1_aux_clk,
	.lpsc = DM355_LPSC_RTO,
};

static struct clk usb_clk = {
	.name = "usb",
	.parent = &pll1_sysclk2,
	.lpsc = DAVINCI_LPSC_USB,
};

static struct davinci_clk dm355_clks[] = {
	CLK(NULL, "ref", &ref_clk),
	CLK(NULL, "pll1", &pll1_clk),
	CLK(NULL, "pll1_sysclk1", &pll1_sysclk1),
	CLK(NULL, "pll1_sysclk2", &pll1_sysclk2),
	CLK(NULL, "pll1_sysclk3", &pll1_sysclk3),
	CLK(NULL, "pll1_sysclk4", &pll1_sysclk4),
	CLK(NULL, "pll1_aux", &pll1_aux_clk),
	CLK(NULL, "pll1_sysclkbp", &pll1_sysclkbp),
	CLK(NULL, "vpss_dac", &vpss_dac_clk),
	CLK(NULL, "vpss_master", &vpss_master_clk),
	CLK(NULL, "vpss_slave", &vpss_slave_clk),
	CLK(NULL, "clkout1", &clkout1_clk),
	CLK(NULL, "clkout2", &clkout2_clk),
	CLK(NULL, "pll2", &pll2_clk),
	CLK(NULL, "pll2_sysclk1", &pll2_sysclk1),
	CLK(NULL, "pll2_sysclkbp", &pll2_sysclkbp),
	CLK(NULL, "clkout3", &clkout3_clk),
	CLK(NULL, "arm", &arm_clk),
	CLK(NULL, "mjcp", &mjcp_clk),
	CLK(NULL, "uart0", &uart0_clk),
	CLK(NULL, "uart1", &uart1_clk),
	CLK(NULL, "uart2", &uart2_clk),
	CLK("i2c_davinci.1", NULL, &i2c_clk),
	CLK("soc-audio.0", NULL, &asp0_clk),
	CLK("soc-audio.1", NULL, &asp1_clk),
	CLK("davinci_mmc.0", NULL, &mmcsd0_clk),
	CLK("davinci_mmc.1", NULL, &mmcsd1_clk),
	CLK(NULL, "spi0", &spi0_clk),
	CLK(NULL, "spi1", &spi1_clk),
	CLK(NULL, "spi2", &spi2_clk),
	CLK(NULL, "gpio", &gpio_clk),
	CLK(NULL, "aemif", &aemif_clk),
	CLK(NULL, "pwm0", &pwm0_clk),
	CLK(NULL, "pwm1", &pwm1_clk),
	CLK(NULL, "pwm2", &pwm2_clk),
	CLK(NULL, "pwm3", &pwm3_clk),
	CLK(NULL, "timer0", &timer0_clk),
	CLK(NULL, "timer1", &timer1_clk),
	CLK("watchdog", NULL, &timer2_clk),
	CLK(NULL, "timer3", &timer3_clk),
	CLK(NULL, "rto", &rto_clk),
	CLK(NULL, "usb", &usb_clk),
	CLK(NULL, NULL, NULL),
};

/*----------------------------------------------------------------------*/

static u64 dm355_spi0_dma_mask = DMA_BIT_MASK(32);

static struct resource dm355_spi0_resources[] = {
	{
		.start = 0x01c66000,
		.end   = 0x01c667ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_DM355_SPINT0_1,
		.flags = IORESOURCE_IRQ,
	},
	/* Not yet used, so not included:
	 * IORESOURCE_IRQ:
	 *  - IRQ_DM355_SPINT0_0
	 * IORESOURCE_DMA:
	 *  - DAVINCI_DMA_SPI_SPIX
	 *  - DAVINCI_DMA_SPI_SPIR
	 */
};

static struct platform_device dm355_spi0_device = {
	.name = "spi_davinci",
	.id = 0,
	.dev = {
		.dma_mask = &dm355_spi0_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources = ARRAY_SIZE(dm355_spi0_resources),
	.resource = dm355_spi0_resources,
};

void __init dm355_init_spi0(unsigned chipselect_mask,
		struct spi_board_info *info, unsigned len)
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

#define PINMUX0		0x00
#define PINMUX1		0x04
#define PINMUX2		0x08
#define PINMUX3		0x0c
#define PINMUX4		0x10
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

static const s8 dma_chan_dm355_no_event[] = {
	12, 13, 24, 56, 57,
	58, 59, 60, 61, 62,
	63,
	-1
};

static struct edma_soc_info dm355_edma_info = {
	.n_channel	= 64,
	.n_region	= 4,
	.n_slot		= 128,
	.n_tc		= 2,
	.noevent	= dma_chan_dm355_no_event,
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
	/* not using (or muxing) TC*_ERR */
};

static struct platform_device dm355_edma_device = {
	.name			= "edma",
	.id			= -1,
	.dev.platform_data	= &dm355_edma_info,
	.num_resources		= ARRAY_SIZE(edma_resources),
	.resource		= edma_resources,
};

/*----------------------------------------------------------------------*/

static struct map_desc dm355_io_desc[] = {
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
		/* MT_MEMORY_NONCACHED requires supersection alignment */
		.type		= MT_DEVICE,
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

static void __iomem *dm355_psc_bases[] = {
	IO_ADDRESS(DAVINCI_PWR_SLEEP_CNTRL_BASE),
};

/*
 * T0_BOT: Timer 0, bottom:  clockevent source for hrtimers
 * T0_TOP: Timer 0, top   :  clocksource for generic timekeeping
 * T1_BOT: Timer 1, bottom:  (used by DSP in TI DSPLink code)
 * T1_TOP: Timer 1, top   :  <unused>
 */
struct davinci_timer_info dm355_timer_info = {
	.timers		= davinci_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

static struct plat_serial8250_port dm355_serial_platform_data[] = {
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
		.mapbase	= DM355_UART2_BASE,
		.irq		= IRQ_DM355_UARTINT2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	},
	{
		.flags		= 0
	},
};

static struct platform_device dm355_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= dm355_serial_platform_data,
	},
};

static struct davinci_soc_info davinci_soc_info_dm355 = {
	.io_desc		= dm355_io_desc,
	.io_desc_num		= ARRAY_SIZE(dm355_io_desc),
	.jtag_id_base		= IO_ADDRESS(0x01c40028),
	.ids			= dm355_ids,
	.ids_num		= ARRAY_SIZE(dm355_ids),
	.cpu_clks		= dm355_clks,
	.psc_bases		= dm355_psc_bases,
	.psc_bases_num		= ARRAY_SIZE(dm355_psc_bases),
	.pinmux_base		= IO_ADDRESS(DAVINCI_SYSTEM_MODULE_BASE),
	.pinmux_pins		= dm355_pins,
	.pinmux_pins_num	= ARRAY_SIZE(dm355_pins),
	.intc_base		= IO_ADDRESS(DAVINCI_ARM_INTC_BASE),
	.intc_type		= DAVINCI_INTC_TYPE_AINTC,
	.intc_irq_prios		= dm355_default_priorities,
	.intc_irq_num		= DAVINCI_N_AINTC_IRQ,
	.timer_info		= &dm355_timer_info,
	.wdt_base		= IO_ADDRESS(DAVINCI_WDOG_BASE),
	.gpio_base		= IO_ADDRESS(DAVINCI_GPIO_BASE),
	.gpio_num		= 104,
	.gpio_irq		= IRQ_DM355_GPIOBNK0,
	.serial_dev		= &dm355_serial_device,
	.sram_dma		= 0x00010000,
	.sram_len		= SZ_32K,
};

void __init dm355_init(void)
{
	davinci_common_init(&davinci_soc_info_dm355);
}

static int __init dm355_init_devices(void)
{
	if (!cpu_is_davinci_dm355())
		return 0;

	davinci_cfg_reg(DM355_INT_EDMA_CC);
	platform_device_register(&dm355_edma_device);
	return 0;
}
postcore_initcall(dm355_init_devices);
