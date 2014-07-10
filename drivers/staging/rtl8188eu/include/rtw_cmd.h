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
#ifndef __RTW_CMD_H_
#define __RTW_CMD_H_

#include <wlan_bssdef.h>
#include <rtw_rf.h>
#include <rtw_led.h>

#include <osdep_service.h>
#include <ieee80211.h> /*  <ieee80211/ieee80211.h> */

#define MAX_CMDSZ	1024
#define MAX_RSPSZ	512

#define CMDBUFF_ALIGN_SZ 512

struct cmd_obj {
	struct adapter *padapter;
	u16	cmdcode;
	u8	res;
	u8	*parmbuf;
	u32	cmdsz;
	u8	*rsp;
	u32	rspsz;
	struct list_head list;
};

struct cmd_priv {
	struct semaphore cmd_queue_sema;
	struct semaphore terminate_cmdthread_sema;
	struct __queue cmd_queue;
	u8	cmd_seq;
	u8	*cmd_buf;	/* shall be non-paged, and 4 bytes aligned */
	u8	*cmd_allocated_buf;
	u8	*rsp_buf;	/* shall be non-paged, and 4 bytes aligned */
	u8	*rsp_allocated_buf;
	u32	cmd_issued_cnt;
	u32	cmd_done_cnt;
	u32	rsp_cnt;
	u8 cmdthd_running;
	struct adapter *padapter;
};

#define init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code) \
do {\
	INIT_LIST_HEAD(&pcmd->list);\
	pcmd->cmdcode = code;\
	pcmd->parmbuf = (u8 *)(pparm);\
	pcmd->cmdsz = sizeof(*pparm);\
	pcmd->rsp = NULL;\
	pcmd->rspsz = 0;\
} while (0)

u32 rtw_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
struct cmd_obj *rtw_dequeue_cmd(struct cmd_priv *pcmdpriv);
void rtw_free_cmd_obj(struct cmd_obj *pcmd);

int rtw_cmd_thread(void *context);

u32 rtw_init_cmd_priv(struct cmd_priv *pcmdpriv);
void rtw_free_cmd_priv(struct cmd_priv *pcmdpriv);

enum rtw_drvextra_cmd_id {
	NONE_WK_CID,
	DYNAMIC_CHK_WK_CID,
	DM_CTRL_WK_CID,
	PBC_POLLING_WK_CID,
	POWER_SAVING_CTRL_WK_CID,/* IPS,AUTOSuspend */
	LPS_CTRL_WK_CID,
	ANT_SELECT_WK_CID,
	P2P_PS_WK_CID,
	P2P_PROTO_WK_CID,
	CHECK_HIQ_WK_CID,/* for softap mode, check hi queue if empty */
	INTEl_WIDI_WK_CID,
	C2H_WK_CID,
	RTP_TIMER_CFG_WK_CID,
	MAX_WK_CID
};

enum LPS_CTRL_TYPE {
	LPS_CTRL_SCAN = 0,
	LPS_CTRL_JOINBSS = 1,
	LPS_CTRL_CONNECT = 2,
	LPS_CTRL_DISCONNECT = 3,
	LPS_CTRL_SPECIAL_PACKET = 4,
	LPS_CTRL_LEAVE = 5,
};

enum RFINTFS {
	SWSI,
	HWSI,
	HWPI,
};

/*
Caller Mode: Infra, Ad-HoC(C)

Notes: To disconnect the current associated BSS

Command Mode

*/
struct disconnect_parm {
	u32 deauth_timeout_ms;
};

struct	setopmode_parm {
	u8	mode;
	u8	rsvd[3];
};

/*
Caller Mode: AP, Ad-HoC, Infra

Notes: To ask RTL8711 performing site-survey

Command-Event Mode

*/

#define RTW_SSID_SCAN_AMOUNT 9 /*  for WEXT_CSCAN_AMOUNT 9 */
#define RTW_CHANNEL_SCAN_AMOUNT (14+37)
struct sitesurvey_parm {
	int scan_mode;	/* active: 1, passive: 0 */
	u8 ssid_num;
	u8 ch_num;
	struct ndis_802_11_ssid ssid[RTW_SSID_SCAN_AMOUNT];
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
};

/*
Caller Mode: Any

Notes: To set the auth type of RTL8711. open/shared/802.1x

Command Mode

*/
struct setauth_parm {
	u8 mode;  /* 0: legacy open, 1: legacy shared 2: 802.1x */
	u8 _1x;   /* 0: PSK, 1: TLS */
	u8 rsvd[2];
};

/*
Caller Mode: Infra

a. algorithm: wep40, wep104, tkip & aes
b. keytype: grp key/unicast key
c. key contents

when shared key ==> keyid is the camid
when 802.1x ==> keyid [0:1] ==> grp key
when 802.1x ==> keyid > 2 ==> unicast key

*/
struct setkey_parm {
	u8	algorithm;	/* could be none, wep40, TKIP, CCMP, wep104 */
	u8	keyid;
	u8	grpkey;		/* 1: this is the grpkey for 802.1x.
				 * 0: this is the unicast key for 802.1x */
	u8	set_tx;		/* 1: main tx key for wep. 0: other key. */
	u8	key[16];	/* this could be 40 or 104 */
};

/*
When in AP or Ad-Hoc mode, this is used to
allocate an sw/hw entry for a newly associated sta.

Command

when shared key ==> algorithm/keyid

*/
struct set_stakey_parm {
	u8	addr[ETH_ALEN];
	u8	algorithm;
	u8	id;/* currently for erasing cam entry if
		    * algorithm == _NO_PRIVACY_ */
	u8	key[16];
};

struct set_stakey_rsp {
	u8	addr[ETH_ALEN];
	u8	keyid;
	u8	rsvd;
};

/*
Caller Ad-Hoc/AP

Command -Rsp(AID == CAMID) mode

This is to force fw to add an sta_data entry per driver's request.

FW will write an cam entry associated with it.

*/
struct set_assocsta_parm {
	u8	addr[ETH_ALEN];
};

struct set_assocsta_rsp {
	u8	cam_id;
	u8	rsvd[3];
};

/*
	Caller Ad-Hoc/AP

	Command mode

	This is to force fw to del an sta_data entry per driver's request

	FW will invalidate the cam entry associated with it.

*/
struct del_assocsta_parm {
	u8	addr[ETH_ALEN];
};

/*
Caller Mode: AP/Ad-HoC(M)

Notes: To notify fw that given staid has changed its power state

Command Mode

*/
struct setstapwrstate_parm {
	u8	staid;
	u8	status;
	u8	hwaddr[6];
};

/*
Caller Mode: Any

Notes: To setup the basic rate of RTL8711

Command Mode

*/
struct	setbasicrate_parm {
	u8	basicrates[NumRates];
};

/*
Caller Mode: Any

Notes: To read the current basic rate

Command-Rsp Mode

*/
struct getbasicrate_parm {
	u32 rsvd;
};

/*
Caller Mode: Any

Notes: To setup the data rate of RTL8711

Command Mode

*/
struct setdatarate_parm {
	u8	mac_id;
	u8	datarates[NumRates];
};

/*
Caller Mode: Any

Notes: To read the current data rate

Command-Rsp Mode

*/
struct getdatarate_parm {
	u32 rsvd;

};
struct getdatarate_rsp {
	u8 datarates[NumRates];
};

/*
Caller Mode: Any
AP: AP can use the info for the contents of beacon frame
Infra: STA can use the info when sitesurveying
Ad-HoC(M): Like AP
Ad-HoC(C): Like STA

Notes: To set the phy capability of the NIC

Command Mode

*/

struct	setphyinfo_parm {
	struct regulatory_class class_sets[NUM_REGULATORYS];
	u8	status;
};

struct	getphyinfo_parm {
	u32 rsvd;
};

/*
Caller Mode: Any

Notes: To set the channel/modem/band
This command will be used when channel/modem/band is changed.

Command Mode

*/
struct	setphy_parm {
	u8	rfchannel;
	u8	modem;
};

/*
Caller Mode: Any

Notes: To get the current setting of channel/modem/band

Command-Rsp Mode

*/
struct	getphy_parm {
	u32 rsvd;

};

struct Tx_Beacon_param {
	struct wlan_bssid_ex network;
};

/*
	Notes: This command is used for H2C/C2H loopback testing

	mac[0] == 0
	==> CMD mode, return H2C_SUCCESS.
	The following condition must be ture under CMD mode
		mac[1] == mac[4], mac[2] == mac[3], mac[0]=mac[5]= 0;
		s0 == 0x1234, s1 == 0xabcd, w0 == 0x78563412, w1 == 0x5aa5def7;
		s2 == (b1 << 8 | b0);

	mac[0] == 1
	==> CMD_RSP mode, return H2C_SUCCESS_RSP

	The rsp layout shall be:
	rsp:			parm:
		mac[0]  =   mac[5];
		mac[1]  =   mac[4];
		mac[2]  =   mac[3];
		mac[3]  =   mac[2];
		mac[4]  =   mac[1];
		mac[5]  =   mac[0];
		s0		=   s1;
		s1		=   swap16(s0);
		w0		=	swap32(w1);
		b0		=	b1
		s2		=	s0 + s1
		b1		=	b0
		w1		=	w0

	mac[0] ==	2
	==> CMD_EVENT mode, return	H2C_SUCCESS
	The event layout shall be:
	event:			parm:
		mac[0]  =   mac[5];
		mac[1]  =   mac[4];
		mac[2]  =   event's seq no, starting from 1 to parm's marc[3]
		mac[3]  =   mac[2];
		mac[4]  =   mac[1];
		mac[5]  =   mac[0];
		s0		=   swap16(s0) - event.mac[2];
		s1		=   s1 + event.mac[2];
		w0		=	swap32(w0);
		b0		=	b1
		s2		=	s0 + event.mac[2]
		b1		=	b0
		w1		=	swap32(w1) - event.mac[2];

		parm->mac[3] is the total event counts that host requested.
	event will be the same with the cmd's param.
*/

/*  CMD param Format for driver extra cmd handler */
struct drvextra_cmd_parm {
	int ec_id; /* extra cmd id */
	int type_size; /*  Can use this field as the type id or command size */
	unsigned char *pbuf;
};

struct addBaReq_parm {
	unsigned int tid;
	u8	addr[ETH_ALEN];
};

/*H2C Handler index: 46 */
struct set_ch_parm {
	u8 ch;
	u8 bw;
	u8 ch_offset;
};

/*H2C Handler index: 59 */
struct SetChannelPlan_param {
	u8 channel_plan;
};

/*H2C Handler index: 60 */
struct LedBlink_param {
	struct LED_871x *pLed;
};

/*H2C Handler index: 61 */
struct SetChannelSwitch_param {
	u8 new_ch_no;
};

/*H2C Handler index: 62 */
struct TDLSoption_param {
	u8 addr[ETH_ALEN];
	u8 option;
};

#define GEN_CMD_CODE(cmd)	cmd ## _CMD_

/*

Result:
0x00: success
0x01: success, and check Response.
0x02: cmd ignored due to duplicated sequcne number
0x03: cmd dropped due to invalid cmd code
0x04: reserved.

*/

#define H2C_SUCCESS		0x00
#define H2C_SUCCESS_RSP		0x01
#define H2C_DROPPED		0x03
#define H2C_PARAMETERS_ERROR	0x04
#define H2C_REJECTED		0x05

u8 rtw_sitesurvey_cmd(struct adapter *padapter, struct ndis_802_11_ssid *ssid,
		      int ssid_num, struct rtw_ieee80211_channel *ch,
		      int ch_num);
u8 rtw_createbss_cmd(struct adapter *padapter);
u8 rtw_setstakey_cmd(struct adapter *padapter, u8 *psta, u8 unicast_key);
u8 rtw_clearstakey_cmd(struct adapter *padapter, u8 *psta, u8 entry,
		       u8 enqueue);
u8 rtw_joinbss_cmd(struct adapter *padapter, struct wlan_network *pnetwork);
u8 rtw_disassoc_cmd(struct adapter *padapter, u32 deauth_timeout_ms,
		    bool enqueue);
u8 rtw_setopmode_cmd(struct adapter *padapter,
		     enum ndis_802_11_network_infra networktype);
u8 rtw_setdatarate_cmd(struct adapter *padapter, u8 *rateset);

u8 rtw_addbareq_cmd(struct adapter *padapter, u8 tid, u8 *addr);

u8 rtw_dynamic_chk_wk_cmd(struct adapter *adapter);

u8 rtw_lps_ctrl_wk_cmd(struct adapter *padapter, u8 lps_ctrl_type, u8 enqueue);
u8 rtw_rpt_timer_cfg_cmd(struct adapter *padapter, u16 minRptTime);

u8 rtw_antenna_select_cmd(struct adapter *padapter, u8 antenna, u8 enqueue);
u8 rtw_ps_cmd(struct adapter *padapter);

#ifdef CONFIG_88EU_AP_MODE
u8 rtw_chk_hi_queue_cmd(struct adapter *padapter);
#endif

u8 rtw_set_chplan_cmd(struct adapter *padapter, u8 chplan, u8 enqueue);
u8 rtw_drvextra_cmd_hdl(struct adapter *padapter, unsigned char *pbuf);

void rtw_survey_cmd_callback(struct adapter *padapter, struct cmd_obj *pcmd);
void rtw_disassoc_cmd_callback(struct adapter *padapter, struct cmd_obj *pcmd);
void rtw_joinbss_cmd_callback(struct adapter *padapter, struct cmd_obj *pcmd);
void rtw_createbss_cmd_callback(struct adapter *adapt, struct cmd_obj *pcmd);
void rtw_getbbrfreg_cmdrsp_callback(struct adapter *adapt, struct cmd_obj *cmd);
void rtw_readtssi_cmdrsp_callback(struct adapter *adapt, struct cmd_obj *cmd);

void rtw_setstaKey_cmdrsp_callback(struct adapter *adapt, struct cmd_obj *cmd);
void rtw_setassocsta_cmdrsp_callback(struct adapter *adapt, struct cmd_obj *cm);
void rtw_getrttbl_cmdrsp_callback(struct adapter *adapt, struct cmd_obj *cmd);

struct _cmd_callback {
	u32	cmd_code;
	void (*callback)(struct adapter  *padapter, struct cmd_obj *cmd);
};

enum rtw_h2c_cmd {
	GEN_CMD_CODE(_Read_MACREG),	/*0*/
	GEN_CMD_CODE(_Write_MACREG),
	GEN_CMD_CODE(_Read_BBREG),
	GEN_CMD_CODE(_Write_BBREG),
	GEN_CMD_CODE(_Read_RFREG),
	GEN_CMD_CODE(_Write_RFREG), /*5*/
	GEN_CMD_CODE(_Read_EEPROM),
	GEN_CMD_CODE(_Write_EEPROM),
	GEN_CMD_CODE(_Read_EFUSE),
	GEN_CMD_CODE(_Write_EFUSE),

	GEN_CMD_CODE(_Read_CAM),	/*10*/
	GEN_CMD_CODE(_Write_CAM),
	GEN_CMD_CODE(_setBCNITV),
	GEN_CMD_CODE(_setMBIDCFG),
	GEN_CMD_CODE(_JoinBss),   /*14*/
	GEN_CMD_CODE(_DisConnect), /*15*/
	GEN_CMD_CODE(_CreateBss),
	GEN_CMD_CODE(_SetOpMode),
	GEN_CMD_CODE(_SiteSurvey),  /*18*/
	GEN_CMD_CODE(_SetAuth),

	GEN_CMD_CODE(_SetKey),	/*20*/
	GEN_CMD_CODE(_SetStaKey),
	GEN_CMD_CODE(_SetAssocSta),
	GEN_CMD_CODE(_DelAssocSta),
	GEN_CMD_CODE(_SetStaPwrState),
	GEN_CMD_CODE(_SetBasicRate), /*25*/
	GEN_CMD_CODE(_GetBasicRate),
	GEN_CMD_CODE(_SetDataRate),
	GEN_CMD_CODE(_GetDataRate),
	GEN_CMD_CODE(_SetPhyInfo),

	GEN_CMD_CODE(_GetPhyInfo),	/*30*/
	GEN_CMD_CODE(_SetPhy),
	GEN_CMD_CODE(_GetPhy),
	GEN_CMD_CODE(_readRssi),
	GEN_CMD_CODE(_readGain),
	GEN_CMD_CODE(_SetAtim), /*35*/
	GEN_CMD_CODE(_SetPwrMode),
	GEN_CMD_CODE(_JoinbssRpt),
	GEN_CMD_CODE(_SetRaTable),
	GEN_CMD_CODE(_GetRaTable),

	GEN_CMD_CODE(_GetCCXReport), /*40*/
	GEN_CMD_CODE(_GetDTMReport),
	GEN_CMD_CODE(_GetTXRateStatistics),
	GEN_CMD_CODE(_SetUsbSuspend),
	GEN_CMD_CODE(_SetH2cLbk),
	GEN_CMD_CODE(_AddBAReq), /*45*/
	GEN_CMD_CODE(_SetChannel), /*46*/
	GEN_CMD_CODE(_SetTxPower),
	GEN_CMD_CODE(_SwitchAntenna),
	GEN_CMD_CODE(_SetCrystalCap),
	GEN_CMD_CODE(_SetSingleCarrierTx), /*50*/

	GEN_CMD_CODE(_SetSingleToneTx),/*51*/
	GEN_CMD_CODE(_SetCarrierSuppressionTx),
	GEN_CMD_CODE(_SetContinuousTx),
	GEN_CMD_CODE(_SwitchBandwidth), /*54*/
	GEN_CMD_CODE(_TX_Beacon), /*55*/

	GEN_CMD_CODE(_Set_MLME_EVT), /*56*/
	GEN_CMD_CODE(_Set_Drv_Extra), /*57*/
	GEN_CMD_CODE(_Set_H2C_MSG), /*58*/

	GEN_CMD_CODE(_SetChannelPlan), /*59*/
	GEN_CMD_CODE(_LedBlink), /*60*/

	GEN_CMD_CODE(_SetChannelSwitch), /*61*/
	GEN_CMD_CODE(_TDLS), /*62*/

	MAX_H2CCMD
};

#ifdef _RTW_CMD_C_
static struct _cmd_callback	rtw_cmd_callback[] = {
	{GEN_CMD_CODE(_Read_MACREG), NULL}, /*0*/
	{GEN_CMD_CODE(_Write_MACREG), NULL},
	{GEN_CMD_CODE(_Read_BBREG), &rtw_getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_BBREG), NULL},
	{GEN_CMD_CODE(_Read_RFREG), &rtw_getbbrfreg_cmdrsp_callback},
	{GEN_CMD_CODE(_Write_RFREG), NULL}, /*5*/
	{GEN_CMD_CODE(_Read_EEPROM), NULL},
	{GEN_CMD_CODE(_Write_EEPROM), NULL},
	{GEN_CMD_CODE(_Read_EFUSE), NULL},
	{GEN_CMD_CODE(_Write_EFUSE), NULL},

	{GEN_CMD_CODE(_Read_CAM),	NULL},	/*10*/
	{GEN_CMD_CODE(_Write_CAM),	 NULL},
	{GEN_CMD_CODE(_setBCNITV), NULL},
	{GEN_CMD_CODE(_setMBIDCFG), NULL},
	{GEN_CMD_CODE(_JoinBss), &rtw_joinbss_cmd_callback},  /*14*/
	{GEN_CMD_CODE(_DisConnect), &rtw_disassoc_cmd_callback}, /*15*/
	{GEN_CMD_CODE(_CreateBss), &rtw_createbss_cmd_callback},
	{GEN_CMD_CODE(_SetOpMode), NULL},
	{GEN_CMD_CODE(_SiteSurvey), &rtw_survey_cmd_callback}, /*18*/
	{GEN_CMD_CODE(_SetAuth), NULL},

	{GEN_CMD_CODE(_SetKey), NULL},	/*20*/
	{GEN_CMD_CODE(_SetStaKey), &rtw_setstaKey_cmdrsp_callback},
	{GEN_CMD_CODE(_SetAssocSta), &rtw_setassocsta_cmdrsp_callback},
	{GEN_CMD_CODE(_DelAssocSta), NULL},
	{GEN_CMD_CODE(_SetStaPwrState), NULL},
	{GEN_CMD_CODE(_SetBasicRate), NULL}, /*25*/
	{GEN_CMD_CODE(_GetBasicRate), NULL},
	{GEN_CMD_CODE(_SetDataRate), NULL},
	{GEN_CMD_CODE(_GetDataRate), NULL},
	{GEN_CMD_CODE(_SetPhyInfo), NULL},

	{GEN_CMD_CODE(_GetPhyInfo), NULL}, /*30*/
	{GEN_CMD_CODE(_SetPhy), NULL},
	{GEN_CMD_CODE(_GetPhy), NULL},
	{GEN_CMD_CODE(_readRssi), NULL},
	{GEN_CMD_CODE(_readGain), NULL},
	{GEN_CMD_CODE(_SetAtim), NULL}, /*35*/
	{GEN_CMD_CODE(_SetPwrMode), NULL},
	{GEN_CMD_CODE(_JoinbssRpt), NULL},
	{GEN_CMD_CODE(_SetRaTable), NULL},
	{GEN_CMD_CODE(_GetRaTable), NULL},

	{GEN_CMD_CODE(_GetCCXReport), NULL}, /*40*/
	{GEN_CMD_CODE(_GetDTMReport),	NULL},
	{GEN_CMD_CODE(_GetTXRateStatistics), NULL},
	{GEN_CMD_CODE(_SetUsbSuspend), NULL},
	{GEN_CMD_CODE(_SetH2cLbk), NULL},
	{GEN_CMD_CODE(_AddBAReq), NULL}, /*45*/
	{GEN_CMD_CODE(_SetChannel), NULL},		/*46*/
	{GEN_CMD_CODE(_SetTxPower), NULL},
	{GEN_CMD_CODE(_SwitchAntenna), NULL},
	{GEN_CMD_CODE(_SetCrystalCap), NULL},
	{GEN_CMD_CODE(_SetSingleCarrierTx), NULL},	/*50*/

	{GEN_CMD_CODE(_SetSingleToneTx), NULL}, /*51*/
	{GEN_CMD_CODE(_SetCarrierSuppressionTx), NULL},
	{GEN_CMD_CODE(_SetContinuousTx), NULL},
	{GEN_CMD_CODE(_SwitchBandwidth), NULL},		/*54*/
	{GEN_CMD_CODE(_TX_Beacon), NULL},/*55*/

	{GEN_CMD_CODE(_Set_MLME_EVT), NULL},/*56*/
	{GEN_CMD_CODE(_Set_Drv_Extra), NULL},/*57*/
	{GEN_CMD_CODE(_Set_H2C_MSG), NULL},/*58*/
	{GEN_CMD_CODE(_SetChannelPlan), NULL},/*59*/
	{GEN_CMD_CODE(_LedBlink), NULL},/*60*/

	{GEN_CMD_CODE(_SetChannelSwitch), NULL},/*61*/
	{GEN_CMD_CODE(_TDLS), NULL},/*62*/
};
#endif

#endif /*  _CMD_H_ */
