/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/gfp.h>

static void devm_clk_release(struct device *dev, void *res)
{
	clk_put(*(struct clk **)res);
}

struct clk *devm_clk_get(struct device *dev, const char *id)
{
	struct clk **ptr, *clk;

	ptr = devres_alloc(devm_clk_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	clk = clk_get(dev, id);
	if (!IS_ERR(clk)) {
		*ptr = clk;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return clk;
}
EXPORT_SYMBOL(devm_clk_get);

static int devm_clk_match(struct device *dev, void *res, void *data)
{
	struct clk **c = res;
	if (!c || !*c) {
		WARN_ON(!c || !*c);
		return 0;
	}
	return *c == data;
}

void devm_clk_put(struct device *dev, struct clk *clk)
{
	int ret;

	ret = devres_destroy(dev, devm_clk_release, devm_clk_match, clk);

	WARN_ON(ret);
}
EXPORT_SYMBOL(devm_clk_put);
