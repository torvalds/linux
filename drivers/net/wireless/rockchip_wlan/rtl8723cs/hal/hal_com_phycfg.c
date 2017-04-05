/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HAL_COM_PHYCFG_C_

#include <drv_types.h>
#include <hal_data.h>

#define PG_TXPWR_MSB_DIFF_S4BIT(_pg_v) (((_pg_v) & 0xf0) >> 4)
#define PG_TXPWR_LSB_DIFF_S4BIT(_pg_v) ((_pg_v) & 0x0f)
#define PG_TXPWR_MSB_DIFF_TO_S8BIT(_pg_v) ((PG_TXPWR_MSB_DIFF_S4BIT(_pg_v) & BIT3) ? (PG_TXPWR_MSB_DIFF_S4BIT(_pg_v) | 0xF0) : PG_TXPWR_MSB_DIFF_S4BIT(_pg_v))
#define PG_TXPWR_LSB_DIFF_TO_S8BIT(_pg_v) ((PG_TXPWR_LSB_DIFF_S4BIT(_pg_v) & BIT3) ? (PG_TXPWR_LSB_DIFF_S4BIT(_pg_v) | 0xF0) : PG_TXPWR_LSB_DIFF_S4BIT(_pg_v))
#define IS_PG_TXPWR_BASE_INVALID(_base) ((_base) > 63)
#define IS_PG_TXPWR_DIFF_INVALID(_diff) ((_diff) > 7 || (_diff) < -8)
#define PG_TXPWR_INVALID_BASE 255
#define PG_TXPWR_INVALID_DIFF 8

#if !IS_PG_TXPWR_BASE_INVALID(PG_TXPWR_INVALID_BASE)
#error "PG_TXPWR_BASE definition has problem"
#endif

#if !IS_PG_TXPWR_DIFF_INVALID(PG_TXPWR_INVALID_DIFF)
#error "PG_TXPWR_DIFF definition has problem"
#endif

#define PG_TXPWR_SRC_PG_DATA	0
#define PG_TXPWR_SRC_IC_DEF		1
#define PG_TXPWR_SRC_DEF		2
#define PG_TXPWR_SRC_NUM		3

const char *const _pg_txpwr_src_str[] = {
	"PG_DATA",
	"IC_DEF",
	"DEF",
	"UNKNOWN"
};

#define pg_txpwr_src_str(src) (((src) >= PG_TXPWR_SRC_NUM) ? _pg_txpwr_src_str[PG_TXPWR_SRC_NUM] : _pg_txpwr_src_str[(src)])

#ifndef DBG_PG_TXPWR_READ
#define DBG_PG_TXPWR_READ 0
#endif

void dump_pg_txpwr_info_2g(void *sel, TxPowerInfo24G *txpwr_info, u8 rfpath_num, u8 max_tx_cnt)
{
	int path, group, tx_idx;

	RTW_PRINT_SEL(sel, "2.4G\n");
	RTW_PRINT_SEL(sel, "CCK-1T base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (group = 0; group < MAX_CHNL_GROUP_24G; group++)
		_RTW_PRINT_SEL(sel, "G%02d ", group);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++)
			_RTW_PRINT_SEL(sel, "%3u ", txpwr_info->IndexCCK_Base[path][group]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "CCK diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dT ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->CCK_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW40-1S base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++)
		_RTW_PRINT_SEL(sel, "G%02d ", group);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++)
			_RTW_PRINT_SEL(sel, "%3u ", txpwr_info->IndexBW40_Base[path][group]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "OFDM diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dT ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->OFDM_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW20 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dS ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->BW20_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW40 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dS ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->BW40_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");
}

void dump_pg_txpwr_info_5g(void *sel, TxPowerInfo5G *txpwr_info, u8 rfpath_num, u8 max_tx_cnt)
{
	int path, group, tx_idx;

	RTW_PRINT_SEL(sel, "5G\n");
	RTW_PRINT_SEL(sel, "BW40-1S base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
		_RTW_PRINT_SEL(sel, "G%02d ", group);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
			_RTW_PRINT_SEL(sel, "%3u ", txpwr_info->IndexBW40_Base[path][group]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "OFDM diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dT ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->OFDM_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW20 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dS ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->BW20_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW40 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dS ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->BW40_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW80 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dS ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->BW80_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW160 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++)
		_RTW_PRINT_SEL(sel, "%dS ", path + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", txpwr_info->BW160_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");
}

const struct map_t pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 168,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE, 0xEE, 0xEE,
			0xEE, 0xEE, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x04, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x2A, 0x2A, 0x2A, 0x2A,
			0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x04, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
			0xEE, 0xEE, 0xEE, 0xEE, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24,
			0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x2A, 0x2A, 0x2A, 0x2A, 0x04, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
			0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x04, 0xEE,
			0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE)
	);

#ifdef CONFIG_RTL8188E
static const struct map_t rtl8188e_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 12,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24)
	);
#endif

#ifdef CONFIG_RTL8188F
static const struct map_t rtl8188f_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 12,
			0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x27, 0x27, 0x27, 0x27, 0x27, 0x24)
	);
#endif

#ifdef CONFIG_RTL8723B
static const struct map_t rtl8723b_pg_txpwr_def_info =
	MAP_ENT(0xB8, 2, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 12,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0xE0)
		, MAPSEG_ARRAY_ENT(0x3A, 12,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0xE0)
	);
#endif

#ifdef CONFIG_RTL8703B
static const struct map_t rtl8703b_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 12,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02)
	);
#endif

#ifdef CONFIG_RTL8723D
static const struct map_t rtl8723d_pg_txpwr_def_info =
	MAP_ENT(0xB8, 2, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 12,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02)
		, MAPSEG_ARRAY_ENT(0x3A, 12,
			0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x21, 0x21, 0x21, 0x21, 0x21, 0x02)
	);
#endif

#ifdef CONFIG_RTL8192E
static const struct map_t rtl8192e_pg_txpwr_def_info =
	MAP_ENT(0xB8, 2, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 14,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE)
		, MAPSEG_ARRAY_ENT(0x3A, 14,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE)
	);
#endif

#ifdef CONFIG_RTL8821A
static const struct map_t rtl8821a_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 39,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00)
	);
#endif

#ifdef CONFIG_RTL8821C
static const struct map_t rtl8821c_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 54,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 
			0xFF, 0xFF, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 
			0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEC, 0xFF, 0xFF, 0xFF, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02)
	);
#endif

#ifdef CONFIG_RTL8812A
static const struct map_t rtl8812a_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 82,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xFF, 0xFF,
			0xFF, 0xFF, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x02, 0xEE, 0xFF, 0xFF, 0xEE, 0xFF, 0x00, 0xEE, 0xFF, 0xFF, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xFF, 0xFF, 0xFF, 0xFF, 0x2A, 0x2A, 0x2A, 0x2A,
			0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x02, 0xEE, 0xFF, 0xFF, 0xEE, 0xFF,
			0x00, 0xEE)
	);
#endif

#ifdef CONFIG_RTL8822B
static const struct map_t rtl8822b_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 82,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xFF, 0xFF,
			0xFF, 0xFF, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x02, 0xEE, 0xFF, 0xFF, 0xEE, 0xFF, 0xEC, 0xEC, 0xFF, 0xFF, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xFF, 0xFF, 0xFF, 0xFF, 0x2A, 0x2A, 0x2A, 0x2A,
			0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x02, 0xEE, 0xFF, 0xFF, 0xEE, 0xFF,
			0xEC, 0xEC)
	);
#endif

#ifdef CONFIG_RTL8814A
static const struct map_t rtl8814a_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 168,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xEE, 0xEE,
			0xEE, 0xEE, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x02, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x00, 0xEE, 0xEE, 0xEE, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x2A, 0x2A, 0x2A, 0x2A,
			0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x02, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
			0x00, 0xEE, 0xEE, 0xEE, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02,
			0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A,
			0x2A, 0x2A, 0x2A, 0x2A, 0x02, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0x00, 0xEE, 0xEE, 0xEE, 0x2D, 0x2D,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
			0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x2A, 0x02, 0xEE,
			0xEE, 0xEE, 0xEE, 0xEE, 0x00, 0xEE, 0xEE, 0xEE)
	);
#endif

const struct map_t *hal_pg_txpwr_def_info(_adapter *adapter)
{
	u8 interface_type = 0;
	const struct map_t *map = NULL;

	interface_type = rtw_get_intf_type(adapter);

	switch (rtw_get_chip_type(adapter)) {
#ifdef CONFIG_RTL8723B
	case RTL8723B:
		map = &rtl8723b_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8703B
	case RTL8703B:
		map = &rtl8703b_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8723D
	case RTL8723D:
		map = &rtl8723d_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8188E
	case RTL8188E:
		map = &rtl8188e_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8188F
	case RTL8188F:
		map = &rtl8188f_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8812A
	case RTL8812:
		map = &rtl8812a_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8821A
	case RTL8821:
		map = &rtl8821a_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8192E
	case RTL8192E:
		map = &rtl8192e_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8814A
	case RTL8814A:
		map = &rtl8814a_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8822B
	case RTL8822B:
		map = &rtl8822b_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8821C
	case RTL8821C:
		map = &rtl8821c_pg_txpwr_def_info;
		break;
#endif
	}

	if (map == NULL) {
		RTW_ERR("%s: unknown chip_type:%u\n"
			, __func__, rtw_get_chip_type(adapter));
		rtw_warn_on(1);
	}

	return map;
}

static u8 hal_chk_pg_txpwr_info_2g(_adapter *adapter, TxPowerInfo24G *pwr_info)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path, group, tx_idx;

	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_2G))
		return _SUCCESS;

	for (path = 0; path < MAX_RF_PATH; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path))
			continue;
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			if (IS_PG_TXPWR_BASE_INVALID(pwr_info->IndexCCK_Base[path][group])
				|| IS_PG_TXPWR_BASE_INVALID(pwr_info->IndexBW40_Base[path][group]))
				return _FAIL;
		}
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			if (!HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx))
				continue;
			if (IS_PG_TXPWR_DIFF_INVALID(pwr_info->CCK_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW20_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW40_Diff[path][tx_idx]))
				return _FAIL;
		}
	}

	return _SUCCESS;
}

static u8 hal_chk_pg_txpwr_info_5g(_adapter *adapter, TxPowerInfo5G *pwr_info)
{
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path, group, tx_idx;

	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_5G))
		return _SUCCESS;

	for (path = 0; path < MAX_RF_PATH; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path))
			continue;
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
			if (IS_PG_TXPWR_BASE_INVALID(pwr_info->IndexBW40_Base[path][group]))
				return _FAIL;
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			if (!HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx))
				continue;
			if (IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW20_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW40_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW80_Diff[path][tx_idx])
				|| IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW160_Diff[path][tx_idx]))
				return _FAIL;
		}
	}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
	return _SUCCESS;
}

static inline void hal_init_pg_txpwr_info_2g(_adapter *adapter, TxPowerInfo24G *pwr_info)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path, group, tx_idx;

	if (pwr_info == NULL)
		return;

	_rtw_memset(pwr_info, 0, sizeof(TxPowerInfo24G));

	/* init with invalid value */
	for (path = 0; path < MAX_RF_PATH; path++) {
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			pwr_info->IndexCCK_Base[path][group] = PG_TXPWR_INVALID_BASE;
			pwr_info->IndexBW40_Base[path][group] = PG_TXPWR_INVALID_BASE;
		}
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			pwr_info->CCK_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->OFDM_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->BW20_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->BW40_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
		}
	}

	/* init for dummy base and diff */
	for (path = 0; path < MAX_RF_PATH; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path))
			break;
		/* 2.4G BW40 base has 1 less group than CCK base*/
		pwr_info->IndexBW40_Base[path][MAX_CHNL_GROUP_24G - 1] = 0;

		/* dummy diff */
		pwr_info->CCK_Diff[path][0] = 0; /* 2.4G CCK-1TX */
		pwr_info->BW40_Diff[path][0] = 0; /* 2.4G BW40-1S */
	}
}

static inline void hal_init_pg_txpwr_info_5g(_adapter *adapter, TxPowerInfo5G *pwr_info)
{
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path, group, tx_idx;

	if (pwr_info == NULL)
		return;

	_rtw_memset(pwr_info, 0, sizeof(TxPowerInfo5G));

	/* init with invalid value */
	for (path = 0; path < MAX_RF_PATH; path++) {
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
			pwr_info->IndexBW40_Base[path][group] = PG_TXPWR_INVALID_BASE;
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			pwr_info->OFDM_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->BW20_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->BW40_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->BW80_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
			pwr_info->BW160_Diff[path][tx_idx] = PG_TXPWR_INVALID_DIFF;
		}
	}

	for (path = 0; path < MAX_RF_PATH; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path))
			break;
		/* dummy diff */
		pwr_info->BW40_Diff[path][0] = 0; /* 5G BW40-1S */
	}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
}

#if DBG_PG_TXPWR_READ
#define LOAD_PG_TXPWR_WARN_COND(_txpwr_src) 1
#else
#define LOAD_PG_TXPWR_WARN_COND(_txpwr_src) (_txpwr_src > PG_TXPWR_SRC_PG_DATA)
#endif

u16 hal_load_pg_txpwr_info_path_2g(
	_adapter *adapter,
	TxPowerInfo24G	*pwr_info,
	u32 path,
	u8 txpwr_src,
	const struct map_t *txpwr_map,
	u16 pg_offset)
{
#define PG_TXPWR_1PATH_BYTE_NUM_2G 18

	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u16 offset = pg_offset;
	u8 group, tx_idx;
	u8 val;
	u8 tmp_base;
	s8 tmp_diff;

	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_2G)) {
		offset += PG_TXPWR_1PATH_BYTE_NUM_2G;
		goto exit;
	}

	if (DBG_PG_TXPWR_READ)
		RTW_INFO("%s [%c] offset:0x%03x\n", __func__, rf_path_char(path), offset);

	for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
		if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path)) {
			tmp_base = map_read8(txpwr_map, offset);
			if (!IS_PG_TXPWR_BASE_INVALID(tmp_base)
				&& IS_PG_TXPWR_BASE_INVALID(pwr_info->IndexCCK_Base[path][group])
			) {
				pwr_info->IndexCCK_Base[path][group] = tmp_base;
				if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
					RTW_INFO("[%c] 2G G%02d CCK-1T base:%u from %s\n", rf_path_char(path), group, tmp_base, pg_txpwr_src_str(txpwr_src));
			}
		}
		offset++;
	}

	for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++) {
		if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path)) {
			tmp_base = map_read8(txpwr_map, offset);
			if (!IS_PG_TXPWR_BASE_INVALID(tmp_base)
				&& IS_PG_TXPWR_BASE_INVALID(pwr_info->IndexBW40_Base[path][group])
			) {
				pwr_info->IndexBW40_Base[path][group] =	tmp_base;
				if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
					RTW_INFO("[%c] 2G G%02d BW40-1S base:%u from %s\n", rf_path_char(path), group, tmp_base, pg_txpwr_src_str(txpwr_src));
			}
		}
		offset++;
	}

	for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
		if (tx_idx == 0) {
			if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx)) {
				val = map_read8(txpwr_map, offset);
				tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW20_Diff[path][tx_idx])
				) {
					pwr_info->BW20_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 2G BW20-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
				tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][tx_idx])
				) {
					pwr_info->OFDM_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 2G OFDM-%dT diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
			}
			offset++;
		} else {
			if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx)) {
				val = map_read8(txpwr_map, offset);
				tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW40_Diff[path][tx_idx])
				) {
					pwr_info->BW40_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 2G BW40-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));

				}
				tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW20_Diff[path][tx_idx])
				) {
					pwr_info->BW20_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 2G BW20-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
			}
			offset++;

			if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx)) {
				val = map_read8(txpwr_map, offset);
				tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][tx_idx])
				) {
					pwr_info->OFDM_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 2G OFDM-%dT diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
				tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->CCK_Diff[path][tx_idx])
				) {
					pwr_info->CCK_Diff[path][tx_idx] =	tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 2G CCK-%dT diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
			}
			offset++;
		}
	}

	if (offset != pg_offset + PG_TXPWR_1PATH_BYTE_NUM_2G) {
		RTW_ERR("%s parse %d bytes != %d\n", __func__, offset - pg_offset, PG_TXPWR_1PATH_BYTE_NUM_2G);
		rtw_warn_on(1);
	}

exit:	
	return offset;
}

u16 hal_load_pg_txpwr_info_path_5g(
	_adapter *adapter,
	TxPowerInfo5G	*pwr_info,
	u32 path,
	u8 txpwr_src,
	const struct map_t *txpwr_map,
	u16 pg_offset)
{
#define PG_TXPWR_1PATH_BYTE_NUM_5G 24

	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u16 offset = pg_offset;
	u8 group, tx_idx;
	u8 val;
	u8 tmp_base;
	s8 tmp_diff;

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_5G))
#endif
	{
		offset += PG_TXPWR_1PATH_BYTE_NUM_5G;
		goto exit;
	}
	
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (DBG_PG_TXPWR_READ)
		RTW_INFO("%s[%c] eaddr:0x%03x\n", __func__, rf_path_char(path), offset);

	for (group = 0; group < MAX_CHNL_GROUP_5G; group++) {
		if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path)) {
			tmp_base = map_read8(txpwr_map, offset);
			if (!IS_PG_TXPWR_BASE_INVALID(tmp_base)
				&& IS_PG_TXPWR_BASE_INVALID(pwr_info->IndexBW40_Base[path][group])
			) {
				pwr_info->IndexBW40_Base[path][group] = tmp_base;
				if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
					RTW_INFO("[%c] 5G G%02d BW40-1S base:%u from %s\n", rf_path_char(path), group, tmp_base, pg_txpwr_src_str(txpwr_src));
			}
		}
		offset++;
	}

	for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
		if (tx_idx == 0) {
			if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx)) {
				val = map_read8(txpwr_map, offset);
				tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW20_Diff[path][tx_idx])
				) {
					pwr_info->BW20_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 5G BW20-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
				tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][tx_idx])
				) {
					pwr_info->OFDM_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 5G OFDM-%dT diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
			}
			offset++;
		} else {
			if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx)) {
				val = map_read8(txpwr_map, offset);
				tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW40_Diff[path][tx_idx])
				) {
					pwr_info->BW40_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 5G BW40-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
				tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
				if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
					&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW20_Diff[path][tx_idx])
				) {
					pwr_info->BW20_Diff[path][tx_idx] = tmp_diff;
					if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
						RTW_INFO("[%c] 5G BW20-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
				}
			}
			offset++;
		}
	}	

	/* OFDM diff 2T ~ 3T */
	if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, 1)) {
		val = map_read8(txpwr_map, offset);
		tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
		if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
			&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][1])
		) {
			pwr_info->OFDM_Diff[path][1] = tmp_diff;
			if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
				RTW_INFO("[%c] 5G OFDM-%dT diff:%d from %s\n", rf_path_char(path), 2, tmp_diff, pg_txpwr_src_str(txpwr_src));
		}
		if (HAL_SPEC_CHK_TX_CNT(hal_spec, 2)) {
			tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
			if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
				&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][2])
			) {
				pwr_info->OFDM_Diff[path][2] = tmp_diff;
				if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
					RTW_INFO("[%c] 5G OFDM-%dT diff:%d from %s\n", rf_path_char(path), 3, tmp_diff, pg_txpwr_src_str(txpwr_src));
			}
		}
	}
	offset++;

	/* OFDM diff 4T */
	if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, 3)) {
		val = map_read8(txpwr_map, offset);
		tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
		if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
			&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][3])
		) {
			pwr_info->OFDM_Diff[path][3] = tmp_diff;
			if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
				RTW_INFO("[%c] 5G OFDM-%dT diff:%d from %s\n", rf_path_char(path), 4, tmp_diff, pg_txpwr_src_str(txpwr_src));
		}
	}
	offset++;

	for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
		if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && HAL_SPEC_CHK_TX_CNT(hal_spec, tx_idx)) {
			val = map_read8(txpwr_map, offset);
			tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
			if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
				&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW80_Diff[path][tx_idx])
			) {
				pwr_info->BW80_Diff[path][tx_idx] = tmp_diff;
				if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
					RTW_INFO("[%c] 5G BW80-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
			}
			tmp_diff = PG_TXPWR_LSB_DIFF_TO_S8BIT(val);
			if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
				&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->BW160_Diff[path][tx_idx])
			) {
				pwr_info->BW160_Diff[path][tx_idx] = tmp_diff;
				if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
					RTW_INFO("[%c] 5G BW160-%dS diff:%d from %s\n", rf_path_char(path), tx_idx + 1, tmp_diff, pg_txpwr_src_str(txpwr_src));
			}
		}
		offset++;
	}

	if (offset != pg_offset + PG_TXPWR_1PATH_BYTE_NUM_5G) {
		RTW_ERR("%s parse %d bytes != %d\n", __func__, offset - pg_offset, PG_TXPWR_1PATH_BYTE_NUM_5G);
		rtw_warn_on(1);
	}

#endif /* #ifdef CONFIG_IEEE80211_BAND_5GHZ */

exit:
	return offset;
}

void hal_load_pg_txpwr_info(
	_adapter *adapter,
	TxPowerInfo24G *pwr_info_2g,
	TxPowerInfo5G *pwr_info_5g,
	u8 *pg_data,
	BOOLEAN AutoLoadFail
)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path;
	u16 pg_offset;
	u8 txpwr_src = PG_TXPWR_SRC_PG_DATA;
	struct map_t pg_data_map = MAP_ENT(184, 1, 0xFF, MAPSEG_PTR_ENT(0x00, 184, pg_data));
	const struct map_t *txpwr_map = NULL;

	/* init with invalid value and some dummy base and diff */
	hal_init_pg_txpwr_info_2g(adapter, pwr_info_2g);
	hal_init_pg_txpwr_info_5g(adapter, pwr_info_5g);

select_src:
	pg_offset = 0x10;

	switch (txpwr_src) {
	case PG_TXPWR_SRC_PG_DATA:
		txpwr_map = &pg_data_map;
		break;
	case PG_TXPWR_SRC_IC_DEF:
		txpwr_map = hal_pg_txpwr_def_info(adapter);
		break;
	case PG_TXPWR_SRC_DEF:
	default:
		txpwr_map = &pg_txpwr_def_info;
		break;
	};

	if (txpwr_map == NULL)
		goto end_parse;

	for (path = 0; path < MAX_RF_PATH ; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && !HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path))
			break;
		pg_offset = hal_load_pg_txpwr_info_path_2g(adapter, pwr_info_2g, path, txpwr_src, txpwr_map, pg_offset);
		pg_offset = hal_load_pg_txpwr_info_path_5g(adapter, pwr_info_5g, path, txpwr_src, txpwr_map, pg_offset);
	}

	if (hal_chk_pg_txpwr_info_2g(adapter, pwr_info_2g) == _SUCCESS
		&& hal_chk_pg_txpwr_info_5g(adapter, pwr_info_5g) == _SUCCESS)
		goto exit;

end_parse:
	txpwr_src++;
	if (txpwr_src < PG_TXPWR_SRC_NUM)
		goto select_src;

	if (hal_chk_pg_txpwr_info_2g(adapter, pwr_info_2g) != _SUCCESS
		|| hal_chk_pg_txpwr_info_5g(adapter, pwr_info_5g) != _SUCCESS)
		rtw_warn_on(1);

exit:
	if (DBG_PG_TXPWR_READ) {
		if (pwr_info_2g)
			dump_pg_txpwr_info_2g(RTW_DBGDUMP, pwr_info_2g, 4, 4);
		if (pwr_info_5g)
			dump_pg_txpwr_info_5g(RTW_DBGDUMP, pwr_info_5g, 4, 4);
	}

	return;
}

void hal_load_txpwr_info(
	_adapter *adapter,
	TxPowerInfo24G *pwr_info_2g,
	TxPowerInfo5G *pwr_info_5g,
	u8 *pg_data
)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 max_tx_cnt = hal_spec->max_tx_cnt;
	u8 rfpath, ch_idx, group, tx_idx;

	/* load from pg data (or default value) */
	hal_load_pg_txpwr_info(adapter, pwr_info_2g, pwr_info_5g, pg_data, _FALSE);

	/* transform to hal_data */
	for (rfpath = 0; rfpath < MAX_RF_PATH; rfpath++) {

		if (!pwr_info_2g || !HAL_SPEC_CHK_RF_PATH_2G(hal_spec, rfpath))
			goto bypass_2g;

		/* 2.4G base */
		for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++) {
			u8 cck_group;

			if (rtw_get_ch_group(ch_idx + 1, &group, &cck_group) != BAND_ON_2_4G)
				continue;

			hal_data->Index24G_CCK_Base[rfpath][ch_idx] = pwr_info_2g->IndexCCK_Base[rfpath][cck_group];
			hal_data->Index24G_BW40_Base[rfpath][ch_idx] = pwr_info_2g->IndexBW40_Base[rfpath][group];
		}

		/* 2.4G diff */
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			if (tx_idx >= max_tx_cnt)
				break;

			hal_data->CCK_24G_Diff[rfpath][tx_idx] = pwr_info_2g->CCK_Diff[rfpath][tx_idx];
			hal_data->OFDM_24G_Diff[rfpath][tx_idx] = pwr_info_2g->OFDM_Diff[rfpath][tx_idx];
			hal_data->BW20_24G_Diff[rfpath][tx_idx] = pwr_info_2g->BW20_Diff[rfpath][tx_idx];
			hal_data->BW40_24G_Diff[rfpath][tx_idx] = pwr_info_2g->BW40_Diff[rfpath][tx_idx];
		}
bypass_2g:
		;

#ifdef CONFIG_IEEE80211_BAND_5GHZ
		if (!pwr_info_5g || !HAL_SPEC_CHK_RF_PATH_5G(hal_spec, rfpath))
			goto bypass_5g;

		/* 5G base */
		for (ch_idx = 0; ch_idx < CENTER_CH_5G_ALL_NUM; ch_idx++) {
			if (rtw_get_ch_group(center_ch_5g_all[ch_idx], &group, NULL) != BAND_ON_5G)
				continue;
			hal_data->Index5G_BW40_Base[rfpath][ch_idx] = pwr_info_5g->IndexBW40_Base[rfpath][group];
		}

		for (ch_idx = 0 ; ch_idx < CENTER_CH_5G_80M_NUM; ch_idx++) {
			u8 upper, lower;

			if (rtw_get_ch_group(center_ch_5g_80m[ch_idx], &group, NULL) != BAND_ON_5G)
				continue;

			upper = pwr_info_5g->IndexBW40_Base[rfpath][group];
			lower = pwr_info_5g->IndexBW40_Base[rfpath][group + 1];
			hal_data->Index5G_BW80_Base[rfpath][ch_idx] = (upper + lower) / 2;
		}

		/* 5G diff */
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			if (tx_idx >= max_tx_cnt)
				break;

			hal_data->OFDM_5G_Diff[rfpath][tx_idx] = pwr_info_5g->OFDM_Diff[rfpath][tx_idx];
			hal_data->BW20_5G_Diff[rfpath][tx_idx] = pwr_info_5g->BW20_Diff[rfpath][tx_idx];
			hal_data->BW40_5G_Diff[rfpath][tx_idx] = pwr_info_5g->BW40_Diff[rfpath][tx_idx];
			hal_data->BW80_5G_Diff[rfpath][tx_idx] = pwr_info_5g->BW80_Diff[rfpath][tx_idx];
		}
bypass_5g:
		;
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
	}
}

void dump_hal_txpwr_info_2g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int path, ch_idx, tx_idx;

	RTW_PRINT_SEL(sel, "2.4G\n");
	RTW_PRINT_SEL(sel, "CCK-1T base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
		_RTW_PRINT_SEL(sel, "%2d ", center_ch_2g[ch_idx]);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
			_RTW_PRINT_SEL(sel, "%2u ", hal_data->Index24G_CCK_Base[path][ch_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "CCK diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dT ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->CCK_24G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW40-1S base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
		_RTW_PRINT_SEL(sel, "%2d ", center_ch_2g[ch_idx]);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
			_RTW_PRINT_SEL(sel, "%2u ", hal_data->Index24G_BW40_Base[path][ch_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "OFDM diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dT ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->OFDM_24G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW20 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dS ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->BW20_24G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW40 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dS ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->BW40_24G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");
}

void dump_hal_txpwr_info_5g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt)
{
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int path, ch_idx, tx_idx;
	u8 dump_section = 0;
	u8 ch_idx_s = 0;

	RTW_PRINT_SEL(sel, "5G\n");
	RTW_PRINT_SEL(sel, "BW40-1S base:\n");
	do {
		#define DUMP_5G_BW40_BASE_SECTION_NUM 3
		u8 end[DUMP_5G_BW40_BASE_SECTION_NUM] = {64, 144, 177};

		RTW_PRINT_SEL(sel, "%4s ", "");
		for (ch_idx = ch_idx_s; ch_idx < CENTER_CH_5G_ALL_NUM; ch_idx++) {
			_RTW_PRINT_SEL(sel, "%3d ", center_ch_5g_all[ch_idx]);
			if (end[dump_section] == center_ch_5g_all[ch_idx])
				break;
		}
		_RTW_PRINT_SEL(sel, "\n");
		for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
			RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
			for (ch_idx = ch_idx_s; ch_idx < CENTER_CH_5G_ALL_NUM; ch_idx++) {
				_RTW_PRINT_SEL(sel, "%3u ", hal_data->Index5G_BW40_Base[path][ch_idx]);
				if (end[dump_section] == center_ch_5g_all[ch_idx])
					break;
			}
			_RTW_PRINT_SEL(sel, "\n");
		}
		RTW_PRINT_SEL(sel, "\n");

		ch_idx_s = ch_idx + 1;
		dump_section++;
		if (dump_section >= DUMP_5G_BW40_BASE_SECTION_NUM)
			break;
	} while (1);

	RTW_PRINT_SEL(sel, "BW80-1S base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (ch_idx = 0; ch_idx < CENTER_CH_5G_80M_NUM; ch_idx++)
		_RTW_PRINT_SEL(sel, "%3d ", center_ch_5g_80m[ch_idx]);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (ch_idx = 0; ch_idx < CENTER_CH_5G_80M_NUM; ch_idx++)
			_RTW_PRINT_SEL(sel, "%3u ", hal_data->Index5G_BW80_Base[path][ch_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "OFDM diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dT ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->OFDM_5G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW20 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dS ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->BW20_5G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW40 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dS ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->BW40_5G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "BW80 diff:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
		_RTW_PRINT_SEL(sel, "%dS ", tx_idx + 1);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (tx_idx = RF_1TX; tx_idx < MAX_TX_COUNT && tx_idx < max_tx_cnt; tx_idx++)
			_RTW_PRINT_SEL(sel, "%2d ", hal_data->BW80_5G_Diff[path][tx_idx]);
		_RTW_PRINT_SEL(sel, "\n");
	}
	RTW_PRINT_SEL(sel, "\n");
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
}

/*
* rtw_regsty_get_target_tx_power -
*
* Return dBm or -1 for undefined
*/
s8 rtw_regsty_get_target_tx_power(
	IN	PADAPTER		Adapter,
	IN	u8				Band,
	IN	u8				RfPath,
	IN	RATE_SECTION	RateSection
)
{
	struct registry_priv *regsty = adapter_to_regsty(Adapter);
	s8 value = 0;

	if (RfPath > RF_PATH_D) {
		RTW_PRINT("%s invalid RfPath:%d\n", __func__, RfPath);
		return -1;
	}

	if (Band != BAND_ON_2_4G
		#ifdef CONFIG_IEEE80211_BAND_5GHZ
		&& Band != BAND_ON_5G
		#endif
	) {
		RTW_PRINT("%s invalid Band:%d\n", __func__, Band);
		return -1;
	}

	if (RateSection >= RATE_SECTION_NUM
		#ifdef CONFIG_IEEE80211_BAND_5GHZ
		|| (Band == BAND_ON_5G && RateSection == CCK)
		#endif
	) {
		RTW_PRINT("%s invalid RateSection:%d in Band:%d, RfPath:%d\n", __func__
			, RateSection, Band, RfPath);
		return -1;
	}

	if (Band == BAND_ON_2_4G)
		value = regsty->target_tx_pwr_2g[RfPath][RateSection];
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	else /* BAND_ON_5G */
		value = regsty->target_tx_pwr_5g[RfPath][RateSection - 1];
#endif

	return value;
}

bool rtw_regsty_chk_target_tx_power_valid(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int path, tx_num, band, rs;
	s8 target;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		for (path = 0; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				break;

			for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
				tx_num = rate_section_to_tx_num(rs);
				if (tx_num >= hal_spec->tx_nss_num)
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
					continue;

				target = rtw_regsty_get_target_tx_power(adapter, band, path, rs);
				if (target == -1) {
					RTW_PRINT("%s return _FALSE for band:%d, path:%d, rs:%d, t:%d\n", __func__, band, path, rs, target);
					return _FALSE;
				}
			}
		}
	}

	return _TRUE;
}

/*
* PHY_GetTxPowerByRateBase -
*
* Return 2 times of dBm
*/
u8
PHY_GetTxPowerByRateBase(
	IN	PADAPTER		Adapter,
	IN	u8				Band,
	IN	u8				RfPath,
	IN	u8				TxNum,
	IN	RATE_SECTION	RateSection
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u8 value = 0;

	if (RfPath > RF_PATH_D) {
		RTW_PRINT("%s invalid RfPath:%d\n", __func__, RfPath);
		return 0;
	}

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_PRINT("%s invalid Band:%d\n", __func__, Band);
		return 0;
	}

	if (RateSection >= RATE_SECTION_NUM
		|| (Band == BAND_ON_5G && RateSection == CCK)
	) {
		RTW_PRINT("%s invalid RateSection:%d in Band:%d, RfPath:%d, TxNum:%d\n", __func__
			, RateSection, Band, RfPath, TxNum);
		return 0;
	}

	if (Band == BAND_ON_2_4G)
		value = pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][RateSection];
	else /* BAND_ON_5G */
		value = pHalData->TxPwrByRateBase5G[RfPath][TxNum][RateSection - 1];

	return value;
}

VOID
phy_SetTxPowerByRateBase(
	IN	PADAPTER		Adapter,
	IN	u8				Band,
	IN	u8				RfPath,
	IN	RATE_SECTION	RateSection,
	IN	u8				TxNum,
	IN	u8				Value
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (RfPath > RF_PATH_D) {
		RTW_PRINT("%s invalid RfPath:%d\n", __func__, RfPath);
		return;
	}

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_PRINT("%s invalid Band:%d\n", __func__, Band);
		return;
	}

	if (RateSection >= RATE_SECTION_NUM
		|| (Band == BAND_ON_5G && RateSection == CCK)
	) {
		RTW_PRINT("%s invalid RateSection:%d in %sG, RfPath:%d, TxNum:%d\n", __func__
			, RateSection, (Band == BAND_ON_2_4G) ? "2.4" : "5", RfPath, TxNum);
		return;
	}

	if (Band == BAND_ON_2_4G)
		pHalData->TxPwrByRateBase2_4G[RfPath][TxNum][RateSection] = Value;
	else /* BAND_ON_5G */
		pHalData->TxPwrByRateBase5G[RfPath][TxNum][RateSection - 1] = Value;
}

static inline BOOLEAN phy_is_txpwr_by_rate_undefined_of_band_path(_adapter *adapter, u8 band, u8 path)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 tx_num = 0, rate_idx = 0;

	for (tx_num = 0; tx_num < TX_PWR_BY_RATE_NUM_RF; tx_num++) {
		if (tx_num >= hal_spec->max_tx_cnt || tx_num >= hal_spec->tx_nss_num)
			goto exit;
		for (rate_idx = 0; rate_idx < TX_PWR_BY_RATE_NUM_RATE; rate_idx++) {
			if (hal_data->TxPwrByRateOffset[band][path][tx_num][rate_idx] != 0)
				goto exit;
		}
	}

exit:
	return (tx_num >= hal_spec->max_tx_cnt || tx_num >= hal_spec->tx_nss_num) ? _TRUE : _FALSE;
}

static inline void phy_txpwr_by_rate_duplicate_band_path(_adapter *adapter, u8 band, u8 s_path, u8 t_path)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 tx_num = 0, rate_idx = 0;

	for (tx_num = 0; tx_num < TX_PWR_BY_RATE_NUM_RF; tx_num++)
		for (rate_idx = 0; rate_idx < TX_PWR_BY_RATE_NUM_RATE; rate_idx++)
			hal_data->TxPwrByRateOffset[band][t_path][tx_num][rate_idx] = hal_data->TxPwrByRateOffset[band][s_path][tx_num][rate_idx];
}

static void phy_txpwr_by_rate_chk_for_path_dup(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 band, path;
	s8 src_path;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++)
		for (path = RF_PATH_A; path < RF_PATH_MAX; path++)
			hal_data->txpwr_by_rate_undefined_band_path[band][path] = 0;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				continue;

			if (phy_is_txpwr_by_rate_undefined_of_band_path(adapter, band, path))
				hal_data->txpwr_by_rate_undefined_band_path[band][path] = 1;
		}
	}

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		src_path = -1;
		for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				continue;

			/* find src */
			if (src_path == -1 && hal_data->txpwr_by_rate_undefined_band_path[band][path] == 0)
				src_path = path;
		}

		if (src_path == -1) {
			RTW_ERR("%s all power by rate undefined\n", __func__);
			continue;
		}

		for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				continue;

			/* duplicate src to undefined one */
			if (hal_data->txpwr_by_rate_undefined_band_path[band][path] == 1) {
				RTW_INFO("%s duplicate %s [%c] to [%c]\n", __func__
					, band_str(band), rf_path_char(src_path), rf_path_char(path));
				phy_txpwr_by_rate_duplicate_band_path(adapter, band, src_path, path);
			}
		}
	}
}

VOID
phy_StoreTxPowerByRateBase(
	IN	PADAPTER	pAdapter
)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(pAdapter);
	struct registry_priv *regsty = adapter_to_regsty(pAdapter);

	u8 rate_sec_base[RATE_SECTION_NUM] = {
		MGN_11M,
		MGN_54M,
		MGN_MCS7,
		MGN_MCS15,
		MGN_MCS23,
		MGN_MCS31,
		MGN_VHT1SS_MCS7,
		MGN_VHT2SS_MCS7,
		MGN_VHT3SS_MCS7,
		MGN_VHT4SS_MCS7,
	};

	u8 band, path, rs, tx_num, base, index;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(pAdapter, band))
			continue;

		for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				break;

			for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
				tx_num = rate_section_to_tx_num(rs);
				if (tx_num >= hal_spec->tx_nss_num)
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(pAdapter))
					continue;

				if (regsty->target_tx_pwr_valid == _TRUE)
					base = 2 * rtw_regsty_get_target_tx_power(pAdapter, band, path, rs);
				else
					base = _PHY_GetTxPowerByRate(pAdapter, band, path, tx_num, rate_sec_base[rs]);
				phy_SetTxPowerByRateBase(pAdapter, band, path, rs, tx_num, base);
			}
		}
	}
}

VOID
PHY_GetRateValuesOfTxPowerByRate(
	IN	PADAPTER pAdapter,
	IN	u32 RegAddr,
	IN	u32 BitMask,
	IN	u32 Value,
	OUT	u8 *Rate,
	OUT	s8 *PwrByRateVal,
	OUT	u8 *RateNum
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &pHalData->odmpriv;
	u8				index = 0, i = 0;

	switch (RegAddr) {
	case rTxAGC_A_Rate18_06:
	case rTxAGC_B_Rate18_06:
		Rate[0] = MGN_6M;
		Rate[1] = MGN_9M;
		Rate[2] = MGN_12M;
		Rate[3] = MGN_18M;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Rate54_24:
	case rTxAGC_B_Rate54_24:
		Rate[0] = MGN_24M;
		Rate[1] = MGN_36M;
		Rate[2] = MGN_48M;
		Rate[3] = MGN_54M;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_CCK1_Mcs32:
		Rate[0] = MGN_1M;
		PwrByRateVal[0] = (s8)((((Value >> (8 + 4)) & 0xF)) * 10 +
				       ((Value >> 8) & 0xF));
		*RateNum = 1;
		break;

	case rTxAGC_B_CCK11_A_CCK2_11:
		if (BitMask == 0xffffff00) {
			Rate[0] = MGN_2M;
			Rate[1] = MGN_5_5M;
			Rate[2] = MGN_11M;
			for (i = 1; i < 4; ++i) {
				PwrByRateVal[i - 1] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
						   ((Value >> (i * 8)) & 0xF));
			}
			*RateNum = 3;
		} else if (BitMask == 0x000000ff) {
			Rate[0] = MGN_11M;
			PwrByRateVal[0] = (s8)((((Value >> 4) & 0xF)) * 10 +
					       (Value & 0xF));
			*RateNum = 1;
		}
		break;

	case rTxAGC_A_Mcs03_Mcs00:
	case rTxAGC_B_Mcs03_Mcs00:
		Rate[0] = MGN_MCS0;
		Rate[1] = MGN_MCS1;
		Rate[2] = MGN_MCS2;
		Rate[3] = MGN_MCS3;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs07_Mcs04:
	case rTxAGC_B_Mcs07_Mcs04:
		Rate[0] = MGN_MCS4;
		Rate[1] = MGN_MCS5;
		Rate[2] = MGN_MCS6;
		Rate[3] = MGN_MCS7;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs11_Mcs08:
	case rTxAGC_B_Mcs11_Mcs08:
		Rate[0] = MGN_MCS8;
		Rate[1] = MGN_MCS9;
		Rate[2] = MGN_MCS10;
		Rate[3] = MGN_MCS11;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs15_Mcs12:
	case rTxAGC_B_Mcs15_Mcs12:
		Rate[0] = MGN_MCS12;
		Rate[1] = MGN_MCS13;
		Rate[2] = MGN_MCS14;
		Rate[3] = MGN_MCS15;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;

		break;

	case rTxAGC_B_CCK1_55_Mcs32:
		Rate[0] = MGN_1M;
		Rate[1] = MGN_2M;
		Rate[2] = MGN_5_5M;
		for (i = 1; i < 4; ++i) {
			PwrByRateVal[i - 1] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
						   ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 3;
		break;

	case 0xC20:
	case 0xE20:
	case 0x1820:
	case 0x1a20:
		Rate[0] = MGN_1M;
		Rate[1] = MGN_2M;
		Rate[2] = MGN_5_5M;
		Rate[3] = MGN_11M;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC24:
	case 0xE24:
	case 0x1824:
	case 0x1a24:
		Rate[0] = MGN_6M;
		Rate[1] = MGN_9M;
		Rate[2] = MGN_12M;
		Rate[3] = MGN_18M;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC28:
	case 0xE28:
	case 0x1828:
	case 0x1a28:
		Rate[0] = MGN_24M;
		Rate[1] = MGN_36M;
		Rate[2] = MGN_48M;
		Rate[3] = MGN_54M;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC2C:
	case 0xE2C:
	case 0x182C:
	case 0x1a2C:
		Rate[0] = MGN_MCS0;
		Rate[1] = MGN_MCS1;
		Rate[2] = MGN_MCS2;
		Rate[3] = MGN_MCS3;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC30:
	case 0xE30:
	case 0x1830:
	case 0x1a30:
		Rate[0] = MGN_MCS4;
		Rate[1] = MGN_MCS5;
		Rate[2] = MGN_MCS6;
		Rate[3] = MGN_MCS7;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC34:
	case 0xE34:
	case 0x1834:
	case 0x1a34:
		Rate[0] = MGN_MCS8;
		Rate[1] = MGN_MCS9;
		Rate[2] = MGN_MCS10;
		Rate[3] = MGN_MCS11;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC38:
	case 0xE38:
	case 0x1838:
	case 0x1a38:
		Rate[0] = MGN_MCS12;
		Rate[1] = MGN_MCS13;
		Rate[2] = MGN_MCS14;
		Rate[3] = MGN_MCS15;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC3C:
	case 0xE3C:
	case 0x183C:
	case 0x1a3C:
		Rate[0] = MGN_VHT1SS_MCS0;
		Rate[1] = MGN_VHT1SS_MCS1;
		Rate[2] = MGN_VHT1SS_MCS2;
		Rate[3] = MGN_VHT1SS_MCS3;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC40:
	case 0xE40:
	case 0x1840:
	case 0x1a40:
		Rate[0] = MGN_VHT1SS_MCS4;
		Rate[1] = MGN_VHT1SS_MCS5;
		Rate[2] = MGN_VHT1SS_MCS6;
		Rate[3] = MGN_VHT1SS_MCS7;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC44:
	case 0xE44:
	case 0x1844:
	case 0x1a44:
		Rate[0] = MGN_VHT1SS_MCS8;
		Rate[1] = MGN_VHT1SS_MCS9;
		Rate[2] = MGN_VHT2SS_MCS0;
		Rate[3] = MGN_VHT2SS_MCS1;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC48:
	case 0xE48:
	case 0x1848:
	case 0x1a48:
		Rate[0] = MGN_VHT2SS_MCS2;
		Rate[1] = MGN_VHT2SS_MCS3;
		Rate[2] = MGN_VHT2SS_MCS4;
		Rate[3] = MGN_VHT2SS_MCS5;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xC4C:
	case 0xE4C:
	case 0x184C:
	case 0x1a4C:
		Rate[0] = MGN_VHT2SS_MCS6;
		Rate[1] = MGN_VHT2SS_MCS7;
		Rate[2] = MGN_VHT2SS_MCS8;
		Rate[3] = MGN_VHT2SS_MCS9;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCD8:
	case 0xED8:
	case 0x18D8:
	case 0x1aD8:
		Rate[0] = MGN_MCS16;
		Rate[1] = MGN_MCS17;
		Rate[2] = MGN_MCS18;
		Rate[3] = MGN_MCS19;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCDC:
	case 0xEDC:
	case 0x18DC:
	case 0x1aDC:
		Rate[0] = MGN_MCS20;
		Rate[1] = MGN_MCS21;
		Rate[2] = MGN_MCS22;
		Rate[3] = MGN_MCS23;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCE0:
	case 0xEE0:
	case 0x18E0:
	case 0x1aE0:
		Rate[0] = MGN_VHT3SS_MCS0;
		Rate[1] = MGN_VHT3SS_MCS1;
		Rate[2] = MGN_VHT3SS_MCS2;
		Rate[3] = MGN_VHT3SS_MCS3;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCE4:
	case 0xEE4:
	case 0x18E4:
	case 0x1aE4:
		Rate[0] = MGN_VHT3SS_MCS4;
		Rate[1] = MGN_VHT3SS_MCS5;
		Rate[2] = MGN_VHT3SS_MCS6;
		Rate[3] = MGN_VHT3SS_MCS7;
		for (i = 0; i < 4; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 4;
		break;

	case 0xCE8:
	case 0xEE8:
	case 0x18E8:
	case 0x1aE8:
		Rate[0] = MGN_VHT3SS_MCS8;
		Rate[1] = MGN_VHT3SS_MCS9;
		for (i = 0; i < 2; ++i) {
			PwrByRateVal[i] = (s8)((((Value >> (i * 8 + 4)) & 0xF)) * 10 +
					       ((Value >> (i * 8)) & 0xF));
		}
		*RateNum = 2;
		break;

	default:
		RTW_PRINT("Invalid RegAddr 0x%x in %s()\n", RegAddr, __func__);
		break;
	};
}

void
PHY_StoreTxPowerByRateNew(
	IN	PADAPTER	pAdapter,
	IN	u32			Band,
	IN	u32			RfPath,
	IN	u32			TxNum,
	IN	u32			RegAddr,
	IN	u32			BitMask,
	IN	u32			Data
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	u8	i = 0, rates[4] = {0}, rateNum = 0;
	s8	PwrByRateVal[4] = {0};

	PHY_GetRateValuesOfTxPowerByRate(pAdapter, RegAddr, BitMask, Data, rates, PwrByRateVal, &rateNum);

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_PRINT("Invalid Band %d\n", Band);
		return;
	}

	if (RfPath > ODM_RF_PATH_D) {
		RTW_PRINT("Invalid RfPath %d\n", RfPath);
		return;
	}

	if (TxNum > ODM_RF_PATH_D) {
		RTW_PRINT("Invalid TxNum %d\n", TxNum);
		return;
	}

	for (i = 0; i < rateNum; ++i) {
		u8 rate_idx = PHY_GetRateIndexOfTxPowerByRate(rates[i]);

		if (IS_1T_RATE(rates[i]))
			pHalData->TxPwrByRateOffset[Band][RfPath][RF_1TX][rate_idx] = PwrByRateVal[i];
		else if (IS_2T_RATE(rates[i]))
			pHalData->TxPwrByRateOffset[Band][RfPath][RF_2TX][rate_idx] = PwrByRateVal[i];
		else if (IS_3T_RATE(rates[i]))
			pHalData->TxPwrByRateOffset[Band][RfPath][RF_3TX][rate_idx] = PwrByRateVal[i];
		else if (IS_4T_RATE(rates[i]))
			pHalData->TxPwrByRateOffset[Band][RfPath][RF_4TX][rate_idx] = PwrByRateVal[i];
		else
			rtw_warn_on(1);
	}
}

VOID
PHY_InitTxPowerByRate(
	IN	PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	band = 0, rfPath = 0, TxNum = 0, rate = 0, i = 0, j = 0;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (rfPath = 0; rfPath < TX_PWR_BY_RATE_NUM_RF; ++rfPath)
			for (TxNum = 0; TxNum < TX_PWR_BY_RATE_NUM_RF; ++TxNum)
				for (rate = 0; rate < TX_PWR_BY_RATE_NUM_RATE; ++rate)
					pHalData->TxPwrByRateOffset[band][rfPath][TxNum][rate] = 0;
}

VOID
phy_store_tx_power_by_rate(
	IN	PADAPTER	pAdapter,
	IN	u32			Band,
	IN	u32			RfPath,
	IN	u32			TxNum,
	IN	u32			RegAddr,
	IN	u32			BitMask,
	IN	u32			Data
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &pHalData->odmpriv;

	if (pDM_Odm->phy_reg_pg_version > 0)
		PHY_StoreTxPowerByRateNew(pAdapter, Band, RfPath, TxNum, RegAddr, BitMask, Data);
	else
		RTW_INFO("Invalid PHY_REG_PG.txt version %d\n",  pDM_Odm->phy_reg_pg_version);

}

VOID
phy_ConvertTxPowerByRateInDbmToRelativeValues(
	IN	PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8			base = 0, i = 0, value = 0,
				band = 0, path = 0, txNum = 0, index = 0,
				startIndex = 0, endIndex = 0;
	u8	cckRates[4] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M},
		ofdmRates[8] = {MGN_6M, MGN_9M, MGN_12M, MGN_18M, MGN_24M, MGN_36M, MGN_48M, MGN_54M},
		mcs0_7Rates[8] = {MGN_MCS0, MGN_MCS1, MGN_MCS2, MGN_MCS3, MGN_MCS4, MGN_MCS5, MGN_MCS6, MGN_MCS7},
		mcs8_15Rates[8] = {MGN_MCS8, MGN_MCS9, MGN_MCS10, MGN_MCS11, MGN_MCS12, MGN_MCS13, MGN_MCS14, MGN_MCS15},
		mcs16_23Rates[8] = {MGN_MCS16, MGN_MCS17, MGN_MCS18, MGN_MCS19, MGN_MCS20, MGN_MCS21, MGN_MCS22, MGN_MCS23},
		vht1ssRates[10] = {MGN_VHT1SS_MCS0, MGN_VHT1SS_MCS1, MGN_VHT1SS_MCS2, MGN_VHT1SS_MCS3, MGN_VHT1SS_MCS4,
			MGN_VHT1SS_MCS5, MGN_VHT1SS_MCS6, MGN_VHT1SS_MCS7, MGN_VHT1SS_MCS8, MGN_VHT1SS_MCS9},
		vht2ssRates[10] = {MGN_VHT2SS_MCS0, MGN_VHT2SS_MCS1, MGN_VHT2SS_MCS2, MGN_VHT2SS_MCS3, MGN_VHT2SS_MCS4,
			MGN_VHT2SS_MCS5, MGN_VHT2SS_MCS6, MGN_VHT2SS_MCS7, MGN_VHT2SS_MCS8, MGN_VHT2SS_MCS9},
		vht3ssRates[10] = {MGN_VHT3SS_MCS0, MGN_VHT3SS_MCS1, MGN_VHT3SS_MCS2, MGN_VHT3SS_MCS3, MGN_VHT3SS_MCS4,
			MGN_VHT3SS_MCS5, MGN_VHT3SS_MCS6, MGN_VHT3SS_MCS7, MGN_VHT3SS_MCS8, MGN_VHT3SS_MCS9};

	/* RTW_INFO("===>PHY_ConvertTxPowerByRateInDbmToRelativeValues()\n" ); */

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band) {
		for (path = ODM_RF_PATH_A; path <= ODM_RF_PATH_D; ++path) {
			for (txNum = RF_1TX; txNum < RF_MAX_TX_NUM; ++txNum) {
				/* CCK */
				if (band == BAND_ON_2_4G) {
					base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, CCK);
					for (i = 0; i < sizeof(cckRates); ++i) {
						value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, cckRates[i]);
						PHY_SetTxPowerByRate(pAdapter, band, path, txNum, cckRates[i], value - base);
					}
				}

				/* OFDM */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, OFDM);
				for (i = 0; i < sizeof(ofdmRates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, ofdmRates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, ofdmRates[i], value - base);
				}

				/* HT MCS0~7 */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, HT_1SS);
				for (i = 0; i < sizeof(mcs0_7Rates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, mcs0_7Rates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, mcs0_7Rates[i], value - base);
				}

				/* HT MCS8~15 */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, HT_2SS);
				for (i = 0; i < sizeof(mcs8_15Rates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, mcs8_15Rates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, mcs8_15Rates[i], value - base);
				}

				/* HT MCS16~23 */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, HT_3SS);
				for (i = 0; i < sizeof(mcs16_23Rates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, mcs16_23Rates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, mcs16_23Rates[i], value - base);
				}

				/* VHT 1SS */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, VHT_1SS);
				for (i = 0; i < sizeof(vht1ssRates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, vht1ssRates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, vht1ssRates[i], value - base);
				}

				/* VHT 2SS */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, VHT_2SS);
				for (i = 0; i < sizeof(vht2ssRates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, vht2ssRates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, vht2ssRates[i], value - base);
				}

				/* VHT 3SS */
				base = PHY_GetTxPowerByRateBase(pAdapter, band, path, txNum, VHT_3SS);
				for (i = 0; i < sizeof(vht3ssRates); ++i) {
					value = PHY_GetTxPowerByRate(pAdapter, band, path, txNum, vht3ssRates[i]);
					PHY_SetTxPowerByRate(pAdapter, band, path, txNum, vht3ssRates[i], value - base);
				}
			}
		}
	}

	/* RTW_INFO("<===PHY_ConvertTxPowerByRateInDbmToRelativeValues()\n" ); */
}

/*
  * This function must be called if the value in the PHY_REG_PG.txt(or header)
  * is exact dBm values
  */
VOID
PHY_TxPowerByRateConfiguration(
	IN  PADAPTER			pAdapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	phy_txpwr_by_rate_chk_for_path_dup(pAdapter);
	phy_StoreTxPowerByRateBase(pAdapter);
	phy_ConvertTxPowerByRateInDbmToRelativeValues(pAdapter);
}

VOID
phy_set_tx_power_index_by_rate_section(
	IN	PADAPTER		pAdapter,
	IN	u8				RFPath,
	IN	u8				Channel,
	IN	u8				RateSection
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(pAdapter);

	if (RateSection >= RATE_SECTION_NUM) {
		RTW_INFO("Invalid RateSection %d in %s", RateSection, __func__);
		rtw_warn_on(1);
		goto exit;
	}

	if (RateSection == CCK && pHalData->current_band_type != BAND_ON_2_4G)
		goto exit;

	PHY_SetTxPowerIndexByRateArray(pAdapter, RFPath, pHalData->current_channel_bw, Channel,
		rates_by_sections[RateSection].rates, rates_by_sections[RateSection].rate_num);

exit:
	return;
}

BOOLEAN
phy_GetChnlIndex(
	IN	u8	Channel,
	OUT u8	*ChannelIdx
)
{
	u8  i = 0;
	BOOLEAN bIn24G = _TRUE;

	if (Channel <= 14) {
		bIn24G = _TRUE;
		*ChannelIdx = Channel - 1;
	} else {
		bIn24G = _FALSE;

		for (i = 0; i < CENTER_CH_5G_ALL_NUM; ++i) {
			if (center_ch_5g_all[i] == Channel) {
				*ChannelIdx = i;
				return bIn24G;
			}
		}
	}

	return bIn24G;
}

u8
PHY_GetTxPowerIndexBase(
	IN	PADAPTER		pAdapter,
	IN	u8				RFPath,
	IN	u8				Rate,
	IN	CHANNEL_WIDTH	BandWidth,
	IN	u8				Channel,
	OUT PBOOLEAN		bIn24G
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	struct PHY_DM_STRUCT			*pDM_Odm = &pHalData->odmpriv;
	u8					i = 0;	/* default set to 1S */
	u8					txPower = 0;
	u8					chnlIdx = (Channel - 1);

	if (HAL_IsLegalChannel(pAdapter, Channel) == _FALSE) {
		chnlIdx = 0;
		RTW_INFO("Illegal channel!!\n");
	}

	*bIn24G = phy_GetChnlIndex(Channel, &chnlIdx);

	/* RTW_INFO("[%s] Channel Index: %d\n", (*bIn24G?"2.4G":"5G"), chnlIdx); */

	if (*bIn24G) { /* 3 ============================== 2.4 G ============================== */
		if (IS_CCK_RATE(Rate))
			txPower = pHalData->Index24G_CCK_Base[RFPath][chnlIdx];
		else if (MGN_6M <= Rate)
			txPower = pHalData->Index24G_BW40_Base[RFPath][chnlIdx];
		else
			RTW_INFO("PHY_GetTxPowerIndexBase: INVALID Rate(0x%02x).\n", Rate);

		/* RTW_INFO("Base Tx power(RF-%c, Rate #%d, Channel Index %d) = 0x%X\n",  */
		/*		((RFPath==0)?'A':'B'), Rate, chnlIdx, txPower); */

		/* OFDM-1T */
		if ((MGN_6M <= Rate && Rate <= MGN_54M) && !IS_CCK_RATE(Rate)) {
			txPower += pHalData->OFDM_24G_Diff[RFPath][TX_1S];
			/* RTW_INFO("+PowerDiff 2.4G (RF-%c): (OFDM-1T) = (%d)\n", ((RFPath==0)?'A':'B'), pHalData->OFDM_24G_Diff[RFPath][TX_1S]); */
		}
		/* BW20-1S, BW20-2S */
		if (BandWidth == CHANNEL_WIDTH_20) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_24G_Diff[RFPath][TX_4S];

			/* RTW_INFO("+PowerDiff 2.4G (RF-%c): (BW20-1S, BW20-2S, BW20-3S, BW20-4S) = (%d, %d, %d, %d)\n", ((RFPath==0)?'A':(RFPath==1)?'B':(RFPath==2)?'C':'D'),  */
			/*	pHalData->BW20_24G_Diff[RFPath][TX_1S], pHalData->BW20_24G_Diff[RFPath][TX_2S],  */
			/*	pHalData->BW20_24G_Diff[RFPath][TX_3S], pHalData->BW20_24G_Diff[RFPath][TX_4S]); */
		}
		/* BW40-1S, BW40-2S */
		else if (BandWidth == CHANNEL_WIDTH_40) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_4S];

			/* RTW_INFO("+PowerDiff 2.4G (RF-%c): (BW40-1S, BW40-2S, BW40-3S, BW40-4S) = (%d, %d, %d, %d)\n", ((RFPath==0)?'A':(RFPath==1)?'B':(RFPath==2)?'C':'D'),  */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_1S], pHalData->BW40_24G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_3S], pHalData->BW40_24G_Diff[RFPath][TX_4S]); */
		}
		/* Willis suggest adopt BW 40M power index while in BW 80 mode */
		else if (BandWidth == CHANNEL_WIDTH_80) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_24G_Diff[RFPath][TX_4S];

			/* RTW_INFO("+PowerDiff 2.4G (RF-%c): (BW40-1S, BW40-2S, BW40-3S, BW40-4T) = (%d, %d, %d, %d) P.S. Current is in BW 80MHz\n", ((RFPath==0)?'A':(RFPath==1)?'B':(RFPath==2)?'C':'D'),  */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_1S], pHalData->BW40_24G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW40_24G_Diff[RFPath][TX_3S], pHalData->BW40_24G_Diff[RFPath][TX_4S]); */
		}
	}
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	else { /* 3 ============================== 5 G ============================== */
		if (MGN_6M <= Rate)
			txPower = pHalData->Index5G_BW40_Base[RFPath][chnlIdx];
		else
			RTW_INFO("===>PHY_GetTxPowerIndexBase: INVALID Rate(0x%02x).\n", Rate);

		/* RTW_INFO("Base Tx power(RF-%c, Rate #%d, Channel Index %d) = 0x%X\n",  */
		/*	((RFPath==0)?'A':'B'), Rate, chnlIdx, txPower); */

		/* OFDM-1T */
		if ((MGN_6M <= Rate && Rate <= MGN_54M) && !IS_CCK_RATE(Rate)) {
			txPower += pHalData->OFDM_5G_Diff[RFPath][TX_1S];
			/* RTW_INFO("+PowerDiff 5G (RF-%c): (OFDM-1T) = (%d)\n", ((RFPath==0)?'A':'B'), pHalData->OFDM_5G_Diff[RFPath][TX_1S]); */
		}

		/* BW20-1S, BW20-2S */
		if (BandWidth == CHANNEL_WIDTH_20) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW20_5G_Diff[RFPath][TX_4S];

			/* RTW_INFO("+PowerDiff 5G (RF-%c): (BW20-1S, BW20-2S, BW20-3S, BW20-4S) = (%d, %d, %d, %d)\n", ((RFPath==0)?'A':(RFPath==1)?'B':(RFPath==2)?'C':'D'),  */
			/*	pHalData->BW20_5G_Diff[RFPath][TX_1S], pHalData->BW20_5G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW20_5G_Diff[RFPath][TX_3S], pHalData->BW20_5G_Diff[RFPath][TX_4S]); */
		}
		/* BW40-1S, BW40-2S */
		else if (BandWidth == CHANNEL_WIDTH_40) {
			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_3S];
			if ((MGN_MCS24 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW40_5G_Diff[RFPath][TX_4S];

			/* RTW_INFO("+PowerDiff 5G(RF-%c): (BW40-1S, BW40-2S) = (%d, %d, %d, %d)\n", ((RFPath==0)?'A':(RFPath==1)?'B':(RFPath==2)?'C':'D'),  */
			/*	pHalData->BW40_5G_Diff[RFPath][TX_1S], pHalData->BW40_5G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW40_5G_Diff[RFPath][TX_3S], pHalData->BW40_5G_Diff[RFPath][TX_4S]); */
		}
		/* BW80-1S, BW80-2S */
		else if (BandWidth == CHANNEL_WIDTH_80) {
			/* <20121220, Kordan> Get the index of array "Index5G_BW80_Base". */
			for (i = 0; i < CENTER_CH_5G_80M_NUM; ++i)
				if (center_ch_5g_80m[i] == Channel)
					chnlIdx = i;

			txPower = pHalData->Index5G_BW80_Base[RFPath][chnlIdx];

			if ((MGN_MCS0 <= Rate && Rate <= MGN_MCS31)  || (MGN_VHT1SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += + pHalData->BW80_5G_Diff[RFPath][TX_1S];
			if ((MGN_MCS8 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT2SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW80_5G_Diff[RFPath][TX_2S];
			if ((MGN_MCS16 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT3SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW80_5G_Diff[RFPath][TX_3S];
			if ((MGN_MCS23 <= Rate && Rate <= MGN_MCS31) || (MGN_VHT4SS_MCS0 <= Rate && Rate <= MGN_VHT4SS_MCS9))
				txPower += pHalData->BW80_5G_Diff[RFPath][TX_4S];

			/* RTW_INFO("+PowerDiff 5G(RF-%c): (BW80-1S, BW80-2S, BW80-3S, BW80-4S) = (%d, %d, %d, %d)\n",((RFPath==0)?'A':(RFPath==1)?'B':(RFPath==2)?'C':'D'),  */
			/*	pHalData->BW80_5G_Diff[RFPath][TX_1S], pHalData->BW80_5G_Diff[RFPath][TX_2S], */
			/*	pHalData->BW80_5G_Diff[RFPath][TX_3S], pHalData->BW80_5G_Diff[RFPath][TX_4S]); */
		}
	}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

	return txPower;
}

s8
PHY_GetTxPowerTrackingOffset(
	PADAPTER	pAdapter,
	u8			RFPath,
	u8			Rate
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	struct PHY_DM_STRUCT			*pDM_Odm = &pHalData->odmpriv;
	s8	offset = 0;

	if (pDM_Odm->rf_calibrate_info.txpowertrack_control  == _FALSE)
		return offset;

	if ((Rate == MGN_1M) || (Rate == MGN_2M) || (Rate == MGN_5_5M) || (Rate == MGN_11M)) {
		offset = pDM_Odm->rf_calibrate_info.remnant_cck_swing_idx;
		/*RTW_INFO("+Remnant_CCKSwingIdx = 0x%x\n", RFPath, Rate, pRFCalibrateInfo->Remnant_CCKSwingIdx);*/
	} else {
		offset = pDM_Odm->rf_calibrate_info.remnant_ofdm_swing_idx[RFPath];
		/*RTW_INFO("+Remanant_OFDMSwingIdx[RFPath %u][Rate 0x%x] = 0x%x\n", RFPath, Rate, pRFCalibrateInfo->Remnant_OFDMSwingIdx[RFPath]);	*/

	}

	return offset;
}

/*The same as MRateToHwRate in hal_com.c*/
u8
PHY_GetRateIndexOfTxPowerByRate(
	IN	u8		Rate
)
{
	u8	index = 0;
	switch (Rate) {
	case MGN_1M:
		index = 0;
		break;
	case MGN_2M:
		index = 1;
		break;
	case MGN_5_5M:
		index = 2;
		break;
	case MGN_11M:
		index = 3;
		break;
	case MGN_6M:
		index = 4;
		break;
	case MGN_9M:
		index = 5;
		break;
	case MGN_12M:
		index = 6;
		break;
	case MGN_18M:
		index = 7;
		break;
	case MGN_24M:
		index = 8;
		break;
	case MGN_36M:
		index = 9;
		break;
	case MGN_48M:
		index = 10;
		break;
	case MGN_54M:
		index = 11;
		break;
	case MGN_MCS0:
		index = 12;
		break;
	case MGN_MCS1:
		index = 13;
		break;
	case MGN_MCS2:
		index = 14;
		break;
	case MGN_MCS3:
		index = 15;
		break;
	case MGN_MCS4:
		index = 16;
		break;
	case MGN_MCS5:
		index = 17;
		break;
	case MGN_MCS6:
		index = 18;
		break;
	case MGN_MCS7:
		index = 19;
		break;
	case MGN_MCS8:
		index = 20;
		break;
	case MGN_MCS9:
		index = 21;
		break;
	case MGN_MCS10:
		index = 22;
		break;
	case MGN_MCS11:
		index = 23;
		break;
	case MGN_MCS12:
		index = 24;
		break;
	case MGN_MCS13:
		index = 25;
		break;
	case MGN_MCS14:
		index = 26;
		break;
	case MGN_MCS15:
		index = 27;
		break;
	case MGN_MCS16:
		index = 28;
		break;
	case MGN_MCS17:
		index = 29;
		break;
	case MGN_MCS18:
		index = 30;
		break;
	case MGN_MCS19:
		index = 31;
		break;
	case MGN_MCS20:
		index = 32;
		break;
	case MGN_MCS21:
		index = 33;
		break;
	case MGN_MCS22:
		index = 34;
		break;
	case MGN_MCS23:
		index = 35;
		break;
	case MGN_MCS24:
		index = 36;
		break;
	case MGN_MCS25:
		index = 37;
		break;
	case MGN_MCS26:
		index = 38;
		break;
	case MGN_MCS27:
		index = 39;
		break;
	case MGN_MCS28:
		index = 40;
		break;
	case MGN_MCS29:
		index = 41;
		break;
	case MGN_MCS30:
		index = 42;
		break;
	case MGN_MCS31:
		index = 43;
		break;
	case MGN_VHT1SS_MCS0:
		index = 44;
		break;
	case MGN_VHT1SS_MCS1:
		index = 45;
		break;
	case MGN_VHT1SS_MCS2:
		index = 46;
		break;
	case MGN_VHT1SS_MCS3:
		index = 47;
		break;
	case MGN_VHT1SS_MCS4:
		index = 48;
		break;
	case MGN_VHT1SS_MCS5:
		index = 49;
		break;
	case MGN_VHT1SS_MCS6:
		index = 50;
		break;
	case MGN_VHT1SS_MCS7:
		index = 51;
		break;
	case MGN_VHT1SS_MCS8:
		index = 52;
		break;
	case MGN_VHT1SS_MCS9:
		index = 53;
		break;
	case MGN_VHT2SS_MCS0:
		index = 54;
		break;
	case MGN_VHT2SS_MCS1:
		index = 55;
		break;
	case MGN_VHT2SS_MCS2:
		index = 56;
		break;
	case MGN_VHT2SS_MCS3:
		index = 57;
		break;
	case MGN_VHT2SS_MCS4:
		index = 58;
		break;
	case MGN_VHT2SS_MCS5:
		index = 59;
		break;
	case MGN_VHT2SS_MCS6:
		index = 60;
		break;
	case MGN_VHT2SS_MCS7:
		index = 61;
		break;
	case MGN_VHT2SS_MCS8:
		index = 62;
		break;
	case MGN_VHT2SS_MCS9:
		index = 63;
		break;
	case MGN_VHT3SS_MCS0:
		index = 64;
		break;
	case MGN_VHT3SS_MCS1:
		index = 65;
		break;
	case MGN_VHT3SS_MCS2:
		index = 66;
		break;
	case MGN_VHT3SS_MCS3:
		index = 67;
		break;
	case MGN_VHT3SS_MCS4:
		index = 68;
		break;
	case MGN_VHT3SS_MCS5:
		index = 69;
		break;
	case MGN_VHT3SS_MCS6:
		index = 70;
		break;
	case MGN_VHT3SS_MCS7:
		index = 71;
		break;
	case MGN_VHT3SS_MCS8:
		index = 72;
		break;
	case MGN_VHT3SS_MCS9:
		index = 73;
		break;
	case MGN_VHT4SS_MCS0:
		index = 74;
		break;
	case MGN_VHT4SS_MCS1:
		index = 75;
		break;
	case MGN_VHT4SS_MCS2:
		index = 76;
		break;
	case MGN_VHT4SS_MCS3:
		index = 77;
		break;
	case MGN_VHT4SS_MCS4:
		index = 78;
		break;
	case MGN_VHT4SS_MCS5:
		index = 79;
		break;
	case MGN_VHT4SS_MCS6:
		index = 80;
		break;
	case MGN_VHT4SS_MCS7:
		index = 81;
		break;
	case MGN_VHT4SS_MCS8:
		index = 82;
		break;
	case MGN_VHT4SS_MCS9:
		index = 83;
		break;
	default:
		RTW_INFO("Invalid rate 0x%x in %s\n", Rate, __FUNCTION__);
		break;
	};

	return index;
}

s8
_PHY_GetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			Rate
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	s8 value = 0;
	u8 rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_INFO("Invalid band %d in %s\n", Band, __func__);
		goto exit;
	}
	if (RFPath > ODM_RF_PATH_D) {
		RTW_INFO("Invalid RfPath %d in %s\n", RFPath, __func__);
		goto exit;
	}
	if (TxNum >= RF_MAX_TX_NUM) {
		RTW_INFO("Invalid TxNum %d in %s\n", TxNum, __func__);
		goto exit;
	}
	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE) {
		RTW_INFO("Invalid RateIndex %d in %s\n", rateIndex, __func__);
		goto exit;
	}

	value = pHalData->TxPwrByRateOffset[Band][RFPath][TxNum][rateIndex];

exit:
	return value;
}


s8
PHY_GetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			Rate
)
{
	if (!phy_is_tx_power_by_rate_needed(pAdapter))
		return 0;

	return _PHY_GetTxPowerByRate(pAdapter, Band, RFPath, TxNum, Rate);
}

#ifdef CONFIG_PHYDM_POWERTRACK_BY_TSSI
s8
PHY_GetTxPowerByRateOriginal(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			Rate
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	s8	value = 0, limit = 0;
	u8	rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if ((pAdapter->registrypriv.RegEnableTxPowerByRate == 2 && pHalData->EEPROMRegulatory == 2) ||
		pAdapter->registrypriv.RegEnableTxPowerByRate == 0)
		return 0;

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		DBG_871X("Invalid band %d in %s\n", Band, __func__);
		return value;
	}
	if (RFPath > ODM_RF_PATH_D) {
		DBG_871X("Invalid RfPath %d in %s\n", RFPath, __func__);
		return value;
	}
	if (TxNum >= RF_MAX_TX_NUM) {
		DBG_871X("Invalid TxNum %d in %s\n", TxNum, __func__);
		return value;
	}
	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE) {
		DBG_871X("Invalid RateIndex %d in %s\n", rateIndex, __func__);
		return value;
	}

	value = pHalData->TxPwrByRate[Band][RFPath][TxNum][rateIndex];

	return value;
}

#endif


VOID
PHY_SetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			Rate,
	IN	s8			Value
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u8	rateIndex = PHY_GetRateIndexOfTxPowerByRate(Rate);

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_INFO("Invalid band %d in %s\n", Band, __FUNCTION__);
		return;
	}
	if (RFPath > ODM_RF_PATH_D) {
		RTW_INFO("Invalid RfPath %d in %s\n", RFPath, __FUNCTION__);
		return;
	}
	if (TxNum >= RF_MAX_TX_NUM) {
		RTW_INFO("Invalid TxNum %d in %s\n", TxNum, __FUNCTION__);
		return;
	}
	if (rateIndex >= TX_PWR_BY_RATE_NUM_RATE) {
		RTW_INFO("Invalid RateIndex %d in %s\n", rateIndex, __FUNCTION__);
		return;
	}

	pHalData->TxPwrByRateOffset[Band][RFPath][TxNum][rateIndex] = Value;
}

VOID
phy_set_tx_power_level_by_path(
	IN	PADAPTER	Adapter,
	IN	u8			channel,
	IN	u8			path
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	BOOLEAN bIsIn24G = (pHalData->current_band_type == BAND_ON_2_4G);

	/* if ( pMgntInfo->RegNByteAccess == 0 ) */
	{
		if (bIsIn24G)
			phy_set_tx_power_index_by_rate_section(Adapter, path, channel, CCK);

		phy_set_tx_power_index_by_rate_section(Adapter, path, channel, OFDM);
		phy_set_tx_power_index_by_rate_section(Adapter, path, channel, HT_MCS0_MCS7);

		if (IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_8814A(Adapter))
			phy_set_tx_power_index_by_rate_section(Adapter, path, channel, VHT_1SSMCS0_1SSMCS9);

		if (pHalData->NumTotalRFPath >= 2) {
			phy_set_tx_power_index_by_rate_section(Adapter, path, channel, HT_MCS8_MCS15);

			if (IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_8814A(Adapter))
				phy_set_tx_power_index_by_rate_section(Adapter, path, channel, VHT_2SSMCS0_2SSMCS9);

			if (IS_HARDWARE_TYPE_8814A(Adapter)) {
				phy_set_tx_power_index_by_rate_section(Adapter, path, channel, HT_MCS16_MCS23);
				phy_set_tx_power_index_by_rate_section(Adapter, path, channel, VHT_3SSMCS0_3SSMCS9);
			}
		}
	}
}

#ifndef DBG_TX_POWER_IDX
#define DBG_TX_POWER_IDX 0
#endif

VOID
PHY_SetTxPowerIndexByRateArray(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	CHANNEL_WIDTH		BandWidth,
	IN	u8					Channel,
	IN	u8					*Rates,
	IN	u8					RateArraySize
)
{
	u32	powerIndex = 0;
	int	i = 0;

	for (i = 0; i < RateArraySize; ++i) {
#if DBG_TX_POWER_IDX
		struct txpwr_idx_comp tic;

		powerIndex = rtw_hal_get_tx_power_index(pAdapter, RFPath, Rates[i], BandWidth, Channel, &tic);
		RTW_INFO("TXPWR: [%c][%s]ch:%u, %s, pwr_idx:%u = %u + (%d=%d:%d) + (%d) + (%d)\n"
			, rf_path_char(RFPath), ch_width_str(BandWidth), Channel, MGN_RATE_STR(Rates[i])
			, powerIndex, tic.base, (tic.by_rate > tic.limit ? tic.limit : tic.by_rate), tic.by_rate, tic.limit, tic.tpt, tic.ebias);
#else
		powerIndex = phy_get_tx_power_index(pAdapter, RFPath, Rates[i], BandWidth, Channel);
#endif
		PHY_SetTxPowerIndex(pAdapter, powerIndex, RFPath, Rates[i]);
	}
}

s8
phy_GetWorldWideLimit(
	s8 *LimitTable
)
{
	s8	min = LimitTable[0];
	u8	i = 0;

	for (i = 0; i < MAX_REGULATION_NUM; ++i) {
		if (LimitTable[i] < min)
			min = LimitTable[i];
	}

	return min;
}

s8
phy_GetChannelIndexOfTxPowerLimit(
	IN	u8			Band,
	IN	u8			Channel
)
{
	s8	channelIndex = -1;
	u8	i = 0;

	if (Band == BAND_ON_2_4G)
		channelIndex = Channel - 1;
	else if (Band == BAND_ON_5G) {
		for (i = 0; i < CENTER_CH_5G_ALL_NUM; ++i) {
			if (center_ch_5g_all[i] == Channel)
				channelIndex = i;
		}
	} else
		RTW_PRINT("Invalid Band %d in %s\n", Band, __func__);

	if (channelIndex == -1)
		RTW_PRINT("Invalid Channel %d of Band %d in %s\n", Channel, Band, __func__);

	return channelIndex;
}

static s8 _phy_get_txpwr_lmt(
	IN	PADAPTER			Adapter,
	IN	u32					RegPwrTblSel,
	IN	BAND_TYPE			Band,
	IN	CHANNEL_WIDTH		Bandwidth,
	IN	u8					RfPath,
	IN	u8					DataRate,
	IN	u8					Channel,
	BOOLEAN no_sc
)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(Adapter);
	s8 regulation = -1, bw = -1, rs = -1;
	u8 cch = 0;
	u8 bw_bmp = 0;
	s8 min_lmt = MAX_POWER_INDEX;
	s8 tmp_lmt;
	u8 final_bw = Bandwidth, final_cch = Channel;

	if ((Adapter->registrypriv.RegEnableTxPowerLimit == 2 && hal_data->EEPROMRegulatory != 1) ||
		Adapter->registrypriv.RegEnableTxPowerLimit == 0)
		goto exit;

	switch (RegPwrTblSel) {
	case 1:
		regulation = TXPWR_LMT_ETSI;
		break;
	case 2:
		regulation = TXPWR_LMT_MKK;
		break;
	case 3:
		regulation = TXPWR_LMT_FCC;
		break;
	case 4:
		regulation = TXPWR_LMT_WW;
		break;
	default:
		regulation = (Band == BAND_ON_2_4G) ? hal_data->Regulation2_4G : hal_data->Regulation5G;
		break;
	}

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_ERR("%s invalid band:%u\n", __func__, Band);
		rtw_warn_on(1);
		goto exit;
	}

	if (IS_CCK_RATE(DataRate))
		rs = CCK;
	else if (IS_OFDM_RATE(DataRate))
		rs = OFDM;
	else if (IS_HT1SS_RATE(DataRate))
		rs = HT_1SS;
	else if (IS_HT2SS_RATE(DataRate))
		rs = HT_2SS;
	else if (IS_HT3SS_RATE(DataRate))
		rs = HT_3SS;
	else if (IS_HT4SS_RATE(DataRate))
		rs = HT_4SS;
	else if (IS_VHT1SS_RATE(DataRate))
		rs = VHT_1SS;
	else if (IS_VHT2SS_RATE(DataRate))
		rs = VHT_2SS;
	else if (IS_VHT3SS_RATE(DataRate))
		rs = VHT_3SS;
	else if (IS_VHT4SS_RATE(DataRate))
		rs = VHT_4SS;
	else {
		RTW_ERR("%s invalid rate 0x%x\n", __func__, DataRate);
		rtw_warn_on(1);
		goto exit;
	}

	if (Band == BAND_ON_5G  && rs == CCK) {
		RTW_ERR("Wrong rate No CCK(0x%x) in 5G Band\n", DataRate);
		goto exit;
	}

	if (no_sc == _TRUE) {
		/* use the input center channel and bandwidth directly */
		cch = Channel;
		bw_bmp = ch_width_to_bw_cap(Bandwidth);
	} else {
		/*
		* find the possible tx bandwidth bmp for this rate, and then will get center channel for each bandwidth
		* if no possible tx bandwidth bmp, select valid bandwidth up to current RF bandwidth into bmp
		*/
		if (rs == CCK || rs == OFDM)
			bw_bmp = BW_CAP_20M; /* CCK, OFDM only BW 20M */
		else if (IS_HT_RATE_SECTION(rs)) {
			bw_bmp = rtw_get_tx_bw_bmp_of_ht_rate(dvobj, DataRate, Bandwidth);
			if (bw_bmp == 0)
				bw_bmp = ch_width_to_bw_cap(Bandwidth > CHANNEL_WIDTH_40 ? CHANNEL_WIDTH_40 : Bandwidth);
		} else if (IS_VHT_RATE_SECTION(rs)) {
			bw_bmp = rtw_get_tx_bw_bmp_of_vht_rate(dvobj, DataRate, Bandwidth);
			if (bw_bmp == 0)
				bw_bmp = ch_width_to_bw_cap(Bandwidth > CHANNEL_WIDTH_160 ? CHANNEL_WIDTH_160 : Bandwidth);
		} else
			rtw_warn_on(1);
	}

	if (bw_bmp == 0)
		goto exit;

	/* loop for each possible tx bandwidth to find minimum limit */
	for (bw = CHANNEL_WIDTH_20; bw <= Bandwidth; bw++) {
		s8 ch_idx;

		if (!(ch_width_to_bw_cap(bw) & bw_bmp))
			continue;

		if (no_sc == _FALSE) {
			if (bw == CHANNEL_WIDTH_20)
				cch = hal_data->cch_20;
			else if (bw == CHANNEL_WIDTH_40)
				cch = hal_data->cch_40;
			else if (bw == CHANNEL_WIDTH_80)
				cch = hal_data->cch_80;
			else {
				cch = 0;
				rtw_warn_on(1);
			}
		}

		ch_idx = phy_GetChannelIndexOfTxPowerLimit(Band, cch);
		if (ch_idx == -1)
			continue;

		if (Band == BAND_ON_2_4G) {
			s8 limits[MAX_REGULATION_NUM] = {0};
			u8 i = 0;

			for (i = 0; i < MAX_REGULATION_NUM; ++i)
				limits[i] = hal_data->TxPwrLimit_2_4G[i][bw][rs][ch_idx][RfPath];

			tmp_lmt = (regulation == TXPWR_LMT_WW) ? phy_GetWorldWideLimit(limits) :
				hal_data->TxPwrLimit_2_4G[regulation][bw][rs][ch_idx][RfPath];

		} else if (Band == BAND_ON_5G) {
			s8 limits[MAX_REGULATION_NUM] = {0};
			u8 i = 0;

			for (i = 0; i < MAX_REGULATION_NUM; ++i)
				limits[i] = hal_data->TxPwrLimit_5G[i][bw][rs][ch_idx][RfPath];

			tmp_lmt = (regulation == TXPWR_LMT_WW) ? phy_GetWorldWideLimit(limits) :
				hal_data->TxPwrLimit_5G[regulation][bw][rs][ch_idx][RfPath];
		} else
			continue;

		if (min_lmt >= tmp_lmt) {
			min_lmt = tmp_lmt;
			final_cch = cch;
			final_bw = bw;
		}
	}

exit:
	return min_lmt;
}

inline s8
PHY_GetTxPowerLimit(
	IN	PADAPTER			Adapter,
	IN	u32					RegPwrTblSel,
	IN	BAND_TYPE			Band,
	IN	CHANNEL_WIDTH		Bandwidth,
	IN	u8					RfPath,
	IN	u8					DataRate,
	IN	u8					Channel
)
{
	BOOLEAN no_sc = _FALSE;

	/* MP mode channel don't use secondary channel */
	if (rtw_mi_mp_mode_check(Adapter) == _TRUE)
		no_sc = _TRUE;

	return _phy_get_txpwr_lmt(Adapter, RegPwrTblSel, Band, Bandwidth, RfPath, DataRate, Channel, no_sc);
}

inline s8
PHY_GetTxPowerLimit_no_sc(
	IN	PADAPTER			Adapter,
	IN	u32					RegPwrTblSel,
	IN	BAND_TYPE			Band,
	IN	CHANNEL_WIDTH		Bandwidth,
	IN	u8					RfPath,
	IN	u8					DataRate,
	IN	u8					Channel
)
{
	return _phy_get_txpwr_lmt(Adapter, RegPwrTblSel, Band, Bandwidth, RfPath, DataRate, Channel, _TRUE);
}

#ifdef CONFIG_PHYDM_POWERTRACK_BY_TSSI
s8
PHY_GetTxPowerLimitOriginal(
	IN	PADAPTER			Adapter,
	IN	u32					RegPwrTblSel,
	IN	BAND_TYPE			Band,
	IN	CHANNEL_WIDTH		Bandwidth,
	IN	u8					RfPath,
	IN	u8					DataRate,
	IN	u8					Channel
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	s16				band = -1, regulation = -1, bandwidth = -1,
					rateSection = -1, channel = -1;
	s8				powerLimit = MAX_POWER_INDEX;

	if ((Adapter->registrypriv.RegEnableTxPowerLimit == 2 && pHalData->EEPROMRegulatory != 1) ||
	    Adapter->registrypriv.RegEnableTxPowerLimit == 0)
		return MAX_POWER_INDEX;

	switch (Adapter->registrypriv.RegPwrTblSel) {
	case 1:
		regulation = TXPWR_LMT_ETSI;
		break;

	case 2:
		regulation = TXPWR_LMT_MKK;
		break;

	case 3:
		regulation = TXPWR_LMT_FCC;
		break;

	case 4:
		regulation = TXPWR_LMT_WW;
		break;

	default:
		regulation = (Band == BAND_ON_2_4G) ? pHalData->Regulation2_4G
			     : pHalData->Regulation5G;
		break;
	}

	/*DBG_871X("pMgntInfo->RegPwrTblSel %d, final regulation %d\n", Adapter->registrypriv.RegPwrTblSel, regulation );*/


	if (Band == BAND_ON_2_4G)
		band = 0;
	else if (Band == BAND_ON_5G)
		band = 1;

	if (Bandwidth == CHANNEL_WIDTH_20)
		bandwidth = 0;
	else if (Bandwidth == CHANNEL_WIDTH_40)
		bandwidth = 1;
	else if (Bandwidth == CHANNEL_WIDTH_80)
		bandwidth = 2;
	else if (Bandwidth == CHANNEL_WIDTH_160)
		bandwidth = 3;

	switch (DataRate) {
	case MGN_1M:
	case MGN_2M:
	case MGN_5_5M:
	case MGN_11M:
		rateSection = 0;
		break;

	case MGN_6M:
	case MGN_9M:
	case MGN_12M:
	case MGN_18M:
	case MGN_24M:
	case MGN_36M:
	case MGN_48M:
	case MGN_54M:
		rateSection = 1;
		break;

	case MGN_MCS0:
	case MGN_MCS1:
	case MGN_MCS2:
	case MGN_MCS3:
	case MGN_MCS4:
	case MGN_MCS5:
	case MGN_MCS6:
	case MGN_MCS7:
		rateSection = 2;
		break;

	case MGN_MCS8:
	case MGN_MCS9:
	case MGN_MCS10:
	case MGN_MCS11:
	case MGN_MCS12:
	case MGN_MCS13:
	case MGN_MCS14:
	case MGN_MCS15:
		rateSection = 3;
		break;

	case MGN_MCS16:
	case MGN_MCS17:
	case MGN_MCS18:
	case MGN_MCS19:
	case MGN_MCS20:
	case MGN_MCS21:
	case MGN_MCS22:
	case MGN_MCS23:
		rateSection = 4;
		break;

	case MGN_MCS24:
	case MGN_MCS25:
	case MGN_MCS26:
	case MGN_MCS27:
	case MGN_MCS28:
	case MGN_MCS29:
	case MGN_MCS30:
	case MGN_MCS31:
		rateSection = 5;
		break;

	case MGN_VHT1SS_MCS0:
	case MGN_VHT1SS_MCS1:
	case MGN_VHT1SS_MCS2:
	case MGN_VHT1SS_MCS3:
	case MGN_VHT1SS_MCS4:
	case MGN_VHT1SS_MCS5:
	case MGN_VHT1SS_MCS6:
	case MGN_VHT1SS_MCS7:
	case MGN_VHT1SS_MCS8:
	case MGN_VHT1SS_MCS9:
		rateSection = 6;
		break;

	case MGN_VHT2SS_MCS0:
	case MGN_VHT2SS_MCS1:
	case MGN_VHT2SS_MCS2:
	case MGN_VHT2SS_MCS3:
	case MGN_VHT2SS_MCS4:
	case MGN_VHT2SS_MCS5:
	case MGN_VHT2SS_MCS6:
	case MGN_VHT2SS_MCS7:
	case MGN_VHT2SS_MCS8:
	case MGN_VHT2SS_MCS9:
		rateSection = 7;
		break;

	case MGN_VHT3SS_MCS0:
	case MGN_VHT3SS_MCS1:
	case MGN_VHT3SS_MCS2:
	case MGN_VHT3SS_MCS3:
	case MGN_VHT3SS_MCS4:
	case MGN_VHT3SS_MCS5:
	case MGN_VHT3SS_MCS6:
	case MGN_VHT3SS_MCS7:
	case MGN_VHT3SS_MCS8:
	case MGN_VHT3SS_MCS9:
		rateSection = 8;
		break;

	case MGN_VHT4SS_MCS0:
	case MGN_VHT4SS_MCS1:
	case MGN_VHT4SS_MCS2:
	case MGN_VHT4SS_MCS3:
	case MGN_VHT4SS_MCS4:
	case MGN_VHT4SS_MCS5:
	case MGN_VHT4SS_MCS6:
	case MGN_VHT4SS_MCS7:
	case MGN_VHT4SS_MCS8:
	case MGN_VHT4SS_MCS9:
		rateSection = 9;
		break;

	default:
		DBG_871X("Wrong rate 0x%x\n", DataRate);
		break;
	}

	if (Band == BAND_ON_5G  && rateSection == 0)
		DBG_871X("Wrong rate 0x%x: No CCK in 5G Band\n", DataRate);

	/*workaround for wrong index combination to obtain tx power limit,*/
	/*OFDM only exists in BW 20M*/
	if (rateSection == 1)
		bandwidth = 0;

	/*workaround for wrong index combination to obtain tx power limit,*/
	/*CCK table will only be given in BW 20M*/
	if (rateSection == 0)
		bandwidth = 0;

	/*workaround for wrong indxe combination to obtain tx power limit,*/
	/*HT on 80M will reference to HT on 40M*/
	if ((rateSection == 2 || rateSection == 3) && Band == BAND_ON_5G && bandwidth == 2)
		bandwidth = 1;

	if (Band == BAND_ON_2_4G)
		channel = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_2_4G, Channel);
	else if (Band == BAND_ON_5G)
		channel = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_5G, Channel);
	else if (Band == BAND_ON_BOTH)
		/*BAND_ON_BOTH don't care temporarily*/

		if (band == -1 || regulation == -1 || bandwidth == -1 ||
		    rateSection == -1 || channel == -1) {
			/*DBG_871X("Wrong index value to access power limit table [band %d][regulation %d][bandwidth %d][rf_path %d][rate_section %d][chnlGroup %d]\n",*/
			/*	  band, regulation, bandwidth, RfPath, rateSection, channelGroup );*/

			return MAX_POWER_INDEX;
		}

	if (Band == BAND_ON_2_4G) {
		s8 limits[10] = {0};
		u8 i = 0;

		if (bandwidth >= MAX_2_4G_BANDWIDTH_NUM)
			bandwidth = MAX_2_4G_BANDWIDTH_NUM - 1;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limits[i] = pHalData->TxPwrLimit_2_4G_Original[i][bandwidth][rateSection][channel][RfPath];

		powerLimit = (regulation == TXPWR_LMT_WW) ? phy_GetWorldWideLimit(limits) :
			pHalData->TxPwrLimit_2_4G_Original[regulation][bandwidth][rateSection][channel][RfPath];

	} else if (Band == BAND_ON_5G) {
		s8 limits[10] = {0};
		u8 i = 0;

		for (i = 0; i < MAX_REGULATION_NUM; ++i)
			limits[i] = pHalData->TxPwrLimit_5G_Original[i][bandwidth][rateSection][channel][RfPath];

		powerLimit = (regulation == TXPWR_LMT_WW) ? phy_GetWorldWideLimit(limits) :
			pHalData->TxPwrLimit_5G_Original[regulation][bandwidth][rateSection][channel][RfPath];
	} else
		DBG_871X("No power limit table of the specified band\n");

	/*combine 5G VHT & HT rate*/
	/*5G 20M and 40M HT and VHT can cross reference*/
	/*
	if (Band == BAND_ON_5G && powerLimit == MAX_POWER_INDEX) {
		if (bandwidth == 0 || bandwidth == 1) {
			if (rateSection == 2)
				powerLimit = pHalData->TxPwrLimit_5G_Original[regulation]
										[bandwidth][4][channelGroup][RfPath];
			else if (rateSection == 4)
				powerLimit = pHalData->TxPwrLimit_5G_Original[regulation]
										[bandwidth][2][channelGroup][RfPath];
			else if (rateSection == 3)
				powerLimit = pHalData->TxPwrLimit_5G_Original[regulation]
										[bandwidth][5][channelGroup][RfPath];
			else if (rateSection == 5)
				powerLimit = pHalData->TxPwrLimit_5G_Original[regulation]
										[bandwidth][3][channelGroup][RfPath];
		}
	}
	*/
	/*DBG_871X("TxPwrLmt[Regulation %d][Band %d][BW %d][RFPath %d][Rate 0x%x][Chnl %d] = %d\n",*/
	/*		regulation, pHalData->current_band_type, Bandwidth, RfPath, DataRate, Channel, powerLimit);*/
	return powerLimit;
}
#endif


VOID
phy_CrossReferenceHTAndVHTTxPowerLimit(
	IN	PADAPTER			pAdapter
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(pAdapter);
	u8 regulation, bw, channel, rs, ref_rs;
	int ht_ref_vht_5g_20_40 = 0;
	int vht_ref_ht_5g_20_40 = 0;
	int ht_has_ref_5g_20_40 = 0;
	int vht_has_ref_5g_20_40 = 0;

	pHalData->tx_pwr_lmt_5g_20_40_ref = 0;

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {

		for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; ++bw) {

			for (channel = 0; channel < CENTER_CH_5G_ALL_NUM; ++channel) {

				for (rs = 0; rs < MAX_RATE_SECTION_NUM; ++rs) {

					/* 5G 20M 40M VHT and HT can cross reference */
					if (bw == CHANNEL_WIDTH_20 || bw == CHANNEL_WIDTH_40) {
						if (rs == HT_1SS)
							ref_rs = VHT_1SS;
						else if (rs == HT_2SS)
							ref_rs = VHT_2SS;
						else if (rs == HT_3SS)
							ref_rs = VHT_3SS;
						else if (rs == HT_4SS)
							ref_rs = VHT_4SS;
						else if (rs == VHT_1SS)
							ref_rs = HT_1SS;
						else if (rs == VHT_2SS)
							ref_rs = HT_2SS;
						else if (rs == VHT_3SS)
							ref_rs = HT_3SS;
						else if (rs == VHT_4SS)
							ref_rs = HT_4SS;
						else
							continue;

						if (pHalData->TxPwrLimit_5G[regulation][bw][ref_rs][channel][RF_PATH_A] == MAX_POWER_INDEX)
							continue;

						if (IS_HT_RATE_SECTION(rs))
							ht_has_ref_5g_20_40++;
						else if (IS_VHT_RATE_SECTION(rs))
							vht_has_ref_5g_20_40++;
						else
							continue;

						if (pHalData->TxPwrLimit_5G[regulation][bw][rs][channel][RF_PATH_A] != MAX_POWER_INDEX)
							continue;

						if (IS_HT_RATE_SECTION(rs) && IS_VHT_RATE_SECTION(ref_rs))
							ht_ref_vht_5g_20_40++;
						else if (IS_VHT_RATE_SECTION(rs) && IS_HT_RATE_SECTION(ref_rs))
							vht_ref_ht_5g_20_40++;

						if (0)
							RTW_INFO("reg:%u, bw:%u, ch:%u, %s ref %s\n"
								, regulation, bw, channel
								, rate_section_str(rs), rate_section_str(ref_rs));

						pHalData->TxPwrLimit_5G[regulation][bw][rs][channel][RF_PATH_A] =
							pHalData->TxPwrLimit_5G[regulation][bw][ref_rs][channel][RF_PATH_A];
					}

				}
			}
		}
	}

	if (0) {
		RTW_INFO("ht_ref_vht_5g_20_40:%d, ht_has_ref_5g_20_40:%d\n", ht_ref_vht_5g_20_40, ht_has_ref_5g_20_40);
		RTW_INFO("vht_ref_hht_5g_20_40:%d, vht_has_ref_5g_20_40:%d\n", vht_ref_ht_5g_20_40, vht_has_ref_5g_20_40);
	}

	/* 5G 20M&40M HT all come from VHT*/
	if (ht_ref_vht_5g_20_40 && ht_has_ref_5g_20_40 == ht_ref_vht_5g_20_40)
		pHalData->tx_pwr_lmt_5g_20_40_ref |= TX_PWR_LMT_REF_HT_FROM_VHT;

	/* 5G 20M&40M VHT all come from HT*/
	if (vht_ref_ht_5g_20_40 && vht_has_ref_5g_20_40 == vht_ref_ht_5g_20_40)
		pHalData->tx_pwr_lmt_5g_20_40_ref |= TX_PWR_LMT_REF_VHT_FROM_HT;
}

VOID
PHY_ConvertTxPowerLimitToPowerIndex(
	IN	PADAPTER			Adapter
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u8 base;
	u8 regulation, bw, channel, rateSection;
	s8 tempValue = 0, tempPwrLmt = 0;
	u8 rfPath = 0;

	if (pHalData->odmpriv.phy_reg_pg_value_type != PHY_REG_PG_EXACT_VALUE) {
		rtw_warn_on(1);
		return;
	}

	phy_CrossReferenceHTAndVHTTxPowerLimit(Adapter);

	for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {

		for (bw = 0; bw < MAX_2_4G_BANDWIDTH_NUM; ++bw) {

			for (channel = 0; channel < CENTER_CH_2G_NUM; ++channel) {

				for (rateSection = CCK; rateSection <= HT_4SS; ++rateSection) {
					tempPwrLmt = pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][channel][RF_PATH_A];

					if (tempPwrLmt != MAX_POWER_INDEX) {

						for (rfPath = RF_PATH_A; rfPath < MAX_RF_PATH; ++rfPath) {
							base = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_2_4G, rfPath, rate_section_to_tx_num(rateSection), rateSection);
							tempValue = tempPwrLmt - base;
							pHalData->TxPwrLimit_2_4G[regulation][bw][rateSection][channel][rfPath] = tempValue;
						}
					}
				}
			}
		}
	}

	if (IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(Adapter)) {

		for (regulation = 0; regulation < MAX_REGULATION_NUM; ++regulation) {

			for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; ++bw) {

				for (channel = 0; channel < CENTER_CH_5G_ALL_NUM; ++channel) {

					for (rateSection = OFDM; rateSection <= VHT_4SS; ++rateSection) {
						tempPwrLmt = pHalData->TxPwrLimit_5G[regulation][bw][rateSection][channel][RF_PATH_A];

						if (tempPwrLmt != MAX_POWER_INDEX) {

							for (rfPath = RF_PATH_A; rfPath < MAX_RF_PATH; ++rfPath) {
								base = PHY_GetTxPowerByRateBase(Adapter, BAND_ON_5G, rfPath, rate_section_to_tx_num(rateSection), rateSection);
								tempValue = tempPwrLmt - base;
								pHalData->TxPwrLimit_5G[regulation][bw][rateSection][channel][rfPath] = tempValue;
							}
						}
					}
				}
			}
		}
	}
}

/*
* PHY_InitTxPowerLimit - Set all hal_data.TxPwrLimit_2_4G, TxPwrLimit_5G array to MAX_POWER_INDEX
*/
VOID
PHY_InitTxPowerLimit(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u8 i, j, k, l, m;

	for (i = 0; i < MAX_REGULATION_NUM; ++i)
		for (j = 0; j < MAX_2_4G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CENTER_CH_2G_NUM; ++m)
					for (l = 0; l < MAX_RF_PATH; ++l)
						pHalData->TxPwrLimit_2_4G[i][j][k][m][l] = MAX_POWER_INDEX;

	for (i = 0; i < MAX_REGULATION_NUM; ++i)
		for (j = 0; j < MAX_5G_BANDWIDTH_NUM; ++j)
			for (k = 0; k < MAX_RATE_SECTION_NUM; ++k)
				for (m = 0; m < CENTER_CH_5G_ALL_NUM; ++m)
					for (l = 0; l < MAX_RF_PATH; ++l)
						pHalData->TxPwrLimit_5G[i][j][k][m][l] = MAX_POWER_INDEX;
}

/*
* phy_set_tx_power_limit - Parsing TX power limit from phydm array, called by odm_ConfigBB_TXPWR_LMT_XXX in phydm
*/
VOID
phy_set_tx_power_limit(
	IN	struct PHY_DM_STRUCT		*pDM_Odm,
	IN	u8				*Regulation,
	IN	u8				*Band,
	IN	u8				*Bandwidth,
	IN	u8				*RateSection,
	IN	u8				*RfPath,
	IN	u8				*Channel,
	IN	u8				*PowerLimit
)
{
	PADAPTER Adapter = pDM_Odm->adapter;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u8 regulation = 0, bandwidth = 0, rateSection = 0, channel;
	s8 powerLimit = 0, prevPowerLimit, channelIndex;

	if (0)
		RTW_INFO("Index of power limit table [band %s][regulation %s][bw %s][rate section %s][rf path %s][chnl %s][val %s]\n"
			, Band, Regulation, Bandwidth, RateSection, RfPath, Channel, PowerLimit);

	if (GetU1ByteIntegerFromStringInDecimal((s8 *)Channel, &channel) == _FALSE
		|| GetU1ByteIntegerFromStringInDecimal((s8 *)PowerLimit, &powerLimit) == _FALSE
	) {
		RTW_PRINT("Illegal index of power limit table [ch %s][val %s]\n", Channel, PowerLimit);
		return;
	}

	powerLimit = powerLimit > MAX_POWER_INDEX ? MAX_POWER_INDEX : powerLimit;

	if (eqNByte(Regulation, (u8 *)("FCC"), 3))
		regulation = TXPWR_LMT_FCC;
	else if (eqNByte(Regulation, (u8 *)("MKK"), 3))
		regulation = TXPWR_LMT_MKK;
	else if (eqNByte(Regulation, (u8 *)("ETSI"), 4))
		regulation = TXPWR_LMT_ETSI;
	else if (eqNByte(Regulation, (u8 *)("WW13"), 4))
		regulation = TXPWR_LMT_WW;
	else {
		RTW_PRINT("unknown regulation:%s", Regulation);
		return;
	}

	if (eqNByte(RateSection, (u8 *)("CCK"), 3) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = CCK;
	else if (eqNByte(RateSection, (u8 *)("OFDM"), 4) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = OFDM;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = HT_1SS;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("2T"), 2))
		rateSection = HT_2SS;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("3T"), 2))
		rateSection = HT_3SS;
	else if (eqNByte(RateSection, (u8 *)("HT"), 2) && eqNByte(RfPath, (u8 *)("4T"), 2))
		rateSection = HT_4SS;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("1T"), 2))
		rateSection = VHT_1SS;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("2T"), 2))
		rateSection = VHT_2SS;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("3T"), 2))
		rateSection = VHT_3SS;
	else if (eqNByte(RateSection, (u8 *)("VHT"), 3) && eqNByte(RfPath, (u8 *)("4T"), 2))
		rateSection = VHT_4SS;
	else {
		RTW_PRINT("Wrong rate section: (%s,%s)\n", RateSection, RfPath);
		return;
	}

	if (eqNByte(Bandwidth, (u8 *)("20M"), 3))
		bandwidth = CHANNEL_WIDTH_20;
	else if (eqNByte(Bandwidth, (u8 *)("40M"), 3))
		bandwidth = CHANNEL_WIDTH_40;
	else if (eqNByte(Bandwidth, (u8 *)("80M"), 3))
		bandwidth = CHANNEL_WIDTH_80;
	else {
		RTW_PRINT("unknown bandwidth: %s\n", Bandwidth);
		return;
	}

	if (eqNByte(Band, (u8 *)("2.4G"), 4)) {
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_2_4G, channel);

		if (channelIndex == -1) {
			RTW_PRINT("unsupported channel: %d at 2.4G\n", channel);
			return;
		}

		if (bandwidth >= MAX_2_4G_BANDWIDTH_NUM) {
			RTW_PRINT("unsupported bandwidth: %s at 2.4G\n", Bandwidth);
			return;
		}

		prevPowerLimit = pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A];

		if (prevPowerLimit != MAX_POWER_INDEX)
			RTW_PRINT("duplicate tx power limit combination [band %s][regulation %s][bw %s][rate section %s][rf path %s][chnl %s]\n"
				, Band, Regulation, Bandwidth, RateSection, RfPath, Channel);

		if (powerLimit < prevPowerLimit)
			pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A] = powerLimit;

		if (0)
			RTW_INFO("2.4G Band value : [regulation %d][bw %d][rate_section %d][chnl %d][val %d]\n"
				, regulation, bandwidth, rateSection, channelIndex, pHalData->TxPwrLimit_2_4G[regulation][bandwidth][rateSection][channelIndex][ODM_RF_PATH_A]);
	} else if (eqNByte(Band, (u8 *)("5G"), 2)) {

		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_5G, channel);

		if (channelIndex == -1) {
			RTW_PRINT("unsupported channel: %d at 5G\n", channel);
			return;
		}

		prevPowerLimit = pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A];

		if (prevPowerLimit != MAX_POWER_INDEX)
			RTW_PRINT("duplicate tx power limit combination [band %s][regulation %s][bw %s][rate section %s][rf path %s][chnl %s]\n"
				, Band, Regulation, Bandwidth, RateSection, RfPath, Channel);

		if (powerLimit < prevPowerLimit)
			pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A] = powerLimit;

		if (0)
			RTW_INFO("5G Band value : [regulation %d][bw %d][rate_section %d][chnl %d][val %d]\n"
				, regulation, bandwidth, rateSection, channel, pHalData->TxPwrLimit_5G[regulation][bandwidth][rateSection][channelIndex][RF_PATH_A]);
	} else {
		RTW_PRINT("Cannot recognize the band info in %s\n", Band);
		return;
	}
}

u8
phy_get_tx_power_index(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	u8					Rate,
	IN	CHANNEL_WIDTH		BandWidth,
	IN	u8					Channel
)
{
	return rtw_hal_get_tx_power_index(pAdapter, RFPath, Rate, BandWidth, Channel, NULL);
}

VOID
PHY_SetTxPowerIndex(
	IN	PADAPTER		pAdapter,
	IN	u32				PowerIndex,
	IN	u8				RFPath,
	IN	u8				Rate
)
{
	if (IS_HARDWARE_TYPE_8814A(pAdapter)) {
#if (RTL8814A_SUPPORT == 1)
		PHY_SetTxPowerIndex_8814A(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_JAGUAR(pAdapter)) {
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
		PHY_SetTxPowerIndex_8812A(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8723B(pAdapter)) {
#if (RTL8723B_SUPPORT == 1)
		PHY_SetTxPowerIndex_8723B(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8703B(pAdapter)) {
#if (RTL8703B_SUPPORT == 1)
		PHY_SetTxPowerIndex_8703B(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8723D(pAdapter)) {
#if (RTL8723D_SUPPORT == 1)
		PHY_SetTxPowerIndex_8723D(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8192E(pAdapter)) {
#if (RTL8192E_SUPPORT == 1)
		PHY_SetTxPowerIndex_8192E(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8188E(pAdapter)) {
#if (RTL8188E_SUPPORT == 1)
		PHY_SetTxPowerIndex_8188E(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8188F(pAdapter)) {
#if (RTL8188F_SUPPORT == 1)
		PHY_SetTxPowerIndex_8188F(pAdapter, PowerIndex, RFPath, Rate);
#endif
	} else if (IS_HARDWARE_TYPE_8822B(pAdapter))
		rtw_hal_set_tx_power_index(pAdapter, PowerIndex, RFPath, Rate);
	else if (IS_HARDWARE_TYPE_8821C(pAdapter))
		rtw_hal_set_tx_power_index(pAdapter, PowerIndex, RFPath, Rate);
}

void dump_tx_power_idx_title(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 bw = hal_data->current_channel_bw;

	RTW_PRINT_SEL(sel, "%s", ch_width_str(bw));
	if (bw >= CHANNEL_WIDTH_80)
		_RTW_PRINT_SEL(sel, ", cch80:%u", hal_data->cch_80);
	if (bw >= CHANNEL_WIDTH_40)
		_RTW_PRINT_SEL(sel, ", cch40:%u", hal_data->cch_40);
	_RTW_PRINT_SEL(sel, ", cch20:%u\n", hal_data->cch_20);

	RTW_PRINT_SEL(sel, "%-4s %-9s %-3s %-4s %-3s %-4s %-4s %-3s %-5s\n"
		, "path", "rate", "pwr", "base", "", "(byr", "lmt)", "tpt", "ebias");
}

void dump_tx_power_idx_by_path_rs(void *sel, _adapter *adapter, u8 rfpath, u8 rs)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 power_idx;
	struct txpwr_idx_comp tic;
	u8 tx_num, i;
	u8 band = hal_data->current_band_type;
	u8 cch = hal_data->current_channel;
	u8 bw = hal_data->current_channel_bw;

	if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, rfpath))
		return;

	if (rs >= RATE_SECTION_NUM)
		return;

	tx_num = rate_section_to_tx_num(rs);
	if (tx_num >= hal_spec->tx_nss_num || tx_num >= hal_spec->max_tx_cnt)
		return;

	if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
		return;

	if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
		return;

	for (i = 0; i < rates_by_sections[rs].rate_num; i++) {
		power_idx = rtw_hal_get_tx_power_index(adapter, rfpath, rates_by_sections[rs].rates[i], bw, cch, &tic);

		RTW_PRINT_SEL(sel, "%4c %9s %3u %4u %3d (%3d %3d) %3d %5d\n"
			, rf_path_char(rfpath), MGN_RATE_STR(rates_by_sections[rs].rates[i])
			, power_idx, tic.base, (tic.by_rate > tic.limit ? tic.limit : tic.by_rate), tic.by_rate, tic.limit, tic.tpt, tic.ebias);
	}
}

void dump_tx_power_idx(void *sel, _adapter *adapter)
{
	u8 rfpath, rs;

	dump_tx_power_idx_title(sel, adapter);
	for (rfpath = RF_PATH_A; rfpath < RF_PATH_MAX; rfpath++)
		for (rs = CCK; rs < RATE_SECTION_NUM; rs++)
			dump_tx_power_idx_by_path_rs(sel, adapter, rfpath, rs);
}

bool phy_is_tx_power_limit_needed(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));

	if (regsty->RegEnableTxPowerLimit == 1
		|| (regsty->RegEnableTxPowerLimit == 2 && hal_data->EEPROMRegulatory == 1))
		return _TRUE;
	return _FALSE;
}

bool phy_is_tx_power_by_rate_needed(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));

	if (regsty->RegEnableTxPowerByRate == 1
		|| (regsty->RegEnableTxPowerByRate == 2 && hal_data->EEPROMRegulatory != 2))
		return _TRUE;
	return _FALSE;
}

int phy_load_tx_power_by_rate(_adapter *adapter, u8 chk_file)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));
	int ret = _FAIL;

	hal_data->txpwr_by_rate_loaded = 0;
	PHY_InitTxPowerByRate(adapter);

	/* tx power limit is based on tx power by rate */
	hal_data->txpwr_limit_loaded = 0;

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (chk_file
		&& phy_ConfigBBWithPgParaFile(adapter, PHY_FILE_PHY_REG_PG) == _SUCCESS
	) {
		hal_data->txpwr_by_rate_from_file = 1;
		goto post_hdl;
	}
#endif

#ifdef CONFIG_EMBEDDED_FWIMG
	if (HAL_STATUS_SUCCESS == odm_config_bb_with_header_file(&hal_data->odmpriv, CONFIG_BB_PHY_REG_PG)) {
		RTW_INFO("default power by rate loaded\n");
		hal_data->txpwr_by_rate_from_file = 0;
		goto post_hdl;
	}
#endif

	RTW_ERR("%s():Read Tx power by rate fail\n", __func__);
	goto exit;

post_hdl:
	if (hal_data->odmpriv.phy_reg_pg_value_type != PHY_REG_PG_EXACT_VALUE) {
		rtw_warn_on(1);
		goto exit;
	}

	PHY_TxPowerByRateConfiguration(adapter);
	hal_data->txpwr_by_rate_loaded = 1;

	ret = _SUCCESS;

exit:
	return ret;
}

int phy_load_tx_power_limit(_adapter *adapter, u8 chk_file)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));
	int ret = _FAIL;

	hal_data->txpwr_limit_loaded = 0;
	PHY_InitTxPowerLimit(adapter);

	if (!hal_data->txpwr_by_rate_loaded && regsty->target_tx_pwr_valid != _TRUE) {
		RTW_ERR("%s():Read Tx power limit before target tx power is specify\n", __func__);
		goto exit;
	}

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	if (chk_file
		&& PHY_ConfigRFWithPowerLimitTableParaFile(adapter, PHY_FILE_TXPWR_LMT) == _SUCCESS
	) {
		hal_data->txpwr_limit_from_file = 1;
		goto post_hdl;
	}
#endif

#ifdef CONFIG_EMBEDDED_FWIMG
	if (HAL_STATUS_SUCCESS == odm_config_rf_with_header_file(&hal_data->odmpriv, CONFIG_RF_TXPWR_LMT, (enum odm_rf_radio_path_e)0)) {
		RTW_INFO("default power limit loaded\n");
		hal_data->txpwr_limit_from_file = 0;
		goto post_hdl;
	}
#endif

	RTW_ERR("%s():Read Tx power limit fail\n", __func__);
	goto exit;

post_hdl:
	PHY_ConvertTxPowerLimitToPowerIndex(adapter);
	hal_data->txpwr_limit_loaded = 1;
	ret = _SUCCESS;

exit:
	return ret;
}

void phy_load_tx_power_ext_info(_adapter *adapter, u8 chk_file)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);

	/* check registy target tx power */
	regsty->target_tx_pwr_valid = rtw_regsty_chk_target_tx_power_valid(adapter);

	/* power by rate and limit */
	if (phy_is_tx_power_by_rate_needed(adapter)
		|| (phy_is_tx_power_limit_needed(adapter) && regsty->target_tx_pwr_valid != _TRUE)
	)
		phy_load_tx_power_by_rate(adapter, chk_file);

	if (phy_is_tx_power_limit_needed(adapter))
		phy_load_tx_power_limit(adapter, chk_file);
}

inline void phy_reload_tx_power_ext_info(_adapter *adapter)
{
	phy_load_tx_power_ext_info(adapter, 1);
}

inline void phy_reload_default_tx_power_ext_info(_adapter *adapter)
{
	phy_load_tx_power_ext_info(adapter, 0);
}

void dump_tx_power_ext_info(void *sel, _adapter *adapter)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if (regsty->target_tx_pwr_valid == _TRUE)
		RTW_PRINT_SEL(sel, "target_tx_power: from registry\n");
	else if (phy_is_tx_power_by_rate_needed(adapter))
		RTW_PRINT_SEL(sel, "target_tx_power: from power by rate\n"); 
	else
		RTW_PRINT_SEL(sel, "target_tx_power: unavailable\n");

	RTW_PRINT_SEL(sel, "tx_power_by_rate: %s, %s, %s\n"
		, phy_is_tx_power_by_rate_needed(adapter) ? "enabled" : "disabled"
		, hal_data->txpwr_by_rate_loaded ? "loaded" : "unloaded"
		, hal_data->txpwr_by_rate_from_file ? "file" : "default"
	);

	RTW_PRINT_SEL(sel, "tx_power_limit: %s, %s, %s\n"
		, phy_is_tx_power_limit_needed(adapter) ? "enabled" : "disabled"
		, hal_data->txpwr_limit_loaded ? "loaded" : "unloaded"
		, hal_data->txpwr_limit_from_file ? "file" : "default"
	);
}

void dump_target_tx_power(void *sel, _adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	int path, tx_num, band, rs;
	u8 target;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		for (path = 0; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				break;

			RTW_PRINT_SEL(sel, "[%s][%c]%s\n", band_str(band), rf_path_char(path)
				, (regsty->target_tx_pwr_valid == _FALSE && hal_data->txpwr_by_rate_undefined_band_path[band][path]) ? "(dup)" : "");

			for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
				tx_num = rate_section_to_tx_num(rs);
				if (tx_num >= hal_spec->tx_nss_num)
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
					continue;

				target = PHY_GetTxPowerByRateBase(adapter, band, path, rate_section_to_tx_num(rs), rs);

				if (target % 2)
					_RTW_PRINT_SEL(sel, "%7s: %2d.5\n", rate_section_str(rs), target / 2);
				else
					_RTW_PRINT_SEL(sel, "%7s: %4d\n", rate_section_str(rs), target / 2);
			}
		}
	}

exit:
	return;
}

void dump_tx_power_by_rate(void *sel, _adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int path, tx_num, band, n, rs;
	u8 rate_num, max_rate_num, base;
	s8 by_rate_offset;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		for (path = 0; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				break;

			RTW_PRINT_SEL(sel, "[%s][%c]%s\n", band_str(band), rf_path_char(path)
				, hal_data->txpwr_by_rate_undefined_band_path[band][path] ? "(dup)" : "");

			for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
				tx_num = rate_section_to_tx_num(rs);
				if (tx_num >= hal_spec->tx_nss_num)
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
					continue;

				if (IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
					max_rate_num = 10;
				else
					max_rate_num = 8;
				rate_num = rate_section_rate_num(rs);
				base = PHY_GetTxPowerByRateBase(adapter, band, path, tx_num, rs);

				RTW_PRINT_SEL(sel, "%7s: ", rate_section_str(rs));

				/* dump power by rate in db */
				for (n = rate_num - 1; n >= 0; n--) {
					by_rate_offset = PHY_GetTxPowerByRate(adapter, band, path, tx_num, rates_by_sections[rs].rates[n]);

					if ((base + by_rate_offset) % 2)
						_RTW_PRINT_SEL(sel, "%2d.5 ", (base + by_rate_offset) / 2);
					else
						_RTW_PRINT_SEL(sel, "%4d ", (base + by_rate_offset) / 2);
				}
				for (n = 0; n < max_rate_num - rate_num; n++)
					_RTW_PRINT_SEL(sel, "%4s ", "");

				_RTW_PRINT_SEL(sel, "|");

				/* dump power by rate in offset */
				for (n = rate_num - 1; n >= 0; n--) {
					by_rate_offset = PHY_GetTxPowerByRate(adapter, band, path, tx_num, rates_by_sections[rs].rates[n]);
					_RTW_PRINT_SEL(sel, "%3d ", by_rate_offset);
				}
				RTW_PRINT_SEL(sel, "\n");

			}
		}
	}
}

void dump_tx_power_limit(void *sel, _adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));

	int bw, band, ch_num, rs, i, path;
	u8 ch, n, rd, rfpath_num;

	if (IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
		RTW_PRINT_SEL(sel, "tx_pwr_lmt_5g_20_40_ref:0x%02x\n", hal_data->tx_pwr_lmt_5g_20_40_ref);

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		rd = (band == BAND_ON_2_4G ? hal_data->Regulation2_4G : hal_data->Regulation5G);
		rfpath_num = (band == BAND_ON_2_4G ? hal_spec->rfpath_num_2g : hal_spec->rfpath_num_5g);

		for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; bw++) {

			if (bw >= CHANNEL_WIDTH_160)
				break;
			if (band == BAND_ON_2_4G && bw >= CHANNEL_WIDTH_80)
				break;

			if (band == BAND_ON_2_4G)
				ch_num = CENTER_CH_2G_NUM;
			else
				ch_num = center_chs_5g_num(bw);

			if (ch_num == 0) {
				rtw_warn_on(1);
				break;
			}

			for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
				if (band == BAND_ON_2_4G && IS_VHT_RATE_SECTION(rs))
					continue;
				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;
				if (bw > CHANNEL_WIDTH_20 && (IS_CCK_RATE_SECTION(rs) || IS_OFDM_RATE_SECTION(rs)))
					continue;
				if (bw > CHANNEL_WIDTH_40 && IS_HT_RATE_SECTION(rs))
					continue;

				if (rate_section_to_tx_num(rs) >= hal_spec->tx_nss_num)
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_AND_JAGUAR2(adapter))
					continue;

				/* by pass 5G 20M, 40M pure reference */
				if (band == BAND_ON_5G && (bw == CHANNEL_WIDTH_20 || bw == CHANNEL_WIDTH_40)) {
					if (hal_data->tx_pwr_lmt_5g_20_40_ref == TX_PWR_LMT_REF_HT_FROM_VHT) {
						if (IS_HT_RATE_SECTION(rs))
							continue;
					} else if (hal_data->tx_pwr_lmt_5g_20_40_ref == TX_PWR_LMT_REF_VHT_FROM_HT) {
						if (IS_VHT_RATE_SECTION(rs) && bw <= CHANNEL_WIDTH_40)
							continue;
					}
				}

				RTW_PRINT_SEL(sel, "[%s][%s][%s]\n"
					, band_str(band)
					, ch_width_str(bw)
					, rate_section_str(rs)
				);

				/* header for limit in db */
				RTW_PRINT_SEL(sel, "%3s %5s %5s %5s %5s "
					, "ch"
					, (rd == TXPWR_LMT_FCC ? "*FCC" : "FCC")
					, (rd == TXPWR_LMT_ETSI ? "*ETSI" : "ETSI")
					, (rd == TXPWR_LMT_MKK ? "*MKK" : "MKK")
					, (rd == TXPWR_LMT_WW ? "*WW" : "WW")
				);

				/* header for limit offset */
				for (path = 0; path < RF_PATH_MAX; path++) {
					if (path >= rfpath_num)
						break;
					_RTW_PRINT_SEL(sel, "|%3c %3c %3c %3c "
						, (rd == TXPWR_LMT_FCC ? rf_path_char(path) : ' ')
						, (rd == TXPWR_LMT_ETSI ? rf_path_char(path) : ' ')
						, (rd == TXPWR_LMT_MKK ? rf_path_char(path) : ' ')
						, (rd == TXPWR_LMT_WW ? rf_path_char(path) : ' ')
					);
				}
				_RTW_PRINT_SEL(sel, "\n");

				for (n = 0; n < ch_num; n++) {
					s8 limit_idx[RF_PATH_MAX][MAX_REGULATION_NUM];
					s8 limit_offset[MAX_REGULATION_NUM];
					u8 base;

					if (band == BAND_ON_2_4G)
						ch = n + 1;
					else
						ch = center_chs_5g(bw, n);

					if (ch == 0) {
						rtw_warn_on(1);
						break;
					}

					/* dump limit in db (calculate from path A) */
					limit_offset[0] = PHY_GetTxPowerLimit_no_sc(adapter, 3, band, bw, RF_PATH_A, rates_by_sections[rs].rates[0], ch); /* FCC */
					limit_offset[1] = PHY_GetTxPowerLimit_no_sc(adapter, 1, band, bw, RF_PATH_A, rates_by_sections[rs].rates[0], ch); /* ETSI */
					limit_offset[2] = PHY_GetTxPowerLimit_no_sc(adapter, 2, band, bw, RF_PATH_A, rates_by_sections[rs].rates[0], ch); /* MKK */
					limit_offset[3] = PHY_GetTxPowerLimit_no_sc(adapter, 4, band, bw, RF_PATH_A, rates_by_sections[rs].rates[0], ch); /* WW */

					base = PHY_GetTxPowerByRateBase(adapter, band, RF_PATH_A, rate_section_to_tx_num(rs), rs);

					RTW_PRINT_SEL(sel, "%3u ", ch);
					for (i = 0; i < MAX_REGULATION_NUM; i++) {
						if (limit_offset[i] == MAX_POWER_INDEX) {
							limit_idx[0][i] = MAX_POWER_INDEX;
							_RTW_PRINT_SEL(sel, "%5s ", "NA");
						} else {
							limit_idx[0][i] = limit_offset[i] + base;
							if ((limit_offset[i] + base) % 2)
								_RTW_PRINT_SEL(sel, "%3d.5 ", (limit_offset[i] + base) / 2);
							else
								_RTW_PRINT_SEL(sel, "%5d ", (limit_offset[i] + base) / 2);
						}
					}

					/* dump limit offset of each path */
					for (path = 0; path < RF_PATH_MAX; path++) {
						if (path >= rfpath_num)
							break;
						limit_offset[0] = PHY_GetTxPowerLimit_no_sc(adapter, 3, band, bw, path, rates_by_sections[rs].rates[0], ch); /* FCC */
						limit_offset[1] = PHY_GetTxPowerLimit_no_sc(adapter, 1, band, bw, path, rates_by_sections[rs].rates[0], ch); /* ETSI */
						limit_offset[2] = PHY_GetTxPowerLimit_no_sc(adapter, 2, band, bw, path, rates_by_sections[rs].rates[0], ch); /* MKK */
						limit_offset[3] = PHY_GetTxPowerLimit_no_sc(adapter, 4, band, bw, path, rates_by_sections[rs].rates[0], ch); /* WW */

						base = PHY_GetTxPowerByRateBase(adapter, band, path, rate_section_to_tx_num(rs), rs);

						_RTW_PRINT_SEL(sel, "|");
						for (i = 0; i < MAX_REGULATION_NUM; i++) {
							if (limit_offset[i] == MAX_POWER_INDEX) {
								limit_idx[path][i] = MAX_POWER_INDEX;
								_RTW_PRINT_SEL(sel, "%3s ", "NA");
							} else {
								limit_idx[path][i] = limit_offset[i] + base;
								_RTW_PRINT_SEL(sel, "%3d ", limit_offset[i]);
							}
						}
					}

					/* compare limit_idx of each path, print 'x' when mismatch */
					if (rfpath_num > 1) {
						for (i = 0; i < MAX_REGULATION_NUM; i++) {
							for (path = 0; path < RF_PATH_MAX; path++) {
								if (path >= rfpath_num)
									break;
								if (limit_idx[path][i] != limit_idx[(path + 1) % rfpath_num][i])
									break;
							}
							if (path >= rfpath_num)
								_RTW_PRINT_SEL(sel, " ");
							else
								_RTW_PRINT_SEL(sel, "x");
						}
					}
					_RTW_PRINT_SEL(sel, "\n");

				}
				RTW_PRINT_SEL(sel, "\n");
			} /* loop for rate sections */
		} /* loop for bandwidths */
	} /* loop for bands */
}

/*
 * phy file path is stored in global char array rtw_phy_para_file_path
 * need to care about racing
 */
int rtw_get_phy_file_path(_adapter *adapter, const char *file_name)
{
#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	int len = 0;

	if (file_name) {
		len += snprintf(rtw_phy_para_file_path, PATH_LENGTH_MAX, "%s", rtw_phy_file_path);
		#if defined(CONFIG_MULTIDRV) || defined(REALTEK_CONFIG_PATH_WITH_IC_NAME_FOLDER)
		len += snprintf(rtw_phy_para_file_path + len, PATH_LENGTH_MAX - len, "%s/", hal_spec->ic_name);
		#endif
		len += snprintf(rtw_phy_para_file_path + len, PATH_LENGTH_MAX - len, "%s", file_name);

		return _TRUE;
	}
#endif
	return _FALSE;
}

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
int
phy_ConfigMACWithParaFile(
	IN	PADAPTER	Adapter,
	IN	char		*pFileName
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char	*szLine, *ptmp;
	u32	u4bRegOffset, u4bRegValue, u4bMove;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_MAC_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->mac_reg_len == 0) && (pHalData->mac_reg == NULL)) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->mac_reg = rtw_zvmalloc(rlen);
				if (pHalData->mac_reg) {
					_rtw_memcpy(pHalData->mac_reg, pHalData->para_file_buf, rlen);
					pHalData->mac_reg_len = rlen;
				} else
					RTW_INFO("%s mac_reg alloc fail !\n", __FUNCTION__);
			}
		}
	} else {
		if ((pHalData->mac_reg_len != 0) && (pHalData->mac_reg != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pHalData->mac_reg, pHalData->mac_reg_len);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/* Get 1st hex value as register offset */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xffff) {
						/* Ending. */
						break;
					}

					/* Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove))
						rtw_write8(Adapter, u4bRegOffset, (u8)u4bRegValue);
				}
			}
		}
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

int
phy_ConfigBBWithParaFile(
	IN	PADAPTER	Adapter,
	IN	char		*pFileName,
	IN	u32			ConfigType
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char	*szLine, *ptmp;
	u32	u4bRegOffset, u4bRegValue, u4bMove;
	char	*pBuf = NULL;
	u32	*pBufLen = NULL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_BB_PARA_FILE))
		return rtStatus;

	switch (ConfigType) {
	case CONFIG_BB_PHY_REG:
		pBuf = pHalData->bb_phy_reg;
		pBufLen = &pHalData->bb_phy_reg_len;
		break;
	case CONFIG_BB_AGC_TAB:
		pBuf = pHalData->bb_agc_tab;
		pBufLen = &pHalData->bb_agc_tab_len;
		break;
	default:
		RTW_INFO("Unknown ConfigType!! %d\r\n", ConfigType);
		break;
	}

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pBufLen != NULL) && (*pBufLen == 0) && (pBuf == NULL)) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pBuf = rtw_zvmalloc(rlen);
				if (pBuf) {
					_rtw_memcpy(pBuf, pHalData->para_file_buf, rlen);
					*pBufLen = rlen;

					switch (ConfigType) {
					case CONFIG_BB_PHY_REG:
						pHalData->bb_phy_reg = pBuf;
						break;
					case CONFIG_BB_AGC_TAB:
						pHalData->bb_agc_tab = pBuf;
						break;
					}
				} else
					RTW_INFO("%s(): ConfigType %d  alloc fail !\n", __FUNCTION__, ConfigType);
			}
		}
	} else {
		if ((pBufLen != NULL) && (*pBufLen != 0) && (pBuf != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pBuf, *pBufLen);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/* Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xffff) {
						/* Ending. */
						break;
					} else if (u4bRegOffset == 0xfe || u4bRegOffset == 0xffe) {
#ifdef CONFIG_LONG_DELAY_ISSUE
						rtw_msleep_os(50);
#else
						rtw_mdelay_os(50);
#endif
					} else if (u4bRegOffset == 0xfd)
						rtw_mdelay_os(5);
					else if (u4bRegOffset == 0xfc)
						rtw_mdelay_os(1);
					else if (u4bRegOffset == 0xfb)
						rtw_udelay_os(50);
					else if (u4bRegOffset == 0xfa)
						rtw_udelay_os(5);
					else if (u4bRegOffset == 0xf9)
						rtw_udelay_os(1);

					/* Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
						/* RTW_INFO("[BB-ADDR]%03lX=%08lX\n", u4bRegOffset, u4bRegValue); */
						phy_set_bb_reg(Adapter, u4bRegOffset, bMaskDWord, u4bRegValue);

						if (u4bRegOffset == 0xa24)
							pHalData->odmpriv.rf_calibrate_info.rega24 = u4bRegValue;

						/* Add 1us delay between BB/RF register setting. */
						rtw_udelay_os(1);
					}
				}
			}
		}
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

VOID
phy_DecryptBBPgParaFile(
	PADAPTER		Adapter,
	char			*buffer
)
{
	u32	i = 0, j = 0;
	u8	map[95] = {0};
	u8	currentChar;
	char	*BufOfLines, *ptmp;

	/* RTW_INFO("=====>phy_DecryptBBPgParaFile()\n"); */
	/* 32 the ascii code of the first visable char, 126 the last one */
	for (i = 0; i < 95; ++i)
		map[i] = (u8)(94 - i);

	ptmp = buffer;
	i = 0;
	for (BufOfLines = GetLineFromBuffer(ptmp); BufOfLines != NULL; BufOfLines = GetLineFromBuffer(ptmp)) {
		/* RTW_INFO("Encrypted Line: %s\n", BufOfLines); */

		for (j = 0; j < strlen(BufOfLines); ++j) {
			currentChar = BufOfLines[j];

			if (currentChar == '\0')
				break;

			currentChar -= (u8)((((i + j) * 3) % 128));

			BufOfLines[j] = map[currentChar - 32] + 32;
		}
		/* RTW_INFO("Decrypted Line: %s\n", BufOfLines ); */
		if (strlen(BufOfLines) != 0)
			i++;
		BufOfLines[strlen(BufOfLines)] = '\n';
	}
}

int
phy_ParseBBPgParaFile(
	PADAPTER		Adapter,
	char			*buffer
)
{
	int	rtStatus = _SUCCESS;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	char	*szLine, *ptmp;
	u32	u4bRegOffset, u4bRegMask, u4bRegValue;
	u32	u4bMove;
	BOOLEAN firstLine = _TRUE;
	u8	tx_num = 0;
	u8	band = 0, rf_path = 0;

	/* RTW_INFO("=====>phy_ParseBBPgParaFile()\n"); */

	if (Adapter->registrypriv.RegDecryptCustomFile == 1)
		phy_DecryptBBPgParaFile(Adapter, buffer);

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		if (isAllSpaceOrTab(szLine, sizeof(*szLine)))
			continue;

		if (!IsCommentString(szLine)) {
			/* Get header info (relative value or exact value) */
			if (firstLine) {
				if (eqNByte(szLine, (u8 *)("#[v1]"), 5)) {

					pHalData->odmpriv.phy_reg_pg_version = szLine[3] - '0';
					/* RTW_INFO("This is a new format PHY_REG_PG.txt\n"); */
				} else if (eqNByte(szLine, (u8 *)("#[v0]"), 5)) {
					pHalData->odmpriv.phy_reg_pg_version = szLine[3] - '0';
					/* RTW_INFO("This is a old format PHY_REG_PG.txt ok\n"); */
				} else {
					RTW_INFO("The format in PHY_REG_PG are invalid %s\n", szLine);
					return _FAIL;
				}

				if (eqNByte(szLine + 5, (u8 *)("[Exact]#"), 8)) {
					pHalData->odmpriv.phy_reg_pg_value_type = PHY_REG_PG_EXACT_VALUE;
					/* RTW_INFO("The values in PHY_REG_PG are exact values ok\n"); */
					firstLine = _FALSE;
					continue;
				} else if (eqNByte(szLine + 5, (pu1Byte)("[Relative]#"), 11)) {
					pHalData->odmpriv.phy_reg_pg_value_type = PHY_REG_PG_RELATIVE_VALUE;
					/* RTW_INFO("The values in PHY_REG_PG are relative values ok\n"); */
					firstLine = _FALSE;
					continue;
				} else {
					RTW_INFO("The values in PHY_REG_PG are invalid %s\n", szLine);
					return _FAIL;
				}
			}

			if (pHalData->odmpriv.phy_reg_pg_version == 0) {
				/* Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					szLine += u4bMove;
					if (u4bRegOffset == 0xffff) {
						/* Ending. */
						break;
					}

					/* Get 2nd hex value as register mask. */
					if (GetHexValueFromString(szLine, &u4bRegMask, &u4bMove))
						szLine += u4bMove;
					else
						return _FAIL;

					if (pHalData->odmpriv.phy_reg_pg_value_type == PHY_REG_PG_RELATIVE_VALUE) {
						/* Get 3rd hex value as register value. */
						if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
							phy_store_tx_power_by_rate(Adapter, 0, 0, 1, u4bRegOffset, u4bRegMask, u4bRegValue);
							/* RTW_INFO("[ADDR] %03X=%08X Mask=%08x\n", u4bRegOffset, u4bRegValue, u4bRegMask); */
						} else
							return _FAIL;
					} else if (pHalData->odmpriv.phy_reg_pg_value_type == PHY_REG_PG_EXACT_VALUE) {
						u32	combineValue = 0;
						u8	integer = 0, fraction = 0;

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* RTW_INFO(" %d", integer ); */

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue <<= 8;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* RTW_INFO(" %d", integer ); */

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue <<= 8;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* RTW_INFO(" %d", integer ); */

						if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						integer *= 2;
						if (fraction == 5)
							integer += 1;
						combineValue <<= 8;
						combineValue |= (((integer / 10) << 4) + (integer % 10));
						/* RTW_INFO(" %d", integer ); */
						phy_store_tx_power_by_rate(Adapter, 0, 0, 1, u4bRegOffset, u4bRegMask, combineValue);

						/* RTW_INFO("[ADDR] 0x%3x = 0x%4x\n", u4bRegOffset, combineValue ); */
					}
				}
			} else if (pHalData->odmpriv.phy_reg_pg_version > 0) {
				u32	index = 0, cnt = 0;

				if (eqNByte(szLine, "0xffff", 6))
					break;

				if (!eqNByte("#[END]#", szLine, 7)) {
					/* load the table label info */
					if (szLine[0] == '#') {
						index = 0;
						if (eqNByte(szLine, "#[2.4G]" , 7)) {
							band = BAND_ON_2_4G;
							index += 8;
						} else if (eqNByte(szLine, "#[5G]", 5)) {
							band = BAND_ON_5G;
							index += 6;
						} else {
							RTW_INFO("Invalid band %s in PHY_REG_PG.txt\n", szLine);
							return _FAIL;
						}

						rf_path = szLine[index] - 'A';
						/* RTW_INFO(" Table label Band %d, RfPath %d\n", band, rf_path ); */
					} else { /* load rows of tables */
						if (szLine[1] == '1')
							tx_num = RF_1TX;
						else if (szLine[1] == '2')
							tx_num = RF_2TX;
						else if (szLine[1] == '3')
							tx_num = RF_3TX;
						else if (szLine[1] == '4')
							tx_num = RF_4TX;
						else {
							RTW_INFO("Invalid row in PHY_REG_PG.txt '%c'(%d)\n", szLine[1], szLine[1]);
							return _FAIL;
						}

						while (szLine[index] != ']')
							++index;
						++index;/* skip ] */

						/* Get 2nd hex value as register offset. */
						szLine += index;
						if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						/* Get 2nd hex value as register mask. */
						if (GetHexValueFromString(szLine, &u4bRegMask, &u4bMove))
							szLine += u4bMove;
						else
							return _FAIL;

						if (pHalData->odmpriv.phy_reg_pg_value_type == PHY_REG_PG_RELATIVE_VALUE) {
							/* Get 3rd hex value as register value. */
							if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
								phy_store_tx_power_by_rate(Adapter, band, rf_path, tx_num, u4bRegOffset, u4bRegMask, u4bRegValue);
								/* RTW_INFO("[ADDR] %03X (tx_num %d) =%08X Mask=%08x\n", u4bRegOffset, tx_num, u4bRegValue, u4bRegMask); */
							} else
								return _FAIL;
						} else if (pHalData->odmpriv.phy_reg_pg_value_type == PHY_REG_PG_EXACT_VALUE) {
							u32	combineValue = 0;
							u8	integer = 0, fraction = 0;

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* RTW_INFO(" %d", integer ); */

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue <<= 8;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* RTW_INFO(" %d", integer ); */

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue <<= 8;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* RTW_INFO(" %d", integer ); */

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								return _FAIL;

							integer *= 2;
							if (fraction == 5)
								integer += 1;
							combineValue <<= 8;
							combineValue |= (((integer / 10) << 4) + (integer % 10));
							/* RTW_INFO(" %d", integer ); */
							phy_store_tx_power_by_rate(Adapter, band, rf_path, tx_num, u4bRegOffset, u4bRegMask, combineValue);

							/* RTW_INFO("[ADDR] 0x%3x (tx_num %d) = 0x%4x\n", u4bRegOffset, tx_num, combineValue ); */
						}
					}
				}
			}
		}
	}
	/* RTW_INFO("<=====phy_ParseBBPgParaFile()\n"); */
	return rtStatus;
}

int
phy_ConfigBBWithPgParaFile(
	IN	PADAPTER	Adapter,
	IN	const char	*pFileName)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_BB_PG_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if (pHalData->bb_phy_reg_pg == NULL) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->bb_phy_reg_pg = rtw_zvmalloc(rlen);
				if (pHalData->bb_phy_reg_pg) {
					_rtw_memcpy(pHalData->bb_phy_reg_pg, pHalData->para_file_buf, rlen);
					pHalData->bb_phy_reg_pg_len = rlen;
				} else
					RTW_INFO("%s bb_phy_reg_pg alloc fail !\n", __FUNCTION__);
			}
		}
	} else {
		if ((pHalData->bb_phy_reg_pg_len != 0) && (pHalData->bb_phy_reg_pg != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pHalData->bb_phy_reg_pg, pHalData->bb_phy_reg_pg_len);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		/* RTW_INFO("phy_ConfigBBWithPgParaFile(): read %s ok\n", pFileName); */
		phy_ParseBBPgParaFile(Adapter, pHalData->para_file_buf);
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

#if (MP_DRIVER == 1)

int
phy_ConfigBBWithMpParaFile(
	IN	PADAPTER	Adapter,
	IN	char		*pFileName
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char	*szLine, *ptmp;
	u32	u4bRegOffset, u4bRegValue, u4bMove;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_BB_MP_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->bb_phy_reg_mp_len == 0) && (pHalData->bb_phy_reg_mp == NULL)) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->bb_phy_reg_mp = rtw_zvmalloc(rlen);
				if (pHalData->bb_phy_reg_mp) {
					_rtw_memcpy(pHalData->bb_phy_reg_mp, pHalData->para_file_buf, rlen);
					pHalData->bb_phy_reg_mp_len = rlen;
				} else
					RTW_INFO("%s bb_phy_reg_mp alloc fail !\n", __FUNCTION__);
			}
		}
	} else {
		if ((pHalData->bb_phy_reg_mp_len != 0) && (pHalData->bb_phy_reg_mp != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pHalData->bb_phy_reg_mp, pHalData->bb_phy_reg_mp_len);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		/* RTW_INFO("phy_ConfigBBWithMpParaFile(): read %s ok\n", pFileName); */

		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/* Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xffff) {
						/* Ending. */
						break;
					} else if (u4bRegOffset == 0xfe || u4bRegOffset == 0xffe) {
#ifdef CONFIG_LONG_DELAY_ISSUE
						rtw_msleep_os(50);
#else
						rtw_mdelay_os(50);
#endif
					} else if (u4bRegOffset == 0xfd)
						rtw_mdelay_os(5);
					else if (u4bRegOffset == 0xfc)
						rtw_mdelay_os(1);
					else if (u4bRegOffset == 0xfb)
						rtw_udelay_os(50);
					else if (u4bRegOffset == 0xfa)
						rtw_udelay_os(5);
					else if (u4bRegOffset == 0xf9)
						rtw_udelay_os(1);

					/* Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
						/* RTW_INFO("[ADDR]%03lX=%08lX\n", u4bRegOffset, u4bRegValue); */
						phy_set_bb_reg(Adapter, u4bRegOffset, bMaskDWord, u4bRegValue);

						/* Add 1us delay between BB/RF register setting. */
						rtw_udelay_os(1);
					}
				}
			}
		}
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

#endif

int
PHY_ConfigRFWithParaFile(
	IN	PADAPTER	Adapter,
	IN	char		*pFileName,
	IN	u8			eRFPath
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;
	char	*szLine, *ptmp;
	u32	u4bRegOffset, u4bRegValue, u4bMove;
	u16	i;
	char	*pBuf = NULL;
	u32	*pBufLen = NULL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_PARA_FILE))
		return rtStatus;

	switch (eRFPath) {
	case ODM_RF_PATH_A:
		pBuf = pHalData->rf_radio_a;
		pBufLen = &pHalData->rf_radio_a_len;
		break;
	case ODM_RF_PATH_B:
		pBuf = pHalData->rf_radio_b;
		pBufLen = &pHalData->rf_radio_b_len;
		break;
	default:
		RTW_INFO("Unknown RF path!! %d\r\n", eRFPath);
		break;
	}

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pBufLen != NULL) && (*pBufLen == 0) && (pBuf == NULL)) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pBuf = rtw_zvmalloc(rlen);
				if (pBuf) {
					_rtw_memcpy(pBuf, pHalData->para_file_buf, rlen);
					*pBufLen = rlen;

					switch (eRFPath) {
					case ODM_RF_PATH_A:
						pHalData->rf_radio_a = pBuf;
						break;
					case ODM_RF_PATH_B:
						pHalData->rf_radio_b = pBuf;
						break;
					}
				} else
					RTW_INFO("%s(): eRFPath=%d  alloc fail !\n", __FUNCTION__, eRFPath);
			}
		}
	} else {
		if ((pBufLen != NULL) && (*pBufLen != 0) && (pBuf != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pBuf, *pBufLen);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		/* RTW_INFO("%s(): read %s successfully\n", __FUNCTION__, pFileName); */

		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				/* Get 1st hex value as register offset. */
				if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove)) {
					if (u4bRegOffset == 0xfe || u4bRegOffset == 0xffe) {
						/* Deay specific ms. Only RF configuration require delay.												 */
#ifdef CONFIG_LONG_DELAY_ISSUE
						rtw_msleep_os(50);
#else
						rtw_mdelay_os(50);
#endif
					} else if (u4bRegOffset == 0xfd) {
						/* delay_ms(5); */
						for (i = 0; i < 100; i++)
							rtw_udelay_os(MAX_STALL_TIME);
					} else if (u4bRegOffset == 0xfc) {
						/* delay_ms(1); */
						for (i = 0; i < 20; i++)
							rtw_udelay_os(MAX_STALL_TIME);
					} else if (u4bRegOffset == 0xfb)
						rtw_udelay_os(50);
					else if (u4bRegOffset == 0xfa)
						rtw_udelay_os(5);
					else if (u4bRegOffset == 0xf9)
						rtw_udelay_os(1);
					else if (u4bRegOffset == 0xffff)
						break;

					/* Get 2nd hex value as register value. */
					szLine += u4bMove;
					if (GetHexValueFromString(szLine, &u4bRegValue, &u4bMove)) {
						phy_set_rf_reg(Adapter, eRFPath, u4bRegOffset, bRFRegOffsetMask, u4bRegValue);

						/* Temp add, for frequency lock, if no delay, that may cause */
						/* frequency shift, ex: 2412MHz => 2417MHz */
						/* If frequency shift, the following action may works. */
						/* Fractional-N table in radio_a.txt */
						/* 0x2a 0x00001		 */ /* channel 1 */
						/* 0x2b 0x00808		frequency divider. */
						/* 0x2b 0x53333 */
						/* 0x2c 0x0000c */
						rtw_udelay_os(1);
					}
				}
			}
		}
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

VOID
initDeltaSwingIndexTables(
	PADAPTER	Adapter,
	char		*Band,
	char		*Path,
	char		*Sign,
	char		*Channel,
	char		*Rate,
	char		*Data
)
{
#define STR_EQUAL_5G(_band, _path, _sign, _rate, _chnl) \
	((strcmp(Band, _band) == 0) && (strcmp(Path, _path) == 0) && (strcmp(Sign, _sign) == 0) &&\
	 (strcmp(Rate, _rate) == 0) && (strcmp(Channel, _chnl) == 0)\
	)
#define STR_EQUAL_2G(_band, _path, _sign, _rate) \
	((strcmp(Band, _band) == 0) && (strcmp(Path, _path) == 0) && (strcmp(Sign, _sign) == 0) &&\
	 (strcmp(Rate, _rate) == 0)\
	)

#define STORE_SWING_TABLE(_array, _iteratedIdx) \
	do {	\
	for (token = strsep(&Data, delim); token != NULL; token = strsep(&Data, delim)) {\
		sscanf(token, "%d", &idx);\
		_array[_iteratedIdx++] = (u8)idx;\
	} } while (0)\

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &pHalData->odmpriv;
	struct odm_rf_calibration_structure	*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);
	u32	j = 0;
	char	*token;
	char	delim[] = ",";
	u32	idx = 0;

	/* RTW_INFO("===>initDeltaSwingIndexTables(): Band: %s;\nPath: %s;\nSign: %s;\nChannel: %s;\nRate: %s;\n, Data: %s;\n",  */
	/*	Band, Path, Sign, Channel, Rate, Data); */

	if (STR_EQUAL_2G("2G", "A", "+", "CCK"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2g_cck_a_p, j);
	else if (STR_EQUAL_2G("2G", "A", "-", "CCK"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2g_cck_a_n, j);
	else if (STR_EQUAL_2G("2G", "B", "+", "CCK"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2g_cck_b_p, j);
	else if (STR_EQUAL_2G("2G", "B", "-", "CCK"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2g_cck_b_n, j);
	else if (STR_EQUAL_2G("2G", "A", "+", "ALL"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2ga_p, j);
	else if (STR_EQUAL_2G("2G", "A", "-", "ALL"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2ga_n, j);
	else if (STR_EQUAL_2G("2G", "B", "+", "ALL"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2gb_p, j);
	else if (STR_EQUAL_2G("2G", "B", "-", "ALL"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_2gb_n, j);
	else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "0"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_p[0], j);
	else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "0"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_n[0], j);
	else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "0"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_p[0], j);
	else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "0"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_n[0], j);
	else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "1"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_p[1], j);
	else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "1"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_n[1], j);
	else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "1"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_p[1], j);
	else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "1"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_n[1], j);
	else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "2"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_p[2], j);
	else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "2"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_n[2], j);
	else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "2"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_p[2], j);
	else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "2"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_n[2], j);
	else if (STR_EQUAL_5G("5G", "A", "+", "ALL", "3"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_p[3], j);
	else if (STR_EQUAL_5G("5G", "A", "-", "ALL", "3"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5ga_n[3], j);
	else if (STR_EQUAL_5G("5G", "B", "+", "ALL", "3"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_p[3], j);
	else if (STR_EQUAL_5G("5G", "B", "-", "ALL", "3"))
		STORE_SWING_TABLE(pRFCalibrateInfo->delta_swing_table_idx_5gb_n[3], j);
	else
		RTW_INFO("===>initDeltaSwingIndexTables(): The input is invalid!!\n");
}

int
PHY_ConfigRFWithTxPwrTrackParaFile(
	IN	PADAPTER		Adapter,
	IN	char			*pFileName
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT			*pDM_Odm = &pHalData->odmpriv;
	struct odm_rf_calibration_structure		*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);
	int	rlen = 0, rtStatus = _FAIL;
	char	*szLine, *ptmp;
	u32	i = 0, j = 0;
	char	c = 0;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_TXPWR_TRACK_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->rf_tx_pwr_track_len == 0) && (pHalData->rf_tx_pwr_track == NULL)) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->rf_tx_pwr_track = rtw_zvmalloc(rlen);
				if (pHalData->rf_tx_pwr_track) {
					_rtw_memcpy(pHalData->rf_tx_pwr_track, pHalData->para_file_buf, rlen);
					pHalData->rf_tx_pwr_track_len = rlen;
				} else
					RTW_INFO("%s rf_tx_pwr_track alloc fail !\n", __FUNCTION__);
			}
		}
	} else {
		if ((pHalData->rf_tx_pwr_track_len != 0) && (pHalData->rf_tx_pwr_track != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pHalData->rf_tx_pwr_track, pHalData->rf_tx_pwr_track_len);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		/* RTW_INFO("%s(): read %s successfully\n", __FUNCTION__, pFileName); */

		ptmp = pHalData->para_file_buf;
		for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
			if (!IsCommentString(szLine)) {
				char	band[5] = "", path[5] = "", sign[5]  = "";
				char	chnl[5] = "", rate[10] = "";
				char	data[300] = ""; /* 100 is too small */

				if (strlen(szLine) < 10 || szLine[0] != '[')
					continue;

				strncpy(band, szLine + 1, 2);
				strncpy(path, szLine + 5, 1);
				strncpy(sign, szLine + 8, 1);

				i = 10; /* szLine+10 */
				if (!ParseQualifiedString(szLine, &i, rate, '[', ']')) {
					/* RTW_INFO("Fail to parse rate!\n"); */
				}
				if (!ParseQualifiedString(szLine, &i, chnl, '[', ']')) {
					/* RTW_INFO("Fail to parse channel group!\n"); */
				}
				while (szLine[i] != '{' && i < strlen(szLine))
					i++;
				if (!ParseQualifiedString(szLine, &i, data, '{', '}')) {
					/* RTW_INFO("Fail to parse data!\n"); */
				}

				initDeltaSwingIndexTables(Adapter, band, path, sign, chnl, rate, data);
			}
		}
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);
#if 0
	for (i = 0; i < DELTA_SWINGIDX_SIZE; ++i) {
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2ga_p[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2ga_p[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2ga_n[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2ga_n[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2gb_p[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2gb_p[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2gb_n[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2gb_n[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2g_cck_a_p[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2g_cck_a_p[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2g_cck_a_n[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2g_cck_a_n[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2g_cck_b_p[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2g_cck_b_p[i]);
		RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_2g_cck_b_n[%d] = %d\n", i, pRFCalibrateInfo->delta_swing_table_idx_2g_cck_b_n[i]);

		for (j = 0; j < 3; ++j) {
			RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_5ga_p[%d][%d] = %d\n", j, i, pRFCalibrateInfo->delta_swing_table_idx_5ga_p[j][i]);
			RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_5ga_n[%d][%d] = %d\n", j, i, pRFCalibrateInfo->delta_swing_table_idx_5ga_n[j][i]);
			RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_5gb_p[%d][%d] = %d\n", j, i, pRFCalibrateInfo->delta_swing_table_idx_5gb_p[j][i]);
			RTW_INFO("pRFCalibrateInfo->delta_swing_table_idx_5gb_n[%d][%d] = %d\n", j, i, pRFCalibrateInfo->delta_swing_table_idx_5gb_n[j][i]);
		}
	}
#endif
	return rtStatus;
}

int
phy_ParsePowerLimitTableFile(
	PADAPTER		Adapter,
	char			*buffer
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT	*pDM_Odm = &(pHalData->odmpriv);
	u32	i = 0, forCnt = 0;
	u8	loadingStage = 0, limitValue = 0, fraction = 0;
	char	*szLine, *ptmp;
	int	rtStatus = _SUCCESS;
	char band[10], bandwidth[10], rateSection[10],
	     regulation[TXPWR_LMT_MAX_REGULATION_NUM][10], rfPath[10], colNumBuf[10];
	u8	colNum = 0;

	RTW_INFO("===>phy_ParsePowerLimitTableFile()\n");

	if (Adapter->registrypriv.RegDecryptCustomFile == 1)
		phy_DecryptBBPgParaFile(Adapter, buffer);

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		if (isAllSpaceOrTab(szLine, sizeof(*szLine)))
			continue;

		/* skip comment */
		if (IsCommentString(szLine))
			continue;

		if (loadingStage == 0) {
			for (forCnt = 0; forCnt < TXPWR_LMT_MAX_REGULATION_NUM; ++forCnt)
				_rtw_memset((PVOID) regulation[forCnt], 0, 10);
			_rtw_memset((PVOID) band, 0, 10);
			_rtw_memset((PVOID) bandwidth, 0, 10);
			_rtw_memset((PVOID) rateSection, 0, 10);
			_rtw_memset((PVOID) rfPath, 0, 10);
			_rtw_memset((PVOID) colNumBuf, 0, 10);

			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			szLine[--i] = ' '; /* return the space in front of the regulation info */

			/* Parse the label of the table */
			if (!ParseQualifiedString(szLine, &i, band, ' ', ',')) {
				RTW_INFO("Fail to parse band!\n");
				return _FAIL;
			}
			if (!ParseQualifiedString(szLine, &i, bandwidth, ' ', ',')) {
				RTW_INFO("Fail to parse bandwidth!\n");
				return _FAIL;
			}
			if (!ParseQualifiedString(szLine, &i, rfPath, ' ', ',')) {
				RTW_INFO("Fail to parse rf path!\n");
				return _FAIL;
			}
			if (!ParseQualifiedString(szLine, &i, rateSection, ' ', ',')) {
				RTW_INFO("Fail to parse rate!\n");
				return _FAIL;
			}

			loadingStage = 1;
		} else if (loadingStage == 1) {
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			if (!eqNByte((u8 *)(szLine + i), (u8 *)("START"), 5)) {
				RTW_INFO("Lost \"##   START\" label\n");
				return _FAIL;
			}

			loadingStage = 2;
		} else if (loadingStage == 2) {
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			if (!ParseQualifiedString(szLine, &i, colNumBuf, '#', '#')) {
				RTW_INFO("Fail to parse column number!\n");
				return _FAIL;
			}

			if (!GetU1ByteIntegerFromStringInDecimal(colNumBuf, &colNum))
				return _FAIL;

			if (colNum > TXPWR_LMT_MAX_REGULATION_NUM) {
				RTW_INFO("unvalid col number %d (greater than max %d)\n",
					 colNum, TXPWR_LMT_MAX_REGULATION_NUM);
				return _FAIL;
			}

			for (forCnt = 0; forCnt < colNum; ++forCnt) {
				u8	regulation_name_cnt = 0;

				/* skip the space */
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				while (szLine[i] != ' ' && szLine[i] != '\t' && szLine[i] != '\0')
					regulation[forCnt][regulation_name_cnt++] = szLine[i++];
				/* RTW_INFO("regulation %s!\n", regulation[forCnt]); */

				if (regulation_name_cnt == 0) {
					RTW_INFO("unvalid number of regulation!\n");
					return _FAIL;
				}
			}

			loadingStage = 3;
		} else if (loadingStage == 3) {
			char	channel[10] = {0}, powerLimit[10] = {0};
			u8	cnt = 0;

			/* the table ends */
			if (szLine[0] == '#' && szLine[1] == '#') {
				i = 2;
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				if (eqNByte((u8 *)(szLine + i), (u8 *)("END"), 3)) {
					loadingStage = 0;
					continue;
				} else {
					RTW_INFO("Wrong format\n");
					RTW_INFO("<===== phy_ParsePowerLimitTableFile()\n");
					return _FAIL;
				}
			}

			if ((szLine[0] != 'c' && szLine[0] != 'C') ||
			    (szLine[1] != 'h' && szLine[1] != 'H')) {
				RTW_INFO("Meet wrong channel => power limt pair '%c','%c'(%d,%d)\n", szLine[0], szLine[1], szLine[0], szLine[1]);
				continue;
			}
			i = 2;/* move to the  location behind 'h' */

			/* load the channel number */
			cnt = 0;
			while (szLine[i] >= '0' && szLine[i] <= '9') {
				channel[cnt] = szLine[i];
				++cnt;
				++i;
			}
			/* RTW_INFO("chnl %s!\n", channel); */

			for (forCnt = 0; forCnt < colNum; ++forCnt) {
				/* skip the space between channel number and the power limit value */
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				/* load the power limit value */
				cnt = 0;
				fraction = 0;
				_rtw_memset((PVOID) powerLimit, 0, 10);
				while ((szLine[i] >= '0' && szLine[i] <= '9') || szLine[i] == '.') {
					if (szLine[i] == '.') {
						if ((szLine[i + 1] >= '0' && szLine[i + 1] <= '9')) {
							fraction = szLine[i + 1];
							i += 2;
						} else {
							RTW_INFO("Wrong fraction in TXPWR_LMT.txt\n");
							return _FAIL;
						}

						break;
					}

					powerLimit[cnt] = szLine[i];
					++cnt;
					++i;
				}

				if (powerLimit[0] == '\0') {
					powerLimit[0] = '6';
					powerLimit[1] = '3';
					i += 2;
				} else {
					if (!GetU1ByteIntegerFromStringInDecimal(powerLimit, &limitValue))
						return _FAIL;

					limitValue *= 2;
					cnt = 0;
					if (fraction == '5')
						++limitValue;

					/* the value is greater or equal to 100 */
					if (limitValue >= 100) {
						powerLimit[cnt++] = limitValue / 100 + '0';
						limitValue %= 100;

						if (limitValue >= 10) {
							powerLimit[cnt++] = limitValue / 10 + '0';
							limitValue %= 10;
						} else
							powerLimit[cnt++] = '0';

						powerLimit[cnt++] = limitValue + '0';
					}
					/* the value is greater or equal to 10 */
					else if (limitValue >= 10) {
						powerLimit[cnt++] = limitValue / 10 + '0';
						limitValue %= 10;
						powerLimit[cnt++] = limitValue + '0';
					}
					/* the value is less than 10 */
					else
						powerLimit[cnt++] = limitValue + '0';

					powerLimit[cnt] = '\0';
				}

				/* RTW_INFO("ch%s => %s\n", channel, powerLimit); */

				/* store the power limit value */
				phy_set_tx_power_limit(pDM_Odm, (u8 *)regulation[forCnt], (u8 *)band,
					(u8 *)bandwidth, (u8 *)rateSection, (u8 *)rfPath, (u8 *)channel, (u8 *)powerLimit);

			}
		} else {
			RTW_INFO("Abnormal loading stage in phy_ParsePowerLimitTableFile()!\n");
			rtStatus = _FAIL;
			break;
		}
	}

	RTW_INFO("<===phy_ParsePowerLimitTableFile()\n");
	return rtStatus;
}

int
PHY_ConfigRFWithPowerLimitTableParaFile(
	IN	PADAPTER	Adapter,
	IN	const char	*pFileName
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_TXPWR_LMT_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if (pHalData->rf_tx_pwr_lmt == NULL) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_is_file_readable(rtw_phy_para_file_path) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pHalData->rf_tx_pwr_lmt = rtw_zvmalloc(rlen);
				if (pHalData->rf_tx_pwr_lmt) {
					_rtw_memcpy(pHalData->rf_tx_pwr_lmt, pHalData->para_file_buf, rlen);
					pHalData->rf_tx_pwr_lmt_len = rlen;
				} else
					RTW_INFO("%s rf_tx_pwr_lmt alloc fail !\n", __FUNCTION__);
			}
		}
	} else {
		if ((pHalData->rf_tx_pwr_lmt_len != 0) && (pHalData->rf_tx_pwr_lmt != NULL)) {
			_rtw_memcpy(pHalData->para_file_buf, pHalData->rf_tx_pwr_lmt, pHalData->rf_tx_pwr_lmt_len);
			rtStatus = _SUCCESS;
		} else
			RTW_INFO("%s(): Critical Error !!!\n", __FUNCTION__);
	}

	if (rtStatus == _SUCCESS) {
		/* RTW_INFO("%s(): read %s ok\n", __FUNCTION__, pFileName); */
		rtStatus = phy_ParsePowerLimitTableFile(Adapter, pHalData->para_file_buf);
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

void phy_free_filebuf_mask(_adapter *padapter, u8 mask)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->mac_reg && (mask & LOAD_MAC_PARA_FILE)) {
		rtw_vmfree(pHalData->mac_reg, pHalData->mac_reg_len);
		pHalData->mac_reg = NULL;
	}
	if (mask & LOAD_BB_PARA_FILE) {
		if (pHalData->bb_phy_reg) {
			rtw_vmfree(pHalData->bb_phy_reg, pHalData->bb_phy_reg_len);
			pHalData->bb_phy_reg = NULL;
		}
		if (pHalData->bb_agc_tab) {
			rtw_vmfree(pHalData->bb_agc_tab, pHalData->bb_agc_tab_len);
			pHalData->bb_agc_tab = NULL;
		}
	}
	if (pHalData->bb_phy_reg_pg && (mask & LOAD_BB_PG_PARA_FILE)) {
		rtw_vmfree(pHalData->bb_phy_reg_pg, pHalData->bb_phy_reg_pg_len);
		pHalData->bb_phy_reg_pg = NULL;
	}
	if (pHalData->bb_phy_reg_mp && (mask & LOAD_BB_MP_PARA_FILE)) {
		rtw_vmfree(pHalData->bb_phy_reg_mp, pHalData->bb_phy_reg_mp_len);
		pHalData->bb_phy_reg_mp = NULL;
	}
	if (mask & LOAD_RF_PARA_FILE) {
		if (pHalData->rf_radio_a) {
			rtw_vmfree(pHalData->rf_radio_a, pHalData->rf_radio_a_len);
			pHalData->rf_radio_a = NULL;
		}
		if (pHalData->rf_radio_b) {
			rtw_vmfree(pHalData->rf_radio_b, pHalData->rf_radio_b_len);
			pHalData->rf_radio_b = NULL;
		}
	}
	if (pHalData->rf_tx_pwr_track && (mask & LOAD_RF_TXPWR_TRACK_PARA_FILE)) {
		rtw_vmfree(pHalData->rf_tx_pwr_track, pHalData->rf_tx_pwr_track_len);
		pHalData->rf_tx_pwr_track = NULL;
	}
	if (pHalData->rf_tx_pwr_lmt && (mask & LOAD_RF_TXPWR_LMT_PARA_FILE)) {
		rtw_vmfree(pHalData->rf_tx_pwr_lmt, pHalData->rf_tx_pwr_lmt_len);
		pHalData->rf_tx_pwr_lmt = NULL;
	}
}

inline void phy_free_filebuf(_adapter *padapter)
{
	phy_free_filebuf_mask(padapter, 0xFF);
}

#endif
