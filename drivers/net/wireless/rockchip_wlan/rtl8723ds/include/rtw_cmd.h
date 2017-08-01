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


#define C2H_MEM_SZ (16*1024)

#ifndef CONFIG_RTL8711FW

#define FREE_CMDOBJ_SZ	128

#define MAX_CMDSZ	1024
#define MAX_RSPSZ	512
#define MAX_EVTSZ	1024

#ifdef PLATFORM_OS_CE
	#define CMDBUFF_ALIGN_SZ 4
#else
	#define CMDBUFF_ALIGN_SZ 512
#endif

struct cmd_obj {
	_adapter *padapter;
	u16	cmdcode;
	u8	res;
	u8	*parmbuf;
	u32	cmdsz;
	u8	*rsp;
	u32	rspsz;
	struct submit_ctx *sctx;
	u8 no_io;
	/* _sema 	cmd_sem; */
	_list	list;
};

/* cmd flags */
enum {
	RTW_CMDF_DIRECTLY = BIT0,
	RTW_CMDF_WAIT_ACK = BIT1,
};

struct cmd_priv {
	_sema	cmd_queue_sema;
	/* _sema	cmd_done_sema; */
	_sema	terminate_cmdthread_sema;
	_queue	cmd_queue;
	u8	cmd_seq;
	u8	*cmd_buf;	/* shall be non-paged, and 4 bytes aligned */
	u8	*cmd_allocated_buf;
	u8	*rsp_buf;	/* shall be non-paged, and 4 bytes aligned		 */
	u8	*rsp_allocated_buf;
	u32	cmd_issued_cnt;
	u32	cmd_done_cnt;
	u32	rsp_cnt;
	ATOMIC_T cmdthd_running;
	/* u8 cmdthd_running; */
	u8 stop_req;
	_adapter *padapter;
	_mutex sctx_mutex;
};

#ifdef CONFIG_EVENT_THREAD_MODE
struct evt_obj {
	u16	evtcode;
	u8	res;
	u8	*parmbuf;
	u32	evtsz;
	_list	list;
};
#endif

struct	evt_priv {
#ifdef CONFIG_EVENT_THREAD_MODE
	_sema	evt_notify;
	_sema	terminate_evtthread_sema;
	_queue	evt_queue;
#endif

#ifdef CONFIG_FW_C2H_REG
	#define CONFIG_C2H_WK
#endif

#ifdef CONFIG_C2H_WK
	_workitem c2h_wk;
	bool c2h_wk_alive;
	struct rtw_cbuf *c2h_queue;
	#define C2H_QUEUE_MAX_LEN 10
#endif

#ifdef CONFIG_H2CLBK
	_sema	lbkevt_done;
	u8	lbkevt_limit;
	u8	lbkevt_num;
	u8	*cmdevt_parm;
#endif
	ATOMIC_T event_seq;
	u8	*evt_buf;	/* shall be non-paged, and 4 bytes aligned		 */
	u8	*evt_allocated_buf;
	u32	evt_done_cnt;
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	u8	*c2h_mem;
	u8	*allocated_c2h_mem;
#ifdef PLATFORM_OS_XP
	PMDL	pc2h_mdl;
#endif
#endif

};

#define init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code) \
	do {\
		_rtw_init_listhead(&pcmd->list);\
		pcmd->cmdcode = code;\
		pcmd->parmbuf = (u8 *)(pparm);\
		pcmd->cmdsz = sizeof (*pparm);\
		pcmd->rsp = NULL;\
		pcmd->rspsz = 0;\
	} while (0)

#define init_h2fwcmd_w_parm_no_parm_rsp(pcmd, code) \
	do {\
		_rtw_init_listhead(&pcmd->list);\
		pcmd->cmdcode = code;\
		pcmd->parmbuf = NULL;\
		pcmd->cmdsz = 0;\
		pcmd->rsp = NULL;\
		pcmd->rspsz = 0;\
	} while (0)

struct P2P_PS_Offload_t {
	u8 Offload_En:1;
	u8 role:1; /* 1: Owner, 0: Client */
	u8 CTWindow_En:1;
	u8 NoA0_En:1;
	u8 NoA1_En:1;
	u8 AllStaSleep:1; /* Only valid in Owner */
	u8 discovery:1;
	u8 rsvd:1;
};

struct P2P_PS_CTWPeriod_t {
	u8 CTWPeriod;	/* TU */
};

#ifdef CONFIG_P2P_WOWLAN

struct P2P_WoWlan_Offload_t {
	u8 Disconnect_Wkup_Drv:1;
	u8 role:2;
	u8 Wps_Config[2];
};

#endif /* CONFIG_P2P_WOWLAN */

extern u32 rtw_enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
extern struct cmd_obj *rtw_dequeue_cmd(struct cmd_priv *pcmdpriv);
extern void rtw_free_cmd_obj(struct cmd_obj *pcmd);

#ifdef CONFIG_EVENT_THREAD_MODE
extern u32 rtw_enqueue_evt(struct evt_priv *pevtpriv, struct evt_obj *obj);
extern struct evt_obj *rtw_dequeue_evt(_queue *queue);
extern void rtw_free_evt_obj(struct evt_obj *pcmd);
#endif

void rtw_stop_cmd_thread(_adapter *adapter);
thread_return rtw_cmd_thread(thread_context context);

extern u32 rtw_init_cmd_priv(struct cmd_priv *pcmdpriv);
extern void rtw_free_cmd_priv(struct cmd_priv *pcmdpriv);

extern u32 rtw_init_evt_priv(struct evt_priv *pevtpriv);
extern void rtw_free_evt_priv(struct evt_priv *pevtpriv);
extern void rtw_cmd_clr_isr(struct cmd_priv *pcmdpriv);
extern void rtw_evt_notify_isr(struct evt_priv *pevtpriv);
#ifdef CONFIG_P2P
u8 p2p_protocol_wk_cmd(_adapter *padapter, int intCmdType);

#ifdef CONFIG_IOCTL_CFG80211
struct p2p_roch_parm {
	u64 cookie;
	struct wireless_dev *wdev;
	struct ieee80211_channel ch;
	enum nl80211_channel_type ch_type;
	unsigned int duration;
};

u8 p2p_roch_cmd(_adapter *adapter
	, u64 cookie, struct wireless_dev *wdev
	, struct ieee80211_channel *ch, enum nl80211_channel_type ch_type
	, unsigned int duration
	, u8 flags
);
u8 p2p_cancel_roch_cmd(_adapter *adapter, u64 cookie, struct wireless_dev *wdev, u8 flags);
#endif /* CONFIG_IOCTL_CFG80211 */
#endif /* CONFIG_P2P */

#else
/* #include <ieee80211.h> */
#endif	/* CONFIG_RTL8711FW */

enum rtw_drvextra_cmd_id {
	NONE_WK_CID,
	STA_MSTATUS_RPT_WK_CID,
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
	RESET_SECURITYPRIV, /* add for CONFIG_IEEE80211W, none 11w also can use */
	FREE_ASSOC_RESOURCES, /* add for CONFIG_IEEE80211W, none 11w also can use */
	DM_IN_LPS_WK_CID,
	DM_RA_MSK_WK_CID, /* add for STA update RAMask when bandwith change. */
	BEAMFORMING_WK_CID,
	LPS_CHANGE_DTIM_CID,
	BTINFO_WK_CID,
	DFS_MASTER_WK_CID,
	SESSION_TRACKER_WK_CID,
	EN_HW_UPDATE_TSF_WK_CID,
	TEST_H2C_CID,
	CUSTOMER_STR_WK_CID,
	MAX_WK_CID
};

enum LPS_CTRL_TYPE {
	LPS_CTRL_SCAN = 0,
	LPS_CTRL_JOINBSS = 1,
	LPS_CTRL_CONNECT = 2,
	LPS_CTRL_DISCONNECT = 3,
	LPS_CTRL_SPECIAL_PACKET = 4,
	LPS_CTRL_LEAVE = 5,
	LPS_CTRL_TRAFFIC_BUSY = 6,
	LPS_CTRL_TX_TRAFFIC_LEAVE = 7,
	LPS_CTRL_RX_TRAFFIC_LEAVE = 8,
	LPS_CTRL_ENTER = 9,
	LPS_CTRL_LEAVE_CFG80211_PWRMGMT = 10,
};

enum STAKEY_TYPE {
	GROUP_KEY		= 0,
	UNICAST_KEY		= 1,
	TDLS_KEY		= 2,
};

enum RFINTFS {
	SWSI,
	HWSI,
	HWPI,
};

/*
Caller Mode: Infra, Ad-HoC(C)

Notes: To enter USB suspend mode

Command Mode

*/
struct usb_suspend_parm {
	u32 action;/* 1: sleep, 0:resume */
};

/*
Caller Mode: Infra, Ad-HoC

Notes: To join a known BSS.

Command-Event Mode

*/

/*
Caller Mode: Infra, Ad-Hoc

Notes: To join the specified bss

Command Event Mode

*/
struct joinbss_parm {
	WLAN_BSSID_EX network;
};

/*
Caller Mode: Infra, Ad-HoC(C)

Notes: To disconnect the current associated BSS

Command Mode

*/
struct disconnect_parm {
	u32 deauth_timeout_ms;
};

/*
Caller Mode: AP, Ad-HoC(M)

Notes: To create a BSS

Command Mode
*/
struct createbss_parm {
	bool adhoc;

	/* used by AP mode now */
	s16 req_ch;
	s8 req_bw;
	s8 req_offset;
};

#if 0
/* Caller Mode: AP, Ad-HoC, Infra */
/* Notes: To set the NIC mode of RTL8711 */
/* Command Mode */
/* The definition of mode: */

#define IW_MODE_AUTO	0	/*  Let the driver decides which AP to join */
#define IW_MODE_ADHOC	1	/*  Single cell network (Ad-Hoc Clients) */
#define IW_MODE_INFRA	2	/*  Multi cell network, roaming, .. */
#define IW_MODE_MASTER	3	/*  Synchronisation master or Access Point */
#define IW_MODE_REPEAT	4	/*  Wireless Repeater (forwarder) */
#define IW_MODE_SECOND	5	/*  Secondary master/repeater (backup) */
#define IW_MODE_MONITOR	6	/*  Passive monitor (listen only) */
#endif

struct	setopmode_parm {
	u8	mode;
	u8	rsvd[3];
};

/*
Caller Mode: AP, Ad-HoC, Infra

Notes: To ask RTL8711 performing site-survey

Command-Event Mode

*/

#define RTW_SSID_SCAN_AMOUNT 9 /* for WEXT_CSCAN_AMOUNT 9 */
#define RTW_CHANNEL_SCAN_AMOUNT (14+37)
struct sitesurvey_parm {
	sint scan_mode;	/* active: 1, passive: 0 */
	/* sint bsslimit;	// 1 ~ 48 */
	u8 ssid_num;
	u8 ch_num;
	NDIS_802_11_SSID ssid[RTW_SSID_SCAN_AMOUNT];
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
	u8	algorithm;	/* encryption algorithm, could be none, wep40, TKIP, CCMP, wep104 */
	u8	keyid;
	u8	grpkey;		/* 1: this is the grpkey for 802.1x. 0: this is the unicast key for 802.1x */
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
	u8	keyid;
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

struct getbasicrate_rsp {
	u8 basicrates[NumRates];
};

/*
Caller Mode: Any

Notes: To setup the data rate of RTL8711

Command Mode

*/
struct setdatarate_parm {
#ifdef MP_FIRMWARE_OFFLOAD
	u32	curr_rateidx;
#else
	u8	mac_id;
	u8	datarates[NumRates];
#endif
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

struct	getphyinfo_rsp {
	struct regulatory_class class_sets[NUM_REGULATORYS];
	u8	status;
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

struct readMAC_parm {
	u8 len;
	u32	addr;
};

struct writeBB_parm {
	u8	offset;
	u8	value;
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

struct getrfintfs_parm {
	u8	rfintfs;
};


struct Tx_Beacon_param {
	WLAN_BSSID_EX network;
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
		mac[2]  =   event's sequence number, starting from 1 to parm's marc[3]
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

#ifdef CONFIG_H2CLBK

struct seth2clbk_parm {
	u8 mac[6];
	u16	s0;
	u16	s1;
	u32	w0;
	u8	b0;
	u16  s2;
	u8	b1;
	u32	w1;
};

struct geth2clbk_parm {
	u32 rsv;
};

struct geth2clbk_rsp {
	u8	mac[6];
	u16	s0;
	u16	s1;
	u32	w0;
	u8	b0;
	u16	s2;
	u8	b1;
	u32	w1;
};

#endif	/* CONFIG_H2CLBK */

/* CMD param Formart for driver extra cmd handler */
struct drvextra_cmd_parm {
	int ec_id; /* extra cmd id */
	int type; /* Can use this field as the type id or command size */
	int size; /* buffer size */
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
	u32	agcctrl;		/* 0: pure hw, 1: fw */
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


/* to get TX,RX retry count */
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

/* to get BCNOK,BCNERR count */
struct getbcnokcnt_parm {
	unsigned int rsvd;
};
struct getbcnokcnt_rsp {
	unsigned long  bcnokcnt;
};

struct getbcnerrcnt_parm {
	unsigned int rsvd;
};
struct getbcnerrcnt_rsp {
	unsigned long bcnerrcnt;
};

/* to get current TX power level */
struct getcurtxpwrlevel_parm {
	unsigned int rsvd;
};
struct getcurtxpwrlevel_rsp {
	unsigned short tx_power;
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
	u8	addr[ETH_ALEN];
};

struct addBaRsp_parm {
	unsigned int tid;
	unsigned int start_seq;
	u8 addr[ETH_ALEN];
	u8 status;
	u8 size;
};

/*H2C Handler index: 46 */
struct set_ch_parm {
	u8 ch;
	u8 bw;
	u8 ch_offset;
};

#ifdef MP_FIRMWARE_OFFLOAD
/*H2C Handler index: 47 */
struct SetTxPower_parm {
	u8 TxPower;
};

/*H2C Handler index: 48 */
struct SwitchAntenna_parm {
	u16 antenna_tx;
	u16 antenna_rx;
	/*	R_ANTENNA_SELECT_CCK cck_txrx; */
	u8 cck_txrx;
};

/*H2C Handler index: 49 */
struct SetCrystalCap_parm {
	u32 curr_crystalcap;
};

/*H2C Handler index: 50 */
struct SetSingleCarrierTx_parm {
	u8 bStart;
};

/*H2C Handler index: 51 */
struct SetSingleToneTx_parm {
	u8 bStart;
	u8 curr_rfpath;
};

/*H2C Handler index: 52 */
struct SetCarrierSuppressionTx_parm {
	u8 bStart;
	u32 curr_rateidx;
};

/*H2C Handler index: 53 */
struct SetContinuousTx_parm {
	u8 bStart;
	u8 CCK_flag; /*1:CCK 2:OFDM*/
	u32 curr_rateidx;
};

/*H2C Handler index: 54 */
struct SwitchBandwidth_parm {
	u8 curr_bandwidth;
};

#endif	/* MP_FIRMWARE_OFFLOAD */

/*H2C Handler index: 59 */
struct SetChannelPlan_param {
	const struct country_chplan *country_ent;
	u8 channel_plan;
};

/*H2C Handler index: 60 */
struct LedBlink_param {
	PVOID	 pLed;
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

/*H2C Handler index: 64 */
struct RunInThread_param {
	void (*func)(void *);
	void *context;
};


#define GEN_CMD_CODE(cmd)	cmd ## _CMD_


/*

Result:
0x00: success
0x01: sucess, and check Response.
0x02: cmd ignored due to duplicated sequcne number
0x03: cmd dropped due to invalid cmd code
0x04: reserved.

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
#define H2C_ENQ_HEAD			0x08
#define H2C_ENQ_HEAD_FAIL		0x09

extern u8 rtw_setassocsta_cmd(_adapter  *padapter, u8 *mac_addr);
extern u8 rtw_setstandby_cmd(_adapter *padapter, uint action);
u8 rtw_sitesurvey_cmd(_adapter  *padapter, NDIS_802_11_SSID *ssid, int ssid_num, struct rtw_ieee80211_channel *ch, int ch_num);

u8 rtw_create_ibss_cmd(_adapter *adapter, int flags);
u8 rtw_startbss_cmd(_adapter *adapter, int flags);
u8 rtw_change_bss_chbw_cmd(_adapter *adapter, int flags, s16 req_ch, s8 req_bw, s8 req_offset);

extern u8 rtw_setphy_cmd(_adapter  *padapter, u8 modem, u8 ch);

struct sta_info;
extern u8 rtw_setstakey_cmd(_adapter  *padapter, struct sta_info *sta, u8 key_type, bool enqueue);
extern u8 rtw_clearstakey_cmd(_adapter *padapter, struct sta_info *sta, u8 enqueue);

extern u8 rtw_joinbss_cmd(_adapter  *padapter, struct wlan_network *pnetwork);
u8 rtw_disassoc_cmd(_adapter *padapter, u32 deauth_timeout_ms, bool enqueue);
extern u8 rtw_setopmode_cmd(_adapter  *padapter, NDIS_802_11_NETWORK_INFRASTRUCTURE networktype, bool enqueue);
extern u8 rtw_setdatarate_cmd(_adapter  *padapter, u8 *rateset);
extern u8 rtw_setbasicrate_cmd(_adapter  *padapter, u8 *rateset);
extern u8 rtw_getmacreg_cmd(_adapter *padapter, u8 len, u32 addr);
extern void rtw_usb_catc_trigger_cmd(_adapter *padapter, const char *caller);
extern u8 rtw_setbbreg_cmd(_adapter *padapter, u8 offset, u8 val);
extern u8 rtw_setrfreg_cmd(_adapter *padapter, u8 offset, u32 val);
extern u8 rtw_getbbreg_cmd(_adapter *padapter, u8 offset, u8 *pval);
extern u8 rtw_getrfreg_cmd(_adapter *padapter, u8 offset, u8 *pval);
extern u8 rtw_setrfintfs_cmd(_adapter  *padapter, u8 mode);
extern u8 rtw_setrttbl_cmd(_adapter  *padapter, struct setratable_parm *prate_table);
extern u8 rtw_getrttbl_cmd(_adapter  *padapter, struct getratable_rsp *pval);

extern u8 rtw_gettssi_cmd(_adapter  *padapter, u8 offset, u8 *pval);
extern u8 rtw_setfwdig_cmd(_adapter *padapter, u8 type);
extern u8 rtw_setfwra_cmd(_adapter *padapter, u8 type);

extern u8 rtw_addbareq_cmd(_adapter *padapter, u8 tid, u8 *addr);
extern u8 rtw_addbarsp_cmd(_adapter *padapter, u8 *addr, u16 tid, u8 status, u8 size, u16 start_seq);
/* add for CONFIG_IEEE80211W, none 11w also can use */
extern u8 rtw_reset_securitypriv_cmd(_adapter *padapter);
extern u8 rtw_free_assoc_resources_cmd(_adapter *padapter);
extern u8 rtw_dynamic_chk_wk_cmd(_adapter *adapter);

u8 rtw_lps_ctrl_wk_cmd(_adapter *padapter, u8 lps_ctrl_type, u8 enqueue);
u8 rtw_dm_in_lps_wk_cmd(_adapter *padapter);
u8 rtw_lps_change_dtim_cmd(_adapter *padapter, u8 dtim);

#if (RATE_ADAPTIVE_SUPPORT == 1)
u8 rtw_rpt_timer_cfg_cmd(_adapter *padapter, u16 minRptTime);
#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
extern  u8 rtw_antenna_select_cmd(_adapter *padapter, u8 antenna, u8 enqueue);
#endif

u8 rtw_dm_ra_mask_wk_cmd(_adapter *padapter, u8 *psta);

extern u8 rtw_ps_cmd(_adapter *padapter);

#ifdef CONFIG_AP_MODE
u8 rtw_chk_hi_queue_cmd(_adapter *padapter);
#ifdef CONFIG_DFS_MASTER
u8 rtw_dfs_master_cmd(_adapter *adapter, bool enqueue);
void rtw_dfs_master_timer_hdl(RTW_TIMER_HDL_ARGS);
void rtw_dfs_master_enable(_adapter *adapter, u8 ch, u8 bw, u8 offset);
void rtw_dfs_master_disable(_adapter *adapter, u8 ch, u8 bw, u8 offset, bool by_others);
enum {
	MLME_STA_CONNECTING,
	MLME_STA_CONNECTED,
	MLME_STA_DISCONNECTED,
	MLME_AP_STARTED,
	MLME_AP_STOPPED,
};
void rtw_dfs_master_status_apply(_adapter *adapter, u8 self_action);
#endif /* CONFIG_DFS_MASTER */
#endif /* CONFIG_AP_MODE */

#ifdef CONFIG_BT_COEXIST
u8 rtw_btinfo_cmd(PADAPTER padapter, u8 *pbuf, u16 length);
#endif

u8 rtw_test_h2c_cmd(_adapter *adapter, u8 *buf, u8 len);

u8 rtw_enable_hw_update_tsf_cmd(_adapter *padapter);

u8 rtw_set_ch_cmd(_adapter *padapter, u8 ch, u8 bw, u8 ch_offset, u8 enqueue);

u8 rtw_set_chplan_cmd(_adapter *adapter, int flags, u8 chplan, u8 swconfig);
u8 rtw_set_country_cmd(_adapter *adapter, int flags, const char *country_code, u8 swconfig);

extern u8 rtw_led_blink_cmd(_adapter *padapter, PVOID pLed);
extern u8 rtw_set_csa_cmd(_adapter *padapter, u8 new_ch_no);
extern u8 rtw_tdls_cmd(_adapter *padapter, u8 *addr, u8 option);

#ifdef CONFIG_RTW_CUSTOMER_STR
u8 rtw_customer_str_req_cmd(_adapter *adapter);
u8 rtw_customer_str_write_cmd(_adapter *adapter, const u8 *cstr);
#endif

#ifdef CONFIG_FW_C2H_REG
u8 rtw_c2h_reg_wk_cmd(_adapter *adapter, u8 *c2h_evt);
#endif
#ifdef CONFIG_FW_C2H_PKT
u8 rtw_c2h_packet_wk_cmd(_adapter *adapter, u8 *c2h_evt, u16 length);
#endif

u8 rtw_run_in_thread_cmd(PADAPTER padapter, void (*func)(void *), void *context);

u8 session_tracker_chk_cmd(_adapter *adapter, struct sta_info *sta);
u8 session_tracker_add_cmd(_adapter *adapter, struct sta_info *sta, u8 *local_naddr, u8 *local_port, u8 *remote_naddr, u8 *remote_port);
u8 session_tracker_del_cmd(_adapter *adapter, struct sta_info *sta, u8 *local_naddr, u8 *local_port, u8 *remote_naddr, u8 *remote_port);

u8 rtw_drvextra_cmd_hdl(_adapter *padapter, unsigned char *pbuf);

extern void rtw_survey_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void rtw_disassoc_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void rtw_joinbss_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);
void rtw_create_ibss_post_hdl(_adapter *padapter, int status);
extern void rtw_getbbrfreg_cmdrsp_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void rtw_readtssi_cmdrsp_callback(_adapter	*padapter,  struct cmd_obj *pcmd);

extern void rtw_setstaKey_cmdrsp_callback(_adapter  *padapter,  struct cmd_obj *pcmd);
extern void rtw_setassocsta_cmdrsp_callback(_adapter  *padapter,  struct cmd_obj *pcmd);
extern void rtw_getrttbl_cmdrsp_callback(_adapter  *padapter,  struct cmd_obj *pcmd);
extern void rtw_getmacreg_cmdrsp_callback(_adapter *padapter,  struct cmd_obj *pcmd);


struct _cmd_callback {
	u32	cmd_code;
	void (*callback)(_adapter  *padapter, struct cmd_obj *cmd);
};

enum rtw_h2c_cmd {
	GEN_CMD_CODE(_Read_MACREG) ,	/*0*/
	GEN_CMD_CODE(_Write_MACREG) ,
	GEN_CMD_CODE(_Read_BBREG) ,
	GEN_CMD_CODE(_Write_BBREG) ,
	GEN_CMD_CODE(_Read_RFREG) ,
	GEN_CMD_CODE(_Write_RFREG) , /*5*/
	GEN_CMD_CODE(_Read_EEPROM) ,
	GEN_CMD_CODE(_Write_EEPROM) ,
	GEN_CMD_CODE(_Read_EFUSE) ,
	GEN_CMD_CODE(_Write_EFUSE) ,

	GEN_CMD_CODE(_Read_CAM) ,	/*10*/
	GEN_CMD_CODE(_Write_CAM) ,
	GEN_CMD_CODE(_setBCNITV),
	GEN_CMD_CODE(_setMBIDCFG),
	GEN_CMD_CODE(_JoinBss),   /*14*/
	GEN_CMD_CODE(_DisConnect) , /*15*/
	GEN_CMD_CODE(_CreateBss) ,
	GEN_CMD_CODE(_SetOpMode) ,
	GEN_CMD_CODE(_SiteSurvey),  /*18*/
	GEN_CMD_CODE(_SetAuth) ,

	GEN_CMD_CODE(_SetKey) ,	/*20*/
	GEN_CMD_CODE(_SetStaKey) ,
	GEN_CMD_CODE(_SetAssocSta) ,
	GEN_CMD_CODE(_DelAssocSta) ,
	GEN_CMD_CODE(_SetStaPwrState) ,
	GEN_CMD_CODE(_SetBasicRate) , /*25*/
	GEN_CMD_CODE(_GetBasicRate) ,
	GEN_CMD_CODE(_SetDataRate) ,
	GEN_CMD_CODE(_GetDataRate) ,
	GEN_CMD_CODE(_SetPhyInfo) ,

	GEN_CMD_CODE(_GetPhyInfo) ,	/*30*/
	GEN_CMD_CODE(_SetPhy) ,
	GEN_CMD_CODE(_GetPhy) ,
	GEN_CMD_CODE(_readRssi) ,
	GEN_CMD_CODE(_readGain) ,
	GEN_CMD_CODE(_SetAtim) , /*35*/
	GEN_CMD_CODE(_SetPwrMode) ,
	GEN_CMD_CODE(_JoinbssRpt),
	GEN_CMD_CODE(_SetRaTable) ,
	GEN_CMD_CODE(_GetRaTable) ,

	GEN_CMD_CODE(_GetCCXReport), /*40*/
	GEN_CMD_CODE(_GetDTMReport),
	GEN_CMD_CODE(_GetTXRateStatistics),
	GEN_CMD_CODE(_SetUsbSuspend),
	GEN_CMD_CODE(_SetH2cLbk),
	GEN_CMD_CODE(_AddBAReq) , /*45*/
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
	GEN_CMD_CODE(_ChkBMCSleepq), /*63*/

	GEN_CMD_CODE(_RunInThreadCMD), /*64*/
	GEN_CMD_CODE(_AddBARsp) , /*65*/

	MAX_H2CCMD
};

#define _GetMACReg_CMD_ _Read_MACREG_CMD_
#define _SetMACReg_CMD_ _Write_MACREG_CMD_
#define _GetBBReg_CMD_		_Read_BBREG_CMD_
#define _SetBBReg_CMD_		_Write_BBREG_CMD_
#define _GetRFReg_CMD_		_Read_RFREG_CMD_
#define _SetRFReg_CMD_		_Write_RFREG_CMD_

#ifdef _RTW_CMD_C_
struct _cmd_callback	rtw_cmd_callback[] = {
	{GEN_CMD_CODE(_Read_MACREG), &rtw_getmacreg_cmdrsp_callback}, /*0*/
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
	{GEN_CMD_CODE(_CreateBss), NULL},
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
	{GEN_CMD_CODE(_GetRaTable) , NULL},

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
	{GEN_CMD_CODE(_ChkBMCSleepq), NULL}, /*63*/

	{GEN_CMD_CODE(_RunInThreadCMD), NULL},/*64*/
	{GEN_CMD_CODE(_AddBARsp), NULL}, /*65*/
};
#endif

#define CMD_FMT "cmd=%d,%d,%d"
#define CMD_ARG(cmd) \
	(cmd)->cmdcode, \
	(cmd)->cmdcode == GEN_CMD_CODE(_Set_Drv_Extra) ? ((struct drvextra_cmd_parm *)(cmd)->parmbuf)->ec_id : ((cmd)->cmdcode == GEN_CMD_CODE(_Set_MLME_EVT) ? ((struct C2HEvent_Header *)(cmd)->parmbuf)->ID : 0), \
	(cmd)->cmdcode == GEN_CMD_CODE(_Set_Drv_Extra) ? ((struct drvextra_cmd_parm *)(cmd)->parmbuf)->type : 0

#endif /* _CMD_H_ */
