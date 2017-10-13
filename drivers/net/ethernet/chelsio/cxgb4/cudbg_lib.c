/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#include "t4_regs.h"
#include "cxgb4.h"
#include "cudbg_if.h"
#include "cudbg_lib_common.h"
#include "cudbg_lib.h"
#include "cudbg_entity.h"

static void cudbg_write_and_release_buff(struct cudbg_buffer *pin_buff,
					 struct cudbg_buffer *dbg_buff)
{
	cudbg_update_buff(pin_buff, dbg_buff);
	cudbg_put_buff(pin_buff, dbg_buff);
}

static int is_fw_attached(struct cudbg_init *pdbg_init)
{
	struct adapter *padap = pdbg_init->adap;

	if (!(padap->flags & FW_OK) || padap->use_bd)
		return 0;

	return 1;
}

/* This function will add additional padding bytes into debug_buffer to make it
 * 4 byte aligned.
 */
void cudbg_align_debug_buffer(struct cudbg_buffer *dbg_buff,
			      struct cudbg_entity_hdr *entity_hdr)
{
	u8 zero_buf[4] = {0};
	u8 padding, remain;

	remain = (dbg_buff->offset - entity_hdr->start_offset) % 4;
	padding = 4 - remain;
	if (remain) {
		memcpy(((u8 *)dbg_buff->data) + dbg_buff->offset, &zero_buf,
		       padding);
		dbg_buff->offset += padding;
		entity_hdr->num_pad = padding;
	}
	entity_hdr->size = dbg_buff->offset - entity_hdr->start_offset;
}

struct cudbg_entity_hdr *cudbg_get_entity_hdr(void *outbuf, int i)
{
	struct cudbg_hdr *cudbg_hdr = (struct cudbg_hdr *)outbuf;

	return (struct cudbg_entity_hdr *)
	       ((char *)outbuf + cudbg_hdr->hdr_len +
		(sizeof(struct cudbg_entity_hdr) * (i - 1)));
}

int cudbg_collect_reg_dump(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	u32 buf_size = 0;
	int rc = 0;

	if (is_t4(padap->params.chip))
		buf_size = T4_REGMAP_SIZE;
	else if (is_t5(padap->params.chip) || is_t6(padap->params.chip))
		buf_size = T5_REGMAP_SIZE;

	rc = cudbg_get_buff(dbg_buff, buf_size, &temp_buff);
	if (rc)
		return rc;
	t4_get_regs(padap, (void *)temp_buff.data, temp_buff.size);
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

int cudbg_collect_fw_devlog(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	struct devlog_params *dparams;
	int rc = 0;

	rc = t4_init_devlog_params(padap);
	if (rc < 0) {
		cudbg_err->sys_err = rc;
		return rc;
	}

	dparams = &padap->params.devlog;
	rc = cudbg_get_buff(dbg_buff, dparams->size, &temp_buff);
	if (rc)
		return rc;

	/* Collect FW devlog */
	if (dparams->start != 0) {
		spin_lock(&padap->win0_lock);
		rc = t4_memory_rw(padap, padap->params.drv_memwin,
				  dparams->memtype, dparams->start,
				  dparams->size,
				  (__be32 *)(char *)temp_buff.data,
				  1);
		spin_unlock(&padap->win0_lock);
		if (rc) {
			cudbg_err->sys_err = rc;
			cudbg_put_buff(&temp_buff, dbg_buff);
			return rc;
		}
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}

static int cudbg_read_fw_mem(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff, u8 mem_type,
			     unsigned long tot_len,
			     struct cudbg_error *cudbg_err)
{
	unsigned long bytes, bytes_left, bytes_read = 0;
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_buffer temp_buff = { 0 };
	int rc = 0;

	bytes_left = tot_len;
	while (bytes_left > 0) {
		bytes = min_t(unsigned long, bytes_left,
			      (unsigned long)CUDBG_CHUNK_SIZE);
		rc = cudbg_get_buff(dbg_buff, bytes, &temp_buff);
		if (rc)
			return rc;
		spin_lock(&padap->win0_lock);
		rc = t4_memory_rw(padap, MEMWIN_NIC, mem_type,
				  bytes_read, bytes,
				  (__be32 *)temp_buff.data,
				  1);
		spin_unlock(&padap->win0_lock);
		if (rc) {
			cudbg_err->sys_err = rc;
			cudbg_put_buff(&temp_buff, dbg_buff);
			return rc;
		}
		bytes_left -= bytes;
		bytes_read += bytes;
		cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	}
	return rc;
}

static void cudbg_collect_mem_info(struct cudbg_init *pdbg_init,
				   struct card_mem *mem_info)
{
	struct adapter *padap = pdbg_init->adap;
	u32 value;

	value = t4_read_reg(padap, MA_EDRAM0_BAR_A);
	value = EDRAM0_SIZE_G(value);
	mem_info->size_edc0 = (u16)value;

	value = t4_read_reg(padap, MA_EDRAM1_BAR_A);
	value = EDRAM1_SIZE_G(value);
	mem_info->size_edc1 = (u16)value;

	value = t4_read_reg(padap, MA_TARGET_MEM_ENABLE_A);
	if (value & EDRAM0_ENABLE_F)
		mem_info->mem_flag |= (1 << EDC0_FLAG);
	if (value & EDRAM1_ENABLE_F)
		mem_info->mem_flag |= (1 << EDC1_FLAG);
}

static void cudbg_t4_fwcache(struct cudbg_init *pdbg_init,
			     struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	int rc;

	if (is_fw_attached(pdbg_init)) {
		/* Flush uP dcache before reading edcX/mcX  */
		rc = t4_fwcache(padap, FW_PARAM_DEV_FWCACHE_FLUSH);
		if (rc)
			cudbg_err->sys_warn = rc;
	}
}

static int cudbg_collect_mem_region(struct cudbg_init *pdbg_init,
				    struct cudbg_buffer *dbg_buff,
				    struct cudbg_error *cudbg_err,
				    u8 mem_type)
{
	struct card_mem mem_info = {0};
	unsigned long flag, size;
	int rc;

	cudbg_t4_fwcache(pdbg_init, cudbg_err);
	cudbg_collect_mem_info(pdbg_init, &mem_info);
	switch (mem_type) {
	case MEM_EDC0:
		flag = (1 << EDC0_FLAG);
		size = cudbg_mbytes_to_bytes(mem_info.size_edc0);
		break;
	case MEM_EDC1:
		flag = (1 << EDC1_FLAG);
		size = cudbg_mbytes_to_bytes(mem_info.size_edc1);
		break;
	default:
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}

	if (mem_info.mem_flag & flag) {
		rc = cudbg_read_fw_mem(pdbg_init, dbg_buff, mem_type,
				       size, cudbg_err);
		if (rc)
			goto err;
	} else {
		rc = CUDBG_STATUS_ENTITY_NOT_FOUND;
		goto err;
	}
err:
	return rc;
}

int cudbg_collect_edc0_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_collect_mem_region(pdbg_init, dbg_buff, cudbg_err,
					MEM_EDC0);
}

int cudbg_collect_edc1_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err)
{
	return cudbg_collect_mem_region(pdbg_init, dbg_buff, cudbg_err,
					MEM_EDC1);
}

int cudbg_collect_mbox_log(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err)
{
	struct adapter *padap = pdbg_init->adap;
	struct cudbg_mbox_log *mboxlog = NULL;
	struct cudbg_buffer temp_buff = { 0 };
	struct mbox_cmd_log *log = NULL;
	struct mbox_cmd *entry;
	unsigned int entry_idx;
	u16 mbox_cmds;
	int i, k, rc;
	u64 flit;
	u32 size;

	log = padap->mbox_log;
	mbox_cmds = padap->mbox_log->size;
	size = sizeof(struct cudbg_mbox_log) * mbox_cmds;
	rc = cudbg_get_buff(dbg_buff, size, &temp_buff);
	if (rc)
		return rc;

	mboxlog = (struct cudbg_mbox_log *)temp_buff.data;
	for (k = 0; k < mbox_cmds; k++) {
		entry_idx = log->cursor + k;
		if (entry_idx >= log->size)
			entry_idx -= log->size;

		entry = mbox_cmd_log_entry(log, entry_idx);
		/* skip over unused entries */
		if (entry->timestamp == 0)
			continue;

		memcpy(&mboxlog->entry, entry, sizeof(struct mbox_cmd));
		for (i = 0; i < MBOX_LEN / 8; i++) {
			flit = entry->cmd[i];
			mboxlog->hi[i] = (u32)(flit >> 32);
			mboxlog->lo[i] = (u32)flit;
		}
		mboxlog++;
	}
	cudbg_write_and_release_buff(&temp_buff, dbg_buff);
	return rc;
}
