/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HAL_TXBF_INTERFACE_H__
#define __HAL_TXBF_INTERFACE_H__

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define a_SifsTime					((IS_WIRELESS_MODE_5G(adapter)|| IS_WIRELESS_MODE_N_24G(adapter))? 16 : 10)

void
beamforming_gid_paid(
	struct _ADAPTER	*adapter,
	PRT_TCB		p_tcb
);

enum rt_status
beamforming_get_report_frame(
	struct _ADAPTER		*adapter,
	PRT_RFD			p_rfd,
	POCTET_STRING	p_pdu_os
);

void
beamforming_get_ndpa_frame(
	void			*p_dm_void,
	OCTET_STRING	pdu_os
);

boolean
send_fw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	CHANNEL_WIDTH	BW
);

boolean
send_fw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	CHANNEL_WIDTH	BW
);

boolean
send_sw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	CHANNEL_WIDTH	BW
);

boolean
send_sw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	CHANNEL_WIDTH	BW
);

#if (SUPPORT_MU_BF == 1)
enum rt_status
beamforming_get_vht_gid_mgnt_frame(
	struct _ADAPTER		*adapter,
	PRT_RFD			p_rfd,
	POCTET_STRING	p_pdu_os
);

boolean
send_sw_vht_gid_mgnt_frame(
	void			*p_dm_void,
	u8			*RA,
	u8			idx
);

boolean
send_sw_vht_bf_report_poll(
	void			*p_dm_void,
	u8			*RA,
	boolean			is_final_poll
);

boolean
send_sw_vht_mu_ndpa_packet(
	void			*p_dm_void,
	CHANNEL_WIDTH	BW
);
#else
#define beamforming_get_vht_gid_mgnt_frame(adapter, p_rfd, p_pdu_os) RT_STATUS_FAILURE
#define send_sw_vht_gid_mgnt_frame(p_dm_void, RA)
#define send_sw_vht_bf_report_poll(p_dm_void, RA, is_final_poll)
#define send_sw_vht_mu_ndpa_packet(p_dm_void, BW)
#endif


#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

u32
beamforming_get_report_frame(
	void			*p_dm_void,
	union recv_frame *precv_frame
);

boolean
send_fw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	CHANNEL_WIDTH	BW
);

boolean
send_sw_ht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	CHANNEL_WIDTH	BW
);

boolean
send_fw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	CHANNEL_WIDTH	BW
);

boolean
send_sw_vht_ndpa_packet(
	void			*p_dm_void,
	u8			*RA,
	u16			AID,
	CHANNEL_WIDTH	BW
);
#endif

void
beamforming_get_ndpa_frame(
	void			*p_dm_void,
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	OCTET_STRING	pdu_os
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	union recv_frame *precv_frame
#endif
);

boolean
dbg_send_sw_vht_mundpa_packet(
	void			*p_dm_void,
	CHANNEL_WIDTH	BW
);

#else
#define beamforming_get_ndpa_frame(p_dm_odm, _pdu_os)
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#define beamforming_get_report_frame(adapter, precv_frame)		RT_STATUS_FAILURE
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define beamforming_get_report_frame(adapter, p_rfd, p_pdu_os)		RT_STATUS_FAILURE
	#define beamforming_get_vht_gid_mgnt_frame(adapter, p_rfd, p_pdu_os) RT_STATUS_FAILURE
#endif
#define send_fw_ht_ndpa_packet(p_dm_void, RA, BW)
#define send_sw_ht_ndpa_packet(p_dm_void, RA, BW)
#define send_fw_vht_ndpa_packet(p_dm_void, RA, AID, BW)
#define send_sw_vht_ndpa_packet(p_dm_void, RA,	AID, BW)
#define send_sw_vht_gid_mgnt_frame(p_dm_void, RA, idx)
#define send_sw_vht_bf_report_poll(p_dm_void, RA, is_final_poll)
#define send_sw_vht_mu_ndpa_packet(p_dm_void, BW)
#endif

#endif
