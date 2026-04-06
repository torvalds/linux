// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/bnge/hsi.h>
#include <linux/if_vlan.h>
#include <net/netdev_queues.h>

#include "bnge.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"
#include "bnge_rmem.h"
#include "bnge_resc.h"
#include "bnge_netdev.h"

static const u16 bnge_async_events_arr[] = {
	ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE,
	ASYNC_EVENT_CMPL_EVENT_ID_PORT_PHY_CFG_CHANGE,
};

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
	DECLARE_BITMAP(async_events_bmap, 256);
	struct hwrm_func_drv_rgtr_output *resp;
	struct hwrm_func_drv_rgtr_input *req;
	u32 events[8];
	u32 flags;
	int rc, i;

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

	memset(async_events_bmap, 0, sizeof(async_events_bmap));
	for (i = 0; i < ARRAY_SIZE(bnge_async_events_arr); i++)
		__set_bit(bnge_async_events_arr[i], async_events_bmap);

	bitmap_to_arr32(events, async_events_bmap, 256);
	for (i = 0; i < ARRAY_SIZE(req->async_event_fwd); i++)
		req->async_event_fwd[i] |= cpu_to_le32(events[i]);

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
		ctxm->pg_info = kzalloc_objs(*ctxm->pg_info, n);
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

	ctx = kzalloc_obj(*ctx);
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

	if (bnge_hwrm_req_init(bd, req, HWRM_FUNC_CFG))
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
	u32 flags, flags_ext;
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
	if (flags & FUNC_QCAPS_RESP_FLAGS_EXT_STATS_SUPPORTED)
		bd->fw_cap |= BNGE_FW_CAP_EXT_STATS_SUPPORTED;

	flags_ext = le32_to_cpu(resp->flags_ext);
	if (flags_ext & FUNC_QCAPS_RESP_FLAGS_EXT_EXT_HW_STATS_SUPPORTED)
		bd->fw_cap |= BNGE_FW_CAP_EXT_HW_STATS_SUPPORTED;

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

static bool bnge_phy_qcaps_no_speed(struct hwrm_port_phy_qcaps_output *resp)
{
	return !resp->supported_speeds2_auto_mode &&
	       !resp->supported_speeds2_force_mode;
}

int bnge_hwrm_phy_qcaps(struct bnge_dev *bd)
{
	struct bnge_link_info *link_info = &bd->link_info;
	struct hwrm_port_phy_qcaps_output *resp;
	struct hwrm_port_phy_qcaps_input *req;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_PORT_PHY_QCAPS);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc)
		goto hwrm_phy_qcaps_exit;

	bd->phy_flags = resp->flags |
		       (le16_to_cpu(resp->flags2) << BNGE_PHY_FLAGS2_SHIFT);

	if (bnge_phy_qcaps_no_speed(resp)) {
		link_info->phy_enabled = false;
		netdev_warn(bd->netdev, "Ethernet link disabled\n");
	} else if (!link_info->phy_enabled) {
		link_info->phy_enabled = true;
		netdev_info(bd->netdev, "Ethernet link enabled\n");
		/* Phy re-enabled, reprobe the speeds */
		link_info->support_auto_speeds2 = 0;
	}

	/* Firmware may report 0 for autoneg supported speeds when no
	 * SFP module is present. Skip the update to preserve the
	 * current supported speeds -- storing 0 would cause autoneg
	 * default fallback to advertise nothing.
	 */
	if (resp->supported_speeds2_auto_mode)
		link_info->support_auto_speeds2 =
			le16_to_cpu(resp->supported_speeds2_auto_mode);

	bd->port_count = resp->port_cnt;

hwrm_phy_qcaps_exit:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_set_link_setting(struct bnge_net *bn, bool set_pause)
{
	struct hwrm_port_phy_cfg_input *req;
	struct bnge_dev *bd = bn->bd;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	if (set_pause)
		bnge_hwrm_set_pause_common(bn, req);

	bnge_hwrm_set_link_common(bn, req);

	rc = bnge_hwrm_req_send(bd, req);
	if (!rc)
		bn->eth_link_info.force_link_chng = false;

	return rc;
}

int bnge_update_link(struct bnge_net *bn, bool chng_link_state)
{
	struct hwrm_port_phy_qcfg_output *resp;
	struct hwrm_port_phy_qcfg_input *req;
	struct bnge_link_info *link_info;
	struct bnge_dev *bd = bn->bd;
	bool support_changed;
	u8 link_state;
	int rc;

	link_info = &bd->link_info;
	link_state = link_info->link_state;

	rc = bnge_hwrm_req_init(bd, req, HWRM_PORT_PHY_QCFG);
	if (rc)
		return rc;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (rc) {
		bnge_hwrm_req_drop(bd, req);
		return rc;
	}

	memcpy(&link_info->phy_qcfg_resp, resp, sizeof(*resp));
	link_info->phy_link_status = resp->link;
	link_info->duplex = resp->duplex_state;
	link_info->pause = resp->pause;
	link_info->auto_mode = resp->auto_mode;
	link_info->auto_pause_setting = resp->auto_pause;
	link_info->lp_pause = resp->link_partner_adv_pause;
	link_info->force_pause_setting = resp->force_pause;
	link_info->duplex_setting = resp->duplex_cfg;
	if (link_info->phy_link_status == BNGE_LINK_LINK) {
		link_info->link_speed = le16_to_cpu(resp->link_speed);
		link_info->active_lanes = resp->active_lanes;
	} else {
		link_info->link_speed = 0;
		link_info->active_lanes = 0;
	}
	link_info->force_link_speed2 = le16_to_cpu(resp->force_link_speeds2);
	link_info->support_speeds2 = le16_to_cpu(resp->support_speeds2);
	link_info->auto_link_speeds2 = le16_to_cpu(resp->auto_link_speeds2);
	link_info->lp_auto_link_speeds =
		le16_to_cpu(resp->link_partner_adv_speeds);
	link_info->media_type = resp->media_type;
	link_info->phy_type = resp->phy_type;
	link_info->phy_addr = resp->eee_config_phy_addr &
			      PORT_PHY_QCFG_RESP_PHY_ADDR_MASK;
	link_info->module_status = resp->module_status;

	link_info->fec_cfg = le16_to_cpu(resp->fec_cfg);
	link_info->active_fec_sig_mode = resp->active_fec_signal_mode;

	if (chng_link_state) {
		if (link_info->phy_link_status == BNGE_LINK_LINK)
			link_info->link_state = BNGE_LINK_STATE_UP;
		else
			link_info->link_state = BNGE_LINK_STATE_DOWN;
		if (link_state != link_info->link_state)
			bnge_report_link(bd);
	} else {
		/* always link down if not required to update link state */
		link_info->link_state = BNGE_LINK_STATE_DOWN;
	}
	bnge_hwrm_req_drop(bd, req);

	if (!BNGE_PHY_CFG_ABLE(bd))
		return 0;

	support_changed = bnge_support_speed_dropped(bn);
	if (support_changed && (bn->eth_link_info.autoneg & BNGE_AUTONEG_SPEED))
		rc = bnge_hwrm_set_link_setting(bn, true);
	return rc;
}

int bnge_hwrm_set_pause(struct bnge_net *bn)
{
	struct bnge_ethtool_link_info *elink_info = &bn->eth_link_info;
	struct hwrm_port_phy_cfg_input *req;
	struct bnge_dev *bd = bn->bd;
	bool pause_autoneg;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	pause_autoneg = !!(elink_info->autoneg & BNGE_AUTONEG_FLOW_CTRL);

	/* Prepare PHY pause-advertisement or forced-pause settings. */
	bnge_hwrm_set_pause_common(bn, req);

	/* Prepare speed/autoneg settings */
	if (pause_autoneg || elink_info->force_link_chng)
		bnge_hwrm_set_link_common(bn, req);

	rc = bnge_hwrm_req_send(bd, req);
	if (!rc && !pause_autoneg) {
		/* Since changing of pause setting, with pause autoneg off,
		 * doesn't trigger any link change event, the driver needs to
		 * update the current MAC pause upon successful return of the
		 * phy_cfg command.
		 */
		bd->link_info.force_pause_setting =
		bd->link_info.pause = elink_info->req_flow_ctrl;
		bd->link_info.auto_pause_setting = 0;
		if (!elink_info->force_link_chng)
			bnge_report_link(bd);
	}
	if (!rc)
		elink_info->force_link_chng = false;

	return rc;
}

int bnge_hwrm_shutdown_link(struct bnge_dev *bd)
{
	struct hwrm_port_phy_cfg_input *req;
	int rc;

	if (!BNGE_PHY_CFG_ABLE(bd))
		return 0;

	rc = bnge_hwrm_req_init(bd, req, HWRM_PORT_PHY_CFG);
	if (rc)
		return rc;

	req->flags = cpu_to_le32(PORT_PHY_CFG_REQ_FLAGS_FORCE_LINK_DWN);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc) {
		/* Device is not obliged to link down in certain scenarios,
		 * even when forced. Setting the state unknown is consistent
		 * with driver startup and will force link state to be
		 * reported during subsequent open based on PORT_PHY_QCFG.
		 */
		bd->link_info.link_state = BNGE_LINK_STATE_UNKNOWN;
	}
	return rc;
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

#define BNGE_DFLT_TUNL_TPA_BMAP				\
	(VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_GRE |	\
	 VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_IPV4 |	\
	 VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_IPV6)

static void bnge_hwrm_vnic_update_tunl_tpa(struct bnge_dev *bd,
					   struct hwrm_vnic_tpa_cfg_input *req)
{
	struct bnge_net *bn = netdev_priv(bd->netdev);
	u32 tunl_tpa_bmap = BNGE_DFLT_TUNL_TPA_BMAP;

	if (!(bd->fw_cap & BNGE_FW_CAP_VNIC_TUNNEL_TPA))
		return;

	if (bn->vxlan_port)
		tunl_tpa_bmap |= VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_VXLAN;
	if (bn->vxlan_gpe_port)
		tunl_tpa_bmap |= VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_VXLAN_GPE;
	if (bn->nge_port)
		tunl_tpa_bmap |= VNIC_TPA_CFG_REQ_TNL_TPA_EN_BITMAP_GENEVE;

	req->enables |= cpu_to_le32(VNIC_TPA_CFG_REQ_ENABLES_TNL_TPA_EN);
	req->tnl_tpa_en_bitmap = cpu_to_le32(tunl_tpa_bmap);
}

int bnge_hwrm_vnic_set_tpa(struct bnge_dev *bd, struct bnge_vnic_info *vnic,
			   u32 tpa_flags)
{
	struct bnge_net *bn = netdev_priv(bd->netdev);
	struct hwrm_vnic_tpa_cfg_input *req;
	int rc;

	if (vnic->fw_vnic_id == INVALID_HW_RING_ID)
		return 0;

	rc = bnge_hwrm_req_init(bd, req, HWRM_VNIC_TPA_CFG);
	if (rc)
		return rc;

	if (tpa_flags) {
		u32 flags;

		flags = VNIC_TPA_CFG_REQ_FLAGS_TPA |
			VNIC_TPA_CFG_REQ_FLAGS_ENCAP_TPA |
			VNIC_TPA_CFG_REQ_FLAGS_RSC_WND_UPDATE |
			VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_ECN |
			VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_SAME_GRE_SEQ;
		if (tpa_flags & BNGE_NET_EN_GRO)
			flags |= VNIC_TPA_CFG_REQ_FLAGS_GRO;

		req->flags = cpu_to_le32(flags);
		req->enables =
			cpu_to_le32(VNIC_TPA_CFG_REQ_ENABLES_MAX_AGG_SEGS |
				    VNIC_TPA_CFG_REQ_ENABLES_MAX_AGGS |
				    VNIC_TPA_CFG_REQ_ENABLES_MIN_AGG_LEN);
		req->max_agg_segs = cpu_to_le16(MAX_TPA_SEGS);
		req->max_aggs = cpu_to_le16(bn->max_tpa);
		req->min_agg_len = cpu_to_le32(512);
		bnge_hwrm_vnic_update_tunl_tpa(bd, req);
	}
	req->vnic_id = cpu_to_le16(vnic->fw_vnic_id);

	return bnge_hwrm_req_send(bd, req);
}

int bnge_hwrm_func_qstat_ext(struct bnge_dev *bd, struct bnge_stats_mem *stats)
{
	struct hwrm_func_qstats_ext_output *resp;
	struct hwrm_func_qstats_ext_input *req;
	__le64 *hw_masks;
	int rc;

	if (!(bd->fw_cap & BNGE_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	rc = bnge_hwrm_req_init(bd, req, HWRM_FUNC_QSTATS_EXT);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	req->flags = FUNC_QSTATS_EXT_REQ_FLAGS_COUNTER_MASK;

	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	if (!rc) {
		hw_masks = &resp->rx_ucast_pkts;
		bnge_copy_hw_masks(stats->hw_masks, hw_masks, stats->len / 8);
	}
	bnge_hwrm_req_drop(bd, req);
	return rc;
}

int bnge_hwrm_port_qstats_ext(struct bnge_dev *bd, u8 flags)
{
	struct hwrm_queue_pri2cos_qcfg_output *resp_qc;
	struct bnge_net *bn = netdev_priv(bd->netdev);
	struct hwrm_queue_pri2cos_qcfg_input *req_qc;
	struct hwrm_port_qstats_ext_output *resp_qs;
	struct hwrm_port_qstats_ext_input *req_qs;
	struct bnge_pf_info *pf = &bd->pf;
	u32 tx_stat_size;
	int rc;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS_EXT))
		return 0;

	if (flags && !(bd->fw_cap & BNGE_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	rc = bnge_hwrm_req_init(bd, req_qs, HWRM_PORT_QSTATS_EXT);
	if (rc)
		return rc;

	req_qs->flags = flags;
	req_qs->port_id = cpu_to_le16(pf->port_id);
	req_qs->rx_stat_size = cpu_to_le16(sizeof(struct rx_port_stats_ext));
	req_qs->rx_stat_host_addr =
		cpu_to_le64(bn->rx_port_stats_ext.hw_stats_map);
	tx_stat_size = bn->tx_port_stats_ext.hw_stats ?
		       sizeof(struct tx_port_stats_ext) : 0;
	req_qs->tx_stat_size = cpu_to_le16(tx_stat_size);
	req_qs->tx_stat_host_addr =
		cpu_to_le64(bn->tx_port_stats_ext.hw_stats_map);
	resp_qs = bnge_hwrm_req_hold(bd, req_qs);
	rc = bnge_hwrm_req_send(bd, req_qs);
	if (!rc) {
		bn->fw_rx_stats_ext_size =
			le16_to_cpu(resp_qs->rx_stat_size) / 8;
		bn->fw_tx_stats_ext_size = tx_stat_size ?
			le16_to_cpu(resp_qs->tx_stat_size) / 8 : 0;
	} else {
		bn->fw_rx_stats_ext_size = 0;
		bn->fw_tx_stats_ext_size = 0;
	}
	bnge_hwrm_req_drop(bd, req_qs);

	if (flags)
		return rc;

	if (bn->fw_tx_stats_ext_size <=
	    offsetof(struct tx_port_stats_ext, pfc_pri0_tx_duration_us) / 8) {
		bn->pri2cos_valid = false;
		return rc;
	}

	rc = bnge_hwrm_req_init(bd, req_qc, HWRM_QUEUE_PRI2COS_QCFG);
	if (rc)
		return rc;

	req_qc->flags = cpu_to_le32(QUEUE_PRI2COS_QCFG_REQ_FLAGS_IVLAN);

	resp_qc = bnge_hwrm_req_hold(bd, req_qc);
	rc = bnge_hwrm_req_send(bd, req_qc);
	if (!rc) {
		u8 *pri2cos;
		int i, j;

		pri2cos = &resp_qc->pri0_cos_queue_id;
		for (i = 0; i < 8; i++) {
			u8 queue_id = pri2cos[i];
			u8 queue_idx;

			/* Per port queue IDs start from 0, 10, 20, etc */
			queue_idx = queue_id % 10;
			if (queue_idx >= BNGE_MAX_QUEUE) {
				bn->pri2cos_valid = false;
				rc = -EINVAL;
				goto drop_req;
			}
			for (j = 0; j < bd->max_q; j++) {
				if (bd->q_ids[j] == queue_id)
					bn->pri2cos_idx[i] = queue_idx;
			}
		}
		bn->pri2cos_valid = true;
	}
drop_req:
	bnge_hwrm_req_drop(bd, req_qc);
	return rc;
}

int bnge_hwrm_port_qstats(struct bnge_dev *bd, u8 flags)
{
	struct bnge_net *bn = netdev_priv(bd->netdev);
	struct hwrm_port_qstats_input *req;
	struct bnge_pf_info *pf = &bd->pf;
	int rc;

	if (!(bn->flags & BNGE_FLAG_PORT_STATS))
		return 0;

	if (flags && !(bd->fw_cap & BNGE_FW_CAP_EXT_HW_STATS_SUPPORTED))
		return -EOPNOTSUPP;

	rc = bnge_hwrm_req_init(bd, req, HWRM_PORT_QSTATS);
	if (rc)
		return rc;

	req->flags = flags;
	req->port_id = cpu_to_le16(pf->port_id);
	req->tx_stat_host_addr = cpu_to_le64(bn->port_stats.hw_stats_map +
					     BNGE_TX_PORT_STATS_BYTE_OFFSET);
	req->rx_stat_host_addr = cpu_to_le64(bn->port_stats.hw_stats_map);

	return bnge_hwrm_req_send(bd, req);
}
