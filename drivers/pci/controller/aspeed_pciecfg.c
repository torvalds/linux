// SPDX-License-Identifier: GPL-2.0+
/*
 * PCIe host controller driver for ASPEED PCIe Bridge
 *
 */
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

struct aspeed_pciecfg {
	struct device *dev;
	void __iomem *reg;
	struct reset_control *rst;
	struct reset_control *rc_low_rst;
	struct reset_control *rc_high_rst;
	struct regmap *ahbc;
};

static const struct of_device_id aspeed_pciecfg_of_match[] = {
	{ .compatible = "aspeed,ast2600-pciecfg", },
	{}
};

#define AHBC_UNLOCK	0xAEED1A03
static void aspeed_pciecfg_init(struct aspeed_pciecfg *pciecfg)
{
	//h2x reset init
	reset_control_assert(pciecfg->rst);
	//rcL assert
	if (pciecfg->rc_low_rst) {
		reset_control_deassert(pciecfg->rc_low_rst);
		reset_control_assert(pciecfg->rc_low_rst);
	}
	//rch assert
	if (pciecfg->rc_high_rst) {
		reset_control_deassert(pciecfg->rc_high_rst);
		reset_control_assert(pciecfg->rc_high_rst);
	}

	reset_control_deassert(pciecfg->rst);

	//init
	writel(0x1, pciecfg->reg + 0x00);

	//ahb to pcie rc
	writel(0xe0006000, pciecfg->reg + 0x60);
	writel(0x00000000, pciecfg->reg + 0x64);
	writel(0xFFFFFFFF, pciecfg->reg + 0x68);

	//ahbc remap enable
	regmap_write(pciecfg->ahbc, 0x00, AHBC_UNLOCK);
	regmap_update_bits(pciecfg->ahbc, 0x8C, BIT(5), BIT(5));
	regmap_write(pciecfg->ahbc, 0x00, 0x1);

}

static int aspeed_pciecfg_probe(struct platform_device *pdev)
{
	struct aspeed_pciecfg *pciecfg;
	struct device *dev = &pdev->dev;

	pciecfg = devm_kzalloc(&pdev->dev, sizeof(*pciecfg), GFP_KERNEL);
	if (!pciecfg)
		return -ENOMEM;

	pciecfg->dev = &pdev->dev;
	pciecfg->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pciecfg->reg))
		return PTR_ERR(pciecfg->reg);

	pciecfg->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(pciecfg->rst)) {
		dev_err(&pdev->dev, "can't get pcie reset\n");
		return PTR_ERR(pciecfg->rst);
	}

	if (of_device_is_available(of_parse_phandle(dev->of_node, "aspeed,pcie0", 0))) {
		pciecfg->rc_low_rst = devm_reset_control_get_shared(&pdev->dev, "rc_low");
		if (IS_ERR(pciecfg->rc_low_rst)) {
			dev_err(&pdev->dev, "can't get RC low reset\n");
			pciecfg->rc_low_rst = NULL;
		}
	} else
		pciecfg->rc_low_rst = NULL;


	if (of_device_is_available(of_parse_phandle(dev->of_node, "aspeed,pcie1", 0))) {
		pciecfg->rc_high_rst = devm_reset_control_get_shared(&pdev->dev, "rc_high");
		if (IS_ERR(pciecfg->rc_high_rst)) {
			dev_err(&pdev->dev, "can't get RC high reset\n");
			pciecfg->rc_high_rst = NULL;
		}
	} else
		pciecfg->rc_high_rst = NULL;

	pciecfg->ahbc = syscon_regmap_lookup_by_compatible("aspeed,aspeed-ahbc");
	if (IS_ERR(pciecfg->ahbc))
		return IS_ERR(pciecfg->ahbc);

	aspeed_pciecfg_init(pciecfg);

	return 0;
}

static struct platform_driver aspeed_pciecfg_driver = {
	.driver = {
		.name = "aspeed-pciecfg",
		.suppress_bind_attrs = true,
		.of_match_table = aspeed_pciecfg_of_match,
	},
	.probe = aspeed_pciecfg_probe,
};

static int __init aspeed_pcicfg_init(void)
{
	return platform_driver_register(&aspeed_pciecfg_driver);
}
postcore_initcall(aspeed_pcicfg_init);
