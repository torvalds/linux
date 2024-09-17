/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Slow Path Operators
 */

#define dev_fmt(fmt) "QPLIB: " fmt

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/pci.h>

#include "roce_hsi.h"

#include "qplib_res.h"
#include "qplib_rcfw.h"
#include "qplib_sp.h"

const struct bnxt_qplib_gid bnxt_qplib_gid_zero = {{ 0, 0, 0, 0, 0, 0, 0, 0,
						     0, 0, 0, 0, 0, 0, 0, 0 } };

/* Device */

static bool bnxt_qplib_is_atomic_cap(struct bnxt_qplib_rcfw *rcfw)
{
	u16 pcie_ctl2 = 0;

	if (!bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx))
		return false;

	pcie_capability_read_word(rcfw->pdev, PCI_EXP_DEVCTL2, &pcie_ctl2);
	return (pcie_ctl2 & PCI_EXP_DEVCTL2_ATOMIC_REQ);
}

static void bnxt_qplib_query_version(struct bnxt_qplib_rcfw *rcfw,
				     char *fw_ver)
{
	struct cmdq_query_version req;
	struct creq_query_version_resp resp;
	u16 cmd_flags = 0;
	int rc = 0;

	RCFW_CMD_PREP(req, QUERY_VERSION, cmd_flags);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		return;
	fw_ver[0] = resp.fw_maj;
	fw_ver[1] = resp.fw_minor;
	fw_ver[2] = resp.fw_bld;
	fw_ver[3] = resp.fw_rsvd;
}

int bnxt_qplib_get_dev_attr(struct bnxt_qplib_rcfw *rcfw,
			    struct bnxt_qplib_dev_attr *attr, bool vf)
{
	struct cmdq_query_func req;
	struct creq_query_func_resp resp;
	struct bnxt_qplib_rcfw_sbuf *sbuf;
	struct creq_query_func_resp_sb *sb;
	u16 cmd_flags = 0;
	u32 temp;
	u8 *tqm_alloc;
	int i, rc = 0;

	RCFW_CMD_PREP(req, QUERY_FUNC, cmd_flags);

	sbuf = bnxt_qplib_rcfw_alloc_sbuf(rcfw, sizeof(*sb));
	if (!sbuf) {
		dev_err(&rcfw->pdev->dev,
			"SP: QUERY_FUNC alloc side buffer failed\n");
		return -ENOMEM;
	}

	sb = sbuf->sb;
	req.resp_size = sizeof(*sb) / BNXT_QPLIB_CMDQE_UNITS;
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  (void *)sbuf, 0);
	if (rc)
		goto bail;

	/* Extract the context from the side buffer */
	attr->max_qp = le32_to_cpu(sb->max_qp);
	/* max_qp value reported by FW for PF doesn't include the QP1 for PF */
	if (!vf)
		attr->max_qp += 1;
	attr->max_qp_rd_atom =
		sb->max_qp_rd_atom > BNXT_QPLIB_MAX_OUT_RD_ATOM ?
		BNXT_QPLIB_MAX_OUT_RD_ATOM : sb->max_qp_rd_atom;
	attr->max_qp_init_rd_atom =
		sb->max_qp_init_rd_atom > BNXT_QPLIB_MAX_OUT_RD_ATOM ?
		BNXT_QPLIB_MAX_OUT_RD_ATOM : sb->max_qp_init_rd_atom;
	attr->max_qp_wqes = le16_to_cpu(sb->max_qp_wr);
	/*
	 * 128 WQEs needs to be reserved for the HW (8916). Prevent
	 * reporting the max number
	 */
	attr->max_qp_wqes -= BNXT_QPLIB_RESERVED_QP_WRS + 1;
	attr->max_qp_sges = bnxt_qplib_is_chip_gen_p5(rcfw->res->cctx) ?
			    6 : sb->max_sge;
	attr->max_cq = le32_to_cpu(sb->max_cq);
	attr->max_cq_wqes = le32_to_cpu(sb->max_cqe);
	attr->max_cq_sges = attr->max_qp_sges;
	attr->max_mr = le32_to_cpu(sb->max_mr);
	attr->max_mw = le32_to_cpu(sb->max_mw);

	attr->max_mr_size = le64_to_cpu(sb->max_mr_size);
	attr->max_pd = 64 * 1024;
	attr->max_raw_ethy_qp = le32_to_cpu(sb->max_raw_eth_qp);
	attr->max_ah = le32_to_cpu(sb->max_ah);

	attr->max_srq = le16_to_cpu(sb->max_srq);
	attr->max_srq_wqes = le32_to_cpu(sb->max_srq_wr) - 1;
	attr->max_srq_sges = sb->max_srq_sge;
	attr->max_pkey = 1;
	attr->max_inline_data = le32_to_cpu(sb->max_inline_data);
	attr->l2_db_size = (sb->l2_db_space_size + 1) *
			    (0x01 << RCFW_DBR_BASE_PAGE_SHIFT);
	attr->max_sgid = BNXT_QPLIB_NUM_GIDS_SUPPORTED;
	attr->dev_cap_flags = le16_to_cpu(sb->dev_cap_flags);

	bnxt_qplib_query_version(rcfw, attr->fw_ver);

	for (i = 0; i < MAX_TQM_ALLOC_REQ / 4; i++) {
		temp = le32_to_cpu(sb->tqm_alloc_reqs[i]);
		tqm_alloc = (u8 *)&temp;
		attr->tqm_alloc_reqs[i * 4] = *tqm_alloc;
		attr->tqm_alloc_reqs[i * 4 + 1] = *(++tqm_alloc);
		attr->tqm_alloc_reqs[i * 4 + 2] = *(++tqm_alloc);
		attr->tqm_alloc_reqs[i * 4 + 3] = *(++tqm_alloc);
	}

	attr->is_atomic = bnxt_qplib_is_atomic_cap(rcfw);
bail:
	bnxt_qplib_rcfw_free_sbuf(rcfw, sbuf);
	return rc;
}

int bnxt_qplib_set_func_resources(struct bnxt_qplib_res *res,
				  struct bnxt_qplib_rcfw *rcfw,
				  struct bnxt_qplib_ctx *ctx)
{
	struct cmdq_set_func_resources req;
	struct creq_set_func_resources_resp resp;
	u16 cmd_flags = 0;
	int rc = 0;

	RCFW_CMD_PREP(req, SET_FUNC_RESOURCES, cmd_flags);

	req.number_of_qp = cpu_to_le32(ctx->qpc_count);
	req.number_of_mrw = cpu_to_le32(ctx->mrw_count);
	req.number_of_srq =  cpu_to_le32(ctx->srqc_count);
	req.number_of_cq = cpu_to_le32(ctx->cq_count);

	req.max_qp_per_vf = cpu_to_le32(ctx->vf_res.max_qp_per_vf);
	req.max_mrw_per_vf = cpu_to_le32(ctx->vf_res.max_mrw_per_vf);
	req.max_srq_per_vf = cpu_to_le32(ctx->vf_res.max_srq_per_vf);
	req.max_cq_per_vf = cpu_to_le32(ctx->vf_res.max_cq_per_vf);
	req.max_gid_per_vf = cpu_to_le32(ctx->vf_res.max_gid_per_vf);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp,
					  NULL, 0);
	if (rc) {
		dev_err(&res->pdev->dev, "Failed to set function resources\n");
	}
	return rc;
}

/* SGID */
int bnxt_qplib_get_sgid(struct bnxt_qplib_res *res,
			struct bnxt_qplib_sgid_tbl *sgid_tbl, int index,
			struct bnxt_qplib_gid *gid)
{
	if (index >= sgid_tbl->max) {
		dev_err(&res->pdev->dev,
			"Index %d exceeded SGID table max (%d)\n",
			index, sgid_tbl->max);
		return -EINVAL;
	}
	memcpy(gid, &sgid_tbl->tbl[index].gid, sizeof(*gid));
	return 0;
}

int bnxt_qplib_del_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			struct bnxt_qplib_gid *gid, u16 vlan_id, bool update)
{
	struct bnxt_qplib_res *res = to_bnxt_qplib(sgid_tbl,
						   struct bnxt_qplib_res,
						   sgid_tbl);
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	int index;

	if (!sgid_tbl) {
		dev_err(&res->pdev->dev, "SGID table not allocated\n");
		return -EINVAL;
	}
	/* Do we need a sgid_lock here? */
	if (!sgid_tbl->active) {
		dev_err(&res->pdev->dev, "SGID table has no active entries\n");
		return -ENOMEM;
	}
	for (index = 0; index < sgid_tbl->max; index++) {
		if (!memcmp(&sgid_tbl->tbl[index].gid, gid, sizeof(*gid)) &&
		    vlan_id == sgid_tbl->tbl[index].vlan_id)
			break;
	}
	if (index == sgid_tbl->max) {
		dev_warn(&res->pdev->dev, "GID not found in the SGID table\n");
		return 0;
	}
	/* Remove GID from the SGID table */
	if (update) {
		struct cmdq_delete_gid req;
		struct creq_delete_gid_resp resp;
		u16 cmd_flags = 0;
		int rc;

		RCFW_CMD_PREP(req, DELETE_GID, cmd_flags);
		if (sgid_tbl->hw_id[index] == 0xFFFF) {
			dev_err(&res->pdev->dev,
				"GID entry contains an invalid HW id\n");
			return -EINVAL;
		}
		req.gid_index = cpu_to_le16(sgid_tbl->hw_id[index]);
		rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
						  (void *)&resp, NULL, 0);
		if (rc)
			return rc;
	}
	memcpy(&sgid_tbl->tbl[index].gid, &bnxt_qplib_gid_zero,
	       sizeof(bnxt_qplib_gid_zero));
	sgid_tbl->tbl[index].vlan_id = 0xFFFF;
	sgid_tbl->vlan[index] = 0;
	sgid_tbl->active--;
	dev_dbg(&res->pdev->dev,
		"SGID deleted hw_id[0x%x] = 0x%x active = 0x%x\n",
		 index, sgid_tbl->hw_id[index], sgid_tbl->active);
	sgid_tbl->hw_id[index] = (u16)-1;

	/* unlock */
	return 0;
}

int bnxt_qplib_add_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			struct bnxt_qplib_gid *gid, const u8 *smac,
			u16 vlan_id, bool update, u32 *index)
{
	struct bnxt_qplib_res *res = to_bnxt_qplib(sgid_tbl,
						   struct bnxt_qplib_res,
						   sgid_tbl);
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	int i, free_idx;

	if (!sgid_tbl) {
		dev_err(&res->pdev->dev, "SGID table not allocated\n");
		return -EINVAL;
	}
	/* Do we need a sgid_lock here? */
	if (sgid_tbl->active == sgid_tbl->max) {
		dev_err(&res->pdev->dev, "SGID table is full\n");
		return -ENOMEM;
	}
	free_idx = sgid_tbl->max;
	for (i = 0; i < sgid_tbl->max; i++) {
		if (!memcmp(&sgid_tbl->tbl[i], gid, sizeof(*gid)) &&
		    sgid_tbl->tbl[i].vlan_id == vlan_id) {
			dev_dbg(&res->pdev->dev,
				"SGID entry already exist in entry %d!\n", i);
			*index = i;
			return -EALREADY;
		} else if (!memcmp(&sgid_tbl->tbl[i], &bnxt_qplib_gid_zero,
				   sizeof(bnxt_qplib_gid_zero)) &&
			   free_idx == sgid_tbl->max) {
			free_idx = i;
		}
	}
	if (free_idx == sgid_tbl->max) {
		dev_err(&res->pdev->dev,
			"SGID table is FULL but count is not MAX??\n");
		return -ENOMEM;
	}
	if (update) {
		struct cmdq_add_gid req;
		struct creq_add_gid_resp resp;
		u16 cmd_flags = 0;
		int rc;

		RCFW_CMD_PREP(req, ADD_GID, cmd_flags);

		req.gid[0] = cpu_to_be32(((u32 *)gid->data)[3]);
		req.gid[1] = cpu_to_be32(((u32 *)gid->data)[2]);
		req.gid[2] = cpu_to_be32(((u32 *)gid->data)[1]);
		req.gid[3] = cpu_to_be32(((u32 *)gid->data)[0]);
		/*
		 * driver should ensure that all RoCE traffic is always VLAN
		 * tagged if RoCE traffic is running on non-zero VLAN ID or
		 * RoCE traffic is running on non-zero Priority.
		 */
		if ((vlan_id != 0xFFFF) || res->prio) {
			if (vlan_id != 0xFFFF)
				req.vlan = cpu_to_le16
				(vlan_id & CMDQ_ADD_GID_VLAN_VLAN_ID_MASK);
			req.vlan |= cpu_to_le16
					(CMDQ_ADD_GID_VLAN_TPID_TPID_8100 |
					 CMDQ_ADD_GID_VLAN_VLAN_EN);
		}

		/* MAC in network format */
		req.src_mac[0] = cpu_to_be16(((u16 *)smac)[0]);
		req.src_mac[1] = cpu_to_be16(((u16 *)smac)[1]);
		req.src_mac[2] = cpu_to_be16(((u16 *)smac)[2]);

		rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
						  (void *)&resp, NULL, 0);
		if (rc)
			return rc;
		sgid_tbl->hw_id[free_idx] = le32_to_cpu(resp.xid);
	}
	/* Add GID to the sgid_tbl */
	memcpy(&sgid_tbl->tbl[free_idx], gid, sizeof(*gid));
	sgid_tbl->tbl[free_idx].vlan_id = vlan_id;
	sgid_tbl->active++;
	if (vlan_id != 0xFFFF)
		sgid_tbl->vlan[free_idx] = 1;

	dev_dbg(&res->pdev->dev,
		"SGID added hw_id[0x%x] = 0x%x active = 0x%x\n",
		 free_idx, sgid_tbl->hw_id[free_idx], sgid_tbl->active);

	*index = free_idx;
	/* unlock */
	return 0;
}

int bnxt_qplib_update_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			   struct bnxt_qplib_gid *gid, u16 gid_idx,
			   const u8 *smac)
{
	struct bnxt_qplib_res *res = to_bnxt_qplib(sgid_tbl,
						   struct bnxt_qplib_res,
						   sgid_tbl);
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct creq_modify_gid_resp resp;
	struct cmdq_modify_gid req;
	int rc;
	u16 cmd_flags = 0;

	RCFW_CMD_PREP(req, MODIFY_GID, cmd_flags);

	req.gid[0] = cpu_to_be32(((u32 *)gid->data)[3]);
	req.gid[1] = cpu_to_be32(((u32 *)gid->data)[2]);
	req.gid[2] = cpu_to_be32(((u32 *)gid->data)[1]);
	req.gid[3] = cpu_to_be32(((u32 *)gid->data)[0]);
	if (res->prio) {
		req.vlan |= cpu_to_le16
			(CMDQ_ADD_GID_VLAN_TPID_TPID_8100 |
			 CMDQ_ADD_GID_VLAN_VLAN_EN);
	}

	/* MAC in network format */
	req.src_mac[0] = cpu_to_be16(((u16 *)smac)[0]);
	req.src_mac[1] = cpu_to_be16(((u16 *)smac)[1]);
	req.src_mac[2] = cpu_to_be16(((u16 *)smac)[2]);

	req.gid_index = cpu_to_le16(gid_idx);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	return rc;
}

/* AH */
int bnxt_qplib_create_ah(struct bnxt_qplib_res *res, struct bnxt_qplib_ah *ah,
			 bool block)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_create_ah req;
	struct creq_create_ah_resp resp;
	u16 cmd_flags = 0;
	u32 temp32[4];
	u16 temp16[3];
	int rc;

	RCFW_CMD_PREP(req, CREATE_AH, cmd_flags);

	memcpy(temp32, ah->dgid.data, sizeof(struct bnxt_qplib_gid));
	req.dgid[0] = cpu_to_le32(temp32[0]);
	req.dgid[1] = cpu_to_le32(temp32[1]);
	req.dgid[2] = cpu_to_le32(temp32[2]);
	req.dgid[3] = cpu_to_le32(temp32[3]);

	req.type = ah->nw_type;
	req.hop_limit = ah->hop_limit;
	req.sgid_index = cpu_to_le16(res->sgid_tbl.hw_id[ah->sgid_index]);
	req.dest_vlan_id_flow_label = cpu_to_le32((ah->flow_label &
					CMDQ_CREATE_AH_FLOW_LABEL_MASK) |
					CMDQ_CREATE_AH_DEST_VLAN_ID_MASK);
	req.pd_id = cpu_to_le32(ah->pd->id);
	req.traffic_class = ah->traffic_class;

	/* MAC in network format */
	memcpy(temp16, ah->dmac, 6);
	req.dest_mac[0] = cpu_to_le16(temp16[0]);
	req.dest_mac[1] = cpu_to_le16(temp16[1]);
	req.dest_mac[2] = cpu_to_le16(temp16[2]);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  NULL, block);
	if (rc)
		return rc;

	ah->id = le32_to_cpu(resp.xid);
	return 0;
}

void bnxt_qplib_destroy_ah(struct bnxt_qplib_res *res, struct bnxt_qplib_ah *ah,
			   bool block)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_destroy_ah req;
	struct creq_destroy_ah_resp resp;
	u16 cmd_flags = 0;

	/* Clean up the AH table in the device */
	RCFW_CMD_PREP(req, DESTROY_AH, cmd_flags);

	req.ah_cid = cpu_to_le32(ah->id);

	bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp, NULL,
				     block);
}

/* MRW */
int bnxt_qplib_free_mrw(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mrw)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_deallocate_key req;
	struct creq_deallocate_key_resp resp;
	u16 cmd_flags = 0;
	int rc;

	if (mrw->lkey == 0xFFFFFFFF) {
		dev_info(&res->pdev->dev, "SP: Free a reserved lkey MRW\n");
		return 0;
	}

	RCFW_CMD_PREP(req, DEALLOCATE_KEY, cmd_flags);

	req.mrw_flags = mrw->type;

	if ((mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE1)  ||
	    (mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2A) ||
	    (mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2B))
		req.key = cpu_to_le32(mrw->rkey);
	else
		req.key = cpu_to_le32(mrw->lkey);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  NULL, 0);
	if (rc)
		return rc;

	/* Free the qplib's MRW memory */
	if (mrw->hwq.max_elements)
		bnxt_qplib_free_hwq(res, &mrw->hwq);

	return 0;
}

int bnxt_qplib_alloc_mrw(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mrw)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_allocate_mrw req;
	struct creq_allocate_mrw_resp resp;
	u16 cmd_flags = 0;
	unsigned long tmp;
	int rc;

	RCFW_CMD_PREP(req, ALLOCATE_MRW, cmd_flags);

	req.pd_id = cpu_to_le32(mrw->pd->id);
	req.mrw_flags = mrw->type;
	if ((mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_PMR &&
	     mrw->flags & BNXT_QPLIB_FR_PMR) ||
	    mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2A ||
	    mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2B)
		req.access = CMDQ_ALLOCATE_MRW_ACCESS_CONSUMER_OWNED_KEY;
	tmp = (unsigned long)mrw;
	req.mrw_handle = cpu_to_le64(tmp);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		return rc;

	if ((mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE1)  ||
	    (mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2A) ||
	    (mrw->type == CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2B))
		mrw->rkey = le32_to_cpu(resp.xid);
	else
		mrw->lkey = le32_to_cpu(resp.xid);
	return 0;
}

int bnxt_qplib_dereg_mrw(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mrw,
			 bool block)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_deregister_mr req;
	struct creq_deregister_mr_resp resp;
	u16 cmd_flags = 0;
	int rc;

	RCFW_CMD_PREP(req, DEREGISTER_MR, cmd_flags);

	req.lkey = cpu_to_le32(mrw->lkey);
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, block);
	if (rc)
		return rc;

	/* Free the qplib's MR memory */
	if (mrw->hwq.max_elements) {
		mrw->va = 0;
		mrw->total_size = 0;
		bnxt_qplib_free_hwq(res, &mrw->hwq);
	}

	return 0;
}

int bnxt_qplib_reg_mr(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mr,
		      struct ib_umem *umem, int num_pbls, u32 buf_pg_size)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct bnxt_qplib_hwq_attr hwq_attr = {};
	struct bnxt_qplib_sg_info sginfo = {};
	struct creq_register_mr_resp resp;
	struct cmdq_register_mr req;
	u16 cmd_flags = 0, level;
	int pages, rc;
	u32 pg_size;

	if (num_pbls) {
		pages = roundup_pow_of_two(num_pbls);
		/* Allocate memory for the non-leaf pages to store buf ptrs.
		 * Non-leaf pages always uses system PAGE_SIZE
		 */
		/* Free the hwq if it already exist, must be a rereg */
		if (mr->hwq.max_elements)
			bnxt_qplib_free_hwq(res, &mr->hwq);
		hwq_attr.res = res;
		hwq_attr.depth = pages;
		hwq_attr.stride = sizeof(dma_addr_t);
		hwq_attr.type = HWQ_TYPE_MR;
		hwq_attr.sginfo = &sginfo;
		hwq_attr.sginfo->umem = umem;
		hwq_attr.sginfo->npages = pages;
		hwq_attr.sginfo->pgsize = buf_pg_size;
		hwq_attr.sginfo->pgshft = ilog2(buf_pg_size);
		rc = bnxt_qplib_alloc_init_hwq(&mr->hwq, &hwq_attr);
		if (rc) {
			dev_err(&res->pdev->dev,
				"SP: Reg MR memory allocation failed\n");
			return -ENOMEM;
		}
	}

	RCFW_CMD_PREP(req, REGISTER_MR, cmd_flags);

	/* Configure the request */
	if (mr->hwq.level == PBL_LVL_MAX) {
		/* No PBL provided, just use system PAGE_SIZE */
		level = 0;
		req.pbl = 0;
		pg_size = PAGE_SIZE;
	} else {
		level = mr->hwq.level;
		req.pbl = cpu_to_le64(mr->hwq.pbl[PBL_LVL_0].pg_map_arr[0]);
	}
	pg_size = buf_pg_size ? buf_pg_size : PAGE_SIZE;
	req.log2_pg_size_lvl = (level << CMDQ_REGISTER_MR_LVL_SFT) |
			       ((ilog2(pg_size) <<
				 CMDQ_REGISTER_MR_LOG2_PG_SIZE_SFT) &
				CMDQ_REGISTER_MR_LOG2_PG_SIZE_MASK);
	req.log2_pbl_pg_size = cpu_to_le16(((ilog2(PAGE_SIZE) <<
				 CMDQ_REGISTER_MR_LOG2_PBL_PG_SIZE_SFT) &
				CMDQ_REGISTER_MR_LOG2_PBL_PG_SIZE_MASK));
	req.access = (mr->flags & 0xFFFF);
	req.va = cpu_to_le64(mr->va);
	req.key = cpu_to_le32(mr->lkey);
	req.mr_size = cpu_to_le64(mr->total_size);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, false);
	if (rc)
		goto fail;

	return 0;

fail:
	if (mr->hwq.max_elements)
		bnxt_qplib_free_hwq(res, &mr->hwq);
	return rc;
}

int bnxt_qplib_alloc_fast_reg_page_list(struct bnxt_qplib_res *res,
					struct bnxt_qplib_frpl *frpl,
					int max_pg_ptrs)
{
	struct bnxt_qplib_hwq_attr hwq_attr = {};
	struct bnxt_qplib_sg_info sginfo = {};
	int pg_ptrs, pages, rc;

	/* Re-calculate the max to fit the HWQ allocation model */
	pg_ptrs = roundup_pow_of_two(max_pg_ptrs);
	pages = pg_ptrs >> MAX_PBL_LVL_1_PGS_SHIFT;
	if (!pages)
		pages++;

	if (pages > MAX_PBL_LVL_1_PGS)
		return -ENOMEM;

	sginfo.pgsize = PAGE_SIZE;
	sginfo.nopte = true;

	hwq_attr.res = res;
	hwq_attr.depth = pg_ptrs;
	hwq_attr.stride = PAGE_SIZE;
	hwq_attr.sginfo = &sginfo;
	hwq_attr.type = HWQ_TYPE_CTX;
	rc = bnxt_qplib_alloc_init_hwq(&frpl->hwq, &hwq_attr);
	if (!rc)
		frpl->max_pg_ptrs = pg_ptrs;

	return rc;
}

int bnxt_qplib_free_fast_reg_page_list(struct bnxt_qplib_res *res,
				       struct bnxt_qplib_frpl *frpl)
{
	bnxt_qplib_free_hwq(res, &frpl->hwq);
	return 0;
}

int bnxt_qplib_map_tc2cos(struct bnxt_qplib_res *res, u16 *cids)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_map_tc_to_cos req;
	struct creq_map_tc_to_cos_resp resp;
	u16 cmd_flags = 0;

	RCFW_CMD_PREP(req, MAP_TC_TO_COS, cmd_flags);
	req.cos0 = cpu_to_le16(cids[0]);
	req.cos1 = cpu_to_le16(cids[1]);

	return bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
						NULL, 0);
}

int bnxt_qplib_get_roce_stats(struct bnxt_qplib_rcfw *rcfw,
			      struct bnxt_qplib_roce_stats *stats)
{
	struct cmdq_query_roce_stats req;
	struct creq_query_roce_stats_resp resp;
	struct bnxt_qplib_rcfw_sbuf *sbuf;
	struct creq_query_roce_stats_resp_sb *sb;
	u16 cmd_flags = 0;
	int rc = 0;

	RCFW_CMD_PREP(req, QUERY_ROCE_STATS, cmd_flags);

	sbuf = bnxt_qplib_rcfw_alloc_sbuf(rcfw, sizeof(*sb));
	if (!sbuf) {
		dev_err(&rcfw->pdev->dev,
			"SP: QUERY_ROCE_STATS alloc side buffer failed\n");
		return -ENOMEM;
	}

	sb = sbuf->sb;
	req.resp_size = sizeof(*sb) / BNXT_QPLIB_CMDQE_UNITS;
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  (void *)sbuf, 0);
	if (rc)
		goto bail;
	/* Extract the context from the side buffer */
	stats->to_retransmits = le64_to_cpu(sb->to_retransmits);
	stats->seq_err_naks_rcvd = le64_to_cpu(sb->seq_err_naks_rcvd);
	stats->max_retry_exceeded = le64_to_cpu(sb->max_retry_exceeded);
	stats->rnr_naks_rcvd = le64_to_cpu(sb->rnr_naks_rcvd);
	stats->missing_resp = le64_to_cpu(sb->missing_resp);
	stats->unrecoverable_err = le64_to_cpu(sb->unrecoverable_err);
	stats->bad_resp_err = le64_to_cpu(sb->bad_resp_err);
	stats->local_qp_op_err = le64_to_cpu(sb->local_qp_op_err);
	stats->local_protection_err = le64_to_cpu(sb->local_protection_err);
	stats->mem_mgmt_op_err = le64_to_cpu(sb->mem_mgmt_op_err);
	stats->remote_invalid_req_err = le64_to_cpu(sb->remote_invalid_req_err);
	stats->remote_access_err = le64_to_cpu(sb->remote_access_err);
	stats->remote_op_err = le64_to_cpu(sb->remote_op_err);
	stats->dup_req = le64_to_cpu(sb->dup_req);
	stats->res_exceed_max = le64_to_cpu(sb->res_exceed_max);
	stats->res_length_mismatch = le64_to_cpu(sb->res_length_mismatch);
	stats->res_exceeds_wqe = le64_to_cpu(sb->res_exceeds_wqe);
	stats->res_opcode_err = le64_to_cpu(sb->res_opcode_err);
	stats->res_rx_invalid_rkey = le64_to_cpu(sb->res_rx_invalid_rkey);
	stats->res_rx_domain_err = le64_to_cpu(sb->res_rx_domain_err);
	stats->res_rx_no_perm = le64_to_cpu(sb->res_rx_no_perm);
	stats->res_rx_range_err = le64_to_cpu(sb->res_rx_range_err);
	stats->res_tx_invalid_rkey = le64_to_cpu(sb->res_tx_invalid_rkey);
	stats->res_tx_domain_err = le64_to_cpu(sb->res_tx_domain_err);
	stats->res_tx_no_perm = le64_to_cpu(sb->res_tx_no_perm);
	stats->res_tx_range_err = le64_to_cpu(sb->res_tx_range_err);
	stats->res_irrq_oflow = le64_to_cpu(sb->res_irrq_oflow);
	stats->res_unsup_opcode = le64_to_cpu(sb->res_unsup_opcode);
	stats->res_unaligned_atomic = le64_to_cpu(sb->res_unaligned_atomic);
	stats->res_rem_inv_err = le64_to_cpu(sb->res_rem_inv_err);
	stats->res_mem_error = le64_to_cpu(sb->res_mem_error);
	stats->res_srq_err = le64_to_cpu(sb->res_srq_err);
	stats->res_cmp_err = le64_to_cpu(sb->res_cmp_err);
	stats->res_invalid_dup_rkey = le64_to_cpu(sb->res_invalid_dup_rkey);
	stats->res_wqe_format_err = le64_to_cpu(sb->res_wqe_format_err);
	stats->res_cq_load_err = le64_to_cpu(sb->res_cq_load_err);
	stats->res_srq_load_err = le64_to_cpu(sb->res_srq_load_err);
	stats->res_tx_pci_err = le64_to_cpu(sb->res_tx_pci_err);
	stats->res_rx_pci_err = le64_to_cpu(sb->res_rx_pci_err);
	if (!rcfw->init_oos_stats) {
		rcfw->oos_prev = le64_to_cpu(sb->res_oos_drop_count);
		rcfw->init_oos_stats = 1;
	} else {
		stats->res_oos_drop_count +=
				(le64_to_cpu(sb->res_oos_drop_count) -
				 rcfw->oos_prev) & BNXT_QPLIB_OOS_COUNT_MASK;
		rcfw->oos_prev = le64_to_cpu(sb->res_oos_drop_count);
	}

bail:
	bnxt_qplib_rcfw_free_sbuf(rcfw, sbuf);
	return rc;
}

int bnxt_qplib_qext_stat(struct bnxt_qplib_rcfw *rcfw, u32 fid,
			 struct bnxt_qplib_ext_stat *estat)
{
	struct creq_query_roce_stats_ext_resp resp = {};
	struct creq_query_roce_stats_ext_resp_sb *sb;
	struct cmdq_query_roce_stats_ext req = {};
	struct bnxt_qplib_rcfw_sbuf *sbuf;
	u16 cmd_flags = 0;
	int rc;

	sbuf = bnxt_qplib_rcfw_alloc_sbuf(rcfw, sizeof(*sb));
	if (!sbuf) {
		dev_err(&rcfw->pdev->dev,
			"SP: QUERY_ROCE_STATS_EXT alloc sb failed");
		return -ENOMEM;
	}

	RCFW_CMD_PREP(req, QUERY_ROCE_STATS_EXT, cmd_flags);

	req.resp_size = ALIGN(sizeof(*sb), BNXT_QPLIB_CMDQE_UNITS);
	req.resp_addr = cpu_to_le64(sbuf->dma_addr);
	req.function_id = cpu_to_le32(fid);
	req.flags = cpu_to_le16(CMDQ_QUERY_ROCE_STATS_EXT_FLAGS_FUNCTION_ID);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, (void *)sbuf, 0);
	if (rc)
		goto bail;

	sb = sbuf->sb;
	estat->tx_atomic_req = le64_to_cpu(sb->tx_atomic_req_pkts);
	estat->tx_read_req = le64_to_cpu(sb->tx_read_req_pkts);
	estat->tx_read_res = le64_to_cpu(sb->tx_read_res_pkts);
	estat->tx_write_req = le64_to_cpu(sb->tx_write_req_pkts);
	estat->tx_send_req = le64_to_cpu(sb->tx_send_req_pkts);
	estat->rx_atomic_req = le64_to_cpu(sb->rx_atomic_req_pkts);
	estat->rx_read_req = le64_to_cpu(sb->rx_read_req_pkts);
	estat->rx_read_res = le64_to_cpu(sb->rx_read_res_pkts);
	estat->rx_write_req = le64_to_cpu(sb->rx_write_req_pkts);
	estat->rx_send_req = le64_to_cpu(sb->rx_send_req_pkts);
	estat->rx_roce_good_pkts = le64_to_cpu(sb->rx_roce_good_pkts);
	estat->rx_roce_good_bytes = le64_to_cpu(sb->rx_roce_good_bytes);
	estat->rx_out_of_buffer = le64_to_cpu(sb->rx_out_of_buffer_pkts);
	estat->rx_out_of_sequence = le64_to_cpu(sb->rx_out_of_sequence_pkts);

bail:
	bnxt_qplib_rcfw_free_sbuf(rcfw, sbuf);
	return rc;
}
