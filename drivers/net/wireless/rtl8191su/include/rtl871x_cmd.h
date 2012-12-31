/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#ifndef __RTL871X_CMD_H_
#define __RTL871X_CMD_H_

#include <drv_conf.h>
#include <wlan_bssdef.h>
#include <rtl871x_rf.h>
#define C2H_MEM_SZ (16*1024)

#ifndef CONFIG_RTL8711FW

	#include <osdep_service.h>
	#include <ieee80211.h> // <ieee80211/ieee80211.h>


	#define FREE_CMDOBJ_SZ	128
	
	#define MAX_CMDSZ	512
	#define MAX_RSPSZ	512
	#define MAX_EVTSZ	1024

#ifdef PLATFORM_OS_CE
	#define CMDBUFF_ALIGN_SZ 4
#else
	#define CMDBUFF_ALIGN_SZ 512
#endif

	struct cmd_obj {
		u16	cmdcode;
		u8	res;
		u8	*parmbuf;
		u32	cmdsz;
		u8	*rsp;
		u32	rspsz;
		//_sema 	cmd_sem;
		_list	list;
	};

	struct cmd_priv {
		_sema	cmd_queue_sema;
		//_sema	cmd_done_sema;
		_sema	terminate_cmdthread_sema;		
		_queue	cmd_queue;
		u8	cmd_seq;
		u8	*cmd_buf;	//shall be non-paged, and 4 bytes aligned
		u8	*cmd_allocated_buf;
		u8	*rsp_buf;	//shall be non-paged, and 4 bytes aligned		
		u8	*rsp_allocated_buf;
		u32	cmd_issued_cnt;
		u32	cmd_done_cnt;
		u32	rsp_cnt;
		_adapter *padapter;
	};

	struct evt_obj {
		u16	evtcode;
		u8	res;
		u8	*parmbuf;
		u32	evtsz;		
		_list	list;
	};

	struct	evt_priv {
#ifdef CONFIG_EVENT_THREAD_MODE
		_sema	evt_notify;
		_sema	terminate_evtthread_sema;	
#endif
		_queue	evt_queue;
		
#ifdef CONFIG_H2CLBK
		_sema	lbkevt_done;
		u8	lbkevt_limit;
		u8	lbkevt_num;
		u8	*cmdevt_parm;		
#endif		
		u8	event_seq;
		u8	*evt_buf;	//shall be non-paged, and 4 bytes aligned		
		u8	*evt_allocated_buf;
		u32	evt_done_cnt;
#ifdef CONFIG_SDIO_HCI
		u8	*c2h_mem;
		u8	*allocated_c2h_mem;
#ifdef PLATFORM_OS_XP
		PMDL	pc2h_mdl;
#endif
#endif

#ifdef PLATFORM_LINUX
		struct tasklet_struct event_tasklet;
#endif
	};

#define init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code) \
do {\
	_init_listhead(&pcmd->list);\
	pcmd->cmdcode = code;\
	pcmd->parmbuf = (u8 *)(pparm);\
	pcmd->cmdsz = sizeof (*pparm);\
	pcmd->rsp = NULL;\
	pcmd->rspsz = 0;\
} while(0)

extern u32 enqueue_cmd(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
extern u32 enqueue_cmd_ex(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
extern struct cmd_obj *dequeue_cmd(_queue *queue);
extern void free_cmd_obj(struct cmd_obj *pcmd);

extern u32 enqueue_evt(struct evt_priv *pevtpriv, struct evt_obj *obj);
extern struct evt_obj *dequeue_evt(_queue *queue);
extern void free_evt_obj(struct evt_obj *pcmd);

thread_return cmd_thread(thread_context context);

extern u32 cmd_enqueue(_queue *cmdq,struct cmd_obj	*pcmd);

extern u32 init_cmd_priv (struct cmd_priv *pcmdpriv);
extern void free_cmd_priv (struct cmd_priv *pcmdpriv);

extern u32 init_evt_priv (struct evt_priv *pevtpriv);
extern void free_evt_priv (struct evt_priv *pevtpriv);
extern void cmd_clr_isr(struct cmd_priv *pcmdpriv);
extern void evt_notify_isr(struct evt_priv *pevtpriv);

#else
	#include <ieee80211.h>
#endif	/* CONFIG_RTL8711FW */


enum rtl871x_drvint_cid
{	
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
Caller Mode: Infra, Ad-HoC(C)

Notes: To enter USB suspend mode

Command Mode

*/
struct usb_suspend_parm {
	u32 action;// 1: sleep, 0:resume
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
	NDIS_WLAN_BSSID_EX network;
};

/*
Caller Mode: Infra, Ad-HoC(C)

Notes: To disconnect the current associated BSS

Command Mode

*/
struct disconnect_parm {
	u32 rsvd;
};

/*
Caller Mode: AP, Ad-HoC(M)

Notes: To create a BSS

Command Mode
*/
struct createbss_parm {
	NDIS_WLAN_BSSID_EX network;
};

/*
Caller Mode: AP, Ad-HoC, Infra

Notes: To set the NIC mode of RTL8711

Command Mode

The definition of mode:

#define IW_MODE_AUTO	0	// Let the driver decides which AP to join
#define IW_MODE_ADHOC	1	// Single cell network (Ad-Hoc Clients)
#define IW_MODE_INFRA	2	// Multi cell network, roaming, ..
#define IW_MODE_MASTER	3	// Synchronisation master or Access Point
#define IW_MODE_REPEAT	4	// Wireless Repeater (forwarder)
#define IW_MODE_SECOND	5	// Secondary master/repeater (backup)
#define IW_MODE_MONITOR	6	// Passive monitor (listen only)

*/
struct	setopmode_parm {
	u8	mode;
	u8	rsvd[3];
};

/*
Caller Mode: AP, Ad-HoC, Infra

Notes: To ask RTL8711 performing site-survey

Command-Event Mode 

*/
typedef struct _RT_CHANNEL_PLAN
{
	u8	Channel[NUM_CHANNELS];
	u8	Len;
}RT_CHANNEL_PLAN, *PRT_CHANNEL_PLAN;

typedef struct _SS_DrvCtrl_
{
	u8	EnableDrvCtrlForSurveyTO; // 1: Driver Control, 0: Firmware Default
	u8	rsvd;
	u16	SurveyTO; //Unit: ms

	u8	EnableDrvCtrlForChnlList; // 1: Driver Control, 0: Firmware Default
	RT_CHANNEL_PLAN	ChnlList;
}SS_DrvCtrl, *PSS_DrvCtrl;

#define RTW_SSID_SCAN_AMOUNT 9 // for WEXT_CSCAN_AMOUNT 9
struct sitesurvey_parm
{
	s32	passive_mode; //active: 1, passive: 0
	s32	bsslimit; // 1 ~ 48
	s32	ss_ssidlen;
	u8	ss_ssid[IW_ESSID_MAX_SIZE + 1];

	// Driver Control:
	// 1.) Survey TimeOut for each Channel (Channel Idle Time = SurveyTO * 2)
	// 2.) Survey Channel List
	SS_DrvCtrl	DrvCtrl;
};


/*
Caller Mode: Any

Notes: To set the auth type of RTL8711. open/shared/802.1x

Command Mode

*/
struct setauth_parm {
	u8 mode;  //0: legacy open, 1: legacy shared 2: 802.1x
	u8 _1x;   //0: PSK, 1: TLS
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
	u8	algorithm;	// encryption algorithm, could be none, wep40, TKIP, CCMP, wep104
	u8	keyid;		
	u8 	grpkey;		// 1: this is the grpkey for 802.1x. 0: this is the unicast key for 802.1x
	u8	key[16];	// this could be 40 or 104
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
	u8  	addr[ETH_ALEN];
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
	rsp: 			parm:
		mac[0]  =   mac[5];
		mac[1]  =   mac[4];
		mac[2]  =   mac[3];
		mac[3]  =   mac[2];
		mac[4]  =   mac[1];
		mac[5]  =   mac[0];
		s0		=   s1;
		s1		=   swap16(s0);
		w0		=  	swap32(w1);
		b0		= 	b1
		s2		= 	s0 + s1
		b1		= 	b0
		w1		=	w0
		
	mac[0] == 	2
	==> CMD_EVENT mode, return 	H2C_SUCCESS
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
		w0		=  	swap32(w0);
		b0		= 	b1
		s2		= 	s0 + event.mac[2]
		b1		= 	b0 
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

// CMD param Formart for DRV INTERNAL CMD HDL
struct drvint_cmd_parm {
	int i_cid; //internal cmd id
	int sz; // buf sz
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
	u32	agcctrl;		// 0: pure hw, 1: fw
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

#ifdef CONFIG_PWRCTRL
struct setpwrmode_parm  {
	u8	mode;
	u8	flag_low_traffic_en;
	u8	flag_lpnav_en;
	u8	flag_rf_low_snr_en;
	
	u8	flag_dps_en; // 1: dps, 0: 32k
	u8	bcn_rx_en;
	u8	bcn_pass_cnt;	  // fw report one beacon information to driver  when it receives bcn_pass_cnt  beacons.
	u8	bcn_to; 	  // beacon TO (ms). ¡§=0¡¨ no limit.
	
	u16	bcn_itv;
	u8	app_itv; // only for VOIP mode.
	u8	awake_bcn_itv;
	
	u8	smart_ps;
	u8	bcn_pass_time;	// unit: 100ms
};

struct setatim_parm {
	u8 op;   // 0: add, 1:del
	u8 txid; // id of dest station.
};
#endif /*CONFIG_PWRCTRL*/

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


//to get TX,RX retry count
struct gettxretrycnt_parm{
	unsigned int rsvd;
};
struct gettxretrycnt_rsp{
	unsigned long tx_retrycnt;
};

struct getrxretrycnt_parm{
	unsigned int rsvd;
};
struct getrxretrycnt_rsp{
	unsigned long rx_retrycnt;
};

//to get BCNOK,BCNERR count
struct getbcnokcnt_parm{
	unsigned int rsvd;
};
struct getbcnokcnt_rsp{
	unsigned long  bcnokcnt;
};

struct getbcnerrcnt_parm{
	unsigned int rsvd;
};
struct getbcnerrcnt_rsp{
	unsigned long bcnerrcnt;
};

// to get current TX power level
struct getcurtxpwrlevel_parm{
	unsigned int rsvd;
};
struct getcurtxpwrlevel_rsp{
	unsigned short tx_power;
};

//dynamic on/off DIG
struct setdig_parm{
	unsigned char dig_on;		// 1:on , 0:off
};

//dynamic on/off RA
struct setra_parm{
	unsigned char ra_on;		// 1:on , 0:off
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


struct addBaReq_parm
{
 	unsigned int tid;
};

/*H2C Handler index: 46 */
struct SetChannel_parm
{
	u32 curr_ch;	
};

#ifdef MP_FIRMWARE_OFFLOAD
/*H2C Handler index: 47 */
struct SetTxPower_parm
{
	u8 TxPower;
};

/*H2C Handler index: 48 */
struct SwitchAntenna_parm
{
	u16 antenna_tx;
	u16 antenna_rx;
//	R_ANTENNA_SELECT_CCK cck_txrx;
	u8 cck_txrx;
};

/*H2C Handler index: 49 */
struct SetCrystalCap_parm
{
	u32 curr_crystalcap;
};

/*H2C Handler index: 50 */
struct SetSingleCarrierTx_parm
{
	u8 bStart;
};

/*H2C Handler index: 51 */
struct SetSingleToneTx_parm
{
	u8 bStart;
	u8 curr_rfpath;
};

/*H2C Handler index: 52 */
struct SetCarrierSuppressionTx_parm
{
	u8 bStart;
	u32 curr_rateidx;
};

/*H2C Handler index: 53 */
struct SetContinuousTx_parm
{
	u8 bStart;
	u8 CCK_flag; /*1:CCK 2:OFDM*/
	u32 curr_rateidx;
};

/*H2C Handler index: 54 */
struct SwitchBandwidth_parm
{
	u8 curr_bandwidth;
};

#endif	/* MP_FIRMWARE_OFFLOAD */


/*H2C Handler index: 55 */
struct Tx_Beacon_param
{
	NDIS_WLAN_BSSID_EX network;
};

/*H2C Handler index: 56 */
struct PT_param
{
	u8 PT_En;
};

/*H2C Handler index: 58 */
struct SetMacAddr_param
{
	u8 MacAddr[ETH_ALEN];
};

/*H2C Handler index: 59 */
struct disconnectCtrl_param
{
	 u8  enableDrvCtrl;
	 u8  rsvd1;
	 u8  rsvd2;
	 u8  rsvd3;
	 u32  disconnectTO; // Unit: ms
};

/*H2C Handler index: 60 */
//
// Channel Plan Type.
// Note: 
//	We just add new channel plan when the new channel plan is different from any of the following 
//	channel plan. 
//	If you just wnat to customize the acitions(scan period or join actions) about one of the channel plan,
//	customize them in RT_CHANNEL_INFO in the RT_CHANNEL_LIST.
// 
typedef enum _RT_CHANNEL_DOMAIN
{
	RT_CHANNEL_DOMAIN_FCC = 0,
	RT_CHANNEL_DOMAIN_IC = 1,
	RT_CHANNEL_DOMAIN_ETSI = 2,
	RT_CHANNEL_DOMAIN_SPAIN = 3,
	RT_CHANNEL_DOMAIN_FRANCE = 4,
	RT_CHANNEL_DOMAIN_MKK = 5,
	RT_CHANNEL_DOMAIN_MKK1 = 6,
	RT_CHANNEL_DOMAIN_ISRAEL = 7,
	RT_CHANNEL_DOMAIN_TELEC = 8,
	RT_CHANNEL_DOMAIN_MIC = 9,				// Be compatible with old channel plan. No good!
	RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN = 10,		// Be compatible with old channel plan. No good!
	RT_CHANNEL_DOMAIN_WORLD_WIDE_13 = 11,		// Be compatible with old channel plan. No good!
	RT_CHANNEL_DOMAIN_TELEC_NETGEAR = 12,		// Be compatible with old channel plan. No good!
	RT_CHANNEL_DOMAIN_NCC = 13,
	RT_CHANNEL_DOMAIN_5G = 14,
	RT_CHANNEL_DOMAIN_5G_40M = 15,
	//===== Add new channel plan above this line===============//
	RT_CHANNEL_DOMAIN_MAX,
}RT_CHANNEL_DOMAIN, *PRT_CHANNEL_DOMAIN;

#define rtw_is_channel_plan_valid(chplan) (chplan<RT_CHANNEL_DOMAIN_MAX)

struct SetChannelPlan_param
{
	RT_CHANNEL_DOMAIN	ChannelPlan;
};

#if 0
/*H2C Handler index: 61 */
struct DisconnectCtrlEx_param
{
	//MAXTIME = (2 * FirstStageTO) + (TryPktCnt * TryPktInterval)
	u8	EnableDrvCtrl;
	u8	TryPktCnt;
	u8	TryPktInterval;	//Unit: ms
	u8	rsvd;
	u32	FirstStageTO;	//Unit: ms
};
#endif

/*H2C Handler index: 62 */
typedef struct _bitMask_type
{
	u8 bitMask[16];
	u16 crc_result;
	u16 rsvd2;
}bitMask_type;

typedef struct _WWlanCtrl_param
{
	u8 fun_en;
	u8 magicpkt_en;
	u8 pattern_en;
	u8 GPIO_ACTIVE;		//0:HIGH ACTIVE 1:LOW ACTIVE
	u16 GPIO_DURATION;	//DEFAULT
	u8 bitMask_num;		//max=13
	u8 rsvd1;
	bitMask_type data[1];
}WWlanCtrl_param;

/*H2C Handler index: 63 */
typedef enum _PS_SCHEME_ORIENTED_
{
	PSO_TP			= 0,
	PSO_BATTERY 	= 1
}PS_SCHEME_ORIENTED, *PPS_SCHEME_ORIENTED;

typedef struct _PS_CTRL_PARAM {
	u8	bDrvCtrlEnable;
	u8	BCNToMaxLimit;	
	u16	TRX_RFKeepOnTime;	//Unit: ms
	
	u8	RxBMCFrameTO;		//Unit: ms
	u8 	BCNEarlyTime;		//Unit: ms	
	u8	RxBCNTimeOut;		//Unit: ms
	u8	SchemeOriented;		//PS_SCHEME_ORIENTED
}PS_CTRL_PARAM, *PPS_CTRL_PARAM;

typedef	struct	_SetPwr_Param_
{
	PS_CTRL_PARAM	PSPram;

}SetPwr_Param, *PSetPwr_Param;


/*H2C Handler index: 61 */
struct DisconnectCtrlEx_param
{
	//MAXTIME = (2 * FirstStageTO) + (TryPktCnt * TryPktInterval)
	unsigned char 	EnableDrvCtrl;
	unsigned char		TryPktCnt;
	unsigned char		TryPktInterval;	//Unit: ms
	unsigned char		rsvd;
	unsigned int		FirstStageTO;	//Unit: ms
};

#ifndef CONFIG_RTL8711FW
#else
struct cmdobj {
	uint	parmsize;
	u8 (*h2cfuns)(u8 *pbuf);	
};
extern u8 joinbss_hdl(u8 *pbuf);	
extern u8 disconnect_hdl(u8 *pbuf);
extern u8 createbss_hdl(u8 *pbuf);
extern u8 setopmode_hdl(u8 *pbuf);
extern u8 sitesurvey_hdl(u8 *pbuf);	
extern u8 setauth_hdl(u8 *pbuf);
extern u8 setkey_hdl(u8 *pbuf);
extern u8 set_stakey_hdl(u8 *pbuf);
extern u8 set_assocsta_hdl(u8 *pbuf);
extern u8 del_assocsta_hdl(u8 *pbuf);
extern u8 setstapwrstate_hdl(u8 *pbuf);
extern u8 setbasicrate_hdl(u8 *pbuf);	
extern u8 getbasicrate_hdl(un8 *pbuf);
extern u8 setdatarate_hdl(u8 *pbuf);
extern u8 getdatarate_hdl(u8 *pbuf);
extern u8 setphyinfo_hdl(u8 *pbuf);	
extern u8 getphyinfo_hdl(u8 *pbuf);
extern u8 setphy_hdl(u8 *pbuf);
extern u8 getphy_hdl(u8 *pbuf);
#ifdef CONFIG_H2CLBK
extern u8 seth2clbk_hdl(u8 *pbuf);
extern u8 geth2clbk_hdl(u8 *pbuf);
#endif	/* CONFIG_H2CLBK */
#endif  /* CONFIG_RTL8711FW */


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

extern u8 setMacAddr_cmd(_adapter *padapter, u8 *mac_addr);
extern u8 setassocsta_cmd(_adapter  *padapter, u8 *mac_addr);
extern u8 setstandby_cmd(_adapter *padapter, uint action);
extern u8 sitesurvey_cmd(_adapter  *padapter, NDIS_802_11_SSID *pssid);
extern u8 createbss_cmd(_adapter  *padapter);
extern u8 createbss_cmd_ex(_adapter  *padapter, unsigned char *pbss, unsigned int sz);
extern u8 setphy_cmd(_adapter  *padapter, u8 modem, u8 ch);
extern u8 setstakey_cmd(_adapter  *padapter, u8 *psta, u8 unicast_key);
extern u8 joinbss_cmd(_adapter  *padapter, struct wlan_network* pnetwork);
extern u8 disassoc_cmd(_adapter  *padapter);
extern u8 setopmode_cmd(_adapter  *padapter, NDIS_802_11_NETWORK_INFRASTRUCTURE networktype);
extern u8 setdatarate_cmd(_adapter  *padapter, u8 *rateset);
extern u8 set_chplan_cmd(_adapter  *padapter, int chplan);
extern u8 setbasicrate_cmd(_adapter  *padapter, u8 *rateset);
extern u8 setbbreg_cmd(_adapter * padapter, u8 offset, u8 val);
extern u8 setrfreg_cmd(_adapter * padapter, u8 offset, u32 val);
extern u8 getbbreg_cmd(_adapter * padapter, u8 offset, u8 * pval);
extern u8 getrfreg_cmd(_adapter * padapter, u8 offset, u8 * pval);
extern u8 setrfintfs_cmd(_adapter  *padapter, u8 mode);
extern u8 setrttbl_cmd(_adapter  *padapter, struct setratable_parm *prate_table);
extern u8 getrttbl_cmd(_adapter  *padapter, struct getratable_rsp *pval);

extern u8 gettssi_cmd(_adapter  *padapter, u8 offset,u8 *pval);
extern u8 setptm_cmd(_adapter*padapter, u8 type);
extern u8 setfwdig_cmd(_adapter*padapter, u8 type);
extern u8 setfwra_cmd(_adapter*padapter, u8 type);

extern u8 addbareq_cmd(_adapter*padapter, u8 tid);

extern u8 wdg_wk_cmd(_adapter*padapter);

extern void survey_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void disassoc_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void joinbss_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);	
extern void createbss_cmd_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void getbbrfreg_cmdrsp_callback(_adapter  *padapter, struct cmd_obj *pcmd);
extern void readtssi_cmdrsp_callback(_adapter*	padapter,  struct cmd_obj *pcmd);

extern void setstaKey_cmdrsp_callback(_adapter  *padapter,  struct cmd_obj *pcmd);
extern void setassocsta_cmdrsp_callback(_adapter  *padapter,  struct cmd_obj *pcmd);
extern void getrttbl_cmdrsp_callback(_adapter  *padapter,  struct cmd_obj *pcmd);

#ifdef CONFIG_PWRCTRL
extern u8 setpwrmode_cmd(_adapter* adapter, u32 ps_mode, u32 smart_ps);
extern u8  setatim_cmd(_adapter* adapter, u8 add, u8 txid);
#endif	/* CONFIG_PWRCTRL */

extern u8 disconnectCtrlEx_cmd(_adapter* adapter, u32 enableDrvCtrl, u32 tryPktCnt, u32 tryPktInterval, u32 firstStageTO);

struct _cmd_callback {
	u32	cmd_code;
	void (*callback)(_adapter  *padapter, struct cmd_obj *cmd);
};

#ifdef CONFIG_RTL8712
#include "rtl8712_cmd.h"
#endif

#endif // _CMD_H_

