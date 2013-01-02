/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ****************************************************************************
 */

#ifndef __RTL92C__FW__H__
#define __RTL92C__FW__H__

#define FW_8192C_START_ADDRESS			0x1000
#define FW_8192C_END_ADDRESS			0x3FFF
#define FW_8192C_PAGE_SIZE			4096
#define FW_8192C_POLLING_DELAY			5
#define FW_8192C_POLLING_TIMEOUT_COUNT		1000

#define BEACON_PG				0
#define PSPOLL_PG				2
#define NULL_PG					3
#define PROBERSP_PG				4 /* ->5 */

#define TOTAL_RESERVED_PKT_LEN			768

#define IS_FW_HEADER_EXIST(_pfwhdr)		\
	((_pfwhdr->signature&0xFF00) == 0x2300)

struct rtl8723ae_firmware_header {
	u16 signature;
	u8 category;
	u8 function;
	u16 version;
	u8 subversion;
	u8 rsvd1;
	u8 month;
	u8 date;
	u8 hour;
	u8 minute;
	u16 ramcodeSize;
	u16 rsvd2;
	u32 svnindex;
	u32 rsvd3;
	u32 rsvd4;
	u32 rsvd5;
};

enum rtl8192c_h2c_cmd {
	H2C_AP_OFFLOAD = 0,
	H2C_SETPWRMODE = 1,
	H2C_JOINBSSRPT = 2,
	H2C_RSVDPAGE = 3,
	H2C_RSSI_REPORT = 5,
	H2C_RA_MASK = 6,
	MAX_H2CCMD
};

#define SET_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_SMART_PS(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#define SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)

int rtl8723ae_download_fw(struct ieee80211_hw *hw);
void rtl8723ae_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			    u32 cmd_len, u8 *p_cmdbuffer);
void rtl8723ae_firmware_selfreset(struct ieee80211_hw *hw);
void rtl8723ae_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl8723ae_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished);
void rtl8723ae_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);

#endif
