/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_FW_COMMON_H__
#define __RTL92D_FW_COMMON_H__

#define FW_8192D_START_ADDRESS			0x1000
#define FW_8192D_PAGE_SIZE			4096
#define FW_8192D_POLLING_TIMEOUT_COUNT		1000

#define IS_FW_HEADER_EXIST(_pfwhdr)	\
		((GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFF0) == 0x92C0 || \
		 (GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFF0) == 0x88C0 || \
		 (GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D0 || \
		 (GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D1 || \
		 (GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D2 || \
		 (GET_FIRMWARE_HDR_SIGNATURE(_pfwhdr) & 0xFFFF) == 0x92D3)

/* Firmware Header(8-byte alinment required) */
/* --- LONG WORD 0 ---- */
#define GET_FIRMWARE_HDR_SIGNATURE(__fwhdr)		\
	le32_get_bits(*(__le32 *)__fwhdr, GENMASK(15, 0))
#define GET_FIRMWARE_HDR_VERSION(__fwhdr)		\
	le32_get_bits(*(__le32 *)((__fwhdr) + 4), GENMASK(15, 0))
#define GET_FIRMWARE_HDR_SUB_VER(__fwhdr)		\
	le32_get_bits(*(__le32 *)((__fwhdr) + 4), GENMASK(23, 16))

#define RAID_MASK               GENMASK(31, 28)
#define RATE_MASK_MASK          GENMASK(27, 0)
#define SHORT_GI_MASK           BIT(5)
#define MACID_MASK              GENMASK(4, 0)

struct rtl92d_rate_mask_h2c {
	__le32 rate_mask_and_raid;
	u8 macid_and_short_gi;
} __packed;

bool rtl92d_is_fw_downloaded(struct rtl_priv *rtlpriv);
void rtl92d_enable_fw_download(struct ieee80211_hw *hw, bool enable);
void rtl92d_write_fw(struct ieee80211_hw *hw,
		     enum version_8192d version, u8 *buffer, u32 size);
int rtl92d_fw_free_to_go(struct ieee80211_hw *hw);
void rtl92d_firmware_selfreset(struct ieee80211_hw *hw);
int rtl92d_fw_init(struct ieee80211_hw *hw);
void rtl92d_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			 u32 cmd_len, u8 *p_cmdbuffer);
void rtl92d_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);

#endif
