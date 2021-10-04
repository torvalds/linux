/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2019-2021 Marvell International Ltd.
 */

#ifndef _QED_IRO_HSI_H
#define _QED_IRO_HSI_H

#include <linux/types.h>

/* Ystorm flow control mode. Use enum fw_flow_ctrl_mode */
#define YSTORM_FLOW_CONTROL_MODE_OFFSET			(IRO[0].base)
#define YSTORM_FLOW_CONTROL_MODE_SIZE			(IRO[0].size)

/* Tstorm port statistics */
#define TSTORM_PORT_STAT_OFFSET(port_id) \
	(IRO[1].base + ((port_id) * IRO[1].m1))
#define TSTORM_PORT_STAT_SIZE				(IRO[1].size)

/* Tstorm ll2 port statistics */
#define TSTORM_LL2_PORT_STAT_OFFSET(port_id) \
	(IRO[2].base + ((port_id) * IRO[2].m1))
#define TSTORM_LL2_PORT_STAT_SIZE			(IRO[2].size)

/* Ustorm VF-PF Channel ready flag */
#define USTORM_VF_PF_CHANNEL_READY_OFFSET(vf_id) \
	(IRO[3].base + ((vf_id) * IRO[3].m1))
#define USTORM_VF_PF_CHANNEL_READY_SIZE			(IRO[3].size)

/* Ustorm Final flr cleanup ack */
#define USTORM_FLR_FINAL_ACK_OFFSET(pf_id) \
	(IRO[4].base + ((pf_id) * IRO[4].m1))
#define USTORM_FLR_FINAL_ACK_SIZE			(IRO[4].size)

/* Ustorm Event ring consumer */
#define USTORM_EQE_CONS_OFFSET(pf_id) \
	(IRO[5].base + ((pf_id) * IRO[5].m1))
#define USTORM_EQE_CONS_SIZE				(IRO[5].size)

/* Ustorm eth queue zone */
#define USTORM_ETH_QUEUE_ZONE_OFFSET(queue_zone_id) \
	(IRO[6].base + ((queue_zone_id) * IRO[6].m1))
#define USTORM_ETH_QUEUE_ZONE_SIZE			(IRO[6].size)

/* Ustorm Common Queue ring consumer */
#define USTORM_COMMON_QUEUE_CONS_OFFSET(queue_zone_id) \
	(IRO[7].base + ((queue_zone_id) * IRO[7].m1))
#define USTORM_COMMON_QUEUE_CONS_SIZE			(IRO[7].size)

/* Xstorm common PQ info */
#define XSTORM_PQ_INFO_OFFSET(pq_id) \
	(IRO[8].base + ((pq_id) * IRO[8].m1))
#define XSTORM_PQ_INFO_SIZE				(IRO[8].size)

/* Xstorm Integration Test Data */
#define XSTORM_INTEG_TEST_DATA_OFFSET			(IRO[9].base)
#define XSTORM_INTEG_TEST_DATA_SIZE			(IRO[9].size)

/* Ystorm Integration Test Data */
#define YSTORM_INTEG_TEST_DATA_OFFSET			(IRO[10].base)
#define YSTORM_INTEG_TEST_DATA_SIZE			(IRO[10].size)

/* Pstorm Integration Test Data */
#define PSTORM_INTEG_TEST_DATA_OFFSET			(IRO[11].base)
#define PSTORM_INTEG_TEST_DATA_SIZE			(IRO[11].size)

/* Tstorm Integration Test Data */
#define TSTORM_INTEG_TEST_DATA_OFFSET			(IRO[12].base)
#define TSTORM_INTEG_TEST_DATA_SIZE			(IRO[12].size)

/* Mstorm Integration Test Data */
#define MSTORM_INTEG_TEST_DATA_OFFSET			(IRO[13].base)
#define MSTORM_INTEG_TEST_DATA_SIZE			(IRO[13].size)

/* Ustorm Integration Test Data */
#define USTORM_INTEG_TEST_DATA_OFFSET			(IRO[14].base)
#define USTORM_INTEG_TEST_DATA_SIZE			(IRO[14].size)

/* Xstorm overlay buffer host address */
#define XSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[15].base)
#define XSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[15].size)

/* Ystorm overlay buffer host address */
#define YSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[16].base)
#define YSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[16].size)

/* Pstorm overlay buffer host address */
#define PSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[17].base)
#define PSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[17].size)

/* Tstorm overlay buffer host address */
#define TSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[18].base)
#define TSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[18].size)

/* Mstorm overlay buffer host address */
#define MSTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[19].base)
#define MSTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[19].size)

/* Ustorm overlay buffer host address */
#define USTORM_OVERLAY_BUF_ADDR_OFFSET			(IRO[20].base)
#define USTORM_OVERLAY_BUF_ADDR_SIZE			(IRO[20].size)

/* Tstorm producers */
#define TSTORM_LL2_RX_PRODS_OFFSET(core_rx_queue_id) \
	(IRO[21].base + ((core_rx_queue_id) * IRO[21].m1))
#define TSTORM_LL2_RX_PRODS_SIZE			(IRO[21].size)

/* Tstorm LightL2 queue statistics */
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_OFFSET(core_rx_queue_id) \
	(IRO[22].base + ((core_rx_queue_id) * IRO[22].m1))
#define CORE_LL2_TSTORM_PER_QUEUE_STAT_SIZE		(IRO[22].size)

/* Ustorm LiteL2 queue statistics */
#define CORE_LL2_USTORM_PER_QUEUE_STAT_OFFSET(core_rx_queue_id) \
	(IRO[23].base + ((core_rx_queue_id) * IRO[23].m1))
#define CORE_LL2_USTORM_PER_QUEUE_STAT_SIZE		(IRO[23].size)

/* Pstorm LiteL2 queue statistics */
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_OFFSET(core_tx_stats_id) \
	(IRO[24].base + ((core_tx_stats_id) * IRO[24].m1))
#define CORE_LL2_PSTORM_PER_QUEUE_STAT_SIZE		(IRO[24].size)

/* Mstorm queue statistics */
#define MSTORM_QUEUE_STAT_OFFSET(stat_counter_id) \
	(IRO[25].base + ((stat_counter_id) * IRO[25].m1))
#define MSTORM_QUEUE_STAT_SIZE				(IRO[25].size)

/* TPA agregation timeout in us resolution (on ASIC) */
#define MSTORM_TPA_TIMEOUT_US_OFFSET			(IRO[26].base)
#define MSTORM_TPA_TIMEOUT_US_SIZE			(IRO[26].size)

/* Mstorm ETH VF queues producers offset in RAM. Used in default VF zone size
 * mode
 */
#define MSTORM_ETH_VF_PRODS_OFFSET(vf_id, vf_queue_id) \
	(IRO[27].base + ((vf_id) * IRO[27].m1) + ((vf_queue_id) * IRO[27].m2))
#define MSTORM_ETH_VF_PRODS_SIZE			(IRO[27].size)

/* Mstorm ETH PF queues producers */
#define MSTORM_ETH_PF_PRODS_OFFSET(queue_id) \
	(IRO[28].base + ((queue_id) * IRO[28].m1))
#define MSTORM_ETH_PF_PRODS_SIZE			(IRO[28].size)

/* Mstorm pf statistics */
#define MSTORM_ETH_PF_STAT_OFFSET(pf_id) \
	(IRO[29].base + ((pf_id) * IRO[29].m1))
#define MSTORM_ETH_PF_STAT_SIZE				(IRO[29].size)

/* Ustorm queue statistics */
#define USTORM_QUEUE_STAT_OFFSET(stat_counter_id) \
	(IRO[30].base + ((stat_counter_id) * IRO[30].m1))
#define USTORM_QUEUE_STAT_SIZE				(IRO[30].size)

/* Ustorm pf statistics */
#define USTORM_ETH_PF_STAT_OFFSET(pf_id) \
	(IRO[31].base + ((pf_id) * IRO[31].m1))
#define USTORM_ETH_PF_STAT_SIZE				(IRO[31].size)

/* Pstorm queue statistics */
#define PSTORM_QUEUE_STAT_OFFSET(stat_counter_id)	\
	(IRO[32].base + ((stat_counter_id) * IRO[32].m1))
#define PSTORM_QUEUE_STAT_SIZE				(IRO[32].size)

/* Pstorm pf statistics */
#define PSTORM_ETH_PF_STAT_OFFSET(pf_id) \
	(IRO[33].base + ((pf_id) * IRO[33].m1))
#define PSTORM_ETH_PF_STAT_SIZE				(IRO[33].size)

/* Control frame's EthType configuration for TX control frame security */
#define PSTORM_CTL_FRAME_ETHTYPE_OFFSET(eth_type_id)	\
	(IRO[34].base + ((eth_type_id) * IRO[34].m1))
#define PSTORM_CTL_FRAME_ETHTYPE_SIZE			(IRO[34].size)

/* Tstorm last parser message */
#define TSTORM_ETH_PRS_INPUT_OFFSET			(IRO[35].base)
#define TSTORM_ETH_PRS_INPUT_SIZE			(IRO[35].size)

/* Tstorm Eth limit Rx rate */
#define ETH_RX_RATE_LIMIT_OFFSET(pf_id)	\
	(IRO[36].base + ((pf_id) * IRO[36].m1))
#define ETH_RX_RATE_LIMIT_SIZE				(IRO[36].size)

/* RSS indirection table entry update command per PF offset in TSTORM PF BAR0.
 * Use eth_tstorm_rss_update_data for update
 */
#define TSTORM_ETH_RSS_UPDATE_OFFSET(pf_id) \
	(IRO[37].base + ((pf_id) * IRO[37].m1))
#define TSTORM_ETH_RSS_UPDATE_SIZE			(IRO[37].size)

/* Xstorm queue zone */
#define XSTORM_ETH_QUEUE_ZONE_OFFSET(queue_id) \
	(IRO[38].base + ((queue_id) * IRO[38].m1))
#define XSTORM_ETH_QUEUE_ZONE_SIZE			(IRO[38].size)

/* Ystorm cqe producer */
#define YSTORM_TOE_CQ_PROD_OFFSET(rss_id) \
	(IRO[39].base + ((rss_id) * IRO[39].m1))
#define YSTORM_TOE_CQ_PROD_SIZE				(IRO[39].size)

/* Ustorm cqe producer */
#define USTORM_TOE_CQ_PROD_OFFSET(rss_id) \
	(IRO[40].base + ((rss_id) * IRO[40].m1))
#define USTORM_TOE_CQ_PROD_SIZE				(IRO[40].size)

/* Ustorm grq producer */
#define USTORM_TOE_GRQ_PROD_OFFSET(pf_id) \
	(IRO[41].base + ((pf_id) * IRO[41].m1))
#define USTORM_TOE_GRQ_PROD_SIZE			(IRO[41].size)

/* Tstorm cmdq-cons of given command queue-id */
#define TSTORM_SCSI_CMDQ_CONS_OFFSET(cmdq_queue_id) \
	(IRO[42].base + ((cmdq_queue_id) * IRO[42].m1))
#define TSTORM_SCSI_CMDQ_CONS_SIZE			(IRO[42].size)

/* Tstorm (reflects M-Storm) bdq-external-producer of given function ID,
 * BDqueue-id
 */
#define TSTORM_SCSI_BDQ_EXT_PROD_OFFSET(storage_func_id, bdq_id) \
	(IRO[43].base + ((storage_func_id) * IRO[43].m1) + \
	 ((bdq_id) * IRO[43].m2))
#define TSTORM_SCSI_BDQ_EXT_PROD_SIZE			(IRO[43].size)

/* Mstorm bdq-external-producer of given BDQ resource ID, BDqueue-id */
#define MSTORM_SCSI_BDQ_EXT_PROD_OFFSET(storage_func_id, bdq_id) \
	(IRO[44].base + ((storage_func_id) * IRO[44].m1) + \
	 ((bdq_id) * IRO[44].m2))
#define MSTORM_SCSI_BDQ_EXT_PROD_SIZE			(IRO[44].size)

/* Tstorm iSCSI RX stats */
#define TSTORM_ISCSI_RX_STATS_OFFSET(storage_func_id) \
	(IRO[45].base + ((storage_func_id) * IRO[45].m1))
#define TSTORM_ISCSI_RX_STATS_SIZE			(IRO[45].size)

/* Mstorm iSCSI RX stats */
#define MSTORM_ISCSI_RX_STATS_OFFSET(storage_func_id) \
	(IRO[46].base + ((storage_func_id) * IRO[46].m1))
#define MSTORM_ISCSI_RX_STATS_SIZE			(IRO[46].size)

/* Ustorm iSCSI RX stats */
#define USTORM_ISCSI_RX_STATS_OFFSET(storage_func_id) \
	(IRO[47].base + ((storage_func_id) * IRO[47].m1))
#define USTORM_ISCSI_RX_STATS_SIZE			(IRO[47].size)

/* Xstorm iSCSI TX stats */
#define XSTORM_ISCSI_TX_STATS_OFFSET(storage_func_id) \
	(IRO[48].base + ((storage_func_id) * IRO[48].m1))
#define XSTORM_ISCSI_TX_STATS_SIZE			(IRO[48].size)

/* Ystorm iSCSI TX stats */
#define YSTORM_ISCSI_TX_STATS_OFFSET(storage_func_id) \
	(IRO[49].base + ((storage_func_id) * IRO[49].m1))
#define YSTORM_ISCSI_TX_STATS_SIZE			(IRO[49].size)

/* Pstorm iSCSI TX stats */
#define PSTORM_ISCSI_TX_STATS_OFFSET(storage_func_id) \
	(IRO[50].base + ((storage_func_id) * IRO[50].m1))
#define PSTORM_ISCSI_TX_STATS_SIZE			(IRO[50].size)

/* Tstorm FCoE RX stats */
#define TSTORM_FCOE_RX_STATS_OFFSET(pf_id) \
	(IRO[51].base + ((pf_id) * IRO[51].m1))
#define TSTORM_FCOE_RX_STATS_SIZE			(IRO[51].size)

/* Pstorm FCoE TX stats */
#define PSTORM_FCOE_TX_STATS_OFFSET(pf_id) \
	(IRO[52].base + ((pf_id) * IRO[52].m1))
#define PSTORM_FCOE_TX_STATS_SIZE			(IRO[52].size)

/* Pstorm RDMA queue statistics */
#define PSTORM_RDMA_QUEUE_STAT_OFFSET(rdma_stat_counter_id) \
	(IRO[53].base + ((rdma_stat_counter_id) * IRO[53].m1))
#define PSTORM_RDMA_QUEUE_STAT_SIZE			(IRO[53].size)

/* Tstorm RDMA queue statistics */
#define TSTORM_RDMA_QUEUE_STAT_OFFSET(rdma_stat_counter_id) \
	(IRO[54].base + ((rdma_stat_counter_id) * IRO[54].m1))
#define TSTORM_RDMA_QUEUE_STAT_SIZE			(IRO[54].size)

/* Xstorm error level for assert */
#define XSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[55].base + ((pf_id) * IRO[55].m1))
#define XSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[55].size)

/* Ystorm error level for assert */
#define YSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[56].base + ((pf_id) * IRO[56].m1))
#define YSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[56].size)

/* Pstorm error level for assert */
#define PSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[57].base + ((pf_id) * IRO[57].m1))
#define PSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[57].size)

/* Tstorm error level for assert */
#define TSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[58].base + ((pf_id) * IRO[58].m1))
#define TSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[58].size)

/* Mstorm error level for assert */
#define MSTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[59].base + ((pf_id) * IRO[59].m1))
#define MSTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[59].size)

/* Ustorm error level for assert */
#define USTORM_RDMA_ASSERT_LEVEL_OFFSET(pf_id) \
	(IRO[60].base + ((pf_id) * IRO[60].m1))
#define USTORM_RDMA_ASSERT_LEVEL_SIZE			(IRO[60].size)

/* Xstorm iWARP rxmit stats */
#define XSTORM_IWARP_RXMIT_STATS_OFFSET(pf_id) \
	(IRO[61].base + ((pf_id) * IRO[61].m1))
#define XSTORM_IWARP_RXMIT_STATS_SIZE			(IRO[61].size)

/* Tstorm RoCE Event Statistics */
#define TSTORM_ROCE_EVENTS_STAT_OFFSET(roce_pf_id)	\
	(IRO[62].base + ((roce_pf_id) * IRO[62].m1))
#define TSTORM_ROCE_EVENTS_STAT_SIZE			(IRO[62].size)

/* DCQCN Received Statistics */
#define YSTORM_ROCE_DCQCN_RECEIVED_STATS_OFFSET(roce_pf_id)\
	(IRO[63].base + ((roce_pf_id) * IRO[63].m1))
#define YSTORM_ROCE_DCQCN_RECEIVED_STATS_SIZE		(IRO[63].size)

/* RoCE Error Statistics */
#define YSTORM_ROCE_ERROR_STATS_OFFSET(roce_pf_id)	\
	(IRO[64].base + ((roce_pf_id) * IRO[64].m1))
#define YSTORM_ROCE_ERROR_STATS_SIZE			(IRO[64].size)

/* DCQCN Sent Statistics */
#define PSTORM_ROCE_DCQCN_SENT_STATS_OFFSET(roce_pf_id)	\
	(IRO[65].base + ((roce_pf_id) * IRO[65].m1))
#define PSTORM_ROCE_DCQCN_SENT_STATS_SIZE		(IRO[65].size)

/* RoCE CQEs Statistics */
#define USTORM_ROCE_CQE_STATS_OFFSET(roce_pf_id)	\
	(IRO[66].base + ((roce_pf_id) * IRO[66].m1))
#define USTORM_ROCE_CQE_STATS_SIZE			(IRO[66].size)

#endif
