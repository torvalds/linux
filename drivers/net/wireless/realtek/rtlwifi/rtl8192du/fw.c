// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2024  Realtek Corporation.*/

#include "../wifi.h"
#include "../rtl8192d/reg.h"
#include "../rtl8192d/def.h"
#include "../rtl8192d/fw_common.h"
#include "fw.h"

int rtl92du_download_fw(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	enum version_8192d version = rtlhal->version;
	u8 *pfwheader;
	u8 *pfwdata;
	u32 fwsize;
	int err;

	if (rtlpriv->max_fw_size == 0 || !rtlhal->pfirmware)
		return 1;

	fwsize = rtlhal->fwsize;
	pfwheader = rtlhal->pfirmware;
	pfwdata = rtlhal->pfirmware;
	rtlhal->fw_version = (u16)GET_FIRMWARE_HDR_VERSION(pfwheader);
	rtlhal->fw_subversion = (u16)GET_FIRMWARE_HDR_SUB_VER(pfwheader);

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"FirmwareVersion(%d), FirmwareSubVersion(%d), Signature(%#x)\n",
		rtlhal->fw_version, rtlhal->fw_subversion,
		GET_FIRMWARE_HDR_SIGNATURE(pfwheader));

	if (IS_FW_HEADER_EXIST(pfwheader)) {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"Shift 32 bytes for FW header!!\n");
		pfwdata = pfwdata + 32;
		fwsize = fwsize - 32;
	}

	if (rtl92d_is_fw_downloaded(rtlpriv))
		goto exit;

	/* If 8051 is running in RAM code, driver should
	 * inform Fw to reset by itself, or it will cause
	 * download Fw fail.
	 */
	if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) {
		rtl92d_firmware_selfreset(hw);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);
	}

	rtl92d_enable_fw_download(hw, true);
	rtl92d_write_fw(hw, version, pfwdata, fwsize);
	rtl92d_enable_fw_download(hw, false);

	err = rtl92d_fw_free_to_go(hw);
	if (err)
		pr_err("fw is not ready to run!\n");
exit:
	err = rtl92d_fw_init(hw);
	return err;
}
