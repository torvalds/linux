/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-clk.h>
#include <linux/interconnect-provider.h>

struct icc_clk_node {
	struct clk *clk;
	bool enabled;
};

struct icc_clk_provider {
	struct icc_provider provider;
	int num_clocks;
	struct icc_clk_node clocks[] __counted_by(num_clocks);
};

#define to_icc_clk_provider(_provider) \
	container_of(_provider, struct icc_clk_provider, provider)

static int icc_clk_set(struct icc_node *src, struct icc_node *dst)
{
	struct icc_clk_node *qn = src->data;
	int ret;

	if (!qn || !qn->clk)
		return 0;

	if (!src->peak_bw) {
		if (qn->enabled)
			clk_disable_unprepare(qn->clk);
		qn->enabled = false;

		return 0;
	}

	if (!qn->enabled) {
		ret = clk_prepare_enable(qn->clk);
		if (ret)
			return ret;
		qn->enabled = true;
	}

	return clk_set_rate(qn->clk, icc_units_to_bps(src->peak_bw));
}

static int icc_clk_get_bw(struct icc_node *node, u32 *avg, u32 *peak)
{
	struct icc_clk_node *qn = node->data;

	if (!qn || !qn->clk)
		*peak = INT_MAX;
	else
		*peak = Bps_to_icc(clk_get_rate(qn->clk));

	return 0;
}

/**
 * icc_clk_register() - register a new clk-based interconnect provider
 * @dev: device supporting this provider
 * @first_id: an ID of the first provider's node
 * @num_clocks: number of instances of struct icc_clk_data
 * @data: data for the provider
 *
 * Registers and returns a clk-based interconnect provider. It is a simple
 * wrapper around COMMON_CLK framework, allowing other devices to vote on the
 * clock rate.
 *
 * Return: 0 on success, or an error code otherwise
 */
struct icc_provider *icc_clk_register(struct device *dev,
				      unsigned int first_id,
				      unsigned int num_clocks,
				      const struct icc_clk_data *data)
{
	struct icc_clk_provider *qp;
	struct icc_provider *provider;
	struct icc_onecell_data *onecell;
	struct icc_node *node;
	int ret, i, j;

	onecell = devm_kzalloc(dev, struct_size(onecell, nodes, 2 * num_clocks), GFP_KERNEL);
	if (!onecell)
		return ERR_PTR(-ENOMEM);
	onecell->num_nodes = 2 * num_clocks;

	qp = devm_kzalloc(dev, struct_size(qp, clocks, num_clocks), GFP_KERNEL);
	if (!qp)
		return ERR_PTR(-ENOMEM);

	qp->num_clocks = num_clocks;

	provider = &qp->provider;
	provider->dev = dev;
	provider->get_bw = icc_clk_get_bw;
	provider->set = icc_clk_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->nodes);
	provider->data = onecell;

	icc_provider_init(provider);

	for (i = 0, j = 0; i < num_clocks; i++) {
		qp->clocks[i].clk = data[i].clk;

		node = icc_node_create(first_id + data[i].master_id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = devm_kasprintf(dev, GFP_KERNEL, "%s_master", data[i].name);
		if (!node->name) {
			icc_node_destroy(node->id);
			ret = -ENOMEM;
			goto err;
		}

		node->data = &qp->clocks[i];
		icc_node_add(node, provider);
		/* link to the next node, slave */
		icc_link_create(node, first_id + data[i].slave_id);
		onecell->nodes[j++] = node;

		node = icc_node_create(first_id + data[i].slave_id);
		if (IS_ERR(node)) {
			ret = PTR_ERR(node);
			goto err;
		}

		node->name = devm_kasprintf(dev, GFP_KERNEL, "%s_slave", data[i].name);
		if (!node->name) {
			icc_node_destroy(node->id);
			ret = -ENOMEM;
			goto err;
		}

		/* no data for slave node */
		icc_node_add(node, provider);
		onecell->nodes[j++] = node;
	}

	ret = icc_provider_register(provider);
	if (ret)
		goto err;

	return provider;

err:
	icc_nodes_remove(provider);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(icc_clk_register);

static void devm_icc_release(void *res)
{
	icc_clk_unregister(res);
}

int devm_icc_clk_register(struct device *dev, unsigned int first_id,
			  unsigned int num_clocks, const struct icc_clk_data *data)
{
	struct icc_provider *prov;

	prov = icc_clk_register(dev, first_id, num_clocks, data);
	if (IS_ERR(prov))
		return PTR_ERR(prov);

	return devm_add_action_or_reset(dev, devm_icc_release, prov);
}
EXPORT_SYMBOL_GPL(devm_icc_clk_register);

/**
 * icc_clk_unregister() - unregister a previously registered clk interconnect provider
 * @provider: provider returned by icc_clk_register()
 */
void icc_clk_unregister(struct icc_provider *provider)
{
	struct icc_clk_provider *qp = container_of(provider, struct icc_clk_provider, provider);
	int i;

	icc_provider_deregister(&qp->provider);
	icc_nodes_remove(&qp->provider);

	for (i = 0; i < qp->num_clocks; i++) {
		struct icc_clk_node *qn = &qp->clocks[i];

		if (qn->enabled)
			clk_disable_unprepare(qn->clk);
	}
}
EXPORT_SYMBOL_GPL(icc_clk_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Interconnect wrapper for clocks");
MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
