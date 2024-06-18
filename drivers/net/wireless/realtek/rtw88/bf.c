// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation.
 */

#include "main.h"
#include "reg.h"
#include "bf.h"
#include "debug.h"

void rtw_bf_disassoc(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *bss_conf)
{
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct rtw_bfee *bfee = &rtwvif->bfee;
	struct rtw_bf_info *bfinfo = &rtwdev->bf_info;

	if (bfee->role == RTW_BFEE_NONE)
		return;

	if (bfee->role == RTW_BFEE_MU)
		bfinfo->bfer_mu_cnt--;
	else if (bfee->role == RTW_BFEE_SU)
		bfinfo->bfer_su_cnt--;

	rtw_chip_config_bfee(rtwdev, rtwvif, bfee, false);

	bfee->role = RTW_BFEE_NONE;
}

void rtw_bf_assoc(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
		  struct ieee80211_bss_conf *bss_conf)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct rtw_bfee *bfee = &rtwvif->bfee;
	struct rtw_bf_info *bfinfo = &rtwdev->bf_info;
	struct ieee80211_sta *sta;
	struct ieee80211_sta_vht_cap *vht_cap;
	struct ieee80211_sta_vht_cap *ic_vht_cap;
	const u8 *bssid = bss_conf->bssid;
	u32 sound_dim;
	u8 i;

	if (!(chip->band & RTW_BAND_5G))
		return;

	rcu_read_lock();

	sta = ieee80211_find_sta(vif, bssid);
	if (!sta) {
		rcu_read_unlock();

		rtw_warn(rtwdev, "failed to find station entry for bss %pM\n",
			 bssid);
		return;
	}

	ic_vht_cap = &hw->wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
	vht_cap = &sta->deflink.vht_cap;

	rcu_read_unlock();

	if ((ic_vht_cap->cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) &&
	    (vht_cap->cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE)) {
		if (bfinfo->bfer_mu_cnt >= chip->bfer_mu_max_num) {
			rtw_dbg(rtwdev, RTW_DBG_BF, "mu bfer number over limit\n");
			return;
		}

		ether_addr_copy(bfee->mac_addr, bssid);
		bfee->role = RTW_BFEE_MU;
		bfee->p_aid = (bssid[5] << 1) | (bssid[4] >> 7);
		bfee->aid = vif->cfg.aid;
		bfinfo->bfer_mu_cnt++;

		rtw_chip_config_bfee(rtwdev, rtwvif, bfee, true);
	} else if ((ic_vht_cap->cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE) &&
		   (vht_cap->cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE)) {
		if (bfinfo->bfer_su_cnt >= chip->bfer_su_max_num) {
			rtw_dbg(rtwdev, RTW_DBG_BF, "su bfer number over limit\n");
			return;
		}

		sound_dim = vht_cap->cap &
			    IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK;
		sound_dim >>= IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;

		ether_addr_copy(bfee->mac_addr, bssid);
		bfee->role = RTW_BFEE_SU;
		bfee->sound_dim = (u8)sound_dim;
		bfee->g_id = 0;
		bfee->p_aid = (bssid[5] << 1) | (bssid[4] >> 7);
		bfinfo->bfer_su_cnt++;
		for (i = 0; i < chip->bfer_su_max_num; i++) {
			if (!test_bit(i, bfinfo->bfer_su_reg_maping)) {
				set_bit(i, bfinfo->bfer_su_reg_maping);
				bfee->su_reg_index = i;
				break;
			}
		}

		rtw_chip_config_bfee(rtwdev, rtwvif, bfee, true);
	}
}

void rtw_bf_init_bfer_entry_mu(struct rtw_dev *rtwdev,
			       struct mu_bfer_init_para *param)
{
	u16 mu_bf_ctl = 0;
	u8 *addr = param->bfer_address;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		rtw_write8(rtwdev, REG_ASSOCIATED_BFMER0_INFO + i, addr[i]);
	rtw_write16(rtwdev, REG_ASSOCIATED_BFMER0_INFO + 6, param->paid);
	rtw_write16(rtwdev, REG_TX_CSI_RPT_PARAM_BW20, param->csi_para);

	mu_bf_ctl = rtw_read16(rtwdev, REG_WMAC_MU_BF_CTL) & 0xC000;
	mu_bf_ctl |= param->my_aid | (param->csi_length_sel << 12);
	rtw_write16(rtwdev, REG_WMAC_MU_BF_CTL, mu_bf_ctl);
}

void rtw_bf_cfg_sounding(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			 enum rtw_trx_desc_rate rate)
{
	u32 psf_ctl = 0;
	u8 csi_rsc = 0x1;

	psf_ctl = rtw_read32(rtwdev, REG_BBPSF_CTRL) |
		  BIT_WMAC_USE_NDPARATE |
		  (csi_rsc << 13);

	rtw_write8_mask(rtwdev, REG_SND_PTCL_CTRL, BIT_MASK_BEAMFORM,
			RTW_SND_CTRL_SOUNDING);
	rtw_write8(rtwdev, REG_SND_PTCL_CTRL + 3, 0x26);
	rtw_write8_clr(rtwdev, REG_RXFLTMAP1, BIT_RXFLTMAP1_BF_REPORT_POLL);
	rtw_write8_clr(rtwdev, REG_RXFLTMAP4, BIT_RXFLTMAP4_BF_REPORT_POLL);

	if (vif->net_type == RTW_NET_AP_MODE)
		rtw_write32(rtwdev, REG_BBPSF_CTRL, psf_ctl | BIT(12));
	else
		rtw_write32(rtwdev, REG_BBPSF_CTRL, psf_ctl & ~BIT(12));
}

void rtw_bf_cfg_mu_bfee(struct rtw_dev *rtwdev, struct cfg_mumimo_para *param)
{
	u8 mu_tbl_sel;
	u8 mu_valid;

	mu_valid = rtw_read8(rtwdev, REG_MU_TX_CTL) &
		   ~BIT_MASK_R_MU_TABLE_VALID;

	rtw_write8(rtwdev, REG_MU_TX_CTL,
		   (mu_valid | BIT(0) | BIT(1)) & ~(BIT(7)));

	mu_tbl_sel = rtw_read8(rtwdev, REG_MU_TX_CTL + 1) & 0xF8;

	rtw_write8(rtwdev, REG_MU_TX_CTL + 1, mu_tbl_sel);
	rtw_write32(rtwdev, REG_MU_STA_GID_VLD, param->given_gid_tab[0]);
	rtw_write32(rtwdev, REG_MU_STA_USER_POS_INFO, param->given_user_pos[0]);
	rtw_write32(rtwdev, REG_MU_STA_USER_POS_INFO + 4,
		    param->given_user_pos[1]);

	rtw_write8(rtwdev, REG_MU_TX_CTL + 1, mu_tbl_sel | 1);
	rtw_write32(rtwdev, REG_MU_STA_GID_VLD, param->given_gid_tab[1]);
	rtw_write32(rtwdev, REG_MU_STA_USER_POS_INFO, param->given_user_pos[2]);
	rtw_write32(rtwdev, REG_MU_STA_USER_POS_INFO + 4,
		    param->given_user_pos[3]);
}

void rtw_bf_del_bfer_entry_mu(struct rtw_dev *rtwdev)
{
	rtw_write32(rtwdev, REG_ASSOCIATED_BFMER0_INFO, 0);
	rtw_write32(rtwdev, REG_ASSOCIATED_BFMER0_INFO + 4, 0);
	rtw_write16(rtwdev, REG_WMAC_MU_BF_CTL, 0);
	rtw_write8(rtwdev, REG_MU_TX_CTL, 0);
}

void rtw_bf_del_sounding(struct rtw_dev *rtwdev)
{
	rtw_write8_mask(rtwdev, REG_SND_PTCL_CTRL, BIT_MASK_BEAMFORM, 0);
}

void rtw_bf_enable_bfee_su(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			   struct rtw_bfee *bfee)
{
	u8 nc_index = hweight8(rtwdev->hal.antenna_rx) - 1;
	u8 nr_index = bfee->sound_dim;
	u8 grouping = 0, codebookinfo = 1, coefficientsize = 3;
	u32 addr_bfer_info, addr_csi_rpt, csi_param;
	u8 i;

	rtw_dbg(rtwdev, RTW_DBG_BF, "config as an su bfee\n");

	switch (bfee->su_reg_index) {
	case 1:
		addr_bfer_info = REG_ASSOCIATED_BFMER1_INFO;
		addr_csi_rpt = REG_TX_CSI_RPT_PARAM_BW20 + 2;
		break;
	case 0:
	default:
		addr_bfer_info = REG_ASSOCIATED_BFMER0_INFO;
		addr_csi_rpt = REG_TX_CSI_RPT_PARAM_BW20;
		break;
	}

	/* Sounding protocol control */
	rtw_write8_mask(rtwdev, REG_SND_PTCL_CTRL, BIT_MASK_BEAMFORM,
			RTW_SND_CTRL_SOUNDING);

	/* MAC address/Partial AID of Beamformer */
	for (i = 0; i < ETH_ALEN; i++)
		rtw_write8(rtwdev, addr_bfer_info + i, bfee->mac_addr[i]);

	csi_param = (u16)((coefficientsize << 10) |
			  (codebookinfo << 8) |
			  (grouping << 6) |
			  (nr_index << 3) |
			  nc_index);
	rtw_write16(rtwdev, addr_csi_rpt, csi_param);

	/* ndp rx standby timer */
	rtw_write8(rtwdev, REG_SND_PTCL_CTRL + 3, RTW_NDP_RX_STANDBY_TIME);
}
EXPORT_SYMBOL(rtw_bf_enable_bfee_su);

/* nc index: 1 2T2R 0 1T1R
 * nr index: 1 use Nsts 0 use reg setting
 * codebookinfo: 1 802.11ac 3 802.11n
 */
void rtw_bf_enable_bfee_mu(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			   struct rtw_bfee *bfee)
{
	struct rtw_bf_info *bf_info = &rtwdev->bf_info;
	struct mu_bfer_init_para param;
	u8 nc_index = hweight8(rtwdev->hal.antenna_rx) - 1;
	u8 nr_index = 1;
	u8 grouping = 0, codebookinfo = 1, coefficientsize = 0;
	u32 csi_param;

	rtw_dbg(rtwdev, RTW_DBG_BF, "config as an mu bfee\n");

	csi_param = (u16)((coefficientsize << 10) |
			  (codebookinfo << 8) |
			  (grouping << 6) |
			  (nr_index << 3) |
			  nc_index);

	rtw_dbg(rtwdev, RTW_DBG_BF, "nc=%d nr=%d group=%d codebookinfo=%d coefficientsize=%d\n",
		nc_index, nr_index, grouping, codebookinfo,
		coefficientsize);

	param.paid = bfee->p_aid;
	param.csi_para = csi_param;
	param.my_aid = bfee->aid & 0xfff;
	param.csi_length_sel = HAL_CSI_SEG_4K;
	ether_addr_copy(param.bfer_address, bfee->mac_addr);

	rtw_bf_init_bfer_entry_mu(rtwdev, &param);

	bf_info->cur_csi_rpt_rate = DESC_RATE6M;
	rtw_bf_cfg_sounding(rtwdev, vif, DESC_RATE6M);

	/* accept action_no_ack */
	rtw_write16_set(rtwdev, REG_RXFLTMAP0, BIT_RXFLTMAP0_ACTIONNOACK);

	/* accept NDPA and BF report poll */
	rtw_write16_set(rtwdev, REG_RXFLTMAP1, BIT_RXFLTMAP1_BF);
}
EXPORT_SYMBOL(rtw_bf_enable_bfee_mu);

void rtw_bf_remove_bfee_su(struct rtw_dev *rtwdev,
			   struct rtw_bfee *bfee)
{
	struct rtw_bf_info *bfinfo = &rtwdev->bf_info;

	rtw_dbg(rtwdev, RTW_DBG_BF, "remove as a su bfee\n");
	rtw_write8_mask(rtwdev, REG_SND_PTCL_CTRL, BIT_MASK_BEAMFORM,
			RTW_SND_CTRL_REMOVE);

	switch (bfee->su_reg_index) {
	case 0:
		rtw_write32(rtwdev, REG_ASSOCIATED_BFMER0_INFO, 0);
		rtw_write16(rtwdev, REG_ASSOCIATED_BFMER0_INFO + 4, 0);
		rtw_write16(rtwdev, REG_TX_CSI_RPT_PARAM_BW20, 0);
		break;
	case 1:
		rtw_write32(rtwdev, REG_ASSOCIATED_BFMER1_INFO, 0);
		rtw_write16(rtwdev, REG_ASSOCIATED_BFMER1_INFO + 4, 0);
		rtw_write16(rtwdev, REG_TX_CSI_RPT_PARAM_BW20 + 2, 0);
		break;
	}

	clear_bit(bfee->su_reg_index, bfinfo->bfer_su_reg_maping);
	bfee->su_reg_index = 0xFF;
}
EXPORT_SYMBOL(rtw_bf_remove_bfee_su);

void rtw_bf_remove_bfee_mu(struct rtw_dev *rtwdev,
			   struct rtw_bfee *bfee)
{
	struct rtw_bf_info *bfinfo = &rtwdev->bf_info;

	rtw_write8_mask(rtwdev, REG_SND_PTCL_CTRL, BIT_MASK_BEAMFORM,
			RTW_SND_CTRL_REMOVE);

	rtw_bf_del_bfer_entry_mu(rtwdev);

	if (bfinfo->bfer_su_cnt == 0 && bfinfo->bfer_mu_cnt == 0)
		rtw_bf_del_sounding(rtwdev);
}
EXPORT_SYMBOL(rtw_bf_remove_bfee_mu);

void rtw_bf_set_gid_table(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *conf)
{
	struct rtw_vif *rtwvif = (struct rtw_vif *)vif->drv_priv;
	struct rtw_bfee *bfee = &rtwvif->bfee;
	struct cfg_mumimo_para param;

	if (bfee->role != RTW_BFEE_MU) {
		rtw_dbg(rtwdev, RTW_DBG_BF, "this vif is not mu bfee\n");
		return;
	}

	param.grouping_bitmap = 0;
	param.mu_tx_en = 0;
	memset(param.sounding_sts, 0, 6);
	memcpy(param.given_gid_tab, conf->mu_group.membership, 8);
	memcpy(param.given_user_pos, conf->mu_group.position, 16);
	rtw_dbg(rtwdev, RTW_DBG_BF, "STA0: gid_valid=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
		param.given_gid_tab[0], param.given_user_pos[0],
		param.given_user_pos[1]);

	rtw_dbg(rtwdev, RTW_DBG_BF, "STA1: gid_valid=0x%x, user_position_l=0x%x, user_position_h=0x%x\n",
		param.given_gid_tab[1], param.given_user_pos[2],
		param.given_user_pos[3]);

	rtw_bf_cfg_mu_bfee(rtwdev, &param);
}
EXPORT_SYMBOL(rtw_bf_set_gid_table);

void rtw_bf_phy_init(struct rtw_dev *rtwdev)
{
	u8 tmp8;
	u32 tmp32;
	u8 retry_limit = 0xA;
	u8 ndpa_rate = 0x10;
	u8 ack_policy = 3;

	tmp32 = rtw_read32(rtwdev, REG_MU_TX_CTL);
	/* Enable P1 aggr new packet according to P0 transfer time */
	tmp32 |= BIT_MU_P1_WAIT_STATE_EN;
	/* MU Retry Limit */
	tmp32 &= ~BIT_MASK_R_MU_RL;
	tmp32 |= (retry_limit << BIT_SHIFT_R_MU_RL) & BIT_MASK_R_MU_RL;
	/* Disable Tx MU-MIMO until sounding done */
	tmp32 &= ~BIT_EN_MU_MIMO;
	/* Clear validity of MU STAs */
	tmp32 &= ~BIT_MASK_R_MU_TABLE_VALID;
	rtw_write32(rtwdev, REG_MU_TX_CTL, tmp32);

	/* MU-MIMO Option as default value */
	tmp8 = ack_policy << BIT_SHIFT_WMAC_TXMU_ACKPOLICY;
	tmp8 |= BIT_WMAC_TXMU_ACKPOLICY_EN;
	rtw_write8(rtwdev, REG_WMAC_MU_BF_OPTION, tmp8);

	/* MU-MIMO Control as default value */
	rtw_write16(rtwdev, REG_WMAC_MU_BF_CTL, 0);
	/* Set MU NDPA rate & BW source */
	rtw_write32_set(rtwdev, REG_TXBF_CTRL, BIT_USE_NDPA_PARAMETER);
	/* Set NDPA Rate */
	rtw_write8(rtwdev, REG_NDPA_OPT_CTRL, ndpa_rate);

	rtw_write32_mask(rtwdev, REG_BBPSF_CTRL, BIT_MASK_CSI_RATE,
			 DESC_RATE6M);
}
EXPORT_SYMBOL(rtw_bf_phy_init);

void rtw_bf_cfg_csi_rate(struct rtw_dev *rtwdev, u8 rssi, u8 cur_rate,
			 u8 fixrate_en, u8 *new_rate)
{
	u32 csi_cfg;
	u16 cur_rrsr;

	csi_cfg = rtw_read32(rtwdev, REG_BBPSF_CTRL) & ~BIT_MASK_CSI_RATE;
	cur_rrsr = rtw_read16(rtwdev, REG_RRSR);

	if (rssi >= 40) {
		if (cur_rate != DESC_RATE54M) {
			cur_rrsr |= BIT(DESC_RATE54M);
			csi_cfg |= (DESC_RATE54M & BIT_MASK_CSI_RATE_VAL) <<
				   BIT_SHIFT_CSI_RATE;
			rtw_write16(rtwdev, REG_RRSR, cur_rrsr);
			rtw_write32(rtwdev, REG_BBPSF_CTRL, csi_cfg);
		}
		*new_rate = DESC_RATE54M;
	} else {
		if (cur_rate != DESC_RATE24M) {
			cur_rrsr &= ~BIT(DESC_RATE54M);
			csi_cfg |= (DESC_RATE54M & BIT_MASK_CSI_RATE_VAL) <<
				   BIT_SHIFT_CSI_RATE;
			rtw_write16(rtwdev, REG_RRSR, cur_rrsr);
			rtw_write32(rtwdev, REG_BBPSF_CTRL, csi_cfg);
		}
		*new_rate = DESC_RATE24M;
	}
}
EXPORT_SYMBOL(rtw_bf_cfg_csi_rate);
