#ifndef MACH_TCC8K_COMMON_H
#define MACH_TCC8K_COMMON_H

#include <linux/platform_device.h>

extern struct platform_device tcc_nand_device;

struct clk;

extern void tcc_clocks_init(unsigned long xi_freq, unsigned long xti_freq);
extern void tcc8k_timer_init(struct clk *clock, void __iomem *base, int irq);
extern void tcc8k_init_irq(void);
extern void tcc8k_map_common_io(void);

#endif
