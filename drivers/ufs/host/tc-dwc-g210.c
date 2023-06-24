// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys G210 Test Chip driver
 *
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 *
 * Authors: Joao Pinto <jpinto@synopsys.com>
 */

#include <linux/module.h>

#include <ufs/ufshcd.h>
#include <ufs/unipro.h>

#include "ufshcd-dwc.h"
#include "ufshci-dwc.h"
#include "tc-dwc-g210.h"

/**
 * tc_dwc_g210_setup_40bit_rmmi()
 * This function configures Synopsys TC specific atributes (40-bit RMMI)
 * @hba: Pointer to drivers structure
 *
 * Returns 0 on success or non-zero value on failure
 */
static int tc_dwc_g210_setup_40bit_rmmi(struct ufs_hba *hba)
{
	static const struct ufshcd_dme_attr_val setup_attrs[] = {
		{ UIC_ARG_MIB(TX_GLOBALHIBERNATE), 0x00, DME_LOCAL },
		{ UIC_ARG_MIB(REFCLKMODE), 0x01, DME_LOCAL },
		{ UIC_ARG_MIB(CDIRECTCTRL6), 0x80, DME_LOCAL },
		{ UIC_ARG_MIB(CBDIVFACTOR), 0x08, DME_LOCAL },
		{ UIC_ARG_MIB(CBDCOCTRL5), 0x64, DME_LOCAL },
		{ UIC_ARG_MIB(CBPRGTUNING), 0x09, DME_LOCAL },
		{ UIC_ARG_MIB(RTOBSERVESELECT), 0x00, DME_LOCAL },
		{ UIC_ARG_MIB_SEL(TX_REFCLKFREQ, SELIND_LN0_TX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(TX_CFGCLKFREQVAL, SELIND_LN0_TX), 0x19,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGEXTRATTR, SELIND_LN0_TX), 0x14,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(DITHERCTRL2, SELIND_LN0_TX), 0xd6,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RX_REFCLKFREQ, SELIND_LN0_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RX_CFGCLKFREQVAL, SELIND_LN0_RX), 0x19,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGWIDEINLN, SELIND_LN0_RX), 4,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXCDR8, SELIND_LN0_RX), 0x80,
								DME_LOCAL },
		{ UIC_ARG_MIB(DIRECTCTRL10), 0x04, DME_LOCAL },
		{ UIC_ARG_MIB(DIRECTCTRL19), 0x02, DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXCDR8, SELIND_LN0_RX), 0x80,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG4, SELIND_LN0_RX), 0x03,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR8, SELIND_LN0_RX), 0x16,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXDIRECTCTRL2, SELIND_LN0_RX), 0x42,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG3, SELIND_LN0_RX), 0xa4,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXCALCTRL, SELIND_LN0_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG2, SELIND_LN0_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR4, SELIND_LN0_RX), 0x28,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXSQCTRL, SELIND_LN0_RX), 0x1E,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR6, SELIND_LN0_RX), 0x2f,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR6, SELIND_LN0_RX), 0x2f,
								DME_LOCAL },
		{ UIC_ARG_MIB(CBPRGPLL2), 0x00, DME_LOCAL },
	};

	return ufshcd_dwc_dme_set_attrs(hba, setup_attrs,
						ARRAY_SIZE(setup_attrs));
}

/**
 * tc_dwc_g210_setup_20bit_rmmi_lane0()
 * This function configures Synopsys TC 20-bit RMMI Lane 0
 * @hba: Pointer to drivers structure
 *
 * Returns 0 on success or non-zero value on failure
 */
static int tc_dwc_g210_setup_20bit_rmmi_lane0(struct ufs_hba *hba)
{
	static const struct ufshcd_dme_attr_val setup_attrs[] = {
		{ UIC_ARG_MIB_SEL(TX_REFCLKFREQ, SELIND_LN0_TX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(TX_CFGCLKFREQVAL, SELIND_LN0_TX), 0x19,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RX_CFGCLKFREQVAL, SELIND_LN0_RX), 0x19,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGEXTRATTR, SELIND_LN0_TX), 0x12,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(DITHERCTRL2, SELIND_LN0_TX), 0xd6,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RX_REFCLKFREQ, SELIND_LN0_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGWIDEINLN, SELIND_LN0_RX), 2,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXCDR8, SELIND_LN0_RX), 0x80,
								DME_LOCAL },
		{ UIC_ARG_MIB(DIRECTCTRL10), 0x04, DME_LOCAL },
		{ UIC_ARG_MIB(DIRECTCTRL19), 0x02, DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG4, SELIND_LN0_RX), 0x03,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR8, SELIND_LN0_RX), 0x16,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXDIRECTCTRL2, SELIND_LN0_RX), 0x42,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG3, SELIND_LN0_RX), 0xa4,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXCALCTRL, SELIND_LN0_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG2, SELIND_LN0_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR4, SELIND_LN0_RX), 0x28,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXSQCTRL, SELIND_LN0_RX), 0x1E,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR6, SELIND_LN0_RX), 0x2f,
								DME_LOCAL },
		{ UIC_ARG_MIB(CBPRGPLL2), 0x00, DME_LOCAL },
	};

	return ufshcd_dwc_dme_set_attrs(hba, setup_attrs,
						ARRAY_SIZE(setup_attrs));
}

/**
 * tc_dwc_g210_setup_20bit_rmmi_lane1()
 * This function configures Synopsys TC 20-bit RMMI Lane 1
 * @hba: Pointer to drivers structure
 *
 * Returns 0 on success or non-zero value on failure
 */
static int tc_dwc_g210_setup_20bit_rmmi_lane1(struct ufs_hba *hba)
{
	int connected_rx_lanes = 0;
	int connected_tx_lanes = 0;
	int ret = 0;

	static const struct ufshcd_dme_attr_val setup_tx_attrs[] = {
		{ UIC_ARG_MIB_SEL(TX_REFCLKFREQ, SELIND_LN1_TX), 0x0d,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(TX_CFGCLKFREQVAL, SELIND_LN1_TX), 0x19,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGEXTRATTR, SELIND_LN1_TX), 0x12,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(DITHERCTRL2, SELIND_LN0_TX), 0xd6,
								DME_LOCAL },
	};

	static const struct ufshcd_dme_attr_val setup_rx_attrs[] = {
		{ UIC_ARG_MIB_SEL(RX_REFCLKFREQ, SELIND_LN1_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RX_CFGCLKFREQVAL, SELIND_LN1_RX), 0x19,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGWIDEINLN, SELIND_LN1_RX), 2,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXCDR8, SELIND_LN1_RX), 0x80,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG4, SELIND_LN1_RX), 0x03,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR8, SELIND_LN1_RX), 0x16,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXDIRECTCTRL2, SELIND_LN1_RX), 0x42,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG3, SELIND_LN1_RX), 0xa4,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXCALCTRL, SELIND_LN1_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(ENARXDIRECTCFG2, SELIND_LN1_RX), 0x01,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR4, SELIND_LN1_RX), 0x28,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(RXSQCTRL, SELIND_LN1_RX), 0x1E,
								DME_LOCAL },
		{ UIC_ARG_MIB_SEL(CFGRXOVR6, SELIND_LN1_RX), 0x2f,
								DME_LOCAL },
	};

	/* Get the available lane count */
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_AVAILRXDATALANES),
			&connected_rx_lanes);
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_AVAILTXDATALANES),
			&connected_tx_lanes);

	if (connected_tx_lanes == 2) {

		ret = ufshcd_dwc_dme_set_attrs(hba, setup_tx_attrs,
						ARRAY_SIZE(setup_tx_attrs));

		if (ret)
			goto out;
	}

	if (connected_rx_lanes == 2) {
		ret = ufshcd_dwc_dme_set_attrs(hba, setup_rx_attrs,
						ARRAY_SIZE(setup_rx_attrs));
	}

out:
	return ret;
}

/**
 * tc_dwc_g210_setup_20bit_rmmi()
 * This function configures Synopsys TC specific atributes (20-bit RMMI)
 * @hba: Pointer to drivers structure
 *
 * Returns 0 on success or non-zero value on failure
 */
static int tc_dwc_g210_setup_20bit_rmmi(struct ufs_hba *hba)
{
	int ret = 0;

	static const struct ufshcd_dme_attr_val setup_attrs[] = {
		{ UIC_ARG_MIB(TX_GLOBALHIBERNATE), 0x00, DME_LOCAL },
		{ UIC_ARG_MIB(REFCLKMODE), 0x01, DME_LOCAL },
		{ UIC_ARG_MIB(CDIRECTCTRL6), 0xc0, DME_LOCAL },
		{ UIC_ARG_MIB(CBDIVFACTOR), 0x44, DME_LOCAL },
		{ UIC_ARG_MIB(CBDCOCTRL5), 0x64, DME_LOCAL },
		{ UIC_ARG_MIB(CBPRGTUNING), 0x09, DME_LOCAL },
		{ UIC_ARG_MIB(RTOBSERVESELECT), 0x00, DME_LOCAL },
	};

	ret = ufshcd_dwc_dme_set_attrs(hba, setup_attrs,
						ARRAY_SIZE(setup_attrs));
	if (ret)
		goto out;

	/* Lane 0 configuration*/
	ret = tc_dwc_g210_setup_20bit_rmmi_lane0(hba);
	if (ret)
		goto out;

	/* Lane 1 configuration*/
	ret = tc_dwc_g210_setup_20bit_rmmi_lane1(hba);
	if (ret)
		goto out;

out:
	return ret;
}

/**
 * tc_dwc_g210_config_40_bit()
 * This function configures Local (host) Synopsys 40-bit TC specific attributes
 *
 * @hba: Pointer to drivers structure
 *
 * Returns 0 on success non-zero value on failure
 */
int tc_dwc_g210_config_40_bit(struct ufs_hba *hba)
{
	int ret = 0;

	dev_info(hba->dev, "Configuring Test Chip 40-bit RMMI\n");
	ret = tc_dwc_g210_setup_40bit_rmmi(hba);
	if (ret) {
		dev_err(hba->dev, "Configuration failed\n");
		goto out;
	}

	/* To write Shadow register bank to effective configuration block */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);
	if (ret)
		goto out;

	/* To configure Debug OMC */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_DEBUGOMC), 0x01);

out:
	return ret;
}
EXPORT_SYMBOL(tc_dwc_g210_config_40_bit);

/**
 * tc_dwc_g210_config_20_bit()
 * This function configures Local (host) Synopsys 20-bit TC specific attributes
 *
 * @hba: Pointer to drivers structure
 *
 * Returns 0 on success non-zero value on failure
 */
int tc_dwc_g210_config_20_bit(struct ufs_hba *hba)
{
	int ret = 0;

	dev_info(hba->dev, "Configuring Test Chip 20-bit RMMI\n");
	ret = tc_dwc_g210_setup_20bit_rmmi(hba);
	if (ret) {
		dev_err(hba->dev, "Configuration failed\n");
		goto out;
	}

	/* To write Shadow register bank to effective configuration block */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_MPHYCFGUPDT), 0x01);
	if (ret)
		goto out;

	/* To configure Debug OMC */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(VS_DEBUGOMC), 0x01);

out:
	return ret;
}
EXPORT_SYMBOL(tc_dwc_g210_config_20_bit);

MODULE_AUTHOR("Joao Pinto <Joao.Pinto@synopsys.com>");
MODULE_DESCRIPTION("Synopsys G210 Test Chip driver");
MODULE_LICENSE("Dual BSD/GPL");
