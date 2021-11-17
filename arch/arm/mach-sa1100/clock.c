// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/arm/mach-sa1100/clock.c
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include <mach/hardware.h>
#include <mach/generic.h>

static const char * const clk_tucr_parents[] = {
	"clk32768", "clk3686400",
};

static DEFINE_SPINLOCK(tucr_lock);

static int clk_gpio27_enable(struct clk_hw *hw)
{
	unsigned long flags;

	/*
	 * First, set up the 3.6864MHz clock on GPIO 27 for the SA-1111:
	 * (SA-1110 Developer's Manual, section 9.1.2.1)
	 */
	local_irq_save(flags);
	GAFR |= GPIO_32_768kHz;
	GPDR |= GPIO_32_768kHz;
	local_irq_restore(flags);

	return 0;
}

static void clk_gpio27_disable(struct clk_hw *hw)
{
	unsigned long flags;

	local_irq_save(flags);
	GPDR &= ~GPIO_32_768kHz;
	GAFR &= ~GPIO_32_768kHz;
	local_irq_restore(flags);
}

static const struct clk_ops clk_gpio27_ops = {
	.enable = clk_gpio27_enable,
	.disable = clk_gpio27_disable,
};

static const char * const clk_gpio27_parents[] = {
	"tucr-mux",
};

static const struct clk_init_data clk_gpio27_init_data __initconst = {
	.name = "gpio27",
	.ops = &clk_gpio27_ops,
	.parent_names = clk_gpio27_parents,
	.num_parents = ARRAY_SIZE(clk_gpio27_parents),
};

/*
 * Derived from the table 8-1 in the SA1110 manual, the MPLL appears to
 * multiply its input rate by 4 x (4 + PPCR).  This calculation gives
 * the exact rate.  The figures given in the table are the rates rounded
 * to 100kHz.  Stick with sa11x0_getspeed() for the time being.
 */
static unsigned long clk_mpll_recalc_rate(struct clk_hw *hw,
	unsigned long prate)
{
	return sa11x0_getspeed(0) * 1000;
}

static const struct clk_ops clk_mpll_ops = {
	.recalc_rate = clk_mpll_recalc_rate,
};

static const char * const clk_mpll_parents[] = {
	"clk3686400",
};

static const struct clk_init_data clk_mpll_init_data __initconst = {
	.name = "mpll",
	.ops = &clk_mpll_ops,
	.parent_names = clk_mpll_parents,
	.num_parents = ARRAY_SIZE(clk_mpll_parents),
	.flags = CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL,
};

int __init sa11xx_clk_init(void)
{
	struct clk_hw *hw;
	int ret;

	hw = clk_hw_register_fixed_rate(NULL, "clk32768", NULL, 0, 32768);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	clk_hw_register_clkdev(hw, NULL, "sa1100-rtc");

	hw = clk_hw_register_fixed_rate(NULL, "clk3686400", NULL, 0, 3686400);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	clk_hw_register_clkdev(hw, "OSTIMER0", NULL);

	hw = kzalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;
	hw->init = &clk_mpll_init_data;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(hw);
		return ret;
	}

	clk_hw_register_clkdev(hw, NULL, "sa11x0-fb");
	clk_hw_register_clkdev(hw, NULL, "sa11x0-pcmcia");
	clk_hw_register_clkdev(hw, NULL, "sa11x0-pcmcia.0");
	clk_hw_register_clkdev(hw, NULL, "sa11x0-pcmcia.1");
	clk_hw_register_clkdev(hw, NULL, "1800");

	hw = clk_hw_register_mux(NULL, "tucr-mux", clk_tucr_parents,
				 ARRAY_SIZE(clk_tucr_parents), 0,
				 (void __iomem *)&TUCR, FShft(TUCR_TSEL),
				 FAlnMsk(TUCR_TSEL), 0, &tucr_lock);
	clk_set_rate(hw->clk, 3686400);

	hw = kzalloc(sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;
	hw->init = &clk_gpio27_init_data;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(hw);
		return ret;
	}

	clk_hw_register_clkdev(hw, NULL, "sa1111.0");

	return 0;
}
