/* SPDX-License-Identifier: GPL-2.0-only */
/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 */

#ifndef _QED_DEBUGFS_H
#define _QED_DEBUGFS_H

enum qed_dbg_features {
	DBG_FEATURE_GRC,
	DBG_FEATURE_IDLE_CHK,
	DBG_FEATURE_MCP_TRACE,
	DBG_FEATURE_REG_FIFO,
	DBG_FEATURE_IGU_FIFO,
	DBG_FEATURE_PROTECTION_OVERRIDE,
	DBG_FEATURE_FW_ASSERTS,
	DBG_FEATURE_NUM
};

/* Forward Declaration */
struct qed_dev;

int qed_dbg_grc(struct qed_dev *cdev, void *buffer, u32 *num_dumped_bytes);
int qed_dbg_grc_size(struct qed_dev *cdev);
int qed_dbg_idle_chk(struct qed_dev *cdev, void *buffer,
		     u32 *num_dumped_bytes);
int qed_dbg_idle_chk_size(struct qed_dev *cdev);
int qed_dbg_reg_fifo(struct qed_dev *cdev, void *buffer,
		     u32 *num_dumped_bytes);
int qed_dbg_reg_fifo_size(struct qed_dev *cdev);
int qed_dbg_igu_fifo(struct qed_dev *cdev, void *buffer,
		     u32 *num_dumped_bytes);
int qed_dbg_igu_fifo_size(struct qed_dev *cdev);
int qed_dbg_protection_override(struct qed_dev *cdev, void *buffer,
				u32 *num_dumped_bytes);
int qed_dbg_protection_override_size(struct qed_dev *cdev);
int qed_dbg_fw_asserts(struct qed_dev *cdev, void *buffer,
		       u32 *num_dumped_bytes);
int qed_dbg_fw_asserts_size(struct qed_dev *cdev);
int qed_dbg_mcp_trace(struct qed_dev *cdev, void *buffer,
		      u32 *num_dumped_bytes);
int qed_dbg_mcp_trace_size(struct qed_dev *cdev);
int qed_dbg_all_data(struct qed_dev *cdev, void *buffer);
int qed_dbg_all_data_size(struct qed_dev *cdev);
u8 qed_get_debug_engine(struct qed_dev *cdev);
void qed_set_debug_engine(struct qed_dev *cdev, int engine_number);
int qed_dbg_feature(struct qed_dev *cdev, void *buffer,
		    enum qed_dbg_features feature, u32 *num_dumped_bytes);
int qed_dbg_feature_size(struct qed_dev *cdev, enum qed_dbg_features feature);

void qed_dbg_pf_init(struct qed_dev *cdev);
void qed_dbg_pf_exit(struct qed_dev *cdev);

#endif
