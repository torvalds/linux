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
#include <net/xdp.h>

#include "enetc_hw.h"
#include "enetc4_hw.h"

#define ENETC_MAC_MAXFRM_SIZE	9600
#define ENETC_MAX_MTU		(ENETC_MAC_MAXFRM_SIZE - \
				(ETH_FCS_LEN + ETH_HLEN + VLAN_HLEN))

#define ENETC_CBD_DATA_MEM_ALIGN 64

struct enetc_tx_swbd {
	union {
		struct sk_buff *skb;
		struct xdp_frame *xdp_frame;
	};
	dma_addr_t dma;
	struct page *page;	/* valid only if is_xdp_tx */
	u16 page_offset;	/* valid only if is_xdp_tx */
	u16 len;
	enum dma_data_direction dir;
	u8 is_dma_page:1;
	u8 check_wb:1;
	u8 do_twostep_tstamp:1;
	u8 is_eof:1;
	u8 is_xdp_tx:1;
	u8 is_xdp_redirect:1;
	u8 qbv_en:1;
};

#define ENETC_RX_MAXFRM_SIZE	ENETC_MAC_MAXFRM_SIZE
#define ENETC_RXB_TRUESIZE	2048 /* PAGE_SIZE >> 1 */
#define ENETC_RXB_PAD		NET_SKB_PAD /* add extra space if needed */
#define ENETC_RXB_DMA_SIZE	\
	(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - ENETC_RXB_PAD)
#define ENETC_RXB_DMA_SIZE_XDP	\
	(SKB_WITH_OVERHEAD(ENETC_RXB_TRUESIZE) - XDP_PACKET_HEADROOM)

struct enetc_rx_swbd {
	dma_addr_t dma;
	struct page *page;
	u16 page_offset;
	enum dma_data_direction dir;
	u16 len;
};

/* ENETC overhead: optional extension BD + 1 BD gap */
#define ENETC_TXBDS_NEEDED(val)	((val) + 2)
/* max # of chained Tx BDs is 15, including head and extension BD */
#define ENETC_MAX_SKB_FRAGS	13
#define ENETC_TXBDS_MAX_NEEDED	ENETC_TXBDS_NEEDED(ENETC_MAX_SKB_FRAGS + 1)

struct enetc_ring_stats {
	unsigned int packets;
	unsigned int bytes;
	unsigned int rx_alloc_errs;
	unsigned int xdp_drops;
	unsigned int xdp_tx;
	unsigned int xdp_tx_drops;
	unsigned int xdp_redirect;
	unsigned int xdp_redirect_failures;
	unsigned int recycles;
	unsigned int recycle_failures;
	unsigned int win_drop;
};

struct enetc_xdp_data {
	struct xdp_rxq_info rxq;
	struct bpf_prog *prog;
	int xdp_tx_in_flight;
};

#define ENETC_RX_RING_DEFAULT_SIZE	2048
#define ENETC_TX_RING_DEFAULT_SIZE	2048
#define ENETC_DEFAULT_TX_WORK		(ENETC_TX_RING_DEFAULT_SIZE / 2)

struct enetc_bdr_resource {
	/* Input arguments saved for teardown */
	struct device *dev; /* for DMA mapping */
	size_t bd_count;
	size_t bd_size;

	/* Resource proper */
	void *bd_base; /* points to Rx or Tx BD ring */
	dma_addr_t bd_dma_base;
	union {
		struct enetc_tx_swbd *tx_swbd;
		struct enetc_rx_swbd *rx_swbd;
	};
	char *tso_headers;
	dma_addr_t tso_headers_dma;
};

struct enetc_bdr {
	struct device *dev; /* for DMA mapping */
	struct net_device *ndev;
	void *bd_base; /* points to Rx or Tx BD ring */
	union {
		void __iomem *tpir;
		void __iomem *rcir;
	};
	u16 index;
	u16 prio;
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

	int buffer_offset;
	struct enetc_xdp_data xdp;

	struct enetc_ring_stats stats;

	dma_addr_t bd_dma_base;
	u8 tsd_enable; /* Time specific departure */
	bool ext_en; /* enable h/w descriptor extensions */

	/* DMA buffer for TSO headers */
	char *tso_headers;
	dma_addr_t tso_headers_dma;
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

static inline int enetc_swbd_unused(struct enetc_bdr *bdr)
{
	if (bdr->next_to_clean > bdr->next_to_alloc)
		return bdr->next_to_clean - bdr->next_to_alloc - 1;

	return bdr->bd_count + bdr->next_to_clean - bdr->next_to_alloc - 1;
}

/* Control BD ring */
#define ENETC_CBDR_DEFAULT_SIZE	64
struct enetc_cbdr {
	void *bd_base; /* points to Rx or Tx BD ring */
	void __iomem *pir;
	void __iomem *cir;
	void __iomem *mr; /* mode register */

	int bd_count; /* # of BDs */
	int next_to_use;
	int next_to_clean;

	dma_addr_t bd_dma_base;
	struct device *dma_dev;
};

#define ENETC_TXBD(BDR, i) (&(((union enetc_tx_bd *)((BDR).bd_base))[i]))

static inline union enetc_rx_bd *enetc_rxbd(struct enetc_bdr *rx_ring, int i)
{
	int hw_idx = i;

	if (IS_ENABLED(CONFIG_FSL_ENETC_PTP_CLOCK) && rx_ring->ext_en)
		hw_idx = 2 * i;

	return &(((union enetc_rx_bd *)rx_ring->bd_base)[hw_idx]);
}

static inline void enetc_rxbd_next(struct enetc_bdr *rx_ring,
				   union enetc_rx_bd **old_rxbd, int *old_index)
{
	union enetc_rx_bd *new_rxbd = *old_rxbd;
	int new_index = *old_index;

	new_rxbd++;

	if (IS_ENABLED(CONFIG_FSL_ENETC_PTP_CLOCK) && rx_ring->ext_en)
		new_rxbd++;

	if (unlikely(++new_index == rx_ring->bd_count)) {
		new_rxbd = rx_ring->bd_base;
		new_index = 0;
	}

	*old_rxbd = new_rxbd;
	*old_index = new_index;
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

#define ENETC_SI_F_PSFP BIT(0)
#define ENETC_SI_F_QBV  BIT(1)
#define ENETC_SI_F_QBU  BIT(2)

struct enetc_drvdata {
	u32 pmac_offset; /* Only valid for PSI which supports 802.1Qbu */
	u64 sysclk_freq;
	const struct ethtool_ops *eth_ops;
};

struct enetc_platform_info {
	u16 revision;
	u16 dev_id;
	const struct enetc_drvdata *data;
};

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
	u16 revision;
	int hw_features;
	const struct enetc_drvdata *drvdata;
};

#define ENETC_SI_ALIGN	32

static inline bool is_enetc_rev1(struct enetc_si *si)
{
	return si->pdev->revision == ENETC_REV1;
}

static inline void *enetc_si_priv(const struct enetc_si *si)
{
	return (char *)si + ALIGN(sizeof(struct enetc_si), ENETC_SI_ALIGN);
}

static inline bool enetc_si_is_pf(struct enetc_si *si)
{
	return !!(si->hw.port);
}

static inline int enetc_pf_to_port(struct pci_dev *pf_pdev)
{
	switch (pf_pdev->devfn) {
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return 2;
	case 6:
		return 3;
	default:
		return -1;
	}
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
	struct enetc_bdr tx_ring[] __counted_by(count_tx_rings);
} ____cacheline_aligned_in_smp;

struct enetc_cls_rule {
	struct ethtool_rx_flow_spec fs;
	int used;
};

#define ENETC_MAX_BDR_INT	6 /* fixed to max # of available cpus */
struct psfp_cap {
	u32 max_streamid;
	u32 max_psfp_filter;
	u32 max_psfp_gate;
	u32 max_psfp_gatelist;
	u32 max_psfp_meter;
};

#define ENETC_F_TX_TSTAMP_MASK	0xff
enum enetc_active_offloads {
	/* 8 bits reserved for TX timestamp types (hwtstamp_tx_types) */
	ENETC_F_TX_TSTAMP		= BIT(0),
	ENETC_F_TX_ONESTEP_SYNC_TSTAMP	= BIT(1),

	ENETC_F_RX_TSTAMP		= BIT(8),
	ENETC_F_QBV			= BIT(9),
	ENETC_F_QCI			= BIT(10),
	ENETC_F_QBU			= BIT(11),
};

enum enetc_flags_bit {
	ENETC_TX_ONESTEP_TSTAMP_IN_PROGRESS = 0,
	ENETC_TX_DOWN,
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

struct enetc_ndev_priv {
	struct net_device *ndev;
	struct device *dev; /* dma-mapping device */
	struct enetc_si *si;

	int bdr_int_num; /* number of Rx/Tx ring interrupts */
	struct enetc_int_vector *int_vector[ENETC_MAX_BDR_INT];
	u16 num_rx_rings, num_tx_rings;
	u16 rx_bd_count, tx_bd_count;

	u16 msg_enable;

	u8 preemptible_tcs;

	enum enetc_active_offloads active_offloads;

	u32 speed; /* store speed for compare update pspeed */

	struct enetc_bdr **xdp_tx_ring;
	struct enetc_bdr *tx_ring[16];
	struct enetc_bdr *rx_ring[16];
	const struct enetc_bdr_resource *tx_res;
	const struct enetc_bdr_resource *rx_res;

	struct enetc_cls_rule *cls_rules;

	struct psfp_cap psfp_cap;

	/* Minimum number of TX queues required by the network stack */
	unsigned int min_num_stack_tx_queues;

	struct phylink *phylink;
	int ic_mode;
	u32 tx_ictt;

	struct bpf_prog *xdp_prog;

	unsigned long flags;

	struct work_struct	tx_onestep_tstamp;
	struct sk_buff_head	tx_skbs;

	/* Serialize access to MAC Merge state between ethtool requests
	 * and link state updates
	 */
	struct mutex		mm_lock;

	struct clk *ref_clk; /* RGMII/RMII reference clock */
	u64 sysclk_freq; /* NETC system clock frequency */
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
u32 enetc_port_mac_rd(struct enetc_si *si, u32 reg);
void enetc_port_mac_wr(struct enetc_si *si, u32 reg, u32 val);
int enetc_pci_probe(struct pci_dev *pdev, const char *name, int sizeof_priv);
void enetc_pci_remove(struct pci_dev *pdev);
int enetc_alloc_msix(struct enetc_ndev_priv *priv);
void enetc_free_msix(struct enetc_ndev_priv *priv);
void enetc_get_si_caps(struct enetc_si *si);
void enetc_init_si_rings_params(struct enetc_ndev_priv *priv);
int enetc_alloc_si_resources(struct enetc_ndev_priv *priv);
void enetc_free_si_resources(struct enetc_ndev_priv *priv);
int enetc_configure_si(struct enetc_ndev_priv *priv);
int enetc_get_driver_data(struct enetc_si *si);

int enetc_open(struct net_device *ndev);
int enetc_close(struct net_device *ndev);
void enetc_start(struct net_device *ndev);
void enetc_stop(struct net_device *ndev);
netdev_tx_t enetc_xmit(struct sk_buff *skb, struct net_device *ndev);
struct net_device_stats *enetc_get_stats(struct net_device *ndev);
void enetc_set_features(struct net_device *ndev, netdev_features_t features);
int enetc_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd);
int enetc_setup_tc_mqprio(struct net_device *ndev, void *type_data);
void enetc_reset_tc_mqprio(struct net_device *ndev);
int enetc_setup_bpf(struct net_device *ndev, struct netdev_bpf *bpf);
int enetc_xdp_xmit(struct net_device *ndev, int num_frames,
		   struct xdp_frame **frames, u32 flags);

/* ethtool */
extern const struct ethtool_ops enetc_pf_ethtool_ops;
extern const struct ethtool_ops enetc4_pf_ethtool_ops;
extern const struct ethtool_ops enetc_vf_ethtool_ops;
void enetc_set_ethtool_ops(struct net_device *ndev);
void enetc_mm_link_state_update(struct enetc_ndev_priv *priv, bool link);
void enetc_mm_commit_preemptible_tcs(struct enetc_ndev_priv *priv);

/* control buffer descriptor ring (CBDR) */
int enetc_setup_cbdr(struct device *dev, struct enetc_hw *hw, int bd_count,
		     struct enetc_cbdr *cbdr);
void enetc_teardown_cbdr(struct enetc_cbdr *cbdr);
int enetc_set_mac_flt_entry(struct enetc_si *si, int index,
			    char *mac_addr, int si_map);
int enetc_clear_mac_flt_entry(struct enetc_si *si, int index);
int enetc_set_fs_entry(struct enetc_si *si, struct enetc_cmd_rfse *rfse,
		       int index);
void enetc_set_rss_key(struct enetc_hw *hw, const u8 *bytes);
int enetc_get_rss_table(struct enetc_si *si, u32 *table, int count);
int enetc_set_rss_table(struct enetc_si *si, const u32 *table, int count);
int enetc_send_cmd(struct enetc_si *si, struct enetc_cbd *cbd);

static inline void *enetc_cbd_alloc_data_mem(struct enetc_si *si,
					     struct enetc_cbd *cbd,
					     int size, dma_addr_t *dma,
					     void **data_align)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	dma_addr_t dma_align;
	void *data;

	data = dma_alloc_coherent(ring->dma_dev,
				  size + ENETC_CBD_DATA_MEM_ALIGN,
				  dma, GFP_KERNEL);
	if (!data) {
		dev_err(ring->dma_dev, "CBD alloc data memory failed!\n");
		return NULL;
	}

	dma_align = ALIGN(*dma, ENETC_CBD_DATA_MEM_ALIGN);
	*data_align = PTR_ALIGN(data, ENETC_CBD_DATA_MEM_ALIGN);

	cbd->addr[0] = cpu_to_le32(lower_32_bits(dma_align));
	cbd->addr[1] = cpu_to_le32(upper_32_bits(dma_align));
	cbd->length = cpu_to_le16(size);

	return data;
}

static inline void enetc_cbd_free_data_mem(struct enetc_si *si, int size,
					   void *data, dma_addr_t *dma)
{
	struct enetc_cbdr *ring = &si->cbd_ring;

	dma_free_coherent(ring->dma_dev, size + ENETC_CBD_DATA_MEM_ALIGN,
			  data, *dma);
}

void enetc_reset_ptcmsdur(struct enetc_hw *hw);
void enetc_set_ptcmsdur(struct enetc_hw *hw, u32 *queue_max_sdu);

#ifdef CONFIG_FSL_ENETC_QOS
int enetc_qos_query_caps(struct net_device *ndev, void *type_data);
int enetc_setup_tc_taprio(struct net_device *ndev, void *type_data);
void enetc_sched_speed_set(struct enetc_ndev_priv *priv, int speed);
int enetc_setup_tc_cbs(struct net_device *ndev, void *type_data);
int enetc_setup_tc_txtime(struct net_device *ndev, void *type_data);
int enetc_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			    void *cb_priv);
int enetc_setup_tc_psfp(struct net_device *ndev, void *type_data);
int enetc_psfp_init(struct enetc_ndev_priv *priv);
int enetc_psfp_clean(struct enetc_ndev_priv *priv);
int enetc_set_psfp(struct net_device *ndev, bool en);

static inline void enetc_get_max_cap(struct enetc_ndev_priv *priv)
{
	struct enetc_hw *hw = &priv->si->hw;
	u32 reg;

	reg = enetc_port_rd(hw, ENETC_PSIDCAPR);
	priv->psfp_cap.max_streamid = reg & ENETC_PSIDCAPR_MSK;
	/* Port stream filter capability */
	reg = enetc_port_rd(hw, ENETC_PSFCAPR);
	priv->psfp_cap.max_psfp_filter = reg & ENETC_PSFCAPR_MSK;
	/* Port stream gate capability */
	reg = enetc_port_rd(hw, ENETC_PSGCAPR);
	priv->psfp_cap.max_psfp_gate = (reg & ENETC_PSGCAPR_SGIT_MSK);
	priv->psfp_cap.max_psfp_gatelist = (reg & ENETC_PSGCAPR_GCL_MSK) >> 16;
	/* Port flow meter capability */
	reg = enetc_port_rd(hw, ENETC_PFMCAPR);
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
#define enetc_qos_query_caps(ndev, type_data) -EOPNOTSUPP
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

static inline int enetc_set_psfp(struct net_device *ndev, bool en)
{
	return 0;
}
#endif
