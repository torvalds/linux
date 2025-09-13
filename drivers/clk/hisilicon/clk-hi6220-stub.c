// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hi6220 stub clock driver
 *
 * Copyright (c) 2015 Hisilicon Limited.
 * Copyright (c) 2015 Linaro Limited.
 *
 * Author: Leo Yan <leo.yan@linaro.org>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/mailbox_client.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Stub clocks id */
#define HI6220_STUB_ACPU0		0
#define HI6220_STUB_ACPU1		1
#define HI6220_STUB_GPU			2
#define HI6220_STUB_DDR			5

/* Mailbox message */
#define HI6220_MBOX_MSG_LEN		8

#define HI6220_MBOX_FREQ		0xA
#define HI6220_MBOX_CMD_SET		0x3
#define HI6220_MBOX_OBJ_AP		0x0

/* CPU dynamic frequency scaling */
#define ACPU_DFS_FREQ_MAX		0x1724
#define ACPU_DFS_CUR_FREQ		0x17CC
#define ACPU_DFS_FLAG			0x1B30
#define ACPU_DFS_FREQ_REQ		0x1B34
#define ACPU_DFS_FREQ_LMT		0x1B38
#define ACPU_DFS_LOCK_FLAG		0xAEAEAEAE

#define to_stub_clk(hw) container_of(hw, struct hi6220_stub_clk, hw)

struct hi6220_stub_clk {
	u32 id;

	struct device *dev;
	struct clk_hw hw;

	struct regmap *dfs_map;
	struct mbox_client cl;
	struct mbox_chan *mbox;
};

struct hi6220_mbox_msg {
	unsigned char type;
	unsigned char cmd;
	unsigned char obj;
	unsigned char src;
	unsigned char para[4];
};

union hi6220_mbox_data {
	unsigned int data[HI6220_MBOX_MSG_LEN];
	struct hi6220_mbox_msg msg;
};

static unsigned int hi6220_acpu_get_freq(struct hi6220_stub_clk *stub_clk)
{
	unsigned int freq;

	regmap_read(stub_clk->dfs_map, ACPU_DFS_CUR_FREQ, &freq);
	return freq;
}

static int hi6220_acpu_set_freq(struct hi6220_stub_clk *stub_clk,
				unsigned int freq)
{
	union hi6220_mbox_data data;

	/* set the frequency in sram */
	regmap_write(stub_clk->dfs_map, ACPU_DFS_FREQ_REQ, freq);

	/* compound mailbox message */
	data.msg.type = HI6220_MBOX_FREQ;
	data.msg.cmd  = HI6220_MBOX_CMD_SET;
	data.msg.obj  = HI6220_MBOX_OBJ_AP;
	data.msg.src  = HI6220_MBOX_OBJ_AP;

	mbox_send_message(stub_clk->mbox, &data);
	return 0;
}

static int hi6220_acpu_round_freq(struct hi6220_stub_clk *stub_clk,
				  unsigned int freq)
{
	unsigned int limit_flag, limit_freq = UINT_MAX;
	unsigned int max_freq;

	/* check the constrained frequency */
	regmap_read(stub_clk->dfs_map, ACPU_DFS_FLAG, &limit_flag);
	if (limit_flag == ACPU_DFS_LOCK_FLAG)
		regmap_read(stub_clk->dfs_map, ACPU_DFS_FREQ_LMT, &limit_freq);

	/* check the supported maximum frequency */
	regmap_read(stub_clk->dfs_map, ACPU_DFS_FREQ_MAX, &max_freq);

	/* calculate the real maximum frequency */
	max_freq = min(max_freq, limit_freq);

	if (WARN_ON(freq > max_freq))
		freq = max_freq;

	return freq;
}

static unsigned long hi6220_stub_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	u32 rate = 0;
	struct hi6220_stub_clk *stub_clk = to_stub_clk(hw);

	switch (stub_clk->id) {
	case HI6220_STUB_ACPU0:
		rate = hi6220_acpu_get_freq(stub_clk);

		/* convert from kHz to Hz */
		rate *= 1000;
		break;

	default:
		dev_err(stub_clk->dev, "%s: un-supported clock id %d\n",
			__func__, stub_clk->id);
		break;
	}

	return rate;
}

static int hi6220_stub_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct hi6220_stub_clk *stub_clk = to_stub_clk(hw);
	unsigned long new_rate = rate / 1000;  /* kHz */
	int ret = 0;

	switch (stub_clk->id) {
	case HI6220_STUB_ACPU0:
		ret = hi6220_acpu_set_freq(stub_clk, new_rate);
		if (ret < 0)
			return ret;

		break;

	default:
		dev_err(stub_clk->dev, "%s: un-supported clock id %d\n",
			__func__, stub_clk->id);
		break;
	}

	pr_debug("%s: set rate=%ldkHz\n", __func__, new_rate);
	return ret;
}

static int hi6220_stub_clk_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	struct hi6220_stub_clk *stub_clk = to_stub_clk(hw);
	unsigned long new_rate = req->rate / 1000;  /* kHz */

	switch (stub_clk->id) {
	case HI6220_STUB_ACPU0:
		new_rate = hi6220_acpu_round_freq(stub_clk, new_rate);

		/* convert from kHz to Hz */
		new_rate *= 1000;
		break;

	default:
		dev_err(stub_clk->dev, "%s: un-supported clock id %d\n",
			__func__, stub_clk->id);
		break;
	}

	req->rate = new_rate;

	return 0;
}

static const struct clk_ops hi6220_stub_clk_ops = {
	.recalc_rate	= hi6220_stub_clk_recalc_rate,
	.determine_rate = hi6220_stub_clk_determine_rate,
	.set_rate	= hi6220_stub_clk_set_rate,
};

static int hi6220_stub_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_init_data init;
	struct hi6220_stub_clk *stub_clk;
	struct clk *clk;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	stub_clk = devm_kzalloc(dev, sizeof(*stub_clk), GFP_KERNEL);
	if (!stub_clk)
		return -ENOMEM;

	stub_clk->dfs_map = syscon_regmap_lookup_by_phandle(np,
				"hisilicon,hi6220-clk-sram");
	if (IS_ERR(stub_clk->dfs_map)) {
		dev_err(dev, "failed to get sram regmap\n");
		return PTR_ERR(stub_clk->dfs_map);
	}

	stub_clk->hw.init = &init;
	stub_clk->dev = dev;
	stub_clk->id = HI6220_STUB_ACPU0;

	/* Use mailbox client with blocking mode */
	stub_clk->cl.dev = dev;
	stub_clk->cl.tx_done = NULL;
	stub_clk->cl.tx_block = true;
	stub_clk->cl.tx_tout = 500;
	stub_clk->cl.knows_txdone = false;

	/* Allocate mailbox channel */
	stub_clk->mbox = mbox_request_channel(&stub_clk->cl, 0);
	if (IS_ERR(stub_clk->mbox)) {
		dev_err(dev, "failed get mailbox channel\n");
		return PTR_ERR(stub_clk->mbox);
	}

	init.name = "acpu0";
	init.ops = &hi6220_stub_clk_ops;
	init.num_parents = 0;
	init.flags = 0;

	clk = devm_clk_register(dev, &stub_clk->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (ret) {
		dev_err(dev, "failed to register OF clock provider\n");
		return ret;
	}

	/* initialize buffer to zero */
	regmap_write(stub_clk->dfs_map, ACPU_DFS_FLAG, 0x0);
	regmap_write(stub_clk->dfs_map, ACPU_DFS_FREQ_REQ, 0x0);
	regmap_write(stub_clk->dfs_map, ACPU_DFS_FREQ_LMT, 0x0);

	dev_dbg(dev, "Registered clock '%s'\n", init.name);
	return 0;
}

static const struct of_device_id hi6220_stub_clk_of_match[] = {
	{ .compatible = "hisilicon,hi6220-stub-clk", },
	{}
};

static struct platform_driver hi6220_stub_clk_driver = {
	.driver	= {
		.name = "hi6220-stub-clk",
		.of_match_table = hi6220_stub_clk_of_match,
	},
	.probe = hi6220_stub_clk_probe,
};

static int __init hi6220_stub_clk_init(void)
{
	return platform_driver_register(&hi6220_stub_clk_driver);
}
subsys_initcall(hi6220_stub_clk_init);
