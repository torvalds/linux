// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com
 */

#include <linux/kernel.h>

#include "k3-psil-priv.h"

#define PSIL_PDMA_XY_TR(x)					\
	{							\
		.thread_id = x,					\
		.ep_config = {					\
			.ep_type = PSIL_EP_PDMA_XY,		\
			.mapped_channel_id = -1,		\
			.default_flow_id = -1,			\
		},						\
	}

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
static struct psil_ep am62p_src_ep_map[] = {
	/* SAUL */
	PSIL_SAUL(0x7504, 20, 35, 8, 35, 0),
	PSIL_SAUL(0x7505, 21, 35, 8, 36, 0),
	PSIL_SAUL(0x7506, 22, 43, 8, 43, 0),
	PSIL_SAUL(0x7507, 23, 43, 8, 44, 0),
	/* PDMA_MAIN0 - SPI0-2 */
	PSIL_PDMA_XY_PKT(0x4300),
	PSIL_PDMA_XY_PKT(0x4301),
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
	PSIL_CSI2RX(0x5000),
	PSIL_CSI2RX(0x5001),
	PSIL_CSI2RX(0x5002),
	PSIL_CSI2RX(0x5003),
	PSIL_CSI2RX(0x5004),
	PSIL_CSI2RX(0x5005),
	PSIL_CSI2RX(0x5006),
	PSIL_CSI2RX(0x5007),
	PSIL_CSI2RX(0x5008),
	PSIL_CSI2RX(0x5009),
	PSIL_CSI2RX(0x500a),
	PSIL_CSI2RX(0x500b),
	PSIL_CSI2RX(0x500c),
	PSIL_CSI2RX(0x500d),
	PSIL_CSI2RX(0x500e),
	PSIL_CSI2RX(0x500f),
	PSIL_CSI2RX(0x5010),
	PSIL_CSI2RX(0x5011),
	PSIL_CSI2RX(0x5012),
	PSIL_CSI2RX(0x5013),
	PSIL_CSI2RX(0x5014),
	PSIL_CSI2RX(0x5015),
	PSIL_CSI2RX(0x5016),
	PSIL_CSI2RX(0x5017),
	PSIL_CSI2RX(0x5018),
	PSIL_CSI2RX(0x5019),
	PSIL_CSI2RX(0x501a),
	PSIL_CSI2RX(0x501b),
	PSIL_CSI2RX(0x501c),
	PSIL_CSI2RX(0x501d),
	PSIL_CSI2RX(0x501e),
	PSIL_CSI2RX(0x501f),
	PSIL_CSI2RX(0x5000),
	PSIL_CSI2RX(0x5001),
	PSIL_CSI2RX(0x5002),
	PSIL_CSI2RX(0x5003),
	PSIL_CSI2RX(0x5004),
	PSIL_CSI2RX(0x5005),
	PSIL_CSI2RX(0x5006),
	PSIL_CSI2RX(0x5007),
	PSIL_CSI2RX(0x5008),
	PSIL_CSI2RX(0x5009),
	PSIL_CSI2RX(0x500a),
	PSIL_CSI2RX(0x500b),
	PSIL_CSI2RX(0x500c),
	PSIL_CSI2RX(0x500d),
	PSIL_CSI2RX(0x500e),
	PSIL_CSI2RX(0x500f),
	PSIL_CSI2RX(0x5010),
	PSIL_CSI2RX(0x5011),
	PSIL_CSI2RX(0x5012),
	PSIL_CSI2RX(0x5013),
	PSIL_CSI2RX(0x5014),
	PSIL_CSI2RX(0x5015),
	PSIL_CSI2RX(0x5016),
	PSIL_CSI2RX(0x5017),
	PSIL_CSI2RX(0x5018),
	PSIL_CSI2RX(0x5019),
	PSIL_CSI2RX(0x501a),
	PSIL_CSI2RX(0x501b),
	PSIL_CSI2RX(0x501c),
	PSIL_CSI2RX(0x501d),
	PSIL_CSI2RX(0x501e),
	PSIL_CSI2RX(0x501f),
	/* CSIRX 1-3 (only for J722S) */
	PSIL_CSI2RX(0x5100),
	PSIL_CSI2RX(0x5101),
	PSIL_CSI2RX(0x5102),
	PSIL_CSI2RX(0x5103),
	PSIL_CSI2RX(0x5104),
	PSIL_CSI2RX(0x5105),
	PSIL_CSI2RX(0x5106),
	PSIL_CSI2RX(0x5107),
	PSIL_CSI2RX(0x5108),
	PSIL_CSI2RX(0x5109),
	PSIL_CSI2RX(0x510a),
	PSIL_CSI2RX(0x510b),
	PSIL_CSI2RX(0x510c),
	PSIL_CSI2RX(0x510d),
	PSIL_CSI2RX(0x510e),
	PSIL_CSI2RX(0x510f),
	PSIL_CSI2RX(0x5110),
	PSIL_CSI2RX(0x5111),
	PSIL_CSI2RX(0x5112),
	PSIL_CSI2RX(0x5113),
	PSIL_CSI2RX(0x5114),
	PSIL_CSI2RX(0x5115),
	PSIL_CSI2RX(0x5116),
	PSIL_CSI2RX(0x5117),
	PSIL_CSI2RX(0x5118),
	PSIL_CSI2RX(0x5119),
	PSIL_CSI2RX(0x511a),
	PSIL_CSI2RX(0x511b),
	PSIL_CSI2RX(0x511c),
	PSIL_CSI2RX(0x511d),
	PSIL_CSI2RX(0x511e),
	PSIL_CSI2RX(0x511f),
	PSIL_CSI2RX(0x5200),
	PSIL_CSI2RX(0x5201),
	PSIL_CSI2RX(0x5202),
	PSIL_CSI2RX(0x5203),
	PSIL_CSI2RX(0x5204),
	PSIL_CSI2RX(0x5205),
	PSIL_CSI2RX(0x5206),
	PSIL_CSI2RX(0x5207),
	PSIL_CSI2RX(0x5208),
	PSIL_CSI2RX(0x5209),
	PSIL_CSI2RX(0x520a),
	PSIL_CSI2RX(0x520b),
	PSIL_CSI2RX(0x520c),
	PSIL_CSI2RX(0x520d),
	PSIL_CSI2RX(0x520e),
	PSIL_CSI2RX(0x520f),
	PSIL_CSI2RX(0x5210),
	PSIL_CSI2RX(0x5211),
	PSIL_CSI2RX(0x5212),
	PSIL_CSI2RX(0x5213),
	PSIL_CSI2RX(0x5214),
	PSIL_CSI2RX(0x5215),
	PSIL_CSI2RX(0x5216),
	PSIL_CSI2RX(0x5217),
	PSIL_CSI2RX(0x5218),
	PSIL_CSI2RX(0x5219),
	PSIL_CSI2RX(0x521a),
	PSIL_CSI2RX(0x521b),
	PSIL_CSI2RX(0x521c),
	PSIL_CSI2RX(0x521d),
	PSIL_CSI2RX(0x521e),
	PSIL_CSI2RX(0x521f),
	PSIL_CSI2RX(0x5300),
	PSIL_CSI2RX(0x5301),
	PSIL_CSI2RX(0x5302),
	PSIL_CSI2RX(0x5303),
	PSIL_CSI2RX(0x5304),
	PSIL_CSI2RX(0x5305),
	PSIL_CSI2RX(0x5306),
	PSIL_CSI2RX(0x5307),
	PSIL_CSI2RX(0x5308),
	PSIL_CSI2RX(0x5309),
	PSIL_CSI2RX(0x530a),
	PSIL_CSI2RX(0x530b),
	PSIL_CSI2RX(0x530c),
	PSIL_CSI2RX(0x530d),
	PSIL_CSI2RX(0x530e),
	PSIL_CSI2RX(0x530f),
	PSIL_CSI2RX(0x5310),
	PSIL_CSI2RX(0x5311),
	PSIL_CSI2RX(0x5312),
	PSIL_CSI2RX(0x5313),
	PSIL_CSI2RX(0x5314),
	PSIL_CSI2RX(0x5315),
	PSIL_CSI2RX(0x5316),
	PSIL_CSI2RX(0x5317),
	PSIL_CSI2RX(0x5318),
	PSIL_CSI2RX(0x5319),
	PSIL_CSI2RX(0x531a),
	PSIL_CSI2RX(0x531b),
	PSIL_CSI2RX(0x531c),
	PSIL_CSI2RX(0x531d),
	PSIL_CSI2RX(0x531e),
	PSIL_CSI2RX(0x531f),
};

/* PSI-L destination thread IDs, used for TX (DMA_MEM_TO_DEV) */
static struct psil_ep am62p_dst_ep_map[] = {
	/* SAUL */
	PSIL_SAUL(0xf500, 27, 83, 8, 83, 1),
	PSIL_SAUL(0xf501, 28, 91, 8, 91, 1),
	/* PDMA_MAIN0 - SPI0-2 */
	PSIL_PDMA_XY_PKT(0xc300),
	PSIL_PDMA_XY_PKT(0xc301),
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

struct psil_ep_map am62p_ep_map = {
	.name = "am62p",
	.src = am62p_src_ep_map,
	.src_count = ARRAY_SIZE(am62p_src_ep_map),
	.dst = am62p_dst_ep_map,
	.dst_count = ARRAY_SIZE(am62p_dst_ep_map),
};
