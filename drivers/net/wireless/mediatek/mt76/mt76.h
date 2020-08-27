/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#ifndef __MT76_H
#define __MT76_H

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/leds.h>
#include <linux/usb.h>
#include <linux/average.h>
#include <net/mac80211.h>
#include "util.h"
#include "testmode.h"

#define MT_MCU_RING_SIZE    32
#define MT_RX_BUF_SIZE      2048
#define MT_SKB_HEAD_LEN     128

#define MT_MAX_NON_AQL_PKT  16
#define MT_TXQ_FREE_THR     32

struct mt76_dev;
struct mt76_phy;
struct mt76_wcid;

struct mt76_reg_pair {
	u32 reg;
	u32 value;
};

enum mt76_bus_type {
	MT76_BUS_MMIO,
	MT76_BUS_USB,
	MT76_BUS_SDIO,
};

struct mt76_bus_ops {
	u32 (*rr)(struct mt76_dev *dev, u32 offset);
	void (*wr)(struct mt76_dev *dev, u32 offset, u32 val);
	u32 (*rmw)(struct mt76_dev *dev, u32 offset, u32 mask, u32 val);
	void (*write_copy)(struct mt76_dev *dev, u32 offset, const void *data,
			   int len);
	void (*read_copy)(struct mt76_dev *dev, u32 offset, void *data,
			  int len);
	int (*wr_rp)(struct mt76_dev *dev, u32 base,
		     const struct mt76_reg_pair *rp, int len);
	int (*rd_rp)(struct mt76_dev *dev, u32 base,
		     struct mt76_reg_pair *rp, int len);
	enum mt76_bus_type type;
};

#define mt76_is_usb(dev) ((dev)->bus->type == MT76_BUS_USB)
#define mt76_is_mmio(dev) ((dev)->bus->type == MT76_BUS_MMIO)
#define mt76_is_sdio(dev) ((dev)->bus->type == MT76_BUS_SDIO)

enum mt76_txq_id {
	MT_TXQ_VO = IEEE80211_AC_VO,
	MT_TXQ_VI = IEEE80211_AC_VI,
	MT_TXQ_BE = IEEE80211_AC_BE,
	MT_TXQ_BK = IEEE80211_AC_BK,
	MT_TXQ_PSD,
	MT_TXQ_MCU,
	MT_TXQ_MCU_WA,
	MT_TXQ_BEACON,
	MT_TXQ_CAB,
	MT_TXQ_FWDL,
	__MT_TXQ_MAX
};

enum mt76_rxq_id {
	MT_RXQ_MAIN,
	MT_RXQ_MCU,
	MT_RXQ_MCU_WA,
	__MT_RXQ_MAX
};

struct mt76_queue_buf {
	dma_addr_t addr;
	u16 len;
	bool skip_unmap;
};

struct mt76_tx_info {
	struct mt76_queue_buf buf[32];
	struct sk_buff *skb;
	int nbuf;
	u32 info;
};

struct mt76_queue_entry {
	union {
		void *buf;
		struct sk_buff *skb;
	};
	union {
		struct mt76_txwi_cache *txwi;
		struct urb *urb;
		int buf_sz;
	};
	u32 dma_addr[2];
	u16 dma_len[2];
	u16 wcid;
	bool skip_buf0:1;
	bool skip_buf1:1;
	bool done:1;
};

struct mt76_queue_regs {
	u32 desc_base;
	u32 ring_size;
	u32 cpu_idx;
	u32 dma_idx;
} __packed __aligned(4);

struct mt76_queue {
	struct mt76_queue_regs __iomem *regs;

	spinlock_t lock;
	struct mt76_queue_entry *entry;
	struct mt76_desc *desc;

	u16 first;
	u16 head;
	u16 tail;
	int ndesc;
	int queued;
	int buf_size;
	bool stopped;

	u8 buf_offset;
	u8 hw_idx;

	dma_addr_t desc_dma;
	struct sk_buff *rx_head;
	struct page_frag_cache rx_page;
};

struct mt76_mcu_ops {
	u32 headroom;
	u32 tailroom;

	int (*mcu_send_msg)(struct mt76_dev *dev, int cmd, const void *data,
			    int len, bool wait_resp);
	int (*mcu_skb_send_msg)(struct mt76_dev *dev, struct sk_buff *skb,
				int cmd, bool wait_resp);
	u32 (*mcu_rr)(struct mt76_dev *dev, u32 offset);
	void (*mcu_wr)(struct mt76_dev *dev, u32 offset, u32 val);
	int (*mcu_wr_rp)(struct mt76_dev *dev, u32 base,
			 const struct mt76_reg_pair *rp, int len);
	int (*mcu_rd_rp)(struct mt76_dev *dev, u32 base,
			 struct mt76_reg_pair *rp, int len);
	int (*mcu_restart)(struct mt76_dev *dev);
};

struct mt76_queue_ops {
	int (*init)(struct mt76_dev *dev);

	int (*alloc)(struct mt76_dev *dev, struct mt76_queue *q,
		     int idx, int n_desc, int bufsize,
		     u32 ring_base);

	int (*tx_queue_skb)(struct mt76_dev *dev, enum mt76_txq_id qid,
			    struct sk_buff *skb, struct mt76_wcid *wcid,
			    struct ieee80211_sta *sta);

	int (*tx_queue_skb_raw)(struct mt76_dev *dev, enum mt76_txq_id qid,
				struct sk_buff *skb, u32 tx_info);

	void *(*dequeue)(struct mt76_dev *dev, struct mt76_queue *q, bool flush,
			 int *len, u32 *info, bool *more);

	void (*rx_reset)(struct mt76_dev *dev, enum mt76_rxq_id qid);

	void (*tx_cleanup)(struct mt76_dev *dev, enum mt76_txq_id qid,
			   bool flush);

	void (*kick)(struct mt76_dev *dev, struct mt76_queue *q);
};

enum mt76_wcid_flags {
	MT_WCID_FLAG_CHECK_PS,
	MT_WCID_FLAG_PS,
};

#define MT76_N_WCIDS 288

/* stored in ieee80211_tx_info::hw_queue */
#define MT_TX_HW_QUEUE_EXT_PHY		BIT(3)

DECLARE_EWMA(signal, 10, 8);

#define MT_WCID_TX_INFO_RATE		GENMASK(15, 0)
#define MT_WCID_TX_INFO_NSS		GENMASK(17, 16)
#define MT_WCID_TX_INFO_TXPWR_ADJ	GENMASK(25, 18)
#define MT_WCID_TX_INFO_SET		BIT(31)

struct mt76_wcid {
	struct mt76_rx_tid __rcu *aggr[IEEE80211_NUM_TIDS];

	atomic_t non_aql_packets;
	unsigned long flags;

	struct ewma_signal rssi;
	int inactive_count;

	u16 idx;
	u8 hw_key_idx;

	u8 sta:1;
	u8 ext_phy:1;
	u8 amsdu:1;

	u8 rx_check_pn;
	u8 rx_key_pn[IEEE80211_NUM_TIDS][6];
	u16 cipher;

	u32 tx_info;
	bool sw_iv;

	u8 packet_id;
};

struct mt76_txq {
	struct mt76_queue *q;
	struct mt76_wcid *wcid;

	struct sk_buff_head retry_q;

	u16 agg_ssn;
	bool send_bar;
	bool aggr;
};

struct mt76_txwi_cache {
	struct list_head list;
	dma_addr_t dma_addr;

	struct sk_buff *skb;
};

struct mt76_rx_tid {
	struct rcu_head rcu_head;

	struct mt76_dev *dev;

	spinlock_t lock;
	struct delayed_work reorder_work;

	u16 head;
	u16 size;
	u16 nframes;

	u8 num;

	u8 started:1, stopped:1, timer_pending:1;

	struct sk_buff *reorder_buf[];
};

#define MT_TX_CB_DMA_DONE		BIT(0)
#define MT_TX_CB_TXS_DONE		BIT(1)
#define MT_TX_CB_TXS_FAILED		BIT(2)

#define MT_PACKET_ID_MASK		GENMASK(6, 0)
#define MT_PACKET_ID_NO_ACK		0
#define MT_PACKET_ID_NO_SKB		1
#define MT_PACKET_ID_FIRST		2
#define MT_PACKET_ID_HAS_RATE		BIT(7)

#define MT_TX_STATUS_SKB_TIMEOUT	HZ

struct mt76_tx_cb {
	unsigned long jiffies;
	u16 wcid;
	u8 pktid;
	u8 flags;
};

enum {
	MT76_STATE_INITIALIZED,
	MT76_STATE_RUNNING,
	MT76_STATE_MCU_RUNNING,
	MT76_SCANNING,
	MT76_HW_SCANNING,
	MT76_HW_SCHED_SCANNING,
	MT76_RESTART,
	MT76_RESET,
	MT76_MCU_RESET,
	MT76_REMOVED,
	MT76_READING_STATS,
	MT76_STATE_POWER_OFF,
	MT76_STATE_SUSPEND,
	MT76_STATE_ROC,
	MT76_STATE_PM,
};

struct mt76_hw_cap {
	bool has_2ghz;
	bool has_5ghz;
};

#define MT_DRV_TXWI_NO_FREE		BIT(0)
#define MT_DRV_TX_ALIGNED4_SKBS		BIT(1)
#define MT_DRV_SW_RX_AIRTIME		BIT(2)
#define MT_DRV_RX_DMA_HDR		BIT(3)
#define MT_DRV_HW_MGMT_TXQ		BIT(4)
#define MT_DRV_AMSDU_OFFLOAD		BIT(5)

struct mt76_driver_ops {
	u32 drv_flags;
	u32 survey_flags;
	u16 txwi_size;

	void (*update_survey)(struct mt76_dev *dev);

	int (*tx_prepare_skb)(struct mt76_dev *dev, void *txwi_ptr,
			      enum mt76_txq_id qid, struct mt76_wcid *wcid,
			      struct ieee80211_sta *sta,
			      struct mt76_tx_info *tx_info);

	void (*tx_complete_skb)(struct mt76_dev *dev,
				struct mt76_queue_entry *e);

	bool (*tx_status_data)(struct mt76_dev *dev, u8 *update);

	void (*rx_skb)(struct mt76_dev *dev, enum mt76_rxq_id q,
		       struct sk_buff *skb);

	void (*rx_poll_complete)(struct mt76_dev *dev, enum mt76_rxq_id q);

	void (*sta_ps)(struct mt76_dev *dev, struct ieee80211_sta *sta,
		       bool ps);

	int (*sta_add)(struct mt76_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);

	void (*sta_assoc)(struct mt76_dev *dev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);

	void (*sta_remove)(struct mt76_dev *dev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
};

struct mt76_channel_state {
	u64 cc_active;
	u64 cc_busy;
	u64 cc_rx;
	u64 cc_bss_rx;
	u64 cc_tx;

	s8 noise;
};

struct mt76_sband {
	struct ieee80211_supported_band sband;
	struct mt76_channel_state *chan;
};

struct mt76_rate_power {
	union {
		struct {
			s8 cck[4];
			s8 ofdm[8];
			s8 stbc[10];
			s8 ht[16];
			s8 vht[10];
		};
		s8 all[48];
	};
};

/* addr req mask */
#define MT_VEND_TYPE_EEPROM	BIT(31)
#define MT_VEND_TYPE_CFG	BIT(30)
#define MT_VEND_TYPE_MASK	(MT_VEND_TYPE_EEPROM | MT_VEND_TYPE_CFG)

#define MT_VEND_ADDR(type, n)	(MT_VEND_TYPE_##type | (n))
enum mt_vendor_req {
	MT_VEND_DEV_MODE =	0x1,
	MT_VEND_WRITE =		0x2,
	MT_VEND_POWER_ON =	0x4,
	MT_VEND_MULTI_WRITE =	0x6,
	MT_VEND_MULTI_READ =	0x7,
	MT_VEND_READ_EEPROM =	0x9,
	MT_VEND_WRITE_FCE =	0x42,
	MT_VEND_WRITE_CFG =	0x46,
	MT_VEND_READ_CFG =	0x47,
	MT_VEND_READ_EXT =	0x63,
	MT_VEND_WRITE_EXT =	0x66,
	MT_VEND_FEATURE_SET =	0x91,
};

enum mt76u_in_ep {
	MT_EP_IN_PKT_RX,
	MT_EP_IN_CMD_RESP,
	__MT_EP_IN_MAX,
};

enum mt76u_out_ep {
	MT_EP_OUT_INBAND_CMD,
	MT_EP_OUT_AC_BE,
	MT_EP_OUT_AC_BK,
	MT_EP_OUT_AC_VI,
	MT_EP_OUT_AC_VO,
	MT_EP_OUT_HCCA,
	__MT_EP_OUT_MAX,
};

struct mt76_mcu {
	struct mutex mutex;
	u32 msg_seq;

	struct sk_buff_head res_q;
	wait_queue_head_t wait;
};

#define MT_TX_SG_MAX_SIZE	8
#define MT_RX_SG_MAX_SIZE	4
#define MT_NUM_TX_ENTRIES	256
#define MT_NUM_RX_ENTRIES	128
#define MCU_RESP_URB_SIZE	1024
struct mt76_usb {
	struct mutex usb_ctrl_mtx;
	u8 *data;
	u16 data_len;

	struct tasklet_struct rx_tasklet;
	struct work_struct stat_work;

	u8 out_ep[__MT_EP_OUT_MAX];
	u8 in_ep[__MT_EP_IN_MAX];
	bool sg_en;

	struct mt76u_mcu {
		u8 *data;
		/* multiple reads */
		struct mt76_reg_pair *rp;
		int rp_len;
		u32 base;
		bool burst;
	} mcu;
};

struct mt76_sdio {
	struct workqueue_struct *txrx_wq;
	struct {
		struct work_struct xmit_work;
		struct work_struct status_work;
	} tx;
	struct {
		struct work_struct recv_work;
		struct work_struct net_work;
	} rx;

	struct work_struct stat_work;

	struct sdio_func *func;
	void *intr_data;

	struct {
		struct mutex lock;
		int pse_data_quota;
		int ple_data_quota;
		int pse_mcu_quota;
		int deficit;
	} sched;
};

struct mt76_mmio {
	void __iomem *regs;
	spinlock_t irq_lock;
	u32 irqmask;
};

struct mt76_rx_status {
	union {
		struct mt76_wcid *wcid;
		u16 wcid_idx;
	};

	unsigned long reorder_time;

	u32 ampdu_ref;

	u8 iv[6];

	u8 ext_phy:1;
	u8 aggr:1;
	u8 tid;
	u16 seqno;

	u16 freq;
	u32 flag;
	u8 enc_flags;
	u8 encoding:2, bw:3, he_ru:3;
	u8 he_gi:2, he_dcm:1;
	u8 rate_idx;
	u8 nss;
	u8 band;
	s8 signal;
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
};

struct mt76_testmode_ops {
	int (*set_state)(struct mt76_dev *dev, enum mt76_testmode_state state);
	int (*set_params)(struct mt76_dev *dev, struct nlattr **tb,
			  enum mt76_testmode_state new_state);
	int (*dump_stats)(struct mt76_dev *dev, struct sk_buff *msg);
};

struct mt76_testmode_data {
	enum mt76_testmode_state state;

	u32 param_set[DIV_ROUND_UP(NUM_MT76_TM_ATTRS, 32)];
	struct sk_buff *tx_skb;

	u32 tx_count;
	u16 tx_msdu_len;

	u8 tx_rate_mode;
	u8 tx_rate_idx;
	u8 tx_rate_nss;
	u8 tx_rate_sgi;
	u8 tx_rate_ldpc;

	u8 tx_antenna_mask;

	u32 freq_offset;

	u8 tx_power[4];
	u8 tx_power_control;

	const char *mtd_name;
	u32 mtd_offset;

	u32 tx_pending;
	u32 tx_queued;
	u32 tx_done;
	struct {
		u64 packets[__MT_RXQ_MAX];
		u64 fcs_error[__MT_RXQ_MAX];
	} rx_stats;
};

struct mt76_phy {
	struct ieee80211_hw *hw;
	struct mt76_dev *dev;
	void *priv;

	unsigned long state;

	struct cfg80211_chan_def chandef;
	struct ieee80211_channel *main_chan;

	struct mt76_channel_state *chan_state;
	ktime_t survey_time;

	struct mt76_sband sband_2g;
	struct mt76_sband sband_5g;

	u32 vif_mask;

	int txpower_cur;
	u8 antenna_mask;
};

struct mt76_dev {
	struct mt76_phy phy; /* must be first */

	struct mt76_phy *phy2;

	struct ieee80211_hw *hw;

	spinlock_t lock;
	spinlock_t cc_lock;

	u32 cur_cc_bss_rx;

	struct mt76_rx_status rx_ampdu_status;
	u32 rx_ampdu_len;
	u32 rx_ampdu_ref;

	struct mutex mutex;

	const struct mt76_bus_ops *bus;
	const struct mt76_driver_ops *drv;
	const struct mt76_mcu_ops *mcu_ops;
	struct device *dev;

	struct mt76_mcu mcu;

	struct net_device napi_dev;
	spinlock_t rx_lock;
	struct napi_struct napi[__MT_RXQ_MAX];
	struct sk_buff_head rx_skb[__MT_RXQ_MAX];

	struct list_head txwi_cache;
	struct mt76_queue *q_tx[2 * __MT_TXQ_MAX];
	struct mt76_queue q_rx[__MT_RXQ_MAX];
	const struct mt76_queue_ops *queue_ops;
	int tx_dma_idx[4];

	struct mt76_worker tx_worker;
	struct napi_struct tx_napi;
	struct delayed_work mac_work;

	wait_queue_head_t tx_wait;
	struct sk_buff_head status_list;

	u32 wcid_mask[DIV_ROUND_UP(MT76_N_WCIDS, 32)];
	u32 wcid_phy_mask[DIV_ROUND_UP(MT76_N_WCIDS, 32)];

	struct mt76_wcid global_wcid;
	struct mt76_wcid __rcu *wcid[MT76_N_WCIDS];

	u8 macaddr[ETH_ALEN];
	u32 rev;

	u32 aggr_stats[32];

	struct tasklet_struct pre_tbtt_tasklet;
	int beacon_int;
	u8 beacon_mask;

	struct debugfs_blob_wrapper eeprom;
	struct debugfs_blob_wrapper otp;
	struct mt76_hw_cap cap;

	struct mt76_rate_power rate_power;

	enum nl80211_dfs_regions region;

	u32 debugfs_reg;

	struct led_classdev led_cdev;
	char led_name[32];
	bool led_al;
	u8 led_pin;

	u8 csa_complete;

	u32 rxfilter;

#ifdef CONFIG_NL80211_TESTMODE
	const struct mt76_testmode_ops *test_ops;
	struct mt76_testmode_data test;
#endif

	struct workqueue_struct *wq;

	union {
		struct mt76_mmio mmio;
		struct mt76_usb usb;
		struct mt76_sdio sdio;
	};
};

enum mt76_phy_type {
	MT_PHY_TYPE_CCK,
	MT_PHY_TYPE_OFDM,
	MT_PHY_TYPE_HT,
	MT_PHY_TYPE_HT_GF,
	MT_PHY_TYPE_VHT,
	MT_PHY_TYPE_HE_SU = 8,
	MT_PHY_TYPE_HE_EXT_SU,
	MT_PHY_TYPE_HE_TB,
	MT_PHY_TYPE_HE_MU,
};

#define __mt76_rr(dev, ...)	(dev)->bus->rr((dev), __VA_ARGS__)
#define __mt76_wr(dev, ...)	(dev)->bus->wr((dev), __VA_ARGS__)
#define __mt76_rmw(dev, ...)	(dev)->bus->rmw((dev), __VA_ARGS__)
#define __mt76_wr_copy(dev, ...)	(dev)->bus->write_copy((dev), __VA_ARGS__)
#define __mt76_rr_copy(dev, ...)	(dev)->bus->read_copy((dev), __VA_ARGS__)

#define __mt76_set(dev, offset, val)	__mt76_rmw(dev, offset, 0, val)
#define __mt76_clear(dev, offset, val)	__mt76_rmw(dev, offset, val, 0)

#define mt76_rr(dev, ...)	(dev)->mt76.bus->rr(&((dev)->mt76), __VA_ARGS__)
#define mt76_wr(dev, ...)	(dev)->mt76.bus->wr(&((dev)->mt76), __VA_ARGS__)
#define mt76_rmw(dev, ...)	(dev)->mt76.bus->rmw(&((dev)->mt76), __VA_ARGS__)
#define mt76_wr_copy(dev, ...)	(dev)->mt76.bus->write_copy(&((dev)->mt76), __VA_ARGS__)
#define mt76_rr_copy(dev, ...)	(dev)->mt76.bus->read_copy(&((dev)->mt76), __VA_ARGS__)
#define mt76_wr_rp(dev, ...)	(dev)->mt76.bus->wr_rp(&((dev)->mt76), __VA_ARGS__)
#define mt76_rd_rp(dev, ...)	(dev)->mt76.bus->rd_rp(&((dev)->mt76), __VA_ARGS__)

#define mt76_mcu_send_msg(dev, ...)	(dev)->mt76.mcu_ops->mcu_send_msg(&((dev)->mt76), __VA_ARGS__)

#define __mt76_mcu_send_msg(dev, ...)	(dev)->mcu_ops->mcu_send_msg((dev), __VA_ARGS__)
#define __mt76_mcu_skb_send_msg(dev, ...)	(dev)->mcu_ops->mcu_skb_send_msg((dev), __VA_ARGS__)
#define mt76_mcu_restart(dev, ...)	(dev)->mt76.mcu_ops->mcu_restart(&((dev)->mt76))
#define __mt76_mcu_restart(dev, ...)	(dev)->mcu_ops->mcu_restart((dev))

#define mt76_set(dev, offset, val)	mt76_rmw(dev, offset, 0, val)
#define mt76_clear(dev, offset, val)	mt76_rmw(dev, offset, val, 0)

#define mt76_get_field(_dev, _reg, _field)		\
	FIELD_GET(_field, mt76_rr(dev, _reg))

#define mt76_rmw_field(_dev, _reg, _field, _val)	\
	mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

#define __mt76_rmw_field(_dev, _reg, _field, _val)	\
	__mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

#define mt76_hw(dev) (dev)->mphy.hw

static inline struct ieee80211_hw *
mt76_wcid_hw(struct mt76_dev *dev, u16 wcid)
{
	if (wcid <= MT76_N_WCIDS &&
	    mt76_wcid_mask_test(dev->wcid_phy_mask, wcid))
		return dev->phy2->hw;

	return dev->phy.hw;
}

bool __mt76_poll(struct mt76_dev *dev, u32 offset, u32 mask, u32 val,
		 int timeout);

#define mt76_poll(dev, ...) __mt76_poll(&((dev)->mt76), __VA_ARGS__)

bool __mt76_poll_msec(struct mt76_dev *dev, u32 offset, u32 mask, u32 val,
		      int timeout);

#define mt76_poll_msec(dev, ...) __mt76_poll_msec(&((dev)->mt76), __VA_ARGS__)

void mt76_mmio_init(struct mt76_dev *dev, void __iomem *regs);
void mt76_pci_disable_aspm(struct pci_dev *pdev);

static inline u16 mt76_chip(struct mt76_dev *dev)
{
	return dev->rev >> 16;
}

static inline u16 mt76_rev(struct mt76_dev *dev)
{
	return dev->rev & 0xffff;
}

#define mt76xx_chip(dev) mt76_chip(&((dev)->mt76))
#define mt76xx_rev(dev) mt76_rev(&((dev)->mt76))

#define mt76_init_queues(dev)		(dev)->mt76.queue_ops->init(&((dev)->mt76))
#define mt76_queue_alloc(dev, ...)	(dev)->mt76.queue_ops->alloc(&((dev)->mt76), __VA_ARGS__)
#define mt76_tx_queue_skb_raw(dev, ...)	(dev)->mt76.queue_ops->tx_queue_skb_raw(&((dev)->mt76), __VA_ARGS__)
#define mt76_tx_queue_skb(dev, ...)	(dev)->mt76.queue_ops->tx_queue_skb(&((dev)->mt76), __VA_ARGS__)
#define mt76_queue_rx_reset(dev, ...)	(dev)->mt76.queue_ops->rx_reset(&((dev)->mt76), __VA_ARGS__)
#define mt76_queue_tx_cleanup(dev, ...)	(dev)->mt76.queue_ops->tx_cleanup(&((dev)->mt76), __VA_ARGS__)
#define mt76_queue_kick(dev, ...)	(dev)->mt76.queue_ops->kick(&((dev)->mt76), __VA_ARGS__)

#define mt76_for_each_q_rx(dev, i)	\
	for (i = 0; i < ARRAY_SIZE((dev)->q_rx) && \
		    (dev)->q_rx[i].ndesc; i++)

struct mt76_dev *mt76_alloc_device(struct device *pdev, unsigned int size,
				   const struct ieee80211_ops *ops,
				   const struct mt76_driver_ops *drv_ops);
int mt76_register_device(struct mt76_dev *dev, bool vht,
			 struct ieee80211_rate *rates, int n_rates);
void mt76_unregister_device(struct mt76_dev *dev);
void mt76_free_device(struct mt76_dev *dev);
void mt76_unregister_phy(struct mt76_phy *phy);

struct mt76_phy *mt76_alloc_phy(struct mt76_dev *dev, unsigned int size,
				const struct ieee80211_ops *ops);
int mt76_register_phy(struct mt76_phy *phy);

struct dentry *mt76_register_debugfs(struct mt76_dev *dev);
int mt76_queues_read(struct seq_file *s, void *data);
void mt76_seq_puts_array(struct seq_file *file, const char *str,
			 s8 *val, int len);

int mt76_eeprom_init(struct mt76_dev *dev, int len);
void mt76_eeprom_override(struct mt76_dev *dev);

static inline struct mt76_phy *
mt76_dev_phy(struct mt76_dev *dev, bool phy_ext)
{
	if (phy_ext && dev->phy2)
		return dev->phy2;
	return &dev->phy;
}

static inline struct ieee80211_hw *
mt76_phy_hw(struct mt76_dev *dev, bool phy_ext)
{
	return mt76_dev_phy(dev, phy_ext)->hw;
}

static inline u8 *
mt76_get_txwi_ptr(struct mt76_dev *dev, struct mt76_txwi_cache *t)
{
	return (u8 *)t - dev->drv->txwi_size;
}

/* increment with wrap-around */
static inline int mt76_incr(int val, int size)
{
	return (val + 1) & (size - 1);
}

/* decrement with wrap-around */
static inline int mt76_decr(int val, int size)
{
	return (val - 1) & (size - 1);
}

u8 mt76_ac_to_hwq(u8 ac);

static inline struct ieee80211_txq *
mtxq_to_txq(struct mt76_txq *mtxq)
{
	void *ptr = mtxq;

	return container_of(ptr, struct ieee80211_txq, drv_priv);
}

static inline struct ieee80211_sta *
wcid_to_sta(struct mt76_wcid *wcid)
{
	void *ptr = wcid;

	if (!wcid || !wcid->sta)
		return NULL;

	return container_of(ptr, struct ieee80211_sta, drv_priv);
}

static inline struct mt76_tx_cb *mt76_tx_skb_cb(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct mt76_tx_cb) >
		     sizeof(IEEE80211_SKB_CB(skb)->status.status_driver_data));
	return ((void *)IEEE80211_SKB_CB(skb)->status.status_driver_data);
}

static inline void *mt76_skb_get_hdr(struct sk_buff *skb)
{
	struct mt76_rx_status mstat;
	u8 *data = skb->data;

	/* Alignment concerns */
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_he) % 4);
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_he_mu) % 4);

	mstat = *((struct mt76_rx_status *)skb->cb);

	if (mstat.flag & RX_FLAG_RADIOTAP_HE)
		data += sizeof(struct ieee80211_radiotap_he);
	if (mstat.flag & RX_FLAG_RADIOTAP_HE_MU)
		data += sizeof(struct ieee80211_radiotap_he_mu);

	return data;
}

static inline void mt76_insert_hdr_pad(struct sk_buff *skb)
{
	int len = ieee80211_get_hdrlen_from_skb(skb);

	if (len % 4 == 0)
		return;

	skb_push(skb, 2);
	memmove(skb->data, skb->data + 2, len);

	skb->data[len] = 0;
	skb->data[len + 1] = 0;
}

static inline bool mt76_is_skb_pktid(u8 pktid)
{
	if (pktid & MT_PACKET_ID_HAS_RATE)
		return false;

	return pktid >= MT_PACKET_ID_FIRST;
}

static inline u8 mt76_tx_power_nss_delta(u8 nss)
{
	static const u8 nss_delta[4] = { 0, 6, 9, 12 };

	return nss_delta[nss - 1];
}

static inline bool mt76_testmode_enabled(struct mt76_dev *dev)
{
#ifdef CONFIG_NL80211_TESTMODE
	return dev->test.state != MT76_TM_STATE_OFF;
#else
	return false;
#endif
}

void mt76_rx(struct mt76_dev *dev, enum mt76_rxq_id q, struct sk_buff *skb);
void mt76_tx(struct mt76_phy *dev, struct ieee80211_sta *sta,
	     struct mt76_wcid *wcid, struct sk_buff *skb);
void mt76_txq_init(struct mt76_dev *dev, struct ieee80211_txq *txq);
void mt76_txq_remove(struct mt76_dev *dev, struct ieee80211_txq *txq);
void mt76_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
void mt76_stop_tx_queues(struct mt76_dev *dev, struct ieee80211_sta *sta,
			 bool send_bar);
void mt76_tx_check_agg_ssn(struct ieee80211_sta *sta, struct sk_buff *skb);
void mt76_txq_schedule(struct mt76_phy *phy, enum mt76_txq_id qid);
void mt76_txq_schedule_all(struct mt76_phy *phy);
void mt76_tx_worker(struct mt76_worker *w);
void mt76_release_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta,
				  u16 tids, int nframes,
				  enum ieee80211_frame_release_type reason,
				  bool more_data);
bool mt76_has_tx_pending(struct mt76_phy *phy);
void mt76_set_channel(struct mt76_phy *phy);
void mt76_update_survey(struct mt76_dev *dev);
void mt76_update_survey_active_time(struct mt76_phy *phy, ktime_t time);
int mt76_get_survey(struct ieee80211_hw *hw, int idx,
		    struct survey_info *survey);
void mt76_set_stream_caps(struct mt76_phy *phy, bool vht);

int mt76_rx_aggr_start(struct mt76_dev *dev, struct mt76_wcid *wcid, u8 tid,
		       u16 ssn, u16 size);
void mt76_rx_aggr_stop(struct mt76_dev *dev, struct mt76_wcid *wcid, u8 tid);

void mt76_wcid_key_setup(struct mt76_dev *dev, struct mt76_wcid *wcid,
			 struct ieee80211_key_conf *key);

void mt76_tx_status_lock(struct mt76_dev *dev, struct sk_buff_head *list)
			 __acquires(&dev->status_list.lock);
void mt76_tx_status_unlock(struct mt76_dev *dev, struct sk_buff_head *list)
			   __releases(&dev->status_list.lock);

int mt76_tx_status_skb_add(struct mt76_dev *dev, struct mt76_wcid *wcid,
			   struct sk_buff *skb);
struct sk_buff *mt76_tx_status_skb_get(struct mt76_dev *dev,
				       struct mt76_wcid *wcid, int pktid,
				       struct sk_buff_head *list);
void mt76_tx_status_skb_done(struct mt76_dev *dev, struct sk_buff *skb,
			     struct sk_buff_head *list);
void mt76_tx_complete_skb(struct mt76_dev *dev, u16 wcid, struct sk_buff *skb);
void mt76_tx_status_check(struct mt76_dev *dev, struct mt76_wcid *wcid,
			  bool flush);
int mt76_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta,
		   enum ieee80211_sta_state old_state,
		   enum ieee80211_sta_state new_state);
void __mt76_sta_remove(struct mt76_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt76_sta_pre_rcu_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta);

int mt76_get_min_avg_rssi(struct mt76_dev *dev, bool ext_phy);

int mt76_get_txpower(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		     int *dbm);

void mt76_csa_check(struct mt76_dev *dev);
void mt76_csa_finish(struct mt76_dev *dev);

int mt76_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant);
int mt76_set_tim(struct ieee80211_hw *hw, struct ieee80211_sta *sta, bool set);
void mt76_insert_ccmp_hdr(struct sk_buff *skb, u8 key_id);
int mt76_get_rate(struct mt76_dev *dev,
		  struct ieee80211_supported_band *sband,
		  int idx, bool cck);
void mt76_sw_scan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  const u8 *mac);
void mt76_sw_scan_complete(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif);
int mt76_testmode_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      void *data, int len);
int mt76_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *skb,
		       struct netlink_callback *cb, void *data, int len);
int mt76_testmode_set_state(struct mt76_dev *dev, enum mt76_testmode_state state);

static inline void mt76_testmode_reset(struct mt76_dev *dev, bool disable)
{
#ifdef CONFIG_NL80211_TESTMODE
	enum mt76_testmode_state state = MT76_TM_STATE_IDLE;

	if (disable || dev->test.state == MT76_TM_STATE_OFF)
		state = MT76_TM_STATE_OFF;

	mt76_testmode_set_state(dev, state);
#endif
}


/* internal */
static inline struct ieee80211_hw *
mt76_tx_status_get_hw(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = dev->phy.hw;

	if ((info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY) && dev->phy2)
		hw = dev->phy2->hw;

	info->hw_queue &= ~MT_TX_HW_QUEUE_EXT_PHY;

	return hw;
}

void mt76_tx_free(struct mt76_dev *dev);
struct mt76_txwi_cache *mt76_get_txwi(struct mt76_dev *dev);
void mt76_put_txwi(struct mt76_dev *dev, struct mt76_txwi_cache *t);
void mt76_rx_complete(struct mt76_dev *dev, struct sk_buff_head *frames,
		      struct napi_struct *napi);
void mt76_rx_poll_complete(struct mt76_dev *dev, enum mt76_rxq_id q,
			   struct napi_struct *napi);
void mt76_rx_aggr_reorder(struct sk_buff *skb, struct sk_buff_head *frames);
void mt76_testmode_tx_pending(struct mt76_dev *dev);
void mt76_queue_tx_complete(struct mt76_dev *dev, struct mt76_queue *q,
			    struct mt76_queue_entry *e);

/* usb */
static inline bool mt76u_urb_error(struct urb *urb)
{
	return urb->status &&
	       urb->status != -ECONNRESET &&
	       urb->status != -ESHUTDOWN &&
	       urb->status != -ENOENT;
}

/* Map hardware queues to usb endpoints */
static inline u8 q2ep(u8 qid)
{
	/* TODO: take management packets to queue 5 */
	return qid + 1;
}

static inline int
mt76u_bulk_msg(struct mt76_dev *dev, void *data, int len, int *actual_len,
	       int timeout, int ep)
{
	struct usb_interface *uintf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(uintf);
	struct mt76_usb *usb = &dev->usb;
	unsigned int pipe;

	if (actual_len)
		pipe = usb_rcvbulkpipe(udev, usb->in_ep[ep]);
	else
		pipe = usb_sndbulkpipe(udev, usb->out_ep[ep]);

	return usb_bulk_msg(udev, pipe, data, len, actual_len, timeout);
}

int mt76_skb_adjust_pad(struct sk_buff *skb);
int mt76u_vendor_request(struct mt76_dev *dev, u8 req,
			 u8 req_type, u16 val, u16 offset,
			 void *buf, size_t len);
void mt76u_single_wr(struct mt76_dev *dev, const u8 req,
		     const u16 offset, const u32 val);
int mt76u_init(struct mt76_dev *dev, struct usb_interface *intf,
	       bool ext);
int mt76u_alloc_mcu_queue(struct mt76_dev *dev);
int mt76u_alloc_queues(struct mt76_dev *dev);
void mt76u_stop_tx(struct mt76_dev *dev);
void mt76u_stop_rx(struct mt76_dev *dev);
int mt76u_resume_rx(struct mt76_dev *dev);
void mt76u_queues_deinit(struct mt76_dev *dev);

int mt76s_init(struct mt76_dev *dev, struct sdio_func *func,
	       const struct mt76_bus_ops *bus_ops);
int mt76s_alloc_queues(struct mt76_dev *dev);
void mt76s_stop_txrx(struct mt76_dev *dev);
void mt76s_deinit(struct mt76_dev *dev);

struct sk_buff *
mt76_mcu_msg_alloc(struct mt76_dev *dev, const void *data,
		   int data_len);
void mt76_mcu_rx_event(struct mt76_dev *dev, struct sk_buff *skb);
struct sk_buff *mt76_mcu_get_response(struct mt76_dev *dev,
				      unsigned long expires);

void mt76_set_irq_mask(struct mt76_dev *dev, u32 addr, u32 clear, u32 set);

#endif
