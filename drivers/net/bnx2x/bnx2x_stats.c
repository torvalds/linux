/* bnx2x_stats.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2011 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */
#include "bnx2x_cmn.h"
#include "bnx2x_stats.h"

/* Statistics */

/****************************************************************************
* Macros
****************************************************************************/

/* sum[hi:lo] += add[hi:lo] */
#define ADD_64(s_hi, a_hi, s_lo, a_lo) \
	do { \
		s_lo += a_lo; \
		s_hi += a_hi + ((s_lo < a_lo) ? 1 : 0); \
	} while (0)

/* difference = minuend - subtrahend */
#define DIFF_64(d_hi, m_hi, s_hi, d_lo, m_lo, s_lo) \
	do { \
		if (m_lo < s_lo) { \
			/* underflow */ \
			d_hi = m_hi - s_hi; \
			if (d_hi > 0) { \
				/* we can 'loan' 1 */ \
				d_hi--; \
				d_lo = m_lo + (UINT_MAX - s_lo) + 1; \
			} else { \
				/* m_hi <= s_hi */ \
				d_hi = 0; \
				d_lo = 0; \
			} \
		} else { \
			/* m_lo >= s_lo */ \
			if (m_hi < s_hi) { \
				d_hi = 0; \
				d_lo = 0; \
			} else { \
				/* m_hi >= s_hi */ \
				d_hi = m_hi - s_hi; \
				d_lo = m_lo - s_lo; \
			} \
		} \
	} while (0)

#define UPDATE_STAT64(s, t) \
	do { \
		DIFF_64(diff.hi, new->s##_hi, pstats->mac_stx[0].t##_hi, \
			diff.lo, new->s##_lo, pstats->mac_stx[0].t##_lo); \
		pstats->mac_stx[0].t##_hi = new->s##_hi; \
		pstats->mac_stx[0].t##_lo = new->s##_lo; \
		ADD_64(pstats->mac_stx[1].t##_hi, diff.hi, \
		       pstats->mac_stx[1].t##_lo, diff.lo); \
	} while (0)

#define UPDATE_STAT64_NIG(s, t) \
	do { \
		DIFF_64(diff.hi, new->s##_hi, old->s##_hi, \
			diff.lo, new->s##_lo, old->s##_lo); \
		ADD_64(estats->t##_hi, diff.hi, \
		       estats->t##_lo, diff.lo); \
	} while (0)

/* sum[hi:lo] += add */
#define ADD_EXTEND_64(s_hi, s_lo, a) \
	do { \
		s_lo += a; \
		s_hi += (s_lo < a) ? 1 : 0; \
	} while (0)

#define UPDATE_EXTEND_STAT(s) \
	do { \
		ADD_EXTEND_64(pstats->mac_stx[1].s##_hi, \
			      pstats->mac_stx[1].s##_lo, \
			      new->s); \
	} while (0)

#define UPDATE_EXTEND_TSTAT(s, t) \
	do { \
		diff = le32_to_cpu(tclient->s) - le32_to_cpu(old_tclient->s); \
		old_tclient->s = tclient->s; \
		ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

#define UPDATE_EXTEND_USTAT(s, t) \
	do { \
		diff = le32_to_cpu(uclient->s) - le32_to_cpu(old_uclient->s); \
		old_uclient->s = uclient->s; \
		ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

#define UPDATE_EXTEND_XSTAT(s, t) \
	do { \
		diff = le32_to_cpu(xclient->s) - le32_to_cpu(old_xclient->s); \
		old_xclient->s = xclient->s; \
		ADD_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

/* minuend -= subtrahend */
#define SUB_64(m_hi, s_hi, m_lo, s_lo) \
	do { \
		DIFF_64(m_hi, m_hi, s_hi, m_lo, m_lo, s_lo); \
	} while (0)

/* minuend[hi:lo] -= subtrahend */
#define SUB_EXTEND_64(m_hi, m_lo, s) \
	do { \
		SUB_64(m_hi, 0, m_lo, s); \
	} while (0)

#define SUB_EXTEND_USTAT(s, t) \
	do { \
		diff = le32_to_cpu(uclient->s) - le32_to_cpu(old_uclient->s); \
		SUB_EXTEND_64(qstats->t##_hi, qstats->t##_lo, diff); \
	} while (0)

/*
 * General service functions
 */

static inline long bnx2x_hilo(u32 *hiref)
{
	u32 lo = *(hiref + 1);
#if (BITS_PER_LONG == 64)
	u32 hi = *hiref;

	return HILO_U64(hi, lo);
#else
	return lo;
#endif
}

/*
 * Init service functions
 */


static void bnx2x_storm_stats_post(struct bnx2x *bp)
{
	if (!bp->stats_pending) {
		struct common_query_ramrod_data ramrod_data = {0};
		int i, rc;

		spin_lock_bh(&bp->stats_lock);

		if (bp->stats_pending) {
			spin_unlock_bh(&bp->stats_lock);
			return;
		}

		ramrod_data.drv_counter = bp->stats_counter++;
		ramrod_data.collect_port = bp->port.pmf ? 1 : 0;
		for_each_eth_queue(bp, i)
			ramrod_data.ctr_id_vector |= (1 << bp->fp[i].cl_id);

		rc = bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_STAT_QUERY, 0,
				   ((u32 *)&ramrod_data)[1],
				   ((u32 *)&ramrod_data)[0], 1);
		if (rc == 0)
			bp->stats_pending = 1;

		spin_unlock_bh(&bp->stats_lock);
	}
}

static void bnx2x_hw_stats_post(struct bnx2x *bp)
{
	struct dmae_command *dmae = &bp->stats_dmae;
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	*stats_comp = DMAE_COMP_VAL;
	if (CHIP_REV_IS_SLOW(bp))
		return;

	/* loader */
	if (bp->executer_idx) {
		int loader_idx = PMF_DMAE_C(bp);
		u32 opcode =  bnx2x_dmae_opcode(bp, DMAE_SRC_PCI, DMAE_DST_GRC,
						 true, DMAE_COMP_GRC);
		opcode = bnx2x_dmae_opcode_clr_src_reset(opcode);

		memset(dmae, 0, sizeof(struct dmae_command));
		dmae->opcode = opcode;
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, dmae[0]));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, dmae[0]));
		dmae->dst_addr_lo = (DMAE_REG_CMD_MEM +
				     sizeof(struct dmae_command) *
				     (loader_idx + 1)) >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = sizeof(struct dmae_command) >> 2;
		if (CHIP_IS_E1(bp))
			dmae->len--;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx + 1] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		*stats_comp = 0;
		bnx2x_post_dmae(bp, dmae, loader_idx);

	} else if (bp->func_stx) {
		*stats_comp = 0;
		bnx2x_post_dmae(bp, dmae, INIT_DMAE_C(bp));
	}
}

static int bnx2x_stats_comp(struct bnx2x *bp)
{
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);
	int cnt = 10;

	might_sleep();
	while (*stats_comp != DMAE_COMP_VAL) {
		if (!cnt) {
			BNX2X_ERR("timeout waiting for stats finished\n");
			break;
		}
		cnt--;
		msleep(1);
	}
	return 1;
}

/*
 * Statistics service functions
 */

static void bnx2x_stats_pmf_update(struct bnx2x *bp)
{
	struct dmae_command *dmae;
	u32 opcode;
	int loader_idx = PMF_DMAE_C(bp);
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* sanity */
	if (!IS_MF(bp) || !bp->port.pmf || !bp->port.port_stx) {
		BNX2X_ERR("BUG!\n");
		return;
	}

	bp->executer_idx = 0;

	opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_GRC, DMAE_DST_PCI, false, 0);

	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = bnx2x_dmae_opcode_add_comp(opcode, DMAE_COMP_GRC);
	dmae->src_addr_lo = bp->port.port_stx >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, port_stats));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, port_stats));
	dmae->len = DMAE_LEN32_RD_MAX;
	dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
	dmae->comp_addr_hi = 0;
	dmae->comp_val = 1;

	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = bnx2x_dmae_opcode_add_comp(opcode, DMAE_COMP_PCI);
	dmae->src_addr_lo = (bp->port.port_stx >> 2) + DMAE_LEN32_RD_MAX;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, port_stats) +
				   DMAE_LEN32_RD_MAX * 4);
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, port_stats) +
				   DMAE_LEN32_RD_MAX * 4);
	dmae->len = (sizeof(struct host_port_stats) >> 2) - DMAE_LEN32_RD_MAX;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_val = DMAE_COMP_VAL;

	*stats_comp = 0;
	bnx2x_hw_stats_post(bp);
	bnx2x_stats_comp(bp);
}

static void bnx2x_port_stats_init(struct bnx2x *bp)
{
	struct dmae_command *dmae;
	int port = BP_PORT(bp);
	u32 opcode;
	int loader_idx = PMF_DMAE_C(bp);
	u32 mac_addr;
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* sanity */
	if (!bp->link_vars.link_up || !bp->port.pmf) {
		BNX2X_ERR("BUG!\n");
		return;
	}

	bp->executer_idx = 0;

	/* MCP */
	opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_PCI, DMAE_DST_GRC,
				    true, DMAE_COMP_GRC);

	if (bp->port.port_stx) {

		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, port_stats));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, port_stats));
		dmae->dst_addr_lo = bp->port.port_stx >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = sizeof(struct host_port_stats) >> 2;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;
	}

	if (bp->func_stx) {

		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, func_stats));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, func_stats));
		dmae->dst_addr_lo = bp->func_stx >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = sizeof(struct host_func_stats) >> 2;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;
	}

	/* MAC */
	opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_GRC, DMAE_DST_PCI,
				   true, DMAE_COMP_GRC);

	if (bp->link_vars.mac_type == MAC_TYPE_BMAC) {

		mac_addr = (port ? NIG_REG_INGRESS_BMAC1_MEM :
				   NIG_REG_INGRESS_BMAC0_MEM);

		/* BIGMAC_REGISTER_TX_STAT_GTPKT ..
		   BIGMAC_REGISTER_TX_STAT_GTBYT */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		if (CHIP_IS_E1x(bp)) {
			dmae->src_addr_lo = (mac_addr +
				     BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2;
			dmae->len = (8 + BIGMAC_REGISTER_TX_STAT_GTBYT -
				     BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2;
		} else {
			dmae->src_addr_lo = (mac_addr +
				     BIGMAC2_REGISTER_TX_STAT_GTPOK) >> 2;
			dmae->len = (8 + BIGMAC2_REGISTER_TX_STAT_GTBYT -
				     BIGMAC2_REGISTER_TX_STAT_GTPOK) >> 2;
		}

		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats));
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* BIGMAC_REGISTER_RX_STAT_GR64 ..
		   BIGMAC_REGISTER_RX_STAT_GRIPJ */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_hi = 0;
		if (CHIP_IS_E1x(bp)) {
			dmae->src_addr_lo = (mac_addr +
					     BIGMAC_REGISTER_RX_STAT_GR64) >> 2;
			dmae->dst_addr_lo =
				U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
				offsetof(struct bmac1_stats, rx_stat_gr64_lo));
			dmae->dst_addr_hi =
				U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
				offsetof(struct bmac1_stats, rx_stat_gr64_lo));
			dmae->len = (8 + BIGMAC_REGISTER_RX_STAT_GRIPJ -
				     BIGMAC_REGISTER_RX_STAT_GR64) >> 2;
		} else {
			dmae->src_addr_lo =
				(mac_addr + BIGMAC2_REGISTER_RX_STAT_GR64) >> 2;
			dmae->dst_addr_lo =
				U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
				offsetof(struct bmac2_stats, rx_stat_gr64_lo));
			dmae->dst_addr_hi =
				U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
				offsetof(struct bmac2_stats, rx_stat_gr64_lo));
			dmae->len = (8 + BIGMAC2_REGISTER_RX_STAT_GRIPJ -
				     BIGMAC2_REGISTER_RX_STAT_GR64) >> 2;
		}

		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

	} else if (bp->link_vars.mac_type == MAC_TYPE_EMAC) {

		mac_addr = (port ? GRCBASE_EMAC1 : GRCBASE_EMAC0);

		/* EMAC_REG_EMAC_RX_STAT_AC (EMAC_REG_EMAC_RX_STAT_AC_COUNT)*/
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     EMAC_REG_EMAC_RX_STAT_AC) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats));
		dmae->len = EMAC_REG_EMAC_RX_STAT_AC_COUNT;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* EMAC_REG_EMAC_RX_STAT_AC_28 */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     EMAC_REG_EMAC_RX_STAT_AC_28) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
		     offsetof(struct emac_stats, rx_stat_falsecarriererrors));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
		     offsetof(struct emac_stats, rx_stat_falsecarriererrors));
		dmae->len = 1;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* EMAC_REG_EMAC_TX_STAT_AC (EMAC_REG_EMAC_TX_STAT_AC_COUNT)*/
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = (mac_addr +
				     EMAC_REG_EMAC_TX_STAT_AC) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats) +
			offsetof(struct emac_stats, tx_stat_ifhcoutoctets));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats) +
			offsetof(struct emac_stats, tx_stat_ifhcoutoctets));
		dmae->len = EMAC_REG_EMAC_TX_STAT_AC_COUNT;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;
	}

	/* NIG */
	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = opcode;
	dmae->src_addr_lo = (port ? NIG_REG_STAT1_BRB_DISCARD :
				    NIG_REG_STAT0_BRB_DISCARD) >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig_stats));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig_stats));
	dmae->len = (sizeof(struct nig_stats) - 4*sizeof(u32)) >> 2;
	dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
	dmae->comp_addr_hi = 0;
	dmae->comp_val = 1;

	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = opcode;
	dmae->src_addr_lo = (port ? NIG_REG_STAT1_EGRESS_MAC_PKT0 :
				    NIG_REG_STAT0_EGRESS_MAC_PKT0) >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig_stats) +
			offsetof(struct nig_stats, egress_mac_pkt0_lo));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig_stats) +
			offsetof(struct nig_stats, egress_mac_pkt0_lo));
	dmae->len = (2*sizeof(u32)) >> 2;
	dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
	dmae->comp_addr_hi = 0;
	dmae->comp_val = 1;

	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_GRC, DMAE_DST_PCI,
					 true, DMAE_COMP_PCI);
	dmae->src_addr_lo = (port ? NIG_REG_STAT1_EGRESS_MAC_PKT1 :
				    NIG_REG_STAT0_EGRESS_MAC_PKT1) >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig_stats) +
			offsetof(struct nig_stats, egress_mac_pkt1_lo));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig_stats) +
			offsetof(struct nig_stats, egress_mac_pkt1_lo));
	dmae->len = (2*sizeof(u32)) >> 2;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_val = DMAE_COMP_VAL;

	*stats_comp = 0;
}

static void bnx2x_func_stats_init(struct bnx2x *bp)
{
	struct dmae_command *dmae = &bp->stats_dmae;
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* sanity */
	if (!bp->func_stx) {
		BNX2X_ERR("BUG!\n");
		return;
	}

	bp->executer_idx = 0;
	memset(dmae, 0, sizeof(struct dmae_command));

	dmae->opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_PCI, DMAE_DST_GRC,
					 true, DMAE_COMP_PCI);
	dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, func_stats));
	dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, func_stats));
	dmae->dst_addr_lo = bp->func_stx >> 2;
	dmae->dst_addr_hi = 0;
	dmae->len = sizeof(struct host_func_stats) >> 2;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_val = DMAE_COMP_VAL;

	*stats_comp = 0;
}

static void bnx2x_stats_start(struct bnx2x *bp)
{
	if (bp->port.pmf)
		bnx2x_port_stats_init(bp);

	else if (bp->func_stx)
		bnx2x_func_stats_init(bp);

	bnx2x_hw_stats_post(bp);
	bnx2x_storm_stats_post(bp);
}

static void bnx2x_stats_pmf_start(struct bnx2x *bp)
{
	bnx2x_stats_comp(bp);
	bnx2x_stats_pmf_update(bp);
	bnx2x_stats_start(bp);
}

static void bnx2x_stats_restart(struct bnx2x *bp)
{
	bnx2x_stats_comp(bp);
	bnx2x_stats_start(bp);
}

static void bnx2x_bmac_stats_update(struct bnx2x *bp)
{
	struct host_port_stats *pstats = bnx2x_sp(bp, port_stats);
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	struct {
		u32 lo;
		u32 hi;
	} diff;

	if (CHIP_IS_E1x(bp)) {
		struct bmac1_stats *new = bnx2x_sp(bp, mac_stats.bmac1_stats);

		/* the macros below will use "bmac1_stats" type */
		UPDATE_STAT64(rx_stat_grerb, rx_stat_ifhcinbadoctets);
		UPDATE_STAT64(rx_stat_grfcs, rx_stat_dot3statsfcserrors);
		UPDATE_STAT64(rx_stat_grund, rx_stat_etherstatsundersizepkts);
		UPDATE_STAT64(rx_stat_grovr, rx_stat_dot3statsframestoolong);
		UPDATE_STAT64(rx_stat_grfrg, rx_stat_etherstatsfragments);
		UPDATE_STAT64(rx_stat_grjbr, rx_stat_etherstatsjabbers);
		UPDATE_STAT64(rx_stat_grxcf, rx_stat_maccontrolframesreceived);
		UPDATE_STAT64(rx_stat_grxpf, rx_stat_xoffstateentered);
		UPDATE_STAT64(rx_stat_grxpf, rx_stat_bmac_xpf);
		UPDATE_STAT64(tx_stat_gtxpf, tx_stat_outxoffsent);
		UPDATE_STAT64(tx_stat_gtxpf, tx_stat_flowcontroldone);
		UPDATE_STAT64(tx_stat_gt64, tx_stat_etherstatspkts64octets);
		UPDATE_STAT64(tx_stat_gt127,
				tx_stat_etherstatspkts65octetsto127octets);
		UPDATE_STAT64(tx_stat_gt255,
				tx_stat_etherstatspkts128octetsto255octets);
		UPDATE_STAT64(tx_stat_gt511,
				tx_stat_etherstatspkts256octetsto511octets);
		UPDATE_STAT64(tx_stat_gt1023,
				tx_stat_etherstatspkts512octetsto1023octets);
		UPDATE_STAT64(tx_stat_gt1518,
				tx_stat_etherstatspkts1024octetsto1522octets);
		UPDATE_STAT64(tx_stat_gt2047, tx_stat_bmac_2047);
		UPDATE_STAT64(tx_stat_gt4095, tx_stat_bmac_4095);
		UPDATE_STAT64(tx_stat_gt9216, tx_stat_bmac_9216);
		UPDATE_STAT64(tx_stat_gt16383, tx_stat_bmac_16383);
		UPDATE_STAT64(tx_stat_gterr,
				tx_stat_dot3statsinternalmactransmiterrors);
		UPDATE_STAT64(tx_stat_gtufl, tx_stat_bmac_ufl);

	} else {
		struct bmac2_stats *new = bnx2x_sp(bp, mac_stats.bmac2_stats);

		/* the macros below will use "bmac2_stats" type */
		UPDATE_STAT64(rx_stat_grerb, rx_stat_ifhcinbadoctets);
		UPDATE_STAT64(rx_stat_grfcs, rx_stat_dot3statsfcserrors);
		UPDATE_STAT64(rx_stat_grund, rx_stat_etherstatsundersizepkts);
		UPDATE_STAT64(rx_stat_grovr, rx_stat_dot3statsframestoolong);
		UPDATE_STAT64(rx_stat_grfrg, rx_stat_etherstatsfragments);
		UPDATE_STAT64(rx_stat_grjbr, rx_stat_etherstatsjabbers);
		UPDATE_STAT64(rx_stat_grxcf, rx_stat_maccontrolframesreceived);
		UPDATE_STAT64(rx_stat_grxpf, rx_stat_xoffstateentered);
		UPDATE_STAT64(rx_stat_grxpf, rx_stat_bmac_xpf);
		UPDATE_STAT64(tx_stat_gtxpf, tx_stat_outxoffsent);
		UPDATE_STAT64(tx_stat_gtxpf, tx_stat_flowcontroldone);
		UPDATE_STAT64(tx_stat_gt64, tx_stat_etherstatspkts64octets);
		UPDATE_STAT64(tx_stat_gt127,
				tx_stat_etherstatspkts65octetsto127octets);
		UPDATE_STAT64(tx_stat_gt255,
				tx_stat_etherstatspkts128octetsto255octets);
		UPDATE_STAT64(tx_stat_gt511,
				tx_stat_etherstatspkts256octetsto511octets);
		UPDATE_STAT64(tx_stat_gt1023,
				tx_stat_etherstatspkts512octetsto1023octets);
		UPDATE_STAT64(tx_stat_gt1518,
				tx_stat_etherstatspkts1024octetsto1522octets);
		UPDATE_STAT64(tx_stat_gt2047, tx_stat_bmac_2047);
		UPDATE_STAT64(tx_stat_gt4095, tx_stat_bmac_4095);
		UPDATE_STAT64(tx_stat_gt9216, tx_stat_bmac_9216);
		UPDATE_STAT64(tx_stat_gt16383, tx_stat_bmac_16383);
		UPDATE_STAT64(tx_stat_gterr,
				tx_stat_dot3statsinternalmactransmiterrors);
		UPDATE_STAT64(tx_stat_gtufl, tx_stat_bmac_ufl);
	}

	estats->pause_frames_received_hi =
				pstats->mac_stx[1].rx_stat_bmac_xpf_hi;
	estats->pause_frames_received_lo =
				pstats->mac_stx[1].rx_stat_bmac_xpf_lo;

	estats->pause_frames_sent_hi =
				pstats->mac_stx[1].tx_stat_outxoffsent_hi;
	estats->pause_frames_sent_lo =
				pstats->mac_stx[1].tx_stat_outxoffsent_lo;
}

static void bnx2x_emac_stats_update(struct bnx2x *bp)
{
	struct emac_stats *new = bnx2x_sp(bp, mac_stats.emac_stats);
	struct host_port_stats *pstats = bnx2x_sp(bp, port_stats);
	struct bnx2x_eth_stats *estats = &bp->eth_stats;

	UPDATE_EXTEND_STAT(rx_stat_ifhcinbadoctets);
	UPDATE_EXTEND_STAT(tx_stat_ifhcoutbadoctets);
	UPDATE_EXTEND_STAT(rx_stat_dot3statsfcserrors);
	UPDATE_EXTEND_STAT(rx_stat_dot3statsalignmenterrors);
	UPDATE_EXTEND_STAT(rx_stat_dot3statscarriersenseerrors);
	UPDATE_EXTEND_STAT(rx_stat_falsecarriererrors);
	UPDATE_EXTEND_STAT(rx_stat_etherstatsundersizepkts);
	UPDATE_EXTEND_STAT(rx_stat_dot3statsframestoolong);
	UPDATE_EXTEND_STAT(rx_stat_etherstatsfragments);
	UPDATE_EXTEND_STAT(rx_stat_etherstatsjabbers);
	UPDATE_EXTEND_STAT(rx_stat_maccontrolframesreceived);
	UPDATE_EXTEND_STAT(rx_stat_xoffstateentered);
	UPDATE_EXTEND_STAT(rx_stat_xonpauseframesreceived);
	UPDATE_EXTEND_STAT(rx_stat_xoffpauseframesreceived);
	UPDATE_EXTEND_STAT(tx_stat_outxonsent);
	UPDATE_EXTEND_STAT(tx_stat_outxoffsent);
	UPDATE_EXTEND_STAT(tx_stat_flowcontroldone);
	UPDATE_EXTEND_STAT(tx_stat_etherstatscollisions);
	UPDATE_EXTEND_STAT(tx_stat_dot3statssinglecollisionframes);
	UPDATE_EXTEND_STAT(tx_stat_dot3statsmultiplecollisionframes);
	UPDATE_EXTEND_STAT(tx_stat_dot3statsdeferredtransmissions);
	UPDATE_EXTEND_STAT(tx_stat_dot3statsexcessivecollisions);
	UPDATE_EXTEND_STAT(tx_stat_dot3statslatecollisions);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspkts64octets);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspkts65octetsto127octets);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspkts128octetsto255octets);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspkts256octetsto511octets);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspkts512octetsto1023octets);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspkts1024octetsto1522octets);
	UPDATE_EXTEND_STAT(tx_stat_etherstatspktsover1522octets);
	UPDATE_EXTEND_STAT(tx_stat_dot3statsinternalmactransmiterrors);

	estats->pause_frames_received_hi =
			pstats->mac_stx[1].rx_stat_xonpauseframesreceived_hi;
	estats->pause_frames_received_lo =
			pstats->mac_stx[1].rx_stat_xonpauseframesreceived_lo;
	ADD_64(estats->pause_frames_received_hi,
	       pstats->mac_stx[1].rx_stat_xoffpauseframesreceived_hi,
	       estats->pause_frames_received_lo,
	       pstats->mac_stx[1].rx_stat_xoffpauseframesreceived_lo);

	estats->pause_frames_sent_hi =
			pstats->mac_stx[1].tx_stat_outxonsent_hi;
	estats->pause_frames_sent_lo =
			pstats->mac_stx[1].tx_stat_outxonsent_lo;
	ADD_64(estats->pause_frames_sent_hi,
	       pstats->mac_stx[1].tx_stat_outxoffsent_hi,
	       estats->pause_frames_sent_lo,
	       pstats->mac_stx[1].tx_stat_outxoffsent_lo);
}

static int bnx2x_hw_stats_update(struct bnx2x *bp)
{
	struct nig_stats *new = bnx2x_sp(bp, nig_stats);
	struct nig_stats *old = &(bp->port.old_nig_stats);
	struct host_port_stats *pstats = bnx2x_sp(bp, port_stats);
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	struct {
		u32 lo;
		u32 hi;
	} diff;

	if (bp->link_vars.mac_type == MAC_TYPE_BMAC)
		bnx2x_bmac_stats_update(bp);

	else if (bp->link_vars.mac_type == MAC_TYPE_EMAC)
		bnx2x_emac_stats_update(bp);

	else { /* unreached */
		BNX2X_ERR("stats updated by DMAE but no MAC active\n");
		return -1;
	}

	ADD_EXTEND_64(pstats->brb_drop_hi, pstats->brb_drop_lo,
		      new->brb_discard - old->brb_discard);
	ADD_EXTEND_64(estats->brb_truncate_hi, estats->brb_truncate_lo,
		      new->brb_truncate - old->brb_truncate);

	UPDATE_STAT64_NIG(egress_mac_pkt0,
					etherstatspkts1024octetsto1522octets);
	UPDATE_STAT64_NIG(egress_mac_pkt1, etherstatspktsover1522octets);

	memcpy(old, new, sizeof(struct nig_stats));

	memcpy(&(estats->rx_stat_ifhcinbadoctets_hi), &(pstats->mac_stx[1]),
	       sizeof(struct mac_stx));
	estats->brb_drop_hi = pstats->brb_drop_hi;
	estats->brb_drop_lo = pstats->brb_drop_lo;

	pstats->host_port_stats_start = ++pstats->host_port_stats_end;

	if (!BP_NOMCP(bp)) {
		u32 nig_timer_max =
			SHMEM_RD(bp, port_mb[BP_PORT(bp)].stat_nig_timer);
		if (nig_timer_max != estats->nig_timer_max) {
			estats->nig_timer_max = nig_timer_max;
			BNX2X_ERR("NIG timer max (%u)\n",
				  estats->nig_timer_max);
		}
	}

	return 0;
}

static int bnx2x_storm_stats_update(struct bnx2x *bp)
{
	struct eth_stats_query *stats = bnx2x_sp(bp, fw_stats);
	struct tstorm_per_port_stats *tport =
					&stats->tstorm_common.port_statistics;
	struct host_func_stats *fstats = bnx2x_sp(bp, func_stats);
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	int i;
	u16 cur_stats_counter;

	/* Make sure we use the value of the counter
	 * used for sending the last stats ramrod.
	 */
	spin_lock_bh(&bp->stats_lock);
	cur_stats_counter = bp->stats_counter - 1;
	spin_unlock_bh(&bp->stats_lock);

	memcpy(&(fstats->total_bytes_received_hi),
	       &(bnx2x_sp(bp, func_stats_base)->total_bytes_received_hi),
	       sizeof(struct host_func_stats) - 2*sizeof(u32));
	estats->error_bytes_received_hi = 0;
	estats->error_bytes_received_lo = 0;
	estats->etherstatsoverrsizepkts_hi = 0;
	estats->etherstatsoverrsizepkts_lo = 0;
	estats->no_buff_discard_hi = 0;
	estats->no_buff_discard_lo = 0;

	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		int cl_id = fp->cl_id;
		struct tstorm_per_client_stats *tclient =
				&stats->tstorm_common.client_statistics[cl_id];
		struct tstorm_per_client_stats *old_tclient = &fp->old_tclient;
		struct ustorm_per_client_stats *uclient =
				&stats->ustorm_common.client_statistics[cl_id];
		struct ustorm_per_client_stats *old_uclient = &fp->old_uclient;
		struct xstorm_per_client_stats *xclient =
				&stats->xstorm_common.client_statistics[cl_id];
		struct xstorm_per_client_stats *old_xclient = &fp->old_xclient;
		struct bnx2x_eth_q_stats *qstats = &fp->eth_q_stats;
		u32 diff;

		/* are storm stats valid? */
		if (le16_to_cpu(xclient->stats_counter) != cur_stats_counter) {
			DP(BNX2X_MSG_STATS, "[%d] stats not updated by xstorm"
			   "  xstorm counter (0x%x) != stats_counter (0x%x)\n",
			   i, xclient->stats_counter, cur_stats_counter + 1);
			return -1;
		}
		if (le16_to_cpu(tclient->stats_counter) != cur_stats_counter) {
			DP(BNX2X_MSG_STATS, "[%d] stats not updated by tstorm"
			   "  tstorm counter (0x%x) != stats_counter (0x%x)\n",
			   i, tclient->stats_counter, cur_stats_counter + 1);
			return -2;
		}
		if (le16_to_cpu(uclient->stats_counter) != cur_stats_counter) {
			DP(BNX2X_MSG_STATS, "[%d] stats not updated by ustorm"
			   "  ustorm counter (0x%x) != stats_counter (0x%x)\n",
			   i, uclient->stats_counter, cur_stats_counter + 1);
			return -4;
		}

		qstats->total_bytes_received_hi =
			le32_to_cpu(tclient->rcv_broadcast_bytes.hi);
		qstats->total_bytes_received_lo =
			le32_to_cpu(tclient->rcv_broadcast_bytes.lo);

		ADD_64(qstats->total_bytes_received_hi,
		       le32_to_cpu(tclient->rcv_multicast_bytes.hi),
		       qstats->total_bytes_received_lo,
		       le32_to_cpu(tclient->rcv_multicast_bytes.lo));

		ADD_64(qstats->total_bytes_received_hi,
		       le32_to_cpu(tclient->rcv_unicast_bytes.hi),
		       qstats->total_bytes_received_lo,
		       le32_to_cpu(tclient->rcv_unicast_bytes.lo));

		SUB_64(qstats->total_bytes_received_hi,
		       le32_to_cpu(uclient->bcast_no_buff_bytes.hi),
		       qstats->total_bytes_received_lo,
		       le32_to_cpu(uclient->bcast_no_buff_bytes.lo));

		SUB_64(qstats->total_bytes_received_hi,
		       le32_to_cpu(uclient->mcast_no_buff_bytes.hi),
		       qstats->total_bytes_received_lo,
		       le32_to_cpu(uclient->mcast_no_buff_bytes.lo));

		SUB_64(qstats->total_bytes_received_hi,
		       le32_to_cpu(uclient->ucast_no_buff_bytes.hi),
		       qstats->total_bytes_received_lo,
		       le32_to_cpu(uclient->ucast_no_buff_bytes.lo));

		qstats->valid_bytes_received_hi =
					qstats->total_bytes_received_hi;
		qstats->valid_bytes_received_lo =
					qstats->total_bytes_received_lo;

		qstats->error_bytes_received_hi =
				le32_to_cpu(tclient->rcv_error_bytes.hi);
		qstats->error_bytes_received_lo =
				le32_to_cpu(tclient->rcv_error_bytes.lo);

		ADD_64(qstats->total_bytes_received_hi,
		       qstats->error_bytes_received_hi,
		       qstats->total_bytes_received_lo,
		       qstats->error_bytes_received_lo);

		UPDATE_EXTEND_TSTAT(rcv_unicast_pkts,
					total_unicast_packets_received);
		UPDATE_EXTEND_TSTAT(rcv_multicast_pkts,
					total_multicast_packets_received);
		UPDATE_EXTEND_TSTAT(rcv_broadcast_pkts,
					total_broadcast_packets_received);
		UPDATE_EXTEND_TSTAT(packets_too_big_discard,
					etherstatsoverrsizepkts);
		UPDATE_EXTEND_TSTAT(no_buff_discard, no_buff_discard);

		SUB_EXTEND_USTAT(ucast_no_buff_pkts,
					total_unicast_packets_received);
		SUB_EXTEND_USTAT(mcast_no_buff_pkts,
					total_multicast_packets_received);
		SUB_EXTEND_USTAT(bcast_no_buff_pkts,
					total_broadcast_packets_received);
		UPDATE_EXTEND_USTAT(ucast_no_buff_pkts, no_buff_discard);
		UPDATE_EXTEND_USTAT(mcast_no_buff_pkts, no_buff_discard);
		UPDATE_EXTEND_USTAT(bcast_no_buff_pkts, no_buff_discard);

		qstats->total_bytes_transmitted_hi =
				le32_to_cpu(xclient->unicast_bytes_sent.hi);
		qstats->total_bytes_transmitted_lo =
				le32_to_cpu(xclient->unicast_bytes_sent.lo);

		ADD_64(qstats->total_bytes_transmitted_hi,
		       le32_to_cpu(xclient->multicast_bytes_sent.hi),
		       qstats->total_bytes_transmitted_lo,
		       le32_to_cpu(xclient->multicast_bytes_sent.lo));

		ADD_64(qstats->total_bytes_transmitted_hi,
		       le32_to_cpu(xclient->broadcast_bytes_sent.hi),
		       qstats->total_bytes_transmitted_lo,
		       le32_to_cpu(xclient->broadcast_bytes_sent.lo));

		UPDATE_EXTEND_XSTAT(unicast_pkts_sent,
					total_unicast_packets_transmitted);
		UPDATE_EXTEND_XSTAT(multicast_pkts_sent,
					total_multicast_packets_transmitted);
		UPDATE_EXTEND_XSTAT(broadcast_pkts_sent,
					total_broadcast_packets_transmitted);

		old_tclient->checksum_discard = tclient->checksum_discard;
		old_tclient->ttl0_discard = tclient->ttl0_discard;

		ADD_64(fstats->total_bytes_received_hi,
		       qstats->total_bytes_received_hi,
		       fstats->total_bytes_received_lo,
		       qstats->total_bytes_received_lo);
		ADD_64(fstats->total_bytes_transmitted_hi,
		       qstats->total_bytes_transmitted_hi,
		       fstats->total_bytes_transmitted_lo,
		       qstats->total_bytes_transmitted_lo);
		ADD_64(fstats->total_unicast_packets_received_hi,
		       qstats->total_unicast_packets_received_hi,
		       fstats->total_unicast_packets_received_lo,
		       qstats->total_unicast_packets_received_lo);
		ADD_64(fstats->total_multicast_packets_received_hi,
		       qstats->total_multicast_packets_received_hi,
		       fstats->total_multicast_packets_received_lo,
		       qstats->total_multicast_packets_received_lo);
		ADD_64(fstats->total_broadcast_packets_received_hi,
		       qstats->total_broadcast_packets_received_hi,
		       fstats->total_broadcast_packets_received_lo,
		       qstats->total_broadcast_packets_received_lo);
		ADD_64(fstats->total_unicast_packets_transmitted_hi,
		       qstats->total_unicast_packets_transmitted_hi,
		       fstats->total_unicast_packets_transmitted_lo,
		       qstats->total_unicast_packets_transmitted_lo);
		ADD_64(fstats->total_multicast_packets_transmitted_hi,
		       qstats->total_multicast_packets_transmitted_hi,
		       fstats->total_multicast_packets_transmitted_lo,
		       qstats->total_multicast_packets_transmitted_lo);
		ADD_64(fstats->total_broadcast_packets_transmitted_hi,
		       qstats->total_broadcast_packets_transmitted_hi,
		       fstats->total_broadcast_packets_transmitted_lo,
		       qstats->total_broadcast_packets_transmitted_lo);
		ADD_64(fstats->valid_bytes_received_hi,
		       qstats->valid_bytes_received_hi,
		       fstats->valid_bytes_received_lo,
		       qstats->valid_bytes_received_lo);

		ADD_64(estats->error_bytes_received_hi,
		       qstats->error_bytes_received_hi,
		       estats->error_bytes_received_lo,
		       qstats->error_bytes_received_lo);
		ADD_64(estats->etherstatsoverrsizepkts_hi,
		       qstats->etherstatsoverrsizepkts_hi,
		       estats->etherstatsoverrsizepkts_lo,
		       qstats->etherstatsoverrsizepkts_lo);
		ADD_64(estats->no_buff_discard_hi, qstats->no_buff_discard_hi,
		       estats->no_buff_discard_lo, qstats->no_buff_discard_lo);
	}

	ADD_64(fstats->total_bytes_received_hi,
	       estats->rx_stat_ifhcinbadoctets_hi,
	       fstats->total_bytes_received_lo,
	       estats->rx_stat_ifhcinbadoctets_lo);

	memcpy(estats, &(fstats->total_bytes_received_hi),
	       sizeof(struct host_func_stats) - 2*sizeof(u32));

	ADD_64(estats->etherstatsoverrsizepkts_hi,
	       estats->rx_stat_dot3statsframestoolong_hi,
	       estats->etherstatsoverrsizepkts_lo,
	       estats->rx_stat_dot3statsframestoolong_lo);
	ADD_64(estats->error_bytes_received_hi,
	       estats->rx_stat_ifhcinbadoctets_hi,
	       estats->error_bytes_received_lo,
	       estats->rx_stat_ifhcinbadoctets_lo);

	if (bp->port.pmf) {
		estats->mac_filter_discard =
				le32_to_cpu(tport->mac_filter_discard);
		estats->xxoverflow_discard =
				le32_to_cpu(tport->xxoverflow_discard);
		estats->brb_truncate_discard =
				le32_to_cpu(tport->brb_truncate_discard);
		estats->mac_discard = le32_to_cpu(tport->mac_discard);
	}

	fstats->host_func_stats_start = ++fstats->host_func_stats_end;

	bp->stats_pending = 0;

	return 0;
}

static void bnx2x_net_stats_update(struct bnx2x *bp)
{
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	struct net_device_stats *nstats = &bp->dev->stats;
	unsigned long tmp;
	int i;

	nstats->rx_packets =
		bnx2x_hilo(&estats->total_unicast_packets_received_hi) +
		bnx2x_hilo(&estats->total_multicast_packets_received_hi) +
		bnx2x_hilo(&estats->total_broadcast_packets_received_hi);

	nstats->tx_packets =
		bnx2x_hilo(&estats->total_unicast_packets_transmitted_hi) +
		bnx2x_hilo(&estats->total_multicast_packets_transmitted_hi) +
		bnx2x_hilo(&estats->total_broadcast_packets_transmitted_hi);

	nstats->rx_bytes = bnx2x_hilo(&estats->total_bytes_received_hi);

	nstats->tx_bytes = bnx2x_hilo(&estats->total_bytes_transmitted_hi);

	tmp = estats->mac_discard;
	for_each_rx_queue(bp, i)
		tmp += le32_to_cpu(bp->fp[i].old_tclient.checksum_discard);
	nstats->rx_dropped = tmp;

	nstats->tx_dropped = 0;

	nstats->multicast =
		bnx2x_hilo(&estats->total_multicast_packets_received_hi);

	nstats->collisions =
		bnx2x_hilo(&estats->tx_stat_etherstatscollisions_hi);

	nstats->rx_length_errors =
		bnx2x_hilo(&estats->rx_stat_etherstatsundersizepkts_hi) +
		bnx2x_hilo(&estats->etherstatsoverrsizepkts_hi);
	nstats->rx_over_errors = bnx2x_hilo(&estats->brb_drop_hi) +
				 bnx2x_hilo(&estats->brb_truncate_hi);
	nstats->rx_crc_errors =
		bnx2x_hilo(&estats->rx_stat_dot3statsfcserrors_hi);
	nstats->rx_frame_errors =
		bnx2x_hilo(&estats->rx_stat_dot3statsalignmenterrors_hi);
	nstats->rx_fifo_errors = bnx2x_hilo(&estats->no_buff_discard_hi);
	nstats->rx_missed_errors = estats->xxoverflow_discard;

	nstats->rx_errors = nstats->rx_length_errors +
			    nstats->rx_over_errors +
			    nstats->rx_crc_errors +
			    nstats->rx_frame_errors +
			    nstats->rx_fifo_errors +
			    nstats->rx_missed_errors;

	nstats->tx_aborted_errors =
		bnx2x_hilo(&estats->tx_stat_dot3statslatecollisions_hi) +
		bnx2x_hilo(&estats->tx_stat_dot3statsexcessivecollisions_hi);
	nstats->tx_carrier_errors =
		bnx2x_hilo(&estats->rx_stat_dot3statscarriersenseerrors_hi);
	nstats->tx_fifo_errors = 0;
	nstats->tx_heartbeat_errors = 0;
	nstats->tx_window_errors = 0;

	nstats->tx_errors = nstats->tx_aborted_errors +
			    nstats->tx_carrier_errors +
	    bnx2x_hilo(&estats->tx_stat_dot3statsinternalmactransmiterrors_hi);
}

static void bnx2x_drv_stats_update(struct bnx2x *bp)
{
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	int i;

	estats->driver_xoff = 0;
	estats->rx_err_discard_pkt = 0;
	estats->rx_skb_alloc_failed = 0;
	estats->hw_csum_err = 0;
	for_each_queue(bp, i) {
		struct bnx2x_eth_q_stats *qstats = &bp->fp[i].eth_q_stats;

		estats->driver_xoff += qstats->driver_xoff;
		estats->rx_err_discard_pkt += qstats->rx_err_discard_pkt;
		estats->rx_skb_alloc_failed += qstats->rx_skb_alloc_failed;
		estats->hw_csum_err += qstats->hw_csum_err;
	}
}

static void bnx2x_stats_update(struct bnx2x *bp)
{
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	if (*stats_comp != DMAE_COMP_VAL)
		return;

	if (bp->port.pmf)
		bnx2x_hw_stats_update(bp);

	if (bnx2x_storm_stats_update(bp) && (bp->stats_pending++ == 3)) {
		BNX2X_ERR("storm stats were not updated for 3 times\n");
		bnx2x_panic();
		return;
	}

	bnx2x_net_stats_update(bp);
	bnx2x_drv_stats_update(bp);

	if (netif_msg_timer(bp)) {
		struct bnx2x_eth_stats *estats = &bp->eth_stats;
		int i;

		printk(KERN_DEBUG "%s: brb drops %u  brb truncate %u\n",
		       bp->dev->name,
		       estats->brb_drop_lo, estats->brb_truncate_lo);

		for_each_eth_queue(bp, i) {
			struct bnx2x_fastpath *fp = &bp->fp[i];
			struct bnx2x_eth_q_stats *qstats = &fp->eth_q_stats;

			printk(KERN_DEBUG "%s: rx usage(%4u)  *rx_cons_sb(%u)"
					  "  rx pkt(%lu)  rx calls(%lu %lu)\n",
			       fp->name, (le16_to_cpu(*fp->rx_cons_sb) -
			       fp->rx_comp_cons),
			       le16_to_cpu(*fp->rx_cons_sb),
			       bnx2x_hilo(&qstats->
					  total_unicast_packets_received_hi),
			       fp->rx_calls, fp->rx_pkt);
		}

		for_each_eth_queue(bp, i) {
			struct bnx2x_fastpath *fp = &bp->fp[i];
			struct bnx2x_eth_q_stats *qstats = &fp->eth_q_stats;
			struct netdev_queue *txq =
				netdev_get_tx_queue(bp->dev, i);

			printk(KERN_DEBUG "%s: tx avail(%4u)  *tx_cons_sb(%u)"
					  "  tx pkt(%lu) tx calls (%lu)"
					  "  %s (Xoff events %u)\n",
			       fp->name, bnx2x_tx_avail(fp),
			       le16_to_cpu(*fp->tx_cons_sb),
			       bnx2x_hilo(&qstats->
					  total_unicast_packets_transmitted_hi),
			       fp->tx_pkt,
			       (netif_tx_queue_stopped(txq) ? "Xoff" : "Xon"),
			       qstats->driver_xoff);
		}
	}

	bnx2x_hw_stats_post(bp);
	bnx2x_storm_stats_post(bp);
}

static void bnx2x_port_stats_stop(struct bnx2x *bp)
{
	struct dmae_command *dmae;
	u32 opcode;
	int loader_idx = PMF_DMAE_C(bp);
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	bp->executer_idx = 0;

	opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_PCI, DMAE_DST_GRC, false, 0);

	if (bp->port.port_stx) {

		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		if (bp->func_stx)
			dmae->opcode = bnx2x_dmae_opcode_add_comp(
						opcode, DMAE_COMP_GRC);
		else
			dmae->opcode = bnx2x_dmae_opcode_add_comp(
						opcode, DMAE_COMP_PCI);
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, port_stats));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, port_stats));
		dmae->dst_addr_lo = bp->port.port_stx >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = sizeof(struct host_port_stats) >> 2;
		if (bp->func_stx) {
			dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
			dmae->comp_addr_hi = 0;
			dmae->comp_val = 1;
		} else {
			dmae->comp_addr_lo =
				U64_LO(bnx2x_sp_mapping(bp, stats_comp));
			dmae->comp_addr_hi =
				U64_HI(bnx2x_sp_mapping(bp, stats_comp));
			dmae->comp_val = DMAE_COMP_VAL;

			*stats_comp = 0;
		}
	}

	if (bp->func_stx) {

		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode =
			bnx2x_dmae_opcode_add_comp(opcode, DMAE_COMP_PCI);
		dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, func_stats));
		dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, func_stats));
		dmae->dst_addr_lo = bp->func_stx >> 2;
		dmae->dst_addr_hi = 0;
		dmae->len = sizeof(struct host_func_stats) >> 2;
		dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
		dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
		dmae->comp_val = DMAE_COMP_VAL;

		*stats_comp = 0;
	}
}

static void bnx2x_stats_stop(struct bnx2x *bp)
{
	int update = 0;

	bnx2x_stats_comp(bp);

	if (bp->port.pmf)
		update = (bnx2x_hw_stats_update(bp) == 0);

	update |= (bnx2x_storm_stats_update(bp) == 0);

	if (update) {
		bnx2x_net_stats_update(bp);

		if (bp->port.pmf)
			bnx2x_port_stats_stop(bp);

		bnx2x_hw_stats_post(bp);
		bnx2x_stats_comp(bp);
	}
}

static void bnx2x_stats_do_nothing(struct bnx2x *bp)
{
}

static const struct {
	void (*action)(struct bnx2x *bp);
	enum bnx2x_stats_state next_state;
} bnx2x_stats_stm[STATS_STATE_MAX][STATS_EVENT_MAX] = {
/* state	event	*/
{
/* DISABLED	PMF	*/ {bnx2x_stats_pmf_update, STATS_STATE_DISABLED},
/*		LINK_UP	*/ {bnx2x_stats_start,      STATS_STATE_ENABLED},
/*		UPDATE	*/ {bnx2x_stats_do_nothing, STATS_STATE_DISABLED},
/*		STOP	*/ {bnx2x_stats_do_nothing, STATS_STATE_DISABLED}
},
{
/* ENABLED	PMF	*/ {bnx2x_stats_pmf_start,  STATS_STATE_ENABLED},
/*		LINK_UP	*/ {bnx2x_stats_restart,    STATS_STATE_ENABLED},
/*		UPDATE	*/ {bnx2x_stats_update,     STATS_STATE_ENABLED},
/*		STOP	*/ {bnx2x_stats_stop,       STATS_STATE_DISABLED}
}
};

void bnx2x_stats_handle(struct bnx2x *bp, enum bnx2x_stats_event event)
{
	enum bnx2x_stats_state state;

	if (unlikely(bp->panic))
		return;

	bnx2x_stats_stm[bp->stats_state][event].action(bp);

	/* Protect a state change flow */
	spin_lock_bh(&bp->stats_lock);
	state = bp->stats_state;
	bp->stats_state = bnx2x_stats_stm[state][event].next_state;
	spin_unlock_bh(&bp->stats_lock);

	if ((event != STATS_EVENT_UPDATE) || netif_msg_timer(bp))
		DP(BNX2X_MSG_STATS, "state %d -> event %d -> state %d\n",
		   state, event, bp->stats_state);
}

static void bnx2x_port_stats_base_init(struct bnx2x *bp)
{
	struct dmae_command *dmae;
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* sanity */
	if (!bp->port.pmf || !bp->port.port_stx) {
		BNX2X_ERR("BUG!\n");
		return;
	}

	bp->executer_idx = 0;

	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_PCI, DMAE_DST_GRC,
					 true, DMAE_COMP_PCI);
	dmae->src_addr_lo = U64_LO(bnx2x_sp_mapping(bp, port_stats));
	dmae->src_addr_hi = U64_HI(bnx2x_sp_mapping(bp, port_stats));
	dmae->dst_addr_lo = bp->port.port_stx >> 2;
	dmae->dst_addr_hi = 0;
	dmae->len = sizeof(struct host_port_stats) >> 2;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_val = DMAE_COMP_VAL;

	*stats_comp = 0;
	bnx2x_hw_stats_post(bp);
	bnx2x_stats_comp(bp);
}

static void bnx2x_func_stats_base_init(struct bnx2x *bp)
{
	int vn, vn_max = IS_MF(bp) ? E1HVN_MAX : E1VN_MAX;
	u32 func_stx;

	/* sanity */
	if (!bp->port.pmf || !bp->func_stx) {
		BNX2X_ERR("BUG!\n");
		return;
	}

	/* save our func_stx */
	func_stx = bp->func_stx;

	for (vn = VN_0; vn < vn_max; vn++) {
		int mb_idx = !CHIP_IS_E2(bp) ? 2*vn + BP_PORT(bp) : vn;

		bp->func_stx = SHMEM_RD(bp, func_mb[mb_idx].fw_mb_param);
		bnx2x_func_stats_init(bp);
		bnx2x_hw_stats_post(bp);
		bnx2x_stats_comp(bp);
	}

	/* restore our func_stx */
	bp->func_stx = func_stx;
}

static void bnx2x_func_stats_base_update(struct bnx2x *bp)
{
	struct dmae_command *dmae = &bp->stats_dmae;
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* sanity */
	if (!bp->func_stx) {
		BNX2X_ERR("BUG!\n");
		return;
	}

	bp->executer_idx = 0;
	memset(dmae, 0, sizeof(struct dmae_command));

	dmae->opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_GRC, DMAE_DST_PCI,
					 true, DMAE_COMP_PCI);
	dmae->src_addr_lo = bp->func_stx >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, func_stats_base));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, func_stats_base));
	dmae->len = sizeof(struct host_func_stats) >> 2;
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_val = DMAE_COMP_VAL;

	*stats_comp = 0;
	bnx2x_hw_stats_post(bp);
	bnx2x_stats_comp(bp);
}

void bnx2x_stats_init(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int mb_idx = BP_FW_MB_IDX(bp);
	int i;
	struct eth_stats_query *stats = bnx2x_sp(bp, fw_stats);

	bp->stats_pending = 0;
	bp->executer_idx = 0;
	bp->stats_counter = 0;

	/* port and func stats for management */
	if (!BP_NOMCP(bp)) {
		bp->port.port_stx = SHMEM_RD(bp, port_mb[port].port_stx);
		bp->func_stx = SHMEM_RD(bp, func_mb[mb_idx].fw_mb_param);

	} else {
		bp->port.port_stx = 0;
		bp->func_stx = 0;
	}
	DP(BNX2X_MSG_STATS, "port_stx 0x%x  func_stx 0x%x\n",
	   bp->port.port_stx, bp->func_stx);

	/* port stats */
	memset(&(bp->port.old_nig_stats), 0, sizeof(struct nig_stats));
	bp->port.old_nig_stats.brb_discard =
			REG_RD(bp, NIG_REG_STAT0_BRB_DISCARD + port*0x38);
	bp->port.old_nig_stats.brb_truncate =
			REG_RD(bp, NIG_REG_STAT0_BRB_TRUNCATE + port*0x38);
	REG_RD_DMAE(bp, NIG_REG_STAT0_EGRESS_MAC_PKT0 + port*0x50,
		    &(bp->port.old_nig_stats.egress_mac_pkt0_lo), 2);
	REG_RD_DMAE(bp, NIG_REG_STAT0_EGRESS_MAC_PKT1 + port*0x50,
		    &(bp->port.old_nig_stats.egress_mac_pkt1_lo), 2);

	/* function stats */
	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		memset(&fp->old_tclient, 0,
		       sizeof(struct tstorm_per_client_stats));
		memset(&fp->old_uclient, 0,
		       sizeof(struct ustorm_per_client_stats));
		memset(&fp->old_xclient, 0,
		       sizeof(struct xstorm_per_client_stats));
		memset(&fp->eth_q_stats, 0, sizeof(struct bnx2x_eth_q_stats));
	}

	/* FW stats are currently collected for ETH clients only */
	for_each_eth_queue(bp, i) {
		/* Set initial stats counter in the stats ramrod data to -1 */
		int cl_id = bp->fp[i].cl_id;

		stats->xstorm_common.client_statistics[cl_id].
			stats_counter = 0xffff;
		stats->ustorm_common.client_statistics[cl_id].
			stats_counter = 0xffff;
		stats->tstorm_common.client_statistics[cl_id].
			stats_counter = 0xffff;
	}

	memset(&bp->dev->stats, 0, sizeof(struct net_device_stats));
	memset(&bp->eth_stats, 0, sizeof(struct bnx2x_eth_stats));

	bp->stats_state = STATS_STATE_DISABLED;

	if (bp->port.pmf) {
		if (bp->port.port_stx)
			bnx2x_port_stats_base_init(bp);

		if (bp->func_stx)
			bnx2x_func_stats_base_init(bp);

	} else if (bp->func_stx)
		bnx2x_func_stats_base_update(bp);
}
