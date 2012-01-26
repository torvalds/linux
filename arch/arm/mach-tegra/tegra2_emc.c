/*
 * Copyright (C) 2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tegra_emc.h>

#include <mach/iomap.h>

#include "tegra2_emc.h"
#include "fuse.h"

#ifdef CONFIG_TEGRA_EMC_SCALING_ENABLE
static bool emc_enable = true;
#else
static bool emc_enable;
#endif
module_param(emc_enable, bool, 0644);

static struct platform_device *emc_pdev;
static void __iomem *emc_regbase;

static inline void emc_writel(u32 val, unsigned long addr)
{
	writel(val, emc_regbase + addr);
}

static inline u32 emc_readl(unsigned long addr)
{
	return readl(emc_regbase + addr);
}

static const unsigned long emc_reg_addr[TEGRA_EMC_NUM_REGS] = {
	0x2c,	/* RC */
	0x30,	/* RFC */
	0x34,	/* RAS */
	0x38,	/* RP */
	0x3c,	/* R2W */
	0x40,	/* W2R */
	0x44,	/* R2P */
	0x48,	/* W2P */
	0x4c,	/* RD_RCD */
	0x50,	/* WR_RCD */
	0x54,	/* RRD */
	0x58,	/* REXT */
	0x5c,	/* WDV */
	0x60,	/* QUSE */
	0x64,	/* QRST */
	0x68,	/* QSAFE */
	0x6c,	/* RDV */
	0x70,	/* REFRESH */
	0x74,	/* BURST_REFRESH_NUM */
	0x78,	/* PDEX2WR */
	0x7c,	/* PDEX2RD */
	0x80,	/* PCHG2PDEN */
	0x84,	/* ACT2PDEN */
	0x88,	/* AR2PDEN */
	0x8c,	/* RW2PDEN */
	0x90,	/* TXSR */
	0x94,	/* TCKE */
	0x98,	/* TFAW */
	0x9c,	/* TRPAB */
	0xa0,	/* TCLKSTABLE */
	0xa4,	/* TCLKSTOP */
	0xa8,	/* TREFBW */
	0xac,	/* QUSE_EXTRA */
	0x114,	/* FBIO_CFG6 */
	0xb0,	/* ODT_WRITE */
	0xb4,	/* ODT_READ */
	0x104,	/* FBIO_CFG5 */
	0x2bc,	/* CFG_DIG_DLL */
	0x2c0,	/* DLL_XFORM_DQS */
	0x2c4,	/* DLL_XFORM_QUSE */
	0x2e0,	/* ZCAL_REF_CNT */
	0x2e4,	/* ZCAL_WAIT_CNT */
	0x2a8,	/* AUTO_CAL_INTERVAL */
	0x2d0,	/* CFG_CLKTRIM_0 */
	0x2d4,	/* CFG_CLKTRIM_1 */
	0x2d8,	/* CFG_CLKTRIM_2 */
};

/* Select the closest EMC rate that is higher than the requested rate */
long tegra_emc_round_rate(unsigned long rate)
{
	struct tegra_emc_pdata *pdata;
	int i;
	int best = -1;
	unsigned long distance = ULONG_MAX;

	if (!emc_pdev)
		return -EINVAL;

	pdata = emc_pdev->dev.platform_data;

	pr_debug("%s: %lu\n", __func__, rate);

	/*
	 * The EMC clock rate is twice the bus rate, and the bus rate is
	 * measured in kHz
	 */
	rate = rate / 2 / 1000;

	for (i = 0; i < pdata->num_tables; i++) {
		if (pdata->tables[i].rate >= rate &&
		    (pdata->tables[i].rate - rate) < distance) {
			distance = pdata->tables[i].rate - rate;
			best = i;
		}
	}

	if (best < 0)
		return -EINVAL;

	pr_debug("%s: using %lu\n", __func__, pdata->tables[best].rate);

	return pdata->tables[best].rate * 2 * 1000;
}

/*
 * The EMC registers have shadow registers.  When the EMC clock is updated
 * in the clock controller, the shadow registers are copied to the active
 * registers, allowing glitchless memory bus frequency changes.
 * This function updates the shadow registers for a new clock frequency,
 * and relies on the clock lock on the emc clock to avoid races between
 * multiple frequency changes
 */
int tegra_emc_set_rate(unsigned long rate)
{
	struct tegra_emc_pdata *pdata;
	int i;
	int j;

	if (!emc_pdev)
		return -EINVAL;

	pdata = emc_pdev->dev.platform_data;

	/*
	 * The EMC clock rate is twice the bus rate, and the bus rate is
	 * measured in kHz
	 */
	rate = rate / 2 / 1000;

	for (i = 0; i < pdata->num_tables; i++)
		if (pdata->tables[i].rate == rate)
			break;

	if (i >= pdata->num_tables)
		return -EINVAL;

	pr_debug("%s: setting to %lu\n", __func__, rate);

	for (j = 0; j < TEGRA_EMC_NUM_REGS; j++)
		emc_writel(pdata->tables[i].regs[j], emc_reg_addr[j]);

	emc_readl(pdata->tables[i].regs[TEGRA_EMC_NUM_REGS - 1]);

	return 0;
}

#ifdef CONFIG_OF
static struct device_node *tegra_emc_ramcode_devnode(struct device_node *np)
{
	struct device_node *iter;
	u32 reg;

	for_each_child_of_node(np, iter) {
		if (of_property_read_u32(np, "nvidia,ram-code", &reg))
			continue;
		if (reg == tegra_bct_strapping)
			return of_node_get(iter);
	}

	return NULL;
}

static struct tegra_emc_pdata *tegra_emc_dt_parse_pdata(
		struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *tnp, *iter;
	struct tegra_emc_pdata *pdata;
	int ret, i, num_tables;

	if (!np)
		return NULL;

	if (of_find_property(np, "nvidia,use-ram-code", NULL)) {
		tnp = tegra_emc_ramcode_devnode(np);
		if (!tnp)
			dev_warn(&pdev->dev,
				 "can't find emc table for ram-code 0x%02x\n",
				 tegra_bct_strapping);
	} else
		tnp = of_node_get(np);

	if (!tnp)
		return NULL;

	num_tables = 0;
	for_each_child_of_node(tnp, iter)
		if (of_device_is_compatible(iter, "nvidia,tegra20-emc-table"))
			num_tables++;

	if (!num_tables) {
		pdata = NULL;
		goto out;
	}

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	pdata->tables = devm_kzalloc(&pdev->dev,
				     sizeof(*pdata->tables) * num_tables,
				     GFP_KERNEL);

	i = 0;
	for_each_child_of_node(tnp, iter) {
		u32 prop;

		ret = of_property_read_u32(iter, "clock-frequency", &prop);
		if (ret) {
			dev_err(&pdev->dev, "no clock-frequency in %s\n",
				iter->full_name);
			continue;
		}
		pdata->tables[i].rate = prop;

		ret = of_property_read_u32_array(iter, "nvidia,emc-registers",
						 pdata->tables[i].regs,
						 TEGRA_EMC_NUM_REGS);
		if (ret) {
			dev_err(&pdev->dev,
				"malformed emc-registers property in %s\n",
				iter->full_name);
			continue;
		}

		i++;
	}
	pdata->num_tables = i;

out:
	of_node_put(tnp);
	return pdata;
}
#else
static struct tegra_emc_pdata *tegra_emc_dt_parse_pdata(
		struct platform_device *pdev)
{
	return NULL;
}
#endif

static struct tegra_emc_pdata __devinit *tegra_emc_fill_pdata(struct platform_device *pdev)
{
	struct clk *c = clk_get_sys(NULL, "emc");
	struct tegra_emc_pdata *pdata;
	unsigned long khz;
	int i;

	WARN_ON(pdev->dev.platform_data);
	BUG_ON(IS_ERR_OR_NULL(c));

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	pdata->tables = devm_kzalloc(&pdev->dev, sizeof(*pdata->tables),
				     GFP_KERNEL);

	pdata->tables[0].rate = clk_get_rate(c) / 2 / 1000;

	for (i = 0; i < TEGRA_EMC_NUM_REGS; i++)
		pdata->tables[0].regs[i] = emc_readl(emc_reg_addr[i]);

	pdata->num_tables = 1;

	khz = pdata->tables[0].rate;
	dev_info(&pdev->dev, "no tables provided, using %ld kHz emc, "
		 "%ld kHz mem\n", khz * 2, khz);

	return pdata;
}

static int __devinit tegra_emc_probe(struct platform_device *pdev)
{
	struct tegra_emc_pdata *pdata;
	struct resource *res;

	if (!emc_enable) {
		dev_err(&pdev->dev, "disabled per module parameter\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing register base\n");
		return -ENOMEM;
	}

	emc_regbase = devm_request_and_ioremap(&pdev->dev, res);
	if (!emc_regbase) {
		dev_err(&pdev->dev, "failed to remap registers\n");
		return -ENOMEM;
	}

	pdata = pdev->dev.platform_data;

	if (!pdata)
		pdata = tegra_emc_dt_parse_pdata(pdev);

	if (!pdata)
		pdata = tegra_emc_fill_pdata(pdev);

	pdev->dev.platform_data = pdata;

	emc_pdev = pdev;

	return 0;
}

static struct of_device_id tegra_emc_of_match[] __devinitdata = {
	{ .compatible = "nvidia,tegra20-emc", },
	{ },
};

static struct platform_driver tegra_emc_driver = {
	.driver         = {
		.name   = "tegra-emc",
		.owner  = THIS_MODULE,
		.of_match_table = tegra_emc_of_match,
	},
	.probe          = tegra_emc_probe,
};

static int __init tegra_emc_init(void)
{
	return platform_driver_register(&tegra_emc_driver);
}
device_initcall(tegra_emc_init);
