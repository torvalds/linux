/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_rdma.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"

/* QM constants */
#define QM_PQ_ELEMENT_SIZE	4 /* in bytes */

/* Doorbell-Queue constants */
#define DQ_RANGE_SHIFT		4
#define DQ_RANGE_ALIGN		BIT(DQ_RANGE_SHIFT)

/* Searcher constants */
#define SRC_MIN_NUM_ELEMS 256

/* Timers constants */
#define TM_SHIFT        7
#define TM_ALIGN        BIT(TM_SHIFT)
#define TM_ELEM_SIZE    4

#define ILT_DEFAULT_HW_P_SIZE	4

#define ILT_PAGE_IN_BYTES(hw_p_size)	(1U << ((hw_p_size) + 12))
#define ILT_CFG_REG(cli, reg)	PSWRQ2_REG_ ## cli ## _ ## reg ## _RT_OFFSET

/* ILT entry structure */
#define ILT_ENTRY_PHY_ADDR_MASK		(~0ULL >> 12)
#define ILT_ENTRY_PHY_ADDR_SHIFT	0
#define ILT_ENTRY_VALID_MASK		0x1ULL
#define ILT_ENTRY_VALID_SHIFT		52
#define ILT_ENTRY_IN_REGS		2
#define ILT_REG_SIZE_IN_BYTES		4

/* connection context union */
union conn_context {
	struct e4_core_conn_context core_ctx;
	struct e4_eth_conn_context eth_ctx;
	struct e4_iscsi_conn_context iscsi_ctx;
	struct e4_fcoe_conn_context fcoe_ctx;
	struct e4_roce_conn_context roce_ctx;
};

/* TYPE-0 task context - iSCSI, FCOE */
union type0_task_context {
	struct e4_iscsi_task_context iscsi_ctx;
	struct e4_fcoe_task_context fcoe_ctx;
};

/* TYPE-1 task context - ROCE */
union type1_task_context {
	struct e4_rdma_task_context roce_ctx;
};

struct src_ent {
	u8 opaque[56];
	u64 next;
};

#define CDUT_SEG_ALIGNMET		3 /* in 4k chunks */
#define CDUT_SEG_ALIGNMET_IN_BYTES	BIT(CDUT_SEG_ALIGNMET + 12)

#define CONN_CXT_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(union conn_context, p_hwfn)

#define SRQ_CXT_SIZE (sizeof(struct rdma_srq_context))
#define XRC_SRQ_CXT_SIZE (sizeof(struct rdma_xrc_srq_context))

#define TYPE0_TASK_CXT_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(union type0_task_context, p_hwfn)

/* Alignment is inherent to the type1_task_context structure */
#define TYPE1_TASK_CXT_SIZE(p_hwfn) sizeof(union type1_task_context)

static bool src_proto(enum protocol_type type)
{
	return type == PROTOCOLID_ISCSI ||
	       type == PROTOCOLID_FCOE ||
	       type == PROTOCOLID_IWARP;
}

static bool tm_cid_proto(enum protocol_type type)
{
	return type == PROTOCOLID_ISCSI ||
	       type == PROTOCOLID_FCOE ||
	       type == PROTOCOLID_ROCE ||
	       type == PROTOCOLID_IWARP;
}

static bool tm_tid_proto(enum protocol_type type)
{
	return type == PROTOCOLID_FCOE;
}

/* counts the iids for the CDU/CDUC ILT client configuration */
struct qed_cdu_iids {
	u32 pf_cids;
	u32 per_vf_cids;
};

static void qed_cxt_cdu_iids(struct qed_cxt_mngr *p_mngr,
			     struct qed_cdu_iids *iids)
{
	u32 type;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		iids->pf_cids += p_mngr->conn_cfg[type].cid_count;
		iids->per_vf_cids += p_mngr->conn_cfg[type].cids_per_vf;
	}
}

/* counts the iids for the Searcher block configuration */
struct qed_src_iids {
	u32 pf_cids;
	u32 per_vf_cids;
};

static void qed_cxt_src_iids(struct qed_cxt_mngr *p_mngr,
			     struct qed_src_iids *iids)
{
	u32 i;

	for (i = 0; i < MAX_CONN_TYPES; i++) {
		if (!src_proto(i))
			continue;

		iids->pf_cids += p_mngr->conn_cfg[i].cid_count;
		iids->per_vf_cids += p_mngr->conn_cfg[i].cids_per_vf;
	}

	/* Add L2 filtering filters in addition */
	iids->pf_cids += p_mngr->arfs_count;
}

/* counts the iids for the Timers block configuration */
struct qed_tm_iids {
	u32 pf_cids;
	u32 pf_tids[NUM_TASK_PF_SEGMENTS];	/* per segment */
	u32 pf_tids_total;
	u32 per_vf_cids;
	u32 per_vf_tids;
};

static void qed_cxt_tm_iids(struct qed_hwfn *p_hwfn,
			    struct qed_cxt_mngr *p_mngr,
			    struct qed_tm_iids *iids)
{
	bool tm_vf_required = false;
	bool tm_required = false;
	int i, j;

	/* Timers is a special case -> we don't count how many cids require
	 * timers but what's the max cid that will be used by the timer block.
	 * therefore we traverse in reverse order, and once we hit a protocol
	 * that requires the timers memory, we'll sum all the protocols up
	 * to that one.
	 */
	for (i = MAX_CONN_TYPES - 1; i >= 0; i--) {
		struct qed_conn_type_cfg *p_cfg = &p_mngr->conn_cfg[i];

		if (tm_cid_proto(i) || tm_required) {
			if (p_cfg->cid_count)
				tm_required = true;

			iids->pf_cids += p_cfg->cid_count;
		}

		if (tm_cid_proto(i) || tm_vf_required) {
			if (p_cfg->cids_per_vf)
				tm_vf_required = true;

			iids->per_vf_cids += p_cfg->cids_per_vf;
		}

		if (tm_tid_proto(i)) {
			struct qed_tid_seg *segs = p_cfg->tid_seg;

			/* for each segment there is at most one
			 * protocol for which count is not 0.
			 */
			for (j = 0; j < NUM_TASK_PF_SEGMENTS; j++)
				iids->pf_tids[j] += segs[j].count;

			/* The last array elelment is for the VFs. As for PF
			 * segments there can be only one protocol for
			 * which this value is not 0.
			 */
			iids->per_vf_tids += segs[NUM_TASK_PF_SEGMENTS].count;
		}
	}

	iids->pf_cids = roundup(iids->pf_cids, TM_ALIGN);
	iids->per_vf_cids = roundup(iids->per_vf_cids, TM_ALIGN);
	iids->per_vf_tids = roundup(iids->per_vf_tids, TM_ALIGN);

	for (iids->pf_tids_total = 0, j = 0; j < NUM_TASK_PF_SEGMENTS; j++) {
		iids->pf_tids[j] = roundup(iids->pf_tids[j], TM_ALIGN);
		iids->pf_tids_total += iids->pf_tids[j];
	}
}

static void qed_cxt_qm_iids(struct qed_hwfn *p_hwfn,
			    struct qed_qm_iids *iids)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_tid_seg *segs;
	u32 vf_cids = 0, type, j;
	u32 vf_tids = 0;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		iids->cids += p_mngr->conn_cfg[type].cid_count;
		vf_cids += p_mngr->conn_cfg[type].cids_per_vf;

		segs = p_mngr->conn_cfg[type].tid_seg;
		/* for each segment there is at most one
		 * protocol for which count is not 0.
		 */
		for (j = 0; j < NUM_TASK_PF_SEGMENTS; j++)
			iids->tids += segs[j].count;

		/* The last array elelment is for the VFs. As for PF
		 * segments there can be only one protocol for
		 * which this value is not 0.
		 */
		vf_tids += segs[NUM_TASK_PF_SEGMENTS].count;
	}

	iids->vf_cids = vf_cids;
	iids->tids += vf_tids * p_mngr->vf_count;

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "iids: CIDS %08x vf_cids %08x tids %08x vf_tids %08x\n",
		   iids->cids, iids->vf_cids, iids->tids, vf_tids);
}

static struct qed_tid_seg *qed_cxt_tid_seg_info(struct qed_hwfn *p_hwfn,
						u32 seg)
{
	struct qed_cxt_mngr *p_cfg = p_hwfn->p_cxt_mngr;
	u32 i;

	/* Find the protocol with tid count > 0 for this segment.
	 * Note: there can only be one and this is already validated.
	 */
	for (i = 0; i < MAX_CONN_TYPES; i++)
		if (p_cfg->conn_cfg[i].tid_seg[seg].count)
			return &p_cfg->conn_cfg[i].tid_seg[seg];
	return NULL;
}

static void qed_cxt_set_srq_count(struct qed_hwfn *p_hwfn,
				  u32 num_srqs, u32 num_xrc_srqs)
{
	struct qed_cxt_mngr *p_mgr = p_hwfn->p_cxt_mngr;

	p_mgr->srq_count = num_srqs;
	p_mgr->xrc_srq_count = num_xrc_srqs;
}

u32 qed_cxt_get_ilt_page_size(struct qed_hwfn *p_hwfn,
			      enum ilt_clients ilt_client)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_ilt_client_cfg *p_cli = &p_mngr->clients[ilt_client];

	return ILT_PAGE_IN_BYTES(p_cli->p_size.val);
}

static u32 qed_cxt_xrc_srqs_per_page(struct qed_hwfn *p_hwfn)
{
	u32 page_size;

	page_size = qed_cxt_get_ilt_page_size(p_hwfn, ILT_CLI_TSDM);
	return page_size / XRC_SRQ_CXT_SIZE;
}

u32 qed_cxt_get_total_srq_count(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mgr = p_hwfn->p_cxt_mngr;
	u32 total_srqs;

	total_srqs = p_mgr->srq_count + p_mgr->xrc_srq_count;

	return total_srqs;
}

/* set the iids count per protocol */
static void qed_cxt_set_proto_cid_count(struct qed_hwfn *p_hwfn,
					enum protocol_type type,
					u32 cid_count, u32 vf_cid_cnt)
{
	struct qed_cxt_mngr *p_mgr = p_hwfn->p_cxt_mngr;
	struct qed_conn_type_cfg *p_conn = &p_mgr->conn_cfg[type];

	p_conn->cid_count = roundup(cid_count, DQ_RANGE_ALIGN);
	p_conn->cids_per_vf = roundup(vf_cid_cnt, DQ_RANGE_ALIGN);

	if (type == PROTOCOLID_ROCE) {
		u32 page_sz = p_mgr->clients[ILT_CLI_CDUC].p_size.val;
		u32 cxt_size = CONN_CXT_SIZE(p_hwfn);
		u32 elems_per_page = ILT_PAGE_IN_BYTES(page_sz) / cxt_size;
		u32 align = elems_per_page * DQ_RANGE_ALIGN;

		p_conn->cid_count = roundup(p_conn->cid_count, align);
	}
}

u32 qed_cxt_get_proto_cid_count(struct qed_hwfn *p_hwfn,
				enum protocol_type type, u32 *vf_cid)
{
	if (vf_cid)
		*vf_cid = p_hwfn->p_cxt_mngr->conn_cfg[type].cids_per_vf;

	return p_hwfn->p_cxt_mngr->conn_cfg[type].cid_count;
}

u32 qed_cxt_get_proto_cid_start(struct qed_hwfn *p_hwfn,
				enum protocol_type type)
{
	return p_hwfn->p_cxt_mngr->acquired[type].start_cid;
}

u32 qed_cxt_get_proto_tid_count(struct qed_hwfn *p_hwfn,
				enum protocol_type type)
{
	u32 cnt = 0;
	int i;

	for (i = 0; i < TASK_SEGMENTS; i++)
		cnt += p_hwfn->p_cxt_mngr->conn_cfg[type].tid_seg[i].count;

	return cnt;
}

static void qed_cxt_set_proto_tid_count(struct qed_hwfn *p_hwfn,
					enum protocol_type proto,
					u8 seg,
					u8 seg_type, u32 count, bool has_fl)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_tid_seg *p_seg = &p_mngr->conn_cfg[proto].tid_seg[seg];

	p_seg->count = count;
	p_seg->has_fl_mem = has_fl;
	p_seg->type = seg_type;
}

static void qed_ilt_cli_blk_fill(struct qed_ilt_client_cfg *p_cli,
				 struct qed_ilt_cli_blk *p_blk,
				 u32 start_line, u32 total_size, u32 elem_size)
{
	u32 ilt_size = ILT_PAGE_IN_BYTES(p_cli->p_size.val);

	/* verify thatits called only once for each block */
	if (p_blk->total_size)
		return;

	p_blk->total_size = total_size;
	p_blk->real_size_in_page = 0;
	if (elem_size)
		p_blk->real_size_in_page = (ilt_size / elem_size) * elem_size;
	p_blk->start_line = start_line;
}

static void qed_ilt_cli_adv_line(struct qed_hwfn *p_hwfn,
				 struct qed_ilt_client_cfg *p_cli,
				 struct qed_ilt_cli_blk *p_blk,
				 u32 *p_line, enum ilt_clients client_id)
{
	if (!p_blk->total_size)
		return;

	if (!p_cli->active)
		p_cli->first.val = *p_line;

	p_cli->active = true;
	*p_line += DIV_ROUND_UP(p_blk->total_size, p_blk->real_size_in_page);
	p_cli->last.val = *p_line - 1;

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "ILT[Client %d] - Lines: [%08x - %08x]. Block - Size %08x [Real %08x] Start line %d\n",
		   client_id, p_cli->first.val,
		   p_cli->last.val, p_blk->total_size,
		   p_blk->real_size_in_page, p_blk->start_line);
}

static u32 qed_ilt_get_dynamic_line_cnt(struct qed_hwfn *p_hwfn,
					enum ilt_clients ilt_client)
{
	u32 cid_count = p_hwfn->p_cxt_mngr->conn_cfg[PROTOCOLID_ROCE].cid_count;
	struct qed_ilt_client_cfg *p_cli;
	u32 lines_to_skip = 0;
	u32 cxts_per_p;

	if (ilt_client == ILT_CLI_CDUC) {
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUC];

		cxts_per_p = ILT_PAGE_IN_BYTES(p_cli->p_size.val) /
		    (u32) CONN_CXT_SIZE(p_hwfn);

		lines_to_skip = cid_count / cxts_per_p;
	}

	return lines_to_skip;
}

static struct qed_ilt_client_cfg *qed_cxt_set_cli(struct qed_ilt_client_cfg
						  *p_cli)
{
	p_cli->active = false;
	p_cli->first.val = 0;
	p_cli->last.val = 0;
	return p_cli;
}

static struct qed_ilt_cli_blk *qed_cxt_set_blk(struct qed_ilt_cli_blk *p_blk)
{
	p_blk->total_size = 0;
	return p_blk;
}

static void qed_cxt_ilt_blk_reset(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *clients = p_hwfn->p_cxt_mngr->clients;
	u32 cli_idx, blk_idx;

	for (cli_idx = 0; cli_idx < MAX_ILT_CLIENTS; cli_idx++) {
		for (blk_idx = 0; blk_idx < ILT_CLI_PF_BLOCKS; blk_idx++)
			clients[cli_idx].pf_blks[blk_idx].total_size = 0;

		for (blk_idx = 0; blk_idx < ILT_CLI_VF_BLOCKS; blk_idx++)
			clients[cli_idx].vf_blks[blk_idx].total_size = 0;
	}
}

int qed_cxt_cfg_ilt_compute(struct qed_hwfn *p_hwfn, u32 *line_count)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 curr_line, total, i, task_size, line;
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	struct qed_cdu_iids cdu_iids;
	struct qed_src_iids src_iids;
	struct qed_qm_iids qm_iids;
	struct qed_tm_iids tm_iids;
	struct qed_tid_seg *p_seg;

	memset(&qm_iids, 0, sizeof(qm_iids));
	memset(&cdu_iids, 0, sizeof(cdu_iids));
	memset(&src_iids, 0, sizeof(src_iids));
	memset(&tm_iids, 0, sizeof(tm_iids));

	p_mngr->pf_start_line = RESC_START(p_hwfn, QED_ILT);

	/* Reset all ILT blocks at the beginning of ILT computing in order
	 * to prevent memory allocation for irrelevant blocks afterwards.
	 */
	qed_cxt_ilt_blk_reset(p_hwfn);

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "hwfn [%d] - Set context manager starting line to be 0x%08x\n",
		   p_hwfn->my_id, p_hwfn->p_cxt_mngr->pf_start_line);

	/* CDUC */
	p_cli = qed_cxt_set_cli(&p_mngr->clients[ILT_CLI_CDUC]);

	curr_line = p_mngr->pf_start_line;

	/* CDUC PF */
	p_cli->pf_total_lines = 0;

	/* get the counters for the CDUC and QM clients  */
	qed_cxt_cdu_iids(p_mngr, &cdu_iids);

	p_blk = qed_cxt_set_blk(&p_cli->pf_blks[CDUC_BLK]);

	total = cdu_iids.pf_cids * CONN_CXT_SIZE(p_hwfn);

	qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
			     total, CONN_CXT_SIZE(p_hwfn));

	qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line, ILT_CLI_CDUC);
	p_cli->pf_total_lines = curr_line - p_blk->start_line;

	p_blk->dynamic_line_cnt = qed_ilt_get_dynamic_line_cnt(p_hwfn,
							       ILT_CLI_CDUC);

	/* CDUC VF */
	p_blk = qed_cxt_set_blk(&p_cli->vf_blks[CDUC_BLK]);
	total = cdu_iids.per_vf_cids * CONN_CXT_SIZE(p_hwfn);

	qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
			     total, CONN_CXT_SIZE(p_hwfn));

	qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line, ILT_CLI_CDUC);
	p_cli->vf_total_lines = curr_line - p_blk->start_line;

	for (i = 1; i < p_mngr->vf_count; i++)
		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_CDUC);

	/* CDUT PF */
	p_cli = qed_cxt_set_cli(&p_mngr->clients[ILT_CLI_CDUT]);
	p_cli->first.val = curr_line;

	/* first the 'working' task memory */
	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		p_seg = qed_cxt_tid_seg_info(p_hwfn, i);
		if (!p_seg || p_seg->count == 0)
			continue;

		p_blk = qed_cxt_set_blk(&p_cli->pf_blks[CDUT_SEG_BLK(i)]);
		total = p_seg->count * p_mngr->task_type_size[p_seg->type];
		qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line, total,
				     p_mngr->task_type_size[p_seg->type]);

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_CDUT);
	}

	/* next the 'init' task memory (forced load memory) */
	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		p_seg = qed_cxt_tid_seg_info(p_hwfn, i);
		if (!p_seg || p_seg->count == 0)
			continue;

		p_blk =
		    qed_cxt_set_blk(&p_cli->pf_blks[CDUT_FL_SEG_BLK(i, PF)]);

		if (!p_seg->has_fl_mem) {
			/* The segment is active (total size pf 'working'
			 * memory is > 0) but has no FL (forced-load, Init)
			 * memory. Thus:
			 *
			 * 1.   The total-size in the corrsponding FL block of
			 *      the ILT client is set to 0 - No ILT line are
			 *      provisioned and no ILT memory allocated.
			 *
			 * 2.   The start-line of said block is set to the
			 *      start line of the matching working memory
			 *      block in the ILT client. This is later used to
			 *      configure the CDU segment offset registers and
			 *      results in an FL command for TIDs of this
			 *      segement behaves as regular load commands
			 *      (loading TIDs from the working memory).
			 */
			line = p_cli->pf_blks[CDUT_SEG_BLK(i)].start_line;

			qed_ilt_cli_blk_fill(p_cli, p_blk, line, 0, 0);
			continue;
		}
		total = p_seg->count * p_mngr->task_type_size[p_seg->type];

		qed_ilt_cli_blk_fill(p_cli, p_blk,
				     curr_line, total,
				     p_mngr->task_type_size[p_seg->type]);

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_CDUT);
	}
	p_cli->pf_total_lines = curr_line - p_cli->pf_blks[0].start_line;

	/* CDUT VF */
	p_seg = qed_cxt_tid_seg_info(p_hwfn, TASK_SEGMENT_VF);
	if (p_seg && p_seg->count) {
		/* Stricly speaking we need to iterate over all VF
		 * task segment types, but a VF has only 1 segment
		 */

		/* 'working' memory */
		total = p_seg->count * p_mngr->task_type_size[p_seg->type];

		p_blk = qed_cxt_set_blk(&p_cli->vf_blks[CDUT_SEG_BLK(0)]);
		qed_ilt_cli_blk_fill(p_cli, p_blk,
				     curr_line, total,
				     p_mngr->task_type_size[p_seg->type]);

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_CDUT);

		/* 'init' memory */
		p_blk =
		    qed_cxt_set_blk(&p_cli->vf_blks[CDUT_FL_SEG_BLK(0, VF)]);
		if (!p_seg->has_fl_mem) {
			/* see comment above */
			line = p_cli->vf_blks[CDUT_SEG_BLK(0)].start_line;
			qed_ilt_cli_blk_fill(p_cli, p_blk, line, 0, 0);
		} else {
			task_size = p_mngr->task_type_size[p_seg->type];
			qed_ilt_cli_blk_fill(p_cli, p_blk,
					     curr_line, total, task_size);
			qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
					     ILT_CLI_CDUT);
		}
		p_cli->vf_total_lines = curr_line -
		    p_cli->vf_blks[0].start_line;

		/* Now for the rest of the VFs */
		for (i = 1; i < p_mngr->vf_count; i++) {
			p_blk = &p_cli->vf_blks[CDUT_SEG_BLK(0)];
			qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
					     ILT_CLI_CDUT);

			p_blk = &p_cli->vf_blks[CDUT_FL_SEG_BLK(0, VF)];
			qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
					     ILT_CLI_CDUT);
		}
	}

	/* QM */
	p_cli = qed_cxt_set_cli(&p_mngr->clients[ILT_CLI_QM]);
	p_blk = qed_cxt_set_blk(&p_cli->pf_blks[0]);

	qed_cxt_qm_iids(p_hwfn, &qm_iids);
	total = qed_qm_pf_mem_size(qm_iids.cids,
				   qm_iids.vf_cids, qm_iids.tids,
				   p_hwfn->qm_info.num_pqs,
				   p_hwfn->qm_info.num_vf_pqs);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_ILT,
		   "QM ILT Info, (cids=%d, vf_cids=%d, tids=%d, num_pqs=%d, num_vf_pqs=%d, memory_size=%d)\n",
		   qm_iids.cids,
		   qm_iids.vf_cids,
		   qm_iids.tids,
		   p_hwfn->qm_info.num_pqs, p_hwfn->qm_info.num_vf_pqs, total);

	qed_ilt_cli_blk_fill(p_cli, p_blk,
			     curr_line, total * 0x1000,
			     QM_PQ_ELEMENT_SIZE);

	qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line, ILT_CLI_QM);
	p_cli->pf_total_lines = curr_line - p_blk->start_line;

	/* SRC */
	p_cli = qed_cxt_set_cli(&p_mngr->clients[ILT_CLI_SRC]);
	qed_cxt_src_iids(p_mngr, &src_iids);

	/* Both the PF and VFs searcher connections are stored in the per PF
	 * database. Thus sum the PF searcher cids and all the VFs searcher
	 * cids.
	 */
	total = src_iids.pf_cids + src_iids.per_vf_cids * p_mngr->vf_count;
	if (total) {
		u32 local_max = max_t(u32, total,
				      SRC_MIN_NUM_ELEMS);

		total = roundup_pow_of_two(local_max);

		p_blk = qed_cxt_set_blk(&p_cli->pf_blks[0]);
		qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
				     total * sizeof(struct src_ent),
				     sizeof(struct src_ent));

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_SRC);
		p_cli->pf_total_lines = curr_line - p_blk->start_line;
	}

	/* TM PF */
	p_cli = qed_cxt_set_cli(&p_mngr->clients[ILT_CLI_TM]);
	qed_cxt_tm_iids(p_hwfn, p_mngr, &tm_iids);
	total = tm_iids.pf_cids + tm_iids.pf_tids_total;
	if (total) {
		p_blk = qed_cxt_set_blk(&p_cli->pf_blks[0]);
		qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
				     total * TM_ELEM_SIZE, TM_ELEM_SIZE);

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_TM);
		p_cli->pf_total_lines = curr_line - p_blk->start_line;
	}

	/* TM VF */
	total = tm_iids.per_vf_cids + tm_iids.per_vf_tids;
	if (total) {
		p_blk = qed_cxt_set_blk(&p_cli->vf_blks[0]);
		qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
				     total * TM_ELEM_SIZE, TM_ELEM_SIZE);

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_TM);

		p_cli->vf_total_lines = curr_line - p_blk->start_line;
		for (i = 1; i < p_mngr->vf_count; i++)
			qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
					     ILT_CLI_TM);
	}

	/* TSDM (SRQ CONTEXT) */
	total = qed_cxt_get_total_srq_count(p_hwfn);

	if (total) {
		p_cli = qed_cxt_set_cli(&p_mngr->clients[ILT_CLI_TSDM]);
		p_blk = qed_cxt_set_blk(&p_cli->pf_blks[SRQ_BLK]);
		qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
				     total * SRQ_CXT_SIZE, SRQ_CXT_SIZE);

		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_TSDM);
		p_cli->pf_total_lines = curr_line - p_blk->start_line;
	}

	*line_count = curr_line - p_hwfn->p_cxt_mngr->pf_start_line;

	if (curr_line - p_hwfn->p_cxt_mngr->pf_start_line >
	    RESC_NUM(p_hwfn, QED_ILT))
		return -EINVAL;

	return 0;
}

u32 qed_cxt_cfg_ilt_compute_excess(struct qed_hwfn *p_hwfn, u32 used_lines)
{
	struct qed_ilt_client_cfg *p_cli;
	u32 excess_lines, available_lines;
	struct qed_cxt_mngr *p_mngr;
	u32 ilt_page_size, elem_size;
	struct qed_tid_seg *p_seg;
	int i;

	available_lines = RESC_NUM(p_hwfn, QED_ILT);
	excess_lines = used_lines - available_lines;

	if (!excess_lines)
		return 0;

	if (!QED_IS_RDMA_PERSONALITY(p_hwfn))
		return 0;

	p_mngr = p_hwfn->p_cxt_mngr;
	p_cli = &p_mngr->clients[ILT_CLI_CDUT];
	ilt_page_size = ILT_PAGE_IN_BYTES(p_cli->p_size.val);

	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		p_seg = qed_cxt_tid_seg_info(p_hwfn, i);
		if (!p_seg || p_seg->count == 0)
			continue;

		elem_size = p_mngr->task_type_size[p_seg->type];
		if (!elem_size)
			continue;

		return (ilt_page_size / elem_size) * excess_lines;
	}

	DP_NOTICE(p_hwfn, "failed computing excess ILT lines\n");
	return 0;
}

static void qed_cxt_src_t2_free(struct qed_hwfn *p_hwfn)
{
	struct qed_src_t2 *p_t2 = &p_hwfn->p_cxt_mngr->src_t2;
	u32 i;

	if (!p_t2 || !p_t2->dma_mem)
		return;

	for (i = 0; i < p_t2->num_pages; i++)
		if (p_t2->dma_mem[i].virt_addr)
			dma_free_coherent(&p_hwfn->cdev->pdev->dev,
					  p_t2->dma_mem[i].size,
					  p_t2->dma_mem[i].virt_addr,
					  p_t2->dma_mem[i].phys_addr);

	kfree(p_t2->dma_mem);
	p_t2->dma_mem = NULL;
}

static int
qed_cxt_t2_alloc_pages(struct qed_hwfn *p_hwfn,
		       struct qed_src_t2 *p_t2, u32 total_size, u32 page_size)
{
	void **p_virt;
	u32 size, i;

	if (!p_t2 || !p_t2->dma_mem)
		return -EINVAL;

	for (i = 0; i < p_t2->num_pages; i++) {
		size = min_t(u32, total_size, page_size);
		p_virt = &p_t2->dma_mem[i].virt_addr;

		*p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
					     size,
					     &p_t2->dma_mem[i].phys_addr,
					     GFP_KERNEL);
		if (!p_t2->dma_mem[i].virt_addr)
			return -ENOMEM;

		memset(*p_virt, 0, size);
		p_t2->dma_mem[i].size = size;
		total_size -= size;
	}

	return 0;
}

static int qed_cxt_src_t2_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 conn_num, total_size, ent_per_page, psz, i;
	struct phys_mem_desc *p_t2_last_page;
	struct qed_ilt_client_cfg *p_src;
	struct qed_src_iids src_iids;
	struct qed_src_t2 *p_t2;
	int rc;

	memset(&src_iids, 0, sizeof(src_iids));

	/* if the SRC ILT client is inactive - there are no connection
	 * requiring the searcer, leave.
	 */
	p_src = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_SRC];
	if (!p_src->active)
		return 0;

	qed_cxt_src_iids(p_mngr, &src_iids);
	conn_num = src_iids.pf_cids + src_iids.per_vf_cids * p_mngr->vf_count;
	total_size = conn_num * sizeof(struct src_ent);

	/* use the same page size as the SRC ILT client */
	psz = ILT_PAGE_IN_BYTES(p_src->p_size.val);
	p_t2 = &p_mngr->src_t2;
	p_t2->num_pages = DIV_ROUND_UP(total_size, psz);

	/* allocate t2 */
	p_t2->dma_mem = kcalloc(p_t2->num_pages, sizeof(struct phys_mem_desc),
				GFP_KERNEL);
	if (!p_t2->dma_mem) {
		DP_NOTICE(p_hwfn, "Failed to allocate t2 table\n");
		rc = -ENOMEM;
		goto t2_fail;
	}

	rc = qed_cxt_t2_alloc_pages(p_hwfn, p_t2, total_size, psz);
	if (rc)
		goto t2_fail;

	/* Set the t2 pointers */

	/* entries per page - must be a power of two */
	ent_per_page = psz / sizeof(struct src_ent);

	p_t2->first_free = (u64)p_t2->dma_mem[0].phys_addr;

	p_t2_last_page = &p_t2->dma_mem[(conn_num - 1) / ent_per_page];
	p_t2->last_free = (u64)p_t2_last_page->phys_addr +
	    ((conn_num - 1) & (ent_per_page - 1)) * sizeof(struct src_ent);

	for (i = 0; i < p_t2->num_pages; i++) {
		u32 ent_num = min_t(u32,
				    ent_per_page,
				    conn_num);
		struct src_ent *entries = p_t2->dma_mem[i].virt_addr;
		u64 p_ent_phys = (u64)p_t2->dma_mem[i].phys_addr, val;
		u32 j;

		for (j = 0; j < ent_num - 1; j++) {
			val = p_ent_phys + (j + 1) * sizeof(struct src_ent);
			entries[j].next = cpu_to_be64(val);
		}

		if (i < p_t2->num_pages - 1)
			val = (u64)p_t2->dma_mem[i + 1].phys_addr;
		else
			val = 0;
		entries[j].next = cpu_to_be64(val);

		conn_num -= ent_num;
	}

	return 0;

t2_fail:
	qed_cxt_src_t2_free(p_hwfn);
	return rc;
}

#define for_each_ilt_valid_client(pos, clients)	\
	for (pos = 0; pos < MAX_ILT_CLIENTS; pos++)	\
		if (!clients[pos].active) {	\
			continue;		\
		} else				\

/* Total number of ILT lines used by this PF */
static u32 qed_cxt_ilt_shadow_size(struct qed_ilt_client_cfg *ilt_clients)
{
	u32 size = 0;
	u32 i;

	for_each_ilt_valid_client(i, ilt_clients)
	    size += (ilt_clients[i].last.val - ilt_clients[i].first.val + 1);

	return size;
}

static void qed_ilt_shadow_free(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli = p_hwfn->p_cxt_mngr->clients;
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 ilt_size, i;

	ilt_size = qed_cxt_ilt_shadow_size(p_cli);

	for (i = 0; p_mngr->ilt_shadow && i < ilt_size; i++) {
		struct phys_mem_desc *p_dma = &p_mngr->ilt_shadow[i];

		if (p_dma->virt_addr)
			dma_free_coherent(&p_hwfn->cdev->pdev->dev,
					  p_dma->size, p_dma->virt_addr,
					  p_dma->phys_addr);
		p_dma->virt_addr = NULL;
	}
	kfree(p_mngr->ilt_shadow);
}

static int qed_ilt_blk_alloc(struct qed_hwfn *p_hwfn,
			     struct qed_ilt_cli_blk *p_blk,
			     enum ilt_clients ilt_client,
			     u32 start_line_offset)
{
	struct phys_mem_desc *ilt_shadow = p_hwfn->p_cxt_mngr->ilt_shadow;
	u32 lines, line, sz_left, lines_to_skip = 0;

	/* Special handling for RoCE that supports dynamic allocation */
	if (QED_IS_RDMA_PERSONALITY(p_hwfn) &&
	    ((ilt_client == ILT_CLI_CDUT) || ilt_client == ILT_CLI_TSDM))
		return 0;

	lines_to_skip = p_blk->dynamic_line_cnt;

	if (!p_blk->total_size)
		return 0;

	sz_left = p_blk->total_size;
	lines = DIV_ROUND_UP(sz_left, p_blk->real_size_in_page) - lines_to_skip;
	line = p_blk->start_line + start_line_offset -
	    p_hwfn->p_cxt_mngr->pf_start_line + lines_to_skip;

	for (; lines; lines--) {
		dma_addr_t p_phys;
		void *p_virt;
		u32 size;

		size = min_t(u32, sz_left, p_blk->real_size_in_page);
		p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev, size,
					    &p_phys, GFP_KERNEL);
		if (!p_virt)
			return -ENOMEM;

		ilt_shadow[line].phys_addr = p_phys;
		ilt_shadow[line].virt_addr = p_virt;
		ilt_shadow[line].size = size;

		DP_VERBOSE(p_hwfn, QED_MSG_ILT,
			   "ILT shadow: Line [%d] Physical 0x%llx Virtual %p Size %d\n",
			    line, (u64)p_phys, p_virt, size);

		sz_left -= size;
		line++;
	}

	return 0;
}

static int qed_ilt_shadow_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_ilt_client_cfg *clients = p_mngr->clients;
	struct qed_ilt_cli_blk *p_blk;
	u32 size, i, j, k;
	int rc;

	size = qed_cxt_ilt_shadow_size(clients);
	p_mngr->ilt_shadow = kcalloc(size, sizeof(struct phys_mem_desc),
				     GFP_KERNEL);
	if (!p_mngr->ilt_shadow) {
		rc = -ENOMEM;
		goto ilt_shadow_fail;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "Allocated 0x%x bytes for ilt shadow\n",
		   (u32)(size * sizeof(struct phys_mem_desc)));

	for_each_ilt_valid_client(i, clients) {
		for (j = 0; j < ILT_CLI_PF_BLOCKS; j++) {
			p_blk = &clients[i].pf_blks[j];
			rc = qed_ilt_blk_alloc(p_hwfn, p_blk, i, 0);
			if (rc)
				goto ilt_shadow_fail;
		}
		for (k = 0; k < p_mngr->vf_count; k++) {
			for (j = 0; j < ILT_CLI_VF_BLOCKS; j++) {
				u32 lines = clients[i].vf_total_lines * k;

				p_blk = &clients[i].vf_blks[j];
				rc = qed_ilt_blk_alloc(p_hwfn, p_blk, i, lines);
				if (rc)
					goto ilt_shadow_fail;
			}
		}
	}

	return 0;

ilt_shadow_fail:
	qed_ilt_shadow_free(p_hwfn);
	return rc;
}

static void qed_cid_map_free(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 type, vf;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		kfree(p_mngr->acquired[type].cid_map);
		p_mngr->acquired[type].max_count = 0;
		p_mngr->acquired[type].start_cid = 0;

		for (vf = 0; vf < MAX_NUM_VFS; vf++) {
			kfree(p_mngr->acquired_vf[type][vf].cid_map);
			p_mngr->acquired_vf[type][vf].max_count = 0;
			p_mngr->acquired_vf[type][vf].start_cid = 0;
		}
	}
}

static int
qed_cid_map_alloc_single(struct qed_hwfn *p_hwfn,
			 u32 type,
			 u32 cid_start,
			 u32 cid_count, struct qed_cid_acquired_map *p_map)
{
	u32 size;

	if (!cid_count)
		return 0;

	size = DIV_ROUND_UP(cid_count,
			    sizeof(unsigned long) * BITS_PER_BYTE) *
	       sizeof(unsigned long);
	p_map->cid_map = kzalloc(size, GFP_KERNEL);
	if (!p_map->cid_map)
		return -ENOMEM;

	p_map->max_count = cid_count;
	p_map->start_cid = cid_start;

	DP_VERBOSE(p_hwfn, QED_MSG_CXT,
		   "Type %08x start: %08x count %08x\n",
		   type, p_map->start_cid, p_map->max_count);

	return 0;
}

static int qed_cid_map_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 start_cid = 0, vf_start_cid = 0;
	u32 type, vf;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		struct qed_conn_type_cfg *p_cfg = &p_mngr->conn_cfg[type];
		struct qed_cid_acquired_map *p_map;

		/* Handle PF maps */
		p_map = &p_mngr->acquired[type];
		if (qed_cid_map_alloc_single(p_hwfn, type, start_cid,
					     p_cfg->cid_count, p_map))
			goto cid_map_fail;

		/* Handle VF maps */
		for (vf = 0; vf < MAX_NUM_VFS; vf++) {
			p_map = &p_mngr->acquired_vf[type][vf];
			if (qed_cid_map_alloc_single(p_hwfn, type,
						     vf_start_cid,
						     p_cfg->cids_per_vf, p_map))
				goto cid_map_fail;
		}

		start_cid += p_cfg->cid_count;
		vf_start_cid += p_cfg->cids_per_vf;
	}

	return 0;

cid_map_fail:
	qed_cid_map_free(p_hwfn);
	return -ENOMEM;
}

int qed_cxt_mngr_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *clients;
	struct qed_cxt_mngr *p_mngr;
	u32 i;

	p_mngr = kzalloc(sizeof(*p_mngr), GFP_KERNEL);
	if (!p_mngr)
		return -ENOMEM;

	/* Initialize ILT client registers */
	clients = p_mngr->clients;
	clients[ILT_CLI_CDUC].first.reg = ILT_CFG_REG(CDUC, FIRST_ILT);
	clients[ILT_CLI_CDUC].last.reg = ILT_CFG_REG(CDUC, LAST_ILT);
	clients[ILT_CLI_CDUC].p_size.reg = ILT_CFG_REG(CDUC, P_SIZE);

	clients[ILT_CLI_QM].first.reg = ILT_CFG_REG(QM, FIRST_ILT);
	clients[ILT_CLI_QM].last.reg = ILT_CFG_REG(QM, LAST_ILT);
	clients[ILT_CLI_QM].p_size.reg = ILT_CFG_REG(QM, P_SIZE);

	clients[ILT_CLI_TM].first.reg = ILT_CFG_REG(TM, FIRST_ILT);
	clients[ILT_CLI_TM].last.reg = ILT_CFG_REG(TM, LAST_ILT);
	clients[ILT_CLI_TM].p_size.reg = ILT_CFG_REG(TM, P_SIZE);

	clients[ILT_CLI_SRC].first.reg = ILT_CFG_REG(SRC, FIRST_ILT);
	clients[ILT_CLI_SRC].last.reg = ILT_CFG_REG(SRC, LAST_ILT);
	clients[ILT_CLI_SRC].p_size.reg = ILT_CFG_REG(SRC, P_SIZE);

	clients[ILT_CLI_CDUT].first.reg = ILT_CFG_REG(CDUT, FIRST_ILT);
	clients[ILT_CLI_CDUT].last.reg = ILT_CFG_REG(CDUT, LAST_ILT);
	clients[ILT_CLI_CDUT].p_size.reg = ILT_CFG_REG(CDUT, P_SIZE);

	clients[ILT_CLI_TSDM].first.reg = ILT_CFG_REG(TSDM, FIRST_ILT);
	clients[ILT_CLI_TSDM].last.reg = ILT_CFG_REG(TSDM, LAST_ILT);
	clients[ILT_CLI_TSDM].p_size.reg = ILT_CFG_REG(TSDM, P_SIZE);
	/* default ILT page size for all clients is 64K */
	for (i = 0; i < MAX_ILT_CLIENTS; i++)
		p_mngr->clients[i].p_size.val = ILT_DEFAULT_HW_P_SIZE;

	p_mngr->conn_ctx_size = CONN_CXT_SIZE(p_hwfn);

	/* Initialize task sizes */
	p_mngr->task_type_size[0] = TYPE0_TASK_CXT_SIZE(p_hwfn);
	p_mngr->task_type_size[1] = TYPE1_TASK_CXT_SIZE(p_hwfn);

	if (p_hwfn->cdev->p_iov_info) {
		p_mngr->vf_count = p_hwfn->cdev->p_iov_info->total_vfs;
		p_mngr->first_vf_in_pf =
			p_hwfn->cdev->p_iov_info->first_vf_in_pf;
	}
	/* Initialize the dynamic ILT allocation mutex */
	mutex_init(&p_mngr->mutex);

	/* Set the cxt mangr pointer priori to further allocations */
	p_hwfn->p_cxt_mngr = p_mngr;

	return 0;
}

int qed_cxt_tables_alloc(struct qed_hwfn *p_hwfn)
{
	int rc;

	/* Allocate the ILT shadow table */
	rc = qed_ilt_shadow_alloc(p_hwfn);
	if (rc)
		goto tables_alloc_fail;

	/* Allocate the T2  table */
	rc = qed_cxt_src_t2_alloc(p_hwfn);
	if (rc)
		goto tables_alloc_fail;

	/* Allocate and initialize the acquired cids bitmaps */
	rc = qed_cid_map_alloc(p_hwfn);
	if (rc)
		goto tables_alloc_fail;

	return 0;

tables_alloc_fail:
	qed_cxt_mngr_free(p_hwfn);
	return rc;
}

void qed_cxt_mngr_free(struct qed_hwfn *p_hwfn)
{
	if (!p_hwfn->p_cxt_mngr)
		return;

	qed_cid_map_free(p_hwfn);
	qed_cxt_src_t2_free(p_hwfn);
	qed_ilt_shadow_free(p_hwfn);
	kfree(p_hwfn->p_cxt_mngr);

	p_hwfn->p_cxt_mngr = NULL;
}

void qed_cxt_mngr_setup(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_cid_acquired_map *p_map;
	struct qed_conn_type_cfg *p_cfg;
	int type;
	u32 len;

	/* Reset acquired cids */
	for (type = 0; type < MAX_CONN_TYPES; type++) {
		u32 vf;

		p_cfg = &p_mngr->conn_cfg[type];
		if (p_cfg->cid_count) {
			p_map = &p_mngr->acquired[type];
			len = DIV_ROUND_UP(p_map->max_count,
					   sizeof(unsigned long) *
					   BITS_PER_BYTE) *
			      sizeof(unsigned long);
			memset(p_map->cid_map, 0, len);
		}

		if (!p_cfg->cids_per_vf)
			continue;

		for (vf = 0; vf < MAX_NUM_VFS; vf++) {
			p_map = &p_mngr->acquired_vf[type][vf];
			len = DIV_ROUND_UP(p_map->max_count,
					   sizeof(unsigned long) *
					   BITS_PER_BYTE) *
			      sizeof(unsigned long);
			memset(p_map->cid_map, 0, len);
		}
	}
}

/* CDU Common */
#define CDUC_CXT_SIZE_SHIFT \
	CDU_REG_CID_ADDR_PARAMS_CONTEXT_SIZE_SHIFT

#define CDUC_CXT_SIZE_MASK \
	(CDU_REG_CID_ADDR_PARAMS_CONTEXT_SIZE >> CDUC_CXT_SIZE_SHIFT)

#define CDUC_BLOCK_WASTE_SHIFT \
	CDU_REG_CID_ADDR_PARAMS_BLOCK_WASTE_SHIFT

#define CDUC_BLOCK_WASTE_MASK \
	(CDU_REG_CID_ADDR_PARAMS_BLOCK_WASTE >> CDUC_BLOCK_WASTE_SHIFT)

#define CDUC_NCIB_SHIFT	\
	CDU_REG_CID_ADDR_PARAMS_NCIB_SHIFT

#define CDUC_NCIB_MASK \
	(CDU_REG_CID_ADDR_PARAMS_NCIB >> CDUC_NCIB_SHIFT)

#define CDUT_TYPE0_CXT_SIZE_SHIFT \
	CDU_REG_SEGMENT0_PARAMS_T0_TID_SIZE_SHIFT

#define CDUT_TYPE0_CXT_SIZE_MASK		\
	(CDU_REG_SEGMENT0_PARAMS_T0_TID_SIZE >>	\
	 CDUT_TYPE0_CXT_SIZE_SHIFT)

#define CDUT_TYPE0_BLOCK_WASTE_SHIFT \
	CDU_REG_SEGMENT0_PARAMS_T0_TID_BLOCK_WASTE_SHIFT

#define CDUT_TYPE0_BLOCK_WASTE_MASK		       \
	(CDU_REG_SEGMENT0_PARAMS_T0_TID_BLOCK_WASTE >> \
	 CDUT_TYPE0_BLOCK_WASTE_SHIFT)

#define CDUT_TYPE0_NCIB_SHIFT \
	CDU_REG_SEGMENT0_PARAMS_T0_NUM_TIDS_IN_BLOCK_SHIFT

#define CDUT_TYPE0_NCIB_MASK				 \
	(CDU_REG_SEGMENT0_PARAMS_T0_NUM_TIDS_IN_BLOCK >> \
	 CDUT_TYPE0_NCIB_SHIFT)

#define CDUT_TYPE1_CXT_SIZE_SHIFT \
	CDU_REG_SEGMENT1_PARAMS_T1_TID_SIZE_SHIFT

#define CDUT_TYPE1_CXT_SIZE_MASK		\
	(CDU_REG_SEGMENT1_PARAMS_T1_TID_SIZE >>	\
	 CDUT_TYPE1_CXT_SIZE_SHIFT)

#define CDUT_TYPE1_BLOCK_WASTE_SHIFT \
	CDU_REG_SEGMENT1_PARAMS_T1_TID_BLOCK_WASTE_SHIFT

#define CDUT_TYPE1_BLOCK_WASTE_MASK		       \
	(CDU_REG_SEGMENT1_PARAMS_T1_TID_BLOCK_WASTE >> \
	 CDUT_TYPE1_BLOCK_WASTE_SHIFT)

#define CDUT_TYPE1_NCIB_SHIFT \
	CDU_REG_SEGMENT1_PARAMS_T1_NUM_TIDS_IN_BLOCK_SHIFT

#define CDUT_TYPE1_NCIB_MASK				 \
	(CDU_REG_SEGMENT1_PARAMS_T1_NUM_TIDS_IN_BLOCK >> \
	 CDUT_TYPE1_NCIB_SHIFT)

static void qed_cdu_init_common(struct qed_hwfn *p_hwfn)
{
	u32 page_sz, elems_per_page, block_waste, cxt_size, cdu_params = 0;

	/* CDUC - connection configuration */
	page_sz = p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUC].p_size.val;
	cxt_size = CONN_CXT_SIZE(p_hwfn);
	elems_per_page = ILT_PAGE_IN_BYTES(page_sz) / cxt_size;
	block_waste = ILT_PAGE_IN_BYTES(page_sz) - elems_per_page * cxt_size;

	SET_FIELD(cdu_params, CDUC_CXT_SIZE, cxt_size);
	SET_FIELD(cdu_params, CDUC_BLOCK_WASTE, block_waste);
	SET_FIELD(cdu_params, CDUC_NCIB, elems_per_page);
	STORE_RT_REG(p_hwfn, CDU_REG_CID_ADDR_PARAMS_RT_OFFSET, cdu_params);

	/* CDUT - type-0 tasks configuration */
	page_sz = p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT].p_size.val;
	cxt_size = p_hwfn->p_cxt_mngr->task_type_size[0];
	elems_per_page = ILT_PAGE_IN_BYTES(page_sz) / cxt_size;
	block_waste = ILT_PAGE_IN_BYTES(page_sz) - elems_per_page * cxt_size;

	/* cxt size and block-waste are multipes of 8 */
	cdu_params = 0;
	SET_FIELD(cdu_params, CDUT_TYPE0_CXT_SIZE, (cxt_size >> 3));
	SET_FIELD(cdu_params, CDUT_TYPE0_BLOCK_WASTE, (block_waste >> 3));
	SET_FIELD(cdu_params, CDUT_TYPE0_NCIB, elems_per_page);
	STORE_RT_REG(p_hwfn, CDU_REG_SEGMENT0_PARAMS_RT_OFFSET, cdu_params);

	/* CDUT - type-1 tasks configuration */
	cxt_size = p_hwfn->p_cxt_mngr->task_type_size[1];
	elems_per_page = ILT_PAGE_IN_BYTES(page_sz) / cxt_size;
	block_waste = ILT_PAGE_IN_BYTES(page_sz) - elems_per_page * cxt_size;

	/* cxt size and block-waste are multipes of 8 */
	cdu_params = 0;
	SET_FIELD(cdu_params, CDUT_TYPE1_CXT_SIZE, (cxt_size >> 3));
	SET_FIELD(cdu_params, CDUT_TYPE1_BLOCK_WASTE, (block_waste >> 3));
	SET_FIELD(cdu_params, CDUT_TYPE1_NCIB, elems_per_page);
	STORE_RT_REG(p_hwfn, CDU_REG_SEGMENT1_PARAMS_RT_OFFSET, cdu_params);
}

/* CDU PF */
#define CDU_SEG_REG_TYPE_SHIFT          CDU_SEG_TYPE_OFFSET_REG_TYPE_SHIFT
#define CDU_SEG_REG_TYPE_MASK           0x1
#define CDU_SEG_REG_OFFSET_SHIFT        0
#define CDU_SEG_REG_OFFSET_MASK         CDU_SEG_TYPE_OFFSET_REG_OFFSET_MASK

static void qed_cdu_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli;
	struct qed_tid_seg *p_seg;
	u32 cdu_seg_params, offset;
	int i;

	static const u32 rt_type_offset_arr[] = {
		CDU_REG_PF_SEG0_TYPE_OFFSET_RT_OFFSET,
		CDU_REG_PF_SEG1_TYPE_OFFSET_RT_OFFSET,
		CDU_REG_PF_SEG2_TYPE_OFFSET_RT_OFFSET,
		CDU_REG_PF_SEG3_TYPE_OFFSET_RT_OFFSET
	};

	static const u32 rt_type_offset_fl_arr[] = {
		CDU_REG_PF_FL_SEG0_TYPE_OFFSET_RT_OFFSET,
		CDU_REG_PF_FL_SEG1_TYPE_OFFSET_RT_OFFSET,
		CDU_REG_PF_FL_SEG2_TYPE_OFFSET_RT_OFFSET,
		CDU_REG_PF_FL_SEG3_TYPE_OFFSET_RT_OFFSET
	};

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];

	/* There are initializations only for CDUT during pf Phase */
	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		/* Segment 0 */
		p_seg = qed_cxt_tid_seg_info(p_hwfn, i);
		if (!p_seg)
			continue;

		/* Note: start_line is already adjusted for the CDU
		 * segment register granularity, so we just need to
		 * divide. Adjustment is implicit as we assume ILT
		 * Page size is larger than 32K!
		 */
		offset = (ILT_PAGE_IN_BYTES(p_cli->p_size.val) *
			  (p_cli->pf_blks[CDUT_SEG_BLK(i)].start_line -
			   p_cli->first.val)) / CDUT_SEG_ALIGNMET_IN_BYTES;

		cdu_seg_params = 0;
		SET_FIELD(cdu_seg_params, CDU_SEG_REG_TYPE, p_seg->type);
		SET_FIELD(cdu_seg_params, CDU_SEG_REG_OFFSET, offset);
		STORE_RT_REG(p_hwfn, rt_type_offset_arr[i], cdu_seg_params);

		offset = (ILT_PAGE_IN_BYTES(p_cli->p_size.val) *
			  (p_cli->pf_blks[CDUT_FL_SEG_BLK(i, PF)].start_line -
			   p_cli->first.val)) / CDUT_SEG_ALIGNMET_IN_BYTES;

		cdu_seg_params = 0;
		SET_FIELD(cdu_seg_params, CDU_SEG_REG_TYPE, p_seg->type);
		SET_FIELD(cdu_seg_params, CDU_SEG_REG_OFFSET, offset);
		STORE_RT_REG(p_hwfn, rt_type_offset_fl_arr[i], cdu_seg_params);
	}
}

void qed_qm_init_pf(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, bool is_pf_loading)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct qed_qm_pf_rt_init_params params;
	struct qed_qm_iids iids;

	memset(&iids, 0, sizeof(iids));
	qed_cxt_qm_iids(p_hwfn, &iids);

	memset(&params, 0, sizeof(params));
	params.port_id = p_hwfn->port_id;
	params.pf_id = p_hwfn->rel_pf_id;
	params.max_phys_tcs_per_port = qm_info->max_phys_tcs_per_port;
	params.is_pf_loading = is_pf_loading;
	params.num_pf_cids = iids.cids;
	params.num_vf_cids = iids.vf_cids;
	params.num_tids = iids.tids;
	params.start_pq = qm_info->start_pq;
	params.num_pf_pqs = qm_info->num_pqs - qm_info->num_vf_pqs;
	params.num_vf_pqs = qm_info->num_vf_pqs;
	params.start_vport = qm_info->start_vport;
	params.num_vports = qm_info->num_vports;
	params.pf_wfq = qm_info->pf_wfq;
	params.pf_rl = qm_info->pf_rl;
	params.pq_params = qm_info->qm_pq_params;
	params.vport_params = qm_info->qm_vport_params;

	qed_qm_pf_rt_init(p_hwfn, p_ptt, &params);
}

/* CM PF */
static void qed_cm_init_pf(struct qed_hwfn *p_hwfn)
{
	/* XCM pure-LB queue */
	STORE_RT_REG(p_hwfn, XCM_REG_CON_PHY_Q3_RT_OFFSET,
		     qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_LB));
}

/* DQ PF */
static void qed_dq_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 dq_pf_max_cid = 0, dq_vf_max_cid = 0;

	dq_pf_max_cid += (p_mngr->conn_cfg[0].cid_count >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_0_RT_OFFSET, dq_pf_max_cid);

	dq_vf_max_cid += (p_mngr->conn_cfg[0].cids_per_vf >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_0_RT_OFFSET, dq_vf_max_cid);

	dq_pf_max_cid += (p_mngr->conn_cfg[1].cid_count >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_1_RT_OFFSET, dq_pf_max_cid);

	dq_vf_max_cid += (p_mngr->conn_cfg[1].cids_per_vf >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_1_RT_OFFSET, dq_vf_max_cid);

	dq_pf_max_cid += (p_mngr->conn_cfg[2].cid_count >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_2_RT_OFFSET, dq_pf_max_cid);

	dq_vf_max_cid += (p_mngr->conn_cfg[2].cids_per_vf >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_2_RT_OFFSET, dq_vf_max_cid);

	dq_pf_max_cid += (p_mngr->conn_cfg[3].cid_count >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_3_RT_OFFSET, dq_pf_max_cid);

	dq_vf_max_cid += (p_mngr->conn_cfg[3].cids_per_vf >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_3_RT_OFFSET, dq_vf_max_cid);

	dq_pf_max_cid += (p_mngr->conn_cfg[4].cid_count >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_4_RT_OFFSET, dq_pf_max_cid);

	dq_vf_max_cid += (p_mngr->conn_cfg[4].cids_per_vf >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_4_RT_OFFSET, dq_vf_max_cid);

	dq_pf_max_cid += (p_mngr->conn_cfg[5].cid_count >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_5_RT_OFFSET, dq_pf_max_cid);

	dq_vf_max_cid += (p_mngr->conn_cfg[5].cids_per_vf >> DQ_RANGE_SHIFT);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_5_RT_OFFSET, dq_vf_max_cid);

	/* Connection types 6 & 7 are not in use, yet they must be configured
	 * as the highest possible connection. Not configuring them means the
	 * defaults will be  used, and with a large number of cids a bug may
	 * occur, if the defaults will be smaller than dq_pf_max_cid /
	 * dq_vf_max_cid.
	 */
	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_6_RT_OFFSET, dq_pf_max_cid);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_6_RT_OFFSET, dq_vf_max_cid);

	STORE_RT_REG(p_hwfn, DORQ_REG_PF_MAX_ICID_7_RT_OFFSET, dq_pf_max_cid);
	STORE_RT_REG(p_hwfn, DORQ_REG_VF_MAX_ICID_7_RT_OFFSET, dq_vf_max_cid);
}

static void qed_ilt_bounds_init(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *ilt_clients;
	int i;

	ilt_clients = p_hwfn->p_cxt_mngr->clients;
	for_each_ilt_valid_client(i, ilt_clients) {
		STORE_RT_REG(p_hwfn,
			     ilt_clients[i].first.reg,
			     ilt_clients[i].first.val);
		STORE_RT_REG(p_hwfn,
			     ilt_clients[i].last.reg, ilt_clients[i].last.val);
		STORE_RT_REG(p_hwfn,
			     ilt_clients[i].p_size.reg,
			     ilt_clients[i].p_size.val);
	}
}

static void qed_ilt_vf_bounds_init(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli;
	u32 blk_factor;

	/* For simplicty  we set the 'block' to be an ILT page */
	if (p_hwfn->cdev->p_iov_info) {
		struct qed_hw_sriov_info *p_iov = p_hwfn->cdev->p_iov_info;

		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_VF_BASE_RT_OFFSET,
			     p_iov->first_vf_in_pf);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_VF_LAST_ILT_RT_OFFSET,
			     p_iov->first_vf_in_pf + p_iov->total_vfs);
	}

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUC];
	blk_factor = ilog2(ILT_PAGE_IN_BYTES(p_cli->p_size.val) >> 10);
	if (p_cli->active) {
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_CDUC_BLOCKS_FACTOR_RT_OFFSET,
			     blk_factor);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_CDUC_NUMBER_OF_PF_BLOCKS_RT_OFFSET,
			     p_cli->pf_total_lines);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_CDUC_VF_BLOCKS_RT_OFFSET,
			     p_cli->vf_total_lines);
	}

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
	blk_factor = ilog2(ILT_PAGE_IN_BYTES(p_cli->p_size.val) >> 10);
	if (p_cli->active) {
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_CDUT_BLOCKS_FACTOR_RT_OFFSET,
			     blk_factor);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_CDUT_NUMBER_OF_PF_BLOCKS_RT_OFFSET,
			     p_cli->pf_total_lines);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_CDUT_VF_BLOCKS_RT_OFFSET,
			     p_cli->vf_total_lines);
	}

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_TM];
	blk_factor = ilog2(ILT_PAGE_IN_BYTES(p_cli->p_size.val) >> 10);
	if (p_cli->active) {
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_TM_BLOCKS_FACTOR_RT_OFFSET, blk_factor);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_TM_NUMBER_OF_PF_BLOCKS_RT_OFFSET,
			     p_cli->pf_total_lines);
		STORE_RT_REG(p_hwfn,
			     PSWRQ2_REG_TM_VF_BLOCKS_RT_OFFSET,
			     p_cli->vf_total_lines);
	}
}

/* ILT (PSWRQ2) PF */
static void qed_ilt_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *clients;
	struct qed_cxt_mngr *p_mngr;
	struct phys_mem_desc *p_shdw;
	u32 line, rt_offst, i;

	qed_ilt_bounds_init(p_hwfn);
	qed_ilt_vf_bounds_init(p_hwfn);

	p_mngr = p_hwfn->p_cxt_mngr;
	p_shdw = p_mngr->ilt_shadow;
	clients = p_hwfn->p_cxt_mngr->clients;

	for_each_ilt_valid_client(i, clients) {
		/** Client's 1st val and RT array are absolute, ILT shadows'
		 *  lines are relative.
		 */
		line = clients[i].first.val - p_mngr->pf_start_line;
		rt_offst = PSWRQ2_REG_ILT_MEMORY_RT_OFFSET +
			   clients[i].first.val * ILT_ENTRY_IN_REGS;

		for (; line <= clients[i].last.val - p_mngr->pf_start_line;
		     line++, rt_offst += ILT_ENTRY_IN_REGS) {
			u64 ilt_hw_entry = 0;

			/** p_virt could be NULL incase of dynamic
			 *  allocation
			 */
			if (p_shdw[line].virt_addr) {
				SET_FIELD(ilt_hw_entry, ILT_ENTRY_VALID, 1ULL);
				SET_FIELD(ilt_hw_entry, ILT_ENTRY_PHY_ADDR,
					  (p_shdw[line].phys_addr >> 12));

				DP_VERBOSE(p_hwfn, QED_MSG_ILT,
					   "Setting RT[0x%08x] from ILT[0x%08x] [Client is %d] to Physical addr: 0x%llx\n",
					   rt_offst, line, i,
					   (u64)(p_shdw[line].phys_addr >> 12));
			}

			STORE_RT_REG_AGG(p_hwfn, rt_offst, ilt_hw_entry);
		}
	}
}

/* SRC (Searcher) PF */
static void qed_src_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 rounded_conn_num, conn_num, conn_max;
	struct qed_src_iids src_iids;

	memset(&src_iids, 0, sizeof(src_iids));
	qed_cxt_src_iids(p_mngr, &src_iids);
	conn_num = src_iids.pf_cids + src_iids.per_vf_cids * p_mngr->vf_count;
	if (!conn_num)
		return;

	conn_max = max_t(u32, conn_num, SRC_MIN_NUM_ELEMS);
	rounded_conn_num = roundup_pow_of_two(conn_max);

	STORE_RT_REG(p_hwfn, SRC_REG_COUNTFREE_RT_OFFSET, conn_num);
	STORE_RT_REG(p_hwfn, SRC_REG_NUMBER_HASH_BITS_RT_OFFSET,
		     ilog2(rounded_conn_num));

	STORE_RT_REG_AGG(p_hwfn, SRC_REG_FIRSTFREE_RT_OFFSET,
			 p_hwfn->p_cxt_mngr->first_free);
	STORE_RT_REG_AGG(p_hwfn, SRC_REG_LASTFREE_RT_OFFSET,
			 p_hwfn->p_cxt_mngr->last_free);
}

/* Timers PF */
#define TM_CFG_NUM_IDS_SHIFT            0
#define TM_CFG_NUM_IDS_MASK             0xFFFFULL
#define TM_CFG_PRE_SCAN_OFFSET_SHIFT    16
#define TM_CFG_PRE_SCAN_OFFSET_MASK     0x1FFULL
#define TM_CFG_PARENT_PF_SHIFT          25
#define TM_CFG_PARENT_PF_MASK           0x7ULL

#define TM_CFG_CID_PRE_SCAN_ROWS_SHIFT  30
#define TM_CFG_CID_PRE_SCAN_ROWS_MASK   0x1FFULL

#define TM_CFG_TID_OFFSET_SHIFT         30
#define TM_CFG_TID_OFFSET_MASK          0x7FFFFULL
#define TM_CFG_TID_PRE_SCAN_ROWS_SHIFT  49
#define TM_CFG_TID_PRE_SCAN_ROWS_MASK   0x1FFULL

static void qed_tm_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 active_seg_mask = 0, tm_offset, rt_reg;
	struct qed_tm_iids tm_iids;
	u64 cfg_word;
	u8 i;

	memset(&tm_iids, 0, sizeof(tm_iids));
	qed_cxt_tm_iids(p_hwfn, p_mngr, &tm_iids);

	/* @@@TBD No pre-scan for now */

	/* Note: We assume consecutive VFs for a PF */
	for (i = 0; i < p_mngr->vf_count; i++) {
		cfg_word = 0;
		SET_FIELD(cfg_word, TM_CFG_NUM_IDS, tm_iids.per_vf_cids);
		SET_FIELD(cfg_word, TM_CFG_PRE_SCAN_OFFSET, 0);
		SET_FIELD(cfg_word, TM_CFG_PARENT_PF, p_hwfn->rel_pf_id);
		SET_FIELD(cfg_word, TM_CFG_CID_PRE_SCAN_ROWS, 0);
		rt_reg = TM_REG_CONFIG_CONN_MEM_RT_OFFSET +
		    (sizeof(cfg_word) / sizeof(u32)) *
		    (p_hwfn->cdev->p_iov_info->first_vf_in_pf + i);
		STORE_RT_REG_AGG(p_hwfn, rt_reg, cfg_word);
	}

	cfg_word = 0;
	SET_FIELD(cfg_word, TM_CFG_NUM_IDS, tm_iids.pf_cids);
	SET_FIELD(cfg_word, TM_CFG_PRE_SCAN_OFFSET, 0);
	SET_FIELD(cfg_word, TM_CFG_PARENT_PF, 0);	/* n/a for PF */
	SET_FIELD(cfg_word, TM_CFG_CID_PRE_SCAN_ROWS, 0);	/* scan all   */

	rt_reg = TM_REG_CONFIG_CONN_MEM_RT_OFFSET +
	    (sizeof(cfg_word) / sizeof(u32)) *
	    (NUM_OF_VFS(p_hwfn->cdev) + p_hwfn->rel_pf_id);
	STORE_RT_REG_AGG(p_hwfn, rt_reg, cfg_word);

	/* enale scan */
	STORE_RT_REG(p_hwfn, TM_REG_PF_ENABLE_CONN_RT_OFFSET,
		     tm_iids.pf_cids ? 0x1 : 0x0);

	/* @@@TBD how to enable the scan for the VFs */

	tm_offset = tm_iids.per_vf_cids;

	/* Note: We assume consecutive VFs for a PF */
	for (i = 0; i < p_mngr->vf_count; i++) {
		cfg_word = 0;
		SET_FIELD(cfg_word, TM_CFG_NUM_IDS, tm_iids.per_vf_tids);
		SET_FIELD(cfg_word, TM_CFG_PRE_SCAN_OFFSET, 0);
		SET_FIELD(cfg_word, TM_CFG_PARENT_PF, p_hwfn->rel_pf_id);
		SET_FIELD(cfg_word, TM_CFG_TID_OFFSET, tm_offset);
		SET_FIELD(cfg_word, TM_CFG_TID_PRE_SCAN_ROWS, (u64) 0);

		rt_reg = TM_REG_CONFIG_TASK_MEM_RT_OFFSET +
		    (sizeof(cfg_word) / sizeof(u32)) *
		    (p_hwfn->cdev->p_iov_info->first_vf_in_pf + i);

		STORE_RT_REG_AGG(p_hwfn, rt_reg, cfg_word);
	}

	tm_offset = tm_iids.pf_cids;
	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		cfg_word = 0;
		SET_FIELD(cfg_word, TM_CFG_NUM_IDS, tm_iids.pf_tids[i]);
		SET_FIELD(cfg_word, TM_CFG_PRE_SCAN_OFFSET, 0);
		SET_FIELD(cfg_word, TM_CFG_PARENT_PF, 0);
		SET_FIELD(cfg_word, TM_CFG_TID_OFFSET, tm_offset);
		SET_FIELD(cfg_word, TM_CFG_TID_PRE_SCAN_ROWS, (u64) 0);

		rt_reg = TM_REG_CONFIG_TASK_MEM_RT_OFFSET +
		    (sizeof(cfg_word) / sizeof(u32)) *
		    (NUM_OF_VFS(p_hwfn->cdev) +
		     p_hwfn->rel_pf_id * NUM_TASK_PF_SEGMENTS + i);

		STORE_RT_REG_AGG(p_hwfn, rt_reg, cfg_word);
		active_seg_mask |= (tm_iids.pf_tids[i] ? BIT(i) : 0);

		tm_offset += tm_iids.pf_tids[i];
	}

	if (QED_IS_RDMA_PERSONALITY(p_hwfn))
		active_seg_mask = 0;

	STORE_RT_REG(p_hwfn, TM_REG_PF_ENABLE_TASK_RT_OFFSET, active_seg_mask);

	/* @@@TBD how to enable the scan for the VFs */
}

static void qed_prs_init_common(struct qed_hwfn *p_hwfn)
{
	if ((p_hwfn->hw_info.personality == QED_PCI_FCOE) &&
	    p_hwfn->pf_params.fcoe_pf_params.is_target)
		STORE_RT_REG(p_hwfn,
			     PRS_REG_SEARCH_RESP_INITIATOR_TYPE_RT_OFFSET, 0);
}

static void qed_prs_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_conn_type_cfg *p_fcoe;
	struct qed_tid_seg *p_tid;

	p_fcoe = &p_mngr->conn_cfg[PROTOCOLID_FCOE];

	/* If FCoE is active set the MAX OX_ID (tid) in the Parser */
	if (!p_fcoe->cid_count)
		return;

	p_tid = &p_fcoe->tid_seg[QED_CXT_FCOE_TID_SEG];
	if (p_hwfn->pf_params.fcoe_pf_params.is_target) {
		STORE_RT_REG_AGG(p_hwfn,
				 PRS_REG_TASK_ID_MAX_TARGET_PF_RT_OFFSET,
				 p_tid->count);
	} else {
		STORE_RT_REG_AGG(p_hwfn,
				 PRS_REG_TASK_ID_MAX_INITIATOR_PF_RT_OFFSET,
				 p_tid->count);
	}
}

void qed_cxt_hw_init_common(struct qed_hwfn *p_hwfn)
{
	qed_cdu_init_common(p_hwfn);
	qed_prs_init_common(p_hwfn);
}

void qed_cxt_hw_init_pf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	qed_qm_init_pf(p_hwfn, p_ptt, true);
	qed_cm_init_pf(p_hwfn);
	qed_dq_init_pf(p_hwfn);
	qed_cdu_init_pf(p_hwfn);
	qed_ilt_init_pf(p_hwfn);
	qed_src_init_pf(p_hwfn);
	qed_tm_init_pf(p_hwfn);
	qed_prs_init_pf(p_hwfn);
}

int _qed_cxt_acquire_cid(struct qed_hwfn *p_hwfn,
			 enum protocol_type type, u32 *p_cid, u8 vfid)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_cid_acquired_map *p_map;
	u32 rel_cid;

	if (type >= MAX_CONN_TYPES) {
		DP_NOTICE(p_hwfn, "Invalid protocol type %d", type);
		return -EINVAL;
	}

	if (vfid >= MAX_NUM_VFS && vfid != QED_CXT_PF_CID) {
		DP_NOTICE(p_hwfn, "VF [%02x] is out of range\n", vfid);
		return -EINVAL;
	}

	/* Determine the right map to take this CID from */
	if (vfid == QED_CXT_PF_CID)
		p_map = &p_mngr->acquired[type];
	else
		p_map = &p_mngr->acquired_vf[type][vfid];

	if (!p_map->cid_map) {
		DP_NOTICE(p_hwfn, "Invalid protocol type %d", type);
		return -EINVAL;
	}

	rel_cid = find_first_zero_bit(p_map->cid_map, p_map->max_count);

	if (rel_cid >= p_map->max_count) {
		DP_NOTICE(p_hwfn, "no CID available for protocol %d\n", type);
		return -EINVAL;
	}

	__set_bit(rel_cid, p_map->cid_map);

	*p_cid = rel_cid + p_map->start_cid;

	DP_VERBOSE(p_hwfn, QED_MSG_CXT,
		   "Acquired cid 0x%08x [rel. %08x] vfid %02x type %d\n",
		   *p_cid, rel_cid, vfid, type);

	return 0;
}

int qed_cxt_acquire_cid(struct qed_hwfn *p_hwfn,
			enum protocol_type type, u32 *p_cid)
{
	return _qed_cxt_acquire_cid(p_hwfn, type, p_cid, QED_CXT_PF_CID);
}

static bool qed_cxt_test_cid_acquired(struct qed_hwfn *p_hwfn,
				      u32 cid,
				      u8 vfid,
				      enum protocol_type *p_type,
				      struct qed_cid_acquired_map **pp_map)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 rel_cid;

	/* Iterate over protocols and find matching cid range */
	for (*p_type = 0; *p_type < MAX_CONN_TYPES; (*p_type)++) {
		if (vfid == QED_CXT_PF_CID)
			*pp_map = &p_mngr->acquired[*p_type];
		else
			*pp_map = &p_mngr->acquired_vf[*p_type][vfid];

		if (!((*pp_map)->cid_map))
			continue;
		if (cid >= (*pp_map)->start_cid &&
		    cid < (*pp_map)->start_cid + (*pp_map)->max_count)
			break;
	}

	if (*p_type == MAX_CONN_TYPES) {
		DP_NOTICE(p_hwfn, "Invalid CID %d vfid %02x", cid, vfid);
		goto fail;
	}

	rel_cid = cid - (*pp_map)->start_cid;
	if (!test_bit(rel_cid, (*pp_map)->cid_map)) {
		DP_NOTICE(p_hwfn, "CID %d [vifd %02x] not acquired",
			  cid, vfid);
		goto fail;
	}

	return true;
fail:
	*p_type = MAX_CONN_TYPES;
	*pp_map = NULL;
	return false;
}

void _qed_cxt_release_cid(struct qed_hwfn *p_hwfn, u32 cid, u8 vfid)
{
	struct qed_cid_acquired_map *p_map = NULL;
	enum protocol_type type;
	bool b_acquired;
	u32 rel_cid;

	if (vfid != QED_CXT_PF_CID && vfid > MAX_NUM_VFS) {
		DP_NOTICE(p_hwfn,
			  "Trying to return incorrect CID belonging to VF %02x\n",
			  vfid);
		return;
	}

	/* Test acquired and find matching per-protocol map */
	b_acquired = qed_cxt_test_cid_acquired(p_hwfn, cid, vfid,
					       &type, &p_map);

	if (!b_acquired)
		return;

	rel_cid = cid - p_map->start_cid;
	clear_bit(rel_cid, p_map->cid_map);

	DP_VERBOSE(p_hwfn, QED_MSG_CXT,
		   "Released CID 0x%08x [rel. %08x] vfid %02x type %d\n",
		   cid, rel_cid, vfid, type);
}

void qed_cxt_release_cid(struct qed_hwfn *p_hwfn, u32 cid)
{
	_qed_cxt_release_cid(p_hwfn, cid, QED_CXT_PF_CID);
}

int qed_cxt_get_cid_info(struct qed_hwfn *p_hwfn, struct qed_cxt_info *p_info)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_cid_acquired_map *p_map = NULL;
	u32 conn_cxt_size, hw_p_size, cxts_per_p, line;
	enum protocol_type type;
	bool b_acquired;

	/* Test acquired and find matching per-protocol map */
	b_acquired = qed_cxt_test_cid_acquired(p_hwfn, p_info->iid,
					       QED_CXT_PF_CID, &type, &p_map);

	if (!b_acquired)
		return -EINVAL;

	/* set the protocl type */
	p_info->type = type;

	/* compute context virtual pointer */
	hw_p_size = p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUC].p_size.val;

	conn_cxt_size = CONN_CXT_SIZE(p_hwfn);
	cxts_per_p = ILT_PAGE_IN_BYTES(hw_p_size) / conn_cxt_size;
	line = p_info->iid / cxts_per_p;

	/* Make sure context is allocated (dynamic allocation) */
	if (!p_mngr->ilt_shadow[line].virt_addr)
		return -EINVAL;

	p_info->p_cxt = p_mngr->ilt_shadow[line].virt_addr +
			p_info->iid % cxts_per_p * conn_cxt_size;

	DP_VERBOSE(p_hwfn, (QED_MSG_ILT | QED_MSG_CXT),
		   "Accessing ILT shadow[%d]: CXT pointer is at %p (for iid %d)\n",
		   p_info->iid / cxts_per_p, p_info->p_cxt, p_info->iid);

	return 0;
}

static void qed_rdma_set_pf_params(struct qed_hwfn *p_hwfn,
				   struct qed_rdma_pf_params *p_params,
				   u32 num_tasks)
{
	u32 num_cons, num_qps;
	enum protocol_type proto;

	if (p_hwfn->mcp_info->func_info.protocol == QED_PCI_ETH_RDMA) {
		DP_NOTICE(p_hwfn,
			  "Current day drivers don't support RoCE & iWARP simultaneously on the same PF. Default to RoCE-only\n");
		p_hwfn->hw_info.personality = QED_PCI_ETH_ROCE;
	}

	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ETH_IWARP:
		/* Each QP requires one connection */
		num_cons = min_t(u32, IWARP_MAX_QPS, p_params->num_qps);
		proto = PROTOCOLID_IWARP;
		break;
	case QED_PCI_ETH_ROCE:
		num_qps = min_t(u32, ROCE_MAX_QPS, p_params->num_qps);
		num_cons = num_qps * 2;	/* each QP requires two connections */
		proto = PROTOCOLID_ROCE;
		break;
	default:
		return;
	}

	if (num_cons && num_tasks) {
		u32 num_srqs, num_xrc_srqs;

		qed_cxt_set_proto_cid_count(p_hwfn, proto, num_cons, 0);

		/* Deliberatly passing ROCE for tasks id. This is because
		 * iWARP / RoCE share the task id.
		 */
		qed_cxt_set_proto_tid_count(p_hwfn, PROTOCOLID_ROCE,
					    QED_CXT_ROCE_TID_SEG, 1,
					    num_tasks, false);

		num_srqs = min_t(u32, QED_RDMA_MAX_SRQS, p_params->num_srqs);

		/* XRC SRQs populate a single ILT page */
		num_xrc_srqs = qed_cxt_xrc_srqs_per_page(p_hwfn);

		qed_cxt_set_srq_count(p_hwfn, num_srqs, num_xrc_srqs);
	} else {
		DP_INFO(p_hwfn->cdev,
			"RDMA personality used without setting params!\n");
	}
}

int qed_cxt_set_pf_params(struct qed_hwfn *p_hwfn, u32 rdma_tasks)
{
	/* Set the number of required CORE connections */
	u32 core_cids = 1; /* SPQ */

	if (p_hwfn->using_ll2)
		core_cids += 4;
	qed_cxt_set_proto_cid_count(p_hwfn, PROTOCOLID_CORE, core_cids, 0);

	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ETH_RDMA:
	case QED_PCI_ETH_IWARP:
	case QED_PCI_ETH_ROCE:
	{
			qed_rdma_set_pf_params(p_hwfn,
					       &p_hwfn->
					       pf_params.rdma_pf_params,
					       rdma_tasks);
		/* no need for break since RoCE coexist with Ethernet */
	}
	/* fall through */
	case QED_PCI_ETH:
	{
		struct qed_eth_pf_params *p_params =
		    &p_hwfn->pf_params.eth_pf_params;

		if (!p_params->num_vf_cons)
			p_params->num_vf_cons =
			    ETH_PF_PARAMS_VF_CONS_DEFAULT;
		qed_cxt_set_proto_cid_count(p_hwfn, PROTOCOLID_ETH,
					    p_params->num_cons,
					    p_params->num_vf_cons);
		p_hwfn->p_cxt_mngr->arfs_count = p_params->num_arfs_filters;
		break;
	}
	case QED_PCI_FCOE:
	{
		struct qed_fcoe_pf_params *p_params;

		p_params = &p_hwfn->pf_params.fcoe_pf_params;

		if (p_params->num_cons && p_params->num_tasks) {
			qed_cxt_set_proto_cid_count(p_hwfn,
						    PROTOCOLID_FCOE,
						    p_params->num_cons,
						    0);

			qed_cxt_set_proto_tid_count(p_hwfn, PROTOCOLID_FCOE,
						    QED_CXT_FCOE_TID_SEG, 0,
						    p_params->num_tasks, true);
		} else {
			DP_INFO(p_hwfn->cdev,
				"Fcoe personality used without setting params!\n");
		}
		break;
	}
	case QED_PCI_ISCSI:
	{
		struct qed_iscsi_pf_params *p_params;

		p_params = &p_hwfn->pf_params.iscsi_pf_params;

		if (p_params->num_cons && p_params->num_tasks) {
			qed_cxt_set_proto_cid_count(p_hwfn,
						    PROTOCOLID_ISCSI,
						    p_params->num_cons,
						    0);

			qed_cxt_set_proto_tid_count(p_hwfn,
						    PROTOCOLID_ISCSI,
						    QED_CXT_ISCSI_TID_SEG,
						    0,
						    p_params->num_tasks,
						    true);
		} else {
			DP_INFO(p_hwfn->cdev,
				"Iscsi personality used without setting params!\n");
		}
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

int qed_cxt_get_tid_mem_info(struct qed_hwfn *p_hwfn,
			     struct qed_tid_mem *p_info)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 proto, seg, total_lines, i, shadow_line;
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_fl_seg;
	struct qed_tid_seg *p_seg_info;

	/* Verify the personality */
	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_FCOE:
		proto = PROTOCOLID_FCOE;
		seg = QED_CXT_FCOE_TID_SEG;
		break;
	case QED_PCI_ISCSI:
		proto = PROTOCOLID_ISCSI;
		seg = QED_CXT_ISCSI_TID_SEG;
		break;
	default:
		return -EINVAL;
	}

	p_cli = &p_mngr->clients[ILT_CLI_CDUT];
	if (!p_cli->active)
		return -EINVAL;

	p_seg_info = &p_mngr->conn_cfg[proto].tid_seg[seg];
	if (!p_seg_info->has_fl_mem)
		return -EINVAL;

	p_fl_seg = &p_cli->pf_blks[CDUT_FL_SEG_BLK(seg, PF)];
	total_lines = DIV_ROUND_UP(p_fl_seg->total_size,
				   p_fl_seg->real_size_in_page);

	for (i = 0; i < total_lines; i++) {
		shadow_line = i + p_fl_seg->start_line -
		    p_hwfn->p_cxt_mngr->pf_start_line;
		p_info->blocks[i] = p_mngr->ilt_shadow[shadow_line].virt_addr;
	}
	p_info->waste = ILT_PAGE_IN_BYTES(p_cli->p_size.val) -
	    p_fl_seg->real_size_in_page;
	p_info->tid_size = p_mngr->task_type_size[p_seg_info->type];
	p_info->num_tids_per_block = p_fl_seg->real_size_in_page /
	    p_info->tid_size;

	return 0;
}

/* This function is very RoCE oriented, if another protocol in the future
 * will want this feature we'll need to modify the function to be more generic
 */
int
qed_cxt_dynamic_ilt_alloc(struct qed_hwfn *p_hwfn,
			  enum qed_cxt_elem_type elem_type, u32 iid)
{
	u32 reg_offset, shadow_line, elem_size, hw_p_size, elems_per_p, line;
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	struct qed_ptt *p_ptt;
	dma_addr_t p_phys;
	u64 ilt_hw_entry;
	void *p_virt;
	int rc = 0;

	switch (elem_type) {
	case QED_ELEM_CXT:
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUC];
		elem_size = CONN_CXT_SIZE(p_hwfn);
		p_blk = &p_cli->pf_blks[CDUC_BLK];
		break;
	case QED_ELEM_SRQ:
		/* The first ILT page is not used for regular SRQs. Skip it. */
		iid += p_hwfn->p_cxt_mngr->xrc_srq_count;
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_TSDM];
		elem_size = SRQ_CXT_SIZE;
		p_blk = &p_cli->pf_blks[SRQ_BLK];
		break;
	case QED_ELEM_XRC_SRQ:
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_TSDM];
		elem_size = XRC_SRQ_CXT_SIZE;
		p_blk = &p_cli->pf_blks[SRQ_BLK];
		break;
	case QED_ELEM_TASK:
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
		elem_size = TYPE1_TASK_CXT_SIZE(p_hwfn);
		p_blk = &p_cli->pf_blks[CDUT_SEG_BLK(QED_CXT_ROCE_TID_SEG)];
		break;
	default:
		DP_NOTICE(p_hwfn, "-EINVALID elem type = %d", elem_type);
		return -EINVAL;
	}

	/* Calculate line in ilt */
	hw_p_size = p_cli->p_size.val;
	elems_per_p = ILT_PAGE_IN_BYTES(hw_p_size) / elem_size;
	line = p_blk->start_line + (iid / elems_per_p);
	shadow_line = line - p_hwfn->p_cxt_mngr->pf_start_line;

	/* If line is already allocated, do nothing, otherwise allocate it and
	 * write it to the PSWRQ2 registers.
	 * This section can be run in parallel from different contexts and thus
	 * a mutex protection is needed.
	 */

	mutex_lock(&p_hwfn->p_cxt_mngr->mutex);

	if (p_hwfn->p_cxt_mngr->ilt_shadow[shadow_line].virt_addr)
		goto out0;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_NOTICE(p_hwfn,
			  "QED_TIME_OUT on ptt acquire - dynamic allocation");
		rc = -EBUSY;
		goto out0;
	}

	p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    p_blk->real_size_in_page, &p_phys,
				    GFP_KERNEL);
	if (!p_virt) {
		rc = -ENOMEM;
		goto out1;
	}

	/* configuration of refTagMask to 0xF is required for RoCE DIF MR only,
	 * to compensate for a HW bug, but it is configured even if DIF is not
	 * enabled. This is harmless and allows us to avoid a dedicated API. We
	 * configure the field for all of the contexts on the newly allocated
	 * page.
	 */
	if (elem_type == QED_ELEM_TASK) {
		u32 elem_i;
		u8 *elem_start = (u8 *)p_virt;
		union type1_task_context *elem;

		for (elem_i = 0; elem_i < elems_per_p; elem_i++) {
			elem = (union type1_task_context *)elem_start;
			SET_FIELD(elem->roce_ctx.tdif_context.flags1,
				  TDIF_TASK_CONTEXT_REF_TAG_MASK, 0xf);
			elem_start += TYPE1_TASK_CXT_SIZE(p_hwfn);
		}
	}

	p_hwfn->p_cxt_mngr->ilt_shadow[shadow_line].virt_addr = p_virt;
	p_hwfn->p_cxt_mngr->ilt_shadow[shadow_line].phys_addr = p_phys;
	p_hwfn->p_cxt_mngr->ilt_shadow[shadow_line].size =
	    p_blk->real_size_in_page;

	/* compute absolute offset */
	reg_offset = PSWRQ2_REG_ILT_MEMORY +
	    (line * ILT_REG_SIZE_IN_BYTES * ILT_ENTRY_IN_REGS);

	ilt_hw_entry = 0;
	SET_FIELD(ilt_hw_entry, ILT_ENTRY_VALID, 1ULL);
	SET_FIELD(ilt_hw_entry, ILT_ENTRY_PHY_ADDR,
		  (p_hwfn->p_cxt_mngr->ilt_shadow[shadow_line].phys_addr
		   >> 12));

	/* Write via DMAE since the PSWRQ2_REG_ILT_MEMORY line is a wide-bus */
	qed_dmae_host2grc(p_hwfn, p_ptt, (u64) (uintptr_t)&ilt_hw_entry,
			  reg_offset, sizeof(ilt_hw_entry) / sizeof(u32),
			  NULL);

	if (elem_type == QED_ELEM_CXT) {
		u32 last_cid_allocated = (1 + (iid / elems_per_p)) *
		    elems_per_p;

		/* Update the relevant register in the parser */
		qed_wr(p_hwfn, p_ptt, PRS_REG_ROCE_DEST_QP_MAX_PF,
		       last_cid_allocated - 1);

		if (!p_hwfn->b_rdma_enabled_in_prs) {
			/* Enable RDMA search */
			qed_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 1);
			p_hwfn->b_rdma_enabled_in_prs = true;
		}
	}

out1:
	qed_ptt_release(p_hwfn, p_ptt);
out0:
	mutex_unlock(&p_hwfn->p_cxt_mngr->mutex);

	return rc;
}

/* This function is very RoCE oriented, if another protocol in the future
 * will want this feature we'll need to modify the function to be more generic
 */
static int
qed_cxt_free_ilt_range(struct qed_hwfn *p_hwfn,
		       enum qed_cxt_elem_type elem_type,
		       u32 start_iid, u32 count)
{
	u32 start_line, end_line, shadow_start_line, shadow_end_line;
	u32 reg_offset, elem_size, hw_p_size, elems_per_p;
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	u32 end_iid = start_iid + count;
	struct qed_ptt *p_ptt;
	u64 ilt_hw_entry = 0;
	u32 i;

	switch (elem_type) {
	case QED_ELEM_CXT:
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUC];
		elem_size = CONN_CXT_SIZE(p_hwfn);
		p_blk = &p_cli->pf_blks[CDUC_BLK];
		break;
	case QED_ELEM_SRQ:
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_TSDM];
		elem_size = SRQ_CXT_SIZE;
		p_blk = &p_cli->pf_blks[SRQ_BLK];
		break;
	case QED_ELEM_TASK:
		p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
		elem_size = TYPE1_TASK_CXT_SIZE(p_hwfn);
		p_blk = &p_cli->pf_blks[CDUT_SEG_BLK(QED_CXT_ROCE_TID_SEG)];
		break;
	default:
		DP_NOTICE(p_hwfn, "-EINVALID elem type = %d", elem_type);
		return -EINVAL;
	}

	/* Calculate line in ilt */
	hw_p_size = p_cli->p_size.val;
	elems_per_p = ILT_PAGE_IN_BYTES(hw_p_size) / elem_size;
	start_line = p_blk->start_line + (start_iid / elems_per_p);
	end_line = p_blk->start_line + (end_iid / elems_per_p);
	if (((end_iid + 1) / elems_per_p) != (end_iid / elems_per_p))
		end_line--;

	shadow_start_line = start_line - p_hwfn->p_cxt_mngr->pf_start_line;
	shadow_end_line = end_line - p_hwfn->p_cxt_mngr->pf_start_line;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_NOTICE(p_hwfn,
			  "QED_TIME_OUT on ptt acquire - dynamic allocation");
		return -EBUSY;
	}

	for (i = shadow_start_line; i < shadow_end_line; i++) {
		if (!p_hwfn->p_cxt_mngr->ilt_shadow[i].virt_addr)
			continue;

		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  p_hwfn->p_cxt_mngr->ilt_shadow[i].size,
				  p_hwfn->p_cxt_mngr->ilt_shadow[i].virt_addr,
				  p_hwfn->p_cxt_mngr->ilt_shadow[i].phys_addr);

		p_hwfn->p_cxt_mngr->ilt_shadow[i].virt_addr = NULL;
		p_hwfn->p_cxt_mngr->ilt_shadow[i].phys_addr = 0;
		p_hwfn->p_cxt_mngr->ilt_shadow[i].size = 0;

		/* compute absolute offset */
		reg_offset = PSWRQ2_REG_ILT_MEMORY +
		    ((start_line++) * ILT_REG_SIZE_IN_BYTES *
		     ILT_ENTRY_IN_REGS);

		/* Write via DMAE since the PSWRQ2_REG_ILT_MEMORY line is a
		 * wide-bus.
		 */
		qed_dmae_host2grc(p_hwfn, p_ptt,
				  (u64) (uintptr_t) &ilt_hw_entry,
				  reg_offset,
				  sizeof(ilt_hw_entry) / sizeof(u32),
				  NULL);
	}

	qed_ptt_release(p_hwfn, p_ptt);

	return 0;
}

int qed_cxt_free_proto_ilt(struct qed_hwfn *p_hwfn, enum protocol_type proto)
{
	int rc;
	u32 cid;

	/* Free Connection CXT */
	rc = qed_cxt_free_ilt_range(p_hwfn, QED_ELEM_CXT,
				    qed_cxt_get_proto_cid_start(p_hwfn,
								proto),
				    qed_cxt_get_proto_cid_count(p_hwfn,
								proto, &cid));

	if (rc)
		return rc;

	/* Free Task CXT ( Intentionally RoCE as task-id is shared between
	 * RoCE and iWARP )
	 */
	proto = PROTOCOLID_ROCE;
	rc = qed_cxt_free_ilt_range(p_hwfn, QED_ELEM_TASK, 0,
				    qed_cxt_get_proto_tid_count(p_hwfn, proto));
	if (rc)
		return rc;

	/* Free TSDM CXT */
	rc = qed_cxt_free_ilt_range(p_hwfn, QED_ELEM_XRC_SRQ, 0,
				    p_hwfn->p_cxt_mngr->xrc_srq_count);

	rc = qed_cxt_free_ilt_range(p_hwfn, QED_ELEM_SRQ,
				    p_hwfn->p_cxt_mngr->xrc_srq_count,
				    p_hwfn->p_cxt_mngr->srq_count);

	return rc;
}

int qed_cxt_get_task_ctx(struct qed_hwfn *p_hwfn,
			 u32 tid, u8 ctx_type, void **pp_task_ctx)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_ilt_client_cfg *p_cli;
	struct qed_tid_seg *p_seg_info;
	struct qed_ilt_cli_blk *p_seg;
	u32 num_tids_per_block;
	u32 tid_size, ilt_idx;
	u32 total_lines;
	u32 proto, seg;

	/* Verify the personality */
	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_FCOE:
		proto = PROTOCOLID_FCOE;
		seg = QED_CXT_FCOE_TID_SEG;
		break;
	case QED_PCI_ISCSI:
		proto = PROTOCOLID_ISCSI;
		seg = QED_CXT_ISCSI_TID_SEG;
		break;
	default:
		return -EINVAL;
	}

	p_cli = &p_mngr->clients[ILT_CLI_CDUT];
	if (!p_cli->active)
		return -EINVAL;

	p_seg_info = &p_mngr->conn_cfg[proto].tid_seg[seg];

	if (ctx_type == QED_CTX_WORKING_MEM) {
		p_seg = &p_cli->pf_blks[CDUT_SEG_BLK(seg)];
	} else if (ctx_type == QED_CTX_FL_MEM) {
		if (!p_seg_info->has_fl_mem)
			return -EINVAL;
		p_seg = &p_cli->pf_blks[CDUT_FL_SEG_BLK(seg, PF)];
	} else {
		return -EINVAL;
	}
	total_lines = DIV_ROUND_UP(p_seg->total_size, p_seg->real_size_in_page);
	tid_size = p_mngr->task_type_size[p_seg_info->type];
	num_tids_per_block = p_seg->real_size_in_page / tid_size;

	if (total_lines < tid / num_tids_per_block)
		return -EINVAL;

	ilt_idx = tid / num_tids_per_block + p_seg->start_line -
		  p_mngr->pf_start_line;
	*pp_task_ctx = (u8 *)p_mngr->ilt_shadow[ilt_idx].virt_addr +
		       (tid % num_tids_per_block) * tid_size;

	return 0;
}

static u16 qed_blk_calculate_pages(struct qed_ilt_cli_blk *p_blk)
{
	if (p_blk->real_size_in_page == 0)
		return 0;

	return DIV_ROUND_UP(p_blk->total_size, p_blk->real_size_in_page);
}

u16 qed_get_cdut_num_pf_init_pages(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	u16 i, pages = 0;

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		p_blk = &p_cli->pf_blks[CDUT_FL_SEG_BLK(i, PF)];
		pages += qed_blk_calculate_pages(p_blk);
	}

	return pages;
}

u16 qed_get_cdut_num_vf_init_pages(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	u16 i, pages = 0;

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
	for (i = 0; i < NUM_TASK_VF_SEGMENTS; i++) {
		p_blk = &p_cli->vf_blks[CDUT_FL_SEG_BLK(i, VF)];
		pages += qed_blk_calculate_pages(p_blk);
	}

	return pages;
}

u16 qed_get_cdut_num_pf_work_pages(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	u16 i, pages = 0;

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
	for (i = 0; i < NUM_TASK_PF_SEGMENTS; i++) {
		p_blk = &p_cli->pf_blks[CDUT_SEG_BLK(i)];
		pages += qed_blk_calculate_pages(p_blk);
	}

	return pages;
}

u16 qed_get_cdut_num_vf_work_pages(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	u16 pages = 0, i;

	p_cli = &p_hwfn->p_cxt_mngr->clients[ILT_CLI_CDUT];
	for (i = 0; i < NUM_TASK_VF_SEGMENTS; i++) {
		p_blk = &p_cli->vf_blks[CDUT_SEG_BLK(i)];
		pages += qed_blk_calculate_pages(p_blk);
	}

	return pages;
}
