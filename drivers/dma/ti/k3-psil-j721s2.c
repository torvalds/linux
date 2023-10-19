// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2021 Texas Instruments Incorporated - https://www.ti.com
 */

#include <linux/kernel.h>

#include "k3-psil-priv.h"

#define PSIL_PDMA_XY_TR(x)				\
	{						\
		.thread_id = x,				\
		.ep_config = {				\
			.ep_type = PSIL_EP_PDMA_XY,	\
		},					\
	}

#define PSIL_PDMA_XY_PKT(x)				\
	{						\
		.thread_id = x,				\
		.ep_config = {				\
			.ep_type = PSIL_EP_PDMA_XY,	\
			.pkt_mode = 1,			\
		},					\
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

#define PSIL_ETHERNET(x)				\
	{						\
		.thread_id = x,				\
		.ep_config = {				\
			.ep_type = PSIL_EP_NATIVE,	\
			.pkt_mode = 1,			\
			.needs_epib = 1,		\
			.psd_size = 16,			\
		},					\
	}

#define PSIL_SA2UL(x, tx)				\
	{						\
		.thread_id = x,				\
		.ep_config = {				\
			.ep_type = PSIL_EP_NATIVE,	\
			.pkt_mode = 1,			\
			.needs_epib = 1,		\
			.psd_size = 64,			\
			.notdpkt = tx,			\
		},					\
	}

/* PSI-L source thread IDs, used for RX (DMA_DEV_TO_MEM) */
static struct psil_ep j721s2_src_ep_map[] = {
	/* PDMA_MCASP - McASP0-4 */
	PSIL_PDMA_MCASP(0x4400),
	PSIL_PDMA_MCASP(0x4401),
	PSIL_PDMA_MCASP(0x4402),
	PSIL_PDMA_MCASP(0x4403),
	PSIL_PDMA_MCASP(0x4404),
	/* PDMA_SPI_G0 - SPI0-3 */
	PSIL_PDMA_XY_PKT(0x4600),
	PSIL_PDMA_XY_PKT(0x4601),
	PSIL_PDMA_XY_PKT(0x4602),
	PSIL_PDMA_XY_PKT(0x4603),
	PSIL_PDMA_XY_PKT(0x4604),
	PSIL_PDMA_XY_PKT(0x4605),
	PSIL_PDMA_XY_PKT(0x4606),
	PSIL_PDMA_XY_PKT(0x4607),
	PSIL_PDMA_XY_PKT(0x4608),
	PSIL_PDMA_XY_PKT(0x4609),
	PSIL_PDMA_XY_PKT(0x460a),
	PSIL_PDMA_XY_PKT(0x460b),
	PSIL_PDMA_XY_PKT(0x460c),
	PSIL_PDMA_XY_PKT(0x460d),
	PSIL_PDMA_XY_PKT(0x460e),
	PSIL_PDMA_XY_PKT(0x460f),
	/* PDMA_SPI_G1 - SPI4-7 */
	PSIL_PDMA_XY_PKT(0x4610),
	PSIL_PDMA_XY_PKT(0x4611),
	PSIL_PDMA_XY_PKT(0x4612),
	PSIL_PDMA_XY_PKT(0x4613),
	PSIL_PDMA_XY_PKT(0x4614),
	PSIL_PDMA_XY_PKT(0x4615),
	PSIL_PDMA_XY_PKT(0x4616),
	PSIL_PDMA_XY_PKT(0x4617),
	PSIL_PDMA_XY_PKT(0x4618),
	PSIL_PDMA_XY_PKT(0x4619),
	PSIL_PDMA_XY_PKT(0x461a),
	PSIL_PDMA_XY_PKT(0x461b),
	PSIL_PDMA_XY_PKT(0x461c),
	PSIL_PDMA_XY_PKT(0x461d),
	PSIL_PDMA_XY_PKT(0x461e),
	PSIL_PDMA_XY_PKT(0x461f),
	/* PDMA_USART_G0 - UART0-1 */
	PSIL_PDMA_XY_PKT(0x4700),
	PSIL_PDMA_XY_PKT(0x4701),
	/* PDMA_USART_G1 - UART2-3 */
	PSIL_PDMA_XY_PKT(0x4702),
	PSIL_PDMA_XY_PKT(0x4703),
	/* PDMA_USART_G2 - UART4-9 */
	PSIL_PDMA_XY_PKT(0x4704),
	PSIL_PDMA_XY_PKT(0x4705),
	PSIL_PDMA_XY_PKT(0x4706),
	PSIL_PDMA_XY_PKT(0x4707),
	PSIL_PDMA_XY_PKT(0x4708),
	PSIL_PDMA_XY_PKT(0x4709),
	/* MAIN SA2UL */
	PSIL_SA2UL(0x4a40, 0),
	PSIL_SA2UL(0x4a41, 0),
	PSIL_SA2UL(0x4a42, 0),
	PSIL_SA2UL(0x4a43, 0),
	/* CPSW0 */
	PSIL_ETHERNET(0x7000),
	/* MCU_PDMA0 (MCU_PDMA_MISC_G0) - SPI0 */
	PSIL_PDMA_XY_PKT(0x7100),
	PSIL_PDMA_XY_PKT(0x7101),
	PSIL_PDMA_XY_PKT(0x7102),
	PSIL_PDMA_XY_PKT(0x7103),
	/* MCU_PDMA1 (MCU_PDMA_MISC_G1) - SPI1-2 */
	PSIL_PDMA_XY_PKT(0x7200),
	PSIL_PDMA_XY_PKT(0x7201),
	PSIL_PDMA_XY_PKT(0x7202),
	PSIL_PDMA_XY_PKT(0x7203),
	PSIL_PDMA_XY_PKT(0x7204),
	PSIL_PDMA_XY_PKT(0x7205),
	PSIL_PDMA_XY_PKT(0x7206),
	PSIL_PDMA_XY_PKT(0x7207),
	/* MCU_PDMA2 (MCU_PDMA_MISC_G2) - UART0 */
	PSIL_PDMA_XY_PKT(0x7300),
	/* MCU_PDMA_ADC - ADC0-1 */
	PSIL_PDMA_XY_TR(0x7400),
	PSIL_PDMA_XY_TR(0x7401),
	PSIL_PDMA_XY_TR(0x7402),
	PSIL_PDMA_XY_TR(0x7403),
	/* SA2UL */
	PSIL_SA2UL(0x7500, 0),
	PSIL_SA2UL(0x7501, 0),
	PSIL_SA2UL(0x7502, 0),
	PSIL_SA2UL(0x7503, 0),
};

/* PSI-L destination thread IDs, used for TX (DMA_MEM_TO_DEV) */
static struct psil_ep j721s2_dst_ep_map[] = {
	/* MAIN SA2UL */
	PSIL_SA2UL(0xca40, 1),
	PSIL_SA2UL(0xca41, 1),
	/* CPSW0 */
	PSIL_ETHERNET(0xf000),
	PSIL_ETHERNET(0xf001),
	PSIL_ETHERNET(0xf002),
	PSIL_ETHERNET(0xf003),
	PSIL_ETHERNET(0xf004),
	PSIL_ETHERNET(0xf005),
	PSIL_ETHERNET(0xf006),
	PSIL_ETHERNET(0xf007),
	/* SA2UL */
	PSIL_SA2UL(0xf500, 1),
	PSIL_SA2UL(0xf501, 1),
};

struct psil_ep_map j721s2_ep_map = {
	.name = "j721s2",
	.src = j721s2_src_ep_map,
	.src_count = ARRAY_SIZE(j721s2_src_ep_map),
	.dst = j721s2_dst_ep_map,
	.dst_count = ARRAY_SIZE(j721s2_dst_ep_map),
};
