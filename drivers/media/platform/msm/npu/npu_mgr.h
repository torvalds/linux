/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _NPU_MGR_H
#define _NPU_MGR_H

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include <linux/spinlock.h>
#include "npu_hw_access.h"
#include "npu_common.h"

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
#define NW_CMD_TIMEOUT_MS (1000 * 60 * 5) /* set for 5 minutes */
#define NW_CMD_TIMEOUT msecs_to_jiffies(NW_CMD_TIMEOUT_MS)
#define NW_DEBUG_TIMEOUT_MS (1000 * 60 * 30) /* set for 30 minutes */
#define NW_DEBUG_TIMEOUT msecs_to_jiffies(NW_DEBUG_TIMEOUT_MS)
#define FIRMWARE_VERSION 0x00001000
#define MAX_LOADED_NETWORK 32
#define NPU_IPC_BUF_LENGTH 512

#define FW_DBG_MODE_PAUSE        (1 << 0)
#define FW_DBG_MODE_INC_TIMEOUT  (1 << 1)
#define FW_DBG_DISABLE_WDOG      (1 << 2)
#define FW_DBG_ENABLE_LOGGING    (1 << 3)
/* -------------------------------------------------------------------------
 * Data Structures
 * -------------------------------------------------------------------------
 */
struct npu_network {
	uint64_t id;
	int buf_hdl;
	uint64_t phy_add;
	uint32_t size;
	uint32_t first_block_size;
	uint32_t network_hdl;
	uint32_t priority;
	uint32_t cur_perf_mode;
	uint32_t init_perf_mode;
	uint32_t num_layers;
	void *stats_buf;
	void __user *stats_buf_u;
	uint32_t stats_buf_size;
	uint32_t trans_id;
	atomic_t ref_cnt;
	bool is_valid;
	bool is_active;
	bool is_unloading;
	bool is_executing;
	bool fw_error;
	bool cmd_pending;
	bool cmd_async;
	int cmd_ret_status;
	struct completion cmd_done;
	struct npu_client *client;
};

enum fw_state {
	FW_DISABLED = 0,
	FW_ENABLED = 1,
};

struct npu_host_ctx {
	struct mutex lock;
	void *subsystem_handle;
	struct npu_device *npu_dev;
	enum fw_state fw_state;
	int32_t fw_ref_cnt;
	int32_t npu_init_cnt;
	int32_t power_vote_num;
	struct work_struct irq_work;
	struct delayed_work fw_deinit_work;
	atomic_t fw_deinit_work_cnt;
	struct workqueue_struct *wq;
	struct completion misc_done;
	struct completion fw_deinit_done;
	bool misc_pending;
	void *prop_buf;
	int32_t network_num;
	struct npu_network networks[MAX_LOADED_NETWORK];
	bool sys_cache_disable;
	uint32_t fw_dbg_mode;
	uint32_t exec_flags_override;
	uint32_t fw_unload_delay_ms;
	atomic_t ipc_trans_id;
	atomic_t network_execute_cnt;
	int cmd_ret_status;

	uint32_t err_irq_sts;
	uint32_t wdg_irq_sts;
	bool fw_error;
};

struct npu_device;

/* -------------------------------------------------------------------------
 * Function Prototypes
 * -------------------------------------------------------------------------
 */
int npu_host_init(struct npu_device *npu_dev);
void npu_host_deinit(struct npu_device *npu_dev);

/* Host Driver IPC Interface */
int npu_host_ipc_pre_init(struct npu_device *npu_dev);
int npu_host_ipc_post_init(struct npu_device *npu_dev);
void npu_host_ipc_deinit(struct npu_device *npu_dev);
int npu_host_ipc_send_cmd(struct npu_device *npu_dev, uint32_t queueIndex,
	void *pCmd);
int npu_host_ipc_read_msg(struct npu_device *npu_dev, uint32_t queueIndex,
	uint32_t *pMsg);

int32_t npu_host_get_info(struct npu_device *npu_dev,
	struct msm_npu_get_info_ioctl *get_info_ioctl);
int32_t npu_host_map_buf(struct npu_client *client,
	struct msm_npu_map_buf_ioctl *map_ioctl);
int32_t npu_host_unmap_buf(struct npu_client *client,
	struct msm_npu_unmap_buf_ioctl *unmap_ioctl);
int32_t npu_host_load_network(struct npu_client *client,
	struct msm_npu_load_network_ioctl *load_ioctl);
int32_t npu_host_load_network_v2(struct npu_client *client,
	struct msm_npu_load_network_ioctl_v2 *load_ioctl,
	struct msm_npu_patch_info_v2 *patch_info);
int32_t npu_host_unload_network(struct npu_client *client,
	struct msm_npu_unload_network_ioctl *unload);
int32_t npu_host_exec_network(struct npu_client *client,
	struct msm_npu_exec_network_ioctl *exec_ioctl);
int32_t npu_host_exec_network_v2(struct npu_client *client,
	struct msm_npu_exec_network_ioctl_v2 *exec_ioctl,
	struct msm_npu_patch_buf_info *patch_buf_info);
int32_t npu_host_loopback_test(struct npu_device *npu_dev);
int32_t npu_host_set_fw_property(struct npu_device *npu_dev,
			struct msm_npu_property *property);
int32_t npu_host_get_fw_property(struct npu_device *npu_dev,
			struct msm_npu_property *property);
void npu_host_cleanup_networks(struct npu_client *client);
int32_t npu_host_set_perf_mode(struct npu_client *client, uint32_t network_hdl,
	uint32_t perf_mode);
int32_t npu_host_get_perf_mode(struct npu_client *client, uint32_t network_hdl);
void npu_dump_debug_timeout_stats(struct npu_device *npu_dev);

#endif /* _NPU_MGR_H */
