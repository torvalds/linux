/* -----------------------------------------------------------------------------
 * Copyright (c) 2011 Ozmo Inc
 * Released under the GNU General Public License Version 2 (GPLv2).
 * -----------------------------------------------------------------------------
 */
#ifndef _OZEVENTDEF_H
#define _OZEVENTDEF_H

#define OZ_EVT_RX_FRAME		0
#define OZ_EVT_RX_PROCESS	1
#define OZ_EVT_TX_FRAME		2
#define OZ_EVT_TX_ISOC		3
#define OZ_EVT_URB_SUBMIT	4
#define OZ_EVT_URB_DONE		5
#define OZ_EVT_URB_CANCEL	6
#define OZ_EVT_CTRL_REQ		7
#define OZ_EVT_CTRL_CNF		8
#define OZ_EVT_CTRL_LOCAL	9
#define OZ_EVT_CONNECT_REQ	10
#define OZ_EVT_CONNECT_RSP	11
#define OZ_EVT_EP_CREDIT	12
#define OZ_EVT_EP_BUFFERING	13
#define OZ_EVT_TX_ISOC_DONE	14
#define OZ_EVT_TX_ISOC_DROP	15
#define OZ_EVT_TIMER_CTRL	16
#define OZ_EVT_TIMER		17
#define OZ_EVT_PD_STATE		18
#define OZ_EVT_SERVICE		19
#define OZ_EVT_DEBUG		20

struct oz_event {
	unsigned long jiffies;
	unsigned char evt;
	unsigned char ctx1;
	unsigned short ctx2;
	void *ctx3;
	unsigned ctx4;
};

#define OZ_EVT_LIST_SZ	64
struct oz_evtlist {
	int count;
	int missed;
	struct oz_event evts[OZ_EVT_LIST_SZ];
};

#endif /* _OZEVENTDEF_H */
