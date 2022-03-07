// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "fw.h"
#include "mac.h"
#include "reg.h"
#include "rtw8852c.h"

static const struct rtw89_dle_mem rtw8852c_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_wde_size19, &rtw89_ple_size19,
			   &rtw89_wde_qt18, &rtw89_wde_qt18, &rtw89_ple_qt46,
			   &rtw89_ple_qt47},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_wde_size18,
			    &rtw89_ple_size18, &rtw89_wde_qt17, &rtw89_wde_qt17,
			    &rtw89_ple_qt44, &rtw89_ple_qt45},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const u32 rtw8852c_h2c_regs[RTW89_H2CREG_MAX] = {
	R_AX_H2CREG_DATA0_V1, R_AX_H2CREG_DATA1_V1, R_AX_H2CREG_DATA2_V1,
	R_AX_H2CREG_DATA3_V1
};

static const u32 rtw8852c_c2h_regs[RTW89_H2CREG_MAX] = {
	R_AX_C2HREG_DATA0_V1, R_AX_C2HREG_DATA1_V1, R_AX_C2HREG_DATA2_V1,
	R_AX_C2HREG_DATA3_V1
};

static const struct rtw89_page_regs rtw8852c_page_regs = {
	.hci_fc_ctrl	= R_AX_HCI_FC_CTRL_V1,
	.ch_page_ctrl	= R_AX_CH_PAGE_CTRL_V1,
	.ach_page_ctrl	= R_AX_ACH0_PAGE_CTRL_V1,
	.ach_page_info	= R_AX_ACH0_PAGE_INFO_V1,
	.pub_page_info3	= R_AX_PUB_PAGE_INFO3_V1,
	.pub_page_ctrl1	= R_AX_PUB_PAGE_CTRL1_V1,
	.pub_page_ctrl2	= R_AX_PUB_PAGE_CTRL2_V1,
	.pub_page_info1	= R_AX_PUB_PAGE_INFO1_V1,
	.pub_page_info2 = R_AX_PUB_PAGE_INFO2_V1,
	.wp_page_ctrl1	= R_AX_WP_PAGE_CTRL1_V1,
	.wp_page_ctrl2	= R_AX_WP_PAGE_CTRL2_V1,
	.wp_page_info1	= R_AX_WP_PAGE_INFO1_V1,
};

static int rtw8852c_pwr_on_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u32 ret;

	val32 = rtw89_read32_mask(rtwdev, R_AX_SYS_STATUS1, B_AX_PAD_HCI_SEL_V2_MASK);
	if (val32 == MAC_AX_HCI_SEL_PCIE_USB)
		rtw89_write32_set(rtwdev, R_AX_LDO_AON_CTRL0, B_AX_PD_REGU_L);

	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_AFSM_WLSUS_EN |
						    B_AX_AFSM_PCIE_SUS_EN);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_DIS_WLBT_PDNSUSEN_SOPC);
	rtw89_write32_set(rtwdev, R_AX_WLLPS_CTRL, B_AX_DIS_WLBT_LPSEN_LOPC);
	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APDM_HPDN);
	rtw89_write32_clr(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_AX_RDY_SYSPWR,
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_APFN_ONMAC),
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write8_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write8_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write8_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write8_clr(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);

	rtw89_write8_set(rtwdev, R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
	rtw89_write32_clr(rtwdev, R_AX_SYS_SDIO_CTRL, B_AX_PCIE_CALIB_EN_V1);

	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND, B_AX_CMAC1_FEN);
	rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND, B_AX_R_SYM_ISO_CMAC12PP);
	rtw89_write32_clr(rtwdev, R_AX_AFE_CTRL1, B_AX_R_SYM_WLCMAC1_P4_PC_EN |
						  B_AX_R_SYM_WLCMAC1_P3_PC_EN |
						  B_AX_R_SYM_WLCMAC1_P2_PC_EN |
						  B_AX_R_SYM_WLCMAC1_P1_PC_EN |
						  B_AX_R_SYM_WLCMAC1_PC_EN);
	rtw89_write32_set(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL,
				      XTAL_SI_GND_SHDN_WL, XTAL_SI_GND_SHDN_WL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL,
				      XTAL_SI_SHDN_WL, XTAL_SI_SHDN_WL);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_WEI,
				      XTAL_SI_OFF_WEI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_OFF_EI,
				      XTAL_SI_OFF_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_RFC2RF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_PON_WEI,
				      XTAL_SI_PON_WEI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_PON_EI,
				      XTAL_SI_PON_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SRAM2RFC);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_XMD_2, 0, XTAL_SI_LDO_LPS);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_XTAL_XMD_4, 0, XTAL_SI_LPS_CAP);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	rtw89_write32_set(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);

	fsleep(1000);

	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
	rtw89_write32_clr(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	rtw89_write32_set(rtwdev, R_AX_GPIO0_15_EECS_EESK_LED1_PULL_LOW_EN,
			  B_AX_EECS_PULL_LOW_EN | B_AX_EESK_PULL_LOW_EN |
			  B_AX_LED1_PULL_LOW_EN);

	rtw89_write32_set(rtwdev, R_AX_DMAC_FUNC_EN,
			  B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN |
			  B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN | B_AX_TXPKT_CTRL_EN |
			  B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN |
			  B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN | B_AX_DLE_CPUIO_EN |
			  B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN |
			  B_AX_MAC_UN_EN | B_AX_H_AXIDMA_EN);

	rtw89_write32_set(rtwdev, R_AX_CMAC_FUNC_EN,
			  B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
			  B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN |
			  B_AX_CMAC_DMA_EN | B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN |
			  B_AX_TMAC_EN | B_AX_RMAC_EN);

	return 0;
}

static int rtw8852c_pwr_off_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u32 ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_RFC2RF,
				      XTAL_SI_RFC2RF);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_OFF_WEI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0, XTAL_SI_RF00);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0, XTAL_SI_RF10);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, XTAL_SI_SRAM2RFC,
				      XTAL_SI_SRAM2RFC);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_EI);
	if (ret)
		return ret;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_PON_WEI);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_EN_WLON);
	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN, B_AX_FEN_BB_GLB_RSTN | B_AX_FEN_BBRSTB);
	rtw89_write32_clr(rtwdev, R_AX_SYS_ISO_CTRL_EXTEND,
			  B_AX_R_SYM_FEN_WLBBGLB_1 | B_AX_R_SYM_FEN_WLBBFUN_1);
	rtw89_write32_clr(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_RFC_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_SHDN_WL);
	if (ret)
		return ret;

	rtw89_write32_clr(rtwdev, R_AX_SYS_ADIE_PAD_PWR_CTRL, B_AX_SYM_PADPDN_WL_PTA_1P3);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_ANAPAR_WL, 0, XTAL_SI_GND_SHDN_WL);
	if (ret)
		return ret;

	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_OFFMAC);

	ret = read_poll_timeout(rtw89_read32, val32, !(val32 & B_AX_APFM_OFFMAC),
				1000, 20000, false, rtwdev, R_AX_SYS_PW_CTRL);
	if (ret)
		return ret;

	rtw89_write32(rtwdev, R_AX_WLLPS_CTRL, 0x0001A0B0);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_XTAL_OFF_A_DIE);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	return 0;
}

static const struct rtw89_chip_ops rtw8852c_chip_ops = {
	.pwr_on_func		= rtw8852c_pwr_on_func,
	.pwr_off_func		= rtw8852c_pwr_off_func,
};

const struct rtw89_chip_info rtw8852c_chip_info = {
	.chip_id		= RTL8852C,
	.ops			= &rtw8852c_chip_ops,
	.fw_name		= "rtw89/rtw8852c_fw.bin",
	.dle_mem		= rtw8852c_dle_mem_pcie,
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 96,
	.dav_log_efuse_size	= 16,
	.hci_func_en_addr	= R_AX_HCI_FUNC_EN_V1,
	.h2c_ctrl_reg		= R_AX_H2CREG_CTRL_V1,
	.h2c_regs		= rtw8852c_h2c_regs,
	.c2h_ctrl_reg		= R_AX_C2HREG_CTRL_V1,
	.c2h_regs		= rtw8852c_c2h_regs,
	.page_regs		= &rtw8852c_page_regs,
};
EXPORT_SYMBOL(rtw8852c_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852c_fw.bin");
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852C driver");
MODULE_LICENSE("Dual BSD/GPL");
