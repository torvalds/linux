/*
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2010 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Bartlomiej Zolnierkiewicz

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RT2800LIB_H
#define RT2800LIB_H

struct rt2800_ops {
	void (*register_read)(struct rt2x00_dev *rt2x00dev,
			      const unsigned int offset, u32 *value);
	void (*register_read_lock)(struct rt2x00_dev *rt2x00dev,
				   const unsigned int offset, u32 *value);
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
};

static inline void rt2800_register_read(struct rt2x00_dev *rt2x00dev,
					const unsigned int offset,
					u32 *value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	rt2800ops->register_read(rt2x00dev, offset, value);
}

static inline void rt2800_register_read_lock(struct rt2x00_dev *rt2x00dev,
					     const unsigned int offset,
					     u32 *value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->ops->drv;

	rt2800ops->register_read_lock(rt2x00dev, offset, value);
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

void rt2800_txdone_entry(struct queue_entry *entry, u32 status, __le32* txwi);

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
int rt2800_sta_add(struct rt2x00_dev *rt2x00dev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
int rt2800_sta_remove(struct rt2x00_dev *rt2x00dev, int wcid);
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
		   struct ieee80211_vif *vif, u16 queue_idx,
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

#endif /* RT2800LIB_H */
