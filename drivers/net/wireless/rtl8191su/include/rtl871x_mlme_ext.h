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
#ifndef __RTL871X_MLME_EXT_H_
#define __RTL871X_MLME_EXT_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wlan_bssdef.h>
#include <wifi.h>

struct mlme_handler {
	unsigned int   num;
	char* str;
	unsigned int (*func)(_adapter *padapter, u8 *pframe, uint len);
};

struct	bssid_desc	{
	unsigned char valid;
	unsigned char 	addr[ETH_ALEN];
};



/*
struct	ss_res	{
	int	state;
	int	bss_cnt;
	int	channel_idx;//
	int	ss_ssidlen;
	int	passive_mode;
	unsigned char 	ss_ssid[IW_ESSID_MAX_SIZE + 1];
	struct	bssid_desc	bssid[MAX_BSS_CNT];
	
};
*/

struct	ss_res	
{
	int				state;
	int				active_mode;
	int				ss_ssidlen;
	unsigned char 	ss_ssid[IW_ESSID_MAX_SIZE + 1];
	
	unsigned char	old_channel;
	unsigned char	old_modem;
	unsigned char	sur_regulatory_inx;
	unsigned char 	ch_inx;
	unsigned short	orig_bw_mode;
	unsigned char	orig_channel_offset;
	
	unsigned char	orig_MSR;
	unsigned int	bss_cnt;
	unsigned int	fail_cnt;
	
	struct bssid_desc ma_tbl[MAX_BSS_CNT];
	
};

struct rtl_wmm_ac_params {
	int cwmin;
	int cwmax;
	int aifs;
	int txopLimit; /* in units of 32us */
	int admission_control_mandatory;
};

struct wpa_psk
{	

/*
#define _NO_PRIVACY_	0x0
#define _WEP40_			0x1
#define _TKIP_			0x2
#define _TKIP_WTMIC_	0x3
#define _AES_			0x4
#define _WEP104_		0x5
*/

	unsigned int wpa_psk;//0:disable, bit(0): WPA, bit(1):WPA2
	unsigned int wpa_group_cipher;
	unsigned int wpa2_group_cipher;
	unsigned int wpa_pairwise_cipher;
	unsigned int wpa2_pairwise_cipher;

	unsigned char	PassPhrase[65];
	unsigned long GKRekeyTime;//0:disable
	
};

struct wifi_mib
{
	unsigned int mib_ver;
	
	unsigned char	dot11OperationalRateSet[32];
	unsigned int	dot11OperationalRateSetLen;

	struct Dot11nConfigEntry dot11nConfigEntry;

};

struct mlme_ext_priv
{
	_adapter *padapter;

#ifdef PLATFORM_LINUX
	struct net_device *mondev;
#endif

#ifdef CONFIG_RTL8712

	/*
	//pcur_network pointers to pmlmepriv->cur_network.network
	//for AP_MODE,  pcur_network is available when create_bss is called.
	//			    pcur_network defines ap's capability & related network parameter.
	//for STA_MODE security capability refers to the security_priv.
	//for AP_MODE, security capabilities are defined as follows;	
	//mac/bb/rf related capabilities, basic/supported/mcs rates, channel, modulation, etc. is defined below.		
	*/	
	WLAN_BSSID_EX	*pcur_network;

	struct wifi_mib	wmib;

	//mac_ctrl
	unsigned char	basicrate[NumRates];
	unsigned char	basicrate_inx;
#ifdef LOWEST_BASIC_RATE	
	unsigned char	lowest_basicrate_inx;
#endif	
	unsigned char	datarate[NumRates];
	unsigned char	dtim;
	unsigned char	cur_channel;
	unsigned char	cur_modem;
	//unsigned char	old_channel;
	//unsigned char	old_modem;

        //rf_ctrl related
	unsigned char rf_config;
	struct mib_rf_ctrl	rf_ctrl;	
	struct regulatory_class class_sets[1];


	//
	struct ss_res sitesurvey_res;	
	WLAN_BSSID_EX	joining_network;
	unsigned int	join_res;
	unsigned short aid;
	unsigned int	reauth_count;
	unsigned int	reassoc_count;
	unsigned int	auth_seq;
	unsigned int	join_req_ongoing;
	unsigned int 	authModeToggle;
	unsigned int	authModeRetry;
	unsigned int 	iv;
	unsigned char	chg_txt[128];


	//timer for associating (I)BSS
	_timer	survey_timer;
	_timer	reauth_timer;
	_timer	reassoc_timer;
	_timer	disconnect_timer;


	//mlme_event related
	struct	c2hevent_queue 	c2hevent;
	struct	network_queue	networks;
	unsigned int	c2h_res;
	unsigned char	*c2h_buf;

	
	//xmit_mgnt_frame related
	unsigned short mgnt_seqnum;
	_queue	free_mgnt_queue;	
	unsigned char *pallocated_mgnt_frame_buf;
	unsigned char *pxmit_mgnt_frame_buf;
	unsigned int	free_mgnt_frame_cnt;


	
#else	

	struct 	sta_data	sta_data[MAX_STASZ];
	struct 	stainfo			stainfos[MAX_STASZ];
	struct 	stainfo2			stainfo2s[MAX_STASZ];
	struct	stainfo_rxcache	rxcache[MAX_STASZ];
	struct	stainfo_stats		stainfostats[MAX_STASZ];	
	struct	network_queue	networks;//
	struct	mib_rf_ctrl		rf_ctrl;
	struct 	erp_mib 			dot11ErpInfo;
	struct 	Dot11RsnIE		dot11RsnIE;
	struct	Dot1180211AuthEntry 	dot1180211authentry;
	struct	c2hevent_queue 	c2hevent;//
	struct	del_sta_queue		del_sta_q;
	struct	regulatory_class	class_sets[NUM_REGULATORYS];
	struct	regulatory_class	*cur_class;
	struct	rxbufhead_pool	rxbufheadpool;
	struct	txbufhead_pool	txbufheadpool;
	struct	mib_mac_ctrl		mac_ctrl;
	struct	event_node		survey_done_event;
	struct 	surveydone_event	survey_done;
	struct	event_node		join_res_event;
	struct	joinbss_event		joinbss_done;
	struct	event_node		add_sta_event;
	struct	stassoc_event		add_sta_done;
	struct	event_node		del_sta_event;
	struct	stadel_event		del_sta_done;
	struct	ss_res			sitesurvey_res;//
	NDIS_WLAN_BSSID_EX	cur_network;
	_timer	survey_timer;
	_timer	reauth_timer;
	_timer	reassoc_timer;
	_timer	disconnect_timer;

	_list	free_rxirp_mgt;
	_list	free_txirp_mgt;
	_list 	assoc_entry;	
	_list	hash_array[MAX_STASZ];
	
	_sema cam_sema;
	_lock free_rxirp_mgt_lock;
	_lock free_txirp_mgt_lock;

	u8	evt_clear;
	
	u32	reauth_count;//
	u32	reassoc_count;//
	u32	auth_seq;//
	u32	join_req_ongoing;//
	u32	join_res;
	u32	bcnlimit;
	u32 h2crsp_addr[512>>2];
	u32	c2h_res;//
	int 	authModeToggle;
	int	bcn_to_cnt;

	u16	aid;
	u16	ps_bcn_itv;	/* Current AP's beacon interval */
	
	unsigned char	cur_channel;//
	unsigned char	dtim_period;
	u8	change_chan;	
	u8	cmd_proc;
	u8	cur_modem;//
	u8	network_type;
	u8	vcs_mode;
	u8 	h2cseq;
	u8	old_channel;//
	u8	old_modem;//
	u8	sur_regulatory_inx;
	u8	ch_inx;
	u8	beacon_cnt;
	u8	*c2h_buf;//
	u8	*pallocated_fw_rxirp_buf;
	u8	*pfw_rxirp_buf;
	u8	*pallocated_fw_txirp_buf;
	u8	*pfw_txirp_buf;

	//TX rate
	int	rate;       /* current rate */
	int	basic_rate;

	//DIG parameter
	u8	bDynamicInitGain;
	u8	RegBModeGainStage;
	u8	RegDigOfdmFaUpTh;
	u8	InitialGain;
	u8	DIG_NumberFallbackVote;
	u8	DIG_NumberUpgradeVote;
	u8	StageCCKTh;
	u16	CCKUpperTh;
	u16	CCKLowerTh;
	u32	FalseAlarmRegValue;

	//Rate adaptive parameter
	u8	bRateAdaptive;
	u8	ForcedDataRate;
	u8	TryupingCountNoData;
	u8	TryDownCountLowData;
	u8	bTryuping;
	u8	LastFailTxRate;
	u8	FailTxRateCount;
	u16	CurrRetryCnt;
	u16	LastRetryCnt;
	u16	LastRetryRate;
	u16	TryupingCount;
	u32	LastTxThroughput;
	s32	LastFailTxRateSS;
	s32	RecvSignalPower;
	u64	LastTxokCnt;
	u64	LastRxokCnt;
	u64	LastTxOKBytes;
	u64	NumTxOkTotal;
	u64	NumTxOkBytesTotal;
	u64	NumRxOkTotal;
	
#endif	
	
};


#define MAX_MGNTBUF_SZ 	(512)
#define NR_MGNTFRAME 8

struct mgnt_frame{

	_list	list;

	struct pkt_attrib attrib;
	
	_pkt *pkt;
	
	int frame_tag;
	
	 _adapter *padapter;

	 u8 *buf_addr;

	 struct xmit_buf *pxmitbuf;


#ifdef CONFIG_SDIO_HCI

	u8 pg_num;

#endif
	
#ifdef CONFIG_USB_HCI

	//insert urb, irp, and irpcnt info below...      
	//max frag_cnt = 8 
	
       u8 *mem_addr;      
       u16 sz[8];	   
	PURB	pxmit_urb[8];
#ifdef PLATFORM_WINDOWS
	PIRP		pxmit_irp[8];
#endif
	u8 bpending[8];
	//sint ac_tag[8];
	u8 last[8];
       //uint irpcnt;         
       //uint fragcnt;
#endif
	
	//uint	mem[(MAX_MGNTBUF_SZ >> 2)];
	uint	mem[1];
	
};

int	init_mlme_ext_priv (_adapter* padapter);
void free_mlme_ext_priv (struct mlme_ext_priv *pmlmeext);
void init_mlme_ext_timer(_adapter *padapter);

unsigned char *init_mgnt_xmitframe(struct mgnt_frame *pmgntframe);
struct mgnt_frame *alloc_mgnt_xmitframe(struct mlme_ext_priv *pmlmeext);
int free_mgnt_xmitframe(struct mlme_ext_priv *pmlmeext, struct mgnt_frame *pmgntframe);


void mlmeext_surveydone_event_callback(_adapter* padapter);
void mlmeext_joinbss_event_callback(_adapter *padapter, struct wlan_network *pnetwork);

void start_createbss(_adapter *padapter, char *ssid, unsigned int ssid_len);

int xmitframes_filter(_adapter *padapter, _pkt *pkt);

#endif

