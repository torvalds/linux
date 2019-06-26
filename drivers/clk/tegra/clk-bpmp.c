/*
 * Copyright (C) 2016 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/seq_buf.h>
#include <linux/slab.h>

#include <soc/tegra/bpmp.h>
#include <soc/tegra/bpmp-abi.h>

#define TEGRA_BPMP_DUMP_CLOCK_INFO	0

#define TEGRA_BPMP_CLK_HAS_MUX		BIT(0)
#define TEGRA_BPMP_CLK_HAS_SET_RATE	BIT(1)
#define TEGRA_BPMP_CLK_IS_ROOT		BIT(2)

struct tegra_bpmp_clk_info {
	unsigned int id;
	char name[MRQ_CLK_NAME_MAXLEN];
	unsigned int parents[MRQ_CLK_MAX_PARENTS];
	unsigned int num_parents;
	unsigned long flags;
};

struct tegra_bpmp_clk {
	struct clk_hw hw;

	struct tegra_bpmp *bpmp;
	unsigned int id;

	unsigned int num_parents;
	unsigned int *parents;
};

static inline struct tegra_bpmp_clk *to_tegra_bpmp_clk(struct clk_hw *hw)
{
	return container_of(hw, struct tegra_bpmp_clk, hw);
}

struct tegra_bpmp_clk_message {
	unsigned int cmd;
	unsigned int id;

	struct {
		const void *data;
		size_t size;
	} tx;

	struct {
		void *data;
		size_t size;
		int ret;
	} rx;
};

static int tegra_bpmp_clk_transfer(struct tegra_bpmp *bpmp,
				   const struct tegra_bpmp_clk_message *clk)
{
	struct mrq_clk_request request;
	struct tegra_bpmp_message msg;
	void *req = &request;
	int err;

	memset(&request, 0, sizeof(request));
	request.cmd_and_id = (clk->cmd << 24) | clk->id;

	/*
	 * The mrq_clk_request structure has an anonymous union at offset 4
	 * that contains all possible sub-command structures. Copy the data
	 * to that union. Ideally we'd be able to refer to it by name, but
	 * doing so would require changing the ABI header and increase the
	 * maintenance burden.
	 */
	memcpy(req + 4, clk->tx.data, clk->tx.size);

	memset(&msg, 0, sizeof(msg));
	msg.mrq = MRQ_CLK;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = clk->rx.data;
	msg.rx.size = clk->rx.size;

	err = tegra_bpmp_transfer(bpmp, &msg);
	if (err < 0)
		return err;
	else if (msg.rx.ret < 0)
		return -EINVAL;

	return 0;
}

static int tegra_bpmp_clk_prepare(struct clk_hw *hw)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct tegra_bpmp_clk_message msg;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_ENABLE;
	msg.id = clk->id;

	return tegra_bpmp_clk_transfer(clk->bpmp, &msg);
}

static void tegra_bpmp_clk_unprepare(struct clk_hw *hw)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct tegra_bpmp_clk_message msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_DISABLE;
	msg.id = clk->id;

	err = tegra_bpmp_clk_transfer(clk->bpmp, &msg);
	if (err < 0)
		dev_err(clk->bpmp->dev, "failed to disable clock %s: %d\n",
			clk_hw_get_name(hw), err);
}

static int tegra_bpmp_clk_is_prepared(struct clk_hw *hw)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct cmd_clk_is_enabled_response response;
	struct tegra_bpmp_clk_message msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_IS_ENABLED;
	msg.id = clk->id;
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(clk->bpmp, &msg);
	if (err < 0)
		return err;

	return response.state;
}

static unsigned long tegra_bpmp_clk_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct cmd_clk_get_rate_response response;
	struct cmd_clk_get_rate_request request;
	struct tegra_bpmp_clk_message msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_GET_RATE;
	msg.id = clk->id;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(clk->bpmp, &msg);
	if (err < 0)
		return err;

	return response.rate;
}

static long tegra_bpmp_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct cmd_clk_round_rate_response response;
	struct cmd_clk_round_rate_request request;
	struct tegra_bpmp_clk_message msg;
	int err;

	memset(&request, 0, sizeof(request));
	request.rate = rate;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_ROUND_RATE;
	msg.id = clk->id;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(clk->bpmp, &msg);
	if (err < 0)
		return err;

	return response.rate;
}

static int tegra_bpmp_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct cmd_clk_set_parent_response response;
	struct cmd_clk_set_parent_request request;
	struct tegra_bpmp_clk_message msg;
	int err;

	memset(&request, 0, sizeof(request));
	request.parent_id = clk->parents[index];

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_SET_PARENT;
	msg.id = clk->id;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(clk->bpmp, &msg);
	if (err < 0)
		return err;

	/* XXX check parent ID in response */

	return 0;
}

static u8 tegra_bpmp_clk_get_parent(struct clk_hw *hw)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct cmd_clk_get_parent_response response;
	struct tegra_bpmp_clk_message msg;
	unsigned int i;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_GET_PARENT;
	msg.id = clk->id;
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(clk->bpmp, &msg);
	if (err < 0) {
		dev_err(clk->bpmp->dev, "failed to get parent for %s: %d\n",
			clk_hw_get_name(hw), err);
		return U8_MAX;
	}

	for (i = 0; i < clk->num_parents; i++)
		if (clk->parents[i] == response.parent_id)
			return i;

	return U8_MAX;
}

static int tegra_bpmp_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct tegra_bpmp_clk *clk = to_tegra_bpmp_clk(hw);
	struct cmd_clk_set_rate_response response;
	struct cmd_clk_set_rate_request request;
	struct tegra_bpmp_clk_message msg;

	memset(&request, 0, sizeof(request));
	request.rate = rate;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_SET_RATE;
	msg.id = clk->id;
	msg.tx.data = &request;
	msg.tx.size = sizeof(request);
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	return tegra_bpmp_clk_transfer(clk->bpmp, &msg);
}

static const struct clk_ops tegra_bpmp_clk_gate_ops = {
	.prepare = tegra_bpmp_clk_prepare,
	.unprepare = tegra_bpmp_clk_unprepare,
	.is_prepared = tegra_bpmp_clk_is_prepared,
	.recalc_rate = tegra_bpmp_clk_recalc_rate,
};

static const struct clk_ops tegra_bpmp_clk_mux_ops = {
	.prepare = tegra_bpmp_clk_prepare,
	.unprepare = tegra_bpmp_clk_unprepare,
	.is_prepared = tegra_bpmp_clk_is_prepared,
	.recalc_rate = tegra_bpmp_clk_recalc_rate,
	.set_parent = tegra_bpmp_clk_set_parent,
	.get_parent = tegra_bpmp_clk_get_parent,
};

static const struct clk_ops tegra_bpmp_clk_rate_ops = {
	.prepare = tegra_bpmp_clk_prepare,
	.unprepare = tegra_bpmp_clk_unprepare,
	.is_prepared = tegra_bpmp_clk_is_prepared,
	.recalc_rate = tegra_bpmp_clk_recalc_rate,
	.round_rate = tegra_bpmp_clk_round_rate,
	.set_rate = tegra_bpmp_clk_set_rate,
};

static const struct clk_ops tegra_bpmp_clk_mux_rate_ops = {
	.prepare = tegra_bpmp_clk_prepare,
	.unprepare = tegra_bpmp_clk_unprepare,
	.is_prepared = tegra_bpmp_clk_is_prepared,
	.recalc_rate = tegra_bpmp_clk_recalc_rate,
	.round_rate = tegra_bpmp_clk_round_rate,
	.set_parent = tegra_bpmp_clk_set_parent,
	.get_parent = tegra_bpmp_clk_get_parent,
	.set_rate = tegra_bpmp_clk_set_rate,
};

static int tegra_bpmp_clk_get_max_id(struct tegra_bpmp *bpmp)
{
	struct cmd_clk_get_max_clk_id_response response;
	struct tegra_bpmp_clk_message msg;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_GET_MAX_CLK_ID;
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(bpmp, &msg);
	if (err < 0)
		return err;

	if (response.max_id > INT_MAX)
		return -E2BIG;

	return response.max_id;
}

static int tegra_bpmp_clk_get_info(struct tegra_bpmp *bpmp, unsigned int id,
				   struct tegra_bpmp_clk_info *info)
{
	struct cmd_clk_get_all_info_response response;
	struct tegra_bpmp_clk_message msg;
	unsigned int i;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.cmd = CMD_CLK_GET_ALL_INFO;
	msg.id = id;
	msg.rx.data = &response;
	msg.rx.size = sizeof(response);

	err = tegra_bpmp_clk_transfer(bpmp, &msg);
	if (err < 0)
		return err;

	strlcpy(info->name, response.name, MRQ_CLK_NAME_MAXLEN);
	info->num_parents = response.num_parents;

	for (i = 0; i < info->num_parents; i++)
		info->parents[i] = response.parents[i];

	info->flags = response.flags;

	return 0;
}

static void tegra_bpmp_clk_info_dump(struct tegra_bpmp *bpmp,
				     const char *level,
				     const struct tegra_bpmp_clk_info *info)
{
	const char *prefix = "";
	struct seq_buf buf;
	unsigned int i;
	char flags[64];

	seq_buf_init(&buf, flags, sizeof(flags));

	if (info->flags)
		seq_buf_printf(&buf, "(");

	if (info->flags & TEGRA_BPMP_CLK_HAS_MUX) {
		seq_buf_printf(&buf, "%smux", prefix);
		prefix = ", ";
	}

	if ((info->flags & TEGRA_BPMP_CLK_HAS_SET_RATE) == 0) {
		seq_buf_printf(&buf, "%sfixed", prefix);
		prefix = ", ";
	}

	if (info->flags & TEGRA_BPMP_CLK_IS_ROOT) {
		seq_buf_printf(&buf, "%sroot", prefix);
		prefix = ", ";
	}

	if (info->flags)
		seq_buf_printf(&buf, ")");

	dev_printk(level, bpmp->dev, "%03u: %s\n", info->id, info->name);
	dev_printk(level, bpmp->dev, "  flags: %lx %s\n", info->flags, flags);
	dev_printk(level, bpmp->dev, "  parents: %u\n", info->num_parents);

	for (i = 0; i < info->num_parents; i++)
		dev_printk(level, bpmp->dev, "    %03u\n", info->parents[i]);
}

static int tegra_bpmp_probe_clocks(struct tegra_bpmp *bpmp,
				   struct tegra_bpmp_clk_info **clocksp)
{
	struct tegra_bpmp_clk_info *clocks;
	unsigned int max_id, id, count = 0;
	unsigned int holes = 0;
	int err;

	err = tegra_bpmp_clk_get_max_id(bpmp);
	if (err < 0)
		return err;

	max_id = err;

	dev_dbg(bpmp->dev, "maximum clock ID: %u\n", max_id);

	clocks = kcalloc(max_id + 1, sizeof(*clocks), GFP_KERNEL);
	if (!clocks)
		return -ENOMEM;

	for (id = 0; id <= max_id; id++) {
		struct tegra_bpmp_clk_info *info = &clocks[count];

		err = tegra_bpmp_clk_get_info(bpmp, id, info);
		if (err < 0)
			continue;

		if (info->num_parents >= U8_MAX) {
			dev_err(bpmp->dev,
				"clock %u has too many parents (%u, max: %u)\n",
				id, info->num_parents, U8_MAX);
			continue;
		}

		/* clock not exposed by BPMP */
		if (info->name[0] == '\0') {
			holes++;
			continue;
		}

		info->id = id;
		count++;

		if (TEGRA_BPMP_DUMP_CLOCK_INFO)
			tegra_bpmp_clk_info_dump(bpmp, KERN_DEBUG, info);
	}

	dev_dbg(bpmp->dev, "holes: %u\n", holes);
	*clocksp = clocks;

	return count;
}

static const struct tegra_bpmp_clk_info *
tegra_bpmp_clk_find(const struct tegra_bpmp_clk_info *clocks,
		    unsigned int num_clocks, unsigned int id)
{
	unsigned int i;

	for (i = 0; i < num_clocks; i++)
		if (clocks[i].id == id)
			return &clocks[i];

	return NULL;
}

static struct tegra_bpmp_clk *
tegra_bpmp_clk_register(struct tegra_bpmp *bpmp,
			const struct tegra_bpmp_clk_info *info,
			const struct tegra_bpmp_clk_info *clocks,
			unsigned int num_clocks)
{
	struct tegra_bpmp_clk *clk;
	struct clk_init_data init;
	const char **parents;
	unsigned int i;
	int err;

	clk = devm_kzalloc(bpmp->dev, sizeof(*clk), GFP_KERNEL);
	if (!clk)
		return ERR_PTR(-ENOMEM);

	clk->id = info->id;
	clk->bpmp = bpmp;

	clk->parents = devm_kcalloc(bpmp->dev, info->num_parents,
				    sizeof(*clk->parents), GFP_KERNEL);
	if (!clk->parents)
		return ERR_PTR(-ENOMEM);

	clk->num_parents = info->num_parents;

	/* hardware clock initialization */
	memset(&init, 0, sizeof(init));
	init.name = info->name;
	clk->hw.init = &init;

	if (info->flags & TEGRA_BPMP_CLK_HAS_MUX) {
		if (info->flags & TEGRA_BPMP_CLK_HAS_SET_RATE)
			init.ops = &tegra_bpmp_clk_mux_rate_ops;
		else
			init.ops = &tegra_bpmp_clk_mux_ops;
	} else {
		if (info->flags & TEGRA_BPMP_CLK_HAS_SET_RATE)
			init.ops = &tegra_bpmp_clk_rate_ops;
		else
			init.ops = &tegra_bpmp_clk_gate_ops;
	}

	init.num_parents = info->num_parents;

	parents = kcalloc(info->num_parents, sizeof(*parents), GFP_KERNEL);
	if (!parents)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < info->num_parents; i++) {
		const struct tegra_bpmp_clk_info *parent;

		/* keep a private copy of the ID to parent index map */
		clk->parents[i] = info->parents[i];

		parent = tegra_bpmp_clk_find(clocks, num_clocks,
					     info->parents[i]);
		if (!parent) {
			dev_err(bpmp->dev, "no parent %u found for %u\n",
				info->parents[i], info->id);
			continue;
		}

		parents[i] = parent->name;
	}

	init.parent_names = parents;

	err = devm_clk_hw_register(bpmp->dev, &clk->hw);

	kfree(parents);

	if (err < 0)
		return ERR_PTR(err);

	return clk;
}

static int tegra_bpmp_register_clocks(struct tegra_bpmp *bpmp,
				      struct tegra_bpmp_clk_info *infos,
				      unsigned int count)
{
	struct tegra_bpmp_clk *clk;
	unsigned int i;

	bpmp->num_clocks = count;

	bpmp->clocks = devm_kcalloc(bpmp->dev, count, sizeof(clk), GFP_KERNEL);
	if (!bpmp->clocks)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct tegra_bpmp_clk_info *info = &infos[i];

		clk = tegra_bpmp_clk_register(bpmp, info, infos, count);
		if (IS_ERR(clk)) {
			dev_err(bpmp->dev,
				"failed to register clock %u (%s): %ld\n",
				info->id, info->name, PTR_ERR(clk));
			continue;
		}

		bpmp->clocks[i] = clk;
	}

	return 0;
}

static void tegra_bpmp_unregister_clocks(struct tegra_bpmp *bpmp)
{
	unsigned int i;

	for (i = 0; i < bpmp->num_clocks; i++)
		clk_hw_unregister(&bpmp->clocks[i]->hw);
}

static struct clk_hw *tegra_bpmp_clk_of_xlate(struct of_phandle_args *clkspec,
					      void *data)
{
	unsigned int id = clkspec->args[0], i;
	struct tegra_bpmp *bpmp = data;

	for (i = 0; i < bpmp->num_clocks; i++) {
		struct tegra_bpmp_clk *clk = bpmp->clocks[i];

		if (!clk)
			continue;

		if (clk->id == id)
			return &clk->hw;
	}

	return NULL;
}

int tegra_bpmp_init_clocks(struct tegra_bpmp *bpmp)
{
	struct tegra_bpmp_clk_info *clocks;
	unsigned int count;
	int err;

	err = tegra_bpmp_probe_clocks(bpmp, &clocks);
	if (err < 0)
		return err;

	count = err;

	dev_dbg(bpmp->dev, "%u clocks probed\n", count);

	err = tegra_bpmp_register_clocks(bpmp, clocks, count);
	if (err < 0)
		goto free;

	err = of_clk_add_hw_provider(bpmp->dev->of_node,
				     tegra_bpmp_clk_of_xlate,
				     bpmp);
	if (err < 0) {
		tegra_bpmp_unregister_clocks(bpmp);
		goto free;
	}

free:
	kfree(clocks);
	return err;
}
