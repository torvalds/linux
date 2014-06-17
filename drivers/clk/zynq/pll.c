/*
 * Zynq PLL driver
 *
 *  Copyright (C) 2013 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <linux/clk/zynq.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>

/**
 * struct zynq_pll
 * @hw:		Handle between common and hardware-specific interfaces
 * @pll_ctrl:	PLL control register
 * @pll_status:	PLL status register
 * @lock:	Register lock
 * @lockbit:	Indicates the associated PLL_LOCKED bit in the PLL status
 *		register.
 */
struct zynq_pll {
	struct clk_hw	hw;
	void __iomem	*pll_ctrl;
	void __iomem	*pll_status;
	spinlock_t	*lock;
	u8		lockbit;
};
#define to_zynq_pll(_hw)	container_of(_hw, struct zynq_pll, hw)

/* Register bitfield defines */
#define PLLCTRL_FBDIV_MASK	0x7f000
#define PLLCTRL_FBDIV_SHIFT	12
#define PLLCTRL_BPQUAL_MASK	(1 << 3)
#define PLLCTRL_PWRDWN_MASK	2
#define PLLCTRL_PWRDWN_SHIFT	1
#define PLLCTRL_RESET_MASK	1
#define PLLCTRL_RESET_SHIFT	0

#define PLL_FBDIV_MIN	13
#define PLL_FBDIV_MAX	66

/**
 * zynq_pll_round_rate() - Round a clock frequency
 * @hw:		Handle between common and hardware-specific interfaces
 * @rate:	Desired clock frequency
 * @prate:	Clock frequency of parent clock
 * Returns frequency closest to @rate the hardware can generate.
 */
static long zynq_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	u32 fbdiv;

	fbdiv = DIV_ROUND_CLOSEST(rate, *prate);
	if (fbdiv < PLL_FBDIV_MIN)
		fbdiv = PLL_FBDIV_MIN;
	else if (fbdiv > PLL_FBDIV_MAX)
		fbdiv = PLL_FBDIV_MAX;

	return *prate * fbdiv;
}

/**
 * zynq_pll_recalc_rate() - Recalculate clock frequency
 * @hw:			Handle between common and hardware-specific interfaces
 * @parent_rate:	Clock frequency of parent clock
 * Returns current clock frequency.
 */
static unsigned long zynq_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct zynq_pll *clk = to_zynq_pll(hw);
	u32 fbdiv;

	/*
	 * makes probably sense to redundantly save fbdiv in the struct
	 * zynq_pll to save the IO access.
	 */
	fbdiv = (clk_readl(clk->pll_ctrl) & PLLCTRL_FBDIV_MASK) >>
			PLLCTRL_FBDIV_SHIFT;

	return parent_rate * fbdiv;
}

/**
 * zynq_pll_is_enabled - Check if a clock is enabled
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 1 if the clock is enabled, 0 otherwise.
 *
 * Not sure this is a good idea, but since disabled means bypassed for
 * this clock implementation we say we are always enabled.
 */
static int zynq_pll_is_enabled(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct zynq_pll *clk = to_zynq_pll(hw);

	spin_lock_irqsave(clk->lock, flags);

	reg = clk_readl(clk->pll_ctrl);

	spin_unlock_irqrestore(clk->lock, flags);

	return !(reg & (PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK));
}

/**
 * zynq_pll_enable - Enable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static int zynq_pll_enable(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct zynq_pll *clk = to_zynq_pll(hw);

	if (zynq_pll_is_enabled(hw))
		return 0;

	pr_info("PLL: enable\n");

	/* Power up PLL and wait for lock */
	spin_lock_irqsave(clk->lock, flags);

	reg = clk_readl(clk->pll_ctrl);
	reg &= ~(PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK);
	clk_writel(reg, clk->pll_ctrl);
	while (!(clk_readl(clk->pll_status) & (1 << clk->lockbit)))
		;

	spin_unlock_irqrestore(clk->lock, flags);

	return 0;
}

/**
 * zynq_pll_disable - Disable clock
 * @hw:		Handle between common and hardware-specific interfaces
 * Returns 0 on success
 */
static void zynq_pll_disable(struct clk_hw *hw)
{
	unsigned long flags = 0;
	u32 reg;
	struct zynq_pll *clk = to_zynq_pll(hw);

	if (!zynq_pll_is_enabled(hw))
		return;

	pr_info("PLL: shutdown\n");

	/* shut down PLL */
	spin_lock_irqsave(clk->lock, flags);

	reg = clk_readl(clk->pll_ctrl);
	reg |= PLLCTRL_RESET_MASK | PLLCTRL_PWRDWN_MASK;
	clk_writel(reg, clk->pll_ctrl);

	spin_unlock_irqrestore(clk->lock, flags);
}

static const struct clk_ops zynq_pll_ops = {
	.enable = zynq_pll_enable,
	.disable = zynq_pll_disable,
	.is_enabled = zynq_pll_is_enabled,
	.round_rate = zynq_pll_round_rate,
	.recalc_rate = zynq_pll_recalc_rate
};

/**
 * clk_register_zynq_pll() - Register PLL with the clock framework
 * @name	PLL name
 * @parent	Parent clock name
 * @pll_ctrl	Pointer to PLL control register
 * @pll_status	Pointer to PLL status register
 * @lock_index	Bit index to this PLL's lock status bit in @pll_status
 * @lock	Register lock
 * Returns handle to the registered clock.
 */
struct clk *clk_register_zynq_pll(const char *name, const char *parent,
		void __iomem *pll_ctrl, void __iomem *pll_status, u8 lock_index,
		spinlock_t *lock)
{
	struct zynq_pll *pll;
	struct clk *clk;
	u32 reg;
	const char *parent_arr[1] = {parent};
	unsigned long flags = 0;
	struct clk_init_data initd = {
		.name = name,
		.parent_names = parent_arr,
		.ops = &zynq_pll_ops,
		.num_parents = 1,
		.flags = 0
	};

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: Could not allocate Zynq PLL clk.\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	/* Populate the struct */
	pll->hw.init = &initd;
	pll->pll_ctrl = pll_ctrl;
	pll->pll_status = pll_status;
	pll->lockbit = lock_index;
	pll->lock = lock;

	spin_lock_irqsave(pll->lock, flags);

	reg = clk_readl(pll->pll_ctrl);
	reg &= ~PLLCTRL_BPQUAL_MASK;
	clk_writel(reg, pll->pll_ctrl);

	spin_unlock_irqrestore(pll->lock, flags);

	clk = clk_register(NULL, &pll->hw);
	if (WARN_ON(IS_ERR(clk)))
		goto free_pll;

	return clk;

free_pll:
	kfree(pll);

	return clk;
}
