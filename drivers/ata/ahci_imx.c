/*
 * copyright (c) 2013 Freescale Semiconductor, Inc.
 * Freescale IMX AHCI SATA platform driver
 *
 * based on the AHCI SATA platform driver by Jeff Garzik and Anton Vorontsov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/ahci_platform.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/libata.h>
#include "ahci.h"

enum {
	/* Timer 1-ms Register */
	IMX_TIMER1MS				= 0x00e0,
	/* Port0 PHY Control Register */
	IMX_P0PHYCR				= 0x0178,
	IMX_P0PHYCR_TEST_PDDQ			= 1 << 20,
	IMX_P0PHYCR_CR_READ			= 1 << 19,
	IMX_P0PHYCR_CR_WRITE			= 1 << 18,
	IMX_P0PHYCR_CR_CAP_DATA			= 1 << 17,
	IMX_P0PHYCR_CR_CAP_ADDR			= 1 << 16,
	/* Port0 PHY Status Register */
	IMX_P0PHYSR				= 0x017c,
	IMX_P0PHYSR_CR_ACK			= 1 << 18,
	IMX_P0PHYSR_CR_DATA_OUT			= 0xffff << 0,
	/* Lane0 Output Status Register */
	IMX_LANE0_OUT_STAT			= 0x2003,
	IMX_LANE0_OUT_STAT_RX_PLL_STATE		= 1 << 1,
	/* Clock Reset Register */
	IMX_CLOCK_RESET				= 0x7f3f,
	IMX_CLOCK_RESET_RESET			= 1 << 0,
};

enum ahci_imx_type {
	AHCI_IMX53,
	AHCI_IMX6Q,
};

struct imx_ahci_priv {
	struct platform_device *ahci_pdev;
	enum ahci_imx_type type;
	struct clk *ahb_clk;
	struct regmap *gpr;
	bool no_device;
	bool first_time;
	u32 phy_params;
};

static int ahci_imx_hotplug;
module_param_named(hotplug, ahci_imx_hotplug, int, 0644);
MODULE_PARM_DESC(hotplug, "AHCI IMX hot-plug support (0=Don't support, 1=support)");

static void ahci_imx_host_stop(struct ata_host *host);

static int imx_phy_crbit_assert(void __iomem *mmio, u32 bit, bool assert)
{
	int timeout = 10;
	u32 crval;
	u32 srval;

	/* Assert or deassert the bit */
	crval = readl(mmio + IMX_P0PHYCR);
	if (assert)
		crval |= bit;
	else
		crval &= ~bit;
	writel(crval, mmio + IMX_P0PHYCR);

	/* Wait for the cr_ack signal */
	do {
		srval = readl(mmio + IMX_P0PHYSR);
		if ((assert ? srval : ~srval) & IMX_P0PHYSR_CR_ACK)
			break;
		usleep_range(100, 200);
	} while (--timeout);

	return timeout ? 0 : -ETIMEDOUT;
}

static int imx_phy_reg_addressing(u16 addr, void __iomem *mmio)
{
	u32 crval = addr;
	int ret;

	/* Supply the address on cr_data_in */
	writel(crval, mmio + IMX_P0PHYCR);

	/* Assert the cr_cap_addr signal */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_CAP_ADDR, true);
	if (ret)
		return ret;

	/* Deassert cr_cap_addr */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_CAP_ADDR, false);
	if (ret)
		return ret;

	return 0;
}

static int imx_phy_reg_write(u16 val, void __iomem *mmio)
{
	u32 crval = val;
	int ret;

	/* Supply the data on cr_data_in */
	writel(crval, mmio + IMX_P0PHYCR);

	/* Assert the cr_cap_data signal */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_CAP_DATA, true);
	if (ret)
		return ret;

	/* Deassert cr_cap_data */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_CAP_DATA, false);
	if (ret)
		return ret;

	if (val & IMX_CLOCK_RESET_RESET) {
		/*
		 * In case we're resetting the phy, it's unable to acknowledge,
		 * so we return immediately here.
		 */
		crval |= IMX_P0PHYCR_CR_WRITE;
		writel(crval, mmio + IMX_P0PHYCR);
		goto out;
	}

	/* Assert the cr_write signal */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_WRITE, true);
	if (ret)
		return ret;

	/* Deassert cr_write */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_WRITE, false);
	if (ret)
		return ret;

out:
	return 0;
}

static int imx_phy_reg_read(u16 *val, void __iomem *mmio)
{
	int ret;

	/* Assert the cr_read signal */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_READ, true);
	if (ret)
		return ret;

	/* Capture the data from cr_data_out[] */
	*val = readl(mmio + IMX_P0PHYSR) & IMX_P0PHYSR_CR_DATA_OUT;

	/* Deassert cr_read */
	ret = imx_phy_crbit_assert(mmio, IMX_P0PHYCR_CR_READ, false);
	if (ret)
		return ret;

	return 0;
}

static int imx_sata_phy_reset(struct ahci_host_priv *hpriv)
{
	void __iomem *mmio = hpriv->mmio;
	int timeout = 10;
	u16 val;
	int ret;

	/* Reset SATA PHY by setting RESET bit of PHY register CLOCK_RESET */
	ret = imx_phy_reg_addressing(IMX_CLOCK_RESET, mmio);
	if (ret)
		return ret;
	ret = imx_phy_reg_write(IMX_CLOCK_RESET_RESET, mmio);
	if (ret)
		return ret;

	/* Wait for PHY RX_PLL to be stable */
	do {
		usleep_range(100, 200);
		ret = imx_phy_reg_addressing(IMX_LANE0_OUT_STAT, mmio);
		if (ret)
			return ret;
		ret = imx_phy_reg_read(&val, mmio);
		if (ret)
			return ret;
		if (val & IMX_LANE0_OUT_STAT_RX_PLL_STATE)
			break;
	} while (--timeout);

	return timeout ? 0 : -ETIMEDOUT;
}

static int imx_sata_enable(struct ahci_host_priv *hpriv)
{
	struct imx_ahci_priv *imxpriv = hpriv->plat_data;
	struct device *dev = &imxpriv->ahci_pdev->dev;
	int ret;

	if (imxpriv->no_device)
		return 0;

	if (hpriv->target_pwr) {
		ret = regulator_enable(hpriv->target_pwr);
		if (ret)
			return ret;
	}

	ret = ahci_platform_enable_clks(hpriv);
	if (ret < 0)
		goto disable_regulator;

	if (imxpriv->type == AHCI_IMX6Q) {
		/*
		 * set PHY Paremeters, two steps to configure the GPR13,
		 * one write for rest of parameters, mask of first write
		 * is 0x07ffffff, and the other one write for setting
		 * the mpll_clk_en.
		 */
		regmap_update_bits(imxpriv->gpr, IOMUXC_GPR13,
				   IMX6Q_GPR13_SATA_RX_EQ_VAL_MASK |
				   IMX6Q_GPR13_SATA_RX_LOS_LVL_MASK |
				   IMX6Q_GPR13_SATA_RX_DPLL_MODE_MASK |
				   IMX6Q_GPR13_SATA_SPD_MODE_MASK |
				   IMX6Q_GPR13_SATA_MPLL_SS_EN |
				   IMX6Q_GPR13_SATA_TX_ATTEN_MASK |
				   IMX6Q_GPR13_SATA_TX_BOOST_MASK |
				   IMX6Q_GPR13_SATA_TX_LVL_MASK |
				   IMX6Q_GPR13_SATA_MPLL_CLK_EN |
				   IMX6Q_GPR13_SATA_TX_EDGE_RATE,
				   imxpriv->phy_params);
		regmap_update_bits(imxpriv->gpr, IOMUXC_GPR13,
				   IMX6Q_GPR13_SATA_MPLL_CLK_EN,
				   IMX6Q_GPR13_SATA_MPLL_CLK_EN);

		usleep_range(100, 200);

		ret = imx_sata_phy_reset(hpriv);
		if (ret) {
			dev_err(dev, "failed to reset phy: %d\n", ret);
			goto disable_clk;
		}
	}

	usleep_range(1000, 2000);

	return 0;

disable_clk:
	clk_disable_unprepare(imxpriv->sata_ref_clk);
disable_regulator:
	if (hpriv->target_pwr)
		regulator_disable(hpriv->target_pwr);

	return ret;
}

static void imx_sata_disable(struct ahci_host_priv *hpriv)
{
	struct imx_ahci_priv *imxpriv = hpriv->plat_data;

	if (imxpriv->no_device)
		return;

	if (imxpriv->type == AHCI_IMX6Q) {
		regmap_update_bits(imxpriv->gpr, IOMUXC_GPR13,
				   IMX6Q_GPR13_SATA_MPLL_CLK_EN,
				   !IMX6Q_GPR13_SATA_MPLL_CLK_EN);
	}

	ahci_platform_disable_clks(hpriv);

	if (hpriv->target_pwr)
		regulator_disable(hpriv->target_pwr);
}

static void ahci_imx_error_handler(struct ata_port *ap)
{
	u32 reg_val;
	struct ata_device *dev;
	struct ata_host *host = dev_get_drvdata(ap->dev);
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;
	struct imx_ahci_priv *imxpriv = hpriv->plat_data;

	ahci_error_handler(ap);

	if (!(imxpriv->first_time) || ahci_imx_hotplug)
		return;

	imxpriv->first_time = false;

	ata_for_each_dev(dev, &ap->link, ENABLED)
		return;
	/*
	 * Disable link to save power.  An imx ahci port can't be recovered
	 * without full reset once the pddq mode is enabled making it
	 * impossible to use as part of libata LPM.
	 */
	reg_val = readl(mmio + IMX_P0PHYCR);
	writel(reg_val | IMX_P0PHYCR_TEST_PDDQ, mmio + IMX_P0PHYCR);
	imx_sata_disable(hpriv);
	imxpriv->no_device = true;
}

static int ahci_imx_softreset(struct ata_link *link, unsigned int *class,
		       unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	struct ata_host *host = dev_get_drvdata(ap->dev);
	struct ahci_host_priv *hpriv = host->private_data;
	struct imx_ahci_priv *imxpriv = hpriv->plat_data;
	int ret = -EIO;

	if (imxpriv->type == AHCI_IMX53)
		ret = ahci_pmp_retry_srst_ops.softreset(link, class, deadline);
	else if (imxpriv->type == AHCI_IMX6Q)
		ret = ahci_ops.softreset(link, class, deadline);

	return ret;
}

static struct ata_port_operations ahci_imx_ops = {
	.inherits	= &ahci_ops,
	.host_stop	= ahci_imx_host_stop,
	.error_handler	= ahci_imx_error_handler,
	.softreset	= ahci_imx_softreset,
};

static const struct ata_port_info ahci_imx_port_info = {
	.flags		= AHCI_FLAG_COMMON,
	.pio_mask	= ATA_PIO4,
	.udma_mask	= ATA_UDMA6,
	.port_ops	= &ahci_imx_ops,
};

static const struct of_device_id imx_ahci_of_match[] = {
	{ .compatible = "fsl,imx53-ahci", .data = (void *)AHCI_IMX53 },
	{ .compatible = "fsl,imx6q-ahci", .data = (void *)AHCI_IMX6Q },
	{},
};
MODULE_DEVICE_TABLE(of, imx_ahci_of_match);

struct reg_value {
	u32 of_value;
	u32 reg_value;
};

struct reg_property {
	const char *name;
	const struct reg_value *values;
	size_t num_values;
	u32 def_value;
	u32 set_value;
};

static const struct reg_value gpr13_tx_level[] = {
	{  937, IMX6Q_GPR13_SATA_TX_LVL_0_937_V },
	{  947, IMX6Q_GPR13_SATA_TX_LVL_0_947_V },
	{  957, IMX6Q_GPR13_SATA_TX_LVL_0_957_V },
	{  966, IMX6Q_GPR13_SATA_TX_LVL_0_966_V },
	{  976, IMX6Q_GPR13_SATA_TX_LVL_0_976_V },
	{  986, IMX6Q_GPR13_SATA_TX_LVL_0_986_V },
	{  996, IMX6Q_GPR13_SATA_TX_LVL_0_996_V },
	{ 1005, IMX6Q_GPR13_SATA_TX_LVL_1_005_V },
	{ 1015, IMX6Q_GPR13_SATA_TX_LVL_1_015_V },
	{ 1025, IMX6Q_GPR13_SATA_TX_LVL_1_025_V },
	{ 1035, IMX6Q_GPR13_SATA_TX_LVL_1_035_V },
	{ 1045, IMX6Q_GPR13_SATA_TX_LVL_1_045_V },
	{ 1054, IMX6Q_GPR13_SATA_TX_LVL_1_054_V },
	{ 1064, IMX6Q_GPR13_SATA_TX_LVL_1_064_V },
	{ 1074, IMX6Q_GPR13_SATA_TX_LVL_1_074_V },
	{ 1084, IMX6Q_GPR13_SATA_TX_LVL_1_084_V },
	{ 1094, IMX6Q_GPR13_SATA_TX_LVL_1_094_V },
	{ 1104, IMX6Q_GPR13_SATA_TX_LVL_1_104_V },
	{ 1113, IMX6Q_GPR13_SATA_TX_LVL_1_113_V },
	{ 1123, IMX6Q_GPR13_SATA_TX_LVL_1_123_V },
	{ 1133, IMX6Q_GPR13_SATA_TX_LVL_1_133_V },
	{ 1143, IMX6Q_GPR13_SATA_TX_LVL_1_143_V },
	{ 1152, IMX6Q_GPR13_SATA_TX_LVL_1_152_V },
	{ 1162, IMX6Q_GPR13_SATA_TX_LVL_1_162_V },
	{ 1172, IMX6Q_GPR13_SATA_TX_LVL_1_172_V },
	{ 1182, IMX6Q_GPR13_SATA_TX_LVL_1_182_V },
	{ 1191, IMX6Q_GPR13_SATA_TX_LVL_1_191_V },
	{ 1201, IMX6Q_GPR13_SATA_TX_LVL_1_201_V },
	{ 1211, IMX6Q_GPR13_SATA_TX_LVL_1_211_V },
	{ 1221, IMX6Q_GPR13_SATA_TX_LVL_1_221_V },
	{ 1230, IMX6Q_GPR13_SATA_TX_LVL_1_230_V },
	{ 1240, IMX6Q_GPR13_SATA_TX_LVL_1_240_V }
};

static const struct reg_value gpr13_tx_boost[] = {
	{    0, IMX6Q_GPR13_SATA_TX_BOOST_0_00_DB },
	{  370, IMX6Q_GPR13_SATA_TX_BOOST_0_37_DB },
	{  740, IMX6Q_GPR13_SATA_TX_BOOST_0_74_DB },
	{ 1110, IMX6Q_GPR13_SATA_TX_BOOST_1_11_DB },
	{ 1480, IMX6Q_GPR13_SATA_TX_BOOST_1_48_DB },
	{ 1850, IMX6Q_GPR13_SATA_TX_BOOST_1_85_DB },
	{ 2220, IMX6Q_GPR13_SATA_TX_BOOST_2_22_DB },
	{ 2590, IMX6Q_GPR13_SATA_TX_BOOST_2_59_DB },
	{ 2960, IMX6Q_GPR13_SATA_TX_BOOST_2_96_DB },
	{ 3330, IMX6Q_GPR13_SATA_TX_BOOST_3_33_DB },
	{ 3700, IMX6Q_GPR13_SATA_TX_BOOST_3_70_DB },
	{ 4070, IMX6Q_GPR13_SATA_TX_BOOST_4_07_DB },
	{ 4440, IMX6Q_GPR13_SATA_TX_BOOST_4_44_DB },
	{ 4810, IMX6Q_GPR13_SATA_TX_BOOST_4_81_DB },
	{ 5280, IMX6Q_GPR13_SATA_TX_BOOST_5_28_DB },
	{ 5750, IMX6Q_GPR13_SATA_TX_BOOST_5_75_DB }
};

static const struct reg_value gpr13_tx_atten[] = {
	{  8, IMX6Q_GPR13_SATA_TX_ATTEN_8_16 },
	{  9, IMX6Q_GPR13_SATA_TX_ATTEN_9_16 },
	{ 10, IMX6Q_GPR13_SATA_TX_ATTEN_10_16 },
	{ 12, IMX6Q_GPR13_SATA_TX_ATTEN_12_16 },
	{ 14, IMX6Q_GPR13_SATA_TX_ATTEN_14_16 },
	{ 16, IMX6Q_GPR13_SATA_TX_ATTEN_16_16 },
};

static const struct reg_value gpr13_rx_eq[] = {
	{  500, IMX6Q_GPR13_SATA_RX_EQ_VAL_0_5_DB },
	{ 1000, IMX6Q_GPR13_SATA_RX_EQ_VAL_1_0_DB },
	{ 1500, IMX6Q_GPR13_SATA_RX_EQ_VAL_1_5_DB },
	{ 2000, IMX6Q_GPR13_SATA_RX_EQ_VAL_2_0_DB },
	{ 2500, IMX6Q_GPR13_SATA_RX_EQ_VAL_2_5_DB },
	{ 3000, IMX6Q_GPR13_SATA_RX_EQ_VAL_3_0_DB },
	{ 3500, IMX6Q_GPR13_SATA_RX_EQ_VAL_3_5_DB },
	{ 4000, IMX6Q_GPR13_SATA_RX_EQ_VAL_4_0_DB },
};

static const struct reg_property gpr13_props[] = {
	{
		.name = "fsl,transmit-level-mV",
		.values = gpr13_tx_level,
		.num_values = ARRAY_SIZE(gpr13_tx_level),
		.def_value = IMX6Q_GPR13_SATA_TX_LVL_1_025_V,
	}, {
		.name = "fsl,transmit-boost-mdB",
		.values = gpr13_tx_boost,
		.num_values = ARRAY_SIZE(gpr13_tx_boost),
		.def_value = IMX6Q_GPR13_SATA_TX_BOOST_3_33_DB,
	}, {
		.name = "fsl,transmit-atten-16ths",
		.values = gpr13_tx_atten,
		.num_values = ARRAY_SIZE(gpr13_tx_atten),
		.def_value = IMX6Q_GPR13_SATA_TX_ATTEN_9_16,
	}, {
		.name = "fsl,receive-eq-mdB",
		.values = gpr13_rx_eq,
		.num_values = ARRAY_SIZE(gpr13_rx_eq),
		.def_value = IMX6Q_GPR13_SATA_RX_EQ_VAL_3_0_DB,
	}, {
		.name = "fsl,no-spread-spectrum",
		.def_value = IMX6Q_GPR13_SATA_MPLL_SS_EN,
		.set_value = 0,
	},
};

static u32 imx_ahci_parse_props(struct device *dev,
				const struct reg_property *prop, size_t num)
{
	struct device_node *np = dev->of_node;
	u32 reg_value = 0;
	int i, j;

	for (i = 0; i < num; i++, prop++) {
		u32 of_val;

		if (prop->num_values == 0) {
			if (of_property_read_bool(np, prop->name))
				reg_value |= prop->set_value;
			else
				reg_value |= prop->def_value;
			continue;
		}

		if (of_property_read_u32(np, prop->name, &of_val)) {
			dev_info(dev, "%s not specified, using %08x\n",
				prop->name, prop->def_value);
			reg_value |= prop->def_value;
			continue;
		}

		for (j = 0; j < prop->num_values; j++) {
			if (prop->values[j].of_value == of_val) {
				dev_info(dev, "%s value %u, using %08x\n",
					prop->name, of_val, prop->values[j].reg_value);
				reg_value |= prop->values[j].reg_value;
				break;
			}
		}

		if (j == prop->num_values) {
			dev_err(dev, "DT property %s is not a valid value\n",
				prop->name);
			reg_value |= prop->def_value;
		}
	}

	return reg_value;
}

static int imx_ahci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id;
	struct ahci_host_priv *hpriv;
	struct imx_ahci_priv *imxpriv;
	unsigned int reg_val;
	int ret;

	of_id = of_match_device(imx_ahci_of_match, dev);
	if (!of_id)
		return -EINVAL;

	imxpriv = devm_kzalloc(dev, sizeof(*imxpriv), GFP_KERNEL);
	if (!imxpriv)
		return -ENOMEM;

	imxpriv->ahci_pdev = pdev;
	imxpriv->no_device = false;
	imxpriv->first_time = true;
	imxpriv->type = (enum ahci_imx_type)of_id->data;
	imxpriv->ahb_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(imxpriv->ahb_clk)) {
		dev_err(dev, "can't get ahb clock.\n");
		return PTR_ERR(imxpriv->ahb_clk);
	}

	if (imxpriv->type == AHCI_IMX6Q) {
		u32 reg_value;

		imxpriv->gpr = syscon_regmap_lookup_by_compatible(
							"fsl,imx6q-iomuxc-gpr");
		if (IS_ERR(imxpriv->gpr)) {
			dev_err(dev,
				"failed to find fsl,imx6q-iomux-gpr regmap\n");
			return PTR_ERR(imxpriv->gpr);
		}

		reg_value = imx_ahci_parse_props(dev, gpr13_props,
						 ARRAY_SIZE(gpr13_props));

		imxpriv->phy_params =
				   IMX6Q_GPR13_SATA_RX_LOS_LVL_SATA2M |
				   IMX6Q_GPR13_SATA_RX_DPLL_MODE_2P_4F |
				   IMX6Q_GPR13_SATA_SPD_MODE_3P0G |
				   reg_value;
	}

	hpriv = ahci_platform_get_resources(pdev);
	if (IS_ERR(hpriv))
		return PTR_ERR(hpriv);

	hpriv->plat_data = imxpriv;

	ret = imx_sata_enable(hpriv);
	if (ret)
		return ret;

	/*
	 * Configure the HWINIT bits of the HOST_CAP and HOST_PORTS_IMPL,
	 * and IP vendor specific register IMX_TIMER1MS.
	 * Configure CAP_SSS (support stagered spin up).
	 * Implement the port0.
	 * Get the ahb clock rate, and configure the TIMER1MS register.
	 */
	reg_val = readl(hpriv->mmio + HOST_CAP);
	if (!(reg_val & HOST_CAP_SSS)) {
		reg_val |= HOST_CAP_SSS;
		writel(reg_val, hpriv->mmio + HOST_CAP);
	}
	reg_val = readl(hpriv->mmio + HOST_PORTS_IMPL);
	if (!(reg_val & 0x1)) {
		reg_val |= 0x1;
		writel(reg_val, hpriv->mmio + HOST_PORTS_IMPL);
	}

	reg_val = clk_get_rate(imxpriv->ahb_clk) / 1000;
	writel(reg_val, hpriv->mmio + IMX_TIMER1MS);

	ret = ahci_platform_init_host(pdev, hpriv, &ahci_imx_port_info,
				      0, 0, 0);
	if (ret)
		imx_sata_disable(hpriv);

	return ret;
}

static void ahci_imx_host_stop(struct ata_host *host)
{
	struct ahci_host_priv *hpriv = host->private_data;

	imx_sata_disable(hpriv);
}

#ifdef CONFIG_PM_SLEEP
static int imx_ahci_suspend(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int ret;

	ret = ahci_platform_suspend_host(dev);
	if (ret)
		return ret;

	imx_sata_disable(hpriv);

	return 0;
}

static int imx_ahci_resume(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct ahci_host_priv *hpriv = host->private_data;
	int ret;

	ret = imx_sata_enable(hpriv);
	if (ret)
		return ret;

	return ahci_platform_resume_host(dev);
}
#endif

static SIMPLE_DEV_PM_OPS(ahci_imx_pm_ops, imx_ahci_suspend, imx_ahci_resume);

static struct platform_driver imx_ahci_driver = {
	.probe = imx_ahci_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = "ahci-imx",
		.owner = THIS_MODULE,
		.of_match_table = imx_ahci_of_match,
		.pm = &ahci_imx_pm_ops,
	},
};
module_platform_driver(imx_ahci_driver);

MODULE_DESCRIPTION("Freescale i.MX AHCI SATA platform driver");
MODULE_AUTHOR("Richard Zhu <Hong-Xing.Zhu@freescale.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ahci:imx");
