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
int bnxt_qplib_get_dev_attr(struct bnxt_qplib_rcfw *rcfw,
			    struct bnxt_qplib_dev_attr *attr)
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
			"QPLIB: SP: QUERY_FUNC alloc side buffer failed");
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
	attr->max_qp_wqes -= BNXT_QPLIB_RESERVED_QP_WRS;
	attr->max_qp_sges = sb->max_sge;
	attr->max_cq = le32_to_cpu(sb->max_cq);
	attr->max_cq_wqes = le32_to_cpu(sb->max_cqe);
	attr->max_cq_sges = attr->max_qp_sges;
	attr->max_mr = le32_to_cpu(sb->max_mr);
	attr->max_mw = le32_to_cpu(sb->max_mw);

	attr->max_mr_size = le64_to_cpu(sb->max_mr_size);
	attr->max_pd = 64 * 1024;
	attr->max_raw_ethy_qp = le32_to_cpu(sb->max_raw_eth_qp);
	attr->max_ah = le32_to_cpu(sb->max_ah);

	attr->max_fmr = le32_to_cpu(sb->max_fmr);
	attr->max_map_per_fmr = sb->max_map_per_fmr;

	attr->max_srq = le16_to_cpu(sb->max_srq);
	attr->max_srq_wqes = le32_to_cpu(sb->max_srq_wr) - 1;
	attr->max_srq_sges = sb->max_srq_sge;
	/* Bono only reports 1 PKEY for now, but it can support > 1 */
	attr->max_pkey = le32_to_cpu(sb->max_pkeys);

	attr->max_inline_data = le32_to_cpu(sb->max_inline_data);
	attr->l2_db_size = (sb->l2_db_space_size + 1) * PAGE_SIZE;
	attr->max_sgid = le32_to_cpu(sb->max_gid);

	strlcpy(attr->fw_ver, "20.6.28.0", sizeof(attr->fw_ver));

	for (i = 0; i < MAX_TQM_ALLOC_REQ / 4; i++) {
		temp = le32_to_cpu(sb->tqm_alloc_reqs[i]);
		tqm_alloc = (u8 *)&temp;
		attr->tqm_alloc_reqs[i * 4] = *tqm_alloc;
		attr->tqm_alloc_reqs[i * 4 + 1] = *(++tqm_alloc);
		attr->tqm_alloc_reqs[i * 4 + 2] = *(++tqm_alloc);
		attr->tqm_alloc_reqs[i * 4 + 3] = *(++tqm_alloc);
	}

bail:
	bnxt_qplib_rcfw_free_sbuf(rcfw, sbuf);
	return rc;
}

/* SGID */
int bnxt_qplib_get_sgid(struct bnxt_qplib_res *res,
			struct bnxt_qplib_sgid_tbl *sgid_tbl, int index,
			struct bnxt_qplib_gid *gid)
{
	if (index > sgid_tbl->max) {
		dev_err(&res->pdev->dev,
			"QPLIB: Index %d exceeded SGID table max (%d)",
			index, sgid_tbl->max);
		return -EINVAL;
	}
	memcpy(gid, &sgid_tbl->tbl[index], sizeof(*gid));
	return 0;
}

int bnxt_qplib_del_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			struct bnxt_qplib_gid *gid, bool update)
{
	struct bnxt_qplib_res *res = to_bnxt_qplib(sgid_tbl,
						   struct bnxt_qplib_res,
						   sgid_tbl);
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	int index;

	if (!sgid_tbl) {
		dev_err(&res->pdev->dev, "QPLIB: SGID table not allocated");
		return -EINVAL;
	}
	/* Do we need a sgid_lock here? */
	if (!sgid_tbl->active) {
		dev_err(&res->pdev->dev,
			"QPLIB: SGID table has no active entries");
		return -ENOMEM;
	}
	for (index = 0; index < sgid_tbl->max; index++) {
		if (!memcmp(&sgid_tbl->tbl[index], gid, sizeof(*gid)))
			break;
	}
	if (index == sgid_tbl->max) {
		dev_warn(&res->pdev->dev, "GID not found in the SGID table");
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
				"QPLIB: GID entry contains an invalid HW id");
			return -EINVAL;
		}
		req.gid_index = cpu_to_le16(sgid_tbl->hw_id[index]);
		rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
						  (void *)&resp, NULL, 0);
		if (rc)
			return rc;
	}
	memcpy(&sgid_tbl->tbl[index], &bnxt_qplib_gid_zero,
	       sizeof(bnxt_qplib_gid_zero));
	sgid_tbl->active--;
	dev_dbg(&res->pdev->dev,
		"QPLIB: SGID deleted hw_id[0x%x] = 0x%x active = 0x%x",
		 index, sgid_tbl->hw_id[index], sgid_tbl->active);
	sgid_tbl->hw_id[index] = (u16)-1;

	/* unlock */
	return 0;
}

int bnxt_qplib_add_sgid(struct bnxt_qplib_sgid_tbl *sgid_tbl,
			struct bnxt_qplib_gid *gid, u8 *smac, u16 vlan_id,
			bool update, u32 *index)
{
	struct bnxt_qplib_res *res = to_bnxt_qplib(sgid_tbl,
						   struct bnxt_qplib_res,
						   sgid_tbl);
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	int i, free_idx;

	if (!sgid_tbl) {
		dev_err(&res->pdev->dev, "QPLIB: SGID table not allocated");
		return -EINVAL;
	}
	/* Do we need a sgid_lock here? */
	if (sgid_tbl->active == sgid_tbl->max) {
		dev_err(&res->pdev->dev, "QPLIB: SGID table is full");
		return -ENOMEM;
	}
	free_idx = sgid_tbl->max;
	for (i = 0; i < sgid_tbl->max; i++) {
		if (!memcmp(&sgid_tbl->tbl[i], gid, sizeof(*gid))) {
			dev_dbg(&res->pdev->dev,
				"QPLIB: SGID entry already exist in entry %d!",
				i);
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
			"QPLIB: SGID table is FULL but count is not MAX??");
		return -ENOMEM;
	}
	if (update) {
		struct cmdq_add_gid req;
		struct creq_add_gid_resp resp;
		u16 cmd_flags = 0;
		u32 temp32[4];
		u16 temp16[3];
		int rc;

		RCFW_CMD_PREP(req, ADD_GID, cmd_flags);

		memcpy(temp32, gid->data, sizeof(struct bnxt_qplib_gid));
		req.gid[0] = cpu_to_be32(temp32[3]);
		req.gid[1] = cpu_to_be32(temp32[2]);
		req.gid[2] = cpu_to_be32(temp32[1]);
		req.gid[3] = cpu_to_be32(temp32[0]);
		if (vlan_id != 0xFFFF)
			req.vlan = cpu_to_le16((vlan_id &
					CMDQ_ADD_GID_VLAN_VLAN_ID_MASK) |
					CMDQ_ADD_GID_VLAN_TPID_TPID_8100 |
					CMDQ_ADD_GID_VLAN_VLAN_EN);

		/* MAC in network format */
		memcpy(temp16, smac, 6);
		req.src_mac[0] = cpu_to_be16(temp16[0]);
		req.src_mac[1] = cpu_to_be16(temp16[1]);
		req.src_mac[2] = cpu_to_be16(temp16[2]);

		rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
						  (void *)&resp, NULL, 0);
		if (rc)
			return rc;
		sgid_tbl->hw_id[free_idx] = le32_to_cpu(resp.xid);
	}
	/* Add GID to the sgid_tbl */
	memcpy(&sgid_tbl->tbl[free_idx], gid, sizeof(*gid));
	sgid_tbl->active++;
	dev_dbg(&res->pdev->dev,
		"QPLIB: SGID added hw_id[0x%x] = 0x%x active = 0x%x",
		 free_idx, sgid_tbl->hw_id[free_idx], sgid_tbl->active);

	*index = free_idx;
	/* unlock */
	return 0;
}

/* pkeys */
int bnxt_qplib_get_pkey(struct bnxt_qplib_res *res,
			struct bnxt_qplib_pkey_tbl *pkey_tbl, u16 index,
			u16 *pkey)
{
	if (index == 0xFFFF) {
		*pkey = 0xFFFF;
		return 0;
	}
	if (index > pkey_tbl->max) {
		dev_err(&res->pdev->dev,
			"QPLIB: Index %d exceeded PKEY table max (%d)",
			index, pkey_tbl->max);
		return -EINVAL;
	}
	memcpy(pkey, &pkey_tbl->tbl[index], sizeof(*pkey));
	return 0;
}

int bnxt_qplib_del_pkey(struct bnxt_qplib_res *res,
			struct bnxt_qplib_pkey_tbl *pkey_tbl, u16 *pkey,
			bool update)
{
	int i, rc = 0;

	if (!pkey_tbl) {
		dev_err(&res->pdev->dev, "QPLIB: PKEY table not allocated");
		return -EINVAL;
	}

	/* Do we need a pkey_lock here? */
	if (!pkey_tbl->active) {
		dev_err(&res->pdev->dev,
			"QPLIB: PKEY table has no active entries");
		return -ENOMEM;
	}
	for (i = 0; i < pkey_tbl->max; i++) {
		if (!memcmp(&pkey_tbl->tbl[i], pkey, sizeof(*pkey)))
			break;
	}
	if (i == pkey_tbl->max) {
		dev_err(&res->pdev->dev,
			"QPLIB: PKEY 0x%04x not found in the pkey table",
			*pkey);
		return -ENOMEM;
	}
	memset(&pkey_tbl->tbl[i], 0, sizeof(*pkey));
	pkey_tbl->active--;

	/* unlock */
	return rc;
}

int bnxt_qplib_add_pkey(struct bnxt_qplib_res *res,
			struct bnxt_qplib_pkey_tbl *pkey_tbl, u16 *pkey,
			bool update)
{
	int i, free_idx, rc = 0;

	if (!pkey_tbl) {
		dev_err(&res->pdev->dev, "QPLIB: PKEY table not allocated");
		return -EINVAL;
	}

	/* Do we need a pkey_lock here? */
	if (pkey_tbl->active == pkey_tbl->max) {
		dev_err(&res->pdev->dev, "QPLIB: PKEY table is full");
		return -ENOMEM;
	}
	free_idx = pkey_tbl->max;
	for (i = 0; i < pkey_tbl->max; i++) {
		if (!memcmp(&pkey_tbl->tbl[i], pkey, sizeof(*pkey)))
			return -EALREADY;
		else if (!pkey_tbl->tbl[i] && free_idx == pkey_tbl->max)
			free_idx = i;
	}
	if (free_idx == pkey_tbl->max) {
		dev_err(&res->pdev->dev,
			"QPLIB: PKEY table is FULL but count is not MAX??");
		return -ENOMEM;
	}
	/* Add PKEY to the pkey_tbl */
	memcpy(&pkey_tbl->tbl[free_idx], pkey, sizeof(*pkey));
	pkey_tbl->active++;

	/* unlock */
	return rc;
}

/* AH */
int bnxt_qplib_create_ah(struct bnxt_qplib_res *res, struct bnxt_qplib_ah *ah)
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
					  NULL, 1);
	if (rc)
		return rc;

	ah->id = le32_to_cpu(resp.xid);
	return 0;
}

int bnxt_qplib_destroy_ah(struct bnxt_qplib_res *res, struct bnxt_qplib_ah *ah)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_destroy_ah req;
	struct creq_destroy_ah_resp resp;
	u16 cmd_flags = 0;
	int rc;

	/* Clean up the AH table in the device */
	RCFW_CMD_PREP(req, DESTROY_AH, cmd_flags);

	req.ah_cid = cpu_to_le32(ah->id);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  NULL, 1);
	if (rc)
		return rc;
	return 0;
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
		dev_info(&res->pdev->dev,
			 "QPLIB: SP: Free a reserved lkey MRW");
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
		bnxt_qplib_free_hwq(res->pdev, &mrw->hwq);

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
		bnxt_qplib_free_hwq(res->pdev, &mrw->hwq);
	}

	return 0;
}

int bnxt_qplib_reg_mr(struct bnxt_qplib_res *res, struct bnxt_qplib_mrw *mr,
		      u64 *pbl_tbl, int num_pbls, bool block)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_register_mr req;
	struct creq_register_mr_resp resp;
	u16 cmd_flags = 0, level;
	int pg_ptrs, pages, i, rc;
	dma_addr_t **pbl_ptr;
	u32 pg_size;

	if (num_pbls) {
		pg_ptrs = roundup_pow_of_two(num_pbls);
		pages = pg_ptrs >> MAX_PBL_LVL_1_PGS_SHIFT;
		if (!pages)
			pages++;

		if (pages > MAX_PBL_LVL_1_PGS) {
			dev_err(&res->pdev->dev, "QPLIB: SP: Reg MR pages ");
			dev_err(&res->pdev->dev,
				"requested (0x%x) exceeded max (0x%x)",
				pages, MAX_PBL_LVL_1_PGS);
			return -ENOMEM;
		}
		/* Free the hwq if it already exist, must be a rereg */
		if (mr->hwq.max_elements)
			bnxt_qplib_free_hwq(res->pdev, &mr->hwq);

		mr->hwq.max_elements = pages;
		rc = bnxt_qplib_alloc_init_hwq(res->pdev, &mr->hwq, NULL, 0,
					       &mr->hwq.max_elements,
					       PAGE_SIZE, 0, PAGE_SIZE,
					       HWQ_TYPE_CTX);
		if (rc) {
			dev_err(&res->pdev->dev,
				"SP: Reg MR memory allocation failed");
			return -ENOMEM;
		}
		/* Write to the hwq */
		pbl_ptr = (dma_addr_t **)mr->hwq.pbl_ptr;
		for (i = 0; i < num_pbls; i++)
			pbl_ptr[PTR_PG(i)][PTR_IDX(i)] =
				(pbl_tbl[i] & PAGE_MASK) | PTU_PTE_VALID;
	}

	RCFW_CMD_PREP(req, REGISTER_MR, cmd_flags);

	/* Configure the request */
	if (mr->hwq.level == PBL_LVL_MAX) {
		level = 0;
		req.pbl = 0;
		pg_size = PAGE_SIZE;
	} else {
		level = mr->hwq.level + 1;
		req.pbl = cpu_to_le64(mr->hwq.pbl[PBL_LVL_0].pg_map_arr[0]);
		pg_size = mr->hwq.pbl[PBL_LVL_0].pg_size;
	}
	req.log2_pg_size_lvl = (level << CMDQ_REGISTER_MR_LVL_SFT) |
			       ((ilog2(pg_size) <<
				 CMDQ_REGISTER_MR_LOG2_PG_SIZE_SFT) &
				CMDQ_REGISTER_MR_LOG2_PG_SIZE_MASK);
	req.access = (mr->flags & 0xFFFF);
	req.va = cpu_to_le64(mr->va);
	req.key = cpu_to_le32(mr->lkey);
	req.mr_size = cpu_to_le64(mr->total_size);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, block);
	if (rc)
		goto fail;

	return 0;

fail:
	if (mr->hwq.max_elements)
		bnxt_qplib_free_hwq(res->pdev, &mr->hwq);
	return rc;
}

int bnxt_qplib_alloc_fast_reg_page_list(struct bnxt_qplib_res *res,
					struct bnxt_qplib_frpl *frpl,
					int max_pg_ptrs)
{
	int pg_ptrs, pages, rc;

	/* Re-calculate the max to fit the HWQ allocation model */
	pg_ptrs = roundup_pow_of_two(max_pg_ptrs);
	pages = pg_ptrs >> MAX_PBL_LVL_1_PGS_SHIFT;
	if (!pages)
		pages++;

	if (pages > MAX_PBL_LVL_1_PGS)
		return -ENOMEM;

	frpl->hwq.max_elements = pages;
	rc = bnxt_qplib_alloc_init_hwq(res->pdev, &frpl->hwq, NULL, 0,
				       &frpl->hwq.max_elements, PAGE_SIZE, 0,
				       PAGE_SIZE, HWQ_TYPE_CTX);
	if (!rc)
		frpl->max_pg_ptrs = pg_ptrs;

	return rc;
}

int bnxt_qplib_free_fast_reg_page_list(struct bnxt_qplib_res *res,
				       struct bnxt_qplib_frpl *frpl)
{
	bnxt_qplib_free_hwq(res->pdev, &frpl->hwq);
	return 0;
}

int bnxt_qplib_map_tc2cos(struct bnxt_qplib_res *res, u16 *cids)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_map_tc_to_cos req;
	struct creq_map_tc_to_cos_resp resp;
	u16 cmd_flags = 0;
	int rc = 0;

	RCFW_CMD_PREP(req, MAP_TC_TO_COS, cmd_flags);
	req.cos0 = cpu_to_le16(cids[0]);
	req.cos1 = cpu_to_le16(cids[1]);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	return 0;
}
