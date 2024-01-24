/*
 * TC956X ethernet driver.
 *
 * tc956xmac_config.h - Configuration Helpers
 *
 * Copyright (C) 2019 Synopsys, Inc. and/or its affiliates.
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Synopsys Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

#ifndef __TC956XMAC_CONFIG_H__
#define __TC956XMAC_CONFIG_H__

#define TC956XMAC_CFG(__cfg)		SCFG_TC956XMAC_##__cfg
#define TC956XMAC_EN(__cfg)		IS_ENABLED(TC956XMAC_CFG(__cfg))
#define ___TC956XMAC_TXQ(__p, __cfg)	SCFG_TC956XMAC_TXQ##__p##__cfg
#define ___TC956XMAC_RXQ(__p, __cfg)	SCFG_TC956XMAC_RXQ##__p##__cfg
#define __TC956XMAC_TXQ(__n, __cfg)	___TC956XMAC_TXQ(__n, _##__cfg)
#define __TC956XMAC_RXQ(__n, __cfg)	___TC956XMAC_RXQ(__n, _##__cfg)
#define TC956XMAC_TXQ(__n, __cfg)		__TC956XMAC_TXQ(__n, __cfg)
#define TC956XMAC_RXQ(__n, __cfg)		__TC956XMAC_RXQ(__n, __cfg)

#ifndef SCFG_TC956XMAC
static inline void tc956xmac_config_data(struct plat_tc956xmacenet_data *plat)
{
	/* Nothing to do */
}
#else /* !SCFG_TC956XMAC */
static inline void tc956xmac_config_data(struct plat_tc956xmacenet_data *plat)
{
	plat->force_sf_dma_mode = TC956XMAC_EN(MTL_SF);
	plat->force_thresh_dma_mode = TC956XMAC_EN(MTL_THR);
	plat->tso_en = TC956XMAC_EN(TSO);
	plat->rss_en = TC956XMAC_EN(RSS);
	plat->sph_en = TC956XMAC_EN(SPH);
	plat->pmt = TC956XMAC_EN(PMT);
	plat->riwt_off = !TC956XMAC_EN(RIWT);

#ifdef SCFG_TC956XMAC_PTP_RATE
	plat->clk_ptp_rate = TC956XMAC_CFG(PTP_RATE);
	plat->clk_ref_rate = TC956XMAC_CFG(PTP_RATE);
#endif

#ifdef SCFG_TC956XMAC_TXPBL
	plat->dma_cfg->txpbl = TC956XMAC_CFG(TXPBL);
#endif
#ifdef SCFG_TC956XMAC_RXPBL
	plat->dma_cfg->rxpbl = TC956XMAC_CFG(RXPBL);
#endif
	plat->dma_cfg->pblx8 = TC956XMAC_EN(PBLX8);
	plat->dma_cfg->aal = TC956XMAC_EN(AXI_AAL);

	if (plat->axi) {
#ifdef SCFG_TC956XMAC_AXI_WR
		plat->axi->axi_wr_osr_lmt = TC956XMAC_CFG(AXI_WR);
#endif
#ifdef SCFG_TC956XMAC_AXI_RD
		plat->axi->axi_rd_osr_lmt = TC956XMAC_CFG(AXI_RD);
#endif
	}

	/* TX Scheduling Algorithm */
	if (TC956XMAC_EN(TX_SP))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_SP;
	else if (TC956XMAC_EN(TX_WRR))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WRR;
	else if (TC956XMAC_EN(TX_WFQ))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_WFQ;
	else if (TC956XMAC_EN(TX_DWRR))
		plat->tx_sched_algorithm = MTL_TX_ALGORITHM_DWRR;

	/* RX Scheduling Algorithm */
	if (TC956XMAC_EN(RX_SP))
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_SP;
	else if (TC956XMAC_EN(RX_WSP))
		plat->rx_sched_algorithm = MTL_RX_ALGORITHM_WSP;

#ifdef SCFG_TC956XMAC_TXQ
	plat->tx_queues_to_use = TC956XMAC_CFG(TXQ);
#endif
#ifdef SCFG_TC956XMAC_RXQ
	plat->rx_queues_to_use = TC956XMAC_CFG(RXQ);
#endif

	/* TX Queue 0 */
#ifdef SCFG_TC956XMAC_TXQ0_DCB
	plat->tx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ0_AVB
	plat->tx_queues_cfg[0].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[0].idle_slope = TC956XMAC_TXQ(0, IDLESLOPE);
	plat->tx_queues_cfg[0].send_slope = TC956XMAC_TXQ(0, SENDSLOPE);
	plat->tx_queues_cfg[0].high_credit = TC956XMAC_TXQ(0, HICREDIT);
	plat->tx_queues_cfg[0].low_credit = TC956XMAC_TXQ(0, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ0_WEIGHT
	plat->tx_queues_cfg[0].weight = TC956XMAC_TXQ(0, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ0_PRIO
	plat->tx_queues_cfg[0].use_prio = true;
	plat->tx_queues_cfg[0].prio = TC956XMAC_TXQ(0, PRIO);
#endif
	plat->tx_queues_cfg[0].tbs_en = IS_ENABLED(TC956XMAC_TXQ(0, TBS));

	/* RX Queue 0 */
#ifdef SCFG_TC956XMAC_RXQ0_DCB
	plat->rx_queues_cfg[0].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ0_AVB
	plat->rx_queues_cfg[0].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ0_CHANNEL
	plat->rx_queues_cfg[0].chan = TC956XMAC_RXQ(0, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ0_PRIO
	plat->rx_queues_cfg[0].use_prio = true;
	plat->rx_queues_cfg[0].prio = TC956XMAC_RXQ(0, PRIO);
#endif

	/* TX Queue 1 */
#ifdef SCFG_TC956XMAC_TXQ1_DCB
	plat->tx_queues_cfg[1].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ1_AVB
	plat->tx_queues_cfg[1].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[1].idle_slope = TC956XMAC_TXQ(1, IDLESLOPE);
	plat->tx_queues_cfg[1].send_slope = TC956XMAC_TXQ(1, SENDSLOPE);
	plat->tx_queues_cfg[1].high_credit = TC956XMAC_TXQ(1, HICREDIT);
	plat->tx_queues_cfg[1].low_credit = TC956XMAC_TXQ(1, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ1_WEIGHT
	plat->tx_queues_cfg[1].weight = TC956XMAC_TXQ(1, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ1_PRIO
	plat->tx_queues_cfg[1].use_prio = true;
	plat->tx_queues_cfg[1].prio = TC956XMAC_TXQ(1, PRIO);
#endif
	plat->tx_queues_cfg[1].tbs_en = IS_ENABLED(TC956XMAC_TXQ(1, TBS));

	/* RX Queue 1 */
#ifdef SCFG_TC956XMAC_RXQ1_DCB
	plat->rx_queues_cfg[1].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ1_AVB
	plat->rx_queues_cfg[1].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ1_CHANNEL
	plat->rx_queues_cfg[1].chan = TC956XMAC_RXQ(1, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ1_PRIO
	plat->rx_queues_cfg[1].use_prio = true;
	plat->rx_queues_cfg[1].prio = TC956XMAC_RXQ(1, PRIO);
#endif

	/* TX Queue 2 */
#ifdef SCFG_TC956XMAC_TXQ2_DCB
	plat->tx_queues_cfg[2].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ2_AVB
	plat->tx_queues_cfg[2].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[2].idle_slope = TC956XMAC_TXQ(2, IDLESLOPE);
	plat->tx_queues_cfg[2].send_slope = TC956XMAC_TXQ(2, SENDSLOPE);
	plat->tx_queues_cfg[2].high_credit = TC956XMAC_TXQ(2, HICREDIT);
	plat->tx_queues_cfg[2].low_credit = TC956XMAC_TXQ(2, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ2_WEIGHT
	plat->tx_queues_cfg[2].weight = TC956XMAC_TXQ(2, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ2_PRIO
	plat->tx_queues_cfg[2].use_prio = true;
	plat->tx_queues_cfg[2].prio = TC956XMAC_TXQ(2, PRIO);
#endif
	plat->tx_queues_cfg[2].tbs_en = IS_ENABLED(TC956XMAC_TXQ(2, TBS));

	/* RX Queue 2 */
#ifdef SCFG_TC956XMAC_RXQ2_DCB
	plat->rx_queues_cfg[2].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ2_AVB
	plat->rx_queues_cfg[2].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ2_CHANNEL
	plat->rx_queues_cfg[2].chan = TC956XMAC_RXQ(2, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ2_PRIO
	plat->rx_queues_cfg[2].use_prio = true;
	plat->rx_queues_cfg[2].prio = TC956XMAC_RXQ(2, PRIO);
#endif

	/* TX Queue 3 */
#ifdef SCFG_TC956XMAC_TXQ3_DCB
	plat->tx_queues_cfg[3].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ3_AVB
	plat->tx_queues_cfg[3].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[3].idle_slope = TC956XMAC_TXQ(3, IDLESLOPE);
	plat->tx_queues_cfg[3].send_slope = TC956XMAC_TXQ(3, SENDSLOPE);
	plat->tx_queues_cfg[3].high_credit = TC956XMAC_TXQ(3, HICREDIT);
	plat->tx_queues_cfg[3].low_credit = TC956XMAC_TXQ(3, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ3_WEIGHT
	plat->tx_queues_cfg[3].weight = TC956XMAC_TXQ(3, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ3_PRIO
	plat->tx_queues_cfg[3].use_prio = true;
	plat->tx_queues_cfg[3].prio = TC956XMAC_TXQ(3, PRIO);
#endif
	plat->tx_queues_cfg[3].tbs_en = IS_ENABLED(TC956XMAC_TXQ(3, TBS));

	/* RX Queue 3 */
#ifdef SCFG_TC956XMAC_RXQ3_DCB
	plat->rx_queues_cfg[3].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ3_AVB
	plat->rx_queues_cfg[3].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ3_CHANNEL
	plat->rx_queues_cfg[3].chan = TC956XMAC_RXQ(3, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ3_PRIO
	plat->rx_queues_cfg[3].use_prio = true;
	plat->rx_queues_cfg[3].prio = TC956XMAC_RXQ(3, PRIO);
#endif

	/* TX Queue 4 */
#ifdef SCFG_TC956XMAC_TXQ4_DCB
	plat->tx_queues_cfg[4].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ4_AVB
	plat->tx_queues_cfg[4].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[4].idle_slope = TC956XMAC_TXQ(4, IDLESLOPE);
	plat->tx_queues_cfg[4].send_slope = TC956XMAC_TXQ(4, SENDSLOPE);
	plat->tx_queues_cfg[4].high_credit = TC956XMAC_TXQ(4, HICREDIT);
	plat->tx_queues_cfg[4].low_credit = TC956XMAC_TXQ(4, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ4_WEIGHT
	plat->tx_queues_cfg[4].weight = TC956XMAC_TXQ(4, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ4_PRIO
	plat->tx_queues_cfg[4].use_prio = true;
	plat->tx_queues_cfg[4].prio = TC956XMAC_TXQ(4, PRIO);
#endif
	plat->tx_queues_cfg[4].tbs_en = IS_ENABLED(TC956XMAC_TXQ(4, TBS));

	/* RX Queue 4 */
#ifdef SCFG_TC956XMAC_RXQ4_DCB
	plat->rx_queues_cfg[4].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ4_AVB
	plat->rx_queues_cfg[4].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ4_CHANNEL
	plat->rx_queues_cfg[4].chan = TC956XMAC_RXQ(4, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ4_PRIO
	plat->rx_queues_cfg[4].use_prio = true;
	plat->rx_queues_cfg[4].prio = TC956XMAC_RXQ(4, PRIO);
#endif

	/* TX Queue 5 */
#ifdef SCFG_TC956XMAC_TXQ5_DCB
	plat->tx_queues_cfg[5].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ5_AVB
	plat->tx_queues_cfg[5].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[5].idle_slope = TC956XMAC_TXQ(5, IDLESLOPE);
	plat->tx_queues_cfg[5].send_slope = TC956XMAC_TXQ(5, SENDSLOPE);
	plat->tx_queues_cfg[5].high_credit = TC956XMAC_TXQ(5, HICREDIT);
	plat->tx_queues_cfg[5].low_credit = TC956XMAC_TXQ(5, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ5_WEIGHT
	plat->tx_queues_cfg[5].weight = TC956XMAC_TXQ(5, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ5_PRIO
	plat->tx_queues_cfg[5].use_prio = true;
	plat->tx_queues_cfg[5].prio = TC956XMAC_TXQ(5, PRIO);
#endif
	plat->tx_queues_cfg[5].tbs_en = IS_ENABLED(TC956XMAC_TXQ(5, TBS));

	/* RX Queue 5 */
#ifdef SCFG_TC956XMAC_RXQ5_DCB
	plat->rx_queues_cfg[5].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ5_AVB
	plat->rx_queues_cfg[5].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ5_CHANNEL
	plat->rx_queues_cfg[5].chan = TC956XMAC_RXQ(5, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ5_PRIO
	plat->rx_queues_cfg[5].use_prio = true;
	plat->rx_queues_cfg[5].prio = TC956XMAC_RXQ(5, PRIO);
#endif

	/* TX Queue 6 */
#ifdef SCFG_TC956XMAC_TXQ6_DCB
	plat->tx_queues_cfg[6].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ6_AVB
	plat->tx_queues_cfg[6].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[6].idle_slope = TC956XMAC_TXQ(6, IDLESLOPE);
	plat->tx_queues_cfg[6].send_slope = TC956XMAC_TXQ(6, SENDSLOPE);
	plat->tx_queues_cfg[6].high_credit = TC956XMAC_TXQ(6, HICREDIT);
	plat->tx_queues_cfg[6].low_credit = TC956XMAC_TXQ(6, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ6_WEIGHT
	plat->tx_queues_cfg[6].weight = TC956XMAC_TXQ(6, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ6_PRIO
	plat->tx_queues_cfg[6].use_prio = true;
	plat->tx_queues_cfg[6].prio = TC956XMAC_TXQ(6, PRIO);
#endif
	plat->tx_queues_cfg[6].tbs_en = IS_ENABLED(TC956XMAC_TXQ(6, TBS));

	/* RX Queue 6 */
#ifdef SCFG_TC956XMAC_RXQ6_DCB
	plat->rx_queues_cfg[6].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ6_AVB
	plat->rx_queues_cfg[6].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ6_CHANNEL
	plat->rx_queues_cfg[6].chan = TC956XMAC_RXQ(6, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ6_PRIO
	plat->rx_queues_cfg[6].use_prio = true;
	plat->rx_queues_cfg[6].prio = TC956XMAC_RXQ(6, PRIO);
#endif

	/* TX Queue 7 */
#ifdef SCFG_TC956XMAC_TXQ7_DCB
	plat->tx_queues_cfg[7].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_TXQ7_AVB
	plat->tx_queues_cfg[7].mode_to_use = MTL_QUEUE_AVB;
	plat->tx_queues_cfg[7].idle_slope = TC956XMAC_TXQ(7, IDLESLOPE);
	plat->tx_queues_cfg[7].send_slope = TC956XMAC_TXQ(7, SENDSLOPE);
	plat->tx_queues_cfg[7].high_credit = TC956XMAC_TXQ(7, HICREDIT);
	plat->tx_queues_cfg[7].low_credit = TC956XMAC_TXQ(7, LOCREDIT);
#endif

#ifdef SCFG_TC956XMAC_TXQ7_WEIGHT
	plat->tx_queues_cfg[7].weight = TC956XMAC_TXQ(7, WEIGHT);
#endif
#ifdef SCFG_TC956XMAC_TXQ7_PRIO
	plat->tx_queues_cfg[7].use_prio = true;
	plat->tx_queues_cfg[7].prio = TC956XMAC_TXQ(7, PRIO);
#endif
	plat->tx_queues_cfg[7].tbs_en = IS_ENABLED(TC956XMAC_TXQ(7, TBS));

	/* RX Queue 7 */
#ifdef SCFG_TC956XMAC_RXQ7_DCB
	plat->rx_queues_cfg[7].mode_to_use = MTL_QUEUE_DCB;
#endif
#ifdef SCFG_TC956XMAC_RXQ7_AVB
	plat->rx_queues_cfg[7].mode_to_use = MTL_QUEUE_AVB;
#endif

#ifdef SCFG_TC956XMAC_RXQ7_CHANNEL
	plat->rx_queues_cfg[7].chan = TC956XMAC_RXQ(7, CHANNEL);
#endif
#ifdef SCFG_TC956XMAC_RXQ7_PRIO
	plat->rx_queues_cfg[7].use_prio = true;
	plat->rx_queues_cfg[7].prio = TC956XMAC_RXQ(7, PRIO);
#endif
}
#endif /* !SCFG_TC956XMAC */

#endif /* __TC956XMAC_CONFIG_H__ */
