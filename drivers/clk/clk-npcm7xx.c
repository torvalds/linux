// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NPCM7xx Clock Generator
 * All the clocks are initialized by the bootloader, so this driver allow only
 * reading of current settings directly from the hardware.
 *
 * Copyright (C) 2018 Nuvoton Technologies tali.perry@nuvoton.com
 */

#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/bitfield.h>

#include <dt-bindings/clock/nuvoton,npcm7xx-clock.h>

struct npcm7xx_clk_pll {
	struct clk_hw	hw;
	void __iomem	*pllcon;
	u8		flags;
};

#define to_npcm7xx_clk_pll(_hw) container_of(_hw, struct npcm7xx_clk_pll, hw)

#define PLLCON_LOKI	BIT(31)
#define PLLCON_LOKS	BIT(30)
#define PLLCON_FBDV	GENMASK(27, 16)
#define PLLCON_OTDV2	GENMASK(15, 13)
#define PLLCON_PWDEN	BIT(12)
#define PLLCON_OTDV1	GENMASK(10, 8)
#define PLLCON_INDV	GENMASK(5, 0)

static unsigned long npcm7xx_clk_pll_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct npcm7xx_clk_pll *pll = to_npcm7xx_clk_pll(hw);
	unsigned long fbdv, indv, otdv1, otdv2;
	unsigned int val;
	u64 ret;

	if (parent_rate == 0) {
		pr_err("%s: parent rate is zero", __func__);
		return 0;
	}

	val = readl_relaxed(pll->pllcon);

	indv = FIELD_GET(PLLCON_INDV, val);
	fbdv = FIELD_GET(PLLCON_FBDV, val);
	otdv1 = FIELD_GET(PLLCON_OTDV1, val);
	otdv2 = FIELD_GET(PLLCON_OTDV2, val);

	ret = (u64)parent_rate * fbdv;
	do_div(ret, indv * otdv1 * otdv2);

	return ret;
}

static const struct clk_ops npcm7xx_clk_pll_ops = {
	.recalc_rate = npcm7xx_clk_pll_recalc_rate,
};

static struct clk_hw *
npcm7xx_clk_register_pll(void __iomem *pllcon, const char *name,
			 const char *parent_name, unsigned long flags)
{
	struct npcm7xx_clk_pll *pll;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pr_debug("%s reg, name=%s, p=%s\n", __func__, name, parent_name);

	init.name = name;
	init.ops = &npcm7xx_clk_pll_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = flags;

	pll->pllcon = pllcon;
	pll->hw.init = &init;

	hw = &pll->hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		hw = ERR_PTR(ret);
	}

	return hw;
}

#define NPCM7XX_CLKEN1          (0x00)
#define NPCM7XX_CLKEN2          (0x28)
#define NPCM7XX_CLKEN3          (0x30)
#define NPCM7XX_CLKSEL          (0x04)
#define NPCM7XX_CLKDIV1         (0x08)
#define NPCM7XX_CLKDIV2         (0x2C)
#define NPCM7XX_CLKDIV3         (0x58)
#define NPCM7XX_PLLCON0         (0x0C)
#define NPCM7XX_PLLCON1         (0x10)
#define NPCM7XX_PLLCON2         (0x54)
#define NPCM7XX_SWRSTR          (0x14)
#define NPCM7XX_IRQWAKECON      (0x18)
#define NPCM7XX_IRQWAKEFLAG     (0x1C)
#define NPCM7XX_IPSRST1         (0x20)
#define NPCM7XX_IPSRST2         (0x24)
#define NPCM7XX_IPSRST3         (0x34)
#define NPCM7XX_WD0RCR          (0x38)
#define NPCM7XX_WD1RCR          (0x3C)
#define NPCM7XX_WD2RCR          (0x40)
#define NPCM7XX_SWRSTC1         (0x44)
#define NPCM7XX_SWRSTC2         (0x48)
#define NPCM7XX_SWRSTC3         (0x4C)
#define NPCM7XX_SWRSTC4         (0x50)
#define NPCM7XX_CORSTC          (0x5C)
#define NPCM7XX_PLLCONG         (0x60)
#define NPCM7XX_AHBCKFI         (0x64)
#define NPCM7XX_SECCNT          (0x68)
#define NPCM7XX_CNTR25M         (0x6C)

struct npcm7xx_clk_gate_data {
	u32 reg;
	u8 bit_idx;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	/*
	 * If this clock is exported via DT, set onecell_idx to constant
	 * defined in include/dt-bindings/clock/nuvoton, NPCM7XX-clock.h for
	 * this specific clock.  Otherwise, set to -1.
	 */
	int onecell_idx;
};

struct npcm7xx_clk_mux_data {
	u8 shift;
	u8 mask;
	u32 *table;
	const char *name;
	const char * const *parent_names;
	u8 num_parents;
	unsigned long flags;
	/*
	 * If this clock is exported via DT, set onecell_idx to constant
	 * defined in include/dt-bindings/clock/nuvoton, NPCM7XX-clock.h for
	 * this specific clock.  Otherwise, set to -1.
	 */
	int onecell_idx;

};

struct npcm7xx_clk_div_fixed_data {
	u8 mult;
	u8 div;
	const char *name;
	const char *parent_name;
	u8 clk_divider_flags;
	/*
	 * If this clock is exported via DT, set onecell_idx to constant
	 * defined in include/dt-bindings/clock/nuvoton, NPCM7XX-clock.h for
	 * this specific clock.  Otherwise, set to -1.
	 */
	int onecell_idx;
};


struct npcm7xx_clk_div_data {
	u32 reg;
	u8 shift;
	u8 width;
	const char *name;
	const char *parent_name;
	u8 clk_divider_flags;
	unsigned long flags;
	/*
	 * If this clock is exported via DT, set onecell_idx to constant
	 * defined in include/dt-bindings/clock/nuvoton, NPCM7XX-clock.h for
	 * this specific clock.  Otherwise, set to -1.
	 */
	int onecell_idx;
};

struct npcm7xx_clk_pll_data {
	u32 reg;
	const char *name;
	const char *parent_name;
	unsigned long flags;
	/*
	 * If this clock is exported via DT, set onecell_idx to constant
	 * defined in include/dt-bindings/clock/nuvoton, NPCM7XX-clock.h for
	 * this specific clock.  Otherwise, set to -1.
	 */
	int onecell_idx;
};

/*
 * Single copy of strings used to refer to clocks within this driver indexed by
 * above enum.
 */
#define NPCM7XX_CLK_S_REFCLK      "refclk"
#define NPCM7XX_CLK_S_SYSBYPCK    "sysbypck"
#define NPCM7XX_CLK_S_MCBYPCK     "mcbypck"
#define NPCM7XX_CLK_S_GFXBYPCK    "gfxbypck"
#define NPCM7XX_CLK_S_PLL0        "pll0"
#define NPCM7XX_CLK_S_PLL1        "pll1"
#define NPCM7XX_CLK_S_PLL1_DIV2   "pll1_div2"
#define NPCM7XX_CLK_S_PLL2        "pll2"
#define NPCM7XX_CLK_S_PLL_GFX     "pll_gfx"
#define NPCM7XX_CLK_S_PLL2_DIV2   "pll2_div2"
#define NPCM7XX_CLK_S_PIX_MUX     "gfx_pixel"
#define NPCM7XX_CLK_S_GPRFSEL_MUX "gprfsel_mux"
#define NPCM7XX_CLK_S_MC_MUX      "mc_phy"
#define NPCM7XX_CLK_S_CPU_MUX     "cpu"  /*AKA system clock.*/
#define NPCM7XX_CLK_S_MC          "mc"
#define NPCM7XX_CLK_S_AXI         "axi"  /*AKA CLK2*/
#define NPCM7XX_CLK_S_AHB         "ahb"  /*AKA CLK4*/
#define NPCM7XX_CLK_S_CLKOUT_MUX  "clkout_mux"
#define NPCM7XX_CLK_S_UART_MUX    "uart_mux"
#define NPCM7XX_CLK_S_TIM_MUX     "timer_mux"
#define NPCM7XX_CLK_S_SD_MUX      "sd_mux"
#define NPCM7XX_CLK_S_GFXM_MUX    "gfxm_mux"
#define NPCM7XX_CLK_S_SU_MUX      "serial_usb_mux"
#define NPCM7XX_CLK_S_DVC_MUX     "dvc_mux"
#define NPCM7XX_CLK_S_GFX_MUX     "gfx_mux"
#define NPCM7XX_CLK_S_GFX_PIXEL   "gfx_pixel"
#define NPCM7XX_CLK_S_SPI0        "spi0"
#define NPCM7XX_CLK_S_SPI3        "spi3"
#define NPCM7XX_CLK_S_SPIX        "spix"
#define NPCM7XX_CLK_S_APB1        "apb1"
#define NPCM7XX_CLK_S_APB2        "apb2"
#define NPCM7XX_CLK_S_APB3        "apb3"
#define NPCM7XX_CLK_S_APB4        "apb4"
#define NPCM7XX_CLK_S_APB5        "apb5"
#define NPCM7XX_CLK_S_TOCK        "tock"
#define NPCM7XX_CLK_S_CLKOUT      "clkout"
#define NPCM7XX_CLK_S_UART        "uart"
#define NPCM7XX_CLK_S_TIMER       "timer"
#define NPCM7XX_CLK_S_MMC         "mmc"
#define NPCM7XX_CLK_S_SDHC        "sdhc"
#define NPCM7XX_CLK_S_ADC         "adc"
#define NPCM7XX_CLK_S_GFX         "gfx0_gfx1_mem"
#define NPCM7XX_CLK_S_USBIF       "serial_usbif"
#define NPCM7XX_CLK_S_USB_HOST    "usb_host"
#define NPCM7XX_CLK_S_USB_BRIDGE  "usb_bridge"
#define NPCM7XX_CLK_S_PCI         "pci"

static u32 pll_mux_table[] = {0, 1, 2, 3};
static const char * const pll_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_PLL0,
	NPCM7XX_CLK_S_PLL1_DIV2,
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_PLL2_DIV2,
};

static u32 cpuck_mux_table[] = {0, 1, 2, 3};
static const char * const cpuck_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_PLL0,
	NPCM7XX_CLK_S_PLL1_DIV2,
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_SYSBYPCK,
};

static u32 pixcksel_mux_table[] = {0, 2};
static const char * const pixcksel_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_PLL_GFX,
	NPCM7XX_CLK_S_REFCLK,
};

static u32 sucksel_mux_table[] = {2, 3};
static const char * const sucksel_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_PLL2_DIV2,
};

static u32 mccksel_mux_table[] = {0, 2, 3};
static const char * const mccksel_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_PLL1_DIV2,
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_MCBYPCK,
};

static u32 clkoutsel_mux_table[] = {0, 1, 2, 3, 4};
static const char * const clkoutsel_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_PLL0,
	NPCM7XX_CLK_S_PLL1_DIV2,
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_PLL_GFX, // divided by 2
	NPCM7XX_CLK_S_PLL2_DIV2,
};

static u32 gfxmsel_mux_table[] = {2, 3};
static const char * const gfxmsel_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_PLL2_DIV2,
};

static u32 dvcssel_mux_table[] = {2, 3};
static const char * const dvcssel_mux_parents[] __initconst = {
	NPCM7XX_CLK_S_REFCLK,
	NPCM7XX_CLK_S_PLL2,
};

static const struct npcm7xx_clk_pll_data npcm7xx_plls[] __initconst = {
	{NPCM7XX_PLLCON0, NPCM7XX_CLK_S_PLL0, NPCM7XX_CLK_S_REFCLK, 0, -1},

	{NPCM7XX_PLLCON1, NPCM7XX_CLK_S_PLL1,
	NPCM7XX_CLK_S_REFCLK, 0, -1},

	{NPCM7XX_PLLCON2, NPCM7XX_CLK_S_PLL2,
	NPCM7XX_CLK_S_REFCLK, 0, -1},

	{NPCM7XX_PLLCONG, NPCM7XX_CLK_S_PLL_GFX,
	NPCM7XX_CLK_S_REFCLK, 0, -1},
};

static const struct npcm7xx_clk_mux_data npcm7xx_muxes[] __initconst = {
	{0, GENMASK(1, 0), cpuck_mux_table, NPCM7XX_CLK_S_CPU_MUX,
	cpuck_mux_parents, ARRAY_SIZE(cpuck_mux_parents), CLK_IS_CRITICAL,
	NPCM7XX_CLK_CPU},

	{4, GENMASK(1, 0), pixcksel_mux_table, NPCM7XX_CLK_S_PIX_MUX,
	pixcksel_mux_parents, ARRAY_SIZE(pixcksel_mux_parents), 0,
	NPCM7XX_CLK_GFX_PIXEL},

	{6, GENMASK(1, 0), pll_mux_table, NPCM7XX_CLK_S_SD_MUX,
	pll_mux_parents, ARRAY_SIZE(pll_mux_parents), 0, -1},

	{8, GENMASK(1, 0), pll_mux_table, NPCM7XX_CLK_S_UART_MUX,
	pll_mux_parents, ARRAY_SIZE(pll_mux_parents), 0, -1},

	{10, GENMASK(1, 0), sucksel_mux_table, NPCM7XX_CLK_S_SU_MUX,
	sucksel_mux_parents, ARRAY_SIZE(sucksel_mux_parents), 0, -1},

	{12, GENMASK(1, 0), mccksel_mux_table, NPCM7XX_CLK_S_MC_MUX,
	mccksel_mux_parents, ARRAY_SIZE(mccksel_mux_parents), 0, -1},

	{14, GENMASK(1, 0), pll_mux_table, NPCM7XX_CLK_S_TIM_MUX,
	pll_mux_parents, ARRAY_SIZE(pll_mux_parents), 0, -1},

	{16, GENMASK(1, 0), pll_mux_table, NPCM7XX_CLK_S_GFX_MUX,
	pll_mux_parents, ARRAY_SIZE(pll_mux_parents), 0, -1},

	{18, GENMASK(2, 0), clkoutsel_mux_table, NPCM7XX_CLK_S_CLKOUT_MUX,
	clkoutsel_mux_parents, ARRAY_SIZE(clkoutsel_mux_parents), 0, -1},

	{21, GENMASK(1, 0), gfxmsel_mux_table, NPCM7XX_CLK_S_GFXM_MUX,
	gfxmsel_mux_parents, ARRAY_SIZE(gfxmsel_mux_parents), 0, -1},

	{23, GENMASK(1, 0), dvcssel_mux_table, NPCM7XX_CLK_S_DVC_MUX,
	dvcssel_mux_parents, ARRAY_SIZE(dvcssel_mux_parents), 0, -1},
};

/* configurable dividers: */
static const struct npcm7xx_clk_div_data npcm7xx_divs[] __initconst = {
	{NPCM7XX_CLKDIV1, 28, 3, NPCM7XX_CLK_S_ADC,
	NPCM7XX_CLK_S_TIMER, CLK_DIVIDER_POWER_OF_TWO, 0, NPCM7XX_CLK_ADC},
	/*30-28 ADCCKDIV*/
	{NPCM7XX_CLKDIV1, 26, 2, NPCM7XX_CLK_S_AHB,
	NPCM7XX_CLK_S_AXI, 0, CLK_IS_CRITICAL, NPCM7XX_CLK_AHB},
	/*27-26 CLK4DIV*/
	{NPCM7XX_CLKDIV1, 21, 5, NPCM7XX_CLK_S_TIMER,
	NPCM7XX_CLK_S_TIM_MUX, 0, 0, NPCM7XX_CLK_TIMER},
	/*25-21 TIMCKDIV*/
	{NPCM7XX_CLKDIV1, 16, 5, NPCM7XX_CLK_S_UART,
	NPCM7XX_CLK_S_UART_MUX, 0, 0, NPCM7XX_CLK_UART},
	/*20-16 UARTDIV*/
	{NPCM7XX_CLKDIV1, 11, 5, NPCM7XX_CLK_S_MMC,
	NPCM7XX_CLK_S_SD_MUX, 0, 0, NPCM7XX_CLK_MMC},
	/*15-11 MMCCKDIV*/
	{NPCM7XX_CLKDIV1, 6, 5, NPCM7XX_CLK_S_SPI3,
	NPCM7XX_CLK_S_AHB, 0, 0, NPCM7XX_CLK_SPI3},
	/*10-6 AHB3CKDIV*/
	{NPCM7XX_CLKDIV1, 2, 4, NPCM7XX_CLK_S_PCI,
	NPCM7XX_CLK_S_GFX_MUX, 0, 0, NPCM7XX_CLK_PCI},
	/*5-2 PCICKDIV*/
	{NPCM7XX_CLKDIV1, 0, 1, NPCM7XX_CLK_S_AXI,
	NPCM7XX_CLK_S_CPU_MUX, CLK_DIVIDER_POWER_OF_TWO, CLK_IS_CRITICAL,
	NPCM7XX_CLK_AXI},/*0 CLK2DIV*/

	{NPCM7XX_CLKDIV2, 30, 2, NPCM7XX_CLK_S_APB4,
	NPCM7XX_CLK_S_AHB, CLK_DIVIDER_POWER_OF_TWO, 0, NPCM7XX_CLK_APB4},
	/*31-30 APB4CKDIV*/
	{NPCM7XX_CLKDIV2, 28, 2, NPCM7XX_CLK_S_APB3,
	NPCM7XX_CLK_S_AHB, CLK_DIVIDER_POWER_OF_TWO, 0, NPCM7XX_CLK_APB3},
	/*29-28 APB3CKDIV*/
	{NPCM7XX_CLKDIV2, 26, 2, NPCM7XX_CLK_S_APB2,
	NPCM7XX_CLK_S_AHB, CLK_DIVIDER_POWER_OF_TWO, 0, NPCM7XX_CLK_APB2},
	/*27-26 APB2CKDIV*/
	{NPCM7XX_CLKDIV2, 24, 2, NPCM7XX_CLK_S_APB1,
	NPCM7XX_CLK_S_AHB, CLK_DIVIDER_POWER_OF_TWO, 0, NPCM7XX_CLK_APB1},
	/*25-24 APB1CKDIV*/
	{NPCM7XX_CLKDIV2, 22, 2, NPCM7XX_CLK_S_APB5,
	NPCM7XX_CLK_S_AHB, CLK_DIVIDER_POWER_OF_TWO, 0, NPCM7XX_CLK_APB5},
	/*23-22 APB5CKDIV*/
	{NPCM7XX_CLKDIV2, 16, 5, NPCM7XX_CLK_S_CLKOUT,
	NPCM7XX_CLK_S_CLKOUT_MUX, 0, 0, NPCM7XX_CLK_CLKOUT},
	/*20-16 CLKOUTDIV*/
	{NPCM7XX_CLKDIV2, 13, 3, NPCM7XX_CLK_S_GFX,
	NPCM7XX_CLK_S_GFX_MUX, 0, 0, NPCM7XX_CLK_GFX},
	/*15-13 GFXCKDIV*/
	{NPCM7XX_CLKDIV2, 8, 5, NPCM7XX_CLK_S_USB_BRIDGE,
	NPCM7XX_CLK_S_SU_MUX, 0, 0, NPCM7XX_CLK_SU},
	/*12-8 SUCKDIV*/
	{NPCM7XX_CLKDIV2, 4, 4, NPCM7XX_CLK_S_USB_HOST,
	NPCM7XX_CLK_S_SU_MUX, 0, 0, NPCM7XX_CLK_SU48},
	/*7-4 SU48CKDIV*/
	{NPCM7XX_CLKDIV2, 0, 4, NPCM7XX_CLK_S_SDHC,
	NPCM7XX_CLK_S_SD_MUX, 0, 0, NPCM7XX_CLK_SDHC}
	,/*3-0 SD1CKDIV*/

	{NPCM7XX_CLKDIV3, 6, 5, NPCM7XX_CLK_S_SPI0,
	NPCM7XX_CLK_S_AHB, 0, 0, NPCM7XX_CLK_SPI0},
	/*10-6 SPI0CKDV*/
	{NPCM7XX_CLKDIV3, 1, 5, NPCM7XX_CLK_S_SPIX,
	NPCM7XX_CLK_S_AHB, 0, 0, NPCM7XX_CLK_SPIX},
	/*5-1 SPIXCKDV*/

};

static DEFINE_SPINLOCK(npcm7xx_clk_lock);

static void __init npcm7xx_clk_init(struct device_node *clk_np)
{
	struct clk_hw_onecell_data *npcm7xx_clk_data;
	void __iomem *clk_base;
	struct resource res;
	struct clk_hw *hw;
	int ret;
	int i;

	ret = of_address_to_resource(clk_np, 0, &res);
	if (ret) {
		pr_err("%pOFn: failed to get resource, ret %d\n", clk_np,
			ret);
		return;
	}

	clk_base = ioremap(res.start, resource_size(&res));
	if (!clk_base)
		goto npcm7xx_init_error;

	npcm7xx_clk_data = kzalloc(struct_size(npcm7xx_clk_data, hws,
				   NPCM7XX_NUM_CLOCKS), GFP_KERNEL);
	if (!npcm7xx_clk_data)
		goto npcm7xx_init_np_err;

	npcm7xx_clk_data->num = NPCM7XX_NUM_CLOCKS;

	for (i = 0; i < NPCM7XX_NUM_CLOCKS; i++)
		npcm7xx_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);

	/* Register plls */
	for (i = 0; i < ARRAY_SIZE(npcm7xx_plls); i++) {
		const struct npcm7xx_clk_pll_data *pll_data = &npcm7xx_plls[i];

		hw = npcm7xx_clk_register_pll(clk_base + pll_data->reg,
			pll_data->name, pll_data->parent_name, pll_data->flags);
		if (IS_ERR(hw)) {
			pr_err("npcm7xx_clk: Can't register pll\n");
			goto npcm7xx_init_fail;
		}

		if (pll_data->onecell_idx >= 0)
			npcm7xx_clk_data->hws[pll_data->onecell_idx] = hw;
	}

	/* Register fixed dividers */
	hw = clk_hw_register_fixed_factor(NULL, NPCM7XX_CLK_S_PLL1_DIV2,
			NPCM7XX_CLK_S_PLL1, 0, 1, 2);
	if (IS_ERR(hw)) {
		pr_err("npcm7xx_clk: Can't register fixed div\n");
		goto npcm7xx_init_fail;
	}

	hw = clk_hw_register_fixed_factor(NULL, NPCM7XX_CLK_S_PLL2_DIV2,
			NPCM7XX_CLK_S_PLL2, 0, 1, 2);
	if (IS_ERR(hw)) {
		pr_err("npcm7xx_clk: Can't register div2\n");
		goto npcm7xx_init_fail;
	}

	/* Register muxes */
	for (i = 0; i < ARRAY_SIZE(npcm7xx_muxes); i++) {
		const struct npcm7xx_clk_mux_data *mux_data = &npcm7xx_muxes[i];

		hw = clk_hw_register_mux_table(NULL,
			mux_data->name,
			mux_data->parent_names, mux_data->num_parents,
			mux_data->flags, clk_base + NPCM7XX_CLKSEL,
			mux_data->shift, mux_data->mask, 0,
			mux_data->table, &npcm7xx_clk_lock);

		if (IS_ERR(hw)) {
			pr_err("npcm7xx_clk: Can't register mux\n");
			goto npcm7xx_init_fail;
		}

		if (mux_data->onecell_idx >= 0)
			npcm7xx_clk_data->hws[mux_data->onecell_idx] = hw;
	}

	/* Register clock dividers specified in npcm7xx_divs */
	for (i = 0; i < ARRAY_SIZE(npcm7xx_divs); i++) {
		const struct npcm7xx_clk_div_data *div_data = &npcm7xx_divs[i];

		hw = clk_hw_register_divider(NULL, div_data->name,
				div_data->parent_name,
				div_data->flags,
				clk_base + div_data->reg,
				div_data->shift, div_data->width,
				div_data->clk_divider_flags, &npcm7xx_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("npcm7xx_clk: Can't register div table\n");
			goto npcm7xx_init_fail;
		}

		if (div_data->onecell_idx >= 0)
			npcm7xx_clk_data->hws[div_data->onecell_idx] = hw;
	}

	ret = of_clk_add_hw_provider(clk_np, of_clk_hw_onecell_get,
					npcm7xx_clk_data);
	if (ret)
		pr_err("failed to add DT provider: %d\n", ret);

	of_node_put(clk_np);

	return;

npcm7xx_init_fail:
	kfree(npcm7xx_clk_data->hws);
npcm7xx_init_np_err:
	iounmap(clk_base);
npcm7xx_init_error:
	of_node_put(clk_np);
}
CLK_OF_DECLARE(npcm7xx_clk_init, "nuvoton,npcm750-clk", npcm7xx_clk_init);
