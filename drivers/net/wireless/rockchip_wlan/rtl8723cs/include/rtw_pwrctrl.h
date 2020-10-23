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
#ifndef __RTW_PWRCTRL_H_
#define __RTW_PWRCTRL_H_


#define FW_PWR0	0
#define FW_PWR1	1
#define FW_PWR2	2
#define FW_PWR3	3


#define HW_PWR0	7
#define HW_PWR1	6
#define HW_PWR2	2
#define HW_PWR3	0
#define HW_PWR4	8

#define FW_PWRMSK	0x7


#define XMIT_ALIVE	BIT(0)
#define RECV_ALIVE	BIT(1)
#define CMD_ALIVE	BIT(2)
#define EVT_ALIVE	BIT(3)
#ifdef CONFIG_BT_COEXIST
#define BTCOEX_ALIVE	BIT(4)
#endif /* CONFIG_BT_COEXIST */
#define LPS_ALIVE	BIT(5)

#ifdef CONFIG_WOWLAN
	#ifdef CONFIG_PLATFORM_ANDROID_INTEL_X86
		/* TCP/ICMP/UDP multicast with specific IP addr */
		#define DEFAULT_PATTERN_NUM 4
	#else
		/* TCP/ICMP */
		#define DEFAULT_PATTERN_NUM 3
	#endif

#ifdef CONFIG_WOW_PATTERN_HW_CAM	/* Frame Mask Cam number for pattern match */
#define MAX_WKFM_CAM_NUM	12
#else
#define MAX_WKFM_CAM_NUM	16
#endif

#define MAX_WKFM_SIZE	16 /* (16 bytes for WKFM bit mask, 16*8 = 128 bits) */
#define MAX_WKFM_PATTERN_SIZE	128
#define MAX_IN_PATTERN_SIZE		512

/*
 * MAX_WKFM_PATTERN_STR_LEN : the max. length of wow pattern string
 *	e.g. echo 00:01:02:...:7f > /proc/net/rtl88x2bu/wlan0/wow_pattern_info
 *	- each byte of pattern is represented as 2-bytes ascii : MAX_WKFM_PATTERN_SIZE * 2
 *	- the number of common ':' in pattern string : MAX_WKFM_PATTERN_SIZE - 1
 *	- 1 byte '\n'(0x0a) is generated at the end when we use echo command
 *	so total max. length is (MAX_WKFM_PATTERN_SIZE * 3)
 */
#define MAX_WKFM_PATTERN_STR_LEN (MAX_WKFM_PATTERN_SIZE * 3)

#define WKFMCAM_ADDR_NUM 6
#define WKFMCAM_SIZE 24 /* each entry need 6*4 bytes */
enum pattern_type {
	PATTERN_BROADCAST = 0,
	PATTERN_MULTICAST,
	PATTERN_UNICAST,
	PATTERN_VALID,
	PATTERN_INVALID,
};

typedef struct rtl_priv_pattern {
	int len;
	char content[MAX_WKFM_PATTERN_SIZE];
	char mask[MAX_WKFM_SIZE];
} rtl_priv_pattern_t;

#endif /* CONFIG_WOWLAN */

enum Power_Mgnt {
	PS_MODE_ACTIVE	= 0	,
	PS_MODE_MIN			,
	PS_MODE_MAX			,
	PS_MODE_DTIM			,	/* PS_MODE_SELF_DEFINED */
	PS_MODE_VOIP			,
	PS_MODE_UAPSD_WMM	,
	PS_MODE_UAPSD			,
	PS_MODE_IBSS			,
	PS_MODE_WWLAN		,
	PM_Radio_Off			,
	PM_Card_Disable		,
	PS_MODE_NUM,
};

enum lps_level {
	LPS_NORMAL = 0,
	LPS_LCLK,
	LPS_PG,
	LPS_LEVEL_MAX,
};

#ifdef CONFIG_PNO_SUPPORT
#define MAX_PNO_LIST_COUNT 16
#define MAX_SCAN_LIST_COUNT 14	/* 2.4G only */
#define MAX_HIDDEN_AP 8		/* 8 hidden AP */
#endif

/*
	BIT[2:0] = HW state
	BIT[3] = Protocol PS state,   0: register active state , 1: register sleep state
	BIT[4] = sub-state
*/

#define PS_DPS				BIT(0)
#define PS_LCLK				(PS_DPS)
#define PS_RF_OFF			BIT(1)
#define PS_ALL_ON			BIT(2)
#define PS_ST_ACTIVE		BIT(3)

#define PS_ISR_ENABLE		BIT(4)
#define PS_IMR_ENABLE		BIT(5)
#define PS_ACK				BIT(6)
#define PS_TOGGLE			BIT(7)

#define PS_STATE_MASK		(0x0F)
#define PS_STATE_HW_MASK	(0x07)
#define PS_SEQ_MASK			(0xc0)

#define PS_STATE(x)		(PS_STATE_MASK & (x))
#define PS_STATE_HW(x)	(PS_STATE_HW_MASK & (x))
#define PS_SEQ(x)		(PS_SEQ_MASK & (x))

#define PS_STATE_S0		(PS_DPS)
#define PS_STATE_S1		(PS_LCLK)
#define PS_STATE_S2		(PS_RF_OFF)
#define PS_STATE_S3		(PS_ALL_ON)
#define PS_STATE_S4		((PS_ST_ACTIVE) | (PS_ALL_ON))


#define PS_IS_RF_ON(x)	((x) & (PS_ALL_ON))
#define PS_IS_ACTIVE(x)	((x) & (PS_ST_ACTIVE))
#define CLR_PS_STATE(x)	((x) = ((x) & (0xF0)))


struct reportpwrstate_parm {
	unsigned char mode;
	unsigned char state; /* the CPWM value */
	unsigned short rsvd;
};


typedef _sema _pwrlock;


__inline static void _init_pwrlock(_pwrlock *plock)
{
	_rtw_init_sema(plock, 1);
}

__inline static void _free_pwrlock(_pwrlock *plock)
{
	_rtw_free_sema(plock);
}


__inline static void _enter_pwrlock(_pwrlock *plock)
{
	_rtw_down_sema(plock);
}


__inline static void _exit_pwrlock(_pwrlock *plock)
{
	_rtw_up_sema(plock);
}

#define LPS_DELAY_MS	1000 /* 1 sec */

#define EXE_PWR_NONE	0x01
#define EXE_PWR_IPS		0x02
#define EXE_PWR_LPS		0x04

/* RF state. */
typedef enum _rt_rf_power_state {
	rf_on,		/* RF is on after RFSleep or RFOff */
	rf_sleep,	/* 802.11 Power Save mode */
	rf_off,		/* HW/SW Radio OFF or Inactive Power Save */
	/* =====Add the new RF state above this line===== */
	rf_max
} rt_rf_power_state;

/* ASPM OSC Control bit, added by Roger, 2013.03.29. */
#define	RT_PCI_ASPM_OSC_IGNORE		0	 /* PCI ASPM ignore OSC control in default */
#define	RT_PCI_ASPM_OSC_ENABLE		BIT0 /* PCI ASPM controlled by OS according to ACPI Spec 5.0 */
#define	RT_PCI_ASPM_OSC_DISABLE		BIT1 /* PCI ASPM controlled by driver or BIOS, i.e., force enable ASPM */


enum _PS_BBRegBackup_ {
	PSBBREG_RF0 = 0,
	PSBBREG_RF1,
	PSBBREG_RF2,
	PSBBREG_AFE0,
	PSBBREG_TOTALCNT
};

enum { /* for ips_mode */
	IPS_NONE = 0,
	IPS_NORMAL,
	IPS_LEVEL_2,
	IPS_NUM
};

/* Design for pwrctrl_priv.ips_deny, 32 bits for 32 reasons at most */
typedef enum _PS_DENY_REASON {
	PS_DENY_DRV_INITIAL = 0,
	PS_DENY_SCAN,
	PS_DENY_JOIN,
	PS_DENY_DISCONNECT,
	PS_DENY_SUSPEND,
	PS_DENY_IOCTL,
	PS_DENY_MGNT_TX,
	PS_DENY_MONITOR_MODE,
	PS_DENY_BEAMFORMING,		/* Beamforming */
	PS_DENY_DRV_REMOVE = 30,
	PS_DENY_OTHERS = 31
} PS_DENY_REASON;

#ifdef CONFIG_WAR_OFFLOAD
/* only support mDNS V4/V6 rsp now */
enum {
	WAR_ARP_RSP_EN = 0x0000001,
	WAR_ICMPV6_NS_RSP_EN = 0x00000002,
	WAR_ICMPV4_ECHO_RSP_EN = 0x00000004,
	WAR_ICMPV6_ECHO_RSP_EN = 0x00000008,
	WAR_NETBIOS_RSP_EN = 0x00000010,
	WAR_LLMNR_V4_RSP_EN = 0x00000020,
	WAR_LLMNR_V6_RSP_EN = 0x00000040,
	WAR_SNMP_V4_RSP_EN = 0x00000080,
	WAR_SNMP_V6_RSP_EN = 0x00000100,
	WAR_SNMP_V4_WAKEUP_EN = 0x00000200,
	WAR_SNMP_V6_WAKEUP_EN = 0x00000400,
	WAR_SSDP_V4_WAKEUP_EN = 0x00000800,
	WAR_SSDP_V6_WAKEUP_EN = 0x00001000,
	WAR_WSD_V4_WAKEUP_EN = 0x00002000,
	WAR_WSD_V6_WAKEUP_EN = 0x00004000,
	WAR_SLP_V4_WAKEUP_EN = 0x00008000,
	WAR_SLP_V6_WAKEUP_EN = 0x00010000,
	WAR_MDNS_V4_RSP_EN = 0x00020000,
	WAR_MDNS_V6_RSP_EN = 0x00040000,
	WAR_DESIGNATED_MAC_EN = 0x00080000,
	WAR_LLTD_WAKEUP_EN = 0x00100000,
	WAR_ARP_WAKEUP_EN = 0x00200000,
	WAR_MAGIC_WAKEUP_EN = 0x00400000,
	WAR_MDNS_V4_WAKEUP_EN = 0x000800000,
	WAR_MDNS_V6_WAKEUP_EN = 0x001000000
};

#endif /* CONFIG_WAR_OFFLOAD */

#ifdef CONFIG_PNO_SUPPORT
typedef struct pno_nlo_info {
	u32 fast_scan_period;				/* Fast scan period */
	u8	ssid_num;				/* number of entry */
	u8	hidden_ssid_num;
	u32	slow_scan_period;			/* slow scan period */
	u32	fast_scan_iterations;			/* Fast scan iterations */
	u8	ssid_length[MAX_PNO_LIST_COUNT];	/* SSID Length Array */
	u8	ssid_cipher_info[MAX_PNO_LIST_COUNT];	/* Cipher information for security */
	u8	ssid_channel_info[MAX_PNO_LIST_COUNT];	/* channel information */
	u8	loc_probe_req[MAX_HIDDEN_AP];		/* loc_probeReq */
} pno_nlo_info_t;

typedef struct pno_ssid {
	u32		SSID_len;
	u8		SSID[32];
} pno_ssid_t;

typedef struct pno_ssid_list {
	pno_ssid_t	node[MAX_PNO_LIST_COUNT];
} pno_ssid_list_t;

typedef struct pno_scan_channel_info {
	u8	channel;
	u8	tx_power;
	u8	timeout;
	u8	active;				/* set 1 means active scan, or pasivite scan. */
} pno_scan_channel_info_t;

typedef struct pno_scan_info {
	u8	enableRFE;			/* Enable RFE */
	u8	period_scan_time;		/* exclusive with fast_scan_period and slow_scan_period */
	u8	periodScan;			/* exclusive with fast_scan_period and slow_scan_period */
	u8	orig_80_offset;			/* original channel 80 offset */
	u8	orig_40_offset;			/* original channel 40 offset */
	u8	orig_bw;			/* original bandwidth */
	u8	orig_ch;			/* original channel */
	u8	channel_num;			/* number of channel */
	u64	rfe_type;			/* rfe_type && 0x00000000000000ff */
	pno_scan_channel_info_t ssid_channel_info[MAX_SCAN_LIST_COUNT];
} pno_scan_info_t;
#endif /* CONFIG_PNO_SUPPORT */

#ifdef CONFIG_LPS_POFF
/* Driver context for LPS 32K Close IO Power */
typedef struct lps_poff_info {
	bool	bEn;
	u8	*pStaticFile;
	u8	*pDynamicFile;
	u32	ConfFileOffset;
	u32	tx_bndy_static;
	u32	tx_bndy_dynamic;
	u16	ConfLenForPTK;
	u16	ConfLenForGTK;
	ATOMIC_T bEnterPOFF;
	ATOMIC_T bTxBoundInProgress;
	ATOMIC_T bSetPOFFParm;
} lps_poff_info_t;
#endif /*CONFIG_LPS_POFF*/

struct aoac_report {
	u8 iv[8];
	u8 replay_counter_eapol_key[8];
	u8 group_key[32];
	u8 key_index;
	u8 security_type;
	u8 wow_pattern_idx;
	u8 version_info;
	u8 rekey_ok:1;
	u8 dummy:7;
	u8 reserved[3];
	u8 rxptk_iv[8];
	u8 rxgtk_iv[4][8];
};

#ifdef CONFIG_WAR_OFFLOAD

struct war_ipv4_fmt {
	u32 ip_addr[4];
	u32 ip_subnet[4];
	u32 ip_gateway[4];
};

struct war_ipv6_fmt {
	u8 ipv6_addr[8][16];
};

#if defined(CONFIG_OFFLOAD_MDNS_V4) || defined(CONFIG_OFFLOAD_MDNS_V6)
/* limitation of mDNS parameter : length and number */
#define MAX_MDNS_SERVICE_NAME_LEN 15 
#define MAX_MDNS_TRANS_LEN 4 /* _tcp or _udp */
#define MAX_MDNS_DOMAIN_LEN 5 /* local only for mdns */
#define MAX_MDNS_MACHINE_NAME_LEN (63+1) /* +1 for the length byte used by the DNS format */
#define MAX_MDNS_TARGET_LEN 63
#define MAX_MDNS_DOMAIN_NAME_LEN 63
#define MAX_MDNS_TXT_LEN 1536
#define MAX_MDNS_TXT_SINGLE_LEN 255


#define MAX_MDNS_SERVICE_NUM 10
#define MAX_MDNS_TXT_NUM 8
#define MAX_MDNS_MACHINE_NAME_NUM 3

/* for monitor rsvd page using */
#define MAX_MDNS_PARA_SIZE 1700 // 14*128 = 1792
#define MAX_MDNS_TXT_TOTAL_SIZE 10*MAX_MDNS_TXT_LEN
#define MAX_MDNS_RSP_PKT_SIZE 760   //  6*128 = 768

#define RTW_MDNS_SRV_INFO(sname, sname_len, tname, tname_len, dname, dname_len, port0, port1, ttlv, tar, tar_len, idx) \
	{ .service=sname, .service_len=sname_len, .transport=tname, .transport_len=tname_len, \
	  .domain=dname , .domain_len=dname_len , .port[0]=port0, .port[1]=port1, .ttl=ttlv, \
	  .target=tar, .target_len=tar_len, .txt_rsp_idx=idx }


struct war_mdns_service_info {
	u8  service[MAX_MDNS_SERVICE_NAME_LEN+1];
	u8  service_len;
	u8  transport[MAX_MDNS_TRANS_LEN+1];
	u8  transport_len;
	u8  domain[MAX_MDNS_DOMAIN_LEN+1];
	u8  domain_len;
	u8  port[2];
	u32 ttl;
	u8  target[MAX_MDNS_TARGET_LEN+1];
	u8  target_len;
	s8  txt_rsp_idx;
};

struct war_mdns_machine_name {
	u8  name[MAX_MDNS_MACHINE_NAME_LEN];
	u8  name_len;
};

struct war_mdns_txt_rsp {
	u8  txt[MAX_MDNS_TXT_LEN];
	u16  txt_len;
};
#endif
#endif /* CONFIG_WAR_OFFLOAD */


struct rsvd_page_cache_t;

struct pwrctrl_priv {
	_pwrlock	lock;
	_pwrlock	check_32k_lock;
	volatile u8 rpwm; /* requested power state for fw */
	volatile u8 cpwm; /* fw current power state. updated when 1. read from HCPWM 2. driver lowers power level */
	volatile u8 tog; /* toggling */
	volatile u8 cpwm_tog; /* toggling */
	u8 rpwm_retry;

	u8	pwr_mode;
	u8	smart_ps;
	u8	bcn_ant_mode;
	u8	dtim;
#ifdef CONFIG_LPS_CHK_BY_TP
	u8	lps_chk_by_tp;
	u16	lps_tx_tp_th;/*Mbps*/
	u16	lps_rx_tp_th;/*Mbps*/
	u16	lps_bi_tp_th;/*Mbps*//*TRX TP*/
	int	lps_chk_cnt_th;
	int	lps_chk_cnt;
	u32	lps_tx_pkts;
	u32	lps_rx_pkts;

#endif

#ifdef CONFIG_WMMPS_STA
	u8 wmm_smart_ps;
#endif /* CONFIG_WMMPS_STA */	

	u32	alives;
	_workitem cpwm_event;
	_workitem dma_event; /*for handle un-synchronized tx dma*/
#ifdef CONFIG_LPS_RPWM_TIMER
	u8 brpwmtimeout;
	_workitem rpwmtimeoutwi;
	_timer pwr_rpwm_timer;
#endif /* CONFIG_LPS_RPWM_TIMER */
	u8	bpower_saving; /* for LPS/IPS */

	u8	b_hw_radio_off;
	u8	reg_rfoff;
	u8	reg_pdnmode; /* powerdown mode */
	u32	rfoff_reason;

	uint	ips_enter_cnts;
	uint	ips_leave_cnts;
	uint	lps_enter_cnts;
	uint	lps_leave_cnts;

	u8	ips_mode;
	u8	ips_org_mode;
	u8	ips_mode_req; /* used to accept the mode setting request, will update to ipsmode later */
	uint bips_processing;
	systime ips_deny_time; /* will deny IPS when system time is smaller than this */
	u8 pre_ips_type;/* 0: default flow, 1: carddisbale flow */

	/* ps_deny: if 0, power save is free to go; otherwise deny all kinds of power save. */
	/* Use PS_DENY_REASON to decide reason. */
	/* Don't access this variable directly without control function, */
	/* and this variable should be protected by lock. */
	u32 ps_deny;

	u8 ps_processing; /* temporarily used to mark whether in rtw_ps_processor */

	u8 fw_psmode_iface_id;
	u8	bLeisurePs;
	u8	LpsIdleCount;
	u8	power_mgnt;
	u8	org_power_mgnt;
	u8	bFwCurrentInPSMode;
	systime	lps_deny_time; /* will deny LPS when system time is smaller than this */
	s32		pnp_current_pwr_state;
	u8		pnp_bstop_trx;
	u8		bInSuspend;
#ifdef CONFIG_BT_COEXIST
	u8		bAutoResume;
	u8		autopm_cnt;
#endif
	u8		bSupportRemoteWakeup;
	u8		wowlan_wake_reason;
	u8		wowlan_last_wake_reason;
	u8		wowlan_ap_mode;
	u8		wowlan_mode;
	u8		wowlan_p2p_mode;
	u8		wowlan_pno_enable;
	u8		wowlan_in_resume;

#ifdef CONFIG_GPIO_WAKEUP
	u8		is_high_active;
	u8		wowlan_gpio_index;
	u8		wowlan_gpio_output_state;
#endif /* CONFIG_GPIO_WAKEUP */
	u8		hst2dev_high_active;
#ifdef CONFIG_WOWLAN
	bool		default_patterns_en;
#ifdef CONFIG_IPV6
	u8		wowlan_ns_offload_en;
#endif /*CONFIG_IPV6*/
	u8		wowlan_txpause_status;
	u8		wowlan_pattern_idx;
	u64		wowlan_fw_iv;
	struct rtl_priv_pattern	patterns[MAX_WKFM_CAM_NUM];
#ifdef CONFIG_PNO_SUPPORT
	u8		pno_inited;
	pno_nlo_info_t	*pnlo_info;
	pno_scan_info_t	*pscan_info;
	pno_ssid_list_t	*pno_ssid_list;
#endif /* CONFIG_PNO_SUPPORT */
#ifdef CONFIG_WOW_PATTERN_HW_CAM
	_mutex	wowlan_pattern_cam_mutex;
#endif
	u8		wowlan_aoac_rpt_loc;
	struct aoac_report wowlan_aoac_rpt;
	u8		wowlan_power_mgmt;
	u8		wowlan_lps_level;
	#ifdef CONFIG_LPS_1T1R
	u8		wowlan_lps_1t1r;
	#endif

	#ifdef CONFIG_WOW_KEEP_ALIVE_PATTERN
	/*data 0,rsv page location*/
	u8		wowlan_keep_alive_mode;
	u8		keep_alive_pattern_loc;
	/*data 1 ,cam id, rx udp packet*/
	u8		wowlan_keep_alive_ack_index;
	/*data 2 ,cam id, pattern match packet*/
	u8		wowlan_wake_pattern_index;
	/*data3,unit: TBTT*/
	u16		wowlan_keep_alive_period;
	/*data4,unit: TBTT*/
	u8		wowlan_keep_alive_retry_interval;
	/*data5*/
	u8		wowlan_keep_alive_retry_counter;
	/*from echo*/
	u8		keep_alive_pattern[WLAN_MAX_KEEP_ALIVE_IE_LEN];
	u32		keep_alive_pattern_len;
	#endif /*CONFIG_WOW_KEEP_ALIVE_PATTERN*/

#ifdef CONFIG_WAR_OFFLOAD
	u8		wowlan_war_offload_mode;
	u32 	wowlan_war_offload_ctrl;
	struct war_ipv4_fmt wowlan_war_offload_ipv4;
	struct war_ipv6_fmt wowlan_war_offload_ipv6;
	u8		wowlan_war_offload_mac[6];
#if defined(CONFIG_OFFLOAD_MDNS_V4) || defined(CONFIG_OFFLOAD_MDNS_V6)
	struct war_mdns_machine_name wowlan_war_offload_mdns_mnane[MAX_MDNS_MACHINE_NAME_NUM];
	struct war_mdns_service_info wowlan_war_offload_mdns_service[MAX_MDNS_SERVICE_NUM];
	struct war_mdns_txt_rsp 	 wowlan_war_offload_mdns_txt_rsp[MAX_MDNS_TXT_NUM];
	u8	   wowlan_war_offload_mdns_mnane_num;
	u8	   wowlan_war_offload_mdns_service_info_num;
	u8	   wowlan_war_offload_mdns_txt_rsp_num;
	u8	   wowlan_war_offload_mdns_domain_name[MAX_MDNS_DOMAIN_NAME_LEN+1];
	u8	   wowlan_war_offload_mdns_domain_name_len;
	u32    wowlan_war_offload_mdns_para_cur_size;
	u32    wowlan_war_offload_mdns_rsp_cur_size;
#endif /* CONFIG_OFFLOAD_MDNS_V4 || CONFIG_OFFLOAD_MDNS_V6 */    
#endif /* CONFIG_WAR_OFFLOAD */	
#endif /* CONFIG_WOWLAN */
	_timer	pwr_state_check_timer;
	int		pwr_state_check_interval;
	u8		pwr_state_check_cnts;


	rt_rf_power_state	rf_pwrstate;/* cur power state, only for IPS */
	/* rt_rf_power_state	current_rfpwrstate; */
	rt_rf_power_state	change_rfpwrstate;

	u8		bHWPowerdown; /* power down mode selection. 0:radio off, 1:power down */
	u8		bHWPwrPindetect; /* come from registrypriv.hwpwrp_detect. enable power down function. 0:disable, 1:enable */
	u8		bkeepfwalive;
	u8		brfoffbyhw;
	unsigned long PS_BBRegBackup[PSBBREG_TOTALCNT];

#ifdef CONFIG_RESUME_IN_WORKQUEUE
	struct workqueue_struct *rtw_workqueue;
	_workitem resume_work;
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	u8 do_late_resume;
#endif /* CONFIG_HAS_EARLYSUSPEND */

#ifdef CONFIG_ANDROID_POWER
	android_early_suspend_t early_suspend;
	u8 do_late_resume;
#endif

#ifdef CONFIG_LPS_POFF
	lps_poff_info_t	*plps_poff_info;
#endif
	u8 lps_level_bk;
	u8 lps_level; /*LPS_NORMAL,LPA_CG,LPS_PG*/
#ifdef CONFIG_LPS_1T1R
	u8 lps_1t1r_bk;
	u8 lps_1t1r;
#endif
#ifdef CONFIG_LPS_PG
	struct rsvd_page_cache_t lpspg_info;
#ifdef CONFIG_RTL8822C
	struct rsvd_page_cache_t lpspg_dpk_info;
	struct rsvd_page_cache_t lpspg_iqk_info;
#endif
#endif
	u8 current_lps_hw_port_id;

#ifdef CONFIG_RTW_CFGVENDOR_LLSTATS
	systime radio_on_start_time;
	systime pwr_saving_start_time;
	u32 pwr_saving_time;
	u32 on_time;
	u32 tx_time;
	u32 rx_time;
#endif /* CONFIG_RTW_CFGVENDOR_LLSTATS */

#ifdef CONFIG_LPS_ACK
	struct submit_ctx lps_ack_sctx;
	s8 lps_ack_status;
	_mutex lps_ack_mutex;
#endif /* CONFIG_LPS_ACK */
};

#define rtw_get_ips_mode_req(pwrctl) \
	(pwrctl)->ips_mode_req

#define rtw_ips_mode_req(pwrctl, ips_mode) \
	(pwrctl)->ips_mode_req = (ips_mode)

#define RTW_PWR_STATE_CHK_INTERVAL 2000

#define _rtw_set_pwr_state_check_timer(pwrctl, ms) \
	do { \
		/*RTW_INFO("%s _rtw_set_pwr_state_check_timer(%p, %d)\n", __FUNCTION__, (pwrctl), (ms));*/ \
		_set_timer(&(pwrctl)->pwr_state_check_timer, (ms)); \
	} while (0)

#define rtw_set_pwr_state_check_timer(pwrctl) \
	_rtw_set_pwr_state_check_timer((pwrctl), (pwrctl)->pwr_state_check_interval)

extern void rtw_init_pwrctrl_priv(_adapter *adapter);
extern void rtw_free_pwrctrl_priv(_adapter *adapter);

#ifdef CONFIG_LPS_LCLK
s32 rtw_register_task_alive(PADAPTER, u32 task);
void rtw_unregister_task_alive(PADAPTER, u32 task);
extern s32 rtw_register_tx_alive(PADAPTER padapter);
extern void rtw_unregister_tx_alive(PADAPTER padapter);
extern s32 rtw_register_rx_alive(PADAPTER padapter);
extern void rtw_unregister_rx_alive(PADAPTER padapter);
extern s32 rtw_register_cmd_alive(PADAPTER padapter);
extern void rtw_unregister_cmd_alive(PADAPTER padapter);
extern s32 rtw_register_evt_alive(PADAPTER padapter);
extern void rtw_unregister_evt_alive(PADAPTER padapter);
extern void cpwm_int_hdl(PADAPTER padapter, struct reportpwrstate_parm *preportpwrstate);
extern void LPS_Leave_check(PADAPTER padapter);
#endif

extern void LeaveAllPowerSaveMode(PADAPTER Adapter);
extern void LeaveAllPowerSaveModeDirect(PADAPTER Adapter);
#ifdef CONFIG_IPS
void _ips_enter(_adapter *padapter);
void ips_enter(_adapter *padapter);
int _ips_leave(_adapter *padapter);
int ips_leave(_adapter *padapter);
#endif

void rtw_ps_processor(_adapter *padapter);

#ifdef SUPPORT_HW_RFOFF_DETECTED
rt_rf_power_state RfOnOffDetect(PADAPTER pAdapter);
#endif


#ifdef DBG_CHECK_FW_PS_STATE
int rtw_fw_ps_state(PADAPTER padapter);
#endif

#ifdef CONFIG_LPS
extern const char * const LPS_CTRL_PHYDM;
void LPS_Enter(PADAPTER padapter, const char *msg);
void LPS_Leave(PADAPTER padapter, const char *msg);
void rtw_exec_lps(_adapter *padapter, u8 ps_mode);
void rtw_lps_rfon_ctrl(_adapter *padapter, u8 rfon_ctrl);
#ifdef CONFIG_CHECK_LEAVE_LPS
#ifdef CONFIG_LPS_CHK_BY_TP
void traffic_check_for_leave_lps_by_tp(PADAPTER padapter, u8 tx, struct sta_info *sta);
#endif
void traffic_check_for_leave_lps(PADAPTER padapter, u8 tx, u32 tx_packets);
#endif /*CONFIG_CHECK_LEAVE_LPS*/
void rtw_set_ps_mode(PADAPTER padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode, const char *msg);
void rtw_set_fw_in_ips_mode(PADAPTER padapter, u8 enable);
u8 rtw_set_rpwm(_adapter *padapter, u8 val8);
#ifdef CONFIG_WOWLAN
void rtw_wow_lps_level_decide(_adapter *adapter, u8 wow_en);
#endif /* CONFIG_WOWLAN */
#endif /* CONFIG_LPS */

#ifdef CONFIG_RESUME_IN_WORKQUEUE
void rtw_resume_in_workqueue(struct pwrctrl_priv *pwrpriv);
#endif /* CONFIG_RESUME_IN_WORKQUEUE */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
bool rtw_is_earlysuspend_registered(struct pwrctrl_priv *pwrpriv);
bool rtw_is_do_late_resume(struct pwrctrl_priv *pwrpriv);
void rtw_set_do_late_resume(struct pwrctrl_priv *pwrpriv, bool enable);
void rtw_register_early_suspend(struct pwrctrl_priv *pwrpriv);
void rtw_unregister_early_suspend(struct pwrctrl_priv *pwrpriv);
#else
#define rtw_is_earlysuspend_registered(pwrpriv) _FALSE
#define rtw_is_do_late_resume(pwrpriv) _FALSE
#define rtw_set_do_late_resume(pwrpriv, enable) do {} while (0)
#define rtw_register_early_suspend(pwrpriv) do {} while (0)
#define rtw_unregister_early_suspend(pwrpriv) do {} while (0)
#endif /* CONFIG_HAS_EARLYSUSPEND || CONFIG_ANDROID_POWER */

u8 rtw_interface_ps_func(_adapter *padapter, HAL_INTF_PS_FUNC efunc_id, u8 *val);
void rtw_set_ips_deny(_adapter *padapter, u32 ms);
int _rtw_pwr_wakeup(_adapter *padapter, u32 ips_deffer_ms, const char *caller);
#define rtw_pwr_wakeup(adapter) _rtw_pwr_wakeup(adapter, RTW_PWR_STATE_CHK_INTERVAL, __FUNCTION__)
#define rtw_pwr_wakeup_ex(adapter, ips_deffer_ms) _rtw_pwr_wakeup(adapter, ips_deffer_ms, __FUNCTION__)
int rtw_pm_set_ips(_adapter *padapter, u8 mode);
int rtw_pm_set_lps(_adapter *padapter, u8 mode);
int rtw_pm_set_lps_level(_adapter *padapter, u8 level);
#ifdef CONFIG_LPS_1T1R
int rtw_pm_set_lps_1t1r(_adapter *padapter, u8 en);
#endif
void rtw_set_lps_deny(_adapter *adapter, u32 ms);
#ifdef CONFIG_WOWLAN
int rtw_pm_set_wow_lps(_adapter *padapter, u8 mode);
int rtw_pm_set_wow_lps_level(_adapter *padapter, u8 level);
#ifdef CONFIG_LPS_1T1R
int rtw_pm_set_wow_lps_1t1r(_adapter *padapter, u8 en);
#endif
#endif /* CONFIG_WOWLAN */

void rtw_ps_deny(PADAPTER padapter, PS_DENY_REASON reason);
void rtw_ps_deny_cancel(PADAPTER padapter, PS_DENY_REASON reason);
u32 rtw_ps_deny_get(PADAPTER padapter);

#if defined(CONFIG_WOWLAN)
void rtw_get_current_ip_address(PADAPTER padapter, u8 *pcurrentip);
void rtw_get_sec_iv(PADAPTER padapter, u8 *pcur_dot11txpn, u8 *StaAddr);
bool rtw_wowlan_parser_pattern_cmd(u8 *input, char *pattern,
				int *pattern_len, char *bit_mask);
void rtw_wow_pattern_sw_reset(_adapter *adapter);
u8 rtw_set_default_pattern(_adapter *adapter);
void rtw_wow_pattern_sw_dump(_adapter *adapter);
#ifdef CONFIG_WAR_OFFLOAD
#if defined(CONFIG_OFFLOAD_MDNS_V4) || defined(CONFIG_OFFLOAD_MDNS_V6)
void rtw_wow_war_mdns_dump_buf(struct seq_file *m, u8 *title, u8 *buf, u32 len);
void rtw_wow_war_mdns_dump_txt(struct seq_file *m, u8 *title, u8 *buf, u32 len);
bool rtw_wow_war_mdns_parser_pattern(u8 *input, char *target, u32 *target_len, u32 max_len);
void rtw_wow_war_mdns_parms_reset(_adapter *adapter, u8 is_set_default);
#endif /* defined(CONFIG_OFFLOAD_MDNS_V4) || defined(CONFIG_OFFLOAD_MDNS_V6) */
#endif /* CONFIG_WAR_OFFLOAD */

#endif /* CONFIG_WOWLAN */
void rtw_ssmps_enter(_adapter *adapter, struct sta_info *sta);
void rtw_ssmps_leave(_adapter *adapter, struct sta_info *sta);
#endif /* __RTL871X_PWRCTRL_H_ */
