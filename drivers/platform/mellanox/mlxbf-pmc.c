// SPDX-License-Identifier: GPL-2.0-only OR Linux-OpenIB
/*
 * Mellanox BlueField Performance Monitoring Counters driver
 *
 * This driver provides a sysfs interface for monitoring
 * performance statistics in BlueField SoC.
 *
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <uapi/linux/psci.h>

#define MLXBF_PMC_WRITE_REG_32 0x82000009
#define MLXBF_PMC_READ_REG_32 0x8200000A
#define MLXBF_PMC_WRITE_REG_64 0x8200000B
#define MLXBF_PMC_READ_REG_64 0x8200000C
#define MLXBF_PMC_SIP_SVC_UID 0x8200ff01
#define MLXBF_PMC_SIP_SVC_VERSION 0x8200ff03
#define MLXBF_PMC_SVC_REQ_MAJOR 0
#define MLXBF_PMC_SVC_MIN_MINOR 3

#define MLXBF_PMC_SMCCC_ACCESS_VIOLATION -4

#define MLXBF_PMC_EVENT_SET_BF1 0
#define MLXBF_PMC_EVENT_SET_BF2 1
#define MLXBF_PMC_EVENT_INFO_LEN 100

#define MLXBF_PMC_MAX_BLOCKS 30
#define MLXBF_PMC_MAX_ATTRS 30
#define MLXBF_PMC_INFO_SZ 4
#define MLXBF_PMC_REG_SIZE 8
#define MLXBF_PMC_L3C_REG_SIZE 4

#define MLXBF_PMC_TYPE_COUNTER 1
#define MLXBF_PMC_TYPE_REGISTER 0

#define MLXBF_PMC_PERFCTL 0
#define MLXBF_PMC_PERFEVT 1
#define MLXBF_PMC_PERFACC0 4

#define MLXBF_PMC_PERFMON_CONFIG_WR_R_B BIT(0)
#define MLXBF_PMC_PERFMON_CONFIG_STROBE BIT(1)
#define MLXBF_PMC_PERFMON_CONFIG_ADDR GENMASK_ULL(4, 2)
#define MLXBF_PMC_PERFMON_CONFIG_WDATA GENMASK_ULL(60, 5)

#define MLXBF_PMC_PERFCTL_FM0 GENMASK_ULL(18, 16)
#define MLXBF_PMC_PERFCTL_MS0 GENMASK_ULL(21, 20)
#define MLXBF_PMC_PERFCTL_ACCM0 GENMASK_ULL(26, 24)
#define MLXBF_PMC_PERFCTL_AD0 BIT(27)
#define MLXBF_PMC_PERFCTL_ETRIG0 GENMASK_ULL(29, 28)
#define MLXBF_PMC_PERFCTL_EB0 BIT(30)
#define MLXBF_PMC_PERFCTL_EN0 BIT(31)

#define MLXBF_PMC_PERFEVT_EVTSEL GENMASK_ULL(31, 24)

#define MLXBF_PMC_L3C_PERF_CNT_CFG 0x0
#define MLXBF_PMC_L3C_PERF_CNT_SEL 0x10
#define MLXBF_PMC_L3C_PERF_CNT_SEL_1 0x14
#define MLXBF_PMC_L3C_PERF_CNT_LOW 0x40
#define MLXBF_PMC_L3C_PERF_CNT_HIGH 0x60

#define MLXBF_PMC_L3C_PERF_CNT_CFG_EN BIT(0)
#define MLXBF_PMC_L3C_PERF_CNT_CFG_RST BIT(1)
#define MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_0 GENMASK(5, 0)
#define MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_1 GENMASK(13, 8)
#define MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_2 GENMASK(21, 16)
#define MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_3 GENMASK(29, 24)

#define MLXBF_PMC_L3C_PERF_CNT_SEL_1_CNT_4 GENMASK(5, 0)

#define MLXBF_PMC_L3C_PERF_CNT_LOW_VAL GENMASK(31, 0)
#define MLXBF_PMC_L3C_PERF_CNT_HIGH_VAL GENMASK(24, 0)

/**
 * struct mlxbf_pmc_attribute - Structure to hold attribute and block info
 * for each sysfs entry
 * @dev_attr: Device attribute struct
 * @index: index to identify counter number within a block
 * @nr: block number to which the sysfs belongs
 */
struct mlxbf_pmc_attribute {
	struct device_attribute dev_attr;
	int index;
	int nr;
};

/**
 * struct mlxbf_pmc_block_info - Structure to hold info for each HW block
 *
 * @mmio_base: The VA at which the PMC block is mapped
 * @blk_size: Size of each mapped region
 * @counters: Number of counters in the block
 * @type: Type of counters in the block
 * @attr_counter: Attributes for "counter" sysfs files
 * @attr_event: Attributes for "event" sysfs files
 * @attr_event_list: Attributes for "event_list" sysfs files
 * @attr_enable: Attributes for "enable" sysfs files
 * @block_attr: All attributes needed for the block
 * @block_attr_grp: Attribute group for the block
 */
struct mlxbf_pmc_block_info {
	void __iomem *mmio_base;
	size_t blk_size;
	size_t counters;
	int type;
	struct mlxbf_pmc_attribute *attr_counter;
	struct mlxbf_pmc_attribute *attr_event;
	struct mlxbf_pmc_attribute attr_event_list;
	struct mlxbf_pmc_attribute attr_enable;
	struct attribute *block_attr[MLXBF_PMC_MAX_ATTRS];
	struct attribute_group block_attr_grp;
};

/**
 * struct mlxbf_pmc_context - Structure to hold PMC context info
 *
 * @pdev: The kernel structure representing the device
 * @total_blocks: Total number of blocks
 * @tile_count: Number of tiles in the system
 * @hwmon_dev: Hwmon device for bfperf
 * @block_name: Block name
 * @block:  Block info
 * @groups:  Attribute groups from each block
 * @svc_sreg_support: Whether SMCs are used to access performance registers
 * @sreg_tbl_perf: Secure register access table number
 * @event_set: Event set to use
 */
struct mlxbf_pmc_context {
	struct platform_device *pdev;
	uint32_t total_blocks;
	uint32_t tile_count;
	struct device *hwmon_dev;
	const char *block_name[MLXBF_PMC_MAX_BLOCKS];
	struct mlxbf_pmc_block_info block[MLXBF_PMC_MAX_BLOCKS];
	const struct attribute_group *groups[MLXBF_PMC_MAX_BLOCKS];
	bool svc_sreg_support;
	uint32_t sreg_tbl_perf;
	unsigned int event_set;
};

/**
 * struct mlxbf_pmc_events - Structure to hold supported events for each block
 * @evt_num: Event number used to program counters
 * @evt_name: Name of the event
 */
struct mlxbf_pmc_events {
	int evt_num;
	char *evt_name;
};

static const struct mlxbf_pmc_events mlxbf_pmc_pcie_events[] = {
	{ 0x0, "IN_P_PKT_CNT" },
	{ 0x10, "IN_NP_PKT_CNT" },
	{ 0x18, "IN_C_PKT_CNT" },
	{ 0x20, "OUT_P_PKT_CNT" },
	{ 0x28, "OUT_NP_PKT_CNT" },
	{ 0x30, "OUT_C_PKT_CNT" },
	{ 0x38, "IN_P_BYTE_CNT" },
	{ 0x40, "IN_NP_BYTE_CNT" },
	{ 0x48, "IN_C_BYTE_CNT" },
	{ 0x50, "OUT_P_BYTE_CNT" },
	{ 0x58, "OUT_NP_BYTE_CNT" },
	{ 0x60, "OUT_C_BYTE_CNT" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_smgen_events[] = {
	{ 0x0, "AW_REQ" },
	{ 0x1, "AW_BEATS" },
	{ 0x2, "AW_TRANS" },
	{ 0x3, "AW_RESP" },
	{ 0x4, "AW_STL" },
	{ 0x5, "AW_LAT" },
	{ 0x6, "AW_REQ_TBU" },
	{ 0x8, "AR_REQ" },
	{ 0x9, "AR_BEATS" },
	{ 0xa, "AR_TRANS" },
	{ 0xb, "AR_STL" },
	{ 0xc, "AR_LAT" },
	{ 0xd, "AR_REQ_TBU" },
	{ 0xe, "TBU_MISS" },
	{ 0xf, "TX_DAT_AF" },
	{ 0x10, "RX_DAT_AF" },
	{ 0x11, "RETRYQ_CRED" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_trio_events_1[] = {
	{ 0xa0, "TPIO_DATA_BEAT" },
	{ 0xa1, "TDMA_DATA_BEAT" },
	{ 0xa2, "MAP_DATA_BEAT" },
	{ 0xa3, "TXMSG_DATA_BEAT" },
	{ 0xa4, "TPIO_DATA_PACKET" },
	{ 0xa5, "TDMA_DATA_PACKET" },
	{ 0xa6, "MAP_DATA_PACKET" },
	{ 0xa7, "TXMSG_DATA_PACKET" },
	{ 0xa8, "TDMA_RT_AF" },
	{ 0xa9, "TDMA_PBUF_MAC_AF" },
	{ 0xaa, "TRIO_MAP_WRQ_BUF_EMPTY" },
	{ 0xab, "TRIO_MAP_CPL_BUF_EMPTY" },
	{ 0xac, "TRIO_MAP_RDQ0_BUF_EMPTY" },
	{ 0xad, "TRIO_MAP_RDQ1_BUF_EMPTY" },
	{ 0xae, "TRIO_MAP_RDQ2_BUF_EMPTY" },
	{ 0xaf, "TRIO_MAP_RDQ3_BUF_EMPTY" },
	{ 0xb0, "TRIO_MAP_RDQ4_BUF_EMPTY" },
	{ 0xb1, "TRIO_MAP_RDQ5_BUF_EMPTY" },
	{ 0xb2, "TRIO_MAP_RDQ6_BUF_EMPTY" },
	{ 0xb3, "TRIO_MAP_RDQ7_BUF_EMPTY" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_trio_events_2[] = {
	{ 0xa0, "TPIO_DATA_BEAT" },
	{ 0xa1, "TDMA_DATA_BEAT" },
	{ 0xa2, "MAP_DATA_BEAT" },
	{ 0xa3, "TXMSG_DATA_BEAT" },
	{ 0xa4, "TPIO_DATA_PACKET" },
	{ 0xa5, "TDMA_DATA_PACKET" },
	{ 0xa6, "MAP_DATA_PACKET" },
	{ 0xa7, "TXMSG_DATA_PACKET" },
	{ 0xa8, "TDMA_RT_AF" },
	{ 0xa9, "TDMA_PBUF_MAC_AF" },
	{ 0xaa, "TRIO_MAP_WRQ_BUF_EMPTY" },
	{ 0xab, "TRIO_MAP_CPL_BUF_EMPTY" },
	{ 0xac, "TRIO_MAP_RDQ0_BUF_EMPTY" },
	{ 0xad, "TRIO_MAP_RDQ1_BUF_EMPTY" },
	{ 0xae, "TRIO_MAP_RDQ2_BUF_EMPTY" },
	{ 0xaf, "TRIO_MAP_RDQ3_BUF_EMPTY" },
	{ 0xb0, "TRIO_MAP_RDQ4_BUF_EMPTY" },
	{ 0xb1, "TRIO_MAP_RDQ5_BUF_EMPTY" },
	{ 0xb2, "TRIO_MAP_RDQ6_BUF_EMPTY" },
	{ 0xb3, "TRIO_MAP_RDQ7_BUF_EMPTY" },
	{ 0xb4, "TRIO_RING_TX_FLIT_CH0" },
	{ 0xb5, "TRIO_RING_TX_FLIT_CH1" },
	{ 0xb6, "TRIO_RING_TX_FLIT_CH2" },
	{ 0xb7, "TRIO_RING_TX_FLIT_CH3" },
	{ 0xb8, "TRIO_RING_TX_FLIT_CH4" },
	{ 0xb9, "TRIO_RING_RX_FLIT_CH0" },
	{ 0xba, "TRIO_RING_RX_FLIT_CH1" },
	{ 0xbb, "TRIO_RING_RX_FLIT_CH2" },
	{ 0xbc, "TRIO_RING_RX_FLIT_CH3" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_ecc_events[] = {
	{ 0x100, "ECC_SINGLE_ERROR_CNT" },
	{ 0x104, "ECC_DOUBLE_ERROR_CNT" },
	{ 0x114, "SERR_INJ" },
	{ 0x118, "DERR_INJ" },
	{ 0x124, "ECC_SINGLE_ERROR_0" },
	{ 0x164, "ECC_DOUBLE_ERROR_0" },
	{ 0x340, "DRAM_ECC_COUNT" },
	{ 0x344, "DRAM_ECC_INJECT" },
	{ 0x348, "DRAM_ECC_ERROR" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_mss_events[] = {
	{ 0xc0, "RXREQ_MSS" },
	{ 0xc1, "RXDAT_MSS" },
	{ 0xc2, "TXRSP_MSS" },
	{ 0xc3, "TXDAT_MSS" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_hnf_events[] = {
	{ 0x45, "HNF_REQUESTS" },
	{ 0x46, "HNF_REJECTS" },
	{ 0x47, "ALL_BUSY" },
	{ 0x48, "MAF_BUSY" },
	{ 0x49, "MAF_REQUESTS" },
	{ 0x4a, "RNF_REQUESTS" },
	{ 0x4b, "REQUEST_TYPE" },
	{ 0x4c, "MEMORY_READS" },
	{ 0x4d, "MEMORY_WRITES" },
	{ 0x4e, "VICTIM_WRITE" },
	{ 0x4f, "POC_FULL" },
	{ 0x50, "POC_FAIL" },
	{ 0x51, "POC_SUCCESS" },
	{ 0x52, "POC_WRITES" },
	{ 0x53, "POC_READS" },
	{ 0x54, "FORWARD" },
	{ 0x55, "RXREQ_HNF" },
	{ 0x56, "RXRSP_HNF" },
	{ 0x57, "RXDAT_HNF" },
	{ 0x58, "TXREQ_HNF" },
	{ 0x59, "TXRSP_HNF" },
	{ 0x5a, "TXDAT_HNF" },
	{ 0x5b, "TXSNP_HNF" },
	{ 0x5c, "INDEX_MATCH" },
	{ 0x5d, "A72_ACCESS" },
	{ 0x5e, "IO_ACCESS" },
	{ 0x5f, "TSO_WRITE" },
	{ 0x60, "TSO_CONFLICT" },
	{ 0x61, "DIR_HIT" },
	{ 0x62, "HNF_ACCEPTS" },
	{ 0x63, "REQ_BUF_EMPTY" },
	{ 0x64, "REQ_BUF_IDLE_MAF" },
	{ 0x65, "TSO_NOARB" },
	{ 0x66, "TSO_NOARB_CYCLES" },
	{ 0x67, "MSS_NO_CREDIT" },
	{ 0x68, "TXDAT_NO_LCRD" },
	{ 0x69, "TXSNP_NO_LCRD" },
	{ 0x6a, "TXRSP_NO_LCRD" },
	{ 0x6b, "TXREQ_NO_LCRD" },
	{ 0x6c, "TSO_CL_MATCH" },
	{ 0x6d, "MEMORY_READS_BYPASS" },
	{ 0x6e, "TSO_NOARB_TIMEOUT" },
	{ 0x6f, "ALLOCATE" },
	{ 0x70, "VICTIM" },
	{ 0x71, "A72_WRITE" },
	{ 0x72, "A72_READ" },
	{ 0x73, "IO_WRITE" },
	{ 0x74, "IO_READ" },
	{ 0x75, "TSO_REJECT" },
	{ 0x80, "TXREQ_RN" },
	{ 0x81, "TXRSP_RN" },
	{ 0x82, "TXDAT_RN" },
	{ 0x83, "RXSNP_RN" },
	{ 0x84, "RXRSP_RN" },
	{ 0x85, "RXDAT_RN" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_hnfnet_events[] = {
	{ 0x12, "CDN_REQ" },
	{ 0x13, "DDN_REQ" },
	{ 0x14, "NDN_REQ" },
	{ 0x15, "CDN_DIAG_N_OUT_OF_CRED" },
	{ 0x16, "CDN_DIAG_S_OUT_OF_CRED" },
	{ 0x17, "CDN_DIAG_E_OUT_OF_CRED" },
	{ 0x18, "CDN_DIAG_W_OUT_OF_CRED" },
	{ 0x19, "CDN_DIAG_C_OUT_OF_CRED" },
	{ 0x1a, "CDN_DIAG_N_EGRESS" },
	{ 0x1b, "CDN_DIAG_S_EGRESS" },
	{ 0x1c, "CDN_DIAG_E_EGRESS" },
	{ 0x1d, "CDN_DIAG_W_EGRESS" },
	{ 0x1e, "CDN_DIAG_C_EGRESS" },
	{ 0x1f, "CDN_DIAG_N_INGRESS" },
	{ 0x20, "CDN_DIAG_S_INGRESS" },
	{ 0x21, "CDN_DIAG_E_INGRESS" },
	{ 0x22, "CDN_DIAG_W_INGRESS" },
	{ 0x23, "CDN_DIAG_C_INGRESS" },
	{ 0x24, "CDN_DIAG_CORE_SENT" },
	{ 0x25, "DDN_DIAG_N_OUT_OF_CRED" },
	{ 0x26, "DDN_DIAG_S_OUT_OF_CRED" },
	{ 0x27, "DDN_DIAG_E_OUT_OF_CRED" },
	{ 0x28, "DDN_DIAG_W_OUT_OF_CRED" },
	{ 0x29, "DDN_DIAG_C_OUT_OF_CRED" },
	{ 0x2a, "DDN_DIAG_N_EGRESS" },
	{ 0x2b, "DDN_DIAG_S_EGRESS" },
	{ 0x2c, "DDN_DIAG_E_EGRESS" },
	{ 0x2d, "DDN_DIAG_W_EGRESS" },
	{ 0x2e, "DDN_DIAG_C_EGRESS" },
	{ 0x2f, "DDN_DIAG_N_INGRESS" },
	{ 0x30, "DDN_DIAG_S_INGRESS" },
	{ 0x31, "DDN_DIAG_E_INGRESS" },
	{ 0x32, "DDN_DIAG_W_INGRESS" },
	{ 0x33, "DDN_DIAG_C_INGRESS" },
	{ 0x34, "DDN_DIAG_CORE_SENT" },
	{ 0x35, "NDN_DIAG_N_OUT_OF_CRED" },
	{ 0x36, "NDN_DIAG_S_OUT_OF_CRED" },
	{ 0x37, "NDN_DIAG_E_OUT_OF_CRED" },
	{ 0x38, "NDN_DIAG_W_OUT_OF_CRED" },
	{ 0x39, "NDN_DIAG_C_OUT_OF_CRED" },
	{ 0x3a, "NDN_DIAG_N_EGRESS" },
	{ 0x3b, "NDN_DIAG_S_EGRESS" },
	{ 0x3c, "NDN_DIAG_E_EGRESS" },
	{ 0x3d, "NDN_DIAG_W_EGRESS" },
	{ 0x3e, "NDN_DIAG_C_EGRESS" },
	{ 0x3f, "NDN_DIAG_N_INGRESS" },
	{ 0x40, "NDN_DIAG_S_INGRESS" },
	{ 0x41, "NDN_DIAG_E_INGRESS" },
	{ 0x42, "NDN_DIAG_W_INGRESS" },
	{ 0x43, "NDN_DIAG_C_INGRESS" },
	{ 0x44, "NDN_DIAG_CORE_SENT" },
};

static const struct mlxbf_pmc_events mlxbf_pmc_l3c_events[] = {
	{ 0x00, "DISABLE" },
	{ 0x01, "CYCLES" },
	{ 0x02, "TOTAL_RD_REQ_IN" },
	{ 0x03, "TOTAL_WR_REQ_IN" },
	{ 0x04, "TOTAL_WR_DBID_ACK" },
	{ 0x05, "TOTAL_WR_DATA_IN" },
	{ 0x06, "TOTAL_WR_COMP" },
	{ 0x07, "TOTAL_RD_DATA_OUT" },
	{ 0x08, "TOTAL_CDN_REQ_IN_BANK0" },
	{ 0x09, "TOTAL_CDN_REQ_IN_BANK1" },
	{ 0x0a, "TOTAL_DDN_REQ_IN_BANK0" },
	{ 0x0b, "TOTAL_DDN_REQ_IN_BANK1" },
	{ 0x0c, "TOTAL_EMEM_RD_RES_IN_BANK0" },
	{ 0x0d, "TOTAL_EMEM_RD_RES_IN_BANK1" },
	{ 0x0e, "TOTAL_CACHE_RD_RES_IN_BANK0" },
	{ 0x0f, "TOTAL_CACHE_RD_RES_IN_BANK1" },
	{ 0x10, "TOTAL_EMEM_RD_REQ_BANK0" },
	{ 0x11, "TOTAL_EMEM_RD_REQ_BANK1" },
	{ 0x12, "TOTAL_EMEM_WR_REQ_BANK0" },
	{ 0x13, "TOTAL_EMEM_WR_REQ_BANK1" },
	{ 0x14, "TOTAL_RD_REQ_OUT" },
	{ 0x15, "TOTAL_WR_REQ_OUT" },
	{ 0x16, "TOTAL_RD_RES_IN" },
	{ 0x17, "HITS_BANK0" },
	{ 0x18, "HITS_BANK1" },
	{ 0x19, "MISSES_BANK0" },
	{ 0x1a, "MISSES_BANK1" },
	{ 0x1b, "ALLOCATIONS_BANK0" },
	{ 0x1c, "ALLOCATIONS_BANK1" },
	{ 0x1d, "EVICTIONS_BANK0" },
	{ 0x1e, "EVICTIONS_BANK1" },
	{ 0x1f, "DBID_REJECT" },
	{ 0x20, "WRDB_REJECT_BANK0" },
	{ 0x21, "WRDB_REJECT_BANK1" },
	{ 0x22, "CMDQ_REJECT_BANK0" },
	{ 0x23, "CMDQ_REJECT_BANK1" },
	{ 0x24, "COB_REJECT_BANK0" },
	{ 0x25, "COB_REJECT_BANK1" },
	{ 0x26, "TRB_REJECT_BANK0" },
	{ 0x27, "TRB_REJECT_BANK1" },
	{ 0x28, "TAG_REJECT_BANK0" },
	{ 0x29, "TAG_REJECT_BANK1" },
	{ 0x2a, "ANY_REJECT_BANK0" },
	{ 0x2b, "ANY_REJECT_BANK1" },
};

static struct mlxbf_pmc_context *pmc;

/* UUID used to probe ATF service. */
static const char *mlxbf_pmc_svc_uuid_str = "89c036b4-e7d7-11e6-8797-001aca00bfc4";

/* Calls an SMC to access a performance register */
static int mlxbf_pmc_secure_read(void __iomem *addr, uint32_t command,
				 uint64_t *result)
{
	struct arm_smccc_res res;
	int status, err = 0;

	arm_smccc_smc(command, pmc->sreg_tbl_perf, (uintptr_t)addr, 0, 0, 0, 0,
		      0, &res);

	status = res.a0;

	switch (status) {
	case PSCI_RET_NOT_SUPPORTED:
		err = -EINVAL;
		break;
	case MLXBF_PMC_SMCCC_ACCESS_VIOLATION:
		err = -EACCES;
		break;
	default:
		*result = res.a1;
		break;
	}

	return err;
}

/* Read from a performance counter */
static int mlxbf_pmc_read(void __iomem *addr, uint32_t command,
			  uint64_t *result)
{
	if (pmc->svc_sreg_support)
		return mlxbf_pmc_secure_read(addr, command, result);

	if (command == MLXBF_PMC_READ_REG_32)
		*result = readl(addr);
	else
		*result = readq(addr);

	return 0;
}

/* Convenience function for 32-bit reads */
static int mlxbf_pmc_readl(void __iomem *addr, uint32_t *result)
{
	uint64_t read_out;
	int status;

	status = mlxbf_pmc_read(addr, MLXBF_PMC_READ_REG_32, &read_out);
	if (status)
		return status;
	*result = (uint32_t)read_out;

	return 0;
}

/* Calls an SMC to access a performance register */
static int mlxbf_pmc_secure_write(void __iomem *addr, uint32_t command,
				  uint64_t value)
{
	struct arm_smccc_res res;
	int status, err = 0;

	arm_smccc_smc(command, pmc->sreg_tbl_perf, value, (uintptr_t)addr, 0, 0,
		      0, 0, &res);

	status = res.a0;

	switch (status) {
	case PSCI_RET_NOT_SUPPORTED:
		err = -EINVAL;
		break;
	case MLXBF_PMC_SMCCC_ACCESS_VIOLATION:
		err = -EACCES;
		break;
	}

	return err;
}

/* Write to a performance counter */
static int mlxbf_pmc_write(void __iomem *addr, int command, uint64_t value)
{
	if (pmc->svc_sreg_support)
		return mlxbf_pmc_secure_write(addr, command, value);

	if (command == MLXBF_PMC_WRITE_REG_32)
		writel(value, addr);
	else
		writeq(value, addr);

	return 0;
}

/* Check if the register offset is within the mapped region for the block */
static bool mlxbf_pmc_valid_range(int blk_num, uint32_t offset)
{
	if ((offset >= 0) && !(offset % MLXBF_PMC_REG_SIZE) &&
	    (offset + MLXBF_PMC_REG_SIZE <= pmc->block[blk_num].blk_size))
		return true; /* inside the mapped PMC space */

	return false;
}

/* Get the event list corresponding to a certain block */
static const struct mlxbf_pmc_events *mlxbf_pmc_event_list(const char *blk,
							   int *size)
{
	const struct mlxbf_pmc_events *events;

	if (strstr(blk, "tilenet")) {
		events = mlxbf_pmc_hnfnet_events;
		*size = ARRAY_SIZE(mlxbf_pmc_hnfnet_events);
	} else if (strstr(blk, "tile")) {
		events = mlxbf_pmc_hnf_events;
		*size = ARRAY_SIZE(mlxbf_pmc_hnf_events);
	} else if (strstr(blk, "triogen")) {
		events = mlxbf_pmc_smgen_events;
		*size = ARRAY_SIZE(mlxbf_pmc_smgen_events);
	} else if (strstr(blk, "trio")) {
		switch (pmc->event_set) {
		case MLXBF_PMC_EVENT_SET_BF1:
			events = mlxbf_pmc_trio_events_1;
			*size = ARRAY_SIZE(mlxbf_pmc_trio_events_1);
			break;
		case MLXBF_PMC_EVENT_SET_BF2:
			events = mlxbf_pmc_trio_events_2;
			*size = ARRAY_SIZE(mlxbf_pmc_trio_events_2);
			break;
		default:
			events = NULL;
			*size = 0;
			break;
		}
	} else if (strstr(blk, "mss")) {
		events = mlxbf_pmc_mss_events;
		*size = ARRAY_SIZE(mlxbf_pmc_mss_events);
	} else if (strstr(blk, "ecc")) {
		events = mlxbf_pmc_ecc_events;
		*size = ARRAY_SIZE(mlxbf_pmc_ecc_events);
	} else if (strstr(blk, "pcie")) {
		events = mlxbf_pmc_pcie_events;
		*size = ARRAY_SIZE(mlxbf_pmc_pcie_events);
	} else if (strstr(blk, "l3cache")) {
		events = mlxbf_pmc_l3c_events;
		*size = ARRAY_SIZE(mlxbf_pmc_l3c_events);
	} else if (strstr(blk, "gic")) {
		events = mlxbf_pmc_smgen_events;
		*size = ARRAY_SIZE(mlxbf_pmc_smgen_events);
	} else if (strstr(blk, "smmu")) {
		events = mlxbf_pmc_smgen_events;
		*size = ARRAY_SIZE(mlxbf_pmc_smgen_events);
	} else {
		events = NULL;
		*size = 0;
	}

	return events;
}

/* Get the event number given the name */
static int mlxbf_pmc_get_event_num(const char *blk, const char *evt)
{
	const struct mlxbf_pmc_events *events;
	int i, size;

	events = mlxbf_pmc_event_list(blk, &size);
	if (!events)
		return -EINVAL;

	for (i = 0; i < size; ++i) {
		if (!strcmp(evt, events[i].evt_name))
			return events[i].evt_num;
	}

	return -ENODEV;
}

/* Get the event number given the name */
static char *mlxbf_pmc_get_event_name(const char *blk, int evt)
{
	const struct mlxbf_pmc_events *events;
	int i, size;

	events = mlxbf_pmc_event_list(blk, &size);
	if (!events)
		return NULL;

	for (i = 0; i < size; ++i) {
		if (evt == events[i].evt_num)
			return events[i].evt_name;
	}

	return NULL;
}

/* Method to enable/disable/reset l3cache counters */
static int mlxbf_pmc_config_l3_counters(int blk_num, bool enable, bool reset)
{
	uint32_t perfcnt_cfg = 0;

	if (enable)
		perfcnt_cfg |= MLXBF_PMC_L3C_PERF_CNT_CFG_EN;
	if (reset)
		perfcnt_cfg |= MLXBF_PMC_L3C_PERF_CNT_CFG_RST;

	return mlxbf_pmc_write(pmc->block[blk_num].mmio_base +
				       MLXBF_PMC_L3C_PERF_CNT_CFG,
			       MLXBF_PMC_WRITE_REG_32, perfcnt_cfg);
}

/* Method to handle l3cache counter programming */
static int mlxbf_pmc_program_l3_counter(int blk_num, uint32_t cnt_num,
					uint32_t evt)
{
	uint32_t perfcnt_sel_1 = 0;
	uint32_t perfcnt_sel = 0;
	uint32_t *wordaddr;
	void __iomem *pmcaddr;
	int ret;

	/* Disable all counters before programming them */
	if (mlxbf_pmc_config_l3_counters(blk_num, false, false))
		return -EINVAL;

	/* Select appropriate register information */
	switch (cnt_num) {
	case 0 ... 3:
		pmcaddr = pmc->block[blk_num].mmio_base +
			  MLXBF_PMC_L3C_PERF_CNT_SEL;
		wordaddr = &perfcnt_sel;
		break;
	case 4:
		pmcaddr = pmc->block[blk_num].mmio_base +
			  MLXBF_PMC_L3C_PERF_CNT_SEL_1;
		wordaddr = &perfcnt_sel_1;
		break;
	default:
		return -EINVAL;
	}

	ret = mlxbf_pmc_readl(pmcaddr, wordaddr);
	if (ret)
		return ret;

	switch (cnt_num) {
	case 0:
		perfcnt_sel &= ~MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_0;
		perfcnt_sel |= FIELD_PREP(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_0,
					  evt);
		break;
	case 1:
		perfcnt_sel &= ~MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_1;
		perfcnt_sel |= FIELD_PREP(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_1,
					  evt);
		break;
	case 2:
		perfcnt_sel &= ~MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_2;
		perfcnt_sel |= FIELD_PREP(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_2,
					  evt);
		break;
	case 3:
		perfcnt_sel &= ~MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_3;
		perfcnt_sel |= FIELD_PREP(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_3,
					  evt);
		break;
	case 4:
		perfcnt_sel_1 &= ~MLXBF_PMC_L3C_PERF_CNT_SEL_1_CNT_4;
		perfcnt_sel_1 |= FIELD_PREP(MLXBF_PMC_L3C_PERF_CNT_SEL_1_CNT_4,
					    evt);
		break;
	default:
		return -EINVAL;
	}

	return mlxbf_pmc_write(pmcaddr, MLXBF_PMC_WRITE_REG_32, *wordaddr);
}

/* Method to program a counter to monitor an event */
static int mlxbf_pmc_program_counter(int blk_num, uint32_t cnt_num,
				     uint32_t evt, bool is_l3)
{
	uint64_t perfctl, perfevt, perfmon_cfg;

	if (cnt_num >= pmc->block[blk_num].counters)
		return -ENODEV;

	if (is_l3)
		return mlxbf_pmc_program_l3_counter(blk_num, cnt_num, evt);

	/* Configure the counter */
	perfctl = FIELD_PREP(MLXBF_PMC_PERFCTL_EN0, 1);
	perfctl |= FIELD_PREP(MLXBF_PMC_PERFCTL_EB0, 0);
	perfctl |= FIELD_PREP(MLXBF_PMC_PERFCTL_ETRIG0, 1);
	perfctl |= FIELD_PREP(MLXBF_PMC_PERFCTL_AD0, 0);
	perfctl |= FIELD_PREP(MLXBF_PMC_PERFCTL_ACCM0, 0);
	perfctl |= FIELD_PREP(MLXBF_PMC_PERFCTL_MS0, 0);
	perfctl |= FIELD_PREP(MLXBF_PMC_PERFCTL_FM0, 0);

	perfmon_cfg = FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WDATA, perfctl);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_ADDR,
				  MLXBF_PMC_PERFCTL);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_STROBE, 1);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WR_R_B, 1);

	if (mlxbf_pmc_write(pmc->block[blk_num].mmio_base +
				    cnt_num * MLXBF_PMC_REG_SIZE,
			    MLXBF_PMC_WRITE_REG_64, perfmon_cfg))
		return -EFAULT;

	/* Select the event */
	perfevt = FIELD_PREP(MLXBF_PMC_PERFEVT_EVTSEL, evt);

	perfmon_cfg = FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WDATA, perfevt);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_ADDR,
				  MLXBF_PMC_PERFEVT);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_STROBE, 1);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WR_R_B, 1);

	if (mlxbf_pmc_write(pmc->block[blk_num].mmio_base +
				    cnt_num * MLXBF_PMC_REG_SIZE,
			    MLXBF_PMC_WRITE_REG_64, perfmon_cfg))
		return -EFAULT;

	/* Clear the accumulator */
	perfmon_cfg = FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_ADDR,
				 MLXBF_PMC_PERFACC0);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_STROBE, 1);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WR_R_B, 1);

	if (mlxbf_pmc_write(pmc->block[blk_num].mmio_base +
				    cnt_num * MLXBF_PMC_REG_SIZE,
			    MLXBF_PMC_WRITE_REG_64, perfmon_cfg))
		return -EFAULT;

	return 0;
}

/* Method to handle l3 counter reads */
static int mlxbf_pmc_read_l3_counter(int blk_num, uint32_t cnt_num,
				     uint64_t *result)
{
	uint32_t perfcnt_low = 0, perfcnt_high = 0;
	uint64_t value;
	int status = 0;

	status = mlxbf_pmc_readl(pmc->block[blk_num].mmio_base +
					 MLXBF_PMC_L3C_PERF_CNT_LOW +
					 cnt_num * MLXBF_PMC_L3C_REG_SIZE,
				 &perfcnt_low);

	if (status)
		return status;

	status = mlxbf_pmc_readl(pmc->block[blk_num].mmio_base +
					 MLXBF_PMC_L3C_PERF_CNT_HIGH +
					 cnt_num * MLXBF_PMC_L3C_REG_SIZE,
				 &perfcnt_high);

	if (status)
		return status;

	value = perfcnt_high;
	value = value << 32;
	value |= perfcnt_low;
	*result = value;

	return 0;
}

/* Method to read the counter value */
static int mlxbf_pmc_read_counter(int blk_num, uint32_t cnt_num, bool is_l3,
				  uint64_t *result)
{
	uint32_t perfcfg_offset, perfval_offset;
	uint64_t perfmon_cfg;
	int status;

	if (cnt_num >= pmc->block[blk_num].counters)
		return -EINVAL;

	if (is_l3)
		return mlxbf_pmc_read_l3_counter(blk_num, cnt_num, result);

	perfcfg_offset = cnt_num * MLXBF_PMC_REG_SIZE;
	perfval_offset = perfcfg_offset +
			 pmc->block[blk_num].counters * MLXBF_PMC_REG_SIZE;

	/* Set counter in "read" mode */
	perfmon_cfg = FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_ADDR,
				 MLXBF_PMC_PERFACC0);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_STROBE, 1);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WR_R_B, 0);

	status = mlxbf_pmc_write(pmc->block[blk_num].mmio_base + perfcfg_offset,
				 MLXBF_PMC_WRITE_REG_64, perfmon_cfg);

	if (status)
		return status;

	/* Get the counter value */
	return mlxbf_pmc_read(pmc->block[blk_num].mmio_base + perfval_offset,
			      MLXBF_PMC_READ_REG_64, result);
}

/* Method to read L3 block event */
static int mlxbf_pmc_read_l3_event(int blk_num, uint32_t cnt_num,
				   uint64_t *result)
{
	uint32_t perfcnt_sel = 0, perfcnt_sel_1 = 0;
	uint32_t *wordaddr;
	void __iomem *pmcaddr;
	uint64_t evt;

	/* Select appropriate register information */
	switch (cnt_num) {
	case 0 ... 3:
		pmcaddr = pmc->block[blk_num].mmio_base +
			  MLXBF_PMC_L3C_PERF_CNT_SEL;
		wordaddr = &perfcnt_sel;
		break;
	case 4:
		pmcaddr = pmc->block[blk_num].mmio_base +
			  MLXBF_PMC_L3C_PERF_CNT_SEL_1;
		wordaddr = &perfcnt_sel_1;
		break;
	default:
		return -EINVAL;
	}

	if (mlxbf_pmc_readl(pmcaddr, wordaddr))
		return -EINVAL;

	/* Read from appropriate register field for the counter */
	switch (cnt_num) {
	case 0:
		evt = FIELD_GET(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_0, perfcnt_sel);
		break;
	case 1:
		evt = FIELD_GET(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_1, perfcnt_sel);
		break;
	case 2:
		evt = FIELD_GET(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_2, perfcnt_sel);
		break;
	case 3:
		evt = FIELD_GET(MLXBF_PMC_L3C_PERF_CNT_SEL_CNT_3, perfcnt_sel);
		break;
	case 4:
		evt = FIELD_GET(MLXBF_PMC_L3C_PERF_CNT_SEL_1_CNT_4,
				perfcnt_sel_1);
		break;
	default:
		return -EINVAL;
	}
	*result = evt;

	return 0;
}

/* Method to find the event currently being monitored by a counter */
static int mlxbf_pmc_read_event(int blk_num, uint32_t cnt_num, bool is_l3,
				uint64_t *result)
{
	uint32_t perfcfg_offset, perfval_offset;
	uint64_t perfmon_cfg, perfevt, perfctl;

	if (cnt_num >= pmc->block[blk_num].counters)
		return -EINVAL;

	if (is_l3)
		return mlxbf_pmc_read_l3_event(blk_num, cnt_num, result);

	perfcfg_offset = cnt_num * MLXBF_PMC_REG_SIZE;
	perfval_offset = perfcfg_offset +
			 pmc->block[blk_num].counters * MLXBF_PMC_REG_SIZE;

	/* Set counter in "read" mode */
	perfmon_cfg = FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_ADDR,
				 MLXBF_PMC_PERFCTL);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_STROBE, 1);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WR_R_B, 0);

	if (mlxbf_pmc_write(pmc->block[blk_num].mmio_base + perfcfg_offset,
			    MLXBF_PMC_WRITE_REG_64, perfmon_cfg))
		return -EFAULT;

	/* Check if the counter is enabled */

	if (mlxbf_pmc_read(pmc->block[blk_num].mmio_base + perfval_offset,
			   MLXBF_PMC_READ_REG_64, &perfctl))
		return -EFAULT;

	if (!FIELD_GET(MLXBF_PMC_PERFCTL_EN0, perfctl))
		return -EINVAL;

	/* Set counter in "read" mode */
	perfmon_cfg = FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_ADDR,
				 MLXBF_PMC_PERFEVT);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_STROBE, 1);
	perfmon_cfg |= FIELD_PREP(MLXBF_PMC_PERFMON_CONFIG_WR_R_B, 0);

	if (mlxbf_pmc_write(pmc->block[blk_num].mmio_base + perfcfg_offset,
			    MLXBF_PMC_WRITE_REG_64, perfmon_cfg))
		return -EFAULT;

	/* Get the event number */
	if (mlxbf_pmc_read(pmc->block[blk_num].mmio_base + perfval_offset,
			   MLXBF_PMC_READ_REG_64, &perfevt))
		return -EFAULT;

	*result = FIELD_GET(MLXBF_PMC_PERFEVT_EVTSEL, perfevt);

	return 0;
}

/* Method to read a register */
static int mlxbf_pmc_read_reg(int blk_num, uint32_t offset, uint64_t *result)
{
	uint32_t ecc_out;

	if (strstr(pmc->block_name[blk_num], "ecc")) {
		if (mlxbf_pmc_readl(pmc->block[blk_num].mmio_base + offset,
				    &ecc_out))
			return -EFAULT;

		*result = ecc_out;
		return 0;
	}

	if (mlxbf_pmc_valid_range(blk_num, offset))
		return mlxbf_pmc_read(pmc->block[blk_num].mmio_base + offset,
				      MLXBF_PMC_READ_REG_64, result);

	return -EINVAL;
}

/* Method to write to a register */
static int mlxbf_pmc_write_reg(int blk_num, uint32_t offset, uint64_t data)
{
	if (strstr(pmc->block_name[blk_num], "ecc")) {
		return mlxbf_pmc_write(pmc->block[blk_num].mmio_base + offset,
				       MLXBF_PMC_WRITE_REG_32, data);
	}

	if (mlxbf_pmc_valid_range(blk_num, offset))
		return mlxbf_pmc_write(pmc->block[blk_num].mmio_base + offset,
				       MLXBF_PMC_WRITE_REG_64, data);

	return -EINVAL;
}

/* Show function for "counter" sysfs files */
static ssize_t mlxbf_pmc_counter_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mlxbf_pmc_attribute *attr_counter = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	int blk_num, cnt_num, offset;
	bool is_l3 = false;
	uint64_t value;

	blk_num = attr_counter->nr;
	cnt_num = attr_counter->index;

	if (strstr(pmc->block_name[blk_num], "l3cache"))
		is_l3 = true;

	if (pmc->block[blk_num].type == MLXBF_PMC_TYPE_COUNTER) {
		if (mlxbf_pmc_read_counter(blk_num, cnt_num, is_l3, &value))
			return -EINVAL;
	} else if (pmc->block[blk_num].type == MLXBF_PMC_TYPE_REGISTER) {
		offset = mlxbf_pmc_get_event_num(pmc->block_name[blk_num],
						 attr->attr.name);
		if (offset < 0)
			return -EINVAL;
		if (mlxbf_pmc_read_reg(blk_num, offset, &value))
			return -EINVAL;
	} else
		return -EINVAL;

	return sprintf(buf, "0x%llx\n", value);
}

/* Store function for "counter" sysfs files */
static ssize_t mlxbf_pmc_counter_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct mlxbf_pmc_attribute *attr_counter = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	int blk_num, cnt_num, offset, err, data;
	bool is_l3 = false;
	uint64_t evt_num;

	blk_num = attr_counter->nr;
	cnt_num = attr_counter->index;

	err = kstrtoint(buf, 0, &data);
	if (err < 0)
		return err;

	/* Allow non-zero writes only to the ecc regs */
	if (!(strstr(pmc->block_name[blk_num], "ecc")) && data)
		return -EINVAL;

	/* Do not allow writes to the L3C regs */
	if (strstr(pmc->block_name[blk_num], "l3cache"))
		return -EINVAL;

	if (pmc->block[blk_num].type == MLXBF_PMC_TYPE_COUNTER) {
		err = mlxbf_pmc_read_event(blk_num, cnt_num, is_l3, &evt_num);
		if (err)
			return err;
		err = mlxbf_pmc_program_counter(blk_num, cnt_num, evt_num,
						is_l3);
		if (err)
			return err;
	} else if (pmc->block[blk_num].type == MLXBF_PMC_TYPE_REGISTER) {
		offset = mlxbf_pmc_get_event_num(pmc->block_name[blk_num],
						 attr->attr.name);
		if (offset < 0)
			return -EINVAL;
		err = mlxbf_pmc_write_reg(blk_num, offset, data);
		if (err)
			return err;
	} else
		return -EINVAL;

	return count;
}

/* Show function for "event" sysfs files */
static ssize_t mlxbf_pmc_event_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mlxbf_pmc_attribute *attr_event = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	int blk_num, cnt_num, err;
	bool is_l3 = false;
	uint64_t evt_num;
	char *evt_name;

	blk_num = attr_event->nr;
	cnt_num = attr_event->index;

	if (strstr(pmc->block_name[blk_num], "l3cache"))
		is_l3 = true;

	err = mlxbf_pmc_read_event(blk_num, cnt_num, is_l3, &evt_num);
	if (err)
		return sprintf(buf, "No event being monitored\n");

	evt_name = mlxbf_pmc_get_event_name(pmc->block_name[blk_num], evt_num);
	if (!evt_name)
		return -EINVAL;

	return sprintf(buf, "0x%llx: %s\n", evt_num, evt_name);
}

/* Store function for "event" sysfs files */
static ssize_t mlxbf_pmc_event_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct mlxbf_pmc_attribute *attr_event = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	int blk_num, cnt_num, evt_num, err;
	bool is_l3 = false;

	blk_num = attr_event->nr;
	cnt_num = attr_event->index;

	if (isalpha(buf[0])) {
		evt_num = mlxbf_pmc_get_event_num(pmc->block_name[blk_num],
						  buf);
		if (evt_num < 0)
			return -EINVAL;
	} else {
		err = kstrtoint(buf, 0, &evt_num);
		if (err < 0)
			return err;
	}

	if (strstr(pmc->block_name[blk_num], "l3cache"))
		is_l3 = true;

	err = mlxbf_pmc_program_counter(blk_num, cnt_num, evt_num, is_l3);
	if (err)
		return err;

	return count;
}

/* Show function for "event_list" sysfs files */
static ssize_t mlxbf_pmc_event_list_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct mlxbf_pmc_attribute *attr_event_list = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	int blk_num, i, size, len = 0, ret = 0;
	const struct mlxbf_pmc_events *events;
	char e_info[MLXBF_PMC_EVENT_INFO_LEN];

	blk_num = attr_event_list->nr;

	events = mlxbf_pmc_event_list(pmc->block_name[blk_num], &size);
	if (!events)
		return -EINVAL;

	for (i = 0, buf[0] = '\0'; i < size; ++i) {
		len += sprintf(e_info, "0x%x: %s\n", events[i].evt_num,
			       events[i].evt_name);
		if (len > PAGE_SIZE)
			break;
		strcat(buf, e_info);
		ret = len;
	}

	return ret;
}

/* Show function for "enable" sysfs files - only for l3cache */
static ssize_t mlxbf_pmc_enable_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct mlxbf_pmc_attribute *attr_enable = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	uint32_t perfcnt_cfg;
	int blk_num, value;

	blk_num = attr_enable->nr;

	if (mlxbf_pmc_readl(pmc->block[blk_num].mmio_base +
				    MLXBF_PMC_L3C_PERF_CNT_CFG,
			    &perfcnt_cfg))
		return -EINVAL;

	value = FIELD_GET(MLXBF_PMC_L3C_PERF_CNT_CFG_EN, perfcnt_cfg);

	return sprintf(buf, "%d\n", value);
}

/* Store function for "enable" sysfs files - only for l3cache */
static ssize_t mlxbf_pmc_enable_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct mlxbf_pmc_attribute *attr_enable = container_of(
		attr, struct mlxbf_pmc_attribute, dev_attr);
	int err, en, blk_num;

	blk_num = attr_enable->nr;

	err = kstrtoint(buf, 0, &en);
	if (err < 0)
		return err;

	if (!en) {
		err = mlxbf_pmc_config_l3_counters(blk_num, false, false);
		if (err)
			return err;
	} else if (en == 1) {
		err = mlxbf_pmc_config_l3_counters(blk_num, false, true);
		if (err)
			return err;
		err = mlxbf_pmc_config_l3_counters(blk_num, true, false);
		if (err)
			return err;
	} else
		return -EINVAL;

	return count;
}

/* Populate attributes for blocks with counters to monitor performance */
static int mlxbf_pmc_init_perftype_counter(struct device *dev, int blk_num)
{
	struct mlxbf_pmc_attribute *attr;
	int i = 0, j = 0;

	/* "event_list" sysfs to list events supported by the block */
	attr = &pmc->block[blk_num].attr_event_list;
	attr->dev_attr.attr.mode = 0444;
	attr->dev_attr.show = mlxbf_pmc_event_list_show;
	attr->nr = blk_num;
	attr->dev_attr.attr.name = devm_kasprintf(dev, GFP_KERNEL, "event_list");
	pmc->block[blk_num].block_attr[i] = &attr->dev_attr.attr;
	attr = NULL;

	/* "enable" sysfs to start/stop the counters. Only in L3C blocks */
	if (strstr(pmc->block_name[blk_num], "l3cache")) {
		attr = &pmc->block[blk_num].attr_enable;
		attr->dev_attr.attr.mode = 0644;
		attr->dev_attr.show = mlxbf_pmc_enable_show;
		attr->dev_attr.store = mlxbf_pmc_enable_store;
		attr->nr = blk_num;
		attr->dev_attr.attr.name = devm_kasprintf(dev, GFP_KERNEL,
							  "enable");
		pmc->block[blk_num].block_attr[++i] = &attr->dev_attr.attr;
		attr = NULL;
	}

	pmc->block[blk_num].attr_counter = devm_kcalloc(
		dev, pmc->block[blk_num].counters,
		sizeof(struct mlxbf_pmc_attribute), GFP_KERNEL);
	if (!pmc->block[blk_num].attr_counter)
		return -ENOMEM;

	pmc->block[blk_num].attr_event = devm_kcalloc(
		dev, pmc->block[blk_num].counters,
		sizeof(struct mlxbf_pmc_attribute), GFP_KERNEL);
	if (!pmc->block[blk_num].attr_event)
		return -ENOMEM;

	/* "eventX" and "counterX" sysfs to program and read counter values */
	for (j = 0; j < pmc->block[blk_num].counters; ++j) {
		attr = &pmc->block[blk_num].attr_counter[j];
		attr->dev_attr.attr.mode = 0644;
		attr->dev_attr.show = mlxbf_pmc_counter_show;
		attr->dev_attr.store = mlxbf_pmc_counter_store;
		attr->index = j;
		attr->nr = blk_num;
		attr->dev_attr.attr.name = devm_kasprintf(dev, GFP_KERNEL,
							  "counter%d", j);
		pmc->block[blk_num].block_attr[++i] = &attr->dev_attr.attr;
		attr = NULL;

		attr = &pmc->block[blk_num].attr_event[j];
		attr->dev_attr.attr.mode = 0644;
		attr->dev_attr.show = mlxbf_pmc_event_show;
		attr->dev_attr.store = mlxbf_pmc_event_store;
		attr->index = j;
		attr->nr = blk_num;
		attr->dev_attr.attr.name = devm_kasprintf(dev, GFP_KERNEL,
							  "event%d", j);
		pmc->block[blk_num].block_attr[++i] = &attr->dev_attr.attr;
		attr = NULL;
	}

	return 0;
}

/* Populate attributes for blocks with registers to monitor performance */
static int mlxbf_pmc_init_perftype_reg(struct device *dev, int blk_num)
{
	struct mlxbf_pmc_attribute *attr;
	const struct mlxbf_pmc_events *events;
	int i = 0, j = 0;

	events = mlxbf_pmc_event_list(pmc->block_name[blk_num], &j);
	if (!events)
		return -EINVAL;

	pmc->block[blk_num].attr_event = devm_kcalloc(
		dev, j, sizeof(struct mlxbf_pmc_attribute), GFP_KERNEL);
	if (!pmc->block[blk_num].attr_event)
		return -ENOMEM;

	while (j > 0) {
		--j;
		attr = &pmc->block[blk_num].attr_event[j];
		attr->dev_attr.attr.mode = 0644;
		attr->dev_attr.show = mlxbf_pmc_counter_show;
		attr->dev_attr.store = mlxbf_pmc_counter_store;
		attr->nr = blk_num;
		attr->dev_attr.attr.name = devm_kasprintf(dev, GFP_KERNEL,
							  events[j].evt_name);
		pmc->block[blk_num].block_attr[i] = &attr->dev_attr.attr;
		attr = NULL;
		i++;
	}

	return 0;
}

/* Helper to create the bfperf sysfs sub-directories and files */
static int mlxbf_pmc_create_groups(struct device *dev, int blk_num)
{
	int err;

	/* Populate attributes based on counter type */
	if (pmc->block[blk_num].type == MLXBF_PMC_TYPE_COUNTER)
		err = mlxbf_pmc_init_perftype_counter(dev, blk_num);
	else if (pmc->block[blk_num].type == MLXBF_PMC_TYPE_REGISTER)
		err = mlxbf_pmc_init_perftype_reg(dev, blk_num);
	else
		err = -EINVAL;

	if (err)
		return err;

	/* Add a new attribute_group for the block */
	pmc->block[blk_num].block_attr_grp.attrs = pmc->block[blk_num].block_attr;
	pmc->block[blk_num].block_attr_grp.name = devm_kasprintf(
		dev, GFP_KERNEL, pmc->block_name[blk_num]);
	pmc->groups[blk_num] = &pmc->block[blk_num].block_attr_grp;

	return 0;
}

static bool mlxbf_pmc_guid_match(const guid_t *guid,
				 const struct arm_smccc_res *res)
{
	guid_t id = GUID_INIT(res->a0, res->a1, res->a1 >> 16, res->a2,
			      res->a2 >> 8, res->a2 >> 16, res->a2 >> 24,
			      res->a3, res->a3 >> 8, res->a3 >> 16,
			      res->a3 >> 24);

	return guid_equal(guid, &id);
}

/* Helper to map the Performance Counters from the varios blocks */
static int mlxbf_pmc_map_counters(struct device *dev)
{
	uint64_t info[MLXBF_PMC_INFO_SZ];
	int i, tile_num, ret;

	for (i = 0; i < pmc->total_blocks; ++i) {
		if (strstr(pmc->block_name[i], "tile")) {
			if (sscanf(pmc->block_name[i], "tile%d", &tile_num) != 1)
				return -EINVAL;

			if (tile_num >= pmc->tile_count)
				continue;
		}
		ret = device_property_read_u64_array(dev, pmc->block_name[i],
						     info, MLXBF_PMC_INFO_SZ);
		if (ret)
			return ret;

		/*
		 * Do not remap if the proper SMC calls are supported,
		 * since the SMC calls expect physical addresses.
		 */
		if (pmc->svc_sreg_support)
			pmc->block[i].mmio_base = (void __iomem *)info[0];
		else
			pmc->block[i].mmio_base =
				devm_ioremap(dev, info[0], info[1]);

		pmc->block[i].blk_size = info[1];
		pmc->block[i].counters = info[2];
		pmc->block[i].type = info[3];

		if (!pmc->block[i].mmio_base)
			return -ENOMEM;

		ret = mlxbf_pmc_create_groups(dev, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int mlxbf_pmc_probe(struct platform_device *pdev)
{
	struct acpi_device *acpi_dev = ACPI_COMPANION(&pdev->dev);
	const char *hid = acpi_device_hid(acpi_dev);
	struct device *dev = &pdev->dev;
	struct arm_smccc_res res;
	guid_t guid;
	int ret;

	/* Ensure we have the UUID we expect for this service. */
	arm_smccc_smc(MLXBF_PMC_SIP_SVC_UID, 0, 0, 0, 0, 0, 0, 0, &res);
	guid_parse(mlxbf_pmc_svc_uuid_str, &guid);
	if (!mlxbf_pmc_guid_match(&guid, &res))
		return -ENODEV;

	pmc = devm_kzalloc(dev, sizeof(struct mlxbf_pmc_context), GFP_KERNEL);
	if (!pmc)
		return -ENOMEM;

	/*
	 * ACPI indicates whether we use SMCs to access registers or not.
	 * If sreg_tbl_perf is not present, just assume we're not using SMCs.
	 */
	ret = device_property_read_u32(dev, "sec_reg_block",
				       &pmc->sreg_tbl_perf);
	if (ret) {
		pmc->svc_sreg_support = false;
	} else {
		/*
		 * Check service version to see if we actually do support the
		 * needed SMCs. If we have the calls we need, mark support for
		 * them in the pmc struct.
		 */
		arm_smccc_smc(MLXBF_PMC_SIP_SVC_VERSION, 0, 0, 0, 0, 0, 0, 0,
			      &res);
		if (res.a0 == MLXBF_PMC_SVC_REQ_MAJOR &&
		    res.a1 >= MLXBF_PMC_SVC_MIN_MINOR)
			pmc->svc_sreg_support = true;
		else
			return -EINVAL;
	}

	if (!strcmp(hid, "MLNXBFD0"))
		pmc->event_set = MLXBF_PMC_EVENT_SET_BF1;
	else if (!strcmp(hid, "MLNXBFD1"))
		pmc->event_set = MLXBF_PMC_EVENT_SET_BF2;
	else
		return -ENODEV;

	ret = device_property_read_u32(dev, "block_num", &pmc->total_blocks);
	if (ret)
		return ret;

	ret = device_property_read_string_array(dev, "block_name",
						pmc->block_name,
						pmc->total_blocks);
	if (ret != pmc->total_blocks)
		return -EFAULT;

	ret = device_property_read_u32(dev, "tile_num", &pmc->tile_count);
	if (ret)
		return ret;

	pmc->pdev = pdev;

	ret = mlxbf_pmc_map_counters(dev);
	if (ret)
		return ret;

	pmc->hwmon_dev = devm_hwmon_device_register_with_groups(
		dev, "bfperf", pmc, pmc->groups);
	platform_set_drvdata(pdev, pmc);

	return 0;
}

static const struct acpi_device_id mlxbf_pmc_acpi_ids[] = { { "MLNXBFD0", 0 },
							    { "MLNXBFD1", 0 },
							    {}, };

MODULE_DEVICE_TABLE(acpi, mlxbf_pmc_acpi_ids);
static struct platform_driver pmc_driver = {
	.driver = { .name = "mlxbf-pmc",
		    .acpi_match_table = ACPI_PTR(mlxbf_pmc_acpi_ids), },
	.probe = mlxbf_pmc_probe,
};

module_platform_driver(pmc_driver);

MODULE_AUTHOR("Shravan Kumar Ramani <sramani@mellanox.com>");
MODULE_DESCRIPTION("Mellanox PMC driver");
MODULE_LICENSE("Dual BSD/GPL");
