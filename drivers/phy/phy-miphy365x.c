/*
 * Copyright (C) 2014 STMicroelectronics – All Rights Reserved
 *
 * STMicroelectronics PHY driver MiPHY365 (for SoC STiH416).
 *
 * Authors: Alexandre Torgue <alexandre.torgue@st.com>
 *          Lee Jones <lee.jones@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/phy/phy.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <dt-bindings/phy/phy-miphy365x.h>

#define HFC_TIMEOUT		100

#define SYSCFG_SELECT_SATA_MASK	BIT(1)
#define SYSCFG_SELECT_SATA_POS	1

/* MiPHY365x register definitions */
#define RESET_REG		0x00
#define RST_PLL			BIT(1)
#define RST_PLL_CAL		BIT(2)
#define RST_RX			BIT(4)
#define RST_MACRO		BIT(7)

#define STATUS_REG		0x01
#define IDLL_RDY		BIT(0)
#define PLL_RDY			BIT(1)
#define DES_BIT_LOCK		BIT(2)
#define DES_SYMBOL_LOCK		BIT(3)

#define CTRL_REG		0x02
#define TERM_EN			BIT(0)
#define PCI_EN			BIT(2)
#define DES_BIT_LOCK_EN		BIT(3)
#define TX_POL			BIT(5)

#define INT_CTRL_REG		0x03

#define BOUNDARY1_REG		0x10
#define SPDSEL_SEL		BIT(0)

#define BOUNDARY3_REG		0x12
#define TX_SPDSEL_GEN1_VAL	0
#define TX_SPDSEL_GEN2_VAL	0x01
#define TX_SPDSEL_GEN3_VAL	0x02
#define RX_SPDSEL_GEN1_VAL	0
#define RX_SPDSEL_GEN2_VAL	(0x01 << 3)
#define RX_SPDSEL_GEN3_VAL	(0x02 << 3)

#define PCIE_REG		0x16

#define BUF_SEL_REG		0x20
#define CONF_GEN_SEL_GEN3	0x02
#define CONF_GEN_SEL_GEN2	0x01
#define PD_VDDTFILTER		BIT(4)

#define TXBUF1_REG		0x21
#define SWING_VAL		0x04
#define SWING_VAL_GEN1		0x03
#define PREEMPH_VAL		(0x3 << 5)

#define TXBUF2_REG		0x22
#define TXSLEW_VAL		0x2
#define TXSLEW_VAL_GEN1		0x4

#define RXBUF_OFFSET_CTRL_REG	0x23

#define RXBUF_REG		0x25
#define SDTHRES_VAL		0x01
#define EQ_ON3			(0x03 << 4)
#define EQ_ON1			(0x01 << 4)

#define COMP_CTRL1_REG		0x40
#define START_COMSR		BIT(0)
#define START_COMZC		BIT(1)
#define COMSR_DONE		BIT(2)
#define COMZC_DONE		BIT(3)
#define COMP_AUTO_LOAD		BIT(4)

#define COMP_CTRL2_REG		0x41
#define COMP_2MHZ_RAT_GEN1	0x1e
#define COMP_2MHZ_RAT		0xf

#define COMP_CTRL3_REG		0x42
#define COMSR_COMP_REF		0x33

#define COMP_IDLL_REG		0x47
#define COMZC_IDLL		0x2a

#define PLL_CTRL1_REG		0x50
#define PLL_START_CAL		BIT(0)
#define BUF_EN			BIT(2)
#define SYNCHRO_TX		BIT(3)
#define SSC_EN			BIT(6)
#define CONFIG_PLL		BIT(7)

#define PLL_CTRL2_REG		0x51
#define BYPASS_PLL_CAL		BIT(1)

#define PLL_RAT_REG		0x52

#define PLL_SSC_STEP_MSB_REG	0x56
#define PLL_SSC_STEP_MSB_VAL	0x03

#define PLL_SSC_STEP_LSB_REG	0x57
#define PLL_SSC_STEP_LSB_VAL	0x63

#define PLL_SSC_PER_MSB_REG	0x58
#define PLL_SSC_PER_MSB_VAL	0

#define PLL_SSC_PER_LSB_REG	0x59
#define PLL_SSC_PER_LSB_VAL	0xf1

#define IDLL_TEST_REG		0x72
#define START_CLK_HF		BIT(6)

#define DES_BITLOCK_REG		0x86
#define BIT_LOCK_LEVEL		0x01
#define BIT_LOCK_CNT_512	(0x03 << 5)

struct miphy365x_phy {
	struct phy *phy;
	void __iomem *base;
	bool pcie_tx_pol_inv;
	bool sata_tx_pol_inv;
	u32 sata_gen;
	u64 ctrlreg;
	u8 type;
};

struct miphy365x_dev {
	struct device *dev;
	struct regmap *regmap;
	struct mutex miphy_mutex;
	struct miphy365x_phy **phys;
};

/*
 * These values are represented in Device tree. They are considered to be ABI
 * and although they can be extended any existing values must not change.
 */
enum miphy_sata_gen {
	SATA_GEN1 = 1,
	SATA_GEN2,
	SATA_GEN3
};

static u8 rx_tx_spd[] = {
	TX_SPDSEL_GEN1_VAL | RX_SPDSEL_GEN1_VAL,
	TX_SPDSEL_GEN2_VAL | RX_SPDSEL_GEN2_VAL,
	TX_SPDSEL_GEN3_VAL | RX_SPDSEL_GEN3_VAL
};

/*
 * This function selects the system configuration,
 * either two SATA, one SATA and one PCIe, or two PCIe lanes.
 */
static int miphy365x_set_path(struct miphy365x_phy *miphy_phy,
			      struct miphy365x_dev *miphy_dev)
{
	bool sata = (miphy_phy->type == MIPHY_TYPE_SATA);

	return regmap_update_bits(miphy_dev->regmap,
				  (unsigned int)miphy_phy->ctrlreg,
				  SYSCFG_SELECT_SATA_MASK,
				  sata << SYSCFG_SELECT_SATA_POS);
}

static int miphy365x_init_pcie_port(struct miphy365x_phy *miphy_phy,
				    struct miphy365x_dev *miphy_dev)
{
	u8 val;

	if (miphy_phy->pcie_tx_pol_inv) {
		/* Invert Tx polarity and clear pci_txdetect_pol bit */
		val = TERM_EN | PCI_EN | DES_BIT_LOCK_EN | TX_POL;
		writeb_relaxed(val, miphy_phy->base + CTRL_REG);
		writeb_relaxed(0x00, miphy_phy->base + PCIE_REG);
	}

	return 0;
}

static inline int miphy365x_hfc_not_rdy(struct miphy365x_phy *miphy_phy,
					struct miphy365x_dev *miphy_dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(HFC_TIMEOUT);
	u8 mask = IDLL_RDY | PLL_RDY;
	u8 regval;

	do {
		regval = readb_relaxed(miphy_phy->base + STATUS_REG);
		if (!(regval & mask))
			return 0;

		usleep_range(2000, 2500);
	} while (time_before(jiffies, timeout));

	dev_err(miphy_dev->dev, "HFC ready timeout!\n");
	return -EBUSY;
}

static inline int miphy365x_rdy(struct miphy365x_phy *miphy_phy,
				struct miphy365x_dev *miphy_dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(HFC_TIMEOUT);
	u8 mask = IDLL_RDY | PLL_RDY;
	u8 regval;

	do {
		regval = readb_relaxed(miphy_phy->base + STATUS_REG);
		if ((regval & mask) == mask)
			return 0;

		usleep_range(2000, 2500);
	} while (time_before(jiffies, timeout));

	dev_err(miphy_dev->dev, "PHY not ready timeout!\n");
	return -EBUSY;
}

static inline void miphy365x_set_comp(struct miphy365x_phy *miphy_phy,
				      struct miphy365x_dev *miphy_dev)
{
	u8 val, mask;

	if (miphy_phy->sata_gen == SATA_GEN1)
		writeb_relaxed(COMP_2MHZ_RAT_GEN1,
			       miphy_phy->base + COMP_CTRL2_REG);
	else
		writeb_relaxed(COMP_2MHZ_RAT,
			       miphy_phy->base + COMP_CTRL2_REG);

	if (miphy_phy->sata_gen != SATA_GEN3) {
		writeb_relaxed(COMSR_COMP_REF,
			       miphy_phy->base + COMP_CTRL3_REG);
		/*
		 * Force VCO current to value defined by address 0x5A
		 * and disable PCIe100Mref bit
		 * Enable auto load compensation for pll_i_bias
		 */
		writeb_relaxed(BYPASS_PLL_CAL, miphy_phy->base + PLL_CTRL2_REG);
		writeb_relaxed(COMZC_IDLL, miphy_phy->base + COMP_IDLL_REG);
	}

	/*
	 * Force restart compensation and enable auto load
	 * for Comzc_Tx, Comzc_Rx and Comsr on macro
	 */
	val = START_COMSR | START_COMZC | COMP_AUTO_LOAD;
	writeb_relaxed(val, miphy_phy->base + COMP_CTRL1_REG);

	mask = COMSR_DONE | COMZC_DONE;
	while ((readb_relaxed(miphy_phy->base + COMP_CTRL1_REG) & mask)	!= mask)
		cpu_relax();
}

static inline void miphy365x_set_ssc(struct miphy365x_phy *miphy_phy,
				     struct miphy365x_dev *miphy_dev)
{
	u8 val;

	/*
	 * SSC Settings. SSC will be enabled through Link
	 * SSC Ampl. = 0.4%
	 * SSC Freq = 31KHz
	 */
	writeb_relaxed(PLL_SSC_STEP_MSB_VAL,
		       miphy_phy->base + PLL_SSC_STEP_MSB_REG);
	writeb_relaxed(PLL_SSC_STEP_LSB_VAL,
		       miphy_phy->base + PLL_SSC_STEP_LSB_REG);
	writeb_relaxed(PLL_SSC_PER_MSB_VAL,
		       miphy_phy->base + PLL_SSC_PER_MSB_REG);
	writeb_relaxed(PLL_SSC_PER_LSB_VAL,
		       miphy_phy->base + PLL_SSC_PER_LSB_REG);

	/* SSC Settings complete */
	if (miphy_phy->sata_gen == SATA_GEN1) {
		val = PLL_START_CAL | BUF_EN | SYNCHRO_TX | CONFIG_PLL;
		writeb_relaxed(val, miphy_phy->base + PLL_CTRL1_REG);
	} else {
		val = SSC_EN | PLL_START_CAL | BUF_EN | SYNCHRO_TX | CONFIG_PLL;
		writeb_relaxed(val, miphy_phy->base + PLL_CTRL1_REG);
	}
}

static int miphy365x_init_sata_port(struct miphy365x_phy *miphy_phy,
				    struct miphy365x_dev *miphy_dev)
{
	int ret;
	u8 val;

	/*
	 * Force PHY macro reset, PLL calibration reset, PLL reset
	 * and assert Deserializer Reset
	 */
	val = RST_PLL | RST_PLL_CAL | RST_RX | RST_MACRO;
	writeb_relaxed(val, miphy_phy->base + RESET_REG);

	if (miphy_phy->sata_tx_pol_inv)
		writeb_relaxed(TX_POL, miphy_phy->base + CTRL_REG);

	/*
	 * Force macro1 to use rx_lspd, tx_lspd
	 * Force Rx_Clock on first I-DLL phase
	 * Force Des in HP mode on macro, rx_lspd, tx_lspd for Gen2/3
	 */
	writeb_relaxed(SPDSEL_SEL, miphy_phy->base + BOUNDARY1_REG);
	writeb_relaxed(START_CLK_HF, miphy_phy->base + IDLL_TEST_REG);
	val = rx_tx_spd[miphy_phy->sata_gen];
	writeb_relaxed(val, miphy_phy->base + BOUNDARY3_REG);

	/* Wait for HFC_READY = 0 */
	ret = miphy365x_hfc_not_rdy(miphy_phy, miphy_dev);
	if (ret)
		return ret;

	/* Compensation Recalibration */
	miphy365x_set_comp(miphy_phy, miphy_dev);

	switch (miphy_phy->sata_gen) {
	case SATA_GEN3:
		/*
		 * TX Swing target 550-600mv peak to peak diff
		 * Tx Slew target 90-110ps rising/falling time
		 * Rx Eq ON3, Sigdet threshold SDTH1
		 */
		val = PD_VDDTFILTER | CONF_GEN_SEL_GEN3;
		writeb_relaxed(val, miphy_phy->base + BUF_SEL_REG);
		val = SWING_VAL | PREEMPH_VAL;
		writeb_relaxed(val, miphy_phy->base + TXBUF1_REG);
		writeb_relaxed(TXSLEW_VAL, miphy_phy->base + TXBUF2_REG);
		writeb_relaxed(0x00, miphy_phy->base + RXBUF_OFFSET_CTRL_REG);
		val = SDTHRES_VAL | EQ_ON3;
		writeb_relaxed(val, miphy_phy->base + RXBUF_REG);
		break;
	case SATA_GEN2:
		/*
		 * conf gen sel=0x1 to program Gen2 banked registers
		 * VDDT filter ON
		 * Tx Swing target 550-600mV peak-to-peak diff
		 * Tx Slew target 90-110 ps rising/falling time
		 * RX Equalization ON1, Sigdet threshold SDTH1
		 */
		writeb_relaxed(CONF_GEN_SEL_GEN2,
			       miphy_phy->base + BUF_SEL_REG);
		writeb_relaxed(SWING_VAL, miphy_phy->base + TXBUF1_REG);
		writeb_relaxed(TXSLEW_VAL, miphy_phy->base + TXBUF2_REG);
		val = SDTHRES_VAL | EQ_ON1;
		writeb_relaxed(val, miphy_phy->base + RXBUF_REG);
		break;
	case SATA_GEN1:
		/*
		 * conf gen sel = 00b to program Gen1 banked registers
		 * VDDT filter ON
		 * Tx Swing target 500-550mV peak-to-peak diff
		 * Tx Slew target120-140 ps rising/falling time
		 */
		writeb_relaxed(PD_VDDTFILTER, miphy_phy->base + BUF_SEL_REG);
		writeb_relaxed(SWING_VAL_GEN1, miphy_phy->base + TXBUF1_REG);
		writeb_relaxed(TXSLEW_VAL_GEN1,	miphy_phy->base + TXBUF2_REG);
		break;
	default:
		break;
	}

	/* Force Macro1 in partial mode & release pll cal reset */
	writeb_relaxed(RST_RX, miphy_phy->base + RESET_REG);
	usleep_range(100, 150);

	miphy365x_set_ssc(miphy_phy, miphy_dev);

	/* Wait for phy_ready */
	ret = miphy365x_rdy(miphy_phy, miphy_dev);
	if (ret)
		return ret;

	/*
	 * Enable macro1 to use rx_lspd & tx_lspd
	 * Release Rx_Clock on first I-DLL phase on macro1
	 * Assert deserializer reset
	 * des_bit_lock_en is set
	 * bit lock detection strength
	 * Deassert deserializer reset
	 */
	writeb_relaxed(0x00, miphy_phy->base + BOUNDARY1_REG);
	writeb_relaxed(0x00, miphy_phy->base + IDLL_TEST_REG);
	writeb_relaxed(RST_RX, miphy_phy->base + RESET_REG);
	val = miphy_phy->sata_tx_pol_inv ?
		(TX_POL | DES_BIT_LOCK_EN) : DES_BIT_LOCK_EN;
	writeb_relaxed(val, miphy_phy->base + CTRL_REG);

	val = BIT_LOCK_CNT_512 | BIT_LOCK_LEVEL;
	writeb_relaxed(val, miphy_phy->base + DES_BITLOCK_REG);
	writeb_relaxed(0x00, miphy_phy->base + RESET_REG);

	return 0;
}

static int miphy365x_init(struct phy *phy)
{
	struct miphy365x_phy *miphy_phy = phy_get_drvdata(phy);
	struct miphy365x_dev *miphy_dev = dev_get_drvdata(phy->dev.parent);
	int ret = 0;

	mutex_lock(&miphy_dev->miphy_mutex);

	ret = miphy365x_set_path(miphy_phy, miphy_dev);
	if (ret) {
		mutex_unlock(&miphy_dev->miphy_mutex);
		return ret;
	}

	/* Initialise Miphy for PCIe or SATA */
	if (miphy_phy->type == MIPHY_TYPE_PCIE)
		ret = miphy365x_init_pcie_port(miphy_phy, miphy_dev);
	else
		ret = miphy365x_init_sata_port(miphy_phy, miphy_dev);

	mutex_unlock(&miphy_dev->miphy_mutex);

	return ret;
}

int miphy365x_get_addr(struct device *dev, struct miphy365x_phy *miphy_phy,
		       int index)
{
	struct device_node *phynode = miphy_phy->phy->dev.of_node;
	const char *name;
	const __be32 *taddr;
	int type = miphy_phy->type;
	int ret;

	ret = of_property_read_string_index(phynode, "reg-names", index, &name);
	if (ret) {
		dev_err(dev, "no reg-names property not found\n");
		return ret;
	}

	if (!strncmp(name, "syscfg", 6)) {
		taddr = of_get_address(phynode, index, NULL, NULL);
		if (!taddr) {
			dev_err(dev, "failed to fetch syscfg address\n");
			return -EINVAL;
		}

		miphy_phy->ctrlreg = of_translate_address(phynode, taddr);
		if (miphy_phy->ctrlreg == OF_BAD_ADDR) {
			dev_err(dev, "failed to translate syscfg address\n");
			return -EINVAL;
		}

		return 0;
	}

	if (!((!strncmp(name, "sata", 4) && type == MIPHY_TYPE_SATA) ||
	      (!strncmp(name, "pcie", 4) && type == MIPHY_TYPE_PCIE)))
		return 0;

	miphy_phy->base = of_iomap(phynode, index);
	if (!miphy_phy->base) {
		dev_err(dev, "Failed to map %s\n", phynode->full_name);
		return -EINVAL;
	}

	return 0;
}

static struct phy *miphy365x_xlate(struct device *dev,
				   struct of_phandle_args *args)
{
	struct miphy365x_dev *miphy_dev = dev_get_drvdata(dev);
	struct miphy365x_phy *miphy_phy = NULL;
	struct device_node *phynode = args->np;
	int ret, index;

	if (!of_device_is_available(phynode)) {
		dev_warn(dev, "Requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}

	if (args->args_count != 1) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	for (index = 0; index < of_get_child_count(dev->of_node); index++)
		if (phynode == miphy_dev->phys[index]->phy->dev.of_node) {
			miphy_phy = miphy_dev->phys[index];
			break;
		}

	if (!miphy_phy) {
		dev_err(dev, "Failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	miphy_phy->type = args->args[0];

	if (!(miphy_phy->type == MIPHY_TYPE_SATA ||
	      miphy_phy->type == MIPHY_TYPE_PCIE)) {
		dev_err(dev, "Unsupported device type: %d\n", miphy_phy->type);
		return ERR_PTR(-EINVAL);
	}

	/* Each port handles SATA and PCIE - third entry is always sysconf. */
	for (index = 0; index < 3; index++) {
		ret = miphy365x_get_addr(dev, miphy_phy, index);
		if (ret < 0)
			return ERR_PTR(ret);
	}

	return miphy_phy->phy;
}

static struct phy_ops miphy365x_ops = {
	.init		= miphy365x_init,
	.owner		= THIS_MODULE,
};

static int miphy365x_of_probe(struct device_node *phynode,
			      struct miphy365x_phy *miphy_phy)
{
	of_property_read_u32(phynode, "st,sata-gen", &miphy_phy->sata_gen);
	if (!miphy_phy->sata_gen)
		miphy_phy->sata_gen = SATA_GEN1;

	miphy_phy->pcie_tx_pol_inv =
		of_property_read_bool(phynode, "st,pcie-tx-pol-inv");

	miphy_phy->sata_tx_pol_inv =
		of_property_read_bool(phynode, "st,sata-tx-pol-inv");

	return 0;
}

static int miphy365x_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct miphy365x_dev *miphy_dev;
	struct phy_provider *provider;
	struct phy *phy;
	int chancount, port = 0;
	int ret;

	miphy_dev = devm_kzalloc(&pdev->dev, sizeof(*miphy_dev), GFP_KERNEL);
	if (!miphy_dev)
		return -ENOMEM;

	chancount = of_get_child_count(np);
	miphy_dev->phys = devm_kzalloc(&pdev->dev, sizeof(phy) * chancount,
				       GFP_KERNEL);
	if (!miphy_dev->phys)
		return -ENOMEM;

	miphy_dev->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(miphy_dev->regmap)) {
		dev_err(miphy_dev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(miphy_dev->regmap);
	}

	miphy_dev->dev = &pdev->dev;

	dev_set_drvdata(&pdev->dev, miphy_dev);

	mutex_init(&miphy_dev->miphy_mutex);

	for_each_child_of_node(np, child) {
		struct miphy365x_phy *miphy_phy;

		miphy_phy = devm_kzalloc(&pdev->dev, sizeof(*miphy_phy),
					 GFP_KERNEL);
		if (!miphy_phy)
			return -ENOMEM;

		miphy_dev->phys[port] = miphy_phy;

		phy = devm_phy_create(&pdev->dev, child, &miphy365x_ops, NULL);
		if (IS_ERR(phy)) {
			dev_err(&pdev->dev, "failed to create PHY\n");
			return PTR_ERR(phy);
		}

		miphy_dev->phys[port]->phy = phy;

		ret = miphy365x_of_probe(child, miphy_phy);
		if (ret)
			return ret;

		phy_set_drvdata(phy, miphy_dev->phys[port]);
		port++;
	}

	provider = devm_of_phy_provider_register(&pdev->dev, miphy365x_xlate);
	if (IS_ERR(provider))
		return PTR_ERR(provider);

	return 0;
}

static const struct of_device_id miphy365x_of_match[] = {
	{ .compatible = "st,miphy365x-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, miphy365x_of_match);

static struct platform_driver miphy365x_driver = {
	.probe	= miphy365x_probe,
	.driver = {
		.name	= "miphy365x-phy",
		.owner	= THIS_MODULE,
		.of_match_table	= miphy365x_of_match,
	}
};
module_platform_driver(miphy365x_driver);

MODULE_AUTHOR("Alexandre Torgue <alexandre.torgue@st.com>");
MODULE_DESCRIPTION("STMicroelectronics miphy365x driver");
MODULE_LICENSE("GPL v2");
