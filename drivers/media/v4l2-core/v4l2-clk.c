// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 clock service
 *
 * Copyright (C) 2012-2013, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#include <linux/atomic.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/v4l2-clk.h>
#include <media/v4l2-subdev.h>

static DEFINE_MUTEX(clk_lock);
static LIST_HEAD(clk_list);

static struct v4l2_clk *v4l2_clk_find(const char *dev_id)
{
	struct v4l2_clk *clk;

	list_for_each_entry(clk, &clk_list, list)
		if (!strcmp(dev_id, clk->dev_id))
			return clk;

	return ERR_PTR(-ENODEV);
}

struct v4l2_clk *v4l2_clk_get(struct device *dev, const char *id)
{
	struct v4l2_clk *clk;
	struct clk *ccf_clk = clk_get(dev, id);
	char clk_name[V4L2_CLK_NAME_SIZE];

	if (PTR_ERR(ccf_clk) == -EPROBE_DEFER)
		return ERR_PTR(-EPROBE_DEFER);

	if (!IS_ERR_OR_NULL(ccf_clk)) {
		clk = kzalloc(sizeof(*clk), GFP_KERNEL);
		if (!clk) {
			clk_put(ccf_clk);
			return ERR_PTR(-ENOMEM);
		}
		clk->clk = ccf_clk;

		return clk;
	}

	mutex_lock(&clk_lock);
	clk = v4l2_clk_find(dev_name(dev));

	/* if dev_name is not found, try use the OF name to find again  */
	if (PTR_ERR(clk) == -ENODEV && dev->of_node) {
		v4l2_clk_name_of(clk_name, sizeof(clk_name), dev->of_node);
		clk = v4l2_clk_find(clk_name);
	}

	if (!IS_ERR(clk))
		atomic_inc(&clk->use_count);
	mutex_unlock(&clk_lock);

	return clk;
}
EXPORT_SYMBOL(v4l2_clk_get);

void v4l2_clk_put(struct v4l2_clk *clk)
{
	struct v4l2_clk *tmp;

	if (IS_ERR(clk))
		return;

	if (clk->clk) {
		clk_put(clk->clk);
		kfree(clk);
		return;
	}

	mutex_lock(&clk_lock);

	list_for_each_entry(tmp, &clk_list, list)
		if (tmp == clk)
			atomic_dec(&clk->use_count);

	mutex_unlock(&clk_lock);
}
EXPORT_SYMBOL(v4l2_clk_put);

static int v4l2_clk_lock_driver(struct v4l2_clk *clk)
{
	struct v4l2_clk *tmp;
	int ret = -ENODEV;

	mutex_lock(&clk_lock);

	list_for_each_entry(tmp, &clk_list, list)
		if (tmp == clk) {
			ret = !try_module_get(clk->ops->owner);
			if (ret)
				ret = -EFAULT;
			break;
		}

	mutex_unlock(&clk_lock);

	return ret;
}

static void v4l2_clk_unlock_driver(struct v4l2_clk *clk)
{
	module_put(clk->ops->owner);
}

int v4l2_clk_enable(struct v4l2_clk *clk)
{
	int ret;

	if (clk->clk)
		return clk_prepare_enable(clk->clk);

	ret = v4l2_clk_lock_driver(clk);
	if (ret < 0)
		return ret;

	mutex_lock(&clk->lock);

	if (++clk->enable == 1 && clk->ops->enable) {
		ret = clk->ops->enable(clk);
		if (ret < 0)
			clk->enable--;
	}

	mutex_unlock(&clk->lock);

	return ret;
}
EXPORT_SYMBOL(v4l2_clk_enable);

/*
 * You might Oops if you try to disabled a disabled clock, because then the
 * driver isn't locked and could have been unloaded by now, so, don't do that
 */
void v4l2_clk_disable(struct v4l2_clk *clk)
{
	int enable;

	if (clk->clk)
		return clk_disable_unprepare(clk->clk);

	mutex_lock(&clk->lock);

	enable = --clk->enable;
	if (WARN(enable < 0, "Unbalanced %s() on %s!\n", __func__,
		 clk->dev_id))
		clk->enable++;
	else if (!enable && clk->ops->disable)
		clk->ops->disable(clk);

	mutex_unlock(&clk->lock);

	v4l2_clk_unlock_driver(clk);
}
EXPORT_SYMBOL(v4l2_clk_disable);

unsigned long v4l2_clk_get_rate(struct v4l2_clk *clk)
{
	int ret;

	if (clk->clk)
		return clk_get_rate(clk->clk);

	ret = v4l2_clk_lock_driver(clk);
	if (ret < 0)
		return ret;

	mutex_lock(&clk->lock);
	if (!clk->ops->get_rate)
		ret = -ENOSYS;
	else
		ret = clk->ops->get_rate(clk);
	mutex_unlock(&clk->lock);

	v4l2_clk_unlock_driver(clk);

	return ret;
}
EXPORT_SYMBOL(v4l2_clk_get_rate);

int v4l2_clk_set_rate(struct v4l2_clk *clk, unsigned long rate)
{
	int ret;

	if (clk->clk) {
		long r = clk_round_rate(clk->clk, rate);
		if (r < 0)
			return r;
		return clk_set_rate(clk->clk, r);
	}

	ret = v4l2_clk_lock_driver(clk);

	if (ret < 0)
		return ret;

	mutex_lock(&clk->lock);
	if (!clk->ops->set_rate)
		ret = -ENOSYS;
	else
		ret = clk->ops->set_rate(clk, rate);
	mutex_unlock(&clk->lock);

	v4l2_clk_unlock_driver(clk);

	return ret;
}
EXPORT_SYMBOL(v4l2_clk_set_rate);

struct v4l2_clk *v4l2_clk_register(const struct v4l2_clk_ops *ops,
				   const char *dev_id,
				   void *priv)
{
	struct v4l2_clk *clk;
	int ret;

	if (!ops || !dev_id)
		return ERR_PTR(-EINVAL);

	clk = kzalloc(sizeof(struct v4l2_clk), GFP_KERNEL);
	if (!clk)
		return ERR_PTR(-ENOMEM);

	clk->dev_id = kstrdup(dev_id, GFP_KERNEL);
	if (!clk->dev_id) {
		ret = -ENOMEM;
		goto ealloc;
	}
	clk->ops = ops;
	clk->priv = priv;
	atomic_set(&clk->use_count, 0);
	mutex_init(&clk->lock);

	mutex_lock(&clk_lock);
	if (!IS_ERR(v4l2_clk_find(dev_id))) {
		mutex_unlock(&clk_lock);
		ret = -EEXIST;
		goto eexist;
	}
	list_add_tail(&clk->list, &clk_list);
	mutex_unlock(&clk_lock);

	return clk;

eexist:
ealloc:
	kfree(clk->dev_id);
	kfree(clk);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(v4l2_clk_register);

void v4l2_clk_unregister(struct v4l2_clk *clk)
{
	if (WARN(atomic_read(&clk->use_count),
		 "%s(): Refusing to unregister ref-counted %s clock!\n",
		 __func__, clk->dev_id))
		return;

	mutex_lock(&clk_lock);
	list_del(&clk->list);
	mutex_unlock(&clk_lock);

	kfree(clk->dev_id);
	kfree(clk);
}
EXPORT_SYMBOL(v4l2_clk_unregister);

struct v4l2_clk_fixed {
	unsigned long rate;
	struct v4l2_clk_ops ops;
};

static unsigned long fixed_get_rate(struct v4l2_clk *clk)
{
	struct v4l2_clk_fixed *priv = clk->priv;
	return priv->rate;
}

struct v4l2_clk *__v4l2_clk_register_fixed(const char *dev_id,
				unsigned long rate, struct module *owner)
{
	struct v4l2_clk *clk;
	struct v4l2_clk_fixed *priv = kzalloc(sizeof(*priv), GFP_KERNEL);

	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->rate = rate;
	priv->ops.get_rate = fixed_get_rate;
	priv->ops.owner = owner;

	clk = v4l2_clk_register(&priv->ops, dev_id, priv);
	if (IS_ERR(clk))
		kfree(priv);

	return clk;
}
EXPORT_SYMBOL(__v4l2_clk_register_fixed);

void v4l2_clk_unregister_fixed(struct v4l2_clk *clk)
{
	kfree(clk->priv);
	v4l2_clk_unregister(clk);
}
EXPORT_SYMBOL(v4l2_clk_unregister_fixed);
