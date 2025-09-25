// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "ras.h"
#include "ras_cmd.h"

#define RAS_CMD_MAJOR_VERSION 6
#define RAS_CMD_MINOR_VERSION 0
#define RAS_CMD_VERSION  (((RAS_CMD_MAJOR_VERSION) << 10) | (RAS_CMD_MINOR_VERSION))

static int ras_cmd_add_device(struct ras_core_context *ras_core)
{
	INIT_LIST_HEAD(&ras_core->ras_cmd.head);
	ras_core->ras_cmd.ras_core = ras_core;
	ras_core->ras_cmd.dev_handle = (uintptr_t)ras_core ^ RAS_CMD_DEV_HANDLE_MAGIC;
	return 0;
}

static int ras_cmd_remove_device(struct ras_core_context *ras_core)
{
	memset(&ras_core->ras_cmd, 0, sizeof(ras_core->ras_cmd));
	return 0;
}

static int ras_get_block_ecc_info(struct ras_core_context *ras_core,
				struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_block_ecc_info_req *input_data =
			(struct ras_cmd_block_ecc_info_req *)cmd->input_buff_raw;
	struct ras_cmd_block_ecc_info_rsp *output_data =
			(struct ras_cmd_block_ecc_info_rsp *)cmd->output_buff_raw;
	struct ras_ecc_count err_data;
	int ret;

	if (cmd->input_size != sizeof(struct ras_cmd_block_ecc_info_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	memset(&err_data, 0, sizeof(err_data));
	ret = ras_aca_get_block_ecc_count(ras_core, input_data->block_id, &err_data);
	if (ret)
		return RAS_CMD__ERROR_GENERIC;

	output_data->ce_count = err_data.total_ce_count;
	output_data->ue_count = err_data.total_ue_count;
	output_data->de_count = err_data.total_de_count;

	cmd->output_size = sizeof(struct ras_cmd_block_ecc_info_rsp);
	return RAS_CMD__SUCCESS;
}

static void ras_cmd_update_bad_page_info(struct ras_cmd_bad_page_record *ras_cmd_record,
	struct eeprom_umc_record *record)
{
	ras_cmd_record->retired_page = record->cur_nps_retired_row_pfn;
	ras_cmd_record->ts = record->ts;
	ras_cmd_record->err_type = record->err_type;
	ras_cmd_record->mem_channel = record->mem_channel;
	ras_cmd_record->mcumc_id = record->mcumc_id;
	ras_cmd_record->address = record->address;
	ras_cmd_record->bank = record->bank;
	ras_cmd_record->valid = 1;
}

static int ras_cmd_get_group_bad_pages(struct ras_core_context *ras_core,
	uint32_t group_index, struct ras_cmd_bad_pages_info_rsp *output_data)
{
	struct eeprom_umc_record record;
	struct ras_cmd_bad_page_record *ras_cmd_record;
	uint32_t i = 0, bp_cnt = 0, group_cnt = 0;

	output_data->bp_in_group = 0;
	output_data->group_index = 0;

	bp_cnt = ras_umc_get_badpage_count(ras_core);
	if (bp_cnt) {
		output_data->group_index = group_index;
		group_cnt = bp_cnt / RAS_CMD_MAX_BAD_PAGES_PER_GROUP
			+ ((bp_cnt % RAS_CMD_MAX_BAD_PAGES_PER_GROUP) ? 1 : 0);

		if (group_index >= group_cnt)
			return RAS_CMD__ERROR_INVALID_INPUT_DATA;

		i = group_index * RAS_CMD_MAX_BAD_PAGES_PER_GROUP;
		for (;
		   i < bp_cnt && output_data->bp_in_group < RAS_CMD_MAX_BAD_PAGES_PER_GROUP;
		   i++) {
			if (ras_umc_get_badpage_record(ras_core, i, &record))
				return RAS_CMD__ERROR_GENERIC;

			ras_cmd_record = &output_data->records[i % RAS_CMD_MAX_BAD_PAGES_PER_GROUP];

			memset(ras_cmd_record, 0, sizeof(*ras_cmd_record));
			ras_cmd_update_bad_page_info(ras_cmd_record, &record);
			output_data->bp_in_group++;
		}
	}
	output_data->bp_total_cnt = bp_cnt;
	return RAS_CMD__SUCCESS;
}

static int ras_cmd_get_bad_pages(struct ras_core_context *ras_core,
				struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_bad_pages_info_req *input_data =
			(struct ras_cmd_bad_pages_info_req *)cmd->input_buff_raw;
	struct ras_cmd_bad_pages_info_rsp *output_data =
			(struct ras_cmd_bad_pages_info_rsp *)cmd->output_buff_raw;
	int ret;

	if (cmd->input_size != sizeof(struct ras_cmd_bad_pages_info_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	ret = ras_cmd_get_group_bad_pages(ras_core, input_data->group_index, output_data);
	if (ret)
		return RAS_CMD__ERROR_GENERIC;

	output_data->version = 0;

	cmd->output_size = sizeof(struct ras_cmd_bad_pages_info_rsp);
	return RAS_CMD__SUCCESS;
}

static int ras_cmd_clear_bad_page_info(struct ras_core_context *ras_core,
				struct ras_cmd_ctx *cmd, void *data)
{
	if (cmd->input_size != sizeof(struct ras_cmd_dev_handle))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	if (ras_eeprom_reset_table(ras_core))
		return RAS_CMD__ERROR_GENERIC;

	if (ras_umc_clean_badpage_data(ras_core))
		return RAS_CMD__ERROR_GENERIC;

	return RAS_CMD__SUCCESS;
}

static int ras_cmd_reset_all_error_counts(struct ras_core_context *ras_core,
				struct ras_cmd_ctx *cmd, void *data)
{
	if (cmd->input_size != sizeof(struct ras_cmd_dev_handle))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	if (ras_aca_clear_all_blocks_ecc_count(ras_core))
		return RAS_CMD__ERROR_GENERIC;

	if (ras_umc_clear_logged_ecc(ras_core))
		return RAS_CMD__ERROR_GENERIC;

	return RAS_CMD__SUCCESS;
}

static int ras_cmd_get_cper_snapshot(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_cper_snapshot_rsp *output_data =
			(struct ras_cmd_cper_snapshot_rsp *)cmd->output_buff_raw;
	struct ras_log_batch_overview overview;

	if (cmd->input_size != sizeof(struct ras_cmd_cper_snapshot_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	ras_log_ring_get_batch_overview(ras_core, &overview);

	output_data->total_cper_num = overview.logged_batch_count;
	output_data->start_cper_id = overview.first_batch_id;
	output_data->latest_cper_id = overview.last_batch_id;

	output_data->version = 0;

	cmd->output_size = sizeof(struct ras_cmd_cper_snapshot_rsp);
	return RAS_CMD__SUCCESS;
}

static int ras_cmd_get_cper_records(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_cper_record_req *req =
			(struct ras_cmd_cper_record_req *)cmd->input_buff_raw;
	struct ras_cmd_cper_record_rsp *rsp =
			(struct ras_cmd_cper_record_rsp *)cmd->output_buff_raw;
	struct ras_log_info *trace[MAX_RECORD_PER_BATCH] = {0};
	struct ras_log_batch_overview overview;
	uint32_t offset = 0, real_data_len = 0;
	uint64_t batch_id;
	uint8_t *buffer;
	int ret = 0, i, count;

	if (cmd->input_size != sizeof(struct ras_cmd_cper_record_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	if (!req->buf_size || !req->buf_ptr || !req->cper_num)
		return RAS_CMD__ERROR_INVALID_INPUT_DATA;

	buffer = kzalloc(req->buf_size, GFP_KERNEL);
	if (!buffer)
		return RAS_CMD__ERROR_GENERIC;

	ras_log_ring_get_batch_overview(ras_core, &overview);
	for (i = 0; i < req->cper_num; i++) {
		batch_id = req->cper_start_id + i;
		if (batch_id >= overview.last_batch_id)
			break;

		count = ras_log_ring_get_batch_records(ras_core, batch_id, trace,
					ARRAY_SIZE(trace));
		if (count > 0) {
			ret = ras_cper_generate_cper(ras_core, trace, count,
					&buffer[offset], req->buf_size - offset, &real_data_len);
			if (ret)
				break;

			offset += real_data_len;
		}
	}

	if ((ret && (ret != -ENOMEM)) ||
		copy_to_user(u64_to_user_ptr(req->buf_ptr), buffer, offset)) {
		kfree(buffer);
		return RAS_CMD__ERROR_GENERIC;
	}

	rsp->real_data_size = offset;
	rsp->real_cper_num = i;
	rsp->remain_num = (ret == -ENOMEM) ? (req->cper_num - i) : 0;
	rsp->version = 0;

	cmd->output_size = sizeof(struct ras_cmd_cper_record_rsp);

	kfree(buffer);

	return RAS_CMD__SUCCESS;
}

static int ras_cmd_get_batch_trace_snapshot(struct ras_core_context *ras_core,
	struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_batch_trace_snapshot_rsp *rsp =
			(struct ras_cmd_batch_trace_snapshot_rsp *)cmd->output_buff_raw;
	struct ras_log_batch_overview overview;


	if (cmd->input_size != sizeof(struct ras_cmd_batch_trace_snapshot_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	ras_log_ring_get_batch_overview(ras_core, &overview);

	rsp->total_batch_num = overview.logged_batch_count;
	rsp->start_batch_id = overview.first_batch_id;
	rsp->latest_batch_id = overview.last_batch_id;
	rsp->version = 0;

	cmd->output_size = sizeof(struct ras_cmd_batch_trace_snapshot_rsp);
	return RAS_CMD__SUCCESS;
}

static int ras_cmd_get_batch_trace_records(struct ras_core_context *ras_core,
	struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_batch_trace_record_req *input_data =
			(struct ras_cmd_batch_trace_record_req *)cmd->input_buff_raw;
	struct ras_cmd_batch_trace_record_rsp *output_data =
			(struct ras_cmd_batch_trace_record_rsp *)cmd->output_buff_raw;
	struct ras_log_batch_overview overview;
	struct ras_log_info *trace_arry[MAX_RECORD_PER_BATCH] = {0};
	struct ras_log_info *record;
	int i, j, count = 0, offset = 0;
	uint64_t id;
	bool completed = false;

	if (cmd->input_size != sizeof(struct ras_cmd_batch_trace_record_req))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	if ((!input_data->batch_num) || (input_data->batch_num > RAS_CMD_MAX_BATCH_NUM))
		return RAS_CMD__ERROR_INVALID_INPUT_DATA;

	ras_log_ring_get_batch_overview(ras_core, &overview);
	if ((input_data->start_batch_id < overview.first_batch_id) ||
	    (input_data->start_batch_id >= overview.last_batch_id))
		return RAS_CMD__ERROR_INVALID_INPUT_SIZE;

	for (i = 0; i < input_data->batch_num; i++) {
		id = input_data->start_batch_id + i;
		if (id >= overview.last_batch_id) {
			completed = true;
			break;
		}

		count = ras_log_ring_get_batch_records(ras_core,
					id, trace_arry, ARRAY_SIZE(trace_arry));
		if (count > 0) {
			if ((offset + count) > RAS_CMD_MAX_TRACE_NUM)
				break;
			for (j = 0; j < count; j++) {
				record = &output_data->records[offset + j];
				record->seqno = trace_arry[j]->seqno;
				record->timestamp = trace_arry[j]->timestamp;
				record->event = trace_arry[j]->event;
				memcpy(&record->aca_reg,
					&trace_arry[j]->aca_reg, sizeof(trace_arry[j]->aca_reg));
			}
		} else {
			count = 0;
		}

		output_data->batchs[i].batch_id = id;
		output_data->batchs[i].offset = offset;
		output_data->batchs[i].trace_num = count;
		offset += count;
	}

	output_data->start_batch_id = input_data->start_batch_id;
	output_data->real_batch_num = i;
	output_data->remain_num = completed ? 0 : (input_data->batch_num - i);
	output_data->version = 0;

	cmd->output_size = sizeof(struct ras_cmd_batch_trace_record_rsp);

	return RAS_CMD__SUCCESS;
}

static enum ras_ta_block __get_ras_ta_block(enum ras_block_id block)
{
	switch (block) {
	case RAS_BLOCK_ID__UMC:
		return RAS_TA_BLOCK__UMC;
	case RAS_BLOCK_ID__SDMA:
		return RAS_TA_BLOCK__SDMA;
	case RAS_BLOCK_ID__GFX:
		return RAS_TA_BLOCK__GFX;
	case RAS_BLOCK_ID__MMHUB:
		return RAS_TA_BLOCK__MMHUB;
	case RAS_BLOCK_ID__ATHUB:
		return RAS_TA_BLOCK__ATHUB;
	case RAS_BLOCK_ID__PCIE_BIF:
		return RAS_TA_BLOCK__PCIE_BIF;
	case RAS_BLOCK_ID__HDP:
		return RAS_TA_BLOCK__HDP;
	case RAS_BLOCK_ID__XGMI_WAFL:
		return RAS_TA_BLOCK__XGMI_WAFL;
	case RAS_BLOCK_ID__DF:
		return RAS_TA_BLOCK__DF;
	case RAS_BLOCK_ID__SMN:
		return RAS_TA_BLOCK__SMN;
	case RAS_BLOCK_ID__SEM:
		return RAS_TA_BLOCK__SEM;
	case RAS_BLOCK_ID__MP0:
		return RAS_TA_BLOCK__MP0;
	case RAS_BLOCK_ID__MP1:
		return RAS_TA_BLOCK__MP1;
	case RAS_BLOCK_ID__FUSE:
		return RAS_TA_BLOCK__FUSE;
	case RAS_BLOCK_ID__MCA:
		return RAS_TA_BLOCK__MCA;
	case RAS_BLOCK_ID__VCN:
		return RAS_TA_BLOCK__VCN;
	case RAS_BLOCK_ID__JPEG:
		return RAS_TA_BLOCK__JPEG;
	default:
		return RAS_TA_BLOCK__UMC;
	}
}

static enum ras_ta_error_type __get_ras_ta_err_type(enum ras_ecc_err_type error)
{
	switch (error) {
	case RAS_ECC_ERR__NONE:
		return RAS_TA_ERROR__NONE;
	case RAS_ECC_ERR__PARITY:
		return RAS_TA_ERROR__PARITY;
	case RAS_ECC_ERR__SINGLE_CORRECTABLE:
		return RAS_TA_ERROR__SINGLE_CORRECTABLE;
	case RAS_ECC_ERR__MULTI_UNCORRECTABLE:
		return RAS_TA_ERROR__MULTI_UNCORRECTABLE;
	case RAS_ECC_ERR__POISON:
		return RAS_TA_ERROR__POISON;
	default:
		return RAS_TA_ERROR__NONE;
	}
}

static int ras_cmd_inject_error(struct ras_core_context *ras_core,
			struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_inject_error_req *req =
		(struct ras_cmd_inject_error_req *)cmd->input_buff_raw;
	struct ras_cmd_inject_error_rsp *output_data =
		(struct ras_cmd_inject_error_rsp *)cmd->output_buff_raw;
	int ret = 0;
	struct ras_ta_trigger_error_input block_info = {
		.block_id = __get_ras_ta_block(req->block_id),
		.sub_block_index = req->subblock_id,
		.inject_error_type = __get_ras_ta_err_type(req->error_type),
		.address = req->address,
		.value = req->method,
	};

	ret = ras_psp_trigger_error(ras_core, &block_info, req->instance_mask);
	if (!ret) {
		output_data->version = 0;
		output_data->address = block_info.address;
		cmd->output_size = sizeof(struct ras_cmd_inject_error_rsp);
	} else {
		RAS_DEV_ERR(ras_core->dev, "ras inject block %u failed %d\n", req->block_id, ret);
		ret = RAS_CMD__ERROR_ACCESS_DENIED;
	}

	return ret;
}

static struct ras_cmd_func_map ras_cmd_maps[] = {
	{RAS_CMD__INJECT_ERROR, ras_cmd_inject_error},
	{RAS_CMD__GET_BLOCK_ECC_STATUS, ras_get_block_ecc_info},
	{RAS_CMD__GET_BAD_PAGES, ras_cmd_get_bad_pages},
	{RAS_CMD__CLEAR_BAD_PAGE_INFO, ras_cmd_clear_bad_page_info},
	{RAS_CMD__RESET_ALL_ERROR_COUNTS, ras_cmd_reset_all_error_counts},
	{RAS_CMD__GET_CPER_SNAPSHOT, ras_cmd_get_cper_snapshot},
	{RAS_CMD__GET_CPER_RECORD, ras_cmd_get_cper_records},
	{RAS_CMD__GET_BATCH_TRACE_SNAPSHOT, ras_cmd_get_batch_trace_snapshot},
	{RAS_CMD__GET_BATCH_TRACE_RECORD, ras_cmd_get_batch_trace_records},
};

int rascore_handle_cmd(struct ras_core_context *ras_core,
		struct ras_cmd_ctx *cmd, void *data)
{
	struct ras_cmd_func_map *ras_cmd = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ras_cmd_maps); i++) {
		if (cmd->cmd_id == ras_cmd_maps[i].cmd_id) {
			ras_cmd = &ras_cmd_maps[i];
			break;
		}
	}

	if (!ras_cmd)
		return	RAS_CMD__ERROR_UKNOWN_CMD;

	return ras_cmd->func(ras_core, cmd, data);
}

int ras_cmd_init(struct ras_core_context *ras_core)
{
	return ras_cmd_add_device(ras_core);
}

int ras_cmd_fini(struct ras_core_context *ras_core)
{
	ras_cmd_remove_device(ras_core);
	return 0;
}

int ras_cmd_query_interface_info(struct ras_core_context *ras_core,
	struct ras_query_interface_info_rsp *rsp)
{
	rsp->ras_cmd_major_ver = RAS_CMD_MAJOR_VERSION;
	rsp->ras_cmd_minor_ver = RAS_CMD_MINOR_VERSION;

	return 0;
}

int ras_cmd_translate_soc_pa_to_bank(struct ras_core_context *ras_core,
	uint64_t soc_pa, struct ras_fb_bank_addr *bank_addr)
{
	struct umc_bank_addr  umc_bank = {0};
	int ret;

	ret = ras_umc_translate_soc_pa_and_bank(ras_core, &soc_pa, &umc_bank, false);
	if (ret)
		return RAS_CMD__ERROR_GENERIC;

	bank_addr->stack_id = umc_bank.stack_id;
	bank_addr->bank_group = umc_bank.bank_group;
	bank_addr->bank = umc_bank.bank;
	bank_addr->row = umc_bank.row;
	bank_addr->column = umc_bank.column;
	bank_addr->channel = umc_bank.channel;
	bank_addr->subchannel = umc_bank.subchannel;

	return 0;
}

int ras_cmd_translate_bank_to_soc_pa(struct ras_core_context *ras_core,
		struct ras_fb_bank_addr bank_addr, uint64_t *soc_pa)
{
	struct umc_bank_addr  umc_bank = {0};

	umc_bank.stack_id = bank_addr.stack_id;
	umc_bank.bank_group = bank_addr.bank_group;
	umc_bank.bank = bank_addr.bank;
	umc_bank.row = bank_addr.row;
	umc_bank.column = bank_addr.column;
	umc_bank.channel = bank_addr.channel;
	umc_bank.subchannel = bank_addr.subchannel;

	return ras_umc_translate_soc_pa_and_bank(ras_core, soc_pa, &umc_bank, true);
}

uint64_t ras_cmd_get_dev_handle(struct ras_core_context *ras_core)
{
	return ras_core->ras_cmd.dev_handle;
}
