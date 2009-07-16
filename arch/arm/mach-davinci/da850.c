/*
 * TI DA850/OMAP-L138 chip specific setup
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Derived from: arch/arm/mach-davinci/da830.c
 * Original Copyrights follow:
 *
 * 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <asm/mach/map.h>

#include <mach/clock.h>
#include <mach/psc.h>
#include <mach/mux.h>
#include <mach/irqs.h>
#include <mach/cputype.h>
#include <mach/common.h>
#include <mach/time.h>
#include <mach/da8xx.h>

#include "clock.h"
#include "mux.h"

#define DA850_PLL1_BASE		0x01e1a000
#define DA850_TIMER64P2_BASE	0x01f0c000
#define DA850_TIMER64P3_BASE	0x01f0d000

#define DA850_REF_FREQ		24000000

static struct pll_data pll0_data = {
	.num		= 1,
	.phys_base	= DA8XX_PLL0_BASE,
	.flags		= PLL_HAS_PREDIV | PLL_HAS_POSTDIV,
};

static struct clk ref_clk = {
	.name		= "ref_clk",
	.rate		= DA850_REF_FREQ,
};

static struct clk pll0_clk = {
	.name		= "pll0",
	.parent		= &ref_clk,
	.pll_data	= &pll0_data,
	.flags		= CLK_PLL,
};

static struct clk pll0_aux_clk = {
	.name		= "pll0_aux_clk",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL | PRE_PLL,
};

static struct clk pll0_sysclk2 = {
	.name		= "pll0_sysclk2",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV2,
};

static struct clk pll0_sysclk3 = {
	.name		= "pll0_sysclk3",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV3,
};

static struct clk pll0_sysclk4 = {
	.name		= "pll0_sysclk4",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV4,
};

static struct clk pll0_sysclk5 = {
	.name		= "pll0_sysclk5",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV5,
};

static struct clk pll0_sysclk6 = {
	.name		= "pll0_sysclk6",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV6,
};

static struct clk pll0_sysclk7 = {
	.name		= "pll0_sysclk7",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV7,
};

static struct pll_data pll1_data = {
	.num		= 2,
	.phys_base	= DA850_PLL1_BASE,
	.flags		= PLL_HAS_POSTDIV,
};

static struct clk pll1_clk = {
	.name		= "pll1",
	.parent		= &ref_clk,
	.pll_data	= &pll1_data,
	.flags		= CLK_PLL,
};

static struct clk pll1_aux_clk = {
	.name		= "pll1_aux_clk",
	.parent		= &pll1_clk,
	.flags		= CLK_PLL | PRE_PLL,
};

static struct clk pll1_sysclk2 = {
	.name		= "pll1_sysclk2",
	.parent		= &pll1_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV2,
};

static struct clk pll1_sysclk3 = {
	.name		= "pll1_sysclk3",
	.parent		= &pll1_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV3,
};

static struct clk pll1_sysclk4 = {
	.name		= "pll1_sysclk4",
	.parent		= &pll1_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV4,
};

static struct clk pll1_sysclk5 = {
	.name		= "pll1_sysclk5",
	.parent		= &pll1_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV5,
};

static struct clk pll1_sysclk6 = {
	.name		= "pll0_sysclk6",
	.parent		= &pll0_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV6,
};

static struct clk pll1_sysclk7 = {
	.name		= "pll1_sysclk7",
	.parent		= &pll1_clk,
	.flags		= CLK_PLL,
	.div_reg	= PLLDIV7,
};

static struct clk i2c0_clk = {
	.name		= "i2c0",
	.parent		= &pll0_aux_clk,
};

static struct clk timerp64_0_clk = {
	.name		= "timer0",
	.parent		= &pll0_aux_clk,
};

static struct clk timerp64_1_clk = {
	.name		= "timer1",
	.parent		= &pll0_aux_clk,
};

static struct clk arm_rom_clk = {
	.name		= "arm_rom",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_ARM_RAM_ROM,
	.flags		= ALWAYS_ENABLED,
};

static struct clk tpcc0_clk = {
	.name		= "tpcc0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_TPCC,
	.flags		= ALWAYS_ENABLED | CLK_PSC,
};

static struct clk tptc0_clk = {
	.name		= "tptc0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_TPTC0,
	.flags		= ALWAYS_ENABLED,
};

static struct clk tptc1_clk = {
	.name		= "tptc1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_TPTC1,
	.flags		= ALWAYS_ENABLED,
};

static struct clk tpcc1_clk = {
	.name		= "tpcc1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA850_LPSC1_TPCC1,
	.flags		= CLK_PSC | ALWAYS_ENABLED,
	.psc_ctlr	= 1,
};

static struct clk tptc2_clk = {
	.name		= "tptc2",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA850_LPSC1_TPTC2,
	.flags		= ALWAYS_ENABLED,
	.psc_ctlr	= 1,
};

static struct clk uart0_clk = {
	.name		= "uart0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_UART0,
};

static struct clk uart1_clk = {
	.name		= "uart1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_UART1,
	.psc_ctlr	= 1,
};

static struct clk uart2_clk = {
	.name		= "uart2",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_UART2,
	.psc_ctlr	= 1,
};

static struct clk aintc_clk = {
	.name		= "aintc",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC0_AINTC,
	.flags		= ALWAYS_ENABLED,
};

static struct clk gpio_clk = {
	.name		= "gpio",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC1_GPIO,
	.psc_ctlr	= 1,
};

static struct clk i2c1_clk = {
	.name		= "i2c1",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC1_I2C,
	.psc_ctlr	= 1,
};

static struct clk emif3_clk = {
	.name		= "emif3",
	.parent		= &pll0_sysclk5,
	.lpsc		= DA8XX_LPSC1_EMIF3C,
	.flags		= ALWAYS_ENABLED,
	.psc_ctlr	= 1,
};

static struct clk arm_clk = {
	.name		= "arm",
	.parent		= &pll0_sysclk6,
	.lpsc		= DA8XX_LPSC0_ARM,
	.flags		= ALWAYS_ENABLED,
};

static struct clk rmii_clk = {
	.name		= "rmii",
	.parent		= &pll0_sysclk7,
};

static struct davinci_clk da850_clks[] = {
	CLK(NULL,		"ref",		&ref_clk),
	CLK(NULL,		"pll0",		&pll0_clk),
	CLK(NULL,		"pll0_aux",	&pll0_aux_clk),
	CLK(NULL,		"pll0_sysclk2",	&pll0_sysclk2),
	CLK(NULL,		"pll0_sysclk3",	&pll0_sysclk3),
	CLK(NULL,		"pll0_sysclk4",	&pll0_sysclk4),
	CLK(NULL,		"pll0_sysclk5",	&pll0_sysclk5),
	CLK(NULL,		"pll0_sysclk6",	&pll0_sysclk6),
	CLK(NULL,		"pll0_sysclk7",	&pll0_sysclk7),
	CLK(NULL,		"pll1",		&pll1_clk),
	CLK(NULL,		"pll1_aux",	&pll1_aux_clk),
	CLK(NULL,		"pll1_sysclk2",	&pll1_sysclk2),
	CLK(NULL,		"pll1_sysclk3",	&pll1_sysclk3),
	CLK(NULL,		"pll1_sysclk4",	&pll1_sysclk4),
	CLK(NULL,		"pll1_sysclk5",	&pll1_sysclk5),
	CLK(NULL,		"pll1_sysclk6",	&pll1_sysclk6),
	CLK(NULL,		"pll1_sysclk7",	&pll1_sysclk7),
	CLK("i2c_davinci.1",	NULL,		&i2c0_clk),
	CLK(NULL,		"timer0",	&timerp64_0_clk),
	CLK("watchdog",		NULL,		&timerp64_1_clk),
	CLK(NULL,		"arm_rom",	&arm_rom_clk),
	CLK(NULL,		"tpcc0",	&tpcc0_clk),
	CLK(NULL,		"tptc0",	&tptc0_clk),
	CLK(NULL,		"tptc1",	&tptc1_clk),
	CLK(NULL,		"tpcc1",	&tpcc1_clk),
	CLK(NULL,		"tptc2",	&tptc2_clk),
	CLK(NULL,		"uart0",	&uart0_clk),
	CLK(NULL,		"uart1",	&uart1_clk),
	CLK(NULL,		"uart2",	&uart2_clk),
	CLK(NULL,		"aintc",	&aintc_clk),
	CLK(NULL,		"gpio",		&gpio_clk),
	CLK("i2c_davinci.2",	NULL,		&i2c1_clk),
	CLK(NULL,		"emif3",	&emif3_clk),
	CLK(NULL,		"arm",		&arm_clk),
	CLK(NULL,		"rmii",		&rmii_clk),
	CLK(NULL,		NULL,		NULL),
};

/*
 * Device specific mux setup
 *
 *		soc	description	mux	mode	mode	mux	dbg
 *					reg	offset	mask	mode
 */
static const struct mux_config da850_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
	/* UART0 function */
	MUX_CFG(DA850, NUART0_CTS,	3,	24,	15,	2,	false)
	MUX_CFG(DA850, NUART0_RTS,	3,	28,	15,	2,	false)
	MUX_CFG(DA850, UART0_RXD,	3,	16,	15,	2,	false)
	MUX_CFG(DA850, UART0_TXD,	3,	20,	15,	2,	false)
	/* UART1 function */
	MUX_CFG(DA850, UART1_RXD,	4,	24,	15,	2,	false)
	MUX_CFG(DA850, UART1_TXD,	4,	28,	15,	2,	false)
	/* UART2 function */
	MUX_CFG(DA850, UART2_RXD,	4,	16,	15,	2,	false)
	MUX_CFG(DA850, UART2_TXD,	4,	20,	15,	2,	false)
	/* I2C1 function */
	MUX_CFG(DA850, I2C1_SCL,	4,	16,	15,	4,	false)
	MUX_CFG(DA850, I2C1_SDA,	4,	20,	15,	4,	false)
	/* I2C0 function */
	MUX_CFG(DA850, I2C0_SDA,	4,	12,	15,	2,	false)
	MUX_CFG(DA850, I2C0_SCL,	4,	8,	15,	2,	false)
#endif
};

const short da850_uart0_pins[] __initdata = {
	DA850_NUART0_CTS, DA850_NUART0_RTS, DA850_UART0_RXD, DA850_UART0_TXD,
	-1
};

const short da850_uart1_pins[] __initdata = {
	DA850_UART1_RXD, DA850_UART1_TXD,
	-1
};

const short da850_uart2_pins[] __initdata = {
	DA850_UART2_RXD, DA850_UART2_TXD,
	-1
};

const short da850_i2c0_pins[] __initdata = {
	DA850_I2C0_SDA, DA850_I2C0_SCL,
	-1
};

const short da850_i2c1_pins[] __initdata = {
	DA850_I2C1_SCL, DA850_I2C1_SDA,
	-1
};

/* FIQ are pri 0-1; otherwise 2-7, with 7 lowest priority */
static u8 da850_default_priorities[DA850_N_CP_INTC_IRQ] = {
	[IRQ_DA8XX_COMMTX]		= 7,
	[IRQ_DA8XX_COMMRX]		= 7,
	[IRQ_DA8XX_NINT]		= 7,
	[IRQ_DA8XX_EVTOUT0]		= 7,
	[IRQ_DA8XX_EVTOUT1]		= 7,
	[IRQ_DA8XX_EVTOUT2]		= 7,
	[IRQ_DA8XX_EVTOUT3]		= 7,
	[IRQ_DA8XX_EVTOUT4]		= 7,
	[IRQ_DA8XX_EVTOUT5]		= 7,
	[IRQ_DA8XX_EVTOUT6]		= 7,
	[IRQ_DA8XX_EVTOUT6]		= 7,
	[IRQ_DA8XX_EVTOUT7]		= 7,
	[IRQ_DA8XX_CCINT0]		= 7,
	[IRQ_DA8XX_CCERRINT]		= 7,
	[IRQ_DA8XX_TCERRINT0]		= 7,
	[IRQ_DA8XX_AEMIFINT]		= 7,
	[IRQ_DA8XX_I2CINT0]		= 7,
	[IRQ_DA8XX_MMCSDINT0]		= 7,
	[IRQ_DA8XX_MMCSDINT1]		= 7,
	[IRQ_DA8XX_ALLINT0]		= 7,
	[IRQ_DA8XX_RTC]			= 7,
	[IRQ_DA8XX_SPINT0]		= 7,
	[IRQ_DA8XX_TINT12_0]		= 7,
	[IRQ_DA8XX_TINT34_0]		= 7,
	[IRQ_DA8XX_TINT12_1]		= 7,
	[IRQ_DA8XX_TINT34_1]		= 7,
	[IRQ_DA8XX_UARTINT0]		= 7,
	[IRQ_DA8XX_KEYMGRINT]		= 7,
	[IRQ_DA8XX_SECINT]		= 7,
	[IRQ_DA8XX_SECKEYERR]		= 7,
	[IRQ_DA850_MPUADDRERR0]		= 7,
	[IRQ_DA850_MPUPROTERR0]		= 7,
	[IRQ_DA850_IOPUADDRERR0]	= 7,
	[IRQ_DA850_IOPUPROTERR0]	= 7,
	[IRQ_DA850_IOPUADDRERR1]	= 7,
	[IRQ_DA850_IOPUPROTERR1]	= 7,
	[IRQ_DA850_IOPUADDRERR2]	= 7,
	[IRQ_DA850_IOPUPROTERR2]	= 7,
	[IRQ_DA850_BOOTCFG_ADDR_ERR]	= 7,
	[IRQ_DA850_BOOTCFG_PROT_ERR]	= 7,
	[IRQ_DA850_MPUADDRERR1]		= 7,
	[IRQ_DA850_MPUPROTERR1]		= 7,
	[IRQ_DA850_IOPUADDRERR3]	= 7,
	[IRQ_DA850_IOPUPROTERR3]	= 7,
	[IRQ_DA850_IOPUADDRERR4]	= 7,
	[IRQ_DA850_IOPUPROTERR4]	= 7,
	[IRQ_DA850_IOPUADDRERR5]	= 7,
	[IRQ_DA850_IOPUPROTERR5]	= 7,
	[IRQ_DA850_MIOPU_BOOTCFG_ERR]	= 7,
	[IRQ_DA8XX_CHIPINT0]		= 7,
	[IRQ_DA8XX_CHIPINT1]		= 7,
	[IRQ_DA8XX_CHIPINT2]		= 7,
	[IRQ_DA8XX_CHIPINT3]		= 7,
	[IRQ_DA8XX_TCERRINT1]		= 7,
	[IRQ_DA8XX_C0_RX_THRESH_PULSE]	= 7,
	[IRQ_DA8XX_C0_RX_PULSE]		= 7,
	[IRQ_DA8XX_C0_TX_PULSE]		= 7,
	[IRQ_DA8XX_C0_MISC_PULSE]	= 7,
	[IRQ_DA8XX_C1_RX_THRESH_PULSE]	= 7,
	[IRQ_DA8XX_C1_RX_PULSE]		= 7,
	[IRQ_DA8XX_C1_TX_PULSE]		= 7,
	[IRQ_DA8XX_C1_MISC_PULSE]	= 7,
	[IRQ_DA8XX_MEMERR]		= 7,
	[IRQ_DA8XX_GPIO0]		= 7,
	[IRQ_DA8XX_GPIO1]		= 7,
	[IRQ_DA8XX_GPIO2]		= 7,
	[IRQ_DA8XX_GPIO3]		= 7,
	[IRQ_DA8XX_GPIO4]		= 7,
	[IRQ_DA8XX_GPIO5]		= 7,
	[IRQ_DA8XX_GPIO6]		= 7,
	[IRQ_DA8XX_GPIO7]		= 7,
	[IRQ_DA8XX_GPIO8]		= 7,
	[IRQ_DA8XX_I2CINT1]		= 7,
	[IRQ_DA8XX_LCDINT]		= 7,
	[IRQ_DA8XX_UARTINT1]		= 7,
	[IRQ_DA8XX_MCASPINT]		= 7,
	[IRQ_DA8XX_ALLINT1]		= 7,
	[IRQ_DA8XX_SPINT1]		= 7,
	[IRQ_DA8XX_UHPI_INT1]		= 7,
	[IRQ_DA8XX_USB_INT]		= 7,
	[IRQ_DA8XX_IRQN]		= 7,
	[IRQ_DA8XX_RWAKEUP]		= 7,
	[IRQ_DA8XX_UARTINT2]		= 7,
	[IRQ_DA8XX_DFTSSINT]		= 7,
	[IRQ_DA8XX_EHRPWM0]		= 7,
	[IRQ_DA8XX_EHRPWM0TZ]		= 7,
	[IRQ_DA8XX_EHRPWM1]		= 7,
	[IRQ_DA8XX_EHRPWM1TZ]		= 7,
	[IRQ_DA850_SATAINT]		= 7,
	[IRQ_DA850_TINT12_2]		= 7,
	[IRQ_DA850_TINT34_2]		= 7,
	[IRQ_DA850_TINTALL_2]		= 7,
	[IRQ_DA8XX_ECAP0]		= 7,
	[IRQ_DA8XX_ECAP1]		= 7,
	[IRQ_DA8XX_ECAP2]		= 7,
	[IRQ_DA850_MMCSDINT0_1]		= 7,
	[IRQ_DA850_MMCSDINT1_1]		= 7,
	[IRQ_DA850_T12CMPINT0_2]	= 7,
	[IRQ_DA850_T12CMPINT1_2]	= 7,
	[IRQ_DA850_T12CMPINT2_2]	= 7,
	[IRQ_DA850_T12CMPINT3_2]	= 7,
	[IRQ_DA850_T12CMPINT4_2]	= 7,
	[IRQ_DA850_T12CMPINT5_2]	= 7,
	[IRQ_DA850_T12CMPINT6_2]	= 7,
	[IRQ_DA850_T12CMPINT7_2]	= 7,
	[IRQ_DA850_T12CMPINT0_3]	= 7,
	[IRQ_DA850_T12CMPINT1_3]	= 7,
	[IRQ_DA850_T12CMPINT2_3]	= 7,
	[IRQ_DA850_T12CMPINT3_3]	= 7,
	[IRQ_DA850_T12CMPINT4_3]	= 7,
	[IRQ_DA850_T12CMPINT5_3]	= 7,
	[IRQ_DA850_T12CMPINT6_3]	= 7,
	[IRQ_DA850_T12CMPINT7_3]	= 7,
	[IRQ_DA850_RPIINT]		= 7,
	[IRQ_DA850_VPIFINT]		= 7,
	[IRQ_DA850_CCINT1]		= 7,
	[IRQ_DA850_CCERRINT1]		= 7,
	[IRQ_DA850_TCERRINT2]		= 7,
	[IRQ_DA850_TINT12_3]		= 7,
	[IRQ_DA850_TINT34_3]		= 7,
	[IRQ_DA850_TINTALL_3]		= 7,
	[IRQ_DA850_MCBSP0RINT]		= 7,
	[IRQ_DA850_MCBSP0XINT]		= 7,
	[IRQ_DA850_MCBSP1RINT]		= 7,
	[IRQ_DA850_MCBSP1XINT]		= 7,
	[IRQ_DA8XX_ARMCLKSTOPREQ]	= 7,
};

static struct map_desc da850_io_desc[] = {
	{
		.virtual	= IO_VIRT,
		.pfn		= __phys_to_pfn(IO_PHYS),
		.length		= IO_SIZE,
		.type		= MT_DEVICE
	},
	{
		.virtual	= DA8XX_CP_INTC_VIRT,
		.pfn		= __phys_to_pfn(DA8XX_CP_INTC_BASE),
		.length		= DA8XX_CP_INTC_SIZE,
		.type		= MT_DEVICE
	},
};

static void __iomem *da850_psc_bases[] = {
	IO_ADDRESS(DA8XX_PSC0_BASE),
	IO_ADDRESS(DA8XX_PSC1_BASE),
};

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id da850_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb7d1,
		.manufacturer	= 0x017,	/* 0x02f >> 1 */
		.cpu_id		= DAVINCI_CPU_ID_DA850,
		.name		= "da850/omap-l138",
	},
};

static struct davinci_timer_instance da850_timer_instance[4] = {
	{
		.base		= IO_ADDRESS(DA8XX_TIMER64P0_BASE),
		.bottom_irq	= IRQ_DA8XX_TINT12_0,
		.top_irq	= IRQ_DA8XX_TINT34_0,
	},
	{
		.base		= IO_ADDRESS(DA8XX_TIMER64P1_BASE),
		.bottom_irq	= IRQ_DA8XX_TINT12_1,
		.top_irq	= IRQ_DA8XX_TINT34_1,
	},
	{
		.base		= IO_ADDRESS(DA850_TIMER64P2_BASE),
		.bottom_irq	= IRQ_DA850_TINT12_2,
		.top_irq	= IRQ_DA850_TINT34_2,
	},
	{
		.base		= IO_ADDRESS(DA850_TIMER64P3_BASE),
		.bottom_irq	= IRQ_DA850_TINT12_3,
		.top_irq	= IRQ_DA850_TINT34_3,
	},
};

/*
 * T0_BOT: Timer 0, bottom		: Used for clock_event
 * T0_TOP: Timer 0, top			: Used for clocksource
 * T1_BOT, T1_TOP: Timer 1, bottom & top: Used for watchdog timer
 */
static struct davinci_timer_info da850_timer_info = {
	.timers		= da850_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_TOP,
};

static struct davinci_soc_info davinci_soc_info_da850 = {
	.io_desc		= da850_io_desc,
	.io_desc_num		= ARRAY_SIZE(da850_io_desc),
	.jtag_id_base		= IO_ADDRESS(DA8XX_JTAG_ID_REG),
	.ids			= da850_ids,
	.ids_num		= ARRAY_SIZE(da850_ids),
	.cpu_clks		= da850_clks,
	.psc_bases		= da850_psc_bases,
	.psc_bases_num		= ARRAY_SIZE(da850_psc_bases),
	.pinmux_base		= IO_ADDRESS(DA8XX_BOOT_CFG_BASE + 0x120),
	.pinmux_pins		= da850_pins,
	.pinmux_pins_num	= ARRAY_SIZE(da850_pins),
	.intc_base		= (void __iomem *)DA8XX_CP_INTC_VIRT,
	.intc_type		= DAVINCI_INTC_TYPE_CP_INTC,
	.intc_irq_prios		= da850_default_priorities,
	.intc_irq_num		= DA850_N_CP_INTC_IRQ,
	.timer_info		= &da850_timer_info,
	.gpio_base		= IO_ADDRESS(DA8XX_GPIO_BASE),
	.gpio_num		= 128,
	.gpio_irq		= IRQ_DA8XX_GPIO0,
	.serial_dev		= &da8xx_serial_device,
	.emac_pdata		= &da8xx_emac_pdata,
};

void __init da850_init(void)
{
	davinci_common_init(&davinci_soc_info_da850);
}
