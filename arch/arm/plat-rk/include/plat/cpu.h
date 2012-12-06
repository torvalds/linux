#ifndef __PLAT_CPU_H
#define __PLAT_CPU_H

#include <mach/io.h>
#include <mach/gpio.h>
#include <linux/io.h>

#ifdef CONFIG_ARCH_RK2928
#define SOC_RK2928G     0x01
#define SOC_RK2928L     0x02
#define SOC_RK2926      0x00

static inline bool soc_is_rk2928g(void)
{
	return ((readl_relaxed(RK2928_GPIO3_BASE + 0x50) & 0x07) == SOC_RK2928G);
}

static inline bool soc_is_rk2928l(void)
{
	return ((readl_relaxed(RK2928_GPIO3_BASE + 0x50) & 0x07) == SOC_RK2928L);
}

static inline bool soc_is_rk2926(void)
{
	return ((readl_relaxed(RK2928_GPIO3_BASE + 0x50) & 0x07) == SOC_RK2926);
}
#else
static inline bool soc_is_rk2928g(void) { return false; }
static inline bool soc_is_rk2928l(void) { return false; }
static inline bool soc_is_rk2926(void) { return false; }
#endif

#ifdef CONFIG_ARCH_RK3066B
static inline bool soc_is_rk3066b(void)
{
	return (((readl_relaxed(RK30_GPIO1_BASE + GPIO_EXT_PORT) >> 22) & 3) == 0);
}

static inline bool soc_is_rk3108(void)
{
	return (((readl_relaxed(RK30_GPIO1_BASE + GPIO_EXT_PORT) >> 22) & 3) == 1);
}
#else
static inline bool soc_is_rk3066b(void) { return false; }
static inline bool soc_is_rk3108(void) { return false; }
#endif
static inline bool soc_is_rk3168(void) { return soc_is_rk3108(); }

#endif
