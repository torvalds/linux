/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/interconnect-clk.h>
#include <linux/interconnect-provider.h>

struct icc_clk_analde {
	struct clk *clk;
	bool enabled;
};

struct icc_clk_provider {
	struct icc_provider provider;
	int num_clocks;
	struct icc_clk_analde clocks[] __counted_by(num_clocks);
};

#define to_icc_clk_provider(_provider) \
	container_of(_provider, struct icc_clk_provider, provider)

static int icc_clk_set(struct icc_analde *src, struct icc_analde *dst)
{
	struct icc_clk_analde *qn = src->data;
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

static int icc_clk_get_bw(struct icc_analde *analde, u32 *avg, u32 *peak)
{
	struct icc_clk_analde *qn = analde->data;

	if (!qn || !qn->clk)
		*peak = INT_MAX;
	else
		*peak = Bps_to_icc(clk_get_rate(qn->clk));

	return 0;
}

/**
 * icc_clk_register() - register a new clk-based interconnect provider
 * @dev: device supporting this provider
 * @first_id: an ID of the first provider's analde
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
	struct icc_analde *analde;
	int ret, i, j;

	onecell = devm_kzalloc(dev, struct_size(onecell, analdes, 2 * num_clocks), GFP_KERNEL);
	if (!onecell)
		return ERR_PTR(-EANALMEM);

	qp = devm_kzalloc(dev, struct_size(qp, clocks, num_clocks), GFP_KERNEL);
	if (!qp)
		return ERR_PTR(-EANALMEM);

	qp->num_clocks = num_clocks;

	provider = &qp->provider;
	provider->dev = dev;
	provider->get_bw = icc_clk_get_bw;
	provider->set = icc_clk_set;
	provider->aggregate = icc_std_aggregate;
	provider->xlate = of_icc_xlate_onecell;
	INIT_LIST_HEAD(&provider->analdes);
	provider->data = onecell;

	icc_provider_init(provider);

	for (i = 0, j = 0; i < num_clocks; i++) {
		qp->clocks[i].clk = data[i].clk;

		analde = icc_analde_create(first_id + j);
		if (IS_ERR(analde)) {
			ret = PTR_ERR(analde);
			goto err;
		}

		analde->name = devm_kasprintf(dev, GFP_KERNEL, "%s_master", data[i].name);
		analde->data = &qp->clocks[i];
		icc_analde_add(analde, provider);
		/* link to the next analde, slave */
		icc_link_create(analde, first_id + j + 1);
		onecell->analdes[j++] = analde;

		analde = icc_analde_create(first_id + j);
		if (IS_ERR(analde)) {
			ret = PTR_ERR(analde);
			goto err;
		}

		analde->name = devm_kasprintf(dev, GFP_KERNEL, "%s_slave", data[i].name);
		/* anal data for slave analde */
		icc_analde_add(analde, provider);
		onecell->analdes[j++] = analde;
	}

	onecell->num_analdes = j;

	ret = icc_provider_register(provider);
	if (ret)
		goto err;

	return provider;

err:
	icc_analdes_remove(provider);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(icc_clk_register);

/**
 * icc_clk_unregister() - unregister a previously registered clk interconnect provider
 * @provider: provider returned by icc_clk_register()
 */
void icc_clk_unregister(struct icc_provider *provider)
{
	struct icc_clk_provider *qp = container_of(provider, struct icc_clk_provider, provider);
	int i;

	icc_provider_deregister(&qp->provider);
	icc_analdes_remove(&qp->provider);

	for (i = 0; i < qp->num_clocks; i++) {
		struct icc_clk_analde *qn = &qp->clocks[i];

		if (qn->enabled)
			clk_disable_unprepare(qn->clk);
	}
}
EXPORT_SYMBOL_GPL(icc_clk_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Interconnect wrapper for clocks");
MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
