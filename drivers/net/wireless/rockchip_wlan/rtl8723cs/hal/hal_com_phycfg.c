/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _HAL_COM_PHYCFG_C_

#include <drv_types.h>
#include <hal_data.h>

#define PG_TXPWR_1PATH_BYTE_NUM_2G 18
#define PG_TXPWR_BASE_BYTE_NUM_2G 11

#define PG_TXPWR_1PATH_BYTE_NUM_5G 24
#define PG_TXPWR_BASE_BYTE_NUM_5G 14

#define PG_TXPWR_MSB_DIFF_S4BIT(_pg_v) (((_pg_v) & 0xf0) >> 4)
#define PG_TXPWR_LSB_DIFF_S4BIT(_pg_v) ((_pg_v) & 0x0f)
#define PG_TXPWR_MSB_DIFF_TO_S8BIT(_pg_v) ((PG_TXPWR_MSB_DIFF_S4BIT(_pg_v) & BIT3) ? (PG_TXPWR_MSB_DIFF_S4BIT(_pg_v) | 0xF0) : PG_TXPWR_MSB_DIFF_S4BIT(_pg_v))
#define PG_TXPWR_LSB_DIFF_TO_S8BIT(_pg_v) ((PG_TXPWR_LSB_DIFF_S4BIT(_pg_v) & BIT3) ? (PG_TXPWR_LSB_DIFF_S4BIT(_pg_v) | 0xF0) : PG_TXPWR_LSB_DIFF_S4BIT(_pg_v))
#define IS_PG_TXPWR_BASE_INVALID(hal_spec, _base) ((_base) > hal_spec->txgi_max)
#define IS_PG_TXPWR_DIFF_INVALID(_diff) ((_diff) > 7 || (_diff) < -8)
#define PG_TXPWR_INVALID_BASE 255
#define PG_TXPWR_INVALID_DIFF 8

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

const char *const _txpwr_pg_mode_str[] = {
	"PWR_IDX",
	"TSSI_OFFSET",
	"UNKNOWN",
};

static const u8 rate_sec_base[RATE_SECTION_NUM] = {
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

#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
typedef struct _TxPowerInfo24G {
	u8 IndexCCK_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	u8 IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	/* If only one tx, only BW20 and OFDM are used. */
	s8 CCK_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
} TxPowerInfo24G;

typedef struct _TxPowerInfo5G {
	u8 IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_5G];
	/* If only one tx, only BW20, OFDM, BW80 and BW160 are used. */
	s8 OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW80_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW160_Diff[MAX_RF_PATH][MAX_TX_COUNT];
} TxPowerInfo5G;

#ifndef DBG_PG_TXPWR_READ
#define DBG_PG_TXPWR_READ 0
#endif

#if DBG_PG_TXPWR_READ
static void dump_pg_txpwr_info_2g(void *sel, TxPowerInfo24G *txpwr_info, u8 rfpath_num, u8 max_tx_cnt)
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

static void dump_pg_txpwr_info_5g(void *sel, TxPowerInfo5G *txpwr_info, u8 rfpath_num, u8 max_tx_cnt)
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
#endif /* DBG_PG_TXPWR_READ */

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

#ifdef CONFIG_RTL8188GTV
static const struct map_t rtl8188gtv_pg_txpwr_def_info =
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

#ifdef CONFIG_RTL8710B
static const struct map_t rtl8710b_pg_txpwr_def_info =
	MAP_ENT(0xC8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x20, 12,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x20)
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

#ifdef CONFIG_RTL8822C
static const struct map_t rtl8822c_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 82,
			0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x02, 0x00, 0x00, 0xFF, 0xFF,
			0xFF, 0xFF, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
			0x02, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
			0x33, 0x33, 0x33, 0x33, 0x33, 0x02, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x33, 0x33, 0x33, 0x33,
			0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x02, 0x00, 0xFF, 0xFF, 0x00, 0xFF,
			0x00, 0x00)
	);
#endif

/* todo : 8723f don't know default power */
#ifdef CONFIG_RTL8723F
static const struct map_t rtl8723f_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 82,
			0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x02, 0x00, 0x00, 0xFF, 0xFF,
			0xFF, 0xFF, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
			0x02, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
			0x33, 0x33, 0x33, 0x33, 0x33, 0x02, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x33, 0x33, 0x33, 0x33,
			0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x02, 0x00, 0xFF, 0xFF, 0x00, 0xFF,
			0x00, 0x00)
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

#ifdef CONFIG_RTL8192F/*use 8192F default,no document*/
static const struct map_t rtl8192f_pg_txpwr_def_info =
	MAP_ENT(0xB8, 2, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 14,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE)
		, MAPSEG_ARRAY_ENT(0x3A, 14,
			0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x24, 0xEE, 0xEE)
	);
#endif

#ifdef CONFIG_RTL8814B
static const struct map_t rtl8814b_pg_txpwr_def_info =
	MAP_ENT(0xB8, 1, 0xFF
		, MAPSEG_ARRAY_ENT(0x10, 168,
			0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x02, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
			0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEC, 0xFF, 0xFF, 0xFF, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28,
			0x28, 0x28, 0x28, 0x28, 0x28, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF)
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
#ifdef CONFIG_RTL8188GTV
	case RTL8188GTV:
		map = &rtl8188gtv_pg_txpwr_def_info;
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
#ifdef CONFIG_RTL8710B
	case RTL8710B:
		map = &rtl8710b_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8192F
	case RTL8192F:
		map = &rtl8192f_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8822C
	case RTL8822C:
		map = &rtl8822c_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8814B
	case RTL8814B:
		map = &rtl8814b_pg_txpwr_def_info;
		break;
#endif
#ifdef CONFIG_RTL8723F
	case RTL8723F:
		map = &rtl8723f_pg_txpwr_def_info;
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
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path, group, tx_idx;

	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_2G))
		return _SUCCESS;

	for (path = 0; path < MAX_RF_PATH; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path))
			continue;
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			if (IS_PG_TXPWR_BASE_INVALID(hal_spec, pwr_info->IndexCCK_Base[path][group])
				|| IS_PG_TXPWR_BASE_INVALID(hal_spec, pwr_info->IndexBW40_Base[path][group]))
				return _FAIL;
		}
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			if (tx_idx + 1 > hal_data->max_tx_cnt)
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
#if CONFIG_IEEE80211_BAND_5GHZ
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 path, group, tx_idx;

	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_5G))
		return _SUCCESS;

	for (path = 0; path < MAX_RF_PATH; path++) {
		if (!HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path))
			continue;
		for (group = 0; group < MAX_CHNL_GROUP_5G; group++)
			if (IS_PG_TXPWR_BASE_INVALID(hal_spec, pwr_info->IndexBW40_Base[path][group]))
				return _FAIL;
		for (tx_idx = 0; tx_idx < MAX_TX_COUNT; tx_idx++) {
			if (tx_idx + 1 > hal_data->max_tx_cnt)
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
#if CONFIG_IEEE80211_BAND_5GHZ
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
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
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
			if (!IS_PG_TXPWR_BASE_INVALID(hal_spec, tmp_base)
				&& IS_PG_TXPWR_BASE_INVALID(hal_spec, pwr_info->IndexCCK_Base[path][group])
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
			if (!IS_PG_TXPWR_BASE_INVALID(hal_spec, tmp_base)
				&& IS_PG_TXPWR_BASE_INVALID(hal_spec, pwr_info->IndexBW40_Base[path][group])
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
			if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path)) {
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
			if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && tx_idx + 1 <= hal_data->max_tx_cnt) {
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

			if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && tx_idx + 1 <= hal_data->max_tx_cnt) {
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
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u16 offset = pg_offset;
	u8 group, tx_idx;
	u8 val;
	u8 tmp_base;
	s8 tmp_diff;

#if CONFIG_IEEE80211_BAND_5GHZ
	if (pwr_info == NULL || !hal_chk_band_cap(adapter, BAND_CAP_5G))
#endif
	{
		offset += PG_TXPWR_1PATH_BYTE_NUM_5G;
		goto exit;
	}

#if CONFIG_IEEE80211_BAND_5GHZ
	if (DBG_PG_TXPWR_READ)
		RTW_INFO("%s[%c] eaddr:0x%03x\n", __func__, rf_path_char(path), offset);

	for (group = 0; group < MAX_CHNL_GROUP_5G; group++) {
		if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path)) {
			tmp_base = map_read8(txpwr_map, offset);
			if (!IS_PG_TXPWR_BASE_INVALID(hal_spec, tmp_base)
				&& IS_PG_TXPWR_BASE_INVALID(hal_spec, pwr_info->IndexBW40_Base[path][group])
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
			if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path)) {
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
			if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && tx_idx + 1 <= hal_data->max_tx_cnt) {
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
	if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && hal_data->max_tx_cnt > 1) {
		val = map_read8(txpwr_map, offset);
		tmp_diff = PG_TXPWR_MSB_DIFF_TO_S8BIT(val);
		if (!IS_PG_TXPWR_DIFF_INVALID(tmp_diff)
			&& IS_PG_TXPWR_DIFF_INVALID(pwr_info->OFDM_Diff[path][1])
		) {
			pwr_info->OFDM_Diff[path][1] = tmp_diff;
			if (LOAD_PG_TXPWR_WARN_COND(txpwr_src))
				RTW_INFO("[%c] 5G OFDM-%dT diff:%d from %s\n", rf_path_char(path), 2, tmp_diff, pg_txpwr_src_str(txpwr_src));
		}
		if (hal_data->max_tx_cnt > 2) {
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
	if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && hal_data->max_tx_cnt > 3) {
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
		if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path) && tx_idx + 1 <= hal_data->max_tx_cnt) {
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

#endif /* CONFIG_IEEE80211_BAND_5GHZ */

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
	pg_offset = hal_spec->pg_txpwr_saddr;

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
	#if DBG_PG_TXPWR_READ
	if (pwr_info_2g)
		dump_pg_txpwr_info_2g(RTW_DBGDUMP, pwr_info_2g, 4, 4);
	if (pwr_info_5g)
		dump_pg_txpwr_info_5g(RTW_DBGDUMP, pwr_info_5g, 4, 4);
	#endif

	return;
}
#endif /* CONFIG_TXPWR_PG_WITH_PWR_IDX */

#ifdef CONFIG_EFUSE_CONFIG_FILE

#define EFUSE_POWER_INDEX_INVALID 0xFF

static u8 _check_phy_efuse_tx_power_info_valid(u8 *pg_data, int chk_len, u16 pg_offset)
{
	int ff_cnt = 0;
	int i;

	for (i = 0; i < chk_len; i++) {
		if (*(pg_data + pg_offset + i) == 0xFF)
			ff_cnt++;
	}

	if (ff_cnt == 0)
		return _TRUE;
	else if (ff_cnt == chk_len)
		return _FALSE;
	else
		return EFUSE_POWER_INDEX_INVALID;
}

int check_phy_efuse_tx_power_info_valid(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 *pg_data = hal_data->efuse_eeprom_data;
	u16 pg_offset = hal_spec->pg_txpwr_saddr;
	u8 path;
	u8 valid_2g_path_bmp = 0;
#if CONFIG_IEEE80211_BAND_5GHZ
	u8 valid_5g_path_bmp = 0;
#endif
#ifdef CONFIG_MP_INCLUDED
	struct mp_priv *pmp_priv = &adapter->mppriv;


	if (pmp_priv->efuse_update_file == _TRUE && (rtw_mp_mode_check(adapter))) {
		RTW_INFO("%s: To use efuse_update_file !!!\n", __func__);
		return _FALSE;
	}
#endif
	/* NOTE: TSSI offset use the same layout as TXPWR base */

	for (path = 0; path < MAX_RF_PATH; path++) {
		u8 ret = _FALSE;

		if (!HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path) && !HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path))
			break;

		if (HAL_SPEC_CHK_RF_PATH_2G(hal_spec, path)) {
			ret = _check_phy_efuse_tx_power_info_valid(pg_data, PG_TXPWR_BASE_BYTE_NUM_2G, pg_offset);
			if (ret == _TRUE)
				valid_2g_path_bmp |= BIT(path);
			else if (ret == EFUSE_POWER_INDEX_INVALID)
				return _FALSE;
		}
		pg_offset += PG_TXPWR_1PATH_BYTE_NUM_2G;

		#if CONFIG_IEEE80211_BAND_5GHZ
		if (HAL_SPEC_CHK_RF_PATH_5G(hal_spec, path)) {
			ret = _check_phy_efuse_tx_power_info_valid(pg_data, PG_TXPWR_BASE_BYTE_NUM_5G, pg_offset);
			if (ret == _TRUE)
				valid_5g_path_bmp |= BIT(path);
			else if (ret == EFUSE_POWER_INDEX_INVALID)
				return _FALSE;
		}
		#endif
		pg_offset += PG_TXPWR_1PATH_BYTE_NUM_5G;
	}

	if ((hal_chk_band_cap(adapter, BAND_CAP_2G) && valid_2g_path_bmp)
		#if CONFIG_IEEE80211_BAND_5GHZ
		|| (hal_chk_band_cap(adapter, BAND_CAP_5G) && valid_5g_path_bmp)
		#endif
	)
		return _TRUE;

	return _FALSE;
}
#endif /* CONFIG_EFUSE_CONFIG_FILE */

#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
void hal_load_txpwr_info(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 max_tx_cnt = hal_data->max_tx_cnt;
	u8 *pg_data = hal_data->efuse_eeprom_data;
	TxPowerInfo24G *pwr_info_2g = NULL;
	TxPowerInfo5G *pwr_info_5g = NULL;
	u8 rfpath, ch_idx, group, tx_idx;

	if (hal_chk_band_cap(adapter, BAND_CAP_2G))
		pwr_info_2g = rtw_vmalloc(sizeof(TxPowerInfo24G));
#if CONFIG_IEEE80211_BAND_5GHZ
	if (hal_chk_band_cap(adapter, BAND_CAP_5G))
		pwr_info_5g = rtw_vmalloc(sizeof(TxPowerInfo5G));
#endif

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
			if (tx_idx + 1 > max_tx_cnt)
				break;

			hal_data->CCK_24G_Diff[rfpath][tx_idx] = pwr_info_2g->CCK_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
			hal_data->OFDM_24G_Diff[rfpath][tx_idx] = pwr_info_2g->OFDM_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
			hal_data->BW20_24G_Diff[rfpath][tx_idx] = pwr_info_2g->BW20_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
			hal_data->BW40_24G_Diff[rfpath][tx_idx] = pwr_info_2g->BW40_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
		}
bypass_2g:
		;

#if CONFIG_IEEE80211_BAND_5GHZ
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
			if (tx_idx + 1 > max_tx_cnt)
				break;

			hal_data->OFDM_5G_Diff[rfpath][tx_idx] = pwr_info_5g->OFDM_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
			hal_data->BW20_5G_Diff[rfpath][tx_idx] = pwr_info_5g->BW20_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
			hal_data->BW40_5G_Diff[rfpath][tx_idx] = pwr_info_5g->BW40_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
			hal_data->BW80_5G_Diff[rfpath][tx_idx] = pwr_info_5g->BW80_Diff[rfpath][tx_idx] * hal_spec->pg_txgi_diff_factor;
		}
bypass_5g:
		;
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
	}

	if (pwr_info_2g)
		rtw_vmfree(pwr_info_2g, sizeof(TxPowerInfo24G));
	if (pwr_info_5g)
		rtw_vmfree(pwr_info_5g, sizeof(TxPowerInfo5G));
}

void dump_hal_txpwr_info_2g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int path, ch_idx, tx_idx;

	RTW_PRINT_SEL(sel, "2.4G\n");
	RTW_PRINT_SEL(sel, "CCK-1T base:\n");
	RTW_PRINT_SEL(sel, "%4s ", "");
	for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
		_RTW_PRINT_SEL(sel, "%3d ", center_ch_2g[ch_idx]);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
			_RTW_PRINT_SEL(sel, "%3u ", hal_data->Index24G_CCK_Base[path][ch_idx]);
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
		_RTW_PRINT_SEL(sel, "%3d ", center_ch_2g[ch_idx]);
	_RTW_PRINT_SEL(sel, "\n");
	for (path = 0; path < MAX_RF_PATH && path < rfpath_num; path++) {
		RTW_PRINT_SEL(sel, "[%c]: ", rf_path_char(path));
		for (ch_idx = 0; ch_idx < CENTER_CH_2G_NUM; ch_idx++)
			_RTW_PRINT_SEL(sel, "%3u ", hal_data->Index24G_BW40_Base[path][ch_idx]);
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
#if CONFIG_IEEE80211_BAND_5GHZ
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
#endif /* CONFIG_TXPWR_PG_WITH_PWR_IDX */

/*
* rtw_regsty_get_target_tx_power -
*
* Return dBm or -1 for undefined
*/
s8 rtw_regsty_get_target_tx_power(
		PADAPTER		Adapter,
		u8				Band,
		u8				RfPath,
		RATE_SECTION	RateSection
)
{
	struct registry_priv *regsty = adapter_to_regsty(Adapter);
	s8 value = 0;

	if (RfPath > RF_PATH_D) {
		RTW_PRINT("%s invalid RfPath:%d\n", __func__, RfPath);
		return -1;
	}

	if (Band != BAND_ON_2_4G
		#if CONFIG_IEEE80211_BAND_5GHZ
		&& Band != BAND_ON_5G
		#endif
	) {
		RTW_PRINT("%s invalid Band:%d\n", __func__, Band);
		return -1;
	}

	if (RateSection >= RATE_SECTION_NUM
		#if CONFIG_IEEE80211_BAND_5GHZ
		|| (Band == BAND_ON_5G && RateSection == CCK)
		#endif
	) {
		RTW_PRINT("%s invalid RateSection:%d in Band:%d, RfPath:%d\n", __func__
			, RateSection, Band, RfPath);
		return -1;
	}

	if (Band == BAND_ON_2_4G)
		value = regsty->target_tx_pwr_2g[RfPath][RateSection];
#if CONFIG_IEEE80211_BAND_5GHZ
	else /* BAND_ON_5G */
		value = regsty->target_tx_pwr_5g[RfPath][RateSection - 1];
#endif

	return value;
}

bool rtw_regsty_chk_target_tx_power_valid(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
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
				if (tx_num + 1 > GET_HAL_TX_NSS(adapter))
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
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
* phy_get_target_txpwr -
*
* Return value in unit of TX Gain Index
*/
u8 phy_get_target_txpwr(
		PADAPTER		Adapter,
		u8				Band,
		u8				RfPath,
		RATE_SECTION	RateSection
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
		RTW_PRINT("%s invalid RateSection:%d in Band:%d, RfPath:%d\n", __func__
			, RateSection, Band, RfPath);
		return 0;
	}

	if (Band == BAND_ON_2_4G)
		value = pHalData->target_txpwr_2g[RfPath][RateSection];
	else if (Band == BAND_ON_5G)
		value = pHalData->target_txpwr_5g[RfPath][RateSection - 1];

	return value;
}

static void phy_set_target_txpwr(
		PADAPTER		Adapter,
		u8				Band,
		u8				RfPath,
		RATE_SECTION	RateSection,
		u8				Value
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
		RTW_PRINT("%s invalid RateSection:%d in %sG, RfPath:%d\n", __func__
			, RateSection, (Band == BAND_ON_2_4G) ? "2.4" : "5", RfPath);
		return;
	}

	if (Band == BAND_ON_2_4G)
		pHalData->target_txpwr_2g[RfPath][RateSection] = Value;
	else /* BAND_ON_5G */
		pHalData->target_txpwr_5g[RfPath][RateSection - 1] = Value;
}

static inline BOOLEAN phy_is_txpwr_by_rate_undefined_of_band_path(_adapter *adapter, u8 band, u8 path)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 rate_idx = 0;

	for (rate_idx = 0; rate_idx < TX_PWR_BY_RATE_NUM_RATE; rate_idx++) {
		if (hal_data->TxPwrByRate[band][path][rate_idx] != hal_spec->txgi_max)
			goto exit;
	}

exit:
	return rate_idx >= TX_PWR_BY_RATE_NUM_RATE ? _TRUE : _FALSE;
}

static inline void phy_txpwr_by_rate_duplicate_band_path(_adapter *adapter, u8 band, u8 s_path, u8 t_path)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 rate_idx = 0;

	for (rate_idx = 0; rate_idx < TX_PWR_BY_RATE_NUM_RATE; rate_idx++)
		hal_data->TxPwrByRate[band][t_path][rate_idx] = hal_data->TxPwrByRate[band][s_path][rate_idx];
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

static s8 _phy_get_txpwr_by_rate(_adapter *adapter
	, BAND_TYPE band, enum rf_path rfpath, enum MGN_RATE rate);

void phy_store_target_tx_power(PADAPTER	pAdapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(pAdapter);
	struct registry_priv *regsty = adapter_to_regsty(pAdapter);

	u8 band, path, rs, tx_num, base;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(pAdapter, band))
			continue;

		for (path = RF_PATH_A; path < RF_PATH_MAX; path++) {
			if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, path))
				break;

			for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
				tx_num = rate_section_to_tx_num(rs);
				if (tx_num + 1 > GET_HAL_TX_NSS(pAdapter))
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(pAdapter))
					continue;

				if (regsty->target_tx_pwr_valid == _TRUE)
					base = hal_spec->txgi_pdbm * rtw_regsty_get_target_tx_power(pAdapter, band, path, rs);
				else
					base = _phy_get_txpwr_by_rate(pAdapter, band, path, rate_sec_base[rs]);
				phy_set_target_txpwr(pAdapter, band, path, rs, base);
			}
		}
	}
}

static u8 get_val_from_dhex(u32 dhex, u8 i)
{
	return (((dhex >> (i * 8 + 4)) & 0xF)) * 10 + ((dhex >> (i * 8)) & 0xF);
}

static u8 get_val_from_hex(u32 hex, u8 i)
{
	return (hex >> (i * 8)) & 0xFF;
}

void
PHY_GetRateValuesOfTxPowerByRate(
		PADAPTER pAdapter,
		u32 RegAddr,
		u32 BitMask,
		u32 Value,
		u8 *Rate,
		s8 *PwrByRateVal,
		u8 *RateNum
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;
	u8 i = 0;
	u8 (*get_val)(u32, u8);

	if (pDM_Odm->phy_reg_pg_version == 1)
		get_val = get_val_from_dhex;
	else
		get_val = get_val_from_hex;

	switch (RegAddr) {
	case rTxAGC_A_Rate18_06:
	case rTxAGC_B_Rate18_06:
		Rate[0] = MGN_6M;
		Rate[1] = MGN_9M;
		Rate[2] = MGN_12M;
		Rate[3] = MGN_18M;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case rTxAGC_A_Rate54_24:
	case rTxAGC_B_Rate54_24:
		Rate[0] = MGN_24M;
		Rate[1] = MGN_36M;
		Rate[2] = MGN_48M;
		Rate[3] = MGN_54M;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case rTxAGC_A_CCK1_Mcs32:
		Rate[0] = MGN_1M;
		PwrByRateVal[0] = (s8)get_val(Value, 1);
		*RateNum = 1;
		break;

	case rTxAGC_B_CCK11_A_CCK2_11:
		if (BitMask == 0xffffff00) {
			Rate[0] = MGN_2M;
			Rate[1] = MGN_5_5M;
			Rate[2] = MGN_11M;
			for (i = 1; i < 4; ++i)
				PwrByRateVal[i - 1] = (s8)get_val(Value, i);
			*RateNum = 3;
		} else if (BitMask == 0x000000ff) {
			Rate[0] = MGN_11M;
			PwrByRateVal[0] = (s8)get_val(Value, 0);
			*RateNum = 1;
		}
		break;

	case rTxAGC_A_Mcs03_Mcs00:
	case rTxAGC_B_Mcs03_Mcs00:
		Rate[0] = MGN_MCS0;
		Rate[1] = MGN_MCS1;
		Rate[2] = MGN_MCS2;
		Rate[3] = MGN_MCS3;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs07_Mcs04:
	case rTxAGC_B_Mcs07_Mcs04:
		Rate[0] = MGN_MCS4;
		Rate[1] = MGN_MCS5;
		Rate[2] = MGN_MCS6;
		Rate[3] = MGN_MCS7;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs11_Mcs08:
	case rTxAGC_B_Mcs11_Mcs08:
		Rate[0] = MGN_MCS8;
		Rate[1] = MGN_MCS9;
		Rate[2] = MGN_MCS10;
		Rate[3] = MGN_MCS11;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case rTxAGC_A_Mcs15_Mcs12:
	case rTxAGC_B_Mcs15_Mcs12:
		Rate[0] = MGN_MCS12;
		Rate[1] = MGN_MCS13;
		Rate[2] = MGN_MCS14;
		Rate[3] = MGN_MCS15;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case rTxAGC_B_CCK1_55_Mcs32:
		Rate[0] = MGN_1M;
		Rate[1] = MGN_2M;
		Rate[2] = MGN_5_5M;
		for (i = 1; i < 4; ++i)
			PwrByRateVal[i - 1] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case 0x3a24: /* HT MCS24-27 */
		Rate[0] = MGN_MCS24;
		Rate[1] = MGN_MCS25;
		Rate[2] = MGN_MCS26;
		Rate[3] = MGN_MCS27;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case 0x3a28: /* HT MCS28-31 */
		Rate[0] = MGN_MCS28;
		Rate[1] = MGN_MCS29;
		Rate[2] = MGN_MCS30;
		Rate[3] = MGN_MCS31;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
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
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case 0xCE8:
	case 0xEE8:
	case 0x18E8:
	case 0x1aE8:
	case 0x3a48:
		Rate[0] = MGN_VHT3SS_MCS8;
		Rate[1] = MGN_VHT3SS_MCS9;
		Rate[2] = MGN_VHT4SS_MCS0;
		Rate[3] = MGN_VHT4SS_MCS1;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case 0x3a4c:
		Rate[0] = MGN_VHT4SS_MCS2;
		Rate[1] = MGN_VHT4SS_MCS3;
		Rate[2] = MGN_VHT4SS_MCS4;
		Rate[3] = MGN_VHT4SS_MCS5;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	case 0x3a50:
		Rate[0] = MGN_VHT4SS_MCS6;
		Rate[1] = MGN_VHT4SS_MCS7;
		Rate[2] = MGN_VHT4SS_MCS8;
		Rate[3] = MGN_VHT4SS_MCS9;
		for (i = 0; i < 4; ++i)
			PwrByRateVal[i] = (s8)get_val(Value, i);
		*RateNum = 4;
		break;

	default:
		RTW_PRINT("Invalid RegAddr 0x%x in %s()\n", RegAddr, __func__);
		break;
	};
}

void
PHY_StoreTxPowerByRateNew(
		PADAPTER	pAdapter,
		u32			Band,
		u32			RfPath,
		u32			RegAddr,
		u32			BitMask,
		u32			Data
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

	if (RfPath > RF_PATH_D) {
		RTW_PRINT("Invalid RfPath %d\n", RfPath);
		return;
	}

	for (i = 0; i < rateNum; ++i) {
		u8 rate_idx = phy_get_rate_idx_of_txpwr_by_rate(rates[i]);

		pHalData->TxPwrByRate[Band][RfPath][rate_idx] = PwrByRateVal[i];
	}
}

void
PHY_InitTxPowerByRate(
		PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(pAdapter);
	u8	band = 0, rfPath = 0, rate = 0;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; ++band)
		for (rfPath = 0; rfPath < TX_PWR_BY_RATE_NUM_RF; ++rfPath)
				for (rate = 0; rate < TX_PWR_BY_RATE_NUM_RATE; ++rate)
					pHalData->TxPwrByRate[band][rfPath][rate] = hal_spec->txgi_max;
}

void
phy_store_tx_power_by_rate(
		PADAPTER	pAdapter,
		u32			Band,
		u32			RfPath,
		u32			TxNum,
		u32			RegAddr,
		u32			BitMask,
		u32			Data
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;

	if (pDM_Odm->phy_reg_pg_version > 0)
		PHY_StoreTxPowerByRateNew(pAdapter, Band, RfPath, RegAddr, BitMask, Data);
	else
		RTW_INFO("Invalid PHY_REG_PG.txt version %d\n",  pDM_Odm->phy_reg_pg_version);

}

/*
  * This function must be called if the value in the PHY_REG_PG.txt(or header)
  * is exact dBm values
  */
void
PHY_TxPowerByRateConfiguration(
		PADAPTER			pAdapter
)
{
	phy_txpwr_by_rate_chk_for_path_dup(pAdapter);
	phy_store_target_tx_power(pAdapter);
}

#ifdef CONFIG_FW_OFFLOAD_SET_TXPWR_IDX
extern bool phy_set_txpwr_idx_offload(_adapter *adapter);
#endif

void
phy_set_tx_power_index_by_rate_section(
		PADAPTER		pAdapter,
		enum rf_path		RFPath,
		u8				Channel,
		u8				rs
)
{
	PHAL_DATA_TYPE	hal_data = GET_HAL_DATA(pAdapter);
	u8 band = hal_data->current_band_type;
	u8 bw = hal_data->current_channel_bw;
	u32	powerIndex = 0;
	int	i = 0;

	if (rs >= RATE_SECTION_NUM) {
		RTW_INFO("Invalid RateSection %d in %s", rs, __func__);
		rtw_warn_on(1);
		goto exit;
	}

	if (rs == CCK && bw != BAND_ON_2_4G)
		goto exit;

	for (i = 0; i < rates_by_sections[rs].rate_num; ++i) {
#if DBG_TX_POWER_IDX
		struct txpwr_idx_comp tic;

		powerIndex = rtw_hal_get_tx_power_index(pAdapter, RFPath
			, rs, rates_by_sections[rs].rates[i], bw, band, Channel, 0, &tic);
		dump_tx_power_index_inline(RTW_DBGDUMP, pAdapter, RFPath, bw, Channel
			, rates_by_sections[rs].rates[i], powerIndex, &tic);
#else
		powerIndex = phy_get_tx_power_index_ex(pAdapter, RFPath
			, rs, rates_by_sections[rs].rates[i], bw, band, Channel, 0);
#endif
		PHY_SetTxPowerIndex(pAdapter, powerIndex, RFPath, rates_by_sections[rs].rates[i]);
	}

#ifdef CONFIG_FW_OFFLOAD_SET_TXPWR_IDX
	if (!hal_data->set_entire_txpwr
		&& phy_set_txpwr_idx_offload(pAdapter))
		rtw_hal_set_txpwr_done(pAdapter);
#endif

exit:
	return;
}

bool phy_get_ch_idx(u8 ch, u8 *ch_idx)
{
	u8  i = 0;
	BOOLEAN bIn24G = _TRUE;

	if (ch > 0 && ch <= 14) {
		bIn24G = _TRUE;
		*ch_idx = ch - 1;
	} else {
		bIn24G = _FALSE;

		for (i = 0; i < CENTER_CH_5G_ALL_NUM; ++i) {
			if (center_ch_5g_all[i] == ch) {
				*ch_idx = i;
				break;
			}
		}
	}

	return bIn24G;
}

bool phy_chk_ch_setting_consistency(_adapter *adapter, u8 ch)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 ch_idx = 0xFF;
	u8 ret = _FAIL;

	phy_get_ch_idx(ch, &ch_idx);
	if (ch_idx == 0xFF) {
		rtw_warn_on(1);
		goto exit;
	}

	if (ch != hal_data->current_channel) {
		rtw_warn_on(1);
		goto exit;
	}

	if (ch <= 14) {
		if (hal_data->current_band_type != BAND_ON_2_4G) {
			rtw_warn_on(1);
			goto exit;
		}
		if (hal_data->current_channel_bw > CHANNEL_WIDTH_40) {
			rtw_warn_on(1);
			goto exit;
		}
	}
	if (ch > 14) {
		if (hal_data->current_band_type != BAND_ON_5G) {
			rtw_warn_on(1);
			goto exit;
		}
		if (hal_data->current_channel_bw > CHANNEL_WIDTH_160) {
			rtw_warn_on(1);
			goto exit;
		}
	}

	ret = _SUCCESS;

exit:
	if (ret != _SUCCESS)
		RTW_WARN("ch:%u, hal band:%u, ch:%u, bw:%u\n", ch
			, hal_data->current_band_type, hal_data->current_channel, hal_data->current_channel_bw);

	return ret;
}

#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
u8 phy_get_pg_txpwr_idx(_adapter *pAdapter
	, enum rf_path RFPath, RATE_SECTION rs, u8 ntx_idx
	, enum channel_width BandWidth, u8 band, u8 Channel)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	u8					i;
	u8					txPower = 0;
	u8					chnlIdx = (Channel - 1);

	if (HAL_IsLegalChannel(pAdapter, Channel) == _FALSE) {
		chnlIdx = 0;
		RTW_INFO("Illegal channel!!\n");
	}

	phy_get_ch_idx(Channel, &chnlIdx);

	if (0)
		RTW_INFO("[%s] Channel Index: %d\n", band_str(band), chnlIdx);

	if (band == BAND_ON_2_4G) {
		if (IS_CCK_RATE_SECTION(rs)) {
			/* CCK-nTX */
			txPower = pHalData->Index24G_CCK_Base[RFPath][chnlIdx];
			txPower += pHalData->CCK_24G_Diff[RFPath][RF_1TX];
			if (ntx_idx >= RF_2TX)
				txPower += pHalData->CCK_24G_Diff[RFPath][RF_2TX];
			if (ntx_idx >= RF_3TX)
				txPower += pHalData->CCK_24G_Diff[RFPath][RF_3TX];
			if (ntx_idx >= RF_4TX)
				txPower += pHalData->CCK_24G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		txPower = pHalData->Index24G_BW40_Base[RFPath][chnlIdx];

		/* OFDM-nTX */
		if (IS_OFDM_RATE_SECTION(rs)) {
			txPower += pHalData->OFDM_24G_Diff[RFPath][RF_1TX];
			if (ntx_idx >= RF_2TX)
				txPower += pHalData->OFDM_24G_Diff[RFPath][RF_2TX];
			if (ntx_idx >= RF_3TX)
				txPower += pHalData->OFDM_24G_Diff[RFPath][RF_3TX];
			if (ntx_idx >= RF_4TX)
				txPower += pHalData->OFDM_24G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		/* BW20-nS */
		if (BandWidth == CHANNEL_WIDTH_20) {
			txPower += pHalData->BW20_24G_Diff[RFPath][RF_1TX];
			if (rate_section_to_tx_num(rs) >= RF_2TX)
				txPower += pHalData->BW20_24G_Diff[RFPath][RF_2TX];
			if (rate_section_to_tx_num(rs) >= RF_3TX)
				txPower += pHalData->BW20_24G_Diff[RFPath][RF_3TX];
			if (rate_section_to_tx_num(rs) >= RF_4TX)
				txPower += pHalData->BW20_24G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		/* BW40-nS */
		if (BandWidth == CHANNEL_WIDTH_40
			/* Willis suggest adopt BW 40M power index while in BW 80 mode */
			|| BandWidth == CHANNEL_WIDTH_80
		) {
			txPower += pHalData->BW40_24G_Diff[RFPath][RF_1TX];
			if (rate_section_to_tx_num(rs) >= RF_2TX)
				txPower += pHalData->BW40_24G_Diff[RFPath][RF_2TX];
			if (rate_section_to_tx_num(rs) >= RF_3TX)
				txPower += pHalData->BW40_24G_Diff[RFPath][RF_3TX];
			if (rate_section_to_tx_num(rs) >= RF_4TX)
				txPower += pHalData->BW40_24G_Diff[RFPath][RF_4TX];
			goto exit;
		}
	}
#if CONFIG_IEEE80211_BAND_5GHZ
	else if (band == BAND_ON_5G) {
		if (IS_CCK_RATE_SECTION(rs)) {
			RTW_WARN("===>%s: INVALID, CCK on 5G\n", __func__);
			goto exit;
		}

		txPower = pHalData->Index5G_BW40_Base[RFPath][chnlIdx];

		/* OFDM-nTX */
		if (IS_OFDM_RATE_SECTION(rs)) {
			txPower += pHalData->OFDM_5G_Diff[RFPath][RF_1TX];
			if (ntx_idx >= RF_2TX)
				txPower += pHalData->OFDM_5G_Diff[RFPath][RF_2TX];
			if (ntx_idx >= RF_3TX)
				txPower += pHalData->OFDM_5G_Diff[RFPath][RF_3TX];
			if (ntx_idx >= RF_4TX)
				txPower += pHalData->OFDM_5G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		/* BW20-nS */
		if (BandWidth == CHANNEL_WIDTH_20) {
			txPower += pHalData->BW20_5G_Diff[RFPath][RF_1TX];
			if (rate_section_to_tx_num(rs) >= RF_2TX)
				txPower += pHalData->BW20_5G_Diff[RFPath][RF_2TX];
			if (rate_section_to_tx_num(rs) >= RF_3TX)
				txPower += pHalData->BW20_5G_Diff[RFPath][RF_3TX];
			if (rate_section_to_tx_num(rs) >= RF_4TX)
				txPower += pHalData->BW20_5G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		/* BW40-nS */
		if (BandWidth == CHANNEL_WIDTH_40) {
			txPower += pHalData->BW40_5G_Diff[RFPath][RF_1TX];
			if (rate_section_to_tx_num(rs) >= RF_2TX)
				txPower += pHalData->BW40_5G_Diff[RFPath][RF_2TX];
			if (rate_section_to_tx_num(rs) >= RF_3TX)
				txPower += pHalData->BW40_5G_Diff[RFPath][RF_3TX];
			if (rate_section_to_tx_num(rs) >= RF_4TX)
				txPower += pHalData->BW40_5G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		/* BW80-nS */
		if (BandWidth == CHANNEL_WIDTH_80) {
			/* get 80MHz cch index */
			for (i = 0; i < CENTER_CH_5G_80M_NUM; ++i) {
				if (center_ch_5g_80m[i] == Channel) {
					chnlIdx = i;
					break;
				}
			}
			if (i >= CENTER_CH_5G_80M_NUM) {
			#ifdef CONFIG_MP_INCLUDED
				if (rtw_mp_mode_check(pAdapter) == _FALSE)
			#endif
					rtw_warn_on(1);
				txPower = 0;
				goto exit;
			}

			txPower = pHalData->Index5G_BW80_Base[RFPath][chnlIdx];

			txPower += + pHalData->BW80_5G_Diff[RFPath][RF_1TX];
			if (rate_section_to_tx_num(rs) >= RF_2TX)
				txPower += pHalData->BW80_5G_Diff[RFPath][RF_2TX];
			if (rate_section_to_tx_num(rs) >= RF_3TX)
				txPower += pHalData->BW80_5G_Diff[RFPath][RF_3TX];
			if (rate_section_to_tx_num(rs) >= RF_4TX)
				txPower += pHalData->BW80_5G_Diff[RFPath][RF_4TX];
			goto exit;
		}

		/* TODO: BW160-nS */
		rtw_warn_on(1);
	}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

exit:
	return txPower;
}
#endif /* CONFIG_TXPWR_PG_WITH_PWR_IDX */

s8
PHY_GetTxPowerTrackingOffset(
	PADAPTER	pAdapter,
	enum rf_path	RFPath,
	u8			Rate
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	struct dm_struct			*pDM_Odm = &pHalData->odmpriv;
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

static const u8 _phy_get_rate_idx_of_txpwr_by_rate[MGN_UNKNOWN] = {
	[MGN_1M] = 0,
	[MGN_2M] = 1,
	[MGN_5_5M] = 2,
	[MGN_11M] = 3,
	[MGN_6M] = 4,
	[MGN_9M] = 5,
	[MGN_12M] = 6,
	[MGN_18M] = 7,
	[MGN_24M] = 8,
	[MGN_36M] = 9,
	[MGN_48M] = 10,
	[MGN_54M] = 11,
	[MGN_MCS0] = 12,
	[MGN_MCS1] = 13,
	[MGN_MCS2] = 14,
	[MGN_MCS3] = 15,
	[MGN_MCS4] = 16,
	[MGN_MCS5] = 17,
	[MGN_MCS6] = 18,
	[MGN_MCS7] = 19,
	[MGN_MCS8] = 20,
	[MGN_MCS9] = 21,
	[MGN_MCS10] = 22,
	[MGN_MCS11] = 23,
	[MGN_MCS12] = 24,
	[MGN_MCS13] = 25,
	[MGN_MCS14] = 26,
	[MGN_MCS15] = 27,
	[MGN_MCS16] = 28,
	[MGN_MCS17] = 29,
	[MGN_MCS18] = 30,
	[MGN_MCS19] = 31,
	[MGN_MCS20] = 32,
	[MGN_MCS21] = 33,
	[MGN_MCS22] = 34,
	[MGN_MCS23] = 35,
	[MGN_MCS24] = 36,
	[MGN_MCS25] = 37,
	[MGN_MCS26] = 38,
	[MGN_MCS27] = 39,
	[MGN_MCS28] = 40,
	[MGN_MCS29] = 41,
	[MGN_MCS30] = 42,
	[MGN_MCS31] = 43,
	[MGN_VHT1SS_MCS0] = 44,
	[MGN_VHT1SS_MCS1] = 45,
	[MGN_VHT1SS_MCS2] = 46,
	[MGN_VHT1SS_MCS3] = 47,
	[MGN_VHT1SS_MCS4] = 48,
	[MGN_VHT1SS_MCS5] = 49,
	[MGN_VHT1SS_MCS6] = 50,
	[MGN_VHT1SS_MCS7] = 51,
	[MGN_VHT1SS_MCS8] = 52,
	[MGN_VHT1SS_MCS9] = 53,
	[MGN_VHT2SS_MCS0] = 54,
	[MGN_VHT2SS_MCS1] = 55,
	[MGN_VHT2SS_MCS2] = 56,
	[MGN_VHT2SS_MCS3] = 57,
	[MGN_VHT2SS_MCS4] = 58,
	[MGN_VHT2SS_MCS5] = 59,
	[MGN_VHT2SS_MCS6] = 60,
	[MGN_VHT2SS_MCS7] = 61,
	[MGN_VHT2SS_MCS8] = 62,
	[MGN_VHT2SS_MCS9] = 63,
	[MGN_VHT3SS_MCS0] = 64,
	[MGN_VHT3SS_MCS1] = 65,
	[MGN_VHT3SS_MCS2] = 66,
	[MGN_VHT3SS_MCS3] = 67,
	[MGN_VHT3SS_MCS4] = 68,
	[MGN_VHT3SS_MCS5] = 69,
	[MGN_VHT3SS_MCS6] = 70,
	[MGN_VHT3SS_MCS7] = 71,
	[MGN_VHT3SS_MCS8] = 72,
	[MGN_VHT3SS_MCS9] = 73,
	[MGN_VHT4SS_MCS0] = 74,
	[MGN_VHT4SS_MCS1] = 75,
	[MGN_VHT4SS_MCS2] = 76,
	[MGN_VHT4SS_MCS3] = 77,
	[MGN_VHT4SS_MCS4] = 78,
	[MGN_VHT4SS_MCS5] = 79,
	[MGN_VHT4SS_MCS6] = 80,
	[MGN_VHT4SS_MCS7] = 81,
	[MGN_VHT4SS_MCS8] = 82,
	[MGN_VHT4SS_MCS9] = 83,
};

/*The same as MRateToHwRate in hal_com.c*/
u8 phy_get_rate_idx_of_txpwr_by_rate(enum MGN_RATE rate)
{
	u8 index = 0;

	if (rate < MGN_UNKNOWN)
		index = _phy_get_rate_idx_of_txpwr_by_rate[rate];

	if (rate != MGN_1M && index == 0)
		RTW_WARN("Invalid rate 0x%x in %s\n", rate, __FUNCTION__);

	return index;
}

static s8 _phy_get_txpwr_by_rate(_adapter *adapter
	, BAND_TYPE band, enum rf_path rfpath, enum MGN_RATE rate)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	s8 value = 0;
	u8 rate_idx = phy_get_rate_idx_of_txpwr_by_rate(rate);

	if (band != BAND_ON_2_4G && band != BAND_ON_5G) {
		RTW_INFO("Invalid band %d in %s\n", band, __func__);
		goto exit;
	}
	if (rfpath > RF_PATH_D) {
		RTW_INFO("Invalid RfPath %d in %s\n", rfpath, __func__);
		goto exit;
	}
	if (rate_idx >= TX_PWR_BY_RATE_NUM_RATE) {
		RTW_INFO("Invalid RateIndex %d in %s\n", rate_idx, __func__);
		goto exit;
	}

	value = pHalData->TxPwrByRate[band][rfpath][rate_idx];

exit:
	return value;
}

/*
* Return value in unit of TX Gain Index
*/
s8 phy_get_txpwr_by_rate(_adapter *adapter
	, BAND_TYPE band, enum rf_path rfpath, RATE_SECTION rs, enum MGN_RATE rate)
{
	if (phy_is_tx_power_by_rate_needed(adapter))
		return _phy_get_txpwr_by_rate(adapter, band, rfpath, rate);
	return phy_get_target_txpwr(adapter, band, rfpath, rs);
}

/* get txpowr in mBm for single path */
s16 phy_get_txpwr_by_rate_single_mbm(_adapter *adapter
	, BAND_TYPE band, enum rf_path rfpath, RATE_SECTION rs, enum MGN_RATE rate, bool eirp)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	s16 val;

	val = phy_get_txpwr_by_rate(adapter, band, rfpath, rs, rate);
	if (val == hal_spec->txgi_max)
		val = UNSPECIFIED_MBM;
	else {
		val = (val * MBM_PDBM) / hal_spec->txgi_pdbm;
		if (eirp)
			val += rfctl->antenna_gain;
	}

	return val;
}

/* get txpowr in mBm with effect of N-TX */
s16 phy_get_txpwr_by_rate_total_mbm(_adapter *adapter
	, BAND_TYPE band, RATE_SECTION rs, enum MGN_RATE rate, bool cap, bool eirp)
{
	s16 val;
	u8 tx_num;

	if (cap)
		tx_num = phy_get_capable_tx_num(adapter, rate) + 1;
	else
		tx_num = phy_get_current_tx_num(adapter, rate) + 1;

	/* assume all path have same txpower target */
	val = phy_get_txpwr_by_rate_single_mbm(adapter, band, RF_PATH_A, rs, rate, eirp);
	if (val != UNSPECIFIED_MBM)
		val += mb_of_ntx(tx_num);

	return val;
}

static s16 _phy_get_txpwr_by_rate_max_mbm(_adapter *adapter, BAND_TYPE band, s8 rfpath, bool cap, bool eirp)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 tx_num;
	RATE_SECTION rs;
	int i;
	s16 max = UNSPECIFIED_MBM, mbm;

	for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
		tx_num = rate_section_to_tx_num(rs);
		if (tx_num + 1 > hal_data->tx_nss)
			continue;

		if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
			continue;

		if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
			continue;

		for (i = 0; i < rates_by_sections[rs].rate_num; i++) {
			if (rfpath < 0) /* total */
				mbm = phy_get_txpwr_by_rate_total_mbm(adapter, band, rs, rates_by_sections[rs].rates[i], cap, eirp);
			else
				mbm = phy_get_txpwr_by_rate_single_mbm(adapter, band, rfpath, rs, rates_by_sections[rs].rates[i], eirp);
			if (mbm == UNSPECIFIED_MBM)
				continue;
			if (max == UNSPECIFIED_MBM || mbm > max)
				max = mbm;
		}
	}

	return max;
}

/* get txpowr in mBm for single path */
s16 phy_get_txpwr_by_rate_single_max_mbm(_adapter *adapter, BAND_TYPE band, enum rf_path rfpath, bool eirp)
{
	return _phy_get_txpwr_by_rate_max_mbm(adapter, band, rfpath, 0 /* single don't care */, eirp);
}

/* get txpowr in mBm with effect of N-TX */
s16 phy_get_txpwr_by_rate_total_max_mbm(_adapter *adapter, BAND_TYPE band, bool cap, bool eirp)
{
	return _phy_get_txpwr_by_rate_max_mbm(adapter, band, -1, cap, eirp);
}

u8 phy_check_under_survey_ch(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	u8 ret = _FALSE;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		mlmeext = &iface->mlmeextpriv;

		/* check scan state */
		if (mlmeext_scan_state(mlmeext) != SCAN_DISABLE
			&& mlmeext_scan_state(mlmeext) != SCAN_COMPLETE
				&& mlmeext_scan_state(mlmeext) != SCAN_BACKING_OP) {
			ret = _TRUE;
		} else if (mlmeext_scan_state(mlmeext) == SCAN_BACKING_OP
			&& !mlmeext_chk_scan_backop_flags(mlmeext, SS_BACKOP_TX_RESUME)) {
			ret = _TRUE;
		}
	}

	return ret;
}

void
phy_set_tx_power_level_by_path(
		PADAPTER	Adapter,
		u8			channel,
		u8			path
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	BOOLEAN bIsIn24G = (pHalData->current_band_type == BAND_ON_2_4G);
	u8 under_survey_ch = phy_check_under_survey_ch(Adapter);


	/* if ( pMgntInfo->RegNByteAccess == 0 ) */
	{
		if (bIsIn24G)
			phy_set_tx_power_index_by_rate_section(Adapter, path, channel, CCK);

		phy_set_tx_power_index_by_rate_section(Adapter, path, channel, OFDM);

		if (!under_survey_ch) {
			phy_set_tx_power_index_by_rate_section(Adapter, path, channel, HT_MCS0_MCS7);

			if (IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_8814A(Adapter))
				phy_set_tx_power_index_by_rate_section(Adapter, path, channel, VHT_1SSMCS0_1SSMCS9);

			if (pHalData->tx_nss >= 2) {
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
}

#if CONFIG_TXPWR_LIMIT
const char *const _txpwr_lmt_rs_str[] = {
	"CCK",
	"OFDM",
	"HT",
	"VHT",
	"UNKNOWN",
};

static s8
phy_GetChannelIndexOfTxPowerLimit(
		u8			Band,
		u8			Channel
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

static s8 phy_txpwr_ww_lmt_value(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	if (hal_spec->txgi_max == 63)
		return -63;
	else if (hal_spec->txgi_max == 127)
		return -128;

	rtw_warn_on(1);
	return -128;
}

/*
* return txpwr limit in unit of TX Gain Index
* hsl_spec->txgi_max is returned when NO limit
*/
s8 phy_get_txpwr_lmt(
		PADAPTER			Adapter,
		const char			*regd_name,
		BAND_TYPE			Band,
		enum channel_width		bw,
	u8 tlrs,
	u8 ntx_idx,
	u8 cch,
	u8 lock
)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(Adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(Adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(Adapter);
	struct txpwr_lmt_ent *ent = NULL;
	_irqL irqL;
	_list *cur, *head;
	s8 ch_idx;
	u8 is_ww_regd = 0;
	s8 ww_lmt_val = phy_txpwr_ww_lmt_value(Adapter);
	s8 lmt = hal_spec->txgi_max;

	if ((Adapter->registrypriv.RegEnableTxPowerLimit == 2 && hal_data->EEPROMRegulatory != 1) ||
		Adapter->registrypriv.RegEnableTxPowerLimit == 0)
		goto exit;

	if (Band != BAND_ON_2_4G && Band != BAND_ON_5G) {
		RTW_ERR("%s invalid band:%u\n", __func__, Band);
		rtw_warn_on(1);
		goto exit;
	}

	if (Band == BAND_ON_5G  && tlrs == TXPWR_LMT_RS_CCK) {
		RTW_ERR("5G has no CCK\n");
		goto exit;
	}

	if (lock)
		_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	if (!regd_name) /* no regd_name specified, use currnet */
		regd_name = rfctl->regd_name;

	if (rfctl->txpwr_regd_num == 0
		|| strcmp(regd_name, regd_str(TXPWR_LMT_NONE)) == 0)
		goto release_lock;

	if (strcmp(regd_name, regd_str(TXPWR_LMT_WW)) == 0)
		is_ww_regd = 1;

	if (!is_ww_regd) {
		ent = _rtw_txpwr_lmt_get_by_name(rfctl, regd_name);
		if (!ent)
			goto release_lock;
	}

	ch_idx = phy_GetChannelIndexOfTxPowerLimit(Band, cch);
	if (ch_idx == -1)
		goto release_lock;

	if (Band == BAND_ON_2_4G) {
		if (!is_ww_regd) {
			lmt = ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx];
			if (lmt != ww_lmt_val)
				goto release_lock;
		}

		/* search for min value for WW regd or WW limit */
		lmt = hal_spec->txgi_max;
		head = &rfctl->txpwr_lmt_list;
		cur = get_next(head);
		while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
			ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
			cur = get_next(cur);
			if (ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx] != ww_lmt_val)
				lmt = rtw_min(lmt, ent->lmt_2g[bw][tlrs][ch_idx][ntx_idx]);
		}
	}
	#if CONFIG_IEEE80211_BAND_5GHZ
	else if (Band == BAND_ON_5G) {
		if (!is_ww_regd) {
			lmt = ent->lmt_5g[bw][tlrs - 1][ch_idx][ntx_idx];
			if (lmt != ww_lmt_val)
				goto release_lock;
		}

		/* search for min value for WW regd or WW limit */
		lmt = hal_spec->txgi_max;
		head = &rfctl->txpwr_lmt_list;
		cur = get_next(head);
		while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
			ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
			cur = get_next(cur);
			if (ent->lmt_5g[bw][tlrs - 1][ch_idx][ntx_idx] != ww_lmt_val)
				lmt = rtw_min(lmt, ent->lmt_5g[bw][tlrs - 1][ch_idx][ntx_idx]);
		}
	}
	#endif

release_lock:
	if (lock)
		_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

exit:
	return lmt;
}

/*
* return txpwr limit diff value to target of its rate section in unit of TX Gain Index
* hal_spec->txgi_max is returned when NO limit
*/
inline s8 phy_get_txpwr_lmt_diff(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 rfpath, u8 rs, u8 tlrs, u8 ntx_idx, u8 cch, u8 lock
)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	s8 lmt = phy_get_txpwr_lmt(adapter, regd_name, band, bw, tlrs, ntx_idx, cch, lock);

	if (lmt != hal_spec->txgi_max) {
		/* return diff value */
		lmt = lmt - phy_get_target_txpwr(adapter, band, rfpath, rs);
	}

	return lmt;
}

/*
* May search for secondary channels for max/min limit
* @opch: used to specify operating channel position to get
* cch of every bandwidths which differ from current hal_data.cch20, 40, 80...
*
* return txpwr limit in unit of TX Gain Index
* hsl_spec->txgi_max is returned when NO limit
*/
s8 phy_get_txpwr_lmt_sub_chs(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 rfpath, u8 rate, u8 ntx_idx, u8 cch, u8 opch, bool reg_max)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	BOOLEAN no_sc = _FALSE;
	u8 cch_20 = hal_data->cch_20, cch_40 = hal_data->cch_40, cch_80 = hal_data->cch_80;
	s8 tlrs = -1;
	s8 lmt = hal_spec->txgi_max;
	u8 tmp_cch = 0;
	u8 tmp_bw;
	u8 bw_bmp = 0;
	s8 final_lmt = reg_max ? 0 : hal_spec->txgi_max;
	u8 final_bw = CHANNEL_WIDTH_MAX, final_cch = cch;
	_irqL irqL;

	if ((adapter->registrypriv.RegEnableTxPowerLimit == 2 && hal_data->EEPROMRegulatory != 1) ||
		adapter->registrypriv.RegEnableTxPowerLimit == 0
	) {
		final_lmt = hal_spec->txgi_max;
		goto exit;
	}

#ifdef CONFIG_MP_INCLUDED
	/* MP mode channel don't use secondary channel */
	if (rtw_mp_mode_check(adapter) == _TRUE)
		no_sc = _TRUE;
#endif
	if (IS_CCK_RATE(rate))
		tlrs = TXPWR_LMT_RS_CCK;
	else if (IS_OFDM_RATE(rate))
		tlrs = TXPWR_LMT_RS_OFDM;
	else if (IS_HT_RATE(rate))
		tlrs = TXPWR_LMT_RS_HT;
	else if (IS_VHT_RATE(rate))
		tlrs = TXPWR_LMT_RS_VHT;
	else {
		RTW_ERR("%s invalid rate 0x%x\n", __func__, rate);
		rtw_warn_on(1);
		goto exit;
	}

	if (no_sc == _TRUE) {
		/* use the input center channel and bandwidth directly */
		tmp_cch = cch;
		bw_bmp = ch_width_to_bw_cap(bw);
	} else {
		/* decide center channel of each bandwidth */
		if (opch != 0) {
			cch_80 = bw == CHANNEL_WIDTH_80 ? cch : 0;
			cch_40 = bw == CHANNEL_WIDTH_40 ? cch : 0;
			cch_20 = bw == CHANNEL_WIDTH_20 ? cch : 0;
			if (cch_80 != 0)
				cch_40 = rtw_get_scch_by_cch_opch(cch_80, CHANNEL_WIDTH_80, opch);
			if (cch_40 != 0)
				cch_20 = rtw_get_scch_by_cch_opch(cch_40, CHANNEL_WIDTH_40, opch);
		}

		/*
		* reg_max:
		* get valid full bandwidth bmp up to @bw
		*
		* !reg_max:
		* find the possible tx bandwidth bmp for this rate
		* if no possible tx bandwidth bmp, select valid bandwidth bmp up to @bw
		*/
		if (tlrs == TXPWR_LMT_RS_CCK || tlrs == TXPWR_LMT_RS_OFDM)
			bw_bmp = BW_CAP_20M; /* CCK, OFDM only BW 20M */
		else if (tlrs == TXPWR_LMT_RS_HT) {
			if (reg_max)
				bw_bmp = ch_width_to_bw_cap(bw > CHANNEL_WIDTH_40 ? CHANNEL_WIDTH_40 + 1 : bw + 1) - 1;
			else {
				bw_bmp = rtw_get_tx_bw_bmp_of_ht_rate(dvobj, rate, bw);
				if (bw_bmp == 0)
					bw_bmp = ch_width_to_bw_cap(bw > CHANNEL_WIDTH_40 ? CHANNEL_WIDTH_40 : bw);
			}
		} else if (tlrs == TXPWR_LMT_RS_VHT) {
			if (reg_max)
				bw_bmp = ch_width_to_bw_cap(bw > CHANNEL_WIDTH_160 ? CHANNEL_WIDTH_160 + 1 : bw + 1) - 1;
			else {
				bw_bmp = rtw_get_tx_bw_bmp_of_vht_rate(dvobj, rate, bw);
				if (bw_bmp == 0)
					bw_bmp = ch_width_to_bw_cap(bw > CHANNEL_WIDTH_160 ? CHANNEL_WIDTH_160 : bw);
			}
		} else
			rtw_warn_on(1);
	}

	if (bw_bmp == 0)
		goto exit;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	/* loop for each possible tx bandwidth to find final limit */
	for (tmp_bw = CHANNEL_WIDTH_20; tmp_bw <= bw; tmp_bw++) {
		if (!(ch_width_to_bw_cap(tmp_bw) & bw_bmp))
			continue;

		if (no_sc == _FALSE) {
			/* get center channel for each bandwidth */
			if (tmp_bw == CHANNEL_WIDTH_20)
				tmp_cch = cch_20;
			else if (tmp_bw == CHANNEL_WIDTH_40)
				tmp_cch = cch_40;
			else if (tmp_bw == CHANNEL_WIDTH_80)
				tmp_cch = cch_80;
			else {
				tmp_cch = 0;
				rtw_warn_on(1);
			}
		}

		lmt = phy_get_txpwr_lmt(adapter, regd_name, band, tmp_bw, tlrs, ntx_idx, tmp_cch, 0);

		if (final_lmt > lmt) {
			if (reg_max)
				continue;
		} else if (final_lmt < lmt) {
			if (!reg_max)
				continue;
		} else { /* equal */
			if (final_bw == bw)
				continue;
		}

		final_lmt = lmt;
		final_cch = tmp_cch;
		final_bw = tmp_bw;
	}

	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

	if (0) {
		if (final_bw != bw && (IS_HT_RATE(rate) || IS_VHT_RATE(rate)))
			RTW_INFO("%s final_lmt: %s ch%u -> %s ch%u\n"
				, MGN_RATE_STR(rate)
				, ch_width_str(bw), cch
				, ch_width_str(final_bw), final_cch);
	}

exit:
	return final_lmt;
}

static void phy_txpwr_lmt_cck_ofdm_mt_chk(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct txpwr_lmt_ent *ent;
	_list *cur, *head;
	u8 channel, tlrs, ntx_idx;

	rfctl->txpwr_lmt_2g_cck_ofdm_state = 0;
#if CONFIG_IEEE80211_BAND_5GHZ
	rfctl->txpwr_lmt_5g_cck_ofdm_state = 0;
#endif

	head = &rfctl->txpwr_lmt_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
		cur = get_next(cur);

		/* check 2G CCK, OFDM state*/
		for (tlrs = TXPWR_LMT_RS_CCK; tlrs <= TXPWR_LMT_RS_OFDM; tlrs++) {
			for (ntx_idx = RF_1TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
				for (channel = 0; channel < CENTER_CH_2G_NUM; ++channel) {
					if (ent->lmt_2g[CHANNEL_WIDTH_20][tlrs][channel][ntx_idx] != hal_spec->txgi_max) {
						if (tlrs == TXPWR_LMT_RS_CCK)
							rfctl->txpwr_lmt_2g_cck_ofdm_state |= TXPWR_LMT_HAS_CCK_1T << ntx_idx;
						else
							rfctl->txpwr_lmt_2g_cck_ofdm_state |= TXPWR_LMT_HAS_OFDM_1T << ntx_idx;
						break;
					}
				}
			}
		}

		/* if 2G OFDM multi-TX is not defined, reference HT20 */
		for (channel = 0; channel < CENTER_CH_2G_NUM; ++channel) {
			for (ntx_idx = RF_2TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
				if (rfctl->txpwr_lmt_2g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx))
					continue;
				ent->lmt_2g[CHANNEL_WIDTH_20][TXPWR_LMT_RS_OFDM][channel][ntx_idx] =
					ent->lmt_2g[CHANNEL_WIDTH_20][TXPWR_LMT_RS_HT][channel][ntx_idx];
			}
		}

#if CONFIG_IEEE80211_BAND_5GHZ
		/* check 5G OFDM state*/
		for (ntx_idx = RF_1TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
			for (channel = 0; channel < CENTER_CH_5G_ALL_NUM; ++channel) {
				if (ent->lmt_5g[CHANNEL_WIDTH_20][TXPWR_LMT_RS_OFDM - 1][channel][ntx_idx] != hal_spec->txgi_max) {
					rfctl->txpwr_lmt_5g_cck_ofdm_state |= TXPWR_LMT_HAS_OFDM_1T << ntx_idx;
					break;
				}
			}
		}

		for (channel = 0; channel < CENTER_CH_5G_ALL_NUM; ++channel) {
			for (ntx_idx = RF_2TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
				if (rfctl->txpwr_lmt_5g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx))
					continue;
				/* if 5G OFDM multi-TX is not defined, reference HT20 */
				ent->lmt_5g[CHANNEL_WIDTH_20][TXPWR_LMT_RS_OFDM - 1][channel][ntx_idx] =
					ent->lmt_5g[CHANNEL_WIDTH_20][TXPWR_LMT_RS_HT - 1][channel][ntx_idx];
			}
		}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */
	}
}

#if CONFIG_IEEE80211_BAND_5GHZ
static void phy_txpwr_lmt_cross_ref_ht_vht(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct txpwr_lmt_ent *ent;
	_list *cur, *head;
	u8 bw, channel, tlrs, ref_tlrs, ntx_idx;
	int ht_ref_vht_5g_20_40 = 0;
	int vht_ref_ht_5g_20_40 = 0;
	int ht_has_ref_5g_20_40 = 0;
	int vht_has_ref_5g_20_40 = 0;

	rfctl->txpwr_lmt_5g_20_40_ref = 0;

	head = &rfctl->txpwr_lmt_list;
	cur = get_next(head);

	while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
		ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
		cur = get_next(cur);

		for (bw = 0; bw < MAX_5G_BANDWIDTH_NUM; ++bw) {

			for (channel = 0; channel < CENTER_CH_5G_ALL_NUM; ++channel) {

				for (tlrs = TXPWR_LMT_RS_HT; tlrs < TXPWR_LMT_RS_NUM; ++tlrs) {

					/* 5G 20M 40M VHT and HT can cross reference */
					if (bw == CHANNEL_WIDTH_20 || bw == CHANNEL_WIDTH_40) {
						if (tlrs == TXPWR_LMT_RS_HT)
							ref_tlrs = TXPWR_LMT_RS_VHT;
						else if (tlrs == TXPWR_LMT_RS_VHT)
							ref_tlrs = TXPWR_LMT_RS_HT;
						else
							continue;

						for (ntx_idx = RF_1TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {

							if (ent->lmt_5g[bw][ref_tlrs - 1][channel][ntx_idx] == hal_spec->txgi_max)
								continue;

							if (tlrs == TXPWR_LMT_RS_HT)
								ht_has_ref_5g_20_40++;
							else if (tlrs == TXPWR_LMT_RS_VHT)
								vht_has_ref_5g_20_40++;
							else
								continue;

							if (ent->lmt_5g[bw][tlrs - 1][channel][ntx_idx] != hal_spec->txgi_max)
								continue;

							if (tlrs == TXPWR_LMT_RS_HT && ref_tlrs == TXPWR_LMT_RS_VHT)
								ht_ref_vht_5g_20_40++;
							else if (tlrs == TXPWR_LMT_RS_VHT && ref_tlrs == TXPWR_LMT_RS_HT)
								vht_ref_ht_5g_20_40++;

							if (0)
								RTW_INFO("reg:%s, bw:%u, ch:%u, %s-%uT ref %s-%uT\n"
									, ent->regd_name, bw, channel
									, txpwr_lmt_rs_str(tlrs), ntx_idx + 1
									, txpwr_lmt_rs_str(ref_tlrs), ntx_idx + 1);

							ent->lmt_5g[bw][tlrs - 1][channel][ntx_idx] =
								ent->lmt_5g[bw][ref_tlrs - 1][channel][ntx_idx];
						}
					}

				}
			}
		}
	}

	if (0) {
		RTW_INFO("ht_ref_vht_5g_20_40:%d, ht_has_ref_5g_20_40:%d\n", ht_ref_vht_5g_20_40, ht_has_ref_5g_20_40);
		RTW_INFO("vht_ref_ht_5g_20_40:%d, vht_has_ref_5g_20_40:%d\n", vht_ref_ht_5g_20_40, vht_has_ref_5g_20_40);
	}

	/* 5G 20M&40M HT all come from VHT*/
	if (ht_ref_vht_5g_20_40 && ht_has_ref_5g_20_40 == ht_ref_vht_5g_20_40)
		rfctl->txpwr_lmt_5g_20_40_ref |= TXPWR_LMT_REF_HT_FROM_VHT;

	/* 5G 20M&40M VHT all come from HT*/
	if (vht_ref_ht_5g_20_40 && vht_has_ref_5g_20_40 == vht_ref_ht_5g_20_40)
		rfctl->txpwr_lmt_5g_20_40_ref |= TXPWR_LMT_REF_VHT_FROM_HT;
}
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

#ifndef DBG_TXPWR_LMT_BAND_CHK
#define DBG_TXPWR_LMT_BAND_CHK 0
#endif

#if DBG_TXPWR_LMT_BAND_CHK
/* check if larger bandwidth limit is less than smaller bandwidth for HT & VHT rate */
void phy_txpwr_limit_bandwidth_chk(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 band, bw, path, tlrs, ntx_idx, cch, offset, scch;
	u8 ch_num, n, i;

	for (band = BAND_ON_2_4G; band <= BAND_ON_5G; band++) {
		if (!hal_is_band_support(adapter, band))
			continue;

		for (bw = CHANNEL_WIDTH_40; bw <= CHANNEL_WIDTH_80; bw++) {
			if (bw >= CHANNEL_WIDTH_160)
				continue;
			if (band == BAND_ON_2_4G && bw >= CHANNEL_WIDTH_80)
				continue;

			if (band == BAND_ON_2_4G)
				ch_num = center_chs_2g_num(bw);
			else
				ch_num = center_chs_5g_num(bw);

			if (ch_num == 0) {
				rtw_warn_on(1);
				break;
			}

			for (tlrs = TXPWR_LMT_RS_HT; tlrs < TXPWR_LMT_RS_NUM; tlrs++) {

				if (band == BAND_ON_2_4G && tlrs == TXPWR_LMT_RS_VHT)
					continue;
				if (band == BAND_ON_5G && tlrs == TXPWR_LMT_RS_CCK)
					continue;
				if (bw > CHANNEL_WIDTH_20 && (tlrs == TXPWR_LMT_RS_CCK || tlrs == TXPWR_LMT_RS_OFDM))
					continue;
				if (bw > CHANNEL_WIDTH_40 && tlrs == TXPWR_LMT_RS_HT)
					continue;
				if (tlrs == TXPWR_LMT_RS_VHT && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
					continue;

				for (ntx_idx = RF_1TX; ntx_idx < MAX_TX_COUNT; ntx_idx++) {
					struct txpwr_lmt_ent *ent;
					_list *cur, *head;

					if (ntx_idx + 1 > hal_data->max_tx_cnt)
						continue;

					/* bypass CCK multi-TX is not defined */
					if (tlrs == TXPWR_LMT_RS_CCK && ntx_idx > RF_1TX) {
						if (band == BAND_ON_2_4G
							&& !(rfctl->txpwr_lmt_2g_cck_ofdm_state & (TXPWR_LMT_HAS_CCK_1T << ntx_idx)))
							continue;
					}

					/* bypass OFDM multi-TX is not defined */
					if (tlrs == TXPWR_LMT_RS_OFDM && ntx_idx > RF_1TX) {
						if (band == BAND_ON_2_4G
							&& !(rfctl->txpwr_lmt_2g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx)))
							continue;
						#if CONFIG_IEEE80211_BAND_5GHZ
						if (band == BAND_ON_5G
							&& !(rfctl->txpwr_lmt_5g_cck_ofdm_state & (TXPWR_LMT_HAS_OFDM_1T << ntx_idx)))
							continue;
						#endif
					}

					/* bypass 5G 20M, 40M pure reference */
					#if CONFIG_IEEE80211_BAND_5GHZ
					if (band == BAND_ON_5G && (bw == CHANNEL_WIDTH_20 || bw == CHANNEL_WIDTH_40)) {
						if (rfctl->txpwr_lmt_5g_20_40_ref == TXPWR_LMT_REF_HT_FROM_VHT) {
							if (tlrs == TXPWR_LMT_RS_HT)
								continue;
						} else if (rfctl->txpwr_lmt_5g_20_40_ref == TXPWR_LMT_REF_VHT_FROM_HT) {
							if (tlrs == TXPWR_LMT_RS_VHT && bw <= CHANNEL_WIDTH_40)
								continue;
						}
					}
					#endif

					for (n = 0; n < ch_num; n++) {
						u8 cch_by_bw[3];
						u8 offset_by_bw; /* bitmap, 0 for lower, 1 for upper */
						u8 bw_pos;
						s8 lmt[3];

						if (band == BAND_ON_2_4G)
							cch = center_chs_2g(bw, n);
						else
							cch = center_chs_5g(bw, n);

						if (cch == 0) {
							rtw_warn_on(1);
							break;
						}

						_rtw_memset(cch_by_bw, 0, 3);
						cch_by_bw[bw] = cch;
						offset_by_bw = 0x01;

						do {
							for (bw_pos = bw; bw_pos >= CHANNEL_WIDTH_40; bw_pos--)
								cch_by_bw[bw_pos - 1] = rtw_get_scch_by_cch_offset(cch_by_bw[bw_pos], bw_pos, offset_by_bw & BIT(bw_pos) ? HAL_PRIME_CHNL_OFFSET_UPPER : HAL_PRIME_CHNL_OFFSET_LOWER);

							head = &rfctl->txpwr_lmt_list;
							cur = get_next(head);
							while ((rtw_end_of_queue_search(head, cur)) == _FALSE) {
								ent = LIST_CONTAINOR(cur, struct txpwr_lmt_ent, list);
								cur = get_next(cur);

								for (bw_pos = bw; bw_pos < CHANNEL_WIDTH_160; bw_pos--)
									lmt[bw_pos] = phy_get_txpwr_lmt(adapter, ent->regd_name, band, bw_pos, tlrs, ntx_idx, cch_by_bw[bw_pos], 0);

								for (bw_pos = bw; bw_pos > CHANNEL_WIDTH_20; bw_pos--)
									if (lmt[bw_pos] > lmt[bw_pos - 1])
										break;
								if (bw_pos == CHANNEL_WIDTH_20)
									continue;

								RTW_PRINT_SEL(RTW_DBGDUMP, "[%s][%s][%s][%uT][%-4s] cch:"
									, band_str(band)
									, ch_width_str(bw)
									, txpwr_lmt_rs_str(tlrs)
									, ntx_idx + 1
									, ent->regd_name
								);
								for (bw_pos = bw; bw_pos < CHANNEL_WIDTH_160; bw_pos--)
									_RTW_PRINT_SEL(RTW_DBGDUMP, "%03u ", cch_by_bw[bw_pos]);
								_RTW_PRINT_SEL(RTW_DBGDUMP, "limit:");
								for (bw_pos = bw; bw_pos < CHANNEL_WIDTH_160; bw_pos--) {
									if (lmt[bw_pos] == hal_spec->txgi_max)
										_RTW_PRINT_SEL(RTW_DBGDUMP, "N/A ");
									else if (lmt[bw_pos] > -hal_spec->txgi_pdbm && lmt[bw_pos] < 0) /* -1 < value < 0 */
										_RTW_PRINT_SEL(RTW_DBGDUMP, "-0.%d", (rtw_abs(lmt[bw_pos]) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
									else if (lmt[bw_pos] % hal_spec->txgi_pdbm)
										_RTW_PRINT_SEL(RTW_DBGDUMP, "%2d.%d ", lmt[bw_pos] / hal_spec->txgi_pdbm, (rtw_abs(lmt[bw_pos]) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
									else
										_RTW_PRINT_SEL(RTW_DBGDUMP, "%2d ", lmt[bw_pos] / hal_spec->txgi_pdbm);
								}
								_RTW_PRINT_SEL(RTW_DBGDUMP, "\n");
							}
							for (bw_pos = bw; bw_pos < CHANNEL_WIDTH_160; bw_pos--)
								lmt[bw_pos] = phy_get_txpwr_lmt(adapter, regd_str(TXPWR_LMT_WW), band, bw_pos, tlrs, ntx_idx, cch_by_bw[bw_pos], 0);

							for (bw_pos = bw; bw_pos > CHANNEL_WIDTH_20; bw_pos--)
								if (lmt[bw_pos] > lmt[bw_pos - 1])
									break;
							if (bw_pos != CHANNEL_WIDTH_20) {
								RTW_PRINT_SEL(RTW_DBGDUMP, "[%s][%s][%s][%uT][%-4s] cch:"
									, band_str(band)
									, ch_width_str(bw)
									, txpwr_lmt_rs_str(tlrs)
									, ntx_idx + 1
									, regd_str(TXPWR_LMT_WW)
								);
								for (bw_pos = bw; bw_pos < CHANNEL_WIDTH_160; bw_pos--)
									_RTW_PRINT_SEL(RTW_DBGDUMP, "%03u ", cch_by_bw[bw_pos]);
								_RTW_PRINT_SEL(RTW_DBGDUMP, "limit:");
								for (bw_pos = bw; bw_pos < CHANNEL_WIDTH_160; bw_pos--) {
									if (lmt[bw_pos] == hal_spec->txgi_max)
										_RTW_PRINT_SEL(RTW_DBGDUMP, "N/A ");
									else if (lmt[bw_pos] > -hal_spec->txgi_pdbm && lmt[bw_pos] < 0) /* -1 < value < 0 */
										_RTW_PRINT_SEL(RTW_DBGDUMP, "-0.%d", (rtw_abs(lmt[bw_pos]) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
									else if (lmt[bw_pos] % hal_spec->txgi_pdbm)
										_RTW_PRINT_SEL(RTW_DBGDUMP, "%2d.%d ", lmt[bw_pos] / hal_spec->txgi_pdbm, (rtw_abs(lmt[bw_pos]) % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
									else
										_RTW_PRINT_SEL(RTW_DBGDUMP, "%2d ", lmt[bw_pos] / hal_spec->txgi_pdbm);
								}
								_RTW_PRINT_SEL(RTW_DBGDUMP, "\n");
							}

							offset_by_bw += 2;
							if (offset_by_bw & BIT(bw + 1))
								break;
						} while (1); /* loop for all ch combinations */
					} /* loop for center channels */
				} /* loop fo each ntx_idx */
			} /* loop for tlrs */
		} /* loop for bandwidth */
	} /* loop for band */
}
#endif /* DBG_TXPWR_LMT_BAND_CHK */

static void phy_txpwr_lmt_post_hdl(_adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	_irqL irqL;

	_enter_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);

#if CONFIG_IEEE80211_BAND_5GHZ
	if (IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
		phy_txpwr_lmt_cross_ref_ht_vht(adapter);
#endif
	phy_txpwr_lmt_cck_ofdm_mt_chk(adapter);

#if DBG_TXPWR_LMT_BAND_CHK
	phy_txpwr_limit_bandwidth_chk(adapter);
#endif

	_exit_critical_mutex(&rfctl->txpwr_lmt_mutex, &irqL);
}

BOOLEAN
GetS1ByteIntegerFromStringInDecimal(
			char	*str,
			s8		*val
)
{
	u8 negative = 0;
	u16 i = 0;

	*val = 0;

	while (str[i] != '\0') {
		if (i == 0 && (str[i] == '+' || str[i] == '-')) {
			if (str[i] == '-')
				negative = 1;
		} else if (str[i] >= '0' && str[i] <= '9') {
			*val *= 10;
			*val += (str[i] - '0');
		} else
			return _FALSE;
		++i;
	}

	if (negative)
		*val = -*val;

	return _TRUE;
}
#endif /* CONFIG_TXPWR_LIMIT */

/*
* phy_set_tx_power_limit - Parsing TX power limit from phydm array, called by odm_ConfigBB_TXPWR_LMT_XXX in phydm
*/
void
phy_set_tx_power_limit(
		struct dm_struct		*pDM_Odm,
		u8				*Regulation,
		u8				*Band,
		u8				*Bandwidth,
		u8				*RateSection,
		u8				*ntx,
		u8				*Channel,
		u8				*PowerLimit
)
{
#if CONFIG_TXPWR_LIMIT
	PADAPTER Adapter = pDM_Odm->adapter;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(Adapter);
	u8 band = 0, bandwidth = 0, tlrs = 0, channel;
	u8 ntx_idx;
	s8 powerLimit = 0, prevPowerLimit, channelIndex;
	s8 ww_lmt_val = phy_txpwr_ww_lmt_value(Adapter);

	if (0)
		RTW_INFO("Index of power limit table [regulation %s][band %s][bw %s][rate section %s][ntx %s][chnl %s][val %s]\n"
			, Regulation, Band, Bandwidth, RateSection, ntx, Channel, PowerLimit);

	if (GetU1ByteIntegerFromStringInDecimal((char *)Channel, &channel) == _FALSE
		|| GetS1ByteIntegerFromStringInDecimal((char *)PowerLimit, &powerLimit) == _FALSE
	) {
		RTW_PRINT("Illegal index of power limit table [ch %s][val %s]\n", Channel, PowerLimit);
		return;
	}

	if (powerLimit != ww_lmt_val) {
		if (powerLimit < -hal_spec->txgi_max || powerLimit > hal_spec->txgi_max)
			RTW_PRINT("Illegal power limit value [ch %s][val %s]\n", Channel, PowerLimit);

		if (powerLimit > hal_spec->txgi_max)
			powerLimit = hal_spec->txgi_max;
		else if (powerLimit < -hal_spec->txgi_max)
			powerLimit =  ww_lmt_val + 1;
	}

	if (strncmp(RateSection, "CCK", 3) == 0)
		tlrs = TXPWR_LMT_RS_CCK;
	else if (strncmp(RateSection, "OFDM", 4) == 0)
		tlrs = TXPWR_LMT_RS_OFDM;
	else if (strncmp(RateSection, "HT", 2) == 0)
		tlrs = TXPWR_LMT_RS_HT;
	else if (strncmp(RateSection, "VHT", 3) == 0)
		tlrs = TXPWR_LMT_RS_VHT;
	else {
		RTW_PRINT("Wrong rate section:%s\n", RateSection);
		return;
	}

	if (strncmp(ntx, "1T", 2) == 0)
		ntx_idx = RF_1TX;
	else if (strncmp(ntx, "2T", 2) == 0)
		ntx_idx = RF_2TX;
	else if (strncmp(ntx, "3T", 2) == 0)
		ntx_idx = RF_3TX;
	else if (strncmp(ntx, "4T", 2) == 0)
		ntx_idx = RF_4TX;
	else {
		RTW_PRINT("Wrong tx num:%s\n", ntx);
		return;
	}

	if (strncmp(Bandwidth, "20M", 3) == 0)
		bandwidth = CHANNEL_WIDTH_20;
	else if (strncmp(Bandwidth, "40M", 3) == 0)
		bandwidth = CHANNEL_WIDTH_40;
	else if (strncmp(Bandwidth, "80M", 3) == 0)
		bandwidth = CHANNEL_WIDTH_80;
	else if (strncmp(Bandwidth, "160M", 4) == 0)
		bandwidth = CHANNEL_WIDTH_160;
	else {
		RTW_PRINT("unknown bandwidth: %s\n", Bandwidth);
		return;
	}

	if (strncmp(Band, "2.4G", 4) == 0) {
		band = BAND_ON_2_4G;
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_2_4G, channel);

		if (channelIndex == -1) {
			RTW_PRINT("unsupported channel: %d at 2.4G\n", channel);
			return;
		}

		if (bandwidth >= MAX_2_4G_BANDWIDTH_NUM) {
			RTW_PRINT("unsupported bandwidth: %s at 2.4G\n", Bandwidth);
			return;
		}

		rtw_txpwr_lmt_add(adapter_to_rfctl(Adapter), Regulation, band, bandwidth, tlrs, ntx_idx, channelIndex, powerLimit);
	}
#if CONFIG_IEEE80211_BAND_5GHZ
	else if (strncmp(Band, "5G", 2) == 0) {
		band = BAND_ON_5G;
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_5G, channel);

		if (channelIndex == -1) {
			RTW_PRINT("unsupported channel: %d at 5G\n", channel);
			return;
		}

		rtw_txpwr_lmt_add(adapter_to_rfctl(Adapter), Regulation, band, bandwidth, tlrs, ntx_idx, channelIndex, powerLimit);
	}
#endif
	else {
		RTW_PRINT("unknown/unsupported band:%s\n", Band);
		return;
	}
#endif
}

void
phy_set_tx_power_limit_ex(
		struct dm_struct		*pDM_Odm,
		u8				Regulation,
		u8				Band,
		u8				Bandwidth,
		u8				RateSection,
		u8				ntx,
		u8				channel,
		s8				powerLimit
)
{
#if CONFIG_TXPWR_LIMIT
	PADAPTER Adapter = pDM_Odm->adapter;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(Adapter);
	u8 regd;
	u8 band = 0, bandwidth = 0, tlrs = 0;
	u8 ntx_idx;
	s8 prevPowerLimit, channelIndex;
	s8 ww_lmt_val = phy_txpwr_ww_lmt_value(Adapter);

	if (0)
		RTW_INFO("Index of power limit table [regulation %d][band %d][bw %d][rate section %d][ntx %d][chnl %d][val %d]\n"
			, Regulation, Band, Bandwidth, RateSection, ntx, channel, powerLimit);

	if (powerLimit != ww_lmt_val) {
		if (powerLimit < -hal_spec->txgi_max || powerLimit > hal_spec->txgi_max)
			RTW_PRINT("Illegal power limit value [ch %d][val %d]\n", channel, powerLimit);

		if (powerLimit > hal_spec->txgi_max)
			powerLimit = hal_spec->txgi_max;
		else if (powerLimit < -hal_spec->txgi_max)
			powerLimit =  ww_lmt_val + 1;
	}

	switch (Regulation) {
	case PW_LMT_REGU_FCC:
		regd = TXPWR_LMT_FCC;
		break;
	case PW_LMT_REGU_ETSI:
		regd = TXPWR_LMT_ETSI;
		break;
	case PW_LMT_REGU_MKK:
		regd = TXPWR_LMT_MKK;
		break;
	case PW_LMT_REGU_IC:
		regd = TXPWR_LMT_IC;
		break;
	case PW_LMT_REGU_KCC:
		regd = TXPWR_LMT_KCC;
		break;
	case PW_LMT_REGU_ACMA:
		regd = TXPWR_LMT_ACMA;
		break;
	case PW_LMT_REGU_CHILE:
		regd = TXPWR_LMT_CHILE;
		break;
	case PW_LMT_REGU_UKRAINE:
		regd = TXPWR_LMT_UKRAINE;
		break;
	case PW_LMT_REGU_MEXICO:
		regd = TXPWR_LMT_MEXICO;
		break;
	case PW_LMT_REGU_CN:
		regd = TXPWR_LMT_CN;
		break;
	case PW_LMT_REGU_WW13:
	default:	
		RTW_PRINT("Wrong regulation:%d\n", Regulation);
		return;		
	}

	switch (RateSection) {
	case PW_LMT_RS_CCK:
		tlrs = TXPWR_LMT_RS_CCK;
		break;
	case PW_LMT_RS_OFDM:
		tlrs = TXPWR_LMT_RS_OFDM;
		break;
	case PW_LMT_RS_HT:
		tlrs = TXPWR_LMT_RS_HT;
		break;
	case PW_LMT_RS_VHT:
		tlrs = TXPWR_LMT_RS_VHT;
		break;
	default:
		RTW_PRINT("Wrong rate section:%d\n", RateSection);
		return;
	}

	switch (ntx) {
	case PW_LMT_PH_1T:
		ntx_idx = RF_1TX;
		break;
	case PW_LMT_PH_2T:
		ntx_idx = RF_2TX;
		break;
	case PW_LMT_PH_3T:
		ntx_idx = RF_3TX;
		break;
	case PW_LMT_PH_4T:
		ntx_idx = RF_4TX;
		break;
	default:
		RTW_PRINT("Wrong tx num:%d\n", ntx);
		return;
	}

	switch (Bandwidth) {
	case PW_LMT_BW_20M:
		bandwidth = CHANNEL_WIDTH_20;
		break;
	case PW_LMT_BW_40M:
		bandwidth = CHANNEL_WIDTH_40;
		break;
	case PW_LMT_BW_80M:
		bandwidth = CHANNEL_WIDTH_80;
		break;
	case PW_LMT_BW_160M:
		bandwidth = CHANNEL_WIDTH_160;
		break;
	default:
		RTW_PRINT("unknown bandwidth: %d\n", Bandwidth);
		return;
	}

	if (Band == PW_LMT_BAND_2_4G) {
		band = BAND_ON_2_4G;
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_2_4G, channel);

		if (channelIndex == -1) {
			RTW_PRINT("unsupported channel: %d at 2.4G\n", channel);
			return;
		}

		if (bandwidth >= MAX_2_4G_BANDWIDTH_NUM) {
			RTW_PRINT("unsupported bandwidth: %s at 2.4G\n", ch_width_str(bandwidth));
			return;
		}

		rtw_txpwr_lmt_add(adapter_to_rfctl(Adapter), regd_str(regd), band, bandwidth, tlrs, ntx_idx, channelIndex, powerLimit);
	}
#if CONFIG_IEEE80211_BAND_5GHZ
	else if (Band == PW_LMT_BAND_5G) {
		band = BAND_ON_5G;
		channelIndex = phy_GetChannelIndexOfTxPowerLimit(BAND_ON_5G, channel);

		if (channelIndex == -1) {
			RTW_PRINT("unsupported channel: %d at 5G\n", channel);
			return;
		}

		rtw_txpwr_lmt_add(adapter_to_rfctl(Adapter), regd_str(regd), band, bandwidth, tlrs, ntx_idx, channelIndex, powerLimit);
	}
#endif
	else {
		RTW_PRINT("unknown/unsupported band:%d\n", Band);
		return;
	}
#endif
}

u8 phy_get_tx_power_index_ex(_adapter *adapter
	, enum rf_path rfpath, RATE_SECTION rs, enum MGN_RATE rate
	, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch)
{
	return rtw_hal_get_tx_power_index(adapter, rfpath, rs, rate, bw, band, cch, opch, NULL);
}

u8 phy_get_tx_power_index(
		PADAPTER			pAdapter,
		enum rf_path		RFPath,
		u8					Rate,
		enum channel_width	BandWidth,
		u8					Channel
)
{
	RATE_SECTION rs = mgn_rate_to_rs(Rate);
	BAND_TYPE band = Channel <= 14 ? BAND_ON_2_4G : BAND_ON_5G;

	return rtw_hal_get_tx_power_index(pAdapter, RFPath, rs, Rate, BandWidth, band, Channel, 0, NULL);
}

void
PHY_SetTxPowerIndex(
		PADAPTER		pAdapter,
		u32				PowerIndex,
		enum rf_path		RFPath,
		u8				Rate
)
{
	rtw_hal_set_tx_power_index(pAdapter, PowerIndex, RFPath, Rate);
}

void dump_tx_power_index_inline(void *sel, _adapter *adapter, u8 rfpath, enum channel_width bw, u8 cch, enum MGN_RATE rate, u8 pwr_idx, struct txpwr_idx_comp *tic)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	if (tic->utarget == hal_spec->txgi_max) {
		RTW_PRINT_SEL(sel, "TXPWR: [%c][%s]cch:%u, %s %uT, idx:%u(0x%02x) = base(%d) + min((byr(%d) + btc(%d) + extra(%d)), rlmt(%d), lmt(%d), ulmt(%d)) + tpc(%d) + tpt(%d) + dpd(%d)\n"
			, rf_path_char(rfpath), ch_width_str(bw), cch
			, MGN_RATE_STR(rate), tic->ntx_idx + 1
			, pwr_idx, pwr_idx, tic->base
			, tic->by_rate, tic->btc, tic->extra, tic->rlimit, tic->limit, tic->ulimit
			, tic->tpc
			, tic->tpt, tic->dpd);
	} else {
		RTW_PRINT_SEL(sel, "TXPWR: [%c][%s]cch:%u, %s %uT, idx:%u(0x%02x) = base(%d) + min(utgt(%d), rlmt(%d), lmt(%d), ulmt(%d)) + tpc(%d) + tpt(%d) + dpd(%d)\n"
			, rf_path_char(rfpath), ch_width_str(bw), cch
			, MGN_RATE_STR(rate), tic->ntx_idx + 1
			, pwr_idx, pwr_idx, tic->base
			, tic->utarget, tic->rlimit, tic->limit, tic->ulimit
			, tic->tpc
			, tic->tpt, tic->dpd);
	}
}

#ifdef CONFIG_PROC_DEBUG
void dump_tx_power_idx_value(void *sel, _adapter *adapter, u8 rfpath, enum MGN_RATE rate, u8 pwr_idx, struct txpwr_idx_comp *tic)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	char tmp_str[8];

	txpwr_idx_get_dbm_str(tic->target, hal_spec->txgi_max, hal_spec->txgi_pdbm, 0, tmp_str, 8);

	if (tic->utarget == hal_spec->txgi_max) {
		RTW_PRINT_SEL(sel, "%4c %9s %uT %s %3u(0x%02x)"
			"   %4d      ((%4d   %3d   %5d)  %4d  %4d  %4d)   %3d   %3d   %3d\n"
			, rf_path_char(rfpath), MGN_RATE_STR(rate), tic->ntx_idx + 1
			, tmp_str, pwr_idx, pwr_idx
			, tic->base, tic->by_rate, tic->btc, tic->extra, tic->rlimit, tic->limit, tic->ulimit
			, tic->tpc
			, tic->tpt, tic->dpd);
	} else {
		RTW_PRINT_SEL(sel, "%4c %9s %uT %s %3u(0x%02x)"
			"   %4d      (%4d  %4d  %4d  %4d)   %3d   %3d   %3d\n"
			, rf_path_char(rfpath), MGN_RATE_STR(rate), tic->ntx_idx + 1
			, tmp_str, pwr_idx, pwr_idx
			, tic->base, tic->utarget, tic->rlimit, tic->limit, tic->ulimit
			, tic->tpc
			, tic->tpt, tic->dpd);
	}
}

void dump_tx_power_idx_title(void *sel, _adapter *adapter, enum channel_width bw, u8 cch, u8 opch)
{
	u8 cch_20, cch_40, cch_80;

	cch_80 = bw == CHANNEL_WIDTH_80 ? cch : 0;
	cch_40 = bw == CHANNEL_WIDTH_40 ? cch : 0;
	cch_20 = bw == CHANNEL_WIDTH_20 ? cch : 0;
	if (cch_80 != 0)
		cch_40 = rtw_get_scch_by_cch_opch(cch_80, CHANNEL_WIDTH_80, opch);
	if (cch_40 != 0)
		cch_20 = rtw_get_scch_by_cch_opch(cch_40, CHANNEL_WIDTH_40, opch);

	RTW_PRINT_SEL(sel, "%s", ch_width_str(bw));
	if (bw >= CHANNEL_WIDTH_80)
		_RTW_PRINT_SEL(sel, ", cch80:%u", cch_80);
	if (bw >= CHANNEL_WIDTH_40)
		_RTW_PRINT_SEL(sel, ", cch40:%u", cch_40);
	_RTW_PRINT_SEL(sel, ", cch20:%u\n", cch_20);

	if (!phy_is_txpwr_user_target_specified(adapter)) {
		RTW_PRINT_SEL(sel, "%-4s %-9s %2s %-6s %-3s%6s"
			" = %-4s + min((%-4s + %-3s + %-5s), %-4s, %-4s, %-4s) + %-3s + %-3s + %-3s\n"
			, "path", "rate", "", "dBm", "idx", ""
			, "base", "byr", "btc", "extra", "rlmt", "lmt", "ulmt"
			, "tpc"
			, "tpt", "dpd");
	} else {
		RTW_PRINT_SEL(sel, "%-4s %-9s %2s %-6s %-3s%6s"
			" = %-4s + min(%-4s, %-4s, %-4s, %-4s) + %-3s + %-3s + %-3s\n"
			, "path", "rate", "", "dBm", "idx", ""
			, "base", "utgt", "rlmt", "lmt", "ulmt"
			, "tpc"
			, "tpt", "dpd");
	}
}

void dump_tx_power_idx_by_path_rs(void *sel, _adapter *adapter, u8 rfpath
	, RATE_SECTION rs, enum channel_width bw, u8 cch, u8 opch)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 power_idx;
	struct txpwr_idx_comp tic;
	u8 tx_num, i;
	u8 band = cch > 14 ? BAND_ON_5G : BAND_ON_2_4G;

	if (!HAL_SPEC_CHK_RF_PATH(hal_spec, band, rfpath))
		return;

	if (rs >= RATE_SECTION_NUM)
		return;

	tx_num = rate_section_to_tx_num(rs);
	if (tx_num + 1 > hal_data->tx_nss)
		return;

	if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
		return;

	if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
		return;

	for (i = 0; i < rates_by_sections[rs].rate_num; i++) {
		power_idx = rtw_hal_get_tx_power_index(adapter, rfpath, rs, rates_by_sections[rs].rates[i], bw, band, cch, opch, &tic);
		dump_tx_power_idx_value(sel, adapter, rfpath, rates_by_sections[rs].rates[i], power_idx, &tic);
	}
}

void dump_tx_power_idx(void *sel, _adapter *adapter, enum channel_width bw, u8 cch, u8 opch)
{
	u8 rfpath, rs;

	dump_tx_power_idx_title(sel, adapter, bw, cch, opch);
	for (rfpath = RF_PATH_A; rfpath < RF_PATH_MAX; rfpath++)
		for (rs = CCK; rs < RATE_SECTION_NUM; rs++)
			dump_tx_power_idx_by_path_rs(sel, adapter, rfpath, rs, bw, cch, opch);
}

void dump_txpwr_total_dbm_value(void *sel, _adapter *adapter, enum MGN_RATE rate, u8 ntx_idx
	, s16 target, s16 byr, s16 btc, s16 extra, s16 rlmt, s16 lmt, s16 ulmt, s16 tpc)
{
	char target_str[8];
	char byr_str[8];
	char btc_str[8];
	char extra_str[8];
	char rlmt_str[8];
	char lmt_str[8];
	char ulmt_str[8];
	char tpc_str[8];

	txpwr_mbm_get_dbm_str(target, 0, target_str, 8);
	txpwr_mbm_get_dbm_str(byr, 0, byr_str, 8);
	txpwr_mbm_get_dbm_str(btc, 0, btc_str, 8);
	txpwr_mbm_get_dbm_str(extra, 0, extra_str, 8);
	txpwr_mbm_get_dbm_str(rlmt, 0, rlmt_str, 8);
	txpwr_mbm_get_dbm_str(lmt, 0, lmt_str, 8);
	txpwr_mbm_get_dbm_str(ulmt, 0, ulmt_str, 8);
	txpwr_mbm_get_dbm_str(tpc, 0, tpc_str, 8);

	RTW_PRINT_SEL(sel, "%9s %uT %s =    ((%s   %s   %s), %s, %s, %s)   %s\n"
		, MGN_RATE_STR(rate), ntx_idx + 1
		, target_str, byr_str, btc_str, extra_str, rlmt_str, lmt_str, ulmt_str, tpc_str);
}

void dump_txpwr_total_dbm_value_utgt(void *sel, _adapter *adapter, enum MGN_RATE rate, u8 ntx_idx
	, s16 target, s16 utgt, s16 rlmt, s16 lmt, s16 ulmt, s16 tpc)
{
	char target_str[8];
	char utgt_str[8];
	char rlmt_str[8];
	char lmt_str[8];
	char ulmt_str[8];
	char tpc_str[8];

	txpwr_mbm_get_dbm_str(target, 0, target_str, 8);
	txpwr_mbm_get_dbm_str(utgt, 0, utgt_str, 8);
	txpwr_mbm_get_dbm_str(rlmt, 0, rlmt_str, 8);
	txpwr_mbm_get_dbm_str(lmt, 0, lmt_str, 8);
	txpwr_mbm_get_dbm_str(ulmt, 0, ulmt_str, 8);
	txpwr_mbm_get_dbm_str(tpc, 0, tpc_str, 8);

	RTW_PRINT_SEL(sel, "%9s %uT %s =    (%s, %s, %s, %s)   %s\n"
		, MGN_RATE_STR(rate), ntx_idx + 1
		, target_str, utgt_str, rlmt_str, lmt_str, ulmt_str, tpc_str);
}

void dump_txpwr_total_dbm_title(void *sel, _adapter *adapter, enum channel_width bw, u8 cch, u8 opch)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	char antenna_gain_str[8];
	u8 cch_20, cch_40, cch_80;

	txpwr_mbm_get_dbm_str(rfctl->antenna_gain, 0, antenna_gain_str, 8);
	RTW_PRINT_SEL(sel, "antenna_gain:%s\n", antenna_gain_str);

	cch_80 = bw == CHANNEL_WIDTH_80 ? cch : 0;
	cch_40 = bw == CHANNEL_WIDTH_40 ? cch : 0;
	cch_20 = bw == CHANNEL_WIDTH_20 ? cch : 0;
	if (cch_80 != 0)
		cch_40 = rtw_get_scch_by_cch_opch(cch_80, CHANNEL_WIDTH_80, opch);
	if (cch_40 != 0)
		cch_20 = rtw_get_scch_by_cch_opch(cch_40, CHANNEL_WIDTH_40, opch);

	RTW_PRINT_SEL(sel, "%s", ch_width_str(bw));
	if (bw >= CHANNEL_WIDTH_80)
		_RTW_PRINT_SEL(sel, ", cch80:%u", cch_80);
	if (bw >= CHANNEL_WIDTH_40)
		_RTW_PRINT_SEL(sel, ", cch40:%u", cch_40);
	_RTW_PRINT_SEL(sel, ", cch20:%u\n", cch_20);

	if (!phy_is_txpwr_user_target_specified(adapter)) {
		RTW_PRINT_SEL(sel, "%-9s %2s %-6s = min((%-6s + %-6s + %-6s), %-6s, %-6s, %-6s) + %-6s\n"
			, "rate", "", "target", "byr", "btc", "extra", "rlmt", "lmt", "ulmt", "tpc");
	} else {
		RTW_PRINT_SEL(sel, "%-9s %2s %-6s = min(%-6s, %-6s, %-6s, %-6s) + %-6s\n"
			, "rate", "", "target", "utgt", "rlmt", "lmt", "ulmt", "tpc");
	}
}

void dump_txpwr_total_dbm_by_rs(void *sel, _adapter *adapter, u8 rs, enum channel_width bw, u8 cch, u8 opch)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 i;
	u8 band = cch > 14 ? BAND_ON_5G : BAND_ON_2_4G;

	if (rs >= RATE_SECTION_NUM)
		return;

	if (rate_section_to_tx_num(rs) + 1 > hal_data->tx_nss)
		return;

	if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
		return;

	if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
		return;

	for (i = 0; i < rates_by_sections[rs].rate_num; i++) {
		struct txpwr_idx_comp tic;
		s16 target, byr, tpc, btc, extra, utgt, rlmt, lmt, ulmt;
		u8 tx_num;

		target = phy_get_txpwr_total_mbm(adapter, rs, rates_by_sections[rs].rates[i], bw, cch, opch, 0, 0, &tic);
		tx_num = tic.ntx_idx + 1;
		if (tic.rlimit == hal_spec->txgi_max)
			rlmt = UNSPECIFIED_MBM;
		else
			rlmt = ((tic.rlimit * MBM_PDBM) / hal_spec->txgi_pdbm) + mb_of_ntx(tx_num);
		if (tic.limit == hal_spec->txgi_max)
			lmt = UNSPECIFIED_MBM;
		else
			lmt = ((tic.limit * MBM_PDBM) / hal_spec->txgi_pdbm) + mb_of_ntx(tx_num);
		if (tic.ulimit == hal_spec->txgi_max)
			ulmt = UNSPECIFIED_MBM;
		else
			ulmt = ((tic.ulimit * MBM_PDBM) / hal_spec->txgi_pdbm) + mb_of_ntx(tx_num);
		tpc = (tic.tpc * MBM_PDBM) / hal_spec->txgi_pdbm;

		if (tic.utarget == hal_spec->txgi_max) {
			byr = ((tic.by_rate * MBM_PDBM) / hal_spec->txgi_pdbm) + mb_of_ntx(tx_num);
			btc = (tic.btc * MBM_PDBM) / hal_spec->txgi_pdbm;
			extra = (tic.extra * MBM_PDBM) / hal_spec->txgi_pdbm;
			dump_txpwr_total_dbm_value(sel, adapter, rates_by_sections[rs].rates[i], tic.ntx_idx
				, target, byr, btc, extra, rlmt, lmt, ulmt, tpc);
		} else {
			utgt = ((tic.utarget * MBM_PDBM) / hal_spec->txgi_pdbm) + mb_of_ntx(tx_num);
			dump_txpwr_total_dbm_value_utgt(sel, adapter, rates_by_sections[rs].rates[i], tic.ntx_idx
				, target, utgt, rlmt, lmt, ulmt, tpc);
		}
	}
}

/* dump txpowr in dBm with effect of N-TX */
void dump_txpwr_total_dbm(void *sel, _adapter *adapter, enum channel_width bw, u8 cch, u8 opch)
{
	u8 rs;

	dump_txpwr_total_dbm_title(sel, adapter, bw, cch, opch);
	for (rs = CCK; rs < RATE_SECTION_NUM; rs++)
		dump_txpwr_total_dbm_by_rs(sel, adapter, rs, bw, cch, opch);
}
#endif

bool phy_is_tx_power_limit_needed(_adapter *adapter)
{
#if CONFIG_TXPWR_LIMIT
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));

	if (regsty->RegEnableTxPowerLimit == 1
		|| (regsty->RegEnableTxPowerLimit == 2 && hal_data->EEPROMRegulatory == 1))
		return _TRUE;
#endif

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

#if CONFIG_TXPWR_LIMIT
int phy_load_tx_power_limit(_adapter *adapter, u8 chk_file)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = dvobj_to_regsty(adapter_to_dvobj(adapter));
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	int ret = _FAIL;

	hal_data->txpwr_limit_loaded = 0;
	rtw_regd_exc_list_free(rfctl);
	rtw_txpwr_lmt_list_free(rfctl);

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
	if (odm_config_rf_with_header_file(&hal_data->odmpriv, CONFIG_RF_TXPWR_LMT, RF_PATH_A) == HAL_STATUS_SUCCESS) {
		RTW_INFO("default power limit loaded\n");
		hal_data->txpwr_limit_from_file = 0;
		goto post_hdl;
	}
#endif

	RTW_ERR("%s():Read Tx power limit fail\n", __func__);
	goto exit;

post_hdl:
	phy_txpwr_lmt_post_hdl(adapter);
	rtw_txpwr_init_regd(rfctl);
	hal_data->txpwr_limit_loaded = 1;
	ret = _SUCCESS;

exit:
	return ret;
}
#endif /* CONFIG_TXPWR_LIMIT */

void phy_load_tx_power_ext_info(_adapter *adapter, u8 chk_file)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);

	/* check registy target tx power */
	regsty->target_tx_pwr_valid = rtw_regsty_chk_target_tx_power_valid(adapter);

	/* power by rate */
	if (phy_is_tx_power_by_rate_needed(adapter)
		|| regsty->target_tx_pwr_valid != _TRUE /* need target tx power from by rate table */
	)
		phy_load_tx_power_by_rate(adapter, chk_file);

	/* power limit */
#if CONFIG_TXPWR_LIMIT
	if (phy_is_tx_power_limit_needed(adapter))
		phy_load_tx_power_limit(adapter, chk_file);
#endif
}

inline void phy_reload_tx_power_ext_info(_adapter *adapter)
{
	phy_load_tx_power_ext_info(adapter, 1);
	op_class_pref_apply_regulatory(adapter, REG_TXPWR_CHANGE);
}

inline void phy_reload_default_tx_power_ext_info(_adapter *adapter)
{
	phy_load_tx_power_ext_info(adapter, 0);
	op_class_pref_apply_regulatory(adapter, REG_TXPWR_CHANGE);
}

#ifdef CONFIG_PROC_DEBUG
void dump_tx_power_ext_info(void *sel, _adapter *adapter)
{
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	RTW_PRINT_SEL(sel, "txpwr_pg_mode: %s\n", txpwr_pg_mode_str(hal_data->txpwr_pg_mode));

	if (regsty->target_tx_pwr_valid == _TRUE)
		RTW_PRINT_SEL(sel, "target_tx_power: from registry\n");
	else if (hal_data->txpwr_by_rate_loaded)
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
				if (tx_num + 1 > hal_data->tx_nss)
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
					continue;

				target = phy_get_target_txpwr(adapter, band, path, rs);

				if (target % hal_spec->txgi_pdbm) {
					_RTW_PRINT_SEL(sel, "%7s: %2d.%d\n", rate_section_str(rs)
						, target / hal_spec->txgi_pdbm, (target % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
				} else {
					_RTW_PRINT_SEL(sel, "%7s: %5d\n", rate_section_str(rs)
						, target / hal_spec->txgi_pdbm);
				}
			}
		}
	}

	return;
}

void dump_tx_power_by_rate(void *sel, _adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	int path, tx_num, band, n, rs;
	u8 rate_num, max_rate_num, base;
	s8 by_rate;

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
				if (tx_num + 1 > hal_data->tx_nss)
					continue;

				if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
					continue;

				if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
					continue;

				if (IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
					max_rate_num = 10;
				else
					max_rate_num = 8;
				rate_num = rate_section_rate_num(rs);

				RTW_PRINT_SEL(sel, "%7s: ", rate_section_str(rs));

				/* dump power by rate in db */
				for (n = rate_num - 1; n >= 0; n--) {
					by_rate = phy_get_txpwr_by_rate(adapter, band, path, rs, rates_by_sections[rs].rates[n]);
					if (by_rate % hal_spec->txgi_pdbm) {
						_RTW_PRINT_SEL(sel, "%2d.%d ", by_rate / hal_spec->txgi_pdbm
							, (by_rate % hal_spec->txgi_pdbm) * 100 / hal_spec->txgi_pdbm);
					} else
						_RTW_PRINT_SEL(sel, "%5d ", by_rate / hal_spec->txgi_pdbm);
				}
				for (n = 0; n < max_rate_num - rate_num; n++)
					_RTW_PRINT_SEL(sel, "%5s ", "");

				_RTW_PRINT_SEL(sel, "|");

				/* dump power by rate in offset */
				for (n = rate_num - 1; n >= 0; n--) {
					by_rate = phy_get_txpwr_by_rate(adapter, band, path, rs, rates_by_sections[rs].rates[n]);
					base = phy_get_target_txpwr(adapter, band, path, rs);
					_RTW_PRINT_SEL(sel, "%3d ", by_rate - base);
				}
				RTW_PRINT_SEL(sel, "\n");

			}
		}
	}
}
#endif
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
		PADAPTER	Adapter,
		char		*pFileName
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
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
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
		PADAPTER	Adapter,
		char		*pFileName,
		u32			ConfigType
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
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
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

void
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

#ifndef DBG_TXPWR_BY_RATE_FILE_PARSE
#define DBG_TXPWR_BY_RATE_FILE_PARSE 0
#endif

int
phy_ParseBBPgParaFile(
	PADAPTER		Adapter,
	char			*buffer
)
{
	int	rtStatus = _FAIL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(Adapter);
	char	*szLine, *ptmp;
	u32	u4bRegOffset, u4bRegMask;
	u32	u4bMove;
	BOOLEAN firstLine = _TRUE;
	u8	tx_num = 0;
	u8	band = 0, rf_path = 0;

	if (Adapter->registrypriv.RegDecryptCustomFile == 1)
		phy_DecryptBBPgParaFile(Adapter, buffer);

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		if (isAllSpaceOrTab(szLine, sizeof(*szLine)))
			continue;

		if (!IsCommentString(szLine)) {
			/* Get header info (relative value or exact value) */
			if (firstLine) {
				if (strncmp(szLine, "#[v1]", 5) == 0
					|| strncmp(szLine, "#[v2]", 5) == 0)
					pHalData->odmpriv.phy_reg_pg_version = szLine[3] - '0';
				else {
					RTW_ERR("The format in PHY_REG_PG are invalid %s\n", szLine);
					goto exit;
				}

				if (strncmp(szLine + 5, "[Exact]#", 8) == 0) {
					pHalData->odmpriv.phy_reg_pg_value_type = PHY_REG_PG_EXACT_VALUE;
					firstLine = _FALSE;
					continue;
				} else {
					RTW_ERR("The values in PHY_REG_PG are invalid %s\n", szLine);
					goto exit;
				}
			}

			if (pHalData->odmpriv.phy_reg_pg_version > 0) {
				u32	index = 0;

				if (strncmp(szLine, "0xffff", 6) == 0)
					break;

				if (strncmp(szLine, "#[END]#", 7)) {
					/* load the table label info */
					if (szLine[0] == '#') {
						index = 0;
						if (strncmp(szLine, "#[2.4G]", 7) == 0) {
							band = BAND_ON_2_4G;
							index += 8;
						} else if (strncmp(szLine, "#[5G]", 5) == 0) {
							band = BAND_ON_5G;
							index += 6;
						} else {
							RTW_ERR("Invalid band %s in PHY_REG_PG.txt\n", szLine);
							goto exit;
						}

						rf_path = szLine[index] - 'A';
						if (DBG_TXPWR_BY_RATE_FILE_PARSE)
							RTW_INFO(" Table label Band %d, RfPath %d\n", band, rf_path );
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
							RTW_ERR("Invalid row in PHY_REG_PG.txt '%c'(%d)\n", szLine[1], szLine[1]);
							goto exit;
						}

						while (szLine[index] != ']')
							++index;
						++index;/* skip ] */

						/* Get 2nd hex value as register offset. */
						szLine += index;
						if (GetHexValueFromString(szLine, &u4bRegOffset, &u4bMove))
							szLine += u4bMove;
						else
							goto exit;

						/* Get 2nd hex value as register mask. */
						if (GetHexValueFromString(szLine, &u4bRegMask, &u4bMove))
							szLine += u4bMove;
						else
							goto exit;

						if (pHalData->odmpriv.phy_reg_pg_value_type == PHY_REG_PG_EXACT_VALUE) {
							u32	combineValue = 0;
							u8	integer = 0, fraction = 0;

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								goto exit;

							integer *= hal_spec->txgi_pdbm;
							integer += ((u16)fraction * (u16)hal_spec->txgi_pdbm) / 100;
							if (pHalData->odmpriv.phy_reg_pg_version == 1)
								combineValue |= (((integer / 10) << 4) + (integer % 10));
							else
								combineValue |= integer;

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								goto exit;

							integer *= hal_spec->txgi_pdbm;
							integer += ((u16)fraction * (u16)hal_spec->txgi_pdbm) / 100;
							combineValue <<= 8;
							if (pHalData->odmpriv.phy_reg_pg_version == 1)
								combineValue |= (((integer / 10) << 4) + (integer % 10));
							else
								combineValue |= integer;

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								goto exit;

							integer *= hal_spec->txgi_pdbm;
							integer += ((u16)fraction * (u16)hal_spec->txgi_pdbm) / 100;
							combineValue <<= 8;
							if (pHalData->odmpriv.phy_reg_pg_version == 1)
								combineValue |= (((integer / 10) << 4) + (integer % 10));
							else
								combineValue |= integer;

							if (GetFractionValueFromString(szLine, &integer, &fraction, &u4bMove))
								szLine += u4bMove;
							else
								goto exit;

							integer *= hal_spec->txgi_pdbm;
							integer += ((u16)fraction * (u16)hal_spec->txgi_pdbm) / 100;
							combineValue <<= 8;
							if (pHalData->odmpriv.phy_reg_pg_version == 1)
								combineValue |= (((integer / 10) << 4) + (integer % 10));
							else
								combineValue |= integer;

							phy_store_tx_power_by_rate(Adapter, band, rf_path, tx_num, u4bRegOffset, u4bRegMask, combineValue);

							if (DBG_TXPWR_BY_RATE_FILE_PARSE)
								RTW_INFO("addr:0x%3x mask:0x%08x %dTx = 0x%08x\n", u4bRegOffset, u4bRegMask, tx_num + 1, combineValue);
						}
					}
				}
			}
		}
	}

	rtStatus = _SUCCESS;

exit:
	RTW_INFO("%s return %d\n", __func__, rtStatus);
	return rtStatus;
}

int
phy_ConfigBBWithPgParaFile(
		PADAPTER	Adapter,
		const char	*pFileName)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_BB_PG_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if (pHalData->bb_phy_reg_pg == NULL) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
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
		rtStatus = phy_ParseBBPgParaFile(Adapter, pHalData->para_file_buf);
	} else
		RTW_INFO("%s(): No File %s, Load from HWImg Array!\n", __FUNCTION__, pFileName);

	return rtStatus;
}

#if (MP_DRIVER == 1)

int
phy_ConfigBBWithMpParaFile(
		PADAPTER	Adapter,
		char		*pFileName
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
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
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
		PADAPTER	Adapter,
		char		*pFileName,
		enum rf_path		eRFPath
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
	case RF_PATH_A:
		pBuf = pHalData->rf_radio_a;
		pBufLen = &pHalData->rf_radio_a_len;
		break;
	case RF_PATH_B:
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
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
			rlen = rtw_retrieve_from_file(rtw_phy_para_file_path, pHalData->para_file_buf, MAX_PARA_FILE_BUF_LEN);
			if (rlen > 0) {
				rtStatus = _SUCCESS;
				pBuf = rtw_zvmalloc(rlen);
				if (pBuf) {
					_rtw_memcpy(pBuf, pHalData->para_file_buf, rlen);
					*pBufLen = rlen;

					switch (eRFPath) {
					case RF_PATH_A:
						pHalData->rf_radio_a = pBuf;
						break;
					case RF_PATH_B:
						pHalData->rf_radio_b = pBuf;
						break;
					default:
						RTW_INFO("Unknown RF path!! %d\r\n", eRFPath);
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

void
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
	struct dm_struct		*pDM_Odm = &pHalData->odmpriv;
	struct dm_rf_calibration_struct	*pRFCalibrateInfo = &(pDM_Odm->rf_calibrate_info);
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
		PADAPTER		Adapter,
		char			*pFileName
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct dm_struct			*pDM_Odm = &pHalData->odmpriv;
	int	rlen = 0, rtStatus = _FAIL;
	char	*szLine, *ptmp;
	u32	i = 0;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_TXPWR_TRACK_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if ((pHalData->rf_tx_pwr_track_len == 0) && (pHalData->rf_tx_pwr_track == NULL)) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
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

#if CONFIG_TXPWR_LIMIT

#ifndef DBG_TXPWR_LMT_FILE_PARSE
#define DBG_TXPWR_LMT_FILE_PARSE 0
#endif

#define PARSE_RET_NO_HDL	0
#define PARSE_RET_SUCCESS	1
#define PARSE_RET_FAIL		2

/*
* @@Ver=2.0
* or
* @@DomainCode=0x28, Regulation=C6
* or
* @@CountryCode=GB, Regulation=C7
*/
static u8 parse_reg_exc_config(_adapter *adapter, char *szLine)
{
#define VER_PREFIX "Ver="
#define DOMAIN_PREFIX "DomainCode=0x"
#define COUNTRY_PREFIX "CountryCode="
#define REG_PREFIX "Regulation="

	const u8 ver_prefix_len = strlen(VER_PREFIX);
	const u8 domain_prefix_len = strlen(DOMAIN_PREFIX);
	const u8 country_prefix_len = strlen(COUNTRY_PREFIX);
	const u8 reg_prefix_len = strlen(REG_PREFIX);
	u32 i, i_val_s, i_val_e;
	u32 j;
	u8 domain = 0xFF;
	char *country = NULL;
	u8 parse_reg = 0;

	if (szLine[0] != '@' || szLine[1] != '@')
		return PARSE_RET_NO_HDL;

	i = 2;
	if (strncmp(szLine + i, VER_PREFIX, ver_prefix_len) == 0)
		; /* nothing to do */
	else if (strncmp(szLine + i, DOMAIN_PREFIX, domain_prefix_len) == 0) {
		/* get string after domain prefix to ',' */
		i += domain_prefix_len;
		i_val_s = i;
		while (szLine[i] != ',') {
			if (szLine[i] == '\0')
				return PARSE_RET_FAIL;
			i++;
		}
		i_val_e = i;

		/* check if all hex */
		for (j = i_val_s; j < i_val_e; j++)
			if (IsHexDigit(szLine[j]) == _FALSE)
				return PARSE_RET_FAIL;

		/* get value from hex string */
		if (sscanf(szLine + i_val_s, "%hhx", &domain) != 1)
			return PARSE_RET_FAIL;

		parse_reg = 1;
	} else if (strncmp(szLine + i, COUNTRY_PREFIX, country_prefix_len) == 0) {
		/* get string after country prefix to ',' */
		i += country_prefix_len;
		i_val_s = i;
		while (szLine[i] != ',') {
			if (szLine[i] == '\0')
				return PARSE_RET_FAIL;
			i++;
		}
		i_val_e = i;

		if (i_val_e - i_val_s != 2)
			return PARSE_RET_FAIL;

		/* check if all alpha */
		for (j = i_val_s; j < i_val_e; j++)
			if (is_alpha(szLine[j]) == _FALSE)
				return PARSE_RET_FAIL;

		country = szLine + i_val_s;

		parse_reg = 1;

	} else
		return PARSE_RET_FAIL;

	if (parse_reg) {
		/* move to 'R' */
		while (szLine[i] != 'R') {
			if (szLine[i] == '\0')
				return PARSE_RET_FAIL;
			i++;
		}

		/* check if matching regulation prefix */
		if (strncmp(szLine + i, REG_PREFIX, reg_prefix_len) != 0)
			return PARSE_RET_FAIL;

		/* get string after regulation prefix ending with space */
		i += reg_prefix_len;
		i_val_s = i;
		while (szLine[i] != ' ' && szLine[i] != '\t' && szLine[i] != '\0')
			i++;

		if (i == i_val_s)
			return PARSE_RET_FAIL;

		rtw_regd_exc_add_with_nlen(adapter_to_rfctl(adapter), country, domain, szLine + i_val_s, i - i_val_s);
	}

	return PARSE_RET_SUCCESS;
}

static int
phy_ParsePowerLimitTableFile(
	PADAPTER		Adapter,
	char			*buffer
)
{
#define LD_STAGE_EXC_MAPPING	0
#define LD_STAGE_TAB_DEFINE		1
#define LD_STAGE_TAB_START		2
#define LD_STAGE_COLUMN_DEFINE	3
#define LD_STAGE_CH_ROW			4

	int	rtStatus = _FAIL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(Adapter);
	struct dm_struct	*pDM_Odm = &(pHalData->odmpriv);
	u8	loadingStage = LD_STAGE_EXC_MAPPING;
	u32	i = 0, forCnt = 0;
	char	*szLine, *ptmp;
	char band[10], bandwidth[10], rateSection[10], ntx[10], colNumBuf[10];
	char **regulation = NULL;
	u8	colNum = 0;

	if (Adapter->registrypriv.RegDecryptCustomFile == 1)
		phy_DecryptBBPgParaFile(Adapter, buffer);

	ptmp = buffer;
	for (szLine = GetLineFromBuffer(ptmp); szLine != NULL; szLine = GetLineFromBuffer(ptmp)) {
		if (isAllSpaceOrTab(szLine, sizeof(*szLine)))
			continue;
		if (IsCommentString(szLine))
			continue;

		if (loadingStage == LD_STAGE_EXC_MAPPING) {
			if (szLine[0] == '#' || szLine[1] == '#') {
				loadingStage = LD_STAGE_TAB_DEFINE;
				if (DBG_TXPWR_LMT_FILE_PARSE)
					dump_regd_exc_list(RTW_DBGDUMP, adapter_to_rfctl(Adapter));
			} else {
				if (parse_reg_exc_config(Adapter, szLine) == PARSE_RET_FAIL) {
					RTW_ERR("Fail to parse regulation exception ruls!\n");
					goto exit;
				}
				continue;
			}
		}

		if (loadingStage == LD_STAGE_TAB_DEFINE) {
			/* read "##	2.4G, 20M, 1T, CCK" */
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			szLine[--i] = ' '; /* return the space in front of the regulation info */

			/* Parse the label of the table */
			_rtw_memset((void *) band, 0, 10);
			_rtw_memset((void *) bandwidth, 0, 10);
			_rtw_memset((void *) ntx, 0, 10);
			_rtw_memset((void *) rateSection, 0, 10);
			if (!ParseQualifiedString(szLine, &i, band, ' ', ',')) {
				RTW_ERR("Fail to parse band!\n");
				goto exit;
			}
			if (!ParseQualifiedString(szLine, &i, bandwidth, ' ', ',')) {
				RTW_ERR("Fail to parse bandwidth!\n");
				goto exit;
			}
			if (!ParseQualifiedString(szLine, &i, ntx, ' ', ',')) {
				RTW_ERR("Fail to parse ntx!\n");
				goto exit;
			}
			if (!ParseQualifiedString(szLine, &i, rateSection, ' ', ',')) {
				RTW_ERR("Fail to parse rate!\n");
				goto exit;
			}

			loadingStage = LD_STAGE_TAB_START;
		} else if (loadingStage == LD_STAGE_TAB_START) {
			/* read "##	START" */
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			if (strncmp((u8 *)(szLine + i), "START", 5)) {
				RTW_ERR("Missing \"##   START\" label\n");
				goto exit;
			}

			loadingStage = LD_STAGE_COLUMN_DEFINE;
		} else if (loadingStage == LD_STAGE_COLUMN_DEFINE) {
			/* read "##	#5#	FCC	ETSI	MKK	IC	KCC" */
			if (szLine[0] != '#' || szLine[1] != '#')
				continue;

			/* skip the space */
			i = 2;
			while (szLine[i] == ' ' || szLine[i] == '\t')
				++i;

			_rtw_memset((void *) colNumBuf, 0, 10);
			if (!ParseQualifiedString(szLine, &i, colNumBuf, '#', '#')) {
				RTW_ERR("Fail to parse column number!\n");
				goto exit;
			}
			if (!GetU1ByteIntegerFromStringInDecimal(colNumBuf, &colNum)) {
				RTW_ERR("Column number \"%s\" is not unsigned decimal\n", colNumBuf);
				goto exit;
			}
			if (colNum == 0) {
				RTW_ERR("Column number is 0\n");
				goto exit;
			}

			if (DBG_TXPWR_LMT_FILE_PARSE)
				RTW_PRINT("[%s][%s][%s][%s] column num:%d\n", band, bandwidth, rateSection, ntx, colNum);

			regulation = (char **)rtw_zmalloc(sizeof(char *) * colNum);
			if (!regulation) {
				RTW_ERR("Regulation alloc fail\n");
				goto exit;
			}

			for (forCnt = 0; forCnt < colNum; ++forCnt) {
				u32 i_ns;

				/* skip the space */
				while (szLine[i] == ' ' || szLine[i] == '\t')
					i++;
				i_ns = i;

				while (szLine[i] != ' ' && szLine[i] != '\t' && szLine[i] != '\0')
					i++;

				regulation[forCnt] = (char *)rtw_malloc(i - i_ns + 1);
				if (!regulation[forCnt]) {
					RTW_ERR("Regulation alloc fail\n");
					goto exit;
				}

				_rtw_memcpy(regulation[forCnt], szLine + i_ns, i - i_ns);
				regulation[forCnt][i - i_ns] = '\0';
			}

			if (DBG_TXPWR_LMT_FILE_PARSE) {
				RTW_PRINT("column name:");
				for (forCnt = 0; forCnt < colNum; ++forCnt)
					_RTW_PRINT(" %s", regulation[forCnt]);
				_RTW_PRINT("\n");
			}

			loadingStage = LD_STAGE_CH_ROW;
		} else if (loadingStage == LD_STAGE_CH_ROW) {
			char	channel[10] = {0}, powerLimit[10] = {0};
			u8	cnt = 0;

			/* the table ends */
			if (szLine[0] == '#' && szLine[1] == '#') {
				i = 2;
				while (szLine[i] == ' ' || szLine[i] == '\t')
					++i;

				if (strncmp((u8 *)(szLine + i), "END", 3) == 0) {
					loadingStage = LD_STAGE_TAB_DEFINE;
					if (regulation) {
						for (forCnt = 0; forCnt < colNum; ++forCnt) {
							if (regulation[forCnt]) {
								rtw_mfree(regulation[forCnt], strlen(regulation[forCnt]) + 1);
								regulation[forCnt] = NULL;
							}
						}
						rtw_mfree((u8 *)regulation, sizeof(char *) * colNum);
						regulation = NULL;
					}
					colNum = 0;
					continue;
				} else {
					RTW_ERR("Missing \"##   END\" label\n");
					goto exit;
				}
			}

			if ((szLine[0] != 'c' && szLine[0] != 'C') ||
				(szLine[1] != 'h' && szLine[1] != 'H')
			) {
				RTW_WARN("Wrong channel prefix: '%c','%c'(%d,%d)\n", szLine[0], szLine[1], szLine[0], szLine[1]);
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
				_rtw_memset((void *) powerLimit, 0, 10);

				if (szLine[i] == 'W' && szLine[i + 1] == 'W') {
					/*
					* case "WW" assign special ww value
					* means to get minimal limit in other regulations at same channel
					*/
					s8 ww_value = phy_txpwr_ww_lmt_value(Adapter);

					sprintf(powerLimit, "%d", ww_value);
					i += 2;

				} else if (szLine[i] == 'N' && szLine[i + 1] == 'A') {
					/*
					* case "NA" assign max txgi value
					* means no limitation
					*/
					sprintf(powerLimit, "%d", hal_spec->txgi_max);
					i += 2;

				} else if ((szLine[i] >= '0' && szLine[i] <= '9') || szLine[i] == '.'
					|| szLine[i] == '+' || szLine[i] == '-'
				){
					/* case of dBm value */
					u8 integer = 0, fraction = 0, negative = 0;
					u32 u4bMove;
					s8 lmt = 0;

					if (szLine[i] == '+' || szLine[i] == '-') {
						if (szLine[i] == '-')
							negative = 1;
						i++;
					}

					if (GetFractionValueFromString(&szLine[i], &integer, &fraction, &u4bMove))
						i += u4bMove;
					else {
						RTW_ERR("Limit \"%s\" is not valid decimal\n", &szLine[i]);
						goto exit;
					}

					/* transform to string of value in unit of txgi */
					lmt = integer * hal_spec->txgi_pdbm + ((u16)fraction * (u16)hal_spec->txgi_pdbm) / 100;
					if (negative)
						lmt = -lmt;
					sprintf(powerLimit, "%d", lmt);

				} else {
					RTW_ERR("Wrong limit expression \"%c%c\"(%d, %d)\n"
						, szLine[i], szLine[i + 1], szLine[i], szLine[i + 1]);
					goto exit;
				}

				/* store the power limit value */
				phy_set_tx_power_limit(pDM_Odm, (u8 *)regulation[forCnt], (u8 *)band,
					(u8 *)bandwidth, (u8 *)rateSection, (u8 *)ntx, (u8 *)channel, (u8 *)powerLimit);

			}
		}
	}

	rtStatus = _SUCCESS;

exit:
	if (regulation) {
		for (forCnt = 0; forCnt < colNum; ++forCnt) {
			if (regulation[forCnt]) {
				rtw_mfree(regulation[forCnt], strlen(regulation[forCnt]) + 1);
				regulation[forCnt] = NULL;
			}
		}
		rtw_mfree((u8 *)regulation, sizeof(char *) * colNum);
		regulation = NULL;
	}

	RTW_INFO("%s return %d\n", __func__, rtStatus);
	return rtStatus;
}

int
PHY_ConfigRFWithPowerLimitTableParaFile(
		PADAPTER	Adapter,
		const char	*pFileName
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	int	rlen = 0, rtStatus = _FAIL;

	if (!(Adapter->registrypriv.load_phy_file & LOAD_RF_TXPWR_LMT_PARA_FILE))
		return rtStatus;

	_rtw_memset(pHalData->para_file_buf, 0, MAX_PARA_FILE_BUF_LEN);

	if (pHalData->rf_tx_pwr_lmt == NULL) {
		rtw_get_phy_file_path(Adapter, pFileName);
		if (rtw_readable_file_sz_chk(rtw_phy_para_file_path, 
			MAX_PARA_FILE_BUF_LEN) == _TRUE) {
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
#endif /* CONFIG_TXPWR_LIMIT */

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

/*
* TX power limit of regulatory without HAL consideration
* Return value in unit of TX Gain Index
* hal_spec.txgi_max means unspecified
*/
s8 phy_get_txpwr_regd_lmt(_adapter *adapter, struct hal_spec_t *hal_spec, u8 cch, enum channel_width bw, u8 ntx_idx)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	s16 total_mbm = UNSPECIFIED_MBM;
	s8 lmt;

	if ((adapter->registrypriv.RegEnableTxPowerLimit == 2 && hal_data->EEPROMRegulatory != 1) ||
		adapter->registrypriv.RegEnableTxPowerLimit == 0)
		goto exit;

#ifdef CONFIG_REGD_SRC_FROM_OS
	if (rfctl->regd_src == REGD_SRC_OS)
		total_mbm = rtw_os_get_total_txpwr_regd_lmt_mbm(adapter, cch, bw);
#endif

exit:
	if (total_mbm != UNSPECIFIED_MBM)
		lmt = (total_mbm - mb_of_ntx(ntx_idx + 1) - rfctl->antenna_gain) * hal_spec->txgi_pdbm / MBM_PDBM;
	else
		lmt = hal_spec->txgi_max;

	return lmt;
}

/*
* check if user specified mbm is valid
*/
bool phy_is_txpwr_user_mbm_valid(_adapter *adapter, s16 mbm)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	/* 1T upper bound check */
	if (hal_spec->txgi_max <= mbm * hal_spec->txgi_pdbm / MBM_PDBM)
		return 0;

	return 1;
}

bool phy_is_txpwr_user_target_specified(_adapter *adapter)
{
	s16 total_mbm = UNSPECIFIED_MBM;

#ifdef CONFIG_IOCTL_CFG80211
	total_mbm = rtw_cfg80211_dev_get_total_txpwr_target_mbm(adapter_to_dvobj(adapter));
#endif

	return total_mbm != UNSPECIFIED_MBM;
}

/*
* Return value in unit of TX Gain Index
* hal_spec.txgi_max means unspecified
*/
s8 phy_get_txpwr_user_target(_adapter *adapter, struct hal_spec_t *hal_spec, u8 ntx_idx)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	s16 total_mbm = UNSPECIFIED_MBM;
	s8 target;

#ifdef CONFIG_IOCTL_CFG80211
	total_mbm = rtw_cfg80211_dev_get_total_txpwr_target_mbm(adapter_to_dvobj(adapter));
#endif
	if (total_mbm != UNSPECIFIED_MBM)
		target = (total_mbm - mb_of_ntx(ntx_idx + 1) - rfctl->antenna_gain) * hal_spec->txgi_pdbm / MBM_PDBM;
	else
		target = hal_spec->txgi_max;

	return target;
}

/*
* Return value in unit of TX Gain Index
* hal_spec.txgi_max means unspecified
*/
s8 phy_get_txpwr_user_lmt(_adapter *adapter, struct hal_spec_t *hal_spec, u8 ntx_idx)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	s16 total_mbm = UNSPECIFIED_MBM;
	s8 lmt;

#ifdef CONFIG_IOCTL_CFG80211
	total_mbm = rtw_cfg80211_dev_get_total_txpwr_lmt_mbm(adapter_to_dvobj(adapter));
#endif
	if (total_mbm != UNSPECIFIED_MBM)
		lmt = (total_mbm - mb_of_ntx(ntx_idx + 1) - rfctl->antenna_gain) * hal_spec->txgi_pdbm / MBM_PDBM;
	else
		lmt = hal_spec->txgi_max;

	return lmt;
}

/*
* Return value in unit of TX Gain Index
* 0 means unspecified
*/
s8 phy_get_txpwr_tpc(_adapter *adapter, struct hal_spec_t *hal_spec)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	u16 cnst = 0;

	if (rfctl->tpc_mode == TPC_MODE_MANUAL)
		cnst = rfctl->tpc_manual_constraint * hal_spec->txgi_pdbm / MBM_PDBM;

	return -cnst;
}

void dump_txpwr_tpc_settings(void *sel, _adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	if (rfctl->tpc_mode == TPC_MODE_DISABLE)
		RTW_PRINT_SEL(sel, "mode:DISABLE(%d)\n", rfctl->tpc_mode);
	else if (rfctl->tpc_mode == TPC_MODE_MANUAL) {
		RTW_PRINT_SEL(sel, "mode:MANUAL(%d)\n", rfctl->tpc_mode);
		RTW_PRINT_SEL(sel, "constraint:%d (mB)\n", rfctl->tpc_manual_constraint);
	}
}

void dump_txpwr_antenna_gain(void *sel, _adapter *adapter)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	RTW_PRINT_SEL(sel, "%d (mBi)\n", rfctl->antenna_gain);
}

/*
* Return value in unit of TX Gain Index
*/
s8 phy_get_txpwr_target(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate, u8 ntx_idx
	, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch, bool reg_max, struct txpwr_idx_comp *tic)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	s8 target, by_rate = 0, btc_diff = 0, extra = 0;
	s8 lmt, rlmt, utgt, ulmt;
	s8 tpc = 0;

	rlmt = lmt = utgt = ulmt = hal_spec->txgi_max;

	if (band != BAND_ON_2_4G && IS_CCK_RATE(rate))
		goto exit;

	if (!reg_max) {
		utgt = phy_get_txpwr_user_target(adapter, hal_spec, ntx_idx);
		if (utgt != hal_spec->txgi_max)
			goto get_lmt;
	}

#ifdef CONFIG_RTL8812A
	if (IS_HARDWARE_TYPE_8812(adapter)
		&& phy_get_txpwr_target_skip_by_rate_8812a(adapter, rate))
		by_rate = phy_get_target_txpwr(adapter, band, rfpath, rs);
	else
#endif
		by_rate = phy_get_txpwr_by_rate(adapter, band, rfpath, rs, rate);
	if (by_rate == hal_spec->txgi_max)
		by_rate = 0;

#ifdef CONFIG_BT_COEXIST
	if (!reg_max) {
		if (hal_data->EEPROMBluetoothCoexist == _TRUE)
			btc_diff = -(rtw_btcoex_query_reduced_wl_pwr_lvl(adapter) * hal_spec->txgi_pdbm);
	}
#endif

	extra = rtw_hal_get_txpwr_target_extra_bias(adapter, rfpath, rs, rate, bw, band, cch);

get_lmt:
	rlmt = phy_get_txpwr_regd_lmt(adapter, hal_spec, cch, bw, ntx_idx);
	lmt = phy_get_txpwr_lmt_sub_chs(adapter, NULL, band, bw, rfpath, rate, ntx_idx, cch, opch, reg_max);
	if (!reg_max)
		ulmt = phy_get_txpwr_user_lmt(adapter, hal_spec, ntx_idx);
	/* TODO: limit from outer source, ex: 11d */

	if (!reg_max)
		tpc = phy_get_txpwr_tpc(adapter, hal_spec);

exit:
	if (utgt != hal_spec->txgi_max)
		target = utgt;
	else
		target = by_rate + btc_diff + extra;

	if (target > rlmt)
		target = rlmt;
	if (target > lmt)
		target = lmt;
	if (target > ulmt)
		target = ulmt;

	target += tpc;

	if (tic) {
		tic->target = target;
		if (utgt == hal_spec->txgi_max) {
			tic->by_rate = by_rate;
			tic->btc = btc_diff;
			tic->extra = extra;
		}
		tic->utarget = utgt;
		tic->rlimit = rlmt;
		tic->limit = lmt;
		tic->ulimit = ulmt;
		tic->tpc = tpc;
	}

	return target;
}

/* TODO: common dpd_diff getting API from phydm */
#ifdef CONFIG_RTL8822C
#include "./rtl8822c/rtl8822c.h"
#endif

/*
* Return in unit of TX Gain Index
*/
s8 phy_get_txpwr_amends(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate, u8 ntx_idx
	, enum channel_width bw, BAND_TYPE band, u8 cch, struct txpwr_idx_comp *tic)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	s8 tpt_diff = 0, dpd_diff = 0, val = 0;

	if (band != BAND_ON_2_4G && IS_CCK_RATE(rate))
		goto exit;

	if (IS_HARDWARE_TYPE_8188E(adapter) || IS_HARDWARE_TYPE_8188F(adapter) || IS_HARDWARE_TYPE_8188GTV(adapter)
		|| IS_HARDWARE_TYPE_8192E(adapter) || IS_HARDWARE_TYPE_8192F(adapter)
		|| IS_HARDWARE_TYPE_8723B(adapter) || IS_HARDWARE_TYPE_8703B(adapter) || IS_HARDWARE_TYPE_8723D(adapter)
		|| IS_HARDWARE_TYPE_8710B(adapter)
		|| IS_HARDWARE_TYPE_8821(adapter) || IS_HARDWARE_TYPE_8812(adapter)
	)
		tpt_diff = PHY_GetTxPowerTrackingOffset(adapter, rfpath, rate);

#ifdef CONFIG_RTL8822C
	if (IS_HARDWARE_TYPE_8822C(adapter))
		dpd_diff = -(rtl8822c_get_dis_dpd_by_rate_diff(adapter, rate) * hal_spec->txgi_pdbm);
#endif

exit:
	if (tic) {
		tic->tpt = tpt_diff;
		tic->dpd = dpd_diff;
	}

	return tpt_diff + dpd_diff;
}

#ifdef CONFIG_TXPWR_PG_WITH_TSSI_OFFSET
s8 phy_get_tssi_txpwr_by_rate_ref(_adapter *adapter, enum rf_path path
	, enum channel_width bw, u8 cch, u8 opch)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 ntx_idx = phy_get_current_tx_num(adapter, MGN_MCS7);
	BAND_TYPE band = cch > 14 ? BAND_ON_5G : BAND_ON_2_4G;
	s8 pwr_idx;

	pwr_idx = phy_get_txpwr_target(adapter, path, HT_1SS, MGN_MCS7
		, ntx_idx, bw, band, cch, opch, 0, NULL);
	pwr_idx += phy_get_txpwr_amends(adapter, path, HT_1SS, MGN_MCS7
		, ntx_idx, bw, band, cch, NULL);

	return pwr_idx;
}
#endif

/*
 * Rteurn tx power index for rate
 */
u8 hal_com_get_txpwr_idx(_adapter *adapter, enum rf_path rfpath
	, RATE_SECTION rs, enum MGN_RATE rate, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch
	, struct txpwr_idx_comp *tic)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	s16 power_idx = 0;
	s8 base = 0;
	s8 rate_target, rate_amends;
	u8 ntx_idx = phy_get_current_tx_num(adapter, rate);

	/* target */
	rate_target = phy_get_txpwr_target(adapter, rfpath, rs, rate, ntx_idx, bw, band, cch, opch, 0, tic);

	/* amends */
	rate_amends = phy_get_txpwr_amends(adapter, rfpath, rs, rate, ntx_idx, bw, band, cch, tic);

	switch (hal->txpwr_pg_mode) {
#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
	case TXPWR_PG_WITH_PWR_IDX: {
		/*
		* power index = 
		* 1. pg base (per rate section) +
		* 2. target diff (per rate) to target of its rate section +
		* 3. amends diff (per rate)
		*/
		u8 rs_target;

		base = phy_get_pg_txpwr_idx(adapter, rfpath, rs, ntx_idx, bw, band, cch);
		rs_target = phy_get_target_txpwr(adapter, band, rfpath, rs);
		power_idx = base + (rate_target - rs_target) + (rate_amends);

		if (tic) {
			if (tic->utarget == hal_spec->txgi_max)
				tic->by_rate -= rs_target;
			else
				tic->utarget -= rs_target;
			if (tic->rlimit != hal_spec->txgi_max)
				tic->rlimit -= rs_target;
			if (tic->limit != hal_spec->txgi_max)
				tic->limit -= rs_target;
			if (tic->ulimit != hal_spec->txgi_max)
				tic->ulimit -= rs_target;
		}
	}
		break;
#endif
#ifdef CONFIG_TXPWR_PG_WITH_TSSI_OFFSET
	case TXPWR_PG_WITH_TSSI_OFFSET: {
		/*
		* power index = 
		* 1. base (fixed) +
		* 2. target (per rate) +
		* 3. amends diff (per rate)
		* base is selected that power index of MCS7 ==  halrf_get_tssi_codeword_for_txindex()
		*/
		s8 mcs7_idx;

		mcs7_idx = phy_get_tssi_txpwr_by_rate_ref(adapter, rfpath, bw, cch, opch);
		base = halrf_get_tssi_codeword_for_txindex(adapter_to_phydm(adapter)) - mcs7_idx;
		power_idx = base + rate_target + rate_amends;
	}
		break;
#endif
	}

	if (tic) {
		tic->ntx_idx = ntx_idx;
		tic->base = base;
	}

	if (power_idx < 0)
		power_idx = 0;
	else if (power_idx > hal_spec->txgi_max)
		power_idx = hal_spec->txgi_max;

#if defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8812A)
	if ((IS_HARDWARE_TYPE_8821(adapter) || IS_HARDWARE_TYPE_8812(adapter))
		&& power_idx % 2 == 1 && !IS_NORMAL_CHIP(hal->version_id))
		--power_idx;
#endif

	return power_idx;
}

static s16 phy_get_txpwr_mbm(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate
	, enum channel_width bw, u8 cch, u8 opch, bool total, bool reg_max, bool eirp, struct txpwr_idx_comp *tic)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	BAND_TYPE band = cch <= 14 ? BAND_ON_2_4G : BAND_ON_5G;
	u8 ntx_idx_max, ntx_idx, i;
	s16 val, max = UNSPECIFIED_MBM;

	if (reg_max) {
		ntx_idx_max = phy_get_capable_tx_num(adapter, rate);
		ntx_idx = rate_section_to_tx_num(rs);
		if (ntx_idx > ntx_idx_max) {
			rtw_warn_on(1);
			return 0;
		}
	} else
		ntx_idx_max = ntx_idx = phy_get_current_tx_num(adapter, rate);

	for (i = 0; ntx_idx + i <= ntx_idx_max; i++) {
		val = phy_get_txpwr_target(adapter, rfpath, rs, rate, ntx_idx, bw, band, cch, opch, reg_max, tic);
		val = (val * MBM_PDBM) / hal_spec->txgi_pdbm;
		if (total)
			val += mb_of_ntx(ntx_idx + 1);
		if (eirp)
			val += rfctl->antenna_gain;

		if (max == UNSPECIFIED_MBM || max < val)
			max = val;
	}

	if (tic)
		tic->ntx_idx = ntx_idx;

	if (max == UNSPECIFIED_MBM) {
		rtw_warn_on(1);
		max = 0;
	}
	return max;
}

/* get txpowr in mBm for single path */
s16 phy_get_txpwr_single_mbm(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate
	, enum channel_width bw, u8 cch, u8 opch, bool reg_max, bool eirp, struct txpwr_idx_comp *tic)
{
	return phy_get_txpwr_mbm(adapter, rfpath, rs, rate, bw, cch, opch, 0, reg_max, eirp, tic);
}

/* get txpowr in mBm with effect of N-TX */
s16 phy_get_txpwr_total_mbm(_adapter *adapter, RATE_SECTION rs, u8 rate
	, enum channel_width bw, u8 cch, u8 opch, bool reg_max, bool eirp, struct txpwr_idx_comp *tic)
{
	/* assume all path have same txpower target */
	return phy_get_txpwr_mbm(adapter, RF_PATH_A, rs, rate, bw, cch, opch, 1, reg_max, eirp, tic);
}

static s16 _phy_get_txpwr_max_mbm(_adapter *adapter, s8 rfpath
	, enum channel_width bw, u8 cch, u8 opch, u16 bmp_cck_ofdm, u32 bmp_ht, u64 bmp_vht, bool reg_max, bool eirp)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	BAND_TYPE band = cch <= 14 ? BAND_ON_2_4G : BAND_ON_5G;
	u8 tx_num;
	RATE_SECTION rs;
	u8 hw_rate;
	int i;
	s16 max = UNSPECIFIED_MBM, mbm;

	if (0)
		RTW_INFO("cck_ofdm:0x%04x, ht:0x%08x, vht:0x%016llx\n", bmp_cck_ofdm, bmp_ht, bmp_vht);

	for (rs = 0; rs < RATE_SECTION_NUM; rs++) {
		tx_num = rate_section_to_tx_num(rs);
		if (tx_num + 1 > hal_data->tx_nss)
			continue;
		
		if (band == BAND_ON_5G && IS_CCK_RATE_SECTION(rs))
			continue;
		
		if (IS_VHT_RATE_SECTION(rs) && !IS_HARDWARE_TYPE_JAGUAR_ALL(adapter))
			continue;
		
		for (i = 0; i < rates_by_sections[rs].rate_num; i++) {
			hw_rate = MRateToHwRate(rates_by_sections[rs].rates[i]);
			if (IS_LEGACY_HRATE(hw_rate)) {
				if (!(bmp_cck_ofdm & BIT(hw_rate)))
					continue;
			} else if (IS_HT_HRATE(hw_rate)) {
				if (!(bmp_ht & BIT(hw_rate - DESC_RATEMCS0)))
					continue;
			} else if (IS_VHT_HRATE(hw_rate)) {
				if (!(bmp_vht & BIT(hw_rate - DESC_RATEVHTSS1MCS0)))
					continue;
			}

			if (rfpath < 0) /* total */
				mbm = phy_get_txpwr_total_mbm(adapter, rs, rates_by_sections[rs].rates[i], bw, cch, opch, reg_max, eirp, NULL);
			else
				mbm = phy_get_txpwr_single_mbm(adapter, rfpath, rs, rates_by_sections[rs].rates[i], bw, cch, opch, reg_max, eirp, NULL);

			if (max == UNSPECIFIED_MBM || mbm > max)
				max = mbm;
		}
	}

	return max;
}

s16 phy_get_txpwr_single_max_mbm(_adapter *adapter, u8 rfpath
	, enum channel_width bw, u8 cch, u8 opch, u16 bmp_cck_ofdm, u32 bmp_ht, u64 bmp_vht, bool reg_max, bool eirp)
{
	return _phy_get_txpwr_max_mbm(adapter, rfpath, bw, cch, opch, bmp_cck_ofdm, bmp_ht, bmp_vht, reg_max, eirp);
}

s16 phy_get_txpwr_total_max_mbm(_adapter *adapter
	, enum channel_width bw, u8 cch, u8 opch, u16 bmp_cck_ofdm, u32 bmp_ht, u64 bmp_vht, bool reg_max, bool eirp)
{
	return _phy_get_txpwr_max_mbm(adapter, -1, bw, cch, opch, bmp_cck_ofdm, bmp_ht, bmp_vht, reg_max, eirp);
}

s8
phy_get_tx_power_final_absolute_value(_adapter *adapter, u8 rfpath, u8 rate,
				      enum channel_width bw, u8 cch)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	RATE_SECTION rs = mgn_rate_to_rs(rate);
	BAND_TYPE band = cch <= 14 ? BAND_ON_2_4G : BAND_ON_5G;
	s8 val;

	val = phy_get_txpwr_target(adapter, rfpath
		, rs, rate, phy_get_current_tx_num(adapter, rate), bw, band, cch, 0, 0, NULL);

	val /= hal_spec->txgi_pdbm;

	return val;
}
