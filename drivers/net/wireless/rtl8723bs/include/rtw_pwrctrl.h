/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#ifndef __RTW_PWRCTRL_H_
#define __RTW_PWRCTRL_H_


#define FW_PWR0	0	
#define FW_PWR1 	1
#define FW_PWR2 	2
#define FW_PWR3 	3


#define HW_PWR0	7	
#define HW_PWR1 	6
#define HW_PWR2 	2
#define HW_PWR3	0
#define HW_PWR4	8

#define FW_PWRMSK	0x7


#define XMIT_ALIVE	BIT(0)
#define RECV_ALIVE	BIT(1)
#define CMD_ALIVE	BIT(2)
#define EVT_ALIVE	BIT(3)
#ifdef CONFIG_BT_COEXIST
#define BTCOEX_ALIVE	BIT(4)
#endif // CONFIG_BT_COEXIST


enum Power_Mgnt
{
	PS_MODE_ACTIVE	= 0	,
	PS_MODE_MIN			,
	PS_MODE_MAX			,
	PS_MODE_DTIM			,	//PS_MODE_SELF_DEFINED
	PS_MODE_VOIP			,
	PS_MODE_UAPSD_WMM	,
	PS_MODE_UAPSD			,
	PS_MODE_IBSS			,
	PS_MODE_WWLAN		,
	PM_Radio_Off			,
	PM_Card_Disable		,
	PS_MODE_NUM,
};

#ifdef CONFIG_PNO_SUPPORT
#define MAX_PNO_LIST_COUNT 16
#define MAX_SCAN_LIST_COUNT 14 //2.4G only
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
	unsigned char state; //the CPWM value
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

#define LPS_DELAY_TIME	1*HZ // 1 sec

#define EXE_PWR_NONE	0x01
#define EXE_PWR_IPS		0x02
#define EXE_PWR_LPS		0x04

// RF state.
typedef enum _rt_rf_power_state
{
	rf_on,		// RF is on after RFSleep or RFOff
	rf_sleep,	// 802.11 Power Save mode
	rf_off,		// HW/SW Radio OFF or Inactive Power Save
	//=====Add the new RF state above this line=====//
	rf_max
}rt_rf_power_state;

// RF Off Level for IPS or HW/SW radio off
#define	RT_RF_OFF_LEVL_ASPM			BIT(0)	// PCI ASPM
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT(1)	// PCI clock request
#define	RT_RF_OFF_LEVL_PCI_D3			BIT(2)	// PCI D3 mode
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT(3)	// NIC halt, re-initialize hw parameters
#define	RT_RF_OFF_LEVL_FREE_FW		BIT(4)	// FW free, re-download the FW
#define	RT_RF_OFF_LEVL_FW_32K		BIT(5)	// FW in 32k
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT(6)	// Always enable ASPM and Clock Req in initialization.
#define	RT_RF_LPS_DISALBE_2R			BIT(30)	// When LPS is on, disable 2R if no packet is received or transmittd.
#define	RT_RF_LPS_LEVEL_ASPM			BIT(31)	// LPS with ASPM

#define	RT_IN_PS_LEVEL(ppsc, _PS_FLAG)		((ppsc->cur_ps_level & _PS_FLAG) ? _TRUE : _FALSE)
#define	RT_CLEAR_PS_LEVEL(ppsc, _PS_FLAG)	(ppsc->cur_ps_level &= (~(_PS_FLAG)))
#define	RT_SET_PS_LEVEL(ppsc, _PS_FLAG)		(ppsc->cur_ps_level |= _PS_FLAG)

// ASPM OSC Control bit, added by Roger, 2013.03.29.
#define	RT_PCI_ASPM_OSC_IGNORE		0	 // PCI ASPM ignore OSC control in default
#define	RT_PCI_ASPM_OSC_ENABLE		BIT0 // PCI ASPM controlled by OS according to ACPI Spec 5.0
#define	RT_PCI_ASPM_OSC_DISABLE		BIT1 // PCI ASPM controlled by driver or BIOS, i.e., force enable ASPM


enum _PS_BBRegBackup_ {
	PSBBREG_RF0 = 0,
	PSBBREG_RF1,
	PSBBREG_RF2,
	PSBBREG_AFE0,
	PSBBREG_TOTALCNT
};

enum { // for ips_mode
	IPS_NONE=0,
	IPS_NORMAL,
	IPS_LEVEL_2,	
	IPS_NUM
};

// Design for pwrctrl_priv.ips_deny, 32 bits for 32 reasons at most
typedef enum _PS_DENY_REASON
{
	PS_DENY_DRV_INITIAL = 0,
	PS_DENY_SCAN,
	PS_DENY_JOIN,
	PS_DENY_DISCONNECT,
	PS_DENY_SUSPEND,
	PS_DENY_IOCTL,
	PS_DENY_MGNT_TX,
	PS_DENY_DRV_REMOVE = 30,
	PS_DENY_OTHERS = 31
} PS_DENY_REASON;

#ifdef CONFIG_PNO_SUPPORT
typedef struct pno_nlo_info
{
	u32 fast_scan_period;				//Fast scan period
	u32	ssid_num;				//number of entry
	u32	slow_scan_period;			//slow scan period
	u32	fast_scan_iterations;			//Fast scan iterations
	u8	ssid_length[MAX_PNO_LIST_COUNT];	//SSID Length Array
	u8	ssid_chiper_info[MAX_PNO_LIST_COUNT];	//Chiper information for security
	u8	ssid_channel_info[MAX_PNO_LIST_COUNT];	//channel information
}pno_nlo_info_t;	

typedef struct pno_ssid {
	u32		SSID_len;
	u8		SSID[32];
} pno_ssid_t;

typedef struct pno_ssid_list {
	pno_ssid_t	node[MAX_PNO_LIST_COUNT];
}pno_ssid_list_t;

typedef struct pno_scan_channel_info
{
	u8	channel;
	u8	tx_power;
	u8	timeout;
	u8	active;				//set 1 means active scan, or pasivite scan.
}pno_scan_channel_info_t;

typedef struct pno_scan_info
{
	u8	enableRFE;			//Enable RFE
	u8	period_scan_time;		//exclusive with fast_scan_period and slow_scan_period
	u8	periodScan;			//exclusive with fast_scan_period and slow_scan_period
	u8	orig_80_offset;			//original channel 80 offset
	u8	orig_40_offset;			//original channel 40 offset
	u8	orig_bw;			//original bandwidth
	u8	orig_ch;			//original channel
	u8	channel_num;			//number of channel
	u64	rfe_type;			//rfe_type && 0x00000000000000ff
	pno_scan_channel_info_t ssid_channel_info[MAX_SCAN_LIST_COUNT];
}pno_scan_info_t;
#endif //CONFIG_PNO_SUPPORT

struct pwrctrl_priv
{
	_pwrlock	lock;
	_pwrlock	check_32k_lock;
	volatile u8 rpwm; // requested power state for fw
	volatile u8 cpwm; // fw current power state. updated when 1. read from HCPWM 2. driver lowers power level
	volatile u8 tog; // toggling
	volatile u8 cpwm_tog; // toggling

	u8	pwr_mode;
	u8	smart_ps;
	u8	bcn_ant_mode;
	u8 	dtim;

	u32	alives;
	_workitem cpwm_event;
#ifdef CONFIG_LPS_RPWM_TIMER
	u8 brpwmtimeout;
	_workitem rpwmtimeoutwi;
	_timer pwr_rpwm_timer;
#endif // CONFIG_LPS_RPWM_TIMER
	u8	bpower_saving; //for LPS/IPS

	u8	b_hw_radio_off;
	u8	reg_rfoff;
	u8	reg_pdnmode; //powerdown mode
	u32	rfoff_reason;

	//RF OFF Level
	u32	cur_ps_level;
	u32	reg_rfps_level;

	uint 	ips_enter_cnts;
	uint 	ips_leave_cnts;

	u8	ips_mode; 
	u8	ips_mode_req; // used to accept the mode setting request, will update to ipsmode later
	uint bips_processing;
	u32 ips_deny_time; /* will deny IPS when system time is smaller than this */

	// ps_deny: if 0, power save is free to go; otherwise deny all kinds of power save.
	// Use PS_DENY_REASON to decide reason.
	// Don't access this variable directly without control function,
	// and this variable should be protected by lock.
	u32 ps_deny;

	u8 ps_processing; /* temporarily used to mark whether in rtw_ps_processor */

	u8 fw_psmode_iface_id;
	u8	bLeisurePs;
	u8	LpsIdleCount;
	u8	power_mgnt;
	u8	bFwCurrentInPSMode;
	u32	DelayLPSLastTimeStamp;
	s32		pnp_current_pwr_state;
	u8		pnp_bstop_trx;


	u8		bInternalAutoSuspend;
	u8		bInSuspend;
#ifdef	CONFIG_BT_COEXIST
	u8		bAutoResume;
	u8		autopm_cnt;
#endif
	u8		bSupportRemoteWakeup;	
	u8		wowlan_wake_reason;
	u8		wowlan_ap_mode;
#ifdef CONFIG_WOWLAN
	u8		wowlan_mode;	u8		wowlan_pattern;
	u8		wowlan_magic;
	u8		wowlan_unicast;
	u8		wowlan_pattern_idx;
	u8		wowlan_pno_enable;
#ifdef CONFIG_PNO_SUPPORT
	u8		pno_in_resume;
	pno_nlo_info_t	*pnlo_info;
	pno_scan_info_t	*pscan_info;
	pno_ssid_list_t	*pno_ssid_list;
#endif
	u32		wowlan_pattern_context[8][5];
	u64		wowlan_fw_iv;
#endif // CONFIG_WOWLAN
	_timer 	pwr_state_check_timer;
	int		pwr_state_check_interval;
	u8		pwr_state_check_cnts;

	int 		ps_flag; /* used by autosuspend */
	
	rt_rf_power_state	rf_pwrstate;//cur power state, only for IPS
	//rt_rf_power_state 	current_rfpwrstate;
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
	#endif //CONFIG_HAS_EARLYSUSPEND
	
	#ifdef CONFIG_ANDROID_POWER
	android_early_suspend_t early_suspend;
	u8 do_late_resume;
	#endif

	#ifdef CONFIG_INTEL_PROXIM
	u8	stored_power_mgnt;
	#endif
};

#define rtw_get_ips_mode_req(pwrctl) \
	(pwrctl)->ips_mode_req

#define rtw_ips_mode_req(pwrctl, ips_mode) \
	(pwrctl)->ips_mode_req = (ips_mode)

#define RTW_PWR_STATE_CHK_INTERVAL 2000

#define _rtw_set_pwr_state_check_timer(pwrctl, ms) \
	do { \
		/*DBG_871X("%s _rtw_set_pwr_state_check_timer(%p, %d)\n", __FUNCTION__, (pwrctl), (ms));*/ \
		_set_timer(&(pwrctl)->pwr_state_check_timer, (ms)); \
	} while(0)
	
#define rtw_set_pwr_state_check_timer(pwrctl) \
	_rtw_set_pwr_state_check_timer((pwrctl), (pwrctl)->pwr_state_check_interval)

extern void rtw_init_pwrctrl_priv(_adapter *adapter);
extern void rtw_free_pwrctrl_priv(_adapter * adapter);

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

extern void rtw_set_ps_mode(PADAPTER padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode, const char *msg);
extern void rtw_set_rpwm(_adapter * padapter, u8 val8);
extern void LeaveAllPowerSaveMode(PADAPTER Adapter);
extern void LeaveAllPowerSaveModeDirect(PADAPTER Adapter);
#ifdef CONFIG_IPS
void _ips_enter(_adapter * padapter);
void ips_enter(_adapter * padapter);
int _ips_leave(_adapter * padapter);
int ips_leave(_adapter * padapter);
#endif

void rtw_ps_processor(_adapter*padapter);

#ifdef CONFIG_AUTOSUSPEND
int autoresume_enter(_adapter* padapter);
#endif
#ifdef SUPPORT_HW_RFOFF_DETECTED
rt_rf_power_state RfOnOffDetect(IN	PADAPTER pAdapter );
#endif


int rtw_fw_ps_state(PADAPTER padapter);

#ifdef CONFIG_LPS
s32 LPS_RF_ON_check(PADAPTER padapter, u32 delay_ms);
void LPS_Enter(PADAPTER padapter, const char *msg);
void LPS_Leave(PADAPTER padapter, const char *msg);
void	traffic_check_for_leave_lps(PADAPTER padapter, u8 tx, u32 tx_packets);
#endif

#ifdef CONFIG_RESUME_IN_WORKQUEUE
void rtw_resume_in_workqueue(struct pwrctrl_priv *pwrpriv);
#endif //CONFIG_RESUME_IN_WORKQUEUE

#if defined(CONFIG_HAS_EARLYSUSPEND ) || defined(CONFIG_ANDROID_POWER)
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

u8 rtw_interface_ps_func(_adapter *padapter,HAL_INTF_PS_FUNC efunc_id,u8* val);
void rtw_set_ips_deny(_adapter *padapter, u32 ms);
int _rtw_pwr_wakeup(_adapter *padapter, u32 ips_deffer_ms, const char *caller);
#define rtw_pwr_wakeup(adapter) _rtw_pwr_wakeup(adapter, RTW_PWR_STATE_CHK_INTERVAL, __FUNCTION__)
#define rtw_pwr_wakeup_ex(adapter, ips_deffer_ms) _rtw_pwr_wakeup(adapter, ips_deffer_ms, __FUNCTION__)
int rtw_pm_set_ips(_adapter *padapter, u8 mode);
int rtw_pm_set_lps(_adapter *padapter, u8 mode);

void rtw_ps_deny(PADAPTER padapter, PS_DENY_REASON reason);
void rtw_ps_deny_cancel(PADAPTER padapter, PS_DENY_REASON reason);
u32 rtw_ps_deny_get(PADAPTER padapter);

#endif  //__RTL871X_PWRCTRL_H_

