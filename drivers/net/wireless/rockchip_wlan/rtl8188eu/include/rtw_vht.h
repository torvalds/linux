/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef _RTW_VHT_H_
#define _RTW_VHT_H_

#define	LDPC_VHT_ENABLE_RX			BIT0
#define	LDPC_VHT_ENABLE_TX			BIT1
#define	LDPC_VHT_TEST_TX_ENABLE		BIT2
#define	LDPC_VHT_CAP_TX				BIT3

#define	STBC_VHT_ENABLE_RX			BIT0
#define	STBC_VHT_ENABLE_TX			BIT1
#define	STBC_VHT_TEST_TX_ENABLE		BIT2
#define	STBC_VHT_CAP_TX				BIT3

#define	BEAMFORMING_VHT_BEAMFORMER_ENABLE	BIT0	/* Declare our NIC supports beamformer */
#define	BEAMFORMING_VHT_BEAMFORMEE_ENABLE	BIT1	/* Declare our NIC supports beamformee */
#define	BEAMFORMING_VHT_MU_MIMO_AP_ENABLE		BIT2			/*Declare our NIC support MU-MIMO AP mode*/
#define	BEAMFORMING_VHT_MU_MIMO_STA_ENABLE	BIT3			/*Declare our NIC support MU-MIMO STA mode*/
#define	BEAMFORMING_VHT_BEAMFORMER_TEST		BIT4			/*Transmiting Beamforming no matter the target supports it or not*/
#define	BEAMFORMING_VHT_BEAMFORMER_STS_CAP		(BIT8 | BIT9 | BIT10)		/*Asoc rsp cap*/
#define	BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM		(BIT12 | BIT13 | BIT14)		/*Asoc rsp cap*/


/* VHT capability info */
#define SET_VHT_CAPABILITY_ELE_MAX_MPDU_LENGTH(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart, 0, 2, _val)
#define SET_VHT_CAPABILITY_ELE_CHL_WIDTH(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart, 2, 2, _val)
#define SET_VHT_CAPABILITY_ELE_RX_LDPC(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart, 4, 1, _val)
#define SET_VHT_CAPABILITY_ELE_SHORT_GI80M(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE(_pEleStart, 5, 1, _val)
#define SET_VHT_CAPABILITY_ELE_SHORT_GI160M(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE(_pEleStart, 6, 1, _val)
#define SET_VHT_CAPABILITY_ELE_TX_STBC(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE(_pEleStart, 7, 1, _val)
#define SET_VHT_CAPABILITY_ELE_RX_STBC(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+1, 0, 3, _val)
#define SET_VHT_CAPABILITY_ELE_SU_BFER(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+1, 3, 1, _val)
#define SET_VHT_CAPABILITY_ELE_SU_BFEE(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+1, 4, 1, _val)
#define SET_VHT_CAPABILITY_ELE_BFER_ANT_SUPP(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+1, 5, 3, _val)
#define SET_VHT_CAPABILITY_ELE_SOUNDING_DIMENSIONS(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+2, 0, 3, _val)

#define SET_VHT_CAPABILITY_ELE_MU_BFER(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+2, 3, 1, _val)
#define SET_VHT_CAPABILITY_ELE_MU_BFEE(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+2, 4, 1, _val)
#define SET_VHT_CAPABILITY_ELE_TXOP_PS(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+2, 5, 1, _val)
#define SET_VHT_CAPABILITY_ELE_HTC_VHT(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+2, 6, 1, _val)
#define SET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(_pEleStart, _val)		SET_BITS_TO_LE_2BYTE((_pEleStart)+2, 7, 3, _val) /* B23~B25 */
#define SET_VHT_CAPABILITY_ELE_LINK_ADAPTION(_pEleStart, _val)				SET_BITS_TO_LE_1BYTE((_pEleStart)+2, 2, 2, _val)
#define SET_VHT_CAPABILITY_ELE_MCS_RX_MAP(_pEleStart, _val)				SET_BITS_TO_LE_2BYTE((_pEleStart)+4, 0, 16, _val)   /* B0~B15 indicate Rx MCS MAP, we write 0 to indicate MCS0~7. by page */
#define SET_VHT_CAPABILITY_ELE_MCS_RX_HIGHEST_RATE(_pEleStart, _val)				SET_BITS_TO_LE_2BYTE((_pEleStart)+6, 0, 13, _val)
#define SET_VHT_CAPABILITY_ELE_MCS_TX_MAP(_pEleStart, _val)				SET_BITS_TO_LE_2BYTE((_pEleStart)+8, 0, 16, _val)   /* B0~B15 indicate Tx MCS MAP, we write 0 to indicate MCS0~7. by page */
#define SET_VHT_CAPABILITY_ELE_MCS_TX_HIGHEST_RATE(_pEleStart, _val)				SET_BITS_TO_LE_2BYTE((_pEleStart)+10, 0, 13, _val)


#define GET_VHT_CAPABILITY_ELE_MAX_MPDU_LENGTH(_pEleStart)			LE_BITS_TO_1BYTE(_pEleStart, 0, 2)
#define GET_VHT_CAPABILITY_ELE_CHL_WIDTH(_pEleStart)				LE_BITS_TO_1BYTE(_pEleStart, 2, 2)
#define GET_VHT_CAPABILITY_ELE_RX_LDPC(_pEleStart)			LE_BITS_TO_1BYTE(_pEleStart, 4, 1)
#define GET_VHT_CAPABILITY_ELE_SHORT_GI80M(_pEleStart)				LE_BITS_TO_1BYTE(_pEleStart, 5, 1)
#define GET_VHT_CAPABILITY_ELE_SHORT_GI160M(_pEleStart)				LE_BITS_TO_1BYTE(_pEleStart, 6, 1)
#define GET_VHT_CAPABILITY_ELE_TX_STBC(_pEleStart)				LE_BITS_TO_1BYTE(_pEleStart, 7, 1)
#define GET_VHT_CAPABILITY_ELE_RX_STBC(_pEleStart)				LE_BITS_TO_1BYTE((_pEleStart)+1, 0, 3)
#define GET_VHT_CAPABILITY_ELE_SU_BFER(_pEleStart)					LE_BITS_TO_1BYTE((_pEleStart)+1, 3, 1)
#define GET_VHT_CAPABILITY_ELE_SU_BFEE(_pEleStart)					LE_BITS_TO_1BYTE((_pEleStart)+1, 4, 1)
/*phydm-beamforming*/
#define GET_VHT_CAPABILITY_ELE_SU_BFEE_STS_CAP(_pEleStart)	LE_BITS_TO_2BYTE((_pEleStart)+1, 5, 3)
#define GET_VHT_CAPABILITY_ELE_SU_BFER_SOUND_DIM_NUM(_pEleStart)	LE_BITS_TO_2BYTE((_pEleStart)+2, 0, 3)
#define GET_VHT_CAPABILITY_ELE_MU_BFER(_pEleStart)				LE_BITS_TO_1BYTE((_pEleStart)+2, 3, 1)
#define GET_VHT_CAPABILITY_ELE_MU_BFEE(_pEleStart)				LE_BITS_TO_1BYTE((_pEleStart)+2, 4, 1)
#define GET_VHT_CAPABILITY_ELE_TXOP_PS(_pEleStart)				LE_BITS_TO_1BYTE((_pEleStart)+2, 5, 1)
#define GET_VHT_CAPABILITY_ELE_MAX_RXAMPDU_FACTOR(_pEleStart)	LE_BITS_TO_2BYTE((_pEleStart)+2, 7, 3)
#define GET_VHT_CAPABILITY_ELE_RX_MCS(_pEleStart)					       ((_pEleStart)+4)
#define GET_VHT_CAPABILITY_ELE_MCS_RX_HIGHEST_RATE(_pEleStart)			LE_BITS_TO_2BYTE((_pEleStart)+6, 0, 13)
#define GET_VHT_CAPABILITY_ELE_TX_MCS(_pEleStart)					       ((_pEleStart)+8)
#define GET_VHT_CAPABILITY_ELE_MCS_TX_HIGHEST_RATE(_pEleStart)			LE_BITS_TO_2BYTE((_pEleStart)+10, 0, 13)


/* VHT Operation Information Element */
#define SET_VHT_OPERATION_ELE_CHL_WIDTH(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart, 0, 8, _val)
#define SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ1(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart+1, 0, 8, _val)
#define SET_VHT_OPERATION_ELE_CHL_CENTER_FREQ2(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart+2, 0, 8, _val)
#define SET_VHT_OPERATION_ELE_BASIC_MCS_SET(_pEleStart, _val)			SET_BITS_TO_LE_2BYTE((_pEleStart)+3, 0, 16, _val)

#define GET_VHT_OPERATION_ELE_CHL_WIDTH(_pEleStart)		LE_BITS_TO_1BYTE(_pEleStart, 0, 8)
#define GET_VHT_OPERATION_ELE_CENTER_FREQ1(_pEleStart)	LE_BITS_TO_1BYTE((_pEleStart)+1, 0, 8)
#define GET_VHT_OPERATION_ELE_CENTER_FREQ2(_pEleStart)     LE_BITS_TO_1BYTE((_pEleStart)+2, 0, 8)

/* VHT Operating Mode */
#define SET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(_pEleStart, _val)		SET_BITS_TO_LE_1BYTE(_pEleStart, 0, 2, _val)
#define SET_VHT_OPERATING_MODE_FIELD_RX_NSS(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE(_pEleStart, 4, 3, _val)
#define SET_VHT_OPERATING_MODE_FIELD_RX_NSS_TYPE(_pEleStart, _val)	SET_BITS_TO_LE_1BYTE(_pEleStart, 7, 1, _val)
#define GET_VHT_OPERATING_MODE_FIELD_CHNL_WIDTH(_pEleStart)			LE_BITS_TO_1BYTE(_pEleStart, 0, 2)
#define GET_VHT_OPERATING_MODE_FIELD_RX_NSS(_pEleStart)				LE_BITS_TO_1BYTE(_pEleStart, 4, 3)
#define GET_VHT_OPERATING_MODE_FIELD_RX_NSS_TYPE(_pEleStart)		LE_BITS_TO_1BYTE(_pEleStart, 7, 1)

#define SET_EXT_CAPABILITY_ELE_OP_MODE_NOTIF(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE((_pEleStart)+7, 6, 1, _val)
#define GET_EXT_CAPABILITY_ELE_OP_MODE_NOTIF(_pEleStart)				LE_BITS_TO_1BYTE((_pEleStart)+7, 6, 1)

struct vht_priv {
	u8	vht_option;

	u8	ldpc_cap;
	u8	stbc_cap;
	u16	beamform_cap;

	u8	sgi_80m;/* short GI */
	u8	ampdu_len;

	u8	vht_op_mode_notify;
	u8	vht_highest_rate;
	u8	vht_mcs_map[2];

	u8	vht_cap[32];
};

u8	rtw_get_vht_highest_rate(u8 *pvht_mcs_map);
u16	rtw_vht_mcs_to_data_rate(u8 bw, u8 short_GI, u8 vht_mcs_rate);
u64	rtw_vht_mcs_map_to_bitmap(u8 *mcs_map, u8 nss);
void	rtw_vht_use_default_setting(_adapter *padapter);
u32	rtw_build_vht_operation_ie(_adapter *padapter, u8 *pbuf, u8 channel);
u32	rtw_build_vht_op_mode_notify_ie(_adapter *padapter, u8 *pbuf, u8 bw);
u32	rtw_build_vht_cap_ie(_adapter *padapter, u8 *pbuf);
void	update_sta_vht_info_apmode(_adapter *padapter, PVOID psta);
void	update_hw_vht_param(_adapter *padapter);
void	VHT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void	VHT_operation_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void	rtw_process_vht_op_mode_notify(_adapter *padapter, u8 *pframe, PVOID sta);
u32	rtw_restructure_vht_ie(_adapter *padapter, u8 *in_ie, u8 *out_ie, uint in_len, uint *pout_len);
void	VHTOnAssocRsp(_adapter *padapter);
u8	rtw_vht_mcsmap_to_nss(u8 *pvht_mcs_map);
void rtw_vht_nss_to_mcsmap(u8 nss, u8 *target_mcs_map, u8 *cur_mcs_map);

#endif /* _RTW_VHT_H_ */
