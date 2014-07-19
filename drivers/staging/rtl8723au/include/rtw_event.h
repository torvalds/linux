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
 ******************************************************************************/
#ifndef _RTW_EVENT_H_
#define _RTW_EVENT_H_

#include <osdep_service.h>
#include <wlan_bssdef.h>

/*
Used to report a bss has been scanned
*/
struct survey_event {
	struct wlan_bssid_ex bss;
};

/*
Used to report that the requested site survey has been done.
bss_cnt indicates the number of bss that has been reported.
*/
struct surveydone_event {
	unsigned int bss_cnt;
};

/*
Used to report the link result of joinning the given bss
join_res:
-1: authentication fail
-2: association fail
> 0: TID
*/
struct joinbss_event {
	struct wlan_network network;
};

/*
Used to report a given STA has joinned the created BSS.
It is used in AP/Ad-HoC(M) mode.
*/
struct stassoc_event {
	unsigned char macaddr[6];
	unsigned char rsvd[2];
	int cam_id;
};

struct stadel_event {
	unsigned char macaddr[6];
	unsigned char rsvd[2]; /* for reason */
	int mac_id;
};

struct addba_event {
	unsigned int tid;
};

#define GEN_EVT_CODE(event)	event ## _EVT_

struct fwevent {
	u32 parmsize;
	void (*event_callback)(struct rtw_adapter *dev, const u8 *pbuf);
};

#endif /*  _WLANEVENT_H_ */
