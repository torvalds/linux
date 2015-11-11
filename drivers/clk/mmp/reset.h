#ifndef __MACH_MMP_CLK_RESET_H
#define __MACH_MMP_CLK_RESET_H

#include <linux/reset-controller.h>

#define MMP_RESET_INVERT	1

struct mmp_clk_reset_cell {
	unsigned int clk_id;
	void __iomem *reg;
	u32 bits;
	unsigned int flags;
	spinlock_t *lock;
};

struct mmp_clk_reset_unit {
	struct reset_controller_dev rcdev;
	struct mmp_clk_reset_cell *cells;
};

#ifdef CONFIG_RESET_CONTROLLER
void mmp_clk_reset_register(struct device_node *np,
			struct mmp_clk_reset_cell *cells, int nr_resets);
#else
static inline void mmp_clk_reset_register(struct device_node *np,
			struct mmp_clk_reset_cell *cells, int nr_resets)
{
}
#endif

#endif
