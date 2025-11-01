// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 Oleksij Rempel <linux@rempel-privat.de>.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/alphascale,asm9260.h>

#define HW_AHBCLKCTRL0		0x0020
#define HW_AHBCLKCTRL1		0x0030
#define HW_SYSPLLCTRL		0x0100
#define HW_MAINCLKSEL		0x0120
#define HW_MAINCLKUEN		0x0124
#define HW_UARTCLKSEL		0x0128
#define HW_UARTCLKUEN		0x012c
#define HW_I2S0CLKSEL		0x0130
#define HW_I2S0CLKUEN		0x0134
#define HW_I2S1CLKSEL		0x0138
#define HW_I2S1CLKUEN		0x013c
#define HW_WDTCLKSEL		0x0160
#define HW_WDTCLKUEN		0x0164
#define HW_CLKOUTCLKSEL		0x0170
#define HW_CLKOUTCLKUEN		0x0174
#define HW_CPUCLKDIV		0x017c
#define HW_SYSAHBCLKDIV		0x0180
#define HW_I2S0MCLKDIV		0x0190
#define HW_I2S0SCLKDIV		0x0194
#define HW_I2S1MCLKDIV		0x0188
#define HW_I2S1SCLKDIV		0x018c
#define HW_UART0CLKDIV		0x0198
#define HW_UART1CLKDIV		0x019c
#define HW_UART2CLKDIV		0x01a0
#define HW_UART3CLKDIV		0x01a4
#define HW_UART4CLKDIV		0x01a8
#define HW_UART5CLKDIV		0x01ac
#define HW_UART6CLKDIV		0x01b0
#define HW_UART7CLKDIV		0x01b4
#define HW_UART8CLKDIV		0x01b8
#define HW_UART9CLKDIV		0x01bc
#define HW_SPI0CLKDIV		0x01c0
#define HW_SPI1CLKDIV		0x01c4
#define HW_QUADSPICLKDIV	0x01c8
#define HW_SSP0CLKDIV		0x01d0
#define HW_NANDCLKDIV		0x01d4
#define HW_TRACECLKDIV		0x01e0
#define HW_CAMMCLKDIV		0x01e8
#define HW_WDTCLKDIV		0x01ec
#define HW_CLKOUTCLKDIV		0x01f4
#define HW_MACCLKDIV		0x01f8
#define HW_LCDCLKDIV		0x01fc
#define HW_ADCANACLKDIV		0x0200

static struct clk_hw_onecell_data *clk_data;
static DEFINE_SPINLOCK(asm9260_clk_lock);

struct asm9260_div_clk {
	unsigned int idx;
	const char *name;
	const char *parent_name;
	u32 reg;
};

struct asm9260_gate_data {
	unsigned int idx;
	const char *name;
	const char *parent_name;
	u32 reg;
	u8 bit_idx;
	unsigned long flags;
};

struct asm9260_mux_clock {
	u8			mask;
	u32			*table;
	const char		*name;
	const struct clk_parent_data *parent_data;
	u8			num_parents;
	unsigned long		offset;
	unsigned long		flags;
};

static void __iomem *base;

static const struct asm9260_div_clk asm9260_div_clks[] __initconst = {
	{ CLKID_SYS_CPU,	"cpu_div", "main_gate", HW_CPUCLKDIV },
	{ CLKID_SYS_AHB,	"ahb_div", "cpu_div", HW_SYSAHBCLKDIV },

	/* i2s has two dividers: one for only external mclk and internal
	 * divider for all clks. */
	{ CLKID_SYS_I2S0M,	"i2s0m_div", "i2s0_mclk",  HW_I2S0MCLKDIV },
	{ CLKID_SYS_I2S1M,	"i2s1m_div", "i2s1_mclk",  HW_I2S1MCLKDIV },
	{ CLKID_SYS_I2S0S,	"i2s0s_div", "i2s0_gate",  HW_I2S0SCLKDIV },
	{ CLKID_SYS_I2S1S,	"i2s1s_div", "i2s0_gate",  HW_I2S1SCLKDIV },

	{ CLKID_SYS_UART0,	"uart0_div", "uart_gate", HW_UART0CLKDIV },
	{ CLKID_SYS_UART1,	"uart1_div", "uart_gate", HW_UART1CLKDIV },
	{ CLKID_SYS_UART2,	"uart2_div", "uart_gate", HW_UART2CLKDIV },
	{ CLKID_SYS_UART3,	"uart3_div", "uart_gate", HW_UART3CLKDIV },
	{ CLKID_SYS_UART4,	"uart4_div", "uart_gate", HW_UART4CLKDIV },
	{ CLKID_SYS_UART5,	"uart5_div", "uart_gate", HW_UART5CLKDIV },
	{ CLKID_SYS_UART6,	"uart6_div", "uart_gate", HW_UART6CLKDIV },
	{ CLKID_SYS_UART7,	"uart7_div", "uart_gate", HW_UART7CLKDIV },
	{ CLKID_SYS_UART8,	"uart8_div", "uart_gate", HW_UART8CLKDIV },
	{ CLKID_SYS_UART9,	"uart9_div", "uart_gate", HW_UART9CLKDIV },

	{ CLKID_SYS_SPI0,	"spi0_div",	"main_gate", HW_SPI0CLKDIV },
	{ CLKID_SYS_SPI1,	"spi1_div",	"main_gate", HW_SPI1CLKDIV },
	{ CLKID_SYS_QUADSPI,	"quadspi_div",	"main_gate", HW_QUADSPICLKDIV },
	{ CLKID_SYS_SSP0,	"ssp0_div",	"main_gate", HW_SSP0CLKDIV },
	{ CLKID_SYS_NAND,	"nand_div",	"main_gate", HW_NANDCLKDIV },
	{ CLKID_SYS_TRACE,	"trace_div",	"main_gate", HW_TRACECLKDIV },
	{ CLKID_SYS_CAMM,	"camm_div",	"main_gate", HW_CAMMCLKDIV },
	{ CLKID_SYS_MAC,	"mac_div",	"main_gate", HW_MACCLKDIV },
	{ CLKID_SYS_LCD,	"lcd_div",	"main_gate", HW_LCDCLKDIV },
	{ CLKID_SYS_ADCANA,	"adcana_div",	"main_gate", HW_ADCANACLKDIV },

	{ CLKID_SYS_WDT,	"wdt_div",	"wdt_gate",    HW_WDTCLKDIV },
	{ CLKID_SYS_CLKOUT,	"clkout_div",	"clkout_gate", HW_CLKOUTCLKDIV },
};

static const struct asm9260_gate_data asm9260_mux_gates[] __initconst = {
	{ 0, "main_gate",	"main_mux",	HW_MAINCLKUEN,	0 },
	{ 0, "uart_gate",	"uart_mux",	HW_UARTCLKUEN,	0 },
	{ 0, "i2s0_gate",	"i2s0_mux",	HW_I2S0CLKUEN,	0 },
	{ 0, "i2s1_gate",	"i2s1_mux",	HW_I2S1CLKUEN,	0 },
	{ 0, "wdt_gate",	"wdt_mux",	HW_WDTCLKUEN,	0 },
	{ 0, "clkout_gate",	"clkout_mux",	HW_CLKOUTCLKUEN, 0 },
};
static const struct asm9260_gate_data asm9260_ahb_gates[] __initconst = {
	/* ahb gates */
	{ CLKID_AHB_ROM,	"rom",		"ahb_div",
		HW_AHBCLKCTRL0,	1, CLK_IGNORE_UNUSED},
	{ CLKID_AHB_RAM,	"ram",		"ahb_div",
		HW_AHBCLKCTRL0,	2, CLK_IGNORE_UNUSED},
	{ CLKID_AHB_GPIO,	"gpio",		"ahb_div",
		HW_AHBCLKCTRL0,	4 },
	{ CLKID_AHB_MAC,	"mac",		"ahb_div",
		HW_AHBCLKCTRL0,	5 },
	{ CLKID_AHB_EMI,	"emi",		"ahb_div",
		HW_AHBCLKCTRL0,	6, CLK_IGNORE_UNUSED},
	{ CLKID_AHB_USB0,	"usb0",		"ahb_div",
		HW_AHBCLKCTRL0,	7 },
	{ CLKID_AHB_USB1,	"usb1",		"ahb_div",
		HW_AHBCLKCTRL0,	8 },
	{ CLKID_AHB_DMA0,	"dma0",		"ahb_div",
		HW_AHBCLKCTRL0,	9 },
	{ CLKID_AHB_DMA1,	"dma1",		"ahb_div",
		HW_AHBCLKCTRL0,	10 },
	{ CLKID_AHB_UART0,	"uart0",	"ahb_div",
		HW_AHBCLKCTRL0,	11 },
	{ CLKID_AHB_UART1,	"uart1",	"ahb_div",
		HW_AHBCLKCTRL0,	12 },
	{ CLKID_AHB_UART2,	"uart2",	"ahb_div",
		HW_AHBCLKCTRL0,	13 },
	{ CLKID_AHB_UART3,	"uart3",	"ahb_div",
		HW_AHBCLKCTRL0,	14 },
	{ CLKID_AHB_UART4,	"uart4",	"ahb_div",
		HW_AHBCLKCTRL0,	15 },
	{ CLKID_AHB_UART5,	"uart5",	"ahb_div",
		HW_AHBCLKCTRL0,	16 },
	{ CLKID_AHB_UART6,	"uart6",	"ahb_div",
		HW_AHBCLKCTRL0,	17 },
	{ CLKID_AHB_UART7,	"uart7",	"ahb_div",
		HW_AHBCLKCTRL0,	18 },
	{ CLKID_AHB_UART8,	"uart8",	"ahb_div",
		HW_AHBCLKCTRL0,	19 },
	{ CLKID_AHB_UART9,	"uart9",	"ahb_div",
		HW_AHBCLKCTRL0,	20 },
	{ CLKID_AHB_I2S0,	"i2s0",		"ahb_div",
		HW_AHBCLKCTRL0,	21 },
	{ CLKID_AHB_I2C0,	"i2c0",		"ahb_div",
		HW_AHBCLKCTRL0,	22 },
	{ CLKID_AHB_I2C1,	"i2c1",		"ahb_div",
		HW_AHBCLKCTRL0,	23 },
	{ CLKID_AHB_SSP0,	"ssp0",		"ahb_div",
		HW_AHBCLKCTRL0,	24 },
	{ CLKID_AHB_IOCONFIG,	"ioconf",	"ahb_div",
		HW_AHBCLKCTRL0,	25 },
	{ CLKID_AHB_WDT,	"wdt",		"ahb_div",
		HW_AHBCLKCTRL0,	26 },
	{ CLKID_AHB_CAN0,	"can0",		"ahb_div",
		HW_AHBCLKCTRL0,	27 },
	{ CLKID_AHB_CAN1,	"can1",		"ahb_div",
		HW_AHBCLKCTRL0,	28 },
	{ CLKID_AHB_MPWM,	"mpwm",		"ahb_div",
		HW_AHBCLKCTRL0,	29 },
	{ CLKID_AHB_SPI0,	"spi0",		"ahb_div",
		HW_AHBCLKCTRL0,	30 },
	{ CLKID_AHB_SPI1,	"spi1",		"ahb_div",
		HW_AHBCLKCTRL0,	31 },

	{ CLKID_AHB_QEI,	"qei",		"ahb_div",
		HW_AHBCLKCTRL1,	0 },
	{ CLKID_AHB_QUADSPI0,	"quadspi0",	"ahb_div",
		HW_AHBCLKCTRL1,	1 },
	{ CLKID_AHB_CAMIF,	"capmif",	"ahb_div",
		HW_AHBCLKCTRL1,	2 },
	{ CLKID_AHB_LCDIF,	"lcdif",	"ahb_div",
		HW_AHBCLKCTRL1,	3 },
	{ CLKID_AHB_TIMER0,	"timer0",	"ahb_div",
		HW_AHBCLKCTRL1,	4 },
	{ CLKID_AHB_TIMER1,	"timer1",	"ahb_div",
		HW_AHBCLKCTRL1,	5 },
	{ CLKID_AHB_TIMER2,	"timer2",	"ahb_div",
		HW_AHBCLKCTRL1,	6 },
	{ CLKID_AHB_TIMER3,	"timer3",	"ahb_div",
		HW_AHBCLKCTRL1,	7 },
	{ CLKID_AHB_IRQ,	"irq",		"ahb_div",
		HW_AHBCLKCTRL1,	8, CLK_IGNORE_UNUSED},
	{ CLKID_AHB_RTC,	"rtc",		"ahb_div",
		HW_AHBCLKCTRL1,	9 },
	{ CLKID_AHB_NAND,	"nand",		"ahb_div",
		HW_AHBCLKCTRL1,	10 },
	{ CLKID_AHB_ADC0,	"adc0",		"ahb_div",
		HW_AHBCLKCTRL1,	11 },
	{ CLKID_AHB_LED,	"led",		"ahb_div",
		HW_AHBCLKCTRL1,	12 },
	{ CLKID_AHB_DAC0,	"dac0",		"ahb_div",
		HW_AHBCLKCTRL1,	13 },
	{ CLKID_AHB_LCD,	"lcd",		"ahb_div",
		HW_AHBCLKCTRL1,	14 },
	{ CLKID_AHB_I2S1,	"i2s1",		"ahb_div",
		HW_AHBCLKCTRL1,	15 },
	{ CLKID_AHB_MAC1,	"mac1",		"ahb_div",
		HW_AHBCLKCTRL1,	16 },
};

static struct clk_parent_data __initdata main_mux_p[] =   { { .index = 0, }, { .name = "pll" } };
static struct clk_parent_data __initdata i2s0_mux_p[] =   { { .index = 0, }, { .name = "pll" }, { .name = "i2s0m_div"} };
static struct clk_parent_data __initdata i2s1_mux_p[] =   { { .index = 0, }, { .name = "pll" }, { .name = "i2s1m_div"} };
static struct clk_parent_data __initdata clkout_mux_p[] = { { .index = 0, }, { .name = "pll" }, { .name = "rtc"} };
static u32 three_mux_table[] = {0, 1, 3};

static struct asm9260_mux_clock asm9260_mux_clks[] __initdata = {
	{ 1, three_mux_table, "main_mux",	main_mux_p,
		ARRAY_SIZE(main_mux_p), HW_MAINCLKSEL, },
	{ 1, three_mux_table, "uart_mux",	main_mux_p,
		ARRAY_SIZE(main_mux_p), HW_UARTCLKSEL, },
	{ 1, three_mux_table, "wdt_mux",	main_mux_p,
		ARRAY_SIZE(main_mux_p), HW_WDTCLKSEL, },
	{ 3, three_mux_table, "i2s0_mux",	i2s0_mux_p,
		ARRAY_SIZE(i2s0_mux_p), HW_I2S0CLKSEL, },
	{ 3, three_mux_table, "i2s1_mux",	i2s1_mux_p,
		ARRAY_SIZE(i2s1_mux_p), HW_I2S1CLKSEL, },
	{ 3, three_mux_table, "clkout_mux",	clkout_mux_p,
		ARRAY_SIZE(clkout_mux_p), HW_CLKOUTCLKSEL, },
};

static void __init asm9260_acc_init(struct device_node *np)
{
	struct clk_hw *pll_hw;
	struct clk_hw **hws;
	const char *pll_clk = "pll";
	struct clk_parent_data pll_parent_data = { .index = 0 };
	u32 rate;
	int n;

	clk_data = kzalloc(struct_size(clk_data, hws, MAX_CLKS), GFP_KERNEL);
	if (!clk_data)
		return;
	clk_data->num = MAX_CLKS;
	hws = clk_data->hws;

	base = of_io_request_and_map(np, 0, np->name);
	if (IS_ERR(base))
		panic("%pOFn: unable to map resource", np);

	/* register pll */
	rate = (ioread32(base + HW_SYSPLLCTRL) & 0xffff) * 1000000;

	pll_hw = clk_hw_register_fixed_rate_parent_accuracy(NULL, pll_clk, &pll_parent_data,
							0, rate);
	if (IS_ERR(pll_hw))
		panic("%pOFn: can't register REFCLK. Check DT!", np);

	for (n = 0; n < ARRAY_SIZE(asm9260_mux_clks); n++) {
		const struct asm9260_mux_clock *mc = &asm9260_mux_clks[n];

		clk_hw_register_mux_table_parent_data(NULL, mc->name, mc->parent_data,
				mc->num_parents, mc->flags, base + mc->offset,
				0, mc->mask, 0, mc->table, &asm9260_clk_lock);
	}

	/* clock mux gate cells */
	for (n = 0; n < ARRAY_SIZE(asm9260_mux_gates); n++) {
		const struct asm9260_gate_data *gd = &asm9260_mux_gates[n];

		clk_hw_register_gate(NULL, gd->name,
			gd->parent_name, gd->flags | CLK_SET_RATE_PARENT,
			base + gd->reg, gd->bit_idx, 0, &asm9260_clk_lock);
	}

	/* clock div cells */
	for (n = 0; n < ARRAY_SIZE(asm9260_div_clks); n++) {
		const struct asm9260_div_clk *dc = &asm9260_div_clks[n];

		hws[dc->idx] = clk_hw_register_divider(NULL, dc->name,
				dc->parent_name, CLK_SET_RATE_PARENT,
				base + dc->reg, 0, 8, CLK_DIVIDER_ONE_BASED,
				&asm9260_clk_lock);
	}

	/* clock ahb gate cells */
	for (n = 0; n < ARRAY_SIZE(asm9260_ahb_gates); n++) {
		const struct asm9260_gate_data *gd = &asm9260_ahb_gates[n];

		hws[gd->idx] = clk_hw_register_gate(NULL, gd->name,
				gd->parent_name, gd->flags, base + gd->reg,
				gd->bit_idx, 0, &asm9260_clk_lock);
	}

	/* check for errors on leaf clocks */
	for (n = 0; n < MAX_CLKS; n++) {
		if (!IS_ERR(hws[n]))
			continue;

		pr_err("%pOF: Unable to register leaf clock %d\n",
				np, n);
		goto fail;
	}

	/* register clk-provider */
	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
	return;
fail:
	iounmap(base);
}
CLK_OF_DECLARE(asm9260_acc, "alphascale,asm9260-clock-controller",
		asm9260_acc_init);
