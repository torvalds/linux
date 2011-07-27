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

#include <linux/firmware.h>
#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../rtl8192ce/reg.h"
#include "../rtl8192ce/def.h"
#include "fw_common.h"

static void _rtl92c_enable_fw_download(struct ieee80211_hw *hw, bool enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192CU) {
		u32 value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
		if (enable)
			value32 |= MCUFWDL_EN;
		else
			value32 &= ~MCUFWDL_EN;
		rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);
	} else if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192CE) {
		u8 tmp;
		if (enable) {

			tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
			rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1,
				       tmp | 0x04);

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
}

static void _rtl92c_fw_block_write(struct ieee80211_hw *hw,
				   const u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 blockSize = sizeof(u32);
	u8 *bufferPtr = (u8 *) buffer;
	u32 *pu4BytePtr = (u32 *) buffer;
	u32 i, offset, blockCount, remainSize;

	blockCount = size / blockSize;
	remainSize = size % blockSize;

	for (i = 0; i < blockCount; i++) {
		offset = i * blockSize;
		rtl_write_dword(rtlpriv, (FW_8192C_START_ADDRESS + offset),
				*(pu4BytePtr + i));
	}

	if (remainSize) {
		offset = blockCount * blockSize;
		bufferPtr += offset;
		for (i = 0; i < remainSize; i++) {
			rtl_write_byte(rtlpriv, (FW_8192C_START_ADDRESS +
						 offset + i), *(bufferPtr + i));
		}
	}
}

static void _rtl92c_fw_page_write(struct ieee80211_hw *hw,
				  u32 page, const u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value8;
	u8 u8page = (u8) (page & 0x07);

	value8 = (rtl_read_byte(rtlpriv, REG_MCUFWDL + 2) & 0xF8) | u8page;

	rtl_write_byte(rtlpriv, (REG_MCUFWDL + 2), value8);
	_rtl92c_fw_block_write(hw, buffer, size);
}

static void _rtl92c_fill_dummy(u8 *pfwbuf, u32 *pfwlen)
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

static void _rtl92c_write_fw(struct ieee80211_hw *hw,
			     enum version_8192c version, u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 *bufferPtr = (u8 *) buffer;

	RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE, ("FW size is %d bytes,\n", size));

	if (IS_CHIP_VER_B(version)) {
		u32 pageNums, remainSize;
		u32 page, offset;

		if (IS_HARDWARE_TYPE_8192CE(rtlhal))
			_rtl92c_fill_dummy(bufferPtr, &size);

		pageNums = size / FW_8192C_PAGE_SIZE;
		remainSize = size % FW_8192C_PAGE_SIZE;

		if (pageNums > 4) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 ("Page numbers should not greater then 4\n"));
		}

		for (page = 0; page < pageNums; page++) {
			offset = page * FW_8192C_PAGE_SIZE;
			_rtl92c_fw_page_write(hw, page, (bufferPtr + offset),
					      FW_8192C_PAGE_SIZE);
		}

		if (remainSize) {
			offset = pageNums * FW_8192C_PAGE_SIZE;
			page = pageNums;
			_rtl92c_fw_page_write(hw, page, (bufferPtr + offset),
					      remainSize);
		}
	} else {
		_rtl92c_fw_block_write(hw, buffer, size);
	}
}

static int _rtl92c_fw_free_to_go(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 counter = 0;
	u32 value32;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	} while ((counter++ < FW_8192C_POLLING_TIMEOUT_COUNT) &&
		 (!(value32 & FWDL_ChkSum_rpt)));

	if (counter >= FW_8192C_POLLING_TIMEOUT_COUNT) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("chksum report faill ! REG_MCUFWDL:0x%08x .\n",
			  value32));
		return -EIO;
	}

	RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
		 ("Checksum report OK ! REG_MCUFWDL:0x%08x .\n", value32));

	value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);

	counter = 0;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
		if (value32 & WINTINI_RDY) {
			RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
				 ("Polling FW ready success!!"
				 " REG_MCUFWDL:0x%08x .\n",
				 value32));
			return 0;
		}

		mdelay(FW_8192C_POLLING_DELAY);

	} while (counter++ < FW_8192C_POLLING_TIMEOUT_COUNT);

	RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
		 ("Polling FW ready fail!! REG_MCUFWDL:0x%08x .\n", value32));
	return -EIO;
}

int rtl92c_download_fw(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl92c_firmware_header *pfwheader;
	u8 *pfwdata;
	u32 fwsize;
	enum version_8192c version = rtlhal->version;

	printk(KERN_INFO "rtl8192c: Loading firmware file %s\n",
	       rtlpriv->cfg->fw_name);
	if (!rtlhal->pfirmware)
		return 1;

	pfwheader = (struct rtl92c_firmware_header *)rtlhal->pfirmware;
	pfwdata = (u8 *) rtlhal->pfirmware;
	fwsize = rtlhal->fwsize;

	if (IS_FW_HEADER_EXIST(pfwheader)) {
		RT_TRACE(rtlpriv, COMP_FW, DBG_DMESG,
			 ("Firmware Version(%d), Signature(%#x),Size(%d)\n",
			  pfwheader->version, pfwheader->signature,
			  (uint)sizeof(struct rtl92c_firmware_header)));

		pfwdata = pfwdata + sizeof(struct rtl92c_firmware_header);
		fwsize = fwsize - sizeof(struct rtl92c_firmware_header);
	}

	_rtl92c_enable_fw_download(hw, true);
	_rtl92c_write_fw(hw, version, pfwdata, fwsize);
	_rtl92c_enable_fw_download(hw, false);

	if (_rtl92c_fw_free_to_go(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Firmware is not ready to run!\n"));
	} else {
		RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
			 ("Firmware is ready to run!\n"));
	}

	return 0;
}
EXPORT_SYMBOL(rtl92c_download_fw);

static bool _rtl92c_check_fw_read_last_h2c(struct ieee80211_hw *hw, u8 boxnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 val_hmetfr, val_mcutst_1;
	bool result = false;

	val_hmetfr = rtl_read_byte(rtlpriv, REG_HMETFR);
	val_mcutst_1 = rtl_read_byte(rtlpriv, (REG_MCUTST_1 + boxnum));

	if (((val_hmetfr >> boxnum) & BIT(0)) == 0 && val_mcutst_1 == 0)
		result = true;
	return result;
}

static void _rtl92c_fill_h2c_command(struct ieee80211_hw *hw,
			      u8 element_id, u32 cmd_len, u8 *p_cmdbuffer)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 boxnum;
	u16 box_reg = 0, box_extreg = 0;
	u8 u1b_tmp;
	bool isfw_read = false;
	bool bwrite_sucess = false;
	u8 wait_h2c_limmit = 100;
	u8 wait_writeh2c_limmit = 100;
	u8 boxcontent[4], boxextcontent[2];
	u32 h2c_waitcounter = 0;
	unsigned long flag;
	u8 idx;

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

		isfw_read = _rtl92c_check_fw_read_last_h2c(hw, boxnum);
		while (!isfw_read) {

			wait_h2c_limmit--;
			if (wait_h2c_limmit == 0) {
				RT_TRACE(rtlpriv, COMP_CMD, DBG_LOUD,
					 ("Wating too long for FW read "
					  "clear HMEBox(%d)!\n", boxnum));
				break;
			}

			udelay(10);

			isfw_read = _rtl92c_check_fw_read_last_h2c(hw, boxnum);
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
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer, 1);

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		case 2:
			boxcontent[0] &= ~(BIT(7));
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer, 2);

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		case 3:
			boxcontent[0] &= ~(BIT(7));
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer, 3);

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		case 4:
			boxcontent[0] |= (BIT(7));
			memcpy((u8 *) (boxextcontent),
			       p_cmdbuffer, 2);
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer + 2, 2);

			for (idx = 0; idx < 2; idx++) {
				rtl_write_byte(rtlpriv, box_extreg + idx,
					       boxextcontent[idx]);
			}

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		case 5:
			boxcontent[0] |= (BIT(7));
			memcpy((u8 *) (boxextcontent),
			       p_cmdbuffer, 2);
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer + 2, 3);

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

void rtl92c_fill_h2c_cmd(struct ieee80211_hw *hw,
			 u8 element_id, u32 cmd_len, u8 *p_cmdbuffer)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 tmp_cmdbuf[2];

	if (rtlhal->fw_ready == false) {
		RT_ASSERT(false, ("return H2C cmd because of Fw "
				  "download fail!!!\n"));
		return;
	}

	memset(tmp_cmdbuf, 0, 8);
	memcpy(tmp_cmdbuf, p_cmdbuffer, cmd_len);
	_rtl92c_fill_h2c_command(hw, element_id, cmd_len, (u8 *)&tmp_cmdbuf);

	return;
}
EXPORT_SYMBOL(rtl92c_fill_h2c_cmd);

void rtl92c_firmware_selfreset(struct ieee80211_hw *hw)
{
	u8 u1b_tmp;
	u8 delay = 100;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_HMETFR + 3, 0x20);
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);

	while (u1b_tmp & BIT(2)) {
		delay--;
		if (delay == 0) {
			RT_ASSERT(false, ("8051 reset fail.\n"));
			break;
		}
		udelay(50);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	}
}
EXPORT_SYMBOL(rtl92c_firmware_selfreset);

void rtl92c_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1_h2c_set_pwrmode[3] = {0};
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD, ("FW LPS mode = %d\n", mode));

	SET_H2CCMD_PWRMODE_PARM_MODE(u1_h2c_set_pwrmode, mode);
	SET_H2CCMD_PWRMODE_PARM_SMART_PS(u1_h2c_set_pwrmode, 1);
	SET_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1_h2c_set_pwrmode,
					      ppsc->reg_max_lps_awakeintvl);

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
		      "rtl92c_set_fw_rsvdpagepkt(): u1_h2c_set_pwrmode\n",
		      u1_h2c_set_pwrmode, 3);
	rtl92c_fill_h2c_cmd(hw, H2C_SETPWRMODE, 3, u1_h2c_set_pwrmode);

}
EXPORT_SYMBOL(rtl92c_set_fw_pwrmode_cmd);

static bool _rtl92c_cmd_send_packet(struct ieee80211_hw *hw,
				struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	u8 own;
	unsigned long flags;
	struct sk_buff *pskb = NULL;

	ring = &rtlpci->tx_ring[BEACON_QUEUE];

	pskb = __skb_dequeue(&ring->queue);
	if (pskb)
		kfree_skb(pskb);

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	pdesc = &ring->desc[0];
	own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) pdesc, true, HW_DESC_OWN);

	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *) pdesc, 1, 1, skb);

	__skb_queue_tail(&ring->queue, skb);

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	rtlpriv->cfg->ops->tx_polling(hw, BEACON_QUEUE);

	return true;
}

#define BEACON_PG		0 /*->1*/
#define PSPOLL_PG		2
#define NULL_PG			3
#define PROBERSP_PG		4 /*->5*/

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

void rtl92c_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool dl_finished)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct sk_buff *skb = NULL;

	u32 totalpacketlen;
	bool rtstatus;
	u8 u1RsvdPageLoc[3] = {0};
	bool dlok = false;

	u8 *beacon;
	u8 *pspoll;
	u8 *nullfunc;
	u8 *probersp;
	/*---------------------------------------------------------
				(1) beacon
	---------------------------------------------------------*/
	beacon = &reserved_page_packet[BEACON_PG * 128];
	SET_80211_HDR_ADDRESS2(beacon, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(beacon, mac->bssid);

	/*-------------------------------------------------------
				(2) ps-poll
	--------------------------------------------------------*/
	pspoll = &reserved_page_packet[PSPOLL_PG * 128];
	SET_80211_PS_POLL_AID(pspoll, (mac->assoc_id | 0xc000));
	SET_80211_PS_POLL_BSSID(pspoll, mac->bssid);
	SET_80211_PS_POLL_TA(pspoll, mac->mac_addr);

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
	probersp = &reserved_page_packet[PROBERSP_PG * 128];
	SET_80211_HDR_ADDRESS1(probersp, mac->bssid);
	SET_80211_HDR_ADDRESS2(probersp, mac->mac_addr);
	SET_80211_HDR_ADDRESS3(probersp, mac->bssid);

	SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1RsvdPageLoc, PROBERSP_PG);

	totalpacketlen = TOTAL_RESERVED_PKT_LEN;

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD,
		      "rtl92c_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      &reserved_page_packet[0], totalpacketlen);
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
		      "rtl92c_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      u1RsvdPageLoc, 3);


	skb = dev_alloc_skb(totalpacketlen);
	memcpy((u8 *) skb_put(skb, totalpacketlen),
	       &reserved_page_packet, totalpacketlen);

	rtstatus = _rtl92c_cmd_send_packet(hw, skb);

	if (rtstatus)
		dlok = true;

	if (dlok) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 ("Set RSVD page location to Fw.\n"));
		RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_DMESG,
				"H2C_RSVDPAGE:\n",
				u1RsvdPageLoc, 3);
		rtl92c_fill_h2c_cmd(hw, H2C_RSVDPAGE,
				    sizeof(u1RsvdPageLoc), u1RsvdPageLoc);
	} else
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("Set RSVD page location to Fw FAIL!!!!!!.\n"));
}
EXPORT_SYMBOL(rtl92c_set_fw_rsvdpagepkt);

void rtl92c_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus)
{
	u8 u1_joinbssrpt_parm[1] = {0};

	SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(u1_joinbssrpt_parm, mstatus);

	rtl92c_fill_h2c_cmd(hw, H2C_JOINBSSRPT, 1, u1_joinbssrpt_parm);
}
EXPORT_SYMBOL(rtl92c_set_fw_joinbss_report_cmd);
