/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_MAC_H
#define __MT7915_MAC_H

#include "../mt76_connac2_mac.h"

#define MT_TX_FREE_VER			GENMASK(18, 16)
#define MT_TX_FREE_MSDU_CNT_V0		GENMASK(6, 0)
/* 0: success, others: dropped */
#define MT_TX_FREE_MPDU_HEADER		BIT(30)
#define MT_TX_FREE_MSDU_ID_V3		GENMASK(14, 0)

#define MT_TXS5_F0_FINAL_MPDU		BIT(31)
#define MT_TXS5_F0_QOS			BIT(30)
#define MT_TXS5_F0_TX_COUNT		GENMASK(29, 25)
#define MT_TXS5_F0_FRONT_TIME		GENMASK(24, 0)
#define MT_TXS5_F1_MPDU_TX_COUNT	GENMASK(31, 24)
#define MT_TXS5_F1_MPDU_TX_BYTES	GENMASK(23, 0)

#define MT_TXS6_F0_NOISE_3		GENMASK(31, 24)
#define MT_TXS6_F0_NOISE_2		GENMASK(23, 16)
#define MT_TXS6_F0_NOISE_1		GENMASK(15, 8)
#define MT_TXS6_F0_NOISE_0		GENMASK(7, 0)
#define MT_TXS6_F1_MPDU_FAIL_COUNT	GENMASK(31, 24)
#define MT_TXS6_F1_MPDU_FAIL_BYTES	GENMASK(23, 0)

#define MT_TXS7_F0_RCPI_3		GENMASK(31, 24)
#define MT_TXS7_F0_RCPI_2		GENMASK(23, 16)
#define MT_TXS7_F0_RCPI_1		GENMASK(15, 8)
#define MT_TXS7_F0_RCPI_0		GENMASK(7, 0)
#define MT_TXS7_F1_MPDU_RETRY_COUNT	GENMASK(31, 24)
#define MT_TXS7_F1_MPDU_RETRY_BYTES	GENMASK(23, 0)

struct mt7915_dfs_pulse {
	u32 max_width;		/* us */
	int max_pwr;		/* dbm */
	int min_pwr;		/* dbm */
	u32 min_stgr_pri;	/* us */
	u32 max_stgr_pri;	/* us */
	u32 min_cr_pri;		/* us */
	u32 max_cr_pri;		/* us */
};

struct mt7915_dfs_pattern {
	u8 enb;
	u8 stgr;
	u8 min_crpn;
	u8 max_crpn;
	u8 min_crpr;
	u8 min_pw;
	u32 min_pri;
	u32 max_pri;
	u8 max_pw;
	u8 min_crbn;
	u8 max_crbn;
	u8 min_stgpn;
	u8 max_stgpn;
	u8 min_stgpr;
	u8 rsv[2];
	u32 min_stgpr_diff;
} __packed;

struct mt7915_dfs_radar_spec {
	struct mt7915_dfs_pulse pulse_th;
	struct mt7915_dfs_pattern radar_pattern[16];
};

#endif
