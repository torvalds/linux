/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 */

#ifndef __WL1251_PS_H__
#define __WL1251_PS_H__

#include "wl1251.h"
#include "acx.h"

int wl1251_ps_set_mode(struct wl1251 *wl, enum wl1251_station_mode mode);
void wl1251_ps_elp_sleep(struct wl1251 *wl);
int wl1251_ps_elp_wakeup(struct wl1251 *wl);
void wl1251_elp_work(struct work_struct *work);


#endif /* __WL1251_PS_H__ */
