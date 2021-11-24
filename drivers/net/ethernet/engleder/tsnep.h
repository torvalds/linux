/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

#ifndef _TSNEP_H
#define _TSNEP_H

#include "tsnep_hw.h"

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>
#include <linux/ethtool.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/miscdevice.h>

#define TSNEP "tsnep"

#define TSNEP_RING_SIZE 256
#define TSNEP_RING_ENTRIES_PER_PAGE (PAGE_SIZE / TSNEP_DESC_SIZE)
#define TSNEP_RING_PAGE_COUNT (TSNEP_RING_SIZE / TSNEP_RING_ENTRIES_PER_PAGE)

#define TSNEP_QUEUES 1

struct tsnep_gcl {
	void __iomem *addr;

	u64 base_time;
	u64 cycle_time;
	u64 cycle_time_extension;

	struct tsnep_gcl_operation operation[TSNEP_GCL_COUNT];
	int count;

	u64 change_limit;

	u64 start_time;
	bool change;
};

struct tsnep_tx_entry {
	struct tsnep_tx_desc *desc;
	struct tsnep_tx_desc_wb *desc_wb;
	dma_addr_t desc_dma;
	bool owner_user_flag;

	u32 properties;

	struct sk_buff *skb;
	size_t len;
	DEFINE_DMA_UNMAP_ADDR(dma);
};

struct tsnep_tx {
	struct tsnep_adapter *adapter;
	void __iomem *addr;

	void *page[TSNEP_RING_PAGE_COUNT];
	dma_addr_t page_dma[TSNEP_RING_PAGE_COUNT];

	/* TX ring lock */
	spinlock_t lock;
	struct tsnep_tx_entry entry[TSNEP_RING_SIZE];
	int write;
	int read;
	u32 owner_counter;
	int increment_owner_counter;

	u32 packets;
	u32 bytes;
	u32 dropped;
};

struct tsnep_rx_entry {
	struct tsnep_rx_desc *desc;
	struct tsnep_rx_desc_wb *desc_wb;
	dma_addr_t desc_dma;

	u32 properties;

	struct sk_buff *skb;
	size_t len;
	DEFINE_DMA_UNMAP_ADDR(dma);
};

struct tsnep_rx {
	struct tsnep_adapter *adapter;
	void __iomem *addr;

	void *page[TSNEP_RING_PAGE_COUNT];
	dma_addr_t page_dma[TSNEP_RING_PAGE_COUNT];

	struct tsnep_rx_entry entry[TSNEP_RING_SIZE];
	int read;
	u32 owner_counter;
	int increment_owner_counter;

	u32 packets;
	u32 bytes;
	u32 dropped;
	u32 multicast;
};

struct tsnep_queue {
	struct tsnep_adapter *adapter;

	struct tsnep_tx *tx;
	struct tsnep_rx *rx;

	struct napi_struct napi;

	u32 irq_mask;
};

struct tsnep_adapter {
	struct net_device *netdev;
	u8 mac_address[ETH_ALEN];
	struct mii_bus *mdiobus;
	bool suppress_preamble;
	phy_interface_t phy_mode;
	struct phy_device *phydev;
	int msg_enable;

	struct platform_device *pdev;
	struct device *dmadev;
	void __iomem *addr;
	int irq;

	bool gate_control;
	/* gate control lock */
	struct mutex gate_control_lock;
	bool gate_control_active;
	struct tsnep_gcl gcl[2];
	int next_gcl;

	struct hwtstamp_config hwtstamp_config;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	/* ptp clock lock */
	spinlock_t ptp_lock;

	int num_tx_queues;
	struct tsnep_tx tx[TSNEP_MAX_QUEUES];
	int num_rx_queues;
	struct tsnep_rx rx[TSNEP_MAX_QUEUES];

	int num_queues;
	struct tsnep_queue queue[TSNEP_MAX_QUEUES];
};

extern const struct ethtool_ops tsnep_ethtool_ops;

int tsnep_ptp_init(struct tsnep_adapter *adapter);
void tsnep_ptp_cleanup(struct tsnep_adapter *adapter);
int tsnep_ptp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);

int tsnep_tc_init(struct tsnep_adapter *adapter);
void tsnep_tc_cleanup(struct tsnep_adapter *adapter);
int tsnep_tc_setup(struct net_device *netdev, enum tc_setup_type type,
		   void *type_data);

#if IS_ENABLED(CONFIG_TSNEP_SELFTESTS)
int tsnep_ethtool_get_test_count(void);
void tsnep_ethtool_get_test_strings(u8 *data);
void tsnep_ethtool_self_test(struct net_device *netdev,
			     struct ethtool_test *eth_test, u64 *data);
#else
static inline int tsnep_ethtool_get_test_count(void)
{
	return -EOPNOTSUPP;
}

static inline void tsnep_ethtool_get_test_strings(u8 *data)
{
	/* not enabled */
}

static inline void tsnep_ethtool_self_test(struct net_device *dev,
					   struct ethtool_test *eth_test,
					   u64 *data)
{
	/* not enabled */
}
#endif /* CONFIG_TSNEP_SELFTESTS */

void tsnep_get_system_time(struct tsnep_adapter *adapter, u64 *time);

#endif /* _TSNEP_H */
