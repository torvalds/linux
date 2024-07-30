/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_PWRCTRL_H_
#define __RTW_PWRCTRL_H_

#include <linux/mutex.h>

#define XMIT_ALIVE	BIT(0)
#define CMD_ALIVE	BIT(2)
#define BTCOEX_ALIVE	BIT(4)


enum {
	PS_MODE_ACTIVE	= 0,
	PS_MODE_MIN,
	PS_MODE_MAX,
	PS_MODE_DTIM,	/* PS_MODE_SELF_DEFINED */
	PS_MODE_VOIP,
	PS_MODE_UAPSD_WMM,
	PS_MODE_UAPSD,
	PS_MODE_IBSS,
	PS_MODE_WWLAN,
	PM_Radio_Off,
	PM_Card_Disable,
	PS_MODE_NUM,
};

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

#define PS_ACK				BIT(6)
#define PS_TOGGLE			BIT(7)

#define PS_STATE_MASK		(0x0F)

#define PS_STATE(x)		(PS_STATE_MASK & (x))

#define PS_STATE_S0		(PS_DPS)
#define PS_STATE_S2		(PS_RF_OFF)
#define PS_STATE_S4		((PS_ST_ACTIVE) | (PS_ALL_ON))

struct reportpwrstate_parm {
	unsigned char mode;
	unsigned char state; /* the CPWM value */
	unsigned short rsvd;
};

#define LPS_DELAY_TIME	(1 * HZ) /*  1 sec */

/*  RF state. */
enum rt_rf_power_state {
	rf_on,		/*  RF is on after RFSleep or RFOff */
	rf_sleep,	/*  802.11 Power Save mode */
	rf_off,		/*  HW/SW Radio OFF or Inactive Power Save */
	/* Add the new RF state above this line ===== */
	rf_max
};

/*  RF Off Level for IPS or HW/SW radio off */
#define	RT_RF_OFF_LEVL_ASPM			BIT(0)	/*  PCI ASPM */
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT(1)	/*  PCI clock request */
#define	RT_RF_OFF_LEVL_PCI_D3			BIT(2)	/*  PCI D3 mode */
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT(3)	/*  NIC halt, re-initialize hw parameters */
#define	RT_RF_OFF_LEVL_FREE_FW		BIT(4)	/*  FW free, re-download the FW */
#define	RT_RF_OFF_LEVL_FW_32K		BIT(5)	/*  FW in 32k */
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT(6)	/*  Always enable ASPM and Clock Req in initialization. */
#define	RT_RF_LPS_DISALBE_2R			BIT(30)	/*  When LPS is on, disable 2R if no packet is received or transmitted. */
#define	RT_RF_LPS_LEVEL_ASPM			BIT(31)	/*  LPS with ASPM */

#define	RT_IN_PS_LEVEL(ppsc, _PS_FLAG)		((ppsc->cur_ps_level & _PS_FLAG) ? true : false)
#define	RT_CLEAR_PS_LEVEL(ppsc, _PS_FLAG)	(ppsc->cur_ps_level &= (~(_PS_FLAG)))
#define	RT_SET_PS_LEVEL(ppsc, _PS_FLAG)		(ppsc->cur_ps_level |= _PS_FLAG)

/*  ASPM OSC Control bit, added by Roger, 2013.03.29. */
#define	RT_PCI_ASPM_OSC_IGNORE		0	 /*  PCI ASPM ignore OSC control in default */
#define	RT_PCI_ASPM_OSC_ENABLE		BIT0 /*  PCI ASPM controlled by OS according to ACPI Spec 5.0 */
#define	RT_PCI_ASPM_OSC_DISABLE		BIT1 /*  PCI ASPM controlled by driver or BIOS, i.e., force enable ASPM */

enum {
	PSBBREG_RF0 = 0,
	PSBBREG_RF1,
	PSBBREG_RF2,
	PSBBREG_AFE0,
	PSBBREG_TOTALCNT
};

enum { /*  for ips_mode */
	IPS_NONE = 0,
	IPS_NORMAL,
	IPS_LEVEL_2,
	IPS_NUM
};

/*  Design for pwrctrl_priv.ips_deny, 32 bits for 32 reasons at most */
enum ps_deny_reason {
	PS_DENY_DRV_INITIAL = 0,
	PS_DENY_SCAN,
	PS_DENY_JOIN,
	PS_DENY_DISCONNECT,
	PS_DENY_SUSPEND,
	PS_DENY_IOCTL,
	PS_DENY_MGNT_TX,
	PS_DENY_DRV_REMOVE = 30,
	PS_DENY_OTHERS = 31
};

struct pwrctrl_priv {
	struct mutex lock;
	volatile u8 rpwm; /*  requested power state for fw */
	volatile u8 cpwm; /*  fw current power state. updated when 1. read from HCPWM 2. driver lowers power level */
	volatile u8 tog; /*  toggling */
	volatile u8 cpwm_tog; /*  toggling */

	u8 pwr_mode;
	u8 smart_ps;
	u8 bcn_ant_mode;
	u8 dtim;

	u32 alives;
	struct work_struct cpwm_event;
	u8 brpwmtimeout;
	struct work_struct rpwmtimeoutwi;
	struct timer_list pwr_rpwm_timer;
	u8 bpower_saving; /* for LPS/IPS */

	u8 b_hw_radio_off;
	u8 reg_rfoff;
	u8 reg_pdnmode; /* powerdown mode */
	u32 rfoff_reason;

	/* RF OFF Level */
	u32 cur_ps_level;
	u32 reg_rfps_level;

	uint	ips_enter_cnts;
	uint	ips_leave_cnts;

	u8 ips_mode;
	u8 ips_org_mode;
	u8 ips_mode_req; /*  used to accept the mode setting request, will update to ipsmode later */
	bool bips_processing;
	unsigned long ips_deny_time; /* will deny IPS when system time is smaller than this */
	u8 pre_ips_type;/*  0: default flow, 1: carddisbale flow */

	/*  ps_deny: if 0, power save is free to go; otherwise deny all kinds of power save. */
	/*  Use enum ps_deny_reason to decide reason. */
	/*  Don't access this variable directly without control function, */
	/*  and this variable should be protected by lock. */
	u32 ps_deny;

	u8 ps_processing; /* temporarily used to mark whether in rtw_ps_processor */

	u8 fw_psmode_iface_id;
	u8 bLeisurePs;
	u8 LpsIdleCount;
	u8 power_mgnt;
	u8 org_power_mgnt;
	bool fw_current_in_ps_mode;
	unsigned long	DelayLPSLastTimeStamp;
	s32		pnp_current_pwr_state;
	u8 pnp_bstop_trx;


	u8 bInternalAutoSuspend;
	u8 bInSuspend;

	u8 bAutoResume;
	u8 autopm_cnt;

	u8 bSupportRemoteWakeup;
	u8 wowlan_wake_reason;
	u8 wowlan_ap_mode;
	u8 wowlan_mode;
	struct timer_list	pwr_state_check_timer;
	struct adapter *adapter;
	int		pwr_state_check_interval;
	u8 pwr_state_check_cnts;

	int		ps_flag; /* used by autosuspend */

	enum rt_rf_power_state	rf_pwrstate;/* cur power state, only for IPS */
	/* rt_rf_power_state	current_rfpwrstate; */
	enum rt_rf_power_state	change_rfpwrstate;

	u8 bHWPowerdown; /* power down mode selection. 0:radio off, 1:power down */
	u8 bHWPwrPindetect; /* come from registrypriv.hwpwrp_detect. enable power down function. 0:disable, 1:enable */
	u8 bkeepfwalive;
	u8 brfoffbyhw;
	unsigned long PS_BBRegBackup[PSBBREG_TOTALCNT];
};

#define rtw_ips_mode_req(pwrctl, ips_mode) \
	((pwrctl)->ips_mode_req = (ips_mode))

#define RTW_PWR_STATE_CHK_INTERVAL 2000

#define _rtw_set_pwr_state_check_timer(pwrctl, ms) \
	do { \
		_set_timer(&(pwrctl)->pwr_state_check_timer, (ms)); \
	} while (0)

extern void rtw_init_pwrctrl_priv(struct adapter *adapter);
extern void rtw_free_pwrctrl_priv(struct adapter *adapter);

s32 rtw_register_task_alive(struct adapter *, u32 task);
void rtw_unregister_task_alive(struct adapter *, u32 task);
extern s32 rtw_register_tx_alive(struct adapter *padapter);
extern void rtw_unregister_tx_alive(struct adapter *padapter);
extern s32 rtw_register_cmd_alive(struct adapter *padapter);
extern void rtw_unregister_cmd_alive(struct adapter *padapter);
extern void cpwm_int_hdl(struct adapter *padapter, struct reportpwrstate_parm *preportpwrstate);
extern void LPS_Leave_check(struct adapter *padapter);

extern void LeaveAllPowerSaveMode(struct adapter *Adapter);
extern void LeaveAllPowerSaveModeDirect(struct adapter *Adapter);
void _ips_enter(struct adapter *padapter);
void ips_enter(struct adapter *padapter);
int _ips_leave(struct adapter *padapter);
int ips_leave(struct adapter *padapter);

void rtw_ps_processor(struct adapter *padapter);

s32 LPS_RF_ON_check(struct adapter *padapter, u32 delay_ms);
void LPS_Enter(struct adapter *padapter, const char *msg);
void LPS_Leave(struct adapter *padapter, const char *msg);
void traffic_check_for_leave_lps(struct adapter *padapter, u8 tx, u32 tx_packets);
void rtw_set_ps_mode(struct adapter *padapter, u8 ps_mode, u8 smart_ps, u8 bcn_ant_mode, const char *msg);
void rtw_set_rpwm(struct adapter *padapter, u8 val8);

void rtw_set_ips_deny(struct adapter *padapter, u32 ms);
int _rtw_pwr_wakeup(struct adapter *padapter, u32 ips_deffer_ms, const char *caller);
#define rtw_pwr_wakeup(adapter) _rtw_pwr_wakeup(adapter, RTW_PWR_STATE_CHK_INTERVAL, __func__)
#define rtw_pwr_wakeup_ex(adapter, ips_deffer_ms) _rtw_pwr_wakeup(adapter, ips_deffer_ms, __func__)
int rtw_pm_set_ips(struct adapter *padapter, u8 mode);
int rtw_pm_set_lps(struct adapter *padapter, u8 mode);

void rtw_ps_deny(struct adapter *padapter, enum ps_deny_reason reason);
void rtw_ps_deny_cancel(struct adapter *padapter, enum ps_deny_reason reason);
u32 rtw_ps_deny_get(struct adapter *padapter);

#endif  /* __RTL871X_PWRCTRL_H_ */
