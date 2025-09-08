// SPDX-License-Identifier: GPL-2.0+
/*
 * dwmac-renesas-gbeth.c - DWMAC Specific Glue layer for Renesas GBETH
 *
 * The Rx and Tx clocks are supplied as follows for the GBETH IP.
 *
 *                         Rx / Tx
 *   -------+------------- on / off -------
 *          |
 *          |            Rx-180 / Tx-180
 *          +---- not ---- on / off -------
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/types.h>

#include "stmmac_platform.h"

/**
 * struct renesas_gbeth_of_data - OF data for Renesas GBETH
 *
 * @clks: Array of clock names
 * @num_clks: Number of clocks
 * @stmmac_flags: Flags for the stmmac driver
 * @handle_reset: Flag to indicate if reset control is
 *                handled by the glue driver or core driver.
 * @set_clk_tx_rate: Flag to indicate if Tx clock is fixed or
 *                   set_clk_tx_rate is needed.
 */
struct renesas_gbeth_of_data {
	const char * const *clks;
	u8 num_clks;
	u32 stmmac_flags;
	bool handle_reset;
	bool set_clk_tx_rate;
};

struct renesas_gbeth {
	const struct renesas_gbeth_of_data *of_data;
	struct plat_stmmacenet_data *plat_dat;
	struct reset_control *rstc;
	struct device *dev;
};

static const char *const renesas_gbeth_clks[] = {
	"tx", "tx-180", "rx", "rx-180",
};

static int renesas_gbeth_init(struct platform_device *pdev, void *priv)
{
	struct plat_stmmacenet_data *plat_dat;
	struct renesas_gbeth *gbeth = priv;
	int ret;

	plat_dat = gbeth->plat_dat;

	ret = reset_control_deassert(gbeth->rstc);
	if (ret) {
		dev_err(gbeth->dev, "Reset deassert failed\n");
		return ret;
	}

	ret = clk_bulk_prepare_enable(plat_dat->num_clks,
				      plat_dat->clks);
	if (ret)
		reset_control_assert(gbeth->rstc);

	return ret;
}

static void renesas_gbeth_exit(struct platform_device *pdev, void *priv)
{
	struct plat_stmmacenet_data *plat_dat;
	struct renesas_gbeth *gbeth = priv;
	int ret;

	plat_dat = gbeth->plat_dat;

	clk_bulk_disable_unprepare(plat_dat->num_clks, plat_dat->clks);

	ret = reset_control_assert(gbeth->rstc);
	if (ret)
		dev_err(gbeth->dev, "Reset assert failed\n");
}

static int renesas_gbeth_probe(struct platform_device *pdev)
{
	const struct renesas_gbeth_of_data *of_data;
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	struct renesas_gbeth *gbeth;
	unsigned int i;
	int err;

	err = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to get resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(dev, PTR_ERR(plat_dat),
				     "dt configuration failed\n");

	gbeth = devm_kzalloc(dev, sizeof(*gbeth), GFP_KERNEL);
	if (!gbeth)
		return -ENOMEM;

	of_data = of_device_get_match_data(&pdev->dev);
	gbeth->of_data = of_data;

	plat_dat->num_clks = of_data->num_clks;
	plat_dat->clks = devm_kcalloc(dev, plat_dat->num_clks,
				      sizeof(*plat_dat->clks), GFP_KERNEL);
	if (!plat_dat->clks)
		return -ENOMEM;

	for (i = 0; i < plat_dat->num_clks; i++)
		plat_dat->clks[i].id = of_data->clks[i];

	err = devm_clk_bulk_get(dev, plat_dat->num_clks, plat_dat->clks);
	if (err < 0)
		return err;

	plat_dat->clk_tx_i = stmmac_pltfr_find_clk(plat_dat, "tx");
	if (!plat_dat->clk_tx_i)
		return dev_err_probe(dev, -EINVAL,
				     "error finding tx clock\n");

	if (of_data->handle_reset) {
		gbeth->rstc = devm_reset_control_get_exclusive(dev, NULL);
		if (IS_ERR(gbeth->rstc))
			return PTR_ERR(gbeth->rstc);
	}

	gbeth->dev = dev;
	gbeth->plat_dat = plat_dat;
	plat_dat->bsp_priv = gbeth;
	if (of_data->set_clk_tx_rate)
		plat_dat->set_clk_tx_rate = stmmac_set_clk_tx_rate;
	plat_dat->init = renesas_gbeth_init;
	plat_dat->exit = renesas_gbeth_exit;
	plat_dat->flags |= STMMAC_FLAG_EN_TX_LPI_CLK_PHY_CAP |
			   gbeth->of_data->stmmac_flags;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct renesas_gbeth_of_data renesas_gbeth_of_data = {
	.clks = renesas_gbeth_clks,
	.num_clks = ARRAY_SIZE(renesas_gbeth_clks),
	.handle_reset = true,
	.set_clk_tx_rate = true,
	.stmmac_flags = STMMAC_FLAG_HWTSTAMP_CORRECT_LATENCY |
			STMMAC_FLAG_SPH_DISABLE,
};

static const struct of_device_id renesas_gbeth_match[] = {
	{ .compatible = "renesas,rzv2h-gbeth", .data = &renesas_gbeth_of_data },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, renesas_gbeth_match);

static struct platform_driver renesas_gbeth_driver = {
	.probe  = renesas_gbeth_probe,
	.driver = {
		.name		= "renesas-gbeth",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table	= renesas_gbeth_match,
	},
};
module_platform_driver(renesas_gbeth_driver);

MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas GBETH DWMAC Specific Glue layer");
MODULE_LICENSE("GPL");
