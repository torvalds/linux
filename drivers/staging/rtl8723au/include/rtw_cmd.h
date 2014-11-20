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
 ******************************************************************************/
#ifndef __RTW_CMD_H_
#define __RTW_CMD_H_

#include <wlan_bssdef.h>
#include <rtw_rf.h>
#include <rtw_led.h>

#define C2H_MEM_SZ (16*1024)

#include <osdep_service.h>
#include <ieee80211.h> /*  <ieee80211/ieee80211.h> */


#define MAX_CMDSZ	1024
#define MAX_RSPSZ	512
#define MAX_EVTSZ	1024

#define CMDBUFF_ALIGN_SZ 512

struct cmd_obj {
	struct work_struct work;
	struct rtw_adapter *padapter;
	u16	cmdcode;
	int	res;
	u32	cmdsz;
	u8	*parmbuf;
	u8	*rsp;
	u32	rspsz;
};

struct cmd_priv {
	struct workqueue_struct *wq;
	u32	cmd_issued_cnt;
	u32	cmd_done_cnt;
	u32	rsp_cnt;
	struct rtw_adapter *padapter;
};

#define C2H_QUEUE_MAX_LEN 10

struct	evt_priv {
	struct workqueue_struct *wq;
	struct work_struct irq_wk;
};

#define init_h2fwcmd_w_parm_no_rsp(pcmd, pparm, code) \
do {\
	pcmd->cmdcode = code;\
	pcmd->parmbuf = (u8 *)(pparm);\
	pcmd->cmdsz = sizeof (*pparm);\
	pcmd->rsp = NULL;\
	pcmd->rspsz = 0;\
} while(0)

struct c2h_evt_hdr {
	u8 id:4;
	u8 plen:4;
	u8 seq;
	u8 payload[0];
};

/*
 * Do not reorder - this allows for struct evt_work to be passed on to
 * rtw_c2h_wk_cmd23a() as a 'struct c2h_evt_hdr *' without making an
 * additional copy.
 */
struct evt_work {
	union {
		struct c2h_evt_hdr c2h_evt;
		u8 buf[16];
	} u;
	struct work_struct work;
	struct rtw_adapter *adapter;
};

#define c2h_evt_exist(c2h_evt) ((c2h_evt)->id || (c2h_evt)->plen)

void rtw_evt_work(struct work_struct *work);

int rtw_enqueue_cmd23a(struct cmd_priv *pcmdpriv, struct cmd_obj *obj);
void rtw_free_cmd_obj23a(struct cmd_obj *pcmd);

int rtw_cmd_thread23a(void *context);

int rtw_init_cmd_priv23a(struct cmd_priv *pcmdpriv);

u32 rtw_init_evt_priv23a (struct evt_priv *pevtpriv);
void rtw_free_evt_priv23a (struct evt_priv *pevtpriv);
void rtw_cmd_clr_isr23a(struct cmd_priv *pcmdpriv);
void rtw_evt_notify_isr(struct evt_priv *pevtpriv);

enum rtw_drvextra_cmd_id
{
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
	C2H_WK_CID,
	RTP_TIMER_CFG_WK_CID,
	MAX_WK_CID
};

enum LPS_CTRL_TYPE
{
	LPS_CTRL_SCAN=0,
	LPS_CTRL_JOINBSS=1,
	LPS_CTRL_CONNECT=2,
	LPS_CTRL_DISCONNECT=3,
	LPS_CTRL_SPECIAL_PACKET=4,
	LPS_CTRL_LEAVE=5,
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
	u32 action;/*  1: sleep, 0:resume */
};

/*
Caller Mode: Infra, Ad-HoC

Notes: To join a known BSS.

Command-Event Mode

*/

/*
Caller Mode: Infra, Ad-HoC(C)

Notes: To disconnect the current associated BSS

Command Mode

*/
struct disconnect_parm {
	u32 deauth_timeout_ms;
};

struct	setopmode_parm {
	enum nl80211_iftype mode;
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
	struct cfg80211_ssid ssid[RTW_SSID_SCAN_AMOUNT];
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
	u32	algorithm;	/*  encryption algorithm, could be none, wep40, TKIP, CCMP, wep104 */
	u8	keyid;
	u8	grpkey;		/*  1: this is the grpkey for 802.1x. 0: this is the unicast key for 802.1x */
	u8	set_tx;		/*  1: main tx key for wep. 0: other key. */
	u8	key[16];	/*  this could be 40 or 104 */
};

/*
When in AP or Ad-Hoc mode, this is used to
allocate an sw/hw entry for a newly associated sta.

Command

when shared key ==> algorithm/keyid

*/
struct set_stakey_parm {
	u8	addr[ETH_ALEN];
	u8	id;/*  currently for erasing cam entry if algorithm == _NO_PRIVACY_ */
	u32	algorithm;
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

struct Tx_Beacon_param
{
	struct wlan_bssid_ex network;
};

/*  CMD param Formart for driver extra cmd handler */
struct drvextra_cmd_parm {
	int ec_id; /* extra cmd id */
	int type_size; /*  Can use this field as the type id or command size */
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
	u32	agcctrl;		/*  0: pure hw, 1: fw */
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

/* to get BCNOK,BCNERR count */
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

/*  to get current TX power level */
struct getcurtxpwrlevel_parm{
	unsigned int rsvd;
};

struct getcurtxpwrlevel_rsp{
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
	struct led_8723a *pLed;
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

int rtw_setassocsta_cmd(struct rtw_adapter  *padapter, u8 *mac_addr);
int rtw_setstandby_cmd(struct rtw_adapter *padapter, uint action);
int rtw_sitesurvey_cmd23a(struct rtw_adapter  *padapter, struct cfg80211_ssid *ssid, int ssid_num, struct rtw_ieee80211_channel *ch, int ch_num);
int rtw_createbss_cmd23a(struct rtw_adapter  *padapter);
int rtw_createbss_cmd23a_ex(struct rtw_adapter  *padapter, unsigned char *pbss, unsigned int sz);
int rtw_setphy_cmd(struct rtw_adapter  *padapter, u8 modem, u8 ch);
int rtw_setstakey_cmd23a(struct rtw_adapter  *padapter, u8 *psta, u8 unicast_key);
int rtw_clearstakey_cmd23a(struct rtw_adapter *padapter, u8 *psta, u8 entry, u8 enqueue);
int rtw_joinbss_cmd23a(struct rtw_adapter  *padapter, struct wlan_network* pnetwork);
int rtw_disassoc_cmd23a(struct rtw_adapter *padapter, u32 deauth_timeout_ms, bool enqueue);
int rtw_setopmode_cmd23a(struct rtw_adapter *padapter, enum nl80211_iftype ifmode);
int rtw_setdatarate_cmd(struct rtw_adapter  *padapter, u8 *rateset);
int rtw_setbasicrate_cmd(struct rtw_adapter  *padapter, u8 *rateset);
int rtw_setbbreg_cmd(struct rtw_adapter * padapter, u8 offset, u8 val);
int rtw_setrfreg_cmd(struct rtw_adapter * padapter, u8 offset, u32 val);
int rtw_getbbreg_cmd(struct rtw_adapter * padapter, u8 offset, u8 * pval);
int rtw_getrfreg_cmd(struct rtw_adapter * padapter, u8 offset, u8 * pval);
int rtw_setrfintfs_cmd(struct rtw_adapter  *padapter, u8 mode);
int rtw_setrttbl_cmd(struct rtw_adapter  *padapter, struct setratable_parm *prate_table);
int rtw_getrttbl_cmd(struct rtw_adapter  *padapter, struct getratable_rsp *pval);

int rtw_gettssi_cmd(struct rtw_adapter  *padapter, u8 offset, u8 *pval);
int rtw_setfwdig_cmd(struct rtw_adapter*padapter, u8 type);
int rtw_setfwra_cmd(struct rtw_adapter*padapter, u8 type);

int rtw_addbareq_cmd23a(struct rtw_adapter*padapter, u8 tid, u8 *addr);

int rtw_dynamic_chk_wk_cmd23a(struct rtw_adapter *adapter);

int rtw_lps_ctrl_wk_cmd23a(struct rtw_adapter*padapter, u8 lps_ctrl_type, u8 enqueue);

int rtw_ps_cmd23a(struct rtw_adapter*padapter);

#ifdef CONFIG_8723AU_AP_MODE
int rtw_chk_hi_queue_cmd23a(struct rtw_adapter*padapter);
#endif

int rtw_set_ch_cmd23a(struct rtw_adapter*padapter, u8 ch, u8 bw, u8 ch_offset, u8 enqueue);
int rtw_set_chplan_cmd(struct rtw_adapter*padapter, u8 chplan, u8 enqueue);
int rtw_led_blink_cmd(struct rtw_adapter*padapter, struct led_8723a *pLed);
int rtw_set_csa_cmd(struct rtw_adapter*padapter, u8 new_ch_no);

int rtw_c2h_wk_cmd23a(struct rtw_adapter *padapter, u8 *c2h_evt);

int rtw_drvextra_cmd_hdl23a(struct rtw_adapter *padapter, const u8 *pbuf);

void rtw_survey_cmd_callback23a(struct rtw_adapter  *padapter, struct cmd_obj *pcmd);
void rtw_disassoc_cmd23a_callback(struct rtw_adapter  *padapter, struct cmd_obj *pcmd);
void rtw_joinbss_cmd23a_callback(struct rtw_adapter  *padapter, struct cmd_obj *pcmd);
void rtw_createbss_cmd23a_callback(struct rtw_adapter  *padapter, struct cmd_obj *pcmd);
void rtw_getbbrfreg_cmdrsp_callback23a(struct rtw_adapter  *padapter, struct cmd_obj *pcmd);
void rtw_readtssi_cmdrsp_callback(struct rtw_adapter*	padapter,  struct cmd_obj *pcmd);

void rtw_setstaKey_cmdrsp_callback23a(struct rtw_adapter  *padapter,  struct cmd_obj *pcmd);
void rtw_setassocsta_cmdrsp_callback23a(struct rtw_adapter  *padapter,  struct cmd_obj *pcmd);
void rtw_getrttbl_cmdrsp_callback(struct rtw_adapter  *padapter,  struct cmd_obj *pcmd);

struct _cmd_callback {
	u32	cmd_code;
	void (*callback)(struct rtw_adapter  *padapter, struct cmd_obj *cmd);
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

	MAX_H2CCMD
};

extern struct _cmd_callback	rtw_cmd_callback[];

#endif /*  _CMD_H_ */
