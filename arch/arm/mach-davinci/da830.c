/*
 * TI DA830/OMAP L137 chip specific setup
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2009 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_data/gpio-davinci.h>

#include <asm/mach/map.h>

#include "psc.h"
#include <mach/irqs.h>
#include <mach/cputype.h>
#include <mach/common.h>
#include <mach/time.h>
#include <mach/da8xx.h>

#include "clock.h"
#include "mux.h"

/* Offsets of the 8 compare registers on the da830 */
#define DA830_CMP12_0		0x60
#define DA830_CMP12_1		0x64
#define DA830_CMP12_2		0x68
#define DA830_CMP12_3		0x6c
#define DA830_CMP12_4		0x70
#define DA830_CMP12_5		0x74
#define DA830_CMP12_6		0x78
#define DA830_CMP12_7		0x7c

#define DA830_REF_FREQ		24000000

static struct pll_data pll0_data = {
	.num		= 1,
	.phys_base	= DA8XX_PLL0_BASE,
	.flags		= PLL_HAS_PREDIV | PLL_HAS_POSTDIV,
};

static struct clk ref_clk = {
	.name		= "ref_clk",
	.rate		= DA830_REF_FREQ,
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

static struct clk scr0_ss_clk = {
	.name		= "scr0_ss",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_SCR0_SS,
	.flags		= ALWAYS_ENABLED,
};

static struct clk scr1_ss_clk = {
	.name		= "scr1_ss",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_SCR1_SS,
	.flags		= ALWAYS_ENABLED,
};

static struct clk scr2_ss_clk = {
	.name		= "scr2_ss",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_SCR2_SS,
	.flags		= ALWAYS_ENABLED,
};

static struct clk dmax_clk = {
	.name		= "dmax",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_PRUSS,
	.flags		= ALWAYS_ENABLED,
};

static struct clk tpcc_clk = {
	.name		= "tpcc",
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

static struct clk mmcsd_clk = {
	.name		= "mmcsd",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_MMC_SD,
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
	.gpsc		= 1,
};

static struct clk uart2_clk = {
	.name		= "uart2",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_UART2,
	.gpsc		= 1,
};

static struct clk spi0_clk = {
	.name		= "spi0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC0_SPI0,
};

static struct clk spi1_clk = {
	.name		= "spi1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_SPI1,
	.gpsc		= 1,
};

static struct clk ecap0_clk = {
	.name		= "ecap0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_ECAP,
	.gpsc		= 1,
};

static struct clk ecap1_clk = {
	.name		= "ecap1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_ECAP,
	.gpsc		= 1,
};

static struct clk ecap2_clk = {
	.name		= "ecap2",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_ECAP,
	.gpsc		= 1,
};

static struct clk pwm0_clk = {
	.name		= "pwm0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_PWM,
	.gpsc		= 1,
};

static struct clk pwm1_clk = {
	.name		= "pwm1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_PWM,
	.gpsc		= 1,
};

static struct clk pwm2_clk = {
	.name		= "pwm2",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_PWM,
	.gpsc		= 1,
};

static struct clk eqep0_clk = {
	.name		= "eqep0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA830_LPSC1_EQEP,
	.gpsc		= 1,
};

static struct clk eqep1_clk = {
	.name		= "eqep1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA830_LPSC1_EQEP,
	.gpsc		= 1,
};

static struct clk lcdc_clk = {
	.name		= "lcdc",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_LCDC,
	.gpsc		= 1,
};

static struct clk mcasp0_clk = {
	.name		= "mcasp0",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_McASP0,
	.gpsc		= 1,
};

static struct clk mcasp1_clk = {
	.name		= "mcasp1",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA830_LPSC1_McASP1,
	.gpsc		= 1,
};

static struct clk mcasp2_clk = {
	.name		= "mcasp2",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA830_LPSC1_McASP2,
	.gpsc		= 1,
};

static struct clk usb20_clk = {
	.name		= "usb20",
	.parent		= &pll0_sysclk2,
	.lpsc		= DA8XX_LPSC1_USB20,
	.gpsc		= 1,
};

static struct clk aemif_clk = {
	.name		= "aemif",
	.parent		= &pll0_sysclk3,
	.lpsc		= DA8XX_LPSC0_EMIF25,
	.flags		= ALWAYS_ENABLED,
};

static struct clk aintc_clk = {
	.name		= "aintc",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC0_AINTC,
	.flags		= ALWAYS_ENABLED,
};

static struct clk secu_mgr_clk = {
	.name		= "secu_mgr",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC0_SECU_MGR,
	.flags		= ALWAYS_ENABLED,
};

static struct clk emac_clk = {
	.name		= "emac",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC1_CPGMAC,
	.gpsc		= 1,
};

static struct clk gpio_clk = {
	.name		= "gpio",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC1_GPIO,
	.gpsc		= 1,
};

static struct clk i2c1_clk = {
	.name		= "i2c1",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC1_I2C,
	.gpsc		= 1,
};

static struct clk usb11_clk = {
	.name		= "usb11",
	.parent		= &pll0_sysclk4,
	.lpsc		= DA8XX_LPSC1_USB11,
	.gpsc		= 1,
};

static struct clk emif3_clk = {
	.name		= "emif3",
	.parent		= &pll0_sysclk5,
	.lpsc		= DA8XX_LPSC1_EMIF3C,
	.gpsc		= 1,
	.flags		= ALWAYS_ENABLED,
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

static struct clk_lookup da830_clks[] = {
	CLK(NULL,		"ref",		&ref_clk),
	CLK(NULL,		"pll0",		&pll0_clk),
	CLK(NULL,		"pll0_aux",	&pll0_aux_clk),
	CLK(NULL,		"pll0_sysclk2",	&pll0_sysclk2),
	CLK(NULL,		"pll0_sysclk3",	&pll0_sysclk3),
	CLK(NULL,		"pll0_sysclk4",	&pll0_sysclk4),
	CLK(NULL,		"pll0_sysclk5",	&pll0_sysclk5),
	CLK(NULL,		"pll0_sysclk6",	&pll0_sysclk6),
	CLK(NULL,		"pll0_sysclk7",	&pll0_sysclk7),
	CLK("i2c_davinci.1",	NULL,		&i2c0_clk),
	CLK(NULL,		"timer0",	&timerp64_0_clk),
	CLK("davinci-wdt",	NULL,		&timerp64_1_clk),
	CLK(NULL,		"arm_rom",	&arm_rom_clk),
	CLK(NULL,		"scr0_ss",	&scr0_ss_clk),
	CLK(NULL,		"scr1_ss",	&scr1_ss_clk),
	CLK(NULL,		"scr2_ss",	&scr2_ss_clk),
	CLK(NULL,		"dmax",		&dmax_clk),
	CLK(NULL,		"tpcc",		&tpcc_clk),
	CLK(NULL,		"tptc0",	&tptc0_clk),
	CLK(NULL,		"tptc1",	&tptc1_clk),
	CLK("da830-mmc.0",	NULL,		&mmcsd_clk),
	CLK("serial8250.0",	NULL,		&uart0_clk),
	CLK("serial8250.1",	NULL,		&uart1_clk),
	CLK("serial8250.2",	NULL,		&uart2_clk),
	CLK("spi_davinci.0",	NULL,		&spi0_clk),
	CLK("spi_davinci.1",	NULL,		&spi1_clk),
	CLK(NULL,		"ecap0",	&ecap0_clk),
	CLK(NULL,		"ecap1",	&ecap1_clk),
	CLK(NULL,		"ecap2",	&ecap2_clk),
	CLK(NULL,		"pwm0",		&pwm0_clk),
	CLK(NULL,		"pwm1",		&pwm1_clk),
	CLK(NULL,		"pwm2",		&pwm2_clk),
	CLK("eqep.0",		NULL,		&eqep0_clk),
	CLK("eqep.1",		NULL,		&eqep1_clk),
	CLK("da8xx_lcdc.0",	"fck",		&lcdc_clk),
	CLK("davinci-mcasp.0",	NULL,		&mcasp0_clk),
	CLK("davinci-mcasp.1",	NULL,		&mcasp1_clk),
	CLK("davinci-mcasp.2",	NULL,		&mcasp2_clk),
	CLK("musb-da8xx",	"usb20",	&usb20_clk),
	CLK(NULL,		"aemif",	&aemif_clk),
	CLK(NULL,		"aintc",	&aintc_clk),
	CLK(NULL,		"secu_mgr",	&secu_mgr_clk),
	CLK("davinci_emac.1",	NULL,		&emac_clk),
	CLK("davinci_mdio.0",   "fck",          &emac_clk),
	CLK(NULL,		"gpio",		&gpio_clk),
	CLK("i2c_davinci.2",	NULL,		&i2c1_clk),
	CLK("ohci-da8xx",	"usb11",	&usb11_clk),
	CLK(NULL,		"emif3",	&emif3_clk),
	CLK(NULL,		"arm",		&arm_clk),
	CLK(NULL,		"rmii",		&rmii_clk),
	CLK(NULL,		NULL,		NULL),
};

/*
 * Device specific mux setup
 *
 *	     soc      description	mux    mode    mode   mux	dbg
 *					reg   offset   mask   mode
 */
static const struct mux_config da830_pins[] = {
#ifdef CONFIG_DAVINCI_MUX
	MUX_CFG(DA830, GPIO7_14,	0,	0,	0xf,	1,	false)
	MUX_CFG(DA830, RTCK,		0,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_15,	0,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMU_0,		0,	4,	0xf,	8,	false)
	MUX_CFG(DA830, EMB_SDCKE,	0,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_CLK_GLUE,	0,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_CLK,		0,	12,	0xf,	2,	false)
	MUX_CFG(DA830, NEMB_CS_0,	0,	16,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_CAS,	0,	20,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_RAS,	0,	24,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_WE,		0,	28,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_BA_1,	1,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_BA_0,	1,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_0,		1,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_1,		1,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_2,		1,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_3,		1,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_4,		1,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_5,		1,	28,	0xf,	1,	false)
	MUX_CFG(DA830, GPIO7_0,		1,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_1,		1,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_2,		1,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_3,		1,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_4,		1,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_5,		1,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_6,		1,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_7,		1,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMB_A_6,		2,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_7,		2,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_8,		2,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_9,		2,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_10,	2,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_11,	2,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_A_12,	2,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_31,	2,	28,	0xf,	1,	false)
	MUX_CFG(DA830, GPIO7_8,		2,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_9,		2,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_10,	2,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_11,	2,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_12,	2,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO7_13,	2,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_13,	2,	24,	0xf,	8,	false)
	MUX_CFG(DA830, EMB_D_30,	3,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_29,	3,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_28,	3,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_27,	3,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_26,	3,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_25,	3,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_24,	3,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_23,	3,	28,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_22,	4,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_21,	4,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_20,	4,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_19,	4,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_18,	4,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_17,	4,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_16,	4,	24,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_WE_DQM_3,	4,	28,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_WE_DQM_2,	5,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_0,		5,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_1,		5,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_2,		5,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_3,		5,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_4,		5,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_5,		5,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_6,		5,	28,	0xf,	1,	false)
	MUX_CFG(DA830, GPIO6_0,		5,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_1,		5,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_2,		5,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_3,		5,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_4,		5,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_5,		5,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_6,		5,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMB_D_7,		6,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_8,		6,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_9,		6,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_10,	6,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_11,	6,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_12,	6,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_13,	6,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMB_D_14,	6,	28,	0xf,	1,	false)
	MUX_CFG(DA830, GPIO6_7,		6,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_8,		6,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_9,		6,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_10,	6,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_11,	6,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_12,	6,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_13,	6,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO6_14,	6,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMB_D_15,	7,	0,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_WE_DQM_1,	7,	4,	0xf,	1,	false)
	MUX_CFG(DA830, NEMB_WE_DQM_0,	7,	8,	0xf,	1,	false)
	MUX_CFG(DA830, SPI0_SOMI_0,	7,	12,	0xf,	1,	false)
	MUX_CFG(DA830, SPI0_SIMO_0,	7,	16,	0xf,	1,	false)
	MUX_CFG(DA830, SPI0_CLK,	7,	20,	0xf,	1,	false)
	MUX_CFG(DA830, NSPI0_ENA,	7,	24,	0xf,	1,	false)
	MUX_CFG(DA830, NSPI0_SCS_0,	7,	28,	0xf,	1,	false)
	MUX_CFG(DA830, EQEP0I,		7,	12,	0xf,	2,	false)
	MUX_CFG(DA830, EQEP0S,		7,	16,	0xf,	2,	false)
	MUX_CFG(DA830, EQEP1I,		7,	20,	0xf,	2,	false)
	MUX_CFG(DA830, NUART0_CTS,	7,	24,	0xf,	2,	false)
	MUX_CFG(DA830, NUART0_RTS,	7,	28,	0xf,	2,	false)
	MUX_CFG(DA830, EQEP0A,		7,	24,	0xf,	4,	false)
	MUX_CFG(DA830, EQEP0B,		7,	28,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO6_15,	7,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_14,	7,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_15,	7,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_0,		7,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_1,		7,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_2,		7,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_3,		7,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_4,		7,	28,	0xf,	8,	false)
	MUX_CFG(DA830, SPI1_SOMI_0,	8,	0,	0xf,	1,	false)
	MUX_CFG(DA830, SPI1_SIMO_0,	8,	4,	0xf,	1,	false)
	MUX_CFG(DA830, SPI1_CLK,	8,	8,	0xf,	1,	false)
	MUX_CFG(DA830, UART0_RXD,	8,	12,	0xf,	1,	false)
	MUX_CFG(DA830, UART0_TXD,	8,	16,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_10,		8,	20,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_11,		8,	24,	0xf,	1,	false)
	MUX_CFG(DA830, NSPI1_ENA,	8,	28,	0xf,	1,	false)
	MUX_CFG(DA830, I2C1_SCL,	8,	0,	0xf,	2,	false)
	MUX_CFG(DA830, I2C1_SDA,	8,	4,	0xf,	2,	false)
	MUX_CFG(DA830, EQEP1S,		8,	8,	0xf,	2,	false)
	MUX_CFG(DA830, I2C0_SDA,	8,	12,	0xf,	2,	false)
	MUX_CFG(DA830, I2C0_SCL,	8,	16,	0xf,	2,	false)
	MUX_CFG(DA830, UART2_RXD,	8,	28,	0xf,	2,	false)
	MUX_CFG(DA830, TM64P0_IN12,	8,	12,	0xf,	4,	false)
	MUX_CFG(DA830, TM64P0_OUT12,	8,	16,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO5_5,		8,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_6,		8,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_7,		8,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_8,		8,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_9,		8,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_10,	8,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_11,	8,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO5_12,	8,	28,	0xf,	8,	false)
	MUX_CFG(DA830, NSPI1_SCS_0,	9,	0,	0xf,	1,	false)
	MUX_CFG(DA830, USB0_DRVVBUS,	9,	4,	0xf,	1,	false)
	MUX_CFG(DA830, AHCLKX0,		9,	8,	0xf,	1,	false)
	MUX_CFG(DA830, ACLKX0,		9,	12,	0xf,	1,	false)
	MUX_CFG(DA830, AFSX0,		9,	16,	0xf,	1,	false)
	MUX_CFG(DA830, AHCLKR0,		9,	20,	0xf,	1,	false)
	MUX_CFG(DA830, ACLKR0,		9,	24,	0xf,	1,	false)
	MUX_CFG(DA830, AFSR0,		9,	28,	0xf,	1,	false)
	MUX_CFG(DA830, UART2_TXD,	9,	0,	0xf,	2,	false)
	MUX_CFG(DA830, AHCLKX2,		9,	8,	0xf,	2,	false)
	MUX_CFG(DA830, ECAP0_APWM0,	9,	12,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_MHZ_50_CLK,	9,	20,	0xf,	2,	false)
	MUX_CFG(DA830, ECAP1_APWM1,	9,	24,	0xf,	2,	false)
	MUX_CFG(DA830, USB_REFCLKIN,	9,	8,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO5_13,	9,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_15,	9,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_11,	9,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_12,	9,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_13,	9,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_14,	9,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_15,	9,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_12,	9,	28,	0xf,	8,	false)
	MUX_CFG(DA830, AMUTE0,		10,	0,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_0,		10,	4,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_1,		10,	8,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_2,		10,	12,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_3,		10,	16,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_4,		10,	20,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_5,		10,	24,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_6,		10,	28,	0xf,	1,	false)
	MUX_CFG(DA830, RMII_TXD_0,	10,	4,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_TXD_1,	10,	8,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_TXEN,	10,	12,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_CRS_DV,	10,	16,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_RXD_0,	10,	20,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_RXD_1,	10,	24,	0xf,	2,	false)
	MUX_CFG(DA830, RMII_RXER,	10,	28,	0xf,	2,	false)
	MUX_CFG(DA830, AFSR2,		10,	4,	0xf,	4,	false)
	MUX_CFG(DA830, ACLKX2,		10,	8,	0xf,	4,	false)
	MUX_CFG(DA830, AXR2_3,		10,	12,	0xf,	4,	false)
	MUX_CFG(DA830, AXR2_2,		10,	16,	0xf,	4,	false)
	MUX_CFG(DA830, AXR2_1,		10,	20,	0xf,	4,	false)
	MUX_CFG(DA830, AFSX2,		10,	24,	0xf,	4,	false)
	MUX_CFG(DA830, ACLKR2,		10,	28,	0xf,	4,	false)
	MUX_CFG(DA830, NRESETOUT,	10,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_0,		10,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_1,		10,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_2,		10,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_3,		10,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_4,		10,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_5,		10,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_6,		10,	28,	0xf,	8,	false)
	MUX_CFG(DA830, AXR0_7,		11,	0,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_8,		11,	4,	0xf,	1,	false)
	MUX_CFG(DA830, UART1_RXD,	11,	8,	0xf,	1,	false)
	MUX_CFG(DA830, UART1_TXD,	11,	12,	0xf,	1,	false)
	MUX_CFG(DA830, AXR0_11,		11,	16,	0xf,	1,	false)
	MUX_CFG(DA830, AHCLKX1,		11,	20,	0xf,	1,	false)
	MUX_CFG(DA830, ACLKX1,		11,	24,	0xf,	1,	false)
	MUX_CFG(DA830, AFSX1,		11,	28,	0xf,	1,	false)
	MUX_CFG(DA830, MDIO_CLK,	11,	0,	0xf,	2,	false)
	MUX_CFG(DA830, MDIO_D,		11,	4,	0xf,	2,	false)
	MUX_CFG(DA830, AXR0_9,		11,	8,	0xf,	2,	false)
	MUX_CFG(DA830, AXR0_10,		11,	12,	0xf,	2,	false)
	MUX_CFG(DA830, EPWM0B,		11,	20,	0xf,	2,	false)
	MUX_CFG(DA830, EPWM0A,		11,	24,	0xf,	2,	false)
	MUX_CFG(DA830, EPWMSYNCI,	11,	28,	0xf,	2,	false)
	MUX_CFG(DA830, AXR2_0,		11,	16,	0xf,	4,	false)
	MUX_CFG(DA830, EPWMSYNC0,	11,	28,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO3_7,		11,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_8,		11,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_9,		11,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_10,	11,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_11,	11,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_14,	11,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO3_15,	11,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_10,	11,	28,	0xf,	8,	false)
	MUX_CFG(DA830, AHCLKR1,		12,	0,	0xf,	1,	false)
	MUX_CFG(DA830, ACLKR1,		12,	4,	0xf,	1,	false)
	MUX_CFG(DA830, AFSR1,		12,	8,	0xf,	1,	false)
	MUX_CFG(DA830, AMUTE1,		12,	12,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_0,		12,	16,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_1,		12,	20,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_2,		12,	24,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_3,		12,	28,	0xf,	1,	false)
	MUX_CFG(DA830, ECAP2_APWM2,	12,	4,	0xf,	2,	false)
	MUX_CFG(DA830, EHRPWMGLUETZ,	12,	12,	0xf,	2,	false)
	MUX_CFG(DA830, EQEP1A,		12,	28,	0xf,	2,	false)
	MUX_CFG(DA830, GPIO4_11,	12,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_12,	12,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_13,	12,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_14,	12,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_0,		12,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_1,		12,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_2,		12,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_3,		12,	28,	0xf,	8,	false)
	MUX_CFG(DA830, AXR1_4,		13,	0,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_5,		13,	4,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_6,		13,	8,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_7,		13,	12,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_8,		13,	16,	0xf,	1,	false)
	MUX_CFG(DA830, AXR1_9,		13,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_0,		13,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_1,		13,	28,	0xf,	1,	false)
	MUX_CFG(DA830, EQEP1B,		13,	0,	0xf,	2,	false)
	MUX_CFG(DA830, EPWM2B,		13,	4,	0xf,	2,	false)
	MUX_CFG(DA830, EPWM2A,		13,	8,	0xf,	2,	false)
	MUX_CFG(DA830, EPWM1B,		13,	12,	0xf,	2,	false)
	MUX_CFG(DA830, EPWM1A,		13,	16,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_0,	13,	24,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_1,	13,	28,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_0,	13,	24,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HD_1,	13,	28,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO4_4,		13,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_5,		13,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_6,		13,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_7,		13,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_8,		13,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO4_9,		13,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_0,		13,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_1,		13,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMA_D_2,		14,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_3,		14,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_4,		14,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_5,		14,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_6,		14,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_7,		14,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_8,		14,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_9,		14,	28,	0xf,	1,	false)
	MUX_CFG(DA830, MMCSD_DAT_2,	14,	0,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_3,	14,	4,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_4,	14,	8,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_5,	14,	12,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_6,	14,	16,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_DAT_7,	14,	20,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_8,	14,	24,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_9,	14,	28,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_2,	14,	0,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HD_3,	14,	4,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HD_4,	14,	8,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HD_5,	14,	12,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HD_6,	14,	16,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HD_7,	14,	20,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_8,		14,	24,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_9,		14,	28,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO0_2,		14,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_3,		14,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_4,		14,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_5,		14,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_6,		14,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_7,		14,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_8,		14,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_9,		14,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMA_D_10,	15,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_11,	15,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_12,	15,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_13,	15,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_14,	15,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_D_15,	15,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_0,		15,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_1,		15,	28,	0xf,	1,	false)
	MUX_CFG(DA830, UHPI_HD_10,	15,	0,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_11,	15,	4,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_12,	15,	8,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_13,	15,	12,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_14,	15,	16,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HD_15,	15,	20,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_7,		15,	24,	0xf,	2,	false)
	MUX_CFG(DA830, MMCSD_CLK,	15,	28,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_10,	15,	0,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_11,	15,	4,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_12,	15,	8,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_13,	15,	12,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_14,	15,	16,	0xf,	4,	false)
	MUX_CFG(DA830, LCD_D_15,	15,	20,	0xf,	4,	false)
	MUX_CFG(DA830, UHPI_HCNTL0,	15,	28,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO0_10,	15,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_11,	15,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_12,	15,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_13,	15,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_14,	15,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO0_15,	15,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_0,		15,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_1,		15,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMA_A_2,		16,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_3,		16,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_4,		16,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_5,		16,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_6,		16,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_7,		16,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_8,		16,	24,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_9,		16,	28,	0xf,	1,	false)
	MUX_CFG(DA830, MMCSD_CMD,	16,	0,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_6,		16,	4,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_3,		16,	8,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_2,		16,	12,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_1,		16,	16,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_0,		16,	20,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_PCLK,	16,	24,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_HSYNC,	16,	28,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HCNTL1,	16,	0,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO1_2,		16,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_3,		16,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_4,		16,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_5,		16,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_6,		16,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_7,		16,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_8,		16,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_9,		16,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMA_A_10,	17,	0,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_11,	17,	4,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_A_12,	17,	8,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_BA_1,	17,	12,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_BA_0,	17,	16,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_CLK,		17,	20,	0xf,	1,	false)
	MUX_CFG(DA830, EMA_SDCKE,	17,	24,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_CAS,	17,	28,	0xf,	1,	false)
	MUX_CFG(DA830, LCD_VSYNC,	17,	0,	0xf,	2,	false)
	MUX_CFG(DA830, NLCD_AC_ENB_CS,	17,	4,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_MCLK,	17,	8,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_5,		17,	12,	0xf,	2,	false)
	MUX_CFG(DA830, LCD_D_4,		17,	16,	0xf,	2,	false)
	MUX_CFG(DA830, OBSCLK,		17,	20,	0xf,	2,	false)
	MUX_CFG(DA830, NEMA_CS_4,	17,	28,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HHWIL,	17,	12,	0xf,	4,	false)
	MUX_CFG(DA830, AHCLKR2,		17,	20,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO1_10,	17,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_11,	17,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_12,	17,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_13,	17,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_14,	17,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO1_15,	17,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_0,		17,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_1,		17,	28,	0xf,	8,	false)
	MUX_CFG(DA830, NEMA_RAS,	18,	0,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_WE,		18,	4,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_CS_0,	18,	8,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_CS_2,	18,	12,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_CS_3,	18,	16,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_OE,		18,	20,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_WE_DQM_1,	18,	24,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_WE_DQM_0,	18,	28,	0xf,	1,	false)
	MUX_CFG(DA830, NEMA_CS_5,	18,	0,	0xf,	2,	false)
	MUX_CFG(DA830, UHPI_HRNW,	18,	4,	0xf,	2,	false)
	MUX_CFG(DA830, NUHPI_HAS,	18,	8,	0xf,	2,	false)
	MUX_CFG(DA830, NUHPI_HCS,	18,	12,	0xf,	2,	false)
	MUX_CFG(DA830, NUHPI_HDS1,	18,	20,	0xf,	2,	false)
	MUX_CFG(DA830, NUHPI_HDS2,	18,	24,	0xf,	2,	false)
	MUX_CFG(DA830, NUHPI_HINT,	18,	28,	0xf,	2,	false)
	MUX_CFG(DA830, AXR0_12,		18,	4,	0xf,	4,	false)
	MUX_CFG(DA830, AMUTE2,		18,	16,	0xf,	4,	false)
	MUX_CFG(DA830, AXR0_13,		18,	20,	0xf,	4,	false)
	MUX_CFG(DA830, AXR0_14,		18,	24,	0xf,	4,	false)
	MUX_CFG(DA830, AXR0_15,		18,	28,	0xf,	4,	false)
	MUX_CFG(DA830, GPIO2_2,		18,	0,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_3,		18,	4,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_4,		18,	8,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_5,		18,	12,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_6,		18,	16,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_7,		18,	20,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_8,		18,	24,	0xf,	8,	false)
	MUX_CFG(DA830, GPIO2_9,		18,	28,	0xf,	8,	false)
	MUX_CFG(DA830, EMA_WAIT_0,	19,	0,	0xf,	1,	false)
	MUX_CFG(DA830, NUHPI_HRDY,	19,	0,	0xf,	2,	false)
	MUX_CFG(DA830, GPIO2_10,	19,	0,	0xf,	8,	false)
#endif
};

const short da830_emif25_pins[] __initconst = {
	DA830_EMA_D_0, DA830_EMA_D_1, DA830_EMA_D_2, DA830_EMA_D_3,
	DA830_EMA_D_4, DA830_EMA_D_5, DA830_EMA_D_6, DA830_EMA_D_7,
	DA830_EMA_D_8, DA830_EMA_D_9, DA830_EMA_D_10, DA830_EMA_D_11,
	DA830_EMA_D_12, DA830_EMA_D_13, DA830_EMA_D_14, DA830_EMA_D_15,
	DA830_EMA_A_0, DA830_EMA_A_1, DA830_EMA_A_2, DA830_EMA_A_3,
	DA830_EMA_A_4, DA830_EMA_A_5, DA830_EMA_A_6, DA830_EMA_A_7,
	DA830_EMA_A_8, DA830_EMA_A_9, DA830_EMA_A_10, DA830_EMA_A_11,
	DA830_EMA_A_12, DA830_EMA_BA_0, DA830_EMA_BA_1, DA830_EMA_CLK,
	DA830_EMA_SDCKE, DA830_NEMA_CS_4, DA830_NEMA_CS_5, DA830_NEMA_WE,
	DA830_NEMA_CS_0, DA830_NEMA_CS_2, DA830_NEMA_CS_3, DA830_NEMA_OE,
	DA830_NEMA_WE_DQM_1, DA830_NEMA_WE_DQM_0, DA830_EMA_WAIT_0,
	-1
};

const short da830_spi0_pins[] __initconst = {
	DA830_SPI0_SOMI_0, DA830_SPI0_SIMO_0, DA830_SPI0_CLK, DA830_NSPI0_ENA,
	DA830_NSPI0_SCS_0,
	-1
};

const short da830_spi1_pins[] __initconst = {
	DA830_SPI1_SOMI_0, DA830_SPI1_SIMO_0, DA830_SPI1_CLK, DA830_NSPI1_ENA,
	DA830_NSPI1_SCS_0,
	-1
};

const short da830_mmc_sd_pins[] __initconst = {
	DA830_MMCSD_DAT_0, DA830_MMCSD_DAT_1, DA830_MMCSD_DAT_2,
	DA830_MMCSD_DAT_3, DA830_MMCSD_DAT_4, DA830_MMCSD_DAT_5,
	DA830_MMCSD_DAT_6, DA830_MMCSD_DAT_7, DA830_MMCSD_CLK,
	DA830_MMCSD_CMD,
	-1
};

const short da830_uart0_pins[] __initconst = {
	DA830_NUART0_CTS, DA830_NUART0_RTS, DA830_UART0_RXD, DA830_UART0_TXD,
	-1
};

const short da830_uart1_pins[] __initconst = {
	DA830_UART1_RXD, DA830_UART1_TXD,
	-1
};

const short da830_uart2_pins[] __initconst = {
	DA830_UART2_RXD, DA830_UART2_TXD,
	-1
};

const short da830_usb20_pins[] __initconst = {
	DA830_USB0_DRVVBUS, DA830_USB_REFCLKIN,
	-1
};

const short da830_usb11_pins[] __initconst = {
	DA830_USB_REFCLKIN,
	-1
};

const short da830_uhpi_pins[] __initconst = {
	DA830_UHPI_HD_0, DA830_UHPI_HD_1, DA830_UHPI_HD_2, DA830_UHPI_HD_3,
	DA830_UHPI_HD_4, DA830_UHPI_HD_5, DA830_UHPI_HD_6, DA830_UHPI_HD_7,
	DA830_UHPI_HD_8, DA830_UHPI_HD_9, DA830_UHPI_HD_10, DA830_UHPI_HD_11,
	DA830_UHPI_HD_12, DA830_UHPI_HD_13, DA830_UHPI_HD_14, DA830_UHPI_HD_15,
	DA830_UHPI_HCNTL0, DA830_UHPI_HCNTL1, DA830_UHPI_HHWIL, DA830_UHPI_HRNW,
	DA830_NUHPI_HAS, DA830_NUHPI_HCS, DA830_NUHPI_HDS1, DA830_NUHPI_HDS2,
	DA830_NUHPI_HINT, DA830_NUHPI_HRDY,
	-1
};

const short da830_cpgmac_pins[] __initconst = {
	DA830_RMII_TXD_0, DA830_RMII_TXD_1, DA830_RMII_TXEN, DA830_RMII_CRS_DV,
	DA830_RMII_RXD_0, DA830_RMII_RXD_1, DA830_RMII_RXER, DA830_MDIO_CLK,
	DA830_MDIO_D,
	-1
};

const short da830_emif3c_pins[] __initconst = {
	DA830_EMB_SDCKE, DA830_EMB_CLK_GLUE, DA830_EMB_CLK, DA830_NEMB_CS_0,
	DA830_NEMB_CAS, DA830_NEMB_RAS, DA830_NEMB_WE, DA830_EMB_BA_1,
	DA830_EMB_BA_0, DA830_EMB_A_0, DA830_EMB_A_1, DA830_EMB_A_2,
	DA830_EMB_A_3, DA830_EMB_A_4, DA830_EMB_A_5, DA830_EMB_A_6,
	DA830_EMB_A_7, DA830_EMB_A_8, DA830_EMB_A_9, DA830_EMB_A_10,
	DA830_EMB_A_11, DA830_EMB_A_12, DA830_NEMB_WE_DQM_3,
	DA830_NEMB_WE_DQM_2, DA830_EMB_D_0, DA830_EMB_D_1, DA830_EMB_D_2,
	DA830_EMB_D_3, DA830_EMB_D_4, DA830_EMB_D_5, DA830_EMB_D_6,
	DA830_EMB_D_7, DA830_EMB_D_8, DA830_EMB_D_9, DA830_EMB_D_10,
	DA830_EMB_D_11, DA830_EMB_D_12, DA830_EMB_D_13, DA830_EMB_D_14,
	DA830_EMB_D_15, DA830_EMB_D_16, DA830_EMB_D_17, DA830_EMB_D_18,
	DA830_EMB_D_19, DA830_EMB_D_20, DA830_EMB_D_21, DA830_EMB_D_22,
	DA830_EMB_D_23, DA830_EMB_D_24, DA830_EMB_D_25, DA830_EMB_D_26,
	DA830_EMB_D_27, DA830_EMB_D_28, DA830_EMB_D_29, DA830_EMB_D_30,
	DA830_EMB_D_31, DA830_NEMB_WE_DQM_1, DA830_NEMB_WE_DQM_0,
	-1
};

const short da830_mcasp0_pins[] __initconst = {
	DA830_AHCLKX0, DA830_ACLKX0, DA830_AFSX0,
	DA830_AHCLKR0, DA830_ACLKR0, DA830_AFSR0, DA830_AMUTE0,
	DA830_AXR0_0, DA830_AXR0_1, DA830_AXR0_2, DA830_AXR0_3,
	DA830_AXR0_4, DA830_AXR0_5, DA830_AXR0_6, DA830_AXR0_7,
	DA830_AXR0_8, DA830_AXR0_9, DA830_AXR0_10, DA830_AXR0_11,
	DA830_AXR0_12, DA830_AXR0_13, DA830_AXR0_14, DA830_AXR0_15,
	-1
};

const short da830_mcasp1_pins[] __initconst = {
	DA830_AHCLKX1, DA830_ACLKX1, DA830_AFSX1,
	DA830_AHCLKR1, DA830_ACLKR1, DA830_AFSR1, DA830_AMUTE1,
	DA830_AXR1_0, DA830_AXR1_1, DA830_AXR1_2, DA830_AXR1_3,
	DA830_AXR1_4, DA830_AXR1_5, DA830_AXR1_6, DA830_AXR1_7,
	DA830_AXR1_8, DA830_AXR1_9, DA830_AXR1_10, DA830_AXR1_11,
	-1
};

const short da830_mcasp2_pins[] __initconst = {
	DA830_AHCLKX2, DA830_ACLKX2, DA830_AFSX2,
	DA830_AHCLKR2, DA830_ACLKR2, DA830_AFSR2, DA830_AMUTE2,
	DA830_AXR2_0, DA830_AXR2_1, DA830_AXR2_2, DA830_AXR2_3,
	-1
};

const short da830_i2c0_pins[] __initconst = {
	DA830_I2C0_SDA, DA830_I2C0_SCL,
	-1
};

const short da830_i2c1_pins[] __initconst = {
	DA830_I2C1_SCL, DA830_I2C1_SDA,
	-1
};

const short da830_lcdcntl_pins[] __initconst = {
	DA830_LCD_D_0, DA830_LCD_D_1, DA830_LCD_D_2, DA830_LCD_D_3,
	DA830_LCD_D_4, DA830_LCD_D_5, DA830_LCD_D_6, DA830_LCD_D_7,
	DA830_LCD_D_8, DA830_LCD_D_9, DA830_LCD_D_10, DA830_LCD_D_11,
	DA830_LCD_D_12, DA830_LCD_D_13, DA830_LCD_D_14, DA830_LCD_D_15,
	DA830_LCD_PCLK, DA830_LCD_HSYNC, DA830_LCD_VSYNC, DA830_NLCD_AC_ENB_CS,
	DA830_LCD_MCLK,
	-1
};

const short da830_pwm_pins[] __initconst = {
	DA830_ECAP0_APWM0, DA830_ECAP1_APWM1, DA830_EPWM0B, DA830_EPWM0A,
	DA830_EPWMSYNCI, DA830_EPWMSYNC0, DA830_ECAP2_APWM2, DA830_EHRPWMGLUETZ,
	DA830_EPWM2B, DA830_EPWM2A, DA830_EPWM1B, DA830_EPWM1A,
	-1
};

const short da830_ecap0_pins[] __initconst = {
	DA830_ECAP0_APWM0,
	-1
};

const short da830_ecap1_pins[] __initconst = {
	DA830_ECAP1_APWM1,
	-1
};

const short da830_ecap2_pins[] __initconst = {
	DA830_ECAP2_APWM2,
	-1
};

const short da830_eqep0_pins[] __initconst = {
	DA830_EQEP0I, DA830_EQEP0S, DA830_EQEP0A, DA830_EQEP0B,
	-1
};

const short da830_eqep1_pins[] __initconst = {
	DA830_EQEP1I, DA830_EQEP1S, DA830_EQEP1A, DA830_EQEP1B,
	-1
};

/* FIQ are pri 0-1; otherwise 2-7, with 7 lowest priority */
static u8 da830_default_priorities[DA830_N_CP_INTC_IRQ] = {
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
	[IRQ_DA830_MPUERR]		= 7,
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
	[IRQ_DA830_EHRPWM2]		= 7,
	[IRQ_DA830_EHRPWM2TZ]		= 7,
	[IRQ_DA8XX_ECAP0]		= 7,
	[IRQ_DA8XX_ECAP1]		= 7,
	[IRQ_DA8XX_ECAP2]		= 7,
	[IRQ_DA830_EQEP0]		= 7,
	[IRQ_DA830_EQEP1]		= 7,
	[IRQ_DA830_T12CMPINT0_0]	= 7,
	[IRQ_DA830_T12CMPINT1_0]	= 7,
	[IRQ_DA830_T12CMPINT2_0]	= 7,
	[IRQ_DA830_T12CMPINT3_0]	= 7,
	[IRQ_DA830_T12CMPINT4_0]	= 7,
	[IRQ_DA830_T12CMPINT5_0]	= 7,
	[IRQ_DA830_T12CMPINT6_0]	= 7,
	[IRQ_DA830_T12CMPINT7_0]	= 7,
	[IRQ_DA830_T12CMPINT0_1]	= 7,
	[IRQ_DA830_T12CMPINT1_1]	= 7,
	[IRQ_DA830_T12CMPINT2_1]	= 7,
	[IRQ_DA830_T12CMPINT3_1]	= 7,
	[IRQ_DA830_T12CMPINT4_1]	= 7,
	[IRQ_DA830_T12CMPINT5_1]	= 7,
	[IRQ_DA830_T12CMPINT6_1]	= 7,
	[IRQ_DA830_T12CMPINT7_1]	= 7,
	[IRQ_DA8XX_ARMCLKSTOPREQ]	= 7,
};

static struct map_desc da830_io_desc[] = {
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

static u32 da830_psc_bases[] = { DA8XX_PSC0_BASE, DA8XX_PSC1_BASE };

/* Contents of JTAG ID register used to identify exact cpu type */
static struct davinci_id da830_ids[] = {
	{
		.variant	= 0x0,
		.part_no	= 0xb7df,
		.manufacturer	= 0x017,	/* 0x02f >> 1 */
		.cpu_id		= DAVINCI_CPU_ID_DA830,
		.name		= "da830/omap-l137 rev1.0",
	},
	{
		.variant	= 0x8,
		.part_no	= 0xb7df,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DA830,
		.name		= "da830/omap-l137 rev1.1",
	},
	{
		.variant	= 0x9,
		.part_no	= 0xb7df,
		.manufacturer	= 0x017,
		.cpu_id		= DAVINCI_CPU_ID_DA830,
		.name		= "da830/omap-l137 rev2.0",
	},
};

static struct davinci_gpio_platform_data da830_gpio_platform_data = {
	.ngpio = 128,
};

int __init da830_register_gpio(void)
{
	return da8xx_register_gpio(&da830_gpio_platform_data);
}

static struct davinci_timer_instance da830_timer_instance[2] = {
	{
		.base		= DA8XX_TIMER64P0_BASE,
		.bottom_irq	= IRQ_DA8XX_TINT12_0,
		.top_irq	= IRQ_DA8XX_TINT34_0,
		.cmp_off	= DA830_CMP12_0,
		.cmp_irq	= IRQ_DA830_T12CMPINT0_0,
	},
	{
		.base		= DA8XX_TIMER64P1_BASE,
		.bottom_irq	= IRQ_DA8XX_TINT12_1,
		.top_irq	= IRQ_DA8XX_TINT34_1,
		.cmp_off	= DA830_CMP12_0,
		.cmp_irq	= IRQ_DA830_T12CMPINT0_1,
	},
};

/*
 * T0_BOT: Timer 0, bottom		: Used for clock_event & clocksource
 * T0_TOP: Timer 0, top			: Used by DSP
 * T1_BOT, T1_TOP: Timer 1, bottom & top: Used for watchdog timer
 */
static struct davinci_timer_info da830_timer_info = {
	.timers		= da830_timer_instance,
	.clockevent_id	= T0_BOT,
	.clocksource_id	= T0_BOT,
};

static struct davinci_soc_info davinci_soc_info_da830 = {
	.io_desc		= da830_io_desc,
	.io_desc_num		= ARRAY_SIZE(da830_io_desc),
	.jtag_id_reg		= DA8XX_SYSCFG0_BASE + DA8XX_JTAG_ID_REG,
	.ids			= da830_ids,
	.ids_num		= ARRAY_SIZE(da830_ids),
	.cpu_clks		= da830_clks,
	.psc_bases		= da830_psc_bases,
	.psc_bases_num		= ARRAY_SIZE(da830_psc_bases),
	.pinmux_base		= DA8XX_SYSCFG0_BASE + 0x120,
	.pinmux_pins		= da830_pins,
	.pinmux_pins_num	= ARRAY_SIZE(da830_pins),
	.intc_base		= DA8XX_CP_INTC_BASE,
	.intc_type		= DAVINCI_INTC_TYPE_CP_INTC,
	.intc_irq_prios		= da830_default_priorities,
	.intc_irq_num		= DA830_N_CP_INTC_IRQ,
	.timer_info		= &da830_timer_info,
	.emac_pdata		= &da8xx_emac_pdata,
};

void __init da830_init(void)
{
	davinci_common_init(&davinci_soc_info_da830);

	da8xx_syscfg0_base = ioremap(DA8XX_SYSCFG0_BASE, SZ_4K);
	WARN(!da8xx_syscfg0_base, "Unable to map syscfg0 module");

	davinci_clk_init(davinci_soc_info_da830.cpu_clks);
}
