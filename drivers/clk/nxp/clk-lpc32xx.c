/*
 * Copyright 2015 Vladimir Zapolskiy <vz@mleia.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/lpc32xx-clock.h>

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/* Common bitfield definitions for x397 PLL (lock), USB PLL and HCLK PLL */
#define PLL_CTRL_ENABLE			BIT(16)
#define PLL_CTRL_BYPASS			BIT(15)
#define PLL_CTRL_DIRECT			BIT(14)
#define PLL_CTRL_FEEDBACK		BIT(13)
#define PLL_CTRL_POSTDIV		(BIT(12)|BIT(11))
#define PLL_CTRL_PREDIV			(BIT(10)|BIT(9))
#define PLL_CTRL_FEEDDIV		(0xFF << 1)
#define PLL_CTRL_LOCK			BIT(0)

/* Clock registers on System Control Block */
#define LPC32XX_CLKPWR_DEBUG_CTRL	0x00
#define LPC32XX_CLKPWR_USB_DIV		0x1C
#define LPC32XX_CLKPWR_HCLKDIV_CTRL	0x40
#define LPC32XX_CLKPWR_PWR_CTRL		0x44
#define LPC32XX_CLKPWR_PLL397_CTRL	0x48
#define LPC32XX_CLKPWR_OSC_CTRL		0x4C
#define LPC32XX_CLKPWR_SYSCLK_CTRL	0x50
#define LPC32XX_CLKPWR_LCDCLK_CTRL	0x54
#define LPC32XX_CLKPWR_HCLKPLL_CTRL	0x58
#define LPC32XX_CLKPWR_ADCCLK_CTRL1	0x60
#define LPC32XX_CLKPWR_USB_CTRL		0x64
#define LPC32XX_CLKPWR_SSP_CTRL		0x78
#define LPC32XX_CLKPWR_I2S_CTRL		0x7C
#define LPC32XX_CLKPWR_MS_CTRL		0x80
#define LPC32XX_CLKPWR_MACCLK_CTRL	0x90
#define LPC32XX_CLKPWR_TEST_CLK_CTRL	0xA4
#define LPC32XX_CLKPWR_I2CCLK_CTRL	0xAC
#define LPC32XX_CLKPWR_KEYCLK_CTRL	0xB0
#define LPC32XX_CLKPWR_ADCCLK_CTRL	0xB4
#define LPC32XX_CLKPWR_PWMCLK_CTRL	0xB8
#define LPC32XX_CLKPWR_TIMCLK_CTRL	0xBC
#define LPC32XX_CLKPWR_TIMCLK_CTRL1	0xC0
#define LPC32XX_CLKPWR_SPI_CTRL		0xC4
#define LPC32XX_CLKPWR_FLASHCLK_CTRL	0xC8
#define LPC32XX_CLKPWR_UART3_CLK_CTRL	0xD0
#define LPC32XX_CLKPWR_UART4_CLK_CTRL	0xD4
#define LPC32XX_CLKPWR_UART5_CLK_CTRL	0xD8
#define LPC32XX_CLKPWR_UART6_CLK_CTRL	0xDC
#define LPC32XX_CLKPWR_IRDA_CLK_CTRL	0xE0
#define LPC32XX_CLKPWR_UART_CLK_CTRL	0xE4
#define LPC32XX_CLKPWR_DMA_CLK_CTRL	0xE8

/* Clock registers on USB controller */
#define LPC32XX_USB_CLK_CTRL		0xF4
#define LPC32XX_USB_CLK_STS		0xF8

static struct regmap_config lpc32xx_scb_regmap_config = {
	.name = "scb",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = 0x114,
	.fast_io = true,
};

static struct regmap *clk_regmap;
static void __iomem *usb_clk_vbase;

enum {
	LPC32XX_USB_CLK_OTG = LPC32XX_USB_CLK_HOST + 1,
	LPC32XX_USB_CLK_AHB,

	LPC32XX_USB_CLK_MAX = LPC32XX_USB_CLK_AHB + 1,
};

enum {
	/* Start from the last defined clock in dt bindings */
	LPC32XX_CLK_ADC_DIV = LPC32XX_CLK_PERIPH + 1,
	LPC32XX_CLK_ADC_RTC,
	LPC32XX_CLK_TEST1,
	LPC32XX_CLK_TEST2,

	/* System clocks, PLL 397x and HCLK PLL clocks */
	LPC32XX_CLK_OSC,
	LPC32XX_CLK_SYS,
	LPC32XX_CLK_PLL397X,
	LPC32XX_CLK_HCLK_DIV_PERIPH,
	LPC32XX_CLK_HCLK_DIV,
	LPC32XX_CLK_HCLK,
	LPC32XX_CLK_ARM,
	LPC32XX_CLK_ARM_VFP,

	/* USB clocks */
	LPC32XX_CLK_USB_PLL,
	LPC32XX_CLK_USB_DIV,
	LPC32XX_CLK_USB,

	/* Only one control PWR_CTRL[10] for both muxes */
	LPC32XX_CLK_PERIPH_HCLK_MUX,
	LPC32XX_CLK_PERIPH_ARM_MUX,

	/* Only one control PWR_CTRL[2] for all three muxes */
	LPC32XX_CLK_SYSCLK_PERIPH_MUX,
	LPC32XX_CLK_SYSCLK_HCLK_MUX,
	LPC32XX_CLK_SYSCLK_ARM_MUX,

	/* Two clock sources external to the driver */
	LPC32XX_CLK_XTAL_32K,
	LPC32XX_CLK_XTAL,

	/* Renumbered USB clocks, may have a parent from SCB table */
	LPC32XX_CLK_USB_OFFSET,
	LPC32XX_CLK_USB_I2C = LPC32XX_USB_CLK_I2C + LPC32XX_CLK_USB_OFFSET,
	LPC32XX_CLK_USB_DEV = LPC32XX_USB_CLK_DEVICE + LPC32XX_CLK_USB_OFFSET,
	LPC32XX_CLK_USB_HOST = LPC32XX_USB_CLK_HOST + LPC32XX_CLK_USB_OFFSET,
	LPC32XX_CLK_USB_OTG = LPC32XX_USB_CLK_OTG + LPC32XX_CLK_USB_OFFSET,
	LPC32XX_CLK_USB_AHB = LPC32XX_USB_CLK_AHB + LPC32XX_CLK_USB_OFFSET,

	/* Stub for composite clocks */
	LPC32XX_CLK__NULL,

	/* Subclocks of composite clocks, clocks above are for CCF */
	LPC32XX_CLK_PWM1_MUX,
	LPC32XX_CLK_PWM1_DIV,
	LPC32XX_CLK_PWM1_GATE,
	LPC32XX_CLK_PWM2_MUX,
	LPC32XX_CLK_PWM2_DIV,
	LPC32XX_CLK_PWM2_GATE,
	LPC32XX_CLK_UART3_MUX,
	LPC32XX_CLK_UART3_DIV,
	LPC32XX_CLK_UART3_GATE,
	LPC32XX_CLK_UART4_MUX,
	LPC32XX_CLK_UART4_DIV,
	LPC32XX_CLK_UART4_GATE,
	LPC32XX_CLK_UART5_MUX,
	LPC32XX_CLK_UART5_DIV,
	LPC32XX_CLK_UART5_GATE,
	LPC32XX_CLK_UART6_MUX,
	LPC32XX_CLK_UART6_DIV,
	LPC32XX_CLK_UART6_GATE,
	LPC32XX_CLK_TEST1_MUX,
	LPC32XX_CLK_TEST1_GATE,
	LPC32XX_CLK_TEST2_MUX,
	LPC32XX_CLK_TEST2_GATE,
	LPC32XX_CLK_USB_DIV_DIV,
	LPC32XX_CLK_USB_DIV_GATE,
	LPC32XX_CLK_SD_DIV,
	LPC32XX_CLK_SD_GATE,
	LPC32XX_CLK_LCD_DIV,
	LPC32XX_CLK_LCD_GATE,

	LPC32XX_CLK_HW_MAX,
	LPC32XX_CLK_MAX = LPC32XX_CLK_SYSCLK_ARM_MUX + 1,
	LPC32XX_CLK_CCF_MAX = LPC32XX_CLK_USB_AHB + 1,
};

static struct clk *clk[LPC32XX_CLK_MAX];
static struct clk_onecell_data clk_data = {
	.clks = clk,
	.clk_num = LPC32XX_CLK_MAX,
};

static struct clk *usb_clk[LPC32XX_USB_CLK_MAX];
static struct clk_onecell_data usb_clk_data = {
	.clks = usb_clk,
	.clk_num = LPC32XX_USB_CLK_MAX,
};

#define LPC32XX_CLK_PARENTS_MAX			5

struct clk_proto_t {
	const char *name;
	const u8 parents[LPC32XX_CLK_PARENTS_MAX];
	u8 num_parents;
	unsigned long flags;
};

#define CLK_PREFIX(LITERAL)		LPC32XX_CLK_ ## LITERAL
#define NUMARGS(...)	(sizeof((int[]){__VA_ARGS__})/sizeof(int))

#define LPC32XX_CLK_DEFINE(_idx, _name, _flags, ...)		\
	[CLK_PREFIX(_idx)] = {					\
		.name = _name,					\
		.flags = _flags,				\
		.parents = { __VA_ARGS__ },			\
		.num_parents = NUMARGS(__VA_ARGS__),		\
	 }

static const struct clk_proto_t clk_proto[LPC32XX_CLK_CCF_MAX] __initconst = {
	LPC32XX_CLK_DEFINE(XTAL, "xtal", 0x0),
	LPC32XX_CLK_DEFINE(XTAL_32K, "xtal_32k", 0x0),

	LPC32XX_CLK_DEFINE(RTC, "rtc", 0x0, LPC32XX_CLK_XTAL_32K),
	LPC32XX_CLK_DEFINE(OSC, "osc", CLK_IGNORE_UNUSED, LPC32XX_CLK_XTAL),
	LPC32XX_CLK_DEFINE(SYS, "sys", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_OSC, LPC32XX_CLK_PLL397X),
	LPC32XX_CLK_DEFINE(PLL397X, "pll_397x", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_RTC),
	LPC32XX_CLK_DEFINE(HCLK_PLL, "hclk_pll", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYS),
	LPC32XX_CLK_DEFINE(HCLK_DIV_PERIPH, "hclk_div_periph",
		CLK_IGNORE_UNUSED, LPC32XX_CLK_HCLK_PLL),
	LPC32XX_CLK_DEFINE(HCLK_DIV, "hclk_div", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_HCLK_PLL),
	LPC32XX_CLK_DEFINE(HCLK, "hclk", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_PERIPH_HCLK_MUX),
	LPC32XX_CLK_DEFINE(PERIPH, "pclk", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYSCLK_PERIPH_MUX),
	LPC32XX_CLK_DEFINE(ARM, "arm", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_PERIPH_ARM_MUX),

	LPC32XX_CLK_DEFINE(PERIPH_HCLK_MUX, "periph_hclk_mux",
		CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYSCLK_HCLK_MUX, LPC32XX_CLK_SYSCLK_PERIPH_MUX),
	LPC32XX_CLK_DEFINE(PERIPH_ARM_MUX, "periph_arm_mux", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYSCLK_ARM_MUX, LPC32XX_CLK_SYSCLK_PERIPH_MUX),
	LPC32XX_CLK_DEFINE(SYSCLK_PERIPH_MUX, "sysclk_periph_mux",
		CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYS, LPC32XX_CLK_HCLK_DIV_PERIPH),
	LPC32XX_CLK_DEFINE(SYSCLK_HCLK_MUX, "sysclk_hclk_mux",
		CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYS, LPC32XX_CLK_HCLK_DIV),
	LPC32XX_CLK_DEFINE(SYSCLK_ARM_MUX, "sysclk_arm_mux", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_SYS, LPC32XX_CLK_HCLK_PLL),

	LPC32XX_CLK_DEFINE(ARM_VFP, "vfp9", CLK_IGNORE_UNUSED,
		LPC32XX_CLK_ARM),
	LPC32XX_CLK_DEFINE(USB_PLL, "usb_pll",
		CLK_SET_RATE_GATE | CLK_SET_RATE_PARENT, LPC32XX_CLK_USB_DIV),
	LPC32XX_CLK_DEFINE(USB_DIV, "usb_div", 0x0, LPC32XX_CLK_OSC),
	LPC32XX_CLK_DEFINE(USB, "usb", 0x0, LPC32XX_CLK_USB_PLL),
	LPC32XX_CLK_DEFINE(DMA, "dma", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(MLC, "mlc", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(SLC, "slc", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(LCD, "lcd", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(MAC, "mac", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(SD, "sd", 0x0, LPC32XX_CLK_ARM),
	LPC32XX_CLK_DEFINE(DDRAM, "ddram", CLK_GET_RATE_NOCACHE,
		LPC32XX_CLK_SYSCLK_ARM_MUX),
	LPC32XX_CLK_DEFINE(SSP0, "ssp0", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(SSP1, "ssp1", 0x0, LPC32XX_CLK_HCLK),

	/*
	 * CLK_GET_RATE_NOCACHE is needed, if UART clock is disabled, its
	 * divider register does not contain information about selected rate.
	 */
	LPC32XX_CLK_DEFINE(UART3, "uart3", CLK_GET_RATE_NOCACHE,
		LPC32XX_CLK_PERIPH, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(UART4, "uart4", CLK_GET_RATE_NOCACHE,
		LPC32XX_CLK_PERIPH, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(UART5, "uart5", CLK_GET_RATE_NOCACHE,
		LPC32XX_CLK_PERIPH, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(UART6, "uart6", CLK_GET_RATE_NOCACHE,
		LPC32XX_CLK_PERIPH, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(IRDA, "irda", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(I2C1, "i2c1", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(I2C2, "i2c2", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(TIMER0, "timer0", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(TIMER1, "timer1", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(TIMER2, "timer2", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(TIMER3, "timer3", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(TIMER4, "timer4", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(TIMER5, "timer5", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(WDOG, "watchdog", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(I2S0, "i2s0", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(I2S1, "i2s1", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(SPI1, "spi1", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(SPI2, "spi2", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(MCPWM, "mcpwm", 0x0, LPC32XX_CLK_HCLK),
	LPC32XX_CLK_DEFINE(HSTIMER, "hstimer", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(KEY, "key", 0x0, LPC32XX_CLK_RTC),
	LPC32XX_CLK_DEFINE(PWM1, "pwm1", 0x0,
		LPC32XX_CLK_RTC, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(PWM2, "pwm2", 0x0,
		LPC32XX_CLK_RTC, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(ADC, "adc", 0x0,
		LPC32XX_CLK_ADC_RTC, LPC32XX_CLK_ADC_DIV),
	LPC32XX_CLK_DEFINE(ADC_DIV, "adc_div", 0x0, LPC32XX_CLK_PERIPH),
	LPC32XX_CLK_DEFINE(ADC_RTC, "adc_rtc", 0x0, LPC32XX_CLK_RTC),
	LPC32XX_CLK_DEFINE(TEST1, "test1", 0x0,
		LPC32XX_CLK_PERIPH, LPC32XX_CLK_RTC, LPC32XX_CLK_OSC),
	LPC32XX_CLK_DEFINE(TEST2, "test2", 0x0,
		LPC32XX_CLK_HCLK, LPC32XX_CLK_PERIPH, LPC32XX_CLK_USB,
		LPC32XX_CLK_OSC, LPC32XX_CLK_PLL397X),

	/* USB controller clocks */
	LPC32XX_CLK_DEFINE(USB_AHB, "usb_ahb", 0x0, LPC32XX_CLK_USB),
	LPC32XX_CLK_DEFINE(USB_OTG, "usb_otg", 0x0, LPC32XX_CLK_USB_AHB),
	LPC32XX_CLK_DEFINE(USB_I2C, "usb_i2c", 0x0, LPC32XX_CLK_USB_AHB),
	LPC32XX_CLK_DEFINE(USB_DEV, "usb_dev", 0x0, LPC32XX_CLK_USB_OTG),
	LPC32XX_CLK_DEFINE(USB_HOST, "usb_host", 0x0, LPC32XX_CLK_USB_OTG),
};

struct lpc32xx_clk {
	struct clk_hw hw;
	u32 reg;
	u32 enable;
	u32 enable_mask;
	u32 disable;
	u32 disable_mask;
	u32 busy;
	u32 busy_mask;
};

enum clk_pll_mode {
	PLL_UNKNOWN,
	PLL_DIRECT,
	PLL_BYPASS,
	PLL_DIRECT_BYPASS,
	PLL_INTEGER,
	PLL_NON_INTEGER,
};

struct lpc32xx_pll_clk {
	struct clk_hw hw;
	u32 reg;
	u32 enable;
	unsigned long m_div;
	unsigned long n_div;
	unsigned long p_div;
	enum clk_pll_mode mode;
};

struct lpc32xx_usb_clk {
	struct clk_hw hw;
	u32 ctrl_enable;
	u32 ctrl_disable;
	u32 ctrl_mask;
	u32 enable;
	u32 busy;
};

struct lpc32xx_clk_mux {
	struct clk_hw	hw;
	u32		reg;
	u32		mask;
	u8		shift;
	u32		*table;
	u8		flags;
};

struct lpc32xx_clk_div {
	struct clk_hw	hw;
	u32		reg;
	u8		shift;
	u8		width;
	const struct clk_div_table	*table;
	u8		flags;
};

struct lpc32xx_clk_gate {
	struct clk_hw	hw;
	u32		reg;
	u8		bit_idx;
	u8		flags;
};

#define to_lpc32xx_clk(_hw)	container_of(_hw, struct lpc32xx_clk, hw)
#define to_lpc32xx_pll_clk(_hw)	container_of(_hw, struct lpc32xx_pll_clk, hw)
#define to_lpc32xx_usb_clk(_hw)	container_of(_hw, struct lpc32xx_usb_clk, hw)
#define to_lpc32xx_mux(_hw)	container_of(_hw, struct lpc32xx_clk_mux, hw)
#define to_lpc32xx_div(_hw)	container_of(_hw, struct lpc32xx_clk_div, hw)
#define to_lpc32xx_gate(_hw)	container_of(_hw, struct lpc32xx_clk_gate, hw)

static inline bool pll_is_valid(u64 val0, u64 val1, u64 min, u64 max)
{
	return (val0 >= (val1 * min) && val0 <= (val1 * max));
}

static inline u32 lpc32xx_usb_clk_read(struct lpc32xx_usb_clk *clk)
{
	return readl(usb_clk_vbase + LPC32XX_USB_CLK_STS);
}

static inline void lpc32xx_usb_clk_write(struct lpc32xx_usb_clk *clk, u32 val)
{
	writel(val, usb_clk_vbase + LPC32XX_USB_CLK_CTRL);
}

static int clk_mask_enable(struct clk_hw *hw)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);
	u32 val;

	regmap_read(clk_regmap, clk->reg, &val);

	if (clk->busy_mask && (val & clk->busy_mask) == clk->busy)
		return -EBUSY;

	return regmap_update_bits(clk_regmap, clk->reg,
				  clk->enable_mask, clk->enable);
}

static void clk_mask_disable(struct clk_hw *hw)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);

	regmap_update_bits(clk_regmap, clk->reg,
			   clk->disable_mask, clk->disable);
}

static int clk_mask_is_enabled(struct clk_hw *hw)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);
	u32 val;

	regmap_read(clk_regmap, clk->reg, &val);

	return ((val & clk->enable_mask) == clk->enable);
}

static const struct clk_ops clk_mask_ops = {
	.enable = clk_mask_enable,
	.disable = clk_mask_disable,
	.is_enabled = clk_mask_is_enabled,
};

static int clk_pll_enable(struct clk_hw *hw)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);
	u32 val, count;

	regmap_update_bits(clk_regmap, clk->reg, clk->enable, clk->enable);

	for (count = 0; count < 1000; count++) {
		regmap_read(clk_regmap, clk->reg, &val);
		if (val & PLL_CTRL_LOCK)
			break;
	}

	if (val & PLL_CTRL_LOCK)
		return 0;

	return -ETIMEDOUT;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);

	regmap_update_bits(clk_regmap, clk->reg, clk->enable, 0x0);
}

static int clk_pll_is_enabled(struct clk_hw *hw)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);
	u32 val;

	regmap_read(clk_regmap, clk->reg, &val);

	val &= clk->enable | PLL_CTRL_LOCK;
	if (val == (clk->enable | PLL_CTRL_LOCK))
		return 1;

	return 0;
}

static unsigned long clk_pll_397x_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	return parent_rate * 397;
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);
	bool is_direct, is_bypass, is_feedback;
	unsigned long rate, cco_rate, ref_rate;
	u32 val;

	regmap_read(clk_regmap, clk->reg, &val);
	is_direct = val & PLL_CTRL_DIRECT;
	is_bypass = val & PLL_CTRL_BYPASS;
	is_feedback = val & PLL_CTRL_FEEDBACK;

	clk->m_div = ((val & PLL_CTRL_FEEDDIV) >> 1) + 1;
	clk->n_div = ((val & PLL_CTRL_PREDIV) >> 9) + 1;
	clk->p_div = ((val & PLL_CTRL_POSTDIV) >> 11) + 1;

	if (is_direct && is_bypass) {
		clk->p_div = 0;
		clk->mode = PLL_DIRECT_BYPASS;
		return parent_rate;
	}
	if (is_bypass) {
		clk->mode = PLL_BYPASS;
		return parent_rate / (1 << clk->p_div);
	}
	if (is_direct) {
		clk->p_div = 0;
		clk->mode = PLL_DIRECT;
	}

	ref_rate = parent_rate / clk->n_div;
	rate = cco_rate = ref_rate * clk->m_div;

	if (!is_direct) {
		if (is_feedback) {
			cco_rate *= (1 << clk->p_div);
			clk->mode = PLL_INTEGER;
		} else {
			rate /= (1 << clk->p_div);
			clk->mode = PLL_NON_INTEGER;
		}
	}

	pr_debug("%s: %lu: 0x%x: %d/%d/%d, %lu/%lu/%d => %lu\n",
		 clk_hw_get_name(hw),
		 parent_rate, val, is_direct, is_bypass, is_feedback,
		 clk->n_div, clk->m_div, (1 << clk->p_div), rate);

	if (clk_pll_is_enabled(hw) &&
	    !(pll_is_valid(parent_rate, 1, 1000000, 20000000)
	      && pll_is_valid(cco_rate, 1, 156000000, 320000000)
	      && pll_is_valid(ref_rate, 1, 1000000, 27000000)))
		pr_err("%s: PLL clocks are not in valid ranges: %lu/%lu/%lu\n",
		       clk_hw_get_name(hw),
		       parent_rate, cco_rate, ref_rate);

	return rate;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);
	u32 val;
	unsigned long new_rate;

	/* Validate PLL clock parameters computed on round rate stage */
	switch (clk->mode) {
	case PLL_DIRECT:
		val = PLL_CTRL_DIRECT;
		val |= (clk->m_div - 1) << 1;
		val |= (clk->n_div - 1) << 9;
		new_rate = (parent_rate * clk->m_div) / clk->n_div;
		break;
	case PLL_BYPASS:
		val = PLL_CTRL_BYPASS;
		val |= (clk->p_div - 1) << 11;
		new_rate = parent_rate / (1 << (clk->p_div));
		break;
	case PLL_DIRECT_BYPASS:
		val = PLL_CTRL_DIRECT | PLL_CTRL_BYPASS;
		new_rate = parent_rate;
		break;
	case PLL_INTEGER:
		val = PLL_CTRL_FEEDBACK;
		val |= (clk->m_div - 1) << 1;
		val |= (clk->n_div - 1) << 9;
		val |= (clk->p_div - 1) << 11;
		new_rate = (parent_rate * clk->m_div) / clk->n_div;
		break;
	case PLL_NON_INTEGER:
		val = 0x0;
		val |= (clk->m_div - 1) << 1;
		val |= (clk->n_div - 1) << 9;
		val |= (clk->p_div - 1) << 11;
		new_rate = (parent_rate * clk->m_div) /
				(clk->n_div * (1 << clk->p_div));
		break;
	default:
		return -EINVAL;
	}

	/* Sanity check that round rate is equal to the requested one */
	if (new_rate != rate)
		return -EINVAL;

	return regmap_update_bits(clk_regmap, clk->reg, 0x1FFFF, val);
}

static long clk_hclk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);
	u64 m_i, o = rate, i = *parent_rate, d = (u64)rate << 6;
	u64 m = 0, n = 0, p = 0;
	int p_i, n_i;

	pr_debug("%s: %lu/%lu\n", clk_hw_get_name(hw), *parent_rate, rate);

	if (rate > 266500000)
		return -EINVAL;

	/* Have to check all 20 possibilities to find the minimal M */
	for (p_i = 4; p_i >= 0; p_i--) {
		for (n_i = 4; n_i > 0; n_i--) {
			m_i = div64_u64(o * n_i * (1 << p_i), i);

			/* Check for valid PLL parameter constraints */
			if (!(m_i && m_i <= 256
			      && pll_is_valid(i, n_i, 1000000, 27000000)
			      && pll_is_valid(i * m_i * (1 << p_i), n_i,
					      156000000, 320000000)))
				continue;

			/* Store some intermediate valid parameters */
			if (o * n_i * (1 << p_i) - i * m_i <= d) {
				m = m_i;
				n = n_i;
				p = p_i;
				d = o * n_i * (1 << p_i) - i * m_i;
			}
		}
	}

	if (d == (u64)rate << 6) {
		pr_err("%s: %lu: no valid PLL parameters are found\n",
		       clk_hw_get_name(hw), rate);
		return -EINVAL;
	}

	clk->m_div = m;
	clk->n_div = n;
	clk->p_div = p;

	/* Set only direct or non-integer mode of PLL */
	if (!p)
		clk->mode = PLL_DIRECT;
	else
		clk->mode = PLL_NON_INTEGER;

	o = div64_u64(i * m, n * (1 << p));

	if (!d)
		pr_debug("%s: %lu: found exact match: %llu/%llu/%llu\n",
			 clk_hw_get_name(hw), rate, m, n, p);
	else
		pr_debug("%s: %lu: found closest: %llu/%llu/%llu - %llu\n",
			 clk_hw_get_name(hw), rate, m, n, p, o);

	return o;
}

static long clk_usb_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct lpc32xx_pll_clk *clk = to_lpc32xx_pll_clk(hw);
	struct clk_hw *usb_div_hw, *osc_hw;
	u64 d_i, n_i, m, o;

	pr_debug("%s: %lu/%lu\n", clk_hw_get_name(hw), *parent_rate, rate);

	/*
	 * The only supported USB clock is 48MHz, with PLL internal constraints
	 * on Fclkin, Fcco and Fref this implies that Fcco must be 192MHz
	 * and post-divider must be 4, this slightly simplifies calculation of
	 * USB divider, USB PLL N and M parameters.
	 */
	if (rate != 48000000)
		return -EINVAL;

	/* USB divider clock */
	usb_div_hw = clk_hw_get_parent_by_index(hw, 0);
	if (!usb_div_hw)
		return -EINVAL;

	/* Main oscillator clock */
	osc_hw = clk_hw_get_parent_by_index(usb_div_hw, 0);
	if (!osc_hw)
		return -EINVAL;
	o = clk_hw_get_rate(osc_hw);	/* must be in range 1..20 MHz */

	/* Check if valid USB divider and USB PLL parameters exists */
	for (d_i = 16; d_i >= 1; d_i--) {
		for (n_i = 1; n_i <= 4; n_i++) {
			m = div64_u64(192000000 * d_i * n_i, o);
			if (!(m && m <= 256
			      && m * o == 192000000 * d_i * n_i
			      && pll_is_valid(o, d_i, 1000000, 20000000)
			      && pll_is_valid(o, d_i * n_i, 1000000, 27000000)))
				continue;

			clk->n_div = n_i;
			clk->m_div = m;
			clk->p_div = 2;
			clk->mode = PLL_NON_INTEGER;
			*parent_rate = div64_u64(o, d_i);

			return rate;
		}
	}

	return -EINVAL;
}

#define LPC32XX_DEFINE_PLL_OPS(_name, _rc, _sr, _rr)			\
	static const struct clk_ops clk_ ##_name ## _ops = {		\
		.enable = clk_pll_enable,				\
		.disable = clk_pll_disable,				\
		.is_enabled = clk_pll_is_enabled,			\
		.recalc_rate = _rc,					\
		.set_rate = _sr,					\
		.round_rate = _rr,					\
	}

LPC32XX_DEFINE_PLL_OPS(pll_397x, clk_pll_397x_recalc_rate, NULL, NULL);
LPC32XX_DEFINE_PLL_OPS(hclk_pll, clk_pll_recalc_rate,
		       clk_pll_set_rate, clk_hclk_pll_round_rate);
LPC32XX_DEFINE_PLL_OPS(usb_pll,  clk_pll_recalc_rate,
		       clk_pll_set_rate, clk_usb_pll_round_rate);

static int clk_ddram_is_enabled(struct clk_hw *hw)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);
	u32 val;

	regmap_read(clk_regmap, clk->reg, &val);
	val &= clk->enable_mask | clk->busy_mask;

	return (val == (BIT(7) | BIT(0)) ||
		val == (BIT(8) | BIT(1)));
}

static int clk_ddram_enable(struct clk_hw *hw)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);
	u32 val, hclk_div;

	regmap_read(clk_regmap, clk->reg, &val);
	hclk_div = val & clk->busy_mask;

	/*
	 * DDRAM clock must be 2 times higher than HCLK,
	 * this implies DDRAM clock can not be enabled,
	 * if HCLK clock rate is equal to ARM clock rate
	 */
	if (hclk_div == 0x0 || hclk_div == (BIT(1) | BIT(0)))
		return -EINVAL;

	return regmap_update_bits(clk_regmap, clk->reg,
				  clk->enable_mask, hclk_div << 7);
}

static unsigned long clk_ddram_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);
	u32 val;

	if (!clk_ddram_is_enabled(hw))
		return 0;

	regmap_read(clk_regmap, clk->reg, &val);
	val &= clk->enable_mask;

	return parent_rate / (val >> 7);
}

static const struct clk_ops clk_ddram_ops = {
	.enable = clk_ddram_enable,
	.disable = clk_mask_disable,
	.is_enabled = clk_ddram_is_enabled,
	.recalc_rate = clk_ddram_recalc_rate,
};

static unsigned long lpc32xx_clk_uart_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct lpc32xx_clk *clk = to_lpc32xx_clk(hw);
	u32 val, x, y;

	regmap_read(clk_regmap, clk->reg, &val);
	x = (val & 0xFF00) >> 8;
	y = val & 0xFF;

	if (x && y)
		return (parent_rate * x) / y;
	else
		return 0;
}

static const struct clk_ops lpc32xx_uart_div_ops = {
	.recalc_rate = lpc32xx_clk_uart_recalc_rate,
};

static const struct clk_div_table clk_hclk_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ },
};

static u32 test1_mux_table[] = { 0, 1, 2, };
static u32 test2_mux_table[] = { 0, 1, 2, 5, 7, };

static int clk_usb_enable(struct clk_hw *hw)
{
	struct lpc32xx_usb_clk *clk = to_lpc32xx_usb_clk(hw);
	u32 val, ctrl_val, count;

	pr_debug("%s: 0x%x\n", clk_hw_get_name(hw), clk->enable);

	if (clk->ctrl_mask) {
		regmap_read(clk_regmap, LPC32XX_CLKPWR_USB_CTRL, &ctrl_val);
		regmap_update_bits(clk_regmap, LPC32XX_CLKPWR_USB_CTRL,
				   clk->ctrl_mask, clk->ctrl_enable);
	}

	val = lpc32xx_usb_clk_read(clk);
	if (clk->busy && (val & clk->busy) == clk->busy) {
		if (clk->ctrl_mask)
			regmap_write(clk_regmap, LPC32XX_CLKPWR_USB_CTRL,
				     ctrl_val);
		return -EBUSY;
	}

	val |= clk->enable;
	lpc32xx_usb_clk_write(clk, val);

	for (count = 0; count < 1000; count++) {
		val = lpc32xx_usb_clk_read(clk);
		if ((val & clk->enable) == clk->enable)
			break;
	}

	if ((val & clk->enable) == clk->enable)
		return 0;

	if (clk->ctrl_mask)
		regmap_write(clk_regmap, LPC32XX_CLKPWR_USB_CTRL, ctrl_val);

	return -ETIMEDOUT;
}

static void clk_usb_disable(struct clk_hw *hw)
{
	struct lpc32xx_usb_clk *clk = to_lpc32xx_usb_clk(hw);
	u32 val = lpc32xx_usb_clk_read(clk);

	val &= ~clk->enable;
	lpc32xx_usb_clk_write(clk, val);

	if (clk->ctrl_mask)
		regmap_update_bits(clk_regmap, LPC32XX_CLKPWR_USB_CTRL,
				   clk->ctrl_mask, clk->ctrl_disable);
}

static int clk_usb_is_enabled(struct clk_hw *hw)
{
	struct lpc32xx_usb_clk *clk = to_lpc32xx_usb_clk(hw);
	u32 ctrl_val, val;

	if (clk->ctrl_mask) {
		regmap_read(clk_regmap, LPC32XX_CLKPWR_USB_CTRL, &ctrl_val);
		if ((ctrl_val & clk->ctrl_mask) != clk->ctrl_enable)
			return 0;
	}

	val = lpc32xx_usb_clk_read(clk);

	return ((val & clk->enable) == clk->enable);
}

static unsigned long clk_usb_i2c_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return clk_get_rate(clk[LPC32XX_CLK_PERIPH]);
}

static const struct clk_ops clk_usb_ops = {
	.enable = clk_usb_enable,
	.disable = clk_usb_disable,
	.is_enabled = clk_usb_is_enabled,
};

static const struct clk_ops clk_usb_i2c_ops = {
	.enable = clk_usb_enable,
	.disable = clk_usb_disable,
	.is_enabled = clk_usb_is_enabled,
	.recalc_rate = clk_usb_i2c_recalc_rate,
};

static int lpc32xx_clk_gate_enable(struct clk_hw *hw)
{
	struct lpc32xx_clk_gate *clk = to_lpc32xx_gate(hw);
	u32 mask = BIT(clk->bit_idx);
	u32 val = (clk->flags & CLK_GATE_SET_TO_DISABLE ? 0x0 : mask);

	return regmap_update_bits(clk_regmap, clk->reg, mask, val);
}

static void lpc32xx_clk_gate_disable(struct clk_hw *hw)
{
	struct lpc32xx_clk_gate *clk = to_lpc32xx_gate(hw);
	u32 mask = BIT(clk->bit_idx);
	u32 val = (clk->flags & CLK_GATE_SET_TO_DISABLE ? mask : 0x0);

	regmap_update_bits(clk_regmap, clk->reg, mask, val);
}

static int lpc32xx_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct lpc32xx_clk_gate *clk = to_lpc32xx_gate(hw);
	u32 val;
	bool is_set;

	regmap_read(clk_regmap, clk->reg, &val);
	is_set = val & BIT(clk->bit_idx);

	return (clk->flags & CLK_GATE_SET_TO_DISABLE ? !is_set : is_set);
}

static const struct clk_ops lpc32xx_clk_gate_ops = {
	.enable = lpc32xx_clk_gate_enable,
	.disable = lpc32xx_clk_gate_disable,
	.is_enabled = lpc32xx_clk_gate_is_enabled,
};

#define div_mask(width)	((1 << (width)) - 1)

static unsigned int _get_table_div(const struct clk_div_table *table,
							unsigned int val)
{
	const struct clk_div_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->val == val)
			return clkt->div;
	return 0;
}

static unsigned int _get_div(const struct clk_div_table *table,
			     unsigned int val, unsigned long flags, u8 width)
{
	if (flags & CLK_DIVIDER_ONE_BASED)
		return val;
	if (table)
		return _get_table_div(table, val);
	return val + 1;
}

static unsigned long clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct lpc32xx_clk_div *divider = to_lpc32xx_div(hw);
	unsigned int val;

	regmap_read(clk_regmap, divider->reg, &val);

	val >>= divider->shift;
	val &= div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags, divider->width);
}

static long clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct lpc32xx_clk_div *divider = to_lpc32xx_div(hw);
	unsigned int bestdiv;

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		regmap_read(clk_regmap, divider->reg, &bestdiv);
		bestdiv >>= divider->shift;
		bestdiv &= div_mask(divider->width);
		bestdiv = _get_div(divider->table, bestdiv, divider->flags,
			divider->width);
		return DIV_ROUND_UP(*prate, bestdiv);
	}

	return divider_round_rate(hw, rate, prate, divider->table,
				  divider->width, divider->flags);
}

static int clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct lpc32xx_clk_div *divider = to_lpc32xx_div(hw);
	unsigned int value;

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);

	return regmap_update_bits(clk_regmap, divider->reg,
				  div_mask(divider->width) << divider->shift,
				  value << divider->shift);
}

static const struct clk_ops lpc32xx_clk_divider_ops = {
	.recalc_rate = clk_divider_recalc_rate,
	.round_rate = clk_divider_round_rate,
	.set_rate = clk_divider_set_rate,
};

static u8 clk_mux_get_parent(struct clk_hw *hw)
{
	struct lpc32xx_clk_mux *mux = to_lpc32xx_mux(hw);
	u32 num_parents = clk_hw_get_num_parents(hw);
	u32 val;

	regmap_read(clk_regmap, mux->reg, &val);
	val >>= mux->shift;
	val &= mux->mask;

	if (mux->table) {
		u32 i;

		for (i = 0; i < num_parents; i++)
			if (mux->table[i] == val)
				return i;
		return -EINVAL;
	}

	if (val >= num_parents)
		return -EINVAL;

	return val;
}

static int clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct lpc32xx_clk_mux *mux = to_lpc32xx_mux(hw);

	if (mux->table)
		index = mux->table[index];

	return regmap_update_bits(clk_regmap, mux->reg,
			  mux->mask << mux->shift, index << mux->shift);
}

static const struct clk_ops lpc32xx_clk_mux_ro_ops = {
	.get_parent = clk_mux_get_parent,
};

static const struct clk_ops lpc32xx_clk_mux_ops = {
	.get_parent = clk_mux_get_parent,
	.set_parent = clk_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

enum lpc32xx_clk_type {
	CLK_FIXED,
	CLK_MUX,
	CLK_DIV,
	CLK_GATE,
	CLK_COMPOSITE,
	CLK_LPC32XX,
	CLK_LPC32XX_PLL,
	CLK_LPC32XX_USB,
};

struct clk_hw_proto0 {
	const struct clk_ops *ops;
	union {
		struct lpc32xx_pll_clk pll;
		struct lpc32xx_clk clk;
		struct lpc32xx_usb_clk usb_clk;
		struct lpc32xx_clk_mux mux;
		struct lpc32xx_clk_div div;
		struct lpc32xx_clk_gate gate;
	};
};

struct clk_hw_proto1 {
	struct clk_hw_proto0 *mux;
	struct clk_hw_proto0 *div;
	struct clk_hw_proto0 *gate;
};

struct clk_hw_proto {
	enum lpc32xx_clk_type type;

	union {
		struct clk_fixed_rate f;
		struct clk_hw_proto0 hw0;
		struct clk_hw_proto1 hw1;
	};
};

#define LPC32XX_DEFINE_FIXED(_idx, _rate)			\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_FIXED,						\
	{								\
		.f = {							\
			.fixed_rate = (_rate),				\
		},							\
	},								\
}

#define LPC32XX_DEFINE_PLL(_idx, _name, _reg, _enable)			\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_LPC32XX_PLL,					\
	{								\
		.hw0 = {						\
			.ops = &clk_ ##_name ## _ops,			\
			{						\
				.pll = {				\
					.reg = LPC32XX_CLKPWR_ ## _reg,	\
					.enable = (_enable),		\
				},					\
			},						\
		},							\
	},								\
}

#define LPC32XX_DEFINE_MUX(_idx, _reg, _shift, _mask, _table, _flags)	\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_MUX,						\
	{								\
		.hw0 = {						\
			.ops = (_flags & CLK_MUX_READ_ONLY ?		\
				&lpc32xx_clk_mux_ro_ops :		\
				&lpc32xx_clk_mux_ops),			\
			{						\
				.mux = {				\
					.reg = LPC32XX_CLKPWR_ ## _reg,	\
					.mask = (_mask),		\
					.shift = (_shift),		\
					.table = (_table),		\
					.flags = (_flags),		\
				},					\
			},						\
		},							\
	},								\
}

#define LPC32XX_DEFINE_DIV(_idx, _reg, _shift, _width, _table, _flags)	\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_DIV,						\
	{								\
		.hw0 = {						\
			.ops = &lpc32xx_clk_divider_ops,		\
			{						\
				.div = {				\
					.reg = LPC32XX_CLKPWR_ ## _reg,	\
					.shift = (_shift),		\
					.width = (_width),		\
					.table = (_table),		\
					.flags = (_flags),		\
				 },					\
			},						\
		 },							\
	},								\
}

#define LPC32XX_DEFINE_GATE(_idx, _reg, _bit, _flags)			\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_GATE,						\
	{								\
		.hw0 = {						\
			.ops = &lpc32xx_clk_gate_ops,			\
			{						\
				.gate = {				\
					.reg = LPC32XX_CLKPWR_ ## _reg,	\
					.bit_idx = (_bit),		\
					.flags = (_flags),		\
				},					\
			},						\
		},							\
	},								\
}

#define LPC32XX_DEFINE_CLK(_idx, _reg, _e, _em, _d, _dm, _b, _bm, _ops)	\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_LPC32XX,						\
	{								\
		.hw0 = {						\
			.ops = &(_ops),					\
			{						\
				.clk = {				\
					.reg = LPC32XX_CLKPWR_ ## _reg,	\
					.enable = (_e),			\
					.enable_mask = (_em),		\
					.disable = (_d),		\
					.disable_mask = (_dm),		\
					.busy = (_b),			\
					.busy_mask = (_bm),		\
				},					\
			},						\
		},							\
	},								\
}

#define LPC32XX_DEFINE_USB(_idx, _ce, _cd, _cm, _e, _b, _ops)		\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_LPC32XX_USB,					\
	{								\
		.hw0 = {						\
			.ops = &(_ops),					\
			{						\
				.usb_clk = {				\
					.ctrl_enable = (_ce),		\
					.ctrl_disable = (_cd),		\
					.ctrl_mask = (_cm),		\
					.enable = (_e),			\
					.busy = (_b),			\
				}					\
			},						\
		}							\
	},								\
}

#define LPC32XX_DEFINE_COMPOSITE(_idx, _mux, _div, _gate)		\
[CLK_PREFIX(_idx)] = {							\
	.type = CLK_COMPOSITE,						\
	{								\
		.hw1 = {						\
		.mux = (CLK_PREFIX(_mux) == LPC32XX_CLK__NULL ? NULL :	\
			&clk_hw_proto[CLK_PREFIX(_mux)].hw0),		\
		.div = (CLK_PREFIX(_div) == LPC32XX_CLK__NULL ? NULL :	\
			&clk_hw_proto[CLK_PREFIX(_div)].hw0),		\
		.gate = (CLK_PREFIX(_gate) == LPC32XX_CLK__NULL ? NULL :\
			 &clk_hw_proto[CLK_PREFIX(_gate)].hw0),		\
		},							\
	},								\
}

static struct clk_hw_proto clk_hw_proto[LPC32XX_CLK_HW_MAX] = {
	LPC32XX_DEFINE_FIXED(RTC, 32768),
	LPC32XX_DEFINE_PLL(PLL397X, pll_397x, HCLKPLL_CTRL, BIT(1)),
	LPC32XX_DEFINE_PLL(HCLK_PLL, hclk_pll, HCLKPLL_CTRL, PLL_CTRL_ENABLE),
	LPC32XX_DEFINE_PLL(USB_PLL, usb_pll, USB_CTRL, PLL_CTRL_ENABLE),
	LPC32XX_DEFINE_GATE(OSC, OSC_CTRL, 0, CLK_GATE_SET_TO_DISABLE),
	LPC32XX_DEFINE_GATE(USB, USB_CTRL, 18, 0),

	LPC32XX_DEFINE_DIV(HCLK_DIV_PERIPH, HCLKDIV_CTRL, 2, 5, NULL,
			   CLK_DIVIDER_READ_ONLY),
	LPC32XX_DEFINE_DIV(HCLK_DIV, HCLKDIV_CTRL, 0, 2, clk_hclk_div_table,
			   CLK_DIVIDER_READ_ONLY),

	/* Register 3 read-only muxes with a single control PWR_CTRL[2] */
	LPC32XX_DEFINE_MUX(SYSCLK_PERIPH_MUX, PWR_CTRL, 2, 0x1, NULL,
			   CLK_MUX_READ_ONLY),
	LPC32XX_DEFINE_MUX(SYSCLK_HCLK_MUX, PWR_CTRL, 2, 0x1, NULL,
			   CLK_MUX_READ_ONLY),
	LPC32XX_DEFINE_MUX(SYSCLK_ARM_MUX, PWR_CTRL, 2, 0x1, NULL,
			   CLK_MUX_READ_ONLY),
	/* Register 2 read-only muxes with a single control PWR_CTRL[10] */
	LPC32XX_DEFINE_MUX(PERIPH_HCLK_MUX, PWR_CTRL, 10, 0x1, NULL,
			   CLK_MUX_READ_ONLY),
	LPC32XX_DEFINE_MUX(PERIPH_ARM_MUX, PWR_CTRL, 10, 0x1, NULL,
			   CLK_MUX_READ_ONLY),

	/* 3 always on gates with a single control PWR_CTRL[0] same as OSC */
	LPC32XX_DEFINE_GATE(PERIPH, PWR_CTRL, 0, CLK_GATE_SET_TO_DISABLE),
	LPC32XX_DEFINE_GATE(HCLK, PWR_CTRL, 0, CLK_GATE_SET_TO_DISABLE),
	LPC32XX_DEFINE_GATE(ARM, PWR_CTRL, 0, CLK_GATE_SET_TO_DISABLE),

	LPC32XX_DEFINE_GATE(ARM_VFP, DEBUG_CTRL, 4, 0),
	LPC32XX_DEFINE_GATE(DMA, DMA_CLK_CTRL, 0, 0),
	LPC32XX_DEFINE_CLK(DDRAM, HCLKDIV_CTRL, 0x0, BIT(8) | BIT(7),
		   0x0, BIT(8) | BIT(7), 0x0, BIT(1) | BIT(0), clk_ddram_ops),

	LPC32XX_DEFINE_GATE(TIMER0, TIMCLK_CTRL1, 2, 0),
	LPC32XX_DEFINE_GATE(TIMER1, TIMCLK_CTRL1, 3, 0),
	LPC32XX_DEFINE_GATE(TIMER2, TIMCLK_CTRL1, 4, 0),
	LPC32XX_DEFINE_GATE(TIMER3, TIMCLK_CTRL1, 5, 0),
	LPC32XX_DEFINE_GATE(TIMER4, TIMCLK_CTRL1, 0, 0),
	LPC32XX_DEFINE_GATE(TIMER5, TIMCLK_CTRL1, 1, 0),

	LPC32XX_DEFINE_GATE(SSP0, SSP_CTRL, 0, 0),
	LPC32XX_DEFINE_GATE(SSP1, SSP_CTRL, 1, 0),
	LPC32XX_DEFINE_GATE(SPI1, SPI_CTRL, 0, 0),
	LPC32XX_DEFINE_GATE(SPI2, SPI_CTRL, 4, 0),
	LPC32XX_DEFINE_GATE(I2S0, I2S_CTRL, 0, 0),
	LPC32XX_DEFINE_GATE(I2S1, I2S_CTRL, 1, 0),
	LPC32XX_DEFINE_GATE(I2C1, I2CCLK_CTRL, 0, 0),
	LPC32XX_DEFINE_GATE(I2C2, I2CCLK_CTRL, 1, 0),
	LPC32XX_DEFINE_GATE(WDOG, TIMCLK_CTRL, 0, 0),
	LPC32XX_DEFINE_GATE(HSTIMER, TIMCLK_CTRL, 1, 0),

	LPC32XX_DEFINE_GATE(KEY, KEYCLK_CTRL, 0, 0),
	LPC32XX_DEFINE_GATE(MCPWM, TIMCLK_CTRL1, 6, 0),

	LPC32XX_DEFINE_MUX(PWM1_MUX, PWMCLK_CTRL, 1, 0x1, NULL, 0),
	LPC32XX_DEFINE_DIV(PWM1_DIV, PWMCLK_CTRL, 4, 4, NULL,
			   CLK_DIVIDER_ONE_BASED),
	LPC32XX_DEFINE_GATE(PWM1_GATE, PWMCLK_CTRL, 0, 0),
	LPC32XX_DEFINE_COMPOSITE(PWM1, PWM1_MUX, PWM1_DIV, PWM1_GATE),

	LPC32XX_DEFINE_MUX(PWM2_MUX, PWMCLK_CTRL, 3, 0x1, NULL, 0),
	LPC32XX_DEFINE_DIV(PWM2_DIV, PWMCLK_CTRL, 8, 4, NULL,
			   CLK_DIVIDER_ONE_BASED),
	LPC32XX_DEFINE_GATE(PWM2_GATE, PWMCLK_CTRL, 2, 0),
	LPC32XX_DEFINE_COMPOSITE(PWM2, PWM2_MUX, PWM2_DIV, PWM2_GATE),

	LPC32XX_DEFINE_MUX(UART3_MUX, UART3_CLK_CTRL, 16, 0x1, NULL, 0),
	LPC32XX_DEFINE_CLK(UART3_DIV, UART3_CLK_CTRL,
			   0, 0, 0, 0, 0, 0, lpc32xx_uart_div_ops),
	LPC32XX_DEFINE_GATE(UART3_GATE, UART_CLK_CTRL, 0, 0),
	LPC32XX_DEFINE_COMPOSITE(UART3, UART3_MUX, UART3_DIV, UART3_GATE),

	LPC32XX_DEFINE_MUX(UART4_MUX, UART4_CLK_CTRL, 16, 0x1, NULL, 0),
	LPC32XX_DEFINE_CLK(UART4_DIV, UART4_CLK_CTRL,
			   0, 0, 0, 0, 0, 0, lpc32xx_uart_div_ops),
	LPC32XX_DEFINE_GATE(UART4_GATE, UART_CLK_CTRL, 1, 0),
	LPC32XX_DEFINE_COMPOSITE(UART4, UART4_MUX, UART4_DIV, UART4_GATE),

	LPC32XX_DEFINE_MUX(UART5_MUX, UART5_CLK_CTRL, 16, 0x1, NULL, 0),
	LPC32XX_DEFINE_CLK(UART5_DIV, UART5_CLK_CTRL,
			   0, 0, 0, 0, 0, 0, lpc32xx_uart_div_ops),
	LPC32XX_DEFINE_GATE(UART5_GATE, UART_CLK_CTRL, 2, 0),
	LPC32XX_DEFINE_COMPOSITE(UART5, UART5_MUX, UART5_DIV, UART5_GATE),

	LPC32XX_DEFINE_MUX(UART6_MUX, UART6_CLK_CTRL, 16, 0x1, NULL, 0),
	LPC32XX_DEFINE_CLK(UART6_DIV, UART6_CLK_CTRL,
			   0, 0, 0, 0, 0, 0, lpc32xx_uart_div_ops),
	LPC32XX_DEFINE_GATE(UART6_GATE, UART_CLK_CTRL, 3, 0),
	LPC32XX_DEFINE_COMPOSITE(UART6, UART6_MUX, UART6_DIV, UART6_GATE),

	LPC32XX_DEFINE_CLK(IRDA, IRDA_CLK_CTRL,
			   0, 0, 0, 0, 0, 0, lpc32xx_uart_div_ops),

	LPC32XX_DEFINE_MUX(TEST1_MUX, TEST_CLK_CTRL, 5, 0x3,
			   test1_mux_table, 0),
	LPC32XX_DEFINE_GATE(TEST1_GATE, TEST_CLK_CTRL, 4, 0),
	LPC32XX_DEFINE_COMPOSITE(TEST1, TEST1_MUX, _NULL, TEST1_GATE),

	LPC32XX_DEFINE_MUX(TEST2_MUX, TEST_CLK_CTRL, 1, 0x7,
			   test2_mux_table, 0),
	LPC32XX_DEFINE_GATE(TEST2_GATE, TEST_CLK_CTRL, 0, 0),
	LPC32XX_DEFINE_COMPOSITE(TEST2, TEST2_MUX, _NULL, TEST2_GATE),

	LPC32XX_DEFINE_MUX(SYS, SYSCLK_CTRL, 0, 0x1, NULL, CLK_MUX_READ_ONLY),

	LPC32XX_DEFINE_DIV(USB_DIV_DIV, USB_DIV, 0, 4, NULL, 0),
	LPC32XX_DEFINE_GATE(USB_DIV_GATE, USB_CTRL, 17, 0),
	LPC32XX_DEFINE_COMPOSITE(USB_DIV, _NULL, USB_DIV_DIV, USB_DIV_GATE),

	LPC32XX_DEFINE_DIV(SD_DIV, MS_CTRL, 0, 4, NULL, CLK_DIVIDER_ONE_BASED),
	LPC32XX_DEFINE_CLK(SD_GATE, MS_CTRL, BIT(5) | BIT(9), BIT(5) | BIT(9),
			   0x0, BIT(5) | BIT(9), 0x0, 0x0, clk_mask_ops),
	LPC32XX_DEFINE_COMPOSITE(SD, _NULL, SD_DIV, SD_GATE),

	LPC32XX_DEFINE_DIV(LCD_DIV, LCDCLK_CTRL, 0, 5, NULL, 0),
	LPC32XX_DEFINE_GATE(LCD_GATE, LCDCLK_CTRL, 5, 0),
	LPC32XX_DEFINE_COMPOSITE(LCD, _NULL, LCD_DIV, LCD_GATE),

	LPC32XX_DEFINE_CLK(MAC, MACCLK_CTRL,
			   BIT(2) | BIT(1) | BIT(0), BIT(2) | BIT(1) | BIT(0),
			   BIT(2) | BIT(1) | BIT(0), BIT(2) | BIT(1) | BIT(0),
			   0x0, 0x0, clk_mask_ops),
	LPC32XX_DEFINE_CLK(SLC, FLASHCLK_CTRL,
			   BIT(2) | BIT(0), BIT(2) | BIT(0), 0x0,
			   BIT(0), BIT(1), BIT(2) | BIT(1), clk_mask_ops),
	LPC32XX_DEFINE_CLK(MLC, FLASHCLK_CTRL,
			   BIT(1), BIT(2) | BIT(1), 0x0, BIT(1),
			   BIT(2) | BIT(0), BIT(2) | BIT(0), clk_mask_ops),
	/*
	 * ADC/TS clock unfortunately cannot be registered as a composite one
	 * due to a different connection of gate, div and mux, e.g. gating it
	 * won't mean that the clock is off, if peripheral clock is its parent:
	 *
	 * rtc-->[gate]-->|     |
	 *                | mux |--> adc/ts
	 * pclk-->[div]-->|     |
	 *
	 * Constraints:
	 * ADC --- resulting clock must be <= 4.5 MHz
	 * TS  --- resulting clock must be <= 400 KHz
	 */
	LPC32XX_DEFINE_DIV(ADC_DIV, ADCCLK_CTRL1, 0, 8, NULL, 0),
	LPC32XX_DEFINE_GATE(ADC_RTC, ADCCLK_CTRL, 0, 0),
	LPC32XX_DEFINE_MUX(ADC, ADCCLK_CTRL1, 8, 0x1, NULL, 0),

	/* USB controller clocks */
	LPC32XX_DEFINE_USB(USB_AHB,
			   BIT(24), 0x0, BIT(24), BIT(4), 0, clk_usb_ops),
	LPC32XX_DEFINE_USB(USB_OTG,
			   0x0, 0x0, 0x0, BIT(3), 0, clk_usb_ops),
	LPC32XX_DEFINE_USB(USB_I2C,
			   0x0, BIT(23), BIT(23), BIT(2), 0, clk_usb_i2c_ops),
	LPC32XX_DEFINE_USB(USB_DEV,
			   BIT(22), 0x0, BIT(22), BIT(1), BIT(0), clk_usb_ops),
	LPC32XX_DEFINE_USB(USB_HOST,
			   BIT(21), 0x0, BIT(21), BIT(0), BIT(1), clk_usb_ops),
};

static struct clk * __init lpc32xx_clk_register(u32 id)
{
	const struct clk_proto_t *lpc32xx_clk = &clk_proto[id];
	struct clk_hw_proto *clk_hw = &clk_hw_proto[id];
	const char *parents[LPC32XX_CLK_PARENTS_MAX];
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < lpc32xx_clk->num_parents; i++)
		parents[i] = clk_proto[lpc32xx_clk->parents[i]].name;

	pr_debug("%s: derived from '%s', clock type %d\n", lpc32xx_clk->name,
		 parents[0], clk_hw->type);

	switch (clk_hw->type) {
	case CLK_LPC32XX:
	case CLK_LPC32XX_PLL:
	case CLK_LPC32XX_USB:
	case CLK_MUX:
	case CLK_DIV:
	case CLK_GATE:
	{
		struct clk_init_data clk_init = {
			.name = lpc32xx_clk->name,
			.parent_names = parents,
			.num_parents = lpc32xx_clk->num_parents,
			.flags = lpc32xx_clk->flags,
			.ops = clk_hw->hw0.ops,
		};
		struct clk_hw *hw;

		if (clk_hw->type == CLK_LPC32XX)
			hw = &clk_hw->hw0.clk.hw;
		else if (clk_hw->type == CLK_LPC32XX_PLL)
			hw = &clk_hw->hw0.pll.hw;
		else if (clk_hw->type == CLK_LPC32XX_USB)
			hw = &clk_hw->hw0.usb_clk.hw;
		else if (clk_hw->type == CLK_MUX)
			hw = &clk_hw->hw0.mux.hw;
		else if (clk_hw->type == CLK_DIV)
			hw = &clk_hw->hw0.div.hw;
		else if (clk_hw->type == CLK_GATE)
			hw = &clk_hw->hw0.gate.hw;
		else
			return ERR_PTR(-EINVAL);

		hw->init = &clk_init;
		clk = clk_register(NULL, hw);
		break;
	}
	case CLK_COMPOSITE:
	{
		struct clk_hw *mux_hw = NULL, *div_hw = NULL, *gate_hw = NULL;
		const struct clk_ops *mops = NULL, *dops = NULL, *gops = NULL;
		struct clk_hw_proto0 *mux0, *div0, *gate0;

		mux0 = clk_hw->hw1.mux;
		div0 = clk_hw->hw1.div;
		gate0 = clk_hw->hw1.gate;
		if (mux0) {
			mops = mux0->ops;
			mux_hw = &mux0->clk.hw;
		}
		if (div0) {
			dops = div0->ops;
			div_hw = &div0->clk.hw;
		}
		if (gate0) {
			gops = gate0->ops;
			gate_hw = &gate0->clk.hw;
		}

		clk = clk_register_composite(NULL, lpc32xx_clk->name,
				parents, lpc32xx_clk->num_parents,
				mux_hw, mops, div_hw, dops,
				gate_hw, gops, lpc32xx_clk->flags);
		break;
	}
	case CLK_FIXED:
	{
		struct clk_fixed_rate *fixed = &clk_hw->f;

		clk = clk_register_fixed_rate(NULL, lpc32xx_clk->name,
			parents[0], 0, fixed->fixed_rate);
		break;
	}
	default:
		clk = ERR_PTR(-EINVAL);
	}

	return clk;
}

static void __init lpc32xx_clk_div_quirk(u32 reg, u32 div_mask, u32 gate)
{
	u32 val;

	regmap_read(clk_regmap, reg, &val);

	if (!(val & div_mask)) {
		val &= ~gate;
		val |= BIT(__ffs(div_mask));
	}

	regmap_update_bits(clk_regmap, reg, gate | div_mask, val);
}

static void __init lpc32xx_clk_init(struct device_node *np)
{
	unsigned int i;
	struct clk *clk_osc, *clk_32k;
	void __iomem *base = NULL;

	/* Ensure that parent clocks are available and valid */
	clk_32k = of_clk_get_by_name(np, clk_proto[LPC32XX_CLK_XTAL_32K].name);
	if (IS_ERR(clk_32k)) {
		pr_err("failed to find external 32KHz clock: %ld\n",
		       PTR_ERR(clk_32k));
		return;
	}
	if (clk_get_rate(clk_32k) != 32768) {
		pr_err("invalid clock rate of external 32KHz oscillator\n");
		return;
	}

	clk_osc = of_clk_get_by_name(np, clk_proto[LPC32XX_CLK_XTAL].name);
	if (IS_ERR(clk_osc)) {
		pr_err("failed to find external main oscillator clock: %ld\n",
		       PTR_ERR(clk_osc));
		return;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("failed to map system control block registers\n");
		return;
	}

	clk_regmap = regmap_init_mmio(NULL, base, &lpc32xx_scb_regmap_config);
	if (IS_ERR(clk_regmap)) {
		pr_err("failed to regmap system control block: %ld\n",
			PTR_ERR(clk_regmap));
		iounmap(base);
		return;
	}

	/*
	 * Divider part of PWM and MS clocks requires a quirk to avoid
	 * a misinterpretation of formally valid zero value in register
	 * bitfield, which indicates another clock gate. Instead of
	 * adding complexity to a gate clock ensure that zero value in
	 * divider clock is never met in runtime.
	 */
	lpc32xx_clk_div_quirk(LPC32XX_CLKPWR_PWMCLK_CTRL, 0xf0, BIT(0));
	lpc32xx_clk_div_quirk(LPC32XX_CLKPWR_PWMCLK_CTRL, 0xf00, BIT(2));
	lpc32xx_clk_div_quirk(LPC32XX_CLKPWR_MS_CTRL, 0xf, BIT(5) | BIT(9));

	for (i = 1; i < LPC32XX_CLK_MAX; i++) {
		clk[i] = lpc32xx_clk_register(i);
		if (IS_ERR(clk[i])) {
			pr_err("failed to register %s clock: %ld\n",
				clk_proto[i].name, PTR_ERR(clk[i]));
			clk[i] = NULL;
		}
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	/* Set 48MHz rate of USB PLL clock */
	clk_set_rate(clk[LPC32XX_CLK_USB_PLL], 48000000);

	/* These two clocks must be always on independently on consumers */
	clk_prepare_enable(clk[LPC32XX_CLK_ARM]);
	clk_prepare_enable(clk[LPC32XX_CLK_HCLK]);

	/* Enable ARM VFP by default */
	clk_prepare_enable(clk[LPC32XX_CLK_ARM_VFP]);

	/* Disable enabled by default clocks for NAND MLC and SLC */
	clk_mask_disable(&clk_hw_proto[LPC32XX_CLK_SLC].hw0.clk.hw);
	clk_mask_disable(&clk_hw_proto[LPC32XX_CLK_MLC].hw0.clk.hw);
}
CLK_OF_DECLARE(lpc32xx_clk, "nxp,lpc3220-clk", lpc32xx_clk_init);

static void __init lpc32xx_usb_clk_init(struct device_node *np)
{
	unsigned int i;

	usb_clk_vbase = of_iomap(np, 0);
	if (!usb_clk_vbase) {
		pr_err("failed to map address range\n");
		return;
	}

	for (i = 1; i < LPC32XX_USB_CLK_MAX; i++) {
		usb_clk[i] = lpc32xx_clk_register(i + LPC32XX_CLK_USB_OFFSET);
		if (IS_ERR(usb_clk[i])) {
			pr_err("failed to register %s clock: %ld\n",
				clk_proto[i].name, PTR_ERR(usb_clk[i]));
			usb_clk[i] = NULL;
		}
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &usb_clk_data);
}
CLK_OF_DECLARE(lpc32xx_usb_clk, "nxp,lpc3220-usb-clk", lpc32xx_usb_clk_init);
