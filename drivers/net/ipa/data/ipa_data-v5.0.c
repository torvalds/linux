// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2023-2024 Linaro Ltd. */

#include <linux/array_size.h>
#include <linux/log2.h>

#include "../ipa_data.h"
#include "../ipa_endpoint.h"
#include "../ipa_mem.h"
#include "../ipa_version.h"

/** enum ipa_resource_type - IPA resource types for an SoC having IPA v5.0 */
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
	IPA_RESOURCE_TYPE_DST_ULSO_SEGMENTS,
};

/* Resource groups used for an SoC having IPA v5.0 */
enum ipa_rsrc_group_id {
	/* Source resource group identifiers */
	IPA_RSRC_GROUP_SRC_UL				= 0,
	IPA_RSRC_GROUP_SRC_DL,
	IPA_RSRC_GROUP_SRC_UNUSED_2,
	IPA_RSRC_GROUP_SRC_UNUSED_3,
	IPA_RSRC_GROUP_SRC_URLLC,
	IPA_RSRC_GROUP_SRC_U_RX_QC,
	IPA_RSRC_GROUP_SRC_COUNT,	/* Last in set; not a source group */

	/* Destination resource group identifiers */
	IPA_RSRC_GROUP_DST_UL				= 0,
	IPA_RSRC_GROUP_DST_DL,
	IPA_RSRC_GROUP_DST_DMA,
	IPA_RSRC_GROUP_DST_QDSS,
	IPA_RSRC_GROUP_DST_CV2X,
	IPA_RSRC_GROUP_DST_UC,
	IPA_RSRC_GROUP_DST_DRB_IP,
	IPA_RSRC_GROUP_DST_COUNT,	/* Last; not a destination group */
};

/* QSB configuration data for an SoC having IPA v5.0 */
static const struct ipa_qsb_data ipa_qsb_data[] = {
	[IPA_QSB_MASTER_DDR] = {
		.max_writes		= 0,
		.max_reads		= 0,	/* no limit (hardware max) */
		.max_reads_beats	= 0,
	},
	[IPA_QSB_MASTER_PCIE] = {
		.max_writes		= 0,
		.max_reads		= 0,	/* no limit (hardware max) */
		.max_reads_beats	= 0,
	},
};

/* Endpoint configuration data for an SoC having IPA v5.0 */
static const struct ipa_gsi_endpoint_data ipa_gsi_endpoint_data[] = {
	[IPA_ENDPOINT_AP_COMMAND_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 12,
		.endpoint_id	= 14,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 20,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_SRC_UL,
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
		.channel_id	= 13,
		.endpoint_id	= 16,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 9,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_DST_UL,
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
		.channel_id	= 11,
		.endpoint_id	= 2,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 512,
			.event_count	= 512,
			.tlv_count	= 25,
		},
		.endpoint = {
			.filter_support	= true,
			.config = {
				.resource_group	= IPA_RSRC_GROUP_SRC_UL,
				.checksum       = true,
				.qmap		= true,
				.status_enable	= true,
				.tx = {
					.seq_type = IPA_SEQ_2_PASS_SKIP_LAST_UC,
					.status_endpoint =
						IPA_ENDPOINT_MODEM_AP_RX,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 1,
		.endpoint_id	= 23,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 9,
		},
		.endpoint = {
			.config = {
				.resource_group	= IPA_RSRC_GROUP_DST_DL,
				.checksum       = true,
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
	[IPA_ENDPOINT_MODEM_AP_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 0,
		.endpoint_id	= 12,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
	[IPA_ENDPOINT_MODEM_AP_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 7,
		.endpoint_id	= 21,
		.toward_ipa	= false,
	},
	[IPA_ENDPOINT_MODEM_DL_NLO_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 2,
		.endpoint_id	= 15,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
};

/* Source resource configuration data for an SoC having IPA v5.0 */
static const struct ipa_resource ipa_resource_src[] = {
	[IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS] = {
		.limits[IPA_RSRC_GROUP_SRC_UL] = {
			.min = 3,	.max = 9,
		},
		.limits[IPA_RSRC_GROUP_SRC_DL] = {
			.min = 4,	.max = 10,
		},
		.limits[IPA_RSRC_GROUP_SRC_URLLC] = {
			.min = 1,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_U_RX_QC] = {
			.min = 0,	.max = 63,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS] = {
		.limits[IPA_RSRC_GROUP_SRC_UL] = {
			.min = 9,	.max = 9,
		},
		.limits[IPA_RSRC_GROUP_SRC_DL] = {
			.min = 12,	.max = 12,
		},
		.limits[IPA_RSRC_GROUP_SRC_URLLC] = {
			.min = 10,	.max = 10,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF] = {
		.limits[IPA_RSRC_GROUP_SRC_UL] = {
			.min = 9,	.max = 9,
		},
		.limits[IPA_RSRC_GROUP_SRC_DL] = {
			.min = 24,	.max = 24,
		},
		.limits[IPA_RSRC_GROUP_SRC_URLLC] = {
			.min = 20,	.max = 20,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_HPS_DMARS] = {
		.limits[IPA_RSRC_GROUP_SRC_UL] = {
			.min = 0,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_DL] = {
			.min = 0,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_URLLC] = {
			.min = 1,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_SRC_U_RX_QC] = {
			.min = 0,	.max = 63,
		},
	},
	[IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES] = {
		.limits[IPA_RSRC_GROUP_SRC_UL] = {
			.min = 22,	.max = 22,
		},
		.limits[IPA_RSRC_GROUP_SRC_DL] = {
			.min = 16,	.max = 16,
		},
		.limits[IPA_RSRC_GROUP_SRC_URLLC] = {
			.min = 16,	.max = 16,
		},
	},
};

/* Destination resource configuration data for an SoC having IPA v5.0 */
static const struct ipa_resource ipa_resource_dst[] = {
	[IPA_RESOURCE_TYPE_DST_DATA_SECTORS] = {
		.limits[IPA_RSRC_GROUP_DST_UL] = {
			.min = 6,	.max = 6,
		},
		.limits[IPA_RSRC_GROUP_DST_DL] = {
			.min = 5,	.max = 5,
		},
		.limits[IPA_RSRC_GROUP_DST_DRB_IP] = {
			.min = 39,	.max = 39,
		},
	},
	[IPA_RESOURCE_TYPE_DST_DPS_DMARS] = {
		.limits[IPA_RSRC_GROUP_DST_UL] = {
			.min = 0,	.max = 3,
		},
		.limits[IPA_RSRC_GROUP_DST_DL] = {
			.min = 0,	.max = 3,
		},
	},
	[IPA_RESOURCE_TYPE_DST_ULSO_SEGMENTS] = {
		.limits[IPA_RSRC_GROUP_DST_UL] = {
			.min = 0,	.max = 63,
		},
		.limits[IPA_RSRC_GROUP_DST_DL] = {
			.min = 0,	.max = 63,
		},
	},
};

/* Resource configuration data for an SoC having IPA v5.0 */
static const struct ipa_resource_data ipa_resource_data = {
	.rsrc_group_dst_count	= IPA_RSRC_GROUP_DST_COUNT,
	.rsrc_group_src_count	= IPA_RSRC_GROUP_SRC_COUNT,
	.resource_src_count	= ARRAY_SIZE(ipa_resource_src),
	.resource_src		= ipa_resource_src,
	.resource_dst_count	= ARRAY_SIZE(ipa_resource_dst),
	.resource_dst		= ipa_resource_dst,
};

/* IPA-resident memory region data for an SoC having IPA v5.0 */
static const struct ipa_mem ipa_mem_local_data[] = {
	{
		.id		= IPA_MEM_UC_EVENT_RING,
		.offset		= 0x0000,
		.size		= 0x1000,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_SHARED,
		.offset		= 0x1000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_UC_INFO,
		.offset		= 0x1080,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_V4_FILTER_HASHED,
		.offset		= 0x1288,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_FILTER,
		.offset		= 0x1308,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER_HASHED,
		.offset		= 0x1388,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_FILTER,
		.offset		= 0x1408,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE_HASHED,
		.offset		= 0x1488,
		.size		= 0x0098,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V4_ROUTE,
		.offset		= 0x1528,
		.size		= 0x0098,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE_HASHED,
		.offset		= 0x15c8,
		.size		= 0x0098,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_V6_ROUTE,
		.offset		= 0x1668,
		.size		= 0x0098,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_MODEM_HEADER,
		.offset		= 0x1708,
		.size		= 0x0240,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_HEADER,
		.offset		= 0x1948,
		.size		= 0x01e0,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_MODEM_PROC_CTX,
		.offset		= 0x1b40,
		.size		= 0x0b20,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_PROC_CTX,
		.offset		= 0x2660,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_STATS_QUOTA_MODEM,
		.offset		= 0x2868,
		.size		= 0x0060,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_STATS_QUOTA_AP,
		.offset		= 0x28c8,
		.size		= 0x0048,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_AP_V4_FILTER,
		.offset		= 0x2918,
		.size		= 0x0118,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_AP_V6_FILTER,
		.offset		= 0x2aa0,
		.size		= 0x0228,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_STATS_FILTER_ROUTE,
		.offset		= 0x2cd0,
		.size		= 0x0ba0,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_STATS_DROP,
		.offset		= 0x3870,
		.size		= 0x0020,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_MODEM,
		.offset		= 0x3898,
		.size		= 0x0d48,
		.canary_count	= 2,
	},
	{
		.id		= IPA_MEM_NAT_TABLE,
		.offset		= 0x45e0,
		.size		= 0x0900,
		.canary_count	= 0,
	},
	{
		.id		= IPA_MEM_PDN_CONFIG,
		.offset		= 0x4ee8,
		.size		= 0x0100,
		.canary_count	= 2,
	},
};

/* Memory configuration data for an SoC having IPA v5.0 */
static const struct ipa_mem_data ipa_mem_data = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data),
	.local		= ipa_mem_local_data,
	.imem_addr	= 0x14688000,
	.imem_size	= 0x00003000,
	.smem_id	= 497,
	.smem_size	= 0x00009000,
};

/* Interconnect rates are in 1000 byte/second units */
static const struct ipa_interconnect_data ipa_interconnect_data[] = {
	{
		.name			= "memory",
		.peak_bandwidth		= 1900000,	/* 1.9 GBps */
		.average_bandwidth	= 600000,	/* 600 MBps */
	},
	/* Average rate is unused for the next interconnect */
	{
		.name			= "config",
		.peak_bandwidth		= 76800,	/* 76.8 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
};

/* Clock and interconnect configuration data for an SoC having IPA v5.0 */
static const struct ipa_power_data ipa_power_data = {
	.core_clock_rate	= 120 * 1000 * 1000,	/* Hz */
	.interconnect_count	= ARRAY_SIZE(ipa_interconnect_data),
	.interconnect_data	= ipa_interconnect_data,
};

/* Configuration data for an SoC having IPA v5.0. */
const struct ipa_data ipa_data_v5_0 = {
	.version		= IPA_VERSION_5_0,
	.qsb_count		= ARRAY_SIZE(ipa_qsb_data),
	.qsb_data		= ipa_qsb_data,
	.modem_route_count	= 11,
	.endpoint_count		= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data		= ipa_gsi_endpoint_data,
	.resource_data		= &ipa_resource_data,
	.mem_data		= &ipa_mem_data,
	.power_data		= &ipa_power_data,
};
