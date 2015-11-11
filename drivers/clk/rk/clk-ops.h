#ifndef __RK_CLK_OPS_H
#define __RK_CLK_OPS_H

#include <dt-bindings/clock/rockchip,rk3188.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>

#define MHZ			(1000UL * 1000UL)
#define KHZ			(1000UL)

struct clk_ops_table {
	unsigned int 		index;
	const struct clk_ops	*clk_ops;
};
const struct clk_ops *rk_get_clkops(unsigned int idx);

//#define RKCLK_DEBUG
//#define RKCLK_TEST

#if defined(RKCLK_DEBUG)
#define clk_debug(fmt, args...) printk(KERN_INFO "rkclk: "fmt, ##args)
#else
#define clk_debug(fmt, args...) do {} while(0)
#endif

#define clk_err(fmt, args...) printk(KERN_ERR "rkclk: "fmt, ##args)

u32 cru_readl(u32 offset);
void cru_writel(u32 val, u32 offset);

u32 grf_readl(u32 offset);

#endif /* __RK_CLKOPS_H */
