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
#include <linux/phylink.h>
#include <linux/dim.h>

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

#define ENETC_RX_RING_DEFAULT_SIZE	512
#define ENETC_TX_RING_DEFAULT_SIZE	256
#define ENETC_DEFAULT_TX_WORK		(ENETC_TX_RING_DEFAULT_SIZE / 2)

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
	u8 tsd_enable; /* Time specific departure */
	bool ext_en; /* enable h/w descriptor extensions */
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

static inline union enetc_rx_bd *enetc_rxbd(struct enetc_bdr *rx_ring, int i)
{
	int hw_idx = i;

#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
	if (rx_ring->ext_en)
		hw_idx = 2 * i;
#endif
	return &(((union enetc_rx_bd *)rx_ring->bd_base)[hw_idx]);
}

static inline union enetc_rx_bd *enetc_rxbd_next(struct enetc_bdr *rx_ring,
						 union enetc_rx_bd *rxbd,
						 int i)
{
	rxbd++;
#ifdef CONFIG_FSL_ENETC_PTP_CLOCK
	if (rx_ring->ext_en)
		rxbd++;
#endif
	if (unlikely(++i == rx_ring->bd_count))
		rxbd = rx_ring->bd_base;

	return rxbd;
}

static inline union enetc_rx_bd *enetc_rxbd_ext(union enetc_rx_bd *rxbd)
{
	return ++rxbd;
}

struct enetc_msg_swbd {
	void *vaddr;
	dma_addr_t dma;
	int size;
};

#define ENETC_REV1	0x1
enum enetc_errata {
	ENETC_ERR_VLAN_ISOL	= BIT(0),
	ENETC_ERR_UCMCSWP	= BIT(1),
};

#define ENETC_SI_F_QBV BIT(0)
#define ENETC_SI_F_PSFP BIT(1)

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
	void __iomem *ricr1;
	unsigned long tx_rings_map;
	int count_tx_rings;
	u32 rx_ictt;
	u16 comp_cnt;
	bool rx_dim_en, rx_napi_work;
	struct napi_struct napi ____cacheline_aligned_in_smp;
	struct dim rx_dim ____cacheline_aligned_in_smp;
	char name[ENETC_INT_NAME_MAX];

	struct enetc_bdr rx_ring;
	struct enetc_bdr tx_ring[];
} ____cacheline_aligned_in_smp;

struct enetc_cls_rule {
	struct ethtool_rx_flow_spec fs;
	int used;
};

#define ENETC_MAX_BDR_INT	2 /* fixed to max # of available cpus */
struct psfp_cap {
	u32 max_streamid;
	u32 max_psfp_filter;
	u32 max_psfp_gate;
	u32 max_psfp_gatelist;
	u32 max_psfp_meter;
};

/* TODO: more hardware offloads */
enum enetc_active_offloads {
	ENETC_F_RX_TSTAMP	= BIT(0),
	ENETC_F_TX_TSTAMP	= BIT(1),
	ENETC_F_QBV             = BIT(2),
	ENETC_F_QCI		= BIT(3),
};

/* interrupt coalescing modes */
enum enetc_ic_mode {
	/* one interrupt per frame */
	ENETC_IC_NONE = 0,
	/* activated when int coalescing time is set to a non-0 value */
	ENETC_IC_RX_MANUAL = BIT(0),
	ENETC_IC_TX_MANUAL = BIT(1),
	/* use dynamic interrupt moderation */
	ENETC_IC_RX_ADAPTIVE = BIT(2),
};

#define ENETC_RXIC_PKTTHR	min_t(u32, 256, ENETC_RX_RING_DEFAULT_SIZE / 2)
#define ENETC_TXIC_PKTTHR	min_t(u32, 128, ENETC_TX_RING_DEFAULT_SIZE / 2)
#define ENETC_TXIC_TIMETHR	enetc_usecs_to_cycles(600)

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

	struct psfp_cap psfp_cap;

	struct phylink *phylink;
	int ic_mode;
	u32 tx_ictt;
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
void enetc_start(struct net_device *ndev);
void enetc_stop(struct net_device *ndev);
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
void enetc_sched_speed_set(struct enetc_ndev_priv *priv, int speed);
int enetc_setup_tc_cbs(struct net_device *ndev, void *type_data);
int enetc_setup_tc_txtime(struct net_device *ndev, void *type_data);
int enetc_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			    void *cb_priv);
int enetc_setup_tc_psfp(struct net_device *ndev, void *type_data);
int enetc_psfp_init(struct enetc_ndev_priv *priv);
int enetc_psfp_clean(struct enetc_ndev_priv *priv);

static inline void enetc_get_max_cap(struct enetc_ndev_priv *priv)
{
	u32 reg;

	reg = enetc_port_rd(&priv->si->hw, ENETC_PSIDCAPR);
	priv->psfp_cap.max_streamid = reg & ENETC_PSIDCAPR_MSK;
	/* Port stream filter capability */
	reg = enetc_port_rd(&priv->si->hw, ENETC_PSFCAPR);
	priv->psfp_cap.max_psfp_filter = reg & ENETC_PSFCAPR_MSK;
	/* Port stream gate capability */
	reg = enetc_port_rd(&priv->si->hw, ENETC_PSGCAPR);
	priv->psfp_cap.max_psfp_gate = (reg & ENETC_PSGCAPR_SGIT_MSK);
	priv->psfp_cap.max_psfp_gatelist = (reg & ENETC_PSGCAPR_GCL_MSK) >> 16;
	/* Port flow meter capability */
	reg = enetc_port_rd(&priv->si->hw, ENETC_PFMCAPR);
	priv->psfp_cap.max_psfp_meter = reg & ENETC_PFMCAPR_MSK;
}

static inline int enetc_psfp_enable(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int err;

	enetc_get_max_cap(priv);

	err = enetc_psfp_init(priv);
	if (err)
		return err;

	enetc_wr(hw, ENETC_PPSFPMR, enetc_rd(hw, ENETC_PPSFPMR) |
		 ENETC_PPSFPMR_PSFPEN | ENETC_PPSFPMR_VS |
		 ENETC_PPSFPMR_PVC | ENETC_PPSFPMR_PVZC);

	return 0;
}

static inline int enetc_psfp_disable(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	int err;

	err = enetc_psfp_clean(priv);
	if (err)
		return err;

	enetc_wr(hw, ENETC_PPSFPMR, enetc_rd(hw, ENETC_PPSFPMR) &
		 ~ENETC_PPSFPMR_PSFPEN & ~ENETC_PPSFPMR_VS &
		 ~ENETC_PPSFPMR_PVC & ~ENETC_PPSFPMR_PVZC);

	memset(&priv->psfp_cap, 0, sizeof(struct psfp_cap));

	return 0;
}

#else
#define enetc_setup_tc_taprio(ndev, type_data) -EOPNOTSUPP
#define enetc_sched_speed_set(priv, speed) (void)0
#define enetc_setup_tc_cbs(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_txtime(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_psfp(ndev, type_data) -EOPNOTSUPP
#define enetc_setup_tc_block_cb NULL

#define enetc_get_max_cap(p)		\
	memset(&((p)->psfp_cap), 0, sizeof(struct psfp_cap))

static inline int enetc_psfp_enable(struct enetc_ndev_priv *priv)
{
	return 0;
}

static inline int enetc_psfp_disable(struct enetc_ndev_priv *priv)
{
	return 0;
}
#endif
