/* bnx2x_fw_defs.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNX2X_FW_DEFS_H
#define BNX2X_FW_DEFS_H

#define CSTORM_ASSERT_LIST_INDEX_OFFSET (IRO[142].base)
#define CSTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[141].base + ((assertListEntry) * IRO[141].m1))
#define CSTORM_ETH_STATS_QUERY_ADDR_OFFSET(pfId) \
	(IRO[144].base + ((pfId) * IRO[144].m1))
#define CSTORM_EVENT_RING_DATA_OFFSET(pfId) \
	(IRO[149].base + (((pfId)>>1) * IRO[149].m1) + (((pfId)&1) * \
	IRO[149].m2))
#define CSTORM_EVENT_RING_PROD_OFFSET(pfId) \
	(IRO[150].base + (((pfId)>>1) * IRO[150].m1) + (((pfId)&1) * \
	IRO[150].m2))
#define CSTORM_FINAL_CLEANUP_COMPLETE_OFFSET(funcId) \
	(IRO[156].base + ((funcId) * IRO[156].m1))
#define CSTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[146].base + ((funcId) * IRO[146].m1))
#define CSTORM_FUNCTION_MODE_OFFSET (IRO[153].base)
#define CSTORM_IGU_MODE_OFFSET (IRO[154].base)
#define CSTORM_ISCSI_CQ_SIZE_OFFSET(pfId) \
	(IRO[311].base + ((pfId) * IRO[311].m1))
#define CSTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId) \
	(IRO[312].base + ((pfId) * IRO[312].m1))
	#define CSTORM_ISCSI_EQ_CONS_OFFSET(pfId, iscsiEqId) \
	(IRO[304].base + ((pfId) * IRO[304].m1) + ((iscsiEqId) * \
	IRO[304].m2))
	#define CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(pfId, iscsiEqId) \
	(IRO[306].base + ((pfId) * IRO[306].m1) + ((iscsiEqId) * \
	IRO[306].m2))
	#define CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(pfId, iscsiEqId) \
	(IRO[305].base + ((pfId) * IRO[305].m1) + ((iscsiEqId) * \
	IRO[305].m2))
	#define \
	CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_VALID_OFFSET(pfId, iscsiEqId) \
	(IRO[307].base + ((pfId) * IRO[307].m1) + ((iscsiEqId) * \
	IRO[307].m2))
	#define CSTORM_ISCSI_EQ_PROD_OFFSET(pfId, iscsiEqId) \
	(IRO[303].base + ((pfId) * IRO[303].m1) + ((iscsiEqId) * \
	IRO[303].m2))
	#define CSTORM_ISCSI_EQ_SB_INDEX_OFFSET(pfId, iscsiEqId) \
	(IRO[309].base + ((pfId) * IRO[309].m1) + ((iscsiEqId) * \
	IRO[309].m2))
	#define CSTORM_ISCSI_EQ_SB_NUM_OFFSET(pfId, iscsiEqId) \
	(IRO[308].base + ((pfId) * IRO[308].m1) + ((iscsiEqId) * \
	IRO[308].m2))
#define CSTORM_ISCSI_HQ_SIZE_OFFSET(pfId) \
	(IRO[310].base + ((pfId) * IRO[310].m1))
#define CSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[302].base + ((pfId) * IRO[302].m1))
#define CSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[301].base + ((pfId) * IRO[301].m1))
#define CSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[300].base + ((pfId) * IRO[300].m1))
#define CSTORM_PATH_ID_OFFSET (IRO[159].base)
#define CSTORM_SP_STATUS_BLOCK_DATA_OFFSET(pfId) \
	(IRO[137].base + ((pfId) * IRO[137].m1))
#define CSTORM_SP_STATUS_BLOCK_OFFSET(pfId) \
	(IRO[136].base + ((pfId) * IRO[136].m1))
#define CSTORM_SP_STATUS_BLOCK_SIZE (IRO[136].size)
#define CSTORM_SP_SYNC_BLOCK_OFFSET(pfId) \
	(IRO[138].base + ((pfId) * IRO[138].m1))
#define CSTORM_SP_SYNC_BLOCK_SIZE (IRO[138].size)
#define CSTORM_STATS_FLAGS_OFFSET(pfId) \
	(IRO[143].base + ((pfId) * IRO[143].m1))
#define CSTORM_STATUS_BLOCK_DATA_OFFSET(sbId) \
	(IRO[129].base + ((sbId) * IRO[129].m1))
#define CSTORM_STATUS_BLOCK_OFFSET(sbId) \
	(IRO[128].base + ((sbId) * IRO[128].m1))
#define CSTORM_STATUS_BLOCK_SIZE (IRO[128].size)
#define CSTORM_SYNC_BLOCK_OFFSET(sbId) \
	(IRO[132].base + ((sbId) * IRO[132].m1))
#define CSTORM_SYNC_BLOCK_SIZE (IRO[132].size)
#define CSTORM_VF_PF_CHANNEL_STATE_OFFSET(vfId) \
	(IRO[151].base + ((vfId) * IRO[151].m1))
#define CSTORM_VF_PF_CHANNEL_VALID_OFFSET(vfId) \
	(IRO[152].base + ((vfId) * IRO[152].m1))
#define CSTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[147].base + ((funcId) * IRO[147].m1))
#define TSTORM_ACCEPT_CLASSIFY_FAILED_OFFSET (IRO[199].base)
#define TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(pfId) \
	(IRO[198].base + ((pfId) * IRO[198].m1))
#define TSTORM_ASSERT_LIST_INDEX_OFFSET (IRO[99].base)
#define TSTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[98].base + ((assertListEntry) * IRO[98].m1))
	#define TSTORM_CLIENT_CONFIG_OFFSET(portId, clientId) \
	(IRO[197].base + ((portId) * IRO[197].m1) + ((clientId) * \
	IRO[197].m2))
#define TSTORM_COMMON_SAFC_WORKAROUND_ENABLE_OFFSET (IRO[104].base)
#define TSTORM_COMMON_SAFC_WORKAROUND_TIMEOUT_10USEC_OFFSET \
	(IRO[105].base)
#define TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(pfId) \
	(IRO[96].base + ((pfId) * IRO[96].m1))
#define TSTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[101].base + ((funcId) * IRO[101].m1))
#define TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(pfId) \
	(IRO[195].base + ((pfId) * IRO[195].m1))
#define TSTORM_FUNCTION_MODE_OFFSET (IRO[103].base)
#define TSTORM_INDIRECTION_TABLE_OFFSET(pfId) \
	(IRO[91].base + ((pfId) * IRO[91].m1))
#define TSTORM_INDIRECTION_TABLE_SIZE (IRO[91].size)
	#define \
	TSTORM_ISCSI_CONN_BUF_PBL_OFFSET(pfId, iscsiConBufPblEntry) \
	(IRO[260].base + ((pfId) * IRO[260].m1) + ((iscsiConBufPblEntry) \
	* IRO[260].m2))
#define TSTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId) \
	(IRO[264].base + ((pfId) * IRO[264].m1))
#define TSTORM_ISCSI_L2_ISCSI_OOO_CID_TABLE_OFFSET(pfId) \
	(IRO[265].base + ((pfId) * IRO[265].m1))
#define TSTORM_ISCSI_L2_ISCSI_OOO_CLIENT_ID_TABLE_OFFSET(pfId) \
	(IRO[266].base + ((pfId) * IRO[266].m1))
#define TSTORM_ISCSI_L2_ISCSI_OOO_PROD_OFFSET(pfId) \
	(IRO[267].base + ((pfId) * IRO[267].m1))
#define TSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[263].base + ((pfId) * IRO[263].m1))
#define TSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[262].base + ((pfId) * IRO[262].m1))
#define TSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[261].base + ((pfId) * IRO[261].m1))
#define TSTORM_ISCSI_RQ_SIZE_OFFSET(pfId) \
	(IRO[259].base + ((pfId) * IRO[259].m1))
#define TSTORM_ISCSI_TCP_LOCAL_ADV_WND_OFFSET(pfId) \
	(IRO[269].base + ((pfId) * IRO[269].m1))
#define TSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId) \
	(IRO[256].base + ((pfId) * IRO[256].m1))
#define TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(pfId) \
	(IRO[257].base + ((pfId) * IRO[257].m1))
#define TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(pfId) \
	(IRO[258].base + ((pfId) * IRO[258].m1))
#define TSTORM_MAC_FILTER_CONFIG_OFFSET(pfId) \
	(IRO[196].base + ((pfId) * IRO[196].m1))
	#define TSTORM_PER_COUNTER_ID_STATS_OFFSET(portId, tStatCntId) \
	(IRO[100].base + ((portId) * IRO[100].m1) + ((tStatCntId) * \
	IRO[100].m2))
#define TSTORM_STATS_FLAGS_OFFSET(pfId) \
	(IRO[95].base + ((pfId) * IRO[95].m1))
#define TSTORM_TCP_MAX_CWND_OFFSET(pfId) \
	(IRO[211].base + ((pfId) * IRO[211].m1))
#define TSTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[102].base + ((funcId) * IRO[102].m1))
#define USTORM_AGG_DATA_OFFSET (IRO[201].base)
#define USTORM_AGG_DATA_SIZE (IRO[201].size)
#define USTORM_ASSERT_LIST_INDEX_OFFSET (IRO[170].base)
#define USTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[169].base + ((assertListEntry) * IRO[169].m1))
#define USTORM_ETH_PAUSE_ENABLED_OFFSET(portId) \
	(IRO[178].base + ((portId) * IRO[178].m1))
#define USTORM_ETH_STATS_QUERY_ADDR_OFFSET(pfId) \
	(IRO[172].base + ((pfId) * IRO[172].m1))
#define USTORM_FCOE_EQ_PROD_OFFSET(pfId) \
	(IRO[313].base + ((pfId) * IRO[313].m1))
#define USTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[174].base + ((funcId) * IRO[174].m1))
#define USTORM_FUNCTION_MODE_OFFSET (IRO[177].base)
#define USTORM_ISCSI_CQ_SIZE_OFFSET(pfId) \
	(IRO[277].base + ((pfId) * IRO[277].m1))
#define USTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfId) \
	(IRO[278].base + ((pfId) * IRO[278].m1))
#define USTORM_ISCSI_ERROR_BITMAP_OFFSET(pfId) \
	(IRO[282].base + ((pfId) * IRO[282].m1))
#define USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(pfId) \
	(IRO[279].base + ((pfId) * IRO[279].m1))
#define USTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[275].base + ((pfId) * IRO[275].m1))
#define USTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[274].base + ((pfId) * IRO[274].m1))
#define USTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[273].base + ((pfId) * IRO[273].m1))
#define USTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId) \
	(IRO[276].base + ((pfId) * IRO[276].m1))
#define USTORM_ISCSI_RQ_BUFFER_SIZE_OFFSET(pfId) \
	(IRO[280].base + ((pfId) * IRO[280].m1))
#define USTORM_ISCSI_RQ_SIZE_OFFSET(pfId) \
	(IRO[281].base + ((pfId) * IRO[281].m1))
#define USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(pfId) \
	(IRO[176].base + ((pfId) * IRO[176].m1))
	#define USTORM_PER_COUNTER_ID_STATS_OFFSET(portId, uStatCntId) \
	(IRO[173].base + ((portId) * IRO[173].m1) + ((uStatCntId) * \
	IRO[173].m2))
	#define USTORM_RX_PRODS_E1X_OFFSET(portId, clientId) \
	(IRO[204].base + ((portId) * IRO[204].m1) + ((clientId) * \
	IRO[204].m2))
#define USTORM_RX_PRODS_E2_OFFSET(qzoneId) \
	(IRO[205].base + ((qzoneId) * IRO[205].m1))
#define USTORM_STATS_FLAGS_OFFSET(pfId) \
	(IRO[171].base + ((pfId) * IRO[171].m1))
#define USTORM_TPA_BTR_OFFSET (IRO[202].base)
#define USTORM_TPA_BTR_SIZE (IRO[202].size)
#define USTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[175].base + ((funcId) * IRO[175].m1))
#define XSTORM_AGG_INT_FINAL_CLEANUP_COMP_TYPE (IRO[59].base)
#define XSTORM_AGG_INT_FINAL_CLEANUP_INDEX (IRO[58].base)
#define XSTORM_ASSERT_LIST_INDEX_OFFSET (IRO[54].base)
#define XSTORM_ASSERT_LIST_OFFSET(assertListEntry) \
	(IRO[53].base + ((assertListEntry) * IRO[53].m1))
#define XSTORM_CMNG_PER_PORT_VARS_OFFSET(portId) \
	(IRO[47].base + ((portId) * IRO[47].m1))
#define XSTORM_E1HOV_OFFSET(pfId) \
	(IRO[55].base + ((pfId) * IRO[55].m1))
#define XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(pfId) \
	(IRO[45].base + ((pfId) * IRO[45].m1))
#define XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(pfId) \
	(IRO[49].base + ((pfId) * IRO[49].m1))
#define XSTORM_FUNC_EN_OFFSET(funcId) \
	(IRO[51].base + ((funcId) * IRO[51].m1))
#define XSTORM_FUNCTION_MODE_OFFSET (IRO[56].base)
#define XSTORM_ISCSI_HQ_SIZE_OFFSET(pfId) \
	(IRO[290].base + ((pfId) * IRO[290].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR0_OFFSET(pfId) \
	(IRO[293].base + ((pfId) * IRO[293].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR1_OFFSET(pfId) \
	(IRO[294].base + ((pfId) * IRO[294].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR2_OFFSET(pfId) \
	(IRO[295].base + ((pfId) * IRO[295].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR3_OFFSET(pfId) \
	(IRO[296].base + ((pfId) * IRO[296].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR4_OFFSET(pfId) \
	(IRO[297].base + ((pfId) * IRO[297].m1))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR5_OFFSET(pfId) \
	(IRO[298].base + ((pfId) * IRO[298].m1))
#define XSTORM_ISCSI_LOCAL_VLAN_OFFSET(pfId) \
	(IRO[299].base + ((pfId) * IRO[299].m1))
#define XSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfId) \
	(IRO[289].base + ((pfId) * IRO[289].m1))
#define XSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfId) \
	(IRO[288].base + ((pfId) * IRO[288].m1))
#define XSTORM_ISCSI_PAGE_SIZE_OFFSET(pfId) \
	(IRO[287].base + ((pfId) * IRO[287].m1))
#define XSTORM_ISCSI_R2TQ_SIZE_OFFSET(pfId) \
	(IRO[292].base + ((pfId) * IRO[292].m1))
#define XSTORM_ISCSI_SQ_SIZE_OFFSET(pfId) \
	(IRO[291].base + ((pfId) * IRO[291].m1))
#define XSTORM_ISCSI_TCP_VARS_ADV_WND_SCL_OFFSET(pfId) \
	(IRO[286].base + ((pfId) * IRO[286].m1))
#define XSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(pfId) \
	(IRO[285].base + ((pfId) * IRO[285].m1))
#define XSTORM_ISCSI_TCP_VARS_TOS_OFFSET(pfId) \
	(IRO[284].base + ((pfId) * IRO[284].m1))
#define XSTORM_ISCSI_TCP_VARS_TTL_OFFSET(pfId) \
	(IRO[283].base + ((pfId) * IRO[283].m1))
#define XSTORM_PATH_ID_OFFSET (IRO[65].base)
	#define XSTORM_PER_COUNTER_ID_STATS_OFFSET(portId, xStatCntId) \
	(IRO[50].base + ((portId) * IRO[50].m1) + ((xStatCntId) * \
	IRO[50].m2))
#define XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(pfId) \
	(IRO[48].base + ((pfId) * IRO[48].m1))
#define XSTORM_SPQ_DATA_OFFSET(funcId) \
	(IRO[32].base + ((funcId) * IRO[32].m1))
#define XSTORM_SPQ_DATA_SIZE (IRO[32].size)
#define XSTORM_SPQ_PAGE_BASE_OFFSET(funcId) \
	(IRO[30].base + ((funcId) * IRO[30].m1))
#define XSTORM_SPQ_PROD_OFFSET(funcId) \
	(IRO[31].base + ((funcId) * IRO[31].m1))
#define XSTORM_STATS_FLAGS_OFFSET(pfId) \
	(IRO[43].base + ((pfId) * IRO[43].m1))
#define XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_ENABLED_OFFSET(portId) \
	(IRO[206].base + ((portId) * IRO[206].m1))
#define XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_MAX_COUNT_OFFSET(portId) \
	(IRO[207].base + ((portId) * IRO[207].m1))
#define XSTORM_TCP_TX_SWS_TIMER_VAL_OFFSET(pfId) \
	(IRO[209].base + (((pfId)>>1) * IRO[209].m1) + (((pfId)&1) * \
	IRO[209].m2))
#define XSTORM_VF_TO_PF_OFFSET(funcId) \
	(IRO[52].base + ((funcId) * IRO[52].m1))
#define COMMON_ASM_INVALID_ASSERT_OPCODE 0x0

/* RSS hash types */
#define DEFAULT_HASH_TYPE 0
#define IPV4_HASH_TYPE 1
#define TCP_IPV4_HASH_TYPE 2
#define IPV6_HASH_TYPE 3
#define TCP_IPV6_HASH_TYPE 4
#define VLAN_PRI_HASH_TYPE 5
#define E1HOV_PRI_HASH_TYPE 6
#define DSCP_HASH_TYPE 7


/* Ethernet Ring parameters */
#define X_ETH_LOCAL_RING_SIZE 13
#define FIRST_BD_IN_PKT 0
#define PARSE_BD_INDEX 1
#define NUM_OF_ETH_BDS_IN_PAGE ((PAGE_SIZE)/(STRUCT_SIZE(eth_tx_bd)/8))
#define U_ETH_NUM_OF_SGES_TO_FETCH 8
#define U_ETH_MAX_SGES_FOR_PACKET 3

/*Tx params*/
#define X_ETH_NO_VLAN 0
#define X_ETH_OUTBAND_VLAN 1
#define X_ETH_INBAND_VLAN 2
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

#define U_ETH_BDS_PER_PAGE_MASK (U_ETH_BDS_PER_PAGE-1)
#define U_ETH_CQE_PER_PAGE_MASK (TU_ETH_CQES_PER_PAGE-1)
#define U_ETH_SGES_PER_PAGE_MASK (U_ETH_SGES_PER_PAGE-1)

#define U_ETH_UNDEFINED_Q 0xFF

/* values of command IDs in the ramrod message */
#define RAMROD_CMD_ID_ETH_UNUSED 0
#define RAMROD_CMD_ID_ETH_CLIENT_SETUP 1
#define RAMROD_CMD_ID_ETH_UPDATE 2
#define RAMROD_CMD_ID_ETH_HALT 3
#define RAMROD_CMD_ID_ETH_FORWARD_SETUP 4
#define RAMROD_CMD_ID_ETH_ACTIVATE 5
#define RAMROD_CMD_ID_ETH_DEACTIVATE 6
#define RAMROD_CMD_ID_ETH_EMPTY 7
#define RAMROD_CMD_ID_ETH_TERMINATE 8

/* command values for set mac command */
#define T_ETH_MAC_COMMAND_SET 0
#define T_ETH_MAC_COMMAND_INVALIDATE 1

#define T_ETH_INDIRECTION_TABLE_SIZE 128

/*The CRC32 seed, that is used for the hash(reduction) multicast address */
#define T_ETH_CRC32_HASH_SEED 0x00000000

/* Maximal L2 clients supported */
#define ETH_MAX_RX_CLIENTS_E1 18
#define ETH_MAX_RX_CLIENTS_E1H 28

#define MAX_STAT_COUNTER_ID ETH_MAX_RX_CLIENTS_E1H

/* Maximal aggregation queues supported */
#define ETH_MAX_AGGREGATION_QUEUES_E1 32
#define ETH_MAX_AGGREGATION_QUEUES_E1H 64

/* ETH RSS modes */
#define ETH_RSS_MODE_DISABLED 0
#define ETH_RSS_MODE_REGULAR 1
#define ETH_RSS_MODE_VLAN_PRI 2
#define ETH_RSS_MODE_E1HOV_PRI 3
#define ETH_RSS_MODE_IP_DSCP 4
#define ETH_RSS_MODE_E2_INTEG 5


/* ETH vlan filtering modes */
#define ETH_VLAN_FILTER_ANY_VLAN 0 /* Don't filter by vlan */
#define ETH_VLAN_FILTER_SPECIFIC_VLAN \
	1 /* Only the vlan_id is allowed */
#define ETH_VLAN_FILTER_CLASSIFY \
	2 /* vlan will be added to CAM for classification */

/* Fast path CQE selection */
#define ETH_FP_CQE_REGULAR 0
#define ETH_FP_CQE_SGL 1
#define ETH_FP_CQE_RAW 2


/**
* This file defines HSI constants common to all microcode flows
*/

/* Connection types */
#define ETH_CONNECTION_TYPE 0
#define TOE_CONNECTION_TYPE 1
#define RDMA_CONNECTION_TYPE 2
#define ISCSI_CONNECTION_TYPE 3
#define FCOE_CONNECTION_TYPE 4
#define RESERVED_CONNECTION_TYPE_0 5
#define RESERVED_CONNECTION_TYPE_1 6
#define RESERVED_CONNECTION_TYPE_2 7
#define NONE_CONNECTION_TYPE 8


#define PROTOCOL_STATE_BIT_OFFSET 6

#define ETH_STATE (ETH_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define TOE_STATE (TOE_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define RDMA_STATE (RDMA_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)

/* values of command IDs in the ramrod message */
#define RAMROD_CMD_ID_COMMON_FUNCTION_START 1
#define RAMROD_CMD_ID_COMMON_FUNCTION_STOP 2
#define RAMROD_CMD_ID_COMMON_CFC_DEL 3
#define RAMROD_CMD_ID_COMMON_CFC_DEL_WB 4
#define RAMROD_CMD_ID_COMMON_SET_MAC 5
#define RAMROD_CMD_ID_COMMON_STAT_QUERY 6
#define RAMROD_CMD_ID_COMMON_STOP_TRAFFIC 7
#define RAMROD_CMD_ID_COMMON_START_TRAFFIC 8

/* microcode fixed page page size 4K (chains and ring segments) */
#define MC_PAGE_SIZE 4096


/* Host coalescing constants */
#define HC_IGU_BC_MODE 0
#define HC_IGU_NBC_MODE 1
/* Host coalescing constants. E1 includes E1H as well */

/* Number of indices per slow-path SB */
#define HC_SP_SB_MAX_INDICES 16

/* Number of indices per SB */
#define HC_SB_MAX_INDICES_E1X 8
#define HC_SB_MAX_INDICES_E2 8

#define HC_SB_MAX_SB_E1X 32
#define HC_SB_MAX_SB_E2 136

#define HC_SP_SB_ID 0xde

#define HC_REGULAR_SEGMENT 0
#define HC_DEFAULT_SEGMENT 1
#define HC_SB_MAX_SM 2

#define HC_SB_MAX_DYNAMIC_INDICES 4
#define HC_FUNCTION_DISABLED 0xff
/* used by the driver to get the SB offset */
#define USTORM_ID 0
#define CSTORM_ID 1
#define XSTORM_ID 2
#define TSTORM_ID 3
#define ATTENTION_ID 4

/* max number of slow path commands per port */
#define MAX_RAMRODS_PER_PORT 8

/* values for RX ETH CQE type field */
#define RX_ETH_CQE_TYPE_ETH_FASTPATH 0
#define RX_ETH_CQE_TYPE_ETH_RAMROD 1


/**** DEFINES FOR TIMERS/CLOCKS RESOLUTIONS ****/

#define TIMERS_TICK_SIZE_CHIP (1e-3)

#define TSEMI_CLK1_RESUL_CHIP (1e-3)

#define XSEMI_CLK1_RESUL_CHIP (1e-3)

#define SDM_TIMER_TICK_RESUL_CHIP (4*(1e-6))

/**** END DEFINES FOR TIMERS/CLOCKS RESOLUTIONS ****/

#define XSTORM_IP_ID_ROLL_HALF 0x8000
#define XSTORM_IP_ID_ROLL_ALL 0

#define FW_LOG_LIST_SIZE 50

#define NUM_OF_PROTOCOLS 4
#define NUM_OF_SAFC_BITS 16
#define MAX_COS_NUMBER 4

#define FAIRNESS_COS_WRR_MODE 0
#define FAIRNESS_COS_ETS_MODE 1


/* Priority Flow Control (PFC) */
#define MAX_PFC_PRIORITIES 8
#define MAX_PFC_TRAFFIC_TYPES 8

/* Available Traffic Types for Link Layer Flow Control */
#define LLFC_TRAFFIC_TYPE_NW 0
#define LLFC_TRAFFIC_TYPE_FCOE 1
#define LLFC_TRAFFIC_TYPE_ISCSI 2
	/***************** START OF E2 INTEGRATION \
	CODE***************************************/
#define LLFC_TRAFFIC_TYPE_NW_COS1_E2INTEG 3
	/***************** END OF E2 INTEGRATION \
	CODE***************************************/
#define LLFC_TRAFFIC_TYPE_MAX 4

	/* used by array traffic_type_to_priority[] to mark traffic type \
	that is not mapped to priority*/
#define LLFC_TRAFFIC_TYPE_TO_PRIORITY_UNMAPPED 0xFF

#define LLFC_MODE_NONE 0
#define LLFC_MODE_PFC 1
#define LLFC_MODE_SAFC 2

#define DCB_DISABLED 0
#define DCB_ENABLED 1

#define UNKNOWN_ADDRESS 0
#define UNICAST_ADDRESS 1
#define MULTICAST_ADDRESS 2
#define BROADCAST_ADDRESS 3

#define SINGLE_FUNCTION 0
#define MULTI_FUNCTION_SD 1
#define MULTI_FUNCTION_SI 2

#define IP_V4 0
#define IP_V6 1


#define C_ERES_PER_PAGE \
	(PAGE_SIZE / BITS_TO_BYTES(STRUCT_SIZE(event_ring_elem)))
#define C_ERE_PER_PAGE_MASK (C_ERES_PER_PAGE - 1)

#define EVENT_RING_OPCODE_VF_PF_CHANNEL 0
#define EVENT_RING_OPCODE_FUNCTION_START 1
#define EVENT_RING_OPCODE_FUNCTION_STOP 2
#define EVENT_RING_OPCODE_CFC_DEL 3
#define EVENT_RING_OPCODE_CFC_DEL_WB 4
#define EVENT_RING_OPCODE_SET_MAC 5
#define EVENT_RING_OPCODE_STAT_QUERY 6
#define EVENT_RING_OPCODE_STOP_TRAFFIC 7
#define EVENT_RING_OPCODE_START_TRAFFIC 8
#define EVENT_RING_OPCODE_FORWARD_SETUP 9

#define VF_PF_CHANNEL_STATE_READY 0
#define VF_PF_CHANNEL_STATE_WAITING_FOR_ACK 1

#define VF_PF_CHANNEL_STATE_MAX_NUMBER 2


#endif /* BNX2X_FW_DEFS_H */
