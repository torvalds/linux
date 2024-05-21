/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _RTL871x_EVENT_H_
#define _RTL871x_EVENT_H_

#include "osdep_service.h"

#include "wlan_bssdef.h"
#include <linux/semaphore.h>
#include <linux/sem.h>

/*
 * Used to report a bss has been scanned
 */
struct survey_event	{
	struct wlan_bssid_ex bss;
};

/*
 * Used to report that the requested site survey has been done.
 * bss_cnt indicates the number of bss that has been reported.
 */
struct surveydone_event {
	unsigned int	bss_cnt;

};

/*
 * Used to report the link result of joining the given bss
 * join_res:
 *  -1: authentication fail
 *  -2: association fail
 *  > 0: TID
 */
struct joinbss_event {
	struct	wlan_network	network;
};

/*
 * Used to report a given STA has joinned the created BSS.
 * It is used in AP/Ad-HoC(M) mode.
 */
struct stassoc_event {
	unsigned char macaddr[6];
	unsigned char rsvd[2];
	__le32    cam_id;
};

struct stadel_event {
	unsigned char macaddr[6];
	unsigned char rsvd[2];
};

struct addba_event {
	unsigned int tid;
};

#define GEN_EVT_CODE(event)	event ## _EVT_

struct fwevent {
	u32	parmsize;
	void (*event_callback)(struct _adapter *dev, u8 *pbuf);
};

#define C2HEVENT_SZ			32
struct event_node {
	unsigned char *node;
	unsigned char evt_code;
	unsigned short evt_sz;
	/*volatile*/ int *caller_ff_tail;
	int	caller_ff_sz;
};

struct c2hevent_queue {
	/*volatile*/ int	head;
	/*volatile*/ int	tail;
	struct	event_node	nodes[C2HEVENT_SZ];
	unsigned char	seq;
};

#define NETWORK_QUEUE_SZ	4

struct network_queue {
	/*volatile*/ int	head;
	/*volatile*/ int	tail;
	struct wlan_bssid_ex networks[NETWORK_QUEUE_SZ];
};

struct ADDBA_Req_Report_parm {
	unsigned char MacAddress[ETH_ALEN];
	unsigned short StartSeqNum;
	unsigned char tid;
};

#include "rtl8712_event.h"

#endif /* _WLANEVENT_H_ */

