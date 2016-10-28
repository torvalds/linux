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
 ******************************************************************************/
#ifndef __RTW_PWRCTRL_H_
#define __RTW_PWRCTRL_H_

#include <osdep_service.h>
#include <drv_types.h>

#define FW_PWR0		0
#define FW_PWR1		1
#define FW_PWR2		2
#define FW_PWR3		3
#define HW_PWR0		7
#define HW_PWR1		6
#define HW_PWR2		2
#define HW_PWR3		0
#define HW_PWR4		8

#define FW_PWRMSK	0x7

#define XMIT_ALIVE	BIT(0)
#define RECV_ALIVE	BIT(1)
#define CMD_ALIVE	BIT(2)
#define EVT_ALIVE	BIT(3)

enum power_mgnt {
	PS_MODE_ACTIVE = 0,
	PS_MODE_MIN,
	PS_MODE_MAX,
	PS_MODE_DTIM,
	PS_MODE_VOIP,
	PS_MODE_UAPSD_WMM,
	PS_MODE_UAPSD,
	PS_MODE_IBSS,
	PS_MODE_WWLAN,
	PM_Radio_Off,
	PM_Card_Disable,
	PS_MODE_NUM
};

/*
	BIT[2:0] = HW state
	BIT[3] = Protocol PS state,   0: register active state,
				      1: register sleep state
	BIT[4] = sub-state
*/

#define PS_DPS			BIT(0)
#define PS_LCLK			(PS_DPS)
#define PS_RF_OFF		BIT(1)
#define PS_ALL_ON		BIT(2)
#define PS_ST_ACTIVE		BIT(3)

#define PS_ISR_ENABLE		BIT(4)
#define PS_IMR_ENABLE		BIT(5)
#define PS_ACK			BIT(6)
#define PS_TOGGLE		BIT(7)

#define PS_STATE_MASK		(0x0F)
#define PS_STATE_HW_MASK	(0x07)
#define PS_SEQ_MASK		(0xc0)

#define PS_STATE(x)		(PS_STATE_MASK & (x))
#define PS_STATE_HW(x)		(PS_STATE_HW_MASK & (x))
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

#define LPS_DELAY_TIME	1*HZ /*  1 sec */

#define EXE_PWR_NONE	0x01
#define EXE_PWR_IPS		0x02
#define EXE_PWR_LPS		0x04

/*  RF state. */
enum rt_rf_power_state {
	rf_on,		/*  RF is on after RFSleep or RFOff */
	rf_sleep,	/*  802.11 Power Save mode */
	rf_off,		/*  HW/SW Radio OFF or Inactive Power Save */
	/* Add the new RF state above this line===== */
	rf_max
};

/*  RF Off Level for IPS or HW/SW radio off */
#define	RT_RF_OFF_LEVL_ASPM		BIT(0)	/* PCI ASPM */
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT(1)	/* PCI clock request */
#define	RT_RF_OFF_LEVL_PCI_D3		BIT(2)	/* PCI D3 mode */
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT(3)	/* NIC halt, re-init hw param*/
#define	RT_RF_OFF_LEVL_FREE_FW		BIT(4)	/* FW free, re-download the FW*/
#define	RT_RF_OFF_LEVL_FW_32K		BIT(5)	/* FW in 32k */
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT(6)	/* Always enable ASPM and Clock
						 * Req in initialization. */
#define	RT_RF_LPS_DISALBE_2R		BIT(30)	/* When LPS is on, disable 2R
						 * if no packet is RX or TX. */
#define	RT_RF_LPS_LEVEL_ASPM		BIT(31)	/* LPS with ASPM */

#define	RT_IN_PS_LEVEL(ppsc, _PS_FLAG)				\
	((ppsc->cur_ps_level & _PS_FLAG) ? true : false)
#define	RT_CLEAR_PS_LEVEL(ppsc, _PS_FLAG)			\
	(ppsc->cur_ps_level &= (~(_PS_FLAG)))
#define	RT_SET_PS_LEVEL(ppsc, _PS_FLAG)				\
	(ppsc->cur_ps_level |= _PS_FLAG)

enum _PS_BBRegBackup_ {
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
};

struct pwrctrl_priv {
	struct mutex mutex_lock;
	volatile u8 rpwm; /*  requested power state for fw */
	volatile u8 cpwm; /*  fw current power state. updated when
			   * 1. read from HCPWM 2. driver lowers power level */
	volatile u8 tog; /*  toggling */
	volatile u8 cpwm_tog; /*  toggling */

	u8	pwr_mode;
	u8	smart_ps;
	u8	bcn_ant_mode;

	u32	alives;
	struct work_struct cpwm_event;
	u8	bpower_saving;

	u8	b_hw_radio_off;
	u8	reg_rfoff;
	u8	reg_pdnmode; /* powerdown mode */
	u32	rfoff_reason;

	/* RF OFF Level */
	u32	cur_ps_level;
	u32	reg_rfps_level;
	uint	ips_enter_cnts;
	uint	ips_leave_cnts;

	u8	ips_mode;
	u8	ips_mode_req;	/*  used to accept the mode setting request,
				 *  will update to ipsmode later */
	uint bips_processing;
	unsigned long ips_deny_time; /* will deny IPS when system time less than this */
	u8 ps_processing; /* temp used to mark whether in rtw_ps_processor */

	u8	bLeisurePs;
	u8	LpsIdleCount;
	u8	power_mgnt;
	u8	bFwCurrentInPSMode;
	u32	DelayLPSLastTimeStamp;
	u8	btcoex_rfon;
	s32		pnp_current_pwr_state;
	u8		pnp_bstop_trx;

	u8		bInternalAutoSuspend;
	u8		bInSuspend;
	u8		bSupportRemoteWakeup;
	struct timer_list pwr_state_check_timer;
	int		pwr_state_check_interval;
	u8		pwr_state_check_cnts;

	int		ps_flag;

	enum rt_rf_power_state	rf_pwrstate;/* cur power state */
	enum rt_rf_power_state	change_rfpwrstate;

	u8		wepkeymask;
	u8		bHWPowerdown;/* if support hw power down */
	u8		bHWPwrPindetect;
	u8		bkeepfwalive;
	u8		brfoffbyhw;
	unsigned long PS_BBRegBackup[PSBBREG_TOTALCNT];
};

#define rtw_get_ips_mode_req(pwrctrlpriv) \
	(pwrctrlpriv)->ips_mode_req

#define rtw_ips_mode_req(pwrctrlpriv, ips_mode) \
	((pwrctrlpriv)->ips_mode_req = (ips_mode))

#define RTW_PWR_STATE_CHK_INTERVAL 2000

#define _rtw_set_pwr_state_check_timer(pwrctrlpriv, ms) \
	mod_timer(&pwrctrlpriv->pwr_state_check_timer,	\
		  jiffies + msecs_to_jiffies(ms))

#define rtw_set_pwr_state_check_timer(pwrctrl)			\
	_rtw_set_pwr_state_check_timer((pwrctrl),		\
				       (pwrctrl)->pwr_state_check_interval)

void rtw_init_pwrctrl_priv(struct adapter *adapter);

void rtw_set_ps_mode(struct adapter *adapter, u8 ps_mode, u8 smart_ps,
		     u8 bcn_ant_mode);
void rtw_set_rpwm(struct adapter *adapter, u8 val8);
void LeaveAllPowerSaveMode(struct adapter *adapter);
void ips_enter(struct adapter *padapter);
int ips_leave(struct adapter *padapter);

void rtw_ps_processor(struct adapter *padapter);

enum rt_rf_power_state RfOnOffDetect(struct adapter *iadapter);

s32 LPS_RF_ON_check(struct adapter *adapter, u32 delay_ms);
void LPS_Enter(struct adapter *adapter);
void LPS_Leave(struct adapter *adapter);

int _rtw_pwr_wakeup(struct adapter *adapter, u32 ips_defer_ms,
		    const char *caller);
#define rtw_pwr_wakeup(adapter)						\
	 _rtw_pwr_wakeup(adapter, RTW_PWR_STATE_CHK_INTERVAL, __func__)
#define rtw_pwr_wakeup_ex(adapter, ips_deffer_ms)			\
	 _rtw_pwr_wakeup(adapter, ips_deffer_ms, __func__)
int rtw_pm_set_ips(struct adapter *adapter, u8 mode);
int rtw_pm_set_lps(struct adapter *adapter, u8 mode);

#endif  /* __RTL871X_PWRCTRL_H_ */
