// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel ThunderBay eMMC PHY driver
 *
 * Copyright (C) 2021 Intel Corporation
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

/* eMMC/SD/SDIO core/phy configuration registers */
#define CTRL_CFG_0	0x00
#define CTRL_CFG_1	0x04
#define CTRL_PRESET_0	0x08
#define CTRL_PRESET_1	0x0c
#define CTRL_PRESET_2	0x10
#define CTRL_PRESET_3	0x14
#define CTRL_PRESET_4	0x18
#define CTRL_CFG_2	0x1c
#define CTRL_CFG_3	0x20
#define PHY_CFG_0	0x24
#define PHY_CFG_1	0x28
#define PHY_CFG_2	0x2c
#define PHYBIST_CTRL	0x30
#define SDHC_STAT3	0x34
#define PHY_STAT	0x38
#define PHYBIST_STAT_0	0x3c
#define PHYBIST_STAT_1	0x40
#define EMMC_AXI        0x44

/* CTRL_PRESET_3 */
#define CTRL_PRESET3_MASK	GENMASK(31, 0)
#define CTRL_PRESET3_SHIFT	0

/* CTRL_CFG_0 bit fields */
#define SUPPORT_HS_MASK		BIT(26)
#define SUPPORT_HS_SHIFT	26

#define SUPPORT_8B_MASK		BIT(24)
#define SUPPORT_8B_SHIFT	24

/* CTRL_CFG_1 bit fields */
#define SUPPORT_SDR50_MASK	BIT(28)
#define SUPPORT_SDR50_SHIFT	28
#define SLOT_TYPE_MASK		GENMASK(27, 26)
#define SLOT_TYPE_OFFSET	26
#define SUPPORT_64B_MASK	BIT(24)
#define SUPPORT_64B_SHIFT	24
#define SUPPORT_HS400_MASK	BIT(2)
#define SUPPORT_HS400_SHIFT	2
#define SUPPORT_DDR50_MASK	BIT(1)
#define SUPPORT_DDR50_SHIFT	1
#define SUPPORT_SDR104_MASK	BIT(0)
#define SUPPORT_SDR104_SHIFT	0

/* PHY_CFG_0 bit fields */
#define SEL_DLY_TXCLK_MASK      BIT(29)
#define SEL_DLY_TXCLK_SHIFT	29
#define SEL_DLY_RXCLK_MASK      BIT(28)
#define SEL_DLY_RXCLK_SHIFT	28

#define OTAP_DLY_ENA_MASK	BIT(27)
#define OTAP_DLY_ENA_SHIFT	27
#define OTAP_DLY_SEL_MASK	GENMASK(26, 23)
#define OTAP_DLY_SEL_SHIFT	23
#define ITAP_CHG_WIN_MASK	BIT(22)
#define ITAP_CHG_WIN_SHIFT	22
#define ITAP_DLY_ENA_MASK	BIT(21)
#define ITAP_DLY_ENA_SHIFT	21
#define ITAP_DLY_SEL_MASK	GENMASK(20, 16)
#define ITAP_DLY_SEL_SHIFT	16
#define RET_ENB_MASK		BIT(15)
#define RET_ENB_SHIFT		15
#define RET_EN_MASK		BIT(14)
#define RET_EN_SHIFT		14
#define DLL_IFF_MASK		GENMASK(13, 11)
#define DLL_IFF_SHIFT		11
#define DLL_EN_MASK		BIT(10)
#define DLL_EN_SHIFT		10
#define DLL_TRIM_ICP_MASK	GENMASK(9, 6)
#define DLL_TRIM_ICP_SHIFT	6
#define RETRIM_EN_MASK		BIT(5)
#define RETRIM_EN_SHIFT		5
#define RETRIM_MASK		BIT(4)
#define RETRIM_SHIFT		4
#define DR_TY_MASK		GENMASK(3, 1)
#define DR_TY_SHIFT		1
#define PWR_DOWN_MASK		BIT(0)
#define PWR_DOWN_SHIFT		0

/* PHY_CFG_1 bit fields */
#define REN_DAT_MASK		GENMASK(19, 12)
#define REN_DAT_SHIFT		12
#define REN_CMD_MASK		BIT(11)
#define REN_CMD_SHIFT		11
#define REN_STRB_MASK		BIT(10)
#define REN_STRB_SHIFT		10
#define PU_STRB_MASK		BIT(20)
#define PU_STRB_SHIFT		20

/* PHY_CFG_2 bit fields */
#define CLKBUF_MASK		GENMASK(24, 21)
#define CLKBUF_SHIFT		21
#define SEL_STRB_MASK		GENMASK(20, 13)
#define SEL_STRB_SHIFT		13
#define SEL_FREQ_MASK		GENMASK(12, 10)
#define SEL_FREQ_SHIFT		10

/* PHY_STAT bit fields */
#define CAL_DONE		BIT(6)
#define DLL_RDY			BIT(5)

#define OTAP_DLY		0x0
#define ITAP_DLY		0x0
#define STRB			0x33

/* From ACS_eMMC51_16nFFC_RO1100_Userguide_v1p0.pdf p17 */
#define FREQSEL_200M_170M	0x0
#define FREQSEL_170M_140M	0x1
#define FREQSEL_140M_110M	0x2
#define FREQSEL_110M_80M	0x3
#define FREQSEL_80M_50M		0x4
#define FREQSEL_275M_250M	0x5
#define FREQSEL_250M_225M	0x6
#define FREQSEL_225M_200M	0x7

/* Phy power status */
#define PHY_UNINITIALIZED	0
#define PHY_INITIALIZED		1

/*
 * During init(400KHz) phy_settings will be called with 200MHZ clock
 * To avoid incorrectly setting the phy for init(400KHZ) "phy_power_sts" is used.
 * When actual clock is set always phy is powered off once and then powered on.
 * (sdhci_arasan_set_clock). That feature will be used to identify whether the
 * settings are for init phy_power_on or actual clock phy_power_on
 * 0 --> init settings
 * 1 --> actual settings
 */

struct thunderbay_emmc_phy {
	void __iomem    *reg_base;
	struct clk      *emmcclk;
	int phy_power_sts;
};

static inline void update_reg(struct thunderbay_emmc_phy *tbh_phy, u32 offset,
			      u32 mask, u32 shift, u32 val)
{
	u32 tmp;

	tmp = readl(tbh_phy->reg_base + offset);
	tmp &= ~mask;
	tmp |= val << shift;
	writel(tmp, tbh_phy->reg_base + offset);
}

static int thunderbay_emmc_phy_power(struct phy *phy, bool power_on)
{
	struct thunderbay_emmc_phy *tbh_phy = phy_get_drvdata(phy);
	unsigned int freqsel = FREQSEL_200M_170M;
	unsigned long rate;
	static int lock;
	u32 val;
	int ret;

	/* Disable DLL */
	rate = clk_get_rate(tbh_phy->emmcclk);
	switch (rate) {
	case 200000000:
		/* lock dll only when it is used, i.e only if SEL_DLY_TXCLK/RXCLK are 0 */
		update_reg(tbh_phy, PHY_CFG_0, DLL_EN_MASK, DLL_EN_SHIFT, 0x0);
		break;

	/* dll lock not required for other frequencies */
	case 50000000 ... 52000000:
	case 400000:
	default:
		break;
	}

	if (!power_on)
		return 0;

	rate = clk_get_rate(tbh_phy->emmcclk);
	switch (rate) {
	case 170000001 ... 200000000:
		freqsel = FREQSEL_200M_170M;
		break;

	case 140000001 ... 170000000:
		freqsel = FREQSEL_170M_140M;
		break;

	case 110000001 ... 140000000:
		freqsel = FREQSEL_140M_110M;
		break;

	case 80000001 ... 110000000:
		freqsel = FREQSEL_110M_80M;
		break;

	case 50000000 ... 80000000:
		freqsel = FREQSEL_80M_50M;
		break;

	case 250000001 ... 275000000:
		freqsel = FREQSEL_275M_250M;
		break;

	case 225000001 ... 250000000:
		freqsel = FREQSEL_250M_225M;
		break;

	case 200000001 ... 225000000:
		freqsel = FREQSEL_225M_200M;
		break;
	default:
		break;
	}
	/* Clock rate is checked against upper limit. It may fall low during init */
	if (rate > 200000000)
		dev_warn(&phy->dev, "Unsupported rate: %lu\n", rate);

	udelay(5);

	if (lock == 0) {
		/* PDB will be done only once per boot */
		update_reg(tbh_phy, PHY_CFG_0, PWR_DOWN_MASK,
			   PWR_DOWN_SHIFT, 0x1);
		lock = 1;
		/*
		 * According to the user manual, it asks driver to wait 5us for
		 * calpad busy trimming. However it is documented that this value is
		 * PVT(A.K.A. process, voltage and temperature) relevant, so some
		 * failure cases are found which indicates we should be more tolerant
		 * to calpad busy trimming.
		 */
		ret = readl_poll_timeout(tbh_phy->reg_base + PHY_STAT,
					 val, (val & CAL_DONE), 10, 50);
		if (ret) {
			dev_err(&phy->dev, "caldone failed, ret=%d\n", ret);
			return ret;
		}
	}
	rate = clk_get_rate(tbh_phy->emmcclk);
	switch (rate) {
	case 200000000:
		/* Set frequency of the DLL operation */
		update_reg(tbh_phy, PHY_CFG_2, SEL_FREQ_MASK, SEL_FREQ_SHIFT, freqsel);

		/* Enable DLL */
		update_reg(tbh_phy, PHY_CFG_0, DLL_EN_MASK, DLL_EN_SHIFT, 0x1);

		/*
		 * After enabling analog DLL circuits docs say that we need 10.2 us if
		 * our source clock is at 50 MHz and that lock time scales linearly
		 * with clock speed. If we are powering on the PHY and the card clock
		 * is super slow (like 100kHz) this could take as long as 5.1 ms as
		 * per the math: 10.2 us * (50000000 Hz / 100000 Hz) => 5.1 ms
		 * hopefully we won't be running at 100 kHz, but we should still make
		 * sure we wait long enough.
		 *
		 * NOTE: There appear to be corner cases where the DLL seems to take
		 * extra long to lock for reasons that aren't understood. In some
		 * extreme cases we've seen it take up to over 10ms (!). We'll be
		 * generous and give it 50ms.
		 */
		ret = readl_poll_timeout(tbh_phy->reg_base + PHY_STAT,
					 val, (val & DLL_RDY), 10, 50 * USEC_PER_MSEC);
		if (ret) {
			dev_err(&phy->dev, "dllrdy failed, ret=%d\n", ret);
			return ret;
		}
		break;

	default:
		break;
	}
	return 0;
}

static int thunderbay_emmc_phy_init(struct phy *phy)
{
	struct thunderbay_emmc_phy *tbh_phy = phy_get_drvdata(phy);

	tbh_phy->emmcclk = clk_get(&phy->dev, "emmcclk");

	return PTR_ERR_OR_ZERO(tbh_phy->emmcclk);
}

static int thunderbay_emmc_phy_exit(struct phy *phy)
{
	struct thunderbay_emmc_phy *tbh_phy = phy_get_drvdata(phy);

	clk_put(tbh_phy->emmcclk);

	return 0;
}

static int thunderbay_emmc_phy_power_on(struct phy *phy)
{
	struct thunderbay_emmc_phy *tbh_phy = phy_get_drvdata(phy);
	unsigned long rate;

	/* Overwrite capability bits configurable in bootloader */
	update_reg(tbh_phy, CTRL_CFG_0,
		   SUPPORT_HS_MASK, SUPPORT_HS_SHIFT, 0x1);
	update_reg(tbh_phy, CTRL_CFG_0,
		   SUPPORT_8B_MASK, SUPPORT_8B_SHIFT, 0x1);
	update_reg(tbh_phy, CTRL_CFG_1,
		   SUPPORT_SDR50_MASK, SUPPORT_SDR50_SHIFT, 0x1);
	update_reg(tbh_phy, CTRL_CFG_1,
		   SUPPORT_DDR50_MASK, SUPPORT_DDR50_SHIFT, 0x1);
	update_reg(tbh_phy, CTRL_CFG_1,
		   SUPPORT_SDR104_MASK, SUPPORT_SDR104_SHIFT, 0x1);
	update_reg(tbh_phy, CTRL_CFG_1,
		   SUPPORT_HS400_MASK, SUPPORT_HS400_SHIFT, 0x1);
	update_reg(tbh_phy, CTRL_CFG_1,
		   SUPPORT_64B_MASK, SUPPORT_64B_SHIFT, 0x1);

	if (tbh_phy->phy_power_sts == PHY_UNINITIALIZED) {
		/* Indicates initialization, settings for init, same as 400KHZ setting */
		update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_TXCLK_MASK, SEL_DLY_TXCLK_SHIFT, 0x1);
		update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_RXCLK_MASK, SEL_DLY_RXCLK_SHIFT, 0x1);
		update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_ENA_MASK, ITAP_DLY_ENA_SHIFT, 0x0);
		update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_SEL_MASK, ITAP_DLY_SEL_SHIFT, 0x0);
		update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_ENA_MASK, OTAP_DLY_ENA_SHIFT, 0x0);
		update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_SEL_MASK, OTAP_DLY_SEL_SHIFT, 0);
		update_reg(tbh_phy, PHY_CFG_0, DLL_TRIM_ICP_MASK, DLL_TRIM_ICP_SHIFT, 0);
		update_reg(tbh_phy, PHY_CFG_0, DR_TY_MASK, DR_TY_SHIFT, 0x1);

	} else if (tbh_phy->phy_power_sts == PHY_INITIALIZED) {
		/* Indicates actual clock setting */
		rate = clk_get_rate(tbh_phy->emmcclk);
		switch (rate) {
		case 200000000:
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_TXCLK_MASK,
				   SEL_DLY_TXCLK_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_RXCLK_MASK,
				   SEL_DLY_RXCLK_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_ENA_MASK,
				   ITAP_DLY_ENA_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_SEL_MASK,
				   ITAP_DLY_SEL_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_ENA_MASK,
				   OTAP_DLY_ENA_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_SEL_MASK,
				   OTAP_DLY_SEL_SHIFT, 2);
			update_reg(tbh_phy, PHY_CFG_0, DLL_TRIM_ICP_MASK,
				   DLL_TRIM_ICP_SHIFT, 0x8);
			update_reg(tbh_phy, PHY_CFG_0, DR_TY_MASK,
				   DR_TY_SHIFT, 0x1);
			/* For HS400 only */
			update_reg(tbh_phy, PHY_CFG_2, SEL_STRB_MASK,
				   SEL_STRB_SHIFT, STRB);
			break;

		case 50000000 ... 52000000:
			/* For both HS and DDR52 this setting works */
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_TXCLK_MASK,
				   SEL_DLY_TXCLK_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_RXCLK_MASK,
				   SEL_DLY_RXCLK_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_ENA_MASK,
				   ITAP_DLY_ENA_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_SEL_MASK,
				   ITAP_DLY_SEL_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_ENA_MASK,
				   OTAP_DLY_ENA_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_SEL_MASK,
				   OTAP_DLY_SEL_SHIFT, 4);
			update_reg(tbh_phy, PHY_CFG_0, DLL_TRIM_ICP_MASK,
				   DLL_TRIM_ICP_SHIFT, 0x8);
			update_reg(tbh_phy, PHY_CFG_0,
				   DR_TY_MASK, DR_TY_SHIFT, 0x1);
			break;

		case 400000:
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_TXCLK_MASK,
				   SEL_DLY_TXCLK_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_RXCLK_MASK,
				   SEL_DLY_RXCLK_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_ENA_MASK,
				   ITAP_DLY_ENA_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_SEL_MASK,
				   ITAP_DLY_SEL_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_ENA_MASK,
				   OTAP_DLY_ENA_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_SEL_MASK,
				   OTAP_DLY_SEL_SHIFT, 0);
			update_reg(tbh_phy, PHY_CFG_0, DLL_TRIM_ICP_MASK,
				   DLL_TRIM_ICP_SHIFT, 0);
			update_reg(tbh_phy, PHY_CFG_0, DR_TY_MASK, DR_TY_SHIFT, 0x1);
			break;

		default:
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_TXCLK_MASK,
				   SEL_DLY_TXCLK_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, SEL_DLY_RXCLK_MASK,
				   SEL_DLY_RXCLK_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_ENA_MASK,
				   ITAP_DLY_ENA_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, ITAP_DLY_SEL_MASK,
				   ITAP_DLY_SEL_SHIFT, 0x0);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_ENA_MASK,
				   OTAP_DLY_ENA_SHIFT, 0x1);
			update_reg(tbh_phy, PHY_CFG_0, OTAP_DLY_SEL_MASK,
				   OTAP_DLY_SEL_SHIFT, 2);
			update_reg(tbh_phy, PHY_CFG_0, DLL_TRIM_ICP_MASK,
				   DLL_TRIM_ICP_SHIFT, 0x8);
			update_reg(tbh_phy, PHY_CFG_0, DR_TY_MASK,
				   DR_TY_SHIFT, 0x1);
			break;
		}
		/* Reset, init seq called without phy_power_off, this indicates init seq */
		tbh_phy->phy_power_sts = PHY_UNINITIALIZED;
	}

	update_reg(tbh_phy, PHY_CFG_0, RETRIM_EN_MASK, RETRIM_EN_SHIFT, 0x1);
	update_reg(tbh_phy, PHY_CFG_0, RETRIM_MASK, RETRIM_SHIFT, 0x0);

	return thunderbay_emmc_phy_power(phy, 1);
}

static int thunderbay_emmc_phy_power_off(struct phy *phy)
{
	struct thunderbay_emmc_phy *tbh_phy = phy_get_drvdata(phy);

	tbh_phy->phy_power_sts = PHY_INITIALIZED;

	return thunderbay_emmc_phy_power(phy, 0);
}

static const struct phy_ops thunderbay_emmc_phy_ops = {
	.init		= thunderbay_emmc_phy_init,
	.exit		= thunderbay_emmc_phy_exit,
	.power_on	= thunderbay_emmc_phy_power_on,
	.power_off	= thunderbay_emmc_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id thunderbay_emmc_phy_of_match[] = {
	{ .compatible = "intel,thunderbay-emmc-phy",
		(void *)&thunderbay_emmc_phy_ops },
	{}
};
MODULE_DEVICE_TABLE(of, thunderbay_emmc_phy_of_match);

static int thunderbay_emmc_phy_probe(struct platform_device *pdev)
{
	struct thunderbay_emmc_phy *tbh_phy;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	const struct of_device_id *id;
	struct phy *generic_phy;
	struct resource *res;

	if (!dev->of_node)
		return -ENODEV;

	tbh_phy = devm_kzalloc(dev, sizeof(*tbh_phy), GFP_KERNEL);
	if (!tbh_phy)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tbh_phy->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tbh_phy->reg_base))
		return PTR_ERR(tbh_phy->reg_base);

	tbh_phy->phy_power_sts = PHY_UNINITIALIZED;
	id = of_match_node(thunderbay_emmc_phy_of_match, pdev->dev.of_node);
	if (!id) {
		dev_err(dev, "failed to get match_node\n");
		return -EINVAL;
	}

	generic_phy = devm_phy_create(dev, dev->of_node, id->data);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, tbh_phy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver thunderbay_emmc_phy_driver = {
	.probe		 = thunderbay_emmc_phy_probe,
	.driver		 = {
		.name	 = "thunderbay-emmc-phy",
		.of_match_table = thunderbay_emmc_phy_of_match,
	},
};
module_platform_driver(thunderbay_emmc_phy_driver);

MODULE_AUTHOR("Nandhini S <nandhini.srikandan@intel.com>");
MODULE_AUTHOR("Rashmi A <rashmi.a@intel.com>");
MODULE_DESCRIPTION("Intel Thunder Bay eMMC PHY driver");
MODULE_LICENSE("GPL v2");
