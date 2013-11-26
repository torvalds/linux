/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef __RTW_ANDROID_H__
#define __RTW_ANDROID_H__

#include <linux/module.h>
#include <linux/netdevice.h>

enum ANDROID_WIFI_CMD {
	ANDROID_WIFI_CMD_START,
	ANDROID_WIFI_CMD_STOP,
	ANDROID_WIFI_CMD_SCAN_ACTIVE,
	ANDROID_WIFI_CMD_SCAN_PASSIVE,
	ANDROID_WIFI_CMD_RSSI,
	ANDROID_WIFI_CMD_LINKSPEED,
	ANDROID_WIFI_CMD_RXFILTER_START,
	ANDROID_WIFI_CMD_RXFILTER_STOP,
	ANDROID_WIFI_CMD_RXFILTER_ADD,
	ANDROID_WIFI_CMD_RXFILTER_REMOVE,
	ANDROID_WIFI_CMD_BTCOEXSCAN_START,
	ANDROID_WIFI_CMD_BTCOEXSCAN_STOP,
	ANDROID_WIFI_CMD_BTCOEXMODE,
	ANDROID_WIFI_CMD_SETSUSPENDOPT,
	ANDROID_WIFI_CMD_P2P_DEV_ADDR,
	ANDROID_WIFI_CMD_SETFWPATH,
	ANDROID_WIFI_CMD_SETBAND,
	ANDROID_WIFI_CMD_GETBAND,
	ANDROID_WIFI_CMD_COUNTRY,
	ANDROID_WIFI_CMD_P2P_SET_NOA,
	ANDROID_WIFI_CMD_P2P_GET_NOA,
	ANDROID_WIFI_CMD_P2P_SET_PS,
	ANDROID_WIFI_CMD_SET_AP_WPS_P2P_IE,
	ANDROID_WIFI_CMD_MACADDR,
	ANDROID_WIFI_CMD_BLOCK,
	ANDROID_WIFI_CMD_WFD_ENABLE,
	ANDROID_WIFI_CMD_WFD_DISABLE,
	ANDROID_WIFI_CMD_WFD_SET_TCPPORT,
	ANDROID_WIFI_CMD_WFD_SET_MAX_TPUT,
	ANDROID_WIFI_CMD_WFD_SET_DEVTYPE,
	ANDROID_WIFI_CMD_MAX
};

int rtw_android_cmdstr_to_num(char *cmdstr);
int rtw_android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);

#endif /* __RTW_ANDROID_H__ */
