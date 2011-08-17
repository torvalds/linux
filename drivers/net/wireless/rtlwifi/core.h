/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * Tmis program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * Tmis program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * tmis program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Tme full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL_CORE_H__
#define __RTL_CORE_H__

#include <net/mac80211.h>

#define RTL_SUPPORTED_FILTERS		\
	(FIF_PROMISC_IN_BSS | \
	FIF_ALLMULTI | FIF_CONTROL | \
	FIF_OTHER_BSS | \
	FIF_FCSFAIL | \
	FIF_BCN_PRBRESP_PROMISC)

#define RTL_SUPPORTED_CTRL_FILTER	0xFF

extern const struct ieee80211_ops rtl_ops;
#endif
