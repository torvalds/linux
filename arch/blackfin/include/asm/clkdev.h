#ifndef __ASM_CLKDEV__H_
#define __ASM_CLKDEV__H_

#include <linux/slab.h>

static inline struct clk_lookup_alloc *__clkdev_alloc(size_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

#ifndef CONFIG_COMMON_CLK
#define __clk_put(clk)
#define __clk_get(clk) ({ 1; })
#endif

#endif
