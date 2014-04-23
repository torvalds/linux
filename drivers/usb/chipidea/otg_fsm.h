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
