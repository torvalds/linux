// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../efuse.h"
#include "def.h"
#include "reg.h"
#include "fw_common.h"

bool rtl92d_is_fw_downloaded(struct rtl_priv *rtlpriv)
{
	return !!(rtl_read_dword(rtlpriv, REG_MCUFWDL) & MCUFWDL_RDY);
}
EXPORT_SYMBOL_GPL(rtl92d_is_fw_downloaded);

void rtl92d_enable_fw_download(struct ieee80211_hw *hw, bool enable)
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
		 * rtl_write_byte(rtlpriv, REG_MCUFWDL + 1, 0x00);
		 */
	}
}
EXPORT_SYMBOL_GPL(rtl92d_enable_fw_download);

void rtl92d_write_fw(struct ieee80211_hw *hw,
		     enum version_8192d version, u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 *bufferptr = buffer;
	u32 pagenums, remainsize;
	u32 page, offset;

	rtl_dbg(rtlpriv, COMP_FW, DBG_TRACE, "FW size is %d bytes,\n", size);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192DE)
		rtl_fill_dummy(bufferptr, &size);

	pagenums = size / FW_8192D_PAGE_SIZE;
	remainsize = size % FW_8192D_PAGE_SIZE;

	if (pagenums > 8)
		pr_err("Page numbers should not greater then 8\n");

	for (page = 0; page < pagenums; page++) {
		offset = page * FW_8192D_PAGE_SIZE;
		rtl_fw_page_write(hw, page, (bufferptr + offset),
				  FW_8192D_PAGE_SIZE);
	}

	if (remainsize) {
		offset = pagenums * FW_8192D_PAGE_SIZE;
		page = pagenums;
		rtl_fw_page_write(hw, page, (bufferptr + offset), remainsize);
	}
}
EXPORT_SYMBOL_GPL(rtl92d_write_fw);

int rtl92d_fw_free_to_go(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 counter = 0;
	u32 value32;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	} while ((counter++ < FW_8192D_POLLING_TIMEOUT_COUNT) &&
		 (!(value32 & FWDL_CHKSUM_RPT)));

	if (counter >= FW_8192D_POLLING_TIMEOUT_COUNT) {
		pr_err("chksum report fail! REG_MCUFWDL:0x%08x\n",
		       value32);
		return -EIO;
	}

	value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);

	return 0;
}
EXPORT_SYMBOL_GPL(rtl92d_fw_free_to_go);

#define RTL_USB_DELAY_FACTOR		60

void rtl92d_firmware_selfreset(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 u1b_tmp;
	u8 delay = 100;

	if (rtlhal->interface == INTF_USB) {
		delay *= RTL_USB_DELAY_FACTOR;

		rtl_write_byte(rtlpriv, REG_FSIMR, 0);

		/* We need to disable other HRCV INT to influence 8051 reset. */
		rtl_write_byte(rtlpriv, REG_FWIMR, 0x20);

		/* Close mask to prevent incorrect FW write operation. */
		rtl_write_byte(rtlpriv, REG_FTIMR, 0);
	}

	/* Set (REG_HMETFR + 3) to  0x20 is reset 8051 */
	rtl_write_byte(rtlpriv, REG_HMETFR + 3, 0x20);

	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);

	while (u1b_tmp & (FEN_CPUEN >> 8)) {
		delay--;
		if (delay == 0)
			break;
		udelay(50);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	}

	if (rtlhal->interface == INTF_USB) {
		if ((u1b_tmp & (FEN_CPUEN >> 8)) && delay == 0)
			rtl_write_byte(rtlpriv, REG_FWIMR, 0);
	}

	WARN_ONCE((delay <= 0), "rtl8192de: 8051 reset failed!\n");
	rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG,
		"=====> 8051 reset success (%d)\n", delay);
}
EXPORT_SYMBOL_GPL(rtl92d_firmware_selfreset);

int rtl92d_fw_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u32 counter;

	rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG, "FW already have download\n");
	/* polling for FW ready */
	counter = 0;
	do {
		if (rtlhal->interfaceindex == 0) {
			if (rtl_read_byte(rtlpriv, FW_MAC0_READY) &
			    MAC0_READY) {
				rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG,
					"Polling FW ready success!! REG_MCUFWDL: 0x%x\n",
					rtl_read_byte(rtlpriv,
						      FW_MAC0_READY));
				return 0;
			}
			udelay(5);
		} else {
			if (rtl_read_byte(rtlpriv, FW_MAC1_READY) &
			    MAC1_READY) {
				rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG,
					"Polling FW ready success!! REG_MCUFWDL: 0x%x\n",
					rtl_read_byte(rtlpriv,
						      FW_MAC1_READY));
				return 0;
			}
			udelay(5);
		}
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (rtlhal->interfaceindex == 0) {
		rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG,
			"Polling FW ready fail!! MAC0 FW init not ready: 0x%x\n",
			rtl_read_byte(rtlpriv, FW_MAC0_READY));
	} else {
		rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG,
			"Polling FW ready fail!! MAC1 FW init not ready: 0x%x\n",
			rtl_read_byte(rtlpriv, FW_MAC1_READY));
	}
	rtl_dbg(rtlpriv, COMP_FW, DBG_DMESG,
		"Polling FW ready fail!! REG_MCUFWDL:0x%08x\n",
		rtl_read_dword(rtlpriv, REG_MCUFWDL));
	return -1;
}
EXPORT_SYMBOL_GPL(rtl92d_fw_init);

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

void rtl92d_fill_h2c_cmd(struct ieee80211_hw *hw,
			 u8 element_id, u32 cmd_len, u8 *cmdbuffer)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 boxcontent[4], boxextcontent[2];
	u16 box_reg = 0, box_extreg = 0;
	u8 wait_writeh2c_limmit = 100;
	bool bwrite_success = false;
	u8 wait_h2c_limmit = 100;
	u32 h2c_waitcounter = 0;
	bool isfw_read = false;
	unsigned long flag;
	u8 u1b_tmp;
	u8 boxnum;
	u8 idx;

	if (ppsc->rfpwr_state == ERFOFF || ppsc->inactive_pwrstate == ERFOFF) {
		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
			"Return as RF is off!!!\n");
		return;
	}

	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "come in\n");

	while (true) {
		spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
		if (rtlhal->h2c_setinprogress) {
			rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
				"H2C set in progress! Wait to set..element_id(%d)\n",
				element_id);

			while (rtlhal->h2c_setinprogress) {
				spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock,
						       flag);
				h2c_waitcounter++;
				rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
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

	while (!bwrite_success) {
		wait_writeh2c_limmit--;
		if (wait_writeh2c_limmit == 0) {
			pr_err("Write H2C fail because no trigger for FW INT!\n");
			break;
		}

		boxnum = rtlhal->last_hmeboxnum;
		if (boxnum > 3) {
			pr_err("boxnum %#x too big\n", boxnum);
			break;
		}

		box_reg = REG_HMEBOX_0 + boxnum * SIZE_OF_REG_HMEBOX;
		box_extreg = REG_HMEBOX_EXT_0 + boxnum * SIZE_OF_REG_HMEBOX_EXT;

		isfw_read = _rtl92d_check_fw_read_last_h2c(hw, boxnum);
		while (!isfw_read) {
			wait_h2c_limmit--;
			if (wait_h2c_limmit == 0) {
				rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
					"Waiting too long for FW read clear HMEBox(%d)!\n",
					boxnum);
				break;
			}

			udelay(10);

			isfw_read = _rtl92d_check_fw_read_last_h2c(hw, boxnum);
			u1b_tmp = rtl_read_byte(rtlpriv, 0x1BF);
			rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
				"Waiting for FW read clear HMEBox(%d)!!! 0x1BF = %2x\n",
				boxnum, u1b_tmp);
		}

		if (!isfw_read) {
			rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
				"Write H2C register BOX[%d] fail!!!!! Fw do not read.\n",
				boxnum);
			break;
		}

		memset(boxcontent, 0, sizeof(boxcontent));
		memset(boxextcontent, 0, sizeof(boxextcontent));
		boxcontent[0] = element_id;

		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
			"Write element_id box_reg(%4x) = %2x\n",
			box_reg, element_id);

		switch (cmd_len) {
		case 1 ... 3:
			/* BOX:      | ID | A0 | A1 | A2 |
			 * BOX_EXT:  --- N/A ------
			 */
			boxcontent[0] &= ~BIT(7);
			memcpy(boxcontent + 1, cmdbuffer, cmd_len);

			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		case 4 ... 5:
			/* * ID ext = ID | BIT(7)
			 * BOX:      | ID ext | A2 | A3 | A4 |
			 * BOX_EXT:  | A0     | A1 |
			 */
			boxcontent[0] |= BIT(7);
			memcpy(boxextcontent, cmdbuffer, 2);
			memcpy(boxcontent + 1, cmdbuffer + 2, cmd_len - 2);

			for (idx = 0; idx < 2; idx++)
				rtl_write_byte(rtlpriv, box_extreg + idx,
					       boxextcontent[idx]);

			for (idx = 0; idx < 4; idx++)
				rtl_write_byte(rtlpriv, box_reg + idx,
					       boxcontent[idx]);
			break;
		default:
			pr_err("switch case %#x not processed\n", cmd_len);
			break;
		}

		bwrite_success = true;
		rtlhal->last_hmeboxnum = boxnum + 1;
		if (rtlhal->last_hmeboxnum == 4)
			rtlhal->last_hmeboxnum = 0;

		rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD,
			"pHalData->last_hmeboxnum  = %d\n",
			rtlhal->last_hmeboxnum);
	}
	spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flag);
	rtlhal->h2c_setinprogress = false;
	spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flag);
	rtl_dbg(rtlpriv, COMP_CMD, DBG_LOUD, "go out\n");
}
EXPORT_SYMBOL_GPL(rtl92d_fill_h2c_cmd);

void rtl92d_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus)
{
	u8 u1_joinbssrpt_parm[1] = {0};

	u1_joinbssrpt_parm[0] = mstatus;
	rtl92d_fill_h2c_cmd(hw, H2C_JOINBSSRPT, 1, u1_joinbssrpt_parm);
}
EXPORT_SYMBOL_GPL(rtl92d_set_fw_joinbss_report_cmd);
