/*
 * This file is part of wl1251
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
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
