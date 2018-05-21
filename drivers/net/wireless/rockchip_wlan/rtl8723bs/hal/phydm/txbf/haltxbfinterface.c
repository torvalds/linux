/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
/* ************************************************************
 * Description:
 *
 * This file is for TXBF interface mechanism
 *
 * ************************************************************ */
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
beamforming_gid_paid(
	struct _ADAPTER	*adapter,
	PRT_TCB		p_tcb
)
{
	u8		RA[6] = {0};
	u8		*p_header = GET_FRAME_OF_FIRST_FRAG(adapter, p_tcb);
	HAL_DATA_TYPE			*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT				*p_dm = &p_hal_data->DM_OutSrc;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);

	if (adapter->HardwareType < HARDWARE_TYPE_RTL8192EE)
		return;
	else if (IS_WIRELESS_MODE_N(adapter) == false)
		return;

#if (SUPPORT_MU_BF == 1)
	if (p_tcb->tx_bf_pkt_type == RT_BF_PKT_TYPE_BROADCAST_NDPA) { /* MU NDPA */
#else
	if (0) {
#endif
		/* Fill G_ID and P_AID */
		p_tcb->G_ID = 63;
		if (p_beam_info->first_mu_bfee_index < BEAMFORMEE_ENTRY_NUM) {
			p_tcb->P_AID = p_beam_info->beamformee_entry[p_beam_info->first_mu_bfee_index].p_aid;
			RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, G_ID=0x%X, P_AID=0x%X\n", __func__, p_tcb->G_ID, p_tcb->P_AID));
		}
	} else {
		GET_80211_HDR_ADDRESS1(p_header, &RA);

		/* VHT SU PPDU carrying one or more group addressed MPDUs or */
		/* Transmitting a VHT NDP intended for multiple recipients */
		if (MacAddr_isBcst(RA) || MacAddr_isMulticast(RA)	|| p_tcb->macId == MAC_ID_STATIC_FOR_BROADCAST_MULTICAST) {
			p_tcb->G_ID = 63;
			p_tcb->P_AID = 0;
		} else if (ACTING_AS_AP(adapter)) {
			u16	AID = (u16)(MacIdGetOwnerAssociatedClientAID(adapter, p_tcb->macId) & 0x1ff);		/*AID[0:8]*/

			/*RT_DISP(FBEAM, FBEAM_FUN, ("@%s  p_tcb->mac_id=0x%X, AID=0x%X\n", __func__, p_tcb->mac_id, AID));*/
			p_tcb->G_ID = 63;

			if (AID == 0)		/*A PPDU sent by an AP to a non associated STA*/
				p_tcb->P_AID = 0;
			else {				/*Sent by an AP and addressed to a STA associated with that AP*/
				u16	BSSID = 0;
				GET_80211_HDR_ADDRESS2(p_header, &RA);
				BSSID = ((RA[5] & 0xf0) >> 4) ^ (RA[5] & 0xf);	/*BSSID[44:47] xor BSSID[40:43]*/
				p_tcb->P_AID = (AID + BSSID * 32) & 0x1ff;		/*(dec(A) + dec(B)*32) mod 512*/
			}
		} else if (ACTING_AS_IBSS(adapter)) {
			p_tcb->G_ID = 63;
			/*P_AID for infrasturcture mode; MACID for ad-hoc mode. */
			p_tcb->P_AID = p_tcb->macId;
		} else if (MgntLinkStatusQuery(adapter)) {				/*Addressed to AP*/
			p_tcb->G_ID = 0;
			GET_80211_HDR_ADDRESS1(p_header, &RA);
			p_tcb->P_AID =  RA[5];							/*RA[39:47]*/
			p_tcb->P_AID = (p_tcb->P_AID << 1) | (RA[4] >> 7);
		} else {
			p_tcb->G_ID = 63;
			p_tcb->P_AID = 0;
		}
		/*RT_DISP(FBEAM, FBEAM_FUN, ("[David]@%s End, G_ID=0x%X, P_AID=0x%X\n", __func__, p_tcb->G_ID, p_tcb->P_AID));*/
	}
}


enum rt_status
beamforming_get_report_frame(
	struct _ADAPTER		*adapter,
	PRT_RFD			p_rfd,
	POCTET_STRING	p_pdu_os
)
{
	HAL_DATA_TYPE				*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT					*p_dm = &p_hal_data->DM_OutSrc;
	struct _RT_BEAMFORMEE_ENTRY		*p_beamform_entry = NULL;
	u8						*p_mimo_ctrl_field, p_csi_matrix;
	u8						idx, nc, nr, CH_W;
	u16						csi_matrix_len = 0;

	ACT_PKT_TYPE				pkt_type = ACT_PKT_TYPE_UNKNOWN;

	/* Memory comparison to see if CSI report is the same with previous one */
	p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, Frame_Addr2(*p_pdu_os), &idx);

	if (p_beamform_entry == NULL) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("beamforming_get_report_frame: Cannot find entry by addr\n"));
		return RT_STATUS_FAILURE;
	}

	pkt_type = PacketGetActionFrameType(p_pdu_os);

	/* -@ Modified by David */
	if (pkt_type == ACT_PKT_VHT_COMPRESSED_BEAMFORMING) {
		p_mimo_ctrl_field = p_pdu_os->Octet + 26;
		nc = ((*p_mimo_ctrl_field) & 0x7) + 1;
		nr = (((*p_mimo_ctrl_field) & 0x38) >> 3) + 1;
		CH_W = (((*p_mimo_ctrl_field) & 0xC0) >> 6);
		/*p_csi_matrix = p_mimo_ctrl_field + 3 + nc;*/ /* 24+(1+1+3)+2  MAC header+(Category+ActionCode+MIMOControlField) +SNR(nc=2) */
		csi_matrix_len = p_pdu_os->Length  - 26 - 3 - nc;
	} else if (pkt_type == ACT_PKT_HT_COMPRESSED_BEAMFORMING) {
		p_mimo_ctrl_field = p_pdu_os->Octet + 26;
		nc = ((*p_mimo_ctrl_field) & 0x3) + 1;
		nr = (((*p_mimo_ctrl_field) & 0xC) >> 2) + 1;
		CH_W = (((*p_mimo_ctrl_field) & 0x10) >> 4);
		/*p_csi_matrix = p_mimo_ctrl_field + 6 + nr;*/	/* 24+(1+1+6)+2  MAC header+(Category+ActionCode+MIMOControlField) +SNR(nc=2) */
		csi_matrix_len = p_pdu_os->Length  - 26 - 6 - nr;
	} else
		return RT_STATUS_SUCCESS;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] idx=%d, pkt type=%d, nc=%d, nr=%d, CH_W=%d\n", __func__, idx, pkt_type, nc, nr, CH_W));

	return RT_STATUS_SUCCESS;
}


void
construct_ht_ndpa_packet(
	struct _ADAPTER		*adapter,
	u8			*RA,
	u8			*buffer,
	u32			*p_length,
	enum channel_width	BW
)
{
	u16					duration = 0;
	PMGNT_INFO				p_mgnt_info = &(adapter->MgntInfo);
	OCTET_STRING			p_ndpa_frame, action_content;
	u8					action_hdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};

	PlatformZeroMemory(buffer, 32);

	SET_80211_HDR_FRAME_CONTROL(buffer, 0);

	SET_80211_HDR_ORDER(buffer, 1);
	SET_80211_HDR_TYPE_AND_SUBTYPE(buffer, Type_Action_No_Ack);

	SET_80211_HDR_ADDRESS1(buffer, RA);
	SET_80211_HDR_ADDRESS2(buffer, adapter->CurrentAddress);
	SET_80211_HDR_ADDRESS3(buffer, p_mgnt_info->Bssid);

	duration = 2 * a_SifsTime + 40;

	if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	SET_80211_HDR_DURATION(buffer, duration);

	/* HT control field */
	SET_HT_CTRL_CSI_STEERING(buffer + sMacHdrLng, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(buffer + sMacHdrLng, 1);

	FillOctetString(p_ndpa_frame, buffer, sMacHdrLng + sHTCLng);

	FillOctetString(action_content, action_hdr, 4);
	PacketAppendData(&p_ndpa_frame, action_content);

	*p_length = 32;
}




boolean
send_fw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	PRT_TCB				p_tcb;
	PRT_TX_LOCAL_BUFFER	p_buf;
	boolean				ret = true;
	u32					buf_len;
	u8					*buf_addr;
	u8					desc_len = 0, idx = 0, ndp_tx_rate;
	struct _ADAPTER				*p_def_adapter = GetDefaultAdapter(adapter);
	HAL_DATA_TYPE			*p_hal_data = GET_HAL_DATA(adapter);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (p_beamform_entry == NULL)
		return false;

	ndp_tx_rate = beamforming_get_htndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));
	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetFWBuffer(p_def_adapter, &p_tcb, &p_buf)) {
#if (DEV_BUS_TYPE != RT_PCI_INTERFACE)
		desc_len = adapter->HWDescHeadLength - p_hal_data->USBALLDummyLength;
#endif
		buf_addr = p_buf->Buffer.VirtualAddress + desc_len;

		construct_ht_ndpa_packet(
			adapter,
			RA,
			buf_addr,
			&buf_len,
			BW
		);

		p_tcb->PacketLength = buf_len + desc_len;

		p_tcb->bTxEnableSwCalcDur = true;

		p_tcb->BWOfPacket = BW;

		if (ACTING_AS_IBSS(adapter) || ACTING_AS_AP(adapter))
			p_tcb->G_ID = 63;

		p_tcb->P_AID = p_beamform_entry->p_aid;
		p_tcb->DataRate = ndp_tx_rate;	/*rate of NDP decide by nr*/

		adapter->HalFunc.CmdSendPacketHandler(adapter, p_tcb, p_buf, p_tcb->PacketLength, DESC_PACKET_TYPE_NORMAL, false);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}


boolean
send_sw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u8					idx = 0, ndp_tx_rate = 0;
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	ndp_tx_rate = beamforming_get_htndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(adapter, &p_tcb, &p_buf)) {
		construct_ht_ndpa_packet(
			adapter,
			RA,
			p_buf->Buffer.VirtualAddress,
			&p_tcb->PacketLength,
			BW
		);

		p_tcb->bTxEnableSwCalcDur = true;

		p_tcb->BWOfPacket = BW;

		MgntSendPacket(adapter, p_tcb, p_buf, p_tcb->PacketLength, NORMAL_QUEUE, ndp_tx_rate);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}



void
construct_vht_ndpa_packet(
	struct PHY_DM_STRUCT	*p_dm,
	u8			*RA,
	u16			AID,
	u8			*buffer,
	u32			*p_length,
	enum channel_width	BW
)
{
	u16					duration = 0;
	u8					sequence = 0;
	u8					*p_ndpa_frame = buffer;
	struct _RT_NDPA_STA_INFO		sta_info;
	struct _ADAPTER				*adapter = p_dm->adapter;
	u8	idx = 0;
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);
	/* Frame control. */
	SET_80211_HDR_FRAME_CONTROL(p_ndpa_frame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(p_ndpa_frame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(p_ndpa_frame, RA);
	SET_80211_HDR_ADDRESS2(p_ndpa_frame, p_beamform_entry->my_mac_addr);

	duration = 2 * a_SifsTime + 44;

	if (BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	SET_80211_HDR_DURATION(p_ndpa_frame, duration);

	sequence = *(p_dm->p_sounding_seq) << 2;
	odm_move_memory(p_dm, p_ndpa_frame + 16, &sequence, 1);

	if (phydm_acting_determine(p_dm, phydm_acting_as_ibss) || phydm_acting_determine(p_dm, phydm_acting_as_ap) == false)
		AID = 0;

	sta_info.aid = AID;
	sta_info.feedback_type = 0;
	sta_info.nc_index = 0;

	odm_move_memory(p_dm, p_ndpa_frame + 17, (u8 *)&sta_info, 2);

	*p_length = 19;
}


boolean
send_fw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u32					buf_len;
	u8					*buf_addr;
	u8					desc_len = 0, idx = 0, ndp_tx_rate = 0;
	struct _ADAPTER				*p_def_adapter = GetDefaultAdapter(adapter);
	HAL_DATA_TYPE			*p_hal_data = GET_HAL_DATA(adapter);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (p_beamform_entry == NULL)
		return false;

	ndp_tx_rate = beamforming_get_vht_ndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetFWBuffer(p_def_adapter, &p_tcb, &p_buf)) {
#if (DEV_BUS_TYPE != RT_PCI_INTERFACE)
		desc_len = adapter->HWDescHeadLength - p_hal_data->USBALLDummyLength;
#endif
		buf_addr = p_buf->Buffer.VirtualAddress + desc_len;

		construct_vht_ndpa_packet(
			p_dm,
			RA,
			AID,
			buf_addr,
			&buf_len,
			BW
		);

		p_tcb->PacketLength = buf_len + desc_len;

		p_tcb->bTxEnableSwCalcDur = true;

		p_tcb->BWOfPacket = BW;

		if (phydm_acting_determine(p_dm, phydm_acting_as_ibss) || phydm_acting_determine(p_dm, phydm_acting_as_ap))
			p_tcb->G_ID = 63;

		p_tcb->P_AID = p_beamform_entry->p_aid;
		p_tcb->DataRate = ndp_tx_rate;	/*decide by nr*/

		adapter->HalFunc.CmdSendPacketHandler(adapter, p_tcb, p_buf, p_tcb->PacketLength, DESC_PACKET_TYPE_NORMAL, false);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] End, ret=%d\n", __func__, ret));

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}



boolean
send_sw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u8					idx = 0, ndp_tx_rate = 0;
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	ndp_tx_rate = beamforming_get_vht_ndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(adapter, &p_tcb, &p_buf)) {
		construct_vht_ndpa_packet(
			p_dm,
			RA,
			AID,
			p_buf->Buffer.VirtualAddress,
			&p_tcb->PacketLength,
			BW
		);

		p_tcb->bTxEnableSwCalcDur = true;
		p_tcb->BWOfPacket = BW;

		/*rate of NDP decide by nr*/
		MgntSendPacket(adapter, p_tcb, p_buf, p_tcb->PacketLength, NORMAL_QUEUE, ndp_tx_rate);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}

#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
/*
 * Description: On VHT GID management frame by an MU beamformee.
 *
 * 2015.05.20. Created by tynli.
 */
enum rt_status
beamforming_get_vht_gid_mgnt_frame(
	struct _ADAPTER		*adapter,
	PRT_RFD			p_rfd,
	POCTET_STRING	p_pdu_os
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
	enum rt_status		rt_status = RT_STATUS_SUCCESS;
	u8			*p_buffer = NULL;
	u8			*p_raddr = NULL;
	u8			mem_status[8] = {0}, user_pos[16] = {0};
	u8			idx;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMER_ENTRY	*p_beamform_entry = &p_beam_info->beamformer_entry[p_beam_info->mu_ap_index];

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] On VHT GID mgnt frame!\n", __func__));

	/* Check length*/
	if (p_pdu_os->length < (FRAME_OFFSET_VHT_GID_MGNT_USER_POSITION_ARRAY + 16)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("beamforming_get_vht_gid_mgnt_frame(): Invalid length (%d)\n", p_pdu_os->length));
		return RT_STATUS_INVALID_LENGTH;
	}

	/* Check RA*/
	p_raddr = (u8 *)(p_pdu_os->Octet) + 4;
	if (!eq_mac_addr(p_raddr, adapter->CurrentAddress)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("beamforming_get_vht_gid_mgnt_frame(): Drop because of RA error.\n"));
		return RT_STATUS_PKT_DROP;
	}

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "On VHT GID Mgnt Frame ==>:\n", p_pdu_os->Octet, p_pdu_os->length);

	/*Parsing Membership status array*/
	p_buffer = p_pdu_os->Octet + FRAME_OFFSET_VHT_GID_MGNT_MEMBERSHIP_STATUS_ARRAY;
	for (idx = 0; idx < 8; idx++) {
		mem_status[idx] = GET_VHT_GID_MGNT_INFO_MEMBERSHIP_STATUS(p_buffer + idx);
		p_beamform_entry->gid_valid[idx] = GET_VHT_GID_MGNT_INFO_MEMBERSHIP_STATUS(p_buffer + idx);
	}

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "mem_status: ", mem_status, 8);

	/* Parsing User Position array*/
	p_buffer = p_pdu_os->Octet + FRAME_OFFSET_VHT_GID_MGNT_USER_POSITION_ARRAY;
	for (idx = 0; idx < 16; idx++) {
		user_pos[idx] = GET_VHT_GID_MGNT_INFO_USER_POSITION(p_buffer + idx);
		p_beamform_entry->user_position[idx] = GET_VHT_GID_MGNT_INFO_USER_POSITION(p_buffer + idx);
	}

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "user_pos: ", user_pos, 16);

	/* Group ID detail printed*/
	{
		u8	i, j;
		u8	tmp_val;
		u16	tmp_val2;

		for (i = 0; i < 8; i++) {
			tmp_val = mem_status[i];
			tmp_val2 = ((user_pos[i * 2 + 1] << 8) & 0xFF00) + (user_pos[i * 2] & 0xFF);
			for (j = 0; j < 8; j++) {
				if ((tmp_val >> j) & BIT(0)) {
					PHYDM_DBG(p_dm, DBG_TXBF, ("Use Group ID (%d), User Position (%d)\n",
						(i * 8 + j), (tmp_val2 >> 2 * j) & 0x3));
				}
			}
		}
	}

	/* Indicate GID frame to IHV service. */
	{
		u8	indibuffer[24] = {0};
		u8	indioffset = 0;

		PlatformMoveMemory(indibuffer + indioffset, p_beamform_entry->gid_valid, 8);
		indioffset += 8;
		PlatformMoveMemory(indibuffer + indioffset, p_beamform_entry->user_position, 16);
		indioffset += 16;

		PlatformIndicateCustomStatus(
			adapter,
			RT_CUSTOM_EVENT_VHT_RECV_GID_MGNT_FRAME,
			RT_CUSTOM_INDI_TARGET_IHV,
			indibuffer,
			indioffset);
	}

	/* Config HW GID table */
	hal_com_txbf_config_gtab(p_dm);

	return rt_status;
}

/*
 * Description: Construct VHT Group ID (GID) management frame.
 *
 * 2015.05.20. Created by tynli.
 */
void
construct_vht_gid_mgnt_frame(
	struct PHY_DM_STRUCT		*p_dm,
	u8			*RA,
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry,
	u8			*buffer,
	u32			*p_length

)
{
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;
	OCTET_STRING		os_ftm_frame, tmp;

	FillOctetString(os_ftm_frame, buffer, 0);
	*p_length = 0;

	ConstructMaFrameHdr(
		adapter,
		RA,
		ACT_CAT_VHT,
		ACT_VHT_GROUPID_MANAGEMENT,
		&os_ftm_frame);

	/* Membership status array*/
	FillOctetString(tmp, p_beamform_entry->gid_valid, 8);
	PacketAppendData(&os_ftm_frame, tmp);

	/* User Position array*/
	FillOctetString(tmp, p_beamform_entry->user_position, 16);
	PacketAppendData(&os_ftm_frame, tmp);

	*p_length = os_ftm_frame.length;

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "construct_vht_gid_mgnt_frame():\n", buffer, *p_length);
}

boolean
send_sw_vht_gid_mgnt_frame(
	void			*p_dm_void,
	u8			*RA,
	u8			idx
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u8					data_rate = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = &p_beam_info->beamformee_entry[idx];
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(adapter, &p_tcb, &p_buf)) {
		construct_vht_gid_mgnt_frame(
			p_dm,
			RA,
			p_beamform_entry,
			p_buf->Buffer.VirtualAddress,
			&p_tcb->PacketLength
		);

		p_tcb->bw_of_packet = CHANNEL_WIDTH_20;
		data_rate = MGN_6M;
		MgntSendPacket(adapter, p_tcb, p_buf, p_tcb->PacketLength, NORMAL_QUEUE, data_rate);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}


/*
 * Description: Construct VHT beamforming report poll.
 *
 * 2015.05.20. Created by tynli.
 */
void
construct_vht_bf_report_poll(
	struct PHY_DM_STRUCT		*p_dm,
	u8			*RA,
	u8			*buffer,
	u32			*p_length
)
{
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;
	u8			*p_bf_rpt_poll = buffer;

	/* Frame control*/
	SET_80211_HDR_FRAME_CONTROL(p_bf_rpt_poll, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(p_bf_rpt_poll, Type_Beamforming_Report_Poll);

	/* duration*/
	SET_80211_HDR_DURATION(p_bf_rpt_poll, 100);

	/* RA*/
	SET_VHT_BF_REPORT_POLL_RA(p_bf_rpt_poll, RA);

	/* TA*/
	SET_VHT_BF_REPORT_POLL_TA(p_bf_rpt_poll, adapter->CurrentAddress);

	/* Feedback Segment Retransmission Bitmap*/
	SET_VHT_BF_REPORT_POLL_FEEDBACK_SEG_RETRAN_BITMAP(p_bf_rpt_poll, 0xFF);

	*p_length = 17;

	RT_DISP_DATA(FBEAM, FBEAM_DATA, "construct_vht_bf_report_poll():\n", buffer, *p_length);

}

boolean
send_sw_vht_bf_report_poll(
	void			*p_dm_void,
	u8			*RA,
	boolean			is_final_poll
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u8					idx = 0, data_rate = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(adapter, &p_tcb, &p_buf)) {
		construct_vht_bf_report_poll(
			p_dm,
			RA,
			p_buf->Buffer.VirtualAddress,
			&p_tcb->PacketLength
		);

		p_tcb->bTxEnableSwCalcDur = true; /* <tynli_note> need?*/
		p_tcb->BWOfPacket = CHANNEL_WIDTH_20;

		if (is_final_poll)
			p_tcb->TxBFPktType = RT_BF_PKT_TYPE_FINAL_BF_REPORT_POLL;
		else
			p_tcb->TxBFPktType = RT_BF_PKT_TYPE_BF_REPORT_POLL;

		data_rate = MGN_6M;	/* Legacy OFDM rate*/
		MgntSendPacket(adapter, p_tcb, p_buf, p_tcb->PacketLength, NORMAL_QUEUE, data_rate);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "send_sw_vht_bf_report_poll():\n", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;

}


/*
 * Description: Construct VHT MU NDPA packet.
 *	<Note> We should combine this function with construct_vht_ndpa_packet() in the future.
 *
 * 2015.05.21. Created by tynli.
 */
void
construct_vht_mu_ndpa_packet(
	struct PHY_DM_STRUCT		*p_dm,
	enum channel_width	BW,
	u8			*buffer,
	u32			*p_length
)
{
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;
	u16					duration = 0;
	u8					sequence = 0;
	u8					*p_ndpa_frame = buffer;
	struct _RT_NDPA_STA_INFO		sta_info;
	u8					idx;
	u8					dest_addr[6] = {0};
	struct _RT_BEAMFORMEE_ENTRY	*p_entry = NULL;

	/* Fill the first MU BFee entry (STA1) MAC addr to destination address then
	     HW will change A1 to broadcast addr. 2015.05.28. Suggested by SD1 Chunchu. */
	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		p_entry = &(p_beam_info->beamformee_entry[idx]);
		if (p_entry->is_mu_sta) {
			cp_mac_addr(dest_addr, p_entry->mac_addr);
			break;
		}
	}
	if (p_entry == NULL)
		return;

	/* Frame control.*/
	SET_80211_HDR_FRAME_CONTROL(p_ndpa_frame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(p_ndpa_frame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(p_ndpa_frame, dest_addr);
	SET_80211_HDR_ADDRESS2(p_ndpa_frame, p_entry->my_mac_addr);

	/*--------------------------------------------*/
	/* <Note> Need to modify "duration" to MU consideration. */
	duration = 2 * a_SifsTime + 44;

	if (BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;
	/*--------------------------------------------*/

	SET_80211_HDR_DURATION(p_ndpa_frame, duration);

	sequence = *(p_dm->p_sounding_seq) << 2;
	odm_move_memory(p_dm, p_ndpa_frame + 16, &sequence, 1);

	*p_length = 17;

	/* Construct STA info. for multiple STAs*/
	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		p_entry = &(p_beam_info->beamformee_entry[idx]);
		if (p_entry->is_mu_sta) {
			sta_info.aid = p_entry->AID;
			sta_info.feedback_type = 1; /* 1'b1: MU*/
			sta_info.nc_index = 0;

			PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Get beamformee_entry idx(%d), AID =%d\n", __func__, idx, p_entry->AID));

			odm_move_memory(p_dm, p_ndpa_frame + (*p_length), (u8 *)&sta_info, 2);
			*p_length += 2;
		}
	}

}

boolean
send_sw_vht_mu_ndpa_packet(
	void			*p_dm_void,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u8					ndp_tx_rate = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;

	ndp_tx_rate = MGN_VHT2SS_MCS0;
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(adapter, &p_tcb, &p_buf)) {
		construct_vht_mu_ndpa_packet(
			p_dm,
			BW,
			p_buf->Buffer.VirtualAddress,
			&p_tcb->PacketLength
		);

		p_tcb->bTxEnableSwCalcDur = true;
		p_tcb->BWOfPacket = BW;
		p_tcb->TxBFPktType = RT_BF_PKT_TYPE_BROADCAST_NDPA;

		/*rate of NDP decide by nr*/
		MgntSendPacket(adapter, p_tcb, p_buf, p_tcb->PacketLength, NORMAL_QUEUE, ndp_tx_rate);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}


void
dbg_construct_vht_mundpa_packet(
	struct PHY_DM_STRUCT		*p_dm,
	enum channel_width	BW,
	u8			*buffer,
	u32			*p_length
)
{
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;
	u16					duration = 0;
	u8					sequence = 0;
	u8					*p_ndpa_frame = buffer;
	struct _RT_NDPA_STA_INFO		sta_info;
	u8					idx;
	u8					dest_addr[6] = {0};
	struct _RT_BEAMFORMEE_ENTRY	*p_entry = NULL;

	boolean	is_STA1 = false;


	/* Fill the first MU BFee entry (STA1) MAC addr to destination address then
	     HW will change A1 to broadcast addr. 2015.05.28. Suggested by SD1 Chunchu. */
	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		p_entry = &(p_beam_info->beamformee_entry[idx]);
		if (p_entry->is_mu_sta) {
			if (is_STA1 == false) {
				is_STA1 = true;
				continue;
			} else {
				cp_mac_addr(dest_addr, p_entry->mac_addr);
				break;
			}
		}
	}

	/* Frame control.*/
	SET_80211_HDR_FRAME_CONTROL(p_ndpa_frame, 0);
	SET_80211_HDR_TYPE_AND_SUBTYPE(p_ndpa_frame, Type_NDPA);

	SET_80211_HDR_ADDRESS1(p_ndpa_frame, dest_addr);
	SET_80211_HDR_ADDRESS2(p_ndpa_frame, p_dm->CurrentAddress);

	/*--------------------------------------------*/
	/* <Note> Need to modify "duration" to MU consideration. */
	duration = 2 * a_SifsTime + 44;

	if (BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;
	/*--------------------------------------------*/

	SET_80211_HDR_DURATION(p_ndpa_frame, duration);

	sequence = *(p_dm->p_sounding_seq) << 2;
	odm_move_memory(p_dm, p_ndpa_frame + 16, &sequence, 1);

	*p_length = 17;

	/*STA2's STA Info*/
	sta_info.aid = p_entry->aid;
	sta_info.feedback_type = 1; /* 1'b1: MU */
	sta_info.nc_index = 0;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Get beamformee_entry idx(%d), AID =%d\n", __func__, idx, p_entry->aid));

	odm_move_memory(p_dm, p_ndpa_frame + (*p_length), (u8 *)&sta_info, 2);
	*p_length += 2;

}

boolean
dbg_send_sw_vht_mundpa_packet(
	void			*p_dm_void,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	PRT_TCB					p_tcb;
	PRT_TX_LOCAL_BUFFER		p_buf;
	boolean					ret = true;
	u8					ndp_tx_rate = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _ADAPTER				*adapter = p_beam_info->source_adapter;

	ndp_tx_rate = MGN_VHT2SS_MCS0;
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));

	PlatformAcquireSpinLock(adapter, RT_TX_SPINLOCK);

	if (MgntGetBuffer(adapter, &p_tcb, &p_buf)) {
		dbg_construct_vht_mundpa_packet(
			p_dm,
			BW,
			p_buf->Buffer.VirtualAddress,
			&p_tcb->PacketLength
		);

		p_tcb->bTxEnableSwCalcDur = true;
		p_tcb->BWOfPacket = BW;
		p_tcb->TxBFPktType = RT_BF_PKT_TYPE_UNICAST_NDPA;

		/*rate of NDP decide by nr*/
		MgntSendPacket(adapter, p_tcb, p_buf, p_tcb->PacketLength, NORMAL_QUEUE, ndp_tx_rate);
	} else
		ret = false;

	PlatformReleaseSpinLock(adapter, RT_TX_SPINLOCK);

	if (ret)
		RT_DISP_DATA(FBEAM, FBEAM_DATA, "", p_buf->Buffer.VirtualAddress, p_tcb->PacketLength);

	return ret;
}


#endif	/*#if (SUPPORT_MU_BF == 1)*/
#endif	/*#ifdef SUPPORT_MU_BF*/


#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

u32
beamforming_get_report_frame(
	void			*p_dm_void,
	union recv_frame *precv_frame
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32					ret = _SUCCESS;
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = NULL;
	u8					*pframe = precv_frame->u.hdr.rx_data;
	u32					frame_len = precv_frame->u.hdr.len;
	u8					*TA;
	u8					idx, offset;


	/*Memory comparison to see if CSI report is the same with previous one*/
	TA = get_addr2_ptr(pframe);
	p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, TA, &idx);
	if (p_beamform_entry->beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)
		offset = 31;		/*24+(1+1+3)+2  MAC header+(Category+ActionCode+MIMOControlField)+SNR(nc=2)*/
	else if (p_beamform_entry->beamform_entry_cap & BEAMFORMER_CAP_HT_EXPLICIT)
		offset = 34;		/*24+(1+1+6)+2  MAC header+(Category+ActionCode+MIMOControlField)+SNR(nc=2)*/
	else
		return ret;


	return ret;
}


boolean
send_fw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	action_hdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	a_sifs_time = 0, ndp_tx_rate = 0, idx = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s, alloc mgnt frame fail\n", __func__));
		return false;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, pattrib);

	pattrib->qsel = QSLT_BEACON;
	ndp_tx_rate = beamforming_get_htndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));
	pattrib->rate = ndp_tx_rate;
	pattrib->bwmode = BW;
	pattrib->order = 1;
	pattrib->subtype = WIFI_ACTION_NOACK;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	set_order_bit(pframe);
	set_frame_sub_type(pframe, WIFI_ACTION_NOACK);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, p_beamform_entry->my_mac_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		a_sifs_time = 10;
	else
		a_sifs_time = 16;

	duration = 2 * a_sifs_time + 40;

	if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	set_duration(pframe, duration);

	/* HT control field */
	SET_HT_CTRL_CSI_STEERING(pframe + 24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe + 24, 1);

	_rtw_memcpy(pframe + 28, action_hdr, 4);

	pattrib->pktlen = 32;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return true;
}


boolean
send_sw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	action_hdr[4] = {ACT_CAT_VENDOR, 0x00, 0xe0, 0x4c};
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	a_sifs_time = 0, ndp_tx_rate = 0, idx = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	ndp_tx_rate = beamforming_get_htndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s, alloc mgnt frame fail\n", __func__));
		return false;
	}

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapter, pattrib);
	pattrib->qsel = QSLT_MGNT;
	pattrib->rate = ndp_tx_rate;
	pattrib->bwmode = BW;
	pattrib->order = 1;
	pattrib->subtype = WIFI_ACTION_NOACK;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	set_order_bit(pframe);
	set_frame_sub_type(pframe, WIFI_ACTION_NOACK);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, p_beamform_entry->my_mac_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		a_sifs_time = 10;
	else
		a_sifs_time = 16;

	duration = 2 * a_sifs_time + 40;

	if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	set_duration(pframe, duration);

	/*HT control field*/
	SET_HT_CTRL_CSI_STEERING(pframe + 24, 3);
	SET_HT_CTRL_NDP_ANNOUNCEMENT(pframe + 24, 1);

	_rtw_memcpy(pframe + 28, action_hdr, 4);

	pattrib->pktlen = 32;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return true;
}


boolean
send_fw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &(adapter->mlmepriv);
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	u8	sequence = 0, a_sifs_time = 0, ndp_tx_rate = 0, idx = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);
	struct _RT_NDPA_STA_INFO	sta_info;

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s, alloc mgnt frame fail\n", __func__));
		return false;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	_rtw_memcpy(pattrib->ra, RA, ETH_ALEN);
	update_mgntframe_attrib(adapter, pattrib);

	pattrib->qsel = QSLT_BEACON;
	ndp_tx_rate = beamforming_get_vht_ndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));
	pattrib->rate = ndp_tx_rate;
	pattrib->bwmode = BW;
	pattrib->subtype = WIFI_NDPA;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	set_frame_sub_type(pframe, WIFI_NDPA);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, p_beamform_entry->my_mac_addr, ETH_ALEN);

	if (is_supported_5g(pmlmeext->cur_wireless_mode) || is_supported_ht(pmlmeext->cur_wireless_mode))
		a_sifs_time = 16;
	else
		a_sifs_time = 10;

	duration = 2 * a_sifs_time + 44;

	if (BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	set_duration(pframe, duration);

	sequence = p_beam_info->sounding_sequence << 2;
	if (p_beam_info->sounding_sequence >= 0x3f)
		p_beam_info->sounding_sequence = 0;
	else
		p_beam_info->sounding_sequence++;

	_rtw_memcpy(pframe + 16, &sequence, 1);

	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
		AID = 0;

	sta_info.aid = AID;
	sta_info.feedback_type = 0;
	sta_info.nc_index = 0;

	_rtw_memcpy(pframe + 17, (u8 *)&sta_info, 2);

	pattrib->pktlen = 19;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);

	return true;
}



boolean
send_sw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	enum channel_width	BW
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER				*adapter = p_dm->adapter;
	struct xmit_frame		*pmgntframe;
	struct pkt_attrib		*pattrib;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct xmit_priv		*pxmitpriv = &(adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &(adapter->mlmepriv);
	struct _RT_NDPA_STA_INFO	ndpa_sta_info;
	u8	ndp_tx_rate = 0, sequence = 0, a_sifs_time = 0, idx = 0;
	u8	*pframe;
	u16	*fctrl;
	u16	duration = 0;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &(p_dm->beamforming_info);
	struct _RT_BEAMFORMEE_ENTRY	*p_beamform_entry = phydm_beamforming_get_bfee_entry_by_addr(p_dm, RA, &idx);

	ndp_tx_rate = beamforming_get_vht_ndp_tx_rate(p_dm, p_beamform_entry->comp_steering_num_of_bfer);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ndp_tx_rate =%d\n", __func__, ndp_tx_rate));

	pmgntframe = alloc_mgtxmitframe(pxmitpriv);

	if (pmgntframe == NULL) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s, alloc mgnt frame fail\n", __func__));
		return false;
	}

	/*update attribute*/
	pattrib = &pmgntframe->attrib;
	_rtw_memcpy(pattrib->ra, RA, ETH_ALEN);
	update_mgntframe_attrib(adapter, pattrib);
	pattrib->qsel = QSLT_MGNT;
	pattrib->rate = ndp_tx_rate;
	pattrib->bwmode = BW;
	pattrib->subtype = WIFI_NDPA;

	_rtw_memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	set_frame_sub_type(pframe, WIFI_NDPA);

	_rtw_memcpy(pwlanhdr->addr1, RA, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, p_beamform_entry->my_mac_addr, ETH_ALEN);

	if (is_supported_5g(pmlmeext->cur_wireless_mode) || is_supported_ht(pmlmeext->cur_wireless_mode))
		a_sifs_time = 16;
	else
		a_sifs_time = 10;

	duration = 2 * a_sifs_time + 44;

	if (BW == CHANNEL_WIDTH_80)
		duration += 40;
	else if (BW == CHANNEL_WIDTH_40)
		duration += 87;
	else
		duration += 180;

	set_duration(pframe, duration);

	sequence = p_beam_info->sounding_sequence << 2;
	if (p_beam_info->sounding_sequence >= 0x3f)
		p_beam_info->sounding_sequence = 0;
	else
		p_beam_info->sounding_sequence++;

	_rtw_memcpy(pframe + 16, &sequence, 1);
	if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
		AID = 0;

	ndpa_sta_info.aid = AID;
	ndpa_sta_info.feedback_type = 0;
	ndpa_sta_info.nc_index = 0;

	_rtw_memcpy(pframe + 17, (u8 *)&ndpa_sta_info, 2);

	pattrib->pktlen = 19;

	pattrib->last_txcmdsz = pattrib->pktlen;

	dump_mgntframe(adapter, pmgntframe);
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] [%d]\n", __func__, __LINE__));

	return true;
}


#endif


void
beamforming_get_ndpa_frame(
	void			*p_dm_void,
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	OCTET_STRING	pdu_os
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	union recv_frame *precv_frame
#endif
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8						*TA ;
	u8						idx, sequence;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	u8						*p_ndpa_frame = pdu_os.Octet;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	u8						*p_ndpa_frame = precv_frame->u.hdr.rx_data;
#endif
	struct _RT_BEAMFORMER_ENTRY		*p_beamformer_entry = NULL;		/*Modified By Jeffery @2014-10-29*/


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_DISP_DATA(FBEAM, FBEAM_DATA, "beamforming_get_ndpa_frame\n", pdu_os.Octet, pdu_os.Length);
	if (IsCtrlNDPA(p_ndpa_frame) == false)
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (get_frame_sub_type(p_ndpa_frame) != WIFI_NDPA)
#endif
		return;
	else if (!(p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821))) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] not 8812 or 8821A, return\n", __func__));
		return;
	}
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	TA = Frame_Addr2(pdu_os);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	TA = get_addr2_ptr(p_ndpa_frame);
#endif
	/*Remove signaling TA. */
	TA[0] = TA[0] & 0xFE;

	p_beamformer_entry = phydm_beamforming_get_bfer_entry_by_addr(p_dm, TA, &idx);		/* Modified By Jeffery @2014-10-29 */

	/*Break options for Clock Reset*/
	if (p_beamformer_entry == NULL)
		return;
	else if (!(p_beamformer_entry->beamform_entry_cap & BEAMFORMEE_CAP_VHT_SU))
		return;
	/*log_success: As long as 8812A receive NDPA and feedback CSI succeed once, clock reset is NO LONGER needed !2015-04-10, Jeffery*/
	/*clock_reset_times: While BFer entry always doesn't receive our CSI, clock will reset again and again.So clock_reset_times is limited to 5 times.2015-04-13, Jeffery*/
	else if ((p_beamformer_entry->log_success == 1) || (p_beamformer_entry->clock_reset_times == 5)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] log_seq=%d, pre_log_seq=%d, log_retry_cnt=%d, log_success=%d, clock_reset_times=%d, clock reset is no longer needed.\n",
			__func__, p_beamformer_entry->log_seq, p_beamformer_entry->pre_log_seq, p_beamformer_entry->log_retry_cnt, p_beamformer_entry->log_success, p_beamformer_entry->clock_reset_times));

		return;
	}

	sequence = (p_ndpa_frame[16]) >> 2;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start, sequence=%d, log_seq=%d, pre_log_seq=%d, log_retry_cnt=%d, clock_reset_times=%d, log_success=%d\n",
		__func__, sequence, p_beamformer_entry->log_seq, p_beamformer_entry->pre_log_seq, p_beamformer_entry->log_retry_cnt, p_beamformer_entry->clock_reset_times, p_beamformer_entry->log_success));

	if ((p_beamformer_entry->log_seq != 0) && (p_beamformer_entry->pre_log_seq != 0)) {
		/*Success condition*/
		if ((p_beamformer_entry->log_seq != sequence) && (p_beamformer_entry->pre_log_seq != p_beamformer_entry->log_seq)) {
			/* break option for clcok reset, 2015-03-30, Jeffery */
			p_beamformer_entry->log_retry_cnt = 0;
			/*As long as 8812A receive NDPA and feedback CSI succeed once, clock reset is no longer needed.*/
			/*That is, log_success is NOT needed to be reset to zero, 2015-04-13, Jeffery*/
			p_beamformer_entry->log_success = 1;

		} else {/*Fail condition*/

			if (p_beamformer_entry->log_retry_cnt == 5) {
				p_beamformer_entry->clock_reset_times++;
				p_beamformer_entry->log_retry_cnt = 0;

				PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Clock Reset!!! clock_reset_times=%d\n",
					__func__, p_beamformer_entry->clock_reset_times));
				hal_com_txbf_set(p_dm, TXBF_SET_SOUNDING_CLK, NULL);

			} else
				p_beamformer_entry->log_retry_cnt++;
		}
	}

	/*Update log_seq & pre_log_seq*/
	p_beamformer_entry->pre_log_seq = p_beamformer_entry->log_seq;
	p_beamformer_entry->log_seq = sequence;

}



#endif
