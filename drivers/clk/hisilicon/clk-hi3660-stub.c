/*
 * Hisilicon clock driver
 *
 * Copyright (c) 2013-2017 Hisilicon Limited.
 * Copyright (c) 2017 Linaro Limited.
 *
 * Author: Kai Zhao <zhaokai1@hisilicon.com>
 *	    Tao Wang <kevin.wangtao@hisilicon.com>
 *	    Leo Yan <leo.yan@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/hi3660-clock.h>

#define HI3660_STUB_CLOCK_DATA		(0x70)
#define MHZ				(1000 * 1000)

#define DEFINE_CLK_STUB(_id, _cmd, _name)			\
	{							\
		.id = (_id),					\
		.cmd = (_cmd),					\
		.hw.init = &(struct clk_init_data) {		\
			.name = #_name,				\
			.ops = &hi3660_stub_clk_ops,		\
			.num_parents = 0,			\
			.flags = CLK_GET_RATE_NOCACHE,		\
		},						\
	},

#define to_stub_clk(_hw) container_of(_hw, struct hi3660_stub_clk, hw)

struct hi3660_stub_clk_chan {
	struct mbox_client cl;
	struct mbox_chan *mbox;
};

struct hi3660_stub_clk {
	unsigned int id;
	struct clk_hw hw;
	unsigned int cmd;
	unsigned int msg[8];
	unsigned int rate;
};

static void __iomem *freq_reg;
static struct hi3660_stub_clk_chan stub_clk_chan;

static unsigned long hi3660_stub_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct hi3660_stub_clk *stub_clk = to_stub_clk(hw);

	/*
	 * LPM3 writes back the CPU frequency in shared SRAM so read
	 * back the frequency.
	 */
	stub_clk->rate = readl(freq_reg + (stub_clk->id << 2)) * MHZ;
	return stub_clk->rate;
}

static long hi3660_stub_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *prate)
{
	/*
	 * LPM3 handles rate rounding so just return whatever
	 * rate is requested.
	 */
	return rate;
}

static int hi3660_stub_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct hi3660_stub_clk *stub_clk = to_stub_clk(hw);

	stub_clk->msg[0] = stub_clk->cmd;
	stub_clk->msg[1] = rate / MHZ;

	dev_dbg(stub_clk_chan.cl.dev, "set rate msg[0]=0x%x msg[1]=0x%x\n",
		stub_clk->msg[0], stub_clk->msg[1]);

	mbox_send_message(stub_clk_chan.mbox, stub_clk->msg);
	mbox_client_txdone(stub_clk_chan.mbox, 0);

	stub_clk->rate = rate;
	return 0;
}

static const struct clk_ops hi3660_stub_clk_ops = {
	.recalc_rate    = hi3660_stub_clk_recalc_rate,
	.round_rate     = hi3660_stub_clk_round_rate,
	.set_rate       = hi3660_stub_clk_set_rate,
};

static struct hi3660_stub_clk hi3660_stub_clks[HI3660_CLK_STUB_NUM] = {
	DEFINE_CLK_STUB(HI3660_CLK_STUB_CLUSTER0, 0x0001030A, "cpu-cluster.0")
	DEFINE_CLK_STUB(HI3660_CLK_STUB_CLUSTER1, 0x0002030A, "cpu-cluster.1")
	DEFINE_CLK_STUB(HI3660_CLK_STUB_GPU, 0x0003030A, "clk-g3d")
	DEFINE_CLK_STUB(HI3660_CLK_STUB_DDR, 0x00040309, "clk-ddrc")
};

static struct clk_hw *hi3660_stub_clk_hw_get(struct of_phandle_args *clkspec,
					     void *data)
{
	unsigned int idx = clkspec->args[0];

	if (idx >= HI3660_CLK_STUB_NUM) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &hi3660_stub_clks[idx].hw;
}

static int hi3660_stub_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	unsigned int i;
	int ret;

	/* Use mailbox client without blocking */
	stub_clk_chan.cl.dev = dev;
	stub_clk_chan.cl.tx_done = NULL;
	stub_clk_chan.cl.tx_block = false;
	stub_clk_chan.cl.knows_txdone = false;

	/* Allocate mailbox channel */
	stub_clk_chan.mbox = mbox_request_channel(&stub_clk_chan.cl, 0);
	if (IS_ERR(stub_clk_chan.mbox))
		return PTR_ERR(stub_clk_chan.mbox);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	freq_reg = devm_ioremap(dev, res->start, resource_size(res));
	if (!freq_reg)
		return -ENOMEM;

	freq_reg += HI3660_STUB_CLOCK_DATA;

	for (i = 0; i < HI3660_CLK_STUB_NUM; i++) {
		ret = devm_clk_hw_register(&pdev->dev, &hi3660_stub_clks[i].hw);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(&pdev->dev, hi3660_stub_clk_hw_get,
					   hi3660_stub_clks);
}

static const struct of_device_id hi3660_stub_clk_of_match[] = {
	{ .compatible = "hisilicon,hi3660-stub-clk", },
	{}
};

static struct platform_driver hi3660_stub_clk_driver = {
	.probe	= hi3660_stub_clk_probe,
	.driver = {
		.name = "hi3660-stub-clk",
		.of_match_table = hi3660_stub_clk_of_match,
	},
};

static int __init hi3660_stub_clk_init(void)
{
	return platform_driver_register(&hi3660_stub_clk_driver);
}
subsys_initcall(hi3660_stub_clk_init);
