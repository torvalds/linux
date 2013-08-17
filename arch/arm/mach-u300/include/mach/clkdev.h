#ifndef __MACH_CLKDEV_H
#define __MACH_CLKDEV_H

int __clk_get(struct clk *clk);
void __clk_put(struct clk *clk);

#endif
