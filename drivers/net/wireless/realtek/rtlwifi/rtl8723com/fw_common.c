/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
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
#include "../efuse.h"
#include "fw_common.h"
#include <linux/module.h>

void rtl8723_enable_fw_download(struct ieee80211_hw *hw, bool enable)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
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
EXPORT_SYMBOL_GPL(rtl8723_enable_fw_download);

void rtl8723_write_fw(struct ieee80211_hw *hw,
		      enum version_8723e version,
		      u8 *buffer, u32 size, u8 max_page)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 *bufferptr = buffer;
	u32 page_nums, remain_size;
	u32 page, offset;

	RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE, "FW size is %d bytes,\n", size);

	rtl_fill_dummy(bufferptr, &size);

	page_nums = size / FW_8192C_PAGE_SIZE;
	remain_size = size % FW_8192C_PAGE_SIZE;

	if (page_nums > max_page) {
		pr_err("Page numbers should not greater than %d\n",
		       max_page);
	}
	for (page = 0; page < page_nums; page++) {
		offset = page * FW_8192C_PAGE_SIZE;
		rtl_fw_page_write(hw, page, (bufferptr + offset),
				  FW_8192C_PAGE_SIZE);
	}

	if (remain_size) {
		offset = page_nums * FW_8192C_PAGE_SIZE;
		page = page_nums;
		rtl_fw_page_write(hw, page, (bufferptr + offset), remain_size);
	}
	RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE, "FW write done.\n");
}
EXPORT_SYMBOL_GPL(rtl8723_write_fw);

void rtl8723ae_firmware_selfreset(struct ieee80211_hw *hw)
{
	u8 u1b_tmp;
	u8 delay = 100;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, REG_HMETFR + 3, 0x20);
	u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);

	while (u1b_tmp & BIT(2)) {
		delay--;
		if (delay == 0)
			break;
		udelay(50);
		u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
	}
	if (delay == 0) {
		u1b_tmp = rtl_read_byte(rtlpriv, REG_SYS_FUNC_EN + 1);
		rtl_write_byte(rtlpriv, REG_SYS_FUNC_EN + 1,
			       u1b_tmp&(~BIT(2)));
	}
}
EXPORT_SYMBOL_GPL(rtl8723ae_firmware_selfreset);

void rtl8723be_firmware_selfreset(struct ieee80211_hw *hw)
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

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "  _8051Reset8723be(): 8051 reset success .\n");
}
EXPORT_SYMBOL_GPL(rtl8723be_firmware_selfreset);

int rtl8723_fw_free_to_go(struct ieee80211_hw *hw, bool is_8723be,
			  int max_count)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err = -EIO;
	u32 counter = 0;
	u32 value32;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
	} while ((counter++ < max_count) &&
		 (!(value32 & FWDL_CHKSUM_RPT)));

	if (counter >= max_count) {
		pr_err("chksum report fail ! REG_MCUFWDL:0x%08x .\n",
		       value32);
		goto exit;
	}
	value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL) | MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtl_write_dword(rtlpriv, REG_MCUFWDL, value32);

	if (is_8723be)
		rtl8723be_firmware_selfreset(hw);
	counter = 0;

	do {
		value32 = rtl_read_dword(rtlpriv, REG_MCUFWDL);
		if (value32 & WINTINI_RDY) {
			RT_TRACE(rtlpriv, COMP_FW, DBG_TRACE,
				 "Polling FW ready success!! REG_MCUFWDL:0x%08x .\n",
				 value32);
			err = 0;
			goto exit;
		}

		mdelay(FW_8192C_POLLING_DELAY);

	} while (counter++ < max_count);

	pr_err("Polling FW ready fail!! REG_MCUFWDL:0x%08x .\n",
	       value32);

exit:
	return err;
}
EXPORT_SYMBOL_GPL(rtl8723_fw_free_to_go);

int rtl8723_download_fw(struct ieee80211_hw *hw,
			bool is_8723be, int max_count)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtlwifi_firmware_header *pfwheader;
	u8 *pfwdata;
	u32 fwsize;
	int err;
	enum version_8723e version = rtlhal->version;
	int max_page;

	if (!rtlhal->pfirmware)
		return 1;

	pfwheader = (struct rtlwifi_firmware_header *)rtlhal->pfirmware;
	pfwdata = rtlhal->pfirmware;
	fwsize = rtlhal->fwsize;

	if (!is_8723be)
		max_page = 6;
	else
		max_page = 8;
	if (rtlpriv->cfg->ops->is_fw_header(pfwheader)) {
		RT_TRACE(rtlpriv, COMP_FW, DBG_LOUD,
			 "Firmware Version(%d), Signature(%#x), Size(%d)\n",
			 pfwheader->version, pfwheader->signature,
			 (int)sizeof(struct rtlwifi_firmware_header));

		pfwdata = pfwdata + sizeof(struct rtlwifi_firmware_header);
		fwsize = fwsize - sizeof(struct rtlwifi_firmware_header);
	}

	if (rtl_read_byte(rtlpriv, REG_MCUFWDL)&BIT(7)) {
		if (is_8723be)
			rtl8723be_firmware_selfreset(hw);
		else
			rtl8723ae_firmware_selfreset(hw);
		rtl_write_byte(rtlpriv, REG_MCUFWDL, 0x00);
	}
	rtl8723_enable_fw_download(hw, true);
	rtl8723_write_fw(hw, version, pfwdata, fwsize, max_page);
	rtl8723_enable_fw_download(hw, false);

	err = rtl8723_fw_free_to_go(hw, is_8723be, max_count);
	if (err)
		pr_err("Firmware is not ready to run!\n");
	return 0;
}
EXPORT_SYMBOL_GPL(rtl8723_download_fw);

bool rtl8723_cmd_send_packet(struct ieee80211_hw *hw,
			     struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	struct sk_buff *pskb = NULL;
	u8 own;
	unsigned long flags;

	ring = &rtlpci->tx_ring[BEACON_QUEUE];

	pskb = __skb_dequeue(&ring->queue);
	kfree_skb(pskb);
	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	pdesc = &ring->desc[0];
	own = (u8) rtlpriv->cfg->ops->get_desc((u8 *)pdesc, true, HW_DESC_OWN);

	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *)pdesc, 1, 1, skb);

	__skb_queue_tail(&ring->queue, skb);

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	rtlpriv->cfg->ops->tx_polling(hw, BEACON_QUEUE);

	return true;
}
EXPORT_SYMBOL_GPL(rtl8723_cmd_send_packet);
