/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_reg_addr.h"

enum cminterface {
	MCM_SEC,
	MCM_PRI,
	UCM_SEC,
	UCM_PRI,
	TCM_SEC,
	TCM_PRI,
	YCM_SEC,
	YCM_PRI,
	XCM_SEC,
	XCM_PRI,
	NUM_OF_CM_INTERFACES
};

/* general constants */
#define QM_PQ_ELEMENT_SIZE                      4 /* in bytes */
#define QM_PQ_MEM_4KB(pq_size)	(pq_size ? DIV_ROUND_UP((pq_size + 1) *	\
							QM_PQ_ELEMENT_SIZE, \
							0x1000) : 0)
#define QM_PQ_SIZE_256B(pq_size)	(pq_size ? DIV_ROUND_UP(pq_size, \
								0x100) - 1 : 0)
#define QM_INVALID_PQ_ID                        0xffff
/* feature enable */
#define QM_BYPASS_EN                            1
#define QM_BYTE_CRD_EN                          1
/* other PQ constants */
#define QM_OTHER_PQS_PER_PF                     4
/* WFQ constants */
#define QM_WFQ_UPPER_BOUND		6250000
#define QM_WFQ_VP_PQ_VOQ_SHIFT          0
#define QM_WFQ_VP_PQ_PF_SHIFT           5
#define QM_WFQ_INC_VAL(weight)          ((weight) * 0x9000)
#define QM_WFQ_MAX_INC_VAL                      4375000
#define QM_WFQ_INIT_CRD(inc_val)        (2 * (inc_val))
/* RL constants */
#define QM_RL_UPPER_BOUND                       6250000
#define QM_RL_PERIOD                            5               /* in us */
#define QM_RL_PERIOD_CLK_25M            (25 * QM_RL_PERIOD)
#define QM_RL_INC_VAL(rate)		max_t(u32,	\
					      (((rate ? rate : 1000000)	\
						* QM_RL_PERIOD) / 8), 1)
#define QM_RL_MAX_INC_VAL                       4375000
/* AFullOprtnstcCrdMask constants */
#define QM_OPPOR_LINE_VOQ_DEF           1
#define QM_OPPOR_FW_STOP_DEF            0
#define QM_OPPOR_PQ_EMPTY_DEF           1
#define EAGLE_WORKAROUND_TC                     7
/* Command Queue constants */
#define PBF_CMDQ_PURE_LB_LINES                          150
#define PBF_CMDQ_EAGLE_WORKAROUND_LINES         8
#define PBF_CMDQ_LINES_RT_OFFSET(voq)           (		 \
		PBF_REG_YCMD_QS_NUM_LINES_VOQ0_RT_OFFSET + voq * \
		(PBF_REG_YCMD_QS_NUM_LINES_VOQ1_RT_OFFSET -	 \
		 PBF_REG_YCMD_QS_NUM_LINES_VOQ0_RT_OFFSET))
#define PBF_BTB_GUARANTEED_RT_OFFSET(voq)       (	      \
		PBF_REG_BTB_GUARANTEED_VOQ0_RT_OFFSET + voq * \
		(PBF_REG_BTB_GUARANTEED_VOQ1_RT_OFFSET -      \
		 PBF_REG_BTB_GUARANTEED_VOQ0_RT_OFFSET))
#define QM_VOQ_LINE_CRD(pbf_cmd_lines)          ((((pbf_cmd_lines) - \
						   4) *		     \
						  2) | QM_LINE_CRD_REG_SIGN_BIT)
/* BTB: blocks constants (block size = 256B) */
#define BTB_JUMBO_PKT_BLOCKS            38
#define BTB_HEADROOM_BLOCKS                     BTB_JUMBO_PKT_BLOCKS
#define BTB_EAGLE_WORKAROUND_BLOCKS     4
#define BTB_PURE_LB_FACTOR                      10
#define BTB_PURE_LB_RATIO                       7
/* QM stop command constants */
#define QM_STOP_PQ_MASK_WIDTH                   32
#define QM_STOP_CMD_ADDR                                0x2
#define QM_STOP_CMD_STRUCT_SIZE                 2
#define QM_STOP_CMD_PAUSE_MASK_OFFSET   0
#define QM_STOP_CMD_PAUSE_MASK_SHIFT    0
#define QM_STOP_CMD_PAUSE_MASK_MASK             -1
#define QM_STOP_CMD_GROUP_ID_OFFSET             1
#define QM_STOP_CMD_GROUP_ID_SHIFT              16
#define QM_STOP_CMD_GROUP_ID_MASK               15
#define QM_STOP_CMD_PQ_TYPE_OFFSET              1
#define QM_STOP_CMD_PQ_TYPE_SHIFT               24
#define QM_STOP_CMD_PQ_TYPE_MASK                1
#define QM_STOP_CMD_MAX_POLL_COUNT              100
#define QM_STOP_CMD_POLL_PERIOD_US              500
/* QM command macros */
#define QM_CMD_STRUCT_SIZE(cmd)			cmd ## \
	_STRUCT_SIZE
#define QM_CMD_SET_FIELD(var, cmd, field,				  \
			 value)        SET_FIELD(var[cmd ## _ ## field ## \
						     _OFFSET],		  \
						 cmd ## _ ## field,	  \
						 value)
/* QM: VOQ macros */
#define PHYS_VOQ(port, tc, max_phy_tcs_pr_port)	((port) *	\
						 (max_phy_tcs_pr_port) \
						 + (tc))
#define LB_VOQ(port)				( \
		MAX_PHYS_VOQS + (port))
#define VOQ(port, tc, max_phy_tcs_pr_port)	\
	((tc) <		\
	 LB_TC ? PHYS_VOQ(port,		\
			  tc,			 \
			  max_phy_tcs_pr_port) \
		: LB_VOQ(port))
/******************** INTERNAL IMPLEMENTATION *********************/
/* Prepare PF RL enable/disable runtime init values */
static void qed_enable_pf_rl(struct qed_hwfn *p_hwfn,
			     bool pf_rl_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_RLPFENABLE_RT_OFFSET, pf_rl_en ? 1 : 0);
	if (pf_rl_en) {
		/* enable RLs for all VOQs */
		STORE_RT_REG(p_hwfn, QM_REG_RLPFVOQENABLE_RT_OFFSET,
			     (1 << MAX_NUM_VOQS) - 1);
		/* write RL period */
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLPFPERIOD_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLPFPERIODTIMER_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);
		/* set credit threshold for QM bypass flow */
		if (QM_BYPASS_EN)
			STORE_RT_REG(p_hwfn,
				     QM_REG_AFULLQMBYPTHRPFRL_RT_OFFSET,
				     QM_RL_UPPER_BOUND);
	}
}

/* Prepare PF WFQ enable/disable runtime init values */
static void qed_enable_pf_wfq(struct qed_hwfn *p_hwfn,
			      bool pf_wfq_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_WFQPFENABLE_RT_OFFSET, pf_wfq_en ? 1 : 0);
	/* set credit threshold for QM bypass flow */
	if (pf_wfq_en && QM_BYPASS_EN)
		STORE_RT_REG(p_hwfn,
			     QM_REG_AFULLQMBYPTHRPFWFQ_RT_OFFSET,
			     QM_WFQ_UPPER_BOUND);
}

/* Prepare VPORT RL enable/disable runtime init values */
static void qed_enable_vport_rl(struct qed_hwfn *p_hwfn,
				bool vport_rl_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_RLGLBLENABLE_RT_OFFSET,
		     vport_rl_en ? 1 : 0);
	if (vport_rl_en) {
		/* write RL period (use timer 0 only) */
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLPERIOD_0_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLPERIODTIMER_0_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);
		/* set credit threshold for QM bypass flow */
		if (QM_BYPASS_EN)
			STORE_RT_REG(p_hwfn,
				     QM_REG_AFULLQMBYPTHRGLBLRL_RT_OFFSET,
				     QM_RL_UPPER_BOUND);
	}
}

/* Prepare VPORT WFQ enable/disable runtime init values */
static void qed_enable_vport_wfq(struct qed_hwfn *p_hwfn,
				 bool vport_wfq_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_WFQVPENABLE_RT_OFFSET,
		     vport_wfq_en ? 1 : 0);
	/* set credit threshold for QM bypass flow */
	if (vport_wfq_en && QM_BYPASS_EN)
		STORE_RT_REG(p_hwfn,
			     QM_REG_AFULLQMBYPTHRVPWFQ_RT_OFFSET,
			     QM_WFQ_UPPER_BOUND);
}

/* Prepare runtime init values to allocate PBF command queue lines for
 * the specified VOQ
 */
static void qed_cmdq_lines_voq_rt_init(struct qed_hwfn *p_hwfn,
				       u8 voq,
				       u16 cmdq_lines)
{
	u32 qm_line_crd;

	/* In A0 - Limit the size of pbf queue so that only 511 commands with
	 * the minimum size of 4 (FCoE minimum size)
	 */
	bool is_bb_a0 = QED_IS_BB_A0(p_hwfn->cdev);

	if (is_bb_a0)
		cmdq_lines = min_t(u32, cmdq_lines, 1022);
	qm_line_crd = QM_VOQ_LINE_CRD(cmdq_lines);
	OVERWRITE_RT_REG(p_hwfn, PBF_CMDQ_LINES_RT_OFFSET(voq),
			 (u32)cmdq_lines);
	STORE_RT_REG(p_hwfn, QM_REG_VOQCRDLINE_RT_OFFSET + voq, qm_line_crd);
	STORE_RT_REG(p_hwfn, QM_REG_VOQINITCRDLINE_RT_OFFSET + voq,
		     qm_line_crd);
}

/* Prepare runtime init values to allocate PBF command queue lines. */
static void qed_cmdq_lines_rt_init(
	struct qed_hwfn *p_hwfn,
	u8 max_ports_per_engine,
	u8 max_phys_tcs_per_port,
	struct init_qm_port_params port_params[MAX_NUM_PORTS])
{
	u8 tc, voq, port_id;

	/* clear PBF lines for all VOQs */
	for (voq = 0; voq < MAX_NUM_VOQS; voq++)
		STORE_RT_REG(p_hwfn, PBF_CMDQ_LINES_RT_OFFSET(voq), 0);
	for (port_id = 0; port_id < max_ports_per_engine; port_id++) {
		if (port_params[port_id].active) {
			u16 phys_lines, phys_lines_per_tc;
			u8 phys_tcs = port_params[port_id].num_active_phys_tcs;

			/* find #lines to divide between the active
			 * physical TCs.
			 */
			phys_lines = port_params[port_id].num_pbf_cmd_lines -
				     PBF_CMDQ_PURE_LB_LINES;
			/* find #lines per active physical TC */
			phys_lines_per_tc = phys_lines / phys_tcs;
			/* init registers per active TC */
			for (tc = 0; tc < phys_tcs; tc++) {
				voq = PHYS_VOQ(port_id, tc,
					       max_phys_tcs_per_port);
				qed_cmdq_lines_voq_rt_init(p_hwfn, voq,
							   phys_lines_per_tc);
			}
			/* init registers for pure LB TC */
			qed_cmdq_lines_voq_rt_init(p_hwfn, LB_VOQ(port_id),
						   PBF_CMDQ_PURE_LB_LINES);
		}
	}
}

static void qed_btb_blocks_rt_init(
	struct qed_hwfn *p_hwfn,
	u8 max_ports_per_engine,
	u8 max_phys_tcs_per_port,
	struct init_qm_port_params port_params[MAX_NUM_PORTS])
{
	u32 usable_blocks, pure_lb_blocks, phys_blocks;
	u8 tc, voq, port_id;

	for (port_id = 0; port_id < max_ports_per_engine; port_id++) {
		u32 temp;
		u8 phys_tcs;

		if (!port_params[port_id].active)
			continue;

		phys_tcs = port_params[port_id].num_active_phys_tcs;

		/* subtract headroom blocks */
		usable_blocks = port_params[port_id].num_btb_blocks -
				BTB_HEADROOM_BLOCKS;

		/* find blocks per physical TC. use factor to avoid
		 * floating arithmethic.
		 */
		pure_lb_blocks = (usable_blocks * BTB_PURE_LB_FACTOR) /
				 (phys_tcs * BTB_PURE_LB_FACTOR +
				  BTB_PURE_LB_RATIO);
		pure_lb_blocks = max_t(u32, BTB_JUMBO_PKT_BLOCKS,
				       pure_lb_blocks / BTB_PURE_LB_FACTOR);
		phys_blocks = (usable_blocks - pure_lb_blocks) / phys_tcs;

		/* init physical TCs */
		for (tc = 0; tc < phys_tcs; tc++) {
			voq = PHYS_VOQ(port_id, tc, max_phys_tcs_per_port);
			STORE_RT_REG(p_hwfn, PBF_BTB_GUARANTEED_RT_OFFSET(voq),
				     phys_blocks);
		}

		/* init pure LB TC */
		temp = LB_VOQ(port_id);
		STORE_RT_REG(p_hwfn, PBF_BTB_GUARANTEED_RT_OFFSET(temp),
			     pure_lb_blocks);
	}
}

/* Prepare Tx PQ mapping runtime init values for the specified PF */
static void qed_tx_pq_map_rt_init(
	struct qed_hwfn *p_hwfn,
	struct qed_ptt *p_ptt,
	struct qed_qm_pf_rt_init_params *p_params,
	u32 base_mem_addr_4kb)
{
	struct init_qm_vport_params *vport_params = p_params->vport_params;
	u16 num_pqs = p_params->num_pf_pqs + p_params->num_vf_pqs;
	u16 first_pq_group = p_params->start_pq / QM_PF_QUEUE_GROUP_SIZE;
	u16 last_pq_group = (p_params->start_pq + num_pqs - 1) /
			    QM_PF_QUEUE_GROUP_SIZE;
	bool is_bb_a0 = QED_IS_BB_A0(p_hwfn->cdev);
	u16 i, pq_id, pq_group;

	/* a bit per Tx PQ indicating if the PQ is associated with a VF */
	u32 tx_pq_vf_mask[MAX_QM_TX_QUEUES / QM_PF_QUEUE_GROUP_SIZE] = { 0 };
	u32 tx_pq_vf_mask_width = is_bb_a0 ? 32 : QM_PF_QUEUE_GROUP_SIZE;
	u32 num_tx_pq_vf_masks = MAX_QM_TX_QUEUES / tx_pq_vf_mask_width;
	u32 pq_mem_4kb = QM_PQ_MEM_4KB(p_params->num_pf_cids);
	u32 vport_pq_mem_4kb = QM_PQ_MEM_4KB(p_params->num_vf_cids);
	u32 mem_addr_4kb = base_mem_addr_4kb;

	/* set mapping from PQ group to PF */
	for (pq_group = first_pq_group; pq_group <= last_pq_group; pq_group++)
		STORE_RT_REG(p_hwfn, QM_REG_PQTX2PF_0_RT_OFFSET + pq_group,
			     (u32)(p_params->pf_id));
	/* set PQ sizes */
	STORE_RT_REG(p_hwfn, QM_REG_MAXPQSIZE_0_RT_OFFSET,
		     QM_PQ_SIZE_256B(p_params->num_pf_cids));
	STORE_RT_REG(p_hwfn, QM_REG_MAXPQSIZE_1_RT_OFFSET,
		     QM_PQ_SIZE_256B(p_params->num_vf_cids));

	/* go over all Tx PQs */
	for (i = 0, pq_id = p_params->start_pq; i < num_pqs; i++, pq_id++) {
		u8 voq = VOQ(p_params->port_id, p_params->pq_params[i].tc_id,
			     p_params->max_phys_tcs_per_port);
		bool is_vf_pq = (i >= p_params->num_pf_pqs);
		struct qm_rf_pq_map tx_pq_map;

		/* update first Tx PQ of VPORT/TC */
		u8 vport_id_in_pf = p_params->pq_params[i].vport_id -
				    p_params->start_vport;
		u16 *pq_ids = &vport_params[vport_id_in_pf].first_tx_pq_id[0];
		u16 first_tx_pq_id = pq_ids[p_params->pq_params[i].tc_id];

		if (first_tx_pq_id == QM_INVALID_PQ_ID) {
			/* create new VP PQ */
			pq_ids[p_params->pq_params[i].tc_id] = pq_id;
			first_tx_pq_id = pq_id;
			/* map VP PQ to VOQ and PF */
			STORE_RT_REG(p_hwfn,
				     QM_REG_WFQVPMAP_RT_OFFSET +
				     first_tx_pq_id,
				     (voq << QM_WFQ_VP_PQ_VOQ_SHIFT) |
				     (p_params->pf_id <<
				      QM_WFQ_VP_PQ_PF_SHIFT));
		}
		/* fill PQ map entry */
		memset(&tx_pq_map, 0, sizeof(tx_pq_map));
		SET_FIELD(tx_pq_map.reg, QM_RF_PQ_MAP_PQ_VALID, 1);
		SET_FIELD(tx_pq_map.reg, QM_RF_PQ_MAP_RL_VALID,
			  is_vf_pq ? 1 : 0);
		SET_FIELD(tx_pq_map.reg, QM_RF_PQ_MAP_VP_PQ_ID, first_tx_pq_id);
		SET_FIELD(tx_pq_map.reg, QM_RF_PQ_MAP_RL_ID,
			  is_vf_pq ? p_params->pq_params[i].vport_id : 0);
		SET_FIELD(tx_pq_map.reg, QM_RF_PQ_MAP_VOQ, voq);
		SET_FIELD(tx_pq_map.reg, QM_RF_PQ_MAP_WRR_WEIGHT_GROUP,
			  p_params->pq_params[i].wrr_group);
		/* write PQ map entry to CAM */
		STORE_RT_REG(p_hwfn, QM_REG_TXPQMAP_RT_OFFSET + pq_id,
			     *((u32 *)&tx_pq_map));
		/* set base address */
		STORE_RT_REG(p_hwfn,
			     QM_REG_BASEADDRTXPQ_RT_OFFSET + pq_id,
			     mem_addr_4kb);
		/* check if VF PQ */
		if (is_vf_pq) {
			/* if PQ is associated with a VF, add indication
			 * to PQ VF mask
			 */
			tx_pq_vf_mask[pq_id / tx_pq_vf_mask_width] |=
				(1 << (pq_id % tx_pq_vf_mask_width));
			mem_addr_4kb += vport_pq_mem_4kb;
		} else {
			mem_addr_4kb += pq_mem_4kb;
		}
	}

	/* store Tx PQ VF mask to size select register */
	for (i = 0; i < num_tx_pq_vf_masks; i++) {
		if (tx_pq_vf_mask[i]) {
			if (is_bb_a0) {
				u32 curr_mask = 0, addr;

				addr = QM_REG_MAXPQSIZETXSEL_0 + (i * 4);
				if (!p_params->is_first_pf)
					curr_mask = qed_rd(p_hwfn, p_ptt,
							   addr);

				addr = QM_REG_MAXPQSIZETXSEL_0_RT_OFFSET + i;

				STORE_RT_REG(p_hwfn, addr,
					     curr_mask | tx_pq_vf_mask[i]);
			} else {
				u32 addr;

				addr = QM_REG_MAXPQSIZETXSEL_0_RT_OFFSET + i;
				STORE_RT_REG(p_hwfn, addr,
					     tx_pq_vf_mask[i]);
			}
		}
	}
}

/* Prepare Other PQ mapping runtime init values for the specified PF */
static void qed_other_pq_map_rt_init(struct qed_hwfn *p_hwfn,
				     u8 port_id,
				     u8 pf_id,
				     u32 num_pf_cids,
				     u32 num_tids,
				     u32 base_mem_addr_4kb)
{
	u16 i, pq_id;

	/* a single other PQ group is used in each PF,
	 * where PQ group i is used in PF i.
	 */
	u16 pq_group = pf_id;
	u32 pq_size = num_pf_cids + num_tids;
	u32 pq_mem_4kb = QM_PQ_MEM_4KB(pq_size);
	u32 mem_addr_4kb = base_mem_addr_4kb;

	/* map PQ group to PF */
	STORE_RT_REG(p_hwfn, QM_REG_PQOTHER2PF_0_RT_OFFSET + pq_group,
		     (u32)(pf_id));
	/* set PQ sizes */
	STORE_RT_REG(p_hwfn, QM_REG_MAXPQSIZE_2_RT_OFFSET,
		     QM_PQ_SIZE_256B(pq_size));
	/* set base address */
	for (i = 0, pq_id = pf_id * QM_PF_QUEUE_GROUP_SIZE;
	     i < QM_OTHER_PQS_PER_PF; i++, pq_id++) {
		STORE_RT_REG(p_hwfn,
			     QM_REG_BASEADDROTHERPQ_RT_OFFSET + pq_id,
			     mem_addr_4kb);
		mem_addr_4kb += pq_mem_4kb;
	}
}

/* Prepare PF WFQ runtime init values for the specified PF.
 * Return -1 on error.
 */
static int qed_pf_wfq_rt_init(struct qed_hwfn *p_hwfn,
			      struct qed_qm_pf_rt_init_params *p_params)
{
	u16 num_tx_pqs = p_params->num_pf_pqs + p_params->num_vf_pqs;
	u32 crd_reg_offset;
	u32 inc_val;
	u16 i;

	if (p_params->pf_id < MAX_NUM_PFS_BB)
		crd_reg_offset = QM_REG_WFQPFCRD_RT_OFFSET;
	else
		crd_reg_offset = QM_REG_WFQPFCRD_MSB_RT_OFFSET +
				 (p_params->pf_id % MAX_NUM_PFS_BB);

	inc_val = QM_WFQ_INC_VAL(p_params->pf_wfq);
	if (inc_val > QM_WFQ_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF WFQ weight configuration");
		return -1;
	}
	STORE_RT_REG(p_hwfn, QM_REG_WFQPFWEIGHT_RT_OFFSET + p_params->pf_id,
		     inc_val);
	STORE_RT_REG(p_hwfn,
		     QM_REG_WFQPFUPPERBOUND_RT_OFFSET + p_params->pf_id,
		     QM_WFQ_UPPER_BOUND | QM_WFQ_CRD_REG_SIGN_BIT);

	for (i = 0; i < num_tx_pqs; i++) {
		u8 voq = VOQ(p_params->port_id, p_params->pq_params[i].tc_id,
			     p_params->max_phys_tcs_per_port);

		OVERWRITE_RT_REG(p_hwfn,
				 crd_reg_offset + voq * MAX_NUM_PFS_BB,
				 QM_WFQ_INIT_CRD(inc_val) |
				 QM_WFQ_CRD_REG_SIGN_BIT);
	}

	return 0;
}

/* Prepare PF RL runtime init values for the specified PF.
 * Return -1 on error.
 */
static int qed_pf_rl_rt_init(struct qed_hwfn *p_hwfn,
			     u8 pf_id,
			     u32 pf_rl)
{
	u32 inc_val = QM_RL_INC_VAL(pf_rl);

	if (inc_val > QM_RL_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF rate limit configuration");
		return -1;
	}
	STORE_RT_REG(p_hwfn, QM_REG_RLPFCRD_RT_OFFSET + pf_id,
		     QM_RL_CRD_REG_SIGN_BIT);
	STORE_RT_REG(p_hwfn, QM_REG_RLPFUPPERBOUND_RT_OFFSET + pf_id,
		     QM_RL_UPPER_BOUND | QM_RL_CRD_REG_SIGN_BIT);
	STORE_RT_REG(p_hwfn, QM_REG_RLPFINCVAL_RT_OFFSET + pf_id, inc_val);
	return 0;
}

/* Prepare VPORT WFQ runtime init values for the specified VPORTs.
 * Return -1 on error.
 */
static int qed_vp_wfq_rt_init(struct qed_hwfn *p_hwfn,
			      u8 num_vports,
			      struct init_qm_vport_params *vport_params)
{
	u32 inc_val;
	u8 tc, i;

	/* go over all PF VPORTs */
	for (i = 0; i < num_vports; i++) {

		if (!vport_params[i].vport_wfq)
			continue;

		inc_val = QM_WFQ_INC_VAL(vport_params[i].vport_wfq);
		if (inc_val > QM_WFQ_MAX_INC_VAL) {
			DP_NOTICE(p_hwfn,
				  "Invalid VPORT WFQ weight configuration");
			return -1;
		}

		/* each VPORT can have several VPORT PQ IDs for
		 * different TCs
		 */
		for (tc = 0; tc < NUM_OF_TCS; tc++) {
			u16 vport_pq_id = vport_params[i].first_tx_pq_id[tc];

			if (vport_pq_id != QM_INVALID_PQ_ID) {
				STORE_RT_REG(p_hwfn,
					     QM_REG_WFQVPCRD_RT_OFFSET +
					     vport_pq_id,
					     QM_WFQ_CRD_REG_SIGN_BIT);
				STORE_RT_REG(p_hwfn,
					     QM_REG_WFQVPWEIGHT_RT_OFFSET +
					     vport_pq_id, inc_val);
			}
		}
	}

	return 0;
}

static int qed_vport_rl_rt_init(struct qed_hwfn *p_hwfn,
				u8 start_vport,
				u8 num_vports,
				struct init_qm_vport_params *vport_params)
{
	u8 i, vport_id;

	/* go over all PF VPORTs */
	for (i = 0, vport_id = start_vport; i < num_vports; i++, vport_id++) {
		u32 inc_val = QM_RL_INC_VAL(vport_params[i].vport_rl);

		if (inc_val > QM_RL_MAX_INC_VAL) {
			DP_NOTICE(p_hwfn,
				  "Invalid VPORT rate-limit configuration");
			return -1;
		}

		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLCRD_RT_OFFSET + vport_id,
			     QM_RL_CRD_REG_SIGN_BIT);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLUPPERBOUND_RT_OFFSET + vport_id,
			     QM_RL_UPPER_BOUND | QM_RL_CRD_REG_SIGN_BIT);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLINCVAL_RT_OFFSET + vport_id,
			     inc_val);
	}

	return 0;
}

static bool qed_poll_on_qm_cmd_ready(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt)
{
	u32 reg_val, i;

	for (i = 0, reg_val = 0; i < QM_STOP_CMD_MAX_POLL_COUNT && reg_val == 0;
	     i++) {
		udelay(QM_STOP_CMD_POLL_PERIOD_US);
		reg_val = qed_rd(p_hwfn, p_ptt, QM_REG_SDMCMDREADY);
	}

	/* check if timeout while waiting for SDM command ready */
	if (i == QM_STOP_CMD_MAX_POLL_COUNT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "Timeout when waiting for QM SDM command ready signal\n");
		return false;
	}

	return true;
}

static bool qed_send_qm_cmd(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u32 cmd_addr,
			    u32 cmd_data_lsb,
			    u32 cmd_data_msb)
{
	if (!qed_poll_on_qm_cmd_ready(p_hwfn, p_ptt))
		return false;

	qed_wr(p_hwfn, p_ptt, QM_REG_SDMCMDADDR, cmd_addr);
	qed_wr(p_hwfn, p_ptt, QM_REG_SDMCMDDATALSB, cmd_data_lsb);
	qed_wr(p_hwfn, p_ptt, QM_REG_SDMCMDDATAMSB, cmd_data_msb);
	qed_wr(p_hwfn, p_ptt, QM_REG_SDMCMDGO, 1);
	qed_wr(p_hwfn, p_ptt, QM_REG_SDMCMDGO, 0);

	return qed_poll_on_qm_cmd_ready(p_hwfn, p_ptt);
}

/******************** INTERFACE IMPLEMENTATION *********************/
u32 qed_qm_pf_mem_size(u8 pf_id,
		       u32 num_pf_cids,
		       u32 num_vf_cids,
		       u32 num_tids,
		       u16 num_pf_pqs,
		       u16 num_vf_pqs)
{
	return QM_PQ_MEM_4KB(num_pf_cids) * num_pf_pqs +
	       QM_PQ_MEM_4KB(num_vf_cids) * num_vf_pqs +
	       QM_PQ_MEM_4KB(num_pf_cids + num_tids) * QM_OTHER_PQS_PER_PF;
}

int qed_qm_common_rt_init(
	struct qed_hwfn *p_hwfn,
	struct qed_qm_common_rt_init_params *p_params)
{
	/* init AFullOprtnstcCrdMask */
	u32 mask = (QM_OPPOR_LINE_VOQ_DEF <<
		    QM_RF_OPPORTUNISTIC_MASK_LINEVOQ_SHIFT) |
		   (QM_BYTE_CRD_EN << QM_RF_OPPORTUNISTIC_MASK_BYTEVOQ_SHIFT) |
		   (p_params->pf_wfq_en <<
		    QM_RF_OPPORTUNISTIC_MASK_PFWFQ_SHIFT) |
		   (p_params->vport_wfq_en <<
		    QM_RF_OPPORTUNISTIC_MASK_VPWFQ_SHIFT) |
		   (p_params->pf_rl_en <<
		    QM_RF_OPPORTUNISTIC_MASK_PFRL_SHIFT) |
		   (p_params->vport_rl_en <<
		    QM_RF_OPPORTUNISTIC_MASK_VPQCNRL_SHIFT) |
		   (QM_OPPOR_FW_STOP_DEF <<
		    QM_RF_OPPORTUNISTIC_MASK_FWPAUSE_SHIFT) |
		   (QM_OPPOR_PQ_EMPTY_DEF <<
		    QM_RF_OPPORTUNISTIC_MASK_QUEUEEMPTY_SHIFT);

	STORE_RT_REG(p_hwfn, QM_REG_AFULLOPRTNSTCCRDMASK_RT_OFFSET, mask);
	qed_enable_pf_rl(p_hwfn, p_params->pf_rl_en);
	qed_enable_pf_wfq(p_hwfn, p_params->pf_wfq_en);
	qed_enable_vport_rl(p_hwfn, p_params->vport_rl_en);
	qed_enable_vport_wfq(p_hwfn, p_params->vport_wfq_en);
	qed_cmdq_lines_rt_init(p_hwfn,
			       p_params->max_ports_per_engine,
			       p_params->max_phys_tcs_per_port,
			       p_params->port_params);
	qed_btb_blocks_rt_init(p_hwfn,
			       p_params->max_ports_per_engine,
			       p_params->max_phys_tcs_per_port,
			       p_params->port_params);
	return 0;
}

int qed_qm_pf_rt_init(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      struct qed_qm_pf_rt_init_params *p_params)
{
	struct init_qm_vport_params *vport_params = p_params->vport_params;
	u32 other_mem_size_4kb = QM_PQ_MEM_4KB(p_params->num_pf_cids +
					       p_params->num_tids) *
				 QM_OTHER_PQS_PER_PF;
	u8 tc, i;

	/* clear first Tx PQ ID array for each VPORT */
	for (i = 0; i < p_params->num_vports; i++)
		for (tc = 0; tc < NUM_OF_TCS; tc++)
			vport_params[i].first_tx_pq_id[tc] = QM_INVALID_PQ_ID;

	/* map Other PQs (if any) */
	qed_other_pq_map_rt_init(p_hwfn, p_params->port_id, p_params->pf_id,
				 p_params->num_pf_cids, p_params->num_tids, 0);

	/* map Tx PQs */
	qed_tx_pq_map_rt_init(p_hwfn, p_ptt, p_params, other_mem_size_4kb);

	if (p_params->pf_wfq)
		if (qed_pf_wfq_rt_init(p_hwfn, p_params))
			return -1;

	if (qed_pf_rl_rt_init(p_hwfn, p_params->pf_id, p_params->pf_rl))
		return -1;

	if (qed_vp_wfq_rt_init(p_hwfn, p_params->num_vports, vport_params))
		return -1;

	if (qed_vport_rl_rt_init(p_hwfn, p_params->start_vport,
				 p_params->num_vports, vport_params))
		return -1;

	return 0;
}

int qed_init_pf_rl(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt,
		   u8 pf_id,
		   u32 pf_rl)
{
	u32 inc_val = QM_RL_INC_VAL(pf_rl);

	if (inc_val > QM_RL_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF rate limit configuration");
		return -1;
	}

	qed_wr(p_hwfn, p_ptt,
	       QM_REG_RLPFCRD + pf_id * 4,
	       QM_RL_CRD_REG_SIGN_BIT);
	qed_wr(p_hwfn, p_ptt, QM_REG_RLPFINCVAL + pf_id * 4, inc_val);

	return 0;
}

int qed_init_vport_rl(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u8 vport_id,
		      u32 vport_rl)
{
	u32 inc_val = QM_RL_INC_VAL(vport_rl);

	if (inc_val > QM_RL_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid VPORT rate-limit configuration");
		return -1;
	}

	qed_wr(p_hwfn, p_ptt,
	       QM_REG_RLGLBLCRD + vport_id * 4,
	       QM_RL_CRD_REG_SIGN_BIT);
	qed_wr(p_hwfn, p_ptt, QM_REG_RLGLBLINCVAL + vport_id * 4, inc_val);

	return 0;
}

bool qed_send_qm_stop_cmd(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  bool is_release_cmd,
			  bool is_tx_pq,
			  u16 start_pq,
			  u16 num_pqs)
{
	u32 cmd_arr[QM_CMD_STRUCT_SIZE(QM_STOP_CMD)] = { 0 };
	u32 pq_mask = 0, last_pq = start_pq + num_pqs - 1, pq_id;

	/* set command's PQ type */
	QM_CMD_SET_FIELD(cmd_arr, QM_STOP_CMD, PQ_TYPE, is_tx_pq ? 0 : 1);

	for (pq_id = start_pq; pq_id <= last_pq; pq_id++) {
		/* set PQ bit in mask (stop command only) */
		if (!is_release_cmd)
			pq_mask |= (1 << (pq_id % QM_STOP_PQ_MASK_WIDTH));

		/* if last PQ or end of PQ mask, write command */
		if ((pq_id == last_pq) ||
		    (pq_id % QM_STOP_PQ_MASK_WIDTH ==
		     (QM_STOP_PQ_MASK_WIDTH - 1))) {
			QM_CMD_SET_FIELD(cmd_arr, QM_STOP_CMD,
					 PAUSE_MASK, pq_mask);
			QM_CMD_SET_FIELD(cmd_arr, QM_STOP_CMD,
					 GROUP_ID,
					 pq_id / QM_STOP_PQ_MASK_WIDTH);
			if (!qed_send_qm_cmd(p_hwfn, p_ptt, QM_STOP_CMD_ADDR,
					     cmd_arr[0], cmd_arr[1]))
				return false;
			pq_mask = 0;
		}
	}

	return true;
}
