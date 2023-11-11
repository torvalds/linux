// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* Copyright 2021 Marvell. All rights reserved. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/qed/common_hsi.h>
#include <linux/qed/storage_common.h>
#include <linux/qed/nvmetcp_common.h>
#include <linux/qed/qed_nvmetcp_if.h>
#include "qed_nvmetcp_fw_funcs.h"

#define NVMETCP_NUM_SGES_IN_CACHE 0x4

bool nvmetcp_is_slow_sgl(u16 num_sges, bool small_mid_sge)
{
	return (num_sges > SCSI_NUM_SGES_SLOW_SGL_THR && small_mid_sge);
}

void init_scsi_sgl_context(struct scsi_sgl_params *ctx_sgl_params,
			   struct scsi_cached_sges *ctx_data_desc,
			   struct storage_sgl_task_params *sgl_params)
{
	u8 num_sges_to_init = (u8)(sgl_params->num_sges > NVMETCP_NUM_SGES_IN_CACHE ?
				   NVMETCP_NUM_SGES_IN_CACHE : sgl_params->num_sges);
	u8 sge_index;

	/* sgl params */
	ctx_sgl_params->sgl_addr.lo = cpu_to_le32(sgl_params->sgl_phys_addr.lo);
	ctx_sgl_params->sgl_addr.hi = cpu_to_le32(sgl_params->sgl_phys_addr.hi);
	ctx_sgl_params->sgl_total_length = cpu_to_le32(sgl_params->total_buffer_size);
	ctx_sgl_params->sgl_num_sges = cpu_to_le16(sgl_params->num_sges);

	for (sge_index = 0; sge_index < num_sges_to_init; sge_index++) {
		ctx_data_desc->sge[sge_index].sge_addr.lo =
			cpu_to_le32(sgl_params->sgl[sge_index].sge_addr.lo);
		ctx_data_desc->sge[sge_index].sge_addr.hi =
			cpu_to_le32(sgl_params->sgl[sge_index].sge_addr.hi);
		ctx_data_desc->sge[sge_index].sge_len =
			cpu_to_le32(sgl_params->sgl[sge_index].sge_len);
	}
}

static inline u32 calc_rw_task_size(struct nvmetcp_task_params *task_params,
				    enum nvmetcp_task_type task_type)
{
	u32 io_size;

	if (task_type == NVMETCP_TASK_TYPE_HOST_WRITE)
		io_size = task_params->tx_io_size;
	else
		io_size = task_params->rx_io_size;

	if (unlikely(!io_size))
		return 0;

	return io_size;
}

static inline void init_sqe(struct nvmetcp_task_params *task_params,
			    struct storage_sgl_task_params *sgl_task_params,
			    enum nvmetcp_task_type task_type)
{
	if (!task_params->sqe)
		return;

	memset(task_params->sqe, 0, sizeof(*task_params->sqe));
	task_params->sqe->task_id = cpu_to_le16(task_params->itid);

	switch (task_type) {
	case NVMETCP_TASK_TYPE_HOST_WRITE: {
		u32 buf_size = 0;
		u32 num_sges = 0;

		SET_FIELD(task_params->sqe->contlen_cdbsize,
			  NVMETCP_WQE_CDB_SIZE_OR_NVMETCP_CMD, 1);
		SET_FIELD(task_params->sqe->flags, NVMETCP_WQE_WQE_TYPE,
			  NVMETCP_WQE_TYPE_NORMAL);
		if (task_params->tx_io_size) {
			if (task_params->send_write_incapsule)
				buf_size = calc_rw_task_size(task_params, task_type);

			if (nvmetcp_is_slow_sgl(sgl_task_params->num_sges,
						sgl_task_params->small_mid_sge))
				num_sges = NVMETCP_WQE_NUM_SGES_SLOWIO;
			else
				num_sges = min((u16)sgl_task_params->num_sges,
					       (u16)SCSI_NUM_SGES_SLOW_SGL_THR);
		}
		SET_FIELD(task_params->sqe->flags, NVMETCP_WQE_NUM_SGES, num_sges);
		SET_FIELD(task_params->sqe->contlen_cdbsize, NVMETCP_WQE_CONT_LEN, buf_size);
	} break;

	case NVMETCP_TASK_TYPE_HOST_READ: {
		SET_FIELD(task_params->sqe->flags, NVMETCP_WQE_WQE_TYPE,
			  NVMETCP_WQE_TYPE_NORMAL);
		SET_FIELD(task_params->sqe->contlen_cdbsize,
			  NVMETCP_WQE_CDB_SIZE_OR_NVMETCP_CMD, 1);
	} break;

	case NVMETCP_TASK_TYPE_INIT_CONN_REQUEST: {
		SET_FIELD(task_params->sqe->flags, NVMETCP_WQE_WQE_TYPE,
			  NVMETCP_WQE_TYPE_MIDDLE_PATH);

		if (task_params->tx_io_size) {
			SET_FIELD(task_params->sqe->contlen_cdbsize, NVMETCP_WQE_CONT_LEN,
				  task_params->tx_io_size);
			SET_FIELD(task_params->sqe->flags, NVMETCP_WQE_NUM_SGES,
				  min((u16)sgl_task_params->num_sges,
				      (u16)SCSI_NUM_SGES_SLOW_SGL_THR));
		}
	} break;

	case NVMETCP_TASK_TYPE_CLEANUP:
		SET_FIELD(task_params->sqe->flags, NVMETCP_WQE_WQE_TYPE,
			  NVMETCP_WQE_TYPE_TASK_CLEANUP);

	default:
		break;
	}
}

/* The following function initializes of NVMeTCP task params */
static inline void
init_nvmetcp_task_params(struct e5_nvmetcp_task_context *context,
			 struct nvmetcp_task_params *task_params,
			 enum nvmetcp_task_type task_type)
{
	context->ystorm_st_context.state.cccid = task_params->host_cccid;
	SET_FIELD(context->ustorm_st_context.error_flags, USTORM_NVMETCP_TASK_ST_CTX_NVME_TCP, 1);
	context->ustorm_st_context.nvme_tcp_opaque_lo = cpu_to_le32(task_params->opq.lo);
	context->ustorm_st_context.nvme_tcp_opaque_hi = cpu_to_le32(task_params->opq.hi);
}

/* The following function initializes default values to all tasks */
static inline void
init_default_nvmetcp_task(struct nvmetcp_task_params *task_params,
			  void *pdu_header, void *nvme_cmd,
			  enum nvmetcp_task_type task_type)
{
	struct e5_nvmetcp_task_context *context = task_params->context;
	const u8 val_byte = context->mstorm_ag_context.cdu_validation;
	u8 dw_index;

	memset(context, 0, sizeof(*context));
	init_nvmetcp_task_params(context, task_params,
				 (enum nvmetcp_task_type)task_type);

	/* Swapping requirements used below, will be removed in future FW versions */
	if (task_type == NVMETCP_TASK_TYPE_HOST_WRITE ||
	    task_type == NVMETCP_TASK_TYPE_HOST_READ) {
		for (dw_index = 0;
		     dw_index < QED_NVMETCP_CMN_HDR_SIZE / sizeof(u32);
		     dw_index++)
			context->ystorm_st_context.pdu_hdr.task_hdr.reg[dw_index] =
				cpu_to_le32(__swab32(((u32 *)pdu_header)[dw_index]));

		for (dw_index = QED_NVMETCP_CMN_HDR_SIZE / sizeof(u32);
		     dw_index < QED_NVMETCP_CMD_HDR_SIZE / sizeof(u32);
		     dw_index++)
			context->ystorm_st_context.pdu_hdr.task_hdr.reg[dw_index] =
				cpu_to_le32(__swab32(((u32 *)nvme_cmd)[dw_index - 2]));
	} else {
		for (dw_index = 0;
		     dw_index < QED_NVMETCP_NON_IO_HDR_SIZE / sizeof(u32);
		     dw_index++)
			context->ystorm_st_context.pdu_hdr.task_hdr.reg[dw_index] =
				cpu_to_le32(__swab32(((u32 *)pdu_header)[dw_index]));
	}

	/* M-Storm Context: */
	context->mstorm_ag_context.cdu_validation = val_byte;
	context->mstorm_st_context.task_type = (u8)(task_type);
	context->mstorm_ag_context.task_cid = cpu_to_le16(task_params->conn_icid);

	/* Ustorm Context: */
	SET_FIELD(context->ustorm_ag_context.flags1, E5_USTORM_NVMETCP_TASK_AG_CTX_R2T2RECV, 1);
	context->ustorm_st_context.task_type = (u8)(task_type);
	context->ustorm_st_context.cq_rss_number = task_params->cq_rss_number;
	context->ustorm_ag_context.icid = cpu_to_le16(task_params->conn_icid);
}

/* The following function initializes the U-Storm Task Contexts */
static inline void
init_ustorm_task_contexts(struct ustorm_nvmetcp_task_st_ctx *ustorm_st_context,
			  struct e5_ustorm_nvmetcp_task_ag_ctx *ustorm_ag_context,
			  u32 remaining_recv_len,
			  u32 expected_data_transfer_len, u8 num_sges,
			  bool tx_dif_conn_err_en)
{
	/* Remaining data to be received in bytes. Used in validations*/
	ustorm_st_context->rem_rcv_len = cpu_to_le32(remaining_recv_len);
	ustorm_ag_context->exp_data_acked = cpu_to_le32(expected_data_transfer_len);
	ustorm_st_context->exp_data_transfer_len = cpu_to_le32(expected_data_transfer_len);
	SET_FIELD(ustorm_st_context->reg1_map, REG1_NUM_SGES, num_sges);
	SET_FIELD(ustorm_ag_context->flags2, E5_USTORM_NVMETCP_TASK_AG_CTX_DIF_ERROR_CF_EN,
		  tx_dif_conn_err_en ? 1 : 0);
}

/* The following function initializes Local Completion Contexts: */
static inline void
set_local_completion_context(struct e5_nvmetcp_task_context *context)
{
	SET_FIELD(context->ystorm_st_context.state.flags,
		  YSTORM_NVMETCP_TASK_STATE_LOCAL_COMP, 1);
	SET_FIELD(context->ustorm_st_context.flags,
		  USTORM_NVMETCP_TASK_ST_CTX_LOCAL_COMP, 1);
}

/* Common Fastpath task init function: */
static inline void
init_rw_nvmetcp_task(struct nvmetcp_task_params *task_params,
		     enum nvmetcp_task_type task_type,
		     void *pdu_header, void *nvme_cmd,
		     struct storage_sgl_task_params *sgl_task_params)
{
	struct e5_nvmetcp_task_context *context = task_params->context;
	u32 task_size = calc_rw_task_size(task_params, task_type);
	bool slow_io = false;
	u8 num_sges = 0;

	init_default_nvmetcp_task(task_params, pdu_header, nvme_cmd, task_type);

	/* Tx/Rx: */
	if (task_params->tx_io_size) {
		/* if data to transmit: */
		init_scsi_sgl_context(&context->ystorm_st_context.state.sgl_params,
				      &context->ystorm_st_context.state.data_desc,
				      sgl_task_params);
		slow_io = nvmetcp_is_slow_sgl(sgl_task_params->num_sges,
					      sgl_task_params->small_mid_sge);
		num_sges =
			(u8)(!slow_io ? min((u32)sgl_task_params->num_sges,
					    (u32)SCSI_NUM_SGES_SLOW_SGL_THR) :
					    NVMETCP_WQE_NUM_SGES_SLOWIO);
		if (slow_io) {
			SET_FIELD(context->ystorm_st_context.state.flags,
				  YSTORM_NVMETCP_TASK_STATE_SLOW_IO, 1);
		}
	} else if (task_params->rx_io_size) {
		/* if data to receive: */
		init_scsi_sgl_context(&context->mstorm_st_context.sgl_params,
				      &context->mstorm_st_context.data_desc,
				      sgl_task_params);
		num_sges =
			(u8)(!nvmetcp_is_slow_sgl(sgl_task_params->num_sges,
						  sgl_task_params->small_mid_sge) ?
						  min((u32)sgl_task_params->num_sges,
						      (u32)SCSI_NUM_SGES_SLOW_SGL_THR) :
						      NVMETCP_WQE_NUM_SGES_SLOWIO);
		context->mstorm_st_context.rem_task_size = cpu_to_le32(task_size);
	}

	/* Ustorm context: */
	init_ustorm_task_contexts(&context->ustorm_st_context,
				  &context->ustorm_ag_context,
				  /* Remaining Receive length is the Task Size */
				  task_size,
				  /* The size of the transmitted task */
				  task_size,
				  /* num_sges */
				  num_sges,
				  false);

	/* Set exp_data_acked */
	if (task_type == NVMETCP_TASK_TYPE_HOST_WRITE) {
		if (task_params->send_write_incapsule)
			context->ustorm_ag_context.exp_data_acked = task_size;
		else
			context->ustorm_ag_context.exp_data_acked = 0;
	} else if (task_type == NVMETCP_TASK_TYPE_HOST_READ) {
		context->ustorm_ag_context.exp_data_acked = 0;
	}

	context->ustorm_ag_context.exp_cont_len = 0;
	init_sqe(task_params, sgl_task_params, task_type);
}

static void
init_common_initiator_read_task(struct nvmetcp_task_params *task_params,
				struct nvme_tcp_cmd_pdu *cmd_pdu_header,
				struct nvme_command *nvme_cmd,
				struct storage_sgl_task_params *sgl_task_params)
{
	init_rw_nvmetcp_task(task_params, NVMETCP_TASK_TYPE_HOST_READ,
			     cmd_pdu_header, nvme_cmd, sgl_task_params);
}

void init_nvmetcp_host_read_task(struct nvmetcp_task_params *task_params,
				 struct nvme_tcp_cmd_pdu *cmd_pdu_header,
				 struct nvme_command *nvme_cmd,
				 struct storage_sgl_task_params *sgl_task_params)
{
	init_common_initiator_read_task(task_params, (void *)cmd_pdu_header,
					(void *)nvme_cmd, sgl_task_params);
}

static void
init_common_initiator_write_task(struct nvmetcp_task_params *task_params,
				 struct nvme_tcp_cmd_pdu *cmd_pdu_header,
				 struct nvme_command *nvme_cmd,
				 struct storage_sgl_task_params *sgl_task_params)
{
	init_rw_nvmetcp_task(task_params, NVMETCP_TASK_TYPE_HOST_WRITE,
			     cmd_pdu_header, nvme_cmd, sgl_task_params);
}

void init_nvmetcp_host_write_task(struct nvmetcp_task_params *task_params,
				  struct nvme_tcp_cmd_pdu *cmd_pdu_header,
				  struct nvme_command *nvme_cmd,
				  struct storage_sgl_task_params *sgl_task_params)
{
	init_common_initiator_write_task(task_params, (void *)cmd_pdu_header,
					 (void *)nvme_cmd, sgl_task_params);
}

static void
init_common_login_request_task(struct nvmetcp_task_params *task_params,
			       void *login_req_pdu_header,
			       struct storage_sgl_task_params *tx_sgl_task_params,
			       struct storage_sgl_task_params *rx_sgl_task_params)
{
	struct e5_nvmetcp_task_context *context = task_params->context;

	init_default_nvmetcp_task(task_params, (void *)login_req_pdu_header, NULL,
				  NVMETCP_TASK_TYPE_INIT_CONN_REQUEST);

	/* Ustorm Context: */
	init_ustorm_task_contexts(&context->ustorm_st_context,
				  &context->ustorm_ag_context,

				  /* Remaining Receive length is the Task Size */
				  task_params->rx_io_size ?
				  rx_sgl_task_params->total_buffer_size : 0,

				  /* The size of the transmitted task */
				  task_params->tx_io_size ?
				  tx_sgl_task_params->total_buffer_size : 0,
				  0, /* num_sges */
				  0); /* tx_dif_conn_err_en */

	/* SGL context: */
	if (task_params->tx_io_size)
		init_scsi_sgl_context(&context->ystorm_st_context.state.sgl_params,
				      &context->ystorm_st_context.state.data_desc,
				      tx_sgl_task_params);
	if (task_params->rx_io_size)
		init_scsi_sgl_context(&context->mstorm_st_context.sgl_params,
				      &context->mstorm_st_context.data_desc,
				      rx_sgl_task_params);

	context->mstorm_st_context.rem_task_size =
		cpu_to_le32(task_params->rx_io_size ?
				 rx_sgl_task_params->total_buffer_size : 0);
	init_sqe(task_params, tx_sgl_task_params, NVMETCP_TASK_TYPE_INIT_CONN_REQUEST);
}

/* The following function initializes Login task in Host mode: */
void init_nvmetcp_init_conn_req_task(struct nvmetcp_task_params *task_params,
				     struct nvme_tcp_icreq_pdu *init_conn_req_pdu_hdr,
				     struct storage_sgl_task_params *tx_sgl_task_params,
				     struct storage_sgl_task_params *rx_sgl_task_params)
{
	init_common_login_request_task(task_params, init_conn_req_pdu_hdr,
				       tx_sgl_task_params, rx_sgl_task_params);
}

void init_cleanup_task_nvmetcp(struct nvmetcp_task_params *task_params)
{
	init_sqe(task_params, NULL, NVMETCP_TASK_TYPE_CLEANUP);
}
