#ifndef __RK_CLK_PD_H
#define __RK_CLK_PD_H

#include <linux/clk-provider.h>
#include <linux/rockchip/pmu.h>



#define to_clk_pd(_hw) container_of(_hw, struct clk_pd, hw)

struct clk_pd {
	struct clk_hw	hw;
	u32 		id;
	spinlock_t	*lock;
};

struct clk *rk_clk_register_pd(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, 
		u32 pd_id, spinlock_t *lock);


#endif /* __RK_CLK_PD_H */
