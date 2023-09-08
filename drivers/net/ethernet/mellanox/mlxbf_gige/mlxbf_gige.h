/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */

/* Header file for Gigabit Ethernet driver for Mellanox BlueField SoC
 * - this file contains software data structures and any chip-specific
 *   data structures (e.g. TX WQE format) that are memory resident.
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#ifndef __MLXBF_GIGE_H__
#define __MLXBF_GIGE_H__

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/irqreturn.h>
#include <linux/netdevice.h>
#include <linux/irq.h>
#include <linux/phy.h>

/* The silicon design supports a maximum RX ring size of
 * 32K entries. Based on current testing this maximum size
 * is not required to be supported.  Instead the RX ring
 * will be capped at a realistic value of 1024 entries.
 */
#define MLXBF_GIGE_MIN_RXQ_SZ     32
#define MLXBF_GIGE_MAX_RXQ_SZ     1024
#define MLXBF_GIGE_DEFAULT_RXQ_SZ 128

#define MLXBF_GIGE_MIN_TXQ_SZ     4
#define MLXBF_GIGE_MAX_TXQ_SZ     256
#define MLXBF_GIGE_DEFAULT_TXQ_SZ 128

#define MLXBF_GIGE_DEFAULT_BUF_SZ 2048

#define MLXBF_GIGE_DMA_PAGE_SZ    4096
#define MLXBF_GIGE_DMA_PAGE_SHIFT 12

/* There are four individual MAC RX filters. Currently
 * two of them are being used: one for the broadcast MAC
 * (index 0) and one for local MAC (index 1)
 */
#define MLXBF_GIGE_BCAST_MAC_FILTER_IDX 0
#define MLXBF_GIGE_LOCAL_MAC_FILTER_IDX 1

/* Define for broadcast MAC literal */
#define BCAST_MAC_ADDR 0xFFFFFFFFFFFF

/* There are three individual interrupts:
 *   1) Errors, "OOB" interrupt line
 *   2) Receive Packet, "OOB_LLU" interrupt line
 *   3) LLU and PLU Events, "OOB_PLU" interrupt line
 */
#define MLXBF_GIGE_ERROR_INTR_IDX       0
#define MLXBF_GIGE_RECEIVE_PKT_INTR_IDX 1
#define MLXBF_GIGE_LLU_PLU_INTR_IDX     2

struct mlxbf_gige_stats {
	u64 hw_access_errors;
	u64 tx_invalid_checksums;
	u64 tx_small_frames;
	u64 tx_index_errors;
	u64 sw_config_errors;
	u64 sw_access_errors;
	u64 rx_truncate_errors;
	u64 rx_mac_errors;
	u64 rx_din_dropped_pkts;
	u64 tx_fifo_full;
	u64 rx_filter_passed_pkts;
	u64 rx_filter_discard_pkts;
};

struct mlxbf_gige_reg_param {
	u32 mask;
	u32 shift;
};

struct mlxbf_gige_mdio_gw {
	u32 gw_address;
	u32 read_data_address;
	struct mlxbf_gige_reg_param busy;
	struct mlxbf_gige_reg_param write_data;
	struct mlxbf_gige_reg_param read_data;
	struct mlxbf_gige_reg_param devad;
	struct mlxbf_gige_reg_param partad;
	struct mlxbf_gige_reg_param opcode;
	struct mlxbf_gige_reg_param st1;
};

struct mlxbf_gige_link_cfg {
	void (*set_phy_link_mode)(struct phy_device *phydev);
	void (*adjust_link)(struct net_device *netdev);
	phy_interface_t phy_mode;
};

struct mlxbf_gige {
	void __iomem *base;
	void __iomem *llu_base;
	void __iomem *plu_base;
	struct device *dev;
	struct net_device *netdev;
	struct platform_device *pdev;
	void __iomem *mdio_io;
	void __iomem *clk_io;
	struct mii_bus *mdiobus;
	spinlock_t lock;      /* for packet processing indices */
	u16 rx_q_entries;
	u16 tx_q_entries;
	u64 *tx_wqe_base;
	dma_addr_t tx_wqe_base_dma;
	u64 *tx_wqe_next;
	u64 *tx_cc;
	dma_addr_t tx_cc_dma;
	dma_addr_t *rx_wqe_base;
	dma_addr_t rx_wqe_base_dma;
	u64 *rx_cqe_base;
	dma_addr_t rx_cqe_base_dma;
	u16 tx_pi;
	u16 prev_tx_ci;
	struct sk_buff *rx_skb[MLXBF_GIGE_MAX_RXQ_SZ];
	struct sk_buff *tx_skb[MLXBF_GIGE_MAX_TXQ_SZ];
	int error_irq;
	int rx_irq;
	int llu_plu_irq;
	int phy_irq;
	int hw_phy_irq;
	bool promisc_enabled;
	u8 valid_polarity;
	struct napi_struct napi;
	struct mlxbf_gige_stats stats;
	u8 hw_version;
	struct mlxbf_gige_mdio_gw *mdio_gw;
	int prev_speed;
};

/* Rx Work Queue Element definitions */
#define MLXBF_GIGE_RX_WQE_SZ                   8

/* Rx Completion Queue Element definitions */
#define MLXBF_GIGE_RX_CQE_SZ                   8
#define MLXBF_GIGE_RX_CQE_PKT_LEN_MASK         GENMASK(10, 0)
#define MLXBF_GIGE_RX_CQE_VALID_MASK           GENMASK(11, 11)
#define MLXBF_GIGE_RX_CQE_PKT_STATUS_MASK      GENMASK(15, 12)
#define MLXBF_GIGE_RX_CQE_PKT_STATUS_MAC_ERR   GENMASK(12, 12)
#define MLXBF_GIGE_RX_CQE_PKT_STATUS_TRUNCATED GENMASK(13, 13)
#define MLXBF_GIGE_RX_CQE_CHKSUM_MASK          GENMASK(31, 16)

/* Tx Work Queue Element definitions */
#define MLXBF_GIGE_TX_WQE_SZ_QWORDS            2
#define MLXBF_GIGE_TX_WQE_SZ                   16
#define MLXBF_GIGE_TX_WQE_PKT_LEN_MASK         GENMASK(10, 0)
#define MLXBF_GIGE_TX_WQE_UPDATE_MASK          GENMASK(31, 31)
#define MLXBF_GIGE_TX_WQE_CHKSUM_LEN_MASK      GENMASK(42, 32)
#define MLXBF_GIGE_TX_WQE_CHKSUM_START_MASK    GENMASK(55, 48)
#define MLXBF_GIGE_TX_WQE_CHKSUM_OFFSET_MASK   GENMASK(63, 56)

/* Macro to return packet length of specified TX WQE */
#define MLXBF_GIGE_TX_WQE_PKT_LEN(tx_wqe_addr) \
	(*((tx_wqe_addr) + 1) & MLXBF_GIGE_TX_WQE_PKT_LEN_MASK)

/* Tx Completion Count */
#define MLXBF_GIGE_TX_CC_SZ                    8

/* List of resources in ACPI table */
enum mlxbf_gige_res {
	MLXBF_GIGE_RES_MAC,
	MLXBF_GIGE_RES_MDIO9,
	MLXBF_GIGE_RES_GPIO0,
	MLXBF_GIGE_RES_LLU,
	MLXBF_GIGE_RES_PLU,
	MLXBF_GIGE_RES_CLK
};

/* Version of register data returned by mlxbf_gige_get_regs() */
#define MLXBF_GIGE_REGS_VERSION 1

int mlxbf_gige_mdio_probe(struct platform_device *pdev,
			  struct mlxbf_gige *priv);
void mlxbf_gige_mdio_remove(struct mlxbf_gige *priv);
irqreturn_t mlxbf_gige_mdio_handle_phy_interrupt(int irq, void *dev_id);
void mlxbf_gige_mdio_enable_phy_int(struct mlxbf_gige *priv);

void mlxbf_gige_set_mac_rx_filter(struct mlxbf_gige *priv,
				  unsigned int index, u64 dmac);
void mlxbf_gige_get_mac_rx_filter(struct mlxbf_gige *priv,
				  unsigned int index, u64 *dmac);
void mlxbf_gige_enable_promisc(struct mlxbf_gige *priv);
void mlxbf_gige_disable_promisc(struct mlxbf_gige *priv);
int mlxbf_gige_rx_init(struct mlxbf_gige *priv);
void mlxbf_gige_rx_deinit(struct mlxbf_gige *priv);
int mlxbf_gige_tx_init(struct mlxbf_gige *priv);
void mlxbf_gige_tx_deinit(struct mlxbf_gige *priv);
bool mlxbf_gige_handle_tx_complete(struct mlxbf_gige *priv);
netdev_tx_t mlxbf_gige_start_xmit(struct sk_buff *skb,
				  struct net_device *netdev);
struct sk_buff *mlxbf_gige_alloc_skb(struct mlxbf_gige *priv,
				     unsigned int map_len,
				     dma_addr_t *buf_dma,
				     enum dma_data_direction dir);
int mlxbf_gige_request_irqs(struct mlxbf_gige *priv);
void mlxbf_gige_free_irqs(struct mlxbf_gige *priv);
int mlxbf_gige_poll(struct napi_struct *napi, int budget);
extern const struct ethtool_ops mlxbf_gige_ethtool_ops;
void mlxbf_gige_update_tx_wqe_next(struct mlxbf_gige *priv);

#endif /* !defined(__MLXBF_GIGE_H__) */
