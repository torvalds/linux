#ifndef __MACH_CPU_H
#define __MACH_CPU_H

#include <mach/io.h>
#include <linux/io.h>

#define SOC_RK2928G     0x01
#define SOC_RK2928L     0x02
#define SOC_RK2926      0x00

static inline void soc_gpio_init(void)
{
        writel_relaxed(readl_relaxed(RK2928_GPIO3_BASE + 0x04) & (~0x07), RK2928_GPIO3_BASE + 0x04);
}
static inline int soc_is_rk2928g(void)
{
        return ((readl_relaxed(RK2928_GPIO3_BASE + 0x50) & 0x07) == SOC_RK2928G);
}
static inline int soc_is_rk2928l(void)
{
        return ((readl_relaxed(RK2928_GPIO3_BASE + 0x50) & 0x07) == SOC_RK2928L);
}
static inline int soc_is_rk2926(void)
{
        return ((readl_relaxed(RK2928_GPIO3_BASE + 0x50) & 0x07) == SOC_RK2926);
}
        
#endif
