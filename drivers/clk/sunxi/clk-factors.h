#ifndef __MACH_SUNXI_CLK_FACTORS_H
#define __MACH_SUNXI_CLK_FACTORS_H

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/spinlock.h>

#define SUNXI_FACTORS_NOT_APPLICABLE	(0)

#define SUNXI_FACTORS_MUX_MASK 0x3

struct clk_factors_config {
	u8 nshift;
	u8 nwidth;
	u8 kshift;
	u8 kwidth;
	u8 mshift;
	u8 mwidth;
	u8 pshift;
	u8 pwidth;
	u8 n_start;
};

struct factors_data {
	int enable;
	int mux;
	struct clk_factors_config *table;
	void (*getter) (u32 *rate, u32 parent_rate, u8 *n, u8 *k, u8 *m, u8 *p);
	const char *name;
};

struct clk_factors {
	struct clk_hw hw;
	void __iomem *reg;
	struct clk_factors_config *config;
	void (*get_factors) (u32 *rate, u32 parent, u8 *n, u8 *k, u8 *m, u8 *p);
	spinlock_t *lock;
};

struct clk * __init sunxi_factors_register(struct device_node *node,
					   const struct factors_data *data,
					   spinlock_t *lock);

#endif
