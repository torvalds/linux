#ifndef __MACH_CPU_H
#define __MACH_CPU_H

#include <plat/cpu.h>

static inline void soc_gpio_init(void)
{
        writel_relaxed(readl_relaxed(RK2928_GPIO3_BASE + 0x04) & (~0x07), RK2928_GPIO3_BASE + 0x04);
}

#endif
