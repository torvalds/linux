// SPDX-License-Identifier: GPL-2.0-only

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/bitfield.h>

/* USB QSCRATCH Hardware registers */
#define QSCRATCH_GENERAL_CFG		(0x08)
#define HSUSB_PHY_CTRL_REG		(0x10)

/* PHY_CTRL_REG */
#define HSUSB_CTRL_DMSEHV_CLAMP		BIT(24)
#define HSUSB_CTRL_USB2_SUSPEND		BIT(23)
#define HSUSB_CTRL_UTMI_CLK_EN		BIT(21)
#define HSUSB_CTRL_UTMI_OTG_VBUS_VALID	BIT(20)
#define HSUSB_CTRL_USE_CLKCORE		BIT(18)
#define HSUSB_CTRL_DPSEHV_CLAMP		BIT(17)
#define HSUSB_CTRL_COMMONONN		BIT(11)
#define HSUSB_CTRL_ID_HV_CLAMP		BIT(9)
#define HSUSB_CTRL_OTGSESSVLD_CLAMP	BIT(8)
#define HSUSB_CTRL_CLAMP_EN		BIT(7)
#define HSUSB_CTRL_RETENABLEN		BIT(1)
#define HSUSB_CTRL_POR			BIT(0)

/* QSCRATCH_GENERAL_CFG */
#define HSUSB_GCFG_XHCI_REV		BIT(2)

/* USB QSCRATCH Hardware registers */
#define SSUSB_PHY_CTRL_REG		(0x00)
#define SSUSB_PHY_PARAM_CTRL_1		(0x04)
#define SSUSB_PHY_PARAM_CTRL_2		(0x08)
#define CR_PROTOCOL_DATA_IN_REG		(0x0c)
#define CR_PROTOCOL_DATA_OUT_REG	(0x10)
#define CR_PROTOCOL_CAP_ADDR_REG	(0x14)
#define CR_PROTOCOL_CAP_DATA_REG	(0x18)
#define CR_PROTOCOL_READ_REG		(0x1c)
#define CR_PROTOCOL_WRITE_REG		(0x20)

/* PHY_CTRL_REG */
#define SSUSB_CTRL_REF_USE_PAD		BIT(28)
#define SSUSB_CTRL_TEST_POWERDOWN	BIT(27)
#define SSUSB_CTRL_LANE0_PWR_PRESENT	BIT(24)
#define SSUSB_CTRL_SS_PHY_EN		BIT(8)
#define SSUSB_CTRL_SS_PHY_RESET		BIT(7)

/* SSPHY control registers - Does this need 0x30? */
#define SSPHY_CTRL_RX_OVRD_IN_HI(lane)	(0x1006 + 0x100 * (lane))
#define SSPHY_CTRL_TX_OVRD_DRV_LO(lane)	(0x1002 + 0x100 * (lane))

/* SSPHY SoC version specific values */
#define SSPHY_RX_EQ_VALUE		4 /* Override value for rx_eq */
/* Override value for transmit preemphasis */
#define SSPHY_TX_DEEMPH_3_5DB		23
/* Override value for mpll */
#define SSPHY_MPLL_VALUE		0

/* QSCRATCH PHY_PARAM_CTRL1 fields */
#define PHY_PARAM_CTRL1_TX_FULL_SWING_MASK	GENMASK(26, 19)
#define PHY_PARAM_CTRL1_TX_DEEMPH_6DB_MASK	GENMASK(19, 13)
#define PHY_PARAM_CTRL1_TX_DEEMPH_3_5DB_MASK	GENMASK(13, 7)
#define PHY_PARAM_CTRL1_LOS_BIAS_MASK		GENMASK(7, 2)

#define PHY_PARAM_CTRL1_MASK				\
		(PHY_PARAM_CTRL1_TX_FULL_SWING_MASK |	\
		 PHY_PARAM_CTRL1_TX_DEEMPH_6DB_MASK |	\
		 PHY_PARAM_CTRL1_TX_DEEMPH_3_5DB_MASK |	\
		 PHY_PARAM_CTRL1_LOS_BIAS_MASK)

#define PHY_PARAM_CTRL1_TX_FULL_SWING(x)	\
		FIELD_PREP(PHY_PARAM_CTRL1_TX_FULL_SWING_MASK, (x))
#define PHY_PARAM_CTRL1_TX_DEEMPH_6DB(x)	\
		FIELD_PREP(PHY_PARAM_CTRL1_TX_DEEMPH_6DB_MASK, (x))
#define PHY_PARAM_CTRL1_TX_DEEMPH_3_5DB(x)	\
		FIELD_PREP(PHY_PARAM_CTRL1_TX_DEEMPH_3_5DB_MASK, x)
#define PHY_PARAM_CTRL1_LOS_BIAS(x)	\
		FIELD_PREP(PHY_PARAM_CTRL1_LOS_BIAS_MASK, (x))

/* RX OVRD IN HI bits */
#define RX_OVRD_IN_HI_RX_RESET_OVRD		BIT(13)
#define RX_OVRD_IN_HI_RX_RX_RESET		BIT(12)
#define RX_OVRD_IN_HI_RX_EQ_OVRD		BIT(11)
#define RX_OVRD_IN_HI_RX_EQ_MASK		GENMASK(10, 7)
#define RX_OVRD_IN_HI_RX_EQ(x)			FIELD_PREP(RX_OVRD_IN_HI_RX_EQ_MASK, (x))
#define RX_OVRD_IN_HI_RX_EQ_EN_OVRD		BIT(7)
#define RX_OVRD_IN_HI_RX_EQ_EN			BIT(6)
#define RX_OVRD_IN_HI_RX_LOS_FILTER_OVRD	BIT(5)
#define RX_OVRD_IN_HI_RX_LOS_FILTER_MASK	GENMASK(4, 2)
#define RX_OVRD_IN_HI_RX_RATE_OVRD		BIT(2)
#define RX_OVRD_IN_HI_RX_RATE_MASK		GENMASK(2, 0)

/* TX OVRD DRV LO register bits */
#define TX_OVRD_DRV_LO_AMPLITUDE_MASK		GENMASK(6, 0)
#define TX_OVRD_DRV_LO_PREEMPH_MASK		GENMASK(13, 6)
#define TX_OVRD_DRV_LO_PREEMPH(x)		((x) << 7)
#define TX_OVRD_DRV_LO_EN			BIT(14)

/* MPLL bits */
#define SSPHY_MPLL_MASK				GENMASK(8, 5)
#define SSPHY_MPLL(x)				((x) << 5)

/* SS CAP register bits */
#define SS_CR_CAP_ADDR_REG			BIT(0)
#define SS_CR_CAP_DATA_REG			BIT(0)
#define SS_CR_READ_REG				BIT(0)
#define SS_CR_WRITE_REG				BIT(0)

#define LATCH_SLEEP				40
#define LATCH_TIMEOUT				100

struct usb_phy {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*xo_clk;
	struct clk		*ref_clk;
	u32			rx_eq;
	u32			tx_deamp_3_5db;
	u32			mpll;
};

struct phy_drvdata {
	struct phy_ops	ops;
	u32		clk_rate;
};

/**
 * usb_phy_write_readback() - Write register and read back masked value to
 * confirm it is written
 *
 * @phy_dwc3: QCOM DWC3 phy context
 * @offset: register offset.
 * @mask: register bitmask specifying what should be updated
 * @val: value to write.
 */
static inline void usb_phy_write_readback(struct usb_phy *phy_dwc3,
					  u32 offset,
					  const u32 mask, u32 val)
{
	u32 write_val, tmp = readl(phy_dwc3->base + offset);

	tmp &= ~mask;		/* retain other bits */
	write_val = tmp | val;

	writel(write_val, phy_dwc3->base + offset);

	/* Read back to see if val was written */
	tmp = readl(phy_dwc3->base + offset);
	tmp &= mask;		/* clear other bits */

	if (tmp != val)
		dev_err(phy_dwc3->dev, "write: %x to QSCRATCH: %x FAILED\n", val, offset);
}

static int wait_for_latch(void __iomem *addr)
{
	u32 val;

	return readl_poll_timeout(addr, val, !val, LATCH_SLEEP, LATCH_TIMEOUT);
}

/**
 * usb_ss_write_phycreg() - Write SSPHY register
 *
 * @phy_dwc3: QCOM DWC3 phy context
 * @addr: SSPHY address to write.
 * @val: value to write.
 */
static int usb_ss_write_phycreg(struct usb_phy *phy_dwc3,
				u32 addr, u32 val)
{
	int ret;

	writel(addr, phy_dwc3->base + CR_PROTOCOL_DATA_IN_REG);
	writel(SS_CR_CAP_ADDR_REG,
	       phy_dwc3->base + CR_PROTOCOL_CAP_ADDR_REG);

	ret = wait_for_latch(phy_dwc3->base + CR_PROTOCOL_CAP_ADDR_REG);
	if (ret)
		goto err_wait;

	writel(val, phy_dwc3->base + CR_PROTOCOL_DATA_IN_REG);
	writel(SS_CR_CAP_DATA_REG,
	       phy_dwc3->base + CR_PROTOCOL_CAP_DATA_REG);

	ret = wait_for_latch(phy_dwc3->base + CR_PROTOCOL_CAP_DATA_REG);
	if (ret)
		goto err_wait;

	writel(SS_CR_WRITE_REG, phy_dwc3->base + CR_PROTOCOL_WRITE_REG);

	ret = wait_for_latch(phy_dwc3->base + CR_PROTOCOL_WRITE_REG);

err_wait:
	if (ret)
		dev_err(phy_dwc3->dev, "timeout waiting for latch\n");
	return ret;
}

/**
 * usb_ss_read_phycreg() - Read SSPHY register.
 *
 * @phy_dwc3: QCOM DWC3 phy context
 * @addr: SSPHY address to read.
 * @val: pointer in which read is store.
 */
static int usb_ss_read_phycreg(struct usb_phy *phy_dwc3,
			       u32 addr, u32 *val)
{
	int ret;

	writel(addr, phy_dwc3->base + CR_PROTOCOL_DATA_IN_REG);
	writel(SS_CR_CAP_ADDR_REG,
	       phy_dwc3->base + CR_PROTOCOL_CAP_ADDR_REG);

	ret = wait_for_latch(phy_dwc3->base + CR_PROTOCOL_CAP_ADDR_REG);
	if (ret)
		goto err_wait;

	/*
	 * Due to hardware bug, first read of SSPHY register might be
	 * incorrect. Hence as workaround, SW should perform SSPHY register
	 * read twice, but use only second read and ignore first read.
	 */
	writel(SS_CR_READ_REG, phy_dwc3->base + CR_PROTOCOL_READ_REG);

	ret = wait_for_latch(phy_dwc3->base + CR_PROTOCOL_READ_REG);
	if (ret)
		goto err_wait;

	/* throwaway read */
	readl(phy_dwc3->base + CR_PROTOCOL_DATA_OUT_REG);

	writel(SS_CR_READ_REG, phy_dwc3->base + CR_PROTOCOL_READ_REG);

	ret = wait_for_latch(phy_dwc3->base + CR_PROTOCOL_READ_REG);
	if (ret)
		goto err_wait;

	*val = readl(phy_dwc3->base + CR_PROTOCOL_DATA_OUT_REG);

err_wait:
	return ret;
}

static int qcom_ipq806x_usb_hs_phy_init(struct phy *phy)
{
	struct usb_phy *phy_dwc3 = phy_get_drvdata(phy);
	int ret;
	u32 val;

	ret = clk_prepare_enable(phy_dwc3->xo_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy_dwc3->ref_clk);
	if (ret) {
		clk_disable_unprepare(phy_dwc3->xo_clk);
		return ret;
	}

	/*
	 * HSPHY Initialization: Enable UTMI clock, select 19.2MHz fsel
	 * enable clamping, and disable RETENTION (power-on default is ENABLED)
	 */
	val = HSUSB_CTRL_DPSEHV_CLAMP | HSUSB_CTRL_DMSEHV_CLAMP |
		HSUSB_CTRL_RETENABLEN  | HSUSB_CTRL_COMMONONN |
		HSUSB_CTRL_OTGSESSVLD_CLAMP | HSUSB_CTRL_ID_HV_CLAMP |
		HSUSB_CTRL_UTMI_OTG_VBUS_VALID | HSUSB_CTRL_UTMI_CLK_EN |
		HSUSB_CTRL_CLAMP_EN | 0x70;

	/* use core clock if external reference is not present */
	if (!phy_dwc3->xo_clk)
		val |= HSUSB_CTRL_USE_CLKCORE;

	writel(val, phy_dwc3->base + HSUSB_PHY_CTRL_REG);
	usleep_range(2000, 2200);

	/* Disable (bypass) VBUS and ID filters */
	writel(HSUSB_GCFG_XHCI_REV, phy_dwc3->base + QSCRATCH_GENERAL_CFG);

	return 0;
}

static int qcom_ipq806x_usb_hs_phy_exit(struct phy *phy)
{
	struct usb_phy *phy_dwc3 = phy_get_drvdata(phy);

	clk_disable_unprepare(phy_dwc3->ref_clk);
	clk_disable_unprepare(phy_dwc3->xo_clk);

	return 0;
}

static int qcom_ipq806x_usb_ss_phy_init(struct phy *phy)
{
	struct usb_phy *phy_dwc3 = phy_get_drvdata(phy);
	int ret;
	u32 data;

	ret = clk_prepare_enable(phy_dwc3->xo_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(phy_dwc3->ref_clk);
	if (ret) {
		clk_disable_unprepare(phy_dwc3->xo_clk);
		return ret;
	}

	/* reset phy */
	data = readl(phy_dwc3->base + SSUSB_PHY_CTRL_REG);
	writel(data | SSUSB_CTRL_SS_PHY_RESET,
	       phy_dwc3->base + SSUSB_PHY_CTRL_REG);
	usleep_range(2000, 2200);
	writel(data, phy_dwc3->base + SSUSB_PHY_CTRL_REG);

	/* clear REF_PAD if we don't have XO clk */
	if (!phy_dwc3->xo_clk)
		data &= ~SSUSB_CTRL_REF_USE_PAD;
	else
		data |= SSUSB_CTRL_REF_USE_PAD;

	writel(data, phy_dwc3->base + SSUSB_PHY_CTRL_REG);

	/* wait for ref clk to become stable, this can take up to 30ms */
	msleep(30);

	data |= SSUSB_CTRL_SS_PHY_EN | SSUSB_CTRL_LANE0_PWR_PRESENT;
	writel(data, phy_dwc3->base + SSUSB_PHY_CTRL_REG);

	/*
	 * WORKAROUND: There is SSPHY suspend bug due to which USB enumerates
	 * in HS mode instead of SS mode. Workaround it by asserting
	 * LANE0.TX_ALT_BLOCK.EN_ALT_BUS to enable TX to use alt bus mode
	 */
	ret = usb_ss_read_phycreg(phy_dwc3, 0x102D, &data);
	if (ret)
		goto err_phy_trans;

	data |= (1 << 7);
	ret = usb_ss_write_phycreg(phy_dwc3, 0x102D, data);
	if (ret)
		goto err_phy_trans;

	ret = usb_ss_read_phycreg(phy_dwc3, 0x1010, &data);
	if (ret)
		goto err_phy_trans;

	data &= ~0xff0;
	data |= 0x20;
	ret = usb_ss_write_phycreg(phy_dwc3, 0x1010, data);
	if (ret)
		goto err_phy_trans;

	/*
	 * Fix RX Equalization setting as follows
	 * LANE0.RX_OVRD_IN_HI. RX_EQ_EN set to 0
	 * LANE0.RX_OVRD_IN_HI.RX_EQ_EN_OVRD set to 1
	 * LANE0.RX_OVRD_IN_HI.RX_EQ set based on SoC version
	 * LANE0.RX_OVRD_IN_HI.RX_EQ_OVRD set to 1
	 */
	ret = usb_ss_read_phycreg(phy_dwc3, SSPHY_CTRL_RX_OVRD_IN_HI(0), &data);
	if (ret)
		goto err_phy_trans;

	data &= ~RX_OVRD_IN_HI_RX_EQ_EN;
	data |= RX_OVRD_IN_HI_RX_EQ_EN_OVRD;
	data &= ~RX_OVRD_IN_HI_RX_EQ_MASK;
	data |= RX_OVRD_IN_HI_RX_EQ(phy_dwc3->rx_eq);
	data |= RX_OVRD_IN_HI_RX_EQ_OVRD;
	ret = usb_ss_write_phycreg(phy_dwc3,
				   SSPHY_CTRL_RX_OVRD_IN_HI(0), data);
	if (ret)
		goto err_phy_trans;

	/*
	 * Set EQ and TX launch amplitudes as follows
	 * LANE0.TX_OVRD_DRV_LO.PREEMPH set based on SoC version
	 * LANE0.TX_OVRD_DRV_LO.AMPLITUDE set to 110
	 * LANE0.TX_OVRD_DRV_LO.EN set to 1.
	 */
	ret = usb_ss_read_phycreg(phy_dwc3,
				  SSPHY_CTRL_TX_OVRD_DRV_LO(0), &data);
	if (ret)
		goto err_phy_trans;

	data &= ~TX_OVRD_DRV_LO_PREEMPH_MASK;
	data |= TX_OVRD_DRV_LO_PREEMPH(phy_dwc3->tx_deamp_3_5db);
	data &= ~TX_OVRD_DRV_LO_AMPLITUDE_MASK;
	data |= 0x6E;
	data |= TX_OVRD_DRV_LO_EN;
	ret = usb_ss_write_phycreg(phy_dwc3,
				   SSPHY_CTRL_TX_OVRD_DRV_LO(0), data);
	if (ret)
		goto err_phy_trans;

	data = 0;
	data &= ~SSPHY_MPLL_MASK;
	data |= SSPHY_MPLL(phy_dwc3->mpll);
	usb_ss_write_phycreg(phy_dwc3, 0x30, data);

	/*
	 * Set the QSCRATCH PHY_PARAM_CTRL1 parameters as follows
	 * TX_FULL_SWING [26:20] amplitude to 110
	 * TX_DEEMPH_6DB [19:14] to 32
	 * TX_DEEMPH_3_5DB [13:8] set based on SoC version
	 * LOS_BIAS [7:3] to 9
	 */
	data = readl(phy_dwc3->base + SSUSB_PHY_PARAM_CTRL_1);

	data &= ~PHY_PARAM_CTRL1_MASK;

	data |= PHY_PARAM_CTRL1_TX_FULL_SWING(0x6e) |
		PHY_PARAM_CTRL1_TX_DEEMPH_6DB(0x20) |
		PHY_PARAM_CTRL1_TX_DEEMPH_3_5DB(phy_dwc3->tx_deamp_3_5db) |
		PHY_PARAM_CTRL1_LOS_BIAS(0x9);

	usb_phy_write_readback(phy_dwc3, SSUSB_PHY_PARAM_CTRL_1,
			       PHY_PARAM_CTRL1_MASK, data);

err_phy_trans:
	return ret;
}

static int qcom_ipq806x_usb_ss_phy_exit(struct phy *phy)
{
	struct usb_phy *phy_dwc3 = phy_get_drvdata(phy);

	/* Sequence to put SSPHY in low power state:
	 * 1. Clear REF_PHY_EN in PHY_CTRL_REG
	 * 2. Clear REF_USE_PAD in PHY_CTRL_REG
	 * 3. Set TEST_POWERED_DOWN in PHY_CTRL_REG to enable PHY retention
	 */
	usb_phy_write_readback(phy_dwc3, SSUSB_PHY_CTRL_REG,
			       SSUSB_CTRL_SS_PHY_EN, 0x0);
	usb_phy_write_readback(phy_dwc3, SSUSB_PHY_CTRL_REG,
			       SSUSB_CTRL_REF_USE_PAD, 0x0);
	usb_phy_write_readback(phy_dwc3, SSUSB_PHY_CTRL_REG,
			       SSUSB_CTRL_TEST_POWERDOWN, 0x0);

	clk_disable_unprepare(phy_dwc3->ref_clk);
	clk_disable_unprepare(phy_dwc3->xo_clk);

	return 0;
}

static const struct phy_drvdata qcom_ipq806x_usb_hs_drvdata = {
	.ops = {
		.init		= qcom_ipq806x_usb_hs_phy_init,
		.exit		= qcom_ipq806x_usb_hs_phy_exit,
		.owner		= THIS_MODULE,
	},
	.clk_rate = 60000000,
};

static const struct phy_drvdata qcom_ipq806x_usb_ss_drvdata = {
	.ops = {
		.init		= qcom_ipq806x_usb_ss_phy_init,
		.exit		= qcom_ipq806x_usb_ss_phy_exit,
		.owner		= THIS_MODULE,
	},
	.clk_rate = 125000000,
};

static const struct of_device_id qcom_ipq806x_usb_phy_table[] = {
	{ .compatible = "qcom,ipq806x-usb-phy-hs",
	  .data = &qcom_ipq806x_usb_hs_drvdata },
	{ .compatible = "qcom,ipq806x-usb-phy-ss",
	  .data = &qcom_ipq806x_usb_ss_drvdata },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, qcom_ipq806x_usb_phy_table);

static int qcom_ipq806x_usb_phy_probe(struct platform_device *pdev)
{
	struct resource *res;
	resource_size_t size;
	struct phy *generic_phy;
	struct usb_phy *phy_dwc3;
	const struct phy_drvdata *data;
	struct phy_provider *phy_provider;

	phy_dwc3 = devm_kzalloc(&pdev->dev, sizeof(*phy_dwc3), GFP_KERNEL);
	if (!phy_dwc3)
		return -ENOMEM;

	data = of_device_get_match_data(&pdev->dev);

	phy_dwc3->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	size = resource_size(res);
	phy_dwc3->base = devm_ioremap(phy_dwc3->dev, res->start, size);

	if (!phy_dwc3->base) {
		dev_err(phy_dwc3->dev, "failed to map reg\n");
		return -ENOMEM;
	}

	phy_dwc3->ref_clk = devm_clk_get(phy_dwc3->dev, "ref");
	if (IS_ERR(phy_dwc3->ref_clk)) {
		dev_dbg(phy_dwc3->dev, "cannot get reference clock\n");
		return PTR_ERR(phy_dwc3->ref_clk);
	}

	clk_set_rate(phy_dwc3->ref_clk, data->clk_rate);

	phy_dwc3->xo_clk = devm_clk_get(phy_dwc3->dev, "xo");
	if (IS_ERR(phy_dwc3->xo_clk)) {
		dev_dbg(phy_dwc3->dev, "cannot get TCXO clock\n");
		phy_dwc3->xo_clk = NULL;
	}

	/* Parse device node to probe HSIO settings */
	if (device_property_read_u32(&pdev->dev, "qcom,rx-eq",
				     &phy_dwc3->rx_eq))
		phy_dwc3->rx_eq = SSPHY_RX_EQ_VALUE;

	if (device_property_read_u32(&pdev->dev, "qcom,tx-deamp_3_5db",
				     &phy_dwc3->tx_deamp_3_5db))
		phy_dwc3->tx_deamp_3_5db = SSPHY_TX_DEEMPH_3_5DB;

	if (device_property_read_u32(&pdev->dev, "qcom,mpll", &phy_dwc3->mpll))
		phy_dwc3->mpll = SSPHY_MPLL_VALUE;

	generic_phy = devm_phy_create(phy_dwc3->dev, pdev->dev.of_node, &data->ops);

	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, phy_dwc3);
	platform_set_drvdata(pdev, phy_dwc3);

	phy_provider = devm_of_phy_provider_register(phy_dwc3->dev,
						     of_phy_simple_xlate);

	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static struct platform_driver qcom_ipq806x_usb_phy_driver = {
	.probe		= qcom_ipq806x_usb_phy_probe,
	.driver		= {
		.name	= "qcom-ipq806x-usb-phy",
		.of_match_table = qcom_ipq806x_usb_phy_table,
	},
};

module_platform_driver(qcom_ipq806x_usb_phy_driver);

MODULE_ALIAS("platform:phy-qcom-ipq806x-usb");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_AUTHOR("Ivan T. Ivanov <iivanov@mm-sol.com>");
MODULE_DESCRIPTION("DesignWare USB3 QCOM PHY driver");
