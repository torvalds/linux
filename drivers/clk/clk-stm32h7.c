/*
 * Copyright (C) Gabriel Fernandez 2017
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com>
 *
 * License terms: GPL V2.0.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/stm32h7-clks.h>

/* Reset Clock Control Registers */
#define RCC_CR		0x00
#define RCC_CFGR	0x10
#define RCC_D1CFGR	0x18
#define RCC_D2CFGR	0x1C
#define RCC_D3CFGR	0x20
#define RCC_PLLCKSELR	0x28
#define RCC_PLLCFGR	0x2C
#define RCC_PLL1DIVR	0x30
#define RCC_PLL1FRACR	0x34
#define RCC_PLL2DIVR	0x38
#define RCC_PLL2FRACR	0x3C
#define RCC_PLL3DIVR	0x40
#define RCC_PLL3FRACR	0x44
#define RCC_D1CCIPR	0x4C
#define RCC_D2CCIP1R	0x50
#define RCC_D2CCIP2R	0x54
#define RCC_D3CCIPR	0x58
#define RCC_BDCR	0x70
#define RCC_CSR		0x74
#define RCC_AHB3ENR	0xD4
#define RCC_AHB1ENR	0xD8
#define RCC_AHB2ENR	0xDC
#define RCC_AHB4ENR	0xE0
#define RCC_APB3ENR	0xE4
#define RCC_APB1LENR	0xE8
#define RCC_APB1HENR	0xEC
#define RCC_APB2ENR	0xF0
#define RCC_APB4ENR	0xF4

static DEFINE_SPINLOCK(stm32rcc_lock);

static void __iomem *base;
static struct clk_hw **hws;

/* System clock parent */
static const char * const sys_src[] = {
	"hsi_ck", "csi_ck", "hse_ck", "pll1_p" };

static const char * const tracein_src[] = {
	"hsi_ck", "csi_ck", "hse_ck", "pll1_r" };

static const char * const per_src[] = {
	"hsi_ker", "csi_ker", "hse_ck", "disabled" };

static const char * const pll_src[] = {
	"hsi_ck", "csi_ck", "hse_ck", "no clock" };

static const char * const sdmmc_src[] = { "pll1_q", "pll2_r" };

static const char * const dsi_src[] = { "ck_dsi_phy", "pll2_q" };

static const char * const qspi_src[] = {
	"hclk", "pll1_q", "pll2_r", "per_ck" };

static const char * const fmc_src[] = {
	"hclk", "pll1_q", "pll2_r", "per_ck" };

/* Kernel clock parent */
static const char * const swp_src[] = {	"pclk1", "hsi_ker" };

static const char * const fdcan_src[] = { "hse_ck", "pll1_q", "pll2_q" };

static const char * const dfsdm1_src[] = { "pclk2", "sys_ck" };

static const char * const spdifrx_src[] = {
	"pll1_q", "pll2_r", "pll3_r", "hsi_ker" };

static const char *spi_src1[5] = {
	"pll1_q", "pll2_p", "pll3_p", NULL, "per_ck" };

static const char * const spi_src2[] = {
	"pclk2", "pll2_q", "pll3_q", "hsi_ker", "csi_ker", "hse_ck" };

static const char * const spi_src3[] = {
	"pclk4", "pll2_q", "pll3_q", "hsi_ker", "csi_ker", "hse_ck" };

static const char * const lptim_src1[] = {
	"pclk1", "pll2_p", "pll3_r", "lse_ck", "lsi_ck", "per_ck" };

static const char * const lptim_src2[] = {
	"pclk4", "pll2_p", "pll3_r", "lse_ck", "lsi_ck", "per_ck" };

static const char * const cec_src[] = {"lse_ck", "lsi_ck", "csi_ker_div122" };

static const char * const usbotg_src[] = {"pll1_q", "pll3_q", "rc48_ck" };

/* i2c 1,2,3 src */
static const char * const i2c_src1[] = {
	"pclk1", "pll3_r", "hsi_ker", "csi_ker" };

static const char * const i2c_src2[] = {
	"pclk4", "pll3_r", "hsi_ker", "csi_ker" };

static const char * const rng_src[] = {
	"rc48_ck", "pll1_q", "lse_ck", "lsi_ck" };

/* usart 1,6 src */
static const char * const usart_src1[] = {
	"pclk2", "pll2_q", "pll3_q", "hsi_ker", "csi_ker", "lse_ck" };

/* usart 2,3,4,5,7,8 src */
static const char * const usart_src2[] = {
	"pclk1", "pll2_q", "pll3_q", "hsi_ker", "csi_ker", "lse_ck" };

static const char *sai_src[5] = {
	"pll1_q", "pll2_p", "pll3_p", NULL, "per_ck" };

static const char * const adc_src[] = { "pll2_p", "pll3_r", "per_ck" };

/* lptim 2,3,4,5 src */
static const char * const lpuart1_src[] = {
	"pclk3", "pll2_q", "pll3_q", "csi_ker", "lse_ck" };

static const char * const hrtim_src[] = { "tim2_ker", "d1cpre" };

/* RTC clock parent */
static const char * const rtc_src[] = { "off", "lse_ck", "lsi_ck", "hse_1M" };

/* Micro-controller output clock parent */
static const char * const mco_src1[] = {
	"hsi_ck", "lse_ck", "hse_ck", "pll1_q",	"rc48_ck" };

static const char * const mco_src2[] = {
	"sys_ck", "pll2_p", "hse_ck", "pll1_p", "csi_ck", "lsi_ck" };

/* LCD clock */
static const char * const ltdc_src[] = {"pll3_r"};

/* Gate clock with ready bit and backup domain management */
struct stm32_ready_gate {
	struct	clk_gate gate;
	u8	bit_rdy;
};

#define to_ready_gate_clk(_rgate) container_of(_rgate, struct stm32_ready_gate,\
		gate)

#define RGATE_TIMEOUT 10000

static int ready_gate_clk_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32_ready_gate *rgate = to_ready_gate_clk(gate);
	int bit_status;
	unsigned int timeout = RGATE_TIMEOUT;

	if (clk_gate_ops.is_enabled(hw))
		return 0;

	clk_gate_ops.enable(hw);

	/* We can't use readl_poll_timeout() because we can blocked if
	 * someone enables this clock before clocksource changes.
	 * Only jiffies counter is available. Jiffies are incremented by
	 * interruptions and enable op does not allow to be interrupted.
	 */
	do {
		bit_status = !(readl(gate->reg) & BIT(rgate->bit_rdy));

		if (bit_status)
			udelay(100);

	} while (bit_status && --timeout);

	return bit_status;
}

static void ready_gate_clk_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	struct stm32_ready_gate *rgate = to_ready_gate_clk(gate);
	int bit_status;
	unsigned int timeout = RGATE_TIMEOUT;

	if (!clk_gate_ops.is_enabled(hw))
		return;

	clk_gate_ops.disable(hw);

	do {
		bit_status = !!(readl(gate->reg) & BIT(rgate->bit_rdy));

		if (bit_status)
			udelay(100);

	} while (bit_status && --timeout);
}

static const struct clk_ops ready_gate_clk_ops = {
	.enable		= ready_gate_clk_enable,
	.disable	= ready_gate_clk_disable,
	.is_enabled	= clk_gate_is_enabled,
};

static struct clk_hw *clk_register_ready_gate(struct device *dev,
		const char *name, const char *parent_name,
		void __iomem *reg, u8 bit_idx, u8 bit_rdy,
		unsigned long flags, spinlock_t *lock)
{
	struct stm32_ready_gate *rgate;
	struct clk_init_data init = { NULL };
	struct clk_hw *hw;
	int ret;

	rgate = kzalloc(sizeof(*rgate), GFP_KERNEL);
	if (!rgate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &ready_gate_clk_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	rgate->bit_rdy = bit_rdy;
	rgate->gate.lock = lock;
	rgate->gate.reg = reg;
	rgate->gate.bit_idx = bit_idx;
	rgate->gate.hw.init = &init;

	hw = &rgate->gate.hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(rgate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

struct gate_cfg {
	u32 offset;
	u8  bit_idx;
};

struct muxdiv_cfg {
	u32 offset;
	u8 shift;
	u8 width;
};

struct composite_clk_cfg {
	struct gate_cfg *gate;
	struct muxdiv_cfg *mux;
	struct muxdiv_cfg *div;
	const char *name;
	const char * const *parent_name;
	int num_parents;
	u32 flags;
};

struct composite_clk_gcfg_t {
	u8 flags;
	const struct clk_ops *ops;
};

/*
 * General config definition of a composite clock (only clock diviser for rate)
 */
struct composite_clk_gcfg {
	struct composite_clk_gcfg_t *mux;
	struct composite_clk_gcfg_t *div;
	struct composite_clk_gcfg_t *gate;
};

#define M_CFG_MUX(_mux_ops, _mux_flags)\
	.mux = &(struct composite_clk_gcfg_t) { _mux_flags, _mux_ops}

#define M_CFG_DIV(_rate_ops, _rate_flags)\
	.div = &(struct composite_clk_gcfg_t) {_rate_flags, _rate_ops}

#define M_CFG_GATE(_gate_ops, _gate_flags)\
	.gate = &(struct composite_clk_gcfg_t) { _gate_flags, _gate_ops}

static struct clk_mux *_get_cmux(void __iomem *reg, u8 shift, u8 width,
		u32 flags, spinlock_t *lock)
{
	struct clk_mux *mux;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->reg	= reg;
	mux->shift	= shift;
	mux->mask	= (1 << width) - 1;
	mux->flags	= flags;
	mux->lock	= lock;

	return mux;
}

static struct clk_divider *_get_cdiv(void __iomem *reg, u8 shift, u8 width,
		u32 flags, spinlock_t *lock)
{
	struct clk_divider *div;

	div = kzalloc(sizeof(*div), GFP_KERNEL);

	if (!div)
		return ERR_PTR(-ENOMEM);

	div->reg   = reg;
	div->shift = shift;
	div->width = width;
	div->flags = flags;
	div->lock  = lock;

	return div;
}

static struct clk_gate *_get_cgate(void __iomem *reg, u8 bit_idx, u32 flags,
		spinlock_t *lock)
{
	struct clk_gate *gate;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->reg	= reg;
	gate->bit_idx	= bit_idx;
	gate->flags	= flags;
	gate->lock	= lock;

	return gate;
}

struct composite_cfg {
	struct clk_hw *mux_hw;
	struct clk_hw *div_hw;
	struct clk_hw *gate_hw;

	const struct clk_ops *mux_ops;
	const struct clk_ops *div_ops;
	const struct clk_ops *gate_ops;
};

static void get_cfg_composite_div(const struct composite_clk_gcfg *gcfg,
		const struct composite_clk_cfg *cfg,
		struct composite_cfg *composite, spinlock_t *lock)
{
	struct clk_mux     *mux = NULL;
	struct clk_divider *div = NULL;
	struct clk_gate    *gate = NULL;
	const struct clk_ops *mux_ops, *div_ops, *gate_ops;
	struct clk_hw *mux_hw;
	struct clk_hw *div_hw;
	struct clk_hw *gate_hw;

	mux_ops = div_ops = gate_ops = NULL;
	mux_hw = div_hw = gate_hw = NULL;

	if (gcfg->mux && gcfg->mux) {
		mux = _get_cmux(base + cfg->mux->offset,
				cfg->mux->shift,
				cfg->mux->width,
				gcfg->mux->flags, lock);

		if (!IS_ERR(mux)) {
			mux_hw = &mux->hw;
			mux_ops = gcfg->mux->ops ?
				  gcfg->mux->ops : &clk_mux_ops;
		}
	}

	if (gcfg->div && cfg->div) {
		div = _get_cdiv(base + cfg->div->offset,
				cfg->div->shift,
				cfg->div->width,
				gcfg->div->flags, lock);

		if (!IS_ERR(div)) {
			div_hw = &div->hw;
			div_ops = gcfg->div->ops ?
				  gcfg->div->ops : &clk_divider_ops;
		}
	}

	if (gcfg->gate && gcfg->gate) {
		gate = _get_cgate(base + cfg->gate->offset,
				cfg->gate->bit_idx,
				gcfg->gate->flags, lock);

		if (!IS_ERR(gate)) {
			gate_hw = &gate->hw;
			gate_ops = gcfg->gate->ops ?
				   gcfg->gate->ops : &clk_gate_ops;
		}
	}

	composite->mux_hw = mux_hw;
	composite->mux_ops = mux_ops;

	composite->div_hw = div_hw;
	composite->div_ops = div_ops;

	composite->gate_hw = gate_hw;
	composite->gate_ops = gate_ops;
}

/* Kernel Timer */
struct timer_ker {
	u8 dppre_shift;
	struct clk_hw hw;
	spinlock_t *lock;
};

#define to_timer_ker(_hw) container_of(_hw, struct timer_ker, hw)

static unsigned long timer_ker_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct timer_ker *clk_elem = to_timer_ker(hw);
	u32 timpre;
	u32 dppre_shift = clk_elem->dppre_shift;
	u32 prescaler;
	u32 mul;

	timpre = (readl(base + RCC_CFGR) >> 15) & 0x01;

	prescaler = (readl(base + RCC_D2CFGR) >> dppre_shift) & 0x03;

	mul = 2;

	if (prescaler < 4)
		mul = 1;

	else if (timpre && prescaler > 4)
		mul = 4;

	return parent_rate * mul;
}

static const struct clk_ops timer_ker_ops = {
	.recalc_rate = timer_ker_recalc_rate,
};

static struct clk_hw *clk_register_stm32_timer_ker(struct device *dev,
		const char *name, const char *parent_name,
		unsigned long flags,
		u8 dppre_shift,
		spinlock_t *lock)
{
	struct timer_ker *element;
	struct clk_init_data init;
	struct clk_hw *hw;
	int err;

	element = kzalloc(sizeof(*element), GFP_KERNEL);
	if (!element)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &timer_ker_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	element->hw.init = &init;
	element->lock = lock;
	element->dppre_shift = dppre_shift;

	hw = &element->hw;
	err = clk_hw_register(dev, hw);

	if (err) {
		kfree(element);
		return ERR_PTR(err);
	}

	return hw;
}

static const struct clk_div_table d1cpre_div_table[] = {
	{ 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1},
	{ 4, 1 }, { 5, 1 }, { 6, 1 }, { 7, 1},
	{ 8, 2 }, { 9, 4 }, { 10, 8 }, { 11, 16 },
	{ 12, 64 }, { 13, 128 }, { 14, 256 },
	{ 15, 512 },
	{ 0 },
};

static const struct clk_div_table ppre_div_table[] = {
	{ 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1},
	{ 4, 2 }, { 5, 4 }, { 6, 8 }, { 7, 16 },
	{ 0 },
};

static void register_core_and_bus_clocks(void)
{
	/* CORE AND BUS */
	hws[SYS_D1CPRE] = clk_hw_register_divider_table(NULL, "d1cpre",
			"sys_ck", CLK_IGNORE_UNUSED, base + RCC_D1CFGR, 8, 4, 0,
			d1cpre_div_table, &stm32rcc_lock);

	hws[HCLK] = clk_hw_register_divider_table(NULL, "hclk", "d1cpre",
			CLK_IGNORE_UNUSED, base + RCC_D1CFGR, 0, 4, 0,
			d1cpre_div_table, &stm32rcc_lock);

	/* D1 DOMAIN */
	/* * CPU Systick */
	hws[CPU_SYSTICK] = clk_hw_register_fixed_factor(NULL, "systick",
			"d1cpre", 0, 1, 8);

	/* * APB3 peripheral */
	hws[PCLK3] = clk_hw_register_divider_table(NULL, "pclk3", "hclk", 0,
			base + RCC_D1CFGR, 4, 3, 0,
			ppre_div_table, &stm32rcc_lock);

	/* D2 DOMAIN */
	/* * APB1 peripheral */
	hws[PCLK1] = clk_hw_register_divider_table(NULL, "pclk1", "hclk", 0,
			base + RCC_D2CFGR, 4, 3, 0,
			ppre_div_table, &stm32rcc_lock);

	/* Timers prescaler clocks */
	clk_register_stm32_timer_ker(NULL, "tim1_ker", "pclk1", 0,
			4, &stm32rcc_lock);

	/* * APB2 peripheral */
	hws[PCLK2] = clk_hw_register_divider_table(NULL, "pclk2", "hclk", 0,
			base + RCC_D2CFGR, 8, 3, 0, ppre_div_table,
			&stm32rcc_lock);

	clk_register_stm32_timer_ker(NULL, "tim2_ker", "pclk2", 0, 8,
			&stm32rcc_lock);

	/* D3 DOMAIN */
	/* * APB4 peripheral */
	hws[PCLK4] = clk_hw_register_divider_table(NULL, "pclk4", "hclk", 0,
			base + RCC_D3CFGR, 4, 3, 0,
			ppre_div_table, &stm32rcc_lock);
}

/* MUX clock configuration */
struct stm32_mux_clk {
	const char *name;
	const char * const *parents;
	u8 num_parents;
	u32 offset;
	u8 shift;
	u8 width;
	u32 flags;
};

#define M_MCLOCF(_name, _parents, _mux_offset, _mux_shift, _mux_width, _flags)\
{\
	.name		= _name,\
	.parents	= _parents,\
	.num_parents	= ARRAY_SIZE(_parents),\
	.offset		= _mux_offset,\
	.shift		= _mux_shift,\
	.width		= _mux_width,\
	.flags		= _flags,\
}

#define M_MCLOC(_name, _parents, _mux_offset, _mux_shift, _mux_width)\
	M_MCLOCF(_name, _parents, _mux_offset, _mux_shift, _mux_width, 0)\

static const struct stm32_mux_clk stm32_mclk[] __initconst = {
	M_MCLOC("per_ck",	per_src,	RCC_D1CCIPR,	28, 3),
	M_MCLOC("pllsrc",	pll_src,	RCC_PLLCKSELR,	 0, 3),
	M_MCLOC("sys_ck",	sys_src,	RCC_CFGR,	 0, 3),
	M_MCLOC("tracein_ck",	tracein_src,	RCC_CFGR,	 0, 3),
};

/* Oscillary clock configuration */
struct stm32_osc_clk {
	const char *name;
	const char *parent;
	u32 gate_offset;
	u8 bit_idx;
	u8 bit_rdy;
	u32 flags;
};

#define OSC_CLKF(_name, _parent, _gate_offset, _bit_idx, _bit_rdy, _flags)\
{\
	.name		= _name,\
	.parent		= _parent,\
	.gate_offset	= _gate_offset,\
	.bit_idx	= _bit_idx,\
	.bit_rdy	= _bit_rdy,\
	.flags		= _flags,\
}

#define OSC_CLK(_name, _parent, _gate_offset, _bit_idx, _bit_rdy)\
	OSC_CLKF(_name, _parent, _gate_offset, _bit_idx, _bit_rdy, 0)

static const struct stm32_osc_clk stm32_oclk[] __initconst = {
	OSC_CLKF("hsi_ck",  "hsidiv",   RCC_CR,   0,  2, CLK_IGNORE_UNUSED),
	OSC_CLKF("hsi_ker", "hsidiv",   RCC_CR,   1,  2, CLK_IGNORE_UNUSED),
	OSC_CLKF("csi_ck",  "clk-csi",  RCC_CR,   7,  8, CLK_IGNORE_UNUSED),
	OSC_CLKF("csi_ker", "clk-csi",  RCC_CR,   9,  8, CLK_IGNORE_UNUSED),
	OSC_CLKF("rc48_ck", "clk-rc48", RCC_CR,  12, 13, CLK_IGNORE_UNUSED),
	OSC_CLKF("lsi_ck",  "clk-lsi",  RCC_CSR,  0,  1, CLK_IGNORE_UNUSED),
};

/* PLL configuration */
struct st32h7_pll_cfg {
	u8 bit_idx;
	u32 offset_divr;
	u8 bit_frac_en;
	u32 offset_frac;
	u8 divm;
};

struct stm32_pll_data {
	const char *name;
	const char *parent_name;
	unsigned long flags;
	const struct st32h7_pll_cfg *cfg;
};

static const struct st32h7_pll_cfg stm32h7_pll1 = {
	.bit_idx = 24,
	.offset_divr = RCC_PLL1DIVR,
	.bit_frac_en = 0,
	.offset_frac = RCC_PLL1FRACR,
	.divm = 4,
};

static const struct st32h7_pll_cfg stm32h7_pll2 = {
	.bit_idx = 26,
	.offset_divr = RCC_PLL2DIVR,
	.bit_frac_en = 4,
	.offset_frac = RCC_PLL2FRACR,
	.divm = 12,
};

static const struct st32h7_pll_cfg stm32h7_pll3 = {
	.bit_idx = 28,
	.offset_divr = RCC_PLL3DIVR,
	.bit_frac_en = 8,
	.offset_frac = RCC_PLL3FRACR,
	.divm = 20,
};

static const struct stm32_pll_data stm32_pll[] = {
	{ "vco1", "pllsrc", CLK_IGNORE_UNUSED, &stm32h7_pll1 },
	{ "vco2", "pllsrc", 0, &stm32h7_pll2 },
	{ "vco3", "pllsrc", 0, &stm32h7_pll3 },
};

struct stm32_fractional_divider {
	void __iomem	*mreg;
	u8		mshift;
	u8		mwidth;
	u32		mmask;

	void __iomem	*nreg;
	u8		nshift;
	u8		nwidth;

	void __iomem	*freg_status;
	u8		freg_bit;
	void __iomem	*freg_value;
	u8		fshift;
	u8		fwidth;

	u8		flags;
	struct clk_hw	hw;
	spinlock_t	*lock;
};

struct stm32_pll_obj {
	spinlock_t *lock;
	struct stm32_fractional_divider div;
	struct stm32_ready_gate rgate;
	struct clk_hw hw;
};

#define to_pll(_hw) container_of(_hw, struct stm32_pll_obj, hw)

static int pll_is_enabled(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	struct clk_hw *_hw = &clk_elem->rgate.gate.hw;

	__clk_hw_set_clk(_hw, hw);

	return ready_gate_clk_ops.is_enabled(_hw);
}

static int pll_enable(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	struct clk_hw *_hw = &clk_elem->rgate.gate.hw;

	__clk_hw_set_clk(_hw, hw);

	return ready_gate_clk_ops.enable(_hw);
}

static void pll_disable(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	struct clk_hw *_hw = &clk_elem->rgate.gate.hw;

	__clk_hw_set_clk(_hw, hw);

	ready_gate_clk_ops.disable(_hw);
}

static int pll_frac_is_enabled(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	struct stm32_fractional_divider *fd = &clk_elem->div;

	return (readl(fd->freg_status) >> fd->freg_bit) & 0x01;
}

static unsigned long pll_read_frac(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	struct stm32_fractional_divider *fd = &clk_elem->div;

	return (readl(fd->freg_value) >> fd->fshift) &
		GENMASK(fd->fwidth - 1, 0);
}

static unsigned long pll_fd_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	struct stm32_fractional_divider *fd = &clk_elem->div;
	unsigned long m, n;
	u32 val, mask;
	u64 rate, rate1 = 0;

	val = readl(fd->mreg);
	mask = GENMASK(fd->mwidth - 1, 0) << fd->mshift;
	m = (val & mask) >> fd->mshift;

	val = readl(fd->nreg);
	mask = GENMASK(fd->nwidth - 1, 0) << fd->nshift;
	n = ((val & mask) >> fd->nshift) + 1;

	if (!n || !m)
		return parent_rate;

	rate = (u64)parent_rate * n;
	do_div(rate, m);

	if (pll_frac_is_enabled(hw)) {
		val = pll_read_frac(hw);
		rate1 = (u64)parent_rate * (u64)val;
		do_div(rate1, (m * 8191));
	}

	return rate + rate1;
}

static const struct clk_ops pll_ops = {
	.enable		= pll_enable,
	.disable	= pll_disable,
	.is_enabled	= pll_is_enabled,
	.recalc_rate	= pll_fd_recalc_rate,
};

static struct clk_hw *clk_register_stm32_pll(struct device *dev,
		const char *name,
		const char *parent,
		unsigned long flags,
		const struct st32h7_pll_cfg *cfg,
		spinlock_t *lock)
{
	struct stm32_pll_obj *pll;
	struct clk_init_data init = { NULL };
	struct clk_hw *hw;
	int ret;
	struct stm32_fractional_divider *div = NULL;
	struct stm32_ready_gate *rgate;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &pll_ops;
	init.flags = flags;
	init.parent_names = &parent;
	init.num_parents = 1;
	pll->hw.init = &init;

	hw = &pll->hw;
	rgate = &pll->rgate;

	rgate->bit_rdy = cfg->bit_idx + 1;
	rgate->gate.lock = lock;
	rgate->gate.reg = base + RCC_CR;
	rgate->gate.bit_idx = cfg->bit_idx;

	div = &pll->div;
	div->flags = 0;
	div->mreg = base + RCC_PLLCKSELR;
	div->mshift = cfg->divm;
	div->mwidth = 6;
	div->nreg = base +  cfg->offset_divr;
	div->nshift = 0;
	div->nwidth = 9;

	div->freg_status = base + RCC_PLLCFGR;
	div->freg_bit = cfg->bit_frac_en;
	div->freg_value = base +  cfg->offset_frac;
	div->fshift = 3;
	div->fwidth = 13;

	div->lock = lock;

	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(pll);
		hw = ERR_PTR(ret);
	}

	return hw;
}

/* ODF CLOCKS */
static unsigned long odf_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static long odf_divider_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_divider_ops.round_rate(hw, rate, prate);
}

static int odf_divider_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_hw *hwp;
	int pll_status;
	int ret;

	hwp = clk_hw_get_parent(hw);

	pll_status = pll_is_enabled(hwp);

	if (pll_status)
		pll_disable(hwp);

	ret = clk_divider_ops.set_rate(hw, rate, parent_rate);

	if (pll_status)
		pll_enable(hwp);

	return ret;
}

static const struct clk_ops odf_divider_ops = {
	.recalc_rate	= odf_divider_recalc_rate,
	.round_rate	= odf_divider_round_rate,
	.set_rate	= odf_divider_set_rate,
};

static int odf_gate_enable(struct clk_hw *hw)
{
	struct clk_hw *hwp;
	int pll_status;
	int ret;

	if (clk_gate_ops.is_enabled(hw))
		return 0;

	hwp = clk_hw_get_parent(hw);

	pll_status = pll_is_enabled(hwp);

	if (pll_status)
		pll_disable(hwp);

	ret = clk_gate_ops.enable(hw);

	if (pll_status)
		pll_enable(hwp);

	return ret;
}

static void odf_gate_disable(struct clk_hw *hw)
{
	struct clk_hw *hwp;
	int pll_status;

	if (!clk_gate_ops.is_enabled(hw))
		return;

	hwp = clk_hw_get_parent(hw);

	pll_status = pll_is_enabled(hwp);

	if (pll_status)
		pll_disable(hwp);

	clk_gate_ops.disable(hw);

	if (pll_status)
		pll_enable(hwp);
}

static const struct clk_ops odf_gate_ops = {
	.enable		= odf_gate_enable,
	.disable	= odf_gate_disable,
	.is_enabled	= clk_gate_is_enabled,
};

static struct composite_clk_gcfg odf_clk_gcfg = {
	M_CFG_DIV(&odf_divider_ops, 0),
	M_CFG_GATE(&odf_gate_ops, 0),
};

#define M_ODF_F(_name, _parent, _gate_offset,  _bit_idx, _rate_offset,\
		_rate_shift, _rate_width, _flags)\
{\
	.mux = NULL,\
	.div = &(struct muxdiv_cfg) {_rate_offset, _rate_shift, _rate_width},\
	.gate = &(struct gate_cfg) {_gate_offset, _bit_idx },\
	.name = _name,\
	.parent_name = &(const char *) {_parent},\
	.num_parents = 1,\
	.flags = _flags,\
}

#define M_ODF(_name, _parent, _gate_offset,  _bit_idx, _rate_offset,\
		_rate_shift, _rate_width)\
M_ODF_F(_name, _parent, _gate_offset,  _bit_idx, _rate_offset,\
		_rate_shift, _rate_width, 0)\

static const struct composite_clk_cfg stm32_odf[3][3] = {
	{
		M_ODF_F("pll1_p", "vco1", RCC_PLLCFGR, 16, RCC_PLL1DIVR,  9, 7,
				CLK_IGNORE_UNUSED),
		M_ODF_F("pll1_q", "vco1", RCC_PLLCFGR, 17, RCC_PLL1DIVR, 16, 7,
				CLK_IGNORE_UNUSED),
		M_ODF_F("pll1_r", "vco1", RCC_PLLCFGR, 18, RCC_PLL1DIVR, 24, 7,
				CLK_IGNORE_UNUSED),
	},

	{
		M_ODF("pll2_p", "vco2", RCC_PLLCFGR, 19, RCC_PLL2DIVR,  9, 7),
		M_ODF("pll2_q", "vco2", RCC_PLLCFGR, 20, RCC_PLL2DIVR, 16, 7),
		M_ODF("pll2_r", "vco2", RCC_PLLCFGR, 21, RCC_PLL2DIVR, 24, 7),
	},
	{
		M_ODF("pll3_p", "vco3", RCC_PLLCFGR, 22, RCC_PLL3DIVR,  9, 7),
		M_ODF("pll3_q", "vco3", RCC_PLLCFGR, 23, RCC_PLL3DIVR, 16, 7),
		M_ODF("pll3_r", "vco3", RCC_PLLCFGR, 24, RCC_PLL3DIVR, 24, 7),
	}
};

/* PERIF CLOCKS */
struct pclk_t {
	u32 gate_offset;
	u8 bit_idx;
	const char *name;
	const char *parent;
	u32 flags;
};

#define PER_CLKF(_gate_offset, _bit_idx, _name, _parent, _flags)\
{\
	.gate_offset	= _gate_offset,\
	.bit_idx	= _bit_idx,\
	.name		= _name,\
	.parent		= _parent,\
	.flags		= _flags,\
}

#define PER_CLK(_gate_offset, _bit_idx, _name, _parent)\
	PER_CLKF(_gate_offset, _bit_idx, _name, _parent, 0)

static const struct pclk_t pclk[] = {
	PER_CLK(RCC_AHB3ENR, 31, "d1sram1", "hclk"),
	PER_CLK(RCC_AHB3ENR, 30, "itcm", "hclk"),
	PER_CLK(RCC_AHB3ENR, 29, "dtcm2", "hclk"),
	PER_CLK(RCC_AHB3ENR, 28, "dtcm1", "hclk"),
	PER_CLK(RCC_AHB3ENR, 8, "flitf", "hclk"),
	PER_CLK(RCC_AHB3ENR, 5, "jpgdec", "hclk"),
	PER_CLK(RCC_AHB3ENR, 4, "dma2d", "hclk"),
	PER_CLK(RCC_AHB3ENR, 0, "mdma", "hclk"),
	PER_CLK(RCC_AHB1ENR, 28, "usb2ulpi", "hclk"),
	PER_CLK(RCC_AHB1ENR, 26, "usb1ulpi", "hclk"),
	PER_CLK(RCC_AHB1ENR, 17, "eth1rx", "hclk"),
	PER_CLK(RCC_AHB1ENR, 16, "eth1tx", "hclk"),
	PER_CLK(RCC_AHB1ENR, 15, "eth1mac", "hclk"),
	PER_CLK(RCC_AHB1ENR, 14, "art", "hclk"),
	PER_CLK(RCC_AHB1ENR, 1, "dma2", "hclk"),
	PER_CLK(RCC_AHB1ENR, 0, "dma1", "hclk"),
	PER_CLK(RCC_AHB2ENR, 31, "d2sram3", "hclk"),
	PER_CLK(RCC_AHB2ENR, 30, "d2sram2", "hclk"),
	PER_CLK(RCC_AHB2ENR, 29, "d2sram1", "hclk"),
	PER_CLK(RCC_AHB2ENR, 5, "hash", "hclk"),
	PER_CLK(RCC_AHB2ENR, 4, "crypt", "hclk"),
	PER_CLK(RCC_AHB2ENR, 0, "camitf", "hclk"),
	PER_CLK(RCC_AHB4ENR, 28, "bkpram", "hclk"),
	PER_CLK(RCC_AHB4ENR, 25, "hsem", "hclk"),
	PER_CLK(RCC_AHB4ENR, 21, "bdma", "hclk"),
	PER_CLK(RCC_AHB4ENR, 19, "crc", "hclk"),
	PER_CLK(RCC_AHB4ENR, 10, "gpiok", "hclk"),
	PER_CLK(RCC_AHB4ENR, 9, "gpioj", "hclk"),
	PER_CLK(RCC_AHB4ENR, 8, "gpioi", "hclk"),
	PER_CLK(RCC_AHB4ENR, 7, "gpioh", "hclk"),
	PER_CLK(RCC_AHB4ENR, 6, "gpiog", "hclk"),
	PER_CLK(RCC_AHB4ENR, 5, "gpiof", "hclk"),
	PER_CLK(RCC_AHB4ENR, 4, "gpioe", "hclk"),
	PER_CLK(RCC_AHB4ENR, 3, "gpiod", "hclk"),
	PER_CLK(RCC_AHB4ENR, 2, "gpioc", "hclk"),
	PER_CLK(RCC_AHB4ENR, 1, "gpiob", "hclk"),
	PER_CLK(RCC_AHB4ENR, 0, "gpioa", "hclk"),
	PER_CLK(RCC_APB3ENR, 6, "wwdg1", "pclk3"),
	PER_CLK(RCC_APB1LENR, 29, "dac12", "pclk1"),
	PER_CLK(RCC_APB1LENR, 11, "wwdg2", "pclk1"),
	PER_CLK(RCC_APB1LENR, 8, "tim14", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 7, "tim13", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 6, "tim12", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 5, "tim7", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 4, "tim6", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 3, "tim5", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 2, "tim4", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 1, "tim3", "tim1_ker"),
	PER_CLK(RCC_APB1LENR, 0, "tim2", "tim1_ker"),
	PER_CLK(RCC_APB1HENR, 5, "mdios", "pclk1"),
	PER_CLK(RCC_APB1HENR, 4, "opamp", "pclk1"),
	PER_CLK(RCC_APB1HENR, 1, "crs", "pclk1"),
	PER_CLK(RCC_APB2ENR, 18, "tim17", "tim2_ker"),
	PER_CLK(RCC_APB2ENR, 17, "tim16", "tim2_ker"),
	PER_CLK(RCC_APB2ENR, 16, "tim15", "tim2_ker"),
	PER_CLK(RCC_APB2ENR, 1, "tim8", "tim2_ker"),
	PER_CLK(RCC_APB2ENR, 0, "tim1", "tim2_ker"),
	PER_CLK(RCC_APB4ENR, 26, "tmpsens", "pclk4"),
	PER_CLK(RCC_APB4ENR, 16, "rtcapb", "pclk4"),
	PER_CLK(RCC_APB4ENR, 15, "vref", "pclk4"),
	PER_CLK(RCC_APB4ENR, 14, "comp12", "pclk4"),
	PER_CLK(RCC_APB4ENR, 1, "syscfg", "pclk4"),
};

/* KERNEL CLOCKS */
#define KER_CLKF(_gate_offset, _bit_idx,\
		_mux_offset, _mux_shift, _mux_width,\
		_name, _parent_name,\
		_flags) \
{ \
	.gate = &(struct gate_cfg) {_gate_offset, _bit_idx},\
	.mux = &(struct muxdiv_cfg) {_mux_offset, _mux_shift, _mux_width },\
	.name = _name, \
	.parent_name = _parent_name, \
	.num_parents = ARRAY_SIZE(_parent_name),\
	.flags = _flags,\
}

#define KER_CLK(_gate_offset, _bit_idx, _mux_offset, _mux_shift, _mux_width,\
		_name, _parent_name) \
KER_CLKF(_gate_offset, _bit_idx, _mux_offset, _mux_shift, _mux_width,\
		_name, _parent_name, 0)\

#define KER_CLKF_NOMUX(_gate_offset, _bit_idx,\
		_name, _parent_name,\
		_flags) \
{ \
	.gate = &(struct gate_cfg) {_gate_offset, _bit_idx},\
	.mux = NULL,\
	.name = _name, \
	.parent_name = _parent_name, \
	.num_parents = 1,\
	.flags = _flags,\
}

static const struct composite_clk_cfg kclk[] = {
	KER_CLK(RCC_AHB3ENR,  16, RCC_D1CCIPR,	16, 1, "sdmmc1", sdmmc_src),
	KER_CLKF(RCC_AHB3ENR, 14, RCC_D1CCIPR,	 4, 2, "quadspi", qspi_src,
			CLK_IGNORE_UNUSED),
	KER_CLKF(RCC_AHB3ENR, 12, RCC_D1CCIPR,	 0, 2, "fmc", fmc_src,
			CLK_IGNORE_UNUSED),
	KER_CLK(RCC_AHB1ENR,  27, RCC_D2CCIP2R,	20, 2, "usb2otg", usbotg_src),
	KER_CLK(RCC_AHB1ENR,  25, RCC_D2CCIP2R, 20, 2, "usb1otg", usbotg_src),
	KER_CLK(RCC_AHB1ENR,   5, RCC_D3CCIPR,	16, 2, "adc12", adc_src),
	KER_CLK(RCC_AHB2ENR,   9, RCC_D1CCIPR,	16, 1, "sdmmc2", sdmmc_src),
	KER_CLK(RCC_AHB2ENR,   6, RCC_D2CCIP2R,	 8, 2, "rng", rng_src),
	KER_CLK(RCC_AHB4ENR,  24, RCC_D3CCIPR,  16, 2, "adc3", adc_src),
	KER_CLKF(RCC_APB3ENR,   4, RCC_D1CCIPR,	 8, 1, "dsi", dsi_src,
			CLK_SET_RATE_PARENT),
	KER_CLKF_NOMUX(RCC_APB3ENR, 3, "ltdc", ltdc_src, CLK_SET_RATE_PARENT),
	KER_CLK(RCC_APB1LENR, 31, RCC_D2CCIP2R,  0, 3, "usart8", usart_src2),
	KER_CLK(RCC_APB1LENR, 30, RCC_D2CCIP2R,  0, 3, "usart7", usart_src2),
	KER_CLK(RCC_APB1LENR, 27, RCC_D2CCIP2R, 22, 2, "hdmicec", cec_src),
	KER_CLK(RCC_APB1LENR, 23, RCC_D2CCIP2R, 12, 2, "i2c3", i2c_src1),
	KER_CLK(RCC_APB1LENR, 22, RCC_D2CCIP2R, 12, 2, "i2c2", i2c_src1),
	KER_CLK(RCC_APB1LENR, 21, RCC_D2CCIP2R, 12, 2, "i2c1", i2c_src1),
	KER_CLK(RCC_APB1LENR, 20, RCC_D2CCIP2R,	 0, 3, "uart5", usart_src2),
	KER_CLK(RCC_APB1LENR, 19, RCC_D2CCIP2R,  0, 3, "uart4", usart_src2),
	KER_CLK(RCC_APB1LENR, 18, RCC_D2CCIP2R,  0, 3, "usart3", usart_src2),
	KER_CLK(RCC_APB1LENR, 17, RCC_D2CCIP2R,  0, 3, "usart2", usart_src2),
	KER_CLK(RCC_APB1LENR, 16, RCC_D2CCIP1R, 20, 2, "spdifrx", spdifrx_src),
	KER_CLK(RCC_APB1LENR, 15, RCC_D2CCIP1R, 16, 3, "spi3", spi_src1),
	KER_CLK(RCC_APB1LENR, 14, RCC_D2CCIP1R, 16, 3, "spi2", spi_src1),
	KER_CLK(RCC_APB1LENR,  9, RCC_D2CCIP2R, 28, 3, "lptim1", lptim_src1),
	KER_CLK(RCC_APB1HENR,  8, RCC_D2CCIP1R, 28, 2, "fdcan", fdcan_src),
	KER_CLK(RCC_APB1HENR,  2, RCC_D2CCIP1R, 31, 1, "swp", swp_src),
	KER_CLK(RCC_APB2ENR,  29, RCC_CFGR,	14, 1, "hrtim", hrtim_src),
	KER_CLK(RCC_APB2ENR,  28, RCC_D2CCIP1R, 24, 1, "dfsdm1", dfsdm1_src),
	KER_CLKF(RCC_APB2ENR,  24, RCC_D2CCIP1R,  6, 3, "sai3", sai_src,
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	KER_CLKF(RCC_APB2ENR,  23, RCC_D2CCIP1R,  6, 3, "sai2", sai_src,
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	KER_CLKF(RCC_APB2ENR,  22, RCC_D2CCIP1R,  0, 3, "sai1", sai_src,
		 CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT),
	KER_CLK(RCC_APB2ENR,  20, RCC_D2CCIP1R, 16, 3, "spi5", spi_src2),
	KER_CLK(RCC_APB2ENR,  13, RCC_D2CCIP1R, 16, 3, "spi4", spi_src2),
	KER_CLK(RCC_APB2ENR,  12, RCC_D2CCIP1R, 16, 3, "spi1", spi_src1),
	KER_CLK(RCC_APB2ENR,   5, RCC_D2CCIP2R,  3, 3, "usart6", usart_src1),
	KER_CLK(RCC_APB2ENR,   4, RCC_D2CCIP2R,  3, 3, "usart1", usart_src1),
	KER_CLK(RCC_APB4ENR,  21, RCC_D3CCIPR,	24, 3, "sai4b", sai_src),
	KER_CLK(RCC_APB4ENR,  21, RCC_D3CCIPR,	21, 3, "sai4a", sai_src),
	KER_CLK(RCC_APB4ENR,  12, RCC_D3CCIPR,	13, 3, "lptim5", lptim_src2),
	KER_CLK(RCC_APB4ENR,  11, RCC_D3CCIPR,	13, 3, "lptim4", lptim_src2),
	KER_CLK(RCC_APB4ENR,  10, RCC_D3CCIPR,	13, 3, "lptim3", lptim_src2),
	KER_CLK(RCC_APB4ENR,   9, RCC_D3CCIPR,	10, 3, "lptim2", lptim_src2),
	KER_CLK(RCC_APB4ENR,   7, RCC_D3CCIPR,	 8, 2, "i2c4", i2c_src2),
	KER_CLK(RCC_APB4ENR,   5, RCC_D3CCIPR,	28, 3, "spi6", spi_src3),
	KER_CLK(RCC_APB4ENR,   3, RCC_D3CCIPR,	 0, 3, "lpuart1", lpuart1_src),
};

static struct composite_clk_gcfg kernel_clk_cfg = {
	M_CFG_MUX(NULL, 0),
	M_CFG_GATE(NULL, 0),
};

/* RTC clock */
/*
 * RTC & LSE registers are protected against parasitic write access.
 * PWR_CR_DBP bit must be set to enable write access to RTC registers.
 */
/* STM32_PWR_CR */
#define PWR_CR				0x00
/* STM32_PWR_CR bit field */
#define PWR_CR_DBP			BIT(8)

static struct composite_clk_gcfg rtc_clk_cfg = {
	M_CFG_MUX(NULL, 0),
	M_CFG_GATE(NULL, 0),
};

static const struct composite_clk_cfg rtc_clk =
	KER_CLK(RCC_BDCR, 15, RCC_BDCR, 8, 2, "rtc_ck", rtc_src);

/* Micro-controller output clock */
static struct composite_clk_gcfg mco_clk_cfg = {
	M_CFG_MUX(NULL, 0),
	M_CFG_DIV(NULL,	CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO),
};

#define M_MCO_F(_name, _parents, _mux_offset,  _mux_shift, _mux_width,\
		_rate_offset, _rate_shift, _rate_width,\
		_flags)\
{\
	.mux = &(struct muxdiv_cfg) {_mux_offset, _mux_shift, _mux_width },\
	.div = &(struct muxdiv_cfg) {_rate_offset, _rate_shift, _rate_width},\
	.gate = NULL,\
	.name = _name,\
	.parent_name = _parents,\
	.num_parents = ARRAY_SIZE(_parents),\
	.flags = _flags,\
}

static const struct composite_clk_cfg mco_clk[] = {
	M_MCO_F("mco1", mco_src1, RCC_CFGR, 22, 4, RCC_CFGR, 18, 4, 0),
	M_MCO_F("mco2", mco_src2, RCC_CFGR, 29, 3, RCC_CFGR, 25, 4, 0),
};

static void __init stm32h7_rcc_init(struct device_node *np)
{
	struct clk_hw_onecell_data *clk_data;
	struct composite_cfg c_cfg;
	int n;
	const char *hse_clk, *lse_clk, *i2s_clk;
	struct regmap *pdrm;

	clk_data = kzalloc(sizeof(*clk_data) +
			sizeof(*clk_data->hws) * STM32H7_MAX_CLKS,
			GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->num = STM32H7_MAX_CLKS;

	hws = clk_data->hws;

	for (n = 0; n < STM32H7_MAX_CLKS; n++)
		hws[n] = ERR_PTR(-ENOENT);

	/* get RCC base @ from DT */
	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: unable to map resource", np->name);
		goto err_free_clks;
	}

	pdrm = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(pdrm))
		pr_warn("%s: Unable to get syscfg\n", __func__);
	else
		/* In any case disable backup domain write protection
		 * and will never be enabled.
		 * Needed by LSE & RTC clocks.
		 */
		regmap_update_bits(pdrm, PWR_CR, PWR_CR_DBP, PWR_CR_DBP);

	/* Put parent names from DT */
	hse_clk = of_clk_get_parent_name(np, 0);
	lse_clk = of_clk_get_parent_name(np, 1);
	i2s_clk = of_clk_get_parent_name(np, 2);

	sai_src[3] = i2s_clk;
	spi_src1[3] = i2s_clk;

	/* Register Internal oscillators */
	clk_hw_register_fixed_rate(NULL, "clk-hsi", NULL, 0, 64000000);
	clk_hw_register_fixed_rate(NULL, "clk-csi", NULL, 0, 4000000);
	clk_hw_register_fixed_rate(NULL, "clk-lsi", NULL, 0, 32000);
	clk_hw_register_fixed_rate(NULL, "clk-rc48", NULL, 0, 48000);

	/* This clock is coming from outside. Frequencies unknown */
	hws[CK_DSI_PHY] = clk_hw_register_fixed_rate(NULL, "ck_dsi_phy", NULL,
			0, 0);

	hws[HSI_DIV] = clk_hw_register_divider(NULL, "hsidiv", "clk-hsi", 0,
			base + RCC_CR, 3, 2, CLK_DIVIDER_POWER_OF_TWO,
			&stm32rcc_lock);

	hws[HSE_1M] = clk_hw_register_divider(NULL, "hse_1M", "hse_ck",	0,
			base + RCC_CFGR, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO,
			&stm32rcc_lock);

	/* Mux system clocks */
	for (n = 0; n < ARRAY_SIZE(stm32_mclk); n++)
		hws[MCLK_BANK + n] = clk_hw_register_mux(NULL,
				stm32_mclk[n].name,
				stm32_mclk[n].parents,
				stm32_mclk[n].num_parents,
				stm32_mclk[n].flags,
				stm32_mclk[n].offset + base,
				stm32_mclk[n].shift,
				stm32_mclk[n].width,
				0,
				&stm32rcc_lock);

	register_core_and_bus_clocks();

	/* Oscillary clocks */
	for (n = 0; n < ARRAY_SIZE(stm32_oclk); n++)
		hws[OSC_BANK + n] = clk_register_ready_gate(NULL,
				stm32_oclk[n].name,
				stm32_oclk[n].parent,
				stm32_oclk[n].gate_offset + base,
				stm32_oclk[n].bit_idx,
				stm32_oclk[n].bit_rdy,
				stm32_oclk[n].flags,
				&stm32rcc_lock);

	hws[HSE_CK] = clk_register_ready_gate(NULL,
				"hse_ck",
				hse_clk,
				RCC_CR + base,
				16, 17,
				0,
				&stm32rcc_lock);

	hws[LSE_CK] = clk_register_ready_gate(NULL,
				"lse_ck",
				lse_clk,
				RCC_BDCR + base,
				0, 1,
				0,
				&stm32rcc_lock);

	hws[CSI_KER_DIV122 + n] = clk_hw_register_fixed_factor(NULL,
			"csi_ker_div122", "csi_ker", 0, 1, 122);

	/* PLLs */
	for (n = 0; n < ARRAY_SIZE(stm32_pll); n++) {
		int odf;

		/* Register the VCO */
		clk_register_stm32_pll(NULL, stm32_pll[n].name,
				stm32_pll[n].parent_name, stm32_pll[n].flags,
				stm32_pll[n].cfg,
				&stm32rcc_lock);

		/* Register the 3 output dividers */
		for (odf = 0; odf < 3; odf++) {
			int idx = n * 3 + odf;

			get_cfg_composite_div(&odf_clk_gcfg, &stm32_odf[n][odf],
					&c_cfg,	&stm32rcc_lock);

			hws[ODF_BANK + idx] = clk_hw_register_composite(NULL,
					stm32_odf[n][odf].name,
					stm32_odf[n][odf].parent_name,
					stm32_odf[n][odf].num_parents,
					c_cfg.mux_hw, c_cfg.mux_ops,
					c_cfg.div_hw, c_cfg.div_ops,
					c_cfg.gate_hw, c_cfg.gate_ops,
					stm32_odf[n][odf].flags);
		}
	}

	/* Peripheral clocks */
	for (n = 0; n < ARRAY_SIZE(pclk); n++)
		hws[PERIF_BANK + n] = clk_hw_register_gate(NULL, pclk[n].name,
				pclk[n].parent,
				pclk[n].flags, base + pclk[n].gate_offset,
				pclk[n].bit_idx, pclk[n].flags, &stm32rcc_lock);

	/* Kernel clocks */
	for (n = 0; n < ARRAY_SIZE(kclk); n++) {
		get_cfg_composite_div(&kernel_clk_cfg, &kclk[n], &c_cfg,
				&stm32rcc_lock);

		hws[KERN_BANK + n] = clk_hw_register_composite(NULL,
				kclk[n].name,
				kclk[n].parent_name,
				kclk[n].num_parents,
				c_cfg.mux_hw, c_cfg.mux_ops,
				c_cfg.div_hw, c_cfg.div_ops,
				c_cfg.gate_hw, c_cfg.gate_ops,
				kclk[n].flags);
	}

	/* RTC clock (default state is off) */
	clk_hw_register_fixed_rate(NULL, "off", NULL, 0, 0);

	get_cfg_composite_div(&rtc_clk_cfg, &rtc_clk, &c_cfg, &stm32rcc_lock);

	hws[RTC_CK] = clk_hw_register_composite(NULL,
			rtc_clk.name,
			rtc_clk.parent_name,
			rtc_clk.num_parents,
			c_cfg.mux_hw, c_cfg.mux_ops,
			c_cfg.div_hw, c_cfg.div_ops,
			c_cfg.gate_hw, c_cfg.gate_ops,
			rtc_clk.flags);

	/* Micro-controller clocks */
	for (n = 0; n < ARRAY_SIZE(mco_clk); n++) {
		get_cfg_composite_div(&mco_clk_cfg, &mco_clk[n], &c_cfg,
				&stm32rcc_lock);

		hws[MCO_BANK + n] = clk_hw_register_composite(NULL,
				mco_clk[n].name,
				mco_clk[n].parent_name,
				mco_clk[n].num_parents,
				c_cfg.mux_hw, c_cfg.mux_ops,
				c_cfg.div_hw, c_cfg.div_ops,
				c_cfg.gate_hw, c_cfg.gate_ops,
				mco_clk[n].flags);
	}

	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);

	return;

err_free_clks:
	kfree(clk_data);
}

/* The RCC node is a clock and reset controller, and these
 * functionalities are supported by different drivers that
 * matches the same compatible strings.
 */
CLK_OF_DECLARE_DRIVER(stm32h7_rcc, "st,stm32h743-rcc", stm32h7_rcc_init);
