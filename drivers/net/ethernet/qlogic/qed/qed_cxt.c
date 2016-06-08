/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
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
#include <linux/bitops.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"

/* Max number of connection types in HW (DQ/CDU etc.) */
#define MAX_CONN_TYPES		PROTOCOLID_COMMON
#define NUM_TASK_TYPES		2
#define NUM_TASK_PF_SEGMENTS	4
#define NUM_TASK_VF_SEGMENTS	1

/* QM constants */
#define QM_PQ_ELEMENT_SIZE	4 /* in bytes */

/* Doorbell-Queue constants */
#define DQ_RANGE_SHIFT		4
#define DQ_RANGE_ALIGN		BIT(DQ_RANGE_SHIFT)

/* ILT constants */
#define ILT_DEFAULT_HW_P_SIZE		3
#define ILT_PAGE_IN_BYTES(hw_p_size)	(1U << ((hw_p_size) + 12))
#define ILT_CFG_REG(cli, reg)	PSWRQ2_REG_ ## cli ## _ ## reg ## _RT_OFFSET

/* ILT entry structure */
#define ILT_ENTRY_PHY_ADDR_MASK		0x000FFFFFFFFFFFULL
#define ILT_ENTRY_PHY_ADDR_SHIFT	0
#define ILT_ENTRY_VALID_MASK		0x1ULL
#define ILT_ENTRY_VALID_SHIFT		52
#define ILT_ENTRY_IN_REGS		2
#define ILT_REG_SIZE_IN_BYTES		4

/* connection context union */
union conn_context {
	struct core_conn_context core_ctx;
	struct eth_conn_context eth_ctx;
};

#define CONN_CXT_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(union conn_context, p_hwfn)

/* PF per protocl configuration object */
struct qed_conn_type_cfg {
	u32 cid_count;
	u32 cid_start;
	u32 cids_per_vf;
};

/* ILT Client configuration, Per connection type (protocol) resources. */
#define ILT_CLI_PF_BLOCKS	(1 + NUM_TASK_PF_SEGMENTS * 2)
#define ILT_CLI_VF_BLOCKS       (1 + NUM_TASK_VF_SEGMENTS * 2)
#define CDUC_BLK		(0)

enum ilt_clients {
	ILT_CLI_CDUC,
	ILT_CLI_QM,
	ILT_CLI_MAX
};

struct ilt_cfg_pair {
	u32 reg;
	u32 val;
};

struct qed_ilt_cli_blk {
	u32 total_size; /* 0 means not active */
	u32 real_size_in_page;
	u32 start_line;
};

struct qed_ilt_client_cfg {
	bool active;

	/* ILT boundaries */
	struct ilt_cfg_pair first;
	struct ilt_cfg_pair last;
	struct ilt_cfg_pair p_size;

	/* ILT client blocks for PF */
	struct qed_ilt_cli_blk pf_blks[ILT_CLI_PF_BLOCKS];
	u32 pf_total_lines;

	/* ILT client blocks for VFs */
	struct qed_ilt_cli_blk vf_blks[ILT_CLI_VF_BLOCKS];
	u32 vf_total_lines;
};

/* Per Path -
 *      ILT shadow table
 *      Protocol acquired CID lists
 *      PF start line in ILT
 */
struct qed_dma_mem {
	dma_addr_t p_phys;
	void *p_virt;
	size_t size;
};

struct qed_cid_acquired_map {
	u32		start_cid;
	u32		max_count;
	unsigned long	*cid_map;
};

struct qed_cxt_mngr {
	/* Per protocl configuration */
	struct qed_conn_type_cfg	conn_cfg[MAX_CONN_TYPES];

	/* computed ILT structure */
	struct qed_ilt_client_cfg	clients[ILT_CLI_MAX];

	/* total number of VFs for this hwfn -
	 * ALL VFs are symmetric in terms of HW resources
	 */
	u32				vf_count;

	/* Acquired CIDs */
	struct qed_cid_acquired_map	acquired[MAX_CONN_TYPES];

	/* ILT  shadow table */
	struct qed_dma_mem		*ilt_shadow;
	u32				pf_start_line;
};

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

static void qed_cxt_qm_iids(struct qed_hwfn *p_hwfn,
			    struct qed_qm_iids *iids)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 vf_cids = 0, type;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		iids->cids += p_mngr->conn_cfg[type].cid_count;
		vf_cids += p_mngr->conn_cfg[type].cids_per_vf;
	}

	iids->vf_cids += vf_cids * p_mngr->vf_count;
	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "iids: CIDS %08x vf_cids %08x\n",
		   iids->cids, iids->vf_cids);
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
}

u32 qed_cxt_get_proto_cid_count(struct qed_hwfn		*p_hwfn,
				enum protocol_type	type,
				u32			*vf_cid)
{
	if (vf_cid)
		*vf_cid = p_hwfn->p_cxt_mngr->conn_cfg[type].cids_per_vf;

	return p_hwfn->p_cxt_mngr->conn_cfg[type].cid_count;
}

static void qed_ilt_cli_blk_fill(struct qed_ilt_client_cfg *p_cli,
				 struct qed_ilt_cli_blk *p_blk,
				 u32 start_line, u32 total_size,
				 u32 elem_size)
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
	*p_line += DIV_ROUND_UP(p_blk->total_size,
				p_blk->real_size_in_page);
	p_cli->last.val = *p_line - 1;

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "ILT[Client %d] - Lines: [%08x - %08x]. Block - Size %08x [Real %08x] Start line %d\n",
		   client_id, p_cli->first.val,
		   p_cli->last.val, p_blk->total_size,
		   p_blk->real_size_in_page, p_blk->start_line);
}

int qed_cxt_cfg_ilt_compute(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_ilt_client_cfg *p_cli;
	struct qed_ilt_cli_blk *p_blk;
	struct qed_cdu_iids cdu_iids;
	struct qed_qm_iids qm_iids;
	u32 curr_line, total, i;

	memset(&qm_iids, 0, sizeof(qm_iids));
	memset(&cdu_iids, 0, sizeof(cdu_iids));

	p_mngr->pf_start_line = RESC_START(p_hwfn, QED_ILT);

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "hwfn [%d] - Set context manager starting line to be 0x%08x\n",
		   p_hwfn->my_id, p_hwfn->p_cxt_mngr->pf_start_line);

	/* CDUC */
	p_cli = &p_mngr->clients[ILT_CLI_CDUC];
	curr_line = p_mngr->pf_start_line;

	/* CDUC PF */
	p_cli->pf_total_lines = 0;

	/* get the counters for the CDUC and QM clients  */
	qed_cxt_cdu_iids(p_mngr, &cdu_iids);

	p_blk = &p_cli->pf_blks[CDUC_BLK];

	total = cdu_iids.pf_cids * CONN_CXT_SIZE(p_hwfn);

	qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
			     total, CONN_CXT_SIZE(p_hwfn));

	qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line, ILT_CLI_CDUC);
	p_cli->pf_total_lines = curr_line - p_blk->start_line;

	/* CDUC VF */
	p_blk = &p_cli->vf_blks[CDUC_BLK];
	total = cdu_iids.per_vf_cids * CONN_CXT_SIZE(p_hwfn);

	qed_ilt_cli_blk_fill(p_cli, p_blk, curr_line,
			     total, CONN_CXT_SIZE(p_hwfn));

	qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line, ILT_CLI_CDUC);
	p_cli->vf_total_lines = curr_line - p_blk->start_line;

	for (i = 1; i < p_mngr->vf_count; i++)
		qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line,
				     ILT_CLI_CDUC);

	/* QM */
	p_cli = &p_mngr->clients[ILT_CLI_QM];
	p_blk = &p_cli->pf_blks[0];

	qed_cxt_qm_iids(p_hwfn, &qm_iids);
	total = qed_qm_pf_mem_size(p_hwfn->rel_pf_id, qm_iids.cids,
				   qm_iids.vf_cids, 0,
				   p_hwfn->qm_info.num_pqs,
				   p_hwfn->qm_info.num_vf_pqs);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_ILT,
		   "QM ILT Info, (cids=%d, vf_cids=%d, num_pqs=%d, num_vf_pqs=%d, memory_size=%d)\n",
		   qm_iids.cids,
		   qm_iids.vf_cids,
		   p_hwfn->qm_info.num_pqs, p_hwfn->qm_info.num_vf_pqs, total);

	qed_ilt_cli_blk_fill(p_cli, p_blk,
			     curr_line, total * 0x1000,
			     QM_PQ_ELEMENT_SIZE);

	qed_ilt_cli_adv_line(p_hwfn, p_cli, p_blk, &curr_line, ILT_CLI_QM);
	p_cli->pf_total_lines = curr_line - p_blk->start_line;

	if (curr_line - p_hwfn->p_cxt_mngr->pf_start_line >
	    RESC_NUM(p_hwfn, QED_ILT)) {
		DP_ERR(p_hwfn, "too many ilt lines...#lines=%d\n",
		       curr_line - p_hwfn->p_cxt_mngr->pf_start_line);
		return -EINVAL;
	}

	return 0;
}

#define for_each_ilt_valid_client(pos, clients)	\
		for (pos = 0; pos < ILT_CLI_MAX; pos++)

/* Total number of ILT lines used by this PF */
static u32 qed_cxt_ilt_shadow_size(struct qed_ilt_client_cfg *ilt_clients)
{
	u32 size = 0;
	u32 i;

	for_each_ilt_valid_client(i, ilt_clients) {
		if (!ilt_clients[i].active)
			continue;
		size += (ilt_clients[i].last.val -
			 ilt_clients[i].first.val + 1);
	}

	return size;
}

static void qed_ilt_shadow_free(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *p_cli = p_hwfn->p_cxt_mngr->clients;
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 ilt_size, i;

	ilt_size = qed_cxt_ilt_shadow_size(p_cli);

	for (i = 0; p_mngr->ilt_shadow && i < ilt_size; i++) {
		struct qed_dma_mem *p_dma = &p_mngr->ilt_shadow[i];

		if (p_dma->p_virt)
			dma_free_coherent(&p_hwfn->cdev->pdev->dev,
					  p_dma->size, p_dma->p_virt,
					  p_dma->p_phys);
		p_dma->p_virt = NULL;
	}
	kfree(p_mngr->ilt_shadow);
}

static int qed_ilt_blk_alloc(struct qed_hwfn *p_hwfn,
			     struct qed_ilt_cli_blk *p_blk,
			     enum ilt_clients ilt_client,
			     u32 start_line_offset)
{
	struct qed_dma_mem *ilt_shadow = p_hwfn->p_cxt_mngr->ilt_shadow;
	u32 lines, line, sz_left;

	if (!p_blk->total_size)
		return 0;

	sz_left = p_blk->total_size;
	lines = DIV_ROUND_UP(sz_left, p_blk->real_size_in_page);
	line = p_blk->start_line + start_line_offset -
	       p_hwfn->p_cxt_mngr->pf_start_line;

	for (; lines; lines--) {
		dma_addr_t p_phys;
		void *p_virt;
		u32 size;

		size = min_t(u32, sz_left,
			     p_blk->real_size_in_page);
		p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
					    size,
					    &p_phys,
					    GFP_KERNEL);
		if (!p_virt)
			return -ENOMEM;
		memset(p_virt, 0, size);

		ilt_shadow[line].p_phys = p_phys;
		ilt_shadow[line].p_virt = p_virt;
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
	p_mngr->ilt_shadow = kcalloc(size, sizeof(struct qed_dma_mem),
				     GFP_KERNEL);
	if (!p_mngr->ilt_shadow) {
		DP_NOTICE(p_hwfn, "Failed to allocate ilt shadow table\n");
		rc = -ENOMEM;
		goto ilt_shadow_fail;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_ILT,
		   "Allocated 0x%x bytes for ilt shadow\n",
		   (u32)(size * sizeof(struct qed_dma_mem)));

	for_each_ilt_valid_client(i, clients) {
		if (!clients[i].active)
			continue;
		for (j = 0; j < ILT_CLI_PF_BLOCKS; j++) {
			p_blk = &clients[i].pf_blks[j];
			rc = qed_ilt_blk_alloc(p_hwfn, p_blk, i, 0);
			if (rc != 0)
				goto ilt_shadow_fail;
		}
		for (k = 0; k < p_mngr->vf_count; k++) {
			for (j = 0; j < ILT_CLI_VF_BLOCKS; j++) {
				u32 lines = clients[i].vf_total_lines * k;

				p_blk = &clients[i].vf_blks[j];
				rc = qed_ilt_blk_alloc(p_hwfn, p_blk, i, lines);
				if (rc != 0)
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
	u32 type;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		kfree(p_mngr->acquired[type].cid_map);
		p_mngr->acquired[type].max_count = 0;
		p_mngr->acquired[type].start_cid = 0;
	}
}

static int qed_cid_map_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 start_cid = 0;
	u32 type;

	for (type = 0; type < MAX_CONN_TYPES; type++) {
		u32 cid_cnt = p_hwfn->p_cxt_mngr->conn_cfg[type].cid_count;
		u32 size;

		if (cid_cnt == 0)
			continue;

		size = DIV_ROUND_UP(cid_cnt,
				    sizeof(unsigned long) * BITS_PER_BYTE) *
		       sizeof(unsigned long);
		p_mngr->acquired[type].cid_map = kzalloc(size, GFP_KERNEL);
		if (!p_mngr->acquired[type].cid_map)
			goto cid_map_fail;

		p_mngr->acquired[type].max_count = cid_cnt;
		p_mngr->acquired[type].start_cid = start_cid;

		p_hwfn->p_cxt_mngr->conn_cfg[type].cid_start = start_cid;

		DP_VERBOSE(p_hwfn, QED_MSG_CXT,
			   "Type %08x start: %08x count %08x\n",
			   type, p_mngr->acquired[type].start_cid,
			   p_mngr->acquired[type].max_count);
		start_cid += cid_cnt;
	}

	return 0;

cid_map_fail:
	qed_cid_map_free(p_hwfn);
	return -ENOMEM;
}

int qed_cxt_mngr_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr;
	u32 i;

	p_mngr = kzalloc(sizeof(*p_mngr), GFP_KERNEL);
	if (!p_mngr) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_cxt_mngr'\n");
		return -ENOMEM;
	}

	/* Initialize ILT client registers */
	p_mngr->clients[ILT_CLI_CDUC].first.reg = ILT_CFG_REG(CDUC, FIRST_ILT);
	p_mngr->clients[ILT_CLI_CDUC].last.reg = ILT_CFG_REG(CDUC, LAST_ILT);
	p_mngr->clients[ILT_CLI_CDUC].p_size.reg = ILT_CFG_REG(CDUC, P_SIZE);

	p_mngr->clients[ILT_CLI_QM].first.reg = ILT_CFG_REG(QM, FIRST_ILT);
	p_mngr->clients[ILT_CLI_QM].last.reg = ILT_CFG_REG(QM, LAST_ILT);
	p_mngr->clients[ILT_CLI_QM].p_size.reg = ILT_CFG_REG(QM, P_SIZE);

	/* default ILT page size for all clients is 32K */
	for (i = 0; i < ILT_CLI_MAX; i++)
		p_mngr->clients[i].p_size.val = ILT_DEFAULT_HW_P_SIZE;

	if (p_hwfn->cdev->p_iov_info)
		p_mngr->vf_count = p_hwfn->cdev->p_iov_info->total_vfs;

	/* Set the cxt mangr pointer priori to further allocations */
	p_hwfn->p_cxt_mngr = p_mngr;

	return 0;
}

int qed_cxt_tables_alloc(struct qed_hwfn *p_hwfn)
{
	int rc;

	/* Allocate the ILT shadow table */
	rc = qed_ilt_shadow_alloc(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to allocate ilt memory\n");
		goto tables_alloc_fail;
	}

	/* Allocate and initialize the acquired cids bitmaps */
	rc = qed_cid_map_alloc(p_hwfn);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to allocate cid maps\n");
		goto tables_alloc_fail;
	}

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
	qed_ilt_shadow_free(p_hwfn);
	kfree(p_hwfn->p_cxt_mngr);

	p_hwfn->p_cxt_mngr = NULL;
}

void qed_cxt_mngr_setup(struct qed_hwfn *p_hwfn)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	int type;

	/* Reset acquired cids */
	for (type = 0; type < MAX_CONN_TYPES; type++) {
		u32 cid_cnt = p_hwfn->p_cxt_mngr->conn_cfg[type].cid_count;

		if (cid_cnt == 0)
			continue;

		memset(p_mngr->acquired[type].cid_map, 0,
		       DIV_ROUND_UP(cid_cnt,
				    sizeof(unsigned long) * BITS_PER_BYTE) *
		       sizeof(unsigned long));
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
}

void qed_qm_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_pf_rt_init_params params;
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct qed_qm_iids iids;

	memset(&iids, 0, sizeof(iids));
	qed_cxt_qm_iids(p_hwfn, &iids);

	memset(&params, 0, sizeof(params));
	params.port_id = p_hwfn->port_id;
	params.pf_id = p_hwfn->rel_pf_id;
	params.max_phys_tcs_per_port = qm_info->max_phys_tcs_per_port;
	params.is_first_pf = p_hwfn->first_on_engine;
	params.num_pf_cids = iids.cids;
	params.num_vf_cids = iids.vf_cids;
	params.start_pq = qm_info->start_pq;
	params.num_pf_pqs = qm_info->num_pqs - qm_info->num_vf_pqs;
	params.num_vf_pqs = qm_info->num_vf_pqs;
	params.start_vport = qm_info->start_vport;
	params.num_vports = qm_info->num_vports;
	params.pf_wfq = qm_info->pf_wfq;
	params.pf_rl = qm_info->pf_rl;
	params.pq_params = qm_info->qm_pq_params;
	params.vport_params = qm_info->qm_vport_params;

	qed_qm_pf_rt_init(p_hwfn, p_hwfn->p_main_ptt, &params);
}

/* CM PF */
static int qed_cm_init_pf(struct qed_hwfn *p_hwfn)
{
	union qed_qm_pq_params pq_params;
	u16 pq;

	/* XCM pure-LB queue */
	memset(&pq_params, 0, sizeof(pq_params));
	pq_params.core.tc = LB_TC;
	pq = qed_get_qm_pq(p_hwfn, PROTOCOLID_CORE, &pq_params);
	STORE_RT_REG(p_hwfn, XCM_REG_CON_PHY_Q3_RT_OFFSET, pq);

	return 0;
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
		if (!ilt_clients[i].active)
			continue;
		STORE_RT_REG(p_hwfn,
			     ilt_clients[i].first.reg,
			     ilt_clients[i].first.val);
		STORE_RT_REG(p_hwfn,
			     ilt_clients[i].last.reg,
			     ilt_clients[i].last.val);
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
}

/* ILT (PSWRQ2) PF */
static void qed_ilt_init_pf(struct qed_hwfn *p_hwfn)
{
	struct qed_ilt_client_cfg *clients;
	struct qed_cxt_mngr *p_mngr;
	struct qed_dma_mem *p_shdw;
	u32 line, rt_offst, i;

	qed_ilt_bounds_init(p_hwfn);
	qed_ilt_vf_bounds_init(p_hwfn);

	p_mngr = p_hwfn->p_cxt_mngr;
	p_shdw = p_mngr->ilt_shadow;
	clients = p_hwfn->p_cxt_mngr->clients;

	for_each_ilt_valid_client(i, clients) {
		if (!clients[i].active)
			continue;

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
			if (p_shdw[line].p_virt) {
				SET_FIELD(ilt_hw_entry, ILT_ENTRY_VALID, 1ULL);
				SET_FIELD(ilt_hw_entry, ILT_ENTRY_PHY_ADDR,
					  (p_shdw[line].p_phys >> 12));

				DP_VERBOSE(p_hwfn, QED_MSG_ILT,
					   "Setting RT[0x%08x] from ILT[0x%08x] [Client is %d] to Physical addr: 0x%llx\n",
					   rt_offst, line, i,
					   (u64)(p_shdw[line].p_phys >> 12));
			}

			STORE_RT_REG_AGG(p_hwfn, rt_offst, ilt_hw_entry);
		}
	}
}

void qed_cxt_hw_init_common(struct qed_hwfn *p_hwfn)
{
	qed_cdu_init_common(p_hwfn);
}

void qed_cxt_hw_init_pf(struct qed_hwfn *p_hwfn)
{
	qed_qm_init_pf(p_hwfn);
	qed_cm_init_pf(p_hwfn);
	qed_dq_init_pf(p_hwfn);
	qed_ilt_init_pf(p_hwfn);
}

int qed_cxt_acquire_cid(struct qed_hwfn *p_hwfn,
			enum protocol_type type,
			u32 *p_cid)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 rel_cid;

	if (type >= MAX_CONN_TYPES || !p_mngr->acquired[type].cid_map) {
		DP_NOTICE(p_hwfn, "Invalid protocol type %d", type);
		return -EINVAL;
	}

	rel_cid = find_first_zero_bit(p_mngr->acquired[type].cid_map,
				      p_mngr->acquired[type].max_count);

	if (rel_cid >= p_mngr->acquired[type].max_count) {
		DP_NOTICE(p_hwfn, "no CID available for protocol %d\n",
			  type);
		return -EINVAL;
	}

	__set_bit(rel_cid, p_mngr->acquired[type].cid_map);

	*p_cid = rel_cid + p_mngr->acquired[type].start_cid;

	return 0;
}

static bool qed_cxt_test_cid_acquired(struct qed_hwfn *p_hwfn,
				      u32 cid,
				      enum protocol_type *p_type)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	struct qed_cid_acquired_map *p_map;
	enum protocol_type p;
	u32 rel_cid;

	/* Iterate over protocols and find matching cid range */
	for (p = 0; p < MAX_CONN_TYPES; p++) {
		p_map = &p_mngr->acquired[p];

		if (!p_map->cid_map)
			continue;
		if (cid >= p_map->start_cid &&
		    cid < p_map->start_cid + p_map->max_count)
			break;
	}
	*p_type = p;

	if (p == MAX_CONN_TYPES) {
		DP_NOTICE(p_hwfn, "Invalid CID %d", cid);
		return false;
	}

	rel_cid = cid - p_map->start_cid;
	if (!test_bit(rel_cid, p_map->cid_map)) {
		DP_NOTICE(p_hwfn, "CID %d not acquired", cid);
		return false;
	}
	return true;
}

void qed_cxt_release_cid(struct qed_hwfn *p_hwfn,
			 u32 cid)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	enum protocol_type type;
	bool b_acquired;
	u32 rel_cid;

	/* Test acquired and find matching per-protocol map */
	b_acquired = qed_cxt_test_cid_acquired(p_hwfn, cid, &type);

	if (!b_acquired)
		return;

	rel_cid = cid - p_mngr->acquired[type].start_cid;
	__clear_bit(rel_cid, p_mngr->acquired[type].cid_map);
}

int qed_cxt_get_cid_info(struct qed_hwfn *p_hwfn,
			 struct qed_cxt_info *p_info)
{
	struct qed_cxt_mngr *p_mngr = p_hwfn->p_cxt_mngr;
	u32 conn_cxt_size, hw_p_size, cxts_per_p, line;
	enum protocol_type type;
	bool b_acquired;

	/* Test acquired and find matching per-protocol map */
	b_acquired = qed_cxt_test_cid_acquired(p_hwfn, p_info->iid, &type);

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
	if (!p_mngr->ilt_shadow[line].p_virt)
		return -EINVAL;

	p_info->p_cxt = p_mngr->ilt_shadow[line].p_virt +
			p_info->iid % cxts_per_p * conn_cxt_size;

	DP_VERBOSE(p_hwfn, (QED_MSG_ILT | QED_MSG_CXT),
		   "Accessing ILT shadow[%d]: CXT pointer is at %p (for iid %d)\n",
		   p_info->iid / cxts_per_p, p_info->p_cxt, p_info->iid);

	return 0;
}

int qed_cxt_set_pf_params(struct qed_hwfn *p_hwfn)
{
	struct qed_eth_pf_params *p_params = &p_hwfn->pf_params.eth_pf_params;

	/* Set the number of required CORE connections */
	u32 core_cids = 1; /* SPQ */

	qed_cxt_set_proto_cid_count(p_hwfn, PROTOCOLID_CORE, core_cids, 0);

	qed_cxt_set_proto_cid_count(p_hwfn, PROTOCOLID_ETH,
				    p_params->num_cons, 1);

	return 0;
}
