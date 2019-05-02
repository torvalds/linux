// SPDX-License-Identifier: GPL-2.0

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

struct clk_bcm63xx_table_entry {
	const char * const name;
	u8 bit;
	unsigned long flags;
};

struct clk_bcm63xx_hw {
	void __iomem *regs;
	spinlock_t lock;

	struct clk_hw_onecell_data data;
};

static const struct clk_bcm63xx_table_entry bcm3368_clocks[] = {
	{ .name = "mac", .bit = 3, },
	{ .name = "tc", .bit = 5, },
	{ .name = "us_top", .bit = 6, },
	{ .name = "ds_top", .bit = 7, },
	{ .name = "acm", .bit = 8, },
	{ .name = "spi", .bit = 9, },
	{ .name = "usbs", .bit = 10, },
	{ .name = "bmu", .bit = 11, },
	{ .name = "pcm", .bit = 12, },
	{ .name = "ntp", .bit = 13, },
	{ .name = "acp_b", .bit = 14, },
	{ .name = "acp_a", .bit = 15, },
	{ .name = "emusb", .bit = 17, },
	{ .name = "enet0", .bit = 18, },
	{ .name = "enet1", .bit = 19, },
	{ .name = "usbsu", .bit = 20, },
	{ .name = "ephy", .bit = 21, },
	{ },
};

static const struct clk_bcm63xx_table_entry bcm6328_clocks[] = {
	{ .name = "phy_mips", .bit = 0, },
	{ .name = "adsl_qproc", .bit = 1, },
	{ .name = "adsl_afe", .bit = 2, },
	{ .name = "adsl", .bit = 3, },
	{ .name = "mips", .bit = 4, .flags = CLK_IS_CRITICAL, },
	{ .name = "sar", .bit = 5, },
	{ .name = "pcm", .bit = 6, },
	{ .name = "usbd", .bit = 7, },
	{ .name = "usbh", .bit = 8, },
	{ .name = "hsspi", .bit = 9, },
	{ .name = "pcie", .bit = 10, },
	{ .name = "robosw", .bit = 11, },
	{ },
};

static const struct clk_bcm63xx_table_entry bcm6358_clocks[] = {
	{ .name = "enet", .bit = 4, },
	{ .name = "adslphy", .bit = 5, },
	{ .name = "pcm", .bit = 8, },
	{ .name = "spi", .bit = 9, },
	{ .name = "usbs", .bit = 10, },
	{ .name = "sar", .bit = 11, },
	{ .name = "emusb", .bit = 17, },
	{ .name = "enet0", .bit = 18, },
	{ .name = "enet1", .bit = 19, },
	{ .name = "usbsu", .bit = 20, },
	{ .name = "ephy", .bit = 21, },
	{ },
};

static const struct clk_bcm63xx_table_entry bcm6362_clocks[] = {
	{ .name = "adsl_qproc", .bit = 1, },
	{ .name = "adsl_afe", .bit = 2, },
	{ .name = "adsl", .bit = 3, },
	{ .name = "mips", .bit = 4, .flags = CLK_IS_CRITICAL, },
	{ .name = "wlan_ocp", .bit = 5, },
	{ .name = "swpkt_usb", .bit = 7, },
	{ .name = "swpkt_sar", .bit = 8, },
	{ .name = "sar", .bit = 9, },
	{ .name = "robosw", .bit = 10, },
	{ .name = "pcm", .bit = 11, },
	{ .name = "usbd", .bit = 12, },
	{ .name = "usbh", .bit = 13, },
	{ .name = "ipsec", .bit = 14, },
	{ .name = "spi", .bit = 15, },
	{ .name = "hsspi", .bit = 16, },
	{ .name = "pcie", .bit = 17, },
	{ .name = "fap", .bit = 18, },
	{ .name = "phymips", .bit = 19, },
	{ .name = "nand", .bit = 20, },
	{ },
};

static const struct clk_bcm63xx_table_entry bcm6368_clocks[] = {
	{ .name = "vdsl_qproc", .bit = 2, },
	{ .name = "vdsl_afe", .bit = 3, },
	{ .name = "vdsl_bonding", .bit = 4, },
	{ .name = "vdsl", .bit = 5, },
	{ .name = "phymips", .bit = 6, },
	{ .name = "swpkt_usb", .bit = 7, },
	{ .name = "swpkt_sar", .bit = 8, },
	{ .name = "spi", .bit = 9, },
	{ .name = "usbd", .bit = 10, },
	{ .name = "sar", .bit = 11, },
	{ .name = "robosw", .bit = 12, },
	{ .name = "utopia", .bit = 13, },
	{ .name = "pcm", .bit = 14, },
	{ .name = "usbh", .bit = 15, },
	{ .name = "disable_gless", .bit = 16, },
	{ .name = "nand", .bit = 17, },
	{ .name = "ipsec", .bit = 18, },
	{ },
};

static const struct clk_bcm63xx_table_entry bcm63268_clocks[] = {
	{ .name = "disable_gless", .bit = 0, },
	{ .name = "vdsl_qproc", .bit = 1, },
	{ .name = "vdsl_afe", .bit = 2, },
	{ .name = "vdsl", .bit = 3, },
	{ .name = "mips", .bit = 4, .flags = CLK_IS_CRITICAL, },
	{ .name = "wlan_ocp", .bit = 5, },
	{ .name = "dect", .bit = 6, },
	{ .name = "fap0", .bit = 7, },
	{ .name = "fap1", .bit = 8, },
	{ .name = "sar", .bit = 9, },
	{ .name = "robosw", .bit = 10, },
	{ .name = "pcm", .bit = 11, },
	{ .name = "usbd", .bit = 12, },
	{ .name = "usbh", .bit = 13, },
	{ .name = "ipsec", .bit = 14, },
	{ .name = "spi", .bit = 15, },
	{ .name = "hsspi", .bit = 16, },
	{ .name = "pcie", .bit = 17, },
	{ .name = "phymips", .bit = 18, },
	{ .name = "gmac", .bit = 19, },
	{ .name = "nand", .bit = 20, },
	{ .name = "tbus", .bit = 27, },
	{ .name = "robosw250", .bit = 31, },
	{ },
};

static int clk_bcm63xx_probe(struct platform_device *pdev)
{
	const struct clk_bcm63xx_table_entry *entry, *table;
	struct clk_bcm63xx_hw *hw;
	struct resource *r;
	u8 maxbit = 0;
	int i, ret;

	table = of_device_get_match_data(&pdev->dev);
	if (!table)
		return -EINVAL;

	for (entry = table; entry->name; entry++)
		maxbit = max_t(u8, maxbit, entry->bit);

	hw = devm_kzalloc(&pdev->dev, struct_size(hw, data.hws, maxbit),
			  GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	platform_set_drvdata(pdev, hw);

	spin_lock_init(&hw->lock);

	hw->data.num = maxbit;
	for (i = 0; i < maxbit; i++)
		hw->data.hws[i] = ERR_PTR(-ENODEV);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hw->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(hw->regs))
		return PTR_ERR(hw->regs);

	for (entry = table; entry->name; entry++) {
		struct clk_hw *clk;

		clk = clk_hw_register_gate(&pdev->dev, entry->name, NULL,
					   entry->flags, hw->regs, entry->bit,
					   CLK_GATE_BIG_ENDIAN, &hw->lock);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto out_err;
		}

		hw->data.hws[entry->bit] = clk;
	}

	ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_onecell_get,
				     &hw->data);
	if (!ret)
		return 0;
out_err:
	for (i = 0; i < hw->data.num; i++) {
		if (!IS_ERR(hw->data.hws[i]))
			clk_hw_unregister_gate(hw->data.hws[i]);
	}

	return ret;
}

static int clk_bcm63xx_remove(struct platform_device *pdev)
{
	struct clk_bcm63xx_hw *hw = platform_get_drvdata(pdev);
	int i;

	of_clk_del_provider(pdev->dev.of_node);

	for (i = 0; i < hw->data.num; i++) {
		if (!IS_ERR(hw->data.hws[i]))
			clk_hw_unregister_gate(hw->data.hws[i]);
	}

	return 0;
}

static const struct of_device_id clk_bcm63xx_dt_ids[] = {
	{ .compatible = "brcm,bcm3368-clocks", .data = &bcm3368_clocks, },
	{ .compatible = "brcm,bcm6328-clocks", .data = &bcm6328_clocks, },
	{ .compatible = "brcm,bcm6358-clocks", .data = &bcm6358_clocks, },
	{ .compatible = "brcm,bcm6362-clocks", .data = &bcm6362_clocks, },
	{ .compatible = "brcm,bcm6368-clocks", .data = &bcm6368_clocks, },
	{ .compatible = "brcm,bcm63268-clocks", .data = &bcm63268_clocks, },
	{ }
};

static struct platform_driver clk_bcm63xx = {
	.probe = clk_bcm63xx_probe,
	.remove = clk_bcm63xx_remove,
	.driver = {
		.name = "bcm63xx-clock",
		.of_match_table = clk_bcm63xx_dt_ids,
	},
};
builtin_platform_driver(clk_bcm63xx);
