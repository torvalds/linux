// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <linux/regulator/consumer.h>

#define USB_PHY_UTMI_CTRL0		(0x3c)
#define SLEEPM				BIT(0)

#define USB_PHY_UTMI_CTRL5		(0x50)
#define POR				BIT(1)

#define USB_PHY_HS_PHY_CTRL_COMMON0	(0x54)
#define SIDDQ_SEL			BIT(1)
#define SIDDQ				BIT(2)
#define FSEL				GENMASK(6, 4)
#define FSEL_38_4_MHZ_VAL		(0x6)

#define USB_PHY_HS_PHY_CTRL2		(0x64)
#define USB2_SUSPEND_N			BIT(2)
#define USB2_SUSPEND_N_SEL		BIT(3)

#define USB_PHY_CFG0			(0x94)
#define UTMI_PHY_CMN_CTRL_OVERRIDE_EN	BIT(1)

#define USB_PHY_CFG1			(0x154)
#define PLL_EN				BIT(0)

#define USB_PHY_FSEL_SEL		(0xb8)
#define FSEL_SEL			BIT(0)

#define USB_PHY_XCFGI_39_32		(0x16c)
#define HSTX_PE				GENMASK(3, 2)

#define USB_PHY_XCFGI_71_64		(0x17c)
#define HSTX_SWING			GENMASK(3, 0)

#define USB_PHY_XCFGI_31_24		(0x168)
#define HSTX_SLEW			GENMASK(2, 0)

#define USB_PHY_XCFGI_7_0		(0x15c)
#define PLL_LOCK_TIME			GENMASK(1, 0)

#define M31_EUSB_PHY_INIT_CFG(o, b, v)	\
{				\
	.off = o,		\
	.mask = b,		\
	.val = v,		\
}

struct m31_phy_tbl_entry {
	u32 off;
	u32 mask;
	u32 val;
};

struct m31_eusb2_priv_data {
	const struct m31_phy_tbl_entry	*setup_seq;
	unsigned int			setup_seq_nregs;
	const struct m31_phy_tbl_entry	*override_seq;
	unsigned int			override_seq_nregs;
	const struct m31_phy_tbl_entry	*reset_seq;
	unsigned int			reset_seq_nregs;
	unsigned int			fsel;
};

static const struct m31_phy_tbl_entry m31_eusb2_setup_tbl[] = {
	M31_EUSB_PHY_INIT_CFG(USB_PHY_CFG0, UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_UTMI_CTRL5, POR, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_CFG1, PLL_EN, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_FSEL_SEL, FSEL_SEL, 1),
};

static const struct m31_phy_tbl_entry m31_eusb_phy_override_tbl[] = {
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_39_32, HSTX_PE, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_71_64, HSTX_SWING, 7),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_31_24, HSTX_SLEW, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_7_0, PLL_LOCK_TIME, 0),
};

static const struct m31_phy_tbl_entry m31_eusb_phy_reset_tbl[] = {
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL2, USB2_SUSPEND_N_SEL, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL2, USB2_SUSPEND_N, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_UTMI_CTRL0, SLEEPM, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL_COMMON0, SIDDQ_SEL, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL_COMMON0, SIDDQ, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_UTMI_CTRL5, POR, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL2, USB2_SUSPEND_N_SEL, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_CFG0, UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 0),
};

static const struct regulator_bulk_data m31_eusb_phy_vregs[] = {
	{ .supply = "vdd" },
	{ .supply = "vdda12" },
};

#define M31_EUSB_NUM_VREGS		ARRAY_SIZE(m31_eusb_phy_vregs)

struct m31eusb2_phy {
	struct phy			 *phy;
	void __iomem			 *base;
	const struct m31_eusb2_priv_data *data;
	enum phy_mode			 mode;

	struct regulator_bulk_data	 *vregs;
	struct clk			 *clk;
	struct reset_control		 *reset;

	struct phy			 *repeater;
};

static int m31eusb2_phy_write_readback(void __iomem *base, u32 offset,
				       const u32 mask, u32 val)
{
	u32 write_val;
	u32 tmp;

	tmp = readl(base + offset);
	tmp &= ~mask;
	write_val = tmp | val;

	writel(write_val, base + offset);

	tmp = readl(base + offset);
	tmp &= mask;

	if (tmp != val) {
		pr_err("write: %x to offset: %x FAILED\n", val, offset);
		return -EINVAL;
	}

	return 0;
}

static int m31eusb2_phy_write_sequence(struct m31eusb2_phy *phy,
				       const struct m31_phy_tbl_entry *tbl,
				       int num)
{
	int i;
	int ret;

	for (i = 0 ; i < num; i++, tbl++) {
		dev_dbg(&phy->phy->dev, "Offset:%x BitMask:%x Value:%x",
			tbl->off, tbl->mask, tbl->val);

		ret = m31eusb2_phy_write_readback(phy->base,
						  tbl->off, tbl->mask,
						  tbl->val << __ffs(tbl->mask));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int m31eusb2_phy_set_mode(struct phy *uphy, enum phy_mode mode, int submode)
{
	struct m31eusb2_phy *phy = phy_get_drvdata(uphy);

	phy->mode = mode;

	return phy_set_mode_ext(phy->repeater, mode, submode);
}

static int m31eusb2_phy_init(struct phy *uphy)
{
	struct m31eusb2_phy *phy = phy_get_drvdata(uphy);
	const struct m31_eusb2_priv_data *data = phy->data;
	int ret;

	ret = regulator_bulk_enable(M31_EUSB_NUM_VREGS, phy->vregs);
	if (ret) {
		dev_err(&uphy->dev, "failed to enable regulator, %d\n", ret);
		return ret;
	}

	ret = phy_init(phy->repeater);
	if (ret) {
		dev_err(&uphy->dev, "repeater init failed. %d\n", ret);
		goto disable_vreg;
	}

	ret = clk_prepare_enable(phy->clk);
	if (ret) {
		dev_err(&uphy->dev, "failed to enable ref clock, %d\n", ret);
		goto disable_repeater;
	}

	/* Perform phy reset */
	reset_control_assert(phy->reset);
	udelay(5);
	reset_control_deassert(phy->reset);

	m31eusb2_phy_write_sequence(phy, data->setup_seq, data->setup_seq_nregs);
	m31eusb2_phy_write_readback(phy->base,
				    USB_PHY_HS_PHY_CTRL_COMMON0, FSEL,
				    FIELD_PREP(FSEL, data->fsel));
	m31eusb2_phy_write_sequence(phy, data->override_seq, data->override_seq_nregs);
	m31eusb2_phy_write_sequence(phy, data->reset_seq, data->reset_seq_nregs);

	return 0;

disable_repeater:
	phy_exit(phy->repeater);
disable_vreg:
	regulator_bulk_disable(M31_EUSB_NUM_VREGS, phy->vregs);

	return 0;
}

static int m31eusb2_phy_exit(struct phy *uphy)
{
	struct m31eusb2_phy *phy = phy_get_drvdata(uphy);

	clk_disable_unprepare(phy->clk);
	regulator_bulk_disable(M31_EUSB_NUM_VREGS, phy->vregs);
	phy_exit(phy->repeater);

	return 0;
}

static const struct phy_ops m31eusb2_phy_gen_ops = {
	.init		= m31eusb2_phy_init,
	.exit		= m31eusb2_phy_exit,
	.set_mode	= m31eusb2_phy_set_mode,
	.owner		= THIS_MODULE,
};

static int m31eusb2_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	const struct m31_eusb2_priv_data *data;
	struct device *dev = &pdev->dev;
	struct m31eusb2_phy *phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;
	phy->data = data;

	phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	phy->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(phy->reset))
		return PTR_ERR(phy->reset);

	phy->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(phy->clk))
		return dev_err_probe(dev, PTR_ERR(phy->clk),
				     "failed to get clk\n");

	phy->phy = devm_phy_create(dev, NULL, &m31eusb2_phy_gen_ops);
	if (IS_ERR(phy->phy))
		return dev_err_probe(dev, PTR_ERR(phy->phy),
				     "failed to create phy\n");

	ret = devm_regulator_bulk_get_const(dev, M31_EUSB_NUM_VREGS,
					    m31_eusb_phy_vregs, &phy->vregs);
	if (ret)
		return dev_err_probe(dev, ret,
				"failed to get regulator supplies\n");

	phy_set_drvdata(phy->phy, phy);

	phy->repeater = devm_of_phy_get_by_index(dev, dev->of_node, 0);
	if (IS_ERR(phy->repeater))
		return dev_err_probe(dev, PTR_ERR(phy->repeater),
				     "failed to get repeater\n");

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct m31_eusb2_priv_data m31_eusb_v1_data = {
	.setup_seq = m31_eusb2_setup_tbl,
	.setup_seq_nregs = ARRAY_SIZE(m31_eusb2_setup_tbl),
	.override_seq = m31_eusb_phy_override_tbl,
	.override_seq_nregs = ARRAY_SIZE(m31_eusb_phy_override_tbl),
	.reset_seq = m31_eusb_phy_reset_tbl,
	.reset_seq_nregs = ARRAY_SIZE(m31_eusb_phy_reset_tbl),
	.fsel = FSEL_38_4_MHZ_VAL,
};

static const struct of_device_id m31eusb2_phy_id_table[] = {
	{ .compatible = "qcom,sm8750-m31-eusb2-phy", .data = &m31_eusb_v1_data },
	{ },
};
MODULE_DEVICE_TABLE(of, m31eusb2_phy_id_table);

static struct platform_driver m31eusb2_phy_driver = {
	.probe = m31eusb2_phy_probe,
	.driver = {
		.name = "qcom-m31eusb2-phy",
		.of_match_table = m31eusb2_phy_id_table,
	},
};

module_platform_driver(m31eusb2_phy_driver);

MODULE_AUTHOR("Wesley Cheng <quic_wcheng@quicinc.com>");
MODULE_DESCRIPTION("eUSB2 Qualcomm M31 HSPHY driver");
MODULE_LICENSE("GPL");
