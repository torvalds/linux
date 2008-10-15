/* bnx2x_fw_defs.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2008 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */


#define CSTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0x7000 : 0x1000)
#define CSTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0x7020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define CSTORM_DEF_SB_HC_DISABLE_OFFSET(function, index) \
	(IS_E1H_OFFSET ? (0x8522 + ((function>>1) * 0x40) + \
	((function&1) * 0x100) + (index * 0x4)) : (0x1922 + (function * \
	0x40) + (index * 0x4)))
#define CSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8500 + ((function>>1) * 0x40) + \
	((function&1) * 0x100)) : (0x1900 + (function * 0x40)))
#define CSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8508 + ((function>>1) * 0x40) + \
	((function&1) * 0x100)) : (0x1908 + (function * 0x40)))
#define CSTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x11e8 : 0xffffffff)
#define CSTORM_HC_BTR_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x8704 + (port * 0xf0)) : (0x1984 + (port * 0xc0)))
#define CSTORM_SB_HC_DISABLE_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0x801a + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)) : (0x141a + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)))
#define CSTORM_SB_HC_TIMEOUT_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0x8018 + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)) : (0x1418 + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)))
#define CSTORM_SB_HOST_SB_ADDR_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0x8000 + (port * 0x280) + (cpu_id * 0x28)) : \
	(0x1400 + (port * 0x280) + (cpu_id * 0x28)))
#define CSTORM_SB_HOST_STATUS_BLOCK_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0x8008 + (port * 0x280) + (cpu_id * 0x28)) : \
	(0x1408 + (port * 0x280) + (cpu_id * 0x28)))
#define CSTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x1108 + (function * 0x8)) : (0x5108 + \
	(function * 0x8)))
#define TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x31c0 + (function * 0x20)) : 0xffffffff)
#define TSTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0xa000 : 0x1000)
#define TSTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0xa020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define TSTORM_CLIENT_CONFIG_OFFSET(port, client_id) \
	(IS_E1H_OFFSET ? (0x3358 + (port * 0x3e8) + (client_id * 0x28)) \
	: (0x9c8 + (port * 0x2f8) + (client_id * 0x28)))
#define TSTORM_DEF_SB_HC_DISABLE_OFFSET(function, index) \
	(IS_E1H_OFFSET ? (0xb01a + ((function>>1) * 0x28) + \
	((function&1) * 0xa0) + (index * 0x4)) : (0x141a + (function * \
	0x28) + (index * 0x4)))
#define TSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0xb000 + ((function>>1) * 0x28) + \
	((function&1) * 0xa0)) : (0x1400 + (function * 0x28)))
#define TSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(function) \
	(IS_E1H_OFFSET ? (0xb008 + ((function>>1) * 0x28) + \
	((function&1) * 0xa0)) : (0x1408 + (function * 0x28)))
#define TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2b80 + (function * 0x8)) : (0x4b68 + \
	(function * 0x8)))
#define TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x3000 + (function * 0x38)) : (0x1500 + \
	(function * 0x38)))
#define TSTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x1ad0 : 0xffffffff)
#define TSTORM_HC_BTR_OFFSET(port) \
	(IS_E1H_OFFSET ? (0xb144 + (port * 0x30)) : (0x1454 + (port * 0x18)))
#define TSTORM_INDIRECTION_TABLE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x12c8 + (function * 0x80)) : (0x22c8 + \
	(function * 0x80)))
#define TSTORM_INDIRECTION_TABLE_SIZE 0x80
#define TSTORM_MAC_FILTER_CONFIG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x3008 + (function * 0x38)) : (0x1508 + \
	(function * 0x38)))
#define TSTORM_PER_COUNTER_ID_STATS_OFFSET(port, stats_counter_id) \
	(IS_E1H_OFFSET ? (0x2010 + (port * 0x5b0) + (stats_counter_id * \
	0x50)) : (0x4000 + (port * 0x3f0) + (stats_counter_id * 0x38)))
#define TSTORM_RX_PRODS_OFFSET(port, client_id) \
	(IS_E1H_OFFSET ? (0x3350 + (port * 0x3e8) + (client_id * 0x28)) \
	: (0x9c0 + (port * 0x2f8) + (client_id * 0x28)))
#define TSTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2c00 + (function * 0x8)) : (0x4b88 + \
	(function * 0x8)))
#define TSTORM_TPA_EXIST_OFFSET (IS_E1H_OFFSET ? 0x3b30 : 0x1c20)
#define USTORM_AGG_DATA_OFFSET (IS_E1H_OFFSET ? 0xa040 : 0x2c10)
#define USTORM_AGG_DATA_SIZE (IS_E1H_OFFSET ? 0x2440 : 0x1200)
#define USTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0x8000 : 0x1000)
#define USTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0x8020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define USTORM_CQE_PAGE_BASE_OFFSET(port, clientId) \
	(IS_E1H_OFFSET ? (0x3298 + (port * 0x258) + (clientId * 0x18)) : \
	(0x5450 + (port * 0x1c8) + (clientId * 0x18)))
#define USTORM_DEF_SB_HC_DISABLE_OFFSET(function, index) \
	(IS_E1H_OFFSET ? (0x951a + ((function>>1) * 0x28) + \
	((function&1) * 0xa0) + (index * 0x4)) : (0x191a + (function * \
	0x28) + (index * 0x4)))
#define USTORM_DEF_SB_HOST_SB_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x9500 + ((function>>1) * 0x28) + \
	((function&1) * 0xa0)) : (0x1900 + (function * 0x28)))
#define USTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x9508 + ((function>>1) * 0x28) + \
	((function&1) * 0xa0)) : (0x1908 + (function * 0x28)))
#define USTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x2448 : 0xffffffff)
#define USTORM_HC_BTR_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x9644 + (port * 0xd0)) : (0x1954 + (port * 0xb8)))
#define USTORM_MAX_AGG_SIZE_OFFSET(port, clientId) \
	(IS_E1H_OFFSET ? (0x3290 + (port * 0x258) + (clientId * 0x18)) : \
	(0x5448 + (port * 0x1c8) + (clientId * 0x18)))
#define USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2408 + (function * 0x8)) : (0x5408 + \
	(function * 0x8)))
#define USTORM_SB_HC_DISABLE_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0x901a + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)) : (0x141a + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)))
#define USTORM_SB_HC_TIMEOUT_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0x9018 + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)) : (0x1418 + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)))
#define USTORM_SB_HOST_SB_ADDR_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0x9000 + (port * 0x280) + (cpu_id * 0x28)) : \
	(0x1400 + (port * 0x280) + (cpu_id * 0x28)))
#define USTORM_SB_HOST_STATUS_BLOCK_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0x9008 + (port * 0x280) + (cpu_id * 0x28)) : \
	(0x1408 + (port * 0x280) + (cpu_id * 0x28)))
#define XSTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0x9000 : 0x1000)
#define XSTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0x9020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define XSTORM_CMNG_PER_PORT_VARS_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x24a8 + (port * 0x40)) : (0x3ba0 + (port * 0x40)))
#define XSTORM_DEF_SB_HC_DISABLE_OFFSET(function, index) \
	(IS_E1H_OFFSET ? (0xa01a + ((function>>1) * 0x28) + \
	((function&1) * 0xa0) + (index * 0x4)) : (0x141a + (function * \
	0x28) + (index * 0x4)))
#define XSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0xa000 + ((function>>1) * 0x28) + \
	((function&1) * 0xa0)) : (0x1400 + (function * 0x28)))
#define XSTORM_DEF_SB_HOST_STATUS_BLOCK_OFFSET(function) \
	(IS_E1H_OFFSET ? (0xa008 + ((function>>1) * 0x28) + \
	((function&1) * 0xa0)) : (0x1408 + (function * 0x28)))
#define XSTORM_E1HOV_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2ab8 + (function * 0x2)) : 0xffffffff)
#define XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2418 + (function * 0x8)) : (0x3b70 + \
	(function * 0x8)))
#define XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2568 + (function * 0x70)) : (0x3c60 + \
	(function * 0x70)))
#define XSTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x2ac8 : 0xffffffff)
#define XSTORM_HC_BTR_OFFSET(port) \
	(IS_E1H_OFFSET ? (0xa144 + (port * 0x30)) : (0x1454 + (port * 0x18)))
#define XSTORM_PER_COUNTER_ID_STATS_OFFSET(port, stats_counter_id) \
	(IS_E1H_OFFSET ? (0xc000 + (port * 0x3f0) + (stats_counter_id * \
	0x38)) : (0x3378 + (port * 0x3f0) + (stats_counter_id * 0x38)))
#define XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2528 + (function * 0x70)) : (0x3c20 + \
	(function * 0x70)))
#define XSTORM_SPQ_PAGE_BASE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2000 + (function * 0x10)) : (0x3328 + \
	(function * 0x10)))
#define XSTORM_SPQ_PROD_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2008 + (function * 0x10)) : (0x3330 + \
	(function * 0x10)))
#define XSTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x23d8 + (function * 0x8)) : (0x3b60 + \
	(function * 0x8)))
#define COMMON_ASM_INVALID_ASSERT_OPCODE 0x0

/**
* This file defines HSI constatnts for the ETH flow
*/
#ifdef _EVEREST_MICROCODE
#include "microcode_constants.h"
#include "eth_rx_bd.h"
#include "eth_tx_bd.h"
#include "eth_rx_cqe.h"
#include "eth_rx_sge.h"
#include "eth_rx_cqe_next_page.h"
#endif

/* RSS hash types */
#define DEFAULT_HASH_TYPE 0
#define IPV4_HASH_TYPE 1
#define TCP_IPV4_HASH_TYPE 2
#define IPV6_HASH_TYPE 3
#define TCP_IPV6_HASH_TYPE 4

/* Ethernet Ring parmaters */
#define X_ETH_LOCAL_RING_SIZE 13
#define FIRST_BD_IN_PKT 0
#define PARSE_BD_INDEX 1
#define NUM_OF_ETH_BDS_IN_PAGE \
	((PAGE_SIZE) / (STRUCT_SIZE(eth_tx_bd)/8))


/* Rx ring params */
#define U_ETH_LOCAL_BD_RING_SIZE (16)
#define U_ETH_LOCAL_SGE_RING_SIZE (12)
#define U_ETH_SGL_SIZE (8)


#define U_ETH_BDS_PER_PAGE_MASK \
	((PAGE_SIZE/(STRUCT_SIZE(eth_rx_bd)/8))-1)
#define U_ETH_CQE_PER_PAGE_MASK \
	((PAGE_SIZE/(STRUCT_SIZE(eth_rx_cqe)/8))-1)
#define U_ETH_SGES_PER_PAGE_MASK \
	((PAGE_SIZE/(STRUCT_SIZE(eth_rx_sge)/8))-1)

#define U_ETH_SGES_PER_PAGE_INVERSE_MASK \
	(0xFFFF - ((PAGE_SIZE/((STRUCT_SIZE(eth_rx_sge))/8))-1))


#define TU_ETH_CQES_PER_PAGE \
	(PAGE_SIZE/(STRUCT_SIZE(eth_rx_cqe_next_page)/8))
#define U_ETH_BDS_PER_PAGE (PAGE_SIZE/(STRUCT_SIZE(eth_rx_bd)/8))
#define U_ETH_SGES_PER_PAGE (PAGE_SIZE/(STRUCT_SIZE(eth_rx_sge)/8))

#define U_ETH_UNDEFINED_Q 0xFF

/* values of command IDs in the ramrod message */
#define RAMROD_CMD_ID_ETH_PORT_SETUP (80)
#define RAMROD_CMD_ID_ETH_CLIENT_SETUP (85)
#define RAMROD_CMD_ID_ETH_STAT_QUERY (90)
#define RAMROD_CMD_ID_ETH_UPDATE (100)
#define RAMROD_CMD_ID_ETH_HALT (105)
#define RAMROD_CMD_ID_ETH_SET_MAC (110)
#define RAMROD_CMD_ID_ETH_CFC_DEL (115)
#define RAMROD_CMD_ID_ETH_PORT_DEL (120)
#define RAMROD_CMD_ID_ETH_FORWARD_SETUP (125)


/* command values for set mac command */
#define T_ETH_MAC_COMMAND_SET 0
#define T_ETH_MAC_COMMAND_INVALIDATE 1

#define T_ETH_INDIRECTION_TABLE_SIZE 128

/*The CRC32 seed, that is used for the hash(reduction) multicast address */
#define T_ETH_CRC32_HASH_SEED 0x00000000

/* Maximal L2 clients supported */
#define ETH_MAX_RX_CLIENTS_E1 19
#define ETH_MAX_RX_CLIENTS_E1H 25

/* Maximal aggregation queues supported */
#define ETH_MAX_AGGREGATION_QUEUES_E1 (32)
#define ETH_MAX_AGGREGATION_QUEUES_E1H (64)


/**
* This file defines HSI constatnts common to all microcode flows
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


#define PROTOCOL_STATE_BIT_OFFSET 6

#define ETH_STATE (ETH_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define TOE_STATE (TOE_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define RDMA_STATE (RDMA_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define ISCSI_STATE \
	(ISCSI_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define FCOE_STATE (FCOE_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)

/* microcode fixed page page size 4K (chains and ring segments) */
#define MC_PAGE_SIZE (4096)


/* Host coalescing constants */

/* index numbers */
#define HC_USTORM_DEF_SB_NUM_INDICES 4
#define HC_CSTORM_DEF_SB_NUM_INDICES 8
#define HC_XSTORM_DEF_SB_NUM_INDICES 4
#define HC_TSTORM_DEF_SB_NUM_INDICES 4
#define HC_USTORM_SB_NUM_INDICES 4
#define HC_CSTORM_SB_NUM_INDICES 4

/* index values - which counterto update */

#define HC_INDEX_U_TOE_RX_CQ_CONS 0
#define HC_INDEX_U_ETH_RX_CQ_CONS 1
#define HC_INDEX_U_ETH_RX_BD_CONS 2
#define HC_INDEX_U_FCOE_EQ_CONS 3

#define HC_INDEX_C_TOE_TX_CQ_CONS 0
#define HC_INDEX_C_ETH_TX_CQ_CONS 1
#define HC_INDEX_C_ISCSI_EQ_CONS 2

#define HC_INDEX_DEF_X_SPQ_CONS 0

#define HC_INDEX_DEF_C_RDMA_EQ_CONS 0
#define HC_INDEX_DEF_C_RDMA_NAL_PROD 1
#define HC_INDEX_DEF_C_ETH_FW_TX_CQ_CONS 2
#define HC_INDEX_DEF_C_ETH_SLOW_PATH 3
#define HC_INDEX_DEF_C_ETH_RDMA_CQ_CONS 4
#define HC_INDEX_DEF_C_ETH_ISCSI_CQ_CONS 5

#define HC_INDEX_DEF_U_ETH_RDMA_RX_CQ_CONS 0
#define HC_INDEX_DEF_U_ETH_ISCSI_RX_CQ_CONS 1
#define HC_INDEX_DEF_U_ETH_RDMA_RX_BD_CONS 2
#define HC_INDEX_DEF_U_ETH_ISCSI_RX_BD_CONS 3


/* used by the driver to get the SB offset */
#define USTORM_ID 0
#define CSTORM_ID 1
#define XSTORM_ID 2
#define TSTORM_ID 3
#define ATTENTION_ID 4

/* max number of slow path commands per port */
#define MAX_RAMRODS_PER_PORT (8)

/* values for RX ETH CQE type field */
#define RX_ETH_CQE_TYPE_ETH_FASTPATH (0)
#define RX_ETH_CQE_TYPE_ETH_RAMROD (1)


/**** DEFINES FOR TIMERS/CLOCKS RESOLUTIONS ****/
#define EMULATION_FREQUENCY_FACTOR (1600)
#define FPGA_FREQUENCY_FACTOR (100)

#define TIMERS_TICK_SIZE_CHIP (1e-3)
#define TIMERS_TICK_SIZE_EMUL \
 ((TIMERS_TICK_SIZE_CHIP)/((EMULATION_FREQUENCY_FACTOR)))
#define TIMERS_TICK_SIZE_FPGA \
 ((TIMERS_TICK_SIZE_CHIP)/((FPGA_FREQUENCY_FACTOR)))

#define TSEMI_CLK1_RESUL_CHIP (1e-3)
#define TSEMI_CLK1_RESUL_EMUL \
 ((TSEMI_CLK1_RESUL_CHIP)/(EMULATION_FREQUENCY_FACTOR))
#define TSEMI_CLK1_RESUL_FPGA \
 ((TSEMI_CLK1_RESUL_CHIP)/(FPGA_FREQUENCY_FACTOR))

#define USEMI_CLK1_RESUL_CHIP \
 (TIMERS_TICK_SIZE_CHIP)
#define USEMI_CLK1_RESUL_EMUL \
 (TIMERS_TICK_SIZE_EMUL)
#define USEMI_CLK1_RESUL_FPGA \
 (TIMERS_TICK_SIZE_FPGA)

#define XSEMI_CLK1_RESUL_CHIP (1e-3)
#define XSEMI_CLK1_RESUL_EMUL \
 ((XSEMI_CLK1_RESUL_CHIP)/(EMULATION_FREQUENCY_FACTOR))
#define XSEMI_CLK1_RESUL_FPGA \
 ((XSEMI_CLK1_RESUL_CHIP)/(FPGA_FREQUENCY_FACTOR))

#define XSEMI_CLK2_RESUL_CHIP (1e-6)
#define XSEMI_CLK2_RESUL_EMUL \
 ((XSEMI_CLK2_RESUL_CHIP)/(EMULATION_FREQUENCY_FACTOR))
#define XSEMI_CLK2_RESUL_FPGA \
 ((XSEMI_CLK2_RESUL_CHIP)/(FPGA_FREQUENCY_FACTOR))

#define SDM_TIMER_TICK_RESUL_CHIP (4*(1e-6))
#define SDM_TIMER_TICK_RESUL_EMUL \
 ((SDM_TIMER_TICK_RESUL_CHIP)/(EMULATION_FREQUENCY_FACTOR))
#define SDM_TIMER_TICK_RESUL_FPGA \
 ((SDM_TIMER_TICK_RESUL_CHIP)/(FPGA_FREQUENCY_FACTOR))


/**** END DEFINES FOR TIMERS/CLOCKS RESOLUTIONS ****/
#define XSTORM_IP_ID_ROLL_HALF 0x8000
#define XSTORM_IP_ID_ROLL_ALL 0

#define FW_LOG_LIST_SIZE (50)

#define NUM_OF_PROTOCOLS 4
#define MAX_COS_NUMBER 16
#define MAX_T_STAT_COUNTER_ID 18
#define MAX_X_STAT_COUNTER_ID 18

#define UNKNOWN_ADDRESS 0
#define UNICAST_ADDRESS 1
#define MULTICAST_ADDRESS 2
#define BROADCAST_ADDRESS 3

#define SINGLE_FUNCTION 0
#define MULTI_FUNCTION 1

#define IP_V4 0
#define IP_V6 1

