// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2022 Texas Instruments Incorporated - https://www.ti.com
 */

#include <linux/kernel.h>

#include "k3-psil-priv.h"

#define PSIL_PDMA_XY_PKT(x)					\
	{							\
		.thread_id = x,					\
		.ep_config = {					\
			.ep_type = PSIL_EP_PDMA_XY,		\
			.mapped_channel_id = -1,		\
			.default_flow_id = -1,			\
			.pkt_mode = 1,				\
		},						\
	}

#define PSIL_ETHERNET(x, ch, flow_base, flow_cnt)		\
	{							\
		.thread_id = x,					\
		.ep_config = {					\
			.ep_type = PSIL_EP_NATIVE,		\
			.pkt_mode = 1,				\
			.needs_epib = 1,			\
			.psd_size = 16,				\
			.mapped_channel_id = ch,		\
			.flow_start = flow_base,		\
			.flow_num = flow_cnt,			\
			.default_flow_id = flow_base,		\
		},						\
	}

#define PSIL_SAUL(x, ch, flow_base, flow_cnt, default_flow, tx)	\
	{							\
		.thread_id = x,					\
		.ep_config = {					\
			.ep_type = PSIL_EP_NATIVE,		\
			.pkt_mode = 1,				\
			.needs_epib = 1,			\
			.psd_size = 64,				\
			.mapped_channel_id = ch,		\
			.flow_start = flow_base,		\
			.flow_num = flow_cnt,			\
			.default_flow_id = default_flow,	\
			.notdpkt = tx,				\
		},						\
	}

#define PSIL_PDMA_MCASP(x)				\
	{						\
		.thread_id = x,				\
		.ep_config = {				\
			.ep_type = PSIL_EP_PDMA_XY,	\
			.pdma_acc32 = 1,		\
			.pdma_burst = 1,		\
		},					\
	}

#define PSIL_CSI2RX(x)					\
	{						\
		.thread_id = x,				\
		.ep_config = {				\
			.ep_type = PSIL_EP_NATIVE,	\
		},					\
	}

/* PSI-L source thread IDs, used for RX (DMA_DEV_TO_MEM) */
static struct psil_ep am62_src_ep_map[] = {
	/* SAUL */
	PSIL_SAUL(0x7504, 20, 35, 8, 35, 0),
	PSIL_SAUL(0x7505, 21, 35, 8, 36, 0),
	PSIL_SAUL(0x7506, 22, 43, 8, 43, 0),
	PSIL_SAUL(0x7507, 23, 43, 8, 44, 0),
	/* PDMA_MAIN0 - SPI0-3 */
	PSIL_PDMA_XY_PKT(0x4302),
	PSIL_PDMA_XY_PKT(0x4303),
	PSIL_PDMA_XY_PKT(0x4304),
	PSIL_PDMA_XY_PKT(0x4305),
	PSIL_PDMA_XY_PKT(0x4306),
	PSIL_PDMA_XY_PKT(0x4307),
	PSIL_PDMA_XY_PKT(0x4308),
	PSIL_PDMA_XY_PKT(0x4309),
	PSIL_PDMA_XY_PKT(0x430a),
	PSIL_PDMA_XY_PKT(0x430b),
	PSIL_PDMA_XY_PKT(0x430c),
	PSIL_PDMA_XY_PKT(0x430d),
	/* PDMA_MAIN1 - UART0-6 */
	PSIL_PDMA_XY_PKT(0x4400),
	PSIL_PDMA_XY_PKT(0x4401),
	PSIL_PDMA_XY_PKT(0x4402),
	PSIL_PDMA_XY_PKT(0x4403),
	PSIL_PDMA_XY_PKT(0x4404),
	PSIL_PDMA_XY_PKT(0x4405),
	PSIL_PDMA_XY_PKT(0x4406),
	/* PDMA_MAIN2 - MCASP0-2 */
	PSIL_PDMA_MCASP(0x4500),
	PSIL_PDMA_MCASP(0x4501),
	PSIL_PDMA_MCASP(0x4502),
	/* CPSW3G */
	PSIL_ETHERNET(0x4600, 19, 19, 16),
	/* CSI2RX */
	PSIL_CSI2RX(0x4700),
	PSIL_CSI2RX(0x4701),
	PSIL_CSI2RX(0x4702),
	PSIL_CSI2RX(0x4703),
	PSIL_CSI2RX(0x4704),
	PSIL_CSI2RX(0x4705),
	PSIL_CSI2RX(0x4706),
	PSIL_CSI2RX(0x4707),
	PSIL_CSI2RX(0x4708),
	PSIL_CSI2RX(0x4709),
	PSIL_CSI2RX(0x470a),
	PSIL_CSI2RX(0x470b),
	PSIL_CSI2RX(0x470c),
	PSIL_CSI2RX(0x470d),
	PSIL_CSI2RX(0x470e),
	PSIL_CSI2RX(0x470f),
	PSIL_CSI2RX(0x4710),
	PSIL_CSI2RX(0x4711),
	PSIL_CSI2RX(0x4712),
	PSIL_CSI2RX(0x4713),
	PSIL_CSI2RX(0x4714),
	PSIL_CSI2RX(0x4715),
	PSIL_CSI2RX(0x4716),
	PSIL_CSI2RX(0x4717),
	PSIL_CSI2RX(0x4718),
	PSIL_CSI2RX(0x4719),
	PSIL_CSI2RX(0x471a),
	PSIL_CSI2RX(0x471b),
	PSIL_CSI2RX(0x471c),
	PSIL_CSI2RX(0x471d),
	PSIL_CSI2RX(0x471e),
	PSIL_CSI2RX(0x471f),
};

/* PSI-L destination thread IDs, used for TX (DMA_MEM_TO_DEV) */
static struct psil_ep am62_dst_ep_map[] = {
	/* SAUL */
	PSIL_SAUL(0xf500, 27, 83, 8, 83, 1),
	PSIL_SAUL(0xf501, 28, 91, 8, 91, 1),
	/* PDMA_MAIN0 - SPI0-3 */
	PSIL_PDMA_XY_PKT(0xc302),
	PSIL_PDMA_XY_PKT(0xc303),
	PSIL_PDMA_XY_PKT(0xc304),
	PSIL_PDMA_XY_PKT(0xc305),
	PSIL_PDMA_XY_PKT(0xc306),
	PSIL_PDMA_XY_PKT(0xc307),
	PSIL_PDMA_XY_PKT(0xc308),
	PSIL_PDMA_XY_PKT(0xc309),
	PSIL_PDMA_XY_PKT(0xc30a),
	PSIL_PDMA_XY_PKT(0xc30b),
	PSIL_PDMA_XY_PKT(0xc30c),
	PSIL_PDMA_XY_PKT(0xc30d),
	/* PDMA_MAIN1 - UART0-6 */
	PSIL_PDMA_XY_PKT(0xc400),
	PSIL_PDMA_XY_PKT(0xc401),
	PSIL_PDMA_XY_PKT(0xc402),
	PSIL_PDMA_XY_PKT(0xc403),
	PSIL_PDMA_XY_PKT(0xc404),
	PSIL_PDMA_XY_PKT(0xc405),
	PSIL_PDMA_XY_PKT(0xc406),
	/* PDMA_MAIN2 - MCASP0-2 */
	PSIL_PDMA_MCASP(0xc500),
	PSIL_PDMA_MCASP(0xc501),
	PSIL_PDMA_MCASP(0xc502),
	/* CPSW3G */
	PSIL_ETHERNET(0xc600, 19, 19, 8),
	PSIL_ETHERNET(0xc601, 20, 27, 8),
	PSIL_ETHERNET(0xc602, 21, 35, 8),
	PSIL_ETHERNET(0xc603, 22, 43, 8),
	PSIL_ETHERNET(0xc604, 23, 51, 8),
	PSIL_ETHERNET(0xc605, 24, 59, 8),
	PSIL_ETHERNET(0xc606, 25, 67, 8),
	PSIL_ETHERNET(0xc607, 26, 75, 8),
};

struct psil_ep_map am62_ep_map = {
	.name = "am62",
	.src = am62_src_ep_map,
	.src_count = ARRAY_SIZE(am62_src_ep_map),
	.dst = am62_dst_ep_map,
	.dst_count = ARRAY_SIZE(am62_dst_ep_map),
};
