/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __STMMAC_H__
#define __STMMAC_H__

#define STMMAC_RESOURCE_NAME   "stmmaceth"

#include <linux/clk.h>
#include <linux/hrtimer.h>
#include <linux/if_vlan.h>
#include <linux/stmmac.h>
#include <linux/phylink.h>
#include <linux/pci.h>
#include "common.h"
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/reset.h>
#include <net/page_pool/types.h>
#include <net/xdp.h>
#include <uapi/linux/bpf.h>

struct stmmac_resources {
	void __iomem *addr;
	u8 mac[ETH_ALEN];
	int wol_irq;
	int lpi_irq;
	int irq;
	int sfty_irq;
	int sfty_ce_irq;
	int sfty_ue_irq;
	int rx_irq[MTL_MAX_RX_QUEUES];
	int tx_irq[MTL_MAX_TX_QUEUES];
};

enum stmmac_txbuf_type {
	STMMAC_TXBUF_T_SKB,
	STMMAC_TXBUF_T_XDP_TX,
	STMMAC_TXBUF_T_XDP_NDO,
	STMMAC_TXBUF_T_XSK_TX,
};

struct stmmac_tx_info {
	dma_addr_t buf;
	bool map_as_page;
	unsigned len;
	bool last_segment;
	bool is_jumbo;
	enum stmmac_txbuf_type buf_type;
	struct xsk_tx_metadata_compl xsk_meta;
};

#define STMMAC_TBS_AVAIL	BIT(0)
#define STMMAC_TBS_EN		BIT(1)

/* Frequently used values are kept adjacent for cache effect */
struct stmmac_tx_queue {
	u32 tx_count_frames;
	int tbs;
	struct hrtimer txtimer;
	u32 queue_index;
	struct stmmac_priv *priv_data;
	struct dma_extended_desc *dma_etx ____cacheline_aligned_in_smp;
	struct dma_edesc *dma_entx;
	struct dma_desc *dma_tx;
	union {
		struct sk_buff **tx_skbuff;
		struct xdp_frame **xdpf;
	};
	struct stmmac_tx_info *tx_skbuff_dma;
	struct xsk_buff_pool *xsk_pool;
	u32 xsk_frames_done;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	dma_addr_t dma_tx_phy;
	dma_addr_t tx_tail_addr;
	u32 mss;
};

struct stmmac_rx_buffer {
	union {
		struct {
			struct page *page;
			dma_addr_t addr;
			__u32 page_offset;
		};
		struct xdp_buff *xdp;
	};
	struct page *sec_page;
	dma_addr_t sec_addr;
};

struct stmmac_xdp_buff {
	struct xdp_buff xdp;
	struct stmmac_priv *priv;
	struct dma_desc *desc;
	struct dma_desc *ndesc;
};

struct stmmac_metadata_request {
	struct stmmac_priv *priv;
	struct dma_desc *tx_desc;
	bool *set_ic;
};

struct stmmac_xsk_tx_complete {
	struct stmmac_priv *priv;
	struct dma_desc *desc;
};

struct stmmac_rx_queue {
	u32 rx_count_frames;
	u32 queue_index;
	struct xdp_rxq_info xdp_rxq;
	struct xsk_buff_pool *xsk_pool;
	struct page_pool *page_pool;
	struct stmmac_rx_buffer *buf_pool;
	struct stmmac_priv *priv_data;
	struct dma_extended_desc *dma_erx;
	struct dma_desc *dma_rx ____cacheline_aligned_in_smp;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	unsigned int buf_alloc_num;
	unsigned int napi_skb_frag_size;
	dma_addr_t dma_rx_phy;
	u32 rx_tail_addr;
	unsigned int state_saved;
	struct {
		struct sk_buff *skb;
		unsigned int len;
		unsigned int error;
	} state;
};

struct stmmac_channel {
	struct napi_struct rx_napi ____cacheline_aligned_in_smp;
	struct napi_struct tx_napi ____cacheline_aligned_in_smp;
	struct napi_struct rxtx_napi ____cacheline_aligned_in_smp;
	struct stmmac_priv *priv_data;
	spinlock_t lock;
	u32 index;
};

struct stmmac_fpe_cfg {
	/* Serialize access to MAC Merge state between ethtool requests
	 * and link state updates.
	 */
	spinlock_t lock;

	const struct stmmac_fpe_reg *reg;
	u32 fpe_csr;				/* MAC_FPE_CTRL_STS reg cache */

	enum ethtool_mm_verify_status status;
	struct timer_list verify_timer;
	bool verify_enabled;
	int verify_retries;
	bool pmac_enabled;
	u32 verify_time;
	bool tx_enabled;
};

struct stmmac_tc_entry {
	bool in_use;
	bool in_hw;
	bool is_last;
	bool is_frag;
	void *frag_ptr;
	unsigned int table_pos;
	u32 handle;
	u32 prio;
	struct {
		u32 match_data;
		u32 match_en;
		u8 af:1;
		u8 rf:1;
		u8 im:1;
		u8 nc:1;
		u8 res1:4;
		u8 frame_offset;
		u8 ok_index;
		u8 dma_ch_no;
		u32 res2;
	} __packed val;
};

#define STMMAC_PPS_MAX		4
struct stmmac_pps_cfg {
	bool available;
	struct timespec64 start;
	struct timespec64 period;
};

struct stmmac_rss {
	int enable;
	u8 key[STMMAC_RSS_HASH_KEY_SIZE];
	u32 table[STMMAC_RSS_MAX_TABLE_SIZE];
};

#define STMMAC_FLOW_ACTION_DROP		BIT(0)
struct stmmac_flow_entry {
	unsigned long cookie;
	unsigned long action;
	u8 ip_proto;
	int in_use;
	int idx;
	int is_l4;
};

/* Rx Frame Steering */
enum stmmac_rfs_type {
	STMMAC_RFS_T_VLAN,
	STMMAC_RFS_T_LLDP,
	STMMAC_RFS_T_1588,
	STMMAC_RFS_T_MAX,
};

struct stmmac_rfs_entry {
	unsigned long cookie;
	u16 etype;
	int in_use;
	int type;
	int tc;
};

struct stmmac_dma_conf {
	unsigned int dma_buf_sz;

	/* RX Queue */
	struct stmmac_rx_queue rx_queue[MTL_MAX_RX_QUEUES];
	unsigned int dma_rx_size;

	/* TX Queue */
	struct stmmac_tx_queue tx_queue[MTL_MAX_TX_QUEUES];
	unsigned int dma_tx_size;
};

#define EST_GCL         1024
struct stmmac_est {
	int enable;
	u32 btr_reserve[2];
	u32 btr_offset[2];
	u32 btr[2];
	u32 ctr[2];
	u32 ter;
	u32 gcl_unaligned[EST_GCL];
	u32 gcl[EST_GCL];
	u32 gcl_size;
	u32 max_sdu[MTL_MAX_TX_QUEUES];
};

struct stmmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
	u32 tx_coal_frames[MTL_MAX_TX_QUEUES];
	u32 tx_coal_timer[MTL_MAX_TX_QUEUES];
	u32 rx_coal_frames[MTL_MAX_TX_QUEUES];

	int hwts_tx_en;
	bool tx_path_in_lpi_mode;
	bool tso;
	int sph;
	int sph_cap;
	u32 sarc_type;

	u32 rx_riwt[MTL_MAX_TX_QUEUES];
	int hwts_rx_en;

	void __iomem *ioaddr;
	struct net_device *dev;
	struct device *device;
	struct mac_device_info *hw;
	int (*hwif_quirks)(struct stmmac_priv *priv);
	struct mutex lock;

	struct stmmac_dma_conf dma_conf;

	/* Generic channel for NAPI */
	struct stmmac_channel channel[STMMAC_CH_MAX];

	int speed;
	unsigned int flow_ctrl;
	unsigned int pause;
	struct mii_bus *mii;

	struct phylink_config phylink_config;
	struct phylink *phylink;

	struct stmmac_extra_stats xstats ____cacheline_aligned_in_smp;
	struct stmmac_safety_stats sstats;
	struct plat_stmmacenet_data *plat;
	/* Protect est parameters */
	struct mutex est_lock;
	struct stmmac_est *est;
	struct dma_features dma_cap;
	struct stmmac_counters mmc;
	int hw_cap_support;
	int synopsys_id;
	u32 msg_enable;
	int wolopts;
	int wol_irq;
	bool wol_irq_disabled;
	int clk_csr;
	struct timer_list eee_ctrl_timer;
	int lpi_irq;
	u32 tx_lpi_timer;
	bool eee_enabled;
	bool eee_active;
	bool eee_sw_timer_en;
	unsigned int mode;
	unsigned int chain_mode;
	int extend_desc;
	struct hwtstamp_config tstamp_config;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_ops;
	unsigned int default_addend;
	u32 sub_second_inc;
	u32 systime_flags;
	u32 adv_ts;
	int use_riwt;
	int irq_wake;
	rwlock_t ptp_lock;
	/* Protects auxiliary snapshot registers from concurrent access. */
	struct mutex aux_ts_lock;
	wait_queue_head_t tstamp_busy_wait;

	void __iomem *mmcaddr;
	void __iomem *ptpaddr;
	void __iomem *estaddr;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	int sfty_irq;
	int sfty_ce_irq;
	int sfty_ue_irq;
	int rx_irq[MTL_MAX_RX_QUEUES];
	int tx_irq[MTL_MAX_TX_QUEUES];
	/*irq name */
	char int_name_mac[IFNAMSIZ + 9];
	char int_name_wol[IFNAMSIZ + 9];
	char int_name_lpi[IFNAMSIZ + 9];
	char int_name_sfty[IFNAMSIZ + 10];
	char int_name_sfty_ce[IFNAMSIZ + 10];
	char int_name_sfty_ue[IFNAMSIZ + 10];
	char int_name_rx_irq[MTL_MAX_TX_QUEUES][IFNAMSIZ + 14];
	char int_name_tx_irq[MTL_MAX_TX_QUEUES][IFNAMSIZ + 18];

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
#endif

	unsigned long state;
	struct workqueue_struct *wq;
	struct work_struct service_task;

	/* Frame Preemption feature (FPE) */
	struct stmmac_fpe_cfg fpe_cfg;

	/* TC Handling */
	unsigned int tc_entries_max;
	unsigned int tc_off_max;
	struct stmmac_tc_entry *tc_entries;
	unsigned int flow_entries_max;
	struct stmmac_flow_entry *flow_entries;
	unsigned int rfs_entries_max[STMMAC_RFS_T_MAX];
	unsigned int rfs_entries_cnt[STMMAC_RFS_T_MAX];
	unsigned int rfs_entries_total;
	struct stmmac_rfs_entry *rfs_entries;

	/* Pulse Per Second output */
	struct stmmac_pps_cfg pps[STMMAC_PPS_MAX];

	/* Receive Side Scaling */
	struct stmmac_rss rss;

	/* XDP BPF Program */
	unsigned long *af_xdp_zc_qps;
	struct bpf_prog *xdp_prog;
};

enum stmmac_state {
	STMMAC_DOWN,
	STMMAC_RESET_REQUESTED,
	STMMAC_RESETING,
	STMMAC_SERVICE_SCHED,
};

int stmmac_mdio_unregister(struct net_device *ndev);
int stmmac_mdio_register(struct net_device *ndev);
int stmmac_mdio_reset(struct mii_bus *mii);
int stmmac_pcs_setup(struct net_device *ndev);
void stmmac_pcs_clean(struct net_device *ndev);
void stmmac_set_ethtool_ops(struct net_device *netdev);

int stmmac_init_tstamp_counter(struct stmmac_priv *priv, u32 systime_flags);
void stmmac_ptp_register(struct stmmac_priv *priv);
void stmmac_ptp_unregister(struct stmmac_priv *priv);
int stmmac_xdp_open(struct net_device *dev);
void stmmac_xdp_release(struct net_device *dev);
int stmmac_resume(struct device *dev);
int stmmac_suspend(struct device *dev);
void stmmac_dvr_remove(struct device *dev);
int stmmac_dvr_probe(struct device *device,
		     struct plat_stmmacenet_data *plat_dat,
		     struct stmmac_resources *res);
int stmmac_reinit_queues(struct net_device *dev, u32 rx_cnt, u32 tx_cnt);
int stmmac_reinit_ringparam(struct net_device *dev, u32 rx_size, u32 tx_size);
int stmmac_bus_clks_config(struct stmmac_priv *priv, bool enabled);

static inline bool stmmac_xdp_is_enabled(struct stmmac_priv *priv)
{
	return !!priv->xdp_prog;
}

void stmmac_disable_rx_queue(struct stmmac_priv *priv, u32 queue);
void stmmac_enable_rx_queue(struct stmmac_priv *priv, u32 queue);
void stmmac_disable_tx_queue(struct stmmac_priv *priv, u32 queue);
void stmmac_enable_tx_queue(struct stmmac_priv *priv, u32 queue);
int stmmac_xsk_wakeup(struct net_device *dev, u32 queue, u32 flags);
struct timespec64 stmmac_calc_tas_basetime(ktime_t old_base_time,
					   ktime_t current_time,
					   u64 cycle_time);

#if IS_ENABLED(CONFIG_STMMAC_SELFTESTS)
void stmmac_selftest_run(struct net_device *dev,
			 struct ethtool_test *etest, u64 *buf);
void stmmac_selftest_get_strings(struct stmmac_priv *priv, u8 *data);
int stmmac_selftest_get_count(struct stmmac_priv *priv);
#else
static inline void stmmac_selftest_run(struct net_device *dev,
				       struct ethtool_test *etest, u64 *buf)
{
	/* Not enabled */
}
static inline void stmmac_selftest_get_strings(struct stmmac_priv *priv,
					       u8 *data)
{
	/* Not enabled */
}
static inline int stmmac_selftest_get_count(struct stmmac_priv *priv)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_STMMAC_SELFTESTS */

#endif /* __STMMAC_H__ */
