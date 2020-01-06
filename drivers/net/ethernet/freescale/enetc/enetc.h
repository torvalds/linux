/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2017-2019 NXP */

#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/phy.h>

#include "enetc_hw.h"

#define ENETC_MAC_MAXFRM_SIZE	9600
#define ENETC_MAX_MTU		(ENETC_MAC_MAXFRM_SIZE - \
				(ETH_FCS_LEN + ETH_HLEN + VLAN_HLEN))

struct enetc_tx_swbd {
	struct sk_buff *skb;
	dma_addr_t dma;
	u16 len;
	u8 is_dma_page:1;
	u8 check_wb:1;
	u8 do_tstamp:1;
};

#define ENETC_RX_MAXFRM_SIZE	ENETC_MAC_MAXFRM_SIZE
#define ENETC_RXB_TRUESIZE	2048 /* PAGE_SIZE >> 1 */
#define ENETC_RXB_PAD		NET_SKB_PAD /* add extra space if needed */
#define ENETC_RXB_DMA_SIZE	\
	(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - ENETC_RXB_PAD)

struct enetc_rx_swbd {
	dma_addr_t dma;
	struct page *page;
	u16 page_offset;
};

struct enetc_ring_stats {
	unsigned int packets;
	unsigned int bytes;
	unsigned int rx_alloc_errs;
};

#define ENETC_BDR_DEFAULT_SIZE	1024
#define ENETC_DEFAULT_TX_WORK	256

struct enetc_bdr {
	struct device *dev; /* for DMA mapping */
	struct net_device *ndev;
	void *bd_base; /* points to Rx or Tx BD ring */
	union {
		void __iomem *tpir;
		void __iomem *rcir;
	};
	u16 index;
	int bd_count; /* # of BDs */
	int next_to_use;
	int next_to_clean;
	union {
		struct enetc_tx_swbd *tx_swbd;
		struct enetc_rx_swbd *rx_swbd;
	};
	union {
		void __iomem *tcir; /* Tx */
		int next_to_alloc; /* Rx */
	};
	void __iomem *idr; /* Interrupt Detect Register pointer */

	struct enetc_ring_stats stats;

	dma_addr_t bd_dma_base;
} ____cacheline_aligned_in_smp;

static inline void enetc_bdr_idx_inc(struct enetc_bdr *bdr, int *i)
{
	if (unlikely(++*i == bdr->bd_count))
		*i = 0;
}

static inline int enetc_bd_unused(struct enetc_bdr *bdr)
{
	if (bdr->next_to_clean > bdr->next_to_use)
		return bdr->next_to_clean - bdr->next_to_use - 1;

	return bdr->bd_count + bdr->next_to_clean - bdr->next_to_use - 1;
}

/* Control BD ring */
#define ENETC_CBDR_DEFAULT_SIZE	64
struct enetc_cbdr {
	void *bd_base; /* points to Rx or Tx BD ring */
	void __iomem *pir;
	void __iomem *cir;

	int bd_count; /* # of BDs */
	int next_to_use;
	int next_to_clean;

	dma_addr_t bd_dma_base;
};

#define ENETC_TXBD(BDR, i) (&(((union enetc_tx_bd *)((BDR).bd_base))[i]))
#define ENETC_RXBD(BDR, i) (&(((union enetc_rx_bd *)((BDR).bd_base))[i]))

struct enetc_msg_swbd {
	void *vaddr;
	dma_addr_t dma;
	int size;
};

#define ENETC_REV1	0x1
enum enetc_errata {
	ENETC_ERR_TXCSUM	= BIT(0),
	ENETC_ERR_VLAN_ISOL	= BIT(1),
	ENETC_ERR_UCMCSWP	= BIT(2),
};

#define ENETC_SI_F_QBV BIT(0)

/* PCI IEP device data */
struct enetc_si {
	struct pci_dev *pdev;
	struct enetc_hw hw;
	enum enetc_errata errata;

	struct net_device *ndev; /* back ref. */

	struct enetc_cbdr cbd_ring;

	int num_rx_rings; /* how many rings are available in the SI */
	int num_tx_rings;
	int num_fs_entries;
	int num_rss; /* number of RSS buckets */
	unsigned short pad;
	int hw_features;
};

#define ENETC_SI_ALIGN	32

static inline void *enetc_si_priv(const struct enetc_si *si)
{
	return (char *)si + ALIGN(sizeof(struct enetc_si), ENETC_SI_ALIGN);
}

static inline bool enetc_si_is_pf(struct enetc_si *si)
{
	return !!(si->hw.port);
}

#define ENETC_MAX_NUM_TXQS	8
#define ENETC_INT_NAME_MAX	(IFNAMSIZ + 8)

struct enetc_int_vector {
	void __iomem *rbier;
	void __iomem *tbier_base;
	unsigned long tx_rings_map;
	int count_tx_rings;
	struct napi_struct napi;
	char name[ENETC_INT_NAME_MAX];

	struct enetc_bdr rx_ring ____cacheline_aligned_in_smp;
	struct enetc_bdr tx_ring[0];
};

struct enetc_cls_rule {
	struct ethtool_rx_flow_spec fs;
	int used;
};

#define ENETC_MAX_BDR_INT	2 /* fixed to max # of available cpus */

/* TODO: more hardware offloads */
enum enetc_active_offloads {
	ENETC_F_RX_TSTAMP	= BIT(0),
	ENETC_F_TX_TSTAMP	= BIT(1),
	ENETC_F_QBV             = BIT(2),
};

struct enetc_ndev_priv {
	struct net_device *ndev;
	struct device *dev; /* dma-mapping device */
	struct enetc_si *si;

	int bdr_int_num; /* number of Rx/Tx ring interrupts */
	struct enetc_int_vector *int_vector[ENETC_MAX_BDR_INT];
	u16 num_rx_rings, num_tx_rings;
	u16 rx_bd_count, tx_bd_count;

	u16 msg_enable;
	int active_offloads;

	u32 speed; /* store speed for compare update pspeed */

	struct enetc_bdr *tx_ring[16];
	struct enetc_bdr *rx_ring[16];

	struct enetc_cls_rule *cls_rules;

	struct device_node *phy_node;
	phy_interface_t if_mode;
};

/* Messaging */

/* VF-PF set primary MAC address message format */
struct enetc_msg_cmd_set_primary_mac {
	struct enetc_msg_cmd_header header;
	struct sockaddr mac;
};

#define ENETC_CBD(R, i)	(&(((struct enetc_cbd *)((R).bd_base))[i]))

#define ENETC_CBDR_TIMEOUT	1000 /* usecs */

/* PTP driver exports */
extern int enetc_phc_index;

/* SI common */
int enetc_pci_probe(struct pci_dev *pdev, const char *name, int sizeof_priv);
void enetc_pci_remove(struct pci_dev *pdev);
int enetc_alloc_msix(struct enetc_ndev_priv *priv);
void enetc_free_msix(struct enetc_ndev_priv *priv);
void enetc_get_si_caps(struct enetc_si *si);
void enetc_init_si_rings_params(struct enetc_ndev_priv *priv);
int enetc_alloc_si_resources(struct enetc_ndev_priv *priv);
void enetc_free_si_resources(struct enetc_ndev_priv *priv);

int enetc_open(struct net_device *ndev);
int enetc_close(struct net_device *ndev);
netdev_tx_t enetc_xmit(struct sk_buff *skb, struct net_device *ndev);
struct net_device_stats *enetc_get_stats(struct net_device *ndev);
int enetc_set_features(struct net_device *ndev,
		       netdev_features_t features);
int enetc_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd);
int enetc_setup_tc(struct net_device *ndev, enum tc_setup_type type,
		   void *type_data);

/* ethtool */
void enetc_set_ethtool_ops(struct net_device *ndev);

/* control buffer descriptor ring (CBDR) */
int enetc_set_mac_flt_entry(struct enetc_si *si, int index,
			    char *mac_addr, int si_map);
int enetc_clear_mac_flt_entry(struct enetc_si *si, int index);
int enetc_set_fs_entry(struct enetc_si *si, struct enetc_cmd_rfse *rfse,
		       int index);
void enetc_set_rss_key(struct enetc_hw *hw, const u8 *bytes);
int enetc_get_rss_table(struct enetc_si *si, u32 *table, int count);
int enetc_set_rss_table(struct enetc_si *si, const u32 *table, int count);
int enetc_send_cmd(struct enetc_si *si, struct enetc_cbd *cbd);

#ifdef CONFIG_FSL_ENETC_QOS
int enetc_setup_tc_taprio(struct net_device *ndev, void *type_data);
void enetc_sched_speed_set(struct net_device *ndev);
int enetc_setup_tc_cbs(struct net_device *ndev, void *type_data);
#else
#define enetc_setup_tc_taprio(ndev, type_data) -EOPNOTSUPP
#define enetc_sched_speed_set(ndev) (void)0
#define enetc_setup_tc_cbs(ndev, type_data) -EOPNOTSUPP
#endif
