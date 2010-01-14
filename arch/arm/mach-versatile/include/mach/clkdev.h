#ifndef __ASM_MACH_CLKDEV_H
#define __ASM_MACH_CLKDEV_H

#include <asm/hardware/icst.h>

struct clk {
	unsigned long		rate;
	const struct icst_params *params;
	void __iomem		*vcoreg;
	void			(*setvco)(struct clk *, struct icst_vco vco);
};

#define __clk_get(clk) ({ 1; })
#define __clk_put(clk) do { } while (0)

#endif
