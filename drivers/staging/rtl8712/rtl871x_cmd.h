/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __RTL871X_CMD_H_
#define __RTL871X_CMD_H_

#include "wlan_bssdef.h"
#include "rtl871x_rf.h"
#define C2H_MEM_SZ (16*1024)

#include "osdep_service.h"
#include "ieee80211.h"

#define FREE_CMDOBJ_SZ	128
#define MAX_CMDSZ	512
#define MAX_RSPSZ	512
#define MAX_EVTSZ	1024
#define CMDBUFF_ALIGN_SZ 512

struct cmd_obj {
	u16	cmdcode;
	u8	res;
	u8	*parmbuf;
	u32	cmdsz;
	u8	*rsp;
	u32	rspsz;
	struct list_head list;
};

struct cmd_priv {
	struct completion cmd_queue_comp;
	struct completion terminate_cmdthread_comp;
	struct  __queue	cmd_queue;
	u8 cmd_seq;
	u8 *cmd_buf;	/*shall be non-paged, and 4 bytes aligned*/
	u8 *cmd_allocated_buf;
	u8 *rsp_buf;	/*shall be non-paged, and 4 bytes aligned*/
	u8 *rsp_allocated_buf;
	u32 cmd_issued_cnt;
	u32 cmd_done_cnt;
	u32 rsp_cnt;
	struct _adapter *padapter;
};

struct evt_obj {
	u16 evtcode;
	u8 res;
	u8 *parmbuf;
	u32 evtsz;
	struct list_head list;
};

struct	evt_priv {
	struct  __queue	evt_queue;
	u8	event_seq;
	u8	*evt_buf;	/*shall be non-paged, and 4 bytes aligned*/
	u8	*evt_allocated_buf;
	u32	evt_done_cnt;
	struct tasklet_struct event_tasklet;
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

u32 r8712_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
u32 r8712_enqueue_cmd_ex(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
struct cmd_obj *r8712_dequeue_cmd(struct  __queue *queue);
void r8712_free_cmd_obj(struct cmd_obj *pcmd);
int r8712_cmd_thread(void *context);
u32 r8712_init_cmd_priv(struct cmd_priv *pcmdpriv);
void r8712_free_cmd_priv(struct cmd_priv *pcmdpriv);
u32 r8712_init_evt_priv(struct evt_priv *pevtpriv);
void r8712_free_evt_priv(struct evt_priv *pevtpriv);

enum rtl871x_drvint_cid {
	NONE_WK_CID,
	WDG_WK_CID,
	MAX_WK_CID
};

enum RFINTFS {
	SWSI,
	HWSI,
	HWPI,
};

/*
 * Caller Mode: Infra, Ad-HoC(C)
 * Notes: To enter USB suspend mode
 * Command Mode
 */
struct usb_suspend_parm {
	u32 action; /* 1: sleep, 0:resume */
};

/*
 * Caller Mode: Infra, Ad-HoC(C)
 * Notes: To disconnect the current associated BSS
 * Command Mode
 */
struct disconnect_parm {
	u32 rsvd;
};

/*
 * Caller Mode: AP, Ad-HoC, Infra
 * Notes: To set the NIC mode of RTL8711
 * Command Mode
 * The definition of mode:
 *
 * #define IW_MODE_AUTO	0	// Let the driver decides which AP to join
 * #define IW_MODE_ADHOC	1	// Single cell network (Ad-Hoc Clients)
 * #define IW_MODE_INFRA	2	// Multi cell network, roaming, ..
 * #define IW_MODE_MASTER	3	// Synchronisation master or AP
 * #define IW_MODE_REPEAT	4	// Wireless Repeater (forwarder)
 * #define IW_MODE_SECOND	5	// Secondary master/repeater (backup)
 * #define IW_MODE_MONITOR	6	// Passive monitor (listen only)
 */
struct	setopmode_parm {
	u8	mode;
	u8	rsvd[3];
};

/*
 * Caller Mode: AP, Ad-HoC, Infra
 * Notes: To ask RTL8711 performing site-survey
 * Command-Event Mode
 */
struct sitesurvey_parm {
	__le32	passive_mode;	/*active: 1, passive: 0 */
	__le32	bsslimit;	/* 1 ~ 48 */
	__le32	ss_ssidlen;
	u8	ss_ssid[IW_ESSID_MAX_SIZE + 1];
};

/*
 * Caller Mode: Any
 * Notes: To set the auth type of RTL8711. open/shared/802.1x
 * Command Mode
 */
struct setauth_parm {
	u8 mode;  /*0: legacy open, 1: legacy shared 2: 802.1x*/
	u8 _1x;   /*0: PSK, 1: TLS*/
	u8 rsvd[2];
};

/*
 * Caller Mode: Infra
 * a. algorithm: wep40, wep104, tkip & aes
 * b. keytype: grp key/unicast key
 * c. key contents
 *
 * when shared key ==> keyid is the camid
 * when 802.1x ==> keyid [0:1] ==> grp key
 * when 802.1x ==> keyid > 2 ==> unicast key
 */
struct setkey_parm {
	u8	algorithm;	/* encryption algorithm, could be none, wep40,
				 * TKIP, CCMP, wep104
				 */
	u8	keyid;
	u8	grpkey;		/* 1: this is the grpkey for 802.1x.
				 * 0: this is the unicast key for 802.1x
				 */
	u8	key[16];	/* this could be 40 or 104 */
};

/*
 * When in AP or Ad-Hoc mode, this is used to
 * allocate an sw/hw entry for a newly associated sta.
 * Command
 * when shared key ==> algorithm/keyid
 */
struct set_stakey_parm {
	u8	addr[ETH_ALEN];
	u8	algorithm;
	u8	key[16];
};

struct set_stakey_rsp {
	u8	addr[ETH_ALEN];
	u8	keyid;
	u8	rsvd;
};

struct SetMacAddr_param {
	u8	MacAddr[ETH_ALEN];
};

/*
 *	Caller Ad-Hoc/AP
 *
 *	Command -Rsp(AID == CAMID) mode
 *
 *	This is to force fw to add an sta_data entry per driver's request.
 *
 *	FW will write an cam entry associated with it.
 *
 */
struct set_assocsta_parm {
	u8	addr[ETH_ALEN];
};

struct set_assocsta_rsp {
	u8	cam_id;
	u8	rsvd[3];
};

/*
 *	Caller Ad-Hoc/AP
 *
 *	Command mode
 *
 *	This is to force fw to del an sta_data entry per driver's request
 *
 *	FW will invalidate the cam entry associated with it.
 *
 */
struct del_assocsta_parm {
	u8	addr[ETH_ALEN];
};

/*
 *	Caller Mode: AP/Ad-HoC(M)
 *
 *	Notes: To notify fw that given staid has changed its power state
 *
 *	Command Mode
 *
 */
struct setstapwrstate_parm {
	u8	staid;
	u8	status;
	u8	hwaddr[6];
};

/*
 *	Caller Mode: Any
 *
 *	Notes: To setup the basic rate of RTL8711
 *
 *	Command Mode
 *
 */
struct	setbasicrate_parm {
	u8	basicrates[NumRates];
};

/*
 *	Caller Mode: Any
 *
 *	Notes: To read the current basic rate
 *
 *	Command-Rsp Mode
 *
 */
struct getbasicrate_parm {
	u32 rsvd;
};

struct getbasicrate_rsp {
	u8 basicrates[NumRates];
};

/*
 *	Caller Mode: Any
 *
 *	Notes: To setup the data rate of RTL8711
 *
 *	Command Mode
 *
 */
struct setdatarate_parm {
	u8	mac_id;
	u8	datarates[NumRates];
};

enum _RT_CHANNEL_DOMAIN {
	RT_CHANNEL_DOMAIN_FCC = 0,
	RT_CHANNEL_DOMAIN_IC = 1,
	RT_CHANNEL_DOMAIN_ETSI = 2,
	RT_CHANNEL_DOMAIN_SPAIN = 3,
	RT_CHANNEL_DOMAIN_FRANCE = 4,
	RT_CHANNEL_DOMAIN_MKK = 5,
	RT_CHANNEL_DOMAIN_MKK1 = 6,
	RT_CHANNEL_DOMAIN_ISRAEL = 7,
	RT_CHANNEL_DOMAIN_TELEC = 8,

	/* Be compatible with old channel plan. No good! */
	RT_CHANNEL_DOMAIN_MIC = 9,
	RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN = 10,
	RT_CHANNEL_DOMAIN_WORLD_WIDE_13 = 11,
	RT_CHANNEL_DOMAIN_TELEC_NETGEAR = 12,

	RT_CHANNEL_DOMAIN_NCC = 13,
	RT_CHANNEL_DOMAIN_5G = 14,
	RT_CHANNEL_DOMAIN_5G_40M = 15,
 /*===== Add new channel plan above this line===============*/
	RT_CHANNEL_DOMAIN_MAX,
};


struct SetChannelPlan_param {
	enum _RT_CHANNEL_DOMAIN ChannelPlan;
};

/*
 *	Caller Mode: Any
 *
 *	Notes: To read the current data rate
 *
 *	Command-Rsp Mode
 *
 */
struct getdatarate_parm {
	u32 rsvd;

};
struct getdatarate_rsp {
	u8 datarates[NumRates];
};


/*
 *	Caller Mode: Any
 *	AP: AP can use the info for the contents of beacon frame
 *	Infra: STA can use the info when sitesurveying
 *	Ad-HoC(M): Like AP
 *	Ad-HoC(C): Like STA
 *
 *
 *	Notes: To set the phy capability of the NIC
 *
 *	Command Mode
 *
 */

/*
 *	Caller Mode: Any
 *
 *	Notes: To set the channel/modem/band
 *	This command will be used when channel/modem/band is changed.
 *
 *	Command Mode
 *
 */
/*
 *	Caller Mode: Any
 *
 *	Notes: To get the current setting of channel/modem/band
 *
 *	Command-Rsp Mode
 *
 */
struct	getphy_rsp {
	u8	rfchannel;
	u8	modem;
};

struct readBB_parm {
	u8	offset;
};
struct readBB_rsp {
	u8	value;
};

struct readTSSI_parm {
	u8	offset;
};
struct readTSSI_rsp {
	u8	value;
};

struct writeBB_parm {
	u8	offset;
	u8	value;
};

struct writePTM_parm {
	u8	type;
};

struct readRF_parm {
	u8	offset;
};
struct readRF_rsp {
	u32	value;
};

struct writeRF_parm {
	u32	offset;
	u32	value;
};

struct setrfintfs_parm {
	u8	rfintfs;
};

struct getrfintfs_parm {
	u8	rfintfs;
};

/*
 *	Notes: This command is used for H2C/C2H loopback testing
 *
 *	mac[0] == 0
 *	==> CMD mode, return H2C_SUCCESS.
 *	The following condition must be ture under CMD mode
 *		mac[1] == mac[4], mac[2] == mac[3], mac[0]=mac[5]= 0;
 *		s0 == 0x1234, s1 == 0xabcd, w0 == 0x78563412, w1 == 0x5aa5def7;
 *		s2 == (b1 << 8 | b0);
 *
 *	mac[0] == 1
 *	==> CMD_RSP mode, return H2C_SUCCESS_RSP
 *
 *	The rsp layout shall be:
 *	rsp:			parm:
 *		mac[0]  =   mac[5];
 *		mac[1]  =   mac[4];
 *		mac[2]  =   mac[3];
 *		mac[3]  =   mac[2];
 *		mac[4]  =   mac[1];
 *		mac[5]  =   mac[0];
 *		s0		=   s1;
 *		s1		=   swap16(s0);
 *		w0		=	swap32(w1);
 *		b0		=	b1
 *		s2		=	s0 + s1
 *		b1		=	b0
 *		w1		=	w0
 *
 *	mac[0] ==	2
 *	==> CMD_EVENT mode, return	H2C_SUCCESS
 *	The event layout shall be:
 *	event:	     parm:
 *	mac[0]  =   mac[5];
 *	mac[1]  =   mac[4];
 *	mac[2]  =   event's sequence number, starting from 1 to parm's marc[3]
 *	mac[3]  =   mac[2];
 *	mac[4]  =   mac[1];
 *	mac[5]  =   mac[0];
 *	s0		=   swap16(s0) - event.mac[2];
 *	s1		=   s1 + event.mac[2];
 *	w0		=	swap32(w0);
 *	b0		=	b1
 *	s2		=	s0 + event.mac[2]
 *	b1		=	b0
 *	w1		=	swap32(w1) - event.mac[2];
 *
 *	parm->mac[3] is the total event counts that host requested.
 *
 *
 *	event will be the same with the cmd's param.
 *
 */

/* CMD param Formart for DRV INTERNAL CMD HDL*/
struct drvint_cmd_parm {
	int i_cid; /*internal cmd id*/
	int sz; /* buf sz*/
	unsigned char *pbuf;
};

/*------------------- Below are used for RF/BB tunning ---------------------*/

struct	setantenna_parm {
	u8	tx_antset;
	u8	rx_antset;
	u8	tx_antenna;
	u8	rx_antenna;
};

struct	enrateadaptive_parm {
	u32	en;
};

struct settxagctbl_parm {
	u32	txagc[MAX_RATES_LENGTH];
};

struct gettxagctbl_parm {
	u32 rsvd;
};
struct gettxagctbl_rsp {
	u32	txagc[MAX_RATES_LENGTH];
};

struct setagcctrl_parm {
	u32	agcctrl;	/* 0: pure hw, 1: fw */
};

struct setssup_parm	{
	u32	ss_ForceUp[MAX_RATES_LENGTH];
};

struct getssup_parm	{
	u32 rsvd;
};
struct getssup_rsp	{
	u8	ss_ForceUp[MAX_RATES_LENGTH];
};

struct setssdlevel_parm	{
	u8	ss_DLevel[MAX_RATES_LENGTH];
};

struct getssdlevel_parm	{
	u32 rsvd;
};
struct getssdlevel_rsp	{
	u8	ss_DLevel[MAX_RATES_LENGTH];
};

struct setssulevel_parm	{
	u8	ss_ULevel[MAX_RATES_LENGTH];
};

struct getssulevel_parm	{
	u32 rsvd;
};
struct getssulevel_rsp	{
	u8	ss_ULevel[MAX_RATES_LENGTH];
};

struct	setcountjudge_parm {
	u8	count_judge[MAX_RATES_LENGTH];
};

struct	getcountjudge_parm {
	u32 rsvd;
};

struct	getcountjudge_rsp {
	u8	count_judge[MAX_RATES_LENGTH];
};

struct setpwrmode_parm  {
	u8	mode;
	u8	flag_low_traffic_en;
	u8	flag_lpnav_en;
	u8	flag_rf_low_snr_en;
	u8	flag_dps_en; /* 1: dps, 0: 32k */
	u8	bcn_rx_en;
	u8	bcn_pass_cnt;	  /* fw report one beacon information to
				   * driver  when it receives bcn_pass_cnt
				   * beacons.
				   */
	u8	bcn_to;		  /* beacon TO (ms). ¡§=0¡¨ no limit.*/
	u16	bcn_itv;
	u8	app_itv; /* only for VOIP mode. */
	u8	awake_bcn_itv;
	u8	smart_ps;
	u8	bcn_pass_time;	/* unit: 100ms */
};

struct setatim_parm {
	u8 op;   /*0: add, 1:del*/
	u8 txid; /* id of dest station.*/
};

struct setratable_parm {
	u8 ss_ForceUp[NumRates];
	u8 ss_ULevel[NumRates];
	u8 ss_DLevel[NumRates];
	u8 count_judge[NumRates];
};

struct getratable_parm {
	uint rsvd;
};
struct getratable_rsp {
	u8 ss_ForceUp[NumRates];
	u8 ss_ULevel[NumRates];
	u8 ss_DLevel[NumRates];
	u8 count_judge[NumRates];
};

/*to get TX,RX retry count*/
struct gettxretrycnt_parm {
	unsigned int rsvd;
};

struct gettxretrycnt_rsp {
	unsigned long tx_retrycnt;
};

struct getrxretrycnt_parm {
	unsigned int rsvd;
};

struct getrxretrycnt_rsp {
	unsigned long rx_retrycnt;
};

/*to get BCNOK,BCNERR count*/
struct getbcnokcnt_parm {
	unsigned int rsvd;
};

struct getbcnokcnt_rsp {
	unsigned long bcnokcnt;
};

struct getbcnerrcnt_parm {
	unsigned int rsvd;
};
struct getbcnerrcnt_rsp {
	unsigned long bcnerrcnt;
};

/* to get current TX power level*/
struct getcurtxpwrlevel_parm {
	unsigned int rsvd;
};

struct getcurtxpwrlevel_rsp {
	unsigned short tx_power;
};

/*dynamic on/off DIG*/
struct setdig_parm {
	unsigned char dig_on;	/* 1:on , 0:off */
};

/*dynamic on/off RA*/
struct setra_parm {
	unsigned char ra_on;	/* 1:on , 0:off */
};

struct setprobereqextraie_parm {
	unsigned char e_id;
	unsigned char ie_len;
	unsigned char ie[0];
};

struct setassocreqextraie_parm {
	unsigned char e_id;
	unsigned char ie_len;
	unsigned char ie[0];
};

struct setproberspextraie_parm {
	unsigned char e_id;
	unsigned char ie_len;
	unsigned char ie[0];
};

struct setassocrspextraie_parm {
	unsigned char e_id;
	unsigned char ie_len;
	unsigned char ie[0];
};

struct addBaReq_parm {
	unsigned int tid;
};

/*H2C Handler index: 46 */
struct SetChannel_parm {
	u32 curr_ch;
};

/*H2C Handler index: 61 */
struct DisconnectCtrlEx_param {
	/* MAXTIME = (2 * FirstStageTO) + (TryPktCnt * TryPktInterval) */
	unsigned char EnableDrvCtrl;
	unsigned char TryPktCnt;
	unsigned char TryPktInterval; /* Unit: ms */
	unsigned char rsvd;
	unsigned int  FirstStageTO; /* Unit: ms */
};

#define GEN_CMD_CODE(cmd)	cmd ## _CMD_

/*
 * Result:
 * 0x00: success
 * 0x01: success, and check Response.
 * 0x02: cmd ignored due to duplicated sequence number
 * 0x03: cmd dropped due to invalid cmd code
 * 0x04: reserved.
 */

#define H2C_RSP_OFFSET			512
#define H2C_SUCCESS			0x00
#define H2C_SUCCESS_RSP			0x01
#define H2C_DUPLICATED			0x02
#define H2C_DROPPED			0x03
#define H2C_PARAMETERS_ERROR		0x04
#define H2C_REJECTED			0x05
#define H2C_CMD_OVERFLOW		0x06
#define H2C_RESERVED			0x07

u8 r8712_setMacAddr_cmd(struct _adapter *padapter, u8 *mac_addr);
u8 r8712_setassocsta_cmd(struct _adapter *padapter, u8 *mac_addr);
u8 r8712_sitesurvey_cmd(struct _adapter *padapter,
			struct ndis_802_11_ssid *pssid);
u8 r8712_createbss_cmd(struct _adapter *padapter);
u8 r8712_setstakey_cmd(struct _adapter *padapter, u8 *psta, u8 unicast_key);
u8 r8712_joinbss_cmd(struct _adapter *padapter,
		     struct wlan_network *pnetwork);
u8 r8712_disassoc_cmd(struct _adapter *padapter);
u8 r8712_setopmode_cmd(struct _adapter *padapter,
		 enum NDIS_802_11_NETWORK_INFRASTRUCTURE networktype);
u8 r8712_setdatarate_cmd(struct _adapter *padapter, u8 *rateset);
u8 r8712_set_chplan_cmd(struct _adapter  *padapter, int chplan);
u8 r8712_setbasicrate_cmd(struct _adapter *padapter, u8 *rateset);
u8 r8712_getrfreg_cmd(struct _adapter *padapter, u8 offset, u8 *pval);
u8 r8712_setrfintfs_cmd(struct _adapter *padapter, u8 mode);
u8 r8712_setrfreg_cmd(struct _adapter  *padapter, u8 offset, u32 val);
u8 r8712_setrttbl_cmd(struct _adapter  *padapter,
		      struct setratable_parm *prate_table);
u8 r8712_setfwdig_cmd(struct _adapter *padapter, u8 type);
u8 r8712_setfwra_cmd(struct _adapter *padapter, u8 type);
u8 r8712_addbareq_cmd(struct _adapter *padapter, u8 tid);
u8 r8712_wdg_wk_cmd(struct _adapter *padapter);
void r8712_survey_cmd_callback(struct _adapter  *padapter,
			       struct cmd_obj *pcmd);
void r8712_disassoc_cmd_callback(struct _adapter  *padapter,
				 struct cmd_obj *pcmd);
void r8712_joinbss_cmd_callback(struct _adapter  *padapter,
				struct cmd_obj *pcmd);
void r8712_createbss_cmd_callback(struct _adapter *padapter,
				  struct cmd_obj *pcmd);
void r8712_getbbrfreg_cmdrsp_callback(struct _adapter *padapter,
				      struct cmd_obj *pcmd);
void r8712_readtssi_cmdrsp_callback(struct _adapter *padapter,
				struct cmd_obj *pcmd);
void r8712_setstaKey_cmdrsp_callback(struct _adapter  *padapter,
				     struct cmd_obj *pcmd);
void r8712_setassocsta_cmdrsp_callback(struct _adapter  *padapter,
				       struct cmd_obj *pcmd);
u8 r8712_disconnectCtrlEx_cmd(struct _adapter *adapter, u32 enableDrvCtrl,
			u32 tryPktCnt, u32 tryPktInterval, u32 firstStageTO);

struct _cmd_callback {
	u32	cmd_code;
	void (*callback)(struct _adapter  *padapter, struct cmd_obj *cmd);
};

#include "rtl8712_cmd.h"

#endif /* _CMD_H_ */

