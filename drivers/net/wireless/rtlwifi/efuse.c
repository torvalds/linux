/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * Tmis program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * Tmis program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * tmis program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Tme full GNU General Public License is included in this distribution in the
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

#include <linux/export.h>
#include "wifi.h"
#include "efuse.h"

static const u8 MAX_PGPKT_SIZE = 9;
static const u8 PGPKT_DATA_SIZE = 8;
static const int EFUSE_MAX_SIZE = 512;

static const struct efuse_map RTL8712_SDIO_EFUSE_TABLE[] = {
	{0, 0, 0, 2},
	{0, 1, 0, 2},
	{0, 2, 0, 2},
	{1, 0, 0, 1},
	{1, 0, 1, 1},
	{1, 1, 0, 1},
	{1, 1, 1, 3},
	{1, 3, 0, 17},
	{3, 3, 1, 48},
	{10, 0, 0, 6},
	{10, 3, 0, 1},
	{10, 3, 1, 1},
	{11, 0, 0, 28}
};

static void efuse_shadow_read_1byte(struct ieee80211_hw *hw, u16 offset,
				    u8 *value);
static void efuse_shadow_read_2byte(struct ieee80211_hw *hw, u16 offset,
				    u16 *value);
static void efuse_shadow_read_4byte(struct ieee80211_hw *hw, u16 offset,
				    u32 *value);
static void efuse_shadow_write_1byte(struct ieee80211_hw *hw, u16 offset,
				     u8 value);
static void efuse_shadow_write_2byte(struct ieee80211_hw *hw, u16 offset,
				     u16 value);
static void efuse_shadow_write_4byte(struct ieee80211_hw *hw, u16 offset,
				     u32 value);
static int efuse_one_byte_read(struct ieee80211_hw *hw, u16 addr,
					u8 *data);
static int efuse_one_byte_write(struct ieee80211_hw *hw, u16 addr,
					u8 data);
static void efuse_read_all_map(struct ieee80211_hw *hw, u8 *efuse);
static int efuse_pg_packet_read(struct ieee80211_hw *hw, u8 offset,
					u8 *data);
static int efuse_pg_packet_write(struct ieee80211_hw *hw, u8 offset,
				 u8 word_en, u8 *data);
static void efuse_word_enable_data_read(u8 word_en, u8 *sourdata,
					u8 *targetdata);
static u8 efuse_word_enable_data_write(struct ieee80211_hw *hw,
				       u16 efuse_addr, u8 word_en, u8 *data);
static void efuse_power_switch(struct ieee80211_hw *hw, u8 write,
					u8 pwrstate);
static u16 efuse_get_current_size(struct ieee80211_hw *hw);
static u8 efuse_calculate_word_cnts(u8 word_en);

void efuse_initialize(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bytetemp;
	u8 temp;

	bytetemp = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[SYS_FUNC_EN] + 1);
	temp = bytetemp | 0x20;
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[SYS_FUNC_EN] + 1, temp);

	bytetemp = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[SYS_ISO_CTRL] + 1);
	temp = bytetemp & 0xFE;
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[SYS_ISO_CTRL] + 1, temp);

	bytetemp = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_TEST] + 3);
	temp = bytetemp | 0x80;
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_TEST] + 3, temp);

	rtl_write_byte(rtlpriv, 0x2F8, 0x3);

	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 3, 0x72);

}

u8 efuse_read_1byte(struct ieee80211_hw *hw, u16 address)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 data;
	u8 bytetemp;
	u8 temp;
	u32 k = 0;
	const u32 efuse_len =
		rtlpriv->cfg->maps[EFUSE_REAL_CONTENT_SIZE];

	if (address < efuse_len) {
		temp = address & 0xFF;
		rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 1,
			       temp);
		bytetemp = rtl_read_byte(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL] + 2);
		temp = ((address >> 8) & 0x03) | (bytetemp & 0xFC);
		rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 2,
			       temp);

		bytetemp = rtl_read_byte(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL] + 3);
		temp = bytetemp & 0x7F;
		rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 3,
			       temp);

		bytetemp = rtl_read_byte(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL] + 3);
		while (!(bytetemp & 0x80)) {
			bytetemp = rtl_read_byte(rtlpriv,
						 rtlpriv->cfg->
						 maps[EFUSE_CTRL] + 3);
			k++;
			if (k == 1000) {
				k = 0;
				break;
			}
		}
		data = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL]);
		return data;
	} else
		return 0xFF;

}
EXPORT_SYMBOL(efuse_read_1byte);

void efuse_write_1byte(struct ieee80211_hw *hw, u16 address, u8 value)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 bytetemp;
	u8 temp;
	u32 k = 0;
	const u32 efuse_len =
		rtlpriv->cfg->maps[EFUSE_REAL_CONTENT_SIZE];

	RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD, "Addr=%x Data =%x\n",
		 address, value);

	if (address < efuse_len) {
		rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL], value);

		temp = address & 0xFF;
		rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 1,
			       temp);
		bytetemp = rtl_read_byte(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL] + 2);

		temp = ((address >> 8) & 0x03) | (bytetemp & 0xFC);
		rtl_write_byte(rtlpriv,
			       rtlpriv->cfg->maps[EFUSE_CTRL] + 2, temp);

		bytetemp = rtl_read_byte(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL] + 3);
		temp = bytetemp | 0x80;
		rtl_write_byte(rtlpriv,
			       rtlpriv->cfg->maps[EFUSE_CTRL] + 3, temp);

		bytetemp = rtl_read_byte(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL] + 3);

		while (bytetemp & 0x80) {
			bytetemp = rtl_read_byte(rtlpriv,
						 rtlpriv->cfg->
						 maps[EFUSE_CTRL] + 3);
			k++;
			if (k == 100) {
				k = 0;
				break;
			}
		}
	}

}

void read_efuse_byte(struct ieee80211_hw *hw, u16 _offset, u8 *pbuf)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 value32;
	u8 readbyte;
	u16 retry;

	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 1,
		       (_offset & 0xff));
	readbyte = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 2);
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 2,
		       ((_offset >> 8) & 0x03) | (readbyte & 0xfc));

	readbyte = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 3);
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 3,
		       (readbyte & 0x7f));

	retry = 0;
	value32 = rtl_read_dword(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL]);
	while (!(((value32 >> 24) & 0xff) & 0x80) && (retry < 10000)) {
		value32 = rtl_read_dword(rtlpriv,
					 rtlpriv->cfg->maps[EFUSE_CTRL]);
		retry++;
	}

	udelay(50);
	value32 = rtl_read_dword(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL]);

	*pbuf = (u8) (value32 & 0xff);
}
EXPORT_SYMBOL_GPL(read_efuse_byte);

void read_efuse(struct ieee80211_hw *hw, u16 _offset, u16 _size_byte, u8 *pbuf)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 *efuse_tbl;
	u8 rtemp8[1];
	u16 efuse_addr = 0;
	u8 offset, wren;
	u8 u1temp = 0;
	u16 i;
	u16 j;
	const u16 efuse_max_section =
		rtlpriv->cfg->maps[EFUSE_MAX_SECTION_MAP];
	const u32 efuse_len =
		rtlpriv->cfg->maps[EFUSE_REAL_CONTENT_SIZE];
	u16 **efuse_word;
	u16 efuse_utilized = 0;
	u8 efuse_usage;

	if ((_offset + _size_byte) > rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]) {
		RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD,
			 "read_efuse(): Invalid offset(%#x) with read bytes(%#x)!!\n",
			 _offset, _size_byte);
		return;
	}

	/* allocate memory for efuse_tbl and efuse_word */
	efuse_tbl = kmalloc(rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE] *
			    sizeof(u8), GFP_ATOMIC);
	if (!efuse_tbl)
		return;
	efuse_word = kmalloc(EFUSE_MAX_WORD_UNIT * sizeof(u16 *), GFP_ATOMIC);
	if (!efuse_word)
		goto done;
	for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
		efuse_word[i] = kmalloc(efuse_max_section * sizeof(u16),
					GFP_ATOMIC);
		if (!efuse_word[i])
			goto done;
	}

	for (i = 0; i < efuse_max_section; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			efuse_word[j][i] = 0xFFFF;

	read_efuse_byte(hw, efuse_addr, rtemp8);
	if (*rtemp8 != 0xFF) {
		efuse_utilized++;
		RTPRINT(rtlpriv, FEEPROM, EFUSE_READ_ALL,
			"Addr=%d\n", efuse_addr);
		efuse_addr++;
	}

	while ((*rtemp8 != 0xFF) && (efuse_addr < efuse_len)) {
		/*  Check PG header for section num.  */
		if ((*rtemp8 & 0x1F) == 0x0F) {/* extended header */
			u1temp = ((*rtemp8 & 0xE0) >> 5);
			read_efuse_byte(hw, efuse_addr, rtemp8);

			if ((*rtemp8 & 0x0F) == 0x0F) {
				efuse_addr++;
				read_efuse_byte(hw, efuse_addr, rtemp8);

				if (*rtemp8 != 0xFF &&
				    (efuse_addr < efuse_len)) {
					efuse_addr++;
				}
				continue;
			} else {
				offset = ((*rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (*rtemp8 & 0x0F);
				efuse_addr++;
			}
		} else {
			offset = ((*rtemp8 >> 4) & 0x0f);
			wren = (*rtemp8 & 0x0f);
		}

		if (offset < efuse_max_section) {
			RTPRINT(rtlpriv, FEEPROM, EFUSE_READ_ALL,
				"offset-%d Worden=%x\n", offset, wren);

			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				if (!(wren & 0x01)) {
					RTPRINT(rtlpriv, FEEPROM,
						EFUSE_READ_ALL,
						"Addr=%d\n", efuse_addr);

					read_efuse_byte(hw, efuse_addr, rtemp8);
					efuse_addr++;
					efuse_utilized++;
					efuse_word[i][offset] =
							 (*rtemp8 & 0xff);

					if (efuse_addr >= efuse_len)
						break;

					RTPRINT(rtlpriv, FEEPROM,
						EFUSE_READ_ALL,
						"Addr=%d\n", efuse_addr);

					read_efuse_byte(hw, efuse_addr, rtemp8);
					efuse_addr++;
					efuse_utilized++;
					efuse_word[i][offset] |=
					    (((u16)*rtemp8 << 8) & 0xff00);

					if (efuse_addr >= efuse_len)
						break;
				}

				wren >>= 1;
			}
		}

		RTPRINT(rtlpriv, FEEPROM, EFUSE_READ_ALL,
			"Addr=%d\n", efuse_addr);
		read_efuse_byte(hw, efuse_addr, rtemp8);
		if (*rtemp8 != 0xFF && (efuse_addr < efuse_len)) {
			efuse_utilized++;
			efuse_addr++;
		}
	}

	for (i = 0; i < efuse_max_section; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuse_tbl[(i * 8) + (j * 2)] =
			    (efuse_word[j][i] & 0xff);
			efuse_tbl[(i * 8) + ((j * 2) + 1)] =
			    ((efuse_word[j][i] >> 8) & 0xff);
		}
	}

	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuse_tbl[_offset + i];

	rtlefuse->efuse_usedbytes = efuse_utilized;
	efuse_usage = (u8) ((efuse_utilized * 100) / efuse_len);
	rtlefuse->efuse_usedpercentage = efuse_usage;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_EFUSE_BYTES,
				      (u8 *)&efuse_utilized);
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_EFUSE_USAGE,
				      &efuse_usage);
done:
	for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++)
		kfree(efuse_word[i]);
	kfree(efuse_word);
	kfree(efuse_tbl);
}

bool efuse_shadow_update_chk(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 section_idx, i, Base;
	u16 words_need = 0, hdr_num = 0, totalbytes, efuse_used;
	bool wordchanged, result = true;

	for (section_idx = 0; section_idx < 16; section_idx++) {
		Base = section_idx * 8;
		wordchanged = false;

		for (i = 0; i < 8; i = i + 2) {
			if ((rtlefuse->efuse_map[EFUSE_INIT_MAP][Base + i] !=
			     rtlefuse->efuse_map[EFUSE_MODIFY_MAP][Base + i]) ||
			    (rtlefuse->efuse_map[EFUSE_INIT_MAP][Base + i + 1] !=
			     rtlefuse->efuse_map[EFUSE_MODIFY_MAP][Base + i +
								   1])) {
				words_need++;
				wordchanged = true;
			}
		}

		if (wordchanged)
			hdr_num++;
	}

	totalbytes = hdr_num + words_need * 2;
	efuse_used = rtlefuse->efuse_usedbytes;

	if ((totalbytes + efuse_used) >=
	    (EFUSE_MAX_SIZE -
	     rtlpriv->cfg->maps[EFUSE_OOB_PROTECT_BYTES_LEN]))
		result = false;

	RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD,
		 "efuse_shadow_update_chk(): totalbytes(%#x), hdr_num(%#x), words_need(%#x), efuse_used(%d)\n",
		 totalbytes, hdr_num, words_need, efuse_used);

	return result;
}

void efuse_shadow_read(struct ieee80211_hw *hw, u8 type,
		       u16 offset, u32 *value)
{
	if (type == 1)
		efuse_shadow_read_1byte(hw, offset, (u8 *) value);
	else if (type == 2)
		efuse_shadow_read_2byte(hw, offset, (u16 *) value);
	else if (type == 4)
		efuse_shadow_read_4byte(hw, offset, value);

}

void efuse_shadow_write(struct ieee80211_hw *hw, u8 type, u16 offset,
				u32 value)
{
	if (type == 1)
		efuse_shadow_write_1byte(hw, offset, (u8) value);
	else if (type == 2)
		efuse_shadow_write_2byte(hw, offset, (u16) value);
	else if (type == 4)
		efuse_shadow_write_4byte(hw, offset, value);

}

bool efuse_shadow_update(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u16 i, offset, base;
	u8 word_en = 0x0F;
	u8 first_pg = false;

	RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD, "--->\n");

	if (!efuse_shadow_update_chk(hw)) {
		efuse_read_all_map(hw, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0]);
		memcpy(&rtlefuse->efuse_map[EFUSE_MODIFY_MAP][0],
		       &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);

		RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD,
			 "<---efuse out of capacity!!\n");
		return false;
	}
	efuse_power_switch(hw, true, true);

	for (offset = 0; offset < 16; offset++) {

		word_en = 0x0F;
		base = offset * 8;

		for (i = 0; i < 8; i++) {
			if (first_pg) {

				word_en &= ~(BIT(i / 2));

				rtlefuse->efuse_map[EFUSE_INIT_MAP][base + i] =
				    rtlefuse->efuse_map[EFUSE_MODIFY_MAP][base + i];
			} else {

				if (rtlefuse->efuse_map[EFUSE_INIT_MAP][base + i] !=
				    rtlefuse->efuse_map[EFUSE_MODIFY_MAP][base + i]) {
					word_en &= ~(BIT(i / 2));

					rtlefuse->efuse_map[EFUSE_INIT_MAP][base + i] =
					    rtlefuse->efuse_map[EFUSE_MODIFY_MAP][base + i];
				}
			}
		}

		if (word_en != 0x0F) {
			u8 tmpdata[8];
			memcpy(tmpdata,
			       &rtlefuse->efuse_map[EFUSE_MODIFY_MAP][base],
			       8);
			RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_LOUD,
				      "U-efuse", tmpdata, 8);

			if (!efuse_pg_packet_write(hw, (u8) offset, word_en,
						   tmpdata)) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
					 "PG section(%#x) fail!!\n", offset);
				break;
			}
		}

	}

	efuse_power_switch(hw, true, false);
	efuse_read_all_map(hw, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0]);

	memcpy(&rtlefuse->efuse_map[EFUSE_MODIFY_MAP][0],
	       &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
	       rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);

	RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD, "<---\n");
	return true;
}

void rtl_efuse_shadow_map_update(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	if (rtlefuse->autoload_failflag)
		memset(&rtlefuse->efuse_map[EFUSE_INIT_MAP][0], 0xFF,
			rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);
	else
		efuse_read_all_map(hw, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0]);

	memcpy(&rtlefuse->efuse_map[EFUSE_MODIFY_MAP][0],
	       &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
	       rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);

}
EXPORT_SYMBOL(rtl_efuse_shadow_map_update);

void efuse_force_write_vendor_Id(struct ieee80211_hw *hw)
{
	u8 tmpdata[8] = { 0xFF, 0xFF, 0xEC, 0x10, 0xFF, 0xFF, 0xFF, 0xFF };

	efuse_power_switch(hw, true, true);

	efuse_pg_packet_write(hw, 1, 0xD, tmpdata);

	efuse_power_switch(hw, true, false);

}

void efuse_re_pg_section(struct ieee80211_hw *hw, u8 section_idx)
{
}

static void efuse_shadow_read_1byte(struct ieee80211_hw *hw,
				    u16 offset, u8 *value)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	*value = rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset];
}

static void efuse_shadow_read_2byte(struct ieee80211_hw *hw,
				    u16 offset, u16 *value)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	*value = rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset];
	*value |= rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 1] << 8;

}

static void efuse_shadow_read_4byte(struct ieee80211_hw *hw,
				    u16 offset, u32 *value)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	*value = rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset];
	*value |= rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 1] << 8;
	*value |= rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 2] << 16;
	*value |= rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 3] << 24;
}

static void efuse_shadow_write_1byte(struct ieee80211_hw *hw,
				     u16 offset, u8 value)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset] = value;
}

static void efuse_shadow_write_2byte(struct ieee80211_hw *hw,
				     u16 offset, u16 value)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset] = value & 0x00FF;
	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 1] = value >> 8;

}

static void efuse_shadow_write_4byte(struct ieee80211_hw *hw,
				     u16 offset, u32 value)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset] =
	    (u8) (value & 0x000000FF);
	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 1] =
	    (u8) ((value >> 8) & 0x0000FF);
	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 2] =
	    (u8) ((value >> 16) & 0x00FF);
	rtlefuse->efuse_map[EFUSE_MODIFY_MAP][offset + 3] =
	    (u8) ((value >> 24) & 0xFF);

}

static int efuse_one_byte_read(struct ieee80211_hw *hw, u16 addr, u8 *data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmpidx = 0;
	int result;

	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 1,
		       (u8) (addr & 0xff));
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 2,
		       ((u8) ((addr >> 8) & 0x03)) |
		       (rtl_read_byte(rtlpriv,
				      rtlpriv->cfg->maps[EFUSE_CTRL] + 2) &
			0xFC));

	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 3, 0x72);

	while (!(0x80 & rtl_read_byte(rtlpriv,
				      rtlpriv->cfg->maps[EFUSE_CTRL] + 3))
	       && (tmpidx < 100)) {
		tmpidx++;
	}

	if (tmpidx < 100) {
		*data = rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL]);
		result = true;
	} else {
		*data = 0xff;
		result = false;
	}
	return result;
}

static int efuse_one_byte_write(struct ieee80211_hw *hw, u16 addr, u8 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmpidx = 0;

	RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD, "Addr = %x Data=%x\n",
		 addr, data);

	rtl_write_byte(rtlpriv,
		       rtlpriv->cfg->maps[EFUSE_CTRL] + 1, (u8) (addr & 0xff));
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 2,
		       (rtl_read_byte(rtlpriv,
			 rtlpriv->cfg->maps[EFUSE_CTRL] +
			 2) & 0xFC) | (u8) ((addr >> 8) & 0x03));

	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL], data);
	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CTRL] + 3, 0xF2);

	while ((0x80 & rtl_read_byte(rtlpriv,
				     rtlpriv->cfg->maps[EFUSE_CTRL] + 3))
	       && (tmpidx < 100)) {
		tmpidx++;
	}

	if (tmpidx < 100)
		return true;

	return false;
}

static void efuse_read_all_map(struct ieee80211_hw *hw, u8 * efuse)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	efuse_power_switch(hw, false, true);
	read_efuse(hw, 0, rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE], efuse);
	efuse_power_switch(hw, false, false);
}

static void efuse_read_data_case1(struct ieee80211_hw *hw, u16 *efuse_addr,
				u8 efuse_data, u8 offset, u8 *tmpdata,
				u8 *readstate)
{
	bool dataempty = true;
	u8 hoffset;
	u8 tmpidx;
	u8 hworden;
	u8 word_cnts;

	hoffset = (efuse_data >> 4) & 0x0F;
	hworden = efuse_data & 0x0F;
	word_cnts = efuse_calculate_word_cnts(hworden);

	if (hoffset == offset) {
		for (tmpidx = 0; tmpidx < word_cnts * 2; tmpidx++) {
			if (efuse_one_byte_read(hw, *efuse_addr + 1 + tmpidx,
			    &efuse_data)) {
				tmpdata[tmpidx] = efuse_data;
				if (efuse_data != 0xff)
					dataempty = true;
			}
		}

		if (dataempty) {
			*readstate = PG_STATE_DATA;
		} else {
			*efuse_addr = *efuse_addr + (word_cnts * 2) + 1;
			*readstate = PG_STATE_HEADER;
		}

	} else {
		*efuse_addr = *efuse_addr + (word_cnts * 2) + 1;
		*readstate = PG_STATE_HEADER;
	}
}

static int efuse_pg_packet_read(struct ieee80211_hw *hw, u8 offset, u8 *data)
{
	u8 readstate = PG_STATE_HEADER;
	bool continual = true;
	u8 efuse_data, word_cnts = 0;
	u16 efuse_addr = 0;
	u8 tmpdata[8];

	if (data == NULL)
		return false;
	if (offset > 15)
		return false;

	memset(data, 0xff, PGPKT_DATA_SIZE * sizeof(u8));
	memset(tmpdata, 0xff, PGPKT_DATA_SIZE * sizeof(u8));

	while (continual && (efuse_addr < EFUSE_MAX_SIZE)) {
		if (readstate & PG_STATE_HEADER) {
			if (efuse_one_byte_read(hw, efuse_addr, &efuse_data)
			    && (efuse_data != 0xFF))
				efuse_read_data_case1(hw, &efuse_addr,
						      efuse_data,
						      offset, tmpdata,
						      &readstate);
			else
				continual = false;
		} else if (readstate & PG_STATE_DATA) {
			efuse_word_enable_data_read(0, tmpdata, data);
			efuse_addr = efuse_addr + (word_cnts * 2) + 1;
			readstate = PG_STATE_HEADER;
		}

	}

	if ((data[0] == 0xff) && (data[1] == 0xff) &&
	    (data[2] == 0xff) && (data[3] == 0xff) &&
	    (data[4] == 0xff) && (data[5] == 0xff) &&
	    (data[6] == 0xff) && (data[7] == 0xff))
		return false;
	else
		return true;

}

static void efuse_write_data_case1(struct ieee80211_hw *hw, u16 *efuse_addr,
			u8 efuse_data, u8 offset, int *continual,
			u8 *write_state, struct pgpkt_struct *target_pkt,
			int *repeat_times, int *result, u8 word_en)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct pgpkt_struct tmp_pkt;
	bool dataempty = true;
	u8 originaldata[8 * sizeof(u8)];
	u8 badworden = 0x0F;
	u8 match_word_en, tmp_word_en;
	u8 tmpindex;
	u8 tmp_header = efuse_data;
	u8 tmp_word_cnts;

	tmp_pkt.offset = (tmp_header >> 4) & 0x0F;
	tmp_pkt.word_en = tmp_header & 0x0F;
	tmp_word_cnts = efuse_calculate_word_cnts(tmp_pkt.word_en);

	if (tmp_pkt.offset != target_pkt->offset) {
		*efuse_addr = *efuse_addr + (tmp_word_cnts * 2) + 1;
		*write_state = PG_STATE_HEADER;
	} else {
		for (tmpindex = 0; tmpindex < (tmp_word_cnts * 2); tmpindex++) {
			u16 address = *efuse_addr + 1 + tmpindex;
			if (efuse_one_byte_read(hw, address,
			     &efuse_data) && (efuse_data != 0xFF))
				dataempty = false;
		}

		if (!dataempty) {
			*efuse_addr = *efuse_addr + (tmp_word_cnts * 2) + 1;
			*write_state = PG_STATE_HEADER;
		} else {
			match_word_en = 0x0F;
			if (!((target_pkt->word_en & BIT(0)) |
			     (tmp_pkt.word_en & BIT(0))))
				match_word_en &= (~BIT(0));

			if (!((target_pkt->word_en & BIT(1)) |
			     (tmp_pkt.word_en & BIT(1))))
				match_word_en &= (~BIT(1));

			if (!((target_pkt->word_en & BIT(2)) |
			     (tmp_pkt.word_en & BIT(2))))
				match_word_en &= (~BIT(2));

			if (!((target_pkt->word_en & BIT(3)) |
			     (tmp_pkt.word_en & BIT(3))))
				match_word_en &= (~BIT(3));

			if ((match_word_en & 0x0F) != 0x0F) {
				badworden = efuse_word_enable_data_write(
							    hw, *efuse_addr + 1,
							    tmp_pkt.word_en,
							    target_pkt->data);

				if (0x0F != (badworden & 0x0F)) {
					u8 reorg_offset = offset;
					u8 reorg_worden = badworden;
					efuse_pg_packet_write(hw, reorg_offset,
							       reorg_worden,
							       originaldata);
				}

				tmp_word_en = 0x0F;
				if ((target_pkt->word_en & BIT(0)) ^
				    (match_word_en & BIT(0)))
					tmp_word_en &= (~BIT(0));

				if ((target_pkt->word_en & BIT(1)) ^
				    (match_word_en & BIT(1)))
					tmp_word_en &= (~BIT(1));

				if ((target_pkt->word_en & BIT(2)) ^
				     (match_word_en & BIT(2)))
					tmp_word_en &= (~BIT(2));

				if ((target_pkt->word_en & BIT(3)) ^
				     (match_word_en & BIT(3)))
					tmp_word_en &= (~BIT(3));

				if ((tmp_word_en & 0x0F) != 0x0F) {
					*efuse_addr = efuse_get_current_size(hw);
					target_pkt->offset = offset;
					target_pkt->word_en = tmp_word_en;
				} else {
					*continual = false;
				}
				*write_state = PG_STATE_HEADER;
				*repeat_times += 1;
				if (*repeat_times > EFUSE_REPEAT_THRESHOLD_) {
					*continual = false;
					*result = false;
				}
			} else {
				*efuse_addr += (2 * tmp_word_cnts) + 1;
				target_pkt->offset = offset;
				target_pkt->word_en = word_en;
				*write_state = PG_STATE_HEADER;
			}
		}
	}
	RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,  "efuse PG_STATE_HEADER-1\n");
}

static void efuse_write_data_case2(struct ieee80211_hw *hw, u16 *efuse_addr,
				   int *continual, u8 *write_state,
				   struct pgpkt_struct target_pkt,
				   int *repeat_times, int *result)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct pgpkt_struct tmp_pkt;
	u8 pg_header;
	u8 tmp_header;
	u8 originaldata[8 * sizeof(u8)];
	u8 tmp_word_cnts;
	u8 badworden = 0x0F;

	pg_header = ((target_pkt.offset << 4) & 0xf0) | target_pkt.word_en;
	efuse_one_byte_write(hw, *efuse_addr, pg_header);
	efuse_one_byte_read(hw, *efuse_addr, &tmp_header);

	if (tmp_header == pg_header) {
		*write_state = PG_STATE_DATA;
	} else if (tmp_header == 0xFF) {
		*write_state = PG_STATE_HEADER;
		*repeat_times += 1;
		if (*repeat_times > EFUSE_REPEAT_THRESHOLD_) {
			*continual = false;
			*result = false;
		}
	} else {
		tmp_pkt.offset = (tmp_header >> 4) & 0x0F;
		tmp_pkt.word_en = tmp_header & 0x0F;

		tmp_word_cnts = efuse_calculate_word_cnts(tmp_pkt.word_en);

		memset(originaldata, 0xff, 8 * sizeof(u8));

		if (efuse_pg_packet_read(hw, tmp_pkt.offset, originaldata)) {
			badworden = efuse_word_enable_data_write(hw,
				    *efuse_addr + 1, tmp_pkt.word_en,
				    originaldata);

			if (0x0F != (badworden & 0x0F)) {
				u8 reorg_offset = tmp_pkt.offset;
				u8 reorg_worden = badworden;
				efuse_pg_packet_write(hw, reorg_offset,
						      reorg_worden,
						      originaldata);
				*efuse_addr = efuse_get_current_size(hw);
			} else {
				*efuse_addr = *efuse_addr + (tmp_word_cnts * 2)
					      + 1;
			}
		} else {
			*efuse_addr = *efuse_addr + (tmp_word_cnts * 2) + 1;
		}

		*write_state = PG_STATE_HEADER;
		*repeat_times += 1;
		if (*repeat_times > EFUSE_REPEAT_THRESHOLD_) {
			*continual = false;
			*result = false;
		}

		RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,
			"efuse PG_STATE_HEADER-2\n");
	}
}

static int efuse_pg_packet_write(struct ieee80211_hw *hw,
				 u8 offset, u8 word_en, u8 *data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct pgpkt_struct target_pkt;
	u8 write_state = PG_STATE_HEADER;
	int continual = true, result = true;
	u16 efuse_addr = 0;
	u8 efuse_data;
	u8 target_word_cnts = 0;
	u8 badworden = 0x0F;
	static int repeat_times;

	if (efuse_get_current_size(hw) >= (EFUSE_MAX_SIZE -
	    rtlpriv->cfg->maps[EFUSE_OOB_PROTECT_BYTES_LEN])) {
		RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,
			"efuse_pg_packet_write error\n");
		return false;
	}

	target_pkt.offset = offset;
	target_pkt.word_en = word_en;

	memset(target_pkt.data, 0xFF, 8 * sizeof(u8));

	efuse_word_enable_data_read(word_en, data, target_pkt.data);
	target_word_cnts = efuse_calculate_word_cnts(target_pkt.word_en);

	RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,  "efuse Power ON\n");

	while (continual && (efuse_addr < (EFUSE_MAX_SIZE -
	       rtlpriv->cfg->maps[EFUSE_OOB_PROTECT_BYTES_LEN]))) {

		if (write_state == PG_STATE_HEADER) {
			badworden = 0x0F;
			RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,
				"efuse PG_STATE_HEADER\n");

			if (efuse_one_byte_read(hw, efuse_addr, &efuse_data) &&
			    (efuse_data != 0xFF))
				efuse_write_data_case1(hw, &efuse_addr,
						       efuse_data, offset,
						       &continual,
						       &write_state, &target_pkt,
						       &repeat_times, &result,
						       word_en);
			else
				efuse_write_data_case2(hw, &efuse_addr,
						       &continual,
						       &write_state,
						       target_pkt,
						       &repeat_times,
						       &result);

		} else if (write_state == PG_STATE_DATA) {
			RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,
				"efuse PG_STATE_DATA\n");
			badworden =
			    efuse_word_enable_data_write(hw, efuse_addr + 1,
							 target_pkt.word_en,
							 target_pkt.data);

			if ((badworden & 0x0F) == 0x0F) {
				continual = false;
			} else {
				efuse_addr += (2 * target_word_cnts) + 1;

				target_pkt.offset = offset;
				target_pkt.word_en = badworden;
				target_word_cnts =
				    efuse_calculate_word_cnts(target_pkt.
							      word_en);
				write_state = PG_STATE_HEADER;
				repeat_times++;
				if (repeat_times > EFUSE_REPEAT_THRESHOLD_) {
					continual = false;
					result = false;
				}
				RTPRINT(rtlpriv, FEEPROM, EFUSE_PG,
					"efuse PG_STATE_HEADER-3\n");
			}
		}
	}

	if (efuse_addr >= (EFUSE_MAX_SIZE -
	    rtlpriv->cfg->maps[EFUSE_OOB_PROTECT_BYTES_LEN])) {
		RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD,
			 "efuse_addr(%#x) Out of size!!\n", efuse_addr);
	}

	return true;
}

static void efuse_word_enable_data_read(u8 word_en,
					u8 *sourdata, u8 *targetdata)
{
	if (!(word_en & BIT(0))) {
		targetdata[0] = sourdata[0];
		targetdata[1] = sourdata[1];
	}

	if (!(word_en & BIT(1))) {
		targetdata[2] = sourdata[2];
		targetdata[3] = sourdata[3];
	}

	if (!(word_en & BIT(2))) {
		targetdata[4] = sourdata[4];
		targetdata[5] = sourdata[5];
	}

	if (!(word_en & BIT(3))) {
		targetdata[6] = sourdata[6];
		targetdata[7] = sourdata[7];
	}
}

static u8 efuse_word_enable_data_write(struct ieee80211_hw *hw,
				       u16 efuse_addr, u8 word_en, u8 *data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 tmpaddr;
	u16 start_addr = efuse_addr;
	u8 badworden = 0x0F;
	u8 tmpdata[8];

	memset(tmpdata, 0xff, PGPKT_DATA_SIZE);
	RT_TRACE(rtlpriv, COMP_EFUSE, DBG_LOUD, "word_en = %x efuse_addr=%x\n",
		 word_en, efuse_addr);

	if (!(word_en & BIT(0))) {
		tmpaddr = start_addr;
		efuse_one_byte_write(hw, start_addr++, data[0]);
		efuse_one_byte_write(hw, start_addr++, data[1]);

		efuse_one_byte_read(hw, tmpaddr, &tmpdata[0]);
		efuse_one_byte_read(hw, tmpaddr + 1, &tmpdata[1]);
		if ((data[0] != tmpdata[0]) || (data[1] != tmpdata[1]))
			badworden &= (~BIT(0));
	}

	if (!(word_en & BIT(1))) {
		tmpaddr = start_addr;
		efuse_one_byte_write(hw, start_addr++, data[2]);
		efuse_one_byte_write(hw, start_addr++, data[3]);

		efuse_one_byte_read(hw, tmpaddr, &tmpdata[2]);
		efuse_one_byte_read(hw, tmpaddr + 1, &tmpdata[3]);
		if ((data[2] != tmpdata[2]) || (data[3] != tmpdata[3]))
			badworden &= (~BIT(1));
	}

	if (!(word_en & BIT(2))) {
		tmpaddr = start_addr;
		efuse_one_byte_write(hw, start_addr++, data[4]);
		efuse_one_byte_write(hw, start_addr++, data[5]);

		efuse_one_byte_read(hw, tmpaddr, &tmpdata[4]);
		efuse_one_byte_read(hw, tmpaddr + 1, &tmpdata[5]);
		if ((data[4] != tmpdata[4]) || (data[5] != tmpdata[5]))
			badworden &= (~BIT(2));
	}

	if (!(word_en & BIT(3))) {
		tmpaddr = start_addr;
		efuse_one_byte_write(hw, start_addr++, data[6]);
		efuse_one_byte_write(hw, start_addr++, data[7]);

		efuse_one_byte_read(hw, tmpaddr, &tmpdata[6]);
		efuse_one_byte_read(hw, tmpaddr + 1, &tmpdata[7]);
		if ((data[6] != tmpdata[6]) || (data[7] != tmpdata[7]))
			badworden &= (~BIT(3));
	}

	return badworden;
}

static void efuse_power_switch(struct ieee80211_hw *hw, u8 write, u8 pwrstate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tempval;
	u16 tmpV16;

	if (pwrstate && (rtlhal->hw_type != HARDWARE_TYPE_RTL8192SE)) {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8188EE)
			rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_ACCESS],
				       0x69);

		tmpV16 = rtl_read_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_ISO_CTRL]);
		if (!(tmpV16 & rtlpriv->cfg->maps[EFUSE_PWC_EV12V])) {
			tmpV16 |= rtlpriv->cfg->maps[EFUSE_PWC_EV12V];
			rtl_write_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_ISO_CTRL],
				       tmpV16);
		}

		tmpV16 = rtl_read_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_FUNC_EN]);
		if (!(tmpV16 & rtlpriv->cfg->maps[EFUSE_FEN_ELDR])) {
			tmpV16 |= rtlpriv->cfg->maps[EFUSE_FEN_ELDR];
			rtl_write_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_FUNC_EN], tmpV16);
		}

		tmpV16 = rtl_read_word(rtlpriv, rtlpriv->cfg->maps[SYS_CLK]);
		if ((!(tmpV16 & rtlpriv->cfg->maps[EFUSE_LOADER_CLK_EN])) ||
		    (!(tmpV16 & rtlpriv->cfg->maps[EFUSE_ANA8M]))) {
			tmpV16 |= (rtlpriv->cfg->maps[EFUSE_LOADER_CLK_EN] |
				   rtlpriv->cfg->maps[EFUSE_ANA8M]);
			rtl_write_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_CLK], tmpV16);
		}
	}

	if (pwrstate) {
		if (write) {
			tempval = rtl_read_byte(rtlpriv,
						rtlpriv->cfg->maps[EFUSE_TEST] +
						3);

			if (rtlhal->hw_type != HARDWARE_TYPE_RTL8192SE) {
				tempval &= 0x0F;
				tempval |= (VOLTAGE_V25 << 4);
			}

			rtl_write_byte(rtlpriv,
				       rtlpriv->cfg->maps[EFUSE_TEST] + 3,
				       (tempval | 0x80));
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE) {
			rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CLK],
						0x03);
		}

	} else {
		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8188EE)
			rtl_write_byte(rtlpriv,
				       rtlpriv->cfg->maps[EFUSE_ACCESS], 0);

		if (write) {
			tempval = rtl_read_byte(rtlpriv,
						rtlpriv->cfg->maps[EFUSE_TEST] +
						3);
			rtl_write_byte(rtlpriv,
				       rtlpriv->cfg->maps[EFUSE_TEST] + 3,
				       (tempval & 0x7F));
		}

		if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE) {
			rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[EFUSE_CLK],
						0x02);
		}

	}

}

static u16 efuse_get_current_size(struct ieee80211_hw *hw)
{
	u16 efuse_addr = 0;
	u8 hworden;
	u8 efuse_data, word_cnts;

	while (efuse_one_byte_read(hw, efuse_addr, &efuse_data) &&
	       efuse_addr < EFUSE_MAX_SIZE) {
		if (efuse_data == 0xFF)
			break;

		hworden = efuse_data & 0x0F;
		word_cnts = efuse_calculate_word_cnts(hworden);
		efuse_addr = efuse_addr + (word_cnts * 2) + 1;
	}

	return efuse_addr;
}

static u8 efuse_calculate_word_cnts(u8 word_en)
{
	u8 word_cnts = 0;
	if (!(word_en & BIT(0)))
		word_cnts++;
	if (!(word_en & BIT(1)))
		word_cnts++;
	if (!(word_en & BIT(2)))
		word_cnts++;
	if (!(word_en & BIT(3)))
		word_cnts++;
	return word_cnts;
}

