// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/bnxt/hsi.h>
#include <linux/if_vlan.h>
#include <net/netdev_queues.h>

#include "bnge.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"
#include "bnge_rmem.h"
#include "bnge_resc.h"

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

static int bnge_hwrm_get_rings(struct bnge_dev *bd)
{
	struct bnge_hw_resc *hw_resc = &bd->hw_resc;
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	u16 cp, stats;
	u16 rx, tx;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_QCFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc) {
		bnge_hwrm_req_drop(bd, req);
		return rc;
	}

	hw_resc->resv_tx_rings = le16_to_cpu(resp->alloc_tx_rings);
	hw_resc->resv_rx_rings = le16_to_cpu(resp->alloc_rx_rings);
	hw_resc->resv_hw_ring_grps =
		le32_to_cpu(resp->alloc_hw_ring_grps);
	hw_resc->resv_vnics = le16_to_cpu(resp->alloc_vnics);
	hw_resc->resv_rsscos_ctxs = le16_to_cpu(resp->alloc_rsscos_ctx);
	cp = le16_to_cpu(resp->alloc_cmpl_rings);
	stats = le16_to_cpu(resp->alloc_stat_ctx);
	hw_resc->resv_irqs = cp;
	rx = hw_resc->resv_rx_rings;
	tx = hw_resc->resv_tx_rings;
	if (bnge_is_agg_reqd(bd))
		rx >>= 1;
	if (cp < (rx + tx)) {
		rc = bnge_fix_rings_count(&rx, &tx, cp, false);
		if (rc)
			goto get_rings_exit;
		if (bnge_is_agg_reqd(bd))
			rx <<= 1;
		hw_resc->resv_rx_rings = rx;
		hw_resc->resv_tx_rings = tx;
	}
	hw_resc->resv_irqs = le16_to_cpu(resp->alloc_msix);
	hw_resc->resv_hw_ring_grps = rx;
	hw_resc->resv_cp_rings = cp;
	hw_resc->resv_stat_ctxs = stats;

get_rings_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

static struct hwrm_func_cfg_input *
__bnge_hwrm_reserve_pf_rings(struct bnge_dev *bd, struct bnge_hw_rings *hwr)
{
	struct hwrm_func_cfg_input *req;
	u32 enables = 0;

	if (bnge_hwrm_req_init(bd, req, HWRM_FUNC_QCFG))
		return NULL;

	req->fid = cpu_to_le16(0xffff);
	enables |= hwr->tx ? FUNC_CFG_REQ_ENABLES_NUM_TX_RINGS : 0;
	req->num_tx_rings = cpu_to_le16(hwr->tx);

	enables |= hwr->rx ? FUNC_CFG_REQ_ENABLES_NUM_RX_RINGS : 0;
	enables |= hwr->stat ? FUNC_CFG_REQ_ENABLES_NUM_STAT_CTXS : 0;
	enables |= hwr->nq ? FUNC_CFG_REQ_ENABLES_NUM_MSIX : 0;
	enables |= hwr->cmpl ? FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS : 0;
	enables |= hwr->vnic ? FUNC_CFG_REQ_ENABLES_NUM_VNICS : 0;
	enables |= hwr->rss_ctx ? FUNC_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS : 0;

	req->num_rx_rings = cpu_to_le16(hwr->rx);
	req->num_rsscos_ctxs = cpu_to_le16(hwr->rss_ctx);
	req->num_cmpl_rings = cpu_to_le16(hwr->cmpl);
	req->num_msix = cpu_to_le16(hwr->nq);
	req->num_stat_ctxs = cpu_to_le16(hwr->stat);
	req->num_vnics = cpu_to_le16(hwr->vnic);
	req->enables = cpu_to_le32(enables);

	return req;
}

static int
bnge_hwrm_reserve_pf_rings(struct bnge_dev *bd, struct bnge_hw_rings *hwr)
{
	struct hwrm_func_cfg_input *req;
	int rc;

	req = __bnge_hwrm_reserve_pf_rings(bd, hwr);
	if (!req)
		return -ENOMEM;

	if (!req->enables) {
		bnge_hwrm_req_drop(bd, req);
		return 0;
	}

	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		return rc;

	return bnge_hwrm_get_rings(bd);
}

int bnge_hwrm_reserve_rings(struct bnge_dev *bd, struct bnge_hw_rings *hwr)
{
	return bnge_hwrm_reserve_pf_rings(bd, hwr);
}

int bnge_hwrm_func_qcfg(struct bnge_dev *bd)
{
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_QCFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		goto func_qcfg_exit;

	bd->max_mtu = le16_to_cpu(resp->max_mtu_configured);
	if (!bd->max_mtu)
		bd->max_mtu = BNGE_MAX_MTU;

	if (bd->db_size)
		goto func_qcfg_exit;

	bd->db_offset = le16_to_cpu(resp->legacy_l2_db_size_kb) * 1024;
	bd->db_size = PAGE_ALIGN(le16_to_cpu(resp->l2_doorbell_bar_size_kb) *
			1024);
	if (!bd->db_size || bd->db_size > pci_resource_len(bd->pdev, 2) ||
	    bd->db_size <= bd->db_offset)
		bd->db_size = pci_resource_len(bd->pdev, 2);

func_qcfg_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_func_resc_qcaps(struct bnge_dev *bd)
{
	struct hwrm_func_resource_qcaps_output *resp;
	struct bnge_hw_resc *hw_resc = &bd->hw_resc;
	struct hwrm_func_resource_qcaps_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_RESOURCE_QCAPS);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send_silent(bd, req);
	if (rc)
		goto hwrm_func_resc_qcaps_exit;

	hw_resc->max_tx_sch_inputs = le16_to_cpu(resp->max_tx_scheduler_inputs);
	hw_resc->min_rsscos_ctxs = le16_to_cpu(resp->min_rsscos_ctx);
	hw_resc->max_rsscos_ctxs = le16_to_cpu(resp->max_rsscos_ctx);
	hw_resc->min_cp_rings = le16_to_cpu(resp->min_cmpl_rings);
	hw_resc->max_cp_rings = le16_to_cpu(resp->max_cmpl_rings);
	hw_resc->min_tx_rings = le16_to_cpu(resp->min_tx_rings);
	hw_resc->max_tx_rings = le16_to_cpu(resp->max_tx_rings);
	hw_resc->min_rx_rings = le16_to_cpu(resp->min_rx_rings);
	hw_resc->max_rx_rings = le16_to_cpu(resp->max_rx_rings);
	hw_resc->min_hw_ring_grps = le16_to_cpu(resp->min_hw_ring_grps);
	hw_resc->max_hw_ring_grps = le16_to_cpu(resp->max_hw_ring_grps);
	hw_resc->min_l2_ctxs = le16_to_cpu(resp->min_l2_ctxs);
	hw_resc->max_l2_ctxs = le16_to_cpu(resp->max_l2_ctxs);
	hw_resc->min_vnics = le16_to_cpu(resp->min_vnics);
	hw_resc->max_vnics = le16_to_cpu(resp->max_vnics);
	hw_resc->min_stat_ctxs = le16_to_cpu(resp->min_stat_ctx);
	hw_resc->max_stat_ctxs = le16_to_cpu(resp->max_stat_ctx);

	hw_resc->max_nqs = le16_to_cpu(resp->max_msix);
	hw_resc->max_hw_ring_grps = hw_resc->max_rx_rings;

hwrm_func_resc_qcaps_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_func_qcaps(struct bnge_dev *bd)
{
	struct hwrm_func_qcaps_output *resp;
	struct hwrm_func_qcaps_input *req;
	struct bnge_pf_info *pf = &bd->pf;
	u32 flags;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_QCAPS);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		goto hwrm_func_qcaps_exit;

	flags = le32_to_cpu(resp->flags);
	if (flags & FUNC_QCAPS_RESP_FLAGS_ROCE_V1_SUPPORTED)
		bd->flags |= BNGE_EN_ROCE_V1;
	if (flags & FUNC_QCAPS_RESP_FLAGS_ROCE_V2_SUPPORTED)
		bd->flags |= BNGE_EN_ROCE_V2;

	pf->fw_fid = le16_to_cpu(resp->fid);
	pf->port_id = le16_to_cpu(resp->port_id);
	memcpy(pf->mac_addr, resp->mac_address, ETH_ALEN);

	bd->tso_max_segs = le16_to_cpu(resp->max_tso_segs);

hwrm_func_qcaps_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_vnic_qcaps(struct bnge_dev *bd)
{
	struct hwrm_vnic_qcaps_output *resp;
	struct hwrm_vnic_qcaps_input *req;
	int rc;

	bd->hw_ring_stats_size = sizeof(struct ctx_hw_stats);
	bd->rss_cap &= ~BNGE_RSS_CAP_NEW_RSS_CAP;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_QCAPS);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc) {
		u32 flags = le32_to_cpu(resp->flags);

		if (flags & VNIC_QCAPS_RESP_FLAGS_VLAN_STRIP_CAP)
			bd->fw_cap |= BNGE_FW_CAP_VLAN_RX_STRIP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_HASH_TYPE_DELTA_CAP)
			bd->rss_cap |= BNGE_RSS_CAP_RSS_HASH_TYPE_DELTA;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_PROF_TCAM_MODE_ENABLED)
			bd->rss_cap |= BNGE_RSS_CAP_RSS_TCAM;
		bd->max_tpa_v2 = le16_to_cpu(resp->max_aggs_supported);
		if (bd->max_tpa_v2)
			bd->hw_ring_stats_size = BNGE_RING_STATS_SIZE;
		if (flags & VNIC_QCAPS_RESP_FLAGS_HW_TUNNEL_TPA_CAP)
			bd->fw_cap |= BNGE_FW_CAP_VNIC_TUNNEL_TPA;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_AH_SPI_IPV4_CAP)
			bd->rss_cap |= BNGE_RSS_CAP_AH_V4_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_AH_SPI_IPV6_CAP)
			bd->rss_cap |= BNGE_RSS_CAP_AH_V6_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_ESP_SPI_IPV4_CAP)
			bd->rss_cap |= BNGE_RSS_CAP_ESP_V4_RSS_CAP;
		if (flags & VNIC_QCAPS_RESP_FLAGS_RSS_IPSEC_ESP_SPI_IPV6_CAP)
			bd->rss_cap |= BNGE_RSS_CAP_ESP_V6_RSS_CAP;
	}
	bnge_hwrm_req_drop(bd, req);

	return rc;
}

#define BNGE_CNPQ(q_profile)	\
		((q_profile) ==	\
		 QUEUE_QPORTCFG_RESP_QUEUE_ID0_SERVICE_PROFILE_LOSSY_ROCE_CNP)

int bnge_hwrm_queue_qportcfg(struct bnge_dev *bd)
{
	struct hwrm_queue_qportcfg_output *resp;
	struct hwrm_queue_qportcfg_input *req;
	u8 i, j, *qptr;
	bool no_rdma;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_QUEUE_QPORTCFG);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		goto qportcfg_exit;

	if (!resp->max_configurable_queues) {
		rc = -EINVAL;
		goto qportcfg_exit;
	}
	bd->max_tc = resp->max_configurable_queues;
	bd->max_lltc = resp->max_configurable_lossless_queues;
	if (bd->max_tc > BNGE_MAX_QUEUE)
		bd->max_tc = BNGE_MAX_QUEUE;

	no_rdma = !bnge_is_roce_en(bd);
	qptr = &resp->queue_id0;
	for (i = 0, j = 0; i < bd->max_tc; i++) {
		bd->q_info[j].queue_id = *qptr;
		bd->q_ids[i] = *qptr++;
		bd->q_info[j].queue_profile = *qptr++;
		bd->tc_to_qidx[j] = j;
		if (!BNGE_CNPQ(bd->q_info[j].queue_profile) || no_rdma)
			j++;
	}
	bd->max_q = bd->max_tc;
	bd->max_tc = max_t(u8, j, 1);

	if (resp->queue_cfg_info & QUEUE_QPORTCFG_RESP_QUEUE_CFG_INFO_ASYM_CFG)
		bd->max_tc = 1;

	if (bd->max_lltc > bd->max_tc)
		bd->max_lltc = bd->max_tc;

qportcfg_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_vnic_set_hds(struct bnge_net *bn, struct bnge_vnic_info *vnic)
{
	u16 hds_thresh = (u16)bn->netdev->cfg_pending->hds_thresh;
	struct hwrm_vnic_plcmodes_cfg_input *req;
	struct bnge_dev *bd = bn->bd;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_PLCMODES_CFG);
	if (rc)
		return rc;

	req->flags = cpu_to_le32(VNIC_PLCMODES_CFG_REQ_FLAGS_JUMBO_PLACEMENT);
	req->enables = cpu_to_le32(BNGE_PLC_EN_JUMBO_THRES_VALID);
	req->jumbo_thresh = cpu_to_le16(bn->rx_buf_use_size);

	if (bnge_is_agg_reqd(bd)) {
		req->flags |= cpu_to_le32(VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV4 |
					  VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV6);
		req->enables |=
			cpu_to_le32(BNGE_PLC_EN_HDS_THRES_VALID);
		req->hds_threshold = cpu_to_le16(hds_thresh);
	}
	req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);
	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_vnic_ctx_alloc(struct bnge_dev *bd,
			     struct bnge_vnic_info *vnic, u16 ctx_idx)
{
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_output *resp;
	struct hwrm_vnic_rss_cos_lb_ctx_alloc_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_RSS_COS_LB_CTX_ALLOC);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc)
		vnic->fw_rss_cos_lb_ctx[ctx_idx] =
			le16_to_cpu(resp->rss_cos_lb_ctx_id);
	bnge_hwrm_req_drop(bd, req);

	return rc;
}

static void
__bnge_hwrm_vnic_set_rss(struct bnge_net *bn,
			 struct hwrm_vnic_rss_cfg_input *req,
			 struct bnge_vnic_info *vnic)
{
	struct bnge_dev *bd = bn->bd;

	bnge_fill_hw_rss_tbl(bn, vnic);
	req->flags |= VNIC_RSS_CFG_REQ_FLAGS_IPSEC_HASH_TYPE_CFG_SUPPORT;

	req->hash_type = cpu_to_le32(bd->rss_hash_cfg);
	req->hash_mode_flags = VNIC_RSS_CFG_REQ_HASH_MODE_FLAGS_DEFAULT;
	req->ring_grp_tbl_addr = cpu_to_le64(vnic->rss_table_dma_addr);
	req->hash_key_tbl_addr = cpu_to_le64(vnic->rss_hash_key_dma_addr);
}

int bnge_hwrm_vnic_set_rss(struct bnge_net *bn,
			   struct bnge_vnic_info *vnic, bool set_rss)
{
	struct hwrm_vnic_rss_cfg_input *req;
	struct bnge_dev *bd = bn->bd;
	dma_addr_t ring_tbl_map;
	u32 i, nr_ctxs;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_RSS_CFG);
	if (rc)
		return rc;

	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	if (!set_rss)
		return bnge_hwrm_req_send(bd, req);

	__bnge_hwrm_vnic_set_rss(bn, req, vnic);
	ring_tbl_map = vnic->rss_table_dma_addr;
	nr_ctxs = bnge_cal_nr_rss_ctxs(bd->rx_nr_rings);

	bnge_hwrm_req_hold(bd, req);
	for (i = 0; i < nr_ctxs; ring_tbl_map += BNGE_RSS_TABLE_SIZE, i++) {
		req->ring_grp_tbl_addr = cpu_to_le64(ring_tbl_map);
		req->ring_table_pair_index = i;
		req->rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[i]);
		rc = bnge_hwrm_req_send(bd, req);
		if (rc)
			goto exit;
	}

exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_vnic_cfg(struct bnge_net *bn, struct bnge_vnic_info *vnic)
{
	struct bnge_rx_ring_info *rxr = &bn->rx_ring[0];
	struct hwrm_vnic_cfg_input *req;
	struct bnge_dev *bd = bn->bd;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_CFG);
	if (rc)
		return rc;

	req->default_rx_ring_id =
		cpu_to_le16(rxr->rx_ring_struct.fw_ring_id);
	req->default_cmpl_ring_id =
		cpu_to_le16(bnge_cp_ring_for_rx(rxr));
	req->enables =
		cpu_to_le32(VNIC_CFG_REQ_ENABLES_DEFAULT_RX_RING_ID |
			    VNIC_CFG_REQ_ENABLES_DEFAULT_CMPL_RING_ID);
	vnic->mru = bd->netdev->mtu + ETH_HLEN + VLAN_HLEN;
	req->mru = cpu_to_le16(vnic->mru);

	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);

	if (bd->flags & BNGE_EN_STRIP_VLAN)
		req->flags |= cpu_to_le32(VNIC_CFG_REQ_FLAGS_VLAN_STRIP_MODE);
	if (vnic->vnic_id == BNGE_VNIC_DEFAULT && bnge_aux_registered(bd))
		req->flags |= cpu_to_le32(BNGE_VNIC_CFG_ROCE_DUAL_MODE);

	return bnge_hwrm_req_send(bd, req);
}

void bnge_hwrm_update_rss_hash_cfg(struct bnge_net *bn)
{
	struct bnge_vnic_info *vnic = &bn->vnic_info[BNGE_VNIC_DEFAULT];
	struct hwrm_vnic_rss_qcfg_output *resp;
	struct hwrm_vnic_rss_qcfg_input *req;
	struct bnge_dev *bd = bn->bd;

	if (bnge_hwrm_req_init(bd, req, HWRM_VNIC_RSS_QCFG))
		return;

	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);
	/* all contexts configured to same hash_type, zero always exists */
	req->rss_ctx_idx = cpu_to_le16(vnic->fw_rss_cos_lb_ctx[0]);
	resp = bnge_hwrm_req_hold(bd, req);
	if (!bnge_hwrm_req_send(bd, req))
		bd->rss_hash_cfg =
			le32_to_cpu(resp->hash_type) ?: bd->rss_hash_cfg;
	bnge_hwrm_req_drop(bd, req);
}

int bnge_hwrm_l2_filter_free(struct bnge_dev *bd, struct bnge_l2_filter *fltr)
{
	struct hwrm_cfa_l2_filter_free_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_CFA_L2_FILTER_FREE);
	if (rc)
		return rc;

	req->l2_filter_id = fltr->base.filter_id;
	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_l2_filter_alloc(struct bnge_dev *bd, struct bnge_l2_filter *fltr)
{
	struct hwrm_cfa_l2_filter_alloc_output *resp;
	struct hwrm_cfa_l2_filter_alloc_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_CFA_L2_FILTER_ALLOC);
	if (rc)
		return rc;

	req->flags = cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_RX);

	req->flags |= cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_FLAGS_OUTERMOST);
	req->dst_id = cpu_to_le16(fltr->base.fw_vnic_id);
	req->enables =
		cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_DST_ID |
			    CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR_MASK);
	ether_addr_copy(req->l2_addr, fltr->l2_key.dst_mac_addr);
	eth_broadcast_addr(req->l2_addr_mask);

	if (fltr->l2_key.vlan) {
		req->enables |=
			cpu_to_le32(CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN |
				CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN_MASK |
				CFA_L2_FILTER_ALLOC_REQ_ENABLES_NUM_VLANS);
		req->num_vlans = 1;
		req->l2_ivlan = cpu_to_le16(fltr->l2_key.vlan);
		req->l2_ivlan_mask = cpu_to_le16(0xfff);
	}

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc)
		fltr->base.filter_id = resp->l2_filter_id;

	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_cfa_l2_set_rx_mask(struct bnge_dev *bd,
				 struct bnge_vnic_info *vnic)
{
	struct hwrm_cfa_l2_set_rx_mask_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_CFA_L2_SET_RX_MASK);
	if (rc)
		return rc;

	req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);
	if (vnic->rx_mask & CFA_L2_SET_RX_MASK_REQ_MASK_MCAST) {
		req->num_mc_entries = cpu_to_le32(vnic->mc_list_count);
		req->mc_tbl_addr = cpu_to_le64(vnic->mc_list_mapping);
	}
	req->mask = cpu_to_le32(vnic->rx_mask);
	return bnge_hwrm_req_send_silent(bd, req);
}

int bnge_hwrm_vnic_alloc(struct bnge_dev *bd, struct bnge_vnic_info *vnic,
			 unsigned int nr_rings)
{
	struct hwrm_vnic_alloc_output *resp;
	struct hwrm_vnic_alloc_input *req;
	unsigned int i;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_ALLOC);
	if (rc)
		return rc;

	for (i = 0; i < BNGE_MAX_CTX_PER_VNIC; i++)
		vnic->fw_rss_cos_lb_ctx[i] = INVALID_HW_RING_ID;
	if (vnic->vnic_id == BNGE_VNIC_DEFAULT)
		req->flags = cpu_to_le32(VNIC_ALLOC_REQ_FLAGS_DEFAULT);

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc)
		vnic->fw_vnic_id = le32_to_cpu(resp->vnic_id);
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

void bnge_hwrm_vnic_free_one(struct bnge_dev *bd, struct bnge_vnic_info *vnic)
{
	if (vnic->fw_vnic_id != INVALID_HW_RING_ID) {
		struct hwrm_vnic_free_input *req;

		if (bnge_hwrm_req_init(bd, req, HWRM_VNIC_FREE))
			return;

		req->vnic_id = cpu_to_le32(vnic->fw_vnic_id);

		bnge_hwrm_req_send(bd, req);
		vnic->fw_vnic_id = INVALID_HW_RING_ID;
	}
}

void bnge_hwrm_vnic_ctx_free_one(struct bnge_dev *bd,
				 struct bnge_vnic_info *vnic, u16 ctx_idx)
{
	struct hwrm_vnic_rss_cos_lb_ctx_free_input *req;

	if (bnge_hwrm_req_init(bd, req, HWRM_VNIC_RSS_COS_LB_CTX_FREE))
		return;

	req->rss_cos_lb_ctx_id =
		cpu_to_le16(vnic->fw_rss_cos_lb_ctx[ctx_idx]);

	bnge_hwrm_req_send(bd, req);
	vnic->fw_rss_cos_lb_ctx[ctx_idx] = INVALID_HW_RING_ID;
}

void bnge_hwrm_stat_ctx_free(struct bnge_net *bn)
{
	struct hwrm_stat_ctx_free_input *req;
	struct bnge_dev *bd = bn->bd;
	int i;

	if (bnge_hwrm_req_init(bd, req, HWRM_STAT_CTX_FREE))
		return;

	bnge_hwrm_req_hold(bd, req);
	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;

		if (nqr->hw_stats_ctx_id != INVALID_STATS_CTX_ID) {
			req->stat_ctx_id = cpu_to_le32(nqr->hw_stats_ctx_id);
			bnge_hwrm_req_send(bd, req);

			nqr->hw_stats_ctx_id = INVALID_STATS_CTX_ID;
		}
	}
	bnge_hwrm_req_drop(bd, req);
}

int bnge_hwrm_stat_ctx_alloc(struct bnge_net *bn)
{
	struct hwrm_stat_ctx_alloc_output *resp;
	struct hwrm_stat_ctx_alloc_input *req;
	struct bnge_dev *bd = bn->bd;
	int rc, i;

	rc = bnge_hwrm_req_init(bd, req, HWRM_STAT_CTX_ALLOC);
	if (rc)
		return rc;

	req->stats_dma_length = cpu_to_le16(bd->hw_ring_stats_size);
	req->update_period_ms = cpu_to_le32(bn->stats_coal_ticks / 1000);

	resp = bnge_hwrm_req_hold(bd, req);
	for (i = 0; i < bd->nq_nr_rings; i++) {
		struct bnge_napi *bnapi = bn->bnapi[i];
		struct bnge_nq_ring_info *nqr = &bnapi->nq_ring;

		req->stats_dma_addr = cpu_to_le64(nqr->stats.hw_stats_map);

		rc = bnge_hwrm_req_send(bd, req);
		if (rc)
			break;

		nqr->hw_stats_ctx_id = le32_to_cpu(resp->stat_ctx_id);
		bn->grp_info[i].fw_stats_ctx = nqr->hw_stats_ctx_id;
	}
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int hwrm_ring_free_send_msg(struct bnge_net *bn,
			    struct bnge_ring_struct *ring,
			    u32 ring_type, int cmpl_ring_id)
{
	struct hwrm_ring_free_input *req;
	struct bnge_dev *bd = bn->bd;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_RING_FREE);
	if (rc)
		goto exit;

	req->cmpl_ring = cpu_to_le16(cmpl_ring_id);
	req->ring_type = ring_type;
	req->ring_id = cpu_to_le16(ring->fw_ring_id);

	bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	bnge_hwrm_req_drop(bd, req);
exit:
	if (rc) {
		netdev_err(bd->netdev, "hwrm_ring_free type %d failed. rc:%d\n", ring_type, rc);
		return -EIO;
	}
	return 0;
}

int hwrm_ring_alloc_send_msg(struct bnge_net *bn,
			     struct bnge_ring_struct *ring,
			     u32 ring_type, u32 map_index)
{
	struct bnge_ring_mem_info *rmem = &ring->ring_mem;
	struct bnge_ring_grp_info *grp_info;
	struct hwrm_ring_alloc_output *resp;
	struct hwrm_ring_alloc_input *req;
	struct bnge_dev *bd = bn->bd;
	u16 ring_id, flags = 0;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_RING_ALLOC);
	if (rc)
		goto exit;

	req->enables = 0;
	if (rmem->nr_pages > 1) {
		req->page_tbl_addr = cpu_to_le64(rmem->dma_pg_tbl);
		/* Page size is in log2 units */
		req->page_size = BNGE_PAGE_SHIFT;
		req->page_tbl_depth = 1;
	} else {
		req->page_tbl_addr =  cpu_to_le64(rmem->dma_arr[0]);
	}
	req->fbo = 0;
	/* Association of ring index with doorbell index and MSIX number */
	req->logical_id = cpu_to_le16(map_index);

	switch (ring_type) {
	case HWRM_RING_ALLOC_TX: {
		struct bnge_tx_ring_info *txr;

		txr = container_of(ring, struct bnge_tx_ring_info,
				   tx_ring_struct);
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_TX;
		/* Association of transmit ring with completion ring */
		grp_info = &bn->grp_info[ring->grp_idx];
		req->cmpl_ring_id = cpu_to_le16(bnge_cp_ring_for_tx(txr));
		req->length = cpu_to_le32(bn->tx_ring_mask + 1);
		req->stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
		req->queue_id = cpu_to_le16(ring->queue_id);
		req->flags = cpu_to_le16(flags);
		break;
	}
	case HWRM_RING_ALLOC_RX:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_RX;
		req->length = cpu_to_le32(bn->rx_ring_mask + 1);

		/* Association of rx ring with stats context */
		grp_info = &bn->grp_info[ring->grp_idx];
		req->rx_buf_size = cpu_to_le16(bn->rx_buf_use_size);
		req->stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
		req->enables |=
			cpu_to_le32(RING_ALLOC_REQ_ENABLES_RX_BUF_SIZE_VALID);
		if (NET_IP_ALIGN == 2)
			flags = RING_ALLOC_REQ_FLAGS_RX_SOP_PAD;
		req->flags = cpu_to_le16(flags);
		break;
	case HWRM_RING_ALLOC_AGG:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_RX_AGG;
		/* Association of agg ring with rx ring */
		grp_info = &bn->grp_info[ring->grp_idx];
		req->rx_ring_id = cpu_to_le16(grp_info->rx_fw_ring_id);
		req->rx_buf_size = cpu_to_le16(BNGE_RX_PAGE_SIZE);
		req->stat_ctx_id = cpu_to_le32(grp_info->fw_stats_ctx);
		req->enables |=
			cpu_to_le32(RING_ALLOC_REQ_ENABLES_RX_RING_ID_VALID |
				    RING_ALLOC_REQ_ENABLES_RX_BUF_SIZE_VALID);
		req->length = cpu_to_le32(bn->rx_agg_ring_mask + 1);
		break;
	case HWRM_RING_ALLOC_CMPL:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_L2_CMPL;
		req->length = cpu_to_le32(bn->cp_ring_mask + 1);
		/* Association of cp ring with nq */
		grp_info = &bn->grp_info[map_index];
		req->nq_ring_id = cpu_to_le16(grp_info->nq_fw_ring_id);
		req->cq_handle = cpu_to_le64(ring->handle);
		req->enables |=
			cpu_to_le32(RING_ALLOC_REQ_ENABLES_NQ_RING_ID_VALID);
		break;
	case HWRM_RING_ALLOC_NQ:
		req->ring_type = RING_ALLOC_REQ_RING_TYPE_NQ;
		req->length = cpu_to_le32(bn->cp_ring_mask + 1);
		req->int_mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		break;
	default:
		netdev_err(bn->netdev, "hwrm alloc invalid ring type %d\n", ring_type);
		return -EINVAL;
	}

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	ring_id = le16_to_cpu(resp->ring_id);
	bnge_hwrm_req_drop(bd, req);

exit:
	if (rc) {
		netdev_err(bd->netdev, "hwrm_ring_alloc type %d failed. rc:%d\n", ring_type, rc);
		return -EIO;
	}
	ring->fw_ring_id = ring_id;
	return rc;
}

int bnge_hwrm_set_async_event_cr(struct bnge_dev *bd, int idx)
{
	struct hwrm_func_cfg_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_CFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	req->enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_ASYNC_EVENT_CR);
	req->async_event_cr = cpu_to_le16(idx);
	return bnge_hwrm_req_send(bd, req);
}
