#ifndef __RK_CLK_OPS_H
#define __RK_CLK_OPS_H
#include <dt-bindings/clock/rockchip,rk3188.h>
#include "../../../arch/arm/mach-rockchip/iomap.h"
#include "../../../arch/arm/mach-rockchip/grf.h"


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


#define cru_readl(offset)	readl(RK_CRU_VIRT + (offset))
#define cru_writel(v, o)	do {writel(v, RK_CRU_VIRT + (o)); dsb();} \
				while (0)
#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + (offset))

#endif /* __RK_CLKOPS_H */
