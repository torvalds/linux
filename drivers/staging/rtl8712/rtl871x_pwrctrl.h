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
#ifndef __RTL871X_PWRCTRL_H_
#define __RTL871X_PWRCTRL_H_

#include "osdep_service.h"
#include "drv_types.h"


#define CMD_ALIVE	BIT(2)

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

/*
 * BIT[2:0] = HW state
 * BIT[3] = Protocol PS state, 0: register active state,
 *				1: register sleep state
 * BIT[4] = sub-state
 */

#define		PS_DPS				BIT(0)
#define		PS_LCLK				(PS_DPS)
#define	PS_RF_OFF			BIT(1)
#define	PS_ALL_ON			BIT(2)
#define	PS_ST_ACTIVE		BIT(3)
#define	PS_LP				BIT(4)	/* low performance */

#define	PS_STATE_MASK		(0x0F)
#define	PS_STATE_HW_MASK	(0x07)
#define		PS_SEQ_MASK		(0xc0)

#define	PS_STATE(x)			(PS_STATE_MASK & (x))
#define	PS_STATE_HW(x)	(PS_STATE_HW_MASK & (x))
#define	PS_SEQ(x)			(PS_SEQ_MASK & (x))

#define	PS_STATE_S0		(PS_DPS)
#define		PS_STATE_S1		(PS_LCLK)
#define	PS_STATE_S2		(PS_RF_OFF)
#define		PS_STATE_S3		(PS_ALL_ON)
#define	PS_STATE_S4		((PS_ST_ACTIVE) | (PS_ALL_ON))


#define		PS_IS_RF_ON(x)		((x) & (PS_ALL_ON))
#define		PS_IS_ACTIVE(x)		((x) & (PS_ST_ACTIVE))
#define		CLR_PS_STATE(x)	((x) = ((x) & (0xF0)))


struct reportpwrstate_parm {
	unsigned char mode;
	unsigned char state; /* the CPWM value */
	unsigned short rsvd;
};

struct	pwrctrl_priv {
	struct mutex mutex_lock;
	/*volatile*/ u8 rpwm; /* requested power state for fw */
	/* fw current power state. updated when 1. read from HCPWM or
	 * 2. driver lowers power level
	 */
	/*volatile*/ u8 cpwm;
	/*volatile*/ u8 tog; /* toggling */
	/*volatile*/ u8 cpwm_tog; /* toggling */
	/*volatile*/ u8 tgt_rpwm; /* wanted power state */
	uint pwr_mode;
	uint smart_ps;
	uint alives;
	uint ImrContent;	/* used to store original imr. */
	uint bSleep; /* sleep -> active is different from active -> sleep. */

	struct work_struct SetPSModeWorkItem;
	struct work_struct rpwm_workitem;
	struct timer_list rpwm_check_timer;
	u8	rpwm_retry;
	uint	bSetPSModeWorkItemInProgress;

	spinlock_t pnp_pwr_mgnt_lock;
	s32	pnp_current_pwr_state;
	u8	pnp_bstop_trx;
	u8	pnp_wwirp_pending;
};

void r8712_init_pwrctrl_priv(struct _adapter *adapter);
int r8712_register_cmd_alive(struct _adapter *padapter);
void r8712_unregister_cmd_alive(struct _adapter *padapter);
void r8712_cpwm_int_hdl(struct _adapter *padapter,
			struct reportpwrstate_parm *preportpwrstate);
void r8712_set_ps_mode(struct _adapter *padapter, uint ps_mode,
			uint smart_ps);
void r8712_set_rpwm(struct _adapter *padapter, u8 val8);
void r8712_flush_rwctrl_works(struct _adapter *padapter);

#endif  /* __RTL871X_PWRCTRL_H_ */
