/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
#ifndef __RTW_BEAMFORMING_H_
#define __RTW_BEAMFORMING_H_

#ifdef CONFIG_BEAMFORMING

#ifdef RTW_BEAMFORMING_VERSION_2
#define MAX_NUM_BEAMFORMEE_SU	2
#define MAX_NUM_BEAMFORMER_SU	2
#define MAX_NUM_BEAMFORMEE_MU	6
#define MAX_NUM_BEAMFORMER_MU	1

#define MAX_BEAMFORMEE_ENTRY_NUM	(MAX_NUM_BEAMFORMEE_SU + MAX_NUM_BEAMFORMEE_MU)
#define MAX_BEAMFORMER_ENTRY_NUM	(MAX_NUM_BEAMFORMER_SU + MAX_NUM_BEAMFORMER_MU)

/* <Note> Need to be defined by IC */
#define SU_SOUNDING_TIMEOUT	5	/* unit: ms */
#define MU_SOUNDING_TIMEOUT	8	/* unit: ms */

#define GET_BEAMFORM_INFO(adapter)	(&GET_HAL_DATA(adapter)->beamforming_info)
#define GetInitSoundCnt(_SoundPeriod, _MinSoundPeriod)	((_SoundPeriod)/(_MinSoundPeriod))

enum BEAMFORMING_CTRL_TYPE {
	BEAMFORMING_CTRL_ENTER = 0,
	BEAMFORMING_CTRL_LEAVE = 1,
	BEAMFORMING_CTRL_START_PERIOD = 2,
	BEAMFORMING_CTRL_END_PERIOD = 3,
	BEAMFORMING_CTRL_SOUNDING_FAIL = 4,
	BEAMFORMING_CTRL_SOUNDING_CLK = 5,
	BEAMFORMING_CTRL_SET_GID_TABLE = 6,
	BEAMFORMING_CTRL_SET_CSI_REPORT = 7,
};

enum _BEAMFORMING_STATE {
	BEAMFORMING_STATE_IDLE,
	BEAMFORMING_STATE_START,
	BEAMFORMING_STATE_END,
};

/*
 * typedef BEAMFORMING_CAP for phydm
 */
typedef enum beamforming_cap {
	BEAMFORMING_CAP_NONE = 0x0,
	BEAMFORMER_CAP_HT_EXPLICIT = 0x1,
	BEAMFORMEE_CAP_HT_EXPLICIT = 0x2,
	BEAMFORMER_CAP_VHT_SU = 0x4,			/* Self has er Cap, because Reg er  & peer ee */
	BEAMFORMEE_CAP_VHT_SU = 0x8, 			/* Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP_VHT_MU = 0x10,			/* Self has er Cap, because Reg er & peer ee */
	BEAMFORMEE_CAP_VHT_MU = 0x20,			/* Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP = 0x40,
	BEAMFORMEE_CAP = 0x80,
} BEAMFORMING_CAP;

enum _BEAMFORM_ENTRY_HW_STATE {
	BEAMFORM_ENTRY_HW_STATE_NONE,
	BEAMFORM_ENTRY_HW_STATE_ADD_INIT,
	BEAMFORM_ENTRY_HW_STATE_ADDING,
	BEAMFORM_ENTRY_HW_STATE_ADDED,
	BEAMFORM_ENTRY_HW_STATE_DELETE_INIT,
	BEAMFORM_ENTRY_HW_STATE_DELETING,
	BEAMFORM_ENTRY_HW_STATE_MAX
};

/* The sounding state is recorded by BFer. */
enum _SOUNDING_STATE {
	SOUNDING_STATE_NONE		= 0,
	SOUNDING_STATE_INIT		= 1,
	SOUNDING_STATE_SU_START		= 2,
	SOUNDING_STATE_SU_SOUNDDOWN	= 3,
	SOUNDING_STATE_MU_START		= 4,
	SOUNDING_STATE_MU_SOUNDDOWN	= 5,
	SOUNDING_STATE_SOUNDING_TIMEOUT	= 6,
	SOUNDING_STATE_MAX
};

struct beamformee_entry {
	u8 used;	/* _TRUE/_FALSE */
	u8 txbf;
	u8 sounding;
	/* Used to construct AID field of NDPA packet */
	u16 aid;
	/* Used to Set Reg42C in IBSS mode */
	u16 mac_id;
	/* Used to fill Reg42C & Reg714 to compare with P_AID of Tx DESC */
	u16 p_aid;
	u8 g_id;
	/* Used to fill Reg6E4 to fill Mac address of CSI report frame */
	u8 mac_addr[ETH_ALEN];
	/* Sounding BandWidth */
	enum channel_width sound_bw;
	u16 sound_period;

	enum beamforming_cap cap;
	enum _BEAMFORM_ENTRY_HW_STATE state;

	/* The BFee need to be sounded when count to zero */
	u8 SoundCnt;
	u8 bCandidateSoundingPeer;
	u8 bSoundingTimeout;
	u8 bDeleteSounding;
	/* Get the result through throughput and Tx rate from BB API */
	u8 bApplySounding;

	/* information for sounding judgement */
	systime tx_timestamp;
	u64 tx_bytes;

	u16 LogStatusFailCnt:5;	/* 0~21 */
	u16 DefaultCSICnt:5; /* 0~21 */
	u8 CSIMatrix[327];
	u16 CSIMatrixLen;

	u8 NumofSoundingDim;

	u8 comp_steering_num_of_bfer;


	/* SU-MIMO */
	u8 su_reg_index;

	/* MU-MIMO */
	u8 mu_reg_index;
	u8 gid_valid[8];
	u8 user_position[16];

	/* For 8822B C-cut workaround */
	/* If the flag set to _TRUE, do not sound this STA */
	u8 bSuspendSUCap;
};

struct beamformer_entry {
	u8 used;
	/* p_aid of BFer entry is probably not used */
	/* Used to fill Reg42C & Reg714 to compare with p_aid of Tx DESC */
	u16 p_aid;
	u8 g_id;
	u8 mac_addr[ETH_ALEN];

	enum beamforming_cap cap;
	enum _BEAMFORM_ENTRY_HW_STATE state;

	u8 NumofSoundingDim;

	/* SU-MIMO */
	u8 su_reg_index;

	/* MU-MIMO */
	u8 gid_valid[8];
	u8 user_position[16];
	u16 aid;
};

struct sounding_info {
	u8 su_sounding_list[MAX_NUM_BEAMFORMEE_SU];
	u8 mu_sounding_list[MAX_NUM_BEAMFORMEE_MU];

	enum _SOUNDING_STATE state;
	/*
	 * su_bfee_curidx is index for beamforming_info.bfee_entry[]
	 * range: 0~MAX_BEAMFORMEE_ENTRY_NUM
	 */
	u8 su_bfee_curidx;
	u8 candidate_mu_bfee_cnt;

	/* For sounding schedule maintenance */
	u16 min_sounding_period;
	/* Get from sounding list */
	/* Ex: SU STA1, SU STA2, MU STA(1~n) => the value will be 2+1=3 */
	u8 sound_remain_cnt_per_period;
};

struct _RT_CSI_INFO{
	u8 Nc;
	u8 Nr;
	u8 Ng;
	u8 CodeBook;
	u8 ChnlWidth;
	u8 bVHT;
};

struct beamforming_info {
	enum beamforming_cap beamforming_cap;
	enum _BEAMFORMING_STATE beamforming_state;
	struct beamformee_entry bfee_entry[MAX_BEAMFORMEE_ENTRY_NUM];
	struct beamformer_entry bfer_entry[MAX_BEAMFORMER_ENTRY_NUM];
	u8 sounding_sequence;
	u8 beamformee_su_cnt;
	u8 beamformer_su_cnt;
	u32 beamformee_su_reg_maping;
	u32 beamformer_su_reg_maping;
	/* For MU-MINO */
	u8 beamformee_mu_cnt;
	u8 beamformer_mu_cnt;
	u32 beamformee_mu_reg_maping;
	u8 first_mu_bfee_index;
	u8 mu_bfer_curidx;

	struct sounding_info sounding_info;
	/* schedule regular timer for sounding */
	_timer sounding_timer;
	/* moniter if soudning too long */
	_timer sounding_timeout_timer;

	/* For HW configuration */
	u8 SetHalBFEnterOnDemandCnt;
	u8 SetHalBFLeaveOnDemandCnt;
	u8 SetHalSoundownOnDemandCnt;
	u8 bSetBFHwConfigInProgess;

	/*
	 * Target CSI report info.
	 * Keep the first SU CSI report info for 8822B HW bug workaround.
	 */
	u8 bEnableSUTxBFWorkAround;
	struct _RT_CSI_INFO TargetCSIInfo;
	/* Only peform sounding to the first SU BFee */
	struct beamformee_entry *TargetSUBFee;

	/* For debug */
	s8 sounding_running;
};

enum beamforming_cap rtw_bf_bfee_get_entry_cap_by_macid(void *mlmepriv, u8 mac_id);
struct beamformer_entry *rtw_bf_bfer_get_entry_by_addr(PADAPTER, u8 *ra);
struct beamformee_entry *rtw_bf_bfee_get_entry_by_addr(PADAPTER, u8 *ra);
void rtw_bf_get_ndpa_packet(PADAPTER, union recv_frame *);
u32 rtw_bf_get_report_packet(PADAPTER, union recv_frame *);
u8 rtw_bf_send_vht_gid_mgnt_packet(PADAPTER, u8 *ra, u8 *gid, u8 *position);
void rtw_bf_get_vht_gid_mgnt_packet(PADAPTER, union recv_frame *);
void rtw_bf_init(PADAPTER);
void rtw_bf_cmd_hdl(PADAPTER, u8 type, u8 *pbuf);
u8 rtw_bf_cmd(PADAPTER, s32 type, u8 *pbuf, s32 size, u8 enqueue);
void rtw_bf_update_attrib(PADAPTER, struct pkt_attrib *, struct sta_info *);
void rtw_bf_c2h_handler(PADAPTER, u8 id, u8 *buf, u8 buf_len);
void rtw_bf_update_traffic(PADAPTER);

/* Compatible with old function name, only for using outside rtw_beamforming.c */
#define beamforming_get_entry_beam_cap_by_mac_id	rtw_bf_bfee_get_entry_cap_by_macid
#define rtw_beamforming_get_ndpa_frame			rtw_bf_get_ndpa_packet
#define rtw_beamforming_get_report_frame			rtw_bf_get_report_packet
#define rtw_beamforming_get_vht_gid_mgnt_frame		rtw_bf_get_vht_gid_mgnt_packet
#define beamforming_wk_hdl				rtw_bf_cmd_hdl
#define beamforming_wk_cmd				rtw_bf_cmd
#define update_attrib_txbf_info				rtw_bf_update_attrib

#else /* !RTW_BEAMFORMING_VERSION_2 */

#if (BEAMFORMING_SUPPORT == 0) /*for diver defined beamforming*/
#define BEAMFORMING_ENTRY_NUM		2
#define GET_BEAMFORM_INFO(_pmlmepriv)	((struct beamforming_info *)(&(_pmlmepriv)->beamforming_info))


typedef enum _BEAMFORMING_ENTRY_STATE {
	BEAMFORMING_ENTRY_STATE_UNINITIALIZE,
	BEAMFORMING_ENTRY_STATE_INITIALIZEING,
	BEAMFORMING_ENTRY_STATE_INITIALIZED,
	BEAMFORMING_ENTRY_STATE_PROGRESSING,
	BEAMFORMING_ENTRY_STATE_PROGRESSED,
} BEAMFORMING_ENTRY_STATE, *PBEAMFORMING_ENTRY_STATE;


typedef enum _BEAMFORMING_STATE {
	BEAMFORMING_STATE_IDLE,
	BEAMFORMING_STATE_START,
	BEAMFORMING_STATE_END,
} BEAMFORMING_STATE, *PBEAMFORMING_STATE;


typedef enum _BEAMFORMING_CAP {
	BEAMFORMING_CAP_NONE = 0x0,
	BEAMFORMER_CAP_HT_EXPLICIT = 0x1,
	BEAMFORMEE_CAP_HT_EXPLICIT = 0x2,
	BEAMFORMER_CAP_VHT_SU = 0x4,			/* Self has er Cap, because Reg er  & peer ee */
	BEAMFORMEE_CAP_VHT_SU = 0x8, 			/* Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP = 0x10,
	BEAMFORMEE_CAP = 0x20,
} BEAMFORMING_CAP, *PBEAMFORMING_CAP;


typedef enum _SOUNDING_MODE {
	SOUNDING_SW_VHT_TIMER = 0x0,
	SOUNDING_SW_HT_TIMER = 0x1,
	SOUNDING_STOP_All_TIMER = 0x2,
	SOUNDING_HW_VHT_TIMER = 0x3,
	SOUNDING_HW_HT_TIMER = 0x4,
	SOUNDING_STOP_OID_TIMER = 0x5,
	SOUNDING_AUTO_VHT_TIMER = 0x6,
	SOUNDING_AUTO_HT_TIMER = 0x7,
	SOUNDING_FW_VHT_TIMER = 0x8,
	SOUNDING_FW_HT_TIMER = 0x9,
} SOUNDING_MODE, *PSOUNDING_MODE;

struct beamforming_entry {
	BOOLEAN	bUsed;
	BOOLEAN	bSound;
	u16	aid;			/* Used to construct AID field of NDPA packet. */
	u16	mac_id;		/* Used to Set Reg42C in IBSS mode. */
	u16	p_aid;		/* Used to fill Reg42C & Reg714 to compare with P_AID of Tx DESC. */
	u16 g_id;
	u8	mac_addr[6];/* Used to fill Reg6E4 to fill Mac address of CSI report frame. */
	enum channel_width	sound_bw;	/* Sounding BandWidth */
	u16	sound_period;
	BEAMFORMING_CAP	beamforming_entry_cap;
	BEAMFORMING_ENTRY_STATE	beamforming_entry_state;
	u8				ClockResetTimes;			/*Modified by Jeffery @2015-04-10*/
	u8				PreLogSeq;				/*Modified by Jeffery @2015-03-30*/
	u8				LogSeq;					/*Modified by Jeffery @2014-10-29*/
	u16				LogRetryCnt:3;			/*Modified by Jeffery @2014-10-29*/
	u16				LogSuccess:2;			/*Modified by Jeffery @2014-10-29*/

	u8	LogStatusFailCnt;
	u8	PreCsiReport[327];
	u8	DefaultCsiCnt;
	BOOLEAN	bDefaultCSI;
};

struct sounding_info {
	u8				sound_idx;
	enum channel_width	sound_bw;
	SOUNDING_MODE	sound_mode;
	u16				sound_period;
};

struct beamforming_info {
	BEAMFORMING_CAP		beamforming_cap;
	BEAMFORMING_STATE		beamforming_state;
	struct beamforming_entry	beamforming_entry[BEAMFORMING_ENTRY_NUM];
	u8						beamforming_cur_idx;
	u8						beamforming_in_progress;
	u8						sounding_sequence;
	struct sounding_info		sounding_info;
};

struct rtw_ndpa_sta_info {
	u16	aid:12;
	u16	feedback_type:1;
	u16	nc_index:3;
};

BEAMFORMING_CAP beamforming_get_entry_beam_cap_by_mac_id(PVOID pmlmepriv , u8 mac_id);
void	beamforming_notify(PADAPTER adapter);
BEAMFORMING_CAP beamforming_get_beamform_cap(struct beamforming_info	*pBeamInfo);

BOOLEAN	beamforming_send_ht_ndpa_packet(PADAPTER Adapter, u8 *ra, enum channel_width bw, u8 qidx);
BOOLEAN	beamforming_send_vht_ndpa_packet(PADAPTER Adapter, u8 *ra, u16 aid, enum channel_width bw, u8 qidx);

void	beamforming_check_sounding_success(PADAPTER Adapter, BOOLEAN status);

void	beamforming_watchdog(PADAPTER Adapter);
#endif /*#if (BEAMFORMING_SUPPORT ==0)- for diver defined beamforming*/

enum BEAMFORMING_CTRL_TYPE {
	BEAMFORMING_CTRL_ENTER = 0,
	BEAMFORMING_CTRL_LEAVE = 1,
	BEAMFORMING_CTRL_START_PERIOD = 2,
	BEAMFORMING_CTRL_END_PERIOD = 3,
	BEAMFORMING_CTRL_SOUNDING_FAIL = 4,
	BEAMFORMING_CTRL_SOUNDING_CLK = 5,
};
u32	rtw_beamforming_get_report_frame(PADAPTER	 Adapter, union recv_frame *precv_frame);
void	rtw_beamforming_get_ndpa_frame(PADAPTER	 Adapter, union recv_frame *precv_frame);

void	beamforming_wk_hdl(_adapter *padapter, u8 type, u8 *pbuf);
u8	beamforming_wk_cmd(_adapter *padapter, s32 type, u8 *pbuf, s32 size, u8 enqueue);
void update_attrib_txbf_info(_adapter *padapter, struct pkt_attrib *pattrib, struct sta_info *psta);

#endif /* !RTW_BEAMFORMING_VERSION_2 */

#endif /*#ifdef CONFIG_BEAMFORMING */

#endif /*__RTW_BEAMFORMING_H_*/
