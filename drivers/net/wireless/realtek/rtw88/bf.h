/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation.
 */

#ifndef __RTW_BF_H_
#define __RTW_BF_H_

#define REG_TXBF_CTRL		0x042C
#define REG_RRSR		0x0440
#define REG_NDPA_OPT_CTRL	0x045F

#define REG_ASSOCIATED_BFMER0_INFO	0x06E4
#define REG_ASSOCIATED_BFMER1_INFO	0x06EC
#define REG_TX_CSI_RPT_PARAM_BW20	0x06F4
#define REG_SND_PTCL_CTRL		0x0718
#define BIT_DIS_CHK_VHTSIGB_CRC		BIT(6)
#define BIT_DIS_CHK_VHTSIGA_CRC		BIT(5)
#define BIT_MASK_BEAMFORM		(GENMASK(4, 0) | BIT(7))
#define REG_MU_TX_CTL			0x14C0
#define REG_MU_STA_GID_VLD		0x14C4
#define REG_MU_STA_USER_POS_INFO	0x14C8
#define REG_CSI_RRSR			0x1678
#define REG_WMAC_MU_BF_OPTION		0x167C
#define REG_WMAC_MU_BF_CTL		0x1680

#define BIT_WMAC_USE_NDPARATE			BIT(30)
#define BIT_WMAC_TXMU_ACKPOLICY_EN		BIT(6)
#define BIT_USE_NDPA_PARAMETER			BIT(30)
#define BIT_MU_P1_WAIT_STATE_EN			BIT(16)
#define BIT_EN_MU_MIMO				BIT(7)

#define R_MU_RL				0xf
#define BIT_SHIFT_R_MU_RL		12
#define BIT_SHIFT_WMAC_TXMU_ACKPOLICY	4
#define BIT_SHIFT_CSI_RATE		24

#define BIT_MASK_R_MU_RL (R_MU_RL << BIT_SHIFT_R_MU_RL)
#define BIT_MASK_R_MU_TABLE_VALID	0x3f
#define BIT_MASK_CSI_RATE_VAL		0x3F
#define BIT_MASK_CSI_RATE (BIT_MASK_CSI_RATE_VAL << BIT_SHIFT_CSI_RATE)

#define BIT_RXFLTMAP0_ACTIONNOACK	BIT(14)
#define BIT_RXFLTMAP1_BF		(BIT(4) | BIT(5))
#define BIT_RXFLTMAP1_BF_REPORT_POLL	BIT(4)
#define BIT_RXFLTMAP4_BF_REPORT_POLL	BIT(4)

#define RTW_NDP_RX_STANDBY_TIME	0x70
#define RTW_SND_CTRL_REMOVE	0x98
#define RTW_SND_CTRL_SOUNDING	0x9B

enum csi_seg_len {
	HAL_CSI_SEG_4K = 0,
	HAL_CSI_SEG_8K = 1,
	HAL_CSI_SEG_11K = 2,
};

struct cfg_mumimo_para {
	u8 sounding_sts[6];
	u16 grouping_bitmap;
	u8 mu_tx_en;
	u32 given_gid_tab[2];
	u32 given_user_pos[4];
};

struct mu_bfer_init_para {
	u16 paid;
	u16 csi_para;
	u16 my_aid;
	enum csi_seg_len csi_length_sel;
	u8 bfer_address[ETH_ALEN];
};

void rtw_bf_disassoc(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *bss_conf);
void rtw_bf_assoc(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		  struct ieee80211_bss_conf *bss_conf);
void rtw_bf_init_bfer_entry_mu(struct rtw_dev *rtwdev,
			       struct mu_bfer_init_para *param);
void rtw_bf_cfg_sounding(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			 enum rtw_trx_desc_rate rate);
void rtw_bf_cfg_mu_bfee(struct rtw_dev *rtwdev, struct cfg_mumimo_para *param);
void rtw_bf_del_bfer_entry_mu(struct rtw_dev *rtwdev);
void rtw_bf_del_sounding(struct rtw_dev *rtwdev);
void rtw_bf_enable_bfee_su(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			   struct rtw_bfee *bfee);
void rtw_bf_enable_bfee_mu(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			   struct rtw_bfee *bfee);
void rtw_bf_remove_bfee_su(struct rtw_dev *rtwdev, struct rtw_bfee *bfee);
void rtw_bf_remove_bfee_mu(struct rtw_dev *rtwdev, struct rtw_bfee *bfee);
void rtw_bf_set_gid_table(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *conf);
void rtw_bf_phy_init(struct rtw_dev *rtwdev);
void rtw_bf_cfg_csi_rate(struct rtw_dev *rtwdev, u8 rssi, u8 cur_rate,
			 u8 fixrate_en, u8 *new_rate);
static inline void rtw_chip_config_bfee(struct rtw_dev *rtwdev, struct rtw_vif *vif,
					struct rtw_bfee *bfee, bool enable)
{
	if (rtwdev->chip->ops->config_bfee)
		rtwdev->chip->ops->config_bfee(rtwdev, vif, bfee, enable);
}

static inline void rtw_chip_set_gid_table(struct rtw_dev *rtwdev,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *conf)
{
	if (rtwdev->chip->ops->set_gid_table)
		rtwdev->chip->ops->set_gid_table(rtwdev, vif, conf);
}

static inline void rtw_chip_cfg_csi_rate(struct rtw_dev *rtwdev, u8 rssi, u8 cur_rate,
					 u8 fixrate_en, u8 *new_rate)
{
	if (rtwdev->chip->ops->cfg_csi_rate)
		rtwdev->chip->ops->cfg_csi_rate(rtwdev, rssi, cur_rate,
						fixrate_en, new_rate);
}
#endif
