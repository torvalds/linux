/*
 * Author: Daniel Thompson <daniel.thompson@linaro.org>
 *
 * Inspired by clk-asm9260.c .
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

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_address.h>

#define STM32F4_RCC_PLLCFGR		0x04
#define STM32F4_RCC_CFGR		0x08
#define STM32F4_RCC_AHB1ENR		0x30
#define STM32F4_RCC_AHB2ENR		0x34
#define STM32F4_RCC_AHB3ENR		0x38
#define STM32F4_RCC_APB1ENR		0x40
#define STM32F4_RCC_APB2ENR		0x44

struct stm32f4_gate_data {
	u8	offset;
	u8	bit_idx;
	const char *name;
	const char *parent_name;
	unsigned long flags;
};

static const struct stm32f4_gate_data stm32f4_gates[] __initconst = {
	{ STM32F4_RCC_AHB1ENR,  0,	"gpioa",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  1,	"gpiob",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  2,	"gpioc",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  3,	"gpiod",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  4,	"gpioe",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  5,	"gpiof",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  6,	"gpiog",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  7,	"gpioh",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  8,	"gpioi",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR,  9,	"gpioj",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 10,	"gpiok",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 12,	"crc",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 18,	"bkpsra",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 20,	"ccmdatam",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 21,	"dma1",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 22,	"dma2",		"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 23,	"dma2d",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 25,	"ethmac",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 26,	"ethmactx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 27,	"ethmacrx",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 28,	"ethmacptp",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 29,	"otghs",	"ahb_div" },
	{ STM32F4_RCC_AHB1ENR, 30,	"otghsulpi",	"ahb_div" },

	{ STM32F4_RCC_AHB2ENR,  0,	"dcmi",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  4,	"cryp",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  5,	"hash",		"ahb_div" },
	{ STM32F4_RCC_AHB2ENR,  6,	"rng",		"pll48" },
	{ STM32F4_RCC_AHB2ENR,  7,	"otgfs",	"pll48" },

	{ STM32F4_RCC_AHB3ENR,  0,	"fmc",		"ahb_div",
		CLK_IGNORE_UNUSED },

	{ STM32F4_RCC_APB1ENR,  0,	"tim2",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  1,	"tim3",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  2,	"tim4",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  3,	"tim5",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  4,	"tim6",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  5,	"tim7",		"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  6,	"tim12",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  7,	"tim13",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR,  8,	"tim14",	"apb1_mul" },
	{ STM32F4_RCC_APB1ENR, 11,	"wwdg",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 14,	"spi2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 15,	"spi3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 17,	"uart2",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 18,	"uart3",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 19,	"uart4",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 20,	"uart5",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 21,	"i2c1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 22,	"i2c2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 23,	"i2c3",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 25,	"can1",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 26,	"can2",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 28,	"pwr",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 29,	"dac",		"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 30,	"uart7",	"apb1_div" },
	{ STM32F4_RCC_APB1ENR, 31,	"uart8",	"apb1_div" },

	{ STM32F4_RCC_APB2ENR,  0,	"tim1",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  1,	"tim8",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR,  4,	"usart1",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  5,	"usart6",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  8,	"adc1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR,  9,	"adc2",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 10,	"adc3",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 11,	"sdio",		"pll48" },
	{ STM32F4_RCC_APB2ENR, 12,	"spi1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 13,	"spi4",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 14,	"syscfg",	"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 16,	"tim9",		"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 17,	"tim10",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 18,	"tim11",	"apb2_mul" },
	{ STM32F4_RCC_APB2ENR, 20,	"spi5",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 21,	"spi6",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 22,	"sai1",		"apb2_div" },
	{ STM32F4_RCC_APB2ENR, 26,	"ltdc",		"apb2_div" },
};

/*
 * MAX_CLKS is the maximum value in the enumeration below plus the combined
 * hweight of stm32f42xx_gate_map (plus one).
 */
#define MAX_CLKS 74

enum { SYSTICK, FCLK };

/*
 * This bitmask tells us which bit offsets (0..192) on STM32F4[23]xxx
 * have gate bits associated with them. Its combined hweight is 71.
 */
static const u64 stm32f42xx_gate_map[] = { 0x000000f17ef417ffull,
					   0x0000000000000001ull,
					   0x04777f33f6fec9ffull };

static struct clk *clks[MAX_CLKS];
static DEFINE_SPINLOCK(stm32f4_clk_lock);
static void __iomem *base;

/*
 * "Multiplier" device for APBx clocks.
 *
 * The APBx dividers are power-of-two dividers and, if *not* running in 1:1
 * mode, they also tap out the one of the low order state bits to run the
 * timers. ST datasheets represent this feature as a (conditional) clock
 * multiplier.
 */
struct clk_apb_mul {
	struct clk_hw hw;
	u8 bit_idx;
};

#define to_clk_apb_mul(_hw) container_of(_hw, struct clk_apb_mul, hw)

static unsigned long clk_apb_mul_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct clk_apb_mul *am = to_clk_apb_mul(hw);

	if (readl(base + STM32F4_RCC_CFGR) & BIT(am->bit_idx))
		return parent_rate * 2;

	return parent_rate;
}

static long clk_apb_mul_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate)
{
	struct clk_apb_mul *am = to_clk_apb_mul(hw);
	unsigned long mult = 1;

	if (readl(base + STM32F4_RCC_CFGR) & BIT(am->bit_idx))
		mult = 2;

	if (__clk_get_flags(hw->clk) & CLK_SET_RATE_PARENT) {
		unsigned long best_parent = rate / mult;

		*prate =
		    __clk_round_rate(__clk_get_parent(hw->clk), best_parent);
	}

	return *prate * mult;
}

static int clk_apb_mul_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	/*
	 * We must report success but we can do so unconditionally because
	 * clk_apb_mul_round_rate returns values that ensure this call is a
	 * nop.
	 */

	return 0;
}

static const struct clk_ops clk_apb_mul_factor_ops = {
	.round_rate = clk_apb_mul_round_rate,
	.set_rate = clk_apb_mul_set_rate,
	.recalc_rate = clk_apb_mul_recalc_rate,
};

static struct clk *clk_register_apb_mul(struct device *dev, const char *name,
					const char *parent_name,
					unsigned long flags, u8 bit_idx)
{
	struct clk_apb_mul *am;
	struct clk_init_data init;
	struct clk *clk;

	am = kzalloc(sizeof(*am), GFP_KERNEL);
	if (!am)
		return ERR_PTR(-ENOMEM);

	am->bit_idx = bit_idx;
	am->hw.init = &init;

	init.name = name;
	init.ops = &clk_apb_mul_factor_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk = clk_register(dev, &am->hw);

	if (IS_ERR(clk))
		kfree(am);

	return clk;
}

/*
 * Decode current PLL state and (statically) model the state we inherit from
 * the bootloader.
 */
static void stm32f4_rcc_register_pll(const char *hse_clk, const char *hsi_clk)
{
	unsigned long pllcfgr = readl(base + STM32F4_RCC_PLLCFGR);

	unsigned long pllm   = pllcfgr & 0x3f;
	unsigned long plln   = (pllcfgr >> 6) & 0x1ff;
	unsigned long pllp   = BIT(((pllcfgr >> 16) & 3) + 1);
	const char   *pllsrc = pllcfgr & BIT(22) ? hse_clk : hsi_clk;
	unsigned long pllq   = (pllcfgr >> 24) & 0xf;

	clk_register_fixed_factor(NULL, "vco", pllsrc, 0, plln, pllm);
	clk_register_fixed_factor(NULL, "pll", "vco", 0, 1, pllp);
	clk_register_fixed_factor(NULL, "pll48", "vco", 0, 1, pllq);
}

/*
 * Converts the primary and secondary indices (as they appear in DT) to an
 * offset into our struct clock array.
 */
static int stm32f4_rcc_lookup_clk_idx(u8 primary, u8 secondary)
{
	u64 table[ARRAY_SIZE(stm32f42xx_gate_map)];

	if (primary == 1) {
		if (WARN_ON(secondary > FCLK))
			return -EINVAL;
		return secondary;
	}

	memcpy(table, stm32f42xx_gate_map, sizeof(table));

	/* only bits set in table can be used as indices */
	if (WARN_ON(secondary >= BITS_PER_BYTE * sizeof(table) ||
		    0 == (table[BIT_ULL_WORD(secondary)] &
			  BIT_ULL_MASK(secondary))))
		return -EINVAL;

	/* mask out bits above our current index */
	table[BIT_ULL_WORD(secondary)] &=
	    GENMASK_ULL(secondary % BITS_PER_LONG_LONG, 0);

	return FCLK + hweight64(table[0]) +
	       (BIT_ULL_WORD(secondary) >= 1 ? hweight64(table[1]) : 0) +
	       (BIT_ULL_WORD(secondary) >= 2 ? hweight64(table[2]) : 0);
}

static struct clk *
stm32f4_rcc_lookup_clk(struct of_phandle_args *clkspec, void *data)
{
	int i = stm32f4_rcc_lookup_clk_idx(clkspec->args[0], clkspec->args[1]);

	if (i < 0)
		return ERR_PTR(-EINVAL);

	return clks[i];
}

static const char *sys_parents[] __initdata =   { "hsi", NULL, "pll" };

static const struct clk_div_table ahb_div_table[] = {
	{ 0x0,   1 }, { 0x1,   1 }, { 0x2,   1 }, { 0x3,   1 },
	{ 0x4,   1 }, { 0x5,   1 }, { 0x6,   1 }, { 0x7,   1 },
	{ 0x8,   2 }, { 0x9,   4 }, { 0xa,   8 }, { 0xb,  16 },
	{ 0xc,  64 }, { 0xd, 128 }, { 0xe, 256 }, { 0xf, 512 },
	{ 0 },
};

static const struct clk_div_table apb_div_table[] = {
	{ 0,  1 }, { 0,  1 }, { 0,  1 }, { 0,  1 },
	{ 4,  2 }, { 5,  4 }, { 6,  8 }, { 7, 16 },
	{ 0 },
};

static void __init stm32f4_rcc_init(struct device_node *np)
{
	const char *hse_clk;
	int n;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: unable to map resource", np->name);
		return;
	}

	hse_clk = of_clk_get_parent_name(np, 0);

	clk_register_fixed_rate_with_accuracy(NULL, "hsi", NULL, 0,
			16000000, 160000);
	stm32f4_rcc_register_pll(hse_clk, "hsi");

	sys_parents[1] = hse_clk;
	clk_register_mux_table(
	    NULL, "sys", sys_parents, ARRAY_SIZE(sys_parents), 0,
	    base + STM32F4_RCC_CFGR, 0, 3, 0, NULL, &stm32f4_clk_lock);

	clk_register_divider_table(NULL, "ahb_div", "sys",
				   CLK_SET_RATE_PARENT, base + STM32F4_RCC_CFGR,
				   4, 4, 0, ahb_div_table, &stm32f4_clk_lock);

	clk_register_divider_table(NULL, "apb1_div", "ahb_div",
				   CLK_SET_RATE_PARENT, base + STM32F4_RCC_CFGR,
				   10, 3, 0, apb_div_table, &stm32f4_clk_lock);
	clk_register_apb_mul(NULL, "apb1_mul", "apb1_div",
			     CLK_SET_RATE_PARENT, 12);

	clk_register_divider_table(NULL, "apb2_div", "ahb_div",
				   CLK_SET_RATE_PARENT, base + STM32F4_RCC_CFGR,
				   13, 3, 0, apb_div_table, &stm32f4_clk_lock);
	clk_register_apb_mul(NULL, "apb2_mul", "apb2_div",
			     CLK_SET_RATE_PARENT, 15);

	clks[SYSTICK] = clk_register_fixed_factor(NULL, "systick", "ahb_div",
						  0, 1, 8);
	clks[FCLK] = clk_register_fixed_factor(NULL, "fclk", "ahb_div",
					       0, 1, 1);

	for (n = 0; n < ARRAY_SIZE(stm32f4_gates); n++) {
		const struct stm32f4_gate_data *gd = &stm32f4_gates[n];
		unsigned int secondary =
		    8 * (gd->offset - STM32F4_RCC_AHB1ENR) + gd->bit_idx;
		int idx = stm32f4_rcc_lookup_clk_idx(0, secondary);

		if (idx < 0)
			goto fail;

		clks[idx] = clk_register_gate(
		    NULL, gd->name, gd->parent_name, gd->flags,
		    base + gd->offset, gd->bit_idx, 0, &stm32f4_clk_lock);

		if (IS_ERR(clks[n])) {
			pr_err("%s: Unable to register leaf clock %s\n",
			       np->full_name, gd->name);
			goto fail;
		}
	}

	of_clk_add_provider(np, stm32f4_rcc_lookup_clk, NULL);
	return;
fail:
	iounmap(base);
}
CLK_OF_DECLARE(stm32f4_rcc, "st,stm32f42xx-rcc", stm32f4_rcc_init);
