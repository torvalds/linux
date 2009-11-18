/* bnx2x_fw_defs.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */


#define CSTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0x7000 : 0x1000)
#define CSTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0x7020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define CSTORM_DEF_SB_HC_DISABLE_C_OFFSET(function, index) \
	(IS_E1H_OFFSET ? (0x8622 + ((function>>1) * 0x40) + \
	((function&1) * 0x100) + (index * 0x4)) : (0x3562 + (function * \
	0x40) + (index * 0x4)))
#define CSTORM_DEF_SB_HC_DISABLE_U_OFFSET(function, index) \
	(IS_E1H_OFFSET ? (0x8822 + ((function>>1) * 0x80) + \
	((function&1) * 0x200) + (index * 0x4)) : (0x35e2 + (function * \
	0x80) + (index * 0x4)))
#define CSTORM_DEF_SB_HOST_SB_ADDR_C_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8600 + ((function>>1) * 0x40) + \
	((function&1) * 0x100)) : (0x3540 + (function * 0x40)))
#define CSTORM_DEF_SB_HOST_SB_ADDR_U_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8800 + ((function>>1) * 0x80) + \
	((function&1) * 0x200)) : (0x35c0 + (function * 0x80)))
#define CSTORM_DEF_SB_HOST_STATUS_BLOCK_C_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8608 + ((function>>1) * 0x40) + \
	((function&1) * 0x100)) : (0x3548 + (function * 0x40)))
#define CSTORM_DEF_SB_HOST_STATUS_BLOCK_U_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8808 + ((function>>1) * 0x80) + \
	((function&1) * 0x200)) : (0x35c8 + (function * 0x80)))
#define CSTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x11e8 : 0xffffffff)
#define CSTORM_HC_BTR_C_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x8c04 + (port * 0xf0)) : (0x36c4 + (port * 0xc0)))
#define CSTORM_HC_BTR_U_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x8de4 + (port * 0xf0)) : (0x3844 + (port * 0xc0)))
#define CSTORM_ISCSI_CQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6680 + (function * 0x8)) : (0x25a0 + \
	(function * 0x8)))
#define CSTORM_ISCSI_CQ_SQN_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x66c0 + (function * 0x8)) : (0x25b0 + \
	(function * 0x8)))
#define CSTORM_ISCSI_EQ_CONS_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x6040 + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x2410 + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x6044 + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x2414 + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x604c + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x241c + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_VALID_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x6057 + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x2427 + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_EQ_PROD_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x6042 + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x2412 + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_EQ_SB_INDEX_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x6056 + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x2426 + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_EQ_SB_NUM_OFFSET(function, eqIdx) \
	(IS_E1H_OFFSET ? (0x6054 + (function * 0xc0) + (eqIdx * 0x18)) : \
	(0x2424 + (function * 0xc0) + (eqIdx * 0x18)))
#define CSTORM_ISCSI_HQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6640 + (function * 0x8)) : (0x2590 + \
	(function * 0x8)))
#define CSTORM_ISCSI_NUM_OF_TASKS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6004 + (function * 0x8)) : (0x2404 + \
	(function * 0x8)))
#define CSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6002 + (function * 0x8)) : (0x2402 + \
	(function * 0x8)))
#define CSTORM_ISCSI_PAGE_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6000 + (function * 0x8)) : (0x2400 + \
	(function * 0x8)))
#define CSTORM_SB_HC_DISABLE_C_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0x811a + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)) : (0x305a + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)))
#define CSTORM_SB_HC_DISABLE_U_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0xb01a + (port * 0x800) + (cpu_id * 0x80) + \
	(index * 0x4)) : (0x401a + (port * 0x800) + (cpu_id * 0x80) + \
	(index * 0x4)))
#define CSTORM_SB_HC_TIMEOUT_C_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0x8118 + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)) : (0x3058 + (port * 0x280) + (cpu_id * 0x28) + \
	(index * 0x4)))
#define CSTORM_SB_HC_TIMEOUT_U_OFFSET(port, cpu_id, index) \
	(IS_E1H_OFFSET ? (0xb018 + (port * 0x800) + (cpu_id * 0x80) + \
	(index * 0x4)) : (0x4018 + (port * 0x800) + (cpu_id * 0x80) + \
	(index * 0x4)))
#define CSTORM_SB_HOST_SB_ADDR_C_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0x8100 + (port * 0x280) + (cpu_id * 0x28)) : \
	(0x3040 + (port * 0x280) + (cpu_id * 0x28)))
#define CSTORM_SB_HOST_SB_ADDR_U_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0xb000 + (port * 0x800) + (cpu_id * 0x80)) : \
	(0x4000 + (port * 0x800) + (cpu_id * 0x80)))
#define CSTORM_SB_HOST_STATUS_BLOCK_C_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0x8108 + (port * 0x280) + (cpu_id * 0x28)) : \
	(0x3048 + (port * 0x280) + (cpu_id * 0x28)))
#define CSTORM_SB_HOST_STATUS_BLOCK_U_OFFSET(port, cpu_id) \
	(IS_E1H_OFFSET ? (0xb008 + (port * 0x800) + (cpu_id * 0x80)) : \
	(0x4008 + (port * 0x800) + (cpu_id * 0x80)))
#define CSTORM_SB_STATUS_BLOCK_C_SIZE 0x10
#define CSTORM_SB_STATUS_BLOCK_U_SIZE 0x60
#define CSTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x1108 + (function * 0x8)) : (0x5108 + \
	(function * 0x8)))
#define TSTORM_APPROXIMATE_MATCH_MULTICAST_FILTERING_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x3200 + (function * 0x20)) : 0xffffffff)
#define TSTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0xa000 : 0x1000)
#define TSTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0xa020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define TSTORM_CLIENT_CONFIG_OFFSET(port, client_id) \
	(IS_E1H_OFFSET ? (0x33a0 + (port * 0x1a0) + (client_id * 0x10)) \
	: (0x9c0 + (port * 0x120) + (client_id * 0x10)))
#define TSTORM_COMMON_SAFC_WORKAROUND_ENABLE_OFFSET \
	(IS_E1H_OFFSET ? 0x1ed8 : 0xffffffff)
#define TSTORM_COMMON_SAFC_WORKAROUND_TIMEOUT_10USEC_OFFSET \
	(IS_E1H_OFFSET ? 0x1eda : 0xffffffff)
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
	(IS_E1H_OFFSET ? (0x2940 + (function * 0x8)) : (0x4928 + \
	(function * 0x8)))
#define TSTORM_FUNCTION_COMMON_CONFIG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x3000 + (function * 0x40)) : (0x1500 + \
	(function * 0x40)))
#define TSTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x1ed0 : 0xffffffff)
#define TSTORM_HC_BTR_OFFSET(port) \
	(IS_E1H_OFFSET ? (0xb144 + (port * 0x30)) : (0x1454 + (port * 0x18)))
#define TSTORM_INDIRECTION_TABLE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x12c8 + (function * 0x80)) : (0x22c8 + \
	(function * 0x80)))
#define TSTORM_INDIRECTION_TABLE_SIZE 0x80
#define TSTORM_ISCSI_CONN_BUF_PBL_OFFSET(function, pblEntry) \
	(IS_E1H_OFFSET ? (0x60c0 + (function * 0x40) + (pblEntry * 0x8)) \
	: (0x4c30 + (function * 0x40) + (pblEntry * 0x8)))
#define TSTORM_ISCSI_ERROR_BITMAP_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6340 + (function * 0x8)) : (0x4cd0 + \
	(function * 0x8)))
#define TSTORM_ISCSI_NUM_OF_TASKS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6004 + (function * 0x8)) : (0x4c04 + \
	(function * 0x8)))
#define TSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6002 + (function * 0x8)) : (0x4c02 + \
	(function * 0x8)))
#define TSTORM_ISCSI_PAGE_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6000 + (function * 0x8)) : (0x4c00 + \
	(function * 0x8)))
#define TSTORM_ISCSI_RQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6080 + (function * 0x8)) : (0x4c20 + \
	(function * 0x8)))
#define TSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6040 + (function * 0x8)) : (0x4c10 + \
	(function * 0x8)))
#define TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6042 + (function * 0x8)) : (0x4c12 + \
	(function * 0x8)))
#define TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x6044 + (function * 0x8)) : (0x4c14 + \
	(function * 0x8)))
#define TSTORM_MAC_FILTER_CONFIG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x3008 + (function * 0x40)) : (0x1508 + \
	(function * 0x40)))
#define TSTORM_PER_COUNTER_ID_STATS_OFFSET(port, stats_counter_id) \
	(IS_E1H_OFFSET ? (0x2010 + (port * 0x490) + (stats_counter_id * \
	0x40)) : (0x4010 + (port * 0x490) + (stats_counter_id * 0x40)))
#define TSTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x29c0 + (function * 0x8)) : (0x4948 + \
	(function * 0x8)))
#define TSTORM_TCP_MAX_CWND_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x4004 + (function * 0x8)) : (0x1fb4 + \
	(function * 0x8)))
#define USTORM_AGG_DATA_OFFSET (IS_E1H_OFFSET ? 0xa000 : 0x3000)
#define USTORM_AGG_DATA_SIZE (IS_E1H_OFFSET ? 0x2000 : 0x1000)
#define USTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0x8000 : 0x1000)
#define USTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0x8020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define USTORM_CQE_PAGE_BASE_OFFSET(port, clientId) \
	(IS_E1H_OFFSET ? (0x1010 + (port * 0x680) + (clientId * 0x40)) : \
	(0x4010 + (port * 0x360) + (clientId * 0x30)))
#define USTORM_CQE_PAGE_NEXT_OFFSET(port, clientId) \
	(IS_E1H_OFFSET ? (0x1028 + (port * 0x680) + (clientId * 0x40)) : \
	(0x4028 + (port * 0x360) + (clientId * 0x30)))
#define USTORM_ETH_PAUSE_ENABLED_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x2ad4 + (port * 0x8)) : 0xffffffff)
#define USTORM_ETH_RING_PAUSE_DATA_OFFSET(port, clientId) \
	(IS_E1H_OFFSET ? (0x1030 + (port * 0x680) + (clientId * 0x40)) : \
	0xffffffff)
#define USTORM_ETH_STATS_QUERY_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2a50 + (function * 0x8)) : (0x1dd0 + \
	(function * 0x8)))
#define USTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x2448 : 0xffffffff)
#define USTORM_ISCSI_CQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7044 + (function * 0x8)) : (0x2414 + \
	(function * 0x8)))
#define USTORM_ISCSI_CQ_SQN_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7046 + (function * 0x8)) : (0x2416 + \
	(function * 0x8)))
#define USTORM_ISCSI_ERROR_BITMAP_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7688 + (function * 0x8)) : (0x29c8 + \
	(function * 0x8)))
#define USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7648 + (function * 0x8)) : (0x29b8 + \
	(function * 0x8)))
#define USTORM_ISCSI_NUM_OF_TASKS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7004 + (function * 0x8)) : (0x2404 + \
	(function * 0x8)))
#define USTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7002 + (function * 0x8)) : (0x2402 + \
	(function * 0x8)))
#define USTORM_ISCSI_PAGE_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7000 + (function * 0x8)) : (0x2400 + \
	(function * 0x8)))
#define USTORM_ISCSI_R2TQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7040 + (function * 0x8)) : (0x2410 + \
	(function * 0x8)))
#define USTORM_ISCSI_RQ_BUFFER_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7080 + (function * 0x8)) : (0x2420 + \
	(function * 0x8)))
#define USTORM_ISCSI_RQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x7084 + (function * 0x8)) : (0x2424 + \
	(function * 0x8)))
#define USTORM_MAX_AGG_SIZE_OFFSET(port, clientId) \
	(IS_E1H_OFFSET ? (0x1018 + (port * 0x680) + (clientId * 0x40)) : \
	(0x4018 + (port * 0x360) + (clientId * 0x30)))
#define USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2408 + (function * 0x8)) : (0x1da8 + \
	(function * 0x8)))
#define USTORM_PER_COUNTER_ID_STATS_OFFSET(port, stats_counter_id) \
	(IS_E1H_OFFSET ? (0x2450 + (port * 0x2d0) + (stats_counter_id * \
	0x28)) : (0x1500 + (port * 0x2d0) + (stats_counter_id * 0x28)))
#define USTORM_RX_PRODS_OFFSET(port, client_id) \
	(IS_E1H_OFFSET ? (0x1000 + (port * 0x680) + (client_id * 0x40)) \
	: (0x4000 + (port * 0x360) + (client_id * 0x30)))
#define USTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x29f0 + (function * 0x8)) : (0x1db8 + \
	(function * 0x8)))
#define USTORM_TPA_BTR_OFFSET (IS_E1H_OFFSET ? 0x3da5 : 0x5095)
#define USTORM_TPA_BTR_SIZE 0x1
#define XSTORM_ASSERT_LIST_INDEX_OFFSET \
	(IS_E1H_OFFSET ? 0x9000 : 0x1000)
#define XSTORM_ASSERT_LIST_OFFSET(idx) \
	(IS_E1H_OFFSET ? (0x9020 + (idx * 0x10)) : (0x1020 + (idx * 0x10)))
#define XSTORM_CMNG_PER_PORT_VARS_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x24a8 + (port * 0x50)) : (0x3a80 + (port * 0x50)))
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
	(IS_E1H_OFFSET ? (0x2c10 + (function * 0x8)) : 0xffffffff)
#define XSTORM_ETH_STATS_QUERY_ADDR_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2418 + (function * 0x8)) : (0x3a50 + \
	(function * 0x8)))
#define XSTORM_FAIRNESS_PER_VN_VARS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2588 + (function * 0x90)) : (0x3b60 + \
	(function * 0x90)))
#define XSTORM_FUNCTION_MODE_OFFSET \
	(IS_E1H_OFFSET ? 0x2c50 : 0xffffffff)
#define XSTORM_HC_BTR_OFFSET(port) \
	(IS_E1H_OFFSET ? (0xa144 + (port * 0x30)) : (0x1454 + (port * 0x18)))
#define XSTORM_ISCSI_HQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x80c0 + (function * 0x8)) : (0x1c30 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR0_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8080 + (function * 0x8)) : (0x1c20 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR1_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8081 + (function * 0x8)) : (0x1c21 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR2_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8082 + (function * 0x8)) : (0x1c22 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR3_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8083 + (function * 0x8)) : (0x1c23 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR4_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8084 + (function * 0x8)) : (0x1c24 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_MAC_ADDR5_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8085 + (function * 0x8)) : (0x1c25 + \
	(function * 0x8)))
#define XSTORM_ISCSI_LOCAL_VLAN_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8086 + (function * 0x8)) : (0x1c26 + \
	(function * 0x8)))
#define XSTORM_ISCSI_NUM_OF_TASKS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8004 + (function * 0x8)) : (0x1c04 + \
	(function * 0x8)))
#define XSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8002 + (function * 0x8)) : (0x1c02 + \
	(function * 0x8)))
#define XSTORM_ISCSI_PAGE_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8000 + (function * 0x8)) : (0x1c00 + \
	(function * 0x8)))
#define XSTORM_ISCSI_R2TQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x80c4 + (function * 0x8)) : (0x1c34 + \
	(function * 0x8)))
#define XSTORM_ISCSI_SQ_SIZE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x80c2 + (function * 0x8)) : (0x1c32 + \
	(function * 0x8)))
#define XSTORM_ISCSI_TCP_VARS_ADV_WND_SCL_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8043 + (function * 0x8)) : (0x1c13 + \
	(function * 0x8)))
#define XSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8042 + (function * 0x8)) : (0x1c12 + \
	(function * 0x8)))
#define XSTORM_ISCSI_TCP_VARS_TOS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8041 + (function * 0x8)) : (0x1c11 + \
	(function * 0x8)))
#define XSTORM_ISCSI_TCP_VARS_TTL_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x8040 + (function * 0x8)) : (0x1c10 + \
	(function * 0x8)))
#define XSTORM_PER_COUNTER_ID_STATS_OFFSET(port, stats_counter_id) \
	(IS_E1H_OFFSET ? (0xc000 + (port * 0x360) + (stats_counter_id * \
	0x30)) : (0x3378 + (port * 0x360) + (stats_counter_id * 0x30)))
#define XSTORM_RATE_SHAPING_PER_VN_VARS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2548 + (function * 0x90)) : (0x3b20 + \
	(function * 0x90)))
#define XSTORM_SPQ_PAGE_BASE_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2000 + (function * 0x10)) : (0x3328 + \
	(function * 0x10)))
#define XSTORM_SPQ_PROD_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x2008 + (function * 0x10)) : (0x3330 + \
	(function * 0x10)))
#define XSTORM_STATS_FLAGS_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x23d8 + (function * 0x8)) : (0x3a40 + \
	(function * 0x8)))
#define XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_ENABLED_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x4000 + (port * 0x8)) : (0x1960 + (port * 0x8)))
#define XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_MAX_COUNT_OFFSET(port) \
	(IS_E1H_OFFSET ? (0x4001 + (port * 0x8)) : (0x1961 + (port * 0x8)))
#define XSTORM_TCP_TX_SWS_TIMER_VAL_OFFSET(function) \
	(IS_E1H_OFFSET ? (0x4060 + ((function>>1) * 0x8) + ((function&1) \
	* 0x4)) : (0x1978 + (function * 0x4)))
#define COMMON_ASM_INVALID_ASSERT_OPCODE 0x0

/**
* This file defines HSI constants for the ETH flow
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

/* Rx ring params */
#define U_ETH_LOCAL_BD_RING_SIZE 8
#define U_ETH_LOCAL_SGE_RING_SIZE 10
#define U_ETH_SGL_SIZE 8


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
#define RAMROD_CMD_ID_ETH_PORT_SETUP 80
#define RAMROD_CMD_ID_ETH_CLIENT_SETUP 85
#define RAMROD_CMD_ID_ETH_STAT_QUERY 90
#define RAMROD_CMD_ID_ETH_UPDATE 100
#define RAMROD_CMD_ID_ETH_HALT 105
#define RAMROD_CMD_ID_ETH_SET_MAC 110
#define RAMROD_CMD_ID_ETH_CFC_DEL 115
#define RAMROD_CMD_ID_ETH_PORT_DEL 120
#define RAMROD_CMD_ID_ETH_FORWARD_SETUP 125


/* command values for set mac command */
#define T_ETH_MAC_COMMAND_SET 0
#define T_ETH_MAC_COMMAND_INVALIDATE 1

#define T_ETH_INDIRECTION_TABLE_SIZE 128

/*The CRC32 seed, that is used for the hash(reduction) multicast address */
#define T_ETH_CRC32_HASH_SEED 0x00000000

/* Maximal L2 clients supported */
#define ETH_MAX_RX_CLIENTS_E1 18
#define ETH_MAX_RX_CLIENTS_E1H 26

/* Maximal aggregation queues supported */
#define ETH_MAX_AGGREGATION_QUEUES_E1 32
#define ETH_MAX_AGGREGATION_QUEUES_E1H 64

/* ETH RSS modes */
#define ETH_RSS_MODE_DISABLED 0
#define ETH_RSS_MODE_REGULAR 1
#define ETH_RSS_MODE_VLAN_PRI 2
#define ETH_RSS_MODE_E1HOV_PRI 3
#define ETH_RSS_MODE_IP_DSCP 4


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


#define PROTOCOL_STATE_BIT_OFFSET 6

#define ETH_STATE (ETH_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define TOE_STATE (TOE_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)
#define RDMA_STATE (RDMA_CONNECTION_TYPE << PROTOCOL_STATE_BIT_OFFSET)

/* microcode fixed page page size 4K (chains and ring segments) */
#define MC_PAGE_SIZE 4096


/* Host coalescing constants */

/* index numbers */
#define HC_USTORM_DEF_SB_NUM_INDICES 8
#define HC_CSTORM_DEF_SB_NUM_INDICES 8
#define HC_XSTORM_DEF_SB_NUM_INDICES 4
#define HC_TSTORM_DEF_SB_NUM_INDICES 4
#define HC_USTORM_SB_NUM_INDICES 4
#define HC_CSTORM_SB_NUM_INDICES 4

/* index values - which counter to update */

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
#define HC_INDEX_DEF_C_ETH_FCOE_CQ_CONS 6

#define HC_INDEX_DEF_U_ETH_RDMA_RX_CQ_CONS 0
#define HC_INDEX_DEF_U_ETH_ISCSI_RX_CQ_CONS 1
#define HC_INDEX_DEF_U_ETH_RDMA_RX_BD_CONS 2
#define HC_INDEX_DEF_U_ETH_ISCSI_RX_BD_CONS 3
#define HC_INDEX_DEF_U_ETH_FCOE_RX_CQ_CONS 4
#define HC_INDEX_DEF_U_ETH_FCOE_RX_BD_CONS 5

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
#define EMULATION_FREQUENCY_FACTOR 1600
#define FPGA_FREQUENCY_FACTOR 100

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

#define USEMI_CLK1_RESUL_CHIP (TIMERS_TICK_SIZE_CHIP)
#define USEMI_CLK1_RESUL_EMUL (TIMERS_TICK_SIZE_EMUL)
#define USEMI_CLK1_RESUL_FPGA (TIMERS_TICK_SIZE_FPGA)

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

#define FW_LOG_LIST_SIZE 50

#define NUM_OF_PROTOCOLS 4
#define NUM_OF_SAFC_BITS 16
#define MAX_COS_NUMBER 4
#define MAX_T_STAT_COUNTER_ID 18
#define MAX_X_STAT_COUNTER_ID 18
#define MAX_U_STAT_COUNTER_ID 18


#define UNKNOWN_ADDRESS 0
#define UNICAST_ADDRESS 1
#define MULTICAST_ADDRESS 2
#define BROADCAST_ADDRESS 3

#define SINGLE_FUNCTION 0
#define MULTI_FUNCTION 1

#define IP_V4 0
#define IP_V6 1

