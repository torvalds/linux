/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
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
 * The full GNU General Public License is included in this distribution in the
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

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../core.h"
#include "../efuse.h"
#include "reg.h"
#include "def.h"
#include "fw.h"

static void _rtl88e_enable_fw_download(struct ieee80211_hw *hw, bool enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	if (enable) {
		tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, tmp | 0x04);

		tmp = rtl_read_byte(rtlpriv, REG_MCUFWDL);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, tmp | 0x01);

		tmp = rtl_read_byte(rtlpriv, REG_MCUFWDL + 2);
		rtl_write_byte(rtlpriv, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {
		tmp = rtl_read_byte(rtlpriv, REG_MCUFWDL);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, tmp & 0xfe);

		rtl_write_byte(rtlpriv, REG_MCUFWDL + 1, 0x00);
	}
}

static void _rtl88e_write_fw(struct ieee80211_hw *hw,
			     enum version_8188e version, u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *bufferptr = (u8 *)buffer;
	u32 pagenums, remainsize;
	u32 page, offset;

	RT_TRACE(rtlpriv, COMP_FW, DBG_LOUD, "FW size is %d bytes,\n", size);

	rtl_fill_dummy(bufferptr, &size);

	pagenums = size / FW_8192C_PAGE_SIZE;
	remainsize = size % FW_8192C_PAGE_SIZE;

	if (pagenums > 8)
		pr_err("Page numbers should not greater then 8\n");

	for (page = 0; page < pagenums; page++) {
		offset = page * FW_8192C_PAGE_SIZE;
		rtl_fw_page_write(hw, page, (bufferptr + offset),
				  FW_8192C_PAGE_SIZE);
	}

	if (remainsize) {
		offset = pagenums * FW_8192C_PAGE_SIZE;
		page = pagenums;
		rtl_fw_page_write(hw, page, (bufferptr + offset), remainsize);
	}
}

static int _rtl88e_fw_free_to_go(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err = -EIO;
	u32 counter = 0;
	u32 value32;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	} while ((counter++ < FW_8192C_POLLING_TIMEOUT_COUNT) &&
		 (!(value32 & FWDL_CHKSUM_RPT)));

	if (counter >= FW_8192C_POLLING_TIMEOUT_COUNT) {
		pr_err("chksum report fail! REG_MCUFWDL:0x%08x .\n",
		       value32);
		goto exit;
	}
	value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);

	rtl88e_firmware_selfreset(hw);
	counter = 0;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
		if (value32 & WINTINI_RDY)
			return 0;

		udelay(FW_8192C_POLLING_DELAY);

	} while (counter++ < FW_8192C_POLLING_TIMEOUT_COUNT);

	pr_err("Polling FW ready fail!! REG_MCUFWDL:0x%08x .\n",
	       value32);

exit:
	return err;
}

int rtl88e_download_fw(struct ieee80211_hw *hw,
		       bool buse_wake_on_wlan_fw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtlwifi_firmware_header *pfwheader;
	u8 *pfwdata;
	u32 fwsize;
	int err;
	enum version_8188e version = rtlhal->version;

	if (!rtlhal->pfirmware)
		return 1;

	pfwheader = (struct rtlwifi_firmware_header *)rtlhal->pfirmware;
	rtlhal->fw_version = le16_to_cpu(pfwheader->version);
	rtlhal->fw_subversion = pfwheader->subversion;
	pfwdata = rtlhal->pfirmware;
	fwsize = rtlhal->fwsize;
	RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
		 "normal Firmware SIZE %d\n", fwsize);

	if (IS_FW_HEADER_EXIST(pfwheader)) {
		RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
			 "Firmware Version(%d), Signature(%#x), Size(%d)\n",
			  pfwheader->version, pfwheader->signature,
			  (int)sizeof(struct rtlwifi_firmware_header));

		pfwdata = pfwdata + sizeof(struct rtlwifi_firmware_header);
		fwsize = fwsize - sizeof(struct rtlwifi_firmware_header);
	}

	if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) {
		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0);
		rtl88e_firmware_selfreset(hw);
	}
	_rtl88e_enable_fw_download(hw, true);
	_rtl88e_write_fw(hw, version, pfwdata, fwsize);
	_rtl88e_enable_fw_download(hw, false);

	err = _rtl88e_fw_free_to_go(hw);
	if (err)
		pr_err("Firmware is not ready to run!\n");

	return 0;
}

static bool _rtl88e_check_fw_read_last_h2c(struct ieee80211_hw *hw, u8 boxnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 val_hmetfr;

	val_hmetfr = rtl_read_byte(rtlpriv, REG_HMETFR);
	if (((val_hmetfr >> boxnum) & BIT(0)) == 0)
		return true;
	return false;
}

static void _rtl88e_fill_h2c_command(struct ieee80211_hw *hw,
				     u8 element_id, u32 cmd_len,
				     u8 *cmd_b)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 boxnum;
	u16 box_reg = 0, box_extreg = 0;
	u8 u1b_tmp;
	bool isfw_read = false;
	u8 buf_index = 0;
	bool write_sucess = false;
	u8 wait_h2c_limmit = 100;
	u8 wait_writeh2c_limit = 100;
	u8 boxcontent[4], boxextcontent[4];
	u32 h2c_waitcounter = 0;
	unsigned long flag;
	u8 idx;

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "come in\n");

	while (true) {
		spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
		if (rtlhal->h2c_setinprogress) {
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "H2C set in progress! Wait to set..element_id(%d).\n",
				 element_id);

			while (rtlhal->h2c_setinprogress) {
				spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock,
						       flag);
				h2c_waitcounter++;
				RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
					 "Wait 100 us (%d times)...\n",
					 h2c_waitcounter);
				udelay(100);

				if (h2c_waitcounter > 1000)
					return;
				spin_lock_irqsave(&rtlpriv->locks.h2c_lock,
						  flag);
			}
			spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);
		} else {
			rtlhal->h2c_setinprogress = true;
			spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);
			break;
		}
	}

	while (!write_sucess) {
		wait_writeh2c_limit--;
		if (wait_writeh2c_limit == 0) {
			pr_err("Write H2C fail because no trigger for FW INT!\n");
			break;
		}

		boxnum = rtlhal->last_hmeboxnum;
		switch (boxnum) {
		case 0:
			box_reg = REG_HMEBOX_0;
			box_extreg = REG_HMEBOX_EXT_0;
			break;
		case 1:
			box_reg = REG_HMEBOX_1;
			box_extreg = REG_HMEBOX_EXT_1;
			break;
		case 2:
			box_reg = REG_HMEBOX_2;
			box_extreg = REG_HMEBOX_EXT_2;
			break;
		case 3:
			box_reg = REG_HMEBOX_3;
			box_extreg = REG_HMEBOX_EXT_3;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
				 "switch case %#x not processed\n", boxnum);
			break;
		}
		isfw_read = _rtl88e_check_fw_read_last_h2c(hw, boxnum);
		while (!isfw_read) {
			wait_h2c_limmit--;
			if (wait_h2c_limmit == 0) {
				RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
					 "Waiting too long for FW read clear HMEBox(%d)!\n",
					 boxnum);
				break;
			}

			udelay(10);

			isfw_read = _rtl88e_check_fw_read_last_h2c(hw, boxnum);
			u1b_tmp = rtl_read_byte(rtlpriv, 0x130);
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "Waiting for FW read clear HMEBox(%d)!!! 0x130 = %2x\n",
				 boxnum, u1b_tmp);
		}

		if (!isfw_read) {
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 "Write H2C register BOX[%d] fail!!!!! Fw do not read.\n",
				 boxnum);
			break;
		}

		memset(boxcontent, 0, sizeof(boxcontent));
		memset(boxextcontent, 0, sizeof(boxextcontent));
		boxcontent[0] = element_id;
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
			 "Write element_id box_reg(%4x) = %2x\n",
			 box_reg, element_id);

		switch (cmd_len) {
		case 1:
		case 2:
		case 3:
			/*boxcontent[0] &= ~(BIT(7));*/
			memcpy((u8 *)(boxcontent) + 1,
			       cmd_b + buf_index, cmd_len);

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		case 4:
		case 5:
		case 6:
		case 7:
			/*boxcontent[0] |= (BIT(7));*/
			memcpy((u8 *)(boxextcontent),
			       cmd_b + buf_index+3, cmd_len-3);
			memcpy((u8 *)(boxcontent) + 1,
			       cmd_b + buf_index, 3);

			for (idx = 0; idx < 2; idx++) {
				rtl_write_byte(rtlpriv, box_extreg + idx,
					       boxextcontent[idx]);
			}

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
				 "switch case %#x not processed\n", cmd_len);
			break;
		}

		write_sucess = true;

		rtlhal->last_hmeboxnum = boxnum + 1;
		if (rtlhal->last_hmeboxnum == 4)
			rtlhal->last_hmeboxnum = 0;

		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
			 "pHalData->last_hmeboxnum  = %d\n",
			  rtlhal->last_hmeboxnum);
	}

	spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
	rtlhal->h2c_setinprogress = false;
	spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);

	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, "go out\n");
}

void rtl88e_fill_h2c_cmd(struct ieee80211_hw *hw,
			 u8 element_id, u32 cmd_len, u8 *cmdbuffer)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 tmp_cmdbuf[2];

	if (!rtlhal->fw_ready) {
		WARN_ONCE(true,
			  "rtl8188ee: error H2C cmd because of Fw download fail!!!\n");
		return;
	}

	memset(tmp_cmdbuf, 0, 8);
	memcpy(tmp_cmdbuf, cmdbuffer, cmd_len);
	_rtl88e_fill_h2c_command(hw, element_id, cmd_len, (u8 *)&tmp_cmdbuf);

	return;
}

void rtl88e_firmware_selfreset(struct ieee80211_hw *hw)
{
	u8 u1b_tmp;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN+1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN+1, (u1b_tmp & (~BIT(2))));
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN+1, (u1b_tmp | BIT(2)));
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "8051Reset88E(): 8051 reset success\n");

}

void rtl88e_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1_h2c_set_pwrmode[H2C_88E_PWEMODE_LENGTH] = { 0 };
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 rlbm, power_state = 0;
	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, "FW LPS mode = %d\n", mode);

	SET_H2CCMD_PWRMODE_PARM_MODE(u1_h2c_set_pwrmode, ((mode) ? 1 : 0));
	rlbm = 0;/*YJ, temp, 120316. FW now not support RLBM=2.*/
	SET_H2CCMD_PWRMODE_PARM_RLBM(u1_h2c_set_pwrmode, rlbm);
	SET_H2CCMD_PWRMODE_PARM_SMART_PS(u1_h2c_set_pwrmode,
		(rtlpriv->mac80211.p2p) ? ppsc->smart_ps : 1);
	SET_H2CCMD_PWRMODE_PARM_AWAKE_INTERVAL(u1_h2c_set_pwrmode,
		ppsc->reg_max_lps_awakeintvl);
	SET_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1_h2c_set_pwrmode, 0);
	if (mode == FW_PS_ACTIVE_MODE)
		power_state |= FW_PWR_STATE_ACTIVE;
	else
		power_state |= FW_PWR_STATE_RF_OFF;

	SET_H2CCMD_PWRMODE_PARM_PWR_STATE(u1_h2c_set_pwrmode, power_state);

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
		      "rtl92c_set_fw_pwrmode(): u1_h2c_set_pwrmode\n",
		      u1_h2c_set_pwrmode, H2C_88E_PWEMODE_LENGTH);
	rtl88e_fill_h2c_cmd(hw, H2C_88E_SETPWRMODE,
			    H2C_88E_PWEMODE_LENGTH, u1_h2c_set_pwrmode);
}

void rtl88e_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus)
{
	u8 u1_joinbssrpt_parm[1] = { 0 };

	SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(u1_joinbssrpt_parm, mstatus);

	rtl88e_fill_h2c_cmd(hw, H2C_88E_JOINBSSRPT, 1, u1_joinbssrpt_parm);
}

void rtl88e_set_fw_ap_off_load_cmd(struct ieee80211_hw *hw,
				   u8 ap_offload_enable)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 u1_apoffload_parm[H2C_88E_AP_OFFLOAD_LENGTH] = { 0 };

	SET_H2CCMD_AP_OFFLOAD_ON(u1_apoffload_parm, ap_offload_enable);
	SET_H2CCMD_AP_OFFLOAD_HIDDEN(u1_apoffload_parm, mac->hiddenssid);
	SET_H2CCMD_AP_OFFLOAD_DENYANY(u1_apoffload_parm, 0);

	rtl88e_fill_h2c_cmd(hw, H2C_88E_AP_OFFLOAD,
			    H2C_88E_AP_OFFLOAD_LENGTH, u1_apoffload_parm);

}

#define BEACON_PG		0 /* ->1 */
#define PSPOLL_PG		2
#define NULL_PG			3
#define PROBERSP_PG		4 /* ->5 */

#define TOTAL_RESERVED_PKT_LEN	768

static u8 reserved_page_packet[TOTAL_RESERVED_PKT_LEN] = {
	/* page 0 beacon */
	0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0x00, 0xE0, 0x4C, 0x76, 0x00, 0x42,
	0x00, 0x40, 0x10, 0x10, 0x00, 0x03, 0x50, 0x08,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x64, 0x00, 0x00, 0x04, 0x00, 0x0C, 0x6C, 0x69,
	0x6E, 0x6B, 0x73, 0x79, 0x73, 0x5F, 0x77, 0x6C,
	0x61, 0x6E, 0x01, 0x04, 0x82, 0x84, 0x8B, 0x96,
	0x03, 0x01, 0x01, 0x06, 0x02, 0x00, 0x00, 0x2A,
	0x01, 0x00, 0x32, 0x08, 0x24, 0x30, 0x48, 0x6C,
	0x0C, 0x12, 0x18, 0x60, 0x2D, 0x1A, 0x6C, 0x18,
	0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x3D, 0x00, 0xDD, 0x06, 0x00, 0xE0, 0x4C, 0x02,
	0x01, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 1 beacon */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x10, 0x00, 0x20, 0x8C, 0x00, 0x12, 0x10, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 2  ps-poll */
	0xA4, 0x10, 0x01, 0xC0, 0x00, 0x40, 0x10, 0x10,
	0x00, 0x03, 0x00, 0xE0, 0x4C, 0x76, 0x00, 0x42,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x18, 0x00, 0x20, 0x8C, 0x00, 0x12, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 3  null */
	0x48, 0x01, 0x00, 0x00, 0x00, 0x40, 0x10, 0x10,
	0x00, 0x03, 0x00, 0xE0, 0x4C, 0x76, 0x00, 0x42,
	0x00, 0x40, 0x10, 0x10, 0x00, 0x03, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x72, 0x00, 0x20, 0x8C, 0x00, 0x12, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
	0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 4  probe_resp */
	0x50, 0x00, 0x00, 0x00, 0x00, 0x40, 0x10, 0x10,
	0x00, 0x03, 0x00, 0xE0, 0x4C, 0x76, 0x00, 0x42,
	0x00, 0x40, 0x10, 0x10, 0x00, 0x03, 0x00, 0x00,
	0x9E, 0x46, 0x15, 0x32, 0x27, 0xF2, 0x2D, 0x00,
	0x64, 0x00, 0x00, 0x04, 0x00, 0x0C, 0x6C, 0x69,
	0x6E, 0x6B, 0x73, 0x79, 0x73, 0x5F, 0x77, 0x6C,
	0x61, 0x6E, 0x01, 0x04, 0x82, 0x84, 0x8B, 0x96,
	0x03, 0x01, 0x01, 0x06, 0x02, 0x00, 0x00, 0x2A,
	0x01, 0x00, 0x32, 0x08, 0x24, 0x30, 0x48, 0x6C,
	0x0C, 0x12, 0x18, 0x60, 0x2D, 0x1A, 0x6C, 0x18,
	0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x3D, 0x00, 0xDD, 0x06, 0x00, 0xE0, 0x4C, 0x02,
	0x01, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 5  probe_resp */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

void rtl88e_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct sk_buff *skb = NULL;
	u32 totalpacketlen;
	bool rtstatus;
	u8 u1rsvdpageloc[5] = { 0 };
	bool b_dlok = false;
	u8 *beacon;
	u8 *p_pspoll;
	u8 *nullfunc;
	u8 *p_probersp;

	/*---------------------------------------------------------
	 *			(1) beacon
	 *---------------------------------------------------------
	 */
	beacon = &reserved_page_packet[BEACON_PG * 128];
	SET_80211_HDR_ADDRESS2(beacon, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(beacon, mac->bssid);

	/*-------------------------------------------------------
	 *			(2) ps-poll
	 *--------------------------------------------------------
	 */
	p_pspoll = &reserved_page_packet[PSPOLL_PG * 128];
	SET_80211_PS_POLL_AID(p_pspoll, (mac->assoc_id | 0xc000));
	SET_80211_PS_POLL_BSSID(p_pspoll, mac->bssid);
	SET_80211_PS_POLL_TA(p_pspoll, mac->mac_addr);

	SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(u1rsvdpageloc, PSPOLL_PG);

	/*--------------------------------------------------------
	 *			(3) null data
	 *---------------------------------------------------------
	 */
	nullfunc = &reserved_page_packet[NULL_PG * 128];
	SET_80211_HDR_ADDRESS1(nullfunc, mac->bssid);
	SET_80211_HDR_ADDRESS2(nullfunc, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(nullfunc, mac->bssid);

	SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(u1rsvdpageloc, NULL_PG);

	/*---------------------------------------------------------
	 *			(4) probe response
	 *----------------------------------------------------------
	 */
	p_probersp = &reserved_page_packet[PROBERSP_PG * 128];
	SET_80211_HDR_ADDRESS1(p_probersp, mac->bssid);
	SET_80211_HDR_ADDRESS2(p_probersp, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(p_probersp, mac->bssid);

	SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1rsvdpageloc, PROBERSP_PG);

	totalpacketlen = TOTAL_RESERVED_PKT_LEN;

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "rtl88e_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      &reserved_page_packet[0], totalpacketlen);
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
		      "rtl88e_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      u1rsvdpageloc, 3);

	skb = dev_alloc_skb(totalpacketlen);
	skb_put_data(skb, &reserved_page_packet, totalpacketlen);

	rtstatus = rtl_cmd_send_packet(hw, skb);

	if (rtstatus)
		b_dlok = true;

	if (b_dlok) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "Set RSVD page location to Fw.\n");
		RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
			      "H2C_RSVDPAGE:\n", u1rsvdpageloc, 3);
		rtl88e_fill_h2c_cmd(hw, H2C_88E_RSVDPAGE,
				    sizeof(u1rsvdpageloc), u1rsvdpageloc);
	} else
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Set RSVD page location to Fw FAIL!!!!!!.\n");
}

/*Should check FW support p2p or not.*/
static void rtl88e_set_p2p_ctw_period_cmd(struct ieee80211_hw *hw, u8 ctwindow)
{
	u8 u1_ctwindow_period[1] = { ctwindow};

	rtl88e_fill_h2c_cmd(hw, H2C_88E_P2P_PS_CTW_CMD, 1, u1_ctwindow_period);

}

void rtl88e_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *rtlps = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_p2p_ps_info *p2pinfo = &(rtlps->p2p_ps_info);
	struct p2p_ps_offload_t *p2p_ps_offload = &rtlhal->p2p_ps_offload;
	u8	i;
	u16	ctwindow;
	u32	start_time, tsf_low;

	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		RT_TRACE(rtlpriv, COMP_FW, DBG_LOUD, "P2P_PS_DISABLE\n");
		memset(p2p_ps_offload, 0, sizeof(*p2p_ps_offload));
		break;
	case P2P_PS_ENABLE:
		RT_TRACE(rtlpriv, COMP_FW, DBG_LOUD, "P2P_PS_ENABLE\n");
		/* update CTWindow value. */
		if (p2pinfo->ctwindow > 0) {
			p2p_ps_offload->ctwindow_en = 1;
			ctwindow = p2pinfo->ctwindow;
			rtl88e_set_p2p_ctw_period_cmd(hw, ctwindow);
		}

		/* hw only support 2 set of NoA */
		for (i = 0 ; i < p2pinfo->noa_num; i++) {
			/* To control the register setting for which NOA*/
			rtl_write_byte(rtlpriv, 0x5cf, (i << 4));
			if (i == 0)
				p2p_ps_offload->noa0_en = 1;
			else
				p2p_ps_offload->noa1_en = 1;

			/* config P2P NoA Descriptor Register */
			rtl_write_dword(rtlpriv, 0x5E0,
					p2pinfo->noa_duration[i]);
			rtl_write_dword(rtlpriv, 0x5E4,
					p2pinfo->noa_interval[i]);

			/*Get Current TSF value */
			tsf_low = rtl_read_dword(rtlpriv, REG_TSFTR);

			start_time = p2pinfo->noa_start_time[i];
			if (p2pinfo->noa_count_type[i] != 1) {
				while (start_time <= (tsf_low+(50*1024))) {
					start_time += p2pinfo->noa_interval[i];
					if (p2pinfo->noa_count_type[i] != 255)
						p2pinfo->noa_count_type[i]--;
				}
			}
			rtl_write_dword(rtlpriv, 0x5E8, start_time);
			rtl_write_dword(rtlpriv, 0x5EC,
					p2pinfo->noa_count_type[i]);
		}

		if ((p2pinfo->opp_ps == 1) || (p2pinfo->noa_num > 0)) {
			/* rst p2p circuit */
			rtl_write_byte(rtlpriv, REG_DUAL_TSF_RST, BIT(4));

			p2p_ps_offload->offload_en = 1;

			if (P2P_ROLE_GO == rtlpriv->mac80211.p2p) {
				p2p_ps_offload->role = 1;
				p2p_ps_offload->allstasleep = -1;
			} else {
				p2p_ps_offload->role = 0;
			}

			p2p_ps_offload->discovery = 0;
		}
		break;
	case P2P_PS_SCAN:
		RT_TRACE(rtlpriv, COMP_FW, DBG_LOUD, "P2P_PS_SCAN\n");
		p2p_ps_offload->discovery = 1;
		break;
	case P2P_PS_SCAN_DONE:
		RT_TRACE(rtlpriv, COMP_FW, DBG_LOUD, "P2P_PS_SCAN_DONE\n");
		p2p_ps_offload->discovery = 0;
		p2pinfo->p2p_ps_state = P2P_PS_ENABLE;
		break;
	default:
		break;
	}

	rtl88e_fill_h2c_cmd(hw, H2C_88E_P2P_PS_OFFLOAD, 1,
			    (u8 *)p2p_ps_offload);

}
