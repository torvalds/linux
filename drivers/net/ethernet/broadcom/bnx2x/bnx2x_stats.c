/* bnx2x_stats.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "bnx2x_stats.h"
#include "bnx2x_cmn.h"
#include "bnx2x_sriov.h"

/* Statistics */

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

static inline u16 bnx2x_get_port_stats_dma_len(struct bnx2x *bp)
{
	u16 res = 0;

	/* 'newest' convention - shmem2 cotains the size of the port stats */
	if (SHMEM2_HAS(bp, sizeof_port_stats)) {
		u32 size = SHMEM2_RD(bp, sizeof_port_stats);
		if (size)
			res = size;

		/* prevent newer BC from causing buffer overflow */
		if (res > sizeof(struct host_port_stats))
			res = sizeof(struct host_port_stats);
	}

	/* Older convention - all BCs support the port stats' fields up until
	 * the 'not_used' field
	 */
	if (!res) {
		res = offsetof(struct host_port_stats, not_used) + 4;

		/* if PFC stats are supported by the MFW, DMA them as well */
		if (bp->flags & BC_SUPPORTS_PFC_STATS) {
			res += offsetof(struct host_port_stats,
					pfc_frames_rx_lo) -
			       offsetof(struct host_port_stats,
					pfc_frames_tx_hi) + 4 ;
		}
	}

	res >>= 2;

	WARN_ON(res > 2 * DMAE_LEN32_RD_MAX);
	return res;
}

/*
 * Init service functions
 */

static void bnx2x_dp_stats(struct bnx2x *bp)
{
	int i;

	DP(BNX2X_MSG_STATS, "dumping stats:\n"
	   "fw_stats_req\n"
	   "    hdr\n"
	   "        cmd_num %d\n"
	   "        reserved0 %d\n"
	   "        drv_stats_counter %d\n"
	   "        reserved1 %d\n"
	   "        stats_counters_addrs %x %x\n",
	   bp->fw_stats_req->hdr.cmd_num,
	   bp->fw_stats_req->hdr.reserved0,
	   bp->fw_stats_req->hdr.drv_stats_counter,
	   bp->fw_stats_req->hdr.reserved1,
	   bp->fw_stats_req->hdr.stats_counters_addrs.hi,
	   bp->fw_stats_req->hdr.stats_counters_addrs.lo);

	for (i = 0; i < bp->fw_stats_req->hdr.cmd_num; i++) {
		DP(BNX2X_MSG_STATS,
		   "query[%d]\n"
		   "              kind %d\n"
		   "              index %d\n"
		   "              funcID %d\n"
		   "              reserved %d\n"
		   "              address %x %x\n",
		   i, bp->fw_stats_req->query[i].kind,
		   bp->fw_stats_req->query[i].index,
		   bp->fw_stats_req->query[i].funcID,
		   bp->fw_stats_req->query[i].reserved,
		   bp->fw_stats_req->query[i].address.hi,
		   bp->fw_stats_req->query[i].address.lo);
	}
}

/* Post the next statistics ramrod. Protect it with the spin in
 * order to ensure the strict order between statistics ramrods
 * (each ramrod has a sequence number passed in a
 * bp->fw_stats_req->hdr.drv_stats_counter and ramrods must be
 * sent in order).
 */
static void bnx2x_storm_stats_post(struct bnx2x *bp)
{
	if (!bp->stats_pending) {
		int rc;

		spin_lock_bh(&bp->stats_lock);

		if (bp->stats_pending) {
			spin_unlock_bh(&bp->stats_lock);
			return;
		}

		bp->fw_stats_req->hdr.drv_stats_counter =
			cpu_to_le16(bp->stats_counter++);

		DP(BNX2X_MSG_STATS, "Sending statistics ramrod %d\n",
			bp->fw_stats_req->hdr.drv_stats_counter);

		/* adjust the ramrod to include VF queues statistics */
		bnx2x_iov_adjust_stats_req(bp);
		bnx2x_dp_stats(bp);

		/* send FW stats ramrod */
		rc = bnx2x_sp_post(bp, RAMROD_CMD_ID_COMMON_STAT_QUERY, 0,
				   U64_HI(bp->fw_stats_req_mapping),
				   U64_LO(bp->fw_stats_req_mapping),
				   NONE_CONNECTION_TYPE);
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

	/* Update MCP's statistics if possible */
	if (bp->func_stx)
		memcpy(bnx2x_sp(bp, func_stats), &bp->func_stats,
		       sizeof(bp->func_stats));

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
		usleep_range(1000, 2000);
	}
	return 1;
}

/*
 * Statistics service functions
 */

/* should be called under stats_sema */
static void __bnx2x_stats_pmf_update(struct bnx2x *bp)
{
	struct dmae_command *dmae;
	u32 opcode;
	int loader_idx = PMF_DMAE_C(bp);
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* sanity */
	if (!bp->port.pmf || !bp->port.port_stx) {
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
	dmae->len = bnx2x_get_port_stats_dma_len(bp) - DMAE_LEN32_RD_MAX;

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
		dmae->len = bnx2x_get_port_stats_dma_len(bp);
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

	/* EMAC is special */
	if (bp->link_vars.mac_type == MAC_TYPE_EMAC) {
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
	} else {
		u32 tx_src_addr_lo, rx_src_addr_lo;
		u16 rx_len, tx_len;

		/* configure the params according to MAC type */
		switch (bp->link_vars.mac_type) {
		case MAC_TYPE_BMAC:
			mac_addr = (port ? NIG_REG_INGRESS_BMAC1_MEM :
					   NIG_REG_INGRESS_BMAC0_MEM);

			/* BIGMAC_REGISTER_TX_STAT_GTPKT ..
			   BIGMAC_REGISTER_TX_STAT_GTBYT */
			if (CHIP_IS_E1x(bp)) {
				tx_src_addr_lo = (mac_addr +
					BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2;
				tx_len = (8 + BIGMAC_REGISTER_TX_STAT_GTBYT -
					  BIGMAC_REGISTER_TX_STAT_GTPKT) >> 2;
				rx_src_addr_lo = (mac_addr +
					BIGMAC_REGISTER_RX_STAT_GR64) >> 2;
				rx_len = (8 + BIGMAC_REGISTER_RX_STAT_GRIPJ -
					  BIGMAC_REGISTER_RX_STAT_GR64) >> 2;
			} else {
				tx_src_addr_lo = (mac_addr +
					BIGMAC2_REGISTER_TX_STAT_GTPOK) >> 2;
				tx_len = (8 + BIGMAC2_REGISTER_TX_STAT_GTBYT -
					  BIGMAC2_REGISTER_TX_STAT_GTPOK) >> 2;
				rx_src_addr_lo = (mac_addr +
					BIGMAC2_REGISTER_RX_STAT_GR64) >> 2;
				rx_len = (8 + BIGMAC2_REGISTER_RX_STAT_GRIPJ -
					  BIGMAC2_REGISTER_RX_STAT_GR64) >> 2;
			}
			break;

		case MAC_TYPE_UMAC: /* handled by MSTAT */
		case MAC_TYPE_XMAC: /* handled by MSTAT */
		default:
			mac_addr = port ? GRCBASE_MSTAT1 : GRCBASE_MSTAT0;
			tx_src_addr_lo = (mac_addr +
					  MSTAT_REG_TX_STAT_GTXPOK_LO) >> 2;
			rx_src_addr_lo = (mac_addr +
					  MSTAT_REG_RX_STAT_GR64_LO) >> 2;
			tx_len = sizeof(bp->slowpath->
					mac_stats.mstat_stats.stats_tx) >> 2;
			rx_len = sizeof(bp->slowpath->
					mac_stats.mstat_stats.stats_rx) >> 2;
			break;
		}

		/* TX stats */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_lo = tx_src_addr_lo;
		dmae->src_addr_hi = 0;
		dmae->len = tx_len;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, mac_stats));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, mac_stats));
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;

		/* RX stats */
		dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
		dmae->opcode = opcode;
		dmae->src_addr_hi = 0;
		dmae->src_addr_lo = rx_src_addr_lo;
		dmae->dst_addr_lo =
			U64_LO(bnx2x_sp_mapping(bp, mac_stats) + (tx_len << 2));
		dmae->dst_addr_hi =
			U64_HI(bnx2x_sp_mapping(bp, mac_stats) + (tx_len << 2));
		dmae->len = rx_len;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;
	}

	/* NIG */
	if (!CHIP_IS_E3(bp)) {
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
		dmae->opcode = opcode;
		dmae->src_addr_lo = (port ? NIG_REG_STAT1_EGRESS_MAC_PKT1 :
					    NIG_REG_STAT0_EGRESS_MAC_PKT1) >> 2;
		dmae->src_addr_hi = 0;
		dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig_stats) +
				offsetof(struct nig_stats, egress_mac_pkt1_lo));
		dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig_stats) +
				offsetof(struct nig_stats, egress_mac_pkt1_lo));
		dmae->len = (2*sizeof(u32)) >> 2;
		dmae->comp_addr_lo = dmae_reg_go_c[loader_idx] >> 2;
		dmae->comp_addr_hi = 0;
		dmae->comp_val = 1;
	}

	dmae = bnx2x_sp(bp, dmae[bp->executer_idx++]);
	dmae->opcode = bnx2x_dmae_opcode(bp, DMAE_SRC_GRC, DMAE_DST_PCI,
						 true, DMAE_COMP_PCI);
	dmae->src_addr_lo = (port ? NIG_REG_STAT1_BRB_DISCARD :
				    NIG_REG_STAT0_BRB_DISCARD) >> 2;
	dmae->src_addr_hi = 0;
	dmae->dst_addr_lo = U64_LO(bnx2x_sp_mapping(bp, nig_stats));
	dmae->dst_addr_hi = U64_HI(bnx2x_sp_mapping(bp, nig_stats));
	dmae->len = (sizeof(struct nig_stats) - 4*sizeof(u32)) >> 2;

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

/* should be called under stats_sema */
static void __bnx2x_stats_start(struct bnx2x *bp)
{
	/* vfs travel through here as part of the statistics FSM, but no action
	 * is required
	 */
	if (IS_VF(bp))
		return;

	if (bp->port.pmf)
		bnx2x_port_stats_init(bp);

	else if (bp->func_stx)
		bnx2x_func_stats_init(bp);

	bnx2x_hw_stats_post(bp);
	bnx2x_storm_stats_post(bp);

	bp->stats_started = true;
}

static void bnx2x_stats_start(struct bnx2x *bp)
{
	if (down_timeout(&bp->stats_sema, HZ/10))
		BNX2X_ERR("Unable to acquire stats lock\n");
	__bnx2x_stats_start(bp);
	up(&bp->stats_sema);
}

static void bnx2x_stats_pmf_start(struct bnx2x *bp)
{
	if (down_timeout(&bp->stats_sema, HZ/10))
		BNX2X_ERR("Unable to acquire stats lock\n");
	bnx2x_stats_comp(bp);
	__bnx2x_stats_pmf_update(bp);
	__bnx2x_stats_start(bp);
	up(&bp->stats_sema);
}

static void bnx2x_stats_pmf_update(struct bnx2x *bp)
{
	if (down_timeout(&bp->stats_sema, HZ/10))
		BNX2X_ERR("Unable to acquire stats lock\n");
	__bnx2x_stats_pmf_update(bp);
	up(&bp->stats_sema);
}

static void bnx2x_stats_restart(struct bnx2x *bp)
{
	/* vfs travel through here as part of the statistics FSM, but no action
	 * is required
	 */
	if (IS_VF(bp))
		return;
	if (down_timeout(&bp->stats_sema, HZ/10))
		BNX2X_ERR("Unable to acquire stats lock\n");
	bnx2x_stats_comp(bp);
	__bnx2x_stats_start(bp);
	up(&bp->stats_sema);
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
		UPDATE_STAT64(rx_stat_grxpf, rx_stat_mac_xpf);

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
		UPDATE_STAT64(tx_stat_gt2047, tx_stat_mac_2047);
		UPDATE_STAT64(tx_stat_gt4095, tx_stat_mac_4095);
		UPDATE_STAT64(tx_stat_gt9216, tx_stat_mac_9216);
		UPDATE_STAT64(tx_stat_gt16383, tx_stat_mac_16383);
		UPDATE_STAT64(tx_stat_gterr,
				tx_stat_dot3statsinternalmactransmiterrors);
		UPDATE_STAT64(tx_stat_gtufl, tx_stat_mac_ufl);

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
		UPDATE_STAT64(rx_stat_grxpf, rx_stat_mac_xpf);
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
		UPDATE_STAT64(tx_stat_gt2047, tx_stat_mac_2047);
		UPDATE_STAT64(tx_stat_gt4095, tx_stat_mac_4095);
		UPDATE_STAT64(tx_stat_gt9216, tx_stat_mac_9216);
		UPDATE_STAT64(tx_stat_gt16383, tx_stat_mac_16383);
		UPDATE_STAT64(tx_stat_gterr,
				tx_stat_dot3statsinternalmactransmiterrors);
		UPDATE_STAT64(tx_stat_gtufl, tx_stat_mac_ufl);

		/* collect PFC stats */
		pstats->pfc_frames_tx_hi = new->tx_stat_gtpp_hi;
		pstats->pfc_frames_tx_lo = new->tx_stat_gtpp_lo;

		pstats->pfc_frames_rx_hi = new->rx_stat_grpp_hi;
		pstats->pfc_frames_rx_lo = new->rx_stat_grpp_lo;
	}

	estats->pause_frames_received_hi =
				pstats->mac_stx[1].rx_stat_mac_xpf_hi;
	estats->pause_frames_received_lo =
				pstats->mac_stx[1].rx_stat_mac_xpf_lo;

	estats->pause_frames_sent_hi =
				pstats->mac_stx[1].tx_stat_outxoffsent_hi;
	estats->pause_frames_sent_lo =
				pstats->mac_stx[1].tx_stat_outxoffsent_lo;

	estats->pfc_frames_received_hi =
				pstats->pfc_frames_rx_hi;
	estats->pfc_frames_received_lo =
				pstats->pfc_frames_rx_lo;
	estats->pfc_frames_sent_hi =
				pstats->pfc_frames_tx_hi;
	estats->pfc_frames_sent_lo =
				pstats->pfc_frames_tx_lo;
}

static void bnx2x_mstat_stats_update(struct bnx2x *bp)
{
	struct host_port_stats *pstats = bnx2x_sp(bp, port_stats);
	struct bnx2x_eth_stats *estats = &bp->eth_stats;

	struct mstat_stats *new = bnx2x_sp(bp, mac_stats.mstat_stats);

	ADD_STAT64(stats_rx.rx_grerb, rx_stat_ifhcinbadoctets);
	ADD_STAT64(stats_rx.rx_grfcs, rx_stat_dot3statsfcserrors);
	ADD_STAT64(stats_rx.rx_grund, rx_stat_etherstatsundersizepkts);
	ADD_STAT64(stats_rx.rx_grovr, rx_stat_dot3statsframestoolong);
	ADD_STAT64(stats_rx.rx_grfrg, rx_stat_etherstatsfragments);
	ADD_STAT64(stats_rx.rx_grxcf, rx_stat_maccontrolframesreceived);
	ADD_STAT64(stats_rx.rx_grxpf, rx_stat_xoffstateentered);
	ADD_STAT64(stats_rx.rx_grxpf, rx_stat_mac_xpf);
	ADD_STAT64(stats_tx.tx_gtxpf, tx_stat_outxoffsent);
	ADD_STAT64(stats_tx.tx_gtxpf, tx_stat_flowcontroldone);

	/* collect pfc stats */
	ADD_64(pstats->pfc_frames_tx_hi, new->stats_tx.tx_gtxpp_hi,
		pstats->pfc_frames_tx_lo, new->stats_tx.tx_gtxpp_lo);
	ADD_64(pstats->pfc_frames_rx_hi, new->stats_rx.rx_grxpp_hi,
		pstats->pfc_frames_rx_lo, new->stats_rx.rx_grxpp_lo);

	ADD_STAT64(stats_tx.tx_gt64, tx_stat_etherstatspkts64octets);
	ADD_STAT64(stats_tx.tx_gt127,
			tx_stat_etherstatspkts65octetsto127octets);
	ADD_STAT64(stats_tx.tx_gt255,
			tx_stat_etherstatspkts128octetsto255octets);
	ADD_STAT64(stats_tx.tx_gt511,
			tx_stat_etherstatspkts256octetsto511octets);
	ADD_STAT64(stats_tx.tx_gt1023,
			tx_stat_etherstatspkts512octetsto1023octets);
	ADD_STAT64(stats_tx.tx_gt1518,
			tx_stat_etherstatspkts1024octetsto1522octets);
	ADD_STAT64(stats_tx.tx_gt2047, tx_stat_mac_2047);

	ADD_STAT64(stats_tx.tx_gt4095, tx_stat_mac_4095);
	ADD_STAT64(stats_tx.tx_gt9216, tx_stat_mac_9216);
	ADD_STAT64(stats_tx.tx_gt16383, tx_stat_mac_16383);

	ADD_STAT64(stats_tx.tx_gterr,
			tx_stat_dot3statsinternalmactransmiterrors);
	ADD_STAT64(stats_tx.tx_gtufl, tx_stat_mac_ufl);

	estats->etherstatspkts1024octetsto1522octets_hi =
	    pstats->mac_stx[1].tx_stat_etherstatspkts1024octetsto1522octets_hi;
	estats->etherstatspkts1024octetsto1522octets_lo =
	    pstats->mac_stx[1].tx_stat_etherstatspkts1024octetsto1522octets_lo;

	estats->etherstatspktsover1522octets_hi =
	    pstats->mac_stx[1].tx_stat_mac_2047_hi;
	estats->etherstatspktsover1522octets_lo =
	    pstats->mac_stx[1].tx_stat_mac_2047_lo;

	ADD_64(estats->etherstatspktsover1522octets_hi,
	       pstats->mac_stx[1].tx_stat_mac_4095_hi,
	       estats->etherstatspktsover1522octets_lo,
	       pstats->mac_stx[1].tx_stat_mac_4095_lo);

	ADD_64(estats->etherstatspktsover1522octets_hi,
	       pstats->mac_stx[1].tx_stat_mac_9216_hi,
	       estats->etherstatspktsover1522octets_lo,
	       pstats->mac_stx[1].tx_stat_mac_9216_lo);

	ADD_64(estats->etherstatspktsover1522octets_hi,
	       pstats->mac_stx[1].tx_stat_mac_16383_hi,
	       estats->etherstatspktsover1522octets_lo,
	       pstats->mac_stx[1].tx_stat_mac_16383_lo);

	estats->pause_frames_received_hi =
				pstats->mac_stx[1].rx_stat_mac_xpf_hi;
	estats->pause_frames_received_lo =
				pstats->mac_stx[1].rx_stat_mac_xpf_lo;

	estats->pause_frames_sent_hi =
				pstats->mac_stx[1].tx_stat_outxoffsent_hi;
	estats->pause_frames_sent_lo =
				pstats->mac_stx[1].tx_stat_outxoffsent_lo;

	estats->pfc_frames_received_hi =
				pstats->pfc_frames_rx_hi;
	estats->pfc_frames_received_lo =
				pstats->pfc_frames_rx_lo;
	estats->pfc_frames_sent_hi =
				pstats->pfc_frames_tx_hi;
	estats->pfc_frames_sent_lo =
				pstats->pfc_frames_tx_lo;
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

	switch (bp->link_vars.mac_type) {
	case MAC_TYPE_BMAC:
		bnx2x_bmac_stats_update(bp);
		break;

	case MAC_TYPE_EMAC:
		bnx2x_emac_stats_update(bp);
		break;

	case MAC_TYPE_UMAC:
	case MAC_TYPE_XMAC:
		bnx2x_mstat_stats_update(bp);
		break;

	case MAC_TYPE_NONE: /* unreached */
		DP(BNX2X_MSG_STATS,
		   "stats updated by DMAE but no MAC active\n");
		return -1;

	default: /* unreached */
		BNX2X_ERR("Unknown MAC type\n");
	}

	ADD_EXTEND_64(pstats->brb_drop_hi, pstats->brb_drop_lo,
		      new->brb_discard - old->brb_discard);
	ADD_EXTEND_64(estats->brb_truncate_hi, estats->brb_truncate_lo,
		      new->brb_truncate - old->brb_truncate);

	if (!CHIP_IS_E3(bp)) {
		UPDATE_STAT64_NIG(egress_mac_pkt0,
					etherstatspkts1024octetsto1522octets);
		UPDATE_STAT64_NIG(egress_mac_pkt1,
					etherstatspktsover1522octets);
	}

	memcpy(old, new, sizeof(struct nig_stats));

	memcpy(&(estats->rx_stat_ifhcinbadoctets_hi), &(pstats->mac_stx[1]),
	       sizeof(struct mac_stx));
	estats->brb_drop_hi = pstats->brb_drop_hi;
	estats->brb_drop_lo = pstats->brb_drop_lo;

	pstats->host_port_stats_counter++;

	if (CHIP_IS_E3(bp)) {
		u32 lpi_reg = BP_PORT(bp) ? MISC_REG_CPMU_LP_SM_ENT_CNT_P1
					  : MISC_REG_CPMU_LP_SM_ENT_CNT_P0;
		estats->eee_tx_lpi += REG_RD(bp, lpi_reg);
	}

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

static int bnx2x_storm_stats_validate_counters(struct bnx2x *bp)
{
	struct stats_counter *counters = &bp->fw_stats_data->storm_counters;
	u16 cur_stats_counter;
	/* Make sure we use the value of the counter
	 * used for sending the last stats ramrod.
	 */
	cur_stats_counter = bp->stats_counter - 1;

	/* are storm stats valid? */
	if (le16_to_cpu(counters->xstats_counter) != cur_stats_counter) {
		DP(BNX2X_MSG_STATS,
		   "stats not updated by xstorm  xstorm counter (0x%x) != stats_counter (0x%x)\n",
		   le16_to_cpu(counters->xstats_counter), bp->stats_counter);
		return -EAGAIN;
	}

	if (le16_to_cpu(counters->ustats_counter) != cur_stats_counter) {
		DP(BNX2X_MSG_STATS,
		   "stats not updated by ustorm  ustorm counter (0x%x) != stats_counter (0x%x)\n",
		   le16_to_cpu(counters->ustats_counter), bp->stats_counter);
		return -EAGAIN;
	}

	if (le16_to_cpu(counters->cstats_counter) != cur_stats_counter) {
		DP(BNX2X_MSG_STATS,
		   "stats not updated by cstorm  cstorm counter (0x%x) != stats_counter (0x%x)\n",
		   le16_to_cpu(counters->cstats_counter), bp->stats_counter);
		return -EAGAIN;
	}

	if (le16_to_cpu(counters->tstats_counter) != cur_stats_counter) {
		DP(BNX2X_MSG_STATS,
		   "stats not updated by tstorm  tstorm counter (0x%x) != stats_counter (0x%x)\n",
		   le16_to_cpu(counters->tstats_counter), bp->stats_counter);
		return -EAGAIN;
	}
	return 0;
}

static int bnx2x_storm_stats_update(struct bnx2x *bp)
{
	struct tstorm_per_port_stats *tport =
				&bp->fw_stats_data->port.tstorm_port_statistics;
	struct tstorm_per_pf_stats *tfunc =
				&bp->fw_stats_data->pf.tstorm_pf_statistics;
	struct host_func_stats *fstats = &bp->func_stats;
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	struct bnx2x_eth_stats_old *estats_old = &bp->eth_stats_old;
	int i;

	/* vfs stat counter is managed by pf */
	if (IS_PF(bp) && bnx2x_storm_stats_validate_counters(bp))
		return -EAGAIN;

	estats->error_bytes_received_hi = 0;
	estats->error_bytes_received_lo = 0;

	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		struct tstorm_per_queue_stats *tclient =
			&bp->fw_stats_data->queue_stats[i].
			tstorm_queue_statistics;
		struct tstorm_per_queue_stats *old_tclient =
			&bnx2x_fp_stats(bp, fp)->old_tclient;
		struct ustorm_per_queue_stats *uclient =
			&bp->fw_stats_data->queue_stats[i].
			ustorm_queue_statistics;
		struct ustorm_per_queue_stats *old_uclient =
			&bnx2x_fp_stats(bp, fp)->old_uclient;
		struct xstorm_per_queue_stats *xclient =
			&bp->fw_stats_data->queue_stats[i].
			xstorm_queue_statistics;
		struct xstorm_per_queue_stats *old_xclient =
			&bnx2x_fp_stats(bp, fp)->old_xclient;
		struct bnx2x_eth_q_stats *qstats =
			&bnx2x_fp_stats(bp, fp)->eth_q_stats;
		struct bnx2x_eth_q_stats_old *qstats_old =
			&bnx2x_fp_stats(bp, fp)->eth_q_stats_old;

		u32 diff;

		DP(BNX2X_MSG_STATS, "queue[%d]: ucast_sent 0x%x, bcast_sent 0x%x mcast_sent 0x%x\n",
		   i, xclient->ucast_pkts_sent,
		   xclient->bcast_pkts_sent, xclient->mcast_pkts_sent);

		DP(BNX2X_MSG_STATS, "---------------\n");

		UPDATE_QSTAT(tclient->rcv_bcast_bytes,
			     total_broadcast_bytes_received);
		UPDATE_QSTAT(tclient->rcv_mcast_bytes,
			     total_multicast_bytes_received);
		UPDATE_QSTAT(tclient->rcv_ucast_bytes,
			     total_unicast_bytes_received);

		/*
		 * sum to total_bytes_received all
		 * unicast/multicast/broadcast
		 */
		qstats->total_bytes_received_hi =
			qstats->total_broadcast_bytes_received_hi;
		qstats->total_bytes_received_lo =
			qstats->total_broadcast_bytes_received_lo;

		ADD_64(qstats->total_bytes_received_hi,
		       qstats->total_multicast_bytes_received_hi,
		       qstats->total_bytes_received_lo,
		       qstats->total_multicast_bytes_received_lo);

		ADD_64(qstats->total_bytes_received_hi,
		       qstats->total_unicast_bytes_received_hi,
		       qstats->total_bytes_received_lo,
		       qstats->total_unicast_bytes_received_lo);

		qstats->valid_bytes_received_hi =
					qstats->total_bytes_received_hi;
		qstats->valid_bytes_received_lo =
					qstats->total_bytes_received_lo;

		UPDATE_EXTEND_TSTAT(rcv_ucast_pkts,
					total_unicast_packets_received);
		UPDATE_EXTEND_TSTAT(rcv_mcast_pkts,
					total_multicast_packets_received);
		UPDATE_EXTEND_TSTAT(rcv_bcast_pkts,
					total_broadcast_packets_received);
		UPDATE_EXTEND_E_TSTAT(pkts_too_big_discard,
				      etherstatsoverrsizepkts, 32);
		UPDATE_EXTEND_E_TSTAT(no_buff_discard, no_buff_discard, 16);

		SUB_EXTEND_USTAT(ucast_no_buff_pkts,
					total_unicast_packets_received);
		SUB_EXTEND_USTAT(mcast_no_buff_pkts,
					total_multicast_packets_received);
		SUB_EXTEND_USTAT(bcast_no_buff_pkts,
					total_broadcast_packets_received);
		UPDATE_EXTEND_E_USTAT(ucast_no_buff_pkts, no_buff_discard);
		UPDATE_EXTEND_E_USTAT(mcast_no_buff_pkts, no_buff_discard);
		UPDATE_EXTEND_E_USTAT(bcast_no_buff_pkts, no_buff_discard);

		UPDATE_QSTAT(xclient->bcast_bytes_sent,
			     total_broadcast_bytes_transmitted);
		UPDATE_QSTAT(xclient->mcast_bytes_sent,
			     total_multicast_bytes_transmitted);
		UPDATE_QSTAT(xclient->ucast_bytes_sent,
			     total_unicast_bytes_transmitted);

		/*
		 * sum to total_bytes_transmitted all
		 * unicast/multicast/broadcast
		 */
		qstats->total_bytes_transmitted_hi =
				qstats->total_unicast_bytes_transmitted_hi;
		qstats->total_bytes_transmitted_lo =
				qstats->total_unicast_bytes_transmitted_lo;

		ADD_64(qstats->total_bytes_transmitted_hi,
		       qstats->total_broadcast_bytes_transmitted_hi,
		       qstats->total_bytes_transmitted_lo,
		       qstats->total_broadcast_bytes_transmitted_lo);

		ADD_64(qstats->total_bytes_transmitted_hi,
		       qstats->total_multicast_bytes_transmitted_hi,
		       qstats->total_bytes_transmitted_lo,
		       qstats->total_multicast_bytes_transmitted_lo);

		UPDATE_EXTEND_XSTAT(ucast_pkts_sent,
					total_unicast_packets_transmitted);
		UPDATE_EXTEND_XSTAT(mcast_pkts_sent,
					total_multicast_packets_transmitted);
		UPDATE_EXTEND_XSTAT(bcast_pkts_sent,
					total_broadcast_packets_transmitted);

		UPDATE_EXTEND_TSTAT(checksum_discard,
				    total_packets_received_checksum_discarded);
		UPDATE_EXTEND_TSTAT(ttl0_discard,
				    total_packets_received_ttl0_discarded);

		UPDATE_EXTEND_XSTAT(error_drop_pkts,
				    total_transmitted_dropped_packets_error);

		/* TPA aggregations completed */
		UPDATE_EXTEND_E_USTAT(coalesced_events, total_tpa_aggregations);
		/* Number of network frames aggregated by TPA */
		UPDATE_EXTEND_E_USTAT(coalesced_pkts,
				      total_tpa_aggregated_frames);
		/* Total number of bytes in completed TPA aggregations */
		UPDATE_QSTAT(uclient->coalesced_bytes, total_tpa_bytes);

		UPDATE_ESTAT_QSTAT_64(total_tpa_bytes);

		UPDATE_FSTAT_QSTAT(total_bytes_received);
		UPDATE_FSTAT_QSTAT(total_bytes_transmitted);
		UPDATE_FSTAT_QSTAT(total_unicast_packets_received);
		UPDATE_FSTAT_QSTAT(total_multicast_packets_received);
		UPDATE_FSTAT_QSTAT(total_broadcast_packets_received);
		UPDATE_FSTAT_QSTAT(total_unicast_packets_transmitted);
		UPDATE_FSTAT_QSTAT(total_multicast_packets_transmitted);
		UPDATE_FSTAT_QSTAT(total_broadcast_packets_transmitted);
		UPDATE_FSTAT_QSTAT(valid_bytes_received);
	}

	ADD_64(estats->total_bytes_received_hi,
	       estats->rx_stat_ifhcinbadoctets_hi,
	       estats->total_bytes_received_lo,
	       estats->rx_stat_ifhcinbadoctets_lo);

	ADD_64_LE(estats->total_bytes_received_hi,
		  tfunc->rcv_error_bytes.hi,
		  estats->total_bytes_received_lo,
		  tfunc->rcv_error_bytes.lo);

	ADD_64_LE(estats->error_bytes_received_hi,
		  tfunc->rcv_error_bytes.hi,
		  estats->error_bytes_received_lo,
		  tfunc->rcv_error_bytes.lo);

	UPDATE_ESTAT(etherstatsoverrsizepkts, rx_stat_dot3statsframestoolong);

	ADD_64(estats->error_bytes_received_hi,
	       estats->rx_stat_ifhcinbadoctets_hi,
	       estats->error_bytes_received_lo,
	       estats->rx_stat_ifhcinbadoctets_lo);

	if (bp->port.pmf) {
		struct bnx2x_fw_port_stats_old *fwstats = &bp->fw_stats_old;
		UPDATE_FW_STAT(mac_filter_discard);
		UPDATE_FW_STAT(mf_tag_discard);
		UPDATE_FW_STAT(brb_truncate_discard);
		UPDATE_FW_STAT(mac_discard);
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
	for_each_rx_queue(bp, i) {
		struct tstorm_per_queue_stats *old_tclient =
			&bp->fp_stats[i].old_tclient;
		tmp += le32_to_cpu(old_tclient->checksum_discard);
	}
	nstats->rx_dropped = tmp + bp->net_stats_old.rx_dropped;

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
	nstats->rx_missed_errors = 0;

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

	for_each_queue(bp, i) {
		struct bnx2x_eth_q_stats *qstats = &bp->fp_stats[i].eth_q_stats;
		struct bnx2x_eth_q_stats_old *qstats_old =
			&bp->fp_stats[i].eth_q_stats_old;

		UPDATE_ESTAT_QSTAT(driver_xoff);
		UPDATE_ESTAT_QSTAT(rx_err_discard_pkt);
		UPDATE_ESTAT_QSTAT(rx_skb_alloc_failed);
		UPDATE_ESTAT_QSTAT(hw_csum_err);
		UPDATE_ESTAT_QSTAT(driver_filtered_tx_pkt);
	}
}

static bool bnx2x_edebug_stats_stopped(struct bnx2x *bp)
{
	u32 val;

	if (SHMEM2_HAS(bp, edebug_driver_if[1])) {
		val = SHMEM2_RD(bp, edebug_driver_if[1]);

		if (val == EDEBUG_DRIVER_IF_OP_CODE_DISABLE_STAT)
			return true;
	}

	return false;
}

static void bnx2x_stats_update(struct bnx2x *bp)
{
	u32 *stats_comp = bnx2x_sp(bp, stats_comp);

	/* we run update from timer context, so give up
	 * if somebody is in the middle of transition
	 */
	if (down_trylock(&bp->stats_sema))
		return;

	if (bnx2x_edebug_stats_stopped(bp) || !bp->stats_started)
		goto out;

	if (IS_PF(bp)) {
		if (*stats_comp != DMAE_COMP_VAL)
			goto out;

		if (bp->port.pmf)
			bnx2x_hw_stats_update(bp);

		if (bnx2x_storm_stats_update(bp)) {
			if (bp->stats_pending++ == 3) {
				BNX2X_ERR("storm stats were not updated for 3 times\n");
				bnx2x_panic();
			}
			goto out;
		}
	} else {
		/* vf doesn't collect HW statistics, and doesn't get completions
		 * perform only update
		 */
		bnx2x_storm_stats_update(bp);
	}

	bnx2x_net_stats_update(bp);
	bnx2x_drv_stats_update(bp);

	/* vf is done */
	if (IS_VF(bp))
		goto out;

	if (netif_msg_timer(bp)) {
		struct bnx2x_eth_stats *estats = &bp->eth_stats;

		netdev_dbg(bp->dev, "brb drops %u  brb truncate %u\n",
		       estats->brb_drop_lo, estats->brb_truncate_lo);
	}

	bnx2x_hw_stats_post(bp);
	bnx2x_storm_stats_post(bp);

out:
	up(&bp->stats_sema);
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
		dmae->len = bnx2x_get_port_stats_dma_len(bp);
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

	if (down_timeout(&bp->stats_sema, HZ/10))
		BNX2X_ERR("Unable to acquire stats lock\n");

	bp->stats_started = false;

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

	up(&bp->stats_sema);
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
	void (*action)(struct bnx2x *bp);
	if (unlikely(bp->panic))
		return;

	spin_lock_bh(&bp->stats_lock);
	state = bp->stats_state;
	bp->stats_state = bnx2x_stats_stm[state][event].next_state;
	action = bnx2x_stats_stm[state][event].action;
	spin_unlock_bh(&bp->stats_lock);

	action(bp);

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
	dmae->len = bnx2x_get_port_stats_dma_len(bp);
	dmae->comp_addr_lo = U64_LO(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_addr_hi = U64_HI(bnx2x_sp_mapping(bp, stats_comp));
	dmae->comp_val = DMAE_COMP_VAL;

	*stats_comp = 0;
	bnx2x_hw_stats_post(bp);
	bnx2x_stats_comp(bp);
}

/* This function will prepare the statistics ramrod data the way
 * we will only have to increment the statistics counter and
 * send the ramrod each time we have to.
 */
static void bnx2x_prep_fw_stats_req(struct bnx2x *bp)
{
	int i;
	int first_queue_query_index;
	struct stats_query_header *stats_hdr = &bp->fw_stats_req->hdr;

	dma_addr_t cur_data_offset;
	struct stats_query_entry *cur_query_entry;

	stats_hdr->cmd_num = bp->fw_stats_num;
	stats_hdr->drv_stats_counter = 0;

	/* storm_counters struct contains the counters of completed
	 * statistics requests per storm which are incremented by FW
	 * each time it completes hadning a statistics ramrod. We will
	 * check these counters in the timer handler and discard a
	 * (statistics) ramrod completion.
	 */
	cur_data_offset = bp->fw_stats_data_mapping +
		offsetof(struct bnx2x_fw_stats_data, storm_counters);

	stats_hdr->stats_counters_addrs.hi =
		cpu_to_le32(U64_HI(cur_data_offset));
	stats_hdr->stats_counters_addrs.lo =
		cpu_to_le32(U64_LO(cur_data_offset));

	/* prepare to the first stats ramrod (will be completed with
	 * the counters equal to zero) - init counters to somethig different.
	 */
	memset(&bp->fw_stats_data->storm_counters, 0xff,
	       sizeof(struct stats_counter));

	/**** Port FW statistics data ****/
	cur_data_offset = bp->fw_stats_data_mapping +
		offsetof(struct bnx2x_fw_stats_data, port);

	cur_query_entry = &bp->fw_stats_req->query[BNX2X_PORT_QUERY_IDX];

	cur_query_entry->kind = STATS_TYPE_PORT;
	/* For port query index is a DONT CARE */
	cur_query_entry->index = BP_PORT(bp);
	/* For port query funcID is a DONT CARE */
	cur_query_entry->funcID = cpu_to_le16(BP_FUNC(bp));
	cur_query_entry->address.hi = cpu_to_le32(U64_HI(cur_data_offset));
	cur_query_entry->address.lo = cpu_to_le32(U64_LO(cur_data_offset));

	/**** PF FW statistics data ****/
	cur_data_offset = bp->fw_stats_data_mapping +
		offsetof(struct bnx2x_fw_stats_data, pf);

	cur_query_entry = &bp->fw_stats_req->query[BNX2X_PF_QUERY_IDX];

	cur_query_entry->kind = STATS_TYPE_PF;
	/* For PF query index is a DONT CARE */
	cur_query_entry->index = BP_PORT(bp);
	cur_query_entry->funcID = cpu_to_le16(BP_FUNC(bp));
	cur_query_entry->address.hi = cpu_to_le32(U64_HI(cur_data_offset));
	cur_query_entry->address.lo = cpu_to_le32(U64_LO(cur_data_offset));

	/**** FCoE FW statistics data ****/
	if (!NO_FCOE(bp)) {
		cur_data_offset = bp->fw_stats_data_mapping +
			offsetof(struct bnx2x_fw_stats_data, fcoe);

		cur_query_entry =
			&bp->fw_stats_req->query[BNX2X_FCOE_QUERY_IDX];

		cur_query_entry->kind = STATS_TYPE_FCOE;
		/* For FCoE query index is a DONT CARE */
		cur_query_entry->index = BP_PORT(bp);
		cur_query_entry->funcID = cpu_to_le16(BP_FUNC(bp));
		cur_query_entry->address.hi =
			cpu_to_le32(U64_HI(cur_data_offset));
		cur_query_entry->address.lo =
			cpu_to_le32(U64_LO(cur_data_offset));
	}

	/**** Clients' queries ****/
	cur_data_offset = bp->fw_stats_data_mapping +
		offsetof(struct bnx2x_fw_stats_data, queue_stats);

	/* first queue query index depends whether FCoE offloaded request will
	 * be included in the ramrod
	 */
	if (!NO_FCOE(bp))
		first_queue_query_index = BNX2X_FIRST_QUEUE_QUERY_IDX;
	else
		first_queue_query_index = BNX2X_FIRST_QUEUE_QUERY_IDX - 1;

	for_each_eth_queue(bp, i) {
		cur_query_entry =
			&bp->fw_stats_req->
					query[first_queue_query_index + i];

		cur_query_entry->kind = STATS_TYPE_QUEUE;
		cur_query_entry->index = bnx2x_stats_id(&bp->fp[i]);
		cur_query_entry->funcID = cpu_to_le16(BP_FUNC(bp));
		cur_query_entry->address.hi =
			cpu_to_le32(U64_HI(cur_data_offset));
		cur_query_entry->address.lo =
			cpu_to_le32(U64_LO(cur_data_offset));

		cur_data_offset += sizeof(struct per_queue_stats);
	}

	/* add FCoE queue query if needed */
	if (!NO_FCOE(bp)) {
		cur_query_entry =
			&bp->fw_stats_req->
					query[first_queue_query_index + i];

		cur_query_entry->kind = STATS_TYPE_QUEUE;
		cur_query_entry->index = bnx2x_stats_id(&bp->fp[FCOE_IDX(bp)]);
		cur_query_entry->funcID = cpu_to_le16(BP_FUNC(bp));
		cur_query_entry->address.hi =
			cpu_to_le32(U64_HI(cur_data_offset));
		cur_query_entry->address.lo =
			cpu_to_le32(U64_LO(cur_data_offset));
	}
}

void bnx2x_memset_stats(struct bnx2x *bp)
{
	int i;

	/* function stats */
	for_each_queue(bp, i) {
		struct bnx2x_fp_stats *fp_stats = &bp->fp_stats[i];

		memset(&fp_stats->old_tclient, 0,
		       sizeof(fp_stats->old_tclient));
		memset(&fp_stats->old_uclient, 0,
		       sizeof(fp_stats->old_uclient));
		memset(&fp_stats->old_xclient, 0,
		       sizeof(fp_stats->old_xclient));
		if (bp->stats_init) {
			memset(&fp_stats->eth_q_stats, 0,
			       sizeof(fp_stats->eth_q_stats));
			memset(&fp_stats->eth_q_stats_old, 0,
			       sizeof(fp_stats->eth_q_stats_old));
		}
	}

	memset(&bp->dev->stats, 0, sizeof(bp->dev->stats));

	if (bp->stats_init) {
		memset(&bp->net_stats_old, 0, sizeof(bp->net_stats_old));
		memset(&bp->fw_stats_old, 0, sizeof(bp->fw_stats_old));
		memset(&bp->eth_stats_old, 0, sizeof(bp->eth_stats_old));
		memset(&bp->eth_stats, 0, sizeof(bp->eth_stats));
		memset(&bp->func_stats, 0, sizeof(bp->func_stats));
	}

	bp->stats_state = STATS_STATE_DISABLED;

	if (bp->port.pmf && bp->port.port_stx)
		bnx2x_port_stats_base_init(bp);

	/* mark the end of statistics initializiation */
	bp->stats_init = false;
}

void bnx2x_stats_init(struct bnx2x *bp)
{
	int /*abs*/port = BP_PORT(bp);
	int mb_idx = BP_FW_MB_IDX(bp);

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

	/* pmf should retrieve port statistics from SP on a non-init*/
	if (!bp->stats_init && bp->port.pmf && bp->port.port_stx)
		bnx2x_stats_handle(bp, STATS_EVENT_PMF);

	port = BP_PORT(bp);
	/* port stats */
	memset(&(bp->port.old_nig_stats), 0, sizeof(struct nig_stats));
	bp->port.old_nig_stats.brb_discard =
			REG_RD(bp, NIG_REG_STAT0_BRB_DISCARD + port*0x38);
	bp->port.old_nig_stats.brb_truncate =
			REG_RD(bp, NIG_REG_STAT0_BRB_TRUNCATE + port*0x38);
	if (!CHIP_IS_E3(bp)) {
		REG_RD_DMAE(bp, NIG_REG_STAT0_EGRESS_MAC_PKT0 + port*0x50,
			    &(bp->port.old_nig_stats.egress_mac_pkt0_lo), 2);
		REG_RD_DMAE(bp, NIG_REG_STAT0_EGRESS_MAC_PKT1 + port*0x50,
			    &(bp->port.old_nig_stats.egress_mac_pkt1_lo), 2);
	}

	/* Prepare statistics ramrod data */
	bnx2x_prep_fw_stats_req(bp);

	/* Clean SP from previous statistics */
	if (bp->stats_init) {
		if (bp->func_stx) {
			memset(bnx2x_sp(bp, func_stats), 0,
			       sizeof(struct host_func_stats));
			bnx2x_func_stats_init(bp);
			bnx2x_hw_stats_post(bp);
			bnx2x_stats_comp(bp);
		}
	}

	bnx2x_memset_stats(bp);
}

void bnx2x_save_statistics(struct bnx2x *bp)
{
	int i;
	struct net_device_stats *nstats = &bp->dev->stats;

	/* save queue statistics */
	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		struct bnx2x_eth_q_stats *qstats =
			&bnx2x_fp_stats(bp, fp)->eth_q_stats;
		struct bnx2x_eth_q_stats_old *qstats_old =
			&bnx2x_fp_stats(bp, fp)->eth_q_stats_old;

		UPDATE_QSTAT_OLD(total_unicast_bytes_received_hi);
		UPDATE_QSTAT_OLD(total_unicast_bytes_received_lo);
		UPDATE_QSTAT_OLD(total_broadcast_bytes_received_hi);
		UPDATE_QSTAT_OLD(total_broadcast_bytes_received_lo);
		UPDATE_QSTAT_OLD(total_multicast_bytes_received_hi);
		UPDATE_QSTAT_OLD(total_multicast_bytes_received_lo);
		UPDATE_QSTAT_OLD(total_unicast_bytes_transmitted_hi);
		UPDATE_QSTAT_OLD(total_unicast_bytes_transmitted_lo);
		UPDATE_QSTAT_OLD(total_broadcast_bytes_transmitted_hi);
		UPDATE_QSTAT_OLD(total_broadcast_bytes_transmitted_lo);
		UPDATE_QSTAT_OLD(total_multicast_bytes_transmitted_hi);
		UPDATE_QSTAT_OLD(total_multicast_bytes_transmitted_lo);
		UPDATE_QSTAT_OLD(total_tpa_bytes_hi);
		UPDATE_QSTAT_OLD(total_tpa_bytes_lo);
	}

	/* save net_device_stats statistics */
	bp->net_stats_old.rx_dropped = nstats->rx_dropped;

	/* store port firmware statistics */
	if (bp->port.pmf && IS_MF(bp)) {
		struct bnx2x_eth_stats *estats = &bp->eth_stats;
		struct bnx2x_fw_port_stats_old *fwstats = &bp->fw_stats_old;
		UPDATE_FW_STAT_OLD(mac_filter_discard);
		UPDATE_FW_STAT_OLD(mf_tag_discard);
		UPDATE_FW_STAT_OLD(brb_truncate_discard);
		UPDATE_FW_STAT_OLD(mac_discard);
	}
}

void bnx2x_afex_collect_stats(struct bnx2x *bp, void *void_afex_stats,
			      u32 stats_type)
{
	int i;
	struct afex_stats *afex_stats = (struct afex_stats *)void_afex_stats;
	struct bnx2x_eth_stats *estats = &bp->eth_stats;
	struct per_queue_stats *fcoe_q_stats =
		&bp->fw_stats_data->queue_stats[FCOE_IDX(bp)];

	struct tstorm_per_queue_stats *fcoe_q_tstorm_stats =
		&fcoe_q_stats->tstorm_queue_statistics;

	struct ustorm_per_queue_stats *fcoe_q_ustorm_stats =
		&fcoe_q_stats->ustorm_queue_statistics;

	struct xstorm_per_queue_stats *fcoe_q_xstorm_stats =
		&fcoe_q_stats->xstorm_queue_statistics;

	struct fcoe_statistics_params *fw_fcoe_stat =
		&bp->fw_stats_data->fcoe;

	memset(afex_stats, 0, sizeof(struct afex_stats));

	for_each_eth_queue(bp, i) {
		struct bnx2x_eth_q_stats *qstats = &bp->fp_stats[i].eth_q_stats;

		ADD_64(afex_stats->rx_unicast_bytes_hi,
		       qstats->total_unicast_bytes_received_hi,
		       afex_stats->rx_unicast_bytes_lo,
		       qstats->total_unicast_bytes_received_lo);

		ADD_64(afex_stats->rx_broadcast_bytes_hi,
		       qstats->total_broadcast_bytes_received_hi,
		       afex_stats->rx_broadcast_bytes_lo,
		       qstats->total_broadcast_bytes_received_lo);

		ADD_64(afex_stats->rx_multicast_bytes_hi,
		       qstats->total_multicast_bytes_received_hi,
		       afex_stats->rx_multicast_bytes_lo,
		       qstats->total_multicast_bytes_received_lo);

		ADD_64(afex_stats->rx_unicast_frames_hi,
		       qstats->total_unicast_packets_received_hi,
		       afex_stats->rx_unicast_frames_lo,
		       qstats->total_unicast_packets_received_lo);

		ADD_64(afex_stats->rx_broadcast_frames_hi,
		       qstats->total_broadcast_packets_received_hi,
		       afex_stats->rx_broadcast_frames_lo,
		       qstats->total_broadcast_packets_received_lo);

		ADD_64(afex_stats->rx_multicast_frames_hi,
		       qstats->total_multicast_packets_received_hi,
		       afex_stats->rx_multicast_frames_lo,
		       qstats->total_multicast_packets_received_lo);

		/* sum to rx_frames_discarded all discraded
		 * packets due to size, ttl0 and checksum
		 */
		ADD_64(afex_stats->rx_frames_discarded_hi,
		       qstats->total_packets_received_checksum_discarded_hi,
		       afex_stats->rx_frames_discarded_lo,
		       qstats->total_packets_received_checksum_discarded_lo);

		ADD_64(afex_stats->rx_frames_discarded_hi,
		       qstats->total_packets_received_ttl0_discarded_hi,
		       afex_stats->rx_frames_discarded_lo,
		       qstats->total_packets_received_ttl0_discarded_lo);

		ADD_64(afex_stats->rx_frames_discarded_hi,
		       qstats->etherstatsoverrsizepkts_hi,
		       afex_stats->rx_frames_discarded_lo,
		       qstats->etherstatsoverrsizepkts_lo);

		ADD_64(afex_stats->rx_frames_dropped_hi,
		       qstats->no_buff_discard_hi,
		       afex_stats->rx_frames_dropped_lo,
		       qstats->no_buff_discard_lo);

		ADD_64(afex_stats->tx_unicast_bytes_hi,
		       qstats->total_unicast_bytes_transmitted_hi,
		       afex_stats->tx_unicast_bytes_lo,
		       qstats->total_unicast_bytes_transmitted_lo);

		ADD_64(afex_stats->tx_broadcast_bytes_hi,
		       qstats->total_broadcast_bytes_transmitted_hi,
		       afex_stats->tx_broadcast_bytes_lo,
		       qstats->total_broadcast_bytes_transmitted_lo);

		ADD_64(afex_stats->tx_multicast_bytes_hi,
		       qstats->total_multicast_bytes_transmitted_hi,
		       afex_stats->tx_multicast_bytes_lo,
		       qstats->total_multicast_bytes_transmitted_lo);

		ADD_64(afex_stats->tx_unicast_frames_hi,
		       qstats->total_unicast_packets_transmitted_hi,
		       afex_stats->tx_unicast_frames_lo,
		       qstats->total_unicast_packets_transmitted_lo);

		ADD_64(afex_stats->tx_broadcast_frames_hi,
		       qstats->total_broadcast_packets_transmitted_hi,
		       afex_stats->tx_broadcast_frames_lo,
		       qstats->total_broadcast_packets_transmitted_lo);

		ADD_64(afex_stats->tx_multicast_frames_hi,
		       qstats->total_multicast_packets_transmitted_hi,
		       afex_stats->tx_multicast_frames_lo,
		       qstats->total_multicast_packets_transmitted_lo);

		ADD_64(afex_stats->tx_frames_dropped_hi,
		       qstats->total_transmitted_dropped_packets_error_hi,
		       afex_stats->tx_frames_dropped_lo,
		       qstats->total_transmitted_dropped_packets_error_lo);
	}

	/* now add FCoE statistics which are collected separately
	 * (both offloaded and non offloaded)
	 */
	if (!NO_FCOE(bp)) {
		ADD_64_LE(afex_stats->rx_unicast_bytes_hi,
			  LE32_0,
			  afex_stats->rx_unicast_bytes_lo,
			  fw_fcoe_stat->rx_stat0.fcoe_rx_byte_cnt);

		ADD_64_LE(afex_stats->rx_unicast_bytes_hi,
			  fcoe_q_tstorm_stats->rcv_ucast_bytes.hi,
			  afex_stats->rx_unicast_bytes_lo,
			  fcoe_q_tstorm_stats->rcv_ucast_bytes.lo);

		ADD_64_LE(afex_stats->rx_broadcast_bytes_hi,
			  fcoe_q_tstorm_stats->rcv_bcast_bytes.hi,
			  afex_stats->rx_broadcast_bytes_lo,
			  fcoe_q_tstorm_stats->rcv_bcast_bytes.lo);

		ADD_64_LE(afex_stats->rx_multicast_bytes_hi,
			  fcoe_q_tstorm_stats->rcv_mcast_bytes.hi,
			  afex_stats->rx_multicast_bytes_lo,
			  fcoe_q_tstorm_stats->rcv_mcast_bytes.lo);

		ADD_64_LE(afex_stats->rx_unicast_frames_hi,
			  LE32_0,
			  afex_stats->rx_unicast_frames_lo,
			  fw_fcoe_stat->rx_stat0.fcoe_rx_pkt_cnt);

		ADD_64_LE(afex_stats->rx_unicast_frames_hi,
			  LE32_0,
			  afex_stats->rx_unicast_frames_lo,
			  fcoe_q_tstorm_stats->rcv_ucast_pkts);

		ADD_64_LE(afex_stats->rx_broadcast_frames_hi,
			  LE32_0,
			  afex_stats->rx_broadcast_frames_lo,
			  fcoe_q_tstorm_stats->rcv_bcast_pkts);

		ADD_64_LE(afex_stats->rx_multicast_frames_hi,
			  LE32_0,
			  afex_stats->rx_multicast_frames_lo,
			  fcoe_q_tstorm_stats->rcv_ucast_pkts);

		ADD_64_LE(afex_stats->rx_frames_discarded_hi,
			  LE32_0,
			  afex_stats->rx_frames_discarded_lo,
			  fcoe_q_tstorm_stats->checksum_discard);

		ADD_64_LE(afex_stats->rx_frames_discarded_hi,
			  LE32_0,
			  afex_stats->rx_frames_discarded_lo,
			  fcoe_q_tstorm_stats->pkts_too_big_discard);

		ADD_64_LE(afex_stats->rx_frames_discarded_hi,
			  LE32_0,
			  afex_stats->rx_frames_discarded_lo,
			  fcoe_q_tstorm_stats->ttl0_discard);

		ADD_64_LE16(afex_stats->rx_frames_dropped_hi,
			    LE16_0,
			    afex_stats->rx_frames_dropped_lo,
			    fcoe_q_tstorm_stats->no_buff_discard);

		ADD_64_LE(afex_stats->rx_frames_dropped_hi,
			  LE32_0,
			  afex_stats->rx_frames_dropped_lo,
			  fcoe_q_ustorm_stats->ucast_no_buff_pkts);

		ADD_64_LE(afex_stats->rx_frames_dropped_hi,
			  LE32_0,
			  afex_stats->rx_frames_dropped_lo,
			  fcoe_q_ustorm_stats->mcast_no_buff_pkts);

		ADD_64_LE(afex_stats->rx_frames_dropped_hi,
			  LE32_0,
			  afex_stats->rx_frames_dropped_lo,
			  fcoe_q_ustorm_stats->bcast_no_buff_pkts);

		ADD_64_LE(afex_stats->rx_frames_dropped_hi,
			  LE32_0,
			  afex_stats->rx_frames_dropped_lo,
			  fw_fcoe_stat->rx_stat1.fcoe_rx_drop_pkt_cnt);

		ADD_64_LE(afex_stats->rx_frames_dropped_hi,
			  LE32_0,
			  afex_stats->rx_frames_dropped_lo,
			  fw_fcoe_stat->rx_stat2.fcoe_rx_drop_pkt_cnt);

		ADD_64_LE(afex_stats->tx_unicast_bytes_hi,
			  LE32_0,
			  afex_stats->tx_unicast_bytes_lo,
			  fw_fcoe_stat->tx_stat.fcoe_tx_byte_cnt);

		ADD_64_LE(afex_stats->tx_unicast_bytes_hi,
			  fcoe_q_xstorm_stats->ucast_bytes_sent.hi,
			  afex_stats->tx_unicast_bytes_lo,
			  fcoe_q_xstorm_stats->ucast_bytes_sent.lo);

		ADD_64_LE(afex_stats->tx_broadcast_bytes_hi,
			  fcoe_q_xstorm_stats->bcast_bytes_sent.hi,
			  afex_stats->tx_broadcast_bytes_lo,
			  fcoe_q_xstorm_stats->bcast_bytes_sent.lo);

		ADD_64_LE(afex_stats->tx_multicast_bytes_hi,
			  fcoe_q_xstorm_stats->mcast_bytes_sent.hi,
			  afex_stats->tx_multicast_bytes_lo,
			  fcoe_q_xstorm_stats->mcast_bytes_sent.lo);

		ADD_64_LE(afex_stats->tx_unicast_frames_hi,
			  LE32_0,
			  afex_stats->tx_unicast_frames_lo,
			  fw_fcoe_stat->tx_stat.fcoe_tx_pkt_cnt);

		ADD_64_LE(afex_stats->tx_unicast_frames_hi,
			  LE32_0,
			  afex_stats->tx_unicast_frames_lo,
			  fcoe_q_xstorm_stats->ucast_pkts_sent);

		ADD_64_LE(afex_stats->tx_broadcast_frames_hi,
			  LE32_0,
			  afex_stats->tx_broadcast_frames_lo,
			  fcoe_q_xstorm_stats->bcast_pkts_sent);

		ADD_64_LE(afex_stats->tx_multicast_frames_hi,
			  LE32_0,
			  afex_stats->tx_multicast_frames_lo,
			  fcoe_q_xstorm_stats->mcast_pkts_sent);

		ADD_64_LE(afex_stats->tx_frames_dropped_hi,
			  LE32_0,
			  afex_stats->tx_frames_dropped_lo,
			  fcoe_q_xstorm_stats->error_drop_pkts);
	}

	/* if port stats are requested, add them to the PMF
	 * stats, as anyway they will be accumulated by the
	 * MCP before sent to the switch
	 */
	if ((bp->port.pmf) && (stats_type == VICSTATST_UIF_INDEX)) {
		ADD_64(afex_stats->rx_frames_dropped_hi,
		       0,
		       afex_stats->rx_frames_dropped_lo,
		       estats->mac_filter_discard);
		ADD_64(afex_stats->rx_frames_dropped_hi,
		       0,
		       afex_stats->rx_frames_dropped_lo,
		       estats->brb_truncate_discard);
		ADD_64(afex_stats->rx_frames_discarded_hi,
		       0,
		       afex_stats->rx_frames_discarded_lo,
		       estats->mac_discard);
	}
}
