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

#ifdef CONFIG_USB_OTG_FSM

int ci_hdrc_otg_fsm_init(struct ci_hdrc *ci);

#else

static inline int ci_hdrc_otg_fsm_init(struct ci_hdrc *ci)
{
	return 0;
}

#endif

#endif /* __DRIVERS_USB_CHIPIDEA_OTG_FSM_H */
