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
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_reg_addr.h"

#define CDU_VALIDATION_DEFAULT_CFG	61

static u16 con_region_offsets[3][NUM_OF_CONNECTION_TYPES_E4] = {
	{400, 336, 352, 304, 304, 384, 416, 352},	/* region 3 offsets */
	{528, 496, 416, 448, 448, 512, 544, 480},	/* region 4 offsets */
	{608, 544, 496, 512, 576, 592, 624, 560}	/* region 5 offsets */
};

static u16 task_region_offsets[1][NUM_OF_CONNECTION_TYPES_E4] = {
	{240, 240, 112, 0, 0, 0, 0, 96}	/* region 1 offsets */
};

/* General constants */
#define QM_PQ_MEM_4KB(pq_size)	(pq_size ? DIV_ROUND_UP((pq_size + 1) *	\
							QM_PQ_ELEMENT_SIZE, \
							0x1000) : 0)
#define QM_PQ_SIZE_256B(pq_size)	(pq_size ? DIV_ROUND_UP(pq_size, \
								0x100) - 1 : 0)
#define QM_INVALID_PQ_ID		0xffff

/* Feature enable */
#define QM_BYPASS_EN	1
#define QM_BYTE_CRD_EN	1

/* Other PQ constants */
#define QM_OTHER_PQS_PER_PF	4

/* WFQ constants */

/* Upper bound in MB, 10 * burst size of 1ms in 50Gbps */
#define QM_WFQ_UPPER_BOUND	62500000

/* Bit  of VOQ in WFQ VP PQ map */
#define QM_WFQ_VP_PQ_VOQ_SHIFT	0

/* Bit  of PF in WFQ VP PQ map */
#define QM_WFQ_VP_PQ_PF_E4_SHIFT	5

/* 0x9000 = 4*9*1024 */
#define QM_WFQ_INC_VAL(weight)	((weight) * 0x9000)

/* Max WFQ increment value is 0.7 * upper bound */
#define QM_WFQ_MAX_INC_VAL	((QM_WFQ_UPPER_BOUND * 7) / 10)

/* RL constants */

/* Period in us */
#define QM_RL_PERIOD	5

/* Period in 25MHz cycles */
#define QM_RL_PERIOD_CLK_25M	(25 * QM_RL_PERIOD)

/* RL increment value - rate is specified in mbps */
#define QM_RL_INC_VAL(rate) ({ \
	typeof(rate) __rate = (rate); \
	max_t(u32, \
	      (u32)(((__rate ? __rate : 1000000) * QM_RL_PERIOD * 101) / \
		    (8 * 100)), \
	      1); })

/* PF RL Upper bound is set to 10 * burst size of 1ms in 50Gbps */
#define QM_PF_RL_UPPER_BOUND	62500000

/* Max PF RL increment value is 0.7 * upper bound */
#define QM_PF_RL_MAX_INC_VAL	((QM_PF_RL_UPPER_BOUND * 7) / 10)

/* Vport RL Upper bound, link speed is in Mpbs */
#define QM_VP_RL_UPPER_BOUND(speed)	((u32)max_t(u32, \
						    QM_RL_INC_VAL(speed), \
						    9700 + 1000))

/* Max Vport RL increment value is the Vport RL upper bound */
#define QM_VP_RL_MAX_INC_VAL(speed)	QM_VP_RL_UPPER_BOUND(speed)

/* Vport RL credit threshold in case of QM bypass */
#define QM_VP_RL_BYPASS_THRESH_SPEED	(QM_VP_RL_UPPER_BOUND(10000) - 1)

/* AFullOprtnstcCrdMask constants */
#define QM_OPPOR_LINE_VOQ_DEF	1
#define QM_OPPOR_FW_STOP_DEF	0
#define QM_OPPOR_PQ_EMPTY_DEF	1

/* Command Queue constants */

/* Pure LB CmdQ lines (+spare) */
#define PBF_CMDQ_PURE_LB_LINES	150

#define PBF_CMDQ_LINES_E5_RSVD_RATIO	8

#define PBF_CMDQ_LINES_RT_OFFSET(ext_voq) \
	(PBF_REG_YCMD_QS_NUM_LINES_VOQ0_RT_OFFSET + \
	 (ext_voq) * (PBF_REG_YCMD_QS_NUM_LINES_VOQ1_RT_OFFSET - \
		PBF_REG_YCMD_QS_NUM_LINES_VOQ0_RT_OFFSET))

#define PBF_BTB_GUARANTEED_RT_OFFSET(ext_voq) \
	(PBF_REG_BTB_GUARANTEED_VOQ0_RT_OFFSET + \
	 (ext_voq) * (PBF_REG_BTB_GUARANTEED_VOQ1_RT_OFFSET - \
		PBF_REG_BTB_GUARANTEED_VOQ0_RT_OFFSET))

#define QM_VOQ_LINE_CRD(pbf_cmd_lines) \
	((((pbf_cmd_lines) - 4) * 2) | QM_LINE_CRD_REG_SIGN_BIT)

/* BTB: blocks constants (block size = 256B) */

/* 256B blocks in 9700B packet */
#define BTB_JUMBO_PKT_BLOCKS	38

/* Headroom per-port */
#define BTB_HEADROOM_BLOCKS	BTB_JUMBO_PKT_BLOCKS
#define BTB_PURE_LB_FACTOR	10

/* Factored (hence really 0.7) */
#define BTB_PURE_LB_RATIO	7

/* QM stop command constants */
#define QM_STOP_PQ_MASK_WIDTH		32
#define QM_STOP_CMD_ADDR		2
#define QM_STOP_CMD_STRUCT_SIZE		2
#define QM_STOP_CMD_PAUSE_MASK_OFFSET	0
#define QM_STOP_CMD_PAUSE_MASK_SHIFT	0
#define QM_STOP_CMD_PAUSE_MASK_MASK	-1
#define QM_STOP_CMD_GROUP_ID_OFFSET	1
#define QM_STOP_CMD_GROUP_ID_SHIFT	16
#define QM_STOP_CMD_GROUP_ID_MASK	15
#define QM_STOP_CMD_PQ_TYPE_OFFSET	1
#define QM_STOP_CMD_PQ_TYPE_SHIFT	24
#define QM_STOP_CMD_PQ_TYPE_MASK	1
#define QM_STOP_CMD_MAX_POLL_COUNT	100
#define QM_STOP_CMD_POLL_PERIOD_US	500

/* QM command macros */
#define QM_CMD_STRUCT_SIZE(cmd)	cmd ## _STRUCT_SIZE
#define QM_CMD_SET_FIELD(var, cmd, field, value) \
	SET_FIELD(var[cmd ## _ ## field ## _OFFSET], \
		  cmd ## _ ## field, \
		  value)

#define QM_INIT_TX_PQ_MAP(p_hwfn, map, chip, pq_id, rl_valid, vp_pq_id, rl_id, \
			  ext_voq, wrr) \
	do { \
		typeof(map) __map; \
		memset(&__map, 0, sizeof(__map)); \
		SET_FIELD(__map.reg, QM_RF_PQ_MAP_ ## chip ## _PQ_VALID, 1); \
		SET_FIELD(__map.reg, QM_RF_PQ_MAP_ ## chip ## _RL_VALID, \
			  rl_valid); \
		SET_FIELD(__map.reg, QM_RF_PQ_MAP_ ## chip ## _VP_PQ_ID, \
			  vp_pq_id); \
		SET_FIELD(__map.reg, QM_RF_PQ_MAP_ ## chip ## _RL_ID, rl_id); \
		SET_FIELD(__map.reg, QM_RF_PQ_MAP_ ## chip ## _VOQ, ext_voq); \
		SET_FIELD(__map.reg, \
			  QM_RF_PQ_MAP_ ## chip ## _WRR_WEIGHT_GROUP, wrr); \
		STORE_RT_REG(p_hwfn, QM_REG_TXPQMAP_RT_OFFSET + (pq_id), \
			     *((u32 *)&__map)); \
		(map) = __map; \
	} while (0)

#define WRITE_PQ_INFO_TO_RAM	1
#define PQ_INFO_ELEMENT(vp, pf, tc, port, rl_valid, rl) \
	(((vp) << 0) | ((pf) << 12) | ((tc) << 16) | ((port) << 20) | \
	((rl_valid) << 22) | ((rl) << 24))
#define PQ_INFO_RAM_GRC_ADDRESS(pq_id) \
	(XSEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM + 21776 + (pq_id) * 4)

/******************** INTERNAL IMPLEMENTATION *********************/

/* Returns the external VOQ number */
static u8 qed_get_ext_voq(struct qed_hwfn *p_hwfn,
			  u8 port_id, u8 tc, u8 max_phys_tcs_per_port)
{
	if (tc == PURE_LB_TC)
		return NUM_OF_PHYS_TCS * MAX_NUM_PORTS_BB + port_id;
	else
		return port_id * max_phys_tcs_per_port + tc;
}

/* Prepare PF RL enable/disable runtime init values */
static void qed_enable_pf_rl(struct qed_hwfn *p_hwfn, bool pf_rl_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_RLPFENABLE_RT_OFFSET, pf_rl_en ? 1 : 0);
	if (pf_rl_en) {
		u8 num_ext_voqs = MAX_NUM_VOQS_E4;
		u64 voq_bit_mask = ((u64)1 << num_ext_voqs) - 1;

		/* Enable RLs for all VOQs */
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLPFVOQENABLE_RT_OFFSET,
			     (u32)voq_bit_mask);
		if (num_ext_voqs >= 32)
			STORE_RT_REG(p_hwfn, QM_REG_RLPFVOQENABLE_MSB_RT_OFFSET,
				     (u32)(voq_bit_mask >> 32));

		/* Write RL period */
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLPFPERIOD_RT_OFFSET, QM_RL_PERIOD_CLK_25M);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLPFPERIODTIMER_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);

		/* Set credit threshold for QM bypass flow */
		if (QM_BYPASS_EN)
			STORE_RT_REG(p_hwfn,
				     QM_REG_AFULLQMBYPTHRPFRL_RT_OFFSET,
				     QM_PF_RL_UPPER_BOUND);
	}
}

/* Prepare PF WFQ enable/disable runtime init values */
static void qed_enable_pf_wfq(struct qed_hwfn *p_hwfn, bool pf_wfq_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_WFQPFENABLE_RT_OFFSET, pf_wfq_en ? 1 : 0);

	/* Set credit threshold for QM bypass flow */
	if (pf_wfq_en && QM_BYPASS_EN)
		STORE_RT_REG(p_hwfn,
			     QM_REG_AFULLQMBYPTHRPFWFQ_RT_OFFSET,
			     QM_WFQ_UPPER_BOUND);
}

/* Prepare VPORT RL enable/disable runtime init values */
static void qed_enable_vport_rl(struct qed_hwfn *p_hwfn, bool vport_rl_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_RLGLBLENABLE_RT_OFFSET,
		     vport_rl_en ? 1 : 0);
	if (vport_rl_en) {
		/* Write RL period (use timer 0 only) */
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLPERIOD_0_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLPERIODTIMER_0_RT_OFFSET,
			     QM_RL_PERIOD_CLK_25M);

		/* Set credit threshold for QM bypass flow */
		if (QM_BYPASS_EN)
			STORE_RT_REG(p_hwfn,
				     QM_REG_AFULLQMBYPTHRGLBLRL_RT_OFFSET,
				     QM_VP_RL_BYPASS_THRESH_SPEED);
	}
}

/* Prepare VPORT WFQ enable/disable runtime init values */
static void qed_enable_vport_wfq(struct qed_hwfn *p_hwfn, bool vport_wfq_en)
{
	STORE_RT_REG(p_hwfn, QM_REG_WFQVPENABLE_RT_OFFSET,
		     vport_wfq_en ? 1 : 0);

	/* Set credit threshold for QM bypass flow */
	if (vport_wfq_en && QM_BYPASS_EN)
		STORE_RT_REG(p_hwfn,
			     QM_REG_AFULLQMBYPTHRVPWFQ_RT_OFFSET,
			     QM_WFQ_UPPER_BOUND);
}

/* Prepare runtime init values to allocate PBF command queue lines for
 * the specified VOQ.
 */
static void qed_cmdq_lines_voq_rt_init(struct qed_hwfn *p_hwfn,
				       u8 ext_voq, u16 cmdq_lines)
{
	u32 qm_line_crd = QM_VOQ_LINE_CRD(cmdq_lines);

	OVERWRITE_RT_REG(p_hwfn, PBF_CMDQ_LINES_RT_OFFSET(ext_voq),
			 (u32)cmdq_lines);
	STORE_RT_REG(p_hwfn, QM_REG_VOQCRDLINE_RT_OFFSET + ext_voq,
		     qm_line_crd);
	STORE_RT_REG(p_hwfn, QM_REG_VOQINITCRDLINE_RT_OFFSET + ext_voq,
		     qm_line_crd);
}

/* Prepare runtime init values to allocate PBF command queue lines. */
static void qed_cmdq_lines_rt_init(
	struct qed_hwfn *p_hwfn,
	u8 max_ports_per_engine,
	u8 max_phys_tcs_per_port,
	struct init_qm_port_params port_params[MAX_NUM_PORTS])
{
	u8 tc, ext_voq, port_id, num_tcs_in_port;
	u8 num_ext_voqs = MAX_NUM_VOQS_E4;

	/* Clear PBF lines of all VOQs */
	for (ext_voq = 0; ext_voq < num_ext_voqs; ext_voq++)
		STORE_RT_REG(p_hwfn, PBF_CMDQ_LINES_RT_OFFSET(ext_voq), 0);

	for (port_id = 0; port_id < max_ports_per_engine; port_id++) {
		u16 phys_lines, phys_lines_per_tc;

		if (!port_params[port_id].active)
			continue;

		/* Find number of command queue lines to divide between the
		 * active physical TCs. In E5, 1/8 of the lines are reserved.
		 * the lines for pure LB TC are subtracted.
		 */
		phys_lines = port_params[port_id].num_pbf_cmd_lines;
		phys_lines -= PBF_CMDQ_PURE_LB_LINES;

		/* Find #lines per active physical TC */
		num_tcs_in_port = 0;
		for (tc = 0; tc < max_phys_tcs_per_port; tc++)
			if (((port_params[port_id].active_phys_tcs >>
			      tc) & 0x1) == 1)
				num_tcs_in_port++;
		phys_lines_per_tc = phys_lines / num_tcs_in_port;

		/* Init registers per active TC */
		for (tc = 0; tc < max_phys_tcs_per_port; tc++) {
			ext_voq = qed_get_ext_voq(p_hwfn,
						  port_id,
						  tc, max_phys_tcs_per_port);
			if (((port_params[port_id].active_phys_tcs >>
			      tc) & 0x1) == 1)
				qed_cmdq_lines_voq_rt_init(p_hwfn,
							   ext_voq,
							   phys_lines_per_tc);
		}

		/* Init registers for pure LB TC */
		ext_voq = qed_get_ext_voq(p_hwfn,
					  port_id,
					  PURE_LB_TC, max_phys_tcs_per_port);
		qed_cmdq_lines_voq_rt_init(p_hwfn,
					   ext_voq, PBF_CMDQ_PURE_LB_LINES);
	}
}

static void qed_btb_blocks_rt_init(
	struct qed_hwfn *p_hwfn,
	u8 max_ports_per_engine,
	u8 max_phys_tcs_per_port,
	struct init_qm_port_params port_params[MAX_NUM_PORTS])
{
	u32 usable_blocks, pure_lb_blocks, phys_blocks;
	u8 tc, ext_voq, port_id, num_tcs_in_port;

	for (port_id = 0; port_id < max_ports_per_engine; port_id++) {
		if (!port_params[port_id].active)
			continue;

		/* Subtract headroom blocks */
		usable_blocks = port_params[port_id].num_btb_blocks -
				BTB_HEADROOM_BLOCKS;

		/* Find blocks per physical TC. Use factor to avoid floating
		 * arithmethic.
		 */
		num_tcs_in_port = 0;
		for (tc = 0; tc < NUM_OF_PHYS_TCS; tc++)
			if (((port_params[port_id].active_phys_tcs >>
			      tc) & 0x1) == 1)
				num_tcs_in_port++;

		pure_lb_blocks = (usable_blocks * BTB_PURE_LB_FACTOR) /
				 (num_tcs_in_port * BTB_PURE_LB_FACTOR +
				  BTB_PURE_LB_RATIO);
		pure_lb_blocks = max_t(u32, BTB_JUMBO_PKT_BLOCKS,
				       pure_lb_blocks / BTB_PURE_LB_FACTOR);
		phys_blocks = (usable_blocks - pure_lb_blocks) /
			      num_tcs_in_port;

		/* Init physical TCs */
		for (tc = 0; tc < NUM_OF_PHYS_TCS; tc++) {
			if (((port_params[port_id].active_phys_tcs >>
			      tc) & 0x1) == 1) {
				ext_voq =
					qed_get_ext_voq(p_hwfn,
							port_id,
							tc,
							max_phys_tcs_per_port);
				STORE_RT_REG(p_hwfn,
					     PBF_BTB_GUARANTEED_RT_OFFSET
					     (ext_voq), phys_blocks);
			}
		}

		/* Init pure LB TC */
		ext_voq = qed_get_ext_voq(p_hwfn,
					  port_id,
					  PURE_LB_TC, max_phys_tcs_per_port);
		STORE_RT_REG(p_hwfn, PBF_BTB_GUARANTEED_RT_OFFSET(ext_voq),
			     pure_lb_blocks);
	}
}

/* Prepare Tx PQ mapping runtime init values for the specified PF */
static void qed_tx_pq_map_rt_init(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_qm_pf_rt_init_params *p_params,
				  u32 base_mem_addr_4kb)
{
	u32 tx_pq_vf_mask[MAX_QM_TX_QUEUES / QM_PF_QUEUE_GROUP_SIZE] = { 0 };
	struct init_qm_vport_params *vport_params = p_params->vport_params;
	u32 num_tx_pq_vf_masks = MAX_QM_TX_QUEUES / QM_PF_QUEUE_GROUP_SIZE;
	u16 num_pqs, first_pq_group, last_pq_group, i, j, pq_id, pq_group;
	struct init_qm_pq_params *pq_params = p_params->pq_params;
	u32 pq_mem_4kb, vport_pq_mem_4kb, mem_addr_4kb;

	num_pqs = p_params->num_pf_pqs + p_params->num_vf_pqs;

	first_pq_group = p_params->start_pq / QM_PF_QUEUE_GROUP_SIZE;
	last_pq_group = (p_params->start_pq + num_pqs - 1) /
			QM_PF_QUEUE_GROUP_SIZE;

	pq_mem_4kb = QM_PQ_MEM_4KB(p_params->num_pf_cids);
	vport_pq_mem_4kb = QM_PQ_MEM_4KB(p_params->num_vf_cids);
	mem_addr_4kb = base_mem_addr_4kb;

	/* Set mapping from PQ group to PF */
	for (pq_group = first_pq_group; pq_group <= last_pq_group; pq_group++)
		STORE_RT_REG(p_hwfn, QM_REG_PQTX2PF_0_RT_OFFSET + pq_group,
			     (u32)(p_params->pf_id));

	/* Set PQ sizes */
	STORE_RT_REG(p_hwfn, QM_REG_MAXPQSIZE_0_RT_OFFSET,
		     QM_PQ_SIZE_256B(p_params->num_pf_cids));
	STORE_RT_REG(p_hwfn, QM_REG_MAXPQSIZE_1_RT_OFFSET,
		     QM_PQ_SIZE_256B(p_params->num_vf_cids));

	/* Go over all Tx PQs */
	for (i = 0, pq_id = p_params->start_pq; i < num_pqs; i++, pq_id++) {
		u8 ext_voq, vport_id_in_pf, tc_id = pq_params[i].tc_id;
		u32 max_qm_global_rls = MAX_QM_GLOBAL_RLS;
		struct qm_rf_pq_map_e4 tx_pq_map;
		bool is_vf_pq, rl_valid;
		u16 *p_first_tx_pq_id;

		ext_voq = qed_get_ext_voq(p_hwfn,
					  pq_params[i].port_id,
					  tc_id,
					  p_params->max_phys_tcs_per_port);
		is_vf_pq = (i >= p_params->num_pf_pqs);
		rl_valid = pq_params[i].rl_valid > 0;

		/* Update first Tx PQ of VPORT/TC */
		vport_id_in_pf = pq_params[i].vport_id - p_params->start_vport;
		p_first_tx_pq_id =
		    &vport_params[vport_id_in_pf].first_tx_pq_id[tc_id];
		if (*p_first_tx_pq_id == QM_INVALID_PQ_ID) {
			u32 map_val =
				(ext_voq << QM_WFQ_VP_PQ_VOQ_SHIFT) |
				(p_params->pf_id << QM_WFQ_VP_PQ_PF_E4_SHIFT);

			/* Create new VP PQ */
			*p_first_tx_pq_id = pq_id;

			/* Map VP PQ to VOQ and PF */
			STORE_RT_REG(p_hwfn,
				     QM_REG_WFQVPMAP_RT_OFFSET +
				     *p_first_tx_pq_id,
				     map_val);
		}

		/* Check RL ID */
		if (rl_valid && pq_params[i].vport_id >= max_qm_global_rls) {
			DP_NOTICE(p_hwfn,
				  "Invalid VPORT ID for rate limiter configuration\n");
			rl_valid = false;
		}

		/* Prepare PQ map entry */
		QM_INIT_TX_PQ_MAP(p_hwfn,
				  tx_pq_map,
				  E4,
				  pq_id,
				  rl_valid ? 1 : 0,
				  *p_first_tx_pq_id,
				  rl_valid ? pq_params[i].vport_id : 0,
				  ext_voq, pq_params[i].wrr_group);

		/* Set PQ base address */
		STORE_RT_REG(p_hwfn,
			     QM_REG_BASEADDRTXPQ_RT_OFFSET + pq_id,
			     mem_addr_4kb);

		/* Clear PQ pointer table entry (64 bit) */
		if (p_params->is_pf_loading)
			for (j = 0; j < 2; j++)
				STORE_RT_REG(p_hwfn,
					     QM_REG_PTRTBLTX_RT_OFFSET +
					     (pq_id * 2) + j, 0);

		/* Write PQ info to RAM */
		if (WRITE_PQ_INFO_TO_RAM != 0) {
			u32 pq_info = 0;

			pq_info = PQ_INFO_ELEMENT(*p_first_tx_pq_id,
						  p_params->pf_id,
						  tc_id,
						  pq_params[i].port_id,
						  rl_valid ? 1 : 0,
						  rl_valid ?
						  pq_params[i].vport_id : 0);
			qed_wr(p_hwfn, p_ptt, PQ_INFO_RAM_GRC_ADDRESS(pq_id),
			       pq_info);
		}

		/* If VF PQ, add indication to PQ VF mask */
		if (is_vf_pq) {
			tx_pq_vf_mask[pq_id /
				      QM_PF_QUEUE_GROUP_SIZE] |=
			    BIT((pq_id % QM_PF_QUEUE_GROUP_SIZE));
			mem_addr_4kb += vport_pq_mem_4kb;
		} else {
			mem_addr_4kb += pq_mem_4kb;
		}
	}

	/* Store Tx PQ VF mask to size select register */
	for (i = 0; i < num_tx_pq_vf_masks; i++)
		if (tx_pq_vf_mask[i])
			STORE_RT_REG(p_hwfn,
				     QM_REG_MAXPQSIZETXSEL_0_RT_OFFSET + i,
				     tx_pq_vf_mask[i]);
}

/* Prepare Other PQ mapping runtime init values for the specified PF */
static void qed_other_pq_map_rt_init(struct qed_hwfn *p_hwfn,
				     u8 pf_id,
				     bool is_pf_loading,
				     u32 num_pf_cids,
				     u32 num_tids, u32 base_mem_addr_4kb)
{
	u32 pq_size, pq_mem_4kb, mem_addr_4kb;
	u16 i, j, pq_id, pq_group;

	/* A single other PQ group is used in each PF, where PQ group i is used
	 * in PF i.
	 */
	pq_group = pf_id;
	pq_size = num_pf_cids + num_tids;
	pq_mem_4kb = QM_PQ_MEM_4KB(pq_size);
	mem_addr_4kb = base_mem_addr_4kb;

	/* Map PQ group to PF */
	STORE_RT_REG(p_hwfn, QM_REG_PQOTHER2PF_0_RT_OFFSET + pq_group,
		     (u32)(pf_id));

	/* Set PQ sizes */
	STORE_RT_REG(p_hwfn, QM_REG_MAXPQSIZE_2_RT_OFFSET,
		     QM_PQ_SIZE_256B(pq_size));

	for (i = 0, pq_id = pf_id * QM_PF_QUEUE_GROUP_SIZE;
	     i < QM_OTHER_PQS_PER_PF; i++, pq_id++) {
		/* Set PQ base address */
		STORE_RT_REG(p_hwfn,
			     QM_REG_BASEADDROTHERPQ_RT_OFFSET + pq_id,
			     mem_addr_4kb);

		/* Clear PQ pointer table entry */
		if (is_pf_loading)
			for (j = 0; j < 2; j++)
				STORE_RT_REG(p_hwfn,
					     QM_REG_PTRTBLOTHER_RT_OFFSET +
					     (pq_id * 2) + j, 0);

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
	struct init_qm_pq_params *pq_params = p_params->pq_params;
	u32 inc_val, crd_reg_offset;
	u8 ext_voq;
	u16 i;

	inc_val = QM_WFQ_INC_VAL(p_params->pf_wfq);
	if (!inc_val || inc_val > QM_WFQ_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF WFQ weight configuration\n");
		return -1;
	}

	for (i = 0; i < num_tx_pqs; i++) {
		ext_voq = qed_get_ext_voq(p_hwfn,
					  pq_params[i].port_id,
					  pq_params[i].tc_id,
					  p_params->max_phys_tcs_per_port);
		crd_reg_offset =
			(p_params->pf_id < MAX_NUM_PFS_BB ?
			 QM_REG_WFQPFCRD_RT_OFFSET :
			 QM_REG_WFQPFCRD_MSB_RT_OFFSET) +
			ext_voq * MAX_NUM_PFS_BB +
			(p_params->pf_id % MAX_NUM_PFS_BB);
		OVERWRITE_RT_REG(p_hwfn,
				 crd_reg_offset, (u32)QM_WFQ_CRD_REG_SIGN_BIT);
	}

	STORE_RT_REG(p_hwfn,
		     QM_REG_WFQPFUPPERBOUND_RT_OFFSET + p_params->pf_id,
		     QM_WFQ_UPPER_BOUND | (u32)QM_WFQ_CRD_REG_SIGN_BIT);
	STORE_RT_REG(p_hwfn, QM_REG_WFQPFWEIGHT_RT_OFFSET + p_params->pf_id,
		     inc_val);

	return 0;
}

/* Prepare PF RL runtime init values for the specified PF.
 * Return -1 on error.
 */
static int qed_pf_rl_rt_init(struct qed_hwfn *p_hwfn, u8 pf_id, u32 pf_rl)
{
	u32 inc_val = QM_RL_INC_VAL(pf_rl);

	if (inc_val > QM_PF_RL_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF rate limit configuration\n");
		return -1;
	}

	STORE_RT_REG(p_hwfn,
		     QM_REG_RLPFCRD_RT_OFFSET + pf_id,
		     (u32)QM_RL_CRD_REG_SIGN_BIT);
	STORE_RT_REG(p_hwfn,
		     QM_REG_RLPFUPPERBOUND_RT_OFFSET + pf_id,
		     QM_PF_RL_UPPER_BOUND | (u32)QM_RL_CRD_REG_SIGN_BIT);
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
	u16 vport_pq_id;
	u32 inc_val;
	u8 tc, i;

	/* Go over all PF VPORTs */
	for (i = 0; i < num_vports; i++) {
		if (!vport_params[i].vport_wfq)
			continue;

		inc_val = QM_WFQ_INC_VAL(vport_params[i].vport_wfq);
		if (inc_val > QM_WFQ_MAX_INC_VAL) {
			DP_NOTICE(p_hwfn,
				  "Invalid VPORT WFQ weight configuration\n");
			return -1;
		}

		/* Each VPORT can have several VPORT PQ IDs for various TCs */
		for (tc = 0; tc < NUM_OF_TCS; tc++) {
			vport_pq_id = vport_params[i].first_tx_pq_id[tc];
			if (vport_pq_id != QM_INVALID_PQ_ID) {
				STORE_RT_REG(p_hwfn,
					     QM_REG_WFQVPCRD_RT_OFFSET +
					     vport_pq_id,
					     (u32)QM_WFQ_CRD_REG_SIGN_BIT);
				STORE_RT_REG(p_hwfn,
					     QM_REG_WFQVPWEIGHT_RT_OFFSET +
					     vport_pq_id, inc_val);
			}
		}
	}

	return 0;
}

/* Prepare VPORT RL runtime init values for the specified VPORTs.
 * Return -1 on error.
 */
static int qed_vport_rl_rt_init(struct qed_hwfn *p_hwfn,
				u8 start_vport,
				u8 num_vports,
				u32 link_speed,
				struct init_qm_vport_params *vport_params)
{
	u8 i, vport_id;
	u32 inc_val;

	if (start_vport + num_vports >= MAX_QM_GLOBAL_RLS) {
		DP_NOTICE(p_hwfn,
			  "Invalid VPORT ID for rate limiter configuration\n");
		return -1;
	}

	/* Go over all PF VPORTs */
	for (i = 0, vport_id = start_vport; i < num_vports; i++, vport_id++) {
		inc_val = QM_RL_INC_VAL(vport_params[i].vport_rl ?
			  vport_params[i].vport_rl :
			  link_speed);
		if (inc_val > QM_VP_RL_MAX_INC_VAL(link_speed)) {
			DP_NOTICE(p_hwfn,
				  "Invalid VPORT rate-limit configuration\n");
			return -1;
		}

		STORE_RT_REG(p_hwfn, QM_REG_RLGLBLCRD_RT_OFFSET + vport_id,
			     (u32)QM_RL_CRD_REG_SIGN_BIT);
		STORE_RT_REG(p_hwfn,
			     QM_REG_RLGLBLUPPERBOUND_RT_OFFSET + vport_id,
			     QM_VP_RL_UPPER_BOUND(link_speed) |
			     (u32)QM_RL_CRD_REG_SIGN_BIT);
		STORE_RT_REG(p_hwfn, QM_REG_RLGLBLINCVAL_RT_OFFSET + vport_id,
			     inc_val);
	}

	return 0;
}

static bool qed_poll_on_qm_cmd_ready(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt)
{
	u32 reg_val, i;

	for (i = 0, reg_val = 0; i < QM_STOP_CMD_MAX_POLL_COUNT && !reg_val;
	     i++) {
		udelay(QM_STOP_CMD_POLL_PERIOD_US);
		reg_val = qed_rd(p_hwfn, p_ptt, QM_REG_SDMCMDREADY);
	}

	/* Check if timeout while waiting for SDM command ready */
	if (i == QM_STOP_CMD_MAX_POLL_COUNT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "Timeout when waiting for QM SDM command ready signal\n");
		return false;
	}

	return true;
}

static bool qed_send_qm_cmd(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u32 cmd_addr, u32 cmd_data_lsb, u32 cmd_data_msb)
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

u32 qed_qm_pf_mem_size(u32 num_pf_cids,
		       u32 num_vf_cids,
		       u32 num_tids, u16 num_pf_pqs, u16 num_vf_pqs)
{
	return QM_PQ_MEM_4KB(num_pf_cids) * num_pf_pqs +
	       QM_PQ_MEM_4KB(num_vf_cids) * num_vf_pqs +
	       QM_PQ_MEM_4KB(num_pf_cids + num_tids) * QM_OTHER_PQS_PER_PF;
}

int qed_qm_common_rt_init(struct qed_hwfn *p_hwfn,
			  struct qed_qm_common_rt_init_params *p_params)
{
	/* Init AFullOprtnstcCrdMask */
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

	/* Enable/disable PF RL */
	qed_enable_pf_rl(p_hwfn, p_params->pf_rl_en);

	/* Enable/disable PF WFQ */
	qed_enable_pf_wfq(p_hwfn, p_params->pf_wfq_en);

	/* Enable/disable VPORT RL */
	qed_enable_vport_rl(p_hwfn, p_params->vport_rl_en);

	/* Enable/disable VPORT WFQ */
	qed_enable_vport_wfq(p_hwfn, p_params->vport_wfq_en);

	/* Init PBF CMDQ line credit */
	qed_cmdq_lines_rt_init(p_hwfn,
			       p_params->max_ports_per_engine,
			       p_params->max_phys_tcs_per_port,
			       p_params->port_params);

	/* Init BTB blocks in PBF */
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

	/* Clear first Tx PQ ID array for each VPORT */
	for (i = 0; i < p_params->num_vports; i++)
		for (tc = 0; tc < NUM_OF_TCS; tc++)
			vport_params[i].first_tx_pq_id[tc] = QM_INVALID_PQ_ID;

	/* Map Other PQs (if any) */
	qed_other_pq_map_rt_init(p_hwfn,
				 p_params->pf_id,
				 p_params->is_pf_loading, p_params->num_pf_cids,
				 p_params->num_tids, 0);

	/* Map Tx PQs */
	qed_tx_pq_map_rt_init(p_hwfn, p_ptt, p_params, other_mem_size_4kb);

	/* Init PF WFQ */
	if (p_params->pf_wfq)
		if (qed_pf_wfq_rt_init(p_hwfn, p_params))
			return -1;

	/* Init PF RL */
	if (qed_pf_rl_rt_init(p_hwfn, p_params->pf_id, p_params->pf_rl))
		return -1;

	/* Set VPORT WFQ */
	if (qed_vp_wfq_rt_init(p_hwfn, p_params->num_vports, vport_params))
		return -1;

	/* Set VPORT RL */
	if (qed_vport_rl_rt_init(p_hwfn, p_params->start_vport,
				 p_params->num_vports, p_params->link_speed,
				 vport_params))
		return -1;

	return 0;
}

int qed_init_pf_wfq(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, u8 pf_id, u16 pf_wfq)
{
	u32 inc_val = QM_WFQ_INC_VAL(pf_wfq);

	if (!inc_val || inc_val > QM_WFQ_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF WFQ weight configuration\n");
		return -1;
	}

	qed_wr(p_hwfn, p_ptt, QM_REG_WFQPFWEIGHT + pf_id * 4, inc_val);

	return 0;
}

int qed_init_pf_rl(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, u8 pf_id, u32 pf_rl)
{
	u32 inc_val = QM_RL_INC_VAL(pf_rl);

	if (inc_val > QM_PF_RL_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid PF rate limit configuration\n");
		return -1;
	}

	qed_wr(p_hwfn,
	       p_ptt, QM_REG_RLPFCRD + pf_id * 4, (u32)QM_RL_CRD_REG_SIGN_BIT);
	qed_wr(p_hwfn, p_ptt, QM_REG_RLPFINCVAL + pf_id * 4, inc_val);

	return 0;
}

int qed_init_vport_wfq(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       u16 first_tx_pq_id[NUM_OF_TCS], u16 vport_wfq)
{
	u16 vport_pq_id;
	u32 inc_val;
	u8 tc;

	inc_val = QM_WFQ_INC_VAL(vport_wfq);
	if (!inc_val || inc_val > QM_WFQ_MAX_INC_VAL) {
		DP_NOTICE(p_hwfn, "Invalid VPORT WFQ weight configuration\n");
		return -1;
	}

	for (tc = 0; tc < NUM_OF_TCS; tc++) {
		vport_pq_id = first_tx_pq_id[tc];
		if (vport_pq_id != QM_INVALID_PQ_ID)
			qed_wr(p_hwfn,
			       p_ptt,
			       QM_REG_WFQVPWEIGHT + vport_pq_id * 4, inc_val);
	}

	return 0;
}

int qed_init_vport_rl(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u8 vport_id, u32 vport_rl, u32 link_speed)
{
	u32 inc_val, max_qm_global_rls = MAX_QM_GLOBAL_RLS;

	if (vport_id >= max_qm_global_rls) {
		DP_NOTICE(p_hwfn,
			  "Invalid VPORT ID for rate limiter configuration\n");
		return -1;
	}

	inc_val = QM_RL_INC_VAL(vport_rl ? vport_rl : link_speed);
	if (inc_val > QM_VP_RL_MAX_INC_VAL(link_speed)) {
		DP_NOTICE(p_hwfn, "Invalid VPORT rate-limit configuration\n");
		return -1;
	}

	qed_wr(p_hwfn,
	       p_ptt,
	       QM_REG_RLGLBLCRD + vport_id * 4, (u32)QM_RL_CRD_REG_SIGN_BIT);
	qed_wr(p_hwfn, p_ptt, QM_REG_RLGLBLINCVAL + vport_id * 4, inc_val);

	return 0;
}

bool qed_send_qm_stop_cmd(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  bool is_release_cmd,
			  bool is_tx_pq, u16 start_pq, u16 num_pqs)
{
	u32 cmd_arr[QM_CMD_STRUCT_SIZE(QM_STOP_CMD)] = { 0 };
	u32 pq_mask = 0, last_pq, pq_id;

	last_pq = start_pq + num_pqs - 1;

	/* Set command's PQ type */
	QM_CMD_SET_FIELD(cmd_arr, QM_STOP_CMD, PQ_TYPE, is_tx_pq ? 0 : 1);

	/* Go over requested PQs */
	for (pq_id = start_pq; pq_id <= last_pq; pq_id++) {
		/* Set PQ bit in mask (stop command only) */
		if (!is_release_cmd)
			pq_mask |= BIT((pq_id % QM_STOP_PQ_MASK_WIDTH));

		/* If last PQ or end of PQ mask, write command */
		if ((pq_id == last_pq) ||
		    (pq_id % QM_STOP_PQ_MASK_WIDTH ==
		     (QM_STOP_PQ_MASK_WIDTH - 1))) {
			QM_CMD_SET_FIELD(cmd_arr,
					 QM_STOP_CMD, PAUSE_MASK, pq_mask);
			QM_CMD_SET_FIELD(cmd_arr,
					 QM_STOP_CMD,
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


#define SET_TUNNEL_TYPE_ENABLE_BIT(var, offset, enable) \
	do { \
		typeof(var) *__p_var = &(var); \
		typeof(offset) __offset = offset; \
		*__p_var = (*__p_var & ~BIT(__offset)) | \
			   ((enable) ? BIT(__offset) : 0); \
	} while (0)
#define PRS_ETH_TUNN_OUTPUT_FORMAT        -188897008
#define PRS_ETH_OUTPUT_FORMAT             -46832

void qed_set_vxlan_dest_port(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u16 dest_port)
{
	/* Update PRS register */
	qed_wr(p_hwfn, p_ptt, PRS_REG_VXLAN_PORT, dest_port);

	/* Update NIG register */
	qed_wr(p_hwfn, p_ptt, NIG_REG_VXLAN_CTRL, dest_port);

	/* Update PBF register */
	qed_wr(p_hwfn, p_ptt, PBF_REG_VXLAN_PORT, dest_port);
}

void qed_set_vxlan_enable(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, bool vxlan_enable)
{
	u32 reg_val;
	u8 shift;

	/* Update PRS register */
	reg_val = qed_rd(p_hwfn, p_ptt, PRS_REG_ENCAPSULATION_TYPE_EN);
	shift = PRS_REG_ENCAPSULATION_TYPE_EN_VXLAN_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, vxlan_enable);
	qed_wr(p_hwfn, p_ptt, PRS_REG_ENCAPSULATION_TYPE_EN, reg_val);
	if (reg_val) {
		reg_val =
		    qed_rd(p_hwfn, p_ptt, PRS_REG_OUTPUT_FORMAT_4_0_BB_K2);

		/* Update output  only if tunnel blocks not included. */
		if (reg_val == (u32)PRS_ETH_OUTPUT_FORMAT)
			qed_wr(p_hwfn, p_ptt, PRS_REG_OUTPUT_FORMAT_4_0_BB_K2,
			       (u32)PRS_ETH_TUNN_OUTPUT_FORMAT);
	}

	/* Update NIG register */
	reg_val = qed_rd(p_hwfn, p_ptt, NIG_REG_ENC_TYPE_ENABLE);
	shift = NIG_REG_ENC_TYPE_ENABLE_VXLAN_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, vxlan_enable);
	qed_wr(p_hwfn, p_ptt, NIG_REG_ENC_TYPE_ENABLE, reg_val);

	/* Update DORQ register */
	qed_wr(p_hwfn,
	       p_ptt, DORQ_REG_L2_EDPM_TUNNEL_VXLAN_EN, vxlan_enable ? 1 : 0);
}

void qed_set_gre_enable(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			bool eth_gre_enable, bool ip_gre_enable)
{
	u32 reg_val;
	u8 shift;

	/* Update PRS register */
	reg_val = qed_rd(p_hwfn, p_ptt, PRS_REG_ENCAPSULATION_TYPE_EN);
	shift = PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GRE_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, eth_gre_enable);
	shift = PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GRE_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, ip_gre_enable);
	qed_wr(p_hwfn, p_ptt, PRS_REG_ENCAPSULATION_TYPE_EN, reg_val);
	if (reg_val) {
		reg_val =
		    qed_rd(p_hwfn, p_ptt, PRS_REG_OUTPUT_FORMAT_4_0_BB_K2);

		/* Update output  only if tunnel blocks not included. */
		if (reg_val == (u32)PRS_ETH_OUTPUT_FORMAT)
			qed_wr(p_hwfn, p_ptt, PRS_REG_OUTPUT_FORMAT_4_0_BB_K2,
			       (u32)PRS_ETH_TUNN_OUTPUT_FORMAT);
	}

	/* Update NIG register */
	reg_val = qed_rd(p_hwfn, p_ptt, NIG_REG_ENC_TYPE_ENABLE);
	shift = NIG_REG_ENC_TYPE_ENABLE_ETH_OVER_GRE_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, eth_gre_enable);
	shift = NIG_REG_ENC_TYPE_ENABLE_IP_OVER_GRE_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, ip_gre_enable);
	qed_wr(p_hwfn, p_ptt, NIG_REG_ENC_TYPE_ENABLE, reg_val);

	/* Update DORQ registers */
	qed_wr(p_hwfn,
	       p_ptt,
	       DORQ_REG_L2_EDPM_TUNNEL_GRE_ETH_EN, eth_gre_enable ? 1 : 0);
	qed_wr(p_hwfn,
	       p_ptt, DORQ_REG_L2_EDPM_TUNNEL_GRE_IP_EN, ip_gre_enable ? 1 : 0);
}

void qed_set_geneve_dest_port(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt, u16 dest_port)
{
	/* Update PRS register */
	qed_wr(p_hwfn, p_ptt, PRS_REG_NGE_PORT, dest_port);

	/* Update NIG register */
	qed_wr(p_hwfn, p_ptt, NIG_REG_NGE_PORT, dest_port);

	/* Update PBF register */
	qed_wr(p_hwfn, p_ptt, PBF_REG_NGE_PORT, dest_port);
}

void qed_set_geneve_enable(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt,
			   bool eth_geneve_enable, bool ip_geneve_enable)
{
	u32 reg_val;
	u8 shift;

	/* Update PRS register */
	reg_val = qed_rd(p_hwfn, p_ptt, PRS_REG_ENCAPSULATION_TYPE_EN);
	shift = PRS_REG_ENCAPSULATION_TYPE_EN_ETH_OVER_GENEVE_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, eth_geneve_enable);
	shift = PRS_REG_ENCAPSULATION_TYPE_EN_IP_OVER_GENEVE_ENABLE_SHIFT;
	SET_TUNNEL_TYPE_ENABLE_BIT(reg_val, shift, ip_geneve_enable);
	qed_wr(p_hwfn, p_ptt, PRS_REG_ENCAPSULATION_TYPE_EN, reg_val);
	if (reg_val) {
		reg_val =
		    qed_rd(p_hwfn, p_ptt, PRS_REG_OUTPUT_FORMAT_4_0_BB_K2);

		/* Update output  only if tunnel blocks not included. */
		if (reg_val == (u32)PRS_ETH_OUTPUT_FORMAT)
			qed_wr(p_hwfn, p_ptt, PRS_REG_OUTPUT_FORMAT_4_0_BB_K2,
			       (u32)PRS_ETH_TUNN_OUTPUT_FORMAT);
	}

	/* Update NIG register */
	qed_wr(p_hwfn, p_ptt, NIG_REG_NGE_ETH_ENABLE,
	       eth_geneve_enable ? 1 : 0);
	qed_wr(p_hwfn, p_ptt, NIG_REG_NGE_IP_ENABLE, ip_geneve_enable ? 1 : 0);

	/* EDPM with geneve tunnel not supported in BB */
	if (QED_IS_BB_B0(p_hwfn->cdev))
		return;

	/* Update DORQ registers */
	qed_wr(p_hwfn,
	       p_ptt,
	       DORQ_REG_L2_EDPM_TUNNEL_NGE_ETH_EN_K2_E5,
	       eth_geneve_enable ? 1 : 0);
	qed_wr(p_hwfn,
	       p_ptt,
	       DORQ_REG_L2_EDPM_TUNNEL_NGE_IP_EN_K2_E5,
	       ip_geneve_enable ? 1 : 0);
}

#define PRS_ETH_VXLAN_NO_L2_ENABLE_OFFSET   4
#define PRS_ETH_VXLAN_NO_L2_OUTPUT_FORMAT      -927094512

void qed_set_vxlan_no_l2_enable(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt, bool enable)
{
	u32 reg_val, cfg_mask;

	/* read PRS config register */
	reg_val = qed_rd(p_hwfn, p_ptt, PRS_REG_MSG_INFO);

	/* set VXLAN_NO_L2_ENABLE mask */
	cfg_mask = BIT(PRS_ETH_VXLAN_NO_L2_ENABLE_OFFSET);

	if (enable) {
		/* set VXLAN_NO_L2_ENABLE flag */
		reg_val |= cfg_mask;

		/* update PRS FIC  register */
		qed_wr(p_hwfn,
		       p_ptt,
		       PRS_REG_OUTPUT_FORMAT_4_0_BB_K2,
		       (u32)PRS_ETH_VXLAN_NO_L2_OUTPUT_FORMAT);
	} else {
		/* clear VXLAN_NO_L2_ENABLE flag */
		reg_val &= ~cfg_mask;
	}

	/* write PRS config register */
	qed_wr(p_hwfn, p_ptt, PRS_REG_MSG_INFO, reg_val);
}

#define T_ETH_PACKET_ACTION_GFT_EVENTID  23
#define PARSER_ETH_CONN_GFT_ACTION_CM_HDR  272
#define T_ETH_PACKET_MATCH_RFS_EVENTID 25
#define PARSER_ETH_CONN_CM_HDR 0
#define CAM_LINE_SIZE sizeof(u32)
#define RAM_LINE_SIZE sizeof(u64)
#define REG_SIZE sizeof(u32)

void qed_gft_disable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, u16 pf_id)
{
	/* Disable gft search for PF */
	qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_GFT, 0);

	/* Clean ram & cam for next gft session */

	/* Zero camline */
	qed_wr(p_hwfn, p_ptt, PRS_REG_GFT_CAM + CAM_LINE_SIZE * pf_id, 0);

	/* Zero ramline */
	qed_wr(p_hwfn,
	       p_ptt, PRS_REG_GFT_PROFILE_MASK_RAM + RAM_LINE_SIZE * pf_id, 0);
	qed_wr(p_hwfn,
	       p_ptt,
	       PRS_REG_GFT_PROFILE_MASK_RAM + RAM_LINE_SIZE * pf_id + REG_SIZE,
	       0);
}

void qed_set_gft_event_id_cm_hdr(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 rfs_cm_hdr_event_id;

	/* Set RFS event ID to be awakened i Tstorm By Prs */
	rfs_cm_hdr_event_id = qed_rd(p_hwfn, p_ptt, PRS_REG_CM_HDR_GFT);
	rfs_cm_hdr_event_id |= T_ETH_PACKET_ACTION_GFT_EVENTID <<
			       PRS_REG_CM_HDR_GFT_EVENT_ID_SHIFT;
	rfs_cm_hdr_event_id |= PARSER_ETH_CONN_GFT_ACTION_CM_HDR <<
			       PRS_REG_CM_HDR_GFT_CM_HDR_SHIFT;
	qed_wr(p_hwfn, p_ptt, PRS_REG_CM_HDR_GFT, rfs_cm_hdr_event_id);
}

void qed_gft_config(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    u16 pf_id,
		    bool tcp,
		    bool udp,
		    bool ipv4, bool ipv6, enum gft_profile_type profile_type)
{
	u32 reg_val, cam_line, ram_line_lo, ram_line_hi, search_non_ip_as_gft;

	if (!ipv6 && !ipv4)
		DP_NOTICE(p_hwfn,
			  "gft_config: must accept at least on of - ipv4 or ipv6'\n");
	if (!tcp && !udp)
		DP_NOTICE(p_hwfn,
			  "gft_config: must accept at least on of - udp or tcp\n");
	if (profile_type >= MAX_GFT_PROFILE_TYPE)
		DP_NOTICE(p_hwfn, "gft_config: unsupported gft_profile_type\n");

	/* Set RFS event ID to be awakened i Tstorm By Prs */
	reg_val = T_ETH_PACKET_MATCH_RFS_EVENTID <<
		  PRS_REG_CM_HDR_GFT_EVENT_ID_SHIFT;
	reg_val |= PARSER_ETH_CONN_CM_HDR << PRS_REG_CM_HDR_GFT_CM_HDR_SHIFT;
	qed_wr(p_hwfn, p_ptt, PRS_REG_CM_HDR_GFT, reg_val);

	/* Do not load context only cid in PRS on match. */
	qed_wr(p_hwfn, p_ptt, PRS_REG_LOAD_L2_FILTER, 0);

	/* Do not use tenant ID exist bit for gft search */
	qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TENANT_ID, 0);

	/* Set Cam */
	cam_line = 0;
	SET_FIELD(cam_line, GFT_CAM_LINE_MAPPED_VALID, 1);

	/* Filters are per PF!! */
	SET_FIELD(cam_line,
		  GFT_CAM_LINE_MAPPED_PF_ID_MASK,
		  GFT_CAM_LINE_MAPPED_PF_ID_MASK_MASK);
	SET_FIELD(cam_line, GFT_CAM_LINE_MAPPED_PF_ID, pf_id);

	if (!(tcp && udp)) {
		SET_FIELD(cam_line,
			  GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE_MASK,
			  GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE_MASK_MASK);
		if (tcp)
			SET_FIELD(cam_line,
				  GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE,
				  GFT_PROFILE_TCP_PROTOCOL);
		else
			SET_FIELD(cam_line,
				  GFT_CAM_LINE_MAPPED_UPPER_PROTOCOL_TYPE,
				  GFT_PROFILE_UDP_PROTOCOL);
	}

	if (!(ipv4 && ipv6)) {
		SET_FIELD(cam_line, GFT_CAM_LINE_MAPPED_IP_VERSION_MASK, 1);
		if (ipv4)
			SET_FIELD(cam_line,
				  GFT_CAM_LINE_MAPPED_IP_VERSION,
				  GFT_PROFILE_IPV4);
		else
			SET_FIELD(cam_line,
				  GFT_CAM_LINE_MAPPED_IP_VERSION,
				  GFT_PROFILE_IPV6);
	}

	/* Write characteristics to cam */
	qed_wr(p_hwfn, p_ptt, PRS_REG_GFT_CAM + CAM_LINE_SIZE * pf_id,
	       cam_line);
	cam_line =
	    qed_rd(p_hwfn, p_ptt, PRS_REG_GFT_CAM + CAM_LINE_SIZE * pf_id);

	/* Write line to RAM - compare to filter 4 tuple */
	ram_line_lo = 0;
	ram_line_hi = 0;

	/* Search no IP as GFT */
	search_non_ip_as_gft = 0;

	/* Tunnel type */
	SET_FIELD(ram_line_lo, GFT_RAM_LINE_TUNNEL_DST_PORT, 1);
	SET_FIELD(ram_line_lo, GFT_RAM_LINE_TUNNEL_OVER_IP_PROTOCOL, 1);

	if (profile_type == GFT_PROFILE_TYPE_4_TUPLE) {
		SET_FIELD(ram_line_hi, GFT_RAM_LINE_DST_IP, 1);
		SET_FIELD(ram_line_hi, GFT_RAM_LINE_SRC_IP, 1);
		SET_FIELD(ram_line_hi, GFT_RAM_LINE_OVER_IP_PROTOCOL, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_ETHERTYPE, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_SRC_PORT, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_DST_PORT, 1);
	} else if (profile_type == GFT_PROFILE_TYPE_L4_DST_PORT) {
		SET_FIELD(ram_line_hi, GFT_RAM_LINE_OVER_IP_PROTOCOL, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_ETHERTYPE, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_DST_PORT, 1);
	} else if (profile_type == GFT_PROFILE_TYPE_IP_DST_ADDR) {
		SET_FIELD(ram_line_hi, GFT_RAM_LINE_DST_IP, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_ETHERTYPE, 1);
	} else if (profile_type == GFT_PROFILE_TYPE_IP_SRC_ADDR) {
		SET_FIELD(ram_line_hi, GFT_RAM_LINE_SRC_IP, 1);
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_ETHERTYPE, 1);
	} else if (profile_type == GFT_PROFILE_TYPE_TUNNEL_TYPE) {
		SET_FIELD(ram_line_lo, GFT_RAM_LINE_TUNNEL_ETHERTYPE, 1);

		/* Allow tunneled traffic without inner IP */
		search_non_ip_as_gft = 1;
	}

	qed_wr(p_hwfn,
	       p_ptt, PRS_REG_SEARCH_NON_IP_AS_GFT, search_non_ip_as_gft);
	qed_wr(p_hwfn,
	       p_ptt,
	       PRS_REG_GFT_PROFILE_MASK_RAM + RAM_LINE_SIZE * pf_id,
	       ram_line_lo);
	qed_wr(p_hwfn,
	       p_ptt,
	       PRS_REG_GFT_PROFILE_MASK_RAM + RAM_LINE_SIZE * pf_id + REG_SIZE,
	       ram_line_hi);

	/* Set default profile so that no filter match will happen */
	qed_wr(p_hwfn,
	       p_ptt,
	       PRS_REG_GFT_PROFILE_MASK_RAM + RAM_LINE_SIZE *
	       PRS_GFT_CAM_LINES_NO_MATCH, 0xffffffff);
	qed_wr(p_hwfn,
	       p_ptt,
	       PRS_REG_GFT_PROFILE_MASK_RAM + RAM_LINE_SIZE *
	       PRS_GFT_CAM_LINES_NO_MATCH + REG_SIZE, 0x3ff);

	/* Enable gft search */
	qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_GFT, 1);
}

DECLARE_CRC8_TABLE(cdu_crc8_table);

/* Calculate and return CDU validation byte per connection type/region/cid */
static u8 qed_calc_cdu_validation_byte(u8 conn_type, u8 region, u32 cid)
{
	const u8 validation_cfg = CDU_VALIDATION_DEFAULT_CFG;
	u8 crc, validation_byte = 0;
	static u8 crc8_table_valid; /* automatically initialized to 0 */
	u32 validation_string = 0;
	u32 data_to_crc;

	if (!crc8_table_valid) {
		crc8_populate_msb(cdu_crc8_table, 0x07);
		crc8_table_valid = 1;
	}

	/* The CRC is calculated on the String-to-compress:
	 * [31:8]  = {CID[31:20],CID[11:0]}
	 * [7:4]   = Region
	 * [3:0]   = Type
	 */
	if ((validation_cfg >> CDU_CONTEXT_VALIDATION_CFG_USE_CID) & 1)
		validation_string |= (cid & 0xFFF00000) | ((cid & 0xFFF) << 8);

	if ((validation_cfg >> CDU_CONTEXT_VALIDATION_CFG_USE_REGION) & 1)
		validation_string |= ((region & 0xF) << 4);

	if ((validation_cfg >> CDU_CONTEXT_VALIDATION_CFG_USE_TYPE) & 1)
		validation_string |= (conn_type & 0xF);

	/* Convert to big-endian and calculate CRC8 */
	data_to_crc = be32_to_cpu(validation_string);

	crc = crc8(cdu_crc8_table,
		   (u8 *)&data_to_crc, sizeof(data_to_crc), CRC8_INIT_VALUE);

	/* The validation byte [7:0] is composed:
	 * for type A validation
	 * [7]          = active configuration bit
	 * [6:0]        = crc[6:0]
	 *
	 * for type B validation
	 * [7]          = active configuration bit
	 * [6:3]        = connection_type[3:0]
	 * [2:0]        = crc[2:0]
	 */
	validation_byte |=
	    ((validation_cfg >>
	      CDU_CONTEXT_VALIDATION_CFG_USE_ACTIVE) & 1) << 7;

	if ((validation_cfg >>
	     CDU_CONTEXT_VALIDATION_CFG_VALIDATION_TYPE_SHIFT) & 1)
		validation_byte |= ((conn_type & 0xF) << 3) | (crc & 0x7);
	else
		validation_byte |= crc & 0x7F;

	return validation_byte;
}

/* Calcualte and set validation bytes for session context */
void qed_calc_session_ctx_validation(void *p_ctx_mem,
				     u16 ctx_size, u8 ctx_type, u32 cid)
{
	u8 *x_val_ptr, *t_val_ptr, *u_val_ptr, *p_ctx;

	p_ctx = (u8 * const)p_ctx_mem;
	x_val_ptr = &p_ctx[con_region_offsets[0][ctx_type]];
	t_val_ptr = &p_ctx[con_region_offsets[1][ctx_type]];
	u_val_ptr = &p_ctx[con_region_offsets[2][ctx_type]];

	memset(p_ctx, 0, ctx_size);

	*x_val_ptr = qed_calc_cdu_validation_byte(ctx_type, 3, cid);
	*t_val_ptr = qed_calc_cdu_validation_byte(ctx_type, 4, cid);
	*u_val_ptr = qed_calc_cdu_validation_byte(ctx_type, 5, cid);
}

/* Calcualte and set validation bytes for task context */
void qed_calc_task_ctx_validation(void *p_ctx_mem,
				  u16 ctx_size, u8 ctx_type, u32 tid)
{
	u8 *p_ctx, *region1_val_ptr;

	p_ctx = (u8 * const)p_ctx_mem;
	region1_val_ptr = &p_ctx[task_region_offsets[0][ctx_type]];

	memset(p_ctx, 0, ctx_size);

	*region1_val_ptr = qed_calc_cdu_validation_byte(ctx_type, 1, tid);
}

/* Memset session context to 0 while preserving validation bytes */
void qed_memset_session_ctx(void *p_ctx_mem, u32 ctx_size, u8 ctx_type)
{
	u8 *x_val_ptr, *t_val_ptr, *u_val_ptr, *p_ctx;
	u8 x_val, t_val, u_val;

	p_ctx = (u8 * const)p_ctx_mem;
	x_val_ptr = &p_ctx[con_region_offsets[0][ctx_type]];
	t_val_ptr = &p_ctx[con_region_offsets[1][ctx_type]];
	u_val_ptr = &p_ctx[con_region_offsets[2][ctx_type]];

	x_val = *x_val_ptr;
	t_val = *t_val_ptr;
	u_val = *u_val_ptr;

	memset(p_ctx, 0, ctx_size);

	*x_val_ptr = x_val;
	*t_val_ptr = t_val;
	*u_val_ptr = u_val;
}

/* Memset task context to 0 while preserving validation bytes */
void qed_memset_task_ctx(void *p_ctx_mem, u32 ctx_size, u8 ctx_type)
{
	u8 *p_ctx, *region1_val_ptr;
	u8 region1_val;

	p_ctx = (u8 * const)p_ctx_mem;
	region1_val_ptr = &p_ctx[task_region_offsets[0][ctx_type]];

	region1_val = *region1_val_ptr;

	memset(p_ctx, 0, ctx_size);

	*region1_val_ptr = region1_val;
}

/* Enable and configure context validation */
void qed_enable_context_validation(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	u32 ctx_validation;

	/* Enable validation for connection region 3: CCFC_CTX_VALID0[31:24] */
	ctx_validation = CDU_VALIDATION_DEFAULT_CFG << 24;
	qed_wr(p_hwfn, p_ptt, CDU_REG_CCFC_CTX_VALID0, ctx_validation);

	/* Enable validation for connection region 5: CCFC_CTX_VALID1[15:8] */
	ctx_validation = CDU_VALIDATION_DEFAULT_CFG << 8;
	qed_wr(p_hwfn, p_ptt, CDU_REG_CCFC_CTX_VALID1, ctx_validation);

	/* Enable validation for connection region 1: TCFC_CTX_VALID0[15:8] */
	ctx_validation = CDU_VALIDATION_DEFAULT_CFG << 8;
	qed_wr(p_hwfn, p_ptt, CDU_REG_TCFC_CTX_VALID0, ctx_validation);
}

static u32 qed_get_rdma_assert_ram_addr(struct qed_hwfn *p_hwfn, u8 storm_id)
{
	switch (storm_id) {
	case 0:
		return TSEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM +
		    TSTORM_RDMA_ASSERT_LEVEL_OFFSET(p_hwfn->rel_pf_id);
	case 1:
		return MSEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM +
		    MSTORM_RDMA_ASSERT_LEVEL_OFFSET(p_hwfn->rel_pf_id);
	case 2:
		return USEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM +
		    USTORM_RDMA_ASSERT_LEVEL_OFFSET(p_hwfn->rel_pf_id);
	case 3:
		return XSEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM +
		    XSTORM_RDMA_ASSERT_LEVEL_OFFSET(p_hwfn->rel_pf_id);
	case 4:
		return YSEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM +
		    YSTORM_RDMA_ASSERT_LEVEL_OFFSET(p_hwfn->rel_pf_id);
	case 5:
		return PSEM_REG_FAST_MEMORY + SEM_FAST_REG_INT_RAM +
		    PSTORM_RDMA_ASSERT_LEVEL_OFFSET(p_hwfn->rel_pf_id);

	default:
		return 0;
	}
}

void qed_set_rdma_error_level(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      u8 assert_level[NUM_STORMS])
{
	u8 storm_id;

	for (storm_id = 0; storm_id < NUM_STORMS; storm_id++) {
		u32 ram_addr = qed_get_rdma_assert_ram_addr(p_hwfn, storm_id);

		qed_wr(p_hwfn, p_ptt, ram_addr, assert_level[storm_id]);
	}
}
