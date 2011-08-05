/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "fw.h"
#include "sw.h"

static bool _rtl92d_is_fw_downloaded(struct rtl_priv *rtlpriv)
{
	return (rtl_read_dword(rtlpriv, REG_MCUFWDL) & MCUFWDL_RDY) ?
		true : false;
}

static void _rtl92d_enable_fw_download(struct ieee80211_hw *hw, bool enable)
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
		/* Reserved for fw extension.
		 * 0x81[7] is used for mac0 status ,
		 * so don't write this reg here
		 * rtl_write_byte(rtlpriv, REG_MCUFWDL + 1, 0x00);*/
	}
}

static void _rtl92d_fw_block_write(struct ieee80211_hw *hw,
				   const u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 blocksize = sizeof(u32);
	u8 *bufferptr = (u8 *) buffer;
	u32 *pu4BytePtr = (u32 *) buffer;
	u32 i, offset, blockCount, remainSize;

	blockCount = size / blocksize;
	remainSize = size % blocksize;
	for (i = 0; i < blockCount; i++) {
		offset = i * blocksize;
		rtl_write_dword(rtlpriv, (FW_8192D_START_ADDRESS + offset),
				*(pu4BytePtr + i));
	}
	if (remainSize) {
		offset = blockCount * blocksize;
		bufferptr += offset;
		for (i = 0; i < remainSize; i++) {
			rtl_write_byte(rtlpriv, (FW_8192D_START_ADDRESS +
						 offset + i), *(bufferptr + i));
		}
	}
}

static void _rtl92d_fw_page_write(struct ieee80211_hw *hw,
				  u32 page, const u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value8;
	u8 u8page = (u8) (page & 0x07);

	value8 = (rtl_read_byte(rtlpriv, REG_MCUFWDL + 2) & 0xF8) | u8page;
	rtl_write_byte(rtlpriv, (REG_MCUFWDL + 2), value8);
	_rtl92d_fw_block_write(hw, buffer, size);
}

static void _rtl92d_fill_dummy(u8 *pfwbuf, u32 *pfwlen)
{
	u32 fwlen = *pfwlen;
	u8 remain = (u8) (fwlen % 4);

	remain = (remain == 0) ? 0 : (4 - remain);
	while (remain > 0) {
		pfwbuf[fwlen] = 0;
		fwlen++;
		remain--;
	}
	*pfwlen = fwlen;
}

static void _rtl92d_write_fw(struct ieee80211_hw *hw,
			     enum version_8192d version, u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 *bufferPtr = (u8 *) buffer;
	u32 pagenums, remainSize;
	u32 page, offset;

	RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE, ("FW size is %d bytes,\n", size));
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192DE)
		_rtl92d_fill_dummy(bufferPtr, &size);
	pagenums = size / FW_8192D_PAGE_SIZE;
	remainSize = size % FW_8192D_PAGE_SIZE;
	if (pagenums > 8) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Page numbers should not greater then 8\n"));
	}
	for (page = 0; page < pagenums; page++) {
		offset = page * FW_8192D_PAGE_SIZE;
		_rtl92d_fw_page_write(hw, page, (bufferPtr + offset),
				      FW_8192D_PAGE_SIZE);
	}
	if (remainSize) {
		offset = pagenums * FW_8192D_PAGE_SIZE;
		page = pagenums;
		_rtl92d_fw_page_write(hw, page, (bufferPtr + offset),
				      remainSize);
	}
}

static int _rtl92d_fw_free_to_go(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 counter = 0;
	u32 value32;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	} while ((counter++ < FW_8192D_POLLING_TIMEOUT_COUNT) &&
		 (!(value32 & FWDL_ChkSum_rpt)));
	if (counter >= FW_8192D_POLLING_TIMEOUT_COUNT) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("chksum report faill ! REG_MCUFWDL:0x%08x .\n",
			 value32));
		return -EIO;
	}
	RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
		 ("Checksum report OK ! REG_MCUFWDL:0x%08x .\n", value32));
	value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);
	return 0;
}

void rtl92d_firmware_selfreset(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1b_tmp;
	u8 delay = 100;

	/* Set (REG_HMETFR + 3) to  0x20 is reset 8051 */
	rtl_write_byte(rtlpriv, REG_HMETFR + 3, 0x20);
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	while (u1b_tmp & BIT(2)) {
		delay--;
		if (delay == 0)
			break;
		udelay(50);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	}
	RT_ASSERT((delay > 0), ("8051 reset failed!\n"));
	RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
		 ("=====> 8051 reset success (%d) .\n", delay));
}

static int _rtl92d_fw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 counter;

	RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG, ("FW already have download\n"));
	/* polling for FW ready */
	counter = 0;
	do {
		if (rtlhal->interfaceindex == 0) {
			if (rtl_read_byte(rtlpriv, FW_MAC0_READY) &
			    MAC0_READY) {
				RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
					 ("Polling FW ready success!! "
					 "REG_MCUFWDL: 0x%x .\n",
					 rtl_read_byte(rtlpriv,
					 FW_MAC0_READY)));
				return 0;
			}
			udelay(5);
		} else {
			if (rtl_read_byte(rtlpriv, FW_MAC1_READY) &
			    MAC1_READY) {
				RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
					 ("Polling FW ready success!! "
					 "REG_MCUFWDL: 0x%x .\n",
					 rtl_read_byte(rtlpriv,
						       FW_MAC1_READY)));
				return 0;
			}
			udelay(5);
		}
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (rtlhal->interfaceindex == 0) {
		RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
			 ("Polling FW ready fail!! MAC0 FW init not ready: "
			 "0x%x .\n",
			 rtl_read_byte(rtlpriv, FW_MAC0_READY)));
	} else {
		RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
			 ("Polling FW ready fail!! MAC1 FW init not ready: "
			 "0x%x .\n",
			 rtl_read_byte(rtlpriv, FW_MAC1_READY)));
	}
	RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
		 ("Polling FW ready fail!! REG_MCUFWDL:0x%08ul .\n",
		 rtl_read_dword(rtlpriv, REG_MCUFWDL)));
	return -1;
}

int rtl92d_download_fw(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 *pfwheader;
	u8 *pfwdata;
	u32 fwsize;
	int err;
	enum version_8192d version = rtlhal->version;
	u8 value;
	u32 count;
	bool fw_downloaded = false, fwdl_in_process = false;
	unsigned long flags;

	if (!rtlhal->pfirmware)
		return 1;
	fwsize = rtlhal->fwsize;
	pfwheader = (u8 *) rtlhal->pfirmware;
	pfwdata = (u8 *) rtlhal->pfirmware;
	rtlhal->fw_version = (u16) GET_FIRMWARE_HDR_VERSION(pfwheader);
	rtlhal->fw_subversion = (u16) GET_FIRMWARE_HDR_SUB_VER(pfwheader);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, (" FirmwareVersion(%d),"
		 "FirmwareSubVersion(%d), Signature(%#x)\n",
		 rtlhal->fw_version,	rtlhal->fw_subversion,
		 GET_FIRMWARE_HDR_SIGNATURE(pfwheader)));
	if (IS_FW_HEADER_EXIST(pfwheader)) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 ("Shift 32 bytes for FW header!!\n"));
		pfwdata = pfwdata + 32;
		fwsize = fwsize - 32;
	}

	spin_lock_irqsave(&globalmutex_for_fwdownload, flags);
	fw_downloaded = _rtl92d_is_fw_downloaded(rtlpriv);
	if ((rtl_read_byte(rtlpriv, 0x1f) & BIT(5)) == BIT(5))
		fwdl_in_process = true;
	else
		fwdl_in_process = false;
	if (fw_downloaded) {
		spin_unlock_irqrestore(&globalmutex_for_fwdownload, flags);
		goto exit;
	} else if (fwdl_in_process) {
		spin_unlock_irqrestore(&globalmutex_for_fwdownload, flags);
		for (count = 0; count < 5000; count++) {
			udelay(500);
			spin_lock_irqsave(&globalmutex_for_fwdownload, flags);
			fw_downloaded = _rtl92d_is_fw_downloaded(rtlpriv);
			if ((rtl_read_byte(rtlpriv, 0x1f) & BIT(5)) == BIT(5))
				fwdl_in_process = true;
			else
				fwdl_in_process = false;
			spin_unlock_irqrestore(&globalmutex_for_fwdownload,
					       flags);
			if (fw_downloaded)
				goto exit;
			else if (!fwdl_in_process)
				break;
			else
				RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
					 ("Wait for another mac "
					 "download fw\n"));
		}
		spin_lock_irqsave(&globalmutex_for_fwdownload, flags);
		value = rtl_read_byte(rtlpriv, 0x1f);
		value |= BIT(5);
		rtl_write_byte(rtlpriv, 0x1f, value);
		spin_unlock_irqrestore(&globalmutex_for_fwdownload, flags);
	} else {
		value = rtl_read_byte(rtlpriv, 0x1f);
		value |= BIT(5);
		rtl_write_byte(rtlpriv, 0x1f, value);
		spin_unlock_irqrestore(&globalmutex_for_fwdownload, flags);
	}

	/* If 8051 is running in RAM code, driver should
	 * inform Fw to reset by itself, or it will cause
	 * download Fw fail.*/
	/* 8051 RAM code */
	if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) {
		rtl92d_firmware_selfreset(hw);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);
	}
	_rtl92d_enable_fw_download(hw, true);
	_rtl92d_write_fw(hw, version, pfwdata, fwsize);
	_rtl92d_enable_fw_download(hw, false);
	spin_lock_irqsave(&globalmutex_for_fwdownload, flags);
	err = _rtl92d_fw_free_to_go(hw);
	/* download fw over,clear 0x1f[5] */
	value = rtl_read_byte(rtlpriv, 0x1f);
	value &= (~BIT(5));
	rtl_write_byte(rtlpriv, 0x1f, value);
	spin_unlock_irqrestore(&globalmutex_for_fwdownload, flags);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("fw is not ready to run!\n"));
		goto exit;
	} else {
		RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
			 ("fw is ready to run!\n"));
	}
exit:
	err = _rtl92d_fw_init(hw);
	return err;
}

static bool _rtl92d_check_fw_read_last_h2c(struct ieee80211_hw *hw, u8 boxnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 val_hmetfr;
	bool result = false;

	val_hmetfr = rtl_read_byte(rtlpriv, REG_HMETFR);
	if (((val_hmetfr >> boxnum) & BIT(0)) == 0)
		result = true;
	return result;
}

static void _rtl92d_fill_h2c_command(struct ieee80211_hw *hw,
			      u8 element_id, u32 cmd_len, u8 *cmdbuffer)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 boxnum;
	u16 box_reg = 0, box_extreg = 0;
	u8 u1b_tmp;
	bool isfw_read = false;
	u8 buf_index = 0;
	bool bwrite_sucess = false;
	u8 wait_h2c_limmit = 100;
	u8 wait_writeh2c_limmit = 100;
	u8 boxcontent[4], boxextcontent[2];
	u32 h2c_waitcounter = 0;
	unsigned long flag;
	u8 idx;

	if (ppsc->rfpwr_state == ERFOFF || ppsc->inactive_pwrstate == ERFOFF) {
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
			 ("Return as RF is off!!!\n"));
		return;
	}
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, ("come in\n"));
	while (true) {
		spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
		if (rtlhal->h2c_setinprogress) {
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 ("H2C set in progress! Wait to set.."
				 "element_id(%d).\n", element_id));

			while (rtlhal->h2c_setinprogress) {
				spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock,
						       flag);
				h2c_waitcounter++;
				RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
					 ("Wait 100 us (%d times)...\n",
					 h2c_waitcounter));
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
	while (!bwrite_sucess) {
		wait_writeh2c_limmit--;
		if (wait_writeh2c_limmit == 0) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 ("Write H2C fail because no trigger "
				 "for FW INT!\n"));
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
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 ("switch case not process\n"));
			break;
		}
		isfw_read = _rtl92d_check_fw_read_last_h2c(hw, boxnum);
		while (!isfw_read) {
			wait_h2c_limmit--;
			if (wait_h2c_limmit == 0) {
				RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
					 ("Wating too long for FW read "
					 "clear HMEBox(%d)!\n", boxnum));
				break;
			}
			udelay(10);
			isfw_read = _rtl92d_check_fw_read_last_h2c(hw, boxnum);
			u1b_tmp = rtl_read_byte(rtlpriv, 0x1BF);
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 ("Wating for FW read clear HMEBox(%d)!!! "
				 "0x1BF = %2x\n", boxnum, u1b_tmp));
		}
		if (!isfw_read) {
			RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
				 ("Write H2C register BOX[%d] fail!!!!! "
				 "Fw do not read.\n", boxnum));
			break;
		}
		memset(boxcontent, 0, sizeof(boxcontent));
		memset(boxextcontent, 0, sizeof(boxextcontent));
		boxcontent[0] = element_id;
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
			 ("Write element_id box_reg(%4x) = %2x\n",
			 box_reg, element_id));
		switch (cmd_len) {
		case 1:
			boxcontent[0] &= ~(BIT(7));
			memcpy(boxcontent + 1, cmdbuffer + buf_index, 1);
			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		case 2:
			boxcontent[0] &= ~(BIT(7));
			memcpy(boxcontent + 1, cmdbuffer + buf_index, 2);
			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		case 3:
			boxcontent[0] &= ~(BIT(7));
			memcpy(boxcontent + 1, cmdbuffer + buf_index, 3);
			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		case 4:
			boxcontent[0] |= (BIT(7));
			memcpy(boxextcontent, cmdbuffer + buf_index, 2);
			memcpy(boxcontent + 1, cmdbuffer + buf_index + 2, 2);
			for (idx = 0; idx < 2; idx++)
				rtl_write_byte(rtlpriv, box_extreg + idx,
					       boxextcontent[idx]);
			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		case 5:
			boxcontent[0] |= (BIT(7));
			memcpy(boxextcontent, cmdbuffer + buf_index, 2);
			memcpy(boxcontent + 1, cmdbuffer + buf_index + 2, 3);
			for (idx = 0; idx < 2; idx++)
				rtl_write_byte(rtlpriv, box_extreg + idx,
					       boxextcontent[idx]);
			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				("switch case not process\n"));
			break;
		}
		bwrite_sucess = true;
		rtlhal->last_hmeboxnum = boxnum + 1;
		if (rtlhal->last_hmeboxnum == 4)
			rtlhal->last_hmeboxnum = 0;
		RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
			 ("pHalData->last_hmeboxnum  = %d\n",
			  rtlhal->last_hmeboxnum));
	}
	spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
	rtlhal->h2c_setinprogress = false;
	spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);
	RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD, ("go out\n"));
}

void rtl92d_fill_h2c_cmd(struct ieee80211_hw *hw,
			 u8 element_id, u32 cmd_len, u8 *cmdbuffer)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 tmp_cmdbuf[2];

	if (rtlhal->fw_ready == false) {
		RT_ASSERT(false, ("return H2C cmd because of Fw "
				  "download fail!!!\n"));
		return;
	}
	memset(tmp_cmdbuf, 0, 8);
	memcpy(tmp_cmdbuf, cmdbuffer, cmd_len);
	_rtl92d_fill_h2c_command(hw, element_id, cmd_len, (u8 *)&tmp_cmdbuf);
	return;
}

void rtl92d_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1_h2c_set_pwrmode[3] = { 0 };
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, ("FW LPS mode = %d\n", mode));
	SET_H2CCMD_PWRMODE_PARM_MODE(u1_h2c_set_pwrmode, mode);
	SET_H2CCMD_PWRMODE_PARM_SMART_PS(u1_h2c_set_pwrmode, 1);
	SET_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1_h2c_set_pwrmode,
					      ppsc->reg_max_lps_awakeintvl);
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
		      "rtl92d_set_fw_rsvdpagepkt(): u1_h2c_set_pwrmode\n",
		      u1_h2c_set_pwrmode, 3);
	rtl92d_fill_h2c_cmd(hw, H2C_SETPWRMODE, 3, u1_h2c_set_pwrmode);
}

static bool _rtl92d_cmd_send_packet(struct ieee80211_hw *hw,
				    struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	u8 idx = 0;
	unsigned long flags;
	struct sk_buff *pskb;

	ring = &rtlpci->tx_ring[BEACON_QUEUE];
	pskb = __skb_dequeue(&ring->queue);
	if (pskb)
		kfree_skb(pskb);
	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	pdesc = &ring->desc[idx];
	/* discard output from call below */
	rtlpriv->cfg->ops->get_desc((u8 *) pdesc, true, HW_DESC_OWN);
	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *) pdesc, 1, 1, skb);
	__skb_queue_tail(&ring->queue, skb);
	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
	rtlpriv->cfg->ops->tx_polling(hw, BEACON_QUEUE);
	return true;
}

#define BEACON_PG		0	/*->1 */
#define PSPOLL_PG		2
#define NULL_PG			3
#define PROBERSP_PG		4	/*->5 */
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

void rtl92d_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool dl_finished)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct sk_buff *skb = NULL;
	u32 totalpacketlen;
	bool rtstatus;
	u8 u1RsvdPageLoc[3] = { 0 };
	bool dlok = false;
	u8 *beacon;
	u8 *p_pspoll;
	u8 *nullfunc;
	u8 *p_probersp;
	/*---------------------------------------------------------
						(1) beacon
	---------------------------------------------------------*/
	beacon = &reserved_page_packet[BEACON_PG * 128];
	SET_80211_HDR_ADDRESS2(beacon, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(beacon, mac->bssid);
	/*-------------------------------------------------------
						(2) ps-poll
	--------------------------------------------------------*/
	p_pspoll = &reserved_page_packet[PSPOLL_PG * 128];
	SET_80211_PS_POLL_AID(p_pspoll, (mac->assoc_id | 0xc000));
	SET_80211_PS_POLL_BSSID(p_pspoll, mac->bssid);
	SET_80211_PS_POLL_TA(p_pspoll, mac->mac_addr);
	SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(u1RsvdPageLoc, PSPOLL_PG);
	/*--------------------------------------------------------
						(3) null data
	---------------------------------------------------------*/
	nullfunc = &reserved_page_packet[NULL_PG * 128];
	SET_80211_HDR_ADDRESS1(nullfunc, mac->bssid);
	SET_80211_HDR_ADDRESS2(nullfunc, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(nullfunc, mac->bssid);
	SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(u1RsvdPageLoc, NULL_PG);
	/*---------------------------------------------------------
						(4) probe response
	----------------------------------------------------------*/
	p_probersp = &reserved_page_packet[PROBERSP_PG * 128];
	SET_80211_HDR_ADDRESS1(p_probersp, mac->bssid);
	SET_80211_HDR_ADDRESS2(p_probersp, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(p_probersp, mac->bssid);
	SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1RsvdPageLoc, PROBERSP_PG);
	totalpacketlen = TOTAL_RESERVED_PKT_LEN;
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "rtl92d_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      &reserved_page_packet[0], totalpacketlen);
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
		      "rtl92d_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      u1RsvdPageLoc, 3);
	skb = dev_alloc_skb(totalpacketlen);
	memcpy((u8 *) skb_put(skb, totalpacketlen), &reserved_page_packet,
		totalpacketlen);
	rtstatus = _rtl92d_cmd_send_packet(hw, skb);

	if (rtstatus)
		dlok = true;
	if (dlok) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			("Set RSVD page location to Fw.\n"));
		RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
			      "H2C_RSVDPAGE:\n", u1RsvdPageLoc, 3);
		rtl92d_fill_h2c_cmd(hw, H2C_RSVDPAGE,
			sizeof(u1RsvdPageLoc), u1RsvdPageLoc);
	} else
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			("Set RSVD page location to Fw FAIL!!!!!!.\n"));
}

void rtl92d_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus)
{
	u8 u1_joinbssrpt_parm[1] = {0};

	SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(u1_joinbssrpt_parm, mstatus);
	rtl92d_fill_h2c_cmd(hw, H2C_JOINBSSRPT, 1, u1_joinbssrpt_parm);
}
