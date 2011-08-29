/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef	__RTL871X_RF_H_
#define __RTL871X_RF_H_

#include "rtl871x_cmd.h"
#include "rtl871x_mp_phy_regdef.h"

#define OFDM_PHY		1
#define MIXED_PHY		2
#define CCK_PHY		3
#define NumRates	(13)
#define RTL8711_RF_MAX_SENS 6
#define RTL8711_RF_DEF_SENS 4
#define NUM_CHANNELS	15

struct	regulatory_class {
	u32	starting_freq;		/*MHz, */
	u8	channel_set[NUM_CHANNELS];
	u8	channel_cck_power[NUM_CHANNELS]; /*dbm*/
	u8	channel_ofdm_power[NUM_CHANNELS];/*dbm*/
	u8	txpower_limit;		/*dbm*/
	u8	channel_spacing;	/*MHz*/
	u8	modem;
};

enum	_REG_PREAMBLE_MODE {
	PREAMBLE_LONG	= 1,
	PREAMBLE_AUTO	= 2,
	PREAMBLE_SHORT	= 3,
};

enum {
	RTL8712_RFC_1T = 0x10,
	RTL8712_RFC_2T = 0x20,
	RTL8712_RFC_1R = 0x01,
	RTL8712_RFC_2R = 0x02,
	RTL8712_RFC_1T1R = 0x11,
	RTL8712_RFC_1T2R = 0x12,
	RTL8712_RFC_TURBO = 0x92,
	RTL8712_RFC_2T2R = 0x22
};

#endif /*_RTL8711_RF_H_*/

