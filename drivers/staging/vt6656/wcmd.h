/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * File: wcmd.h
 *
 * Purpose: Handles the management command interface functions
 *
 * Author: Lyndon Chen
 *
 * Date: May 8, 2002
 *
 */

#ifndef __WCMD_H__
#define __WCMD_H__

#include "device.h"

/* Command code */
enum vnt_cmd {
	WLAN_CMD_INIT_MAC80211,
	WLAN_CMD_SETPOWER,
	WLAN_CMD_TBTT_WAKEUP,
	WLAN_CMD_BECON_SEND,
	WLAN_CMD_CHANGE_ANTENNA
};

#define CMD_Q_SIZE              32

/* Command state */
enum vnt_cmd_state {
	WLAN_CMD_INIT_MAC80211_START,
	WLAN_CMD_SETPOWER_START,
	WLAN_CMD_TBTT_WAKEUP_START,
	WLAN_CMD_BECON_SEND_START,
	WLAN_CMD_CHANGE_ANTENNA_START,
	WLAN_CMD_IDLE
};

struct vnt_private;

void vnt_reset_command_timer(struct vnt_private *priv);

int vnt_schedule_command(struct vnt_private *priv, enum vnt_cmd);

void vnt_run_command(struct work_struct *work);

#endif /* __WCMD_H__ */
