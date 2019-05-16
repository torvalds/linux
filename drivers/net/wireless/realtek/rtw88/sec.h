/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_SEC_H_
#define __RTW_SEC_H_

#define RTW_SEC_CMD_REG			0x670
#define RTW_SEC_WRITE_REG		0x674
#define RTW_SEC_READ_REG		0x678
#define RTW_SEC_CONFIG			0x680

#define RTW_SEC_CAM_ENTRY_SHIFT		3
#define RTW_SEC_DEFAULT_KEY_NUM		4
#define RTW_SEC_CMD_WRITE_ENABLE	BIT(16)
#define RTW_SEC_CMD_CLEAR		BIT(30)
#define RTW_SEC_CMD_POLLING		BIT(31)

#define RTW_SEC_TX_UNI_USE_DK		BIT(0)
#define RTW_SEC_RX_UNI_USE_DK		BIT(1)
#define RTW_SEC_TX_DEC_EN		BIT(2)
#define RTW_SEC_RX_DEC_EN		BIT(3)
#define RTW_SEC_TX_BC_USE_DK		BIT(6)
#define RTW_SEC_RX_BC_USE_DK		BIT(7)

#define RTW_SEC_ENGINE_EN		BIT(9)

int rtw_sec_get_free_cam(struct rtw_sec_desc *sec);
void rtw_sec_write_cam(struct rtw_dev *rtwdev,
		       struct rtw_sec_desc *sec,
		       struct ieee80211_sta *sta,
		       struct ieee80211_key_conf *key,
		       u8 hw_key_type, u8 hw_key_idx);
void rtw_sec_clear_cam(struct rtw_dev *rtwdev,
		       struct rtw_sec_desc *sec,
		       u8 hw_key_idx);
void rtw_sec_enable_sec_engine(struct rtw_dev *rtwdev);

#endif
