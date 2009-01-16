#ifndef __ASM_MACH_CLKDEV_H
#define __ASM_MACH_CLKDEV_H

#include <linux/module.h>
#include <asm/hardware/icst525.h>

struct clk {
	unsigned long		rate;
	struct module		*owner;
	const struct icst525_params *params;
	void			*data;
	void			(*setvco)(struct clk *, struct icst525_vco vco);
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
