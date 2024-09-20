// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "wifi.h"
#include "efuse.h"
#include "pci.h"
#include <linux/export.h>

static const u8 PGPKT_DATA_SIZE = 8;
static const int EFUSE_MAX_SIZE = 512;

#define START_ADDRESS		0x1000
#define REG_MCUFWDL		0x0080

static const struct rtl_efuse_ops efuse_ops = {
	.efuse_onebyte_read = efuse_one_byte_read,
	.efuse_logical_map_read = efuse_shadow_read,
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
static int efuse_one_byte_write(struct ieee80211_hw *hw, u16 addr,
				u8 data);
static void efuse_read_all_map(struct ieee80211_hw *hw, u8 *efuse);
static int efuse_pg_packet_read(struct ieee80211_hw *hw, u8 offset,
				u8 *data);
static int efuse_pg_packet_write(struct ieee80211_hw *hw, u8 offset,
				 u8 word_en, u8 *data);
static void efuse_word_enable_data_read(u8 word_en, u8 *sourdata,
					u8 *targetdata);
static u8 enable_efuse_data_write(struct ieee80211_hw *hw,
				  u16 efuse_addr, u8 word_en, u8 *data);
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
			if (k == 1000)
				break;
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

	rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD, "Addr=%x Data =%x\n",
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
		rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD,
			"%s: Invalid offset(%#x) with read bytes(%#x)!!\n",
			__func__, _offset, _size_byte);
		return;
	}

	/* allocate memory for efuse_tbl and efuse_word */
	efuse_tbl = kzalloc(rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE],
			    GFP_ATOMIC);
	if (!efuse_tbl)
		return;
	efuse_word = kcalloc(EFUSE_MAX_WORD_UNIT, sizeof(u16 *), GFP_ATOMIC);
	if (!efuse_word)
		goto out;
	for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
		efuse_word[i] = kcalloc(efuse_max_section, sizeof(u16),
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
out:
	kfree(efuse_tbl);
}

bool efuse_shadow_update_chk(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 section_idx, i, base;
	u16 words_need = 0, hdr_num = 0, totalbytes, efuse_used;
	bool wordchanged, result = true;

	for (section_idx = 0; section_idx < 16; section_idx++) {
		base = section_idx * 8;
		wordchanged = false;

		for (i = 0; i < 8; i = i + 2) {
			if (rtlefuse->efuse_map[EFUSE_INIT_MAP][base + i] !=
			    rtlefuse->efuse_map[EFUSE_MODIFY_MAP][base + i] ||
			    rtlefuse->efuse_map[EFUSE_INIT_MAP][base + i + 1] !=
			    rtlefuse->efuse_map[EFUSE_MODIFY_MAP][base + i +
								   1]) {
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
	    (EFUSE_MAX_SIZE - rtlpriv->cfg->maps[EFUSE_OOB_PROTECT_BYTES_LEN]))
		result = false;

	rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD,
		"%s: totalbytes(%#x), hdr_num(%#x), words_need(%#x), efuse_used(%d)\n",
		__func__, totalbytes, hdr_num, words_need, efuse_used);

	return result;
}

void efuse_shadow_read(struct ieee80211_hw *hw, u8 type,
		       u16 offset, u32 *value)
{
	if (type == 1)
		efuse_shadow_read_1byte(hw, offset, (u8 *)value);
	else if (type == 2)
		efuse_shadow_read_2byte(hw, offset, (u16 *)value);
	else if (type == 4)
		efuse_shadow_read_4byte(hw, offset, value);

}
EXPORT_SYMBOL(efuse_shadow_read);

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

	rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD, "\n");

	if (!efuse_shadow_update_chk(hw)) {
		efuse_read_all_map(hw, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0]);
		memcpy(&rtlefuse->efuse_map[EFUSE_MODIFY_MAP][0],
		       &rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
		       rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);

		rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD,
			"efuse out of capacity!!\n");
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
				      "U-efuse\n", tmpdata, 8);

			if (!efuse_pg_packet_write(hw, (u8) offset, word_en,
						   tmpdata)) {
				rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
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

	rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD, "\n");
	return true;
}

void rtl_efuse_shadow_map_update(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));

	if (rtlefuse->autoload_failflag)
		memset((&rtlefuse->efuse_map[EFUSE_INIT_MAP][0]),
		       0xFF, rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);
	else
		efuse_read_all_map(hw, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0]);

	memcpy(&rtlefuse->efuse_map[EFUSE_MODIFY_MAP][0],
			&rtlefuse->efuse_map[EFUSE_INIT_MAP][0],
			rtlpriv->cfg->maps[EFUSE_HWSET_MAX_SIZE]);

}
EXPORT_SYMBOL(rtl_efuse_shadow_map_update);

void efuse_force_write_vendor_id(struct ieee80211_hw *hw)
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

int efuse_one_byte_read(struct ieee80211_hw *hw, u16 addr, u8 *data)
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
EXPORT_SYMBOL(efuse_one_byte_read);

static int efuse_one_byte_write(struct ieee80211_hw *hw, u16 addr, u8 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmpidx = 0;

	rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD,
		"Addr = %x Data=%x\n", addr, data);

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

static void efuse_read_all_map(struct ieee80211_hw *hw, u8 *efuse)
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
					dataempty = false;
			}
		}

		if (!dataempty) {
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
						      efuse_data, offset,
						      tmpdata, &readstate);
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
				   u8 efuse_data, u8 offset,
				   int *continual, u8 *write_state,
				   struct pgpkt_struct *target_pkt,
				   int *repeat_times, int *result, u8 word_en)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct pgpkt_struct tmp_pkt;
	int dataempty = true;
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
			if (efuse_one_byte_read(hw,
						(*efuse_addr + 1 + tmpindex),
						&efuse_data) &&
			    (efuse_data != 0xFF))
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
				badworden =
				  enable_efuse_data_write(hw,
							  *efuse_addr + 1,
							  tmp_pkt.word_en,
							  target_pkt->data);

				if (0x0F != (badworden & 0x0F))	{
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
	RTPRINT(rtlpriv, FEEPROM, EFUSE_PG, "efuse PG_STATE_HEADER-1\n");
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

		memset(originaldata, 0xff,  8 * sizeof(u8));

		if (efuse_pg_packet_read(hw, tmp_pkt.offset, originaldata)) {
			badworden = enable_efuse_data_write(hw,
							    *efuse_addr + 1,
							    tmp_pkt.word_en,
							    originaldata);

			if (0x0F != (badworden & 0x0F)) {
				u8 reorg_offset = tmp_pkt.offset;
				u8 reorg_worden = badworden;

				efuse_pg_packet_write(hw, reorg_offset,
						      reorg_worden,
						      originaldata);
				*efuse_addr = efuse_get_current_size(hw);
			} else {
				*efuse_addr = *efuse_addr +
					      (tmp_word_cnts * 2) + 1;
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

	memset(target_pkt.data, 0xFF,  8 * sizeof(u8));

	efuse_word_enable_data_read(word_en, data, target_pkt.data);
	target_word_cnts = efuse_calculate_word_cnts(target_pkt.word_en);

	RTPRINT(rtlpriv, FEEPROM, EFUSE_PG, "efuse Power ON\n");

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
						       &write_state,
						       &target_pkt,
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
			    enable_efuse_data_write(hw, efuse_addr + 1,
						    target_pkt.word_en,
						    target_pkt.data);

			if ((badworden & 0x0F) == 0x0F) {
				continual = false;
			} else {
				efuse_addr =
				    efuse_addr + (2 * target_word_cnts) + 1;

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
		rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD,
			"efuse_addr(%#x) Out of size!!\n", efuse_addr);
	}

	return true;
}

static void efuse_word_enable_data_read(u8 word_en, u8 *sourdata,
					u8 *targetdata)
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

static u8 enable_efuse_data_write(struct ieee80211_hw *hw,
				  u16 efuse_addr, u8 word_en, u8 *data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 tmpaddr;
	u16 start_addr = efuse_addr;
	u8 badworden = 0x0F;
	u8 tmpdata[8];

	memset(tmpdata, 0xff, PGPKT_DATA_SIZE);
	rtl_dbg(rtlpriv, COMP_EFUSE, DBG_LOUD,
		"word_en = %x efuse_addr=%x\n", word_en, efuse_addr);

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

void efuse_power_switch(struct ieee80211_hw *hw, u8 write, u8 pwrstate)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 tempval;
	u16 tmpv16;

	if (pwrstate && (rtlhal->hw_type != HARDWARE_TYPE_RTL8192SE)) {
		if (rtlhal->hw_type != HARDWARE_TYPE_RTL8192CE &&
		    rtlhal->hw_type != HARDWARE_TYPE_RTL8192DE) {
			rtl_write_byte(rtlpriv,
				       rtlpriv->cfg->maps[EFUSE_ACCESS], 0x69);
		} else {
			tmpv16 =
			  rtl_read_word(rtlpriv,
					rtlpriv->cfg->maps[SYS_ISO_CTRL]);
			if (!(tmpv16 & rtlpriv->cfg->maps[EFUSE_PWC_EV12V])) {
				tmpv16 |= rtlpriv->cfg->maps[EFUSE_PWC_EV12V];
				rtl_write_word(rtlpriv,
					       rtlpriv->cfg->maps[SYS_ISO_CTRL],
					       tmpv16);
			}
		}
		tmpv16 = rtl_read_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_FUNC_EN]);
		if (!(tmpv16 & rtlpriv->cfg->maps[EFUSE_FEN_ELDR])) {
			tmpv16 |= rtlpriv->cfg->maps[EFUSE_FEN_ELDR];
			rtl_write_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_FUNC_EN], tmpv16);
		}

		tmpv16 = rtl_read_word(rtlpriv, rtlpriv->cfg->maps[SYS_CLK]);
		if ((!(tmpv16 & rtlpriv->cfg->maps[EFUSE_LOADER_CLK_EN])) ||
		    (!(tmpv16 & rtlpriv->cfg->maps[EFUSE_ANA8M]))) {
			tmpv16 |= (rtlpriv->cfg->maps[EFUSE_LOADER_CLK_EN] |
				   rtlpriv->cfg->maps[EFUSE_ANA8M]);
			rtl_write_word(rtlpriv,
				       rtlpriv->cfg->maps[SYS_CLK], tmpv16);
		}
	}

	if (pwrstate) {
		if (write) {
			tempval = rtl_read_byte(rtlpriv,
						rtlpriv->cfg->maps[EFUSE_TEST] +
						3);

			if (rtlhal->hw_type == HARDWARE_TYPE_RTL8812AE) {
				tempval &= ~(BIT(3) | BIT(4) | BIT(5) | BIT(6));
				tempval |= (VOLTAGE_V25 << 3);
			} else if (rtlhal->hw_type != HARDWARE_TYPE_RTL8192SE) {
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
		if (rtlhal->hw_type != HARDWARE_TYPE_RTL8192CE &&
		    rtlhal->hw_type != HARDWARE_TYPE_RTL8192DE)
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
EXPORT_SYMBOL(efuse_power_switch);

static u16 efuse_get_current_size(struct ieee80211_hw *hw)
{
	int continual = true;
	u16 efuse_addr = 0;
	u8 hworden;
	u8 efuse_data, word_cnts;

	while (continual && efuse_one_byte_read(hw, efuse_addr, &efuse_data) &&
	       (efuse_addr < EFUSE_MAX_SIZE)) {
		if (efuse_data != 0xFF) {
			hworden = efuse_data & 0x0F;
			word_cnts = efuse_calculate_word_cnts(hworden);
			efuse_addr = efuse_addr + (word_cnts * 2) + 1;
		} else {
			continual = false;
		}
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

int rtl_get_hwinfo(struct ieee80211_hw *hw, struct rtl_priv *rtlpriv,
		   int max_size, u8 *hwinfo, const int *params)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct device *dev = &rtlpcipriv->dev.pdev->dev;
	u16 eeprom_id;
	u16 i, usvalue;

	switch (rtlefuse->epromtype) {
	case EEPROM_BOOT_EFUSE:
		rtl_efuse_shadow_map_update(hw);
		break;

	case EEPROM_93C46:
		pr_err("RTL8XXX did not boot from eeprom, check it !!\n");
		return 1;

	default:
		dev_warn(dev, "no efuse data\n");
		return 1;
	}

	memcpy(hwinfo, &rtlefuse->efuse_map[EFUSE_INIT_MAP][0], max_size);

	RT_PRINT_DATA(rtlpriv, COMP_INIT, DBG_DMESG, "MAP",
		      hwinfo, max_size);

	eeprom_id = *((u16 *)&hwinfo[0]);
	if (eeprom_id != params[0]) {
		rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
			"EEPROM ID(%#x) is invalid!!\n", eeprom_id);
		rtlefuse->autoload_failflag = true;
	} else {
		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD, "Autoload OK\n");
		rtlefuse->autoload_failflag = false;
	}

	if (rtlefuse->autoload_failflag)
		return 1;

	rtlefuse->eeprom_vid = *(u16 *)&hwinfo[params[1]];
	rtlefuse->eeprom_did = *(u16 *)&hwinfo[params[2]];
	rtlefuse->eeprom_svid = *(u16 *)&hwinfo[params[3]];
	rtlefuse->eeprom_smid = *(u16 *)&hwinfo[params[4]];
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"EEPROMId = 0x%4x\n", eeprom_id);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"EEPROM VID = 0x%4x\n", rtlefuse->eeprom_vid);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"EEPROM DID = 0x%4x\n", rtlefuse->eeprom_did);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"EEPROM SVID = 0x%4x\n", rtlefuse->eeprom_svid);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"EEPROM SMID = 0x%4x\n", rtlefuse->eeprom_smid);

	for (i = 0; i < 6; i += 2) {
		usvalue = *(u16 *)&hwinfo[params[5] + i];
		*((u16 *)(&rtlefuse->dev_addr[i])) = usvalue;
	}
	rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG, "%pM\n", rtlefuse->dev_addr);

	rtlefuse->eeprom_channelplan = *&hwinfo[params[6]];
	rtlefuse->eeprom_version = *(u16 *)&hwinfo[params[7]];
	rtlefuse->txpwr_fromeprom = true;
	rtlefuse->eeprom_oemid = *&hwinfo[params[8]];

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"EEPROM Customer ID: 0x%2x\n", rtlefuse->eeprom_oemid);

	/* set channel plan to world wide 13 */
	rtlefuse->channel_plan = params[9];

	return 0;
}
EXPORT_SYMBOL_GPL(rtl_get_hwinfo);

static void _rtl_fw_block_write_usb(struct ieee80211_hw *hw, u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 start = START_ADDRESS;
	u32 n;

	while (size > 0) {
		if (size >= 64)
			n = 64;
		else if (size >= 8)
			n = 8;
		else
			n = 1;

		rtl_write_chunk(rtlpriv, start, n, buffer);

		start += n;
		buffer += n;
		size -= n;
	}
}

void rtl_fw_block_write(struct ieee80211_hw *hw, u8 *buffer, u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 i;

	if (rtlpriv->rtlhal.interface == INTF_PCI) {
		for (i = 0; i < size; i++)
			rtl_write_byte(rtlpriv, (START_ADDRESS + i),
				       *(buffer + i));
	} else if (rtlpriv->rtlhal.interface == INTF_USB) {
		_rtl_fw_block_write_usb(hw, buffer, size);
	}
}
EXPORT_SYMBOL_GPL(rtl_fw_block_write);

void rtl_fw_page_write(struct ieee80211_hw *hw, u32 page, u8 *buffer,
		       u32 size)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 value8;
	u8 u8page = (u8)(page & 0x07);

	value8 = (rtl_read_byte(rtlpriv, REG_MCUFWDL + 2) & 0xF8) | u8page;

	rtl_write_byte(rtlpriv, (REG_MCUFWDL + 2), value8);
	rtl_fw_block_write(hw, buffer, size);
}
EXPORT_SYMBOL_GPL(rtl_fw_page_write);

void rtl_fill_dummy(u8 *pfwbuf, u32 *pfwlen)
{
	u32 fwlen = *pfwlen;
	u8 remain = (u8)(fwlen % 4);

	remain = (remain == 0) ? 0 : (4 - remain);

	while (remain > 0) {
		pfwbuf[fwlen] = 0;
		fwlen++;
		remain--;
	}

	*pfwlen = fwlen;
}
EXPORT_SYMBOL_GPL(rtl_fill_dummy);

void rtl_efuse_ops_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->efuse.efuse_ops = &efuse_ops;
}
EXPORT_SYMBOL_GPL(rtl_efuse_ops_init);
