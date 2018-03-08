// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Olivier Bideau <olivier.bideau@st.com> for STMicroelectronics.
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/stm32mp1-clks.h>

static DEFINE_SPINLOCK(rlock);

#define RCC_OCENSETR		0x0C
#define RCC_HSICFGR		0x18
#define RCC_RDLSICR		0x144
#define RCC_PLL1CR		0x80
#define RCC_PLL1CFGR1		0x84
#define RCC_PLL1CFGR2		0x88
#define RCC_PLL2CR		0x94
#define RCC_PLL2CFGR1		0x98
#define RCC_PLL2CFGR2		0x9C
#define RCC_PLL3CR		0x880
#define RCC_PLL3CFGR1		0x884
#define RCC_PLL3CFGR2		0x888
#define RCC_PLL4CR		0x894
#define RCC_PLL4CFGR1		0x898
#define RCC_PLL4CFGR2		0x89C
#define RCC_APB1ENSETR		0xA00
#define RCC_APB2ENSETR		0xA08
#define RCC_APB3ENSETR		0xA10
#define RCC_APB4ENSETR		0x200
#define RCC_APB5ENSETR		0x208
#define RCC_AHB2ENSETR		0xA18
#define RCC_AHB3ENSETR		0xA20
#define RCC_AHB4ENSETR		0xA28
#define RCC_AHB5ENSETR		0x210
#define RCC_AHB6ENSETR		0x218
#define RCC_AHB6LPENSETR	0x318
#define RCC_RCK12SELR		0x28
#define RCC_RCK3SELR		0x820
#define RCC_RCK4SELR		0x824
#define RCC_MPCKSELR		0x20
#define RCC_ASSCKSELR		0x24
#define RCC_MSSCKSELR		0x48
#define RCC_SPI6CKSELR		0xC4
#define RCC_SDMMC12CKSELR	0x8F4
#define RCC_SDMMC3CKSELR	0x8F8
#define RCC_FMCCKSELR		0x904
#define RCC_I2C46CKSELR		0xC0
#define RCC_I2C12CKSELR		0x8C0
#define RCC_I2C35CKSELR		0x8C4
#define RCC_UART1CKSELR		0xC8
#define RCC_QSPICKSELR		0x900
#define RCC_ETHCKSELR		0x8FC
#define RCC_RNG1CKSELR		0xCC
#define RCC_RNG2CKSELR		0x920
#define RCC_GPUCKSELR		0x938
#define RCC_USBCKSELR		0x91C
#define RCC_STGENCKSELR		0xD4
#define RCC_SPDIFCKSELR		0x914
#define RCC_SPI2S1CKSELR	0x8D8
#define RCC_SPI2S23CKSELR	0x8DC
#define RCC_SPI2S45CKSELR	0x8E0
#define RCC_CECCKSELR		0x918
#define RCC_LPTIM1CKSELR	0x934
#define RCC_LPTIM23CKSELR	0x930
#define RCC_LPTIM45CKSELR	0x92C
#define RCC_UART24CKSELR	0x8E8
#define RCC_UART35CKSELR	0x8EC
#define RCC_UART6CKSELR		0x8E4
#define RCC_UART78CKSELR	0x8F0
#define RCC_FDCANCKSELR		0x90C
#define RCC_SAI1CKSELR		0x8C8
#define RCC_SAI2CKSELR		0x8CC
#define RCC_SAI3CKSELR		0x8D0
#define RCC_SAI4CKSELR		0x8D4
#define RCC_ADCCKSELR		0x928
#define RCC_MPCKDIVR		0x2C
#define RCC_DSICKSELR		0x924
#define RCC_CPERCKSELR		0xD0
#define RCC_MCO1CFGR		0x800
#define RCC_MCO2CFGR		0x804
#define RCC_BDCR		0x140
#define RCC_AXIDIVR		0x30
#define RCC_MCUDIVR		0x830
#define RCC_APB1DIVR		0x834
#define RCC_APB2DIVR		0x838
#define RCC_APB3DIVR		0x83C
#define RCC_APB4DIVR		0x3C
#define RCC_APB5DIVR		0x40
#define RCC_TIMG1PRER		0x828
#define RCC_TIMG2PRER		0x82C
#define RCC_RTCDIVR		0x44
#define RCC_DBGCFGR		0x80C

#define RCC_CLR	0x4

static const char * const ref12_parents[] = {
	"ck_hsi", "ck_hse"
};

static const char * const ref3_parents[] = {
	"ck_hsi", "ck_hse", "ck_csi"
};

static const char * const ref4_parents[] = {
	"ck_hsi", "ck_hse", "ck_csi"
};

static const char * const cpu_src[] = {
	"ck_hsi", "ck_hse", "pll1_p"
};

static const char * const axi_src[] = {
	"ck_hsi", "ck_hse", "pll2_p", "pll3_p"
};

static const char * const per_src[] = {
	"ck_hsi", "ck_csi", "ck_hse"
};

static const char * const mcu_src[] = {
	"ck_hsi", "ck_hse", "ck_csi", "pll3_p"
};

static const struct clk_div_table axi_div_table[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
	{ 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 },
	{ 0 },
};

static const struct clk_div_table mcu_div_table[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 4 }, { 3, 8 },
	{ 4, 16 }, { 5, 32 }, { 6, 64 }, { 7, 128 },
	{ 8, 512 }, { 9, 512 }, { 10, 512}, { 11, 512 },
	{ 12, 512 }, { 13, 512 }, { 14, 512}, { 15, 512 },
	{ 0 },
};

static const struct clk_div_table apb_div_table[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 4 }, { 3, 8 },
	{ 4, 16 }, { 5, 16 }, { 6, 16 }, { 7, 16 },
	{ 0 },
};

struct clock_config {
	u32 id;
	const char *name;
	union {
		const char *parent_name;
		const char * const *parent_names;
	};
	int num_parents;
	unsigned long flags;
	void *cfg;
	struct clk_hw * (*func)(struct device *dev,
				struct clk_hw_onecell_data *clk_data,
				void __iomem *base, spinlock_t *lock,
				const struct clock_config *cfg);
};

#define NO_ID ~0

struct gate_cfg {
	u32 reg_off;
	u8 bit_idx;
	u8 gate_flags;
};

struct fixed_factor_cfg {
	unsigned int mult;
	unsigned int div;
};

struct div_cfg {
	u32 reg_off;
	u8 shift;
	u8 width;
	u8 div_flags;
	const struct clk_div_table *table;
};

struct mux_cfg {
	u32 reg_off;
	u8 shift;
	u8 width;
	u8 mux_flags;
	u32 *table;
};

struct stm32_gate_cfg {
	struct gate_cfg		*gate;
	const struct clk_ops	*ops;
};

struct stm32_div_cfg {
	struct div_cfg		*div;
	const struct clk_ops	*ops;
};

struct stm32_mux_cfg {
	struct mux_cfg		*mux;
	const struct clk_ops	*ops;
};

/* STM32 Composite clock */
struct stm32_composite_cfg {
	const struct stm32_gate_cfg	*gate;
	const struct stm32_div_cfg	*div;
	const struct stm32_mux_cfg	*mux;
};

static struct clk_hw *
_clk_hw_register_gate(struct device *dev,
		      struct clk_hw_onecell_data *clk_data,
		      void __iomem *base, spinlock_t *lock,
		      const struct clock_config *cfg)
{
	struct gate_cfg *gate_cfg = cfg->cfg;

	return clk_hw_register_gate(dev,
				    cfg->name,
				    cfg->parent_name,
				    cfg->flags,
				    gate_cfg->reg_off + base,
				    gate_cfg->bit_idx,
				    gate_cfg->gate_flags,
				    lock);
}

static struct clk_hw *
_clk_hw_register_fixed_factor(struct device *dev,
			      struct clk_hw_onecell_data *clk_data,
			      void __iomem *base, spinlock_t *lock,
			      const struct clock_config *cfg)
{
	struct fixed_factor_cfg *ff_cfg = cfg->cfg;

	return clk_hw_register_fixed_factor(dev, cfg->name, cfg->parent_name,
					    cfg->flags, ff_cfg->mult,
					    ff_cfg->div);
}

static struct clk_hw *
_clk_hw_register_divider_table(struct device *dev,
			       struct clk_hw_onecell_data *clk_data,
			       void __iomem *base, spinlock_t *lock,
			       const struct clock_config *cfg)
{
	struct div_cfg *div_cfg = cfg->cfg;

	return clk_hw_register_divider_table(dev,
					     cfg->name,
					     cfg->parent_name,
					     cfg->flags,
					     div_cfg->reg_off + base,
					     div_cfg->shift,
					     div_cfg->width,
					     div_cfg->div_flags,
					     div_cfg->table,
					     lock);
}

static struct clk_hw *
_clk_hw_register_mux(struct device *dev,
		     struct clk_hw_onecell_data *clk_data,
		     void __iomem *base, spinlock_t *lock,
		     const struct clock_config *cfg)
{
	struct mux_cfg *mux_cfg = cfg->cfg;

	return clk_hw_register_mux(dev, cfg->name, cfg->parent_names,
				   cfg->num_parents, cfg->flags,
				   mux_cfg->reg_off + base, mux_cfg->shift,
				   mux_cfg->width, mux_cfg->mux_flags, lock);
}

/* MP1 Gate clock with set & clear registers */

static int mp1_gate_clk_enable(struct clk_hw *hw)
{
	if (!clk_gate_ops.is_enabled(hw))
		clk_gate_ops.enable(hw);

	return 0;
}

static void mp1_gate_clk_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	unsigned long flags = 0;

	if (clk_gate_ops.is_enabled(hw)) {
		spin_lock_irqsave(gate->lock, flags);
		writel_relaxed(BIT(gate->bit_idx), gate->reg + RCC_CLR);
		spin_unlock_irqrestore(gate->lock, flags);
	}
}

const struct clk_ops mp1_gate_clk_ops = {
	.enable		= mp1_gate_clk_enable,
	.disable	= mp1_gate_clk_disable,
	.is_enabled	= clk_gate_is_enabled,
};

static struct clk_hw *_get_stm32_mux(void __iomem *base,
				     const struct stm32_mux_cfg *cfg,
				     spinlock_t *lock)
{
	struct clk_mux *mux;
	struct clk_hw *mux_hw;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->reg = cfg->mux->reg_off + base;
	mux->shift = cfg->mux->shift;
	mux->mask = (1 << cfg->mux->width) - 1;
	mux->flags = cfg->mux->mux_flags;
	mux->table = cfg->mux->table;

	mux->lock = lock;

	mux_hw = &mux->hw;

	return mux_hw;
}

static struct clk_hw *_get_stm32_div(void __iomem *base,
				     const struct stm32_div_cfg *cfg,
				     spinlock_t *lock)
{
	struct clk_divider *div;

	div = kzalloc(sizeof(*div), GFP_KERNEL);

	if (!div)
		return ERR_PTR(-ENOMEM);

	div->reg = cfg->div->reg_off + base;
	div->shift = cfg->div->shift;
	div->width = cfg->div->width;
	div->flags = cfg->div->div_flags;
	div->table = cfg->div->table;
	div->lock = lock;

	return &div->hw;
}

static struct clk_hw *
_get_stm32_gate(void __iomem *base,
		const struct stm32_gate_cfg *cfg, spinlock_t *lock)
{
	struct clk_gate *gate;
	struct clk_hw *gate_hw;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->reg = cfg->gate->reg_off + base;
	gate->bit_idx = cfg->gate->bit_idx;
	gate->flags = cfg->gate->gate_flags;
	gate->lock = lock;
	gate_hw = &gate->hw;

	return gate_hw;
}

static struct clk_hw *
clk_stm32_register_gate_ops(struct device *dev,
			    const char *name,
			    const char *parent_name,
			    unsigned long flags,
			    void __iomem *base,
			    const struct stm32_gate_cfg *cfg,
			    spinlock_t *lock)
{
	struct clk_init_data init = { NULL };
	struct clk_gate *gate;
	struct clk_hw *hw;
	int ret;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = flags;

	init.ops = &clk_gate_ops;

	if (cfg->ops)
		init.ops = cfg->ops;

	hw = _get_stm32_gate(base, cfg, lock);
	if (IS_ERR(hw))
		return ERR_PTR(-ENOMEM);

	hw->init = &init;

	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static struct clk_hw *
clk_stm32_register_composite(struct device *dev,
			     const char *name, const char * const *parent_names,
			     int num_parents, void __iomem *base,
			     const struct stm32_composite_cfg *cfg,
			     unsigned long flags, spinlock_t *lock)
{
	const struct clk_ops *mux_ops, *div_ops, *gate_ops;
	struct clk_hw *mux_hw, *div_hw, *gate_hw;

	mux_hw = NULL;
	div_hw = NULL;
	gate_hw = NULL;
	mux_ops = NULL;
	div_ops = NULL;
	gate_ops = NULL;

	if (cfg->mux) {
		mux_hw = _get_stm32_mux(base, cfg->mux, lock);

		if (!IS_ERR(mux_hw)) {
			mux_ops = &clk_mux_ops;

			if (cfg->mux->ops)
				mux_ops = cfg->mux->ops;
		}
	}

	if (cfg->div) {
		div_hw = _get_stm32_div(base, cfg->div, lock);

		if (!IS_ERR(div_hw)) {
			div_ops = &clk_divider_ops;

			if (cfg->div->ops)
				div_ops = cfg->div->ops;
		}
	}

	if (cfg->gate) {
		gate_hw = _get_stm32_gate(base, cfg->gate, lock);

		if (!IS_ERR(gate_hw)) {
			gate_ops = &clk_gate_ops;

			if (cfg->gate->ops)
				gate_ops = cfg->gate->ops;
		}
	}

	return clk_hw_register_composite(dev, name, parent_names, num_parents,
				       mux_hw, mux_ops, div_hw, div_ops,
				       gate_hw, gate_ops, flags);
}

/* STM32 PLL */

struct stm32_pll_obj {
	/* lock pll enable/disable registers */
	spinlock_t *lock;
	void __iomem *reg;
	struct clk_hw hw;
};

#define to_pll(_hw) container_of(_hw, struct stm32_pll_obj, hw)

#define PLL_ON		BIT(0)
#define PLL_RDY		BIT(1)
#define DIVN_MASK	0x1FF
#define DIVM_MASK	0x3F
#define DIVM_SHIFT	16
#define DIVN_SHIFT	0
#define FRAC_OFFSET	0xC
#define FRAC_MASK	0x1FFF
#define FRAC_SHIFT	3
#define FRACLE		BIT(16)

static int __pll_is_enabled(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);

	return readl_relaxed(clk_elem->reg) & PLL_ON;
}

#define TIMEOUT 5

static int pll_enable(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	u32 reg;
	unsigned long flags = 0;
	unsigned int timeout = TIMEOUT;
	int bit_status = 0;

	spin_lock_irqsave(clk_elem->lock, flags);

	if (__pll_is_enabled(hw))
		goto unlock;

	reg = readl_relaxed(clk_elem->reg);
	reg |= PLL_ON;
	writel_relaxed(reg, clk_elem->reg);

	/* We can't use readl_poll_timeout() because we can be blocked if
	 * someone enables this clock before clocksource changes.
	 * Only jiffies counter is available. Jiffies are incremented by
	 * interruptions and enable op does not allow to be interrupted.
	 */
	do {
		bit_status = !(readl_relaxed(clk_elem->reg) & PLL_RDY);

		if (bit_status)
			udelay(120);

	} while (bit_status && --timeout);

unlock:
	spin_unlock_irqrestore(clk_elem->lock, flags);

	return bit_status;
}

static void pll_disable(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	u32 reg;
	unsigned long flags = 0;

	spin_lock_irqsave(clk_elem->lock, flags);

	reg = readl_relaxed(clk_elem->reg);
	reg &= ~PLL_ON;
	writel_relaxed(reg, clk_elem->reg);

	spin_unlock_irqrestore(clk_elem->lock, flags);
}

static u32 pll_frac_val(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	u32 reg, frac = 0;

	reg = readl_relaxed(clk_elem->reg + FRAC_OFFSET);
	if (reg & FRACLE)
		frac = (reg >> FRAC_SHIFT) & FRAC_MASK;

	return frac;
}

static unsigned long pll_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	u32 reg;
	u32 frac, divm, divn;
	u64 rate, rate_frac = 0;

	reg = readl_relaxed(clk_elem->reg + 4);

	divm = ((reg >> DIVM_SHIFT) & DIVM_MASK) + 1;
	divn = ((reg >> DIVN_SHIFT) & DIVN_MASK) + 1;
	rate = (u64)parent_rate * divn;

	do_div(rate, divm);

	frac = pll_frac_val(hw);
	if (frac) {
		rate_frac = (u64)parent_rate * (u64)frac;
		do_div(rate_frac, (divm * 8192));
	}

	return rate + rate_frac;
}

static int pll_is_enabled(struct clk_hw *hw)
{
	struct stm32_pll_obj *clk_elem = to_pll(hw);
	unsigned long flags = 0;
	int ret;

	spin_lock_irqsave(clk_elem->lock, flags);
	ret = __pll_is_enabled(hw);
	spin_unlock_irqrestore(clk_elem->lock, flags);

	return ret;
}

static const struct clk_ops pll_ops = {
	.enable		= pll_enable,
	.disable	= pll_disable,
	.recalc_rate	= pll_recalc_rate,
	.is_enabled	= pll_is_enabled,
};

static struct clk_hw *clk_register_pll(struct device *dev, const char *name,
				       const char *parent_name,
				       void __iomem *reg,
				       unsigned long flags,
				       spinlock_t *lock)
{
	struct stm32_pll_obj *element;
	struct clk_init_data init;
	struct clk_hw *hw;
	int err;

	element = kzalloc(sizeof(*element), GFP_KERNEL);
	if (!element)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &pll_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	element->hw.init = &init;
	element->reg = reg;
	element->lock = lock;

	hw = &element->hw;
	err = clk_hw_register(dev, hw);

	if (err) {
		kfree(element);
		return ERR_PTR(err);
	}

	return hw;
}

/* Kernel Timer */
struct timer_cker {
	/* lock the kernel output divider register */
	spinlock_t *lock;
	void __iomem *apbdiv;
	void __iomem *timpre;
	struct clk_hw hw;
};

#define to_timer_cker(_hw) container_of(_hw, struct timer_cker, hw)

#define APB_DIV_MASK 0x07
#define TIM_PRE_MASK 0x01

static unsigned long __bestmult(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct timer_cker *tim_ker = to_timer_cker(hw);
	u32 prescaler;
	unsigned int mult = 0;

	prescaler = readl_relaxed(tim_ker->apbdiv) & APB_DIV_MASK;
	if (prescaler < 2)
		return 1;

	mult = 2;

	if (rate / parent_rate >= 4)
		mult = 4;

	return mult;
}

static long timer_ker_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	unsigned long factor = __bestmult(hw, rate, *parent_rate);

	return *parent_rate * factor;
}

static int timer_ker_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct timer_cker *tim_ker = to_timer_cker(hw);
	unsigned long flags = 0;
	unsigned long factor = __bestmult(hw, rate, parent_rate);
	int ret = 0;

	spin_lock_irqsave(tim_ker->lock, flags);

	switch (factor) {
	case 1:
		break;
	case 2:
		writel_relaxed(0, tim_ker->timpre);
		break;
	case 4:
		writel_relaxed(1, tim_ker->timpre);
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(tim_ker->lock, flags);

	return ret;
}

static unsigned long timer_ker_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct timer_cker *tim_ker = to_timer_cker(hw);
	u32 prescaler, timpre;
	u32 mul;

	prescaler = readl_relaxed(tim_ker->apbdiv) & APB_DIV_MASK;

	timpre = readl_relaxed(tim_ker->timpre) & TIM_PRE_MASK;

	if (!prescaler)
		return parent_rate;

	mul = (timpre + 1) * 2;

	return parent_rate * mul;
}

static const struct clk_ops timer_ker_ops = {
	.recalc_rate	= timer_ker_recalc_rate,
	.round_rate	= timer_ker_round_rate,
	.set_rate	= timer_ker_set_rate,

};

static struct clk_hw *clk_register_cktim(struct device *dev, const char *name,
					 const char *parent_name,
					 unsigned long flags,
					 void __iomem *apbdiv,
					 void __iomem *timpre,
					 spinlock_t *lock)
{
	struct timer_cker *tim_ker;
	struct clk_init_data init;
	struct clk_hw *hw;
	int err;

	tim_ker = kzalloc(sizeof(*tim_ker), GFP_KERNEL);
	if (!tim_ker)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &timer_ker_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	tim_ker->hw.init = &init;
	tim_ker->lock = lock;
	tim_ker->apbdiv = apbdiv;
	tim_ker->timpre = timpre;

	hw = &tim_ker->hw;
	err = clk_hw_register(dev, hw);

	if (err) {
		kfree(tim_ker);
		return ERR_PTR(err);
	}

	return hw;
}

struct stm32_pll_cfg {
	u32 offset;
};

struct clk_hw *_clk_register_pll(struct device *dev,
				 struct clk_hw_onecell_data *clk_data,
				 void __iomem *base, spinlock_t *lock,
				 const struct clock_config *cfg)
{
	struct stm32_pll_cfg *stm_pll_cfg = cfg->cfg;

	return clk_register_pll(dev, cfg->name, cfg->parent_name,
				base + stm_pll_cfg->offset, cfg->flags, lock);
}

struct stm32_cktim_cfg {
	u32 offset_apbdiv;
	u32 offset_timpre;
};

static struct clk_hw *_clk_register_cktim(struct device *dev,
					  struct clk_hw_onecell_data *clk_data,
					  void __iomem *base, spinlock_t *lock,
					  const struct clock_config *cfg)
{
	struct stm32_cktim_cfg *cktim_cfg = cfg->cfg;

	return clk_register_cktim(dev, cfg->name, cfg->parent_name, cfg->flags,
				  cktim_cfg->offset_apbdiv + base,
				  cktim_cfg->offset_timpre + base, lock);
}

static struct clk_hw *
_clk_stm32_register_gate(struct device *dev,
			 struct clk_hw_onecell_data *clk_data,
			 void __iomem *base, spinlock_t *lock,
			 const struct clock_config *cfg)
{
	return clk_stm32_register_gate_ops(dev,
				    cfg->name,
				    cfg->parent_name,
				    cfg->flags,
				    base,
				    cfg->cfg,
				    lock);
}

static struct clk_hw *
_clk_stm32_register_composite(struct device *dev,
			      struct clk_hw_onecell_data *clk_data,
			      void __iomem *base, spinlock_t *lock,
			      const struct clock_config *cfg)
{
	return clk_stm32_register_composite(dev, cfg->name, cfg->parent_names,
					    cfg->num_parents, base, cfg->cfg,
					    cfg->flags, lock);
}

#define GATE(_id, _name, _parent, _flags, _offset, _bit_idx, _gate_flags)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct gate_cfg) {\
		.reg_off	= _offset,\
		.bit_idx	= _bit_idx,\
		.gate_flags	= _gate_flags,\
	},\
	.func		= _clk_hw_register_gate,\
}

#define FIXED_FACTOR(_id, _name, _parent, _flags, _mult, _div)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct fixed_factor_cfg) {\
		.mult = _mult,\
		.div = _div,\
	},\
	.func		= _clk_hw_register_fixed_factor,\
}

#define DIV_TABLE(_id, _name, _parent, _flags, _offset, _shift, _width,\
		  _div_flags, _div_table)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct div_cfg) {\
		.reg_off	= _offset,\
		.shift		= _shift,\
		.width		= _width,\
		.div_flags	= _div_flags,\
		.table		= _div_table,\
	},\
	.func		= _clk_hw_register_divider_table,\
}

#define DIV(_id, _name, _parent, _flags, _offset, _shift, _width, _div_flags)\
	DIV_TABLE(_id, _name, _parent, _flags, _offset, _shift, _width,\
		  _div_flags, NULL)

#define MUX(_id, _name, _parents, _flags, _offset, _shift, _width, _mux_flags)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_names	= _parents,\
	.num_parents	= ARRAY_SIZE(_parents),\
	.flags		= _flags,\
	.cfg		=  &(struct mux_cfg) {\
		.reg_off	= _offset,\
		.shift		= _shift,\
		.width		= _width,\
		.mux_flags	= _mux_flags,\
	},\
	.func		= _clk_hw_register_mux,\
}

#define PLL(_id, _name, _parent, _flags, _offset)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct stm32_pll_cfg) {\
		.offset = _offset,\
	},\
	.func		= _clk_register_pll,\
}

#define STM32_CKTIM(_name, _parent, _flags, _offset_apbdiv, _offset_timpre)\
{\
	.id		= NO_ID,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct stm32_cktim_cfg) {\
		.offset_apbdiv = _offset_apbdiv,\
		.offset_timpre = _offset_timpre,\
	},\
	.func		= _clk_register_cktim,\
}

#define STM32_TIM(_id, _name, _parent, _offset_set, _bit_idx)\
		  GATE_MP1(_id, _name, _parent, CLK_SET_RATE_PARENT,\
			   _offset_set, _bit_idx, 0)

/* STM32 GATE */
#define STM32_GATE(_id, _name, _parent, _flags, _gate)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		= (struct stm32_gate_cfg *) {_gate},\
	.func		= _clk_stm32_register_gate,\
}

#define _STM32_GATE(_gate_offset, _gate_bit_idx, _gate_flags, _ops)\
	(&(struct stm32_gate_cfg) {\
		&(struct gate_cfg) {\
			.reg_off	= _gate_offset,\
			.bit_idx	= _gate_bit_idx,\
			.gate_flags	= _gate_flags,\
		},\
		.ops		= _ops,\
	})

#define _GATE(_gate_offset, _gate_bit_idx, _gate_flags)\
	_STM32_GATE(_gate_offset, _gate_bit_idx, _gate_flags,\
		    NULL)\

#define _GATE_MP1(_gate_offset, _gate_bit_idx, _gate_flags)\
	_STM32_GATE(_gate_offset, _gate_bit_idx, _gate_flags,\
		    &mp1_gate_clk_ops)\

#define GATE_MP1(_id, _name, _parent, _flags, _offset, _bit_idx, _gate_flags)\
	STM32_GATE(_id, _name, _parent, _flags,\
		   _GATE_MP1(_offset, _bit_idx, _gate_flags))

#define _STM32_DIV(_div_offset, _div_shift, _div_width,\
		   _div_flags, _div_table, _ops)\
	.div = &(struct stm32_div_cfg) {\
		&(struct div_cfg) {\
			.reg_off	= _div_offset,\
			.shift		= _div_shift,\
			.width		= _div_width,\
			.div_flags	= _div_flags,\
			.table		= _div_table,\
		},\
		.ops		= _ops,\
	}

#define _DIV(_div_offset, _div_shift, _div_width, _div_flags, _div_table)\
	_STM32_DIV(_div_offset, _div_shift, _div_width,\
		   _div_flags, _div_table, NULL)\

#define _STM32_MUX(_offset, _shift, _width, _mux_flags, _ops)\
	.mux = &(struct stm32_mux_cfg) {\
		&(struct mux_cfg) {\
			.reg_off	= _offset,\
			.shift		= _shift,\
			.width		= _width,\
			.mux_flags	= _mux_flags,\
			.table		= NULL,\
		},\
		.ops		= _ops,\
	}

#define _MUX(_offset, _shift, _width, _mux_flags)\
	_STM32_MUX(_offset, _shift, _width, _mux_flags, NULL)\

#define PARENT(_parent) ((const char *[]) { _parent})

#define _NO_MUX .mux = NULL
#define _NO_DIV .div = NULL
#define _NO_GATE .gate = NULL

#define COMPOSITE(_id, _name, _parents, _flags, _gate, _mux, _div)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_names	= _parents,\
	.num_parents	= ARRAY_SIZE(_parents),\
	.flags		= _flags,\
	.cfg		= &(struct stm32_composite_cfg) {\
		_gate,\
		_mux,\
		_div,\
	},\
	.func		= _clk_stm32_register_composite,\
}

static const struct clock_config stm32mp1_clock_cfg[] = {
	/* Oscillator divider */
	DIV(NO_ID, "clk-hsi-div", "clk-hsi", 0, RCC_HSICFGR, 0, 2,
	    CLK_DIVIDER_READ_ONLY),

	/*  External / Internal Oscillators */
	GATE_MP1(CK_HSE, "ck_hse", "clk-hse", 0, RCC_OCENSETR, 8, 0),
	GATE_MP1(CK_CSI, "ck_csi", "clk-csi", 0, RCC_OCENSETR, 4, 0),
	GATE_MP1(CK_HSI, "ck_hsi", "clk-hsi-div", 0, RCC_OCENSETR, 0, 0),
	GATE(CK_LSI, "ck_lsi", "clk-lsi", 0, RCC_RDLSICR, 0, 0),
	GATE(CK_LSE, "ck_lse", "clk-lse", 0, RCC_BDCR, 0, 0),

	FIXED_FACTOR(CK_HSE_DIV2, "clk-hse-div2", "ck_hse", 0, 1, 2),

	/* ref clock pll */
	MUX(NO_ID, "ref1", ref12_parents, CLK_OPS_PARENT_ENABLE, RCC_RCK12SELR,
	    0, 2, CLK_MUX_READ_ONLY),

	MUX(NO_ID, "ref3", ref3_parents, CLK_OPS_PARENT_ENABLE, RCC_RCK3SELR,
	    0, 2, CLK_MUX_READ_ONLY),

	MUX(NO_ID, "ref4", ref4_parents, CLK_OPS_PARENT_ENABLE, RCC_RCK4SELR,
	    0, 2, CLK_MUX_READ_ONLY),

	/* PLLs */
	PLL(PLL1, "pll1", "ref1", CLK_IGNORE_UNUSED, RCC_PLL1CR),
	PLL(PLL2, "pll2", "ref1", CLK_IGNORE_UNUSED, RCC_PLL2CR),
	PLL(PLL3, "pll3", "ref3", CLK_IGNORE_UNUSED, RCC_PLL3CR),
	PLL(PLL4, "pll4", "ref4", CLK_IGNORE_UNUSED, RCC_PLL4CR),

	/* ODF */
	COMPOSITE(PLL1_P, "pll1_p", PARENT("pll1"), 0,
		  _GATE(RCC_PLL1CR, 4, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL1CFGR2, 0, 7, 0, NULL)),

	COMPOSITE(PLL2_P, "pll2_p", PARENT("pll2"), 0,
		  _GATE(RCC_PLL2CR, 4, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL2CFGR2, 0, 7, 0, NULL)),

	COMPOSITE(PLL2_Q, "pll2_q", PARENT("pll2"), 0,
		  _GATE(RCC_PLL2CR, 5, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL2CFGR2, 8, 7, 0, NULL)),

	COMPOSITE(PLL2_R, "pll2_r", PARENT("pll2"), CLK_IS_CRITICAL,
		  _GATE(RCC_PLL2CR, 6, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL2CFGR2, 16, 7, 0, NULL)),

	COMPOSITE(PLL3_P, "pll3_p", PARENT("pll3"), 0,
		  _GATE(RCC_PLL3CR, 4, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL3CFGR2, 0, 7, 0, NULL)),

	COMPOSITE(PLL3_Q, "pll3_q", PARENT("pll3"), 0,
		  _GATE(RCC_PLL3CR, 5, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL3CFGR2, 8, 7, 0, NULL)),

	COMPOSITE(PLL3_R, "pll3_r", PARENT("pll3"), 0,
		  _GATE(RCC_PLL3CR, 6, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL3CFGR2, 16, 7, 0, NULL)),

	COMPOSITE(PLL4_P, "pll4_p", PARENT("pll4"), 0,
		  _GATE(RCC_PLL4CR, 4, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL4CFGR2, 0, 7, 0, NULL)),

	COMPOSITE(PLL4_Q, "pll4_q", PARENT("pll4"), 0,
		  _GATE(RCC_PLL4CR, 5, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL4CFGR2, 8, 7, 0, NULL)),

	COMPOSITE(PLL4_R, "pll4_r", PARENT("pll4"), 0,
		  _GATE(RCC_PLL4CR, 6, 0),
		  _NO_MUX,
		  _DIV(RCC_PLL4CFGR2, 16, 7, 0, NULL)),

	/* MUX system clocks */
	MUX(CK_PER, "ck_per", per_src, CLK_OPS_PARENT_ENABLE,
	    RCC_CPERCKSELR, 0, 2, 0),

	MUX(CK_MPU, "ck_mpu", cpu_src, CLK_OPS_PARENT_ENABLE |
	     CLK_IS_CRITICAL, RCC_MPCKSELR, 0, 2, 0),

	COMPOSITE(CK_AXI, "ck_axi", axi_src, CLK_IS_CRITICAL |
		   CLK_OPS_PARENT_ENABLE,
		   _NO_GATE,
		   _MUX(RCC_ASSCKSELR, 0, 2, 0),
		   _DIV(RCC_AXIDIVR, 0, 3, 0, axi_div_table)),

	COMPOSITE(CK_MCU, "ck_mcu", mcu_src, CLK_IS_CRITICAL |
		   CLK_OPS_PARENT_ENABLE,
		   _NO_GATE,
		   _MUX(RCC_MSSCKSELR, 0, 2, 0),
		   _DIV(RCC_MCUDIVR, 0, 4, 0, mcu_div_table)),

	DIV_TABLE(NO_ID, "pclk1", "ck_mcu", CLK_IGNORE_UNUSED, RCC_APB1DIVR, 0,
		  3, CLK_DIVIDER_READ_ONLY, apb_div_table),

	DIV_TABLE(NO_ID, "pclk2", "ck_mcu", CLK_IGNORE_UNUSED, RCC_APB2DIVR, 0,
		  3, CLK_DIVIDER_READ_ONLY, apb_div_table),

	DIV_TABLE(NO_ID, "pclk3", "ck_mcu", CLK_IGNORE_UNUSED, RCC_APB3DIVR, 0,
		  3, CLK_DIVIDER_READ_ONLY, apb_div_table),

	DIV_TABLE(NO_ID, "pclk4", "ck_axi", CLK_IGNORE_UNUSED, RCC_APB4DIVR, 0,
		  3, CLK_DIVIDER_READ_ONLY, apb_div_table),

	DIV_TABLE(NO_ID, "pclk5", "ck_axi", CLK_IGNORE_UNUSED, RCC_APB5DIVR, 0,
		  3, CLK_DIVIDER_READ_ONLY, apb_div_table),

	/* Kernel Timers */
	STM32_CKTIM("ck1_tim", "pclk1", 0, RCC_APB1DIVR, RCC_TIMG1PRER),
	STM32_CKTIM("ck2_tim", "pclk2", 0, RCC_APB2DIVR, RCC_TIMG2PRER),

	STM32_TIM(TIM2_K, "tim2_k", "ck1_tim", RCC_APB1ENSETR, 0),
	STM32_TIM(TIM3_K, "tim3_k", "ck1_tim", RCC_APB1ENSETR, 1),
	STM32_TIM(TIM4_K, "tim4_k", "ck1_tim", RCC_APB1ENSETR, 2),
	STM32_TIM(TIM5_K, "tim5_k", "ck1_tim", RCC_APB1ENSETR, 3),
	STM32_TIM(TIM6_K, "tim6_k", "ck1_tim", RCC_APB1ENSETR, 4),
	STM32_TIM(TIM7_K, "tim7_k", "ck1_tim", RCC_APB1ENSETR, 5),
	STM32_TIM(TIM12_K, "tim12_k", "ck1_tim", RCC_APB1ENSETR, 6),
	STM32_TIM(TIM13_K, "tim13_k", "ck1_tim", RCC_APB1ENSETR, 7),
	STM32_TIM(TIM14_K, "tim14_k", "ck1_tim", RCC_APB1ENSETR, 8),
	STM32_TIM(TIM1_K, "tim1_k", "ck2_tim", RCC_APB2ENSETR, 0),
	STM32_TIM(TIM8_K, "tim8_k", "ck2_tim", RCC_APB2ENSETR, 1),
	STM32_TIM(TIM15_K, "tim15_k", "ck2_tim", RCC_APB2ENSETR, 2),
	STM32_TIM(TIM16_K, "tim16_k", "ck2_tim", RCC_APB2ENSETR, 3),
	STM32_TIM(TIM17_K, "tim17_k", "ck2_tim", RCC_APB2ENSETR, 4),
};

struct stm32_clock_match_data {
	const struct clock_config *cfg;
	unsigned int num;
	unsigned int maxbinding;
};

static struct stm32_clock_match_data stm32mp1_data = {
	.cfg		= stm32mp1_clock_cfg,
	.num		= ARRAY_SIZE(stm32mp1_clock_cfg),
	.maxbinding	= STM32MP1_LAST_CLK,
};

static const struct of_device_id stm32mp1_match_data[] = {
	{
		.compatible = "st,stm32mp1-rcc",
		.data = &stm32mp1_data,
	},
	{ }
};

static int stm32_register_hw_clk(struct device *dev,
				 struct clk_hw_onecell_data *clk_data,
				 void __iomem *base, spinlock_t *lock,
				 const struct clock_config *cfg)
{
	static struct clk_hw **hws;
	struct clk_hw *hw = ERR_PTR(-ENOENT);

	hws = clk_data->hws;

	if (cfg->func)
		hw = (*cfg->func)(dev, clk_data, base, lock, cfg);

	if (IS_ERR(hw)) {
		pr_err("Unable to register %s\n", cfg->name);
		return  PTR_ERR(hw);
	}

	if (cfg->id != NO_ID)
		hws[cfg->id] = hw;

	return 0;
}

static int stm32_rcc_init(struct device_node *np,
			  void __iomem *base,
			  const struct of_device_id *match_data)
{
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **hws;
	const struct of_device_id *match;
	const struct stm32_clock_match_data *data;
	int err, n, max_binding;

	match = of_match_node(match_data, np);
	if (!match) {
		pr_err("%s: match data not found\n", __func__);
		return -ENODEV;
	}

	data = match->data;

	max_binding =  data->maxbinding;

	clk_data = kzalloc(sizeof(*clk_data) +
				  sizeof(*clk_data->hws) * max_binding,
				  GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = max_binding;

	hws = clk_data->hws;

	for (n = 0; n < max_binding; n++)
		hws[n] = ERR_PTR(-ENOENT);

	for (n = 0; n < data->num; n++) {
		err = stm32_register_hw_clk(NULL, clk_data, base, &rlock,
					    &data->cfg[n]);
		if (err) {
			pr_err("%s: can't register  %s\n", __func__,
			       data->cfg[n].name);

			kfree(clk_data);

			return err;
		}
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

static void stm32mp1_rcc_init(struct device_node *np)
{
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: unable to map resource", np->name);
		of_node_put(np);
		return;
	}

	if (stm32_rcc_init(np, base, stm32mp1_match_data)) {
		iounmap(base);
		of_node_put(np);
	}
}

CLK_OF_DECLARE_DRIVER(stm32mp1_rcc, "st,stm32mp1-rcc", stm32mp1_rcc_init);
