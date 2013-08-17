#ifndef __ASM_MACH_CLKDEV_H
#define __ASM_MACH_CLKDEV_H

int __clk_get(struct clk *clk);
void __clk_put(struct clk *clk);

#endif /* __ASM_MACH_CLKDEV_H */
