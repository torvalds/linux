// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/soc/qcom,gsbi.h>

#define GSBI_CTRL_REG		0x0000
#define GSBI_PROTOCOL_SHIFT	4
#define MAX_GSBI		12

#define TCSR_ADM_CRCI_BASE	0x70

struct crci_config {
	u32 num_rows;
	const u32 (*array)[MAX_GSBI];
};

static const u32 crci_ipq8064[][MAX_GSBI] = {
	{
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000c00, 0x003000, 0x00c000,
		0x030000, 0x0c0000, 0x300000, 0xc00000
	},
	{
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000c00, 0x003000, 0x00c000,
		0x030000, 0x0c0000, 0x300000, 0xc00000
	},
};

static const struct crci_config config_ipq8064 = {
	.num_rows = ARRAY_SIZE(crci_ipq8064),
	.array = crci_ipq8064,
};

static const unsigned int crci_apq8064[][MAX_GSBI] = {
	{
		0x001800, 0x006000, 0x000030, 0x0000c0,
		0x000300, 0x000400, 0x000000, 0x000000,
		0x000000, 0x000000, 0x000000, 0x000000
	},
	{
		0x000000, 0x000000, 0x000000, 0x000000,
		0x000000, 0x000020, 0x0000c0, 0x000000,
		0x000000, 0x000000, 0x000000, 0x000000
	},
};

static const struct crci_config config_apq8064 = {
	.num_rows = ARRAY_SIZE(crci_apq8064),
	.array = crci_apq8064,
};

static const unsigned int crci_msm8960[][MAX_GSBI] = {
	{
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000400, 0x000000, 0x000000,
		0x000000, 0x000000, 0x000000, 0x000000
	},
	{
		0x000000, 0x000000, 0x000000, 0x000000,
		0x000000, 0x000020, 0x0000c0, 0x000300,
		0x001800, 0x006000, 0x000000, 0x000000
	},
};

static const struct crci_config config_msm8960 = {
	.num_rows = ARRAY_SIZE(crci_msm8960),
	.array = crci_msm8960,
};

static const unsigned int crci_msm8660[][MAX_GSBI] = {
	{	/* ADM 0 - B */
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000c00, 0x003000, 0x00c000,
		0x030000, 0x0c0000, 0x300000, 0xc00000
	},
	{	/* ADM 0 - B */
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000c00, 0x003000, 0x00c000,
		0x030000, 0x0c0000, 0x300000, 0xc00000
	},
	{	/* ADM 1 - A */
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000c00, 0x003000, 0x00c000,
		0x030000, 0x0c0000, 0x300000, 0xc00000
	},
	{	/* ADM 1 - B */
		0x000003, 0x00000c, 0x000030, 0x0000c0,
		0x000300, 0x000c00, 0x003000, 0x00c000,
		0x030000, 0x0c0000, 0x300000, 0xc00000
	},
};

static const struct crci_config config_msm8660 = {
	.num_rows = ARRAY_SIZE(crci_msm8660),
	.array = crci_msm8660,
};

struct gsbi_info {
	struct clk *hclk;
	u32 mode;
	u32 crci;
	struct regmap *tcsr;
};

static const struct of_device_id tcsr_dt_match[] = {
	{ .compatible = "qcom,tcsr-ipq8064", .data = &config_ipq8064},
	{ .compatible = "qcom,tcsr-apq8064", .data = &config_apq8064},
	{ .compatible = "qcom,tcsr-msm8960", .data = &config_msm8960},
	{ .compatible = "qcom,tcsr-msm8660", .data = &config_msm8660},
	{ },
};

static int gsbi_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device_node *tcsr_node;
	const struct of_device_id *match;
	void __iomem *base;
	struct gsbi_info *gsbi;
	int i, ret;
	u32 mask, gsbi_num;
	const struct crci_config *config = NULL;

	gsbi = devm_kzalloc(&pdev->dev, sizeof(*gsbi), GFP_KERNEL);

	if (!gsbi)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* get the tcsr node and setup the config and regmap */
	gsbi->tcsr = syscon_regmap_lookup_by_phandle(node, "syscon-tcsr");

	if (!IS_ERR(gsbi->tcsr)) {
		tcsr_node = of_parse_phandle(node, "syscon-tcsr", 0);
		if (tcsr_node) {
			match = of_match_node(tcsr_dt_match, tcsr_node);
			if (match)
				config = match->data;
			else
				dev_warn(&pdev->dev, "no matching TCSR\n");

			of_node_put(tcsr_node);
		}
	}

	if (of_property_read_u32(node, "cell-index", &gsbi_num)) {
		dev_err(&pdev->dev, "missing cell-index\n");
		return -EINVAL;
	}

	if (gsbi_num < 1 || gsbi_num > MAX_GSBI) {
		dev_err(&pdev->dev, "invalid cell-index\n");
		return -EINVAL;
	}

	if (of_property_read_u32(node, "qcom,mode", &gsbi->mode)) {
		dev_err(&pdev->dev, "missing mode configuration\n");
		return -EINVAL;
	}

	/* not required, so default to 0 if not present */
	of_property_read_u32(node, "qcom,crci", &gsbi->crci);

	dev_info(&pdev->dev, "GSBI port protocol: %d crci: %d\n",
		 gsbi->mode, gsbi->crci);
	gsbi->hclk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(gsbi->hclk))
		return PTR_ERR(gsbi->hclk);

	clk_prepare_enable(gsbi->hclk);

	writel_relaxed((gsbi->mode << GSBI_PROTOCOL_SHIFT) | gsbi->crci,
				base + GSBI_CTRL_REG);

	/*
	 * modify tcsr to reflect mode and ADM CRCI mux
	 * Each gsbi contains a pair of bits, one for RX and one for TX
	 * SPI mode requires both bits cleared, otherwise they are set
	 */
	if (config) {
		for (i = 0; i < config->num_rows; i++) {
			mask = config->array[i][gsbi_num - 1];

			if (gsbi->mode == GSBI_PROT_SPI)
				regmap_update_bits(gsbi->tcsr,
					TCSR_ADM_CRCI_BASE + 4 * i, mask, 0);
			else
				regmap_update_bits(gsbi->tcsr,
					TCSR_ADM_CRCI_BASE + 4 * i, mask, mask);

		}
	}

	/* make sure the gsbi control write is not reordered */
	wmb();

	platform_set_drvdata(pdev, gsbi);

	ret = of_platform_populate(node, NULL, NULL, &pdev->dev);
	if (ret)
		clk_disable_unprepare(gsbi->hclk);
	return ret;
}

static int gsbi_remove(struct platform_device *pdev)
{
	struct gsbi_info *gsbi = platform_get_drvdata(pdev);

	clk_disable_unprepare(gsbi->hclk);

	return 0;
}

static const struct of_device_id gsbi_dt_match[] = {
	{ .compatible = "qcom,gsbi-v1.0.0", },
	{ },
};

MODULE_DEVICE_TABLE(of, gsbi_dt_match);

static struct platform_driver gsbi_driver = {
	.driver = {
		.name		= "gsbi",
		.of_match_table	= gsbi_dt_match,
	},
	.probe = gsbi_probe,
	.remove	= gsbi_remove,
};

module_platform_driver(gsbi_driver);

MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_DESCRIPTION("QCOM GSBI driver");
MODULE_LICENSE("GPL v2");
