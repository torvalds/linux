/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef _RTW_EVENT_H_
#define _RTW_EVENT_H_

#include "osdep_service.h"

#include "wlan_bssdef.h"
#include <linux/semaphore.h>
#include <linux/sem.h>

/*
Used to report a bss has been scanned
*/
struct survey_event	{
	struct wlan_bssid_ex bss;
};

/*
Used to report that the requested site survey has been done.

bss_cnt indicates the number of bss that has been reported.

*/
struct surveydone_event {
	unsigned int	bss_cnt;

};

/*
Used to report the link result of joinning the given bss

join_res:
-1: authentication fail
-2: association fail
> 0: TID

*/
struct joinbss_event {
	struct	wlan_network	network;
};

/*
Used to report a given STA has joinned the created BSS.
It is used in AP/Ad-HoC(M) mode.
*/

struct stassoc_event {
	unsigned char macaddr[6];
	unsigned char rsvd[2];
	int    cam_id;
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
	u32	parmsize;
	void (*event_callback)(struct adapter *dev, u8 *pbuf);
};

#define C2HEVENT_SZ			32

struct event_node {
	unsigned char *node;
	unsigned char evt_code;
	unsigned short evt_sz;
	int	*caller_ff_tail;
	int	caller_ff_sz;
};

struct c2hevent_queue {
	int	head;
	int	tail;
	struct	event_node	nodes[C2HEVENT_SZ];
	unsigned char	seq;
};

#define NETWORK_QUEUE_SZ	4

struct network_queue {
	int	head;
	int	tail;
	struct wlan_bssid_ex networks[NETWORK_QUEUE_SZ];
};

#endif /*  _WLANEVENT_H_ */
