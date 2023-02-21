/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2010 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Bartlomiej Zolnierkiewicz

 */

#ifndef RT2800LIB_H
#define RT2800LIB_H

/*
 * Hardware has 255 WCID table entries. First 32 entries are reserved for
 * shared keys. Since parts of the pairwise key table might be shared with
 * the beacon frame buffers 6 & 7 we could only use the first 222 entries.
 */
#define WCID_START	33
#define WCID_END	222
#define STA_IDS_SIZE	(WCID_END - WCID_START + 2)
#define CHAIN_0		0x0
#define CHAIN_1		0x1
#define RF_ALC_NUM	6
#define CHAIN_NUM	2

struct rf_reg_pair {
	u8 bank;
	u8 reg;
	u8 value;
};

/* RT2800 driver data structure */
struct rt2800_drv_data {
	u8 calibration_bw20;
	u8 calibration_bw40;
	s8 rx_calibration_bw20;
	s8 rx_calibration_bw40;
	s8 tx_calibration_bw20;
	s8 tx_calibration_bw40;
	u8 bbp25;
	u8 bbp26;
	u8 txmixer_gain_24g;
	u8 txmixer_gain_5g;
	u8 max_psdu;
	unsigned int tbtt_tick;
	unsigned int ampdu_factor_cnt[4];
	DECLARE_BITMAP(sta_ids, STA_IDS_SIZE);
	struct ieee80211_sta *wcid_to_sta[STA_IDS_SIZE];
};

struct rt2800_ops {
	u32 (*register_read)(struct rt2x00_dev *rt2x00dev,
			      const unsigned int offset);
	u32 (*register_read_lock)(struct rt2x00_dev *rt2x00dev,
				   const unsigned int offset);
	void (*register_write)(struct rt2x00_dev *rt2x00dev,
			       const unsigned int offset, u32 value);
	void (*register_write_lock)(struct rt2x00_dev *rt2x00dev,
				    const unsigned int offset, u32 value);

	void (*register_multiread)(struct rt2x00_dev *rt2x00dev,
				   const unsigned int offset,
				   void *value, const u32 length);
	void (*register_multiwrite)(struct rt2x00_dev *rt2x00dev,
				    const unsigned int offset,
				    const void *value, const u32 length);

	int (*regbusy_read)(struct rt2x00_dev *rt2x00dev,
			    const unsigned int offset,
			    const struct rt2x00_field32 field, u32 *reg);

	int (*read_eeprom)(struct rt2x00_dev *rt2x00dev);
	bool (*hwcrypt_disabled)(struct rt2x00_dev *rt2x00dev);

	int (*drv_write_firmware)(struct rt2x00_dev *rt2x00dev,
				  const u8 *data, const size_t len);
	int (*drv_init_registers)(struct rt2x00_dev *rt2x00dev);
	__le32 *(*drv_get_txwi)(struct queue_entry *entry);
	unsigned int (*drv_get_dma_done)(struct data_queue *queue);
};

static inline u32 rt2800_register_read(struct rt2x00_dev *rt2x00dev,
				       const unsigned int offset)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->register_read(rt2x00dev, offset);
}

static inline u32 rt2800_register_read_lock(struct rt2x00_dev *rt2x00dev,
					    const unsigned int offset)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->register_read_lock(rt2x00dev, offset);
}

static inline void rt2800_register_write(struct rt2x00_dev *rt2x00dev,
					 const unsigned int offset,
					 u32 value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	rt2800ops->register_write(rt2x00dev, offset, value);
}

static inline void rt2800_register_write_lock(struct rt2x00_dev *rt2x00dev,
					      const unsigned int offset,
					      u32 value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	rt2800ops->register_write_lock(rt2x00dev, offset, value);
}

static inline void rt2800_register_multiread(struct rt2x00_dev *rt2x00dev,
					     const unsigned int offset,
					     void *value, const u32 length)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	rt2800ops->register_multiread(rt2x00dev, offset, value, length);
}

static inline void rt2800_register_multiwrite(struct rt2x00_dev *rt2x00dev,
					      const unsigned int offset,
					      const void *value,
					      const u32 length)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	rt2800ops->register_multiwrite(rt2x00dev, offset, value, length);
}

static inline int rt2800_regbusy_read(struct rt2x00_dev *rt2x00dev,
				      const unsigned int offset,
				      const struct rt2x00_field32 field,
				      u32 *reg)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->regbusy_read(rt2x00dev, offset, field, reg);
}

static inline int rt2800_read_eeprom(struct rt2x00_dev *rt2x00dev)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->read_eeprom(rt2x00dev);
}

static inline bool rt2800_hwcrypt_disabled(struct rt2x00_dev *rt2x00dev)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->hwcrypt_disabled(rt2x00dev);
}

static inline int rt2800_drv_write_firmware(struct rt2x00_dev *rt2x00dev,
					    const u8 *data, const size_t len)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->drv_write_firmware(rt2x00dev, data, len);
}

static inline int rt2800_drv_init_registers(struct rt2x00_dev *rt2x00dev)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	return rt2800ops->drv_init_registers(rt2x00dev);
}

static inline __le32 *rt2800_drv_get_txwi(struct queue_entry *entry)
{
	const struct rt2800_ops *rt2800ops = entry->queue->rt2x00dev->ops->drv;

	return rt2800ops->drv_get_txwi(entry);
}

static inline unsigned int rt2800_drv_get_dma_done(struct data_queue *queue)
{
	const struct rt2800_ops *rt2800ops = queue->rt2x00dev->ops->drv;

	return rt2800ops->drv_get_dma_done(queue);
}

void rt2800_mcu_request(struct rt2x00_dev *rt2x00dev,
			const u8 command, const u8 token,
			const u8 arg0, const u8 arg1);

int rt2800_wait_csr_ready(struct rt2x00_dev *rt2x00dev);
int rt2800_wait_wpdma_ready(struct rt2x00_dev *rt2x00dev);

int rt2800_check_firmware(struct rt2x00_dev *rt2x00dev,
			  const u8 *data, const size_t len);
int rt2800_load_firmware(struct rt2x00_dev *rt2x00dev,
			 const u8 *data, const size_t len);

void rt2800_write_tx_data(struct queue_entry *entry,
			  struct txentry_desc *txdesc);
void rt2800_process_rxwi(struct queue_entry *entry, struct rxdone_entry_desc *txdesc);

void rt2800_txdone_entry(struct queue_entry *entry, u32 status, __le32 *txwi,
			 bool match);
void rt2800_txdone(struct rt2x00_dev *rt2x00dev, unsigned int quota);
void rt2800_txdone_nostatus(struct rt2x00_dev *rt2x00dev);
bool rt2800_txstatus_timeout(struct rt2x00_dev *rt2x00dev);
bool rt2800_txstatus_pending(struct rt2x00_dev *rt2x00dev);

void rt2800_watchdog(struct rt2x00_dev *rt2x00dev);

void rt2800_write_beacon(struct queue_entry *entry, struct txentry_desc *txdesc);
void rt2800_clear_beacon(struct queue_entry *entry);

extern const struct rt2x00debug rt2800_rt2x00debug;

int rt2800_rfkill_poll(struct rt2x00_dev *rt2x00dev);
int rt2800_config_shared_key(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00lib_crypto *crypto,
			     struct ieee80211_key_conf *key);
int rt2800_config_pairwise_key(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00lib_crypto *crypto,
			       struct ieee80211_key_conf *key);
int rt2800_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
int rt2800_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);
void rt2800_config_filter(struct rt2x00_dev *rt2x00dev,
			  const unsigned int filter_flags);
void rt2800_config_intf(struct rt2x00_dev *rt2x00dev, struct rt2x00_intf *intf,
			struct rt2x00intf_conf *conf, const unsigned int flags);
void rt2800_config_erp(struct rt2x00_dev *rt2x00dev, struct rt2x00lib_erp *erp,
		       u32 changed);
void rt2800_config_ant(struct rt2x00_dev *rt2x00dev, struct antenna_setup *ant);
void rt2800_config(struct rt2x00_dev *rt2x00dev,
		   struct rt2x00lib_conf *libconf,
		   const unsigned int flags);
void rt2800_link_stats(struct rt2x00_dev *rt2x00dev, struct link_qual *qual);
void rt2800_reset_tuner(struct rt2x00_dev *rt2x00dev, struct link_qual *qual);
void rt2800_link_tuner(struct rt2x00_dev *rt2x00dev, struct link_qual *qual,
		       const u32 count);
void rt2800_gain_calibration(struct rt2x00_dev *rt2x00dev);
void rt2800_vco_calibration(struct rt2x00_dev *rt2x00dev);

int rt2800_enable_radio(struct rt2x00_dev *rt2x00dev);
void rt2800_disable_radio(struct rt2x00_dev *rt2x00dev);

int rt2800_efuse_detect(struct rt2x00_dev *rt2x00dev);
int rt2800_read_eeprom_efuse(struct rt2x00_dev *rt2x00dev);

int rt2800_probe_hw(struct rt2x00_dev *rt2x00dev);

void rt2800_get_key_seq(struct ieee80211_hw *hw,
			struct ieee80211_key_conf *key,
			struct ieee80211_key_seq *seq);
int rt2800_set_rts_threshold(struct ieee80211_hw *hw, u32 value);
int rt2800_conf_tx(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   unsigned int link_id, u16 queue_idx,
		   const struct ieee80211_tx_queue_params *params);
u64 rt2800_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
int rt2800_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_ampdu_params *params);
int rt2800_get_survey(struct ieee80211_hw *hw, int idx,
		      struct survey_info *survey);
void rt2800_disable_wpdma(struct rt2x00_dev *rt2x00dev);

void rt2800_get_txwi_rxwi_size(struct rt2x00_dev *rt2x00dev,
			       unsigned short *txwi_size,
			       unsigned short *rxwi_size);
void rt2800_pre_reset_hw(struct rt2x00_dev *rt2x00dev);

#endif /* RT2800LIB_H */
