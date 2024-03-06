/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_DEBUG_H
#define __RTW_DEBUG_H

enum rtw_debug_mask {
	RTW_DBG_PCI		= 0x00000001,
	RTW_DBG_TX		= 0x00000002,
	RTW_DBG_RX		= 0x00000004,
	RTW_DBG_PHY		= 0x00000008,
	RTW_DBG_FW		= 0x00000010,
	RTW_DBG_EFUSE		= 0x00000020,
	RTW_DBG_COEX		= 0x00000040,
	RTW_DBG_RFK		= 0x00000080,
	RTW_DBG_REGD		= 0x00000100,
	RTW_DBG_DEBUGFS		= 0x00000200,
	RTW_DBG_PS		= 0x00000400,
	RTW_DBG_BF		= 0x00000800,
	RTW_DBG_WOW		= 0x00001000,
	RTW_DBG_CFO		= 0x00002000,
	RTW_DBG_PATH_DIV	= 0x00004000,
	RTW_DBG_ADAPTIVITY	= 0x00008000,
	RTW_DBG_HW_SCAN		= 0x00010000,
	RTW_DBG_STATE		= 0x00020000,
	RTW_DBG_SDIO		= 0x00040000,

	RTW_DBG_ALL		= 0xffffffff
};

#ifdef CONFIG_RTW88_DEBUGFS

void rtw_debugfs_init(struct rtw_dev *rtwdev);
void rtw_debugfs_get_simple_phy_info(struct seq_file *m);

#else

static inline void rtw_debugfs_init(struct rtw_dev *rtwdev) {}

#endif /* CONFIG_RTW88_DEBUGFS */

#ifdef CONFIG_RTW88_DEBUG

__printf(3, 4)
void rtw_dbg(struct rtw_dev *rtwdev, enum rtw_debug_mask mask,
	     const char *fmt, ...);

static inline bool rtw_dbg_is_enabled(struct rtw_dev *rtwdev,
				      enum rtw_debug_mask mask)
{
	return !!(rtw_debug_mask & mask);
}

#else

static inline void rtw_dbg(struct rtw_dev *rtwdev, enum rtw_debug_mask mask,
			   const char *fmt, ...) {}

static inline bool rtw_dbg_is_enabled(struct rtw_dev *rtwdev,
				      enum rtw_debug_mask mask)
{
	return false;
}

#endif /* CONFIG_RTW88_DEBUG */

#define rtw_info(rtwdev, a...) dev_info(rtwdev->dev, ##a)
#define rtw_warn(rtwdev, a...) dev_warn(rtwdev->dev, ##a)
#define rtw_err(rtwdev, a...) dev_err(rtwdev->dev, ##a)

#endif
