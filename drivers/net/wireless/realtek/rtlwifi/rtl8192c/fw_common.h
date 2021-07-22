/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92C__FW__COMMON__H__
#define __RTL92C__FW__COMMON__H__

#define FW_8192C_SIZE				0x3000
#define FW_8192C_START_ADDRESS			0x1000
#define FW_8192C_END_ADDRESS			0x1FFF
#define FW_8192C_PAGE_SIZE			4096
#define FW_8192C_POLLING_DELAY			5
#define FW_8192C_POLLING_TIMEOUT_COUNT		100
#define NORMAL_CHIP				BIT(4)
#define H2C_92C_KEEP_ALIVE_CTRL			48

#define IS_FW_HEADER_EXIST(_pfwhdr)	\
	((le16_to_cpu(_pfwhdr->signature)&0xFFF0) == 0x92C0 ||\
	(le16_to_cpu(_pfwhdr->signature)&0xFFF0) == 0x88C0)

#define CUT_VERSION_MASK		(BIT(6)|BIT(7))
#define CHIP_VENDOR_UMC			BIT(5)
#define CHIP_VENDOR_UMC_B_CUT		BIT(6) /* Chip version for ECO */
#define IS_CHIP_VER_B(version)  ((version & CHIP_VER_B) ? true : false)
#define RF_TYPE_MASK			(BIT(0)|BIT(1))
#define GET_CVID_RF_TYPE(version)	\
	((version) & RF_TYPE_MASK)
#define GET_CVID_CUT_VERSION(version) \
	((version) & CUT_VERSION_MASK)
#define IS_NORMAL_CHIP(version)	\
	((version & NORMAL_CHIP) ? true : false)
#define IS_2T2R(version) \
	(((GET_CVID_RF_TYPE(version)) == \
	CHIP_92C_BITMASK) ? true : false)
#define IS_92C_SERIAL(version) \
	((IS_2T2R(version)) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)	\
	((version & CHIP_VENDOR_UMC) ? true : false)
#define IS_VENDOR_UMC_A_CUT(version) \
	((IS_CHIP_VENDOR_UMC(version)) ? \
	((GET_CVID_CUT_VERSION(version)) ? false : true) : false)
#define IS_81XXC_VENDOR_UMC_B_CUT(version)	\
	((IS_CHIP_VENDOR_UMC(version)) ? \
	((GET_CVID_CUT_VERSION(version) == \
		CHIP_VENDOR_UMC_B_CUT) ? true : false) : false)

#define pagenum_128(_len)	(u32)(((_len)>>7) + ((_len)&0x7F ? 1 : 0))

#define SET_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd, __val)			\
	*(u8 *)(__ph2ccmd) = __val
#define SET_H2CCMD_PWRMODE_PARM_SMART_PS(__ph2ccmd, __val)		\
	*(u8 *)(__ph2ccmd + 1) = __val
#define SET_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__ph2ccmd, __val)	\
	*(u8 *)(__ph2ccmd + 2) = __val
#define SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(__ph2ccmd, __val)		\
	*(u8 *)(__ph2ccmd) = __val
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)		\
	*(u8 *)(__ph2ccmd) = __val
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)		\
	*(u8 *)(__ph2ccmd + 1) = __val
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)		\
	*(u8 *)(__ph2ccmd + 2) = __val

int rtl92c_download_fw(struct ieee80211_hw *hw);
void rtl92c_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			 u32 cmd_len, u8 *p_cmdbuffer);
void rtl92c_firmware_selfreset(struct ieee80211_hw *hw);
void rtl92c_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl92c_set_fw_rsvdpagepkt
	(struct ieee80211_hw *hw,
	 bool (*cmd_send_packet)(struct ieee80211_hw *, struct sk_buff *));
void rtl92c_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);
void usb_writeN_async(struct rtl_priv *rtlpriv, u32 addr, void *data, u16 len);
void rtl92c_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);

#endif
