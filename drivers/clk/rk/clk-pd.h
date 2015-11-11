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


#define RK_CLK_PD_PRE_ENABLE			BIT(0)
#define RK_CLK_PD_POST_ENABLE			BIT(1)
#define RK_CLK_PD_PRE_DISABLE			BIT(2)
#define RK_CLK_PD_POST_DISABLE			BIT(3)
#define RK_CLK_PD_PREPARE			BIT(4)
#define RK_CLK_PD_UNPREPARE			BIT(5)


struct clk_pd_notifier {
	struct clk			*clk;
	struct srcu_notifier_head	notifier_head;
	struct list_head		node;
};

int rk_clk_pd_notifier_register(struct clk *clk, struct notifier_block *nb);

int rk_clk_pd_notifier_unregister(struct clk *clk, struct notifier_block *nb);

#endif /* __RK_CLK_PD_H */
