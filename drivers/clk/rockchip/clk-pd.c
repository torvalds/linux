#include <linux/slab.h>

#include "clk-ops.h"
#include "clk-pd.h"


static LIST_HEAD(clk_pd_notifier_list);

static int __clk_pd_notify(struct clk *clk, unsigned long msg)
{
	struct clk_pd_notifier *cn;
	int ret = NOTIFY_DONE;

	list_for_each_entry(cn, &clk_pd_notifier_list, node) {
		if (cn->clk == clk) {
			ret = srcu_notifier_call_chain(&cn->notifier_head, msg,
					NULL);
			break;
		}
	}

	return ret;
}

int rk_clk_pd_notifier_register(struct clk *clk, struct notifier_block *nb)
{
	struct clk_pd_notifier *cn;
	int ret = -ENOMEM;

	if (!clk || !nb)
		return -EINVAL;

	//clk_prepare_lock();

	/* search the list of notifiers for this clk */
	list_for_each_entry(cn, &clk_pd_notifier_list, node)
		if (cn->clk == clk)
			break;

	/* if clk wasn't in the notifier list, allocate new clk_notifier */
	if (cn->clk != clk) {
		cn = kzalloc(sizeof(struct clk_pd_notifier), GFP_KERNEL);
		if (!cn)
			goto out;

		cn->clk = clk;
		srcu_init_notifier_head(&cn->notifier_head);

		list_add(&cn->node, &clk_pd_notifier_list);
	}

	ret = srcu_notifier_chain_register(&cn->notifier_head, nb);

	//clk->notifier_count++;

out:
	//clk_prepare_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rk_clk_pd_notifier_register);

int rk_clk_pd_notifier_unregister(struct clk *clk, struct notifier_block *nb)
{
	struct clk_pd_notifier *cn = NULL;
	int ret = -EINVAL;

	if (!clk || !nb)
		return -EINVAL;

	//clk_prepare_lock();

	list_for_each_entry(cn, &clk_pd_notifier_list, node)
		if (cn->clk == clk)
			break;

	if (cn->clk == clk) {
		ret = srcu_notifier_chain_unregister(&cn->notifier_head, nb);

		//clk->notifier_count--;

		/* XXX the notifier code should handle this better */
		if (!cn->notifier_head.head) {
			srcu_cleanup_notifier_head(&cn->notifier_head);
			list_del(&cn->node);
			kfree(cn);
		}

	} else {
		ret = -ENOENT;
	}

	//clk_prepare_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rk_clk_pd_notifier_unregister);

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
	int ret = 0;

	__clk_pd_notify(hw->clk, RK_CLK_PD_PRE_ENABLE);

	ret = clk_pd_endisable(hw, true);

	__clk_pd_notify(hw->clk, RK_CLK_PD_POST_ENABLE);

	return ret;
}

static void clk_pd_disable(struct clk_hw *hw)
{
	__clk_pd_notify(hw->clk, RK_CLK_PD_PRE_DISABLE);

	clk_pd_endisable(hw, false);

	__clk_pd_notify(hw->clk, RK_CLK_PD_POST_DISABLE);
}

static int clk_pd_is_enabled(struct clk_hw *hw)
{
	struct clk_pd *pd = to_clk_pd(hw);

	return rockchip_pmu_ops.power_domain_is_on(pd->id);
}

static int clk_pd_prepare(struct clk_hw *hw)
{
	__clk_pd_notify(hw->clk, RK_CLK_PD_PREPARE);

	return 0;
}

static void clk_pd_unprepare(struct clk_hw *hw)
{
	__clk_pd_notify(hw->clk, RK_CLK_PD_UNPREPARE);
}

const struct clk_ops clk_pd_ops = {
	.prepare = clk_pd_prepare,
	.unprepare = clk_pd_unprepare,
	.enable = clk_pd_enable,
	.disable = clk_pd_disable,
	.is_enabled = clk_pd_is_enabled,
};

static int clk_pd_virt_enable(struct clk_hw *hw)
{
	__clk_pd_notify(hw->clk, RK_CLK_PD_PRE_ENABLE);

	__clk_pd_notify(hw->clk, RK_CLK_PD_POST_ENABLE);

	return 0;
}

static void clk_pd_virt_disable(struct clk_hw *hw)
{
	__clk_pd_notify(hw->clk, RK_CLK_PD_PRE_DISABLE);

	__clk_pd_notify(hw->clk, RK_CLK_PD_POST_DISABLE);
}

const struct clk_ops clk_pd_virt_ops = {
	.prepare = clk_pd_prepare,
	.unprepare = clk_pd_unprepare,
	.enable = clk_pd_virt_enable,
	.disable = clk_pd_virt_disable,
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

