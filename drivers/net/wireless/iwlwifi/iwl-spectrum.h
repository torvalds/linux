/******************************************************************************
 *
 * Copyright(c) 2003 - 2010 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_spectrum_h__
#define __iwl_spectrum_h__
enum {				/* ieee80211_basic_report.map */
	IEEE80211_BASIC_MAP_BSS = (1 << 0),
	IEEE80211_BASIC_MAP_OFDM = (1 << 1),
	IEEE80211_BASIC_MAP_UNIDENTIFIED = (1 << 2),
	IEEE80211_BASIC_MAP_RADAR = (1 << 3),
	IEEE80211_BASIC_MAP_UNMEASURED = (1 << 4),
	/* Bits 5-7 are reserved */

};
struct ieee80211_basic_report {
	u8 channel;
	__le64 start_time;
	__le16 duration;
	u8 map;
} __packed;

enum {				/* ieee80211_measurement_request.mode */
	/* Bit 0 is reserved */
	IEEE80211_MEASUREMENT_ENABLE = (1 << 1),
	IEEE80211_MEASUREMENT_REQUEST = (1 << 2),
	IEEE80211_MEASUREMENT_REPORT = (1 << 3),
	/* Bits 4-7 are reserved */
};

enum {
	IEEE80211_REPORT_BASIC = 0,	/* required */
	IEEE80211_REPORT_CCA = 1,	/* optional */
	IEEE80211_REPORT_RPI = 2,	/* optional */
	/* 3-255 reserved */
};

struct ieee80211_measurement_params {
	u8 channel;
	__le64 start_time;
	__le16 duration;
} __packed;

struct ieee80211_info_element {
	u8 id;
	u8 len;
	u8 data[0];
} __packed;

struct ieee80211_measurement_request {
	struct ieee80211_info_element ie;
	u8 token;
	u8 mode;
	u8 type;
	struct ieee80211_measurement_params params[0];
} __packed;

struct ieee80211_measurement_report {
	struct ieee80211_info_element ie;
	u8 token;
	u8 mode;
	u8 type;
	union {
		struct ieee80211_basic_report basic[0];
	} u;
} __packed;

#endif
