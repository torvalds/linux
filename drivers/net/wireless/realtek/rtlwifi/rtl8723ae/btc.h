/******************************************************************************
 **
 ** Copyright(c) 2009-2012  Realtek Corporation.
 **
 ** This program is free software; you can redistribute it and/or modify it
 ** under the terms of version 2 of the GNU General Public License as
 ** published by the Free Software Foundation.
 **
 ** This program is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** The full GNU General Public License is included in this distribution in the
 ** file called LICENSE.
 **
 ** Contact Information:
 ** wlanfae <wlanfae@realtek.com>
 ** Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 ** Hsinchu 300, Taiwan.
 ** Larry Finger <Larry.Finger@lwfinger.net>
 **
 ******************************************************************************/

#ifndef __RTL8723E_BTC_H__
#define __RTL8723E_BTC_H__

#include "../wifi.h"
#include "hal_bt_coexist.h"

struct bt_coexist_c2h_info {
	u8 no_parse_c2h;
	u8 has_c2h;
};

#endif
