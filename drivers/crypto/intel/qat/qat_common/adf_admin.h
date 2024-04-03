/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2023 Intel Corporation */
#ifndef ADF_ADMIN
#define ADF_ADMIN

#include "icp_qat_fw_init_admin.h"

struct adf_accel_dev;

int adf_init_admin_comms(struct adf_accel_dev *accel_dev);
void adf_exit_admin_comms(struct adf_accel_dev *accel_dev);
int adf_send_admin_init(struct adf_accel_dev *accel_dev);
int adf_get_ae_fw_counters(struct adf_accel_dev *accel_dev, u16 ae, u64 *reqs, u64 *resps);
int adf_init_admin_pm(struct adf_accel_dev *accel_dev, u32 idle_delay);
int adf_send_admin_tim_sync(struct adf_accel_dev *accel_dev, u32 cnt);
int adf_send_admin_hb_timer(struct adf_accel_dev *accel_dev, uint32_t ticks);
int adf_send_admin_rl_init(struct adf_accel_dev *accel_dev,
			   struct icp_qat_fw_init_admin_slice_cnt *slices);
int adf_send_admin_rl_add_update(struct adf_accel_dev *accel_dev,
				 struct icp_qat_fw_init_admin_req *req);
int adf_send_admin_rl_delete(struct adf_accel_dev *accel_dev, u16 node_id,
			     u8 node_type);
int adf_get_fw_timestamp(struct adf_accel_dev *accel_dev, u64 *timestamp);
int adf_get_pm_info(struct adf_accel_dev *accel_dev, dma_addr_t p_state_addr, size_t buff_size);
int adf_get_cnv_stats(struct adf_accel_dev *accel_dev, u16 ae, u16 *err_cnt, u16 *latest_err);
int adf_send_admin_tl_start(struct adf_accel_dev *accel_dev,
			    dma_addr_t tl_dma_addr, size_t layout_sz, u8 *rp_indexes,
			    struct icp_qat_fw_init_admin_slice_cnt *slice_count);
int adf_send_admin_tl_stop(struct adf_accel_dev *accel_dev);

#endif
