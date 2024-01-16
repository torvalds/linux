/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D__FW__H__
#define __RTL92D__FW__H__

#define FW_8192D_START_ADDRESS			0x1000
#define FW_8192D_PAGE_SIZE				4096
#define FW_8192D_POLLING_TIMEOUT_COUNT	1000

#define IS_FW_HEADER_EXIST(_pfwhdr)	\
		((GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFF0) == 0x92C0 || \
		(GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFF0) == 0x88C0 ||  \
		(GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D0 ||  \
		(GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D1 ||  \
		(GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D2 ||  \
		(GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D3)

/* Firmware Header(8-byte alinment required) */
/* --- LONG WORD 0 ---- */
#define GET_FIRMWARE_HDR_SIGNATURE(__fwhdr)		\
	le32_get_bits(*(__le32 *)__fwhdr, GENMASK(15, 0))
#define GET_FIRMWARE_HDR_VERSION(__fwhdr)		\
	le32_get_bits(*(__le32 *)(__fwhdr + 4), GENMASK(15, 0))
#define GET_FIRMWARE_HDR_SUB_VER(__fwhdr)		\
	le32_get_bits(*(__le32 *)(__fwhdr + 4), GENMASK(23, 16))

#define pagenum_128(_len) \
	(u32)(((_len) >> 7) + ((_len) & 0x7F ? 1 : 0))

#define SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(__ph2ccmd, __val)	\
	*(u8 *)__ph2ccmd = __val;
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)	\
	*(u8 *)__ph2ccmd = __val;
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)	\
	*(u8 *)(__ph2ccmd + 1) = __val;
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)	\
	*(u8 *)(__ph2ccmd + 2) = __val;

int rtl92d_download_fw(struct ieee80211_hw *hw);
void rtl92d_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			 u32 cmd_len, u8 *p_cmdbuffer);
void rtl92d_firmware_selfreset(struct ieee80211_hw *hw);
void rtl92d_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished);
void rtl92d_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);

#endif
