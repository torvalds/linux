// SPDX-License-Identifier: GPL-2.0

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/bcm3368-clock.h>
#include <dt-bindings/clock/bcm6318-clock.h>
#include <dt-bindings/clock/bcm6328-clock.h>
#include <dt-bindings/clock/bcm6358-clock.h>
#include <dt-bindings/clock/bcm6362-clock.h>
#include <dt-bindings/clock/bcm6368-clock.h>
#include <dt-bindings/clock/bcm63268-clock.h>

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
	{
		.name = "mac",
		.bit = BCM3368_CLK_MAC,
	}, {
		.name = "tc",
		.bit = BCM3368_CLK_TC,
	}, {
		.name = "us_top",
		.bit = BCM3368_CLK_US_TOP,
	}, {
		.name = "ds_top",
		.bit = BCM3368_CLK_DS_TOP,
	}, {
		.name = "acm",
		.bit = BCM3368_CLK_ACM,
	}, {
		.name = "spi",
		.bit = BCM3368_CLK_SPI,
	}, {
		.name = "usbs",
		.bit = BCM3368_CLK_USBS,
	}, {
		.name = "bmu",
		.bit = BCM3368_CLK_BMU,
	}, {
		.name = "pcm",
		.bit = BCM3368_CLK_PCM,
	}, {
		.name = "ntp",
		.bit = BCM3368_CLK_NTP,
	}, {
		.name = "acp_b",
		.bit = BCM3368_CLK_ACP_B,
	}, {
		.name = "acp_a",
		.bit = BCM3368_CLK_ACP_A,
	}, {
		.name = "emusb",
		.bit = BCM3368_CLK_EMUSB,
	}, {
		.name = "enet0",
		.bit = BCM3368_CLK_ENET0,
	}, {
		.name = "enet1",
		.bit = BCM3368_CLK_ENET1,
	}, {
		.name = "usbsu",
		.bit = BCM3368_CLK_USBSU,
	}, {
		.name = "ephy",
		.bit = BCM3368_CLK_EPHY,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm6318_clocks[] = {
	{
		.name = "adsl_asb",
		.bit = BCM6318_CLK_ADSL_ASB,
	}, {
		.name = "usb_asb",
		.bit = BCM6318_CLK_USB_ASB,
	}, {
		.name = "mips_asb",
		.bit = BCM6318_CLK_MIPS_ASB,
	}, {
		.name = "pcie_asb",
		.bit = BCM6318_CLK_PCIE_ASB,
	}, {
		.name = "phymips_asb",
		.bit = BCM6318_CLK_PHYMIPS_ASB,
	}, {
		.name = "robosw_asb",
		.bit = BCM6318_CLK_ROBOSW_ASB,
	}, {
		.name = "sar_asb",
		.bit = BCM6318_CLK_SAR_ASB,
	}, {
		.name = "sdr_asb",
		.bit = BCM6318_CLK_SDR_ASB,
	}, {
		.name = "swreg_asb",
		.bit = BCM6318_CLK_SWREG_ASB,
	}, {
		.name = "periph_asb",
		.bit = BCM6318_CLK_PERIPH_ASB,
	}, {
		.name = "cpubus160",
		.bit = BCM6318_CLK_CPUBUS160,
	}, {
		.name = "adsl",
		.bit = BCM6318_CLK_ADSL,
	}, {
		.name = "sar125",
		.bit = BCM6318_CLK_SAR125,
	}, {
		.name = "mips",
		.bit = BCM6318_CLK_MIPS,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "pcie",
		.bit = BCM6318_CLK_PCIE,
	}, {
		.name = "robosw250",
		.bit = BCM6318_CLK_ROBOSW250,
	}, {
		.name = "robosw025",
		.bit = BCM6318_CLK_ROBOSW025,
	}, {
		.name = "sdr",
		.bit = BCM6318_CLK_SDR,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "usbd",
		.bit = BCM6318_CLK_USBD,
	}, {
		.name = "hsspi",
		.bit = BCM6318_CLK_HSSPI,
	}, {
		.name = "pcie25",
		.bit = BCM6318_CLK_PCIE25,
	}, {
		.name = "phymips",
		.bit = BCM6318_CLK_PHYMIPS,
	}, {
		.name = "afe",
		.bit = BCM6318_CLK_AFE,
	}, {
		.name = "qproc",
		.bit = BCM6318_CLK_QPROC,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm6318_ubus_clocks[] = {
	{
		.name = "adsl-ubus",
		.bit = BCM6318_UCLK_ADSL,
	}, {
		.name = "arb-ubus",
		.bit = BCM6318_UCLK_ARB,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "mips-ubus",
		.bit = BCM6318_UCLK_MIPS,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "pcie-ubus",
		.bit = BCM6318_UCLK_PCIE,
	}, {
		.name = "periph-ubus",
		.bit = BCM6318_UCLK_PERIPH,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "phymips-ubus",
		.bit = BCM6318_UCLK_PHYMIPS,
	}, {
		.name = "robosw-ubus",
		.bit = BCM6318_UCLK_ROBOSW,
	}, {
		.name = "sar-ubus",
		.bit = BCM6318_UCLK_SAR,
	}, {
		.name = "sdr-ubus",
		.bit = BCM6318_UCLK_SDR,
	}, {
		.name = "usb-ubus",
		.bit = BCM6318_UCLK_USB,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm6328_clocks[] = {
	{
		.name = "phy_mips",
		.bit = BCM6328_CLK_PHYMIPS,
	}, {
		.name = "adsl_qproc",
		.bit = BCM6328_CLK_ADSL_QPROC,
	}, {
		.name = "adsl_afe",
		.bit = BCM6328_CLK_ADSL_AFE,
	}, {
		.name = "adsl",
		.bit = BCM6328_CLK_ADSL,
	}, {
		.name = "mips",
		.bit = BCM6328_CLK_MIPS,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "sar",
		.bit = BCM6328_CLK_SAR,
	}, {
		.name = "pcm",
		.bit = BCM6328_CLK_PCM,
	}, {
		.name = "usbd",
		.bit = BCM6328_CLK_USBD,
	}, {
		.name = "usbh",
		.bit = BCM6328_CLK_USBH,
	}, {
		.name = "hsspi",
		.bit = BCM6328_CLK_HSSPI,
	}, {
		.name = "pcie",
		.bit = BCM6328_CLK_PCIE,
	}, {
		.name = "robosw",
		.bit = BCM6328_CLK_ROBOSW,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm6358_clocks[] = {
	{
		.name = "enet",
		.bit = BCM6358_CLK_ENET,
	}, {
		.name = "adslphy",
		.bit = BCM6358_CLK_ADSLPHY,
	}, {
		.name = "pcm",
		.bit = BCM6358_CLK_PCM,
	}, {
		.name = "spi",
		.bit = BCM6358_CLK_SPI,
	}, {
		.name = "usbs",
		.bit = BCM6358_CLK_USBS,
	}, {
		.name = "sar",
		.bit = BCM6358_CLK_SAR,
	}, {
		.name = "emusb",
		.bit = BCM6358_CLK_EMUSB,
	}, {
		.name = "enet0",
		.bit = BCM6358_CLK_ENET0,
	}, {
		.name = "enet1",
		.bit = BCM6358_CLK_ENET1,
	}, {
		.name = "usbsu",
		.bit = BCM6358_CLK_USBSU,
	}, {
		.name = "ephy",
		.bit = BCM6358_CLK_EPHY,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm6362_clocks[] = {
	{
		.name = "adsl_qproc",
		.bit = BCM6362_CLK_ADSL_QPROC,
	}, {
		.name = "adsl_afe",
		.bit = BCM6362_CLK_ADSL_AFE,
	}, {
		.name = "adsl",
		.bit = BCM6362_CLK_ADSL,
	}, {
		.name = "mips",
		.bit = BCM6362_CLK_MIPS,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "wlan_ocp",
		.bit = BCM6362_CLK_WLAN_OCP,
	}, {
		.name = "swpkt_usb",
		.bit = BCM6362_CLK_SWPKT_USB,
	}, {
		.name = "swpkt_sar",
		.bit = BCM6362_CLK_SWPKT_SAR,
	}, {
		.name = "sar",
		.bit = BCM6362_CLK_SAR,
	}, {
		.name = "robosw",
		.bit = BCM6362_CLK_ROBOSW,
	}, {
		.name = "pcm",
		.bit = BCM6362_CLK_PCM,
	}, {
		.name = "usbd",
		.bit = BCM6362_CLK_USBD,
	}, {
		.name = "usbh",
		.bit = BCM6362_CLK_USBH,
	}, {
		.name = "ipsec",
		.bit = BCM6362_CLK_IPSEC,
	}, {
		.name = "spi",
		.bit = BCM6362_CLK_SPI,
	}, {
		.name = "hsspi",
		.bit = BCM6362_CLK_HSSPI,
	}, {
		.name = "pcie",
		.bit = BCM6362_CLK_PCIE,
	}, {
		.name = "fap",
		.bit = BCM6362_CLK_FAP,
	}, {
		.name = "phymips",
		.bit = BCM6362_CLK_PHYMIPS,
	}, {
		.name = "nand",
		.bit = BCM6362_CLK_NAND,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm6368_clocks[] = {
	{
		.name = "vdsl_qproc",
		.bit = BCM6368_CLK_VDSL_QPROC,
	}, {
		.name = "vdsl_afe",
		.bit = BCM6368_CLK_VDSL_AFE,
	}, {
		.name = "vdsl_bonding",
		.bit = BCM6368_CLK_VDSL_BONDING,
	}, {
		.name = "vdsl",
		.bit = BCM6368_CLK_VDSL,
	}, {
		.name = "phymips",
		.bit = BCM6368_CLK_PHYMIPS,
	}, {
		.name = "swpkt_usb",
		.bit = BCM6368_CLK_SWPKT_USB,
	}, {
		.name = "swpkt_sar",
		.bit = BCM6368_CLK_SWPKT_SAR,
	}, {
		.name = "spi",
		.bit = BCM6368_CLK_SPI,
	}, {
		.name = "usbd",
		.bit = BCM6368_CLK_USBD,
	}, {
		.name = "sar",
		.bit = BCM6368_CLK_SAR,
	}, {
		.name = "robosw",
		.bit = BCM6368_CLK_ROBOSW,
	}, {
		.name = "utopia",
		.bit = BCM6368_CLK_UTOPIA,
	}, {
		.name = "pcm",
		.bit = BCM6368_CLK_PCM,
	}, {
		.name = "usbh",
		.bit = BCM6368_CLK_USBH,
	}, {
		.name = "disable_gless",
		.bit = BCM6368_CLK_DIS_GLESS,
	}, {
		.name = "nand",
		.bit = BCM6368_CLK_NAND,
	}, {
		.name = "ipsec",
		.bit = BCM6368_CLK_IPSEC,
	}, {
		/* sentinel */
	},
};

static const struct clk_bcm63xx_table_entry bcm63268_clocks[] = {
	{
		.name = "disable_gless",
		.bit = BCM63268_CLK_DIS_GLESS,
	}, {
		.name = "vdsl_qproc",
		.bit = BCM63268_CLK_VDSL_QPROC,
	}, {
		.name = "vdsl_afe",
		.bit = BCM63268_CLK_VDSL_AFE,
	}, {
		.name = "vdsl",
		.bit = BCM63268_CLK_VDSL,
	}, {
		.name = "mips",
		.bit = BCM63268_CLK_MIPS,
		.flags = CLK_IS_CRITICAL,
	}, {
		.name = "wlan_ocp",
		.bit = BCM63268_CLK_WLAN_OCP,
	}, {
		.name = "dect",
		.bit = BCM63268_CLK_DECT,
	}, {
		.name = "fap0",
		.bit = BCM63268_CLK_FAP0,
	}, {
		.name = "fap1",
		.bit = BCM63268_CLK_FAP1,
	}, {
		.name = "sar",
		.bit = BCM63268_CLK_SAR,
	}, {
		.name = "robosw",
		.bit = BCM63268_CLK_ROBOSW,
	}, {
		.name = "pcm",
		.bit = BCM63268_CLK_PCM,
	}, {
		.name = "usbd",
		.bit = BCM63268_CLK_USBD,
	}, {
		.name = "usbh",
		.bit = BCM63268_CLK_USBH,
	}, {
		.name = "ipsec",
		.bit = BCM63268_CLK_IPSEC,
	}, {
		.name = "spi",
		.bit = BCM63268_CLK_SPI,
	}, {
		.name = "hsspi",
		.bit = BCM63268_CLK_HSSPI,
	}, {
		.name = "pcie",
		.bit = BCM63268_CLK_PCIE,
	}, {
		.name = "phymips",
		.bit = BCM63268_CLK_PHYMIPS,
	}, {
		.name = "gmac",
		.bit = BCM63268_CLK_GMAC,
	}, {
		.name = "nand",
		.bit = BCM63268_CLK_NAND,
	}, {
		.name = "tbus",
		.bit = BCM63268_CLK_TBUS,
	}, {
		.name = "robosw250",
		.bit = BCM63268_CLK_ROBOSW250,
	}, {
		/* sentinel */
	},
};

static int clk_bcm63xx_probe(struct platform_device *pdev)
{
	const struct clk_bcm63xx_table_entry *entry, *table;
	struct clk_bcm63xx_hw *hw;
	u8 maxbit = 0;
	int i, ret;

	table = of_device_get_match_data(&pdev->dev);
	if (!table)
		return -EINVAL;

	for (entry = table; entry->name; entry++)
		maxbit = max_t(u8, maxbit, entry->bit);
	maxbit++;

	hw = devm_kzalloc(&pdev->dev, struct_size(hw, data.hws, maxbit),
			  GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	platform_set_drvdata(pdev, hw);

	spin_lock_init(&hw->lock);

	hw->data.num = maxbit;
	for (i = 0; i < maxbit; i++)
		hw->data.hws[i] = ERR_PTR(-ENODEV);

	hw->regs = devm_platform_ioremap_resource(pdev, 0);
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
	{ .compatible = "brcm,bcm6318-clocks", .data = &bcm6318_clocks, },
	{ .compatible = "brcm,bcm6318-ubus-clocks", .data = &bcm6318_ubus_clocks, },
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
