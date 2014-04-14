#include <linux/slab.h>

#include "clk-ops.h"
#include "clk-pd.h"


static int clk_pd_endisable(struct clk_hw *hw, bool enable)
{
	struct clk_pd *pd = to_clk_pd(hw);
	unsigned long flags = 0;
	int ret;

	if (pd->lock)
		spin_lock_irqsave(pd->lock, flags);

	ret = rockchip_pmu_ops.set_power_domain(pd->id, enable);

	if (pd->lock)
		spin_unlock_irqrestore(pd->lock, flags);

	return ret;	
}

static int clk_pd_enable(struct clk_hw *hw)
{
	return clk_pd_endisable(hw, true);
}

static void clk_pd_disable(struct clk_hw *hw)
{
	clk_pd_endisable(hw, false);
}

static int clk_pd_is_enabled(struct clk_hw *hw)
{
	struct clk_pd *pd = to_clk_pd(hw);

	return rockchip_pmu_ops.power_domain_is_on(pd->id);
}

const struct clk_ops clk_pd_ops = {
	.enable = clk_pd_enable,
	.disable = clk_pd_disable,
	.is_enabled = clk_pd_is_enabled,
};

const struct clk_ops clk_pd_virt_ops = {

};


struct clk *rk_clk_register_pd(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags, 
		u32 pd_id, spinlock_t *lock)
{
	struct clk_pd *pd;
	struct clk *clk;
	struct clk_init_data init;


	/* allocate the pd */
	pd = kzalloc(sizeof(struct clk_pd), GFP_KERNEL);
	if (!pd) {
		clk_err("%s: could not allocate pd clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	if(pd_id == CLK_PD_VIRT)
		init.ops = &clk_pd_virt_ops;
	else
		init.ops = &clk_pd_ops;

	/* struct clk_pd assignments */
	pd->id= pd_id;
	pd->lock = lock;
	pd->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &pd->hw);

	if (IS_ERR(clk))
		kfree(pd);

	return clk;
}

