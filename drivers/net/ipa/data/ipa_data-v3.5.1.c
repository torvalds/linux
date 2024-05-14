// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2021 Linaro Ltd.
 */

#include <linux/log2.h>

#include "../gsi.h"
#include "../ipa_data.h"
#include "../ipa_endpoint.h"
#include "../ipa_mem.h"

/** enum ipa_resource_type - IPA resource types for an SoC having IPA v3.5.1 */
enum ipa_resource_type {
	/* Source resource types; first must have value 0 */
	IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS		= 0,
	IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS,
	IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF,
	IPA_RESOURCE_TYPE_SRC_HPS_DMARS,
	IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES,

	/* Destination resource types; first must have value 0 */
	IPA_RESOURCE_TYPE_DST_DATA_SECTORS		= 0,
	IPA_RESOURCE_TYPE_DST_DPS_DMARS,
};

/* Resource groups used for an SoC having IPA v3.5.1 */
enum ipa_rsrc_group_id {
	/* Source resource group identifiers */
	IPA_RSRC_GROUP_SRC_LWA_DL	= 0,
	IPA_RSRC_GROUP_SRC_UL_DL,
	IPA_RSRC_GROUP_SRC_MHI_DMA,
	IPA_RSRC_GROUP_SRC_UC_RX_Q,
	IPA_RSRC_GROUP_SRC_COUNT,	/* Last in set; not a source group */

	/* Destination resource group identifiers */
	IPA_RSRC_GROUP_DST_LWA_DL	= 0,
	IPA_RSRC_GROUP_DST_UL_DL_DPL,
	IPA_RSRC_GROUP_DST_UNUSED_2,
	IPA_RSRC_GROUP_DST_COUNT,	/* Last; not a destination group */
};

/* QSB configuration data for an SoC having IPA v3.5.1 */
static const struct ipa_qsb_data ipa_qsb_data[] = {
	[IPA_QSB_MASTER_DDR] = {
		.max_writes	= 8,
		.max_reads	= 8,
	},
	[IPA_QSB_MASTER_PCIE] = {
		.max_writes	= 4,
		.max_reads	= 12,
	},
};

/* Endpoint datdata for an SoC having IPA v3.5.1 */
static const struct ipa_gsi_endpoint_data ipa_gsi_endpoint_data[] = {
	[IPA_ENDPOINT_AP_COMMAND_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 4,
		.endpoint_id	= 5,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 512,
			.event_count	= 256,
			.tlv_count	= 20,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_SRC_UL_DL,
				.dma_mode	= true,
				.dma_endpoint	= IPA_ENDPOINT_AP_LAN_RX,
				.tx = {
					.seq_type = IPA_SEQ_DMA,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_LAN_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 5,
		.endpoint_id	= 9,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 8,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_DST_UL_DL_DPL,
				.aggregation	= true,
				.status_enable	= true,
				.rx = {
					.buffer_size	= 8192,
					.pad_align	= ilog2(sizeof(u32)),
					.aggr_time_limit = 500,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 3,
		.endpoint_id	= 2,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 512,
			.event_count	= 512,
			.tlv_count	= 16,
		},
		.endpoint = {
			.filter_support	= true,
			.config = {
				.resource_group	= IPA_RSRC_GROUP_SRC_UL_DL,
				.checksum	= true,
				.qmap		= true,
				.status_enable	= true,
				.tx = {
					.seq_type = IPA_SEQ_2_PASS_SKIP_LAST_UC,
					.seq_rep_type = IPA_SEQ_REP_DMA_PARSER,
					.status_endpoint =
						IPA_ENDPOINT_MODEM_AP_RX,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 6,
		.endpoint_id	= 10,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 8,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_DST_UL_DL_DPL,
				.checksum	= true,
				.qmap		= true,
				.aggregation	= true,
				.rx = {
					.buffer_size	= 8192,
					.aggr_time_limit = 500,
					.aggr_close_eof	= true,
				},
			},
		},
	},
	[IPA_ENDPOINT_MODEM_LAN_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 0,
		.endpoint_id	= 3,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
	[IPA_ENDPOINT_MODEM_AP_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 4,
		.endpoint_id	= 6,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
	[IPA_ENDPOINT_MODEM_AP_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 2,
		.endpoint_id	= 12,
		.toward_ipa	= false,
	},
};

/* Source resource configuration data for an SoC having IPA v3.5.1 */
static const struct ipa_resource ipa_resource_src[] = {
	[IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS] = {
		.limits[IPA_RSRC_GROUP_SRC_LWA_DL] = {
			.min = 1,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 1,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_UC_RX_Q] = {
			.min = 1,	.max = 63,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS] = {
		.limits[IPA_RSRC_GROUP_SRC_LWA_DL] = {
			.min = 10,	.max = 10,
		},
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 10,	.max = 10,
		},
		.limits[IPA_RSRC_GROUP_SRC_UC_RX_Q] = {
			.min = 8,	.max = 8,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF] = {
		.limits[IPA_RSRC_GROUP_SRC_LWA_DL] = {
			.min = 12,	.max = 12,
		},
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 14,	.max = 14,
		},
		.limits[IPA_RSRC_GROUP_SRC_UC_RX_Q] = {
			.min = 8,	.max = 8,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_HPS_DMARS] = {
		.limits[IPA_RSRC_GROUP_SRC_LWA_DL] = {
			.min = 0,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 0,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_MHI_DMA] = {
			.min = 0,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_UC_RX_Q] = {
			.min = 0,	.max = 63,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES] = {
		.limits[IPA_RSRC_GROUP_SRC_LWA_DL] = {
			.min = 14,	.max = 14,
		},
		.limits[IPA_RSRC_GROUP_SRC_UL_DL] = {
			.min = 20,	.max = 20,
		},
		.limits[IPA_RSRC_GROUP_SRC_UC_RX_Q] = {
			.min = 14,	.max = 14,
		},
	},
};

/* Destination resource configuration data for an SoC having IPA v3.5.1 */
static const struct ipa_resource ipa_resource_dst[] = {
	[IPA_RESOURCE_TYPE_DST_DATA_SECTORS] = {
		.limits[IPA_RSRC_GROUP_DST_LWA_DL] = {
			.min = 4,	.max = 4,
		},
		.limits[1] = {
			.min = 4,	.max = 4,
		},
		.limits[IPA_RSRC_GROUP_DST_UNUSED_2] = {
			.min = 3,	.max = 3,
		}
	},
	[IPA_RESOURCE_TYPE_DST_DPS_DMARS] = {
		.limits[IPA_RSRC_GROUP_DST_LWA_DL] = {
			.min = 2,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_DST_UL_DL_DPL] = {
			.min = 1,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_DST_UNUSED_2] = {
			.min = 1,	.max = 2,
		}
	},
};

/* Resource configuration data for an SoC having IPA v3.5.1 */
static const struct ipa_resource_data ipa_resource_data = {
	.rsrc_group_src_count	= IPA_RSRC_GROUP_SRC_COUNT,
	.rsrc_group_dst_count	= IPA_RSRC_GROUP_DST_COUNT,
	.resource_src_count	= ARRAY_SIZE(ipa_resource_src),
	.resource_src		= ipa_resource_src,
	.resource_dst_count	= ARRAY_SIZE(ipa_resource_dst),
	.resource_dst		= ipa_resource_dst,
};

/* IPA-resident memory region data for an SoC having IPA v3.5.1 */
static const struct ipa_mem ipa_mem_local_data[] = {
	{
		.id		= IPA_MEM_UC_SHARED,
		.offset		= 0x0000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_INFO,
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_V4_FILTER_HASHED,
		.offset		= 0x0288,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_FILTER,
		.offset		= 0x0308,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER_HASHED,
		.offset		= 0x0388,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER,
		.offset		= 0x0408,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE_HASHED,
		.offset		= 0x0488,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE,
		.offset		= 0x0508,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE_HASHED,
		.offset		= 0x0588,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE,
		.offset		= 0x0608,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_MODEM_HEADER,
		.offset		= 0x0688,
		.size		= 0x0140,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_MODEM_PROC_CTX,
		.offset		= 0x07d0,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_PROC_CTX,
		.offset		= 0x09d0,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_MODEM,
		.offset		= 0x0bd8,
		.size		= 0x1024,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_EVENT_RING,
		.offset		= 0x1c00,
		.size		= 0x0400,
		.canary_count	= 1,
	},
};

/* Memory configuration data for an SoC having IPA v3.5.1 */
static const struct ipa_mem_data ipa_mem_data = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data),
	.local		= ipa_mem_local_data,
	.imem_addr	= 0x146bd000,
	.imem_size	= 0x00002000,
	.smem_id	= 497,
	.smem_size	= 0x00002000,
};

/* Interconnect bandwidths are in 1000 byte/second units */
static const struct ipa_interconnect_data ipa_interconnect_data[] = {
	{
		.name			= "memory",
		.peak_bandwidth		= 600000,	/* 600 MBps */
		.average_bandwidth	= 80000,	/* 80 MBps */
	},
	/* Average bandwidth is unused for the next two interconnects */
	{
		.name			= "imem",
		.peak_bandwidth		= 350000,	/* 350 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
	{
		.name			= "config",
		.peak_bandwidth		= 40000,	/* 40 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
};

/* Clock and interconnect configuration data for an SoC having IPA v3.5.1 */
static const struct ipa_power_data ipa_power_data = {
	.core_clock_rate	= 75 * 1000 * 1000,	/* Hz */
	.interconnect_count	= ARRAY_SIZE(ipa_interconnect_data),
	.interconnect_data	= ipa_interconnect_data,
};

/* Configuration data for an SoC having IPA v3.5.1 */
const struct ipa_data ipa_data_v3_5_1 = {
	.version	= IPA_VERSION_3_5_1,
	.backward_compat = BIT(BCR_CMDQ_L_LACK_ONE_ENTRY) |
			   BIT(BCR_TX_NOT_USING_BRESP) |
			   BIT(BCR_SUSPEND_L2_IRQ) |
			   BIT(BCR_HOLB_DROP_L2_IRQ) |
			   BIT(BCR_DUAL_TX),
	.qsb_count	= ARRAY_SIZE(ipa_qsb_data),
	.qsb_data	= ipa_qsb_data,
	.endpoint_count	= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data	= ipa_gsi_endpoint_data,
	.resource_data	= &ipa_resource_data,
	.mem_data	= &ipa_mem_data,
	.power_data	= &ipa_power_data,
};
