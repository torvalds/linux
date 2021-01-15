// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2019-2020 Linaro Ltd.
 */

#include <linux/log2.h>

#include "gsi.h"
#include "ipa_data.h"
#include "ipa_endpoint.h"
#include "ipa_mem.h"

/* Endpoint configuration for the SDM845 SoC. */
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
			.seq_type	= IPA_SEQ_DMA_ONLY,
			.config = {
				.resource_group	= 1,
				.dma_mode	= true,
				.dma_endpoint	= IPA_ENDPOINT_AP_LAN_RX,
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
			.seq_type	= IPA_SEQ_INVALID,
			.config = {
				.resource_group	= 1,
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
			.seq_type	=
				IPA_SEQ_2ND_PKT_PROCESS_PASS_NO_DEC_UCP,
			.config = {
				.resource_group	= 1,
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
		.channel_id	= 6,
		.endpoint_id	= 10,
		.toward_ipa	= false,
		.channel = {
			.tre_count	= 256,
			.event_count	= 256,
			.tlv_count	= 8,
		},
		.endpoint = {
			.seq_type	= IPA_SEQ_INVALID,
			.config = {
				.resource_group	= 1,
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
		.endpoint_id	= 4,
		.toward_ipa	= true,
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
	[IPA_ENDPOINT_MODEM_LAN_RX] = {
		.ee_id		= GSI_EE_MODEM,
		.channel_id	= 3,
		.endpoint_id	= 13,
		.toward_ipa	= false,
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

/* For the SDM845, resource groups are allocated this way:
 *   group 0:	LWA_DL
 *   group 1:	UL_DL
 */
static const struct ipa_resource_src ipa_resource_src[] = {
	{
		.type = IPA_RESOURCE_TYPE_SRC_PKT_CONTEXTS,
		.limits[0] = {
			.min = 1,
			.max = 255,
		},
		.limits[1] = {
			.min = 1,
			.max = 255,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_LISTS,
		.limits[0] = {
			.min = 10,
			.max = 10,
		},
		.limits[1] = {
			.min = 10,
			.max = 10,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_DESCRIPTOR_BUFF,
		.limits[0] = {
			.min = 12,
			.max = 12,
		},
		.limits[1] = {
			.min = 14,
			.max = 14,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_HPS_DMARS,
		.limits[0] = {
			.min = 0,
			.max = 63,
		},
		.limits[1] = {
			.min = 0,
			.max = 63,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_SRC_ACK_ENTRIES,
		.limits[0] = {
			.min = 14,
			.max = 14,
		},
		.limits[1] = {
			.min = 20,
			.max = 20,
		},
	},
};

static const struct ipa_resource_dst ipa_resource_dst[] = {
	{
		.type = IPA_RESOURCE_TYPE_DST_DATA_SECTORS,
		.limits[0] = {
			.min = 4,
			.max = 4,
		},
		.limits[1] = {
			.min = 4,
			.max = 4,
		},
	},
	{
		.type = IPA_RESOURCE_TYPE_DST_DPS_DMARS,
		.limits[0] = {
			.min = 2,
			.max = 63,
		},
		.limits[1] = {
			.min = 1,
			.max = 63,
		},
	},
};

/* Resource configuration for the SDM845 SoC. */
static const struct ipa_resource_data ipa_resource_data = {
	.resource_src_count	= ARRAY_SIZE(ipa_resource_src),
	.resource_src		= ipa_resource_src,
	.resource_dst_count	= ARRAY_SIZE(ipa_resource_dst),
	.resource_dst		= ipa_resource_dst,
};

/* IPA-resident memory region configuration for the SDM845 SoC. */
static const struct ipa_mem ipa_mem_local_data[] = {
	[IPA_MEM_UC_SHARED] = {
		.offset		= 0x0000,
		.size		= 0x0080,
		.canary_count	= 0,
	},
	[IPA_MEM_UC_INFO] = {
		.offset		= 0x0080,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	[IPA_MEM_V4_FILTER_HASHED] = {
		.offset		= 0x0288,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_FILTER] = {
		.offset		= 0x0308,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_FILTER_HASHED] = {
		.offset		= 0x0388,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_FILTER] = {
		.offset		= 0x0408,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_ROUTE_HASHED] = {
		.offset		= 0x0488,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V4_ROUTE] = {
		.offset		= 0x0508,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_ROUTE_HASHED] = {
		.offset		= 0x0588,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_V6_ROUTE] = {
		.offset		= 0x0608,
		.size		= 0x0078,
		.canary_count	= 2,
	},
	[IPA_MEM_MODEM_HEADER] = {
		.offset		= 0x0688,
		.size		= 0x0140,
		.canary_count	= 2,
	},
	[IPA_MEM_AP_HEADER] = {
		.offset		= 0x07c8,
		.size		= 0x0000,
		.canary_count	= 0,
	},
	[IPA_MEM_MODEM_PROC_CTX] = {
		.offset		= 0x07d0,
		.size		= 0x0200,
		.canary_count	= 2,
	},
	[IPA_MEM_AP_PROC_CTX] = {
		.offset		= 0x09d0,
		.size		= 0x0200,
		.canary_count	= 0,
	},
	[IPA_MEM_MODEM] = {
		.offset		= 0x0bd8,
		.size		= 0x1024,
		.canary_count	= 0,
	},
	[IPA_MEM_UC_EVENT_RING] = {
		.offset		= 0x1c00,
		.size		= 0x0400,
		.canary_count	= 1,
	},
};

static struct ipa_mem_data ipa_mem_data = {
	.local_count	= ARRAY_SIZE(ipa_mem_local_data),
	.local		= ipa_mem_local_data,
	.imem_addr	= 0x146bd000,
	.imem_size	= 0x00002000,
	.smem_id	= 497,
	.smem_size	= 0x00002000,
};

static struct ipa_clock_data ipa_clock_data = {
	.core_clock_rate	= 75 * 1000 * 1000,	/* Hz */
	/* Interconnect rates are in 1000 byte/second units */
	.interconnect = {
		[IPA_INTERCONNECT_MEMORY] = {
			.peak_rate	= 600000,	/* 600 MBps */
			.average_rate	= 80000,	/* 80 MBps */
		},
		/* Average rate is unused for the next two interconnects */
		[IPA_INTERCONNECT_IMEM] = {
			.peak_rate	= 350000,	/* 350 MBps */
			.average_rate	= 0,		/* unused */
		},
		[IPA_INTERCONNECT_CONFIG] = {
			.peak_rate	= 40000,	/* 40 MBps */
			.average_rate	= 0,		/* unused */
		},
	},
};

/* Configuration data for the SDM845 SoC. */
const struct ipa_data ipa_data_sdm845 = {
	.version	= IPA_VERSION_3_5_1,
	.endpoint_count	= ARRAY_SIZE(ipa_gsi_endpoint_data),
	.endpoint_data	= ipa_gsi_endpoint_data,
	.resource_data	= &ipa_resource_data,
	.mem_data	= &ipa_mem_data,
	.clock_data	= &ipa_clock_data,
};
