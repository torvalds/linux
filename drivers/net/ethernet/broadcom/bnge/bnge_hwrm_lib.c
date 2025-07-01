// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include "bnge.h"
#include "../bnxt/bnxt_hsi.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"
#include "bnge_rmem.h"

int bnge_hwrm_ver_get(struct bnge_dev *bd)
{
	u32 dev_caps_cfg, hwrm_ver, hwrm_spec_code;
	u16 fw_maj, fw_min, fw_bld, fw_rsv;
	struct hwrm_ver_get_output *resp;
	struct hwrm_ver_get_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VER_GET);
	if (rc)
		return rc;

	bnge_hwrm_req_flags(bd, req, BNGE_HWRM_FULL_WAIT);
	bd->hwrm_max_req_len = HWRM_MAX_REQ_LEN;
	req->hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req->hwrm_intf_min = HWRM_VERSION_MINOR;
	req->hwrm_intf_upd = HWRM_VERSION_UPDATE;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		goto hwrm_ver_get_exit;

	memcpy(&bd->ver_resp, resp, sizeof(struct hwrm_ver_get_output));

	hwrm_spec_code = resp->hwrm_intf_maj_8b << 16 |
			 resp->hwrm_intf_min_8b << 8 |
			 resp->hwrm_intf_upd_8b;
	hwrm_ver = HWRM_VERSION_MAJOR << 16 | HWRM_VERSION_MINOR << 8 |
			HWRM_VERSION_UPDATE;

	if (hwrm_spec_code > hwrm_ver)
		snprintf(bd->hwrm_ver_supp, FW_VER_STR_LEN, "%d.%d.%d",
			 HWRM_VERSION_MAJOR, HWRM_VERSION_MINOR,
			 HWRM_VERSION_UPDATE);
	else
		snprintf(bd->hwrm_ver_supp, FW_VER_STR_LEN, "%d.%d.%d",
			 resp->hwrm_intf_maj_8b, resp->hwrm_intf_min_8b,
			 resp->hwrm_intf_upd_8b);

	fw_maj = le16_to_cpu(resp->hwrm_fw_major);
	fw_min = le16_to_cpu(resp->hwrm_fw_minor);
	fw_bld = le16_to_cpu(resp->hwrm_fw_build);
	fw_rsv = le16_to_cpu(resp->hwrm_fw_patch);

	bd->fw_ver_code = BNGE_FW_VER_CODE(fw_maj, fw_min, fw_bld, fw_rsv);
	snprintf(bd->fw_ver_str, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 fw_maj, fw_min, fw_bld, fw_rsv);

	if (strlen(resp->active_pkg_name)) {
		int fw_ver_len = strlen(bd->fw_ver_str);

		snprintf(bd->fw_ver_str + fw_ver_len,
			 FW_VER_STR_LEN - fw_ver_len - 1, "/pkg %s",
			 resp->active_pkg_name);
		bd->fw_cap |= BNGE_FW_CAP_PKG_VER;
	}

	bd->hwrm_cmd_timeout = le16_to_cpu(resp->def_req_timeout);
	if (!bd->hwrm_cmd_timeout)
		bd->hwrm_cmd_timeout = BNGE_DFLT_HWRM_CMD_TIMEOUT;
	bd->hwrm_cmd_max_timeout = le16_to_cpu(resp->max_req_timeout) * 1000;
	if (!bd->hwrm_cmd_max_timeout)
		bd->hwrm_cmd_max_timeout = BNGE_HWRM_CMD_MAX_TIMEOUT;
	else if (bd->hwrm_cmd_max_timeout > BNGE_HWRM_CMD_MAX_TIMEOUT)
		dev_warn(bd->dev, "Default HWRM commands max timeout increased to %d seconds\n",
			 bd->hwrm_cmd_max_timeout / 1000);

	bd->hwrm_max_req_len = le16_to_cpu(resp->max_req_win_len);
	bd->hwrm_max_ext_req_len = le16_to_cpu(resp->max_ext_req_len);

	if (bd->hwrm_max_ext_req_len < HWRM_MAX_REQ_LEN)
		bd->hwrm_max_ext_req_len = HWRM_MAX_REQ_LEN;

	bd->chip_num = le16_to_cpu(resp->chip_num);
	bd->chip_rev = resp->chip_rev;

	dev_caps_cfg = le32_to_cpu(resp->dev_caps_cfg);
	if ((dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_SHORT_CMD_SUPPORTED) &&
	    (dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_SHORT_CMD_REQUIRED))
		bd->fw_cap |= BNGE_FW_CAP_SHORT_CMD;

	if (dev_caps_cfg & VER_GET_RESP_DEV_CAPS_CFG_KONG_MB_CHNL_SUPPORTED)
		bd->fw_cap |= BNGE_FW_CAP_KONG_MB_CHNL;

	if (dev_caps_cfg &
	    VER_GET_RESP_DEV_CAPS_CFG_CFA_ADV_FLOW_MGNT_SUPPORTED)
		bd->fw_cap |= BNGE_FW_CAP_CFA_ADV_FLOW;

hwrm_ver_get_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int
bnge_hwrm_nvm_dev_info(struct bnge_dev *bd,
		       struct hwrm_nvm_get_dev_info_output *nvm_info)
{
	struct hwrm_nvm_get_dev_info_output *resp;
	struct hwrm_nvm_get_dev_info_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_NVM_GET_DEV_INFO);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc)
		memcpy(nvm_info, resp, sizeof(*resp));
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_func_reset(struct bnge_dev *bd)
{
	struct hwrm_func_reset_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_RESET);
	if (rc)
		return rc;

	req->enables = 0;
	bnge_hwrm_req_timeout(bd, req, BNGE_HWRM_RESET_TIMEOUT);
	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_fw_set_time(struct bnge_dev *bd)
{
	struct hwrm_fw_set_time_input *req;
	struct tm tm;
	int rc;

	time64_to_tm(ktime_get_real_seconds(), 0, &tm);

	rc = bnge_hwrm_req_init(bd, req, HWRM_FW_SET_TIME);
	if (rc)
		return rc;

	req->year = cpu_to_le16(1900 + tm.tm_year);
	req->month = 1 + tm.tm_mon;
	req->day = tm.tm_mday;
	req->hour = tm.tm_hour;
	req->minute = tm.tm_min;
	req->second = tm.tm_sec;
	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_func_drv_rgtr(struct bnge_dev *bd)
{
	struct hwrm_func_drv_rgtr_output *resp;
	struct hwrm_func_drv_rgtr_input *req;
	u32 flags;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_DRV_RGTR);
	if (rc)
		return rc;

	req->enables = cpu_to_le32(FUNC_DRV_RGTR_REQ_ENABLES_OS_TYPE |
				   FUNC_DRV_RGTR_REQ_ENABLES_VER |
				   FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD);

	req->os_type = cpu_to_le16(FUNC_DRV_RGTR_REQ_OS_TYPE_LINUX);
	flags = FUNC_DRV_RGTR_REQ_FLAGS_16BIT_VER_MODE;

	req->flags = cpu_to_le32(flags);
	req->ver_maj_8b = DRV_VER_MAJ;
	req->ver_min_8b = DRV_VER_MIN;
	req->ver_upd_8b = DRV_VER_UPD;
	req->ver_maj = cpu_to_le16(DRV_VER_MAJ);
	req->ver_min = cpu_to_le16(DRV_VER_MIN);
	req->ver_upd = cpu_to_le16(DRV_VER_UPD);

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc) {
		set_bit(BNGE_STATE_DRV_REGISTERED, &bd->state);
		if (resp->flags &
		    cpu_to_le32(FUNC_DRV_RGTR_RESP_FLAGS_IF_CHANGE_SUPPORTED))
			bd->fw_cap |= BNGE_FW_CAP_IF_CHANGE;
	}
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_func_drv_unrgtr(struct bnge_dev *bd)
{
	struct hwrm_func_drv_unrgtr_input *req;
	int rc;

	if (!test_and_clear_bit(BNGE_STATE_DRV_REGISTERED, &bd->state))
		return 0;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_DRV_UNRGTR);
	if (rc)
		return rc;
	return bnge_hwrm_req_send(bd, req);
}

static void bnge_init_ctx_initializer(struct bnge_ctx_mem_type *ctxm,
				      u8 init_val, u8 init_offset,
				      bool init_mask_set)
{
	ctxm->init_value = init_val;
	ctxm->init_offset = BNGE_CTX_INIT_INVALID_OFFSET;
	if (init_mask_set)
		ctxm->init_offset = init_offset * 4;
	else
		ctxm->init_value = 0;
}

static int bnge_alloc_all_ctx_pg_info(struct bnge_dev *bd, int ctx_max)
{
	struct bnge_ctx_mem_info *ctx = bd->ctx;
	u16 type;

	for (type = 0; type < ctx_max; type++) {
		struct bnge_ctx_mem_type *ctxm = &ctx->ctx_arr[type];
		int n = 1;

		if (!ctxm->max_entries)
			continue;

		if (ctxm->instance_bmap)
			n = hweight32(ctxm->instance_bmap);
		ctxm->pg_info = kcalloc(n, sizeof(*ctxm->pg_info), GFP_KERNEL);
		if (!ctxm->pg_info)
			return -ENOMEM;
	}

	return 0;
}

#define BNGE_CTX_INIT_VALID(flags)	\
	(!!((flags) &			\
	    FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_ENABLE_CTX_KIND_INIT))

int bnge_hwrm_func_backing_store_qcaps(struct bnge_dev *bd)
{
	struct hwrm_func_backing_store_qcaps_v2_output *resp;
	struct hwrm_func_backing_store_qcaps_v2_input *req;
	struct bnge_ctx_mem_info *ctx;
	u16 type;
	int rc;

	if (bd->ctx)
		return 0;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_BACKING_STORE_QCAPS_V2);
	if (rc)
		return rc;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	bd->ctx = ctx;

	resp = bnge_hwrm_req_hold(bd, req);

	for (type = 0; type < BNGE_CTX_V2_MAX; ) {
		struct bnge_ctx_mem_type *ctxm = &ctx->ctx_arr[type];
		u8 init_val, init_off, i;
		__le32 *p;
		u32 flags;

		req->type = cpu_to_le16(type);
		rc = bnge_hwrm_req_send(bd, req);
		if (rc)
			goto ctx_done;
		flags = le32_to_cpu(resp->flags);
		type = le16_to_cpu(resp->next_valid_type);
		if (!(flags &
		      FUNC_BACKING_STORE_QCAPS_V2_RESP_FLAGS_TYPE_VALID))
			continue;

		ctxm->type = le16_to_cpu(resp->type);
		ctxm->entry_size = le16_to_cpu(resp->entry_size);
		ctxm->flags = flags;
		ctxm->instance_bmap = le32_to_cpu(resp->instance_bit_map);
		ctxm->entry_multiple = resp->entry_multiple;
		ctxm->max_entries = le32_to_cpu(resp->max_num_entries);
		ctxm->min_entries = le32_to_cpu(resp->min_num_entries);
		init_val = resp->ctx_init_value;
		init_off = resp->ctx_init_offset;
		bnge_init_ctx_initializer(ctxm, init_val, init_off,
					  BNGE_CTX_INIT_VALID(flags));
		ctxm->split_entry_cnt = min_t(u8, resp->subtype_valid_cnt,
					      BNGE_MAX_SPLIT_ENTRY);
		for (i = 0, p = &resp->split_entry_0; i < ctxm->split_entry_cnt;
		     i++, p++)
			ctxm->split[i] = le32_to_cpu(*p);
	}
	rc = bnge_alloc_all_ctx_pg_info(bd, BNGE_CTX_V2_MAX);

ctx_done:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

static void bnge_hwrm_set_pg_attr(struct bnge_ring_mem_info *rmem, u8 *pg_attr,
				  __le64 *pg_dir)
{
	if (!rmem->nr_pages)
		return;

	BNGE_SET_CTX_PAGE_ATTR(*pg_attr);
	if (rmem->depth >= 1) {
		if (rmem->depth == 2)
			*pg_attr |= 2;
		else
			*pg_attr |= 1;
		*pg_dir = cpu_to_le64(rmem->dma_pg_tbl);
	} else {
		*pg_dir = cpu_to_le64(rmem->dma_arr[0]);
	}
}

int bnge_hwrm_func_backing_store(struct bnge_dev *bd,
				 struct bnge_ctx_mem_type *ctxm,
				 bool last)
{
	struct hwrm_func_backing_store_cfg_v2_input *req;
	u32 instance_bmap = ctxm->instance_bmap;
	int i, j, rc = 0, n = 1;
	__le32 *p;

	if (!(ctxm->flags & BNGE_CTX_MEM_TYPE_VALID) || !ctxm->pg_info)
		return 0;

	if (instance_bmap)
		n = hweight32(ctxm->instance_bmap);
	else
		instance_bmap = 1;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_BACKING_STORE_CFG_V2);
	if (rc)
		return rc;
	bnge_hwrm_req_hold(bd, req);
	req->type = cpu_to_le16(ctxm->type);
	req->entry_size = cpu_to_le16(ctxm->entry_size);
	req->subtype_valid_cnt = ctxm->split_entry_cnt;
	for (i = 0, p = &req->split_entry_0; i < ctxm->split_entry_cnt; i++)
		p[i] = cpu_to_le32(ctxm->split[i]);
	for (i = 0, j = 0; j < n && !rc; i++) {
		struct bnge_ctx_pg_info *ctx_pg;

		if (!(instance_bmap & (1 << i)))
			continue;
		req->instance = cpu_to_le16(i);
		ctx_pg = &ctxm->pg_info[j++];
		if (!ctx_pg->entries)
			continue;
		req->num_entries = cpu_to_le32(ctx_pg->entries);
		bnge_hwrm_set_pg_attr(&ctx_pg->ring_mem,
				      &req->page_size_pbl_level,
				      &req->page_dir);
		if (last && j == n)
			req->flags =
				cpu_to_le32(BNGE_BS_CFG_ALL_DONE);
		rc = bnge_hwrm_req_send(bd, req);
	}
	bnge_hwrm_req_drop(bd, req);

	return rc;
}
