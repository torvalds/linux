#ifndef __BACKPORT_LINUX_CLK_H
#define __BACKPORT_LINUX_CLK_H
#include_next <linux/clk.h>
#include <linux/version.h>

/*
 * commit 93abe8e4 - we only backport the non CONFIG_COMMON_CLK
 * case as the CONFIG_COMMON_CLK case requires arch support. By
 * using the backport_ namespace for older kernels we force usage
 * of these helpers and that's required given that 3.5 added some
 * of these helpers expecting a few exported symbols for the non
 * CONFIG_COMMON_CLK case. The 3.5 kernel is not supported as
 * per kernel.org so we don't send a fix upstream for that.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)

#ifndef CONFIG_COMMON_CLK

/*
 * Whoopsie!
 *
 * clk_enable() and clk_disable() have been left without
 * a nop export symbols when !CONFIG_COMMON_CLK since its
 * introduction on v2.6.16, but fixed until 3.6.
 */
#if 0
#define clk_enable LINUX_BACKPORT(clk_enable)
static inline int clk_enable(struct clk *clk)
{
	return 0;
}

#define clk_disable LINUX_BACKPORT(clk_disable)
static inline void clk_disable(struct clk *clk) {}
#endif


#define clk_get LINUX_BACKPORT(clk_get)
static inline struct clk *clk_get(struct device *dev, const char *id)
{
	return NULL;
}

#define devm_clk_get LINUX_BACKPORT(devm_clk_get)
static inline struct clk *devm_clk_get(struct device *dev, const char *id)
{
	return NULL;
}

#define clk_put LINUX_BACKPORT(clk_put)
static inline void clk_put(struct clk *clk) {}

#define devm_clk_put LINUX_BACKPORT(devm_clk_put)
static inline void devm_clk_put(struct device *dev, struct clk *clk) {}

#define clk_get_rate LINUX_BACKPORT(clk_get_rate)
static inline unsigned long clk_get_rate(struct clk *clk)
{
	return 0;
}

#define clk_set_rate LINUX_BACKPORT(clk_set_rate)
static inline int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

#define clk_round_rate LINUX_BACKPORT(clk_round_rate)
static inline long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

#define clk_set_parent LINUX_BACKPORT(clk_set_parent)
static inline int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return 0;
}

#define clk_get_parent LINUX_BACKPORT(clk_get_parent)
static inline struct clk *clk_get_parent(struct clk *clk)
{
	return NULL;
}
#endif /* CONFIG_COMMON_CLK */

#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0) */

#endif /* __LINUX_CLK_H */
