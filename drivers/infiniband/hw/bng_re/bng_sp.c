// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.
#include <linux/interrupt.h>
#include <linux/pci.h>

#include "bng_res.h"
#include "bng_fw.h"
#include "bng_sp.h"
#include "bng_tlv.h"

static bool bng_re_is_atomic_cap(struct bng_re_rcfw *rcfw)
{
	u16 pcie_ctl2 = 0;

	pcie_capability_read_word(rcfw->pdev, PCI_EXP_DEVCTL2, &pcie_ctl2);
	return (pcie_ctl2 & PCI_EXP_DEVCTL2_ATOMIC_REQ);
}

static void bng_re_query_version(struct bng_re_rcfw *rcfw,
				 char *fw_ver)
{
	struct creq_query_version_resp resp = {};
	struct bng_re_cmdqmsg msg = {};
	struct cmdq_query_version req = {};
	int rc;

	bng_re_rcfw_cmd_prep((struct cmdq_base *)&req,
			     CMDQ_BASE_OPCODE_QUERY_VERSION,
			     sizeof(req));

	bng_re_fill_cmdqmsg(&msg, &req, &resp, NULL, sizeof(req), sizeof(resp), 0);
	rc = bng_re_rcfw_send_message(rcfw, &msg);
	if (rc)
		return;
	fw_ver[0] = resp.fw_maj;
	fw_ver[1] = resp.fw_minor;
	fw_ver[2] = resp.fw_bld;
	fw_ver[3] = resp.fw_rsvd;
}

int bng_re_get_dev_attr(struct bng_re_rcfw *rcfw)
{
	struct bng_re_dev_attr *attr = rcfw->res->dattr;
	struct creq_query_func_resp resp = {};
	struct bng_re_cmdqmsg msg = {};
	struct creq_query_func_resp_sb *sb;
	struct bng_re_rcfw_sbuf sbuf;
	struct cmdq_query_func req = {};
	u8 *tqm_alloc;
	int i, rc;
	u32 temp;

	bng_re_rcfw_cmd_prep((struct cmdq_base *)&req,
			     CMDQ_BASE_OPCODE_QUERY_FUNC,
			     sizeof(req));

	sbuf.size = ALIGN(sizeof(*sb), BNG_FW_CMDQE_UNITS);
	sbuf.sb = dma_alloc_coherent(&rcfw->pdev->dev, sbuf.size,
				     &sbuf.dma_addr, GFP_KERNEL);
	if (!sbuf.sb)
		return -ENOMEM;
	sb = sbuf.sb;
	req.resp_size = sbuf.size / BNG_FW_CMDQE_UNITS;
	bng_re_fill_cmdqmsg(&msg, &req, &resp, &sbuf, sizeof(req),
			    sizeof(resp), 0);
	rc = bng_re_rcfw_send_message(rcfw, &msg);
	if (rc)
		goto bail;
	/* Extract the context from the side buffer */
	attr->max_qp = le32_to_cpu(sb->max_qp);
	/* max_qp value reported by FW doesn't include the QP1 */
	attr->max_qp += 1;
	attr->max_qp_rd_atom =
		sb->max_qp_rd_atom > BNG_RE_MAX_OUT_RD_ATOM ?
		BNG_RE_MAX_OUT_RD_ATOM : sb->max_qp_rd_atom;
	attr->max_qp_init_rd_atom =
		sb->max_qp_init_rd_atom > BNG_RE_MAX_OUT_RD_ATOM ?
		BNG_RE_MAX_OUT_RD_ATOM : sb->max_qp_init_rd_atom;
	attr->max_qp_wqes = le16_to_cpu(sb->max_qp_wr) - 1;

	/* Adjust for max_qp_wqes for variable wqe */
	attr->max_qp_wqes = min_t(u32, attr->max_qp_wqes, BNG_VAR_MAX_WQE - 1);

	attr->max_qp_sges = min_t(u32, sb->max_sge_var_wqe, BNG_VAR_MAX_SGE);
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
	/*
	 * Read the max gid supported by HW.
	 * For each entry in HW  GID in HW table, we consume 2
	 * GID entries in the kernel GID table.  So max_gid reported
	 * to stack can be up to twice the value reported by the HW, up to 256 gids.
	 */
	attr->max_sgid = le32_to_cpu(sb->max_gid);
	attr->max_sgid = min_t(u32, BNG_RE_NUM_GIDS_SUPPORTED, 2 * attr->max_sgid);
	attr->dev_cap_flags = le16_to_cpu(sb->dev_cap_flags);
	attr->dev_cap_flags2 = le16_to_cpu(sb->dev_cap_ext_flags_2);

	if (_is_max_srq_ext_supported(attr->dev_cap_flags2))
		attr->max_srq += le16_to_cpu(sb->max_srq_ext);

	bng_re_query_version(rcfw, attr->fw_ver);
	for (i = 0; i < BNG_MAX_TQM_ALLOC_REQ / 4; i++) {
		temp = le32_to_cpu(sb->tqm_alloc_reqs[i]);
		tqm_alloc = (u8 *)&temp;
		attr->tqm_alloc_reqs[i * 4] = *tqm_alloc;
		attr->tqm_alloc_reqs[i * 4 + 1] = *(++tqm_alloc);
		attr->tqm_alloc_reqs[i * 4 + 2] = *(++tqm_alloc);
		attr->tqm_alloc_reqs[i * 4 + 3] = *(++tqm_alloc);
	}

	attr->max_dpi = le32_to_cpu(sb->max_dpi);
	attr->is_atomic = bng_re_is_atomic_cap(rcfw);
bail:
	dma_free_coherent(&rcfw->pdev->dev, sbuf.size,
			  sbuf.sb, sbuf.dma_addr);
	return rc;
}
