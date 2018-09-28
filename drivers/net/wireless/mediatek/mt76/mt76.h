/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76_H
#define __MT76_H

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/leds.h>
#include <linux/usb.h>
#include <net/mac80211.h>
#include "util.h"

#define MT_TX_RING_SIZE     256
#define MT_MCU_RING_SIZE    32
#define MT_RX_BUF_SIZE      2048

struct mt76_dev;
struct mt76_wcid;

struct mt76_reg_pair {
	u32 reg;
	u32 value;
};

struct mt76_bus_ops {
	u32 (*rr)(struct mt76_dev *dev, u32 offset);
	void (*wr)(struct mt76_dev *dev, u32 offset, u32 val);
	u32 (*rmw)(struct mt76_dev *dev, u32 offset, u32 mask, u32 val);
	void (*copy)(struct mt76_dev *dev, u32 offset, const void *data,
		     int len);
	int (*wr_rp)(struct mt76_dev *dev, u32 base,
		     const struct mt76_reg_pair *rp, int len);
	int (*rd_rp)(struct mt76_dev *dev, u32 base,
		     struct mt76_reg_pair *rp, int len);
};

enum mt76_txq_id {
	MT_TXQ_VO = IEEE80211_AC_VO,
	MT_TXQ_VI = IEEE80211_AC_VI,
	MT_TXQ_BE = IEEE80211_AC_BE,
	MT_TXQ_BK = IEEE80211_AC_BK,
	MT_TXQ_PSD,
	MT_TXQ_MCU,
	MT_TXQ_BEACON,
	MT_TXQ_CAB,
	__MT_TXQ_MAX
};

enum mt76_rxq_id {
	MT_RXQ_MAIN,
	MT_RXQ_MCU,
	__MT_RXQ_MAX
};

struct mt76_queue_buf {
	dma_addr_t addr;
	int len;
};

struct mt76u_buf {
	struct mt76_dev *dev;
	struct urb *urb;
	size_t len;
	bool done;
};

struct mt76_queue_entry {
	union {
		void *buf;
		struct sk_buff *skb;
	};
	union {
		struct mt76_txwi_cache *txwi;
		struct mt76u_buf ubuf;
	};
	bool schedule;
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

	struct list_head swq;
	int swq_queued;

	u16 first;
	u16 head;
	u16 tail;
	int ndesc;
	int queued;
	int buf_size;

	u8 buf_offset;
	u8 hw_idx;

	dma_addr_t desc_dma;
	struct sk_buff *rx_head;
	struct page_frag_cache rx_page;
	spinlock_t rx_page_lock;
};

struct mt76_mcu_ops {
	struct sk_buff *(*mcu_msg_alloc)(const void *data, int len);
	int (*mcu_send_msg)(struct mt76_dev *dev, struct sk_buff *skb,
			    int cmd, bool wait_resp);
	int (*mcu_wr_rp)(struct mt76_dev *dev, u32 base,
			 const struct mt76_reg_pair *rp, int len);
	int (*mcu_rd_rp)(struct mt76_dev *dev, u32 base,
			 struct mt76_reg_pair *rp, int len);
};

struct mt76_queue_ops {
	int (*init)(struct mt76_dev *dev);

	int (*alloc)(struct mt76_dev *dev, struct mt76_queue *q);

	int (*add_buf)(struct mt76_dev *dev, struct mt76_queue *q,
		       struct mt76_queue_buf *buf, int nbufs, u32 info,
		       struct sk_buff *skb, void *txwi);

	int (*tx_queue_skb)(struct mt76_dev *dev, struct mt76_queue *q,
			    struct sk_buff *skb, struct mt76_wcid *wcid,
			    struct ieee80211_sta *sta);

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

#define MT76_N_WCIDS 128

struct mt76_wcid {
	struct mt76_rx_tid __rcu *aggr[IEEE80211_NUM_TIDS];

	struct work_struct aggr_work;

	unsigned long flags;

	u8 idx;
	u8 hw_key_idx;

	u8 sta:1;

	u8 rx_check_pn;
	u8 rx_key_pn[IEEE80211_NUM_TIDS][6];

	__le16 tx_rate;
	bool tx_rate_set;
	u8 tx_rate_nss;
	s8 max_txpwr_adj;
	bool sw_iv;
};

struct mt76_txq {
	struct list_head list;
	struct mt76_queue *hwq;
	struct mt76_wcid *wcid;

	struct sk_buff_head retry_q;

	u16 agg_ssn;
	bool send_bar;
	bool aggr;
};

struct mt76_txwi_cache {
	u32 txwi[8];
	dma_addr_t dma_addr;
	struct list_head list;
};


struct mt76_rx_tid {
	struct rcu_head rcu_head;

	struct mt76_dev *dev;

	spinlock_t lock;
	struct delayed_work reorder_work;

	u16 head;
	u8 size;
	u8 nframes;

	u8 started:1, stopped:1, timer_pending:1;

	struct sk_buff *reorder_buf[];
};

enum {
	MT76_STATE_INITIALIZED,
	MT76_STATE_RUNNING,
	MT76_STATE_MCU_RUNNING,
	MT76_SCANNING,
	MT76_RESET,
	MT76_OFFCHANNEL,
	MT76_REMOVED,
	MT76_READING_STATS,
};

struct mt76_hw_cap {
	bool has_2ghz;
	bool has_5ghz;
};

struct mt76_driver_ops {
	u16 txwi_size;

	void (*update_survey)(struct mt76_dev *dev);

	int (*tx_prepare_skb)(struct mt76_dev *dev, void *txwi_ptr,
			      struct sk_buff *skb, struct mt76_queue *q,
			      struct mt76_wcid *wcid,
			      struct ieee80211_sta *sta, u32 *tx_info);

	void (*tx_complete_skb)(struct mt76_dev *dev, struct mt76_queue *q,
				struct mt76_queue_entry *e, bool flush);

	bool (*tx_status_data)(struct mt76_dev *dev, u8 *update);

	void (*rx_skb)(struct mt76_dev *dev, enum mt76_rxq_id q,
		       struct sk_buff *skb);

	void (*rx_poll_complete)(struct mt76_dev *dev, enum mt76_rxq_id q);

	void (*sta_ps)(struct mt76_dev *dev, struct ieee80211_sta *sta,
		       bool ps);
	s8 (*get_max_txpwr_adj)(struct mt76_dev *dev,
				const struct ieee80211_tx_rate *rate);
};

struct mt76_channel_state {
	u64 cc_active;
	u64 cc_busy;
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
	MT_VEND_MULTI_WRITE =	0x6,
	MT_VEND_MULTI_READ =	0x7,
	MT_VEND_READ_EEPROM =	0x9,
	MT_VEND_WRITE_FCE =	0x42,
	MT_VEND_WRITE_CFG =	0x46,
	MT_VEND_READ_CFG =	0x47,
};

enum mt76u_in_ep {
	MT_EP_IN_PKT_RX,
	MT_EP_IN_CMD_RESP,
	__MT_EP_IN_MAX,
};

enum mt76u_out_ep {
	MT_EP_OUT_INBAND_CMD,
	MT_EP_OUT_AC_BK,
	MT_EP_OUT_AC_BE,
	MT_EP_OUT_AC_VI,
	MT_EP_OUT_AC_VO,
	MT_EP_OUT_HCCA,
	__MT_EP_OUT_MAX,
};

#define MT_SG_MAX_SIZE		8
#define MT_NUM_TX_ENTRIES	256
#define MT_NUM_RX_ENTRIES	128
#define MCU_RESP_URB_SIZE	1024
struct mt76_usb {
	struct mutex usb_ctrl_mtx;
	u8 data[32];

	struct tasklet_struct rx_tasklet;
	struct tasklet_struct tx_tasklet;
	struct delayed_work stat_work;

	u8 out_ep[__MT_EP_OUT_MAX];
	u16 out_max_packet;
	u8 in_ep[__MT_EP_IN_MAX];
	u16 in_max_packet;

	struct mt76u_mcu {
		struct mutex mutex;
		struct completion cmpl;
		struct mt76u_buf res;
		u32 msg_seq;

		/* multiple reads */
		struct mt76_reg_pair *rp;
		int rp_len;
		u32 base;
		bool burst;
	} mcu;
};

struct mt76_mmio {
	struct mt76e_mcu {
		struct mutex mutex;

		wait_queue_head_t wait;
		struct sk_buff_head res_q;

		u32 msg_seq;
	} mcu;
	void __iomem *regs;
	spinlock_t irq_lock;
	u32 irqmask;
};

struct mt76_dev {
	struct ieee80211_hw *hw;
	struct cfg80211_chan_def chandef;
	struct ieee80211_channel *main_chan;

	spinlock_t lock;
	spinlock_t cc_lock;

	struct mutex mutex;

	const struct mt76_bus_ops *bus;
	const struct mt76_driver_ops *drv;
	const struct mt76_mcu_ops *mcu_ops;
	struct device *dev;

	struct net_device napi_dev;
	spinlock_t rx_lock;
	struct napi_struct napi[__MT_RXQ_MAX];
	struct sk_buff_head rx_skb[__MT_RXQ_MAX];

	struct list_head txwi_cache;
	struct mt76_queue q_tx[__MT_TXQ_MAX];
	struct mt76_queue q_rx[__MT_RXQ_MAX];
	const struct mt76_queue_ops *queue_ops;

	wait_queue_head_t tx_wait;

	unsigned long wcid_mask[MT76_N_WCIDS / BITS_PER_LONG];

	struct mt76_wcid global_wcid;
	struct mt76_wcid __rcu *wcid[MT76_N_WCIDS];

	u8 macaddr[ETH_ALEN];
	u32 rev;
	unsigned long state;

	u8 antenna_mask;

	struct mt76_sband sband_2g;
	struct mt76_sband sband_5g;
	struct debugfs_blob_wrapper eeprom;
	struct debugfs_blob_wrapper otp;
	struct mt76_hw_cap cap;

	struct mt76_rate_power rate_power;
	int txpower_conf;
	int txpower_cur;

	u32 debugfs_reg;

	struct led_classdev led_cdev;
	char led_name[32];
	bool led_al;
	u8 led_pin;

	u32 rxfilter;

	union {
		struct mt76_mmio mmio;
		struct mt76_usb usb;
	};
};

enum mt76_phy_type {
	MT_PHY_TYPE_CCK,
	MT_PHY_TYPE_OFDM,
	MT_PHY_TYPE_HT,
	MT_PHY_TYPE_HT_GF,
	MT_PHY_TYPE_VHT,
};

struct mt76_rx_status {
	struct mt76_wcid *wcid;

	unsigned long reorder_time;

	u8 iv[6];

	u8 aggr:1;
	u8 tid;
	u16 seqno;

	u16 freq;
	u32 flag;
	u8 enc_flags;
	u8 encoding:2, bw:3;
	u8 rate_idx;
	u8 nss;
	u8 band;
	u8 signal;
	u8 chains;
	s8 chain_signal[IEEE80211_MAX_CHAINS];
};

#define __mt76_rr(dev, ...)	(dev)->bus->rr((dev), __VA_ARGS__)
#define __mt76_wr(dev, ...)	(dev)->bus->wr((dev), __VA_ARGS__)
#define __mt76_rmw(dev, ...)	(dev)->bus->rmw((dev), __VA_ARGS__)
#define __mt76_wr_copy(dev, ...)	(dev)->bus->copy((dev), __VA_ARGS__)

#define __mt76_set(dev, offset, val)	__mt76_rmw(dev, offset, 0, val)
#define __mt76_clear(dev, offset, val)	__mt76_rmw(dev, offset, val, 0)

#define mt76_rr(dev, ...)	(dev)->mt76.bus->rr(&((dev)->mt76), __VA_ARGS__)
#define mt76_wr(dev, ...)	(dev)->mt76.bus->wr(&((dev)->mt76), __VA_ARGS__)
#define mt76_rmw(dev, ...)	(dev)->mt76.bus->rmw(&((dev)->mt76), __VA_ARGS__)
#define mt76_wr_copy(dev, ...)	(dev)->mt76.bus->copy(&((dev)->mt76), __VA_ARGS__)
#define mt76_wr_rp(dev, ...)	(dev)->mt76.bus->wr_rp(&((dev)->mt76), __VA_ARGS__)
#define mt76_rd_rp(dev, ...)	(dev)->mt76.bus->rd_rp(&((dev)->mt76), __VA_ARGS__)

#define mt76_mcu_msg_alloc(dev, ...)	(dev)->mt76.mcu_ops->mcu_msg_alloc(__VA_ARGS__)
#define mt76_mcu_send_msg(dev, ...)	(dev)->mt76.mcu_ops->mcu_send_msg(&((dev)->mt76), __VA_ARGS__)

#define mt76_set(dev, offset, val)	mt76_rmw(dev, offset, 0, val)
#define mt76_clear(dev, offset, val)	mt76_rmw(dev, offset, val, 0)

#define mt76_get_field(_dev, _reg, _field)		\
	FIELD_GET(_field, mt76_rr(dev, _reg))

#define mt76_rmw_field(_dev, _reg, _field, _val)	\
	mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

#define __mt76_rmw_field(_dev, _reg, _field, _val)	\
	__mt76_rmw(_dev, _reg, _field, FIELD_PREP(_field, _val))

#define mt76_hw(dev) (dev)->mt76.hw

bool __mt76_poll(struct mt76_dev *dev, u32 offset, u32 mask, u32 val,
		 int timeout);

#define mt76_poll(dev, ...) __mt76_poll(&((dev)->mt76), __VA_ARGS__)

bool __mt76_poll_msec(struct mt76_dev *dev, u32 offset, u32 mask, u32 val,
		      int timeout);

#define mt76_poll_msec(dev, ...) __mt76_poll_msec(&((dev)->mt76), __VA_ARGS__)

void mt76_mmio_init(struct mt76_dev *dev, void __iomem *regs);

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

#define __mt76_init_queues(dev)		(dev)->queue_ops->init((dev))
#define __mt76_queue_alloc(dev, ...)	(dev)->queue_ops->alloc((dev), __VA_ARGS__)
#define mt76_queue_add_buf(dev, ...)	(dev)->mt76.queue_ops->add_buf(&((dev)->mt76), __VA_ARGS__)
#define mt76_queue_rx_reset(dev, ...)	(dev)->mt76.queue_ops->rx_reset(&((dev)->mt76), __VA_ARGS__)
#define mt76_queue_tx_cleanup(dev, ...)	(dev)->mt76.queue_ops->tx_cleanup(&((dev)->mt76), __VA_ARGS__)
#define mt76_queue_kick(dev, ...)	(dev)->mt76.queue_ops->kick(&((dev)->mt76), __VA_ARGS__)

static inline struct mt76_channel_state *
mt76_channel_state(struct mt76_dev *dev, struct ieee80211_channel *c)
{
	struct mt76_sband *msband;
	int idx;

	if (c->band == NL80211_BAND_2GHZ)
		msband = &dev->sband_2g;
	else
		msband = &dev->sband_5g;

	idx = c - &msband->sband.channels[0];
	return &msband->chan[idx];
}

struct mt76_dev *mt76_alloc_device(unsigned int size,
				   const struct ieee80211_ops *ops);
int mt76_register_device(struct mt76_dev *dev, bool vht,
			 struct ieee80211_rate *rates, int n_rates);
void mt76_unregister_device(struct mt76_dev *dev);

struct dentry *mt76_register_debugfs(struct mt76_dev *dev);
void mt76_seq_puts_array(struct seq_file *file, const char *str,
			 s8 *val, int len);

int mt76_eeprom_init(struct mt76_dev *dev, int len);
void mt76_eeprom_override(struct mt76_dev *dev);

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

int mt76_dma_tx_queue_skb(struct mt76_dev *dev, struct mt76_queue *q,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta);

void mt76_rx(struct mt76_dev *dev, enum mt76_rxq_id q, struct sk_buff *skb);
void mt76_tx(struct mt76_dev *dev, struct ieee80211_sta *sta,
	     struct mt76_wcid *wcid, struct sk_buff *skb);
void mt76_txq_init(struct mt76_dev *dev, struct ieee80211_txq *txq);
void mt76_txq_remove(struct mt76_dev *dev, struct ieee80211_txq *txq);
void mt76_wake_tx_queue(struct ieee80211_hw *hw, struct ieee80211_txq *txq);
void mt76_stop_tx_queues(struct mt76_dev *dev, struct ieee80211_sta *sta,
			 bool send_bar);
void mt76_txq_schedule(struct mt76_dev *dev, struct mt76_queue *hwq);
void mt76_txq_schedule_all(struct mt76_dev *dev);
void mt76_release_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta,
				  u16 tids, int nframes,
				  enum ieee80211_frame_release_type reason,
				  bool more_data);
void mt76_set_channel(struct mt76_dev *dev);
int mt76_get_survey(struct ieee80211_hw *hw, int idx,
		    struct survey_info *survey);
void mt76_set_stream_caps(struct mt76_dev *dev, bool vht);

int mt76_rx_aggr_start(struct mt76_dev *dev, struct mt76_wcid *wcid, u8 tid,
		       u16 ssn, u8 size);
void mt76_rx_aggr_stop(struct mt76_dev *dev, struct mt76_wcid *wcid, u8 tid);

void mt76_wcid_key_setup(struct mt76_dev *dev, struct mt76_wcid *wcid,
			 struct ieee80211_key_conf *key);

struct ieee80211_sta *mt76_rx_convert(struct sk_buff *skb);

/* internal */
void mt76_tx_free(struct mt76_dev *dev);
struct mt76_txwi_cache *mt76_get_txwi(struct mt76_dev *dev);
void mt76_put_txwi(struct mt76_dev *dev, struct mt76_txwi_cache *t);
void mt76_rx_complete(struct mt76_dev *dev, struct sk_buff_head *frames,
		      struct napi_struct *napi);
void mt76_rx_poll_complete(struct mt76_dev *dev, enum mt76_rxq_id q,
			   struct napi_struct *napi);
void mt76_rx_aggr_reorder(struct sk_buff *skb, struct sk_buff_head *frames);

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

static inline bool mt76u_check_sg(struct mt76_dev *dev)
{
	struct usb_interface *intf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(intf);

	return (udev->bus->sg_tablesize > 0 &&
		(udev->bus->no_sg_constraint ||
		 udev->speed == USB_SPEED_WIRELESS));
}

int mt76u_vendor_request(struct mt76_dev *dev, u8 req,
			 u8 req_type, u16 val, u16 offset,
			 void *buf, size_t len);
void mt76u_single_wr(struct mt76_dev *dev, const u8 req,
		     const u16 offset, const u32 val);
u32 mt76u_rr(struct mt76_dev *dev, u32 addr);
void mt76u_wr(struct mt76_dev *dev, u32 addr, u32 val);
int mt76u_init(struct mt76_dev *dev, struct usb_interface *intf);
void mt76u_deinit(struct mt76_dev *dev);
int mt76u_buf_alloc(struct mt76_dev *dev, struct mt76u_buf *buf,
		    int nsgs, int len, int sglen, gfp_t gfp);
void mt76u_buf_free(struct mt76u_buf *buf);
int mt76u_submit_buf(struct mt76_dev *dev, int dir, int index,
		     struct mt76u_buf *buf, gfp_t gfp,
		     usb_complete_t complete_fn, void *context);
int mt76u_submit_rx_buffers(struct mt76_dev *dev);
int mt76u_alloc_queues(struct mt76_dev *dev);
void mt76u_stop_queues(struct mt76_dev *dev);
void mt76u_stop_stat_wk(struct mt76_dev *dev);
void mt76u_queues_deinit(struct mt76_dev *dev);

void mt76u_mcu_complete_urb(struct urb *urb);
int mt76u_mcu_init_rx(struct mt76_dev *dev);
void mt76u_mcu_deinit(struct mt76_dev *dev);

#endif
