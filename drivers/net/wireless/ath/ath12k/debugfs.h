/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _ATH12K_DEBUGFS_H_
#define _ATH12K_DEBUGFS_H_

#ifdef CONFIG_ATH12K_DEBUGFS
void ath12k_debugfs_soc_create(struct ath12k_base *ab);
void ath12k_debugfs_soc_destroy(struct ath12k_base *ab);
void ath12k_debugfs_register(struct ath12k *ar);
void ath12k_debugfs_unregister(struct ath12k *ar);
void ath12k_debugfs_fw_stats_process(struct ath12k *ar,
				     struct ath12k_fw_stats *stats);

static inline bool ath12k_debugfs_is_extd_rx_stats_enabled(struct ath12k *ar)
{
	return ar->debug.extd_rx_stats;
}

static inline int ath12k_debugfs_rx_filter(struct ath12k *ar)
{
	return ar->debug.rx_filter;
}

#define ATH12K_CCK_RATES			4
#define ATH12K_OFDM_RATES			8
#define ATH12K_HT_RATES				8
#define ATH12K_VHT_RATES			12
#define ATH12K_HE_RATES				12
#define ATH12K_HE_RATES_WITH_EXTRA_MCS		14
#define ATH12K_EHT_RATES			16
#define HE_EXTRA_MCS_SUPPORT			GENMASK(31, 16)
#define ATH12K_NSS_1				1
#define ATH12K_NSS_4				4
#define ATH12K_NSS_8				8
#define ATH12K_HW_NSS(_rcode)			(((_rcode) >> 5) & 0x7)
#define TPC_STATS_WAIT_TIME			(1 * HZ)
#define MAX_TPC_PREAM_STR_LEN			7
#define TPC_INVAL				-128
#define TPC_MAX					127
#define TPC_STATS_WAIT_TIME			(1 * HZ)
#define TPC_STATS_TOT_ROW			700
#define TPC_STATS_TOT_COLUMN			100
#define MODULATION_LIMIT			126

#define ATH12K_TPC_STATS_BUF_SIZE	(TPC_STATS_TOT_ROW * TPC_STATS_TOT_COLUMN)

enum wmi_tpc_pream_bw {
	WMI_TPC_PREAM_CCK,
	WMI_TPC_PREAM_OFDM,
	WMI_TPC_PREAM_HT20,
	WMI_TPC_PREAM_HT40,
	WMI_TPC_PREAM_VHT20,
	WMI_TPC_PREAM_VHT40,
	WMI_TPC_PREAM_VHT80,
	WMI_TPC_PREAM_VHT160,
	WMI_TPC_PREAM_HE20,
	WMI_TPC_PREAM_HE40,
	WMI_TPC_PREAM_HE80,
	WMI_TPC_PREAM_HE160,
	WMI_TPC_PREAM_EHT20,
	WMI_TPC_PREAM_EHT40,
	WMI_TPC_PREAM_EHT60,
	WMI_TPC_PREAM_EHT80,
	WMI_TPC_PREAM_EHT120,
	WMI_TPC_PREAM_EHT140,
	WMI_TPC_PREAM_EHT160,
	WMI_TPC_PREAM_EHT200,
	WMI_TPC_PREAM_EHT240,
	WMI_TPC_PREAM_EHT280,
	WMI_TPC_PREAM_EHT320,
	WMI_TPC_PREAM_MAX
};

enum ath12k_debug_tpc_stats_ctl_mode {
	ATH12K_TPC_STATS_CTL_MODE_LEGACY_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HT_VHT20_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HE_EHT20_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HT_VHT40_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HE_EHT40_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_VHT80_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HE_EHT80_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_VHT160_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HE_EHT160_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HE_EHT320_5GHZ_6GHZ,
	ATH12K_TPC_STATS_CTL_MODE_CCK_2GHZ,
	ATH12K_TPC_STATS_CTL_MODE_LEGACY_2GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HT20_2GHZ,
	ATH12K_TPC_STATS_CTL_MODE_HT40_2GHZ,

	ATH12K_TPC_STATS_CTL_MODE_EHT80_SU_PUNC20 = 23,
	ATH12K_TPC_STATS_CTL_MODE_EHT160_SU_PUNC20,
	ATH12K_TPC_STATS_CTL_MODE_EHT320_SU_PUNC40,
	ATH12K_TPC_STATS_CTL_MODE_EHT320_SU_PUNC80,
	ATH12K_TPC_STATS_CTL_MODE_EHT320_SU_PUNC120
};

enum ath12k_debug_tpc_stats_support_modes {
	ATH12K_TPC_STATS_SUPPORT_160 = 0,
	ATH12K_TPC_STATS_SUPPORT_320,
	ATH12K_TPC_STATS_SUPPORT_AX,
	ATH12K_TPC_STATS_SUPPORT_AX_EXTRA_MCS,
	ATH12K_TPC_STATS_SUPPORT_BE,
	ATH12K_TPC_STATS_SUPPORT_BE_PUNC,
};
#else
static inline void ath12k_debugfs_soc_create(struct ath12k_base *ab)
{
}

static inline void ath12k_debugfs_soc_destroy(struct ath12k_base *ab)
{
}

static inline void ath12k_debugfs_register(struct ath12k *ar)
{
}

static inline void ath12k_debugfs_unregister(struct ath12k *ar)
{
}

static inline void ath12k_debugfs_fw_stats_process(struct ath12k *ar,
						   struct ath12k_fw_stats *stats)
{
}

static inline bool ath12k_debugfs_is_extd_rx_stats_enabled(struct ath12k *ar)
{
	return false;
}

static inline int ath12k_debugfs_rx_filter(struct ath12k *ar)
{
	return 0;
}
#endif /* CONFIG_ATH12K_DEBUGFS */

#endif /* _ATH12K_DEBUGFS_H_ */
