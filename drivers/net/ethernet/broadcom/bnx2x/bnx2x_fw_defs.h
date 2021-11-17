/* bnx2x_fw_defs.h: Qlogic Everest network driver.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
 * Copyright (c) 2014 QLogic Corporation
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNX2X_FW_DEFS_H
#define BNX2X_FW_DEFS_H

#define CSTORM_ASSERT_LIST_INDEX_OFFSET	(IRO[152].base)
#define CSTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[151].base + ((assertListEntry) * IRO[151].m1))
#define CSTORM_EVENT_RING_DATA_OFFSET(pfId) \
	(IRO[157].base + (((pfId)>>1) * IRO[157].m1) + (((pfId)&1) * \
	IRO[157].m2))
#define CSTORM_EVENT_RING_PROD_OFFSET(pfId) \
	(IRO[158].base + (((pfId)>>1) * IRO[158].m1) + (((pfId)&1) * \
	IRO[158].m2))
#define CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(funcId) \
	(IRO[163].base + ((funcId) * IRO[163].m1))
#define CSTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[153].base + ((funcId) * IRO[153].m1))
#define CSTORM_HC_SYNC_LINE_INDEX_E1X_OFFSET(hcIndex, sbId) \
	(IRO[143].base + ((hcIndex) * IRO[143].m1) + ((sbId) * IRO[143].m2))
#define CSTORM_HC_SYNC_LINE_INDEX_E2_OFFSET(hcIndex, sbId) \
	(IRO[142].base + (((hcIndex)>>2) * IRO[142].m1) + (((hcIndex)&3) \
	* IRO[142].m2) + ((sbId) * IRO[142].m3))
#define CSTORM_IGU_MODE_OFFSET (IRO[161].base)
#define CSTORM_ISCSI_CQ_SIZE_OFFSET(pfId) \
	(IRO[324].base + ((pfId) * IRO[324].m1))
#define CSTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId) \
	(IRO[325].base + ((pfId) * IRO[325].m1))
#define CSTORM_ISCSI_EQ_CONS_OFFSET(pfId, iscsiEqId) \
	(IRO[317].base + ((pfId) * IRO[317].m1) + ((iscsiEqId) * IRO[317].m2))
#define CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(pfId, iscsiEqId) \
	(IRO[319].base + ((pfId) * IRO[319].m1) + ((iscsiEqId) * IRO[319].m2))
#define CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(pfId, iscsiEqId) \
	(IRO[318].base + ((pfId) * IRO[318].m1) + ((iscsiEqId) * IRO[318].m2))
#define CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_VALID_OFFSET(pfId, iscsiEqId) \
	(IRO[320].base + ((pfId) * IRO[320].m1) + ((iscsiEqId) * IRO[320].m2))
#define CSTORM_ISCSI_EQ_PROD_OFFSET(pfId, iscsiEqId) \
	(IRO[316].base + ((pfId) * IRO[316].m1) + ((iscsiEqId) * IRO[316].m2))
#define CSTORM_ISCSI_EQ_SB_INDEX_OFFSET(pfId, iscsiEqId) \
	(IRO[322].base + ((pfId) * IRO[322].m1) + ((iscsiEqId) * IRO[322].m2))
#define CSTORM_ISCSI_EQ_SB_NUM_OFFSET(pfId, iscsiEqId) \
	(IRO[321].base + ((pfId) * IRO[321].m1) + ((iscsiEqId) * IRO[321].m2))
#define CSTORM_ISCSI_HQ_SIZE_OFFSET(pfId) \
	(IRO[323].base + ((pfId) * IRO[323].m1))
#define CSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[315].base + ((pfId) * IRO[315].m1))
#define CSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[314].base + ((pfId) * IRO[314].m1))
#define CSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[313].base + ((pfId) * IRO[313].m1))
#define CSTORM_RECORD_SLOW_PATH_OFFSET(funcId) \
	(IRO[155].base + ((funcId) * IRO[155].m1))
#define CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(pfId) \
	(IRO[146].base + ((pfId) * IRO[146].m1))
#define CSTORM_SP_STATUS_BLOCK_DATA_STATE_OFFSET(pfId) \
	(IRO[147].base + ((pfId) * IRO[147].m1))
#define CSTORM_SP_STATUS_BLOCK_OFFSET(pfId) \
	(IRO[145].base + ((pfId) * IRO[145].m1))
#define CSTORM_SP_STATUS_BLOCK_SIZE (IRO[145].size)
#define CSTORM_SP_SYNC_BLOCK_OFFSET(pfId) \
	(IRO[148].base + ((pfId) * IRO[148].m1))
#define CSTORM_SP_SYNC_BLOCK_SIZE (IRO[148].size)
#define CSTORM_STATUS_BLOCK_DATA_FLAGS_OFFSET(sbId, hcIndex) \
	(IRO[140].base + ((sbId) * IRO[140].m1) + ((hcIndex) * IRO[140].m2))
#define CSTORM_STATUS_BLOCK_DATA_OFFSET(sbId) \
	(IRO[137].base + ((sbId) * IRO[137].m1))
#define CSTORM_STATUS_BLOCK_DATA_STATE_OFFSET(sbId) \
	(IRO[138].base + ((sbId) * IRO[138].m1))
#define CSTORM_STATUS_BLOCK_DATA_TIMEOUT_OFFSET(sbId, hcIndex) \
	(IRO[139].base + ((sbId) * IRO[139].m1) + ((hcIndex) * IRO[139].m2))
#define CSTORM_STATUS_BLOCK_OFFSET(sbId) \
	(IRO[136].base + ((sbId) * IRO[136].m1))
#define CSTORM_STATUS_BLOCK_SIZE (IRO[136].size)
#define CSTORM_SYNC_BLOCK_OFFSET(sbId) \
	(IRO[141].base + ((sbId) * IRO[141].m1))
#define CSTORM_SYNC_BLOCK_SIZE (IRO[141].size)
#define CSTORM_VF_PF_CHANNEL_STATE_OFFSET(vfId) \
	(IRO[159].base + ((vfId) * IRO[159].m1))
#define CSTORM_VF_PF_CHANNEL_VALID_OFFSET(vfId) \
	(IRO[160].base + ((vfId) * IRO[160].m1))
#define CSTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[154].base + ((funcId) * IRO[154].m1))
#define TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(pfId) \
	(IRO[207].base + ((pfId) * IRO[207].m1))
#define TSTORM_ASSERT_LIST_INDEX_OFFSET	(IRO[102].base)
#define TSTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[101].base + ((assertListEntry) * IRO[101].m1))
#define TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(pfId) \
	(IRO[205].base + ((pfId) * IRO[205].m1))
#define TSTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[107].base + ((funcId) * IRO[107].m1))
#define TSTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId) \
	(IRO[279].base + ((pfId) * IRO[279].m1))
#define TSTORM_ISCSI_L2_ISCSI_OOO_CID_TABLE_OFFSET(pfId) \
	(IRO[280].base + ((pfId) * IRO[280].m1))
#define TSTORM_ISCSI_L2_ISCSI_OOO_CLIENT_ID_TABLE_OFFSET(pfId) \
	(IRO[281].base + ((pfId) * IRO[281].m1))
#define TSTORM_ISCSI_L2_ISCSI_OOO_PROD_OFFSET(pfId) \
	(IRO[282].base + ((pfId) * IRO[282].m1))
#define TSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[278].base + ((pfId) * IRO[278].m1))
#define TSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[277].base + ((pfId) * IRO[277].m1))
#define TSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[276].base + ((pfId) * IRO[276].m1))
#define TSTORM_ISCSI_RQ_SIZE_OFFSET(pfId) \
	(IRO[275].base + ((pfId) * IRO[275].m1))
#define TSTORM_ISCSI_TCP_LOCAL_ADV_WND_OFFSET(pfId) \
	(IRO[285].base + ((pfId) * IRO[285].m1))
#define TSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId) \
	(IRO[271].base + ((pfId) * IRO[271].m1))
#define TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(pfId) \
	(IRO[272].base + ((pfId) * IRO[272].m1))
#define TSTORM_ISCSI_TCP_VARS_MID_LOCAL_MAC_ADDR_OFFSET(pfId) \
	(IRO[273].base + ((pfId) * IRO[273].m1))
#define TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(pfId) \
	(IRO[274].base + ((pfId) * IRO[274].m1))
#define TSTORM_MAC_FILTER_CONFIG_OFFSET(pfId) \
	(IRO[206].base + ((pfId) * IRO[206].m1))
#define TSTORM_RECORD_SLOW_PATH_OFFSET(funcId) \
	(IRO[109].base + ((funcId) * IRO[109].m1))
#define TSTORM_TCP_MAX_CWND_OFFSET(pfId) \
	(IRO[224].base + ((pfId) * IRO[224].m1))
#define TSTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[108].base + ((funcId) * IRO[108].m1))
#define USTORM_AGG_DATA_OFFSET (IRO[213].base)
#define USTORM_AGG_DATA_SIZE (IRO[213].size)
#define USTORM_ASSERT_LIST_INDEX_OFFSET	(IRO[181].base)
#define USTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[180].base + ((assertListEntry) * IRO[180].m1))
#define USTORM_ETH_PAUSE_ENABLED_OFFSET(portId) \
	(IRO[187].base + ((portId) * IRO[187].m1))
#define USTORM_FCOE_EQ_PROD_OFFSET(pfId) \
	(IRO[326].base + ((pfId) * IRO[326].m1))
#define USTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[182].base + ((funcId) * IRO[182].m1))
#define USTORM_ISCSI_CQ_SIZE_OFFSET(pfId) \
	(IRO[290].base + ((pfId) * IRO[290].m1))
#define USTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId) \
	(IRO[291].base + ((pfId) * IRO[291].m1))
#define USTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId) \
	(IRO[295].base + ((pfId) * IRO[295].m1))
#define USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(pfId) \
	(IRO[292].base + ((pfId) * IRO[292].m1))
#define USTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[288].base + ((pfId) * IRO[288].m1))
#define USTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[287].base + ((pfId) * IRO[287].m1))
#define USTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[286].base + ((pfId) * IRO[286].m1))
#define USTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId) \
	(IRO[289].base + ((pfId) * IRO[289].m1))
#define USTORM_ISCSI_RQ_BUFFER_SIZE_OFFSET(pfId) \
	(IRO[293].base + ((pfId) * IRO[293].m1))
#define USTORM_ISCSI_RQ_SIZE_OFFSET(pfId) \
	(IRO[294].base + ((pfId) * IRO[294].m1))
#define USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(pfId) \
	(IRO[186].base + ((pfId) * IRO[186].m1))
#define USTORM_RECORD_SLOW_PATH_OFFSET(funcId) \
	(IRO[184].base + ((funcId) * IRO[184].m1))
#define USTORM_RX_PRODS_E1X_OFFSET(portId, clientId) \
	(IRO[216].base + ((portId) * IRO[216].m1) + ((clientId) * \
	IRO[216].m2))
#define USTORM_RX_PRODS_E2_OFFSET(qzoneId) \
	(IRO[217].base + ((qzoneId) * IRO[217].m1))
#define USTORM_TPA_BTR_OFFSET (IRO[214].base)
#define USTORM_TPA_BTR_SIZE (IRO[214].size)
#define USTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[183].base + ((funcId) * IRO[183].m1))
#define XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE (IRO[67].base)
#define XSTORM_AGG_INT_FINAL_CLEANUP_INDEX (IRO[66].base)
#define XSTORM_ASSERT_LIST_INDEX_OFFSET	(IRO[51].base)
#define XSTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[50].base + ((assertListEntry) * IRO[50].m1))
#define XSTORM_CMNG_PER_PORT_VARS_OFFSET(portId) \
	(IRO[43].base + ((portId) * IRO[43].m1))
#define XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(pfId) \
	(IRO[45].base + ((pfId) * IRO[45].m1))
#define XSTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[47].base + ((funcId) * IRO[47].m1))
#define XSTORM_ISCSI_HQ_SIZE_OFFSET(pfId) \
	(IRO[303].base + ((pfId) * IRO[303].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR0_OFFSET(pfId) \
	(IRO[306].base + ((pfId) * IRO[306].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR1_OFFSET(pfId) \
	(IRO[307].base + ((pfId) * IRO[307].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR2_OFFSET(pfId) \
	(IRO[308].base + ((pfId) * IRO[308].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR3_OFFSET(pfId) \
	(IRO[309].base + ((pfId) * IRO[309].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR4_OFFSET(pfId) \
	(IRO[310].base + ((pfId) * IRO[310].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR5_OFFSET(pfId) \
	(IRO[311].base + ((pfId) * IRO[311].m1))
#define XSTORM_ISCSI_LOCAL_VLAN_OFFSET(pfId) \
	(IRO[312].base + ((pfId) * IRO[312].m1))
#define XSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[302].base + ((pfId) * IRO[302].m1))
#define XSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[301].base + ((pfId) * IRO[301].m1))
#define XSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[300].base + ((pfId) * IRO[300].m1))
#define XSTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId) \
	(IRO[305].base + ((pfId) * IRO[305].m1))
#define XSTORM_ISCSI_SQ_SIZE_OFFSET(pfId) \
	(IRO[304].base + ((pfId) * IRO[304].m1))
#define XSTORM_ISCSI_TCP_VARS_ADV_WND_SCL_OFFSET(pfId) \
	(IRO[299].base + ((pfId) * IRO[299].m1))
#define XSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId) \
	(IRO[298].base + ((pfId) * IRO[298].m1))
#define XSTORM_ISCSI_TCP_VARS_TOS_OFFSET(pfId) \
	(IRO[297].base + ((pfId) * IRO[297].m1))
#define XSTORM_ISCSI_TCP_VARS_TTL_OFFSET(pfId) \
	(IRO[296].base + ((pfId) * IRO[296].m1))
#define XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(pfId) \
	(IRO[44].base + ((pfId) * IRO[44].m1))
#define XSTORM_RECORD_SLOW_PATH_OFFSET(funcId) \
	(IRO[49].base + ((funcId) * IRO[49].m1))
#define XSTORM_SPQ_DATA_OFFSET(funcId) \
	(IRO[32].base + ((funcId) * IRO[32].m1))
#define XSTORM_SPQ_DATA_SIZE (IRO[32].size)
#define XSTORM_SPQ_PAGE_BASE_OFFSET(funcId) \
	(IRO[30].base + ((funcId) * IRO[30].m1))
#define XSTORM_SPQ_PROD_OFFSET(funcId) \
	(IRO[31].base + ((funcId) * IRO[31].m1))
#define XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_ENABLED_OFFSET(portId) \
	(IRO[218].base + ((portId) * IRO[218].m1))
#define XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_MAX_COUNT_OFFSET(portId) \
	(IRO[219].base + ((portId) * IRO[219].m1))
#define XSTORM_TCP_TX_SWS_TIMER_VAL_OFFSET(pfId) \
	(IRO[221].base + (((pfId)>>1) * IRO[221].m1) + (((pfId)&1) * \
	IRO[221].m2))
#define XSTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[48].base + ((funcId) * IRO[48].m1))
#define COMMON_ASM_INVALID_ASSERT_OPCODE 0x0

/* eth hsi version */
#define ETH_FP_HSI_VERSION (ETH_FP_HSI_VER_2)

/* Ethernet Ring parameters */
#define X_ETH_LOCAL_RING_SIZE 13
#define FIRST_BD_IN_PKT	0
#define PARSE_BD_INDEX 1
#define NUM_OF_ETH_BDS_IN_PAGE ((PAGE_SIZE)/(STRUCT_SIZE(eth_tx_bd)/8))
#define U_ETH_NUM_OF_SGES_TO_FETCH 8
#define U_ETH_MAX_SGES_FOR_PACKET 3

/* Rx ring params */
#define U_ETH_LOCAL_BD_RING_SIZE 8
#define U_ETH_LOCAL_SGE_RING_SIZE 10
#define U_ETH_SGL_SIZE 8
	/* The fw will padd the buffer with this value, so the IP header \
	will be align to 4 Byte */
#define IP_HEADER_ALIGNMENT_PADDING 2

#define U_ETH_SGES_PER_PAGE_INVERSE_MASK \
	(0xFFFF - ((PAGE_SIZE/((STRUCT_SIZE(eth_rx_sge))/8))-1))

#define TU_ETH_CQES_PER_PAGE (PAGE_SIZE/(STRUCT_SIZE(eth_rx_cqe)/8))
#define U_ETH_BDS_PER_PAGE (PAGE_SIZE/(STRUCT_SIZE(eth_rx_bd)/8))
#define U_ETH_SGES_PER_PAGE (PAGE_SIZE/(STRUCT_SIZE(eth_rx_sge)/8))

#define U_ETH_BDS_PER_PAGE_MASK	(U_ETH_BDS_PER_PAGE-1)
#define U_ETH_CQE_PER_PAGE_MASK	(TU_ETH_CQES_PER_PAGE-1)
#define U_ETH_SGES_PER_PAGE_MASK (U_ETH_SGES_PER_PAGE-1)

#define U_ETH_UNDEFINED_Q 0xFF

#define T_ETH_INDIRECTION_TABLE_SIZE 128
#define T_ETH_RSS_KEY 10
#define ETH_NUM_OF_RSS_ENGINES_E2 72

#define FILTER_RULES_COUNT 16
#define MULTICAST_RULES_COUNT 16
#define CLASSIFY_RULES_COUNT 16

/*The CRC32 seed, that is used for the hash(reduction) multicast address */
#define ETH_CRC32_HASH_SEED 0x00000000

#define ETH_CRC32_HASH_BIT_SIZE	(8)
#define ETH_CRC32_HASH_MASK EVAL((1<<ETH_CRC32_HASH_BIT_SIZE)-1)

/* Maximal L2 clients supported */
#define ETH_MAX_RX_CLIENTS_E1 18
#define ETH_MAX_RX_CLIENTS_E1H 28
#define ETH_MAX_RX_CLIENTS_E2 152

/* Maximal statistics client Ids */
#define MAX_STAT_COUNTER_ID_E1 36
#define MAX_STAT_COUNTER_ID_E1H	56
#define MAX_STAT_COUNTER_ID_E2 140

#define MAX_MAC_CREDIT_E1 192 /* Per Chip */
#define MAX_MAC_CREDIT_E1H 256 /* Per Chip */
#define MAX_MAC_CREDIT_E2 272 /* Per Path */
#define MAX_VLAN_CREDIT_E1 0 /* Per Chip */
#define MAX_VLAN_CREDIT_E1H 0 /* Per Chip */
#define MAX_VLAN_CREDIT_E2 272 /* Per Path */

/* Maximal aggregation queues supported */
#define ETH_MAX_AGGREGATION_QUEUES_E1 32
#define ETH_MAX_AGGREGATION_QUEUES_E1H_E2 64

#define ETH_NUM_OF_MCAST_BINS 256
#define ETH_NUM_OF_MCAST_ENGINES_E2 72

#define ETH_MIN_RX_CQES_WITHOUT_TPA (MAX_RAMRODS_PER_PORT + 3)
#define ETH_MIN_RX_CQES_WITH_TPA_E1 \
	(ETH_MAX_AGGREGATION_QUEUES_E1 + ETH_MIN_RX_CQES_WITHOUT_TPA)
#define ETH_MIN_RX_CQES_WITH_TPA_E1H_E2 \
	(ETH_MAX_AGGREGATION_QUEUES_E1H_E2 + ETH_MIN_RX_CQES_WITHOUT_TPA)

#define DISABLE_STATISTIC_COUNTER_ID_VALUE 0


/* This file defines HSI constants common to all microcode flows */

#define PROTOCOL_STATE_BIT_OFFSET 6

#define ETH_STATE (ETH_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define TOE_STATE (TOE_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define RDMA_STATE (RDMA_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)

/* microcode fixed page page size 4K (chains and ring segments) */
#define MC_PAGE_SIZE 4096

/* Number of indices per slow-path SB */
#define HC_SP_SB_MAX_INDICES 16

/* Number of indices per SB */
#define HC_SB_MAX_INDICES_E1X 8
#define HC_SB_MAX_INDICES_E2 8

#define HC_SB_MAX_SB_E1X 32
#define HC_SB_MAX_SB_E2	136

#define HC_SP_SB_ID 0xde

#define HC_SB_MAX_SM 2

#define HC_SB_MAX_DYNAMIC_INDICES 4

/* max number of slow path commands per port */
#define MAX_RAMRODS_PER_PORT 8

/**** DEFINES FOR TIMERS/CLOCKS RESOLUTIONS ****/

#define TIMERS_TICK_SIZE_CHIP (1e-3)

#define TSEMI_CLK1_RESUL_CHIP (1e-3)

#define XSEMI_CLK1_RESUL_CHIP (1e-3)

#define SDM_TIMER_TICK_RESUL_CHIP (4 * (1e-6))
#define TSDM_TIMER_TICK_RESUL_CHIP (1 * (1e-6))

/**** END DEFINES FOR TIMERS/CLOCKS RESOLUTIONS ****/

#define XSTORM_IP_ID_ROLL_HALF 0x8000
#define XSTORM_IP_ID_ROLL_ALL 0

#define FW_LOG_LIST_SIZE 50

#define NUM_OF_SAFC_BITS 16
#define MAX_COS_NUMBER 4
#define MAX_TRAFFIC_TYPES 8
#define MAX_PFC_PRIORITIES 8
#define MAX_VLAN_PRIORITIES 8
	/* used by array traffic_type_to_priority[] to mark traffic type \
	that is not mapped to priority*/
#define LLFC_TRAFFIC_TYPE_TO_PRIORITY_UNMAPPED 0xFF

#define C_ERES_PER_PAGE \
	(PAGE_SIZE / BITS_TO_BYTES(STRUCT_SIZE(event_ring_elem)))
#define C_ERE_PER_PAGE_MASK (C_ERES_PER_PAGE - 1)

#define STATS_QUERY_CMD_COUNT 16

#define AFEX_LIST_TABLE_SIZE 4096

#define INVALID_VNIC_ID	0xFF

#define UNDEF_IRO 0x80000000

/* used for defining the amount of FCoE tasks supported for PF */
#define MAX_FCOE_FUNCS_PER_ENGINE 2
#define MAX_NUM_FCOE_TASKS_PER_ENGINE 4096

#endif /* BNX2X_FW_DEFS_H */
