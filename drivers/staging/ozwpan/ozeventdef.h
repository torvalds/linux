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
	__u32 jiffies;
	__u8 evt;
	__u8 ctx1;
	__u16 ctx2;
	__u32 ctx3;
	__u32 ctx4;
};

#endif /* _OZEVENTDEF_H */
