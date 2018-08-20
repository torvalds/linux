/* QLogic FCoE Offload Driver
 * Copyright (c) 2016-2018 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */
#ifndef _FCOE_FW_FUNCS_H
#define _FCOE_FW_FUNCS_H
#include "drv_scsi_fw_funcs.h"
#include "qedf_hsi.h"
#include <linux/qed/qed_if.h>

struct fcoe_task_params {
	/* Output parameter [set/filled by the HSI function] */
	struct e4_fcoe_task_context *context;

	/* Output parameter [set/filled by the HSI function] */
	struct fcoe_wqe *sqe;
	enum fcoe_task_type task_type;
	u32 tx_io_size; /* in bytes */
	u32 rx_io_size; /* in bytes */
	u32 conn_cid;
	u16 itid;
	u8 cq_rss_number;

	 /* Whether it's Tape device or not (0=Disk, 1=Tape) */
	u8 is_tape_device;
};

/**
 * @brief init_initiator_rw_fcoe_task - Initializes FCoE task context for
 * read/write task types and init fcoe_sqe
 *
 * @param task_params - Pointer to task parameters struct
 * @param sgl_task_params - Pointer to SGL task params
 * @param sense_data_buffer_phys_addr - Pointer to sense data buffer
 * @param task_retry_id - retry identification - Used only for Tape device
 * @param fcp_cmnd_payload - FCP CMD Payload
 */
int init_initiator_rw_fcoe_task(struct fcoe_task_params *task_params,
	struct scsi_sgl_task_params *sgl_task_params,
	struct regpair sense_data_buffer_phys_addr,
	u32 task_retry_id,
	u8 fcp_cmd_payload[32]);

/**
 * @brief init_initiator_midpath_fcoe_task - Initializes FCoE task context for
 * midpath/unsolicited task types and init fcoe_sqe
 *
 * @param task_params - Pointer to task parameters struct
 * @param mid_path_fc_header - FC header
 * @param tx_sgl_task_params - Pointer to Tx SGL task params
 * @param rx_sgl_task_params - Pointer to Rx SGL task params
 * @param fw_to_place_fc_header	- Indication if the FW will place the FC header
 * in addition to the data arrives.
 */
int init_initiator_midpath_unsolicited_fcoe_task(
	struct fcoe_task_params *task_params,
	struct fcoe_tx_mid_path_params *mid_path_fc_header,
	struct scsi_sgl_task_params *tx_sgl_task_params,
	struct scsi_sgl_task_params *rx_sgl_task_params,
	u8 fw_to_place_fc_header);

/**
 * @brief init_initiator_abort_fcoe_task - Initializes FCoE task context for
 * abort task types and init fcoe_sqe
 *
 * @param task_params - Pointer to task parameters struct
 */
int init_initiator_abort_fcoe_task(struct fcoe_task_params *task_params);

/**
 * @brief init_initiator_cleanup_fcoe_task - Initializes FCoE task context for
 * cleanup task types and init fcoe_sqe
 *
 *
 * @param task_params - Pointer to task parameters struct
 */
int init_initiator_cleanup_fcoe_task(struct fcoe_task_params *task_params);

/**
 * @brief init_initiator_cleanup_fcoe_task - Initializes FCoE task context for
 * sequence recovery task types and init fcoe_sqe
 *
 *
 * @param task_params - Pointer to task parameters struct
 * @param desired_offset - The desired offest the task will be re-sent from
 */
int init_initiator_sequence_recovery_fcoe_task(
	struct fcoe_task_params *task_params,
	u32 desired_offset);
#endif
