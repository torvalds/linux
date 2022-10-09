// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */

#include "coex.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852b.h"
#include "rtw8852b_rfk.h"
#include "rtw8852b_table.h"
#include "txrx.h"

static const struct rtw89_hfc_ch_cfg rtw8852b_hfc_chcfg_pcie[] = {
	{5, 343, grp_0}, /* ACH 0 */
	{5, 343, grp_0}, /* ACH 1 */
	{5, 343, grp_0}, /* ACH 2 */
	{5, 343, grp_0}, /* ACH 3 */
	{0, 0, grp_0}, /* ACH 4 */
	{0, 0, grp_0}, /* ACH 5 */
	{0, 0, grp_0}, /* ACH 6 */
	{0, 0, grp_0}, /* ACH 7 */
	{4, 344, grp_0}, /* B0MGQ */
	{4, 344, grp_0}, /* B0HIQ */
	{0, 0, grp_0}, /* B1MGQ */
	{0, 0, grp_0}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8852b_hfc_pubcfg_pcie = {
	448, /* Group 0 */
	0, /* Group 1 */
	448, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8852b_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8852b_hfc_chcfg_pcie, &rtw8852b_hfc_pubcfg_pcie,
			   &rtw89_mac_size.hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw89_mac_size.hfc_preccfg_pcie,
			    RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8852b_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &rtw89_mac_size.wde_size6,
			   &rtw89_mac_size.ple_size6, &rtw89_mac_size.wde_qt6,
			   &rtw89_mac_size.wde_qt6, &rtw89_mac_size.ple_qt18,
			   &rtw89_mac_size.ple_qt58},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &rtw89_mac_size.wde_size9,
			    &rtw89_mac_size.ple_size8, &rtw89_mac_size.wde_qt4,
			    &rtw89_mac_size.wde_qt4, &rtw89_mac_size.ple_qt13,
			    &rtw89_mac_size.ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static int rtw8852b_pwr_on_func(struct rtw89_dev *rtwdev)
{
	u32 val32;
	u32 ret;

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

	rtw89_write32_set(rtwdev, R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN);
	ret = read_poll_timeout(rtw89_read32, val32, val32 & B_AX_AON_OFF_PC_EN,
				1000, 20000, false, rtwdev, R_AX_AFE_LDO_CTRL);
	if (ret)
		return ret;

	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_OFF_CTRL0, B_AX_C1_L1_MASK, 0x1);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_OFF_CTRL0, B_AX_C3_L1_MASK, 0x3);
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
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_SRAM_CTRL, 0, XTAL_SI_SRAM_DIS);
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

	if (!rtwdev->efuse.valid || rtwdev->efuse.power_k_valid)
		goto func_en;

	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_VOL_L1_MASK, 0x9);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_VREFPFM_L_MASK, 0xA);

	if (rtwdev->hal.cv == CHIP_CBV) {
		rtw89_write32_set(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
		rtw89_write16_mask(rtwdev, R_AX_HCI_LDO_CTRL, B_AX_R_AX_VADJ_MASK, 0xA);
		rtw89_write32_clr(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	}

func_en:
	rtw89_write32_set(rtwdev, R_AX_DMAC_FUNC_EN,
			  B_AX_MAC_FUNC_EN | B_AX_DMAC_FUNC_EN | B_AX_MPDU_PROC_EN |
			  B_AX_WD_RLS_EN | B_AX_DLE_WDE_EN | B_AX_TXPKT_CTRL_EN |
			  B_AX_STA_SCH_EN | B_AX_DLE_PLE_EN | B_AX_PKT_BUF_EN |
			  B_AX_DMAC_TBL_EN | B_AX_PKT_IN_EN | B_AX_DLE_CPUIO_EN |
			  B_AX_DISPATCHER_EN | B_AX_BBRPT_EN | B_AX_MAC_SEC_EN |
			  B_AX_DMACREG_GCKEN);
	rtw89_write32_set(rtwdev, R_AX_CMAC_FUNC_EN,
			  B_AX_CMAC_EN | B_AX_CMAC_TXEN | B_AX_CMAC_RXEN |
			  B_AX_FORCE_CMACREG_GCKEN | B_AX_PHYINTF_EN | B_AX_CMAC_DMA_EN |
			  B_AX_PTCLTOP_EN | B_AX_SCHEDULER_EN | B_AX_TMAC_EN |
			  B_AX_RMAC_EN);

	rtw89_write32_mask(rtwdev, R_AX_EECS_EESK_FUNC_SEL, B_AX_PINMUX_EESK_FUNC_SEL_MASK,
			   PINMUX_EESK_FUNC_SEL_BT_LOG);

	return 0;
}

static int rtw8852b_pwr_off_func(struct rtw89_dev *rtwdev)
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

	rtw89_write32(rtwdev, R_AX_WLLPS_CTRL, SW_LPS_OPTION);
	rtw89_write32_set(rtwdev, R_AX_SYS_SWR_CTRL1, B_AX_SYM_CTRL_SPS_PWMFREQ);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x3);
	rtw89_write32_set(rtwdev, R_AX_SYS_PW_CTRL, B_AX_APFM_SWLPS);

	return 0;
}

static void rtw8852be_efuse_parsing(struct rtw89_efuse *efuse,
				    struct rtw8852b_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
}

static void rtw8852b_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					struct rtw8852b_efuse *map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	struct rtw8852b_tssi_offset *ofst[] = {&map->path_a_tssi, &map->path_b_tssi};
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;
	tssi->thermal[RF_PATH_B] = map->path_b_therm;

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		memcpy(tssi->tssi_cck[i], ofst[i]->cck_tssi,
		       sizeof(ofst[i]->cck_tssi));

		for (j = 0; j < TSSI_CCK_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d cck[%d]=0x%x\n",
				    i, j, tssi->tssi_cck[i][j]);

		memcpy(tssi->tssi_mcs[i], ofst[i]->bw40_tssi,
		       sizeof(ofst[i]->bw40_tssi));
		memcpy(tssi->tssi_mcs[i] + TSSI_MCS_2G_CH_GROUP_NUM,
		       ofst[i]->bw40_1s_tssi_5g, sizeof(ofst[i]->bw40_1s_tssi_5g));

		for (j = 0; j < TSSI_MCS_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI][EFUSE] path=%d mcs[%d]=0x%x\n",
				    i, j, tssi->tssi_mcs[i][j]);
	}
}

static bool _decode_efuse_gain(u8 data, s8 *high, s8 *low)
{
	if (high)
		*high = sign_extend32(FIELD_GET(GENMASK(7,  4), data), 3);
	if (low)
		*low = sign_extend32(FIELD_GET(GENMASK(3,  0), data), 3);

	return data != 0xff;
}

static void rtw8852b_efuse_parsing_gain_offset(struct rtw89_dev *rtwdev,
					       struct rtw8852b_efuse *map)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	bool valid = false;

	valid |= _decode_efuse_gain(map->rx_gain_2g_cck,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_CCK],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_2G_CCK]);
	valid |= _decode_efuse_gain(map->rx_gain_2g_ofdm,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_2G_OFDM],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_2G_OFDM]);
	valid |= _decode_efuse_gain(map->rx_gain_5g_low,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_LOW],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_LOW]);
	valid |= _decode_efuse_gain(map->rx_gain_5g_mid,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_MID],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_MID]);
	valid |= _decode_efuse_gain(map->rx_gain_5g_high,
				    &gain->offset[RF_PATH_A][RTW89_GAIN_OFFSET_5G_HIGH],
				    &gain->offset[RF_PATH_B][RTW89_GAIN_OFFSET_5G_HIGH]);

	gain->offset_valid = valid;
}

static int rtw8852b_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw8852b_efuse *map;

	map = (struct rtw8852b_efuse *)log_map;

	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	rtw8852b_efuse_parsing_tssi(rtwdev, map);
	rtw8852b_efuse_parsing_gain_offset(rtwdev, map);

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		rtw8852be_efuse_parsing(efuse, map);
		break;
	default:
		return -EOPNOTSUPP;
	}

	rtw89_info(rtwdev, "chip rfe_type is %d\n", efuse->rfe_type);

	return 0;
}

static void rtw8852b_phycap_parsing_power_cal(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
#define PWR_K_CHK_OFFSET 0x5E9
#define PWR_K_CHK_VALUE 0xAA
	u32 offset = PWR_K_CHK_OFFSET - rtwdev->chip->phycap_addr;

	if (phycap_map[offset] == PWR_K_CHK_VALUE)
		rtwdev->efuse.power_k_valid = true;
}

static void rtw8852b_phycap_parsing_tssi(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	static const u32 tssi_trim_addr[RF_PATH_NUM_8852B] = {0x5D6, 0x5AB};
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = false;
	u32 ofst;
	u8 i, j;

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++) {
			/* addrs are in decreasing order */
			ofst = tssi_trim_addr[i] - addr - j;
			tssi->tssi_trim[i][j] = phycap_map[ofst];

			if (phycap_map[ofst] != 0xff)
				pg = true;
		}
	}

	if (!pg) {
		memset(tssi->tssi_trim, 0, sizeof(tssi->tssi_trim));
		rtw89_debug(rtwdev, RTW89_DBG_TSSI,
			    "[TSSI][TRIM] no PG, set all trim info to 0\n");
	}

	for (i = 0; i < RF_PATH_NUM_8852B; i++)
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI] path=%d idx=%d trim=0x%x addr=0x%x\n",
				    i, j, tssi->tssi_trim[i][j],
				    tssi_trim_addr[i] - j);
}

static void rtw8852b_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 thm_trim_addr[RF_PATH_NUM_8852B] = {0x5DF, 0x5DC};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		info->thermal_trim[i] = phycap_map[thm_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x\n",
			    i, info->thermal_trim[i]);

		if (info->thermal_trim[i] != 0xff)
			info->pg_thermal_trim = true;
	}
}

static void rtw8852b_thermal_trim(struct rtw89_dev *rtwdev)
{
#define __thm_setting(raw)				\
({							\
	u8 __v = (raw);					\
	((__v & 0x1) << 3) | ((__v & 0x1f) >> 1);	\
})
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 i, val;

	if (!info->pg_thermal_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		val = __thm_setting(info->thermal_trim[i]);
		rtw89_write_rf(rtwdev, i, RR_TM2, RR_TM2_OFF, val);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_setting=0x%x\n",
			    i, val);
	}
#undef __thm_setting
}

static void rtw8852b_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 pabias_trim_addr[RF_PATH_NUM_8852B] = {0x5DE, 0x5DB};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);

		if (info->pa_bias_trim[i] != 0xff)
			info->pg_pa_bias_trim = true;
	}
}

static void rtw8852b_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8852B; i++) {
		pabias_2g = FIELD_GET(GENMASK(3, 0), info->pa_bias_trim[i]);
		pabias_5g = FIELD_GET(GENMASK(7, 4), info->pa_bias_trim[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA, pabias_5g);
	}
}

static void rtw8852b_phycap_parsing_gain_comp(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	static const u32 comp_addrs[][RTW89_SUBBAND_2GHZ_5GHZ_NR] = {
		{0x5BB, 0x5BA, 0, 0x5B9, 0x5B8},
		{0x590, 0x58F, 0, 0x58E, 0x58D},
	};
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;
	u32 phycap_addr = rtwdev->chip->phycap_addr;
	bool valid = false;
	int path, i;
	u8 data;

	for (path = 0; path < 2; path++)
		for (i = 0; i < RTW89_SUBBAND_2GHZ_5GHZ_NR; i++) {
			if (comp_addrs[path][i] == 0)
				continue;

			data = phycap_map[comp_addrs[path][i] - phycap_addr];
			valid |= _decode_efuse_gain(data, NULL,
						    &gain->comp[path][i]);
		}

	gain->comp_valid = valid;
}

static int rtw8852b_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8852b_phycap_parsing_power_cal(rtwdev, phycap_map);
	rtw8852b_phycap_parsing_tssi(rtwdev, phycap_map);
	rtw8852b_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8852b_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);
	rtw8852b_phycap_parsing_gain_comp(rtwdev, phycap_map);

	return 0;
}

static void rtw8852b_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8852b_thermal_trim(rtwdev);
	rtw8852b_pa_bias_trim(rtwdev);
}

static void rtw8852b_set_channel_mac(struct rtw89_dev *rtwdev,
				     const struct rtw89_chan *chan,
				     u8 mac_idx)
{
	u32 rf_mod = rtw89_mac_reg_by_idx(R_AX_WMAC_RFMOD, mac_idx);
	u32 sub_carr = rtw89_mac_reg_by_idx(R_AX_TX_SUB_CARRIER_VALUE, mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(R_AX_TXRATE_CHK, mac_idx);
	u8 txsc20 = 0, txsc40 = 0;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, chan, RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, BIT(1));
		rtw89_write32(rtwdev, sub_carr, txsc20 | (txsc40 << 4));
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_write8_mask(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK, BIT(0));
		rtw89_write32(rtwdev, sub_carr, txsc20);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_write8_clr(rtwdev, rf_mod, B_AX_WMAC_RFMOD_MASK);
		rtw89_write32(rtwdev, sub_carr, 0);
		break;
	default:
		break;
	}

	if (chan->channel > 14) {
		rtw89_write8_clr(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_set(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	} else {
		rtw89_write8_set(rtwdev, chk_rate, B_AX_BAND_MODE);
		rtw89_write8_clr(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	}
}

static const u32 rtw8852b_sco_barker_threshold[14] = {
	0x1cfea, 0x1d0e1, 0x1d1d7, 0x1d2cd, 0x1d3c3, 0x1d4b9, 0x1d5b0, 0x1d6a6,
	0x1d79c, 0x1d892, 0x1d988, 0x1da7f, 0x1db75, 0x1ddc4
};

static const u32 rtw8852b_sco_cck_threshold[14] = {
	0x27de3, 0x27f35, 0x28088, 0x281da, 0x2832d, 0x2847f, 0x285d2, 0x28724,
	0x28877, 0x289c9, 0x28b1c, 0x28c6e, 0x28dc1, 0x290ed
};

static void rtw8852b_ctrl_sco_cck(struct rtw89_dev *rtwdev, u8 primary_ch)
{
	u8 ch_element = primary_ch - 1;

	rtw89_phy_write32_mask(rtwdev, R_RXSCOBC, B_RXSCOBC_TH,
			       rtw8852b_sco_barker_threshold[ch_element]);
	rtw89_phy_write32_mask(rtwdev, R_RXSCOCCK, B_RXSCOCCK_TH,
			       rtw8852b_sco_cck_threshold[ch_element]);
}

static u8 rtw8852b_sco_mapping(u8 central_ch)
{
	if (central_ch == 1)
		return 109;
	else if (central_ch >= 2 && central_ch <= 6)
		return 108;
	else if (central_ch >= 7 && central_ch <= 10)
		return 107;
	else if (central_ch >= 11 && central_ch <= 14)
		return 106;
	else if (central_ch == 36 || central_ch == 38)
		return 51;
	else if (central_ch >= 40 && central_ch <= 58)
		return 50;
	else if (central_ch >= 60 && central_ch <= 64)
		return 49;
	else if (central_ch == 100 || central_ch == 102)
		return 48;
	else if (central_ch >= 104 && central_ch <= 126)
		return 47;
	else if (central_ch >= 128 && central_ch <= 151)
		return 46;
	else if (central_ch >= 153 && central_ch <= 177)
		return 45;
	else
		return 0;
}

struct rtw8852b_bb_gain {
	u32 gain_g[BB_PATH_NUM_8852B];
	u32 gain_a[BB_PATH_NUM_8852B];
	u32 gain_mask;
};

static const struct rtw8852b_bb_gain bb_gain_lna[LNA_GAIN_NUM] = {
	{ .gain_g = {0x4678, 0x475C}, .gain_a = {0x45DC, 0x4740},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4678, 0x475C}, .gain_a = {0x45DC, 0x4740},
	  .gain_mask = 0xff000000 },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0x000000ff },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0x0000ff00 },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x467C, 0x4760}, .gain_a = {0x4660, 0x4744},
	  .gain_mask = 0xff000000 },
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0x000000ff },
};

static const struct rtw8852b_bb_gain bb_gain_tia[TIA_GAIN_NUM] = {
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0x00ff0000 },
	{ .gain_g = {0x4680, 0x4764}, .gain_a = {0x4664, 0x4748},
	  .gain_mask = 0xff000000 },
};

static void rtw8852b_set_gain_error(struct rtw89_dev *rtwdev,
				    enum rtw89_subband subband,
				    enum rtw89_rf_path path)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain;
	u8 gain_band = rtw89_subband_to_bb_gain_band(subband);
	s32 val;
	u32 reg;
	u32 mask;
	int i;

	for (i = 0; i < LNA_GAIN_NUM; i++) {
		if (subband == RTW89_CH_2G)
			reg = bb_gain_lna[i].gain_g[path];
		else
			reg = bb_gain_lna[i].gain_a[path];

		mask = bb_gain_lna[i].gain_mask;
		val = gain->lna_gain[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);
	}

	for (i = 0; i < TIA_GAIN_NUM; i++) {
		if (subband == RTW89_CH_2G)
			reg = bb_gain_tia[i].gain_g[path];
		else
			reg = bb_gain_tia[i].gain_a[path];

		mask = bb_gain_tia[i].gain_mask;
		val = gain->tia_gain[gain_band][path][i];
		rtw89_phy_write32_mask(rtwdev, reg, mask, val);
	}
}

static void rtw8852b_set_gain_offset(struct rtw89_dev *rtwdev,
				     enum rtw89_subband subband,
				     enum rtw89_phy_idx phy_idx)
{
	static const u32 gain_err_addr[2] = {R_P0_AGC_RSVD, R_P1_AGC_RSVD};
	static const u32 rssi_ofst_addr[2] = {R_PATH0_G_TIA1_LNA6_OP1DB_V1,
					      R_PATH1_G_TIA1_LNA6_OP1DB_V1};
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_phy_efuse_gain *efuse_gain = &rtwdev->efuse_gain;
	enum rtw89_gain_offset gain_ofdm_band;
	s32 offset_a, offset_b;
	s32 offset_ofdm, offset_cck;
	s32 tmp;
	u8 path;

	if (!efuse_gain->comp_valid)
		goto next;

	for (path = RF_PATH_A; path < BB_PATH_NUM_8852B; path++) {
		tmp = efuse_gain->comp[path][subband];
		tmp = clamp_t(s32, tmp << 2, S8_MIN, S8_MAX);
		rtw89_phy_write32_mask(rtwdev, gain_err_addr[path], MASKBYTE0, tmp);
	}

next:
	if (!efuse_gain->offset_valid)
		return;

	gain_ofdm_band = rtw89_subband_to_gain_offset_band_of_ofdm(subband);

	offset_a = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
	offset_b = -efuse_gain->offset[RF_PATH_B][gain_ofdm_band];

	tmp = -((offset_a << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_A], B_PATH0_R_G_OFST_MASK, tmp);

	tmp = -((offset_b << 2) + (efuse_gain->offset_base[RTW89_PHY_0] >> 2));
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_mask(rtwdev, rssi_ofst_addr[RF_PATH_B], B_PATH0_R_G_OFST_MASK, tmp);

	if (hal->antenna_rx == RF_B) {
		offset_ofdm = -efuse_gain->offset[RF_PATH_B][gain_ofdm_band];
		offset_cck = -efuse_gain->offset[RF_PATH_B][0];
	} else {
		offset_ofdm = -efuse_gain->offset[RF_PATH_A][gain_ofdm_band];
		offset_cck = -efuse_gain->offset[RF_PATH_A][0];
	}

	tmp = (offset_ofdm << 4) + efuse_gain->offset_base[RTW89_PHY_0];
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	tmp = (offset_ofdm << 4) + efuse_gain->rssi_base[RTW89_PHY_0];
	tmp = clamp_t(s32, tmp, S8_MIN, S8_MAX);
	rtw89_phy_write32_idx(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK, tmp, phy_idx);

	if (subband == RTW89_CH_2G) {
		tmp = (offset_cck << 3) + (efuse_gain->offset_base[RTW89_PHY_0] >> 1);
		tmp = clamp_t(s32, tmp, S8_MIN >> 1, S8_MAX >> 1);
		rtw89_phy_write32_mask(rtwdev, R_RX_RPL_OFST,
				       B_RX_RPL_OFST_CCK_MASK, tmp);
	}
}

static
void rtw8852b_set_rxsc_rpl_comp(struct rtw89_dev *rtwdev, enum rtw89_subband subband)
{
	const struct rtw89_phy_bb_gain_info *gain = &rtwdev->bb_gain;
	u8 band = rtw89_subband_to_bb_gain_band(subband);
	u32 val;

	val = FIELD_PREP(B_P0_RPL1_20_MASK, (gain->rpl_ofst_20[band][RF_PATH_A] +
					     gain->rpl_ofst_20[band][RF_PATH_B]) / 2) |
	      FIELD_PREP(B_P0_RPL1_40_MASK, (gain->rpl_ofst_40[band][RF_PATH_A][0] +
					     gain->rpl_ofst_40[band][RF_PATH_B][0]) / 2) |
	      FIELD_PREP(B_P0_RPL1_41_MASK, (gain->rpl_ofst_40[band][RF_PATH_A][1] +
					     gain->rpl_ofst_40[band][RF_PATH_B][1]) / 2);
	val >>= B_P0_RPL1_SHIFT;
	rtw89_phy_write32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_MASK, val);
	rtw89_phy_write32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_MASK, val);

	val = FIELD_PREP(B_P0_RTL2_42_MASK, (gain->rpl_ofst_40[band][RF_PATH_A][2] +
					     gain->rpl_ofst_40[band][RF_PATH_B][2]) / 2) |
	      FIELD_PREP(B_P0_RTL2_80_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][0] +
					     gain->rpl_ofst_80[band][RF_PATH_B][0]) / 2) |
	      FIELD_PREP(B_P0_RTL2_81_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][1] +
					     gain->rpl_ofst_80[band][RF_PATH_B][1]) / 2) |
	      FIELD_PREP(B_P0_RTL2_8A_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][10] +
					     gain->rpl_ofst_80[band][RF_PATH_B][10]) / 2);
	rtw89_phy_write32(rtwdev, R_P0_RPL2, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL2, val);

	val = FIELD_PREP(B_P0_RTL3_82_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][2] +
					     gain->rpl_ofst_80[band][RF_PATH_B][2]) / 2) |
	      FIELD_PREP(B_P0_RTL3_83_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][3] +
					     gain->rpl_ofst_80[band][RF_PATH_B][3]) / 2) |
	      FIELD_PREP(B_P0_RTL3_84_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][4] +
					     gain->rpl_ofst_80[band][RF_PATH_B][4]) / 2) |
	      FIELD_PREP(B_P0_RTL3_89_MASK, (gain->rpl_ofst_80[band][RF_PATH_A][9] +
					     gain->rpl_ofst_80[band][RF_PATH_B][9]) / 2);
	rtw89_phy_write32(rtwdev, R_P0_RPL3, val);
	rtw89_phy_write32(rtwdev, R_P1_RPL3, val);
}

static void rtw8852b_ctrl_ch(struct rtw89_dev *rtwdev,
			     const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 central_ch = chan->channel;
	u8 subband = chan->subband_type;
	u8 sco_comp;
	bool is_2g = central_ch <= 14;

	/* Path A */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH0_BAND_SEL_V1,
				      B_PATH0_BAND_SEL_MSK_V1, 0, phy_idx);

	/* Path B */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
				      B_PATH1_BAND_SEL_MSK_V1, 1, phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_PATH1_BAND_SEL_V1,
				      B_PATH1_BAND_SEL_MSK_V1, 0, phy_idx);

	/* SCO compensate FC setting */
	sco_comp = rtw8852b_sco_mapping(central_ch);
	rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_INV, sco_comp, phy_idx);

	if (chan->band_type == RTW89_BAND_6G)
		return;

	/* CCK parameters */
	if (central_ch == 14) {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3b13ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x1c42de);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfdb0ad);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xf60f6e);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xfd8f92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0x2d011);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0x1c02c);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xfff00a);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01, 0x3d23ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23, 0x29b354);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfc1c8);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67, 0xfdb053);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89, 0xf86f9a);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0xfaef92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0xfe5fcc);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF, 0xffdff5);
	}

	rtw8852b_set_gain_error(rtwdev, subband, RF_PATH_A);
	rtw8852b_set_gain_error(rtwdev, subband, RF_PATH_B);
	rtw8852b_set_gain_offset(rtwdev, subband, phy_idx);
	rtw8852b_set_rxsc_rpl_comp(rtwdev, subband);
}

static void rtw8852b_bw_setting(struct rtw89_dev *rtwdev, u8 bw, u8 path)
{
	static const u32 adc_sel[2] = {0xC0EC, 0xC1EC};
	static const u32 wbadc_sel[2] = {0xC0E4, 0xC1E4};

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x1);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x0);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x2);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x1);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
	}
}

static void rtw8852b_ctrl_bw(struct rtw89_dev *rtwdev, u8 pri_ch, u8 bw,
			     enum rtw89_phy_idx phy_idx)
{
	u32 rx_path_0;

	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x1, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);

		/*Set RF mode at 3 */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x333, phy_idx);
		/*CCK primary channel */
		if (pri_ch == RTW89_SC_20_UPPER)
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 1);
		else
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 0);

		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW_V1, B_FC0_BW_SET, 0x2, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH,
				      pri_ch, phy_idx);

		/*Set RF mode at A */
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0xaaa, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0xaaa, phy_idx);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to switch bw (bw:%d, pri ch:%d)\n", bw,
			   pri_ch);
	}

	rtw8852b_bw_setting(rtwdev, bw, RF_PATH_A);
	rtw8852b_bw_setting(rtwdev, bw, RF_PATH_B);

	rx_path_0 = rtw89_phy_read32_idx(rtwdev, R_CHBW_MOD_V1, B_ANT_RX_SEG0,
					 phy_idx);
	if (rx_path_0 == 0x1)
		rtw89_phy_write32_idx(rtwdev, R_P1_RFMODE_ORI_RX,
				      B_P1_RFMODE_ORI_RX_ALL, 0x111, phy_idx);
	else if (rx_path_0 == 0x2)
		rtw89_phy_write32_idx(rtwdev, R_P0_RFMODE_ORI_RX,
				      B_P0_RFMODE_ORI_RX_ALL, 0x111, phy_idx);
}

static void rtw8852b_ctrl_cck_en(struct rtw89_dev *rtwdev, bool cck_en)
{
	if (cck_en) {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 1);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_UPD_CLK_ADC, B_ENABLE_CCK, 0);
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 1);
	}
}

static void rtw8852b_5m_mask(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
			     enum rtw89_phy_idx phy_idx)
{
	u8 pri_ch = chan->primary_channel;
	bool mask_5m_low;
	bool mask_5m_en;

	switch (chan->band_width) {
	case RTW89_CHANNEL_WIDTH_40:
		/* Prich=1: Mask 5M High, Prich=2: Mask 5M Low */
		mask_5m_en = true;
		mask_5m_low = pri_ch == 2;
		break;
	case RTW89_CHANNEL_WIDTH_80:
		/* Prich=3: Mask 5M High, Prich=4: Mask 5M Low, Else: Disable */
		mask_5m_en = pri_ch == 3 || pri_ch == 4;
		mask_5m_low = pri_ch == 4;
		break;
	default:
		mask_5m_en = false;
		break;
	}

	if (!mask_5m_en) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x0);
		rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
				      B_ASSIGN_SBD_OPT_EN_V1, 0x0, phy_idx);
		return;
	}

	if (mask_5m_low) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB2, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB0, 0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_5MDET_V1, B_PATH0_5MDET_SB0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_TH, 0x4);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB2, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_5MDET_V1, B_PATH1_5MDET_SB0, 0x0);
	}
	rtw89_phy_write32_idx(rtwdev, R_ASSIGN_SBD_OPT_V1,
			      B_ASSIGN_SBD_OPT_EN_V1, 0x1, phy_idx);
}

static void rtw8852b_bb_reset_all(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
	fsleep(1);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
}

static void rtw8852b_bb_reset_en(struct rtw89_dev *rtwdev, enum rtw89_band band,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	if (en) {
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x0, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, phy_idx);
		if (band == RTW89_BAND_2G)
			rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0x1);
		rtw89_phy_write32_idx(rtwdev, R_S0_HW_SI_DIS,
				      B_S0_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_S1_HW_SI_DIS,
				      B_S1_HW_SI_DIS_W_R_TRIG, 0x7, phy_idx);
		fsleep(1);
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, phy_idx);
	}
}

static void rtw8852b_bb_reset(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_set(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
	rtw89_phy_write32_set(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_set(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	rtw89_phy_write32_set(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
	rtw8852b_bb_reset_all(rtwdev, phy_idx);
	rtw89_phy_write32_clr(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
	rtw89_phy_write32_clr(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_clr(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	rtw89_phy_write32_clr(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
}

static void rtw8852b_bb_macid_ctrl_init(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy_idx)
{
	u32 addr;

	for (addr = R_AX_PWR_MACID_LMT_TABLE0;
	     addr <= R_AX_PWR_MACID_LMT_TABLE127; addr += 4)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, 0);
}

static void rtw8852b_bb_sethw(struct rtw89_dev *rtwdev)
{
	struct rtw89_phy_efuse_gain *gain = &rtwdev->efuse_gain;

	rtw89_phy_write32_clr(rtwdev, R_P0_EN_SOUND_WO_NDP, B_P0_EN_SOUND_WO_NDP);
	rtw89_phy_write32_clr(rtwdev, R_P1_EN_SOUND_WO_NDP, B_P1_EN_SOUND_WO_NDP);

	rtw8852b_bb_macid_ctrl_init(rtwdev, RTW89_PHY_0);

	/* read these registers after loading BB parameters */
	gain->offset_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_P0_RPL1, B_P0_RPL1_BIAS_MASK);
	gain->rssi_base[RTW89_PHY_0] =
		rtw89_phy_read32_mask(rtwdev, R_P1_RPL1, B_P0_RPL1_BIAS_MASK);
}

static void rtw8852b_set_channel_bb(struct rtw89_dev *rtwdev, const struct rtw89_chan *chan,
				    enum rtw89_phy_idx phy_idx)
{
	bool cck_en = chan->channel <= 14;
	u8 pri_ch_idx = chan->pri_ch_idx;

	if (cck_en)
		rtw8852b_ctrl_sco_cck(rtwdev,  chan->primary_channel);

	rtw8852b_ctrl_ch(rtwdev, chan, phy_idx);
	rtw8852b_ctrl_bw(rtwdev, pri_ch_idx, chan->band_width, phy_idx);
	rtw8852b_ctrl_cck_en(rtwdev, cck_en);
	if (chan->band_type == RTW89_BAND_5G) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BT_SHARE_V1,
				       B_PATH0_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG_PATH_V1,
				       B_PATH0_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BT_SHARE_V1,
				       B_PATH1_BT_SHARE_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG_PATH_V1,
				       B_PATH1_BTG_PATH_V1, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_BT_DYN_DC_EST_EN_V1,
				       B_BT_DYN_DC_EST_EN_MSK, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
	}
	rtw89_phy_write32_mask(rtwdev, R_MAC_PIN_SEL, B_CH_IDX_SEG0,
			       chan->primary_channel);
	rtw8852b_5m_mask(rtwdev, chan, phy_idx);
	rtw8852b_bb_reset_all(rtwdev, phy_idx);
}

static void rtw8852b_set_channel(struct rtw89_dev *rtwdev,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtw8852b_set_channel_mac(rtwdev, chan, mac_idx);
	rtw8852b_set_channel_bb(rtwdev, chan, phy_idx);
	rtw8852b_set_channel_rf(rtwdev, chan, phy_idx);
}

static void rtw8852b_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	static const u32 tssi_trk[2] = {R_P0_TSSI_TRK, R_P1_TSSI_TRK};
	static const u32 ctrl_bbrst[2] = {R_P0_TXPW_RSTB, R_P1_TXPW_RSTB};

	if (en) {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], B_P0_TXPW_RSTB_MANON, 0x0);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], B_P0_TSSI_TRK_EN, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], B_P0_TXPW_RSTB_MANON, 0x1);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], B_P0_TSSI_TRK_EN, 0x1);
	}
}

static void rtw8852b_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en,
					 u8 phy_idx)
{
	if (!rtwdev->dbcc_en) {
		rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		if (phy_idx == RTW89_PHY_0)
			rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8852b_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}

static void rtw8852b_adc_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST, 0xf);
}

static void rtw8852b_set_channel_help(struct rtw89_dev *rtwdev, bool enter,
				      struct rtw89_channel_help_params *p,
				      const struct rtw89_chan *chan,
				      enum rtw89_mac_idx mac_idx,
				      enum rtw89_phy_idx phy_idx)
{
	if (enter) {
		rtw89_chip_stop_sch_tx(rtwdev, RTW89_MAC_0, &p->tx_en, RTW89_SCH_TX_SEL_ALL);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw8852b_tssi_cont_en_phyidx(rtwdev, false, RTW89_PHY_0);
		rtw8852b_adc_en(rtwdev, false);
		fsleep(40);
		rtw8852b_bb_reset_en(rtwdev, chan->band_type, phy_idx, false);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw8852b_adc_en(rtwdev, true);
		rtw8852b_tssi_cont_en_phyidx(rtwdev, true, RTW89_PHY_0);
		rtw8852b_bb_reset_en(rtwdev, chan->band_type, phy_idx, true);
		rtw89_chip_resume_sch_tx(rtwdev, RTW89_MAC_0, p->tx_en);
	}
}

static u32 rtw8852b_bb_cal_txpwr_ref(struct rtw89_dev *rtwdev,
				     enum rtw89_phy_idx phy_idx, s16 ref)
{
	const u16 tssi_16dbm_cw = 0x12c;
	const u8 base_cw_0db = 0x27;
	const s8 ofst_int = 0;
	s16 pwr_s10_3;
	s16 rf_pwr_cw;
	u16 bb_pwr_cw;
	u32 pwr_cw;
	u32 tssi_ofst_cw;

	pwr_s10_3 = (ref << 1) + (s16)(ofst_int) + (s16)(base_cw_0db << 3);
	bb_pwr_cw = FIELD_GET(GENMASK(2, 0), pwr_s10_3);
	rf_pwr_cw = FIELD_GET(GENMASK(8, 3), pwr_s10_3);
	rf_pwr_cw = clamp_t(s16, rf_pwr_cw, 15, 63);
	pwr_cw = (rf_pwr_cw << 3) | bb_pwr_cw;

	tssi_ofst_cw = (u32)((s16)tssi_16dbm_cw + (ref << 1) - (16 << 3));
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] tssi_ofst_cw=%d rf_cw=0x%x bb_cw=0x%x\n",
		    tssi_ofst_cw, rf_pwr_cw, bb_pwr_cw);

	return FIELD_PREP(B_DPD_TSSI_CW, tssi_ofst_cw) |
	       FIELD_PREP(B_DPD_PWR_CW, pwr_cw) |
	       FIELD_PREP(B_DPD_REF, ref);
}

static void rtw8852b_set_txpwr_ref(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	static const u32 addr[RF_PATH_NUM_8852B] = {0x5800, 0x7800};
	const u32 mask = B_DPD_TSSI_CW | B_DPD_PWR_CW | B_DPD_REF;
	const u8 ofst_ofdm = 0x4;
	const u8 ofst_cck = 0x8;
	const s16 ref_ofdm = 0;
	const s16 ref_cck = 0;
	u32 val;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_CTRL,
				     B_AX_PWR_REF, 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb ofdm txpwr ref\n");
	val = rtw8852b_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_ofdm);

	for (i = 0; i < RF_PATH_NUM_8852B; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_ofdm, mask, val,
				      phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb cck txpwr ref\n");
	val = rtw8852b_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_cck);

	for (i = 0; i < RF_PATH_NUM_8852B; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_cck, mask, val,
				      phy_idx);
}

static void rtw8852b_bb_set_tx_shape_dfir(struct rtw89_dev *rtwdev,
					  u8 tx_shape_idx,
					  enum rtw89_phy_idx phy_idx)
{
#define __DFIR_CFG_ADDR(i) (R_TXFIR0 + ((i) << 2))
#define __DFIR_CFG_MASK 0xffffffff
#define __DFIR_CFG_NR 8
#define __DECL_DFIR_PARAM(_name, _val...) \
	static const u32 param_ ## _name[] = {_val}; \
	static_assert(ARRAY_SIZE(param_ ## _name) == __DFIR_CFG_NR)

	__DECL_DFIR_PARAM(flat,
			  0x023D23FF, 0x0029B354, 0x000FC1C8, 0x00FDB053,
			  0x00F86F9A, 0x06FAEF92, 0x00FE5FCC, 0x00FFDFF5);
	__DECL_DFIR_PARAM(sharp,
			  0x023D83FF, 0x002C636A, 0x0013F204, 0x00008090,
			  0x00F87FB0, 0x06F99F83, 0x00FDBFBA, 0x00003FF5);
	__DECL_DFIR_PARAM(sharp_14,
			  0x023B13FF, 0x001C42DE, 0x00FDB0AD, 0x00F60F6E,
			  0x00FD8F92, 0x0602D011, 0x0001C02C, 0x00FFF00A);
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, RTW89_SUB_ENTITY_0);
	u8 ch = chan->channel;
	const u32 *param;
	u32 addr;
	int i;

	if (ch > 14) {
		rtw89_warn(rtwdev,
			   "set tx shape dfir by unknown ch: %d on 2G\n", ch);
		return;
	}

	if (ch == 14)
		param = param_sharp_14;
	else
		param = tx_shape_idx == 0 ? param_flat : param_sharp;

	for (i = 0; i < __DFIR_CFG_NR; i++) {
		addr = __DFIR_CFG_ADDR(i);
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
			    "set tx shape dfir: 0x%x: 0x%x\n", addr, param[i]);
		rtw89_phy_write32_idx(rtwdev, addr, __DFIR_CFG_MASK, param[i],
				      phy_idx);
	}

#undef __DECL_DFIR_PARAM
#undef __DFIR_CFG_NR
#undef __DFIR_CFG_MASK
#undef __DECL_CFG_ADDR
}

static void rtw8852b_set_tx_shape(struct rtw89_dev *rtwdev,
				  const struct rtw89_chan *chan,
				  enum rtw89_phy_idx phy_idx)
{
	u8 band = chan->band_type;
	u8 regd = rtw89_regd_get(rtwdev, band);
	u8 tx_shape_cck = rtw89_8852b_tx_shape[band][RTW89_RS_CCK][regd];
	u8 tx_shape_ofdm = rtw89_8852b_tx_shape[band][RTW89_RS_OFDM][regd];

	if (band == RTW89_BAND_2G)
		rtw8852b_bb_set_tx_shape_dfir(rtwdev, tx_shape_cck, phy_idx);

	rtw89_phy_write32_mask(rtwdev, R_DCFO_OPT, B_TXSHAPE_TRIANGULAR_CFG,
			       tx_shape_ofdm);
}

static void rtw8852b_set_txpwr(struct rtw89_dev *rtwdev,
			       const struct rtw89_chan *chan,
			       enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_set_txpwr_byrate(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_offset(rtwdev, chan, phy_idx);
	rtw8852b_set_tx_shape(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit(rtwdev, chan, phy_idx);
	rtw89_phy_set_txpwr_limit_ru(rtwdev, chan, phy_idx);
}

static void rtw8852b_set_txpwr_ctrl(struct rtw89_dev *rtwdev,
				    enum rtw89_phy_idx phy_idx)
{
	rtw8852b_set_txpwr_ref(rtwdev, phy_idx);
}

static
void rtw8852b_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				     s8 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	u32 reg;

	if (pw_ofst < -16 || pw_ofst > 15) {
		rtw89_warn(rtwdev, "[ULTB] Err pwr_offset=%d\n", pw_ofst);
		return;
	}

	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_CTRL, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_PWR_UL_TB_CTRL_EN);

	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_1T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, pw_ofst);

	pw_ofst = max_t(s8, pw_ofst - 3, -16);
	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_2T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, pw_ofst);
}

static int
rtw8852b_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	int ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL2, 0x07763333);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_COEXT_CTRL, 0x01ebf000);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL0, 0x0002f8ff);
	if (ret)
		return ret;

	rtw8852b_set_txpwr_ul_tb_offset(rtwdev, 0, phy_idx == RTW89_PHY_1 ?
						   RTW89_MAC_1 : RTW89_MAC_0);

	return 0;
}

static u8 rtw8852b_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	if (rtwdev->is_tssi_mode[rf_path]) {
		u32 addr = 0x1c10 + (rf_path << 13);

		return rtw89_phy_read32_mask(rtwdev, addr, 0x3F000000);
	}

	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	return rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL);
}

static int rtw8852b_mac_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_write8_set(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
	rtw89_write32_mask(rtwdev, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x1);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_clr(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
	rtw89_write32_set(rtwdev, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, 0xC7,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	rtw89_write8(rtwdev, R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);

	return 0;
}

static int rtw8852b_mac_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	u8 wl_rfc_s0;
	u8 wl_rfc_s1;
	int ret;

	rtw89_write8_clr(rtwdev, R_AX_SYS_FUNC_EN,
			 B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, &wl_rfc_s0);
	if (ret)
		return ret;
	wl_rfc_s0 &= ~XTAL_SI_RF00S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S0, wl_rfc_s0,
				      FULL_BIT_MASK);
	if (ret)
		return ret;

	ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, &wl_rfc_s1);
	if (ret)
		return ret;
	wl_rfc_s1 &= ~XTAL_SI_RF10S_EN;
	ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_WL_RFC_S1, wl_rfc_s1,
				      FULL_BIT_MASK);
	return ret;
}

static const struct rtw89_chip_ops rtw8852b_chip_ops = {
	.enable_bb_rf		= rtw8852b_mac_enable_bb_rf,
	.disable_bb_rf		= rtw8852b_mac_disable_bb_rf,
	.bb_reset		= rtw8852b_bb_reset,
	.bb_sethw		= rtw8852b_bb_sethw,
	.set_channel		= rtw8852b_set_channel,
	.set_channel_help	= rtw8852b_set_channel_help,
	.read_efuse		= rtw8852b_read_efuse,
	.read_phycap		= rtw8852b_read_phycap,
	.power_trim		= rtw8852b_power_trim,
	.set_txpwr		= rtw8852b_set_txpwr,
	.set_txpwr_ctrl		= rtw8852b_set_txpwr_ctrl,
	.init_txpwr_unit	= rtw8852b_init_txpwr_unit,
	.get_thermal		= rtw8852b_get_thermal,
	.pwr_on_func		= rtw8852b_pwr_on_func,
	.pwr_off_func		= rtw8852b_pwr_off_func,
};

const struct rtw89_chip_info rtw8852b_chip_info = {
	.chip_id		= RTL8852B,
	.ops			= &rtw8852b_chip_ops,
	.fifo_size		= 196608,
	.dle_scc_rsvd_size	= 98304,
	.hfc_param_ini		= rtw8852b_hfc_param_ini_pcie,
	.dle_mem		= rtw8852b_dle_mem_pcie,
	.pwr_on_seq		= NULL,
	.pwr_off_seq		= NULL,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 2048,
	.limit_efuse_size	= 1280,
	.dav_phy_efuse_size	= 96,
	.dav_log_efuse_size	= 16,
	.phycap_addr		= 0x580,
	.phycap_size		= 128,
	.dma_ch_mask		= BIT(RTW89_DMA_ACH4) | BIT(RTW89_DMA_ACH5) |
				  BIT(RTW89_DMA_ACH6) | BIT(RTW89_DMA_ACH7) |
				  BIT(RTW89_DMA_B1MG) | BIT(RTW89_DMA_B1HI),
};
EXPORT_SYMBOL(rtw8852b_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852b_fw.bin");
MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852B driver");
MODULE_LICENSE("Dual BSD/GPL");
