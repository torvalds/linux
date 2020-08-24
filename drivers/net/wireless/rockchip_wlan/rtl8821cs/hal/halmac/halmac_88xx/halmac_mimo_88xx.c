/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "halmac_mimo_88xx.h"
#include "halmac_88xx_cfg.h"
#include "halmac_common_88xx.h"
#include "halmac_init_88xx.h"

#if HALMAC_88XX_SUPPORT

#define TXBF_CTRL_CFG	(BIT_R_ENABLE_NDPA | BIT_USE_NDPA_PARAMETER | \
			 BIT_R_EN_NDPA_INT | BIT_DIS_NDP_BFEN)
#define CSI_RATE_MAP	0x55

static void
cfg_mu_bfee_88xx(struct halmac_adapter *adapter,
		 struct halmac_cfg_mumimo_para *param);

static void
cfg_mu_bfer_88xx(struct halmac_adapter *adapter,
		 struct halmac_cfg_mumimo_para *param);

static enum halmac_cmd_construct_state
fw_snding_cmd_cnstr_state_88xx(struct halmac_adapter *adapter);

static enum halmac_ret_status
cnv_fw_snding_state_88xx(struct halmac_adapter *adapter,
			 enum halmac_cmd_construct_state dest_state);

static u8
snding_pkt_chk_88xx(struct halmac_adapter *adapter, u8 *pkt);

/**
 * cfg_txbf_88xx() - enable/disable specific user's txbf
 * @adapter : the adapter of halmac
 * @userid : su bfee userid = 0 or 1 to apply TXBF
 * @bw : the sounding bandwidth
 * @txbf_en : 0: disable TXBF, 1: enable TXBF
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_txbf_88xx(struct halmac_adapter *adapter, u8 userid, enum halmac_bw bw,
	      u8 txbf_en)
{
	u16 tmp42c = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (txbf_en) {
		switch (bw) {
		case HALMAC_BW_80:
			tmp42c |= BIT_R_TXBF0_80M;
			/* fall through */
		case HALMAC_BW_40:
			tmp42c |= BIT_R_TXBF0_40M;
			/* fall through */
		case HALMAC_BW_20:
			tmp42c |= BIT_R_TXBF0_20M;
			break;
		default:
			return HALMAC_RET_INVALID_SOUNDING_SETTING;
		}
	}

	switch (userid) {
	case 0:
		tmp42c |= HALMAC_REG_R16(REG_TXBF_CTRL) &
			~(BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_W16(REG_TXBF_CTRL, tmp42c);
		break;
	case 1:
		tmp42c |= HALMAC_REG_R16(REG_TXBF_CTRL + 2) &
			~(BIT_R_TXBF0_20M | BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_W16(REG_TXBF_CTRL + 2, tmp42c);
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_mumimo_88xx() -config mumimo
 * @adapter : the adapter of halmac
 * @param : parameters to configure MU PPDU Tx/Rx
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_mumimo_88xx(struct halmac_adapter *adapter,
		struct halmac_cfg_mumimo_para *param)
{
	if (param->role == HAL_BFEE)
		cfg_mu_bfee_88xx(adapter, param);
	else
		cfg_mu_bfer_88xx(adapter, param);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static void
cfg_mu_bfee_88xx(struct halmac_adapter *adapter,
		 struct halmac_cfg_mumimo_para *param)
{
	u8 mu_tbl_sel;
	u8 tmp14c0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	tmp14c0 = HALMAC_REG_R8(REG_MU_TX_CTL) & ~BIT_MASK_R_MU_TABLE_VALID;
	HALMAC_REG_W8(REG_MU_TX_CTL, (tmp14c0 | BIT(0) | BIT(1)) & ~(BIT(7)));

	/*config GID valid table and user position table*/
	mu_tbl_sel = HALMAC_REG_R8(REG_MU_TX_CTL + 1) & 0xF8;

	HALMAC_REG_W8(REG_MU_TX_CTL + 1, mu_tbl_sel);
	HALMAC_REG_W32(REG_MU_STA_GID_VLD, param->given_gid_tab[0]);
	HALMAC_REG_W32(REG_MU_STA_USER_POS_INFO, param->given_user_pos[0]);
	HALMAC_REG_W32(REG_MU_STA_USER_POS_INFO + 4, param->given_user_pos[1]);

	HALMAC_REG_W8(REG_MU_TX_CTL + 1, mu_tbl_sel | 1);
	HALMAC_REG_W32(REG_MU_STA_GID_VLD, param->given_gid_tab[1]);
	HALMAC_REG_W32(REG_MU_STA_USER_POS_INFO, param->given_user_pos[2]);
	HALMAC_REG_W32(REG_MU_STA_USER_POS_INFO + 4, param->given_user_pos[3]);
}

static void
cfg_mu_bfer_88xx(struct halmac_adapter *adapter,
		 struct halmac_cfg_mumimo_para *param)
{
	u8 i;
	u8 idx;
	u8 id0;
	u8 id1;
	u8 gid;
	u8 mu_tbl_sel;
	u8 mu_tbl_valid = 0;
	u32 gid_valid[6] = {0};
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (param->mu_tx_en == 0) {
		HALMAC_REG_W8(REG_MU_TX_CTL,
			      HALMAC_REG_R8(REG_MU_TX_CTL) & ~(BIT(7)));
		return;
	}

	for (idx = 0; idx < 15; idx++) {
		if (idx < 5) {
			/*grouping_bitmap bit0~4, MU_STA0 with MUSTA1~5*/
			id0 = 0;
			id1 = (u8)(idx + 1);
		} else if (idx < 9) {
			/*grouping_bitmap bit5~8, MU_STA1 with MUSTA2~5*/
			id0 = 1;
			id1 = (u8)(idx - 3);
		} else if (idx < 12) {
			/*grouping_bitmap bit9~11, MU_STA2 with MUSTA3~5*/
			id0 = 2;
			id1 = (u8)(idx - 6);
		} else if (idx < 14) {
			/*grouping_bitmap bit12~13, MU_STA3 with MUSTA4~5*/
			id0 = 3;
			id1 = (u8)(idx - 8);
		} else {
			/*grouping_bitmap bit14, MU_STA4 with MUSTA5*/
			id0 = 4;
			id1 = (u8)(idx - 9);
		}
		if (param->grouping_bitmap & BIT(idx)) {
			/*Pair 1*/
			gid = (idx << 1) + 1;
			gid_valid[id0] |= (BIT(gid));
			gid_valid[id1] |= (BIT(gid));
			/*Pair 2*/
			gid += 1;
			gid_valid[id0] |= (BIT(gid));
			gid_valid[id1] |= (BIT(gid));
		} else {
			/*Pair 1*/
			gid = (idx << 1) + 1;
			gid_valid[id0] &= ~(BIT(gid));
			gid_valid[id1] &= ~(BIT(gid));
			/*Pair 2*/
			gid += 1;
			gid_valid[id0] &= ~(BIT(gid));
			gid_valid[id1] &= ~(BIT(gid));
		}
	}

	/*set MU STA GID valid TABLE*/
	mu_tbl_sel = HALMAC_REG_R8(REG_MU_TX_CTL + 1) & 0xF8;
	for (idx = 0; idx < 6; idx++) {
		HALMAC_REG_W8(REG_MU_TX_CTL + 1, idx | mu_tbl_sel);
		HALMAC_REG_W32(REG_MU_STA_GID_VLD, gid_valid[idx]);
	}

	/*To validate the sounding successful MU STA and enable MU TX*/
	for (i = 0; i < 6; i++) {
		if (param->sounding_sts[i] == 1)
			mu_tbl_valid |= BIT(i);
	}
	HALMAC_REG_W8(REG_MU_TX_CTL, mu_tbl_valid | BIT(7));
}

/**
 * cfg_sounding_88xx() - configure general sounding
 * @adapter : the adapter of halmac
 * @role : driver's role, BFer or BFee
 * @rate : set ndpa tx rate if driver is BFer,
 * or set csi response rate if driver is BFee
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_sounding_88xx(struct halmac_adapter *adapter, enum halmac_snd_role role,
		  enum halmac_data_rate rate)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u32 tmp6dc = 0;
	u8 csi_rsc = 0x0;

	/*use ndpa rx rate to decide csi rate*/
	tmp6dc = HALMAC_REG_R32(REG_BBPSF_CTRL) | BIT_WMAC_USE_NDPARATE
							| (csi_rsc << 13);

	switch (role) {
	case HAL_BFER:
		HALMAC_REG_W32_SET(REG_TXBF_CTRL, TXBF_CTRL_CFG);
		HALMAC_REG_W8(REG_NDPA_RATE, rate);
		HALMAC_REG_W8(REG_SND_PTCL_CTRL + 1, 0x2 | BIT(7));
		HALMAC_REG_W8(REG_SND_PTCL_CTRL + 2, 0x2);
		break;
	case HAL_BFEE:
		HALMAC_REG_W8(REG_SND_PTCL_CTRL, 0xDB);
		HALMAC_REG_W8(REG_SND_PTCL_CTRL + 3, 0x3A);
		HALMAC_REG_W8_CLR(REG_RXFLTMAP1, BIT(4));
		HALMAC_REG_W8_CLR(REG_RXFLTMAP4, BIT(4));
		#if (HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)
		if (adapter->chip_id == HALMAC_CHIP_ID_8822C)
			HALMAC_REG_W32(REG_CSI_RRSR,
				       BIT_CSI_RRSC_BITMAP(CSI_RATE_MAP) |
				       BIT_OFDM_LEN_TH(0));
		else if (adapter->chip_id == HALMAC_CHIP_ID_8812F)
			HALMAC_REG_W32(REG_CSI_RRSR,
				       BIT_CSI_RRSC_BITMAP(CSI_RATE_MAP) |
				       BIT_OFDM_LEN_TH(3));
		#endif
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	/*AP mode set tx gid to 63*/
	/*STA mode set tx gid to 0*/
	if (BIT_GET_NETYPE0(HALMAC_REG_R32(REG_CR)) == 0x3)
		HALMAC_REG_W32(REG_BBPSF_CTRL, tmp6dc | BIT(12));
	else
		HALMAC_REG_W32(REG_BBPSF_CTRL, tmp6dc & ~(BIT(12)));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * del_sounding_88xx() - reset general sounding
 * @adapter : the adapter of halmac
 * @role : driver's role, BFer or BFee
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
del_sounding_88xx(struct halmac_adapter *adapter, enum halmac_snd_role role)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	switch (role) {
	case HAL_BFER:
		HALMAC_REG_W8(REG_TXBF_CTRL + 3, 0);
		break;
	case HAL_BFEE:
		HALMAC_REG_W8(REG_SND_PTCL_CTRL, 0);
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * su_bfee_entry_init_88xx() - config SU beamformee's registers
 * @adapter : the adapter of halmac
 * @userid : SU bfee userid = 0 or 1 to be added
 * @paid : partial AID of this bfee
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
su_bfee_entry_init_88xx(struct halmac_adapter *adapter, u8 userid, u16 paid)
{
	u16 tmp42c = 0;
	u16 tmp168x = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	switch (userid) {
	case 0:
		tmp42c = HALMAC_REG_R16(REG_TXBF_CTRL) &
				~(BIT_MASK_R_TXBF0_AID | BIT_R_TXBF0_20M |
				BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_W16(REG_TXBF_CTRL, tmp42c | paid);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMEE_SEL, paid);
		#if HALMAC_8822C_SUPPORT
		if (adapter->chip_id == HALMAC_CHIP_ID_8822C)
			HALMAC_REG_W16(REG_ASSOCIATED_BFMEE_SEL, paid | BIT(9));
		#endif
		break;
	case 1:
		tmp42c = HALMAC_REG_R16(REG_TXBF_CTRL + 2) &
				~(BIT_MASK_R_TXBF1_AID | BIT_R_TXBF0_20M |
				BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_W16(REG_TXBF_CTRL + 2, tmp42c | paid);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMEE_SEL + 2, paid | BIT(9));
		break;
	case 2:
		tmp168x = HALMAC_REG_R16(REG_WMAC_ASSOCIATED_MU_BFMEE2);
		tmp168x = BIT_CLEAR_WMAC_MU_BFEE2_AID(tmp168x);
		tmp168x |= (paid | BIT(9));
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE2, tmp168x);
		break;
	case 3:
		tmp168x = HALMAC_REG_R16(REG_WMAC_ASSOCIATED_MU_BFMEE3);
		tmp168x = BIT_CLEAR_WMAC_MU_BFEE3_AID(tmp168x);
		tmp168x |= (paid | BIT(9));
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE3, tmp168x);
		break;
	case 4:
		tmp168x = HALMAC_REG_R16(REG_WMAC_ASSOCIATED_MU_BFMEE4);
		tmp168x = BIT_CLEAR_WMAC_MU_BFEE4_AID(tmp168x);
		tmp168x |= (paid | BIT(9));
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE4, tmp168x);
		break;
	case 5:
		tmp168x = HALMAC_REG_R16(REG_WMAC_ASSOCIATED_MU_BFMEE5);
		tmp168x = BIT_CLEAR_WMAC_MU_BFEE5_AID(tmp168x);
		tmp168x |= (paid | BIT(9));
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE5, tmp168x);
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * su_bfee_entry_init_88xx() - config SU beamformer's registers
 * @adapter : the adapter of halmac
 * @param : parameters to configure SU BFER entry
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
su_bfer_entry_init_88xx(struct halmac_adapter *adapter,
			struct halmac_su_bfer_init_para *param)
{
	u16 mac_addr_h;
	u32 mac_addr_l;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	mac_addr_l = rtk_le32_to_cpu(param->bfer_address.addr_l_h.low);
	mac_addr_h = rtk_le16_to_cpu(param->bfer_address.addr_l_h.high);

	switch (param->userid) {
	case 0:
		HALMAC_REG_W32(REG_ASSOCIATED_BFMER0_INFO, mac_addr_l);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMER0_INFO + 4, mac_addr_h);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMER0_INFO + 6, param->paid);
		HALMAC_REG_W16(REG_TX_CSI_RPT_PARAM_BW20, param->csi_para);
		break;
	case 1:
		HALMAC_REG_W32(REG_ASSOCIATED_BFMER1_INFO, mac_addr_l);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMER1_INFO + 4, mac_addr_h);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMER1_INFO + 6, param->paid);
		HALMAC_REG_W16(REG_TX_CSI_RPT_PARAM_BW20 + 2, param->csi_para);
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mu_bfee_entry_init_88xx() - config MU beamformee's registers
 * @adapter : the adapter of halmac
 * @param : parameters to configure MU BFEE entry
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mu_bfee_entry_init_88xx(struct halmac_adapter *adapter,
			struct halmac_mu_bfee_init_para *param)
{
	u16 tmp168x = 0;
	u16 tmp14c0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	tmp168x |= param->paid | BIT(9);
	HALMAC_REG_W16((0x1680 + param->userid * 2), tmp168x);

	tmp14c0 = HALMAC_REG_R16(REG_MU_TX_CTL) & ~(BIT(8) | BIT(9) | BIT(10));
	HALMAC_REG_W16(REG_MU_TX_CTL, tmp14c0 | ((param->userid - 2) << 8));
	HALMAC_REG_W32(REG_MU_STA_GID_VLD, 0);
	HALMAC_REG_W32(REG_MU_STA_USER_POS_INFO, param->user_position_l);
	HALMAC_REG_W32(REG_MU_STA_USER_POS_INFO + 4, param->user_position_h);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mu_bfer_entry_init_88xx() - config MU beamformer's registers
 * @adapter : the adapter of halmac
 * @param : parameters to configure MU BFER entry
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mu_bfer_entry_init_88xx(struct halmac_adapter *adapter,
			struct halmac_mu_bfer_init_para *param)
{
	u16 tmp1680 = 0;
	u16 mac_addr_h;
	u32 mac_addr_l;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	mac_addr_l = rtk_le32_to_cpu(param->bfer_address.addr_l_h.low);
	mac_addr_h = rtk_le16_to_cpu(param->bfer_address.addr_l_h.high);

	HALMAC_REG_W32(REG_ASSOCIATED_BFMER0_INFO, mac_addr_l);
	HALMAC_REG_W16(REG_ASSOCIATED_BFMER0_INFO + 4, mac_addr_h);
	HALMAC_REG_W16(REG_ASSOCIATED_BFMER0_INFO + 6, param->paid);
	HALMAC_REG_W16(REG_TX_CSI_RPT_PARAM_BW20, param->csi_para);

	tmp1680 = HALMAC_REG_R16(0x1680) & 0xC000;
	tmp1680 |= param->my_aid | (param->csi_length_sel << 12);
	HALMAC_REG_W16(0x1680, tmp1680);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * su_bfee_entry_del_88xx() - reset SU beamformee's registers
 * @adapter : the adapter of halmac
 * @userid : the SU BFee userid to be deleted
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
su_bfee_entry_del_88xx(struct halmac_adapter *adapter, u8 userid)
{
	u16 value16;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	switch (userid) {
	case 0:
		value16 = HALMAC_REG_R16(REG_TXBF_CTRL);
		value16 &= ~(BIT_MASK_R_TXBF0_AID | BIT_R_TXBF0_20M |
					BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_W16(REG_TXBF_CTRL, value16);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMEE_SEL, 0);
		break;
	case 1:
		value16 = HALMAC_REG_R16(REG_TXBF_CTRL + 2);
		value16 &= ~(BIT_MASK_R_TXBF1_AID | BIT_R_TXBF0_20M |
					BIT_R_TXBF0_40M | BIT_R_TXBF0_80M);
		HALMAC_REG_W16(REG_TXBF_CTRL + 2, value16);
		HALMAC_REG_W16(REG_ASSOCIATED_BFMEE_SEL + 2, 0);
		break;
	case 2:
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE2, 0);
		break;
	case 3:
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE3, 0);
		break;
	case 4:
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE4, 0);
		break;
	case 5:
		HALMAC_REG_W16(REG_WMAC_ASSOCIATED_MU_BFMEE5, 0);
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * su_bfee_entry_del_88xx() - reset SU beamformer's registers
 * @adapter : the adapter of halmac
 * @userid : the SU BFer userid to be deleted
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
su_bfer_entry_del_88xx(struct halmac_adapter *adapter, u8 userid)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	switch (userid) {
	case 0:
		HALMAC_REG_W32(REG_ASSOCIATED_BFMER0_INFO, 0);
		HALMAC_REG_W32(REG_ASSOCIATED_BFMER0_INFO + 4, 0);
		break;
	case 1:
		HALMAC_REG_W32(REG_ASSOCIATED_BFMER1_INFO, 0);
		HALMAC_REG_W32(REG_ASSOCIATED_BFMER1_INFO + 4, 0);
		break;
	default:
		return HALMAC_RET_INVALID_SOUNDING_SETTING;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mu_bfee_entry_del_88xx() - reset MU beamformee's registers
 * @adapter : the adapter of halmac
 * @userid : the MU STA userid to be deleted
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mu_bfee_entry_del_88xx(struct halmac_adapter *adapter, u8 userid)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	HALMAC_REG_W16(0x1680 + userid * 2, 0);
	HALMAC_REG_W8_CLR(REG_MU_TX_CTL, BIT(userid - 2));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mu_bfer_entry_del_88xx() -reset MU beamformer's registers
 * @adapter : the adapter of halmac
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mu_bfer_entry_del_88xx(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	HALMAC_REG_W32(REG_ASSOCIATED_BFMER0_INFO, 0);
	HALMAC_REG_W32(REG_ASSOCIATED_BFMER0_INFO + 4, 0);
	HALMAC_REG_W16(0x1680, 0);
	HALMAC_REG_W8(REG_MU_TX_CTL, 0);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_csi_rate_88xx() - config CSI frame Tx rate
 * @adapter : the adapter of halmac
 * @rssi : rssi in decimal value
 * @cur_rate : current CSI frame rate
 * @fixrate_en : enable to fix CSI frame in VHT rate, otherwise legacy OFDM rate
 * @new_rate : API returns the final CSI frame rate
 * Author : chunchu
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_csi_rate_88xx(struct halmac_adapter *adapter, u8 rssi, u8 cur_rate,
		  u8 fixrate_en, u8 *new_rate, u8 *bmp_ofdm54)
{
	u32 csi_cfg;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	*bmp_ofdm54 = 0xFF;

#if HALMAC_8821C_SUPPORT
	if (adapter->chip_id == HALMAC_CHIP_ID_8821C && fixrate_en) {
		csi_cfg = HALMAC_REG_R32(REG_BBPSF_CTRL) & ~BITS_WMAC_CSI_RATE;
		HALMAC_REG_W32(REG_BBPSF_CTRL,
			       csi_cfg | BIT_CSI_FORCE_RATE_EN |
			       BIT_CSI_RSC(1) |
			       BIT_WMAC_CSI_RATE(HALMAC_VHT_NSS1_MCS3));
		*new_rate = HALMAC_VHT_NSS1_MCS3;
		return HALMAC_RET_SUCCESS;
	}
	csi_cfg = HALMAC_REG_R32(REG_BBPSF_CTRL) & ~BITS_WMAC_CSI_RATE &
							~BIT_CSI_FORCE_RATE_EN;
#else
	csi_cfg = HALMAC_REG_R32(REG_BBPSF_CTRL) & ~BITS_WMAC_CSI_RATE;
#endif

#if (HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)
	if (adapter->chip_id == HALMAC_CHIP_ID_8822C ||
	    adapter->chip_id == HALMAC_CHIP_ID_8812F)
		HALMAC_REG_W32_SET(REG_BBPSF_CTRL, BIT(15));
#endif

	if (rssi >= 40) {
		if (cur_rate != HALMAC_OFDM54) {
			csi_cfg |= BIT_WMAC_CSI_RATE(HALMAC_OFDM54);
			HALMAC_REG_W32(REG_BBPSF_CTRL, csi_cfg);
			*bmp_ofdm54 = 1;
		}
		*new_rate = HALMAC_OFDM54;
	} else {
		if (cur_rate != HALMAC_OFDM24) {
			csi_cfg |= BIT_WMAC_CSI_RATE(HALMAC_OFDM24);
			HALMAC_REG_W32(REG_BBPSF_CTRL, csi_cfg);
			*bmp_ofdm54 = 0;
		}
		*new_rate = HALMAC_OFDM24;
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * fw_snding_88xx() - fw sounding control
 * @adapter : the adapter of halmac
 * @su_info :
 *	su0_en : enable/disable fw sounding
 *	su0_ndpa_pkt : ndpa pkt, shall include txdesc
 *	su0_pkt_sz : ndpa pkt size, shall include txdesc
 * @mu_info : currently not in use, input NULL is acceptable
 * @period : sounding period, unit is 5ms
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
fw_snding_88xx(struct halmac_adapter *adapter,
	       struct halmac_su_snding_info *su_info,
	       struct halmac_mu_snding_info *mu_info, u8 period)
{
	u8 h2c_buf[H2C_PKT_SIZE_88XX] = { 0 };
	u16 seq_num;
	u16 snding_info_addr;
	struct halmac_h2c_header_info hdr_info;
	enum halmac_cmd_process_status *proc_status;
	enum halmac_ret_status status;

	proc_status = &adapter->halmac_state.fw_snding_state.proc_status;

	if (adapter->chip_id == HALMAC_CHIP_ID_8821C)
		return HALMAC_RET_NOT_SUPPORT;

	if (halmac_fw_validate(adapter) != HALMAC_RET_SUCCESS)
		return HALMAC_RET_NO_DLFW;

	if (adapter->fw_ver.h2c_version < 9)
		return HALMAC_RET_FW_NO_SUPPORT;

	if (*proc_status == HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_TRACE("[TRACE]Wait event(snd)\n");
		return HALMAC_RET_BUSY_STATE;
	}

	if (su_info->su0_en == 1) {
		if (!su_info->su0_ndpa_pkt)
			return HALMAC_RET_NULL_POINTER;

		if (su_info->su0_pkt_sz > (u32)SU0_SNDING_PKT_RSVDPG_SIZE -
		    adapter->hw_cfg_info.txdesc_size)
			return HALMAC_RET_DATA_SIZE_INCORRECT;

		if (!snding_pkt_chk_88xx(adapter, su_info->su0_ndpa_pkt))
			return HALMAC_RET_TXDESC_SET_FAIL;

		if (fw_snding_cmd_cnstr_state_88xx(adapter) !=
		    HALMAC_CMD_CNSTR_IDLE) {
			PLTFM_MSG_ERR("[ERR]Not idle(snd)\n");
			return HALMAC_RET_ERROR_STATE;
		}

		snding_info_addr = adapter->txff_alloc.rsvd_h2c_sta_info_addr +
				   SU0_SNDING_PKT_OFFSET;
		status = dl_rsvd_page_88xx(adapter, snding_info_addr,
					   su_info->su0_ndpa_pkt,
					   su_info->su0_pkt_sz);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]dl rsvd page\n");
			return status;
		}

		FW_SNDING_SET_SU0(h2c_buf, 1);
		FW_SNDING_SET_PERIOD(h2c_buf, period);
		FW_SNDING_SET_NDPA0_HEAD_PG(h2c_buf, snding_info_addr -
					    adapter->txff_alloc.rsvd_boundary);
	} else {
		if (fw_snding_cmd_cnstr_state_88xx(adapter) !=
		    HALMAC_CMD_CNSTR_BUSY) {
			PLTFM_MSG_ERR("[ERR]Not snd(snd)\n");
			return HALMAC_RET_ERROR_STATE;
		}
		FW_SNDING_SET_SU0(h2c_buf, 0);
	}

	*proc_status = HALMAC_CMD_PROCESS_SENDING;

	hdr_info.sub_cmd_id = SUB_CMD_ID_FW_SNDING;
	hdr_info.content_size = 8;
	hdr_info.ack = 1;
	set_h2c_pkt_hdr_88xx(adapter, h2c_buf, &hdr_info, &seq_num);
	adapter->halmac_state.fw_snding_state.seq_num = seq_num;

	status = send_h2c_pkt_88xx(adapter, h2c_buf);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]send h2c\n");
		reset_ofld_feature_88xx(adapter, HALMAC_FEATURE_FW_SNDING);
		return status;
	}

	if (cnv_fw_snding_state_88xx(adapter, su_info->su0_en == 1 ?
				     HALMAC_CMD_CNSTR_BUSY :
				     HALMAC_CMD_CNSTR_IDLE)
				     != HALMAC_RET_SUCCESS)
		return HALMAC_RET_ERROR_STATE;

	return HALMAC_RET_SUCCESS;
}

static u8
snding_pkt_chk_88xx(struct halmac_adapter *adapter, u8 *pkt)
{
	u8 data_rate;

	if (GET_TX_DESC_NDPA(pkt) == 0) {
		PLTFM_MSG_ERR("[ERR]txdesc ndpa = 0\n");
		return 0;
	}

	data_rate = (u8)GET_TX_DESC_DATARATE(pkt);
	if (!(data_rate >= HALMAC_VHT_NSS2_MCS0 &&
	      data_rate <= HALMAC_VHT_NSS2_MCS9)) {
		if (!(data_rate >= HALMAC_MCS8 && data_rate <= HALMAC_MCS15)) {
			PLTFM_MSG_ERR("[ERR]txdesc rate\n");
			return 0;
		}
	}

	if (GET_TX_DESC_NAVUSEHDR(pkt) == 0) {
		PLTFM_MSG_ERR("[ERR]txdesc navusehdr = 0\n");
		return 0;
	}

	if (GET_TX_DESC_USE_RATE(pkt) == 0) {
		PLTFM_MSG_ERR("[ERR]txdesc userate = 0\n");
		return 0;
	}

	return 1;
}

static enum halmac_cmd_construct_state
fw_snding_cmd_cnstr_state_88xx(struct halmac_adapter *adapter)
{
	return adapter->halmac_state.fw_snding_state.cmd_cnstr_state;
}

enum halmac_ret_status
get_h2c_ack_fw_snding_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u8 seq_num = 0;
	u8 fw_rc;
	struct halmac_fw_snding_state *state;
	enum halmac_cmd_process_status proc_status;

	state = &adapter->halmac_state.fw_snding_state;

	seq_num = (u8)H2C_ACK_HDR_GET_H2C_SEQ(buf);
	PLTFM_MSG_TRACE("[TRACE]Seq num:h2c->%d c2h->%d\n",
			state->seq_num, seq_num);
	if (seq_num != state->seq_num) {
		PLTFM_MSG_ERR("[ERR]Seq num mismatch:h2c->%d c2h->%d\n",
			      state->seq_num, seq_num);
		return HALMAC_RET_SUCCESS;
	}

	if (state->proc_status != HALMAC_CMD_PROCESS_SENDING) {
		PLTFM_MSG_ERR("[ERR]not sending(snd)\n");
		return HALMAC_RET_SUCCESS;
	}

	fw_rc = (u8)H2C_ACK_HDR_GET_H2C_RETURN_CODE(buf);
	state->fw_rc = fw_rc;

	if ((enum halmac_h2c_return_code)fw_rc == HALMAC_H2C_RETURN_SUCCESS) {
		proc_status = HALMAC_CMD_PROCESS_DONE;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_FW_SNDING, proc_status,
				NULL, 0);
	} else {
		proc_status = HALMAC_CMD_PROCESS_ERROR;
		state->proc_status = proc_status;
		PLTFM_EVENT_SIG(HALMAC_FEATURE_FW_SNDING, proc_status,
				&fw_rc, 1);
	}

	return HALMAC_RET_SUCCESS;
}

enum halmac_ret_status
get_fw_snding_status_88xx(struct halmac_adapter *adapter,
			  enum halmac_cmd_process_status *proc_status)
{
	*proc_status = adapter->halmac_state.fw_snding_state.proc_status;

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
cnv_fw_snding_state_88xx(struct halmac_adapter *adapter,
			 enum halmac_cmd_construct_state dest_state)
{
	struct halmac_fw_snding_state *state;

	state = &adapter->halmac_state.fw_snding_state;

	if (state->cmd_cnstr_state != HALMAC_CMD_CNSTR_IDLE &&
	    state->cmd_cnstr_state != HALMAC_CMD_CNSTR_BUSY)
		return HALMAC_RET_ERROR_STATE;

	if (dest_state == HALMAC_CMD_CNSTR_IDLE) {
		if (state->cmd_cnstr_state == HALMAC_CMD_CNSTR_IDLE)
			return HALMAC_RET_ERROR_STATE;
	} else if (dest_state == HALMAC_CMD_CNSTR_BUSY) {
		if (state->cmd_cnstr_state == HALMAC_CMD_CNSTR_BUSY)
			return HALMAC_RET_ERROR_STATE;
	}

	state->cmd_cnstr_state = dest_state;

	return HALMAC_RET_SUCCESS;
}
#endif /* HALMAC_88XX_SUPPORT */
