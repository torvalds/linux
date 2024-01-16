/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	CA-driver for TwinHan DST Frontend/Card

	Copyright (C) 2004, 2005 Manu Abraham (manu@kromtek.com)

*/

#ifndef _DST_CA_H_
#define _DST_CA_H_

#define RETRIES			5


#define	CA_APP_INFO_ENQUIRY	0x9f8020
#define	CA_APP_INFO		0x9f8021
#define	CA_ENTER_MENU		0x9f8022
#define CA_INFO_ENQUIRY		0x9f8030
#define	CA_INFO			0x9f8031
#define CA_PMT			0x9f8032
#define CA_PMT_REPLY		0x9f8033

#define CA_CLOSE_MMI		0x9f8800
#define CA_DISPLAY_CONTROL	0x9f8801
#define CA_DISPLAY_REPLY	0x9f8802
#define CA_TEXT_LAST		0x9f8803
#define CA_TEXT_MORE		0x9f8804
#define CA_KEYPAD_CONTROL	0x9f8805
#define CA_KEYPRESS		0x9f8806

#define CA_ENQUIRY		0x9f8807
#define CA_ANSWER		0x9f8808
#define CA_MENU_LAST		0x9f8809
#define CA_MENU_MORE		0x9f880a
#define CA_MENU_ANSWER		0x9f880b
#define CA_LIST_LAST		0x9f880c
#define CA_LIST_MORE		0x9f880d


struct dst_ca_private {
	struct dst_state *dst;
	struct dvb_device *dvbdev;
};


#endif
