#ifndef __ASM_MACH_CLKDEV_H
#define __ASM_MACH_CLKDEV_H

#include <linux/module.h>
#include <asm/hardware/icst.h>

struct clk {
	unsigned long		rate;
	struct module		*owner;
	const struct icst_params *params;
	void __iomem		*vcoreg;
	void			(*setvco)(struct clk *, struct icst_vco vco);
	void			*data;
};

static inline int __clk_get(struct clk *clk)
{
	return try_module_get(clk->owner);
}

static inline void __clk_put(struct clk *clk)
{
	module_put(clk->owner);
}

#endif
