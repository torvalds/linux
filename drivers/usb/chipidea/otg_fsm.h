/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Jun Li
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DRIVERS_USB_CHIPIDEA_OTG_FSM_H
#define __DRIVERS_USB_CHIPIDEA_OTG_FSM_H

#include <linux/usb/otg-fsm.h>

/*
 *  A-DEVICE timing  constants
 */

/* Wait for VBUS Rise  */
#define TA_WAIT_VRISE        (100)	/* a_wait_vrise: section 7.1.2
					 * a_wait_vrise_tmr: section 7.4.5.1
					 * TA_VBUS_RISE <= 100ms, section 4.4
					 * Table 4-1: Electrical Characteristics
					 * ->DC Electrical Timing
					 */
/* Wait for VBUS Fall  */
#define TA_WAIT_VFALL        (1000)	/* a_wait_vfall: section 7.1.7
					 * a_wait_vfall_tmr: section: 7.4.5.2
					 */
/* Wait for B-Connect */
#define TA_WAIT_BCON         (10000)	/* a_wait_bcon: section 7.1.3
					 * TA_WAIT_BCON: should be between 1100
					 * and 30000 ms, section 5.5, Table 5-1
					 */
/* A-Idle to B-Disconnect */
#define TA_AIDL_BDIS         (5000)	/* a_suspend min 200 ms, section 5.2.1
					 * TA_AIDL_BDIS: section 5.5, Table 5-1
					 */
/* B-Idle to A-Disconnect */
#define TA_BIDL_ADIS         (500)	/* TA_BIDL_ADIS: section 5.2.1
					 * 500ms is used for B switch to host
					 * for safe
					 */

/*
 * B-device timing constants
 */

/* Data-Line Pulse Time*/
#define TB_DATA_PLS          (10)	/* b_srp_init,continue 5~10ms
					 * section:5.1.3
					 */
/* SRP Fail Time  */
#define TB_SRP_FAIL          (6000)	/* b_srp_init,fail time 5~6s
					 * section:5.1.6
					 */
/* A-SE0 to B-Reset  */
#define TB_ASE0_BRST         (155)	/* minimum 155 ms, section:5.3.1 */
/* SE0 Time Before SRP */
#define TB_SE0_SRP           (1000)	/* b_idle,minimum 1s, section:5.1.2 */
/* SSEND time before SRP */
#define TB_SSEND_SRP         (1500)	/* minimum 1.5 sec, section:5.1.2 */

#define TB_SESS_VLD          (1000)

enum ci_otg_fsm_timer_index {
	/*
	 * CI specific timers, start from the end
	 * of standard and auxiliary OTG timers
	 */
	B_DATA_PLS = NUM_OTG_FSM_TIMERS,
	B_SSEND_SRP,
	B_SESS_VLD,

	NUM_CI_OTG_FSM_TIMERS,
};

struct ci_otg_fsm_timer {
	unsigned long expires;  /* Number of count increase to timeout */
	unsigned long count;    /* Tick counter */
	void (*function)(void *, unsigned long);        /* Timeout function */
	unsigned long data;     /* Data passed to function */
	struct list_head list;
};

struct ci_otg_fsm_timer_list {
	struct ci_otg_fsm_timer *timer_list[NUM_CI_OTG_FSM_TIMERS];
	struct list_head active_timers;
};

#ifdef CONFIG_USB_OTG_FSM

int ci_hdrc_otg_fsm_init(struct ci_hdrc *ci);

#else

static inline int ci_hdrc_otg_fsm_init(struct ci_hdrc *ci)
{
	return 0;
}

#endif

#endif /* __DRIVERS_USB_CHIPIDEA_OTG_FSM_H */
