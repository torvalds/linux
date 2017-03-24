/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QEDI_FW_ISCSI_H_
#define _QEDI_FW_ISCSI_H_

#include "qedi_fw_scsi.h"

struct iscsi_task_params {
	struct iscsi_task_context *context;
	struct iscsi_wqe	  *sqe;
	u32			  tx_io_size;
	u32			  rx_io_size;
	u16			  conn_icid;
	u16			  itid;
	u8			  cq_rss_number;
};

struct iscsi_conn_params {
	u32	first_burst_length;
	u32	max_send_pdu_length;
	u32	max_burst_length;
	bool	initial_r2t;
	bool	immediate_data;
};

/* @brief init_initiator_read_iscsi_task - initializes iSCSI Initiator Read
 * task context.
 *
 * @param task_params	  - Pointer to task parameters struct
 * @param conn_params	  - Connection Parameters
 * @param cmd_params	  - command specific parameters
 * @param cmd_pdu_header  - PDU Header Parameters
 * @param sgl_task_params - Pointer to SGL task params
 * @param dif_task_params - Pointer to DIF parameters struct
 */
int init_initiator_rw_iscsi_task(struct iscsi_task_params *task_params,
				 struct iscsi_conn_params *conn_params,
				 struct scsi_initiator_cmd_params *cmd_params,
				 struct iscsi_cmd_hdr *cmd_pdu_header,
				 struct scsi_sgl_task_params *tx_sgl_params,
				 struct scsi_sgl_task_params *rx_sgl_params,
				 struct scsi_dif_task_params *dif_task_params);

/* @brief init_initiator_login_request_task - initializes iSCSI Initiator Login
 * Request task context.
 *
 * @param task_params		  - Pointer to task parameters struct
 * @param login_req_pdu_header    - PDU Header Parameters
 * @param tx_sgl_task_params	  - Pointer to SGL task params
 * @param rx_sgl_task_params	  - Pointer to SGL task params
 */
int init_initiator_login_request_task(struct iscsi_task_params *task_params,
				      struct iscsi_login_req_hdr *login_header,
				      struct scsi_sgl_task_params *tx_params,
				      struct scsi_sgl_task_params *rx_params);

/* @brief init_initiator_nop_out_task - initializes iSCSI Initiator NOP Out
 * task context.
 *
 * @param task_params		- Pointer to task parameters struct
 * @param nop_out_pdu_header    - PDU Header Parameters
 * @param tx_sgl_task_params	- Pointer to SGL task params
 * @param rx_sgl_task_params	- Pointer to SGL task params
 */
int init_initiator_nop_out_task(struct iscsi_task_params *task_params,
				struct iscsi_nop_out_hdr *nop_out_pdu_header,
				struct scsi_sgl_task_params *tx_sgl_params,
				struct scsi_sgl_task_params *rx_sgl_params);

/* @brief init_initiator_logout_request_task - initializes iSCSI Initiator
 * Logout Request task context.
 *
 * @param task_params		- Pointer to task parameters struct
 * @param logout_pdu_header  - PDU Header Parameters
 * @param tx_sgl_task_params	- Pointer to SGL task params
 * @param rx_sgl_task_params	- Pointer to SGL task params
 */
int init_initiator_logout_request_task(struct iscsi_task_params *task_params,
				       struct iscsi_logout_req_hdr *logout_hdr,
				       struct scsi_sgl_task_params *tx_params,
				       struct scsi_sgl_task_params *rx_params);

/* @brief init_initiator_tmf_request_task - initializes iSCSI Initiator TMF
 * task context.
 *
 * @param task_params	- Pointer to task parameters struct
 * @param tmf_pdu_header - PDU Header Parameters
 */
int init_initiator_tmf_request_task(struct iscsi_task_params *task_params,
				    struct iscsi_tmf_request_hdr *tmf_header);

/* @brief init_initiator_text_request_task - initializes iSCSI Initiator Text
 * Request task context.
 *
 * @param task_params		     - Pointer to task parameters struct
 * @param text_request_pdu_header    - PDU Header Parameters
 * @param tx_sgl_task_params	     - Pointer to Tx SGL task params
 * @param rx_sgl_task_params	     - Pointer to Rx SGL task params
 */
int init_initiator_text_request_task(struct iscsi_task_params *task_params,
				     struct iscsi_text_request_hdr *text_header,
				     struct scsi_sgl_task_params *tx_params,
				     struct scsi_sgl_task_params *rx_params);

/* @brief init_cleanup_task - initializes Clean task (SQE)
 *
 * @param task_params - Pointer to task parameters struct
 */
int init_cleanup_task(struct iscsi_task_params *task_params);
#endif
