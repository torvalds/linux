// SPDX-License-Identifier: GPL-2.0

/* Copyright (C) 2019-2020 Linaro Ltd. */

#include <linux/log2.h>

#include "gsi.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"

/* Endpoint configuration for the SC7180 SoC. */
static const struct ipa_gsi_endpoint_data ipa_gsi_endpoint_data[] = {
	[IPA_ENDPOINT_AP_COMMAND_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 1,
		.endpoint_id	= 6,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 20,
		},
		.endpoint = {
			.seq_type	= IPA_SEQ_DMA_ONLY,
			.config = {
				.resource_group	= 0,
				.dma_mode	= true,
				.dma_endpoint	= IPA_ENDPOINT_AP_LAN_RX,
			},
		},
	},
	[IPA_ENDPOINT_AP_LAN_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 2,
		.endpoint_id	= 8,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 6,
		},
		.endpoint = {
			.seq_type	= IPA_SEQ_INVALID,
			.config = {
				.resource_group	= 0,
				.aggregation	= true,
				.status_enable	= true,
				.rx = {
					.pad_align	= ilog2(sizeof(u32)),
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_TX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 0,
		.endpoint_id	= 1,
		.toward_ipa	= true,
		.channel = {
			.tre_count	= 512,
			.event_count	= 512,
			.tlv_count	= 8,
		},
		.endpoint = {
			.filter_support	= true,
			.seq_type	=
				IPA_SEQ_PKT_PROCESS_NO_DEC_NO_UCP_DMAP,
			.config = {
				.resource_group	= 0,
				.checksum	= true,
				.qmap		= true,
				.status_enable	= true,
				.tx = {
					.status_endpoint =
						IPA_ENDPOINT_MODEM_AP_RX,
				},
			},
		},
	},
	[IPA_ENDPOINT_AP_MODEM_RX] = {
		.ee_id		= GSI_EE_AP,
		.channel_id	= 3,
		.endpoint_id	= 9,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 6,
		},
		.endpoint = {
			.seq_type	= IPA_SEQ_INVALID,
			.config = {
				.resource_group	= 0,
				.checksum	= true,
				.qmap		= true,
				.aggregation	= true,
				.rx = {
					.aggr_close_eof	= true,
				},
			},
		},
	},
	[IPA_ENDPOINT_MODEM_COMMAND_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 1,
		.endpoint_id	= 5,
		.toward_ipa	= true,
	},
	[IPA_ENDPOINT_MODEM_LAN_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 3,
		.endpoint_id	= 11,
		.toward_ipa	= false,
	},
	[IPA_ENDPOINT_MODEM_AP_TX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 0,
		.endpoint_id	= 4,
		.toward_ipa	= true,
		.endpoint = {
			.filter_support	= true,
		},
	},
	[IPA_ENDPOINT_MODEM_AP_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 2,
		.endpoint_id	= 10,
		.toward_ipa	= false,
	},
};

/* For the SC7180, resource groups are allocated this way:
 *   group 0:	UL_DL
 */
static const struct ipa_resource_src ipa_resource_src[] = {
	{
		.type = IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS,
		.limits[0] = {
			.min = 3,
			.max = 63,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS,
		.limits[0] = {
			.min = 3,
			.max = 3,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF,
		.limits[0] = {
			.min = 10,
			.max = 10,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_HPS_DMARS,
		.limits[0] = {
			.min = 1,
			.max = 1,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES,
		.limits[0] = {
			.min = 5,
			.max = 5,
		},
	},
};

static const struct ipa_resource_dst ipa_resource_dst[] = {
	{
		.type = IPA_RESOURCE_TYPE_DST_DATA_SECTORS,
		.limits[0] = {
			.min = 3,
			.max = 3,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_DST_DPS_DMARS,
		.limits[0] = {
			.min = 1,
			.max = 63,
		},
	},
};

/* Resource configuration for the SC7180 SoC. */
static const struct ipa_resource_data ipa_resource_data = {
	.resource_src_count	= ARRAY_SIZE(ipa_resource_src),
	.resource_src		= ipa_resource_src,
	.resource_dst_count	= ARRAY_SIZE(ipa_resource_dst),
	.resource_dst		= ipa_resource_dst,
};

/* IPA-resident memory region configuration for the SC7180 SoC. */
static const struct ipa_mem ipa_mem_local_data[] = {
	[IPA_MEM_UC_SHARED] = {
		.offset		= 0x0000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	[IPA_MEM_UC_INFO] = {
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_FILTER_HASHED] = {
		.offset		= 0x0288,
		.size		= 0,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_FILTER] = {
		.offset		= 0x0290,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_FILTER_HASHED] = {
		.offset		= 0x0310,
		.size		= 0,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_FILTER] = {
		.offset		= 0x0318,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_ROUTE_HASHED] = {
		.offset		= 0x0398,
		.size		= 0,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_ROUTE] = {
		.offset		= 0x03a0,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_ROUTE_HASHED] = {
		.offset		= 0x0420,
		.size		= 0,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_ROUTE] = {
		.offset		= 0x0428,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_MODEM_HEADER] = {
		.offset		= 0x04a8,
		.size		= 0x0140,
		.canary_count	= 2,
	},
	[IPA_MEM_AP_HEADER] = {
		.offset		= 0x05e8,
		.size		= 0x0000,
		.canary_count	= 0,
	},
	[IPA_MEM_MODEM_PROC_CTX] = {
		.offset		= 0x05f0,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	[IPA_MEM_AP_PROC_CTX] = {
		.offset		= 0x07f0,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	[IPA_MEM_PDN_CONFIG] = {
		.offset		= 0x09f8,
		.size		= 0x0050,
		.canary_count	= 2,
	},
	[IPA_MEM_STATS_QUOTA] = {
		.offset		= 0x0a50,
		.size		= 0x0060,
		.canary_count	= 2,
	},
	[IPA_MEM_STATS_TETHERING] = {
		.offset		= 0x0ab0,
		.size		= 0x0140,
		.canary_count	= 0,
	},
	[IPA_MEM_STATS_DROP] = {
		.offset		= 0x0bf0,
		.size		= 0,
		.canary_count	= 0,
	},
	[IPA_MEM_MODEM] = {
		.offset		= 0x0bf0,
		.size		= 0x140c,
		.canary_count	= 0,
	},
	[IPA_MEM_UC_EVENT_RING] = {
		.offset		= 0x2000,
		.size		= 0,
		.canary_count	= 1,
	},
};

static struct ipa_mem_data ipa_mem_data = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data),
	.local		= ipa_mem_local_data,
	.imem_addr	= 0x146a8000,
	.imem_size	= 0x00002000,
	.smem_id	= 497,
	.smem_size	= 0x00002000,
};

/* Interconnect bandwidths are in 1000 byte/second units */
static struct ipa_interconnect_data ipa_interconnect_data[] = {
	{
		.name			= "memory",
		.peak_bandwidth		= 465000,	/* 465 MBps */
		.average_bandwidth	= 80000,	/* 80 MBps */
	},
	/* Average bandwidth is unused for the next two interconnects */
	{
		.name			= "imem",
		.peak_bandwidth		= 68570,	/* 68.570 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
	{
		.name			= "config",
		.peak_bandwidth		= 30000,	/* 30 MBps */
		.average_bandwidth	= 0,		/* unused */
	},
};

static struct ipa_clock_data ipa_clock_data = {
	.core_clock_rate	= 100 * 1000 * 1000,	/* Hz */
	.interconnect_count	= ARRAY_SIZE(ipa_interconnect_data),
	.interconnect_data	= ipa_interconnect_data,
};

/* Configuration data for the SC7180 SoC. */
const struct ipa_data ipa_data_sc7180 = {
	.version	= IPA_VERSION_4_2,
	.endpoint_count	= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data	= ipa_gsi_endpoint_data,
	.resource_data	= &ipa_resource_data,
	.mem_data	= &ipa_mem_data,
	.clock_data	= &ipa_clock_data,
};
