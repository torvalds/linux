/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1251
 *
 * Copyright (C) 2008 Nokia Corporation
 */

#ifndef __BOOT_H__
#define __BOOT_H__

#include "wl1251.h"

int wl1251_boot_soft_reset(struct wl1251 *wl);
int wl1251_boot_init_seq(struct wl1251 *wl);
int wl1251_boot_run_firmware(struct wl1251 *wl);
void wl1251_boot_target_enable_interrupts(struct wl1251 *wl);
int wl1251_boot(struct wl1251 *wl);

/* number of times we try to read the INIT interrupt */
#define INIT_LOOP 20000

/* delay between retries */
#define INIT_LOOP_DELAY 50

#endif
