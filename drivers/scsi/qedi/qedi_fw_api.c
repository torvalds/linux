/* QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include "qedi_hsi.h"
#include <linux/qed/qed_if.h>

#include "qedi_fw_iscsi.h"
#include "qedi_fw_scsi.h"

#define SCSI_NUM_SGES_IN_CACHE 0x4

static bool scsi_is_slow_sgl(u16 num_sges, bool small_mid_sge)
{
	return (num_sges > SCSI_NUM_SGES_SLOW_SGL_THR && small_mid_sge);
}

static
void init_scsi_sgl_context(struct scsi_sgl_params *ctx_sgl_params,
			   struct scsi_cached_sges *ctx_data_desc,
			   struct scsi_sgl_task_params *sgl_task_params)
{
	u8 sge_index;
	u8 num_sges;
	u32 val;

	num_sges = (sgl_task_params->num_sges > SCSI_NUM_SGES_IN_CACHE) ?
			     SCSI_NUM_SGES_IN_CACHE : sgl_task_params->num_sges;

	/* sgl params */
	val = cpu_to_le32(sgl_task_params->sgl_phys_addr.lo);
	ctx_sgl_params->sgl_addr.lo = val;
	val = cpu_to_le32(sgl_task_params->sgl_phys_addr.hi);
	ctx_sgl_params->sgl_addr.hi = val;
	val = cpu_to_le32(sgl_task_params->total_buffer_size);
	ctx_sgl_params->sgl_total_length = val;
	ctx_sgl_params->sgl_num_sges = cpu_to_le16(sgl_task_params->num_sges);

	for (sge_index = 0; sge_index < num_sges; sge_index++) {
		val = cpu_to_le32(sgl_task_params->sgl[sge_index].sge_addr.lo);
		ctx_data_desc->sge[sge_index].sge_addr.lo = val;
		val = cpu_to_le32(sgl_task_params->sgl[sge_index].sge_addr.hi);
		ctx_data_desc->sge[sge_index].sge_addr.hi = val;
		val = cpu_to_le32(sgl_task_params->sgl[sge_index].sge_len);
		ctx_data_desc->sge[sge_index].sge_len = val;
	}
}

static u32 calc_rw_task_size(struct iscsi_task_params *task_params,
			     enum iscsi_task_type task_type,
			     struct scsi_sgl_task_params *sgl_task_params,
			     struct scsi_dif_task_params *dif_task_params)
{
	u32 io_size;

	if (task_type == ISCSI_TASK_TYPE_INITIATOR_WRITE ||
	    task_type == ISCSI_TASK_TYPE_TARGET_READ)
		io_size = task_params->tx_io_size;
	else
		io_size = task_params->rx_io_size;

	if (!io_size)
		return 0;

	if (!dif_task_params)
		return io_size;

	return !dif_task_params->dif_on_network ?
	       io_size : sgl_task_params->total_buffer_size;
}

static void
init_dif_context_flags(struct iscsi_dif_flags *ctx_dif_flags,
		       struct scsi_dif_task_params *dif_task_params)
{
	if (!dif_task_params)
		return;

	SET_FIELD(ctx_dif_flags->flags, ISCSI_DIF_FLAGS_PROT_INTERVAL_SIZE_LOG,
		  dif_task_params->dif_block_size_log);
	SET_FIELD(ctx_dif_flags->flags, ISCSI_DIF_FLAGS_DIF_TO_PEER,
		  dif_task_params->dif_on_network ? 1 : 0);
	SET_FIELD(ctx_dif_flags->flags, ISCSI_DIF_FLAGS_HOST_INTERFACE,
		  dif_task_params->dif_on_host ? 1 : 0);
}

static void init_sqe(struct iscsi_task_params *task_params,
		     struct scsi_sgl_task_params *sgl_task_params,
		     struct scsi_dif_task_params *dif_task_params,
		     struct iscsi_common_hdr *pdu_header,
		     struct scsi_initiator_cmd_params *cmd_params,
		     enum iscsi_task_type task_type,
		     bool is_cleanup)
{
	if (!task_params->sqe)
		return;

	memset(task_params->sqe, 0, sizeof(*task_params->sqe));
	task_params->sqe->task_id = cpu_to_le16(task_params->itid);
	if (is_cleanup) {
		SET_FIELD(task_params->sqe->flags, ISCSI_WQE_WQE_TYPE,
			  ISCSI_WQE_TYPE_TASK_CLEANUP);
		return;
	}

	switch (task_type) {
	case ISCSI_TASK_TYPE_INITIATOR_WRITE:
	{
		u32 buf_size = 0;
		u32 num_sges = 0;

		init_dif_context_flags(&task_params->sqe->prot_flags,
				       dif_task_params);

		SET_FIELD(task_params->sqe->flags, ISCSI_WQE_WQE_TYPE,
			  ISCSI_WQE_TYPE_NORMAL);

		if (task_params->tx_io_size) {
			buf_size = calc_rw_task_size(task_params, task_type,
						     sgl_task_params,
						     dif_task_params);

		if (scsi_is_slow_sgl(sgl_task_params->num_sges,
				     sgl_task_params->small_mid_sge))
			num_sges = ISCSI_WQE_NUM_SGES_SLOWIO;
		else
			num_sges = min(sgl_task_params->num_sges,
				       (u16)SCSI_NUM_SGES_SLOW_SGL_THR);
	}

	SET_FIELD(task_params->sqe->flags, ISCSI_WQE_NUM_SGES, num_sges);
	SET_FIELD(task_params->sqe->contlen_cdbsize, ISCSI_WQE_CONT_LEN,
		  buf_size);

	if (GET_FIELD(pdu_header->hdr_second_dword,
		      ISCSI_CMD_HDR_TOTAL_AHS_LEN))
		SET_FIELD(task_params->sqe->contlen_cdbsize, ISCSI_WQE_CDB_SIZE,
			  cmd_params->extended_cdb_sge.sge_len);
	}
		break;
	case ISCSI_TASK_TYPE_INITIATOR_READ:
		SET_FIELD(task_params->sqe->flags, ISCSI_WQE_WQE_TYPE,
			  ISCSI_WQE_TYPE_NORMAL);

		if (GET_FIELD(pdu_header->hdr_second_dword,
			      ISCSI_CMD_HDR_TOTAL_AHS_LEN))
			SET_FIELD(task_params->sqe->contlen_cdbsize,
				  ISCSI_WQE_CDB_SIZE,
				  cmd_params->extended_cdb_sge.sge_len);
		break;
	case ISCSI_TASK_TYPE_LOGIN_RESPONSE:
	case ISCSI_TASK_TYPE_MIDPATH:
	{
		bool advance_statsn = true;

		if (task_type == ISCSI_TASK_TYPE_LOGIN_RESPONSE)
			SET_FIELD(task_params->sqe->flags, ISCSI_WQE_WQE_TYPE,
				  ISCSI_WQE_TYPE_LOGIN);
		else
			SET_FIELD(task_params->sqe->flags, ISCSI_WQE_WQE_TYPE,
				  ISCSI_WQE_TYPE_MIDDLE_PATH);

		if (task_type == ISCSI_TASK_TYPE_MIDPATH) {
			u8 opcode = GET_FIELD(pdu_header->hdr_first_byte,
					      ISCSI_COMMON_HDR_OPCODE);

			if (opcode != ISCSI_OPCODE_TEXT_RESPONSE &&
			    (opcode != ISCSI_OPCODE_NOP_IN ||
			    pdu_header->itt == ISCSI_TTT_ALL_ONES))
				advance_statsn = false;
		}

		SET_FIELD(task_params->sqe->flags, ISCSI_WQE_RESPONSE,
			  advance_statsn ? 1 : 0);

		if (task_params->tx_io_size) {
			SET_FIELD(task_params->sqe->contlen_cdbsize,
				  ISCSI_WQE_CONT_LEN, task_params->tx_io_size);

		if (scsi_is_slow_sgl(sgl_task_params->num_sges,
				     sgl_task_params->small_mid_sge))
			SET_FIELD(task_params->sqe->flags, ISCSI_WQE_NUM_SGES,
				  ISCSI_WQE_NUM_SGES_SLOWIO);
		else
			SET_FIELD(task_params->sqe->flags, ISCSI_WQE_NUM_SGES,
				  min(sgl_task_params->num_sges,
				      (u16)SCSI_NUM_SGES_SLOW_SGL_THR));
		}
	}
		break;
	default:
		break;
	}
}

static void init_default_iscsi_task(struct iscsi_task_params *task_params,
				    struct data_hdr *pdu_header,
				    enum iscsi_task_type task_type)
{
	struct iscsi_task_context *context;
	u16 index;
	u32 val;

	context = task_params->context;
	memset(context, 0, sizeof(*context));

	for (index = 0; index <
	     ARRAY_SIZE(context->ystorm_st_context.pdu_hdr.data.data);
	     index++) {
		val = cpu_to_le32(pdu_header->data[index]);
		context->ystorm_st_context.pdu_hdr.data.data[index] = val;
	}

	context->mstorm_st_context.task_type = task_type;
	context->mstorm_ag_context.task_cid =
					    cpu_to_le16(task_params->conn_icid);

	SET_FIELD(context->ustorm_ag_context.flags1,
		  USTORM_ISCSI_TASK_AG_CTX_R2T2RECV, 1);

	context->ustorm_st_context.task_type = task_type;
	context->ustorm_st_context.cq_rss_number = task_params->cq_rss_number;
	context->ustorm_ag_context.icid = cpu_to_le16(task_params->conn_icid);
}

static
void init_initiator_rw_cdb_ystorm_context(struct ystorm_iscsi_task_st_ctx *ystc,
					  struct scsi_initiator_cmd_params *cmd)
{
	union iscsi_task_hdr *ctx_pdu_hdr = &ystc->pdu_hdr;
	u32 val;

	if (!cmd->extended_cdb_sge.sge_len)
		return;

	SET_FIELD(ctx_pdu_hdr->ext_cdb_cmd.hdr_second_dword,
		  ISCSI_EXT_CDB_CMD_HDR_CDB_SIZE,
		  cmd->extended_cdb_sge.sge_len);
	val = cpu_to_le32(cmd->extended_cdb_sge.sge_addr.lo);
	ctx_pdu_hdr->ext_cdb_cmd.cdb_sge.sge_addr.lo = val;
	val = cpu_to_le32(cmd->extended_cdb_sge.sge_addr.hi);
	ctx_pdu_hdr->ext_cdb_cmd.cdb_sge.sge_addr.hi = val;
	val = cpu_to_le32(cmd->extended_cdb_sge.sge_len);
	ctx_pdu_hdr->ext_cdb_cmd.cdb_sge.sge_len  = val;
}

static
void init_ustorm_task_contexts(struct ustorm_iscsi_task_st_ctx *ustorm_st_cxt,
			       struct ustorm_iscsi_task_ag_ctx *ustorm_ag_cxt,
			       u32 remaining_recv_len,
			       u32 expected_data_transfer_len,
			       u8 num_sges, bool tx_dif_conn_err_en)
{
	u32 val;

	ustorm_st_cxt->rem_rcv_len = cpu_to_le32(remaining_recv_len);
	ustorm_ag_cxt->exp_data_acked = cpu_to_le32(expected_data_transfer_len);
	val = cpu_to_le32(expected_data_transfer_len);
	ustorm_st_cxt->exp_data_transfer_len = val;
	SET_FIELD(ustorm_st_cxt->reg1.reg1_map, ISCSI_REG1_NUM_SGES, num_sges);
	SET_FIELD(ustorm_ag_cxt->flags2,
		  USTORM_ISCSI_TASK_AG_CTX_DIF_ERROR_CF_EN,
		  tx_dif_conn_err_en ? 1 : 0);
}

static
void set_rw_exp_data_acked_and_cont_len(struct iscsi_task_context *context,
					struct iscsi_conn_params  *conn_params,
					enum iscsi_task_type task_type,
					u32 task_size,
					u32 exp_data_transfer_len,
					u8 total_ahs_length)
{
	u32 max_unsolicited_data = 0, val;

	if (total_ahs_length &&
	    (task_type == ISCSI_TASK_TYPE_INITIATOR_WRITE ||
	     task_type == ISCSI_TASK_TYPE_INITIATOR_READ))
		SET_FIELD(context->ustorm_st_context.flags2,
			  USTORM_ISCSI_TASK_ST_CTX_AHS_EXIST, 1);

	switch (task_type) {
	case ISCSI_TASK_TYPE_INITIATOR_WRITE:
		if (!conn_params->initial_r2t)
			max_unsolicited_data = conn_params->first_burst_length;
		else if (conn_params->immediate_data)
			max_unsolicited_data =
					  min(conn_params->first_burst_length,
					      conn_params->max_send_pdu_length);

		context->ustorm_ag_context.exp_data_acked =
				   cpu_to_le32(total_ahs_length == 0 ?
						min(exp_data_transfer_len,
						    max_unsolicited_data) :
						((u32)(total_ahs_length +
						       ISCSI_AHS_CNTL_SIZE)));
		break;
	case ISCSI_TASK_TYPE_TARGET_READ:
		val = cpu_to_le32(exp_data_transfer_len);
		context->ustorm_ag_context.exp_data_acked = val;
		break;
	case ISCSI_TASK_TYPE_INITIATOR_READ:
		context->ustorm_ag_context.exp_data_acked =
					cpu_to_le32((total_ahs_length == 0 ? 0 :
						     total_ahs_length +
						     ISCSI_AHS_CNTL_SIZE));
		break;
	case ISCSI_TASK_TYPE_TARGET_WRITE:
		val = cpu_to_le32(task_size);
		context->ustorm_ag_context.exp_cont_len = val;
		break;
	default:
		break;
	}
}

static
void init_rtdif_task_context(struct rdif_task_context *rdif_context,
			     struct tdif_task_context *tdif_context,
			     struct scsi_dif_task_params *dif_task_params,
			     enum iscsi_task_type task_type)
{
	u32 val;

	if (!dif_task_params->dif_on_network || !dif_task_params->dif_on_host)
		return;

	if (task_type == ISCSI_TASK_TYPE_TARGET_WRITE ||
	    task_type == ISCSI_TASK_TYPE_INITIATOR_READ) {
		rdif_context->app_tag_value =
				  cpu_to_le16(dif_task_params->application_tag);
		rdif_context->partial_crc_value = cpu_to_le16(0xffff);
		val = cpu_to_le32(dif_task_params->initial_ref_tag);
		rdif_context->initial_ref_tag = val;
		rdif_context->app_tag_mask =
			     cpu_to_le16(dif_task_params->application_tag_mask);
		SET_FIELD(rdif_context->flags0, RDIF_TASK_CONTEXT_CRC_SEED,
			  dif_task_params->crc_seed ? 1 : 0);
		SET_FIELD(rdif_context->flags0, RDIF_TASK_CONTEXT_HOSTGUARDTYPE,
			  dif_task_params->host_guard_type);
		SET_FIELD(rdif_context->flags0,
			  RDIF_TASK_CONTEXT_PROTECTIONTYPE,
			  dif_task_params->protection_type);
		SET_FIELD(rdif_context->flags0,
			  RDIF_TASK_CONTEXT_INITIALREFTAGVALID, 1);
		SET_FIELD(rdif_context->flags0,
			  RDIF_TASK_CONTEXT_KEEPREFTAGCONST,
			  dif_task_params->keep_ref_tag_const ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_VALIDATEAPPTAG,
			  (dif_task_params->validate_app_tag &&
			  dif_task_params->dif_on_network) ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_VALIDATEGUARD,
			  (dif_task_params->validate_guard &&
			  dif_task_params->dif_on_network) ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_VALIDATEREFTAG,
			  (dif_task_params->validate_ref_tag &&
			  dif_task_params->dif_on_network) ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_HOSTINTERFACE,
			  dif_task_params->dif_on_host ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_NETWORKINTERFACE,
			  dif_task_params->dif_on_network ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_FORWARDGUARD,
			  dif_task_params->forward_guard ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_FORWARDAPPTAG,
			  dif_task_params->forward_app_tag ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_FORWARDREFTAG,
			  dif_task_params->forward_ref_tag ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_FORWARDAPPTAGWITHMASK,
			  dif_task_params->forward_app_tag_with_mask ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_FORWARDREFTAGWITHMASK,
			  dif_task_params->forward_ref_tag_with_mask ? 1 : 0);
		SET_FIELD(rdif_context->flags1,
			  RDIF_TASK_CONTEXT_INTERVALSIZE,
			  dif_task_params->dif_block_size_log - 9);
		SET_FIELD(rdif_context->state,
			  RDIF_TASK_CONTEXT_REFTAGMASK,
			  dif_task_params->ref_tag_mask);
		SET_FIELD(rdif_context->state, RDIF_TASK_CONTEXT_IGNOREAPPTAG,
			  dif_task_params->ignore_app_tag);
	}

	if (task_type == ISCSI_TASK_TYPE_TARGET_READ ||
	    task_type == ISCSI_TASK_TYPE_INITIATOR_WRITE) {
		tdif_context->app_tag_value =
				  cpu_to_le16(dif_task_params->application_tag);
		tdif_context->partial_crc_valueB =
		       cpu_to_le16(dif_task_params->crc_seed ? 0xffff : 0x0000);
		tdif_context->partial_crc_value_a =
		       cpu_to_le16(dif_task_params->crc_seed ? 0xffff : 0x0000);
		SET_FIELD(tdif_context->flags0, TDIF_TASK_CONTEXT_CRC_SEED,
			  dif_task_params->crc_seed ? 1 : 0);

		SET_FIELD(tdif_context->flags0,
			  TDIF_TASK_CONTEXT_SETERRORWITHEOP,
			  dif_task_params->tx_dif_conn_err_en ? 1 : 0);
		SET_FIELD(tdif_context->flags1, TDIF_TASK_CONTEXT_FORWARDGUARD,
			  dif_task_params->forward_guard   ? 1 : 0);
		SET_FIELD(tdif_context->flags1, TDIF_TASK_CONTEXT_FORWARDAPPTAG,
			  dif_task_params->forward_app_tag ? 1 : 0);
		SET_FIELD(tdif_context->flags1, TDIF_TASK_CONTEXT_FORWARDREFTAG,
			  dif_task_params->forward_ref_tag ? 1 : 0);
		SET_FIELD(tdif_context->flags1, TDIF_TASK_CONTEXT_INTERVALSIZE,
			  dif_task_params->dif_block_size_log - 9);
		SET_FIELD(tdif_context->flags1, TDIF_TASK_CONTEXT_HOSTINTERFACE,
			  dif_task_params->dif_on_host    ? 1 : 0);
		SET_FIELD(tdif_context->flags1,
			  TDIF_TASK_CONTEXT_NETWORKINTERFACE,
			  dif_task_params->dif_on_network ? 1 : 0);
		val = cpu_to_le32(dif_task_params->initial_ref_tag);
		tdif_context->initial_ref_tag = val;
		tdif_context->app_tag_mask =
			     cpu_to_le16(dif_task_params->application_tag_mask);
		SET_FIELD(tdif_context->flags0,
			  TDIF_TASK_CONTEXT_HOSTGUARDTYPE,
			  dif_task_params->host_guard_type);
		SET_FIELD(tdif_context->flags0,
			  TDIF_TASK_CONTEXT_PROTECTIONTYPE,
			  dif_task_params->protection_type);
		SET_FIELD(tdif_context->flags0,
			  TDIF_TASK_CONTEXT_INITIALREFTAGVALID,
			  dif_task_params->initial_ref_tag_is_valid ? 1 : 0);
		SET_FIELD(tdif_context->flags0,
			  TDIF_TASK_CONTEXT_KEEPREFTAGCONST,
			  dif_task_params->keep_ref_tag_const ? 1 : 0);
		SET_FIELD(tdif_context->flags1, TDIF_TASK_CONTEXT_VALIDATEGUARD,
			  (dif_task_params->validate_guard &&
			   dif_task_params->dif_on_host) ? 1 : 0);
		SET_FIELD(tdif_context->flags1,
			  TDIF_TASK_CONTEXT_VALIDATEAPPTAG,
			  (dif_task_params->validate_app_tag &&
			  dif_task_params->dif_on_host) ? 1 : 0);
		SET_FIELD(tdif_context->flags1,
			  TDIF_TASK_CONTEXT_VALIDATEREFTAG,
			  (dif_task_params->validate_ref_tag &&
			   dif_task_params->dif_on_host) ? 1 : 0);
		SET_FIELD(tdif_context->flags1,
			  TDIF_TASK_CONTEXT_FORWARDAPPTAGWITHMASK,
			  dif_task_params->forward_app_tag_with_mask ? 1 : 0);
		SET_FIELD(tdif_context->flags1,
			  TDIF_TASK_CONTEXT_FORWARDREFTAGWITHMASK,
			  dif_task_params->forward_ref_tag_with_mask ? 1 : 0);
		SET_FIELD(tdif_context->flags1,
			  TDIF_TASK_CONTEXT_REFTAGMASK,
			  dif_task_params->ref_tag_mask);
		SET_FIELD(tdif_context->flags0,
			  TDIF_TASK_CONTEXT_IGNOREAPPTAG,
			  dif_task_params->ignore_app_tag ? 1 : 0);
	}
}

static void set_local_completion_context(struct iscsi_task_context *context)
{
	SET_FIELD(context->ystorm_st_context.state.flags,
		  YSTORM_ISCSI_TASK_STATE_LOCAL_COMP, 1);
	SET_FIELD(context->ustorm_st_context.flags,
		  USTORM_ISCSI_TASK_ST_CTX_LOCAL_COMP, 1);
}

static int init_rw_iscsi_task(struct iscsi_task_params *task_params,
			      enum iscsi_task_type task_type,
			      struct iscsi_conn_params *conn_params,
			      struct iscsi_common_hdr *pdu_header,
			      struct scsi_sgl_task_params *sgl_task_params,
			      struct scsi_initiator_cmd_params *cmd_params,
			      struct scsi_dif_task_params *dif_task_params)
{
	u32 exp_data_transfer_len = conn_params->max_burst_length;
	struct iscsi_task_context *cxt;
	bool slow_io = false;
	u32 task_size, val;
	u8 num_sges = 0;

	task_size = calc_rw_task_size(task_params, task_type, sgl_task_params,
				      dif_task_params);

	init_default_iscsi_task(task_params, (struct data_hdr *)pdu_header,
				task_type);

	cxt = task_params->context;

	val = cpu_to_le32(task_size);
	cxt->ystorm_st_context.pdu_hdr.cmd.expected_transfer_length = val;
	init_initiator_rw_cdb_ystorm_context(&cxt->ystorm_st_context,
					     cmd_params);
	val = cpu_to_le32(cmd_params->sense_data_buffer_phys_addr.lo);
	cxt->mstorm_st_context.sense_db.lo = val;

	val = cpu_to_le32(cmd_params->sense_data_buffer_phys_addr.hi);
	cxt->mstorm_st_context.sense_db.hi = val;

	if (task_params->tx_io_size) {
		init_dif_context_flags(&cxt->ystorm_st_context.state.dif_flags,
				       dif_task_params);
		init_scsi_sgl_context(&cxt->ystorm_st_context.state.sgl_params,
				      &cxt->ystorm_st_context.state.data_desc,
				      sgl_task_params);

		slow_io = scsi_is_slow_sgl(sgl_task_params->num_sges,
					   sgl_task_params->small_mid_sge);

		num_sges = !slow_io ? min_t(u16, sgl_task_params->num_sges,
					    (u16)SCSI_NUM_SGES_SLOW_SGL_THR) :
				      ISCSI_WQE_NUM_SGES_SLOWIO;

		if (slow_io) {
			SET_FIELD(cxt->ystorm_st_context.state.flags,
				  YSTORM_ISCSI_TASK_STATE_SLOW_IO, 1);
		}
	} else if (task_params->rx_io_size) {
		init_dif_context_flags(&cxt->mstorm_st_context.dif_flags,
				       dif_task_params);
		init_scsi_sgl_context(&cxt->mstorm_st_context.sgl_params,
				      &cxt->mstorm_st_context.data_desc,
				      sgl_task_params);
		num_sges = !scsi_is_slow_sgl(sgl_task_params->num_sges,
				sgl_task_params->small_mid_sge) ?
				min_t(u16, sgl_task_params->num_sges,
				      (u16)SCSI_NUM_SGES_SLOW_SGL_THR) :
				ISCSI_WQE_NUM_SGES_SLOWIO;
		cxt->mstorm_st_context.rem_task_size = cpu_to_le32(task_size);
	}

	if (exp_data_transfer_len > task_size  ||
	    task_type != ISCSI_TASK_TYPE_TARGET_WRITE)
		exp_data_transfer_len = task_size;

	init_ustorm_task_contexts(&task_params->context->ustorm_st_context,
				  &task_params->context->ustorm_ag_context,
				  task_size, exp_data_transfer_len, num_sges,
				  dif_task_params ?
				  dif_task_params->tx_dif_conn_err_en : false);

	set_rw_exp_data_acked_and_cont_len(task_params->context, conn_params,
					   task_type, task_size,
					   exp_data_transfer_len,
					GET_FIELD(pdu_header->hdr_second_dword,
						  ISCSI_CMD_HDR_TOTAL_AHS_LEN));

	if (dif_task_params)
		init_rtdif_task_context(&task_params->context->rdif_context,
					&task_params->context->tdif_context,
					dif_task_params, task_type);

	init_sqe(task_params, sgl_task_params, dif_task_params, pdu_header,
		 cmd_params, task_type, false);

	return 0;
}

int init_initiator_rw_iscsi_task(struct iscsi_task_params *task_params,
				 struct iscsi_conn_params *conn_params,
				 struct scsi_initiator_cmd_params *cmd_params,
				 struct iscsi_cmd_hdr *cmd_header,
				 struct scsi_sgl_task_params *tx_sgl_params,
				 struct scsi_sgl_task_params *rx_sgl_params,
				 struct scsi_dif_task_params *dif_task_params)
{
	if (GET_FIELD(cmd_header->flags_attr, ISCSI_CMD_HDR_WRITE))
		return init_rw_iscsi_task(task_params,
					  ISCSI_TASK_TYPE_INITIATOR_WRITE,
					  conn_params,
					  (struct iscsi_common_hdr *)cmd_header,
					  tx_sgl_params, cmd_params,
					  dif_task_params);
	else if (GET_FIELD(cmd_header->flags_attr, ISCSI_CMD_HDR_READ))
		return init_rw_iscsi_task(task_params,
					  ISCSI_TASK_TYPE_INITIATOR_READ,
					  conn_params,
					  (struct iscsi_common_hdr *)cmd_header,
					  rx_sgl_params, cmd_params,
					  dif_task_params);
	else
		return -1;
}

int init_initiator_login_request_task(struct iscsi_task_params *task_params,
				      struct iscsi_login_req_hdr  *login_header,
				      struct scsi_sgl_task_params *tx_params,
				      struct scsi_sgl_task_params *rx_params)
{
	struct iscsi_task_context *cxt;

	cxt = task_params->context;

	init_default_iscsi_task(task_params,
				(struct data_hdr *)login_header,
				ISCSI_TASK_TYPE_MIDPATH);

	init_ustorm_task_contexts(&cxt->ustorm_st_context,
				  &cxt->ustorm_ag_context,
				  task_params->rx_io_size ?
				  rx_params->total_buffer_size : 0,
				  task_params->tx_io_size ?
				  tx_params->total_buffer_size : 0, 0,
				  0);

	if (task_params->tx_io_size)
		init_scsi_sgl_context(&cxt->ystorm_st_context.state.sgl_params,
				      &cxt->ystorm_st_context.state.data_desc,
				      tx_params);

	if (task_params->rx_io_size)
		init_scsi_sgl_context(&cxt->mstorm_st_context.sgl_params,
				      &cxt->mstorm_st_context.data_desc,
				      rx_params);

	cxt->mstorm_st_context.rem_task_size =
			cpu_to_le32(task_params->rx_io_size ?
				    rx_params->total_buffer_size : 0);

	init_sqe(task_params, tx_params, NULL,
		 (struct iscsi_common_hdr *)login_header, NULL,
		 ISCSI_TASK_TYPE_MIDPATH, false);

	return 0;
}

int init_initiator_nop_out_task(struct iscsi_task_params *task_params,
				struct iscsi_nop_out_hdr *nop_out_pdu_header,
				struct scsi_sgl_task_params *tx_sgl_task_params,
				struct scsi_sgl_task_params *rx_sgl_task_params)
{
	struct iscsi_task_context *cxt;

	cxt = task_params->context;

	init_default_iscsi_task(task_params,
				(struct data_hdr *)nop_out_pdu_header,
				ISCSI_TASK_TYPE_MIDPATH);

	if (nop_out_pdu_header->itt == ISCSI_ITT_ALL_ONES)
		set_local_completion_context(task_params->context);

	if (task_params->tx_io_size)
		init_scsi_sgl_context(&cxt->ystorm_st_context.state.sgl_params,
				      &cxt->ystorm_st_context.state.data_desc,
				      tx_sgl_task_params);

	if (task_params->rx_io_size)
		init_scsi_sgl_context(&cxt->mstorm_st_context.sgl_params,
				      &cxt->mstorm_st_context.data_desc,
				      rx_sgl_task_params);

	init_ustorm_task_contexts(&cxt->ustorm_st_context,
				  &cxt->ustorm_ag_context,
				  task_params->rx_io_size ?
				  rx_sgl_task_params->total_buffer_size : 0,
				  task_params->tx_io_size ?
				  tx_sgl_task_params->total_buffer_size : 0,
				  0, 0);

	cxt->mstorm_st_context.rem_task_size =
				cpu_to_le32(task_params->rx_io_size ?
					rx_sgl_task_params->total_buffer_size :
					0);

	init_sqe(task_params, tx_sgl_task_params, NULL,
		 (struct iscsi_common_hdr *)nop_out_pdu_header, NULL,
		 ISCSI_TASK_TYPE_MIDPATH, false);

	return 0;
}

int init_initiator_logout_request_task(struct iscsi_task_params *task_params,
				       struct iscsi_logout_req_hdr *logout_hdr,
				       struct scsi_sgl_task_params *tx_params,
				       struct scsi_sgl_task_params *rx_params)
{
	struct iscsi_task_context *cxt;

	cxt = task_params->context;

	init_default_iscsi_task(task_params,
				(struct data_hdr *)logout_hdr,
				ISCSI_TASK_TYPE_MIDPATH);

	if (task_params->tx_io_size)
		init_scsi_sgl_context(&cxt->ystorm_st_context.state.sgl_params,
				      &cxt->ystorm_st_context.state.data_desc,
				      tx_params);

	if (task_params->rx_io_size)
		init_scsi_sgl_context(&cxt->mstorm_st_context.sgl_params,
				      &cxt->mstorm_st_context.data_desc,
				      rx_params);

	init_ustorm_task_contexts(&cxt->ustorm_st_context,
				  &cxt->ustorm_ag_context,
				  task_params->rx_io_size ?
				  rx_params->total_buffer_size : 0,
				  task_params->tx_io_size ?
				  tx_params->total_buffer_size : 0,
				  0, 0);

	cxt->mstorm_st_context.rem_task_size =
					cpu_to_le32(task_params->rx_io_size ?
					rx_params->total_buffer_size : 0);

	init_sqe(task_params, tx_params, NULL,
		 (struct iscsi_common_hdr *)logout_hdr, NULL,
		 ISCSI_TASK_TYPE_MIDPATH, false);

	return 0;
}

int init_initiator_tmf_request_task(struct iscsi_task_params *task_params,
				    struct iscsi_tmf_request_hdr *tmf_header)
{
	init_default_iscsi_task(task_params, (struct data_hdr *)tmf_header,
				ISCSI_TASK_TYPE_MIDPATH);

	init_sqe(task_params, NULL, NULL,
		 (struct iscsi_common_hdr *)tmf_header, NULL,
		 ISCSI_TASK_TYPE_MIDPATH, false);

	return 0;
}

int init_initiator_text_request_task(struct iscsi_task_params *task_params,
				     struct iscsi_text_request_hdr *text_header,
				     struct scsi_sgl_task_params *tx_params,
				     struct scsi_sgl_task_params *rx_params)
{
	struct iscsi_task_context *cxt;

	cxt = task_params->context;

	init_default_iscsi_task(task_params,
				(struct data_hdr *)text_header,
				ISCSI_TASK_TYPE_MIDPATH);

	if (task_params->tx_io_size)
		init_scsi_sgl_context(&cxt->ystorm_st_context.state.sgl_params,
				      &cxt->ystorm_st_context.state.data_desc,
				      tx_params);

	if (task_params->rx_io_size)
		init_scsi_sgl_context(&cxt->mstorm_st_context.sgl_params,
				      &cxt->mstorm_st_context.data_desc,
				      rx_params);

	cxt->mstorm_st_context.rem_task_size =
				cpu_to_le32(task_params->rx_io_size ?
					rx_params->total_buffer_size : 0);

	init_ustorm_task_contexts(&cxt->ustorm_st_context,
				  &cxt->ustorm_ag_context,
				  task_params->rx_io_size ?
				  rx_params->total_buffer_size : 0,
				  task_params->tx_io_size ?
				  tx_params->total_buffer_size : 0, 0, 0);

	init_sqe(task_params, tx_params, NULL,
		 (struct iscsi_common_hdr *)text_header, NULL,
		 ISCSI_TASK_TYPE_MIDPATH, false);

	return 0;
}

int init_cleanup_task(struct iscsi_task_params *task_params)
{
	init_sqe(task_params, NULL, NULL, NULL, NULL, ISCSI_TASK_TYPE_MIDPATH,
		 true);
	return 0;
}
