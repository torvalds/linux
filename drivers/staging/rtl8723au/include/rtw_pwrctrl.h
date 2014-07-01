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

enum Power_Mgnt {
	PS_MODE_ACTIVE	= 0,
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


/* BIT[2:0] = HW state
 * BIT[3] = Protocol PS state,  0: active, 1: sleep state
 * BIT[4] = sub-state
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

#define LPS_DELAY_TIME	(1*HZ) /*  1 sec */

#define EXE_PWR_NONE		0x01
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
#define	RT_RF_OFF_LEVL_ASPM		BIT(0)	/*  PCI ASPM */
#define	RT_RF_OFF_LEVL_CLK_REQ		BIT(1)	/*  PCI clock request */
#define	RT_RF_OFF_LEVL_PCI_D3		BIT(2)	/*  PCI D3 mode */
/* NIC halt, re-init hw params */
#define	RT_RF_OFF_LEVL_HALT_NIC		BIT(3)
/*  FW free, re-download the FW */
#define	RT_RF_OFF_LEVL_FREE_FW		BIT(4)
#define	RT_RF_OFF_LEVL_FW_32K		BIT(5)	/*  FW in 32k */
/*  Always enable ASPM and Clock Req in initialization. */
#define	RT_RF_PS_LEVEL_ALWAYS_ASPM	BIT(6)
/*  When LPS is on, disable 2R if no packet is received or transmittd. */
#define	RT_RF_LPS_DISALBE_2R		BIT(30)
#define	RT_RF_LPS_LEVEL_ASPM		BIT(31)	/*  LPS with ASPM */

#define	RT_IN_PS_LEVEL(ppsc, _PS_FLAG)				\
	((ppsc->cur_ps_level & _PS_FLAG) ? true : false)
#define	RT_CLEAR_PS_LEVEL(ppsc, _PS_FLAG)			\
	(ppsc->cur_ps_level &= (~(_PS_FLAG)))
#define	RT_SET_PS_LEVEL(ppsc, _PS_FLAG)				\
	(ppsc->cur_ps_level |= _PS_FLAG)


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
};

struct pwrctrl_priv {
	struct semaphore lock;
	volatile u8 rpwm; /* requested power state for fw */
	volatile u8 cpwm; /* fw current power state. updated when 1.
			   * read from HCPWM 2. driver lowers power level
			   */
	volatile u8 tog; /*  toggling */

	u8	pwr_mode;
	u8	smart_ps;
	u8	bcn_ant_mode;

	u8	bpower_saving;

	u8	reg_rfoff;
	u32	rfoff_reason;

	/* RF OFF Level */
	u32	cur_ps_level;
	u32	reg_rfps_level;

	uint	ips_enter23a_cnts;
	uint	ips_leave23a_cnts;

	u8	ips_mode;
	u8	ips_mode_req; /*  used to accept the mode setting request */
	uint bips_processing;
	unsigned long ips_deny_time; /* deny IPS when system time is smaller */
	u8 ps_processing; /* used to mark whether in rtw_ps_processor23a */

	u8	bLeisurePs;
	u8	LpsIdleCount;
	u8	power_mgnt;
	u8	bFwCurrentInPSMode;
	unsigned long	DelayLPSLastTimeStamp;
	u8	btcoex_rfon;

	u8		bInSuspend;
#ifdef	CONFIG_8723AU_BT_COEXIST
	u8		bAutoResume;
	u8		autopm_cnt;
#endif
	u8		bSupportRemoteWakeup;
	struct timer_list	pwr_state_check_timer;
	int		pwr_state_check_interval;
	u8		pwr_state_check_cnts;

	enum rt_rf_power_state	rf_pwrstate;/* cur power state */
	enum rt_rf_power_state	change_rfpwrstate;

	u8	bHWPowerdown;/* if support hw power down */
	u8	bHWPwrPindetect;
	u8	bkeepfwalive;
	unsigned long PS_BBRegBackup[PSBBREG_TOTALCNT];
};

#define RTW_PWR_STATE_CHK_INTERVAL 2000

#define _rtw_set_pwr_state_check_timer(pwrctrlpriv, ms) \
	(mod_timer(&pwrctrlpriv->pwr_state_check_timer, jiffies +	\
		  msecs_to_jiffies(ms)))

#define rtw_set_pwr_state_check_timer(pwrctrlpriv)	\
	(_rtw_set_pwr_state_check_timer((pwrctrlpriv),	\
			 (pwrctrlpriv)->pwr_state_check_interval))

void rtw_init_pwrctrl_priv23a(struct rtw_adapter *adapter);
void rtw_free_pwrctrl_priv(struct rtw_adapter *adapter);

void rtw_set_ps_mode23a(struct rtw_adapter *padapter, u8 ps_mode,
		     u8 smart_ps, u8 bcn_ant_mode);
void rtw_set_rpwm23a(struct rtw_adapter *padapter, u8 val8);
void LeaveAllPowerSaveMode23a(struct rtw_adapter *adapter);
void ips_enter23a(struct rtw_adapter *padapter);
int ips_leave23a(struct rtw_adapter *padapter);

void rtw_ps_processor23a(struct rtw_adapter *padapter);

enum rt_rf_power_state RfOnOffDetect23a(struct rtw_adapter *adapter);

s32 LPS_RF_ON_check23a(struct rtw_adapter *padapter, u32 delay_ms);
void LPS_Enter23a(struct rtw_adapter *padapter);
void LPS_Leave23a(struct rtw_adapter *padapter);

void rtw_set_ips_deny23a(struct rtw_adapter *padapter, u32 ms);
int _rtw_pwr_wakeup23a(struct rtw_adapter *padapter, u32 ips_deffer_ms,
		    const char *caller);
#define rtw_pwr_wakeup(adapter) _rtw_pwr_wakeup23a(adapter,		\
	 RTW_PWR_STATE_CHK_INTERVAL, __func__)
int rtw_pm_set_ips23a(struct rtw_adapter *padapter, u8 mode);
int rtw_pm_set_lps23a(struct rtw_adapter *padapter, u8 mode);

#endif  /* __RTL871X_PWRCTRL_H_ */
