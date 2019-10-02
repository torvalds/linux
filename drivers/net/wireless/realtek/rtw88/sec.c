// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "sec.h"
#include "reg.h"

int rtw_sec_get_free_cam(struct rtw_sec_desc *sec)
{
	/* if default key search is enabled, the first 4 cam entries
	 * are used to direct map to group key with its key->key_idx, so
	 * driver should use cam entries after 4 to install pairwise key
	 */
	if (sec->default_key_search)
		return find_next_zero_bit(sec->cam_map, RTW_MAX_SEC_CAM_NUM,
					  RTW_SEC_DEFAULT_KEY_NUM);

	return find_first_zero_bit(sec->cam_map, RTW_MAX_SEC_CAM_NUM);
}

void rtw_sec_write_cam(struct rtw_dev *rtwdev,
		       struct rtw_sec_desc *sec,
		       struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key,
		       u8 hw_key_type, u8 hw_key_idx)
{
	struct rtw_cam_entry *cam = &sec->cam_table[hw_key_idx];
	u32 write_cmd;
	u32 command;
	u32 content;
	u32 addr;
	int i, j;

	set_bit(hw_key_idx, sec->cam_map);
	cam->valid = true;
	cam->group = !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE);
	cam->hw_key_type = hw_key_type;
	cam->key = key;
	if (sta)
		ether_addr_copy(cam->addr, sta->addr);
	else
		eth_broadcast_addr(cam->addr);

	write_cmd = RTW_SEC_CMD_WRITE_ENABLE | RTW_SEC_CMD_POLLING;
	addr = hw_key_idx << RTW_SEC_CAM_ENTRY_SHIFT;
	for (i = 5; i >= 0; i--) {
		switch (i) {
		case 0:
			content = ((key->keyidx & 0x3))		|
				  ((hw_key_type & 0x7)	<< 2)	|
				  (cam->group		<< 6)	|
				  (cam->valid		<< 15)	|
				  (cam->addr[0]		<< 16)	|
				  (cam->addr[1]		<< 24);
			break;
		case 1:
			content = (cam->addr[2])		|
				  (cam->addr[3]		<< 8)	|
				  (cam->addr[4]		<< 16)	|
				  (cam->addr[5]		<< 24);
			break;
		default:
			j = (i - 2) << 2;
			content = (key->key[j])			|
				  (key->key[j + 1]	<< 8)	|
				  (key->key[j + 2]	<< 16)	|
				  (key->key[j + 3]	<< 24);
			break;
		}

		command = write_cmd | (addr + i);
		rtw_write32(rtwdev, RTW_SEC_WRITE_REG, content);
		rtw_write32(rtwdev, RTW_SEC_CMD_REG, command);
	}
}

void rtw_sec_clear_cam(struct rtw_dev *rtwdev,
		       struct rtw_sec_desc *sec,
		       u8 hw_key_idx)
{
	struct rtw_cam_entry *cam = &sec->cam_table[hw_key_idx];
	u32 write_cmd;
	u32 command;
	u32 addr;

	clear_bit(hw_key_idx, sec->cam_map);
	cam->valid = false;
	cam->key = NULL;
	eth_zero_addr(cam->addr);

	write_cmd = RTW_SEC_CMD_WRITE_ENABLE | RTW_SEC_CMD_POLLING;
	addr = hw_key_idx << RTW_SEC_CAM_ENTRY_SHIFT;
	command = write_cmd | addr;
	rtw_write32(rtwdev, RTW_SEC_WRITE_REG, 0);
	rtw_write32(rtwdev, RTW_SEC_CMD_REG, command);
}

u8 rtw_sec_cam_pg_backup(struct rtw_dev *rtwdev, u8 *used_cam)
{
	struct rtw_sec_desc *sec = &rtwdev->sec;
	u8 offset = 0;
	u8 count, n;

	if (!used_cam)
		return 0;

	for (count = 0; count < MAX_PG_CAM_BACKUP_NUM; count++) {
		n = find_next_bit(sec->cam_map, RTW_MAX_SEC_CAM_NUM, offset);
		if (n == RTW_MAX_SEC_CAM_NUM)
			break;

		used_cam[count] = n;
		offset = n + 1;
	}

	return count;
}

void rtw_sec_enable_sec_engine(struct rtw_dev *rtwdev)
{
	struct rtw_sec_desc *sec = &rtwdev->sec;
	u16 ctrl_reg;
	u16 sec_config;

	/* default use default key search for now */
	sec->default_key_search = true;

	ctrl_reg = rtw_read16(rtwdev, REG_CR);
	ctrl_reg |= RTW_SEC_ENGINE_EN;
	rtw_write16(rtwdev, REG_CR, ctrl_reg);

	sec_config = rtw_read16(rtwdev, RTW_SEC_CONFIG);

	sec_config |= RTW_SEC_TX_DEC_EN | RTW_SEC_RX_DEC_EN;
	if (sec->default_key_search)
		sec_config |= RTW_SEC_TX_UNI_USE_DK | RTW_SEC_RX_UNI_USE_DK |
			      RTW_SEC_TX_BC_USE_DK | RTW_SEC_RX_BC_USE_DK;

	rtw_write16(rtwdev, RTW_SEC_CONFIG, sec_config);
}
