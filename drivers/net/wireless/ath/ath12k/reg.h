/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_REG_H
#define ATH12K_REG_H

#include <linux/kernel.h>
#include <net/regulatory.h>

struct ath12k_base;
struct ath12k;

#define ATH12K_2GHZ_MAX_FREQUENCY	2495
#define ATH12K_5GHZ_MAX_FREQUENCY	5920

/* DFS regdomains supported by Firmware */
enum ath12k_dfs_region {
	ATH12K_DFS_REG_UNSET,
	ATH12K_DFS_REG_FCC,
	ATH12K_DFS_REG_ETSI,
	ATH12K_DFS_REG_MKK,
	ATH12K_DFS_REG_CN,
	ATH12K_DFS_REG_KR,
	ATH12K_DFS_REG_MKK_N,
	ATH12K_DFS_REG_UNDEF,
};

enum ath12k_reg_cc_code {
	REG_SET_CC_STATUS_PASS = 0,
	REG_CURRENT_ALPHA2_NOT_FOUND = 1,
	REG_INIT_ALPHA2_NOT_FOUND = 2,
	REG_SET_CC_CHANGE_NOT_ALLOWED = 3,
	REG_SET_CC_STATUS_NO_MEMORY = 4,
	REG_SET_CC_STATUS_FAIL = 5,
};

struct ath12k_reg_rule {
	u16 start_freq;
	u16 end_freq;
	u16 max_bw;
	u8 reg_power;
	u8 ant_gain;
	u16 flags;
	bool psd_flag;
	u16 psd_eirp;
};

struct ath12k_reg_info {
	enum ath12k_reg_cc_code status_code;
	u8 num_phy;
	u8 phy_id;
	u16 reg_dmn_pair;
	u16 ctry_code;
	u8 alpha2[REG_ALPHA2_LEN + 1];
	u32 dfs_region;
	u32 phybitmap;
	bool is_ext_reg_event;
	u32 min_bw_2g;
	u32 max_bw_2g;
	u32 min_bw_5g;
	u32 max_bw_5g;
	u32 num_2g_reg_rules;
	u32 num_5g_reg_rules;
	struct ath12k_reg_rule *reg_rules_2g_ptr;
	struct ath12k_reg_rule *reg_rules_5g_ptr;
	enum wmi_reg_6g_client_type client_type;
	bool rnr_tpe_usable;
	bool unspecified_ap_usable;
	/* TODO: All 6G related info can be stored only for required
	 * combination instead of all types, to optimize memory usage.
	 */
	u8 domain_code_6g_ap[WMI_REG_CURRENT_MAX_AP_TYPE];
	u8 domain_code_6g_client[WMI_REG_CURRENT_MAX_AP_TYPE][WMI_REG_MAX_CLIENT_TYPE];
	u32 domain_code_6g_super_id;
	u32 min_bw_6g_ap[WMI_REG_CURRENT_MAX_AP_TYPE];
	u32 max_bw_6g_ap[WMI_REG_CURRENT_MAX_AP_TYPE];
	u32 min_bw_6g_client[WMI_REG_CURRENT_MAX_AP_TYPE][WMI_REG_MAX_CLIENT_TYPE];
	u32 max_bw_6g_client[WMI_REG_CURRENT_MAX_AP_TYPE][WMI_REG_MAX_CLIENT_TYPE];
	u32 num_6g_reg_rules_ap[WMI_REG_CURRENT_MAX_AP_TYPE];
	u32 num_6g_reg_rules_cl[WMI_REG_CURRENT_MAX_AP_TYPE][WMI_REG_MAX_CLIENT_TYPE];
	struct ath12k_reg_rule *reg_rules_6g_ap_ptr[WMI_REG_CURRENT_MAX_AP_TYPE];
	struct ath12k_reg_rule *reg_rules_6g_client_ptr
		[WMI_REG_CURRENT_MAX_AP_TYPE][WMI_REG_MAX_CLIENT_TYPE];
};

/* Phy bitmaps */
enum ath12k_reg_phy_bitmap {
	ATH12K_REG_PHY_BITMAP_NO11AX	= BIT(5),
	ATH12K_REG_PHY_BITMAP_NO11BE	= BIT(6),
};

enum ath12k_reg_status {
	ATH12K_REG_STATUS_VALID,
	ATH12K_REG_STATUS_DROP,
	ATH12K_REG_STATUS_FALLBACK,
};

void ath12k_reg_init(struct ieee80211_hw *hw);
void ath12k_reg_free(struct ath12k_base *ab);
void ath12k_regd_update_work(struct work_struct *work);
struct ieee80211_regdomain *ath12k_reg_build_regd(struct ath12k_base *ab,
						  struct ath12k_reg_info *reg_info,
						  enum wmi_vdev_type vdev_type,
						  enum ieee80211_ap_reg_power power_type);
int ath12k_regd_update(struct ath12k *ar, bool init);
int ath12k_reg_update_chan_list(struct ath12k *ar, bool wait);

void ath12k_reg_reset_reg_info(struct ath12k_reg_info *reg_info);
int ath12k_reg_handle_chan_list(struct ath12k_base *ab,
				struct ath12k_reg_info *reg_info,
				enum wmi_vdev_type vdev_type,
				enum ieee80211_ap_reg_power power_type);
void ath12k_regd_update_chan_list_work(struct work_struct *work);
enum wmi_reg_6g_ap_type
ath12k_reg_ap_pwr_convert(enum ieee80211_ap_reg_power power_type);
enum ath12k_reg_status ath12k_reg_validate_reg_info(struct ath12k_base *ab,
						    struct ath12k_reg_info *reg_info);
#endif
