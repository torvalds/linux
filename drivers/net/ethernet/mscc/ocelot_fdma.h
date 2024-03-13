/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi SoCs FDMA driver
 *
 * Copyright (c) 2021 Microchip
 */
#ifndef _MSCC_OCELOT_FDMA_H_
#define _MSCC_OCELOT_FDMA_H_

#include "ocelot.h"

#define MSCC_FDMA_DCB_STAT_BLOCKO(x)	(((x) << 20) & GENMASK(31, 20))
#define MSCC_FDMA_DCB_STAT_BLOCKO_M	GENMASK(31, 20)
#define MSCC_FDMA_DCB_STAT_BLOCKO_X(x)	(((x) & GENMASK(31, 20)) >> 20)
#define MSCC_FDMA_DCB_STAT_PD		BIT(19)
#define MSCC_FDMA_DCB_STAT_ABORT	BIT(18)
#define MSCC_FDMA_DCB_STAT_EOF		BIT(17)
#define MSCC_FDMA_DCB_STAT_SOF		BIT(16)
#define MSCC_FDMA_DCB_STAT_BLOCKL_M	GENMASK(15, 0)
#define MSCC_FDMA_DCB_STAT_BLOCKL(x)	((x) & GENMASK(15, 0))

#define MSCC_FDMA_DCB_LLP(x)		((x) * 4 + 0x0)
#define MSCC_FDMA_DCB_LLP_PREV(x)	((x) * 4 + 0xA0)
#define MSCC_FDMA_CH_SAFE		0xcc
#define MSCC_FDMA_CH_ACTIVATE		0xd0
#define MSCC_FDMA_CH_DISABLE		0xd4
#define MSCC_FDMA_CH_FORCEDIS		0xd8
#define MSCC_FDMA_EVT_ERR		0x164
#define MSCC_FDMA_EVT_ERR_CODE		0x168
#define MSCC_FDMA_INTR_LLP		0x16c
#define MSCC_FDMA_INTR_LLP_ENA		0x170
#define MSCC_FDMA_INTR_FRM		0x174
#define MSCC_FDMA_INTR_FRM_ENA		0x178
#define MSCC_FDMA_INTR_ENA		0x184
#define MSCC_FDMA_INTR_IDENT		0x188

#define MSCC_FDMA_INJ_CHAN		2
#define MSCC_FDMA_XTR_CHAN		0

#define OCELOT_FDMA_WEIGHT		32

#define OCELOT_FDMA_CH_SAFE_TIMEOUT_US	10

#define OCELOT_FDMA_RX_RING_SIZE	512
#define OCELOT_FDMA_TX_RING_SIZE	128

#define OCELOT_FDMA_RX_DCB_SIZE		(OCELOT_FDMA_RX_RING_SIZE * \
					 sizeof(struct ocelot_fdma_dcb))
#define OCELOT_FDMA_TX_DCB_SIZE		(OCELOT_FDMA_TX_RING_SIZE * \
					 sizeof(struct ocelot_fdma_dcb))
/* +4 allows for word alignment after allocation */
#define OCELOT_DCBS_HW_ALLOC_SIZE	(OCELOT_FDMA_RX_DCB_SIZE + \
					 OCELOT_FDMA_TX_DCB_SIZE + \
					 4)

#define OCELOT_FDMA_RX_SIZE		(PAGE_SIZE / 2)

#define OCELOT_FDMA_SKBFRAG_OVR		(4 + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#define OCELOT_FDMA_RXB_SIZE		ALIGN_DOWN(OCELOT_FDMA_RX_SIZE - OCELOT_FDMA_SKBFRAG_OVR, 4)
#define OCELOT_FDMA_SKBFRAG_SIZE	(OCELOT_FDMA_RXB_SIZE + OCELOT_FDMA_SKBFRAG_OVR)

DECLARE_STATIC_KEY_FALSE(ocelot_fdma_enabled);

struct ocelot_fdma_dcb {
	u32 llp;
	u32 datap;
	u32 datal;
	u32 stat;
} __packed;

/**
 * struct ocelot_fdma_tx_buf - TX buffer structure
 * @skb: SKB currently used in the corresponding DCB.
 * @dma_addr: SKB DMA mapped address.
 */
struct ocelot_fdma_tx_buf {
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(dma_addr);
};

/**
 * struct ocelot_fdma_tx_ring - TX ring description of DCBs
 *
 * @dcbs: DCBs allocated for the ring
 * @dcbs_dma: DMA base address of the DCBs
 * @bufs: List of TX buffer associated to the DCBs
 * @xmit_lock: lock for concurrent xmit access
 * @next_to_clean: Next DCB to be cleaned in tx_cleanup
 * @next_to_use: Next available DCB to send SKB
 */
struct ocelot_fdma_tx_ring {
	struct ocelot_fdma_dcb *dcbs;
	dma_addr_t dcbs_dma;
	struct ocelot_fdma_tx_buf bufs[OCELOT_FDMA_TX_RING_SIZE];
	/* Protect concurrent xmit calls */
	spinlock_t xmit_lock;
	u16 next_to_clean;
	u16 next_to_use;
};

/**
 * struct ocelot_fdma_rx_buf - RX buffer structure
 * @page: Struct page used in this buffer
 * @page_offset: Current page offset (either 0 or PAGE_SIZE/2)
 * @dma_addr: DMA address of the page
 */
struct ocelot_fdma_rx_buf {
	struct page *page;
	u32 page_offset;
	dma_addr_t dma_addr;
};

/**
 * struct ocelot_fdma_rx_ring - TX ring description of DCBs
 *
 * @dcbs: DCBs allocated for the ring
 * @dcbs_dma: DMA base address of the DCBs
 * @bufs: List of RX buffer associated to the DCBs
 * @skb: SKB currently received by the netdev
 * @next_to_clean: Next DCB to be cleaned NAPI polling
 * @next_to_use: Next available DCB to send SKB
 * @next_to_alloc: Next buffer that needs to be allocated (page reuse or alloc)
 */
struct ocelot_fdma_rx_ring {
	struct ocelot_fdma_dcb *dcbs;
	dma_addr_t dcbs_dma;
	struct ocelot_fdma_rx_buf bufs[OCELOT_FDMA_RX_RING_SIZE];
	struct sk_buff *skb;
	u16 next_to_clean;
	u16 next_to_use;
	u16 next_to_alloc;
};

/**
 * struct ocelot_fdma - FDMA context
 *
 * @irq: FDMA interrupt
 * @ndev: Net device used to initialize NAPI
 * @dcbs_base: Memory coherent DCBs
 * @dcbs_dma_base: DMA base address of memory coherent DCBs
 * @tx_ring: Injection ring
 * @rx_ring: Extraction ring
 * @napi: NAPI context
 * @ocelot: Back-pointer to ocelot struct
 */
struct ocelot_fdma {
	int irq;
	struct net_device *ndev;
	struct ocelot_fdma_dcb *dcbs_base;
	dma_addr_t dcbs_dma_base;
	struct ocelot_fdma_tx_ring tx_ring;
	struct ocelot_fdma_rx_ring rx_ring;
	struct napi_struct napi;
	struct ocelot *ocelot;
};

void ocelot_fdma_init(struct platform_device *pdev, struct ocelot *ocelot);
void ocelot_fdma_start(struct ocelot *ocelot);
void ocelot_fdma_deinit(struct ocelot *ocelot);
int ocelot_fdma_inject_frame(struct ocelot *fdma, int port, u32 rew_op,
			     struct sk_buff *skb, struct net_device *dev);
void ocelot_fdma_netdev_init(struct ocelot *ocelot, struct net_device *dev);
void ocelot_fdma_netdev_deinit(struct ocelot *ocelot,
			       struct net_device *dev);

#endif
