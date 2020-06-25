/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __GP_TIMER_PUBLIC_H_INCLUDED__
#define __GP_TIMER_PUBLIC_H_INCLUDED__

#include "system_local.h"

/*! initialize mentioned timer
param ID		timer_id
*/
extern void
gp_timer_init(gp_timer_ID_t ID);

/*! read timer value for (platform selected)selected timer.
param ID		timer_id
 \return uint32_t	32 bit timer value
*/
extern uint32_t
gp_timer_read(gp_timer_ID_t ID);

#endif /* __GP_TIMER_PUBLIC_H_INCLUDED__ */
