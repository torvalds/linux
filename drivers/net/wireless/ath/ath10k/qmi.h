/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */
#ifndef _ATH10K_QMI_H_
#define _ATH10K_QMI_H_

#include <linux/soc/qcom/qmi.h>
#include <linux/qrtr.h>
#include "qmi_wlfw_v01.h"

#define MAX_NUM_MEMORY_REGIONS			2
#define MAX_TIMESTAMP_LEN			32
#define MAX_BUILD_ID_LEN			128
#define MAX_NUM_CAL_V01			5

enum ath10k_qmi_driver_event_type {
	ATH10K_QMI_EVENT_SERVER_ARRIVE,
	ATH10K_QMI_EVENT_SERVER_EXIT,
	ATH10K_QMI_EVENT_FW_READY_IND,
	ATH10K_QMI_EVENT_FW_DOWN_IND,
	ATH10K_QMI_EVENT_MSA_READY_IND,
	ATH10K_QMI_EVENT_MAX,
};

struct ath10k_msa_mem_info {
	phys_addr_t addr;
	u32 size;
	bool secure;
};

struct ath10k_qmi_chip_info {
	u32 chip_id;
	u32 chip_family;
};

struct ath10k_qmi_board_info {
	u32 board_id;
};

struct ath10k_qmi_soc_info {
	u32 soc_id;
};

struct ath10k_qmi_cal_data {
	u32 cal_id;
	u32 total_size;
	u8 *data;
};

struct ath10k_tgt_pipe_cfg {
	__le32 pipe_num;
	__le32 pipe_dir;
	__le32 nentries;
	__le32 nbytes_max;
	__le32 flags;
	__le32 reserved;
};

struct ath10k_svc_pipe_cfg {
	__le32 service_id;
	__le32 pipe_dir;
	__le32 pipe_num;
};

struct ath10k_shadow_reg_cfg {
	__le16 ce_id;
	__le16 reg_offset;
};

struct ath10k_qmi_wlan_enable_cfg {
	u32 num_ce_tgt_cfg;
	struct ath10k_tgt_pipe_cfg *ce_tgt_cfg;
	u32 num_ce_svc_pipe_cfg;
	struct ath10k_svc_pipe_cfg *ce_svc_cfg;
	u32 num_shadow_reg_cfg;
	struct ath10k_shadow_reg_cfg *shadow_reg_cfg;
};

struct ath10k_qmi_driver_event {
	struct list_head list;
	enum ath10k_qmi_driver_event_type type;
	void *data;
};

struct ath10k_qmi {
	struct ath10k *ar;
	struct qmi_handle qmi_hdl;
	struct sockaddr_qrtr sq;
	struct work_struct event_work;
	struct workqueue_struct *event_wq;
	struct list_head event_list;
	spinlock_t event_lock; /* spinlock for qmi event list */
	u32 nr_mem_region;
	struct ath10k_msa_mem_info mem_region[MAX_NUM_MEMORY_REGIONS];
	dma_addr_t msa_pa;
	u32 msa_mem_size;
	void *msa_va;
	struct ath10k_qmi_chip_info chip_info;
	struct ath10k_qmi_board_info board_info;
	struct ath10k_qmi_soc_info soc_info;
	char fw_build_id[MAX_BUILD_ID_LEN + 1];
	u32 fw_version;
	bool fw_ready;
	char fw_build_timestamp[MAX_TIMESTAMP_LEN + 1];
	struct ath10k_qmi_cal_data cal_data[MAX_NUM_CAL_V01];
	bool msa_fixed_perm;
};

int ath10k_qmi_wlan_enable(struct ath10k *ar,
			   struct ath10k_qmi_wlan_enable_cfg *config,
			   enum wlfw_driver_mode_enum_v01 mode,
			   const char *version);
int ath10k_qmi_wlan_disable(struct ath10k *ar);
int ath10k_qmi_register_service_notifier(struct notifier_block *nb);
int ath10k_qmi_init(struct ath10k *ar, u32 msa_size);
int ath10k_qmi_deinit(struct ath10k *ar);
int ath10k_qmi_set_fw_log_mode(struct ath10k *ar, u8 fw_log_mode);

#endif /* ATH10K_QMI_H */
