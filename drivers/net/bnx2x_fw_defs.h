/* bnx2x_fw_defs.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2008 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */


#define CSTORM_DEF_SB_HC_DISABLE_OFFSET(port, index)\
	(0x1922 + (port * 0x40) + (index * 0x4))
#define CSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)\
	(0x1900 + (port * 0x40))
#define CSTORM_HC_BTR_OFFSET(port)\
	(0x1984 + (port * 0xc0))
#define CSTORM_SB_HC_DISABLE_OFFSET(port, cpu_id, index)\
	(0x141a + (port * 0x280) + (cpu_id * 0x28) + (index * 0x4))
#define CSTORM_SB_HC_TIMEOUT_OFFSET(port, cpu_id, index)\
	(0x1418 + (port * 0x280) + (cpu_id * 0x28) + (index * 0x4))
#define CSTORM_SB_HOST_SB_ADDR_OFFSET(port, cpu_id)\
	(0x1400 + (port * 0x280) + (cpu_id * 0x28))
#define CSTORM_STATS_FLAGS_OFFSET(port) 		(0x5108 + (port * 0x8))
#define TSTORM_CLIENT_CONFIG_OFFSET(port, client_id)\
	(0x1510 + (port * 0x240) + (client_id * 0x20))
#define TSTORM_DEF_SB_HC_DISABLE_OFFSET(port, index)\
	(0x138a + (port * 0x28) + (index * 0x4))
#define TSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)\
	(0x1370 + (port * 0x28))
#define TSTORM_ETH_STATS_QUERY_ADDR_OFFSET(port)\
	(0x4b70 + (port * 0x8))
#define TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(function)\
	(0x1418 + (function * 0x30))
#define TSTORM_HC_BTR_OFFSET(port)\
	(0x13c4 + (port * 0x18))
#define TSTORM_INDIRECTION_TABLE_OFFSET(port)\
	(0x22c8 + (port * 0x80))
#define TSTORM_INDIRECTION_TABLE_SIZE			0x80
#define TSTORM_MAC_FILTER_CONFIG_OFFSET(port)\
	(0x1420 + (port * 0x30))
#define TSTORM_RCQ_PROD_OFFSET(port, client_id)\
	(0x1508 + (port * 0x240) + (client_id * 0x20))
#define TSTORM_STATS_FLAGS_OFFSET(port) 		(0x4b90 + (port * 0x8))
#define USTORM_DEF_SB_HC_DISABLE_OFFSET(port, index)\
	(0x191a + (port * 0x28) + (index * 0x4))
#define USTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)\
	(0x1900 + (port * 0x28))
#define USTORM_HC_BTR_OFFSET(port)\
	(0x1954 + (port * 0xb8))
#define USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(port)\
	(0x5408 + (port * 0x8))
#define USTORM_SB_HC_DISABLE_OFFSET(port, cpu_id, index)\
	(0x141a + (port * 0x280) + (cpu_id * 0x28) + (index * 0x4))
#define USTORM_SB_HC_TIMEOUT_OFFSET(port, cpu_id, index)\
	(0x1418 + (port * 0x280) + (cpu_id * 0x28) + (index * 0x4))
#define USTORM_SB_HOST_SB_ADDR_OFFSET(port, cpu_id)\
	(0x1400 + (port * 0x280) + (cpu_id * 0x28))
#define XSTORM_ASSERT_LIST_INDEX_OFFSET 		0x1000
#define XSTORM_ASSERT_LIST_OFFSET(idx)			(0x1020 + (idx * 0x10))
#define XSTORM_DEF_SB_HC_DISABLE_OFFSET(port, index)\
	(0x141a + (port * 0x28) + (index * 0x4))
#define XSTORM_DEF_SB_HOST_SB_ADDR_OFFSET(port)\
	(0x1400 + (port * 0x28))
#define XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(port)\
	(0x5408 + (port * 0x8))
#define XSTORM_HC_BTR_OFFSET(port)\
	(0x1454 + (port * 0x18))
#define XSTORM_SPQ_PAGE_BASE_OFFSET(port)\
	(0x5328 + (port * 0x18))
#define XSTORM_SPQ_PROD_OFFSET(port)\
	(0x5330 + (port * 0x18))
#define XSTORM_STATS_FLAGS_OFFSET(port) 		(0x53f8 + (port * 0x8))
#define COMMON_ASM_INVALID_ASSERT_OPCODE 0x0

/**
* This file defines HSI constatnts for the ETH flow
*/

/* hash types */
#define DEFAULT_HASH_TYPE			0
#define IPV4_HASH_TYPE				1
#define TCP_IPV4_HASH_TYPE			2
#define IPV6_HASH_TYPE				3
#define TCP_IPV6_HASH_TYPE			4

/* values of command IDs in the ramrod message */
#define RAMROD_CMD_ID_ETH_PORT_SETUP			(80)
#define RAMROD_CMD_ID_ETH_CLIENT_SETUP			(85)
#define RAMROD_CMD_ID_ETH_STAT_QUERY			(90)
#define RAMROD_CMD_ID_ETH_UPDATE			(100)
#define RAMROD_CMD_ID_ETH_HALT				(105)
#define RAMROD_CMD_ID_ETH_SET_MAC			(110)
#define RAMROD_CMD_ID_ETH_CFC_DEL			(115)
#define RAMROD_CMD_ID_ETH_PORT_DEL			(120)
#define RAMROD_CMD_ID_ETH_FORWARD_SETUP 		(125)


/* command values for set mac command */
#define T_ETH_MAC_COMMAND_SET				0
#define T_ETH_MAC_COMMAND_INVALIDATE			1

#define T_ETH_INDIRECTION_TABLE_SIZE			128

/* Maximal L2 clients supported */
#define ETH_MAX_RX_CLIENTS				(18)

/**
* This file defines HSI constatnts common to all microcode flows
*/

/* Connection types */
#define ETH_CONNECTION_TYPE			0

#define PROTOCOL_STATE_BIT_OFFSET		6

#define ETH_STATE	(ETH_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)

/* microcode fixed page page size 4K (chains and ring segments) */
#define MC_PAGE_SIZE						(4096)

/* Host coalescing constants */

/* IGU constants */
#define IGU_PORT_BASE				0x0400

#define IGU_ADDR_MSIX				0x0000
#define IGU_ADDR_INT_ACK			0x0200
#define IGU_ADDR_PROD_UPD			0x0201
#define IGU_ADDR_ATTN_BITS_UPD			0x0202
#define IGU_ADDR_ATTN_BITS_SET			0x0203
#define IGU_ADDR_ATTN_BITS_CLR			0x0204
#define IGU_ADDR_COALESCE_NOW			0x0205
#define IGU_ADDR_SIMD_MASK			0x0206
#define IGU_ADDR_SIMD_NOMASK			0x0207
#define IGU_ADDR_MSI_CTL			0x0210
#define IGU_ADDR_MSI_ADDR_LO			0x0211
#define IGU_ADDR_MSI_ADDR_HI			0x0212
#define IGU_ADDR_MSI_DATA			0x0213

#define IGU_INT_ENABLE				0
#define IGU_INT_DISABLE 			1
#define IGU_INT_NOP				2
#define IGU_INT_NOP2				3

/* index numbers */
#define HC_USTORM_DEF_SB_NUM_INDICES		4
#define HC_CSTORM_DEF_SB_NUM_INDICES		8
#define HC_XSTORM_DEF_SB_NUM_INDICES		4
#define HC_TSTORM_DEF_SB_NUM_INDICES		4
#define HC_USTORM_SB_NUM_INDICES		4
#define HC_CSTORM_SB_NUM_INDICES		4

/* index values - which counterto update */

#define HC_INDEX_U_ETH_RX_CQ_CONS		1

#define HC_INDEX_C_ETH_TX_CQ_CONS		1

#define HC_INDEX_DEF_X_SPQ_CONS 		0

#define HC_INDEX_DEF_C_ETH_FW_TX_CQ_CONS	2
#define HC_INDEX_DEF_C_ETH_SLOW_PATH		3

/* used by the driver to get the SB offset */
#define USTORM_ID			0
#define CSTORM_ID			1
#define XSTORM_ID			2
#define TSTORM_ID			3
#define ATTENTION_ID			4

/* max number of slow path commands per port */
#define MAX_RAMRODS_PER_PORT		(8)

/* values for RX ETH CQE type field */
#define RX_ETH_CQE_TYPE_ETH_FASTPATH	(0)
#define RX_ETH_CQE_TYPE_ETH_RAMROD		(1)

/* MAC address list size */
#define T_MAC_ADDRESS_LIST_SIZE 	(96)

#define XSTORM_IP_ID_ROLL_HALF 0x8000
#define XSTORM_IP_ID_ROLL_ALL 0

#define FW_LOG_LIST_SIZE	(50)

#define NUM_OF_PROTOCOLS		4
#define MAX_COS_NUMBER			16
#define MAX_T_STAT_COUNTER_ID	18

#define T_FAIR							1
#define FAIR_MEM						2
#define RS_PERIODIC_TIMEOUT_IN_SDM_TICS 25

#define UNKNOWN_ADDRESS 	0
#define UNICAST_ADDRESS 	1
#define MULTICAST_ADDRESS	2
#define BROADCAST_ADDRESS	3

