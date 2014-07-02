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
#include "dm.h"

static void _rtl92ee_enable_fw_download(struct ieee80211_hw *hw, bool enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp;

	if (enable) {
		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x05);

		tmp = rtl_read_byte(rtlpriv, REG_MCUFWDL + 2);
		rtl_write_byte(rtlpriv, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {

		tmp = rtl_read_byte(rtlpriv, REG_MCUFWDL);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, tmp & 0xfe);
	}
}

static void _rtl92ee_fw_block_write(struct ieee80211_hw *hw,
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
			rtl_write_byte(rtlpriv,
				       (FW_8192C_START_ADDRESS + offset + i),
				       *(bufferPtr + i));
		}
	}
}

static void _rtl92ee_fw_page_write(struct ieee80211_hw *hw, u32 page,
				   const u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value8;
	u8 u8page = (u8) (page & 0x07);

	value8 = (rtl_read_byte(rtlpriv, REG_MCUFWDL + 2) & 0xF8) | u8page;
	rtl_write_byte(rtlpriv, (REG_MCUFWDL + 2), value8);

	_rtl92ee_fw_block_write(hw, buffer, size);
}

static void _rtl92ee_fill_dummy(u8 *pfwbuf, u32 *pfwlen)
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

static void _rtl92ee_write_fw(struct ieee80211_hw *hw,
			      enum version_8192e version,
			      u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *bufferPtr = (u8 *) buffer;
	u32 pageNums, remainSize;
	u32 page, offset;

	RT_TRACE(COMP_FW, DBG_LOUD , ("FW size is %d bytes,\n", size));

	_rtl92ee_fill_dummy(bufferPtr, &size);

	pageNums = size / FW_8192C_PAGE_SIZE;
	remainSize = size % FW_8192C_PAGE_SIZE;

	if (pageNums > 8) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Page numbers should not greater then 8\n"));
	}

	for (page = 0; page < pageNums; page++) {
		offset = page * FW_8192C_PAGE_SIZE;
		_rtl92ee_fw_page_write(hw, page, (bufferPtr + offset),
				      FW_8192C_PAGE_SIZE);
		udelay(2);
	}

	if (remainSize) {
		offset = pageNums * FW_8192C_PAGE_SIZE;
		page = pageNums;
		_rtl92ee_fw_page_write(hw, page, (bufferPtr + offset),
				       remainSize);
	}

}

static int _rtl92ee_fw_free_to_go(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err = -EIO;
	u32 counter = 0;
	u32 value32;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	} while ((counter++ < FW_8192C_POLLING_TIMEOUT_COUNT) &&
		 (!(value32 & FWDL_ChkSum_rpt)));

	if (counter >= FW_8192C_POLLING_TIMEOUT_COUNT) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("chksum report faill ! REG_MCUFWDL:0x%08x .\n",
			  value32));
		goto exit;
	}

	RT_TRACE(COMP_FW, DBG_TRACE,
		 ("Checksum report OK ! REG_MCUFWDL:0x%08x .\n", value32));

	value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);

	rtl92ee_firmware_selfreset(hw);
	counter = 0;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
		if (value32 & WINTINI_RDY) {
			RT_TRACE(COMP_FW, DBG_LOUD ,
				("Polling FW ready success!! REG_MCUFWDL:"
				"0x%08x. count = %d\n", value32, counter));
			err = 0;
			goto exit;
		}

		udelay(FW_8192C_POLLING_DELAY*10);

	} while (counter++ < FW_8192C_POLLING_TIMEOUT_COUNT);

	RT_TRACE(COMP_ERR, DBG_EMERG,
		 ("Polling FW ready fail!! REG_MCUFWDL:0x%08x. count = %d\n",
		 value32, counter));

exit:
	return err;
}

int rtl92ee_download_fw(struct ieee80211_hw *hw, bool buse_wake_on_wlan_fw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl92c_firmware_header *pfwheader;
	u8 *pfwdata;
	u32 fwsize;
	int err;
	enum version_8192e version = rtlhal->version;

	if (!rtlhal->pfirmware)
		return 1;

	pfwheader = (struct rtl92c_firmware_header *)rtlhal->pfirmware;
	rtlhal->fw_version = pfwheader->version;
	rtlhal->fw_subversion = pfwheader->subversion;
	pfwdata = (u8 *) rtlhal->pfirmware;
	fwsize = rtlhal->fwsize;
	RT_TRACE(COMP_FW, DBG_DMESG,
		 ("normal Firmware SIZE %d\n" , fwsize));

	if (IS_FW_HEADER_EXIST(pfwheader)) {
		RT_TRACE(COMP_FW, DBG_DMESG,
			 ("Firmware Version(%d), Signature(%#x), Size(%d)\n",
			  pfwheader->version, pfwheader->signature,
			  (int)sizeof(struct rtl92c_firmware_header)));

		pfwdata = pfwdata + sizeof(struct rtl92c_firmware_header);
		fwsize = fwsize - sizeof(struct rtl92c_firmware_header);
	} else {
		RT_TRACE(COMP_FW, DBG_DMESG,
			 ("Firmware no Header, Signature(%#x)\n",
			  pfwheader->signature));
	}

	if (rtlhal->b_mac_func_enable) {
		if (rtl_read_byte(rtlpriv, REG_MCUFWDL) & BIT(7)) {
			rtl_write_byte(rtlpriv, REG_MCUFWDL, 0);
			rtl92ee_firmware_selfreset(hw);
		}
	}
	_rtl92ee_enable_fw_download(hw, true);
	_rtl92ee_write_fw(hw, version, pfwdata, fwsize);
	_rtl92ee_enable_fw_download(hw, false);

	err = _rtl92ee_fw_free_to_go(hw);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Firmware is not ready to run!\n"));
	} else {
		RT_TRACE(COMP_FW, DBG_LOUD ,
			 ("Firmware is ready to run!\n"));
	}

	return 0;
}

static bool _rtl92ee_check_fw_read_last_h2c(struct ieee80211_hw *hw, u8 boxnum)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 val_hmetfr;
	bool result = false;

	val_hmetfr = rtl_read_byte(rtlpriv, REG_HMETFR);
	if (((val_hmetfr >> boxnum) & BIT(0)) == 0)
		result = true;
	return result;
}

static void _rtl92ee_fill_h2c_command(struct ieee80211_hw *hw, u8 element_id,
				      u32 cmd_len, u8 *p_cmdbuffer)
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
	u8 boxcontent[4], boxextcontent[4];
	u32 h2c_waitcounter = 0;
	unsigned long flag;
	u8 idx;

	if (ppsc->dot11_psmode != EACTIVE ||
		ppsc->inactive_pwrstate == ERFOFF) {
		RT_TRACE(COMP_CMD, DBG_LOUD ,
			("FillH2CCommand8192E(): "
			"Return because RF is off!!!\n"));
		return;
	}

	RT_TRACE(COMP_CMD, DBG_LOUD , ("come in\n"));

	/* 1. Prevent race condition in setting H2C cmd.
	 * (copy from MgntActSet_RF_State().)
	 */
	while (true) {
		spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
		if (rtlhal->b_h2c_setinprogress) {
			RT_TRACE(COMP_CMD, DBG_LOUD ,
				 ("H2C set in progress! Wait to set.."
				  "element_id(%d).\n", element_id));

			while (rtlhal->b_h2c_setinprogress) {
				spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock,
						       flag);
				h2c_waitcounter++;
				RT_TRACE(COMP_CMD, DBG_LOUD ,
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
			rtlhal->b_h2c_setinprogress = true;
			spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);
			break;
		}
	}

	while (!bwrite_sucess) {
		/*	cosa remove this because never reach this. */
		/*wait_writeh2c_limmit--;
		if (wait_writeh2c_limmit == 0) {
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("Write H2C fail because no trigger "
				  "for FW INT!\n"));
			break;
		}
		*/
		/* 2. Find the last BOX number which has been writen. */
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
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("switch case not process\n"));
			break;
		}

		/* 3. Check if the box content is empty. */
		isfw_read = false;
		u1b_tmp = rtl_read_byte(rtlpriv, REG_CR);

		if (u1b_tmp != 0xea) {
			isfw_read = true;
		} else {
			if (rtl_read_byte(rtlpriv, REG_TXDMA_STATUS) == 0xea ||
			    rtl_read_byte(rtlpriv, REG_TXPKT_EMPTY) == 0xea)
				rtl_write_byte(rtlpriv, REG_SYS_CFG1 + 3, 0xff);
		}

		if (isfw_read == true) {
			wait_h2c_limmit = 100;
			isfw_read = _rtl92ee_check_fw_read_last_h2c(hw, boxnum);
			while (!isfw_read) {
				wait_h2c_limmit--;
				if (wait_h2c_limmit == 0) {
					RT_TRACE(COMP_CMD, DBG_LOUD ,
						("Wating too long for FW"
						"read clear HMEBox(%d)!!!\n",
						boxnum));
					break;
				}
				udelay(10);
				isfw_read = _rtl92ee_check_fw_read_last_h2c(hw,
									boxnum);
				u1b_tmp = rtl_read_byte(rtlpriv, 0x130);
				RT_TRACE(COMP_CMD, DBG_LOUD ,
					 ("Wating for FW read clear HMEBox(%d)!!! 0x130 = %2x\n",
					 boxnum, u1b_tmp));
			}
		}

		/* If Fw has not read the last
		 H2C cmd, break and give up this H2C. */
		if (!isfw_read) {
			RT_TRACE(COMP_CMD, DBG_LOUD ,
				 ("Write H2C reg BOX[%d] fail, Fw don't read.\n",
				 boxnum));
			break;
		}
		/* 4. Fill the H2C cmd into box */
		memset(boxcontent, 0, sizeof(boxcontent));
		memset(boxextcontent, 0, sizeof(boxextcontent));
		boxcontent[0] = element_id;
		RT_TRACE(COMP_CMD, DBG_LOUD ,
			 ("Write element_id box_reg(%4x) = %2x\n",
			  box_reg, element_id));

		switch (cmd_len) {
		case 1:
		case 2:
		case 3:
			/*boxcontent[0] &= ~(BIT(7));*/
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer + buf_index, cmd_len);

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
			memcpy((u8 *) (boxextcontent),
			       p_cmdbuffer + buf_index+3, cmd_len-3);
			memcpy((u8 *) (boxcontent) + 1,
			       p_cmdbuffer + buf_index, 3);

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_extreg + idx,
					       boxextcontent[idx]);
			}

			for (idx = 0; idx < 4; idx++) {
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			}
			break;
		default:
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("switch case not process\n"));
			break;
		}

		bwrite_sucess = true;

		rtlhal->last_hmeboxnum = boxnum + 1;
		if (rtlhal->last_hmeboxnum == 4)
			rtlhal->last_hmeboxnum = 0;

		RT_TRACE(COMP_CMD, DBG_LOUD ,
			 ("pHalData->last_hmeboxnum  = %d\n",
			  rtlhal->last_hmeboxnum));
	}

	spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
	rtlhal->b_h2c_setinprogress = false;
	spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);

	RT_TRACE(COMP_CMD, DBG_LOUD , ("go out\n"));
}

void rtl92ee_fill_h2c_cmd(struct ieee80211_hw *hw,
			 u8 element_id, u32 cmd_len, u8 *p_cmdbuffer)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 tmp_cmdbuf[2];

	if (rtlhal->bfw_ready == false) {
		RT_ASSERT(false, ("return H2C cmd because of Fw "
				  "download fail!!!\n"));
		return;
	}

	memset(tmp_cmdbuf, 0, 8);
	memcpy(tmp_cmdbuf, p_cmdbuffer, cmd_len);
	_rtl92ee_fill_h2c_command(hw, element_id, cmd_len, (u8 *)&tmp_cmdbuf);

	return;
}

void rtl92ee_firmware_selfreset(struct ieee80211_hw *hw)
{
	u8 u1b_tmp;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp & (~BIT(0))));

	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, (u1b_tmp & (~BIT(2))));

	udelay(50);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_RSV_CTRL + 1);
	rtl_write_byte(rtlpriv, REG_RSV_CTRL + 1, (u1b_tmp | BIT(0)));

	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1, (u1b_tmp | BIT(2)));

	RT_TRACE(COMP_INIT, DBG_LOUD ,
		 ("  _8051Reset92E(): 8051 reset success .\n"));
}

void rtl92ee_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1_h2c_set_pwrmode[H2C_92E_PWEMODE_LENGTH] = { 0 };
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u8 rlbm , power_state = 0;
	RT_TRACE(COMP_POWER, DBG_LOUD , ("FW LPS mode = %d\n", mode));

	SET_H2CCMD_PWRMODE_PARM_MODE(u1_h2c_set_pwrmode, ((mode) ? 1 : 0));
	rlbm = 0;/*YJ, temp, 120316. FW now not support RLBM = 2.*/
	SET_H2CCMD_PWRMODE_PARM_RLBM(u1_h2c_set_pwrmode, rlbm);
	SET_H2CCMD_PWRMODE_PARM_SMART_PS(u1_h2c_set_pwrmode,
					 (rtlpriv->mac80211.p2p) ?
					 ppsc->smart_ps : 1);
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
		      u1_h2c_set_pwrmode, H2C_92E_PWEMODE_LENGTH);
	rtl92ee_fill_h2c_cmd(hw, H2C_92E_SETPWRMODE, H2C_92E_PWEMODE_LENGTH,
			     u1_h2c_set_pwrmode);

}

void rtl92ee_set_fw_media_status_rpt_cmd(struct ieee80211_hw *hw, u8 mstatus)
{
	u8 parm[3] = { 0 , 0 , 0 };
	/* parm[0]: bit0 = 0-->Disconnect, bit0 = 1-->Connect
	 *          bit1 = 0-->update Media Status to MACID
	 *          bit1 = 1-->update Media Status from MACID to MACID_End
	 * parm[1]: MACID, if this is INFRA_STA, MacID = 0
	 * parm[2]: MACID_End*/

	SET_H2CCMD_MSRRPT_PARM_OPMODE(parm, mstatus);
	SET_H2CCMD_MSRRPT_PARM_MACID_IND(parm, 0);

	rtl92ee_fill_h2c_cmd(hw, H2C_92E_MSRRPT, 3, parm);
}

static bool _rtl92ee_cmd_send_packet(struct ieee80211_hw *hw,
				     struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	unsigned long flags;
	struct sk_buff *pskb = NULL;

	ring = &rtlpci->tx_ring[BEACON_QUEUE];

	pskb = __skb_dequeue(&ring->queue);
	if (pskb)
		kfree_skb(pskb);

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	/*this is wrong, fill_tx_cmddesc needs update*/
	pdesc = &ring->desc[0];

	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *) pdesc, 1, 1, skb);

	__skb_queue_tail(&ring->queue, skb);

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	return true;
}

#define BEACON_PG		0 /* ->1 */
#define PSPOLL_PG		2
#define NULL_PG			3
#define PROBERSP_PG		4 /* ->5 */

#define TOTAL_RESERVED_PKT_LEN	768



static u8 reserved_page_packet[TOTAL_RESERVED_PKT_LEN] = {
	/* page 0 beacon */
	0x80, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0x00, 0xE0, 0x4C, 0x02, 0xB1, 0x78,
	0xEC, 0x1A, 0x59, 0x0B, 0xAD, 0xD4, 0x20, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x64, 0x00, 0x10, 0x04, 0x00, 0x05, 0x54, 0x65,
	0x73, 0x74, 0x32, 0x01, 0x08, 0x82, 0x84, 0x0B,
	0x16, 0x24, 0x30, 0x48, 0x6C, 0x03, 0x01, 0x06,
	0x06, 0x02, 0x00, 0x00, 0x2A, 0x01, 0x02, 0x32,
	0x04, 0x0C, 0x12, 0x18, 0x60, 0x2D, 0x1A, 0x6C,
	0x09, 0x03, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x3D, 0x00, 0xDD, 0x07, 0x00, 0xE0, 0x4C,
	0x02, 0x02, 0x00, 0x00, 0xDD, 0x18, 0x00, 0x50,
	0xF2, 0x01, 0x01, 0x00, 0x00, 0x50, 0xF2, 0x04,
	0x01, 0x00, 0x00, 0x50, 0xF2, 0x04, 0x01, 0x00,

	/* page 1 beacon */
	0x00, 0x50, 0xF2, 0x02, 0x00, 0x00, 0x00, 0x00,
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
	0x10, 0x00, 0x28, 0x8C, 0x00, 0x12, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 2  ps-poll */
	0xA4, 0x10, 0x01, 0xC0, 0xEC, 0x1A, 0x59, 0x0B,
	0xAD, 0xD4, 0x00, 0xE0, 0x4C, 0x02, 0xB1, 0x78,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x18, 0x00, 0x28, 0x8C, 0x00, 0x12, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* page 3  null */
	0x48, 0x01, 0x00, 0x00, 0xEC, 0x1A, 0x59, 0x0B,
	0xAD, 0xD4, 0x00, 0xE0, 0x4C, 0x02, 0xB1, 0x78,
	0xEC, 0x1A, 0x59, 0x0B, 0xAD, 0xD4, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x72, 0x00, 0x28, 0x8C, 0x00, 0x12, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

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



void rtl92ee_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct sk_buff *skb = NULL;

	u32 totalpacketlen;
	bool rtstatus;
	u8 u1RsvdPageLoc[5] = { 0 };
	bool b_dlok = false;

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

	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD ,
		      "rtl92ee_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      &reserved_page_packet[0], totalpacketlen);
	RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD ,
		      "rtl92ee_set_fw_rsvdpagepkt(): HW_VAR_SET_TX_CMD: ALL\n",
		      u1RsvdPageLoc, 3);


	skb = dev_alloc_skb(totalpacketlen);
	memcpy((u8 *) skb_put(skb, totalpacketlen),
	       &reserved_page_packet, totalpacketlen);

	rtstatus = _rtl92ee_cmd_send_packet(hw, skb);

	if (rtstatus)
		b_dlok = true;

	if (b_dlok) {
		RT_TRACE(COMP_POWER, DBG_LOUD ,
			 ("Set RSVD page location to Fw.\n"));
		RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_LOUD ,
			      "H2C_RSVDPAGE:\n", u1RsvdPageLoc, 3);
		rtl92ee_fill_h2c_cmd(hw, H2C_92E_RSVDPAGE,
				     sizeof(u1RsvdPageLoc), u1RsvdPageLoc);
	} else
		RT_TRACE(COMP_ERR, DBG_WARNING,
			 ("Set RSVD page location to Fw FAIL!!!!!!.\n"));
}

/*Shoud check FW support p2p or not.*/
static void rtl92ee_set_p2p_ctw_period_cmd(struct ieee80211_hw *hw, u8 ctwindow)
{
	u8 u1_ctwindow_period[1] = {ctwindow};

	rtl92ee_fill_h2c_cmd(hw, H2C_92E_P2P_PS_CTW_CMD, 1, u1_ctwindow_period);

}

void rtl92ee_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *rtlps = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_p2p_ps_info *p2pinfo = &(rtlps->p2p_ps_info);
	struct p2p_ps_offload_t *p2p_ps_offload = &rtlhal->p2p_ps_offload;
	u8 i;
	u16 ctwindow;
	u32 start_time, tsf_low;

	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		RT_TRACE(COMP_FW, DBG_LOUD , ("P2P_PS_DISABLE\n"));
		memset(p2p_ps_offload, 0, 1);
		break;
	case P2P_PS_ENABLE:
		RT_TRACE(COMP_FW, DBG_LOUD , ("P2P_PS_ENABLE\n"));
		/* update CTWindow value. */
		if (p2pinfo->ctwindow > 0) {
			p2p_ps_offload->CTWindow_En = 1;
			ctwindow = p2pinfo->ctwindow;
			rtl92ee_set_p2p_ctw_period_cmd(hw, ctwindow);
		}
		/* hw only support 2 set of NoA */
		for (i = 0 ; i < p2pinfo->noa_num ; i++) {
			/* To control the register setting for which NOA*/
			rtl_write_byte(rtlpriv, 0x5cf, (i << 4));
			if (i == 0)
				p2p_ps_offload->NoA0_En = 1;
			else
				p2p_ps_offload->NoA1_En = 1;
			/* config P2P NoA Descriptor Register */
			rtl_write_dword(rtlpriv, 0x5E0,
					p2pinfo->noa_duration[i]);
			rtl_write_dword(rtlpriv, 0x5E4,
					p2pinfo->noa_interval[i]);

			/*Get Current TSF value */
			tsf_low = rtl_read_dword(rtlpriv, REG_TSFTR);

			start_time = p2pinfo->noa_start_time[i];
			if (p2pinfo->noa_count_type[i] != 1) {
				while (start_time <= (tsf_low + (50 * 1024))) {
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
			p2p_ps_offload->Offload_En = 1;

			if (P2P_ROLE_GO == rtlpriv->mac80211.p2p) {
				p2p_ps_offload->role = 1;
				p2p_ps_offload->AllStaSleep = 0;
			} else {
				p2p_ps_offload->role = 0;
			}
			p2p_ps_offload->discovery = 0;
		}
		break;
	case P2P_PS_SCAN:
		RT_TRACE(COMP_FW, DBG_LOUD , ("P2P_PS_SCAN\n"));
		p2p_ps_offload->discovery = 1;
		break;
	case P2P_PS_SCAN_DONE:
		RT_TRACE(COMP_FW, DBG_LOUD , ("P2P_PS_SCAN_DONE\n"));
		p2p_ps_offload->discovery = 0;
		p2pinfo->p2p_ps_state = P2P_PS_ENABLE;
		break;
	default:
		break;
	}

	rtl92ee_fill_h2c_cmd(hw, H2C_92E_P2P_PS_OFFLOAD, 1,
			     (u8 *)p2p_ps_offload);

}

static void _rtl92ee_c2h_ra_report_handler(struct ieee80211_hw *hw,
					   u8 *cmd_buf, u8 cmd_len)
{
	u8 rate = cmd_buf[0] & 0x3F;
	bool collision_state = cmd_buf[3] & BIT(0);

	rtl92ee_dm_dynamic_arfb_select(hw, rate, collision_state);
}

static void _rtl92ee_c2h_content_parsing(struct ieee80211_hw *hw, u8 c2h_cmd_id,
					 u8 c2h_cmd_len, u8 *tmp_buf)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	switch (c2h_cmd_id) {
	case C2H_8192E_DBG:
		RT_TRACE(COMP_FW, DBG_TRACE , ("[C2H], C2H_8723BE_DBG!!\n"));
		break;
	case C2H_8192E_TXBF:
		RT_TRACE(COMP_FW, DBG_TRACE , ("[C2H], C2H_8192E_TXBF!!\n"));
		break;
	case C2H_8192E_TX_REPORT:
		RT_TRACE(COMP_FW, DBG_TRACE , ("[C2H], C2H_8723BE_TX_REPORT!\n"));
		break;
	case C2H_8192E_BT_INFO:
		RT_TRACE(COMP_FW, DBG_TRACE , ("[C2H], C2H_8723BE_BT_INFO!!\n"));
		rtlpriv->btcoexist.btc_ops->btc_btinfo_notify(rtlpriv, tmp_buf,
							      c2h_cmd_len);
		break;
	case C2H_8192E_BT_MP:
		RT_TRACE(COMP_FW, DBG_TRACE, ("[C2H], C2H_8723BE_BT_MP!!\n"));
		break;
	case C2H_8192E_RA_RPT:
		_rtl92ee_c2h_ra_report_handler(hw, tmp_buf, c2h_cmd_len);
		break;
	default:
		RT_TRACE(COMP_FW, DBG_TRACE,
			 ("[C2H], Unkown packet!! CmdId(%#X)!\n", c2h_cmd_id));
		break;
	}
}

void rtl92ee_c2h_packet_handler(struct ieee80211_hw *hw, u8 *buffer, u8 len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 c2h_cmd_id = 0, c2h_cmd_seq = 0, c2h_cmd_len = 0;
	u8 *tmp_buf = NULL;

	c2h_cmd_id = buffer[0];
	c2h_cmd_seq = buffer[1];
	c2h_cmd_len = len - 2;
	tmp_buf = buffer + 2;

	RT_TRACE(COMP_FW, DBG_TRACE,
		("[C2H packet], c2hCmdId = 0x%x, c2hCmdSeq = 0x%x, c2hCmdLen =%d\n",
		c2h_cmd_id, c2h_cmd_seq, c2h_cmd_len));

	RT_PRINT_DATA(rtlpriv, COMP_FW, DBG_TRACE,
		      "[C2H packet], Content Hex:\n", tmp_buf, c2h_cmd_len);

	_rtl92ee_c2h_content_parsing(hw, c2h_cmd_id, c2h_cmd_len, tmp_buf);
}
