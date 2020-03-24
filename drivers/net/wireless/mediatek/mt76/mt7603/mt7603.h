/* SPDX-License-Identifier: ISC */

#ifndef __MT7603_H
#define __MT7603_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include "../mt76.h"
#include "regs.h"

#define MT7603_MAX_INTERFACES	4
#define MT7603_WTBL_SIZE	128
#define MT7603_WTBL_RESERVED	(MT7603_WTBL_SIZE - 1)
#define MT7603_WTBL_STA		(MT7603_WTBL_RESERVED - MT7603_MAX_INTERFACES)

#define MT7603_RATE_RETRY	2

#define MT7603_MCU_RX_RING_SIZE	64
#define MT7603_RX_RING_SIZE     128

#define MT7603_FIRMWARE_E1	"mt7603_e1.bin"
#define MT7603_FIRMWARE_E2	"mt7603_e2.bin"
#define MT7628_FIRMWARE_E1	"mt7628_e1.bin"
#define MT7628_FIRMWARE_E2	"mt7628_e2.bin"

#define MT7603_EEPROM_SIZE	1024

#define MT_AGG_SIZE_LIMIT(_n)	(((_n) + 1) * 4)

#define MT7603_PRE_TBTT_TIME	5000 /* ms */

#define MT7603_WATCHDOG_TIME	100 /* ms */
#define MT7603_WATCHDOG_TIMEOUT	10 /* number of checks */

#define MT7603_EDCCA_BLOCK_TH	10

#define MT7603_CFEND_RATE_DEFAULT	0x69 /* chip default (24M) */
#define MT7603_CFEND_RATE_11B		0x03 /* 11B LP, 11M */

struct mt7603_vif;
struct mt7603_sta;

enum {
	MT7603_REV_E1 = 0x00,
	MT7603_REV_E2 = 0x10,
	MT7628_REV_E1 = 0x8a00,
};

enum mt7603_bw {
	MT_BW_20,
	MT_BW_40,
	MT_BW_80,
};

struct mt7603_rate_set {
	struct ieee80211_tx_rate probe_rate;
	struct ieee80211_tx_rate rates[4];
};

struct mt7603_sta {
	struct mt76_wcid wcid; /* must be first */

	struct mt7603_vif *vif;

	struct list_head poll_list;
	u32 tx_airtime_ac[4];

	struct sk_buff_head psq;

	struct ieee80211_tx_rate rates[4];

	struct mt7603_rate_set rateset[2];
	u32 rate_set_tsf;

	u8 rate_count;
	u8 n_rates;

	u8 rate_probe;
	u8 smps;

	u8 ps;
};

struct mt7603_vif {
	struct mt7603_sta sta; /* must be first */

	u8 idx;
};

enum mt7603_reset_cause {
	RESET_CAUSE_TX_HANG,
	RESET_CAUSE_TX_BUSY,
	RESET_CAUSE_RX_BUSY,
	RESET_CAUSE_BEACON_STUCK,
	RESET_CAUSE_RX_PSE_BUSY,
	RESET_CAUSE_MCU_HANG,
	RESET_CAUSE_RESET_FAILED,
	__RESET_CAUSE_MAX
};

struct mt7603_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	const struct mt76_bus_ops *bus_ops;

	u32 rxfilter;

	u8 vif_mask;

	struct list_head sta_poll_list;
	spinlock_t sta_poll_lock;

	struct mt7603_sta global_sta;

	u32 agc0, agc3;
	u32 false_cca_ofdm, false_cca_cck;
	unsigned long last_cca_adj;

	u32 ampdu_ref;
	__le32 rx_ampdu_ts;
	u8 rssi_offset[3];

	u8 slottime;
	s16 coverage_class;

	s8 tx_power_limit;

	ktime_t ed_time;

	spinlock_t ps_lock;

	u8 mac_work_count;

	u8 mcu_running;

	u8 ed_monitor_enabled;
	u8 ed_monitor;
	s8 ed_trigger;
	u8 ed_strict_mode;
	u8 ed_strong_signal;

	bool dynamic_sensitivity;
	s8 sensitivity;
	u8 sensitivity_limit;

	u8 beacon_check;
	u8 tx_hang_check;
	u8 tx_dma_check;
	u8 rx_dma_check;
	u8 rx_pse_check;
	u8 mcu_hang;

	enum mt7603_reset_cause cur_reset_cause;

	u16 tx_dma_idx[4];
	u16 rx_dma_idx;

	u32 reset_test;

	unsigned int reset_cause[__RESET_CAUSE_MAX];
};

extern const struct mt76_driver_ops mt7603_drv_ops;
extern const struct ieee80211_ops mt7603_ops;
extern struct pci_driver mt7603_pci_driver;
extern struct platform_driver mt76_wmac_driver;

static inline bool is_mt7603(struct mt7603_dev *dev)
{
	return mt76xx_chip(dev) == 0x7603;
}

static inline bool is_mt7628(struct mt7603_dev *dev)
{
	return mt76xx_chip(dev) == 0x7628;
}

/* need offset to prevent conflict with ampdu_ack_len */
#define MT_RATE_DRIVER_DATA_OFFSET	4

u32 mt7603_reg_map(struct mt7603_dev *dev, u32 addr);

irqreturn_t mt7603_irq_handler(int irq, void *dev_instance);

int mt7603_register_device(struct mt7603_dev *dev);
void mt7603_unregister_device(struct mt7603_dev *dev);
int mt7603_eeprom_init(struct mt7603_dev *dev);
int mt7603_dma_init(struct mt7603_dev *dev);
void mt7603_dma_cleanup(struct mt7603_dev *dev);
int mt7603_mcu_init(struct mt7603_dev *dev);
void mt7603_init_debugfs(struct mt7603_dev *dev);

static inline void mt7603_irq_enable(struct mt7603_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, 0, mask);
}

static inline void mt7603_irq_disable(struct mt7603_dev *dev, u32 mask)
{
	mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

void mt7603_mac_reset_counters(struct mt7603_dev *dev);
void mt7603_mac_dma_start(struct mt7603_dev *dev);
void mt7603_mac_start(struct mt7603_dev *dev);
void mt7603_mac_stop(struct mt7603_dev *dev);
void mt7603_mac_work(struct work_struct *work);
void mt7603_mac_set_timing(struct mt7603_dev *dev);
void mt7603_beacon_set_timer(struct mt7603_dev *dev, int idx, int intval);
int mt7603_mac_fill_rx(struct mt7603_dev *dev, struct sk_buff *skb);
void mt7603_mac_add_txs(struct mt7603_dev *dev, void *data);
void mt7603_mac_rx_ba_reset(struct mt7603_dev *dev, void *addr, u8 tid);
void mt7603_mac_tx_ba_reset(struct mt7603_dev *dev, int wcid, int tid,
			    int ba_size);
void mt7603_mac_sta_poll(struct mt7603_dev *dev);

void mt7603_pse_client_reset(struct mt7603_dev *dev);

int mt7603_mcu_set_channel(struct mt7603_dev *dev);
int mt7603_mcu_set_eeprom(struct mt7603_dev *dev);
void mt7603_mcu_exit(struct mt7603_dev *dev);

void mt7603_wtbl_init(struct mt7603_dev *dev, int idx, int vif,
		      const u8 *mac_addr);
void mt7603_wtbl_clear(struct mt7603_dev *dev, int idx);
void mt7603_wtbl_update_cap(struct mt7603_dev *dev, struct ieee80211_sta *sta);
void mt7603_wtbl_set_rates(struct mt7603_dev *dev, struct mt7603_sta *sta,
			   struct ieee80211_tx_rate *probe_rate,
			   struct ieee80211_tx_rate *rates);
int mt7603_wtbl_set_key(struct mt7603_dev *dev, int wcid,
			struct ieee80211_key_conf *key);
void mt7603_wtbl_set_ps(struct mt7603_dev *dev, struct mt7603_sta *sta,
			bool enabled);
void mt7603_wtbl_set_smps(struct mt7603_dev *dev, struct mt7603_sta *sta,
			  bool enabled);
void mt7603_filter_tx(struct mt7603_dev *dev, int idx, bool abort);

int mt7603_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);

void mt7603_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e);

void mt7603_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb);
void mt7603_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q);
void mt7603_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps);
int mt7603_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
void mt7603_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
void mt7603_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);

void mt7603_pre_tbtt_tasklet(unsigned long arg);

void mt7603_update_channel(struct mt76_dev *mdev);

void mt7603_edcca_set_strict(struct mt7603_dev *dev, bool val);
void mt7603_cca_stats_reset(struct mt7603_dev *dev);

void mt7603_init_edcca(struct mt7603_dev *dev);
#endif
