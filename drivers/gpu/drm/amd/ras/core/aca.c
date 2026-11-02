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
#include "aca.h"
#include "ras_mce.h"
#include "aca_v1_0.h"
#include "aca_v5_0.h"
#include "ras_mp1_v13_0.h"

#define ACA_MARK_FATAL_FLAG    0x100
#define ACA_MARK_UE_READ_FLAG  0x1

#define blk_name(block_id) ras_core_get_ras_block_name(block_id)

static struct aca_regs_dump {
	const char *name;
	int reg_idx;
} aca_regs[] = {
	{"CONTROL",		ACA_REG_IDX__CTL},
	{"STATUS",		ACA_REG_IDX__STATUS},
	{"ADDR",		ACA_REG_IDX__ADDR},
	{"MISC",		ACA_REG_IDX__MISC0},
	{"CONFIG",		ACA_REG_IDX__CONFG},
	{"IPID",		ACA_REG_IDX__IPID},
	{"SYND",		ACA_REG_IDX__SYND},
	{"DESTAT",		ACA_REG_IDX__DESTAT},
	{"DEADDR",		ACA_REG_IDX__DEADDR},
	{"CONTROL_MASK",	ACA_REG_IDX__CTL_MASK},
};


static void aca_report_ecc_info(struct ras_core_context *ras_core,
				u64 seq_no, u32 blk, u32 skt, u32 aid,
				struct aca_aid_ecc *aid_ecc,
				struct aca_bank_ecc *new_ecc)
{
	struct aca_ecc_count ecc_count = {0};
	struct aca_ecc_count *xcd_ecc;
	int xcd_id;

	ecc_count.new_ue_count = new_ecc->ue_count;
	ecc_count.new_de_count = new_ecc->de_count;
	ecc_count.new_ce_count = new_ecc->ce_count;

	ecc_count.total_ue_count = aid_ecc->ecc_err.total_ue_count;
	ecc_count.total_de_count = aid_ecc->ecc_err.total_de_count;
	ecc_count.total_ce_count = aid_ecc->ecc_err.total_ce_count;

	if (blk == RAS_BLOCK_ID__GFX) {
		for (xcd_id = 0; xcd_id < aid_ecc->xcd.xcd_num; xcd_id++) {
			xcd_ecc = &aid_ecc->xcd.xcd[xcd_id].ecc_err;
			ecc_count.total_ue_count += xcd_ecc->total_ue_count;
			ecc_count.total_de_count += xcd_ecc->total_de_count;
			ecc_count.total_ce_count += xcd_ecc->total_ce_count;
		}
	}

	if (ecc_count.new_ue_count) {
		RAS_DEV_INFO(ras_core->dev,
		"{%llu} socket: %d, die: %d, %u new uncorrectable hardware errors detected in %s block\n",
			seq_no, skt, aid, ecc_count.new_ue_count, blk_name(blk));
		RAS_DEV_INFO(ras_core->dev,
		"{%llu} socket: %d, die: %d, %u uncorrectable hardware errors detected in total in %s block\n",
			seq_no, skt, aid, ecc_count.total_ue_count, blk_name(blk));
	}

	if (ecc_count.new_de_count && blk == RAS_BLOCK_ID__UMC) {
		RAS_DEV_INFO(ras_core->dev,
		"{%llu} socket: %d, die: %d, %u new deferred hardware errors detected in %s block\n",
			seq_no, skt, aid, ecc_count.new_de_count,
			blk_name(blk));
		RAS_DEV_INFO(ras_core->dev,
		"{%llu} socket: %d, die: %d, %u deferred hardware errors detected in total in %s block\n",
			seq_no, skt, aid, ecc_count.total_de_count,
			blk_name(blk));
	}

	if (ecc_count.new_ce_count) {
		if (ras_core_is_ce_log_disabled(ras_core))
			return;
		RAS_DEV_INFO(ras_core->dev,
		"{%llu} socket: %d, die: %d, %u new correctable hardware errors detected in %s block\n",
			seq_no, skt, aid, ecc_count.new_ce_count, blk_name(blk));
		RAS_DEV_INFO(ras_core->dev,
		"{%llu} socket: %d, die: %d, %u correctable hardware errors detected in total in %s block\n",
			seq_no, skt, aid, ecc_count.total_ce_count, blk_name(blk));
	}
}

static void aca_bank_log(struct ras_core_context *ras_core,
			 int idx, int total, struct aca_bank_reg *bank,
			 struct aca_bank_ecc *bank_ecc)
{
	int i;

	if (ras_core_is_ce_log_disabled(ras_core) &&
	   bank->ecc_type == RAS_ERR_TYPE__CE &&
	   !bank_ecc->de_count)
		return;

	RAS_DEV_INFO(ras_core->dev,
		"{%llu}" RAS_HW_ERR "Accelerator Check Architecture events logged\n",
		bank->seq_no);
	/* plus 1 for output format, e.g: ACA[08/08]: xxxx */
	for (i = 0; i < ARRAY_SIZE(aca_regs); i++)
		RAS_DEV_INFO(ras_core->dev,
			"{%llu}" RAS_HW_ERR "ACA[%02d/%02d].%s=0x%016llx\n",
			bank->seq_no, idx + 1, total,
			aca_regs[i].name, bank->regs[aca_regs[i].reg_idx]);

	if (ACA_REG_STATUS_SCRUB(bank->regs[ACA_REG_IDX__STATUS]))
		RAS_DEV_INFO(ras_core->dev,
			"{%llu}" RAS_HW_ERR "hardware error logged by the scrubber\n",
			bank->seq_no);
}

static void aca_log_bank_data(struct ras_core_context *ras_core,
			struct aca_bank_reg *bank, struct aca_bank_ecc *bank_ecc,
			struct ras_log_batch_tag *batch)
{
	enum ras_log_event event;

	if (batch && bank->timestamp)
		batch->timestamp = bank->timestamp;

	if (bank_ecc->ue_count)
		event = RAS_LOG_EVENT_UE;
	else if (bank_ecc->de_count)
		event = RAS_LOG_EVENT_DE;
	else
		event = RAS_LOG_EVENT_CE;

	ras_log_ring_add_log_event(ras_core,
		event, bank->regs, sizeof(bank->regs), batch);
}

static int aca_get_bank_count(struct ras_core_context *ras_core,
			      enum ras_err_type type, u32 *count)
{
	return (type == RAS_ERR_TYPE__MCE) ?
		ras_mce_get_bank_count(ras_core, type, count) :
		ras_mp1_get_bank_count(ras_core, type, count);
}

static bool aca_match_bank(struct aca_block *aca_blk, struct aca_bank_reg *bank)
{
	const struct aca_bank_hw_ops *bank_ops;

	if (!aca_blk->blk_info)
		return false;

	bank_ops = &aca_blk->blk_info->bank_ops;
	if (!bank_ops->bank_match)
		return false;

	return bank_ops->bank_match(aca_blk, bank);
}

static int aca_parse_bank(struct ras_core_context *ras_core,
			  struct aca_block *aca_blk,
			  struct aca_bank_reg *bank,
			  struct aca_bank_ecc *ecc)
{
	const struct aca_bank_hw_ops *bank_ops = &aca_blk->blk_info->bank_ops;

	if (!bank_ops || !bank_ops->bank_parse)
		return -RAS_CORE_NOT_SUPPORTED;

	ecc->block_id = aca_blk->blk_info->ras_block_id;
	return bank_ops->bank_parse(ras_core, aca_blk, bank, ecc);
}

static int aca_check_block_ecc_info(struct ras_core_context *ras_core,
			struct aca_block *aca_blk, struct aca_ecc_info *info)
{
	struct device_system_info dev_info = {0};
	int ret;

	ret = ras_core_get_device_system_info(ras_core, &dev_info);
	if (ret) {
		RAS_DEV_ERR(ras_core->dev, "Failed to get system device info.\n");
		return ret;
	}

	if (info->socket_id != dev_info.socket_id) {
		RAS_DEV_ERR(ras_core->dev,
			"Bank socket id (%d) does not match device socket id:%u\n",
			info->socket_id, dev_info.socket_id);
		return -ENXIO;
	}

	if (info->die_id >= aca_blk->ecc.socket.aid_num) {
		RAS_DEV_ERR(ras_core->dev,
			"Die id (%d) is out of config! max:%u\n",
			info->die_id, aca_blk->ecc.socket.aid_num);
		return -ENODATA;
	}

	if ((aca_blk->blk_info->ras_block_id == RAS_BLOCK_ID__GFX) && info->xcd_valid &&
	    (info->xcd_id >=
		 aca_blk->ecc.socket.aid[info->die_id].xcd.xcd_num)) {
		RAS_DEV_ERR(ras_core->dev,
			"Xcd id (%d) is out of config! max:%u\n",
			info->xcd_id,
			aca_blk->ecc.socket.aid[info->die_id].xcd.xcd_num);
		return -ENODATA;
	}

	return 0;
}

static int aca_log_bad_bank(struct ras_core_context *ras_core,
				 struct aca_block *aca_blk, struct aca_bank_reg *bank,
				 struct aca_bank_ecc *bank_ecc)
{
	struct aca_ecc_info *info;
	struct aca_ecc_count *ecc_err;
	struct aca_aid_ecc *aid_ecc;
	int ret;

	info = &bank_ecc->bank_info;

	ret = aca_check_block_ecc_info(ras_core, aca_blk, info);
	if (ret)
		return ret;

	mutex_lock(&ras_core->ras_aca.aca_lock);
	aid_ecc = &aca_blk->ecc.socket.aid[info->die_id];
	ecc_err = &aid_ecc->ecc_err;

	if ((aca_blk->blk_info->ras_block_id == RAS_BLOCK_ID__GFX) &&
		bank_ecc->bank_info.xcd_valid)
		ecc_err = &aid_ecc->xcd.xcd[info->xcd_id].ecc_err;

	ecc_err->new_ce_count += bank_ecc->ce_count;
	ecc_err->total_ce_count += bank_ecc->ce_count;
	ecc_err->new_ue_count += bank_ecc->ue_count;
	ecc_err->total_ue_count += bank_ecc->ue_count;
	ecc_err->new_de_count += bank_ecc->de_count;
	ecc_err->total_de_count += bank_ecc->de_count;
	mutex_unlock(&ras_core->ras_aca.aca_lock);

	/* Log ACA bad bank when FW management EEPROM is disabled. */
	if (!ras_eeprom_mgr_fw_record_enabled(ras_core) &&
	    (aca_blk->blk_info->ras_block_id == RAS_BLOCK_ID__UMC) &&
	    bank_ecc->de_count) {
		struct ras_bank_ecc  ras_ecc = {0};

		ras_ecc.nps = ras_core_get_curr_nps_mode(ras_core);
		ras_ecc.addr = bank_ecc->bank_info.addr;
		ras_ecc.ipid = bank_ecc->bank_info.ipid;
		ras_ecc.status = bank_ecc->bank_info.status;
		ras_ecc.seq_no = bank->seq_no;
		ras_ecc.timestamp = bank->timestamp;

		if (ras_core_gpu_in_reset(ras_core))
			ras_umc_log_bad_bank_pending(ras_core, &ras_ecc);
		else
			ras_umc_log_bad_bank(ras_core, &ras_ecc);
	}

	aca_report_ecc_info(ras_core,
		bank->seq_no, aca_blk->blk_info->ras_block_id, info->socket_id, info->die_id,
		&aca_blk->ecc.socket.aid[info->die_id], bank_ecc);

	return 0;
}

static struct aca_block *aca_get_bank_aca_block(struct ras_core_context *ras_core,
				struct aca_bank_reg *bank)
{
	int i = 0;

	for (i = 0; i < RAS_BLOCK_ID__LAST; i++)
		if (aca_match_bank(&ras_core->ras_aca.aca_blk[i], bank))
			return &ras_core->ras_aca.aca_blk[i];

	return NULL;
}

static int aca_dump_bank(struct ras_core_context *ras_core, u32 ecc_type,
			 int idx, void *data)
{
	struct aca_bank_reg *bank = (struct aca_bank_reg *)data;

	return (ecc_type == RAS_ERR_TYPE__MCE) ?
			ras_mce_dump_bank(ras_core, ecc_type, idx, bank) :
			ras_mp1_dump_bank(ras_core, ecc_type, idx,
				bank->regs, ARRAY_SIZE(bank->regs));
}

static uint64_t aca_get_bank_seqno(struct ras_core_context *ras_core,
				enum ras_err_type err_type, struct aca_block *aca_blk,
				struct aca_bank_ecc *bank_ecc)
{
	uint64_t seq_no = 0;

	if (bank_ecc->de_count) {
		if (aca_blk->blk_info->ras_block_id == RAS_BLOCK_ID__UMC)
			seq_no = ras_core_get_seqno(ras_core, RAS_SEQNO_TYPE_DE, true);
		else
			seq_no = ras_core_get_seqno(ras_core,
					RAS_SEQNO_TYPE_POISON_CONSUMPTION, true);
	} else if (bank_ecc->ue_count) {
		seq_no = ras_core_get_seqno(ras_core, RAS_SEQNO_TYPE_UE, true);
	} else {
		seq_no = ras_core_get_seqno(ras_core, RAS_SEQNO_TYPE_CE, true);
	}

	return seq_no;
}

static bool aca_dup_update_ue_in_fatal(struct ras_core_context *ras_core,
				u32 ecc_type)
{
	struct ras_aca *aca = &ras_core->ras_aca;

	if (ecc_type != RAS_ERR_TYPE__UE)
		return false;

	if (aca->ue_updated_mark & ACA_MARK_FATAL_FLAG) {
		if (aca->ue_updated_mark & ACA_MARK_UE_READ_FLAG)
			return true;

		aca->ue_updated_mark |= ACA_MARK_UE_READ_FLAG;
	}

	return false;
}

void ras_aca_mark_fatal_flag(struct ras_core_context *ras_core)
{
	struct ras_aca *aca = &ras_core->ras_aca;

	if (!aca)
		return;

	aca->ue_updated_mark |= ACA_MARK_FATAL_FLAG;
}

void ras_aca_clear_fatal_flag(struct ras_core_context *ras_core)
{
	struct ras_aca *aca = &ras_core->ras_aca;

	if (!aca)
		return;

	if ((aca->ue_updated_mark & ACA_MARK_FATAL_FLAG) &&
		(aca->ue_updated_mark & ACA_MARK_UE_READ_FLAG))
		aca->ue_updated_mark = 0;
}

static int aca_banks_update(struct ras_core_context *ras_core,
			u32 ecc_type, void *data)
{
	struct aca_bank_reg bank;
	struct aca_block *aca_blk;
	struct aca_bank_ecc bank_ecc;
	struct ras_log_batch_tag *batch_tag = NULL;
	u32 count = 0;
	int ret = 0;
	int i;

	mutex_lock(&ras_core->ras_aca.bank_op_lock);

	if (aca_dup_update_ue_in_fatal(ras_core, ecc_type))
		goto out;

	ret = aca_get_bank_count(ras_core, ecc_type, &count);
	if (ret)
		goto out;

	if (!count)
		goto out;

	for (i = 0; i < count; i++) {
		memset(&bank, 0, sizeof(bank));
		ret = aca_dump_bank(ras_core, ecc_type, i, &bank);
		if (ret)
			break;

		bank.ecc_type = ecc_type;

		memset(&bank_ecc, 0, sizeof(bank_ecc));
		aca_blk = aca_get_bank_aca_block(ras_core, &bank);

		/* Outside of UMC a poison bank is a consumption, not a new error. */
		if (ecc_type == RAS_ERR_TYPE__UE &&
		    ACA_REG_STATUS_POISON(bank.regs[ACA_REG_IDX__STATUS]) &&
		    (!aca_blk || aca_blk->blk_info->ras_block_id != RAS_BLOCK_ID__UMC))
			continue;

		if (aca_blk)
			ret = aca_parse_bank(ras_core, aca_blk, &bank, &bank_ecc);

		bank.seq_no = aca_get_bank_seqno(ras_core, ecc_type, aca_blk, &bank_ecc);

		/* Only one MCE error is logged for each batch */
		if (ecc_type != RAS_ERR_TYPE__MCE && !batch_tag)
			batch_tag = ras_log_ring_create_batch_tag(ras_core);

		aca_log_bank_data(ras_core, &bank, &bank_ecc, batch_tag);
		aca_bank_log(ras_core, i, count, &bank, &bank_ecc);

		if (!ret && aca_blk)
			ret = aca_log_bad_bank(ras_core, aca_blk, &bank, &bank_ecc);

		if (ret)
			break;
	}

	if (batch_tag)
		ras_log_ring_destroy_batch_tag(ras_core, batch_tag);

	if (!ret)
		ras_core_event_notify(ras_core,
			RAS_EVENT_ID__UPDATE_ACA_DATA, NULL);
out:
	mutex_unlock(&ras_core->ras_aca.bank_op_lock);
	return ret;
}

int ras_aca_update_ecc(struct ras_core_context *ras_core, u32 type, void *data)
{
	/* Update aca bank to aca source error_cache first */
	return aca_banks_update(ras_core, type, data);
}

static struct aca_block *ras_aca_get_block_handle(struct ras_core_context *ras_core, uint32_t blk)
{
	return &ras_core->ras_aca.aca_blk[blk];
}

static void __clear_block_socket_ecc_count(struct ras_core_context *ras_core,
			enum ras_block_id blk, struct aca_socket_ecc *socket)
{
	struct aca_aid_ecc  *aid_ecc;
	int aid, xcd;

	for (aid = 0; aid < socket->aid_num; aid++) {
		aid_ecc = &socket->aid[aid];
		memset(&aid_ecc->ecc_err, 0, sizeof(aid_ecc->ecc_err));
		if (blk == RAS_BLOCK_ID__GFX) {
			for (xcd = 0; xcd < aid_ecc->xcd.xcd_num; xcd++)
				memset(&aid_ecc->xcd.xcd[xcd],
						0, sizeof(struct aca_xcd_ecc));
		}
	}
}

int ras_aca_clear_all_blocks_ecc_count(struct ras_core_context *ras_core)
{
	struct aca_block *aca_blk;
	enum ras_block_id blk;

	mutex_lock(&ras_core->ras_aca.aca_lock);
	for (blk = RAS_BLOCK_ID__UMC; blk < RAS_BLOCK_ID__LAST; blk++) {
		aca_blk = ras_aca_get_block_handle(ras_core, blk);
		if (aca_blk)
			__clear_block_socket_ecc_count(ras_core,
						blk, &aca_blk->ecc.socket);
	}
	mutex_unlock(&ras_core->ras_aca.aca_lock);

	return 0;
}

int ras_aca_clear_block_new_ecc_count(struct ras_core_context *ras_core, u32 blk)
{
	struct aca_block *aca_blk;
	int aid, xcd;
	struct aca_ecc_count *ecc_err;
	struct aca_aid_ecc  *aid_ecc;

	mutex_lock(&ras_core->ras_aca.aca_lock);
	aca_blk = ras_aca_get_block_handle(ras_core, blk);
	for (aid = 0; aid < aca_blk->ecc.socket.aid_num; aid++) {
		aid_ecc = &aca_blk->ecc.socket.aid[aid];
		if (blk == RAS_BLOCK_ID__GFX) {
			for (xcd = 0; xcd < aid_ecc->xcd.xcd_num; xcd++) {
				ecc_err = &aid_ecc->xcd.xcd[xcd].ecc_err;
				ecc_err->new_ce_count = 0;
				ecc_err->new_ue_count = 0;
				ecc_err->new_de_count = 0;
			}
		}

		ecc_err = &aid_ecc->ecc_err;
		ecc_err->new_ce_count = 0;
		ecc_err->new_ue_count = 0;
		ecc_err->new_de_count = 0;
	}
	mutex_unlock(&ras_core->ras_aca.aca_lock);

	return 0;
}

static inline void _add_ecc_count(struct aca_ecc_count *des, struct aca_ecc_count *src)
{
	des->new_ce_count += src->new_ce_count;
	des->total_ce_count += src->total_ce_count;
	des->new_ue_count += src->new_ue_count;
	des->total_ue_count += src->total_ue_count;
	des->new_de_count += src->new_de_count;
	des->total_de_count += src->total_de_count;
}

static void __get_block_socket_ecc_count(struct ras_core_context *ras_core,
		u32 blk, struct aca_socket_ecc *socket, struct aca_ecc_count *ecc_count)
{
	struct aca_aid_ecc *aid_ecc;
	int aid, xcd;

	for (aid = 0; aid < socket->aid_num; aid++) {
		aid_ecc = &socket->aid[aid];

		/* Add AID error count */
		_add_ecc_count(ecc_count, &aid_ecc->ecc_err);
		if (blk == RAS_BLOCK_ID__GFX) {
			/* Add XCD error count */
			for (xcd = 0; xcd < aid_ecc->xcd.xcd_num; xcd++)
				_add_ecc_count(ecc_count, &aid_ecc->xcd.xcd[xcd].ecc_err);
		}
	}
}

static const struct ras_aca_ip_func *aca_get_ip_func(
				struct ras_core_context *ras_core, uint32_t ip_version)
{
	switch (ip_version) {
	case IP_VERSION(1, 0, 0):
		return &ras_aca_func_v1_0;
	case IP_VERSION(5, 0, 0):
		return &ras_aca_func_v5_0;
	default:
		RAS_DEV_ERR(ras_core->dev,
			"ACA ip version(0x%x) is not supported!\n", ip_version);
		break;
	}

	return NULL;
}

int ras_aca_get_block_ecc_count(struct ras_core_context *ras_core,
				u32 blk, void *data)
{
	struct ras_ecc_count *err_data = (struct ras_ecc_count *)data;
	struct aca_block *aca_blk;
	struct aca_ecc_count ecc;

	if (blk >= RAS_BLOCK_ID__LAST)
		return -EINVAL;

	if (!err_data)
		return -EINVAL;

	aca_blk = ras_aca_get_block_handle(ras_core, blk);
	memset(&ecc, 0, sizeof(ecc));

	mutex_lock(&ras_core->ras_aca.aca_lock);
	__get_block_socket_ecc_count(ras_core, blk, &aca_blk->ecc.socket, &ecc);

	err_data->new_ce_count = ecc.new_ce_count;
	err_data->total_ce_count = ecc.total_ce_count;
	err_data->new_ue_count = ecc.new_ue_count;
	err_data->total_ue_count = ecc.total_ue_count;
	err_data->new_de_count = ecc.new_de_count;
	err_data->total_de_count = ecc.total_de_count;
	mutex_unlock(&ras_core->ras_aca.aca_lock);

	return 0;
}

int ras_aca_sw_init(struct ras_core_context *ras_core)
{
	struct ras_aca *ras_aca = &ras_core->ras_aca;
	struct ras_aca_config *aca_cfg = &ras_core->config->aca_cfg;
	struct aca_block *aca_blk;
	uint32_t aid_num_per_socket;
	uint32_t xcd_num_per_aid;
	int blk, aid;

	aid_num_per_socket = aca_cfg->aid_num_per_socket;
	xcd_num_per_aid = aca_cfg->xcd_num_per_aid;

	if (!xcd_num_per_aid || !aid_num_per_socket ||
	    (aid_num_per_socket > MAX_AID_NUM_PER_SOCKET) ||
	    (xcd_num_per_aid > MAX_XCD_NUM_PER_AID)) {
		RAS_DEV_ERR(ras_core->dev, "Invalid ACA system configuration: %d, %d\n",
			aid_num_per_socket, xcd_num_per_aid);
		return -EINVAL;
	}

	memset(ras_aca, 0, sizeof(*ras_aca));

	ras_aca->aca_blk = kcalloc(RAS_BLOCK_ID__LAST, sizeof(struct aca_block), GFP_KERNEL);
	if (!ras_aca->aca_blk)
		return -ENOMEM;

	for (blk = 0; blk < RAS_BLOCK_ID__LAST; blk++) {
		aca_blk = &ras_aca->aca_blk[blk];
		aca_blk->ecc.socket.aid_num = aid_num_per_socket;
		if (blk == RAS_BLOCK_ID__GFX) {
			for (aid = 0; aid < aca_blk->ecc.socket.aid_num; aid++)
				aca_blk->ecc.socket.aid[aid].xcd.xcd_num =
							xcd_num_per_aid;
		}
	}

	mutex_init(&ras_aca->aca_lock);
	mutex_init(&ras_aca->bank_op_lock);

	return 0;
}

int ras_aca_sw_fini(struct ras_core_context *ras_core)
{
	struct ras_aca *ras_aca = &ras_core->ras_aca;

	kfree(ras_aca->aca_blk);
	mutex_destroy(&ras_aca->aca_lock);
	mutex_destroy(&ras_aca->bank_op_lock);

	return 0;
}

int ras_aca_hw_init(struct ras_core_context *ras_core)
{
	struct ras_aca *ras_aca = &ras_core->ras_aca;
	struct aca_block *aca_blk;
	const struct ras_aca_ip_func *ip_func;
	int i;

	ras_aca->aca_ip_version = ras_core->config->aca_ip_version;
	ip_func = aca_get_ip_func(ras_core, ras_aca->aca_ip_version);
	if (!ip_func)
		return -EINVAL;

	for (i = 0; i < ip_func->block_num; i++) {
		aca_blk = &ras_aca->aca_blk[ip_func->block_info[i]->ras_block_id];
		aca_blk->blk_info = ip_func->block_info[i];
	}

	ras_aca->ip_func = ip_func;
	ras_aca->ue_updated_mark = 0;

	return 0;
}

int ras_aca_hw_fini(struct ras_core_context *ras_core)
{
	struct ras_aca *ras_aca = &ras_core->ras_aca;

	ras_aca->ue_updated_mark = 0;

	return 0;
}

int ras_aca_parse_bank(struct ras_core_context *ras_core,
				struct aca_bank_reg *bank,
				struct aca_bank_ecc *ecc)
{
	struct aca_block *aca_blk;
	int ret = -ENODATA;

	if (!ras_core || !bank || !ecc)
		return -EINVAL;

	aca_blk = aca_get_bank_aca_block(ras_core, bank);
	if (aca_blk)
		ret = aca_parse_bank(ras_core, aca_blk, bank, ecc);

	return ret;
}

u64 ras_aca_get_parser_caps(struct ras_core_context *ras_core)
{
	struct ras_aca *ras_aca = &ras_core->ras_aca;

	if (ras_aca->ip_func && ras_aca->ip_func->aca_parse_ras_caps)
		return ras_aca->ip_func->aca_parse_ras_caps(ras_core);

	return 0;
}

int ras_aca_fill_rma_bank(struct ras_core_context *ras_core,
			struct aca_bank_reg *bank)
{
	struct ras_aca *ras_aca = &ras_core->ras_aca;

	if (!bank)
		return -EINVAL;

	if (ras_aca->ip_func && ras_aca->ip_func->fill_rma_bank)
		return ras_aca->ip_func->fill_rma_bank(ras_core, bank);

	return -EOPNOTSUPP;
}
