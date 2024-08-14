/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) Tehuti Networks Ltd. */

#ifndef _TN40_H_
#define _TN40_H_

#include "tn40_regs.h"

#define TN40_DRV_NAME "tn40xx"

#define TN40_MDIO_SPEED_1MHZ (1)
#define TN40_MDIO_SPEED_6MHZ (6)

/* netdev tx queue len for Luxor. The default value is 1000.
 * ifconfig eth1 txqueuelen 3000 - to change it at runtime.
 */
#define TN40_NDEV_TXQ_LEN 1000

#define TN40_FIFO_SIZE 4096
#define TN40_FIFO_EXTRA_SPACE 1024

#define TN40_TXF_DESC_SZ 16
#define TN40_MAX_TX_LEVEL (priv->txd_fifo0.m.memsz - 16)
#define TN40_MIN_TX_LEVEL 256
#define TN40_NO_UPD_PACKETS 40
#define TN40_MAX_MTU BIT(14)

#define TN40_PCK_TH_MULT 128
#define TN40_INT_COAL_MULT 2

#define TN40_INT_REG_VAL(coal, coal_rc, rxf_th, pck_th) (	\
	FIELD_PREP(GENMASK(14, 0), (coal)) |		\
	FIELD_PREP(BIT(15), (coal_rc)) |		\
	FIELD_PREP(GENMASK(19, 16), (rxf_th)) |		\
	FIELD_PREP(GENMASK(31, 20), (pck_th))		\
	)

struct tn40_fifo {
	dma_addr_t da; /* Physical address of fifo (used by HW) */
	char *va; /* Virtual address of fifo (used by SW) */
	u32 rptr, wptr;
	 /* Cached values of RPTR and WPTR registers,
	  * they're 32 bits on both 32 and 64 archs.
	  */
	u16 reg_cfg0;
	u16 reg_cfg1;
	u16 reg_rptr;
	u16 reg_wptr;
	u16 memsz; /* Memory size allocated for fifo */
	u16 size_mask;
	u16 pktsz; /* Skb packet size to allocate */
	u16 rcvno; /* Number of buffers that come from this RXF */
};

struct tn40_txf_fifo {
	struct tn40_fifo m; /* The minimal set of variables used by all fifos */
};

struct tn40_txd_fifo {
	struct tn40_fifo m; /* The minimal set of variables used by all fifos */
};

struct tn40_rxf_fifo {
	struct tn40_fifo m; /* The minimal set of variables used by all fifos */
};

struct tn40_rxd_fifo {
	struct tn40_fifo m; /* The minimal set of variables used by all fifos */
};

struct tn40_rx_map {
	struct page *page;
};

struct tn40_rxdb {
	unsigned int *stack;
	struct tn40_rx_map *elems;
	unsigned int nelem;
	unsigned int top;
};

union tn40_tx_dma_addr {
	dma_addr_t dma;
	struct sk_buff *skb;
};

/* Entry in the db.
 * if len == 0 addr is dma
 * if len != 0 addr is skb
 */
struct tn40_tx_map {
	union tn40_tx_dma_addr addr;
	int len;
};

/* tx database - implemented as circular fifo buffer */
struct tn40_txdb {
	struct tn40_tx_map *start; /* Points to the first element */
	struct tn40_tx_map *end; /* Points just AFTER the last element */
	struct tn40_tx_map *rptr; /* Points to the next element to read */
	struct tn40_tx_map *wptr; /* Points to the next element to write */
	int size; /* Number of elements in the db */
};

struct tn40_priv {
	struct net_device *ndev;
	struct pci_dev *pdev;

	struct napi_struct napi;
	/* RX FIFOs: 1 for data (full) descs, and 2 for free descs */
	struct tn40_rxd_fifo rxd_fifo0;
	struct tn40_rxf_fifo rxf_fifo0;
	struct tn40_rxdb *rxdb0; /* Rx dbs to store skb pointers */
	struct page_pool *page_pool;

	/* Tx FIFOs: 1 for data desc, 1 for empty (acks) desc */
	struct tn40_txd_fifo txd_fifo0;
	struct tn40_txf_fifo txf_fifo0;
	struct tn40_txdb txdb;
	int tx_level;
	int tx_update_mark;
	int tx_noupd;

	int stats_flag;
	struct rtnl_link_stats64 stats;
	u64 alloc_fail;
	struct u64_stats_sync syncp;

	u8 txd_size;
	u8 txf_size;
	u8 rxd_size;
	u8 rxf_size;
	u32 rdintcm;
	u32 tdintcm;

	u32 isr_mask;

	void __iomem *regs;

	/* SHORT_PKT_FIX */
	u32 b0_len;
	dma_addr_t b0_dma; /* Physical address of buffer */
	char *b0_va; /* Virtual address of buffer */

	struct mii_bus *mdio;
	struct phy_device *phydev;
	struct phylink *phylink;
	struct phylink_config phylink_config;
};

/* RX FREE descriptor - 64bit */
struct tn40_rxf_desc {
	__le32 info; /* Buffer Count + Info - described below */
	__le32 va_lo; /* VAdr[31:0] */
	__le32 va_hi; /* VAdr[63:32] */
	__le32 pa_lo; /* PAdr[31:0] */
	__le32 pa_hi; /* PAdr[63:32] */
	__le32 len; /* Buffer Length */
};

#define TN40_GET_RXD_BC(x) FIELD_GET(GENMASK(4, 0), (x))
#define TN40_GET_RXD_ERR(x) FIELD_GET(GENMASK(26, 21), (x))
#define TN40_GET_RXD_PKT_ID(x) FIELD_GET(GENMASK(30, 28), (x))
#define TN40_GET_RXD_VTAG(x) FIELD_GET(BIT(31), (x))
#define TN40_GET_RXD_VLAN_TCI(x) FIELD_GET(GENMASK(15, 0), (x))

struct tn40_rxd_desc {
	__le32 rxd_val1;
	__le16 len;
	__le16 rxd_vlan;
	__le32 va_lo;
	__le32 va_hi;
	__le32 rss_lo;
	__le32 rss_hash;
};

#define TN40_MAX_PBL (19)
/* PBL describes each virtual buffer to be transmitted from the host. */
struct tn40_pbl {
	__le32 pa_lo;
	__le32 pa_hi;
	__le32 len;
};

/* First word for TXD descriptor. It means: type = 3 for regular Tx packet,
 * hw_csum = 7 for IP+UDP+TCP HW checksums.
 */
#define TN40_TXD_W1_VAL(bc, checksum, vtag, lgsnd, vlan_id) (		\
	GENMASK(17, 16) |						\
	FIELD_PREP(GENMASK(4, 0), (bc)) |				\
	FIELD_PREP(GENMASK(7, 5), (checksum)) |				\
	FIELD_PREP(BIT(8), (vtag)) |					\
	FIELD_PREP(GENMASK(12, 9), (lgsnd)) |				\
	FIELD_PREP(GENMASK(15, 13),					\
		   FIELD_GET(GENMASK(15, 13), (vlan_id))) |		\
	FIELD_PREP(GENMASK(31, 20),					\
		   FIELD_GET(GENMASK(11, 0), (vlan_id)))		\
	)

struct tn40_txd_desc {
	__le32 txd_val1;
	__le16 mss;
	__le16 length;
	__le32 va_lo;
	__le32 va_hi;
	struct tn40_pbl pbl[]; /* Fragments */
};

struct tn40_txf_desc {
	u32 status;
	u32 va_lo; /* VAdr[31:0] */
	u32 va_hi; /* VAdr[63:32] */
	u32 pad;
};

static inline u32 tn40_read_reg(struct tn40_priv *priv, u32 reg)
{
	return readl(priv->regs + reg);
}

static inline void tn40_write_reg(struct tn40_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->regs + reg);
}

int tn40_set_link_speed(struct tn40_priv *priv, u32 speed);

int tn40_mdiobus_init(struct tn40_priv *priv);

int tn40_phy_register(struct tn40_priv *priv);
void tn40_phy_unregister(struct tn40_priv *priv);

#endif /* _TN40XX_H */
