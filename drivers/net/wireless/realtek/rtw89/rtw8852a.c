// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "coex.h"
#include "mac.h"
#include "phy.h"
#include "reg.h"
#include "rtw8852a.h"
#include "rtw8852a_rfk.h"
#include "rtw8852a_table.h"
#include "txrx.h"

static const struct rtw89_hfc_ch_cfg rtw8852a_hfc_chcfg_pcie[] = {
	{128, 1896, grp_0}, /* ACH 0 */
	{128, 1896, grp_0}, /* ACH 1 */
	{128, 1896, grp_0}, /* ACH 2 */
	{128, 1896, grp_0}, /* ACH 3 */
	{128, 1896, grp_1}, /* ACH 4 */
	{128, 1896, grp_1}, /* ACH 5 */
	{128, 1896, grp_1}, /* ACH 6 */
	{128, 1896, grp_1}, /* ACH 7 */
	{32, 1896, grp_0}, /* B0MGQ */
	{128, 1896, grp_0}, /* B0HIQ */
	{32, 1896, grp_1}, /* B1MGQ */
	{128, 1896, grp_1}, /* B1HIQ */
	{40, 0, 0} /* FWCMDQ */
};

static const struct rtw89_hfc_pub_cfg rtw8852a_hfc_pubcfg_pcie = {
	1896, /* Group 0 */
	1896, /* Group 1 */
	3792, /* Public Max */
	0 /* WP threshold */
};

static const struct rtw89_hfc_param_ini rtw8852a_hfc_param_ini_pcie[] = {
	[RTW89_QTA_SCC] = {rtw8852a_hfc_chcfg_pcie, &rtw8852a_hfc_pubcfg_pcie,
			   &rtw_hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_DLFW] = {NULL, NULL, &rtw_hfc_preccfg_pcie, RTW89_HCIFC_POH},
	[RTW89_QTA_INVALID] = {NULL},
};

static const struct rtw89_dle_mem rtw8852a_dle_mem_pcie[] = {
	[RTW89_QTA_SCC] = {RTW89_QTA_SCC, &wde_size0, &ple_size0, &wde_qt0,
			    &wde_qt0, &ple_qt4, &ple_qt5},
	[RTW89_QTA_DLFW] = {RTW89_QTA_DLFW, &wde_size4, &ple_size4,
			    &wde_qt4, &wde_qt4, &ple_qt13, &ple_qt13},
	[RTW89_QTA_INVALID] = {RTW89_QTA_INVALID, NULL, NULL, NULL, NULL, NULL,
			       NULL},
};

static const struct rtw89_reg2_def  rtw8852a_pmac_ht20_mcs7_tbl[] = {
	{0x44AC, 0x00000000},
	{0x44B0, 0x00000000},
	{0x44B4, 0x00000000},
	{0x44B8, 0x00000000},
	{0x44BC, 0x00000000},
	{0x44C0, 0x00000000},
	{0x44C4, 0x00000000},
	{0x44C8, 0x00000000},
	{0x44CC, 0x00000000},
	{0x44D0, 0x00000000},
	{0x44D4, 0x00000000},
	{0x44D8, 0x00000000},
	{0x44DC, 0x00000000},
	{0x44E0, 0x00000000},
	{0x44E4, 0x00000000},
	{0x44E8, 0x00000000},
	{0x44EC, 0x00000000},
	{0x44F0, 0x00000000},
	{0x44F4, 0x00000000},
	{0x44F8, 0x00000000},
	{0x44FC, 0x00000000},
	{0x4500, 0x00000000},
	{0x4504, 0x00000000},
	{0x4508, 0x00000000},
	{0x450C, 0x00000000},
	{0x4510, 0x00000000},
	{0x4514, 0x00000000},
	{0x4518, 0x00000000},
	{0x451C, 0x00000000},
	{0x4520, 0x00000000},
	{0x4524, 0x00000000},
	{0x4528, 0x00000000},
	{0x452C, 0x00000000},
	{0x4530, 0x4E1F3E81},
	{0x4534, 0x00000000},
	{0x4538, 0x0000005A},
	{0x453C, 0x00000000},
	{0x4540, 0x00000000},
	{0x4544, 0x00000000},
	{0x4548, 0x00000000},
	{0x454C, 0x00000000},
	{0x4550, 0x00000000},
	{0x4554, 0x00000000},
	{0x4558, 0x00000000},
	{0x455C, 0x00000000},
	{0x4560, 0x4060001A},
	{0x4564, 0x40000000},
	{0x4568, 0x00000000},
	{0x456C, 0x00000000},
	{0x4570, 0x04000007},
	{0x4574, 0x0000DC87},
	{0x4578, 0x00000BAB},
	{0x457C, 0x03E00000},
	{0x4580, 0x00000048},
	{0x4584, 0x00000000},
	{0x4588, 0x000003E8},
	{0x458C, 0x30000000},
	{0x4590, 0x00000000},
	{0x4594, 0x10000000},
	{0x4598, 0x00000001},
	{0x459C, 0x00030000},
	{0x45A0, 0x01000000},
	{0x45A4, 0x03000200},
	{0x45A8, 0xC00001C0},
	{0x45AC, 0x78018000},
	{0x45B0, 0x80000000},
	{0x45B4, 0x01C80600},
	{0x45B8, 0x00000002},
	{0x4594, 0x10000000}
};

static const struct rtw89_reg3_def rtw8852a_btc_preagc_en_defs[] = {
	{0x4624, GENMASK(20, 14), 0x40},
	{0x46f8, GENMASK(20, 14), 0x40},
	{0x4674, GENMASK(20, 19), 0x2},
	{0x4748, GENMASK(20, 19), 0x2},
	{0x4650, GENMASK(14, 10), 0x18},
	{0x4724, GENMASK(14, 10), 0x18},
	{0x4688, GENMASK(1, 0), 0x3},
	{0x475c, GENMASK(1, 0), 0x3},
};

static DECLARE_PHY_REG3_TBL(rtw8852a_btc_preagc_en_defs);

static const struct rtw89_reg3_def rtw8852a_btc_preagc_dis_defs[] = {
	{0x4624, GENMASK(20, 14), 0x1a},
	{0x46f8, GENMASK(20, 14), 0x1a},
	{0x4674, GENMASK(20, 19), 0x1},
	{0x4748, GENMASK(20, 19), 0x1},
	{0x4650, GENMASK(14, 10), 0x12},
	{0x4724, GENMASK(14, 10), 0x12},
	{0x4688, GENMASK(1, 0), 0x0},
	{0x475c, GENMASK(1, 0), 0x0},
};

static DECLARE_PHY_REG3_TBL(rtw8852a_btc_preagc_dis_defs);

static const struct rtw89_pwr_cfg rtw8852a_pwron[] = {
	{0x00C6,
	 PWR_CV_MSK_B,
	 PWR_INTF_MSK_PCIE,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x1086,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_SDIO,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), 0},
	{0x1086,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_SDIO,
	 PWR_BASE_MAC,
	 PWR_CMD_POLL, BIT(1), BIT(1)},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(4) | BIT(3), 0},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(7), 0},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(2), 0},
	{0x0006,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_POLL, BIT(1), BIT(1)},
	{0x0006,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_POLL, BIT(0), 0},
	{0x106D,
	 PWR_CV_MSK_B | PWR_CV_MSK_C,
	 PWR_INTF_MSK_USB,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(6), 0},
	{0x0088,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0088,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), 0},
	{0x0088,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0088,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), 0},
	{0x0088,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0083,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(6), 0},
	{0x0080,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0024,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0), 0},
	{0x02A0,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x02A2,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(7) | BIT(6) | BIT(5), 0},
	{0x0071,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_PCIE,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(4), 0},
	{0x0010,
	 PWR_CV_MSK_A,
	 PWR_INTF_MSK_PCIE,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x02A0,
	 PWR_CV_MSK_A,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(7) | BIT(6), 0},
	{0xFFFF,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 0,
	 PWR_CMD_END, 0, 0},
};

static const struct rtw89_pwr_cfg rtw8852a_pwroff[] = {
	{0x02F0,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, 0xFF, 0},
	{0x02F1,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, 0xFF, 0},
	{0x0006,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0002,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(1) | BIT(0), 0},
	{0x0082,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(1) | BIT(0), 0},
	{0x106D,
	 PWR_CV_MSK_B | PWR_CV_MSK_C,
	 PWR_INTF_MSK_USB,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 PWR_BASE_MAC,
	 PWR_CMD_POLL, BIT(1), 0},
	{0x0091,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_PCIE,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_PCIE,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x0007,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_USB,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(4), 0},
	{0x0007,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_SDIO,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(6) | BIT(4), 0},
	{0x0005,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_SDIO,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(4) | BIT(3), BIT(3)},
	{0x0005,
	 PWR_CV_MSK_C | PWR_CV_MSK_D | PWR_CV_MSK_E | PWR_CV_MSK_F |
	 PWR_CV_MSK_G,
	 PWR_INTF_MSK_USB,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(4) | BIT(3), BIT(3)},
	{0x1086,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_SDIO,
	 PWR_BASE_MAC,
	 PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x1086,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_SDIO,
	 PWR_BASE_MAC,
	 PWR_CMD_POLL, BIT(1), 0},
	{0xFFFF,
	 PWR_CV_MSK_ALL,
	 PWR_INTF_MSK_ALL,
	 0,
	 PWR_CMD_END, 0, 0},
};

static const struct rtw89_pwr_cfg * const pwr_on_seq_8852a[] = {
	rtw8852a_pwron, NULL
};

static const struct rtw89_pwr_cfg * const pwr_off_seq_8852a[] = {
	rtw8852a_pwroff, NULL
};

static void rtw8852ae_efuse_parsing(struct rtw89_efuse *efuse,
				    struct rtw8852a_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
	efuse->rfe_type = map->rfe_type;
	efuse->xtal_cap = map->xtal_k;
}

static void rtw8852a_efuse_parsing_tssi(struct rtw89_dev *rtwdev,
					struct rtw8852a_efuse *map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	struct rtw8852a_tssi_offset *ofst[] = {&map->path_a_tssi, &map->path_b_tssi};
	u8 i, j;

	tssi->thermal[RF_PATH_A] = map->path_a_therm;
	tssi->thermal[RF_PATH_B] = map->path_b_therm;

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
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

static int rtw8852a_read_efuse(struct rtw89_dev *rtwdev, u8 *log_map)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw8852a_efuse *map;

	map = (struct rtw8852a_efuse *)log_map;

	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	rtw8852a_efuse_parsing_tssi(rtwdev, map);

	switch (rtwdev->hci.type) {
	case RTW89_HCI_TYPE_PCIE:
		rtw8852ae_efuse_parsing(efuse, map);
		break;
	default:
		return -ENOTSUPP;
	}

	rtw89_info(rtwdev, "chip rfe_type is %d\n", efuse->rfe_type);

	return 0;
}

static void rtw8852a_phycap_parsing_tssi(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	struct rtw89_tssi_info *tssi = &rtwdev->tssi;
	static const u32 tssi_trim_addr[RF_PATH_NUM_8852A] = {0x5D6, 0x5AB};
	u32 addr = rtwdev->chip->phycap_addr;
	bool pg = false;
	u32 ofst;
	u8 i, j;

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
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

	for (i = 0; i < RF_PATH_NUM_8852A; i++)
		for (j = 0; j < TSSI_TRIM_CH_GROUP_NUM; j++)
			rtw89_debug(rtwdev, RTW89_DBG_TSSI,
				    "[TSSI] path=%d idx=%d trim=0x%x addr=0x%x\n",
				    i, j, tssi->tssi_trim[i][j],
				    tssi_trim_addr[i] - j);
}

static void rtw8852a_phycap_parsing_thermal_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 thm_trim_addr[RF_PATH_NUM_8852A] = {0x5DF, 0x5DC};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
		info->thermal_trim[i] = phycap_map[thm_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_trim=0x%x\n",
			    i, info->thermal_trim[i]);

		if (info->thermal_trim[i] != 0xff)
			info->pg_thermal_trim = true;
	}
}

static void rtw8852a_thermal_trim(struct rtw89_dev *rtwdev)
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

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
		val = __thm_setting(info->thermal_trim[i]);
		rtw89_write_rf(rtwdev, i, RR_TM2, RR_TM2_OFF, val);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[THERMAL][TRIM] path=%d thermal_setting=0x%x\n",
			    i, val);
	}
#undef __thm_setting
}

static void rtw8852a_phycap_parsing_pa_bias_trim(struct rtw89_dev *rtwdev,
						 u8 *phycap_map)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	static const u32 pabias_trim_addr[RF_PATH_NUM_8852A] = {0x5DE, 0x5DB};
	u32 addr = rtwdev->chip->phycap_addr;
	u8 i;

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
		info->pa_bias_trim[i] = phycap_map[pabias_trim_addr[i] - addr];

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d pa_bias_trim=0x%x\n",
			    i, info->pa_bias_trim[i]);

		if (info->pa_bias_trim[i] != 0xff)
			info->pg_pa_bias_trim = true;
	}
}

static void rtw8852a_pa_bias_trim(struct rtw89_dev *rtwdev)
{
	struct rtw89_power_trim_info *info = &rtwdev->pwr_trim;
	u8 pabias_2g, pabias_5g;
	u8 i;

	if (!info->pg_pa_bias_trim) {
		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] no PG, do nothing\n");

		return;
	}

	for (i = 0; i < RF_PATH_NUM_8852A; i++) {
		pabias_2g = FIELD_GET(GENMASK(3, 0), info->pa_bias_trim[i]);
		pabias_5g = FIELD_GET(GENMASK(7, 4), info->pa_bias_trim[i]);

		rtw89_debug(rtwdev, RTW89_DBG_RFK,
			    "[PA_BIAS][TRIM] path=%d 2G=0x%x 5G=0x%x\n",
			    i, pabias_2g, pabias_5g);

		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXG, pabias_2g);
		rtw89_write_rf(rtwdev, i, RR_BIASA, RR_BIASA_TXA, pabias_5g);
	}
}

static int rtw8852a_read_phycap(struct rtw89_dev *rtwdev, u8 *phycap_map)
{
	rtw8852a_phycap_parsing_tssi(rtwdev, phycap_map);
	rtw8852a_phycap_parsing_thermal_trim(rtwdev, phycap_map);
	rtw8852a_phycap_parsing_pa_bias_trim(rtwdev, phycap_map);

	return 0;
}

static void rtw8852a_power_trim(struct rtw89_dev *rtwdev)
{
	rtw8852a_thermal_trim(rtwdev);
	rtw8852a_pa_bias_trim(rtwdev);
}

static void rtw8852a_set_channel_mac(struct rtw89_dev *rtwdev,
				     struct rtw89_channel_params *param,
				     u8 mac_idx)
{
	u32 rf_mod = rtw89_mac_reg_by_idx(R_AX_WMAC_RFMOD, mac_idx);
	u32 sub_carr = rtw89_mac_reg_by_idx(R_AX_TX_SUB_CARRIER_VALUE,
					     mac_idx);
	u32 chk_rate = rtw89_mac_reg_by_idx(R_AX_TXRATE_CHK, mac_idx);
	u8 txsc20 = 0, txsc40 = 0;

	switch (param->bandwidth) {
	case RTW89_CHANNEL_WIDTH_80:
		txsc40 = rtw89_phy_get_txsc(rtwdev, param,
					    RTW89_CHANNEL_WIDTH_40);
		fallthrough;
	case RTW89_CHANNEL_WIDTH_40:
		txsc20 = rtw89_phy_get_txsc(rtwdev, param,
					    RTW89_CHANNEL_WIDTH_20);
		break;
	default:
		break;
	}

	switch (param->bandwidth) {
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

	if (param->center_chan > 14)
		rtw89_write8_set(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
	else
		rtw89_write8_clr(rtwdev, chk_rate,
				 B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6);
}

static const u32 rtw8852a_sco_barker_threshold[14] = {
	0x1cfea, 0x1d0e1, 0x1d1d7, 0x1d2cd, 0x1d3c3, 0x1d4b9, 0x1d5b0, 0x1d6a6,
	0x1d79c, 0x1d892, 0x1d988, 0x1da7f, 0x1db75, 0x1ddc4
};

static const u32 rtw8852a_sco_cck_threshold[14] = {
	0x27de3, 0x27f35, 0x28088, 0x281da, 0x2832d, 0x2847f, 0x285d2, 0x28724,
	0x28877, 0x289c9, 0x28b1c, 0x28c6e, 0x28dc1, 0x290ed
};

static int rtw8852a_ctrl_sco_cck(struct rtw89_dev *rtwdev, u8 central_ch,
				 u8 primary_ch, enum rtw89_bandwidth bw)
{
	u8 ch_element;

	if (bw == RTW89_CHANNEL_WIDTH_20) {
		ch_element = central_ch - 1;
	} else if (bw == RTW89_CHANNEL_WIDTH_40) {
		if (primary_ch == 1)
			ch_element = central_ch - 1 + 2;
		else
			ch_element = central_ch - 1 - 2;
	} else {
		rtw89_warn(rtwdev, "Invalid BW:%d for CCK\n", bw);
		return -EINVAL;
	}
	rtw89_phy_write32_mask(rtwdev, R_RXSCOBC, B_RXSCOBC_TH,
			       rtw8852a_sco_barker_threshold[ch_element]);
	rtw89_phy_write32_mask(rtwdev, R_RXSCOCCK, B_RXSCOCCK_TH,
			       rtw8852a_sco_cck_threshold[ch_element]);

	return 0;
}

static void rtw8852a_ch_setting(struct rtw89_dev *rtwdev, u8 central_ch,
				u8 path)
{
	u32 val;

	val = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	if (val == INV_RF_DATA) {
		rtw89_warn(rtwdev, "Invalid RF_0x18 for Path-%d\n", path);
		return;
	}
	val &= ~0x303ff;
	val |= central_ch;
	if (central_ch > 14)
		val |= (BIT(16) | BIT(8));
	rtw89_write_rf(rtwdev, path, RR_CFGCH, RFREG_MASK, val);
}

static u8 rtw8852a_sco_mapping(u8 central_ch)
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

static void rtw8852a_ctrl_ch(struct rtw89_dev *rtwdev, u8 central_ch,
			     enum rtw89_phy_idx phy_idx)
{
	u8 sco_comp;
	bool is_2g = central_ch <= 14;

	if (phy_idx == RTW89_PHY_0) {
		/* Path A */
		rtw8852a_ch_setting(rtwdev, central_ch, RF_PATH_A);
		if (is_2g)
			rtw89_phy_write32_idx(rtwdev, R_PATH0_TIA_ERR_G1,
					      B_PATH0_TIA_ERR_G1_SEL, 1,
					      phy_idx);
		else
			rtw89_phy_write32_idx(rtwdev, R_PATH0_TIA_ERR_G1,
					      B_PATH0_TIA_ERR_G1_SEL, 0,
					      phy_idx);

		/* Path B */
		if (!rtwdev->dbcc_en) {
			rtw8852a_ch_setting(rtwdev, central_ch, RF_PATH_B);
			if (is_2g)
				rtw89_phy_write32_idx(rtwdev, R_P1_MODE,
						      B_P1_MODE_SEL,
						      1, phy_idx);
			else
				rtw89_phy_write32_idx(rtwdev, R_P1_MODE,
						      B_P1_MODE_SEL,
						      0, phy_idx);
		} else {
			if (is_2g)
				rtw89_phy_write32_clr(rtwdev, R_2P4G_BAND,
						      B_2P4G_BAND_SEL);
			else
				rtw89_phy_write32_set(rtwdev, R_2P4G_BAND,
						      B_2P4G_BAND_SEL);
		}
		/* SCO compensate FC setting */
		sco_comp = rtw8852a_sco_mapping(central_ch);
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_INV,
				      sco_comp, phy_idx);
	} else {
		/* Path B */
		rtw8852a_ch_setting(rtwdev, central_ch, RF_PATH_B);
		if (is_2g)
			rtw89_phy_write32_idx(rtwdev, R_P1_MODE,
					      B_P1_MODE_SEL,
					      1, phy_idx);
		else
			rtw89_phy_write32_idx(rtwdev, R_P1_MODE,
					      B_P1_MODE_SEL,
					      0, phy_idx);
		/* SCO compensate FC setting */
		sco_comp = rtw8852a_sco_mapping(central_ch);
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_INV,
				      sco_comp, phy_idx);
	}

	/* Band edge */
	if (is_2g)
		rtw89_phy_write32_idx(rtwdev, R_BANDEDGE, B_BANDEDGE_EN, 1,
				      phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_BANDEDGE, B_BANDEDGE_EN, 0,
				      phy_idx);

	/* CCK parameters */
	if (central_ch == 14) {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01,
				       0x3b13ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23,
				       0x1c42de);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45,
				       0xfdb0ad);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67,
				       0xf60f6e);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89,
				       0xfd8f92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB, 0x2d011);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD, 0x1c02c);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF,
				       0xfff00a);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXFIR0, B_TXFIR_C01,
				       0x3d23ff);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR2, B_TXFIR_C23,
				       0x29b354);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR4, B_TXFIR_C45, 0xfc1c8);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR6, B_TXFIR_C67,
				       0xfdb053);
		rtw89_phy_write32_mask(rtwdev, R_TXFIR8, B_TXFIR_C89,
				       0xf86f9a);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRA, B_TXFIR_CAB,
				       0xfaef92);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRC, B_TXFIR_CCD,
				       0xfe5fcc);
		rtw89_phy_write32_mask(rtwdev, R_TXFIRE, B_TXFIR_CEF,
				       0xffdff5);
	}
}

static void rtw8852a_bw_setting(struct rtw89_dev *rtwdev, u8 bw, u8 path)
{
	u32 val = 0;
	u32 adc_sel[2] = {0x12d0, 0x32d0};
	u32 wbadc_sel[2] = {0x12ec, 0x32ec};

	val = rtw89_read_rf(rtwdev, path, RR_CFGCH, RFREG_MASK);
	if (val == INV_RF_DATA) {
		rtw89_warn(rtwdev, "Invalid RF_0x18 for Path-%d\n", path);
		return;
	}
	val &= ~(BIT(11) | BIT(10));
	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x1);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x0);
		val |= (BIT(11) | BIT(10));
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x2);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x1);
		val |= (BIT(11) | BIT(10));
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		val |= (BIT(11) | BIT(10));
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		val |= BIT(11);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_mask(rtwdev, adc_sel[path], 0x6000, 0x0);
		rtw89_phy_write32_mask(rtwdev, wbadc_sel[path], 0x30, 0x2);
		val |= BIT(10);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to set ADC\n");
	}

	rtw89_write_rf(rtwdev, path, RR_CFGCH, RFREG_MASK, val);
}

static void
rtw8852a_ctrl_bw(struct rtw89_dev *rtwdev, u8 pri_ch, u8 bw,
		 enum rtw89_phy_idx phy_idx)
{
	/* Switch bandwidth */
	switch (bw) {
	case RTW89_CHANNEL_WIDTH_5:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x1,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      0x0, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_10:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x2,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      0x0, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_20:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      0x0, phy_idx);
		break;
	case RTW89_CHANNEL_WIDTH_40:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x1,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      pri_ch,
				      phy_idx);
		if (pri_ch == RTW89_SC_20_UPPER)
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 1);
		else
			rtw89_phy_write32_mask(rtwdev, R_RXSC, B_RXSC_EN, 0);
		break;
	case RTW89_CHANNEL_WIDTH_80:
		rtw89_phy_write32_idx(rtwdev, R_FC0_BW, B_FC0_BW_SET, 0x2,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_SBW, 0x0,
				      phy_idx);
		rtw89_phy_write32_idx(rtwdev, R_CHBW_MOD, B_CHBW_MOD_PRICH,
				      pri_ch,
				      phy_idx);
		break;
	default:
		rtw89_warn(rtwdev, "Fail to switch bw (bw:%d, pri ch:%d)\n", bw,
			   pri_ch);
	}

	if (phy_idx == RTW89_PHY_0) {
		rtw8852a_bw_setting(rtwdev, bw, RF_PATH_A);
		if (!rtwdev->dbcc_en)
			rtw8852a_bw_setting(rtwdev, bw, RF_PATH_B);
	} else {
		rtw8852a_bw_setting(rtwdev, bw, RF_PATH_B);
	}
}

static void rtw8852a_spur_elimination(struct rtw89_dev *rtwdev, u8 central_ch)
{
	if (central_ch == 153) {
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX, B_P0_NBIIDX_VAL,
				       0x210);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX, B_P1_NBIIDX_VAL,
				       0x210);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI, 0xfff, 0x7c0);
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX,
				       B_P0_NBIIDX_NOTCH_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX,
				       B_P1_NBIIDX_NOTCH_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI_EN, B_SEG0CSI_EN,
				       0x1);
	} else if (central_ch == 151) {
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX, B_P0_NBIIDX_VAL,
				       0x210);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX, B_P1_NBIIDX_VAL,
				       0x210);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI, 0xfff, 0x40);
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX,
				       B_P0_NBIIDX_NOTCH_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX,
				       B_P1_NBIIDX_NOTCH_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI_EN, B_SEG0CSI_EN,
				       0x1);
	} else if (central_ch == 155) {
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX, B_P0_NBIIDX_VAL,
				       0x2d0);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX, B_P1_NBIIDX_VAL,
				       0x2d0);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI, 0xfff, 0x740);
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX,
				       B_P0_NBIIDX_NOTCH_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX,
				       B_P1_NBIIDX_NOTCH_EN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI_EN, B_SEG0CSI_EN,
				       0x1);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P0_NBIIDX,
				       B_P0_NBIIDX_NOTCH_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_P1_NBIIDX,
				       B_P1_NBIIDX_NOTCH_EN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_SEG0CSI_EN, B_SEG0CSI_EN,
				       0x0);
	}
}

static void rtw8852a_bb_reset_all(struct rtw89_dev *rtwdev,
				  enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1,
			      phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0,
			      phy_idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1,
			      phy_idx);
}

static void rtw8852a_bb_reset_en(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx, bool en)
{
	if (en)
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL,
				      1,
				      phy_idx);
	else
		rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL,
				      0,
				      phy_idx);
}

static void rtw8852a_bb_reset(struct rtw89_dev *rtwdev,
			      enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_set(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
	rtw89_phy_write32_set(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_set(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	rtw89_phy_write32_set(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
	rtw8852a_bb_reset_all(rtwdev, phy_idx);
	rtw89_phy_write32_clr(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
	rtw89_phy_write32_clr(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_clr(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	rtw89_phy_write32_clr(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
}

static void rtw8852a_bb_macid_ctrl_init(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy_idx)
{
	u32 addr;

	for (addr = R_AX_PWR_MACID_LMT_TABLE0;
	     addr <= R_AX_PWR_MACID_LMT_TABLE127; addr += 4)
		rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, 0);
}

static void rtw8852a_bb_sethw(struct rtw89_dev *rtwdev)
{
	rtw89_phy_write32_clr(rtwdev, R_P0_EN_SOUND_WO_NDP, B_P0_EN_SOUND_WO_NDP);
	rtw89_phy_write32_clr(rtwdev, R_P1_EN_SOUND_WO_NDP, B_P1_EN_SOUND_WO_NDP);

	if (rtwdev->hal.cv <= CHIP_CCV) {
		rtw89_phy_write32_set(rtwdev, R_RSTB_WATCH_DOG, B_P0_RSTB_WATCH_DOG);
		rtw89_phy_write32(rtwdev, R_BRK_ASYNC_RST_EN_1, 0x864FA000);
		rtw89_phy_write32(rtwdev, R_BRK_ASYNC_RST_EN_2, 0x3F);
		rtw89_phy_write32(rtwdev, R_BRK_ASYNC_RST_EN_3, 0x7FFF);
		rtw89_phy_write32_set(rtwdev, R_SPOOF_ASYNC_RST, B_SPOOF_ASYNC_RST);
		rtw89_phy_write32_set(rtwdev, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON);
		rtw89_phy_write32_set(rtwdev, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON);
	}
	rtw89_phy_write32_mask(rtwdev, R_CFO_TRK0, B_CFO_TRK_MSK, 0x1f);
	rtw89_phy_write32_mask(rtwdev, R_CFO_TRK1, B_CFO_TRK_MSK, 0x0c);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0x0, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0x0, RTW89_PHY_1);
	rtw89_phy_write32_clr(rtwdev, R_NDP_BRK0, B_NDP_RU_BRK);
	rtw89_phy_write32_set(rtwdev, R_NDP_BRK1, B_NDP_RU_BRK);

	rtw8852a_bb_macid_ctrl_init(rtwdev, RTW89_PHY_0);
}

static void rtw8852a_bbrst_for_rfk(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	rtw89_phy_write32_set(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_set(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
	rtw8852a_bb_reset_all(rtwdev, phy_idx);
	rtw89_phy_write32_clr(rtwdev, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN);
	rtw89_phy_write32_clr(rtwdev, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN);
	udelay(1);
}

static void rtw8852a_set_channel_bb(struct rtw89_dev *rtwdev,
				    struct rtw89_channel_params *param,
				    enum rtw89_phy_idx phy_idx)
{
	bool cck_en = param->center_chan <= 14;
	u8 pri_ch_idx = param->pri_ch_idx;

	if (cck_en)
		rtw8852a_ctrl_sco_cck(rtwdev, param->center_chan,
				      param->primary_chan, param->bandwidth);

	rtw8852a_ctrl_ch(rtwdev, param->center_chan, phy_idx);
	rtw8852a_ctrl_bw(rtwdev, pri_ch_idx, param->bandwidth, phy_idx);
	if (cck_en) {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_RXCCA, B_RXCCA_DIS, 1);
		rtw8852a_bbrst_for_rfk(rtwdev, phy_idx);
	}
	rtw8852a_spur_elimination(rtwdev, param->center_chan);
	rtw8852a_bb_reset_all(rtwdev, phy_idx);
}

static void rtw8852a_set_channel(struct rtw89_dev *rtwdev,
				 struct rtw89_channel_params *params)
{
	rtw8852a_set_channel_mac(rtwdev, params, RTW89_MAC_0);
	rtw8852a_set_channel_bb(rtwdev, params, RTW89_PHY_0);
}

static void rtw8852a_dfs_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_UPD_P0, B_UPD_P0_EN, 1);
	else
		rtw89_phy_write32_mask(rtwdev, R_UPD_P0, B_UPD_P0_EN, 0);
}

static void rtw8852a_tssi_cont_en(struct rtw89_dev *rtwdev, bool en,
				  enum rtw89_rf_path path)
{
	static const u32 tssi_trk[2] = {0x5818, 0x7818};
	static const u32 ctrl_bbrst[2] = {0x58dc, 0x78dc};

	if (en) {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], BIT(30), 0x0);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], BIT(30), 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, ctrl_bbrst[path], BIT(30), 0x1);
		rtw89_phy_write32_mask(rtwdev, tssi_trk[path], BIT(30), 0x1);
	}
}

static void rtw8852a_tssi_cont_en_phyidx(struct rtw89_dev *rtwdev, bool en,
					 u8 phy_idx)
{
	if (!rtwdev->dbcc_en) {
		rtw8852a_tssi_cont_en(rtwdev, en, RF_PATH_A);
		rtw8852a_tssi_cont_en(rtwdev, en, RF_PATH_B);
	} else {
		if (phy_idx == RTW89_PHY_0)
			rtw8852a_tssi_cont_en(rtwdev, en, RF_PATH_A);
		else
			rtw8852a_tssi_cont_en(rtwdev, en, RF_PATH_B);
	}
}

static void rtw8852a_adc_en(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST,
				       0x0);
	else
		rtw89_phy_write32_mask(rtwdev, R_ADC_FIFO, B_ADC_FIFO_RST,
				       0xf);
}

static void rtw8852a_set_channel_help(struct rtw89_dev *rtwdev, bool enter,
				      struct rtw89_channel_help_params *p)
{
	u8 phy_idx = RTW89_PHY_0;

	if (enter) {
		rtw89_mac_stop_sch_tx(rtwdev, RTW89_MAC_0, &p->tx_en, RTW89_SCH_TX_SEL_ALL);
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
		rtw8852a_dfs_en(rtwdev, false);
		rtw8852a_tssi_cont_en_phyidx(rtwdev, false, RTW89_PHY_0);
		rtw8852a_adc_en(rtwdev, false);
		fsleep(40);
		rtw8852a_bb_reset_en(rtwdev, phy_idx, false);
	} else {
		rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
		rtw8852a_adc_en(rtwdev, true);
		rtw8852a_dfs_en(rtwdev, true);
		rtw8852a_tssi_cont_en_phyidx(rtwdev, true, RTW89_PHY_0);
		rtw8852a_bb_reset_en(rtwdev, phy_idx, true);
		rtw89_mac_resume_sch_tx(rtwdev, RTW89_MAC_0, p->tx_en);
	}
}

static void rtw8852a_fem_setup(struct rtw89_dev *rtwdev)
{
	struct rtw89_efuse *efuse = &rtwdev->efuse;

	switch (efuse->rfe_type) {
	case 11:
	case 12:
	case 17:
	case 18:
	case 51:
	case 53:
		rtwdev->fem.epa_2g = true;
		rtwdev->fem.elna_2g = true;
		fallthrough;
	case 9:
	case 10:
	case 15:
	case 16:
		rtwdev->fem.epa_5g = true;
		rtwdev->fem.elna_5g = true;
		break;
	default:
		break;
	}
}

static void rtw8852a_rfk_init(struct rtw89_dev *rtwdev)
{
	rtwdev->is_tssi_mode[RF_PATH_A] = false;
	rtwdev->is_tssi_mode[RF_PATH_B] = false;

	rtw8852a_rck(rtwdev);
	rtw8852a_dack(rtwdev);
	rtw8852a_rx_dck(rtwdev, RTW89_PHY_0, true);
}

static void rtw8852a_rfk_channel(struct rtw89_dev *rtwdev)
{
	enum rtw89_phy_idx phy_idx = RTW89_PHY_0;

	rtw8852a_rx_dck(rtwdev, phy_idx, true);
	rtw8852a_iqk(rtwdev, phy_idx);
	rtw8852a_tssi(rtwdev, phy_idx);
	rtw8852a_dpk(rtwdev, phy_idx);
}

static void rtw8852a_rfk_band_changed(struct rtw89_dev *rtwdev)
{
	rtw8852a_tssi_scan(rtwdev, RTW89_PHY_0);
}

static void rtw8852a_rfk_scan(struct rtw89_dev *rtwdev, bool start)
{
	rtw8852a_wifi_scan_notify(rtwdev, start, RTW89_PHY_0);
}

static void rtw8852a_rfk_track(struct rtw89_dev *rtwdev)
{
	rtw8852a_dpk_track(rtwdev);
	rtw8852a_iqk_track(rtwdev);
	rtw8852a_tssi_track(rtwdev);
}

static u32 rtw8852a_bb_cal_txpwr_ref(struct rtw89_dev *rtwdev,
				     enum rtw89_phy_idx phy_idx, s16 ref)
{
	s8 ofst_int = 0;
	u8 base_cw_0db = 0x27;
	u16 tssi_16dbm_cw = 0x12c;
	s16 pwr_s10_3 = 0;
	s16 rf_pwr_cw = 0;
	u16 bb_pwr_cw = 0;
	u32 pwr_cw = 0;
	u32 tssi_ofst_cw = 0;

	pwr_s10_3 = (ref << 1) + (s16)(ofst_int) + (s16)(base_cw_0db << 3);
	bb_pwr_cw = FIELD_GET(GENMASK(2, 0), pwr_s10_3);
	rf_pwr_cw = FIELD_GET(GENMASK(8, 3), pwr_s10_3);
	rf_pwr_cw = clamp_t(s16, rf_pwr_cw, 15, 63);
	pwr_cw = (rf_pwr_cw << 3) | bb_pwr_cw;

	tssi_ofst_cw = (u32)((s16)tssi_16dbm_cw + (ref << 1) - (16 << 3));
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] tssi_ofst_cw=%d rf_cw=0x%x bb_cw=0x%x\n",
		    tssi_ofst_cw, rf_pwr_cw, bb_pwr_cw);

	return (tssi_ofst_cw << 18) | (pwr_cw << 9) | (ref & GENMASK(8, 0));
}

static
void rtw8852a_set_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				     s16 pw_ofst, enum rtw89_mac_idx mac_idx)
{
	s32 val_1t = 0;
	s32 val_2t = 0;
	u32 reg;

	if (pw_ofst < -16 || pw_ofst > 15) {
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[ULTB] Err pwr_offset=%d\n",
			    pw_ofst);
		return;
	}
	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_CTRL, mac_idx);
	rtw89_write32_set(rtwdev, reg, B_AX_PWR_UL_TB_CTRL_EN);
	val_1t = (s32)pw_ofst;
	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_1T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_1T_MASK, val_1t);
	val_2t = max(val_1t - 3, -16);
	reg = rtw89_mac_reg_by_idx(R_AX_PWR_UL_TB_2T, mac_idx);
	rtw89_write32_mask(rtwdev, reg, B_AX_PWR_UL_TB_2T_MASK, val_2t);
	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[ULTB] Set TB pwr_offset=(%d, %d)\n",
		    val_1t, val_2t);
}

static void rtw8852a_set_txpwr_ref(struct rtw89_dev *rtwdev,
				   enum rtw89_phy_idx phy_idx)
{
	static const u32 addr[RF_PATH_NUM_8852A] = {0x5800, 0x7800};
	const u32 mask = 0x7FFFFFF;
	const u8 ofst_ofdm = 0x4;
	const u8 ofst_cck = 0x8;
	s16 ref_ofdm = 0;
	s16 ref_cck = 0;
	u32 val;
	u8 i;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr reference\n");

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_CTRL,
				     GENMASK(27, 10), 0x0);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb ofdm txpwr ref\n");
	val = rtw8852a_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_ofdm);

	for (i = 0; i < RF_PATH_NUM_8852A; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_ofdm, mask, val,
				      phy_idx);

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set bb cck txpwr ref\n");
	val = rtw8852a_bb_cal_txpwr_ref(rtwdev, phy_idx, ref_cck);

	for (i = 0; i < RF_PATH_NUM_8852A; i++)
		rtw89_phy_write32_idx(rtwdev, addr[i] + ofst_cck, mask, val,
				      phy_idx);
}

static void rtw8852a_set_txpwr_byrate(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx)
{
	u8 ch = rtwdev->hal.current_channel;
	static const u8 rs[] = {
		RTW89_RS_CCK,
		RTW89_RS_OFDM,
		RTW89_RS_MCS,
		RTW89_RS_HEDCM,
	};
	s8 tmp;
	u8 i, j;
	u32 val, shf, addr = R_AX_PWR_BY_RATE;
	struct rtw89_rate_desc cur;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr byrate with ch=%d\n", ch);

	for (cur.nss = 0; cur.nss <= RTW89_NSS_2; cur.nss++) {
		for (i = 0; i < ARRAY_SIZE(rs); i++) {
			if (cur.nss >= rtw89_rs_nss_max[rs[i]])
				continue;

			val = 0;
			cur.rs = rs[i];

			for (j = 0; j < rtw89_rs_idx_max[rs[i]]; j++) {
				cur.idx = j;
				shf = (j % 4) * 8;
				tmp = rtw89_phy_read_txpwr_byrate(rtwdev, &cur);
				val |= (tmp << shf);

				if ((j + 1) % 4)
					continue;

				rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
				val = 0;
				addr += 4;
			}
		}
	}
}

static void rtw8852a_set_txpwr_offset(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx)
{
	struct rtw89_rate_desc desc = {
		.nss = RTW89_NSS_1,
		.rs = RTW89_RS_OFFSET,
	};
	u32 val = 0;
	s8 v;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR, "[TXPWR] set txpwr offset\n");

	for (desc.idx = 0; desc.idx < RTW89_RATE_OFFSET_MAX; desc.idx++) {
		v = rtw89_phy_read_txpwr_byrate(rtwdev, &desc);
		val |= ((v & 0xf) << (4 * desc.idx));
	}

	rtw89_mac_txpwr_write32_mask(rtwdev, phy_idx, R_AX_PWR_RATE_OFST_CTRL,
				     GENMASK(19, 0), val);
}

static void rtw8852a_set_txpwr_limit(struct rtw89_dev *rtwdev,
				     enum rtw89_phy_idx phy_idx)
{
#define __MAC_TXPWR_LMT_PAGE_SIZE 40
	u8 ch = rtwdev->hal.current_channel;
	u8 bw = rtwdev->hal.current_band_width;
	struct rtw89_txpwr_limit lmt[NTX_NUM_8852A];
	u32 addr, val;
	const s8 *ptr;
	u8 i, j, k;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr limit with ch=%d bw=%d\n", ch, bw);

	for (i = 0; i < NTX_NUM_8852A; i++) {
		rtw89_phy_fill_txpwr_limit(rtwdev, &lmt[i], i);

		for (j = 0; j < __MAC_TXPWR_LMT_PAGE_SIZE; j += 4) {
			addr = R_AX_PWR_LMT + j + __MAC_TXPWR_LMT_PAGE_SIZE * i;
			ptr = (s8 *)&lmt[i] + j;
			val = 0;

			for (k = 0; k < 4; k++)
				val |= (ptr[k] << (8 * k));

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
		}
	}
#undef __MAC_TXPWR_LMT_PAGE_SIZE
}

static void rtw8852a_set_txpwr_limit_ru(struct rtw89_dev *rtwdev,
					enum rtw89_phy_idx phy_idx)
{
#define __MAC_TXPWR_LMT_RU_PAGE_SIZE 24
	u8 ch = rtwdev->hal.current_channel;
	u8 bw = rtwdev->hal.current_band_width;
	struct rtw89_txpwr_limit_ru lmt_ru[NTX_NUM_8852A];
	u32 addr, val;
	const s8 *ptr;
	u8 i, j, k;

	rtw89_debug(rtwdev, RTW89_DBG_TXPWR,
		    "[TXPWR] set txpwr limit ru with ch=%d bw=%d\n", ch, bw);

	for (i = 0; i < NTX_NUM_8852A; i++) {
		rtw89_phy_fill_txpwr_limit_ru(rtwdev, &lmt_ru[i], i);

		for (j = 0; j < __MAC_TXPWR_LMT_RU_PAGE_SIZE; j += 4) {
			addr = R_AX_PWR_RU_LMT + j +
			       __MAC_TXPWR_LMT_RU_PAGE_SIZE * i;
			ptr = (s8 *)&lmt_ru[i] + j;
			val = 0;

			for (k = 0; k < 4; k++)
				val |= (ptr[k] << (8 * k));

			rtw89_mac_txpwr_write32(rtwdev, phy_idx, addr, val);
		}
	}

#undef __MAC_TXPWR_LMT_RU_PAGE_SIZE
}

static void rtw8852a_set_txpwr(struct rtw89_dev *rtwdev)
{
	rtw8852a_set_txpwr_byrate(rtwdev, RTW89_PHY_0);
	rtw8852a_set_txpwr_limit(rtwdev, RTW89_PHY_0);
	rtw8852a_set_txpwr_limit_ru(rtwdev, RTW89_PHY_0);
}

static void rtw8852a_set_txpwr_ctrl(struct rtw89_dev *rtwdev)
{
	rtw8852a_set_txpwr_ref(rtwdev, RTW89_PHY_0);
	rtw8852a_set_txpwr_offset(rtwdev, RTW89_PHY_0);
}

static int
rtw8852a_init_txpwr_unit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	int ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL2, 0x07763333);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_COEXT_CTRL, 0x01ebf004);
	if (ret)
		return ret;

	ret = rtw89_mac_txpwr_write32(rtwdev, phy_idx, R_AX_PWR_UL_CTRL0, 0x0002f8ff);
	if (ret)
		return ret;

	return 0;
}

void rtw8852a_bb_set_plcp_tx(struct rtw89_dev *rtwdev)
{
	u8 i = 0;
	u32 addr, val;

	for (i = 0; i < ARRAY_SIZE(rtw8852a_pmac_ht20_mcs7_tbl); i++) {
		addr = rtw8852a_pmac_ht20_mcs7_tbl[i].addr;
		val = rtw8852a_pmac_ht20_mcs7_tbl[i].data;
		rtw89_phy_write32(rtwdev, addr, val);
	}
}

static void rtw8852a_stop_pmac_tx(struct rtw89_dev *rtwdev,
				  struct rtw8852a_bb_pmac_info *tx_info,
				  enum rtw89_phy_idx idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC Stop Tx");
	if (tx_info->mode == CONT_TX)
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_CTX_EN, 0,
				      idx);
	else if (tx_info->mode == PKTS_TX)
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_PTX_EN, 0,
				      idx);
}

static void rtw8852a_start_pmac_tx(struct rtw89_dev *rtwdev,
				   struct rtw8852a_bb_pmac_info *tx_info,
				   enum rtw89_phy_idx idx)
{
	enum rtw8852a_pmac_mode mode = tx_info->mode;
	u32 pkt_cnt = tx_info->tx_cnt;
	u16 period = tx_info->period;

	if (mode == CONT_TX && !tx_info->is_cck) {
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_CTX_EN, 1,
				      idx);
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CTx Start");
	} else if (mode == PKTS_TX) {
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD, B_PMAC_PTX_EN, 1,
				      idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_PRD,
				      B_PMAC_TX_PRD_MSK, period, idx);
		rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CNT, B_PMAC_TX_CNT_MSK,
				      pkt_cnt, idx);
		rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC PTx Start");
	}
	rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CTRL, B_PMAC_TXEN_DIS, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_TX_CTRL, B_PMAC_TXEN_DIS, 0, idx);
}

void rtw8852a_bb_set_pmac_tx(struct rtw89_dev *rtwdev,
			     struct rtw8852a_bb_pmac_info *tx_info,
			     enum rtw89_phy_idx idx)
{
	if (!tx_info->en_pmac_tx) {
		rtw8852a_stop_pmac_tx(rtwdev, tx_info, idx);
		rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 0, idx);
		if (rtwdev->hal.current_band_type == RTW89_BAND_2G)
			rtw89_phy_write32_clr(rtwdev, R_RXCCA, B_RXCCA_DIS);
		return;
	}
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC Tx Enable");
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_TXEN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_RXEN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RX_CFG1, B_PMAC_OPT1_MSK, 0x3f,
			      idx);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PD_CTRL, B_PD_HIT_DIS, 1, idx);
	rtw89_phy_write32_set(rtwdev, R_RXCCA, B_RXCCA_DIS);
	rtw89_phy_write32_idx(rtwdev, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1, idx);
	rtw8852a_start_pmac_tx(rtwdev, tx_info, idx);
}

void rtw8852a_bb_set_pmac_pkt_tx(struct rtw89_dev *rtwdev, u8 enable,
				 u16 tx_cnt, u16 period, u16 tx_time,
				 enum rtw89_phy_idx idx)
{
	struct rtw8852a_bb_pmac_info tx_info = {0};

	tx_info.en_pmac_tx = enable;
	tx_info.is_cck = 0;
	tx_info.mode = PKTS_TX;
	tx_info.tx_cnt = tx_cnt;
	tx_info.period = period;
	tx_info.tx_time = tx_time;
	rtw8852a_bb_set_pmac_tx(rtwdev, &tx_info, idx);
}

void rtw8852a_bb_set_power(struct rtw89_dev *rtwdev, s16 pwr_dbm,
			   enum rtw89_phy_idx idx)
{
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CFG Tx PWR = %d", pwr_dbm);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 1, idx);
	rtw89_phy_write32_idx(rtwdev, R_TXPWR, B_TXPWR_MSK, pwr_dbm, idx);
}

void rtw8852a_bb_cfg_tx_path(struct rtw89_dev *rtwdev, u8 tx_path)
{
	u32 rst_mask0 = 0;
	u32 rst_mask1 = 0;

	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 7, RTW89_PHY_0);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 7, RTW89_PHY_1);
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "PMAC CFG Tx Path = %d", tx_path);
	if (!rtwdev->dbcc_en) {
		if (tx_path == RF_PATH_A) {
			rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL,
					       B_TXPATH_SEL_MSK, 1);
			rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP,
					       B_TXNSS_MAP_MSK, 0);
		} else if (tx_path == RF_PATH_B) {
			rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL,
					       B_TXPATH_SEL_MSK, 2);
			rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP,
					       B_TXNSS_MAP_MSK, 0);
		} else if (tx_path == RF_PATH_AB) {
			rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL,
					       B_TXPATH_SEL_MSK, 3);
			rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP,
					       B_TXNSS_MAP_MSK, 4);
		} else {
			rtw89_debug(rtwdev, RTW89_DBG_TSSI, "Error Tx Path");
		}
	} else {
		rtw89_phy_write32_mask(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK,
				       1);
		rtw89_phy_write32_idx(rtwdev, R_TXPATH_SEL, B_TXPATH_SEL_MSK, 2,
				      RTW89_PHY_1);
		rtw89_phy_write32_mask(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK,
				       0);
		rtw89_phy_write32_idx(rtwdev, R_TXNSS_MAP, B_TXNSS_MAP_MSK, 4,
				      RTW89_PHY_1);
	}
	rst_mask0 = B_P0_TXPW_RSTB_MANON | B_P0_TXPW_RSTB_TSSI;
	rst_mask1 = B_P1_TXPW_RSTB_MANON | B_P1_TXPW_RSTB_TSSI;
	if (tx_path == RF_PATH_A) {
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 1);
		rtw89_phy_write32_mask(rtwdev, R_P0_TXPW_RSTB, rst_mask0, 3);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 1);
		rtw89_phy_write32_mask(rtwdev, R_P1_TXPW_RSTB, rst_mask1, 3);
	}
}

void rtw8852a_bb_tx_mode_switch(struct rtw89_dev *rtwdev,
				enum rtw89_phy_idx idx, u8 mode)
{
	if (mode != 0)
		return;
	rtw89_debug(rtwdev, RTW89_DBG_TSSI, "Tx mode switch");
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_TXEN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_GNT, B_PMAC_GNT_RXEN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RX_CFG1, B_PMAC_OPT1_MSK, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_PMAC_RXMOD, B_PMAC_RXMOD_MSK, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_DPD_EN, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_MOD, 0, idx);
	rtw89_phy_write32_idx(rtwdev, R_MAC_SEL, B_MAC_SEL_PWR_EN, 0, idx);
}

static void rtw8852a_bb_ctrl_btc_preagc(struct rtw89_dev *rtwdev, bool bt_en)
{
	rtw89_phy_write_reg3_tbl(rtwdev, bt_en ? &rtw8852a_btc_preagc_en_defs_tbl :
						 &rtw8852a_btc_preagc_dis_defs_tbl);
}

static u8 rtw8852a_get_thermal(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path)
{
	if (rtwdev->is_tssi_mode[rf_path]) {
		u32 addr = 0x1c10 + (rf_path << 13);

		return (u8)rtw89_phy_read32_mask(rtwdev, addr, 0x3F000000);
	}

	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x0);
	rtw89_write_rf(rtwdev, rf_path, RR_TM, RR_TM_TRI, 0x1);

	fsleep(200);

	return (u8)rtw89_read_rf(rtwdev, rf_path, RR_TM, RR_TM_VAL);
}

static void rtw8852a_btc_set_rfe(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_module *module = &btc->mdinfo;

	module->rfe_type = rtwdev->efuse.rfe_type;
	module->cv = rtwdev->hal.cv;
	module->bt_solo = 0;
	module->switch_type = BTC_SWITCH_INTERNAL;

	if (module->rfe_type > 0)
		module->ant.num = (module->rfe_type % 2 ? 2 : 3);
	else
		module->ant.num = 2;

	module->ant.diversity = 0;
	module->ant.isolation = 10;

	if (module->ant.num == 3) {
		module->ant.type = BTC_ANT_DEDICATED;
		module->bt_pos = BTC_BT_ALONE;
	} else {
		module->ant.type = BTC_ANT_SHARED;
		module->bt_pos = BTC_BT_BTG;
	}
}

static
void rtw8852a_set_trx_mask(struct rtw89_dev *rtwdev, u8 path, u8 group, u32 val)
{
	rtw89_write_rf(rtwdev, path, RR_LUTWE, 0xfffff, 0x20000);
	rtw89_write_rf(rtwdev, path, RR_LUTWA, 0xfffff, group);
	rtw89_write_rf(rtwdev, path, RR_LUTWD0, 0xfffff, val);
	rtw89_write_rf(rtwdev, path, RR_LUTWE, 0xfffff, 0x0);
}

static void rtw8852a_ctrl_btg(struct rtw89_dev *rtwdev, bool btg)
{
	if (btg) {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG, B_PATH0_BTG_SHEN, 0x1);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG, B_PATH1_BTG_SHEN, 0x3);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0x0);
	} else {
		rtw89_phy_write32_mask(rtwdev, R_PATH0_BTG, B_PATH0_BTG_SHEN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PATH1_BTG, B_PATH1_BTG_SHEN, 0x0);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P1, 0xf);
		rtw89_phy_write32_mask(rtwdev, R_PMAC_GNT, B_PMAC_GNT_P2, 0x4);
	}
}

static void rtw8852a_btc_init_cfg(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_module *module = &btc->mdinfo;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_mac_ax_coex coex_params = {
		.pta_mode = RTW89_MAC_AX_COEX_RTK_MODE,
		.direction = RTW89_MAC_AX_COEX_INNER,
	};

	/* PTA init  */
	rtw89_mac_coex_init(rtwdev, &coex_params);

	/* set WL Tx response = Hi-Pri */
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_TX_RESP, true);
	chip->ops->btc_set_wl_pri(rtwdev, BTC_PRI_MASK_BEACON, true);

	/* set rf gnt debug off */
	rtw89_write_rf(rtwdev, RF_PATH_A, RR_WLSEL, 0xfffff, 0x0);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_WLSEL, 0xfffff, 0x0);

	/* set WL Tx thru in TRX mask table if GNT_WL = 0 && BT_S1 = ss group */
	if (module->ant.type == BTC_ANT_SHARED) {
		rtw8852a_set_trx_mask(rtwdev,
				      RF_PATH_A, BTC_BT_SS_GROUP, 0x5ff);
		rtw8852a_set_trx_mask(rtwdev,
				      RF_PATH_B, BTC_BT_SS_GROUP, 0x5ff);
	} else { /* set WL Tx stb if GNT_WL = 0 && BT_S1 = ss group for 3-ant */
		rtw8852a_set_trx_mask(rtwdev,
				      RF_PATH_A, BTC_BT_SS_GROUP, 0x5df);
		rtw8852a_set_trx_mask(rtwdev,
				      RF_PATH_B, BTC_BT_SS_GROUP, 0x5df);
	}

	/* set PTA break table */
	rtw89_write32(rtwdev, R_BTC_BREAK_TABLE, BTC_BREAK_PARAM);

	 /* enable BT counter 0xda40[16,2] = 2b'11 */
	rtw89_write32_set(rtwdev,
			  R_AX_CSR_MODE, B_AX_BT_CNT_RST | B_AX_STATIS_BT_EN);
	btc->cx.wl.status.map.init_ok = true;
}

static
void rtw8852a_btc_set_wl_pri(struct rtw89_dev *rtwdev, u8 map, bool state)
{
	u32 bitmap = 0;
	u32 reg = 0;

	switch (map) {
	case BTC_PRI_MASK_TX_RESP:
		reg = R_BTC_BT_COEX_MSK_TABLE;
		bitmap = B_BTC_PRI_MASK_TX_RESP_V1;
		break;
	case BTC_PRI_MASK_BEACON:
		reg = R_AX_WL_PRI_MSK;
		bitmap = B_AX_PTA_WL_PRI_MASK_BCNQ;
		break;
	default:
		return;
	}

	if (state)
		rtw89_write32_set(rtwdev, reg, bitmap);
	else
		rtw89_write32_clr(rtwdev, reg, bitmap);
}

static inline u32 __btc_ctrl_val_all_time(u32 ctrl)
{
	return FIELD_GET(GENMASK(15, 0), ctrl);
}

static inline u32 __btc_ctrl_rst_all_time(u32 cur)
{
	return cur & ~B_AX_FORCE_PWR_BY_RATE_EN;
}

static inline u32 __btc_ctrl_gen_all_time(u32 cur, u32 val)
{
	u32 hv = cur & ~B_AX_FORCE_PWR_BY_RATE_VALUE_MASK;
	u32 lv = val & B_AX_FORCE_PWR_BY_RATE_VALUE_MASK;

	return hv | lv | B_AX_FORCE_PWR_BY_RATE_EN;
}

static inline u32 __btc_ctrl_val_gnt_bt(u32 ctrl)
{
	return FIELD_GET(GENMASK(31, 16), ctrl);
}

static inline u32 __btc_ctrl_rst_gnt_bt(u32 cur)
{
	return cur & ~B_AX_TXAGC_BT_EN;
}

static inline u32 __btc_ctrl_gen_gnt_bt(u32 cur, u32 val)
{
	u32 ov = cur & ~B_AX_TXAGC_BT_MASK;
	u32 iv = FIELD_PREP(B_AX_TXAGC_BT_MASK, val);

	return ov | iv | B_AX_TXAGC_BT_EN;
}

static void
rtw8852a_btc_set_wl_txpwr_ctrl(struct rtw89_dev *rtwdev, u32 txpwr_val)
{
	const u32 __btc_cr_all_time = R_AX_PWR_RATE_CTRL;
	const u32 __btc_cr_gnt_bt = R_AX_PWR_COEXT_CTRL;

#define __do_clr(_chk) ((_chk) == GENMASK(15, 0))
#define __handle(_case)							\
	do {								\
		const u32 _reg = __btc_cr_ ## _case;			\
		u32 _val = __btc_ctrl_val_ ## _case(txpwr_val);		\
		u32 _cur, _wrt;						\
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,			\
			    "btc ctrl %s: 0x%x\n", #_case, _val);	\
		rtw89_mac_txpwr_read32(rtwdev, RTW89_PHY_0, _reg, &_cur);\
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,			\
			    "btc ctrl ori 0x%x: 0x%x\n", _reg, _cur);	\
		_wrt = __do_clr(_val) ?					\
			__btc_ctrl_rst_ ## _case(_cur) :		\
			__btc_ctrl_gen_ ## _case(_cur, _val);		\
		rtw89_mac_txpwr_write32(rtwdev, RTW89_PHY_0, _reg, _wrt);\
		rtw89_debug(rtwdev, RTW89_DBG_TXPWR,			\
			    "btc ctrl set 0x%x: 0x%x\n", _reg, _wrt);	\
	} while (0)

	__handle(all_time);
	__handle(gnt_bt);

#undef __handle
#undef __do_clr
}

static
s8 rtw8852a_btc_get_bt_rssi(struct rtw89_dev *rtwdev, s8 val)
{
	return clamp_t(s8, val, -100, 0) + 100;
}

static struct rtw89_btc_rf_trx_para rtw89_btc_8852a_rf_ul[] = {
	{255, 0, 0, 7}, /* 0 -> original */
	{255, 2, 0, 7}, /* 1 -> for BT-connected ACI issue && BTG co-rx */
	{255, 0, 0, 7}, /* 2 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* 3- >reserved for shared-antenna */
	{255, 0, 0, 7}, /* 4 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* the below id is for non-shared-antenna free-run */
	{6, 1, 0, 7},
	{13, 1, 0, 7},
	{13, 1, 0, 7}
};

static struct rtw89_btc_rf_trx_para rtw89_btc_8852a_rf_dl[] = {
	{255, 0, 0, 7}, /* 0 -> original */
	{255, 2, 0, 7}, /* 1 -> reserved for shared-antenna */
	{255, 0, 0, 7}, /* 2 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* 3- >reserved for shared-antenna */
	{255, 0, 0, 7}, /* 4 ->reserved for shared-antenna */
	{255, 0, 0, 7}, /* the below id is for non-shared-antenna free-run */
	{255, 1, 0, 7},
	{255, 1, 0, 7},
	{255, 1, 0, 7}
};

static const
u8 rtw89_btc_8852a_wl_rssi_thres[BTC_WL_RSSI_THMAX] = {60, 50, 40, 30};
static const
u8 rtw89_btc_8852a_bt_rssi_thres[BTC_BT_RSSI_THMAX] = {40, 36, 31, 28};

static struct rtw89_btc_fbtc_mreg rtw89_btc_8852a_mon_reg[] = {
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda24),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda28),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda2c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda30),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda4c),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda10),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda20),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xda34),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0xcef4),
	RTW89_DEF_FBTC_MREG(REG_MAC, 4, 0x8424),
	RTW89_DEF_FBTC_MREG(REG_BB, 4, 0x980),
	RTW89_DEF_FBTC_MREG(REG_BT_MODEM, 4, 0x178),
};

static
void rtw8852a_btc_bt_aci_imp(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_dm *dm = &btc->dm;
	struct rtw89_btc_bt_info *bt = &btc->cx.bt;
	struct rtw89_btc_bt_link_info *b = &bt->link_info;

	/* fix LNA2 = level-5 for BT ACI issue at BTG */
	if (btc->dm.wl_btg_rx && b->profile_cnt.now != 0)
		dm->trx_para_level = 1;
}

static
void rtw8852a_btc_update_bt_cnt(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	struct rtw89_btc_cx *cx = &btc->cx;
	u32 val;

	val = rtw89_read32(rtwdev, R_AX_BT_STAST_HIGH);
	cx->cnt_bt[BTC_BCNT_HIPRI_TX] = FIELD_GET(B_AX_STATIS_BT_HI_TX_MASK, val);
	cx->cnt_bt[BTC_BCNT_HIPRI_RX] = FIELD_GET(B_AX_STATIS_BT_HI_RX_MASK, val);

	val = rtw89_read32(rtwdev, R_AX_BT_STAST_LOW);
	cx->cnt_bt[BTC_BCNT_LOPRI_TX] = FIELD_GET(B_AX_STATIS_BT_LO_TX_1_MASK, val);
	cx->cnt_bt[BTC_BCNT_LOPRI_RX] = FIELD_GET(B_AX_STATIS_BT_LO_RX_1_MASK, val);

	/* clock-gate off before reset counter*/
	rtw89_write32_set(rtwdev, R_AX_BTC_CFG, B_AX_DIS_BTC_CLK_G);
	rtw89_write32_clr(rtwdev, R_AX_CSR_MODE, B_AX_BT_CNT_RST);
	rtw89_write32_set(rtwdev, R_AX_CSR_MODE, B_AX_BT_CNT_RST);
	rtw89_write32_clr(rtwdev, R_AX_BTC_CFG, B_AX_DIS_BTC_CLK_G);
}

static
void rtw8852a_btc_wl_s1_standby(struct rtw89_dev *rtwdev, bool state)
{
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x80000);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWA, RFREG_MASK, 0x1);
	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD1, RFREG_MASK, 0x1);

	/* set WL standby = Rx for GNT_BT_Tx = 1->0 settle issue */
	if (state)
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0,
			       RFREG_MASK, 0xa2d7c);
	else
		rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWD0,
			       RFREG_MASK, 0xa2020);

	rtw89_write_rf(rtwdev, RF_PATH_B, RR_LUTWE, RFREG_MASK, 0x0);
}

static void rtw8852a_query_ppdu(struct rtw89_dev *rtwdev,
				struct rtw89_rx_phy_ppdu *phy_ppdu,
				struct ieee80211_rx_status *status)
{
	u8 path;
	s8 *rx_power = phy_ppdu->rssi;

	status->signal = max_t(s8, rx_power[RF_PATH_A], rx_power[RF_PATH_B]);
	for (path = 0; path < rtwdev->chip->rf_path_num; path++) {
		status->chains |= BIT(path);
		status->chain_signal[path] = rx_power[path];
	}
}

static const struct rtw89_chip_ops rtw8852a_chip_ops = {
	.bb_reset		= rtw8852a_bb_reset,
	.bb_sethw		= rtw8852a_bb_sethw,
	.read_rf		= rtw89_phy_read_rf,
	.write_rf		= rtw89_phy_write_rf,
	.set_channel		= rtw8852a_set_channel,
	.set_channel_help	= rtw8852a_set_channel_help,
	.read_efuse		= rtw8852a_read_efuse,
	.read_phycap		= rtw8852a_read_phycap,
	.fem_setup		= rtw8852a_fem_setup,
	.rfk_init		= rtw8852a_rfk_init,
	.rfk_channel		= rtw8852a_rfk_channel,
	.rfk_band_changed	= rtw8852a_rfk_band_changed,
	.rfk_scan		= rtw8852a_rfk_scan,
	.rfk_track		= rtw8852a_rfk_track,
	.power_trim		= rtw8852a_power_trim,
	.set_txpwr		= rtw8852a_set_txpwr,
	.set_txpwr_ctrl		= rtw8852a_set_txpwr_ctrl,
	.init_txpwr_unit	= rtw8852a_init_txpwr_unit,
	.get_thermal		= rtw8852a_get_thermal,
	.ctrl_btg		= rtw8852a_ctrl_btg,
	.query_ppdu		= rtw8852a_query_ppdu,
	.bb_ctrl_btc_preagc	= rtw8852a_bb_ctrl_btc_preagc,
	.set_txpwr_ul_tb_offset	= rtw8852a_set_txpwr_ul_tb_offset,

	.btc_set_rfe		= rtw8852a_btc_set_rfe,
	.btc_init_cfg		= rtw8852a_btc_init_cfg,
	.btc_set_wl_pri		= rtw8852a_btc_set_wl_pri,
	.btc_set_wl_txpwr_ctrl	= rtw8852a_btc_set_wl_txpwr_ctrl,
	.btc_get_bt_rssi	= rtw8852a_btc_get_bt_rssi,
	.btc_bt_aci_imp		= rtw8852a_btc_bt_aci_imp,
	.btc_update_bt_cnt	= rtw8852a_btc_update_bt_cnt,
	.btc_wl_s1_standby	= rtw8852a_btc_wl_s1_standby,
};

const struct rtw89_chip_info rtw8852a_chip_info = {
	.chip_id		= RTL8852A,
	.ops			= &rtw8852a_chip_ops,
	.fw_name		= "rtw89/rtw8852a_fw.bin",
	.fifo_size		= 458752,
	.max_amsdu_limit	= 3500,
	.dis_2g_40m_ul_ofdma	= true,
	.hfc_param_ini		= rtw8852a_hfc_param_ini_pcie,
	.dle_mem		= rtw8852a_dle_mem_pcie,
	.rf_base_addr		= {0xc000, 0xd000},
	.pwr_on_seq		= pwr_on_seq_8852a,
	.pwr_off_seq		= pwr_off_seq_8852a,
	.bb_table		= &rtw89_8852a_phy_bb_table,
	.rf_table		= {&rtw89_8852a_phy_radioa_table,
				   &rtw89_8852a_phy_radiob_table,},
	.nctl_table		= &rtw89_8852a_phy_nctl_table,
	.byr_table		= &rtw89_8852a_byr_table,
	.txpwr_lmt_2g		= &rtw89_8852a_txpwr_lmt_2g,
	.txpwr_lmt_5g		= &rtw89_8852a_txpwr_lmt_5g,
	.txpwr_lmt_ru_2g	= &rtw89_8852a_txpwr_lmt_ru_2g,
	.txpwr_lmt_ru_5g	= &rtw89_8852a_txpwr_lmt_ru_5g,
	.txpwr_factor_rf	= 2,
	.txpwr_factor_mac	= 1,
	.dig_table		= &rtw89_8852a_phy_dig_table,
	.rf_path_num		= 2,
	.tx_nss			= 2,
	.rx_nss			= 2,
	.acam_num		= 128,
	.bcam_num		= 10,
	.scam_num		= 128,
	.sec_ctrl_efuse_size	= 4,
	.physical_efuse_size	= 1216,
	.logical_efuse_size	= 1536,
	.limit_efuse_size	= 1152,
	.phycap_addr		= 0x580,
	.phycap_size		= 128,
	.para_ver		= 0x05050764,
	.wlcx_desired		= 0x05050000,
	.btcx_desired		= 0x5,
	.scbd			= 0x1,
	.mailbox		= 0x1,
	.afh_guard_ch		= 6,
	.wl_rssi_thres		= rtw89_btc_8852a_wl_rssi_thres,
	.bt_rssi_thres		= rtw89_btc_8852a_bt_rssi_thres,
	.rssi_tol		= 2,
	.mon_reg_num		= ARRAY_SIZE(rtw89_btc_8852a_mon_reg),
	.mon_reg		= rtw89_btc_8852a_mon_reg,
	.rf_para_ulink_num	= ARRAY_SIZE(rtw89_btc_8852a_rf_ul),
	.rf_para_ulink		= rtw89_btc_8852a_rf_ul,
	.rf_para_dlink_num	= ARRAY_SIZE(rtw89_btc_8852a_rf_dl),
	.rf_para_dlink		= rtw89_btc_8852a_rf_dl,
	.ps_mode_supported	= BIT(RTW89_PS_MODE_RFOFF) |
				  BIT(RTW89_PS_MODE_CLK_GATED) |
				  BIT(RTW89_PS_MODE_PWR_GATED),
};
EXPORT_SYMBOL(rtw8852a_chip_info);

MODULE_FIRMWARE("rtw89/rtw8852a_fw.bin");
