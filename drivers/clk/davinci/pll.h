/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Clock driver for TI Davinci PSC controllers
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#ifndef __CLK_DAVINCI_PLL_H___
#define __CLK_DAVINCI_PLL_H___

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/types.h>

#define PLL_HAS_CLKMODE			BIT(0) /* PLL has PLLCTL[CLKMODE] */
#define PLL_HAS_PREDIV			BIT(1) /* has prediv before PLL */
#define PLL_PREDIV_ALWAYS_ENABLED	BIT(2) /* don't clear DEN bit */
#define PLL_PREDIV_FIXED_DIV		BIT(3) /* fixed divider value */
#define PLL_HAS_POSTDIV			BIT(4) /* has postdiv after PLL */
#define PLL_POSTDIV_ALWAYS_ENABLED	BIT(5) /* don't clear DEN bit */
#define PLL_POSTDIV_FIXED_DIV		BIT(6) /* fixed divider value */
#define PLL_HAS_EXTCLKSRC		BIT(7) /* has selectable bypass */
#define PLL_PLLM_2X			BIT(8) /* PLLM value is 2x (DM365) */
#define PLL_PREDIV_FIXED8		BIT(9) /* DM355 quirk */

/** davinci_pll_clk_info - controller-specific PLL info
 * @name: The name of the PLL
 * @unlock_reg: Option CFGCHIP register for unlocking PLL
 * @unlock_mask: Bitmask used with @unlock_reg
 * @pllm_mask: Bitmask for PLLM[PLLM] value
 * @pllm_min: Minimum allowable value for PLLM[PLLM]
 * @pllm_max: Maximum allowable value for PLLM[PLLM]
 * @pllout_min_rate: Minimum allowable rate for PLLOUT
 * @pllout_max_rate: Maximum allowable rate for PLLOUT
 * @flags: Bitmap of PLL_* flags.
 */
struct davinci_pll_clk_info {
	const char *name;
	u32 unlock_reg;
	u32 unlock_mask;
	u32 pllm_mask;
	u32 pllm_min;
	u32 pllm_max;
	unsigned long pllout_min_rate;
	unsigned long pllout_max_rate;
	u32 flags;
};

#define SYSCLK_ARM_RATE		BIT(0) /* Controls ARM rate */
#define SYSCLK_ALWAYS_ENABLED	BIT(1) /* Or bad things happen */
#define SYSCLK_FIXED_DIV	BIT(2) /* Fixed divider */

/** davinci_pll_sysclk_info - SYSCLKn-specific info
 * @name: The name of the clock
 * @parent_name: The name of the parent clock
 * @id: "n" in "SYSCLKn"
 * @ratio_width: Width (in bits) of RATIO in PLLDIVn register
 * @flags: Bitmap of SYSCLK_* flags.
 */
struct davinci_pll_sysclk_info {
	const char *name;
	const char *parent_name;
	u32 id;
	u32 ratio_width;
	u32 flags;
};

#define SYSCLK(i, n, p, w, f)				\
static const struct davinci_pll_sysclk_info n = {	\
	.name		= #n,				\
	.parent_name	= #p,				\
	.id		= (i),				\
	.ratio_width	= (w),				\
	.flags		= (f),				\
}

/** davinci_pll_obsclk_info - OBSCLK-specific info
 * @name: The name of the clock
 * @parent_names: Array of names of the parent clocks
 * @num_parents: Length of @parent_names
 * @table: Array of values to write to OCSEL[OCSRC] cooresponding to
 *         @parent_names
 * @ocsrc_mask: Bitmask for OCSEL[OCSRC]
 */
struct davinci_pll_obsclk_info {
	const char *name;
	const char * const *parent_names;
	u8 num_parents;
	u32 *table;
	u32 ocsrc_mask;
};

struct clk *davinci_pll_clk_register(struct device *dev,
				     const struct davinci_pll_clk_info *info,
				     const char *parent_name,
				     void __iomem *base,
				     struct regmap *cfgchip);
struct clk *davinci_pll_auxclk_register(struct device *dev,
					const char *name,
					void __iomem *base);
struct clk *davinci_pll_sysclkbp_clk_register(struct device *dev,
					      const char *name,
					      void __iomem *base);
struct clk *
davinci_pll_obsclk_register(struct device *dev,
			    const struct davinci_pll_obsclk_info *info,
			    void __iomem *base);
struct clk *
davinci_pll_sysclk_register(struct device *dev,
			    const struct davinci_pll_sysclk_info *info,
			    void __iomem *base);

int of_davinci_pll_init(struct device *dev, struct device_node *node,
			const struct davinci_pll_clk_info *info,
			const struct davinci_pll_obsclk_info *obsclk_info,
			const struct davinci_pll_sysclk_info **div_info,
			u8 max_sysclk_id,
			void __iomem *base,
			struct regmap *cfgchip);

/* Platform-specific callbacks */

#ifdef CONFIG_ARCH_DAVINCI_DA850
int da850_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
void of_da850_pll0_init(struct device_node *node);
int of_da850_pll1_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM355
int dm355_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM644x
int dm644x_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
#endif
#ifdef CONFIG_ARCH_DAVINCI_DM646x
int dm646x_pll2_init(struct device *dev, void __iomem *base, struct regmap *cfgchip);
#endif

#endif /* __CLK_DAVINCI_PLL_H___ */
